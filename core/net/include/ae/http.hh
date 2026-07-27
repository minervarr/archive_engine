#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ae/result.hh"

namespace ae {

struct HttpResponse {
    long status = 0;
    std::string body;
};

// Result of a streaming download. `resumed` is true only when the server
// honored the requested Range with a 206; on a 200 the file was rewritten
// from scratch.
struct DownloadOutcome {
    bool resumed = false;
    uint64_t bytes_written = 0;
};

// Called periodically during a download. `downloaded` and `total` are
// absolute file sizes (resume offset included); `total` is 0 when unknown.
using ProgressFn = std::function<void(uint64_t downloaded, uint64_t total)>;

// Thin synchronous wrapper over libcurl. One instance is safe to share
// across threads: each request uses its own curl easy handle, but all
// handles share one CURLSH (DNS cache + TLS session cache + connection
// pool, set up in the .cc), so back-to-back or concurrent requests to the
// same host reuse an existing TCP+TLS connection instead of renegotiating
// one every call. Without this, every single request pays a fresh DNS
// lookup + TCP handshake + TLS handshake, which is the dominant cost for a
// small JSON API response and compounds badly under the concurrent bursts
// QobuzApiService::search_catalog and the download worker pool issue.
//
// Transport failures are returned as errors; HTTP error statuses (4xx/5xx)
// are returned as a normal HttpResponse for get/post_form so callers can
// inspect status and body. download_to_file treats >=400 as an error.
class HttpClient {
public:
    struct Options {
        std::string user_agent;
        // Headers attached to every request, in "Name: value" form.
        std::vector<std::string> default_headers;
        // PEM CA bundle for TLS verification. Required on Android, where
        // libcurl cannot see the system trust store.
        std::string ca_bundle_path;
        long connect_timeout_ms = 10000;

        // Byte-range connections download_to_file may open per file. CDNs
        // commonly throttle per connection rather than per client — Qobuz's
        // serves a cold file at ~650 KB/s on one connection but ~3.4 MB/s
        // across four ranges of that same file — so splitting a large file is
        // worth roughly 5x. 1 disables segmenting entirely.
        int max_segments = 1;
        // Files smaller than this are fetched in one piece: the extra probe
        // and connection setup costs more than the parallelism buys.
        uint64_t min_segment_bytes = 4ull * 1024 * 1024;
    };

    explicit HttpClient(Options options);
    ~HttpClient();
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    Result<HttpResponse> get(
        const std::string &url,
        const std::vector<std::pair<std::string, std::string>> &query = {},
        const std::vector<std::string> &extra_headers = {}) const;

    Result<HttpResponse> post_form(
        const std::string &url,
        const std::vector<std::pair<std::string, std::string>> &form,
        const std::vector<std::string> &extra_headers = {}) const;

    // Streams `url` into `path`. With resume_offset > 0 a Range request is
    // sent; the file is appended on 206 and truncated/rewritten on 200.
    // `progress` and `cancel` may be null. Cancellation yields
    // ErrorKind::Canceled; HTTP >= 400 yields ErrorKind::Http.
    //
    // When Options::max_segments > 1, the server supports ranges, the size is
    // known and large enough, and no resume was requested, the body is fetched
    // over several range connections at once into "<path>.part" and renamed
    // into place only once every segment is complete. Everything else takes
    // the single-stream path unchanged.
    Result<DownloadOutcome> download_to_file(
        const std::string &url, const std::string &path,
        uint64_t resume_offset = 0, const ProgressFn &progress = nullptr,
        const std::atomic<bool> *cancel = nullptr) const;

    static std::string url_encode(std::string_view text);

private:
    // Returns the total size when the server honours ranges, 0 when the body
    // must be fetched in one piece.
    uint64_t probe_ranged_size(const std::string &url) const;
    Result<DownloadOutcome> download_segmented(const std::string &url,
                                               const std::string &path, uint64_t total,
                                               int count, const ProgressFn &progress,
                                               const std::atomic<bool> *cancel) const;

    Options options_;

    // Opaque holder for the CURLSH share object + its lock mutexes — kept
    // out of this header so libcurl types never leak into consumers.
    struct SharePimpl;
    std::unique_ptr<SharePimpl> share_;
};

// True for transient transport failures worth retrying (timeouts,
// connection resets, etc.).
bool is_retryable_network_error(const Error &error);

} // namespace ae
