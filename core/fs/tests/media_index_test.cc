// arc_fs: the media-index seam.
//
// The real backend is Android's MediaStore and cannot run here. What CAN be
// asserted from a desktop is the part that decides whether an app takes the
// fast path at all — and getting that wrong is not a slow scan, it is a scan
// that silently reports an empty library.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "arc/fs/media_index.hh"

using namespace arc::fs;

namespace {

struct FakeBackend : MediaIndexBackend {
    std::vector<MediaRecord> records;
    uint64_t gen = 42;
    bool failQuery = false;
    int queryCalls = 0;
    bool observing = false;

    bool query(const std::string &root, std::vector<MediaRecord> &out,
               arc::Error &err) override {
        ++queryCalls;
        if (failQuery) {
            err.kind = arc::ErrorKind::Io;
            err.message = "backend said no";
            return false;
        }
        for (const auto &r : records)
            if (r.path.rfind(root, 0) == 0) out.push_back(r);
        return true;
    }
    uint64_t generation() override { return gen; }
    bool observe(std::function<void()> onChange, arc::Error &) override {
        observing = true;
        onChange();
        return true;
    }
};

MediaRecord rec(const std::string &path, const std::string &title) {
    MediaRecord r;
    r.path = path;
    r.title = title;
    r.album = "An Album";
    r.albumArtist = "An Artist";
    r.durationMs = 1000;
    return r;
}

} // namespace

// With no backend, available() is false — the state every desktop build is in,
// permanently. A caller that skipped this check would get an error Result it
// had no reason to expect, and the obvious "handle" for it is an empty list.
static void testUnavailableWithoutBackend() {
    setMediaIndexBackend(nullptr);
    assert(!MediaIndex::available());
    assert(MediaIndex::generation() == 0);

    auto r = MediaIndex::query("/music");
    assert(!r.ok());

    auto o = MediaIndex::observe([] {});
    assert(!o.ok());
}

static void testDispatchesToBackend() {
    FakeBackend fake;
    fake.records.push_back(rec("/music/album/1.flac", "One"));
    fake.records.push_back(rec("/music/album/2.flac", "Two"));
    fake.records.push_back(rec("/elsewhere/3.flac", "Three"));
    setMediaIndexBackend(&fake);

    assert(MediaIndex::available());
    assert(MediaIndex::generation() == 42);

    auto r = MediaIndex::query("/music");
    assert(r.ok());
    assert(fake.queryCalls == 1);
    assert(r.value().size() == 2);
    assert(r.value()[0].title == "One");
    assert(r.value()[1].title == "Two");

    setMediaIndexBackend(nullptr);
}

// A backend failure must arrive as an error, never as an empty result. The
// difference matters: "the index has nothing for this root" is a legitimate
// answer that means fall back to walking, and "the query failed" means the same
// fallback for a different reason — but a caller that cannot tell them apart
// will eventually treat one of them as "the library is empty".
static void testBackendFailureIsAnError() {
    FakeBackend fake;
    fake.failQuery = true;
    setMediaIndexBackend(&fake);

    auto r = MediaIndex::query("/music");
    assert(!r.ok());
    assert(r.error().message == "backend said no");

    setMediaIndexBackend(nullptr);
}

// An empty result is success. This is the case that sends a caller to the walk
// fallback (a root the system scanner never indexed — a .nomedia file, or files
// copied over MTP moments ago), and it must be distinguishable from failure.
static void testEmptyIsSuccess() {
    FakeBackend fake;
    setMediaIndexBackend(&fake);

    auto r = MediaIndex::query("/music");
    assert(r.ok());
    assert(r.value().empty());

    setMediaIndexBackend(nullptr);
}

static void testObserveReachesTheCallback() {
    FakeBackend fake;
    setMediaIndexBackend(&fake);

    bool fired = false;
    auto o = MediaIndex::observe([&] { fired = true; });
    assert(o.ok());
    assert(fake.observing);
    assert(fired);

    setMediaIndexBackend(nullptr);
}

// The gap that forces the two-phase design: no media index anywhere reports
// sample rate, channel count or bit depth. The fields exist on MediaRecord so
// a consumer's own struct can be filled field for field — and they are zero, so
// a consumer that forgets to deepen shows "unknown" rather than a wrong number.
static void testAudioFormatFieldsAreNeverPopulated() {
    MediaRecord r = rec("/music/a.flac", "A");
    assert(r.sampleRate == 0);
    assert(r.channels == 0);
    assert(r.bitDepth == 0);
}

int main() {
    testUnavailableWithoutBackend();
    testDispatchesToBackend();
    testBackendFailureIsAnError();
    testEmptyIsSuccess();
    testObserveReachesTheCallback();
    testAudioFormatFieldsAreNeverPopulated();
    std::printf("media_index_test: all assertions passed\n");
    return 0;
}
