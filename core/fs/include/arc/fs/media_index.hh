#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "arc/result.hh"

namespace arc {
namespace fs {

// ---------------------------------------------------------------------------
// One audio file as the SYSTEM's own catalogue already knows it.
//
// The point of this type is what it costs. Building the same record by walking
// a directory means one readdir, one stat and one file OPEN per track, to parse
// a header. Asking the platform's media index means one query for the whole
// library, because the system already did that work when the files landed.
//
// The fields split cleanly into what such an index carries and what it does
// not, and the split is not negotiable — it is a property of the index, not a
// decision made here:
//
//   Carried    path, size, mtime, title, artist, albumArtist, album, genre,
//              track, disc, year, durationMs, bitrateBps
//
//   NOT carried  sampleRate, channels, bitDepth
//
// Nothing in Android's MediaStore reports sample rate, channel count or bit
// depth. An app that needs them — anything that distinguishes a 16/44.1 release
// from a 24/96 one — must still open those files. It just no longer has to open
// ALL of them, and it can do it after the library is already on screen.
// ---------------------------------------------------------------------------
struct MediaRecord {
    std::string path;
    int64_t size = 0;
    // Unix seconds. Unlike arc::fs::Entry::mtime, which is an opaque tick count,
    // a system index reports a real timestamp — so this one is comparable
    // across builds and platforms.
    int64_t mtimeUnix = 0;

    std::string title;
    std::string artist;
    std::string albumArtist;
    std::string album;
    std::string genre;

    int trackNumber = 0;
    int discNumber = 0;
    int year = 0;
    int durationMs = 0;
    int bitrateBps = 0;

    // Always 0 here. Named rather than omitted so the consumer's record type can
    // be filled from this one field for field, and so the gap is visible at the
    // point where someone would otherwise assume it was populated.
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;
};

// ---------------------------------------------------------------------------
// The platform's media catalogue, if the platform has one.
//
// core/ builds for a desktop with no SDK present and does no JNI — the rule at
// the top of core/CMakeLists.txt — so the implementation cannot live here. What
// lives here is the SHAPE; the platform layer installs the real thing at
// startup with setMediaIndexBackend(). With none installed, available() is
// false and every caller falls back to walking the tree, which is exactly what
// happens on Linux and Windows.
//
// It is an installed pointer rather than a compile-time #ifdef so that a desktop
// test can install a fake and exercise the dispatch, and so an app can turn the
// fast path off at runtime to compare the two against each other.
// ---------------------------------------------------------------------------
struct MediaIndexBackend {
    virtual ~MediaIndexBackend() = default;

    // Every audio file the index knows of at or below `root`.
    virtual bool query(const std::string &root, std::vector<MediaRecord> &out,
                       Error &err) = 0;

    // A monotonic token that changes whenever the catalogue does, so a rescan
    // can be skipped outright when it has not. 0 means the platform cannot say
    // — the caller then falls back to comparing sizes and mtimes.
    virtual uint64_t generation() = 0;

    // Calls back when the catalogue changes. This is the replacement for a
    // directory watch on Android, and not a stylistic one: inotify on a FUSE
    // mount ADDS THE WATCH SUCCESSFULLY and then never fires, so the failure
    // looks exactly like a library nobody touched.
    virtual bool observe(std::function<void()> onChange, Error &err) = 0;
};

// Installs (or with nullptr, removes) the backend. Not thread-safe by design:
// call it once during startup, before anything queries.
void setMediaIndexBackend(MediaIndexBackend *backend);

class MediaIndex {
public:
    // False when no backend is installed — i.e. always, on desktop. Callers are
    // expected to branch on this and use arc::fs::walk() otherwise; there is no
    // emulated implementation, because a fake fast path that is secretly the
    // slow path is worse than no fast path.
    static bool available();

    static Result<std::vector<MediaRecord>> query(const std::string &root);
    static uint64_t generation();
    static Result<void> observe(std::function<void()> onChange);
};

} // namespace fs
} // namespace arc
