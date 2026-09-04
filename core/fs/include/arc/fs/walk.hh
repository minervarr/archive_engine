#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "arc/result.hh"

namespace arc {
namespace fs {

struct Entry {
    std::string path;
    // Size and modification time of a regular file. Both 0 for a directory,
    // and both 0 when WalkOptions::wantFileStats is false.
    //
    // `mtime` is the raw std::filesystem::file_time_type tick count, NOT a unix
    // second. The representation is implementation-defined and differs between
    // platforms and standard libraries — which is fine and is the point: it is
    // only ever compared against a value the SAME build wrote earlier, as an
    // "did this file change" token. Anything that needs a real timestamp must
    // convert it deliberately rather than assume seconds.
    int64_t size = 0;
    int64_t mtime = 0;
    bool isDir = false;
};

enum class WalkAction {
    Continue,     // keep going
    SkipSubtree,  // (directories only) do not descend into this one
    Stop,         // end the walk successfully, right now
};

struct WalkOptions {
    // Symlinks are NOT followed by default. Following them makes a walk
    // vulnerable to a cycle, and a music library reached through one is
    // indistinguishable from the same library reached directly — so the cost
    // is a duplicate scan and the benefit is nothing.
    bool followSymlinks = false;

    // Skip directories whose name begins with '.'. On by default because that
    // is where companion tools keep their own state (.streamer/, .git/), and
    // walking it as content is always wrong.
    bool skipHiddenDirs = true;

    // Fill Entry::size and Entry::mtime for files. Costs one stat per file —
    // over Android's FUSE mount that is the dominant cost of a scan, so a
    // caller that only wants names should say so.
    bool wantFileStats = true;

    // Report directories to the visitor as well as files. Off by default: most
    // callers want files, and a directory entry they must remember to ignore is
    // a bug waiting to happen. Turn it on to prune with SkipSubtree.
    bool reportDirs = false;
};

struct WalkStats {
    uint64_t dirs = 0;
    uint64_t files = 0;
    // Entries that could not be read (permissions, a race with a delete). The
    // walk continues past them; this is how many it stepped over.
    uint64_t errors = 0;
    bool stopped = false;  // the visitor returned Stop
};

using Visitor = std::function<WalkAction(const Entry &)>;

// ---------------------------------------------------------------------------
// Recursive directory walk that CANNOT THROW.
//
// That is the whole reason this exists rather than each caller reaching for
// std::filesystem directly. The two-argument recursive_directory_iterator
// constructor and its operator++ both throw on an unreadable directory, and the
// usual call site is a detached scan thread with no try/catch anywhere above it
// — so a permission change, a card being pulled, or a folder deleted mid-scan
// takes the whole process down. Every operation here takes the error_code
// overload, counts the failure in WalkStats::errors, and keeps going.
//
// The visitor decides where the walk goes: return SkipSubtree from a directory
// entry to prune it (requires WalkOptions::reportDirs), or Stop to end early
// and successfully. Returning Stop is not an error — WalkStats::stopped says it
// happened.
//
// An error Result means the walk never started: the root does not exist, or is
// not a directory. Anything that goes wrong once it IS running is counted, not
// returned.
// ---------------------------------------------------------------------------
Result<WalkStats> walk(const std::string &root, const WalkOptions &opts,
                       const Visitor &visit);

// Convenience overload with default options.
Result<WalkStats> walk(const std::string &root, const Visitor &visit);

// ---------------------------------------------------------------------------
// Size and modification time for one path, in Entry's terms, with the same
// no-throw guarantee. Both outputs are 0 when the file cannot be stat'ed.
//
// One stat, not two. The obvious spelling — file_size() then last_write_time()
// — is two separate syscalls per file, which doubles the per-file cost of a
// scan on the platform where that cost already hurts.
// ---------------------------------------------------------------------------
bool statFile(const std::string &path, int64_t &outSize, int64_t &outMtime);

} // namespace fs
} // namespace arc
