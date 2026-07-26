#include "ae/http.hh"

#include <cstdio>
#include <mutex>

#include <curl/curl.h>

#include "ae/log.hh"

namespace ae {

namespace {

void ensure_curl_global_init() {
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

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

} // namespace

HttpClient::HttpClient(Options options) : options_(std::move(options)) {
    ensure_curl_global_init();
}

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
                          const std::string &url,
                          const std::vector<std::string> &extra_headers) {
    curl_easy_setopt(req.curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(req.curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(req.curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(req.curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(req.curl, CURLOPT_CONNECTTIMEOUT_MS, options.connect_timeout_ms);
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
    apply_common_options(req, options_, full_url, extra_headers);

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

    apply_common_options(req, options_, url, extra_headers);

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

Result<DownloadOutcome> HttpClient::download_to_file(
    const std::string &url, const std::string &path, uint64_t resume_offset,
    const ProgressFn &progress, const std::atomic<bool> *cancel) const {
    EasyRequest req;
    if (!req.curl) return Error{ErrorKind::Network, 0, "curl_easy_init failed"};

    apply_common_options(req, options_, url, {});

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
