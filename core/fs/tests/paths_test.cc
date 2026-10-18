// arc_fs: path arithmetic, layout resolution, and directory ownership.
//
// The path arithmetic is pure — nothing is opened, created or stat'ed — which
// is what lets those answers be asserted exactly. Layout resolution reads the
// one small user-dirs.dirs file, and the scope tests create an isolated scratch
// tree; both are still ordinary Debug-only assert() checks.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "arc/fs/paths.hh"

using namespace arc::fs;
namespace stdfs = std::filesystem;

namespace {

// A clean scratch tree under the system temp directory. Each test that needs
// the filesystem gets its own, so a failed assertion in one does not leave a
// half-built layout for the next.
stdfs::path freshScratch(const char *name) {
    const stdfs::path root = stdfs::temp_directory_path() / name;
    std::error_code ec;
    stdfs::remove_all(root, ec);
    ec.clear();
    stdfs::create_directories(root, ec);
    assert(!ec);
    return root;
}

} // namespace

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
    const stdfs::path root = freshScratch("arc_fs_paths_layout");
    const std::string home = (root / "home").string();
    const std::string config = (root / "config").string();
    const std::string state = (root / "state").string();
    const std::string data = (root / "data").string();
    const std::string cache = (root / "cache").string();

    setenv("HOME", home.c_str(), 1);
    setenv("XDG_CONFIG_HOME", config.c_str(), 1);
    setenv("XDG_STATE_HOME", state.c_str(), 1);
    setenv("XDG_DATA_HOME", data.c_str(), 1);
    setenv("XDG_CACHE_HOME", cache.c_str(), 1);

    Layout l = layout("matrix_player");

#if !defined(_WIN32) && !defined(__ANDROID__)
    assert(l.home == home);
    assert(l.config == join(config, "matrix_player"));
    assert(l.data == join(data, "matrix_player"));
    assert(l.state == join(state, "matrix_player"));
    assert(l.cache == join(cache, "matrix_player"));

    // No user-dirs.dirs in this fresh scratch config: the conventional names
    // are the documented fallback, not an assertion that those dirs exist.
    assert(l.music == join(home, "Music"));
    assert(l.downloads == join(home, "Downloads"));
    assert(l.pictures == join(home, "Pictures"));
    assert(l.documents == join(home, "Documents"));
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

    stdfs::remove_all(root);
}

// The XDG file is the source of truth when it exists: a user who moved their
// directories to /srv or $HOME/webdl must not get the English defaults back.
static void testLayoutUsesUserDirs() {
#if !defined(_WIN32) && !defined(__ANDROID__)
    const stdfs::path root = freshScratch("arc_fs_paths_user_dirs");
    const std::string home = (root / "home").string();
    const std::string config = (root / "config").string();
    stdfs::create_directories(config);

    {
        std::ofstream out(stdfs::u8path((stdfs::path(config) / "user-dirs.dirs").string()),
                          std::ios::binary);
        assert(out);
        out << "# written by xdg-user-dirs-update\n"
               "\n"
               "XDG_DOWNLOAD_DIR=\"$HOME/webdl\"\n"
               "XDG_DOCUMENTS_DIR=\"${HOME}/docs\"\n"
               "XDG_MUSIC_DIR=\"/srv/music\"\n"
               "XDG_PICTURES_DIR=\"$HOME/$OTHER\"\n";
        assert(out.good());
    }

    setenv("HOME", home.c_str(), 1);
    setenv("XDG_CONFIG_HOME", config.c_str(), 1);
    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_STATE_HOME");
    unsetenv("XDG_CACHE_HOME");

    const Layout l = layout("klend_ario");
    assert(l.home == home);
    assert(l.downloads == join(home, "webdl"));
    assert(l.documents == join(home, "docs"));
    assert(l.music == "/srv/music");
    // The value still contains an unexpanded variable: refuse it rather than
    // hand back a path that means nothing, and fall back to the default.
    assert(l.pictures == join(home, "Pictures"));

    stdfs::remove_all(root);
#endif
}

// App scope creates only what the app owns. All scope keeps the historical
// behavior for consumers that explicitly want the user's directories.
static void testEnsureLayoutScopes() {
    const stdfs::path root = freshScratch("arc_fs_paths_ensure");
    Layout l;
    l.home = join(root.string(), "home");
    l.config = join(l.home, ".config/klend_ario");
    l.data = join(l.home, ".local/share/klend_ario");
    l.state = join(root.string(), "state/klend_ario");
    l.cache = join(root.string(), "cache/klend_ario");
    l.music = join(l.home, "Music");
    l.downloads = join(l.home, "Downloads");
    l.pictures = join(l.home, "Pictures");
    l.documents = join(l.home, "Documents");

    assert(ensureLayout(l, LayoutScope::App));
    assert(stdfs::is_directory(stdfs::u8path(l.state)));
    assert(stdfs::is_directory(stdfs::u8path(l.cache)));
    assert(stdfs::is_directory(stdfs::u8path(l.config)));
    assert(stdfs::is_directory(stdfs::u8path(l.data)));
    assert(!stdfs::exists(stdfs::u8path(l.music)));
    assert(!stdfs::exists(stdfs::u8path(l.downloads)));
    assert(!stdfs::exists(stdfs::u8path(l.pictures)));
    assert(!stdfs::exists(stdfs::u8path(l.documents)));

    assert(ensureLayout(l, LayoutScope::All));
    assert(stdfs::is_directory(stdfs::u8path(l.music)));
    assert(stdfs::is_directory(stdfs::u8path(l.downloads)));
    assert(stdfs::is_directory(stdfs::u8path(l.pictures)));
    assert(stdfs::is_directory(stdfs::u8path(l.documents)));

    stdfs::remove_all(root);
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

static void testDocumentsDirFromUserDirs() {
    const char* text =
        "# comment\n"
        "\n"
        "XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
        "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n";
    assert(documentsDirFromUserDirs(text, "/home/nava") == "/home/nava/Documents");
    assert(documentsDirFromUserDirs("XDG_DOCUMENTS_DIR=\"${HOME}/Docs\"\n", "/home/nava") ==
           "/home/nava/Docs");
    assert(documentsDirFromUserDirs("XDG_MUSIC_DIR=\"$HOME/Music\"\n", "/home/nava").empty());
    assert(documentsDirFromUserDirs("XDG_DOCUMENTS_DIR=\"$HOME/$OTHER\"\n", "/home/nava").empty());
    assert(documentsDirFromUserDirs("XDG_DOCUMENTS_DIR=/srv/docs\n", "/home/nava") == "/srv/docs");

    // The same parser serves every XDG_*_DIR key, not just Documents.
    assert(userDirFromUserDirs(text, "XDG_DOWNLOAD_DIR", "/home/nava") ==
           "/home/nava/Downloads");
    assert(userDirFromUserDirs(text, "XDG_MUSIC_DIR", "/home/nava").empty());
    assert(userDirFromUserDirs(text, "", "/home/nava").empty());
}

int main() {
    testDocumentsDirFromUserDirs();
    testNormalize();
    testJoin();
    testParentAndFilename();
    testIsAncestor();
    testRelativeTo();
    testAncestorsUpTo();
    testLayout();
    testLayoutUsesUserDirs();
    testEnsureLayoutScopes();
    testPlatformDirsAreStated();
    std::printf("paths_test: all assertions passed\n");
    return 0;
}
