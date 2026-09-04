// arc_fs: the directory walk.
//
// The property under test is mostly NOT "it lists files" — it is "it never
// throws and never stops early". The std::filesystem iterator this replaces
// throws on an unreadable directory, and the call site that matters is a
// detached scan thread with no try/catch above it, so the difference between
// these two implementations is the difference between skipping a folder and
// killing the process.
#undef NDEBUG
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "arc/fs/walk.hh"

namespace stdfs = std::filesystem;
using namespace arc::fs;

namespace {

struct Fixture {
    stdfs::path root;

    Fixture() {
        root = stdfs::temp_directory_path() /
               ("arc_fs_walk_test_" + std::to_string(::getpid()));
        stdfs::remove_all(root);
        stdfs::create_directories(root);
    }
    ~Fixture() {
        std::error_code ec;
        // Restore any permission we removed, or remove_all cannot clean up.
        stdfs::permissions(root / "locked",
                           stdfs::perms::owner_all,
                           stdfs::perm_options::add, ec);
        stdfs::remove_all(root, ec);
    }

    void file(const std::string &rel, const std::string &content = "x") {
        stdfs::path p = root / rel;
        stdfs::create_directories(p.parent_path());
        std::ofstream(p) << content;
    }
    void dir(const std::string &rel) { stdfs::create_directories(root / rel); }
};

std::vector<std::string> collectFiles(const std::string &root, WalkOptions opts) {
    std::vector<std::string> out;
    auto r = walk(root, opts, [&](const Entry &e) {
        if (!e.isDir) out.push_back(e.path);
        return WalkAction::Continue;
    });
    assert(r.ok());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

static void testFindsFilesRecursively() {
    Fixture f;
    f.file("a.flac");
    f.file("album/b.flac");
    f.file("album/disc2/c.flac");

    auto files = collectFiles(f.root.u8string(), WalkOptions{});
    assert(files.size() == 3);
    assert(files[0] == (f.root / "a.flac").u8string());
    assert(files[1] == (f.root / "album" / "b.flac").u8string());
    assert(files[2] == (f.root / "album" / "disc2" / "c.flac").u8string());
}

static void testHiddenDirsSkippedByDefault() {
    Fixture f;
    f.file("a.flac");
    f.file(".streamer/library.db");

    // The default. A companion tool's private directory is not content, and
    // walking it as content has produced real bugs in this workspace.
    auto files = collectFiles(f.root.u8string(), WalkOptions{});
    assert(files.size() == 1);
    assert(files[0] == (f.root / "a.flac").u8string());

    // ...and it is a choice, not a law.
    WalkOptions withHidden;
    withHidden.skipHiddenDirs = false;
    auto all = collectFiles(f.root.u8string(), withHidden);
    assert(all.size() == 2);
}

static void testStats() {
    Fixture f;
    f.file("a.flac", "0123456789");

    WalkOptions opts;
    opts.wantFileStats = true;
    int64_t seenSize = -1, seenMtime = -1;
    auto r = walk(f.root.u8string(), opts, [&](const Entry &e) {
        seenSize = e.size;
        seenMtime = e.mtime;
        return WalkAction::Continue;
    });
    assert(r.ok());
    assert(seenSize == 10);
    assert(seenMtime != 0);

    // Turning stats off must actually skip the stat, not merely hide it.
    opts.wantFileStats = false;
    int64_t noSize = -1;
    walk(f.root.u8string(), opts, [&](const Entry &e) {
        noSize = e.size;
        return WalkAction::Continue;
    });
    assert(noSize == 0);

    // The standalone statFile() agrees with the walk.
    int64_t sz = 0, mt = 0;
    assert(statFile((f.root / "a.flac").u8string(), sz, mt));
    assert(sz == 10);
    assert(mt == seenMtime);

    // And a missing file is false with both outputs zeroed, never garbage.
    int64_t gs = 7, gm = 7;
    assert(!statFile((f.root / "nope.flac").u8string(), gs, gm));
    assert(gs == 0 && gm == 0);
}

static void testPruneSubtree() {
    Fixture f;
    f.file("keep/a.flac");
    f.file("skip/b.flac");
    f.file("skip/deeper/c.flac");

    WalkOptions opts;
    opts.reportDirs = true;

    std::vector<std::string> files;
    auto r = walk(f.root.u8string(), opts, [&](const Entry &e) {
        if (e.isDir) {
            if (e.path.rfind("skip") != std::string::npos &&
                stdfs::path(e.path).filename() == "skip")
                return WalkAction::SkipSubtree;
            return WalkAction::Continue;
        }
        files.push_back(e.path);
        return WalkAction::Continue;
    });
    assert(r.ok());
    assert(files.size() == 1);
    assert(files[0] == (f.root / "keep" / "a.flac").u8string());
}

static void testStopIsSuccessNotFailure() {
    Fixture f;
    f.file("a.flac");
    f.file("b.flac");
    f.file("c.flac");

    int seen = 0;
    auto r = walk(f.root.u8string(), WalkOptions{}, [&](const Entry &) {
        ++seen;
        return WalkAction::Stop;
    });
    assert(r.ok());               // ending early is not an error
    assert(r.value().stopped);    // ...and the caller can tell it happened
    assert(seen == 1);
}

static void testUnreadableDirectoryIsSteppedOver() {
    Fixture f;
    f.file("good/a.flac");
    f.file("locked/secret.flac");
    f.file("also_good/b.flac");

    // Take away every permission on one directory. Running as root defeats
    // this, so the assertion below is conditional on the setup having worked.
    std::error_code ec;
    stdfs::permissions(f.root / "locked", stdfs::perms::none,
                       stdfs::perm_options::replace, ec);

    bool reallyLocked = false;
    {
        std::error_code probe;
        stdfs::directory_iterator it(f.root / "locked", probe);
        reallyLocked = static_cast<bool>(probe);
    }

    // The point: this call must RETURN, not throw, and must still have visited
    // the two readable branches.
    auto files = collectFiles(f.root.u8string(), WalkOptions{});

    if (reallyLocked) {
        assert(files.size() == 2);
        assert(files[0] == (f.root / "also_good" / "b.flac").u8string());
        assert(files[1] == (f.root / "good" / "a.flac").u8string());
    } else {
        std::printf("walk_test: running as root, unreadable-dir case skipped\n");
        assert(files.size() == 3);
    }

    stdfs::permissions(f.root / "locked", stdfs::perms::owner_all,
                       stdfs::perm_options::replace, ec);
}

static void testSymlinkLoopTerminates() {
    Fixture f;
    f.file("a/b/c.flac");

    // A symlink pointing back at an ancestor. Followed, this walk never ends.
    std::error_code ec;
    stdfs::create_directory_symlink(f.root, f.root / "a" / "loop", ec);
    if (ec) {
        std::printf("walk_test: symlinks unavailable, loop case skipped\n");
        return;
    }

    // Not followed by default — so this returns, which is the whole assertion.
    auto files = collectFiles(f.root.u8string(), WalkOptions{});
    assert(files.size() == 1);
    assert(files[0] == (f.root / "a" / "b" / "c.flac").u8string());
}

static void testMissingRootIsAnError() {
    // A root that does not exist is the one failure reported as an error,
    // because it means the walk never started. Everything that goes wrong
    // once it IS running is counted in WalkStats and stepped over.
    auto r = walk("/definitely/not/a/real/directory/arc_fs",
                  [](const Entry &) { return WalkAction::Continue; });
    assert(!r.ok());
    assert(r.error().kind == arc::ErrorKind::Io);

    // A file is not a directory.
    Fixture f;
    f.file("a.flac");
    auto r2 = walk((f.root / "a.flac").u8string(),
                   [](const Entry &) { return WalkAction::Continue; });
    assert(!r2.ok());
}

static void testEmptyDirectory() {
    Fixture f;
    f.dir("empty");
    auto files = collectFiles(f.root.u8string(), WalkOptions{});
    assert(files.empty());
}

int main() {
    testFindsFilesRecursively();
    testHiddenDirsSkippedByDefault();
    testStats();
    testPruneSubtree();
    testStopIsSuccessNotFailure();
    testUnreadableDirectoryIsSteppedOver();
    testSymlinkLoopTerminates();
    testMissingRootIsAnError();
    testEmptyDirectory();
    std::printf("walk_test: all assertions passed\n");
    return 0;
}
