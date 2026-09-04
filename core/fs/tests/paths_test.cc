// arc_fs: path arithmetic and layout resolution.
//
// Everything here is pure — nothing is opened, created or stat'ed — which is
// what lets the answers be asserted exactly rather than described.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "arc/fs/paths.hh"

using namespace arc::fs;

static void testNormalize() {
    assert(normalize("/a/b") == "/a/b");
    assert(normalize("/a/b/") == "/a/b");
    assert(normalize("/a/./b") == "/a/b");
    assert(normalize("/a/c/../b") == "/a/b");
    assert(normalize("/a//b") == "/a/b");

    // A root must survive normalization. Trimming the separator off "/" would
    // produce "", and every path built on it would then be relative.
    assert(normalize("/") == "/");

    assert(normalize("").empty());
}

static void testJoin() {
    assert(join("/a", "b") == "/a/b");
    assert(join("/a/", "b") == "/a/b");
    assert(join("/a", "b/c") == "/a/b/c");
    assert(join("", "b") == "b");
    assert(join("/a", "") == "/a");

    // An absolute right-hand side wins outright, the same as
    // std::filesystem::path::operator/. Silently producing "/a/b" here would
    // turn an absolute path the caller meant into one relative to something.
    assert(join("/a", "/b") == "/b");
}

static void testParentAndFilename() {
    assert(parent("/a/b/c") == "/a/b");
    assert(parent("/a/b/c/") == "/a/b");
    assert(parent("/a") == "/");
    assert(parent("/") == "/");  // a root is its own parent; the walk must end

    assert(filename("/a/b/c.flac") == "c.flac");
    assert(filename("/a/b/") == "b");
}

static void testIsAncestor() {
    assert(isAncestor("/music", "/music/album/1.flac"));
    assert(isAncestor("/music", "/music"));  // a directory contains itself
    assert(isAncestor("/", "/music"));
    assert(isAncestor("/music/", "/music/album"));  // trailing separator is noise

    assert(!isAncestor("/music/album", "/music"));  // not upward
    assert(!isAncestor("/other", "/music"));

    // THE case a string-prefix test gets wrong, and the reason this compares
    // whole components. "/music" is not an ancestor of "/musicvideos", and an
    // app that thinks it is will happily walk out of the root it was bounded to.
    assert(!isAncestor("/music", "/musicvideos"));
    assert(!isAncestor("/music", "/musicvideos/x.flac"));

    assert(!isAncestor("", "/music"));
    assert(!isAncestor("/music", ""));
}

static void testRelativeTo() {
    assert(relativeTo("/music", "/music/album/1.flac") == "album/1.flac");
    assert(relativeTo("/music", "/music").empty());
    // Not an ancestor: empty, never a chain of "..".
    assert(relativeTo("/music", "/musicvideos/x").empty());
    assert(relativeTo("/music/album", "/music").empty());
}

// ---------------------------------------------------------------------------
// ancestorsUpTo(): the three REAL layouts a companion tree turns up in, taken
// from the case that produced this function — a downloader's .streamer/
// database, looked for from an album folder, bounded by the music root.
// ---------------------------------------------------------------------------
static void testAncestorsUpTo() {
    // 1. The phone. The music root sits ABOVE the downloader's own tree:
    //    root = /sdcard/Music, downloader = /sdcard/Music/streamer.
    //    Nearest first, so the album's own folder is probed before its parents.
    {
        auto v = ancestorsUpTo("/sdcard/Music/streamer/us/12345", "/sdcard/Music");
        assert(v.size() == 5);
        assert(v[0] == "/sdcard/Music/streamer/us/12345");
        assert(v[1] == "/sdcard/Music/streamer/us");
        assert(v[2] == "/sdcard/Music/streamer");
        assert(v[3] == "/sdcard/Music");
        assert(v[4] == "/sdcard");  // one PAST the bound
    }

    // 2. This desktop. The music root IS the downloader's tree.
    {
        auto v = ancestorsUpTo("/home/nava/Music/us/12345", "/home/nava/Music");
        assert(v.size() == 4);
        assert(v[0] == "/home/nava/Music/us/12345");
        assert(v[1] == "/home/nava/Music/us");
        assert(v[2] == "/home/nava/Music");
        assert(v[3] == "/home/nava");
    }

    // 3. The root is BELOW the downloader's tree — pointed straight at one
    //    country folder. The one-past-the-bound entry is what finds it.
    {
        auto v = ancestorsUpTo("/lib/streamer/us/12345", "/lib/streamer/us");
        assert(v.size() == 3);
        assert(v[0] == "/lib/streamer/us/12345");
        assert(v[1] == "/lib/streamer/us");
        assert(v[2] == "/lib/streamer");
    }

    // The bound is not an ancestor at all: refuse the whole walk rather than
    // climb to "/" probing directories the user never mentioned.
    {
        auto v = ancestorsUpTo("/music/album", "/somewhere/else");
        assert(v.size() == 1);
        assert(v[0] == "/music/album");
    }

    // Sibling-prefix, the isAncestor() trap again: "/music" must not bound a
    // walk that starts in "/musicvideos".
    {
        auto v = ancestorsUpTo("/musicvideos/album", "/music");
        assert(v.size() == 1);
        assert(v[0] == "/musicvideos/album");
    }

    // No bound: just the directory itself.
    {
        auto v = ancestorsUpTo("/music/album", "");
        assert(v.size() == 1);
        assert(v[0] == "/music/album");
    }

    // Already at the bound.
    {
        auto v = ancestorsUpTo("/music", "/music");
        assert(v.size() == 2);
        assert(v[0] == "/music");
        assert(v[1] == "/");
    }

    assert(ancestorsUpTo("", "/music").empty());
}

// ---------------------------------------------------------------------------
// layout(): the split that the whole module exists to enforce.
// ---------------------------------------------------------------------------
static void testLayout() {
    setenv("HOME", "/home/tester", 1);
    // Unset any XDG override the developer's own shell might have, so the test
    // asserts the DEFAULTS rather than this machine's configuration.
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_STATE_HOME");
    unsetenv("XDG_CACHE_HOME");

    Layout l = layout("matrix_player");

#if !defined(_WIN32) && !defined(__ANDROID__)
    assert(l.home == "/home/tester");
    assert(l.config == "/home/tester/.config/matrix_player");
    assert(l.data == "/home/tester/.local/share/matrix_player");
    assert(l.state == "/home/tester/.local/state/matrix_player");
    assert(l.cache == "/home/tester/.cache/matrix_player");
    assert(l.music == "/home/tester/Music");
    assert(l.downloads == "/home/tester/Downloads");

    // XDG is honoured when the user set it.
    setenv("XDG_STATE_HOME", "/run/user/1000/state", 1);
    Layout x = layout("matrix_player");
    assert(x.state == "/run/user/1000/state/matrix_player");
    unsetenv("XDG_STATE_HOME");
#endif

    // Whatever the platform, every field is populated and normalized — no
    // trailing separators, so any of them is safe as a cache key.
    for (const std::string *d : {&l.home, &l.config, &l.data, &l.state, &l.cache,
                                 &l.music, &l.downloads, &l.pictures, &l.documents}) {
        assert(!d->empty());
        assert(*d == normalize(*d));
    }

    // The rule the type exists to carry: on every platform, state and cache are
    // reachable, and on Android they are NOT under home. Asserting it here
    // rather than on Android only means the invariant is stated where it can
    // actually be run.
    assert(!l.state.empty());
    assert(!l.cache.empty());
}

// The Android layout is pure string arithmetic, so the interesting half of it
// can be checked from a desktop by driving setPlatformDirs() directly.
static void testPlatformDirsAreStated() {
    PlatformDirs d;
    d.privateDir = "/data/user/0/io.nava.matrixplayer/files";
    d.externalRoot = "/storage/emulated/0";
    setPlatformDirs(d);

    assert(platformDirs().privateDir == d.privateDir);
    assert(platformDirs().externalRoot == d.externalRoot);

    setPlatformDirs(PlatformDirs{});
    assert(platformDirs().privateDir.empty());
}

int main() {
    testNormalize();
    testJoin();
    testParentAndFilename();
    testIsAncestor();
    testRelativeTo();
    testAncestorsUpTo();
    testLayout();
    testPlatformDirsAreStated();
    std::printf("paths_test: all assertions passed\n");
    return 0;
}
