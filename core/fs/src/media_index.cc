#include "arc/fs/media_index.hh"

namespace arc {
namespace fs {
namespace {

MediaIndexBackend *g_backend = nullptr;

Error noBackend() {
    Error e;
    e.kind = ErrorKind::Io;
    e.message = "no media index backend installed on this platform";
    return e;
}

} // namespace

void setMediaIndexBackend(MediaIndexBackend *backend) {
    g_backend = backend;
}

bool MediaIndex::available() {
    return g_backend != nullptr;
}

Result<std::vector<MediaRecord>> MediaIndex::query(const std::string &root) {
    if (!g_backend) return noBackend();

    std::vector<MediaRecord> out;
    Error err;
    if (!g_backend->query(root, out, err)) return err;
    return out;
}

uint64_t MediaIndex::generation() {
    return g_backend ? g_backend->generation() : 0;
}

Result<void> MediaIndex::observe(std::function<void()> onChange) {
    if (!g_backend) return noBackend();

    Error err;
    if (!g_backend->observe(std::move(onChange), err)) return err;
    return {};
}

} // namespace fs
} // namespace arc
