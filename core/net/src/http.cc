#include "ae/http.hh"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include <curl/curl.h>

#include "ae/log.hh"

namespace ae {

namespace {

void ensure_curl_global_init() {
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// Process-wide cap on simultaneous transfer connections. Track concurrency and
// per-file segmenting multiply — 8 concurrent tracks at 4 segments each would
// be 32 sockets, a number neither setting implies on its own — so both go
// through this gate.
class ConnectionBudget {
public:
    static ConnectionBudget &instance() {
        static ConnectionBudget budget;
        return budget;
    }

    void acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return in_flight_ < limit_; });
        ++in_flight_;
    }
    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --in_flight_;
        }
        cv_.notify_one();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int in_flight_ = 0;
    int limit_ = 16;
};

struct BudgetGuard {
    BudgetGuard() { ConnectionBudget::instance().acquire(); }
    ~BudgetGuard() { ConnectionBudget::instance().release(); }
    BudgetGuard(const BudgetGuard &) = delete;
    BudgetGuard &operator=(const BudgetGuard &) = delete;
};

} // namespace

// CURLSH share object: lets every per-request easy handle (see EasyRequest
// below) reuse the same DNS cache, TLS session cache, and — the part that
// actually matters for latency — the same pool of open TCP connections.
// Without this, curl_easy_cleanup() at the end of every single call tears
// down that call's connection, so the next call (even a moment later, even
// to the same host) pays a fresh DNS lookup + TCP handshake + TLS handshake
// from scratch. CURLSH itself isn't thread-safe unless the caller supplies
// lock/unlock callbacks (curl's documented requirement), hence the mutex
// array keyed by curl_lock_data — HttpClient is used concurrently (see
// QobuzApiService::search_catalog's 4-way std::async fan-out).
struct HttpClient::SharePimpl {
    CURLSH *share = nullptr;
    std::array<std::mutex, CURL_LOCK_DATA_LAST> locks;

    SharePimpl() {
        ensure_curl_global_init();
        share = curl_share_init();
        if (!share) return;
        curl_share_setopt(share, CURLSHOPT_LOCKFUNC, &SharePimpl::lock_cb);
        curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, &SharePimpl::unlock_cb);
        curl_share_setopt(share, CURLSHOPT_USERDATA, this);
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    }
    ~SharePimpl() {
        if (share) curl_share_cleanup(share);
    }
    SharePimpl(const SharePimpl &) = delete;
    SharePimpl &operator=(const SharePimpl &) = delete;

    static void lock_cb(CURL *, curl_lock_data data, curl_lock_access, void *userp) {
        static_cast<SharePimpl *>(userp)->locks[static_cast<size_t>(data)].lock();
    }
    static void unlock_cb(CURL *, curl_lock_data data, void *userp) {
        static_cast<SharePimpl *>(userp)->locks[static_cast<size_t>(data)].unlock();
    }
};

namespace {

Error curl_error(CURLcode code, const char *ctx) {
    std::string msg = std::string(ctx) + ": " + curl_easy_strerror(code);
    AE_LOGE("%s", msg.c_str());
    ErrorKind kind =
        (code == CURLE_ABORTED_BY_CALLBACK) ? ErrorKind::Canceled : ErrorKind::Network;
    return Error{kind, static_cast<int>(code), std::move(msg)};
}

size_t append_to_string(char *data, size_t size, size_t nmemb, void *userdata) {
    auto *out = static_cast<std::string *>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}

// RAII for the curl easy handle + header list of one request.
struct EasyRequest {
    CURL *curl = nullptr;
    curl_slist *headers = nullptr;

    EasyRequest() {
        ensure_curl_global_init();
        curl = curl_easy_init();
    }
    ~EasyRequest() {
        if (headers) curl_slist_free_all(headers);
        if (curl) curl_easy_cleanup(curl);
    }
    EasyRequest(const EasyRequest &) = delete;
    EasyRequest &operator=(const EasyRequest &) = delete;

    void add_header(const std::string &header) {
        headers = curl_slist_append(headers, header.c_str());
    }
};

struct DownloadState {
    const std::string *path = nullptr;
    CURL *curl = nullptr;
    std::FILE *file = nullptr;
    uint64_t resume_offset = 0;
    bool resumed = false;
    uint64_t bytes_written = 0;
    const std::atomic<bool> *cancel = nullptr;
    const ProgressFn *progress = nullptr;
    Error open_error{};
    bool open_failed = false;
};

// Opens the output file lazily on the first body chunk, once the status is
// known: append after a 206 (server honored the Range), truncate on a 200
// (full re-download even if a resume was requested).
size_t download_write_cb(char *data, size_t size, size_t nmemb, void *userdata) {
    auto *st = static_cast<DownloadState *>(userdata);
    size_t len = size * nmemb;

    if (st->cancel && st->cancel->load(std::memory_order_relaxed)) return 0;

    if (!st->file) {
        long status = 0;
        curl_easy_getinfo(st->curl, CURLINFO_RESPONSE_CODE, &status);
        st->resumed = (status == 206);
        st->file = std::fopen(st->path->c_str(), st->resumed ? "ab" : "wb");
        if (!st->file) {
            st->open_error = Error{ErrorKind::Io, errno,
                                   "failed to open " + *st->path + " for writing"};
            st->open_failed = true;
            return 0;
        }
    }

    if (std::fwrite(data, 1, len, st->file) != len) {
        st->open_error = Error{ErrorKind::Io, errno, "short write to " + *st->path};
        st->open_failed = true;
        return 0;
    }
    st->bytes_written += len;
    return len;
}

int download_xferinfo_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                         curl_off_t, curl_off_t) {
    auto *st = static_cast<DownloadState *>(userdata);
    if (st->cancel && st->cancel->load(std::memory_order_relaxed)) return 1;
    if (st->progress && *st->progress && dlnow > 0) {
        uint64_t base = st->resumed ? st->resume_offset : 0;
        uint64_t total = dltotal > 0 ? base + static_cast<uint64_t>(dltotal) : 0;
        (*st->progress)(base + static_cast<uint64_t>(dlnow), total);
    }
    return 0;
}

// ── Segmented download ──────────────────────────────────────────────────────

constexpr uint32_t SEGMENT_RETRIES = 3;

bool seek64(std::FILE *file, uint64_t offset) {
#ifdef _WIN32
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

// One segment's slice of the file, and where its own writer is up to.
struct Segment {
    uint64_t start = 0;
    uint64_t end   = 0;   // inclusive
    uint64_t done  = 0;   // bytes already written, so a retry resumes mid-slice
};

struct SegmentState {
    Segment *segment = nullptr;
    std::FILE *file = nullptr;
    const std::atomic<bool> *cancel = nullptr;
    std::atomic<bool> *failed = nullptr;
    std::atomic<uint64_t> *downloaded = nullptr;   // shared across segments
    uint64_t reported = 0;                         // this segment's contribution
    bool write_failed = false;
};

bool segment_should_stop(const SegmentState *st) {
    return (st->cancel && st->cancel->load(std::memory_order_relaxed)) ||
           st->failed->load(std::memory_order_relaxed);
}

size_t segment_write_cb(char *data, size_t size, size_t nmemb, void *userdata) {
    auto *st = static_cast<SegmentState *>(userdata);
    size_t len = size * nmemb;
    if (segment_should_stop(st)) return 0;

    if (std::fwrite(data, 1, len, st->file) != len) {
        st->write_failed = true;
        return 0;
    }
    st->segment->done += len;
    st->downloaded->fetch_add(len, std::memory_order_relaxed);
    return len;
}

int segment_xferinfo_cb(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return segment_should_stop(static_cast<SegmentState *>(userdata)) ? 1 : 0;
}

// Reads "Content-Range: bytes 0-0/12345" from a probe response. Returns 0 when
// the header is absent or unparseable, which is the caller's signal that the
// server will not be segmented.
uint64_t parse_content_range_total(const std::string &headers) {
    std::string lower = headers;
    for (char &c : lower) c = static_cast<char>(std::tolower((unsigned char)c));
    size_t at = lower.find("content-range:");
    if (at == std::string::npos) return 0;
    size_t slash = headers.find('/', at);
    if (slash == std::string::npos) return 0;
    uint64_t total = 0;
    for (size_t i = slash + 1; i < headers.size(); ++i) {
        char c = headers[i];
        if (c < '0' || c > '9') break;
        total = total * 10 + static_cast<uint64_t>(c - '0');
    }
    return total;
}

} // namespace

HttpClient::HttpClient(Options options)
    : options_(std::move(options)), share_(std::make_unique<SharePimpl>()) {
    ensure_curl_global_init();
}

HttpClient::~HttpClient() = default;
HttpClient::HttpClient(HttpClient &&) noexcept = default;
HttpClient &HttpClient::operator=(HttpClient &&) noexcept = default;

std::string HttpClient::url_encode(std::string_view text) {
    ensure_curl_global_init();
    CURL *curl = curl_easy_init();
    if (!curl) return std::string(text);
    char *escaped = curl_easy_escape(curl, text.data(), static_cast<int>(text.size()));
    std::string out = escaped ? escaped : std::string(text);
    if (escaped) curl_free(escaped);
    curl_easy_cleanup(curl);
    return out;
}

namespace {

std::string build_url(const std::string &url,
                      const std::vector<std::pair<std::string, std::string>> &query) {
    if (query.empty()) return url;
    std::string out = url;
    out += (url.find('?') == std::string::npos) ? '?' : '&';
    bool first = true;
    for (const auto &[key, value] : query) {
        if (!first) out += '&';
        first = false;
        out += HttpClient::url_encode(key);
        out += '=';
        out += HttpClient::url_encode(value);
    }
    return out;
}

void apply_common_options(EasyRequest &req, const HttpClient::Options &options,
                          CURLSH *share, const std::string &url,
                          const std::vector<std::string> &extra_headers) {
    curl_easy_setopt(req.curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(req.curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(req.curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(req.curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(req.curl, CURLOPT_CONNECTTIMEOUT_MS, options.connect_timeout_ms);
    if (share) curl_easy_setopt(req.curl, CURLOPT_SHARE, share);
    if (!options.user_agent.empty()) {
        curl_easy_setopt(req.curl, CURLOPT_USERAGENT, options.user_agent.c_str());
    }
    if (!options.ca_bundle_path.empty()) {
        curl_easy_setopt(req.curl, CURLOPT_CAINFO, options.ca_bundle_path.c_str());
    }
    for (const auto &header : options.default_headers) req.add_header(header);
    for (const auto &header : extra_headers) req.add_header(header);
    if (req.headers) curl_easy_setopt(req.curl, CURLOPT_HTTPHEADER, req.headers);
}

} // namespace

Result<HttpResponse> HttpClient::get(
    const std::string &url,
    const std::vector<std::pair<std::string, std::string>> &query,
    const std::vector<std::string> &extra_headers) const {
    EasyRequest req;
    if (!req.curl) return Error{ErrorKind::Network, 0, "curl_easy_init failed"};

    std::string full_url = build_url(url, query);
    apply_common_options(req, options_, share_->share, full_url, extra_headers);

    HttpResponse response;
    curl_easy_setopt(req.curl, CURLOPT_WRITEFUNCTION, append_to_string);
    curl_easy_setopt(req.curl, CURLOPT_WRITEDATA, &response.body);

    CURLcode code = curl_easy_perform(req.curl);
    if (code != CURLE_OK) return curl_error(code, "GET failed");

    curl_easy_getinfo(req.curl, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

Result<HttpResponse> HttpClient::post_form(
    const std::string &url,
    const std::vector<std::pair<std::string, std::string>> &form,
    const std::vector<std::string> &extra_headers) const {
    EasyRequest req;
    if (!req.curl) return Error{ErrorKind::Network, 0, "curl_easy_init failed"};

    apply_common_options(req, options_, share_->share, url, extra_headers);

    std::string body;
    bool first = true;
    for (const auto &[key, value] : form) {
        if (!first) body += '&';
        first = false;
        body += url_encode(key);
        body += '=';
        body += url_encode(value);
    }
    curl_easy_setopt(req.curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(req.curl, CURLOPT_COPYPOSTFIELDS, body.c_str());

    HttpResponse response;
    curl_easy_setopt(req.curl, CURLOPT_WRITEFUNCTION, append_to_string);
    curl_easy_setopt(req.curl, CURLOPT_WRITEDATA, &response.body);

    CURLcode code = curl_easy_perform(req.curl);
    if (code != CURLE_OK) return curl_error(code, "POST failed");

    curl_easy_getinfo(req.curl, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

namespace {

// Single ranged GET for one segment, resuming from whatever it already wrote.
// Returns CURLE_OK once the slice is complete.
CURLcode fetch_segment(const HttpClient::Options &options, CURLSH *share,
                       const std::string &url, SegmentState &state) {
    BudgetGuard budget;

    EasyRequest req;
    if (!req.curl) return CURLE_FAILED_INIT;
    apply_common_options(req, options, share, url, {});

    // Byte ranges are meaningless against a re-encoded body, and a proxy that
    // decompresses would desynchronise every offset.
    curl_easy_setopt(req.curl, CURLOPT_ACCEPT_ENCODING, nullptr);

    std::string range = std::to_string(state.segment->start + state.segment->done) + "-" +
                        std::to_string(state.segment->end);
    curl_easy_setopt(req.curl, CURLOPT_RANGE, range.c_str());
    curl_easy_setopt(req.curl, CURLOPT_WRITEFUNCTION, segment_write_cb);
    curl_easy_setopt(req.curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(req.curl, CURLOPT_XFERINFOFUNCTION, segment_xferinfo_cb);
    curl_easy_setopt(req.curl, CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(req.curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(req.curl, CURLOPT_FAILONERROR, 1L);

    return curl_easy_perform(req.curl);
}

} // namespace

// Probe with "Range: 0-0": one request establishes both that the server honours
// ranges (206) and the total size (from Content-Range). Returns 0 when the file
// must not be segmented.
uint64_t HttpClient::probe_ranged_size(const std::string &url) const {
    EasyRequest req;
    if (!req.curl) return 0;
    apply_common_options(req, options_, share_->share, url, {});
    curl_easy_setopt(req.curl, CURLOPT_ACCEPT_ENCODING, nullptr);
    curl_easy_setopt(req.curl, CURLOPT_RANGE, "0-0");

    std::string headers, body;
    curl_easy_setopt(req.curl, CURLOPT_HEADERFUNCTION, append_to_string);
    curl_easy_setopt(req.curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(req.curl, CURLOPT_WRITEFUNCTION, append_to_string);
    curl_easy_setopt(req.curl, CURLOPT_WRITEDATA, &body);

    if (curl_easy_perform(req.curl) != CURLE_OK) return 0;

    long status = 0;
    curl_easy_getinfo(req.curl, CURLINFO_RESPONSE_CODE, &status);
    if (status != 206) return 0;   // 200 means the range was ignored
    return parse_content_range_total(headers);
}

// Fetches [0, total) over `count` concurrent ranges into "<path>.part", then
// renames it into place. The .part staging is what keeps a failed attempt from
// leaving a full-size file at the real path, which detect_partial_file() would
// read as a finished download.
Result<DownloadOutcome> HttpClient::download_segmented(
    const std::string &url, const std::string &path, uint64_t total, int count,
    const ProgressFn &progress, const std::atomic<bool> *cancel) const {
    namespace fs = std::filesystem;
    std::string part = path + ".part";
    std::error_code ec;
    fs::remove(fs::u8path(part), ec);

    {
        std::FILE *create = std::fopen(part.c_str(), "wb");
        if (!create) {
            return Error{ErrorKind::Io, errno, "failed to create " + part};
        }
        std::fclose(create);
    }
    fs::resize_file(fs::u8path(part), total, ec);
    if (ec) {
        fs::remove(fs::u8path(part), ec);
        return Error{ErrorKind::Io, 0, "cannot preallocate " + part + ": " + ec.message()};
    }

    std::vector<Segment> segments(static_cast<size_t>(count));
    uint64_t chunk = total / static_cast<uint64_t>(count);
    for (int i = 0; i < count; ++i) {
        segments[static_cast<size_t>(i)].start = chunk * static_cast<uint64_t>(i);
        segments[static_cast<size_t>(i)].end =
            (i == count - 1) ? total - 1 : chunk * static_cast<uint64_t>(i + 1) - 1;
    }

    std::atomic<uint64_t> downloaded{0};
    std::atomic<bool> failed{false};
    std::mutex error_mutex;
    Error first_error{};
    bool have_error = false;

    auto record_error = [&](Error e) {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (!have_error) {
            first_error = std::move(e);
            have_error = true;
        }
        failed.store(true, std::memory_order_relaxed);
    };

    // A progress pump on the calling thread would need its own thread; instead
    // each segment's write callback bumps the shared counter and the segment
    // that happens to be writing reports the aggregate.
    ProgressFn aggregate;
    if (progress) {
        aggregate = [&](uint64_t, uint64_t) {
            progress(downloaded.load(std::memory_order_relaxed), total);
        };
    }

    std::vector<std::thread> workers;
    workers.reserve(segments.size());
    for (auto &segment : segments) {
        workers.emplace_back([&, seg = &segment] {
            std::FILE *file = std::fopen(part.c_str(), "r+b");
            if (!file) {
                record_error(Error{ErrorKind::Io, errno, "failed to open " + part});
                return;
            }

            SegmentState state;
            state.segment = seg;
            state.file = file;
            state.cancel = cancel;
            state.failed = &failed;
            state.downloaded = &downloaded;

            CURLcode code = CURLE_OK;
            for (uint32_t attempt = 0; attempt <= SEGMENT_RETRIES; ++attempt) {
                if (segment_should_stop(&state)) break;
                if (!seek64(file, seg->start + seg->done)) {
                    record_error(Error{ErrorKind::Io, errno, "seek failed in " + part});
                    break;
                }
                state.write_failed = false;
                code = fetch_segment(options_, share_->share, url, state);

                if (state.write_failed) {
                    record_error(Error{ErrorKind::Io, errno, "short write to " + part});
                    break;
                }
                if (code == CURLE_OK && seg->done >= (seg->end - seg->start + 1)) break;
                if (segment_should_stop(&state)) break;

                // Retry the remainder of this slice only — a flaky link costs
                // one segment's progress, not the whole file's.
                if (attempt == SEGMENT_RETRIES) {
                    record_error(curl_error(code, "segment download failed"));
                }
            }
            std::fclose(file);

            if (progress && aggregate) aggregate(0, 0);
        });
    }
    for (auto &worker : workers) worker.join();

    bool canceled = cancel && cancel->load(std::memory_order_relaxed);
    uint64_t written = downloaded.load(std::memory_order_relaxed);

    if (canceled || have_error || written != total) {
        fs::remove(fs::u8path(part), ec);
        if (canceled) return Error{ErrorKind::Canceled, 0, "download canceled"};
        if (have_error) return first_error;
        return Error{ErrorKind::Network, 0,
                     "segmented download wrote " + std::to_string(written) + " of " +
                         std::to_string(total) + " bytes"};
    }

    fs::rename(fs::u8path(part), fs::u8path(path), ec);
    if (ec) {
        fs::remove(fs::u8path(part), ec);
        return Error{ErrorKind::Io, 0, "cannot rename " + part + ": " + ec.message()};
    }
    return DownloadOutcome{false, written};
}

Result<DownloadOutcome> HttpClient::download_to_file(
    const std::string &url, const std::string &path, uint64_t resume_offset,
    const ProgressFn &progress, const std::atomic<bool> *cancel) const {
    // Resume keeps the single-stream path: its contract is a Range from an
    // existing partial at the real path, which segmenting cannot honour.
    if (options_.max_segments > 1 && resume_offset == 0) {
        uint64_t total = probe_ranged_size(url);
        if (total >= options_.min_segment_bytes) {
            uint64_t by_size = total / options_.min_segment_bytes;
            int count = static_cast<int>(
                std::min<uint64_t>(by_size, static_cast<uint64_t>(options_.max_segments)));
            if (count > 1) {
                return download_segmented(url, path, total, count, progress, cancel);
            }
        }
    }

    BudgetGuard budget;

    EasyRequest req;
    if (!req.curl) return Error{ErrorKind::Network, 0, "curl_easy_init failed"};

    apply_common_options(req, options_, share_->share, url, {});

    DownloadState state;
    state.path = &path;
    state.curl = req.curl;
    state.resume_offset = resume_offset;
    state.cancel = cancel;
    state.progress = &progress;

    curl_easy_setopt(req.curl, CURLOPT_WRITEFUNCTION, download_write_cb);
    curl_easy_setopt(req.curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(req.curl, CURLOPT_XFERINFOFUNCTION, download_xferinfo_cb);
    curl_easy_setopt(req.curl, CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(req.curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(req.curl, CURLOPT_FAILONERROR, 1L);
    if (resume_offset > 0) {
        std::string range = std::to_string(resume_offset) + "-";
        curl_easy_setopt(req.curl, CURLOPT_RANGE, range.c_str());
    }

    CURLcode code = curl_easy_perform(req.curl);
    if (state.file) std::fclose(state.file);

    if (state.open_failed) return state.open_error;
    if (code == CURLE_ABORTED_BY_CALLBACK ||
        (code == CURLE_WRITE_ERROR && cancel &&
         cancel->load(std::memory_order_relaxed))) {
        return Error{ErrorKind::Canceled, 0, "download canceled"};
    }
    if (code == CURLE_HTTP_RETURNED_ERROR) {
        long status = 0;
        curl_easy_getinfo(req.curl, CURLINFO_RESPONSE_CODE, &status);
        return Error{ErrorKind::Http, static_cast<int>(status),
                     "HTTP error " + std::to_string(status) + " downloading " + url};
    }
    if (code != CURLE_OK) return curl_error(code, "download failed");

    return DownloadOutcome{state.resumed, state.bytes_written};
}

bool is_retryable_network_error(const Error &error) {
    if (error.kind != ErrorKind::Network) return false;
    switch (static_cast<CURLcode>(error.code)) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_PARTIAL_FILE:
        case CURLE_GOT_NOTHING:
        case CURLE_SSL_CONNECT_ERROR:
            return true;
        default:
            return false;
    }
}

} // namespace ae
