#include "arc/fs/walk.hh"

#include <filesystem>
#include <vector>

namespace stdfs = std::filesystem;

namespace arc {
namespace fs {
namespace {

int64_t ticks(const stdfs::file_time_type &t) {
    return static_cast<int64_t>(t.time_since_epoch().count());
}

} // namespace

bool statFile(const std::string &path, int64_t &outSize, int64_t &outMtime) {
    outSize = 0;
    outMtime = 0;

    std::error_code ec;
    // A directory_entry caches the result of the one stat it does on
    // construction, so the two queries below do not go back to the kernel.
    stdfs::directory_entry de(stdfs::u8path(path), ec);
    if (ec) return false;

    const auto sz = de.file_size(ec);
    if (!ec) outSize = static_cast<int64_t>(sz);

    std::error_code tec;
    const auto mt = de.last_write_time(tec);
    if (!tec) outMtime = ticks(mt);

    return !ec && !tec;
}

Result<WalkStats> walk(const std::string &root, const WalkOptions &opts,
                       const Visitor &visit) {
    WalkStats stats;

    std::error_code ec;
    const stdfs::path rootPath = stdfs::u8path(root);
    if (!stdfs::is_directory(rootPath, ec) || ec) {
        Error e;
        e.kind = ErrorKind::Io;
        e.code = ec.value();
        e.message = root + (ec ? (": " + ec.message()) : ": not a directory");
        return e;
    }

    stdfs::directory_options dirOpts = stdfs::directory_options::skip_permission_denied;
    if (opts.followSymlinks) dirOpts |= stdfs::directory_options::follow_directory_symlink;

    // An explicit stack rather than recursive_directory_iterator, deliberately.
    // That iterator's operator++ throws on an unreadable entry, and its
    // error_code overload gives no portable way to step PAST the entry that
    // failed — so one bad directory either takes the process down or truncates
    // the entire remaining walk. Here a directory that cannot be opened or
    // cannot be advanced costs exactly itself: it is counted in errors, and
    // every other branch of the tree is still visited.
    std::vector<stdfs::path> pending;
    pending.push_back(rootPath);

    while (!pending.empty()) {
        const stdfs::path dir = std::move(pending.back());
        pending.pop_back();

        std::error_code dec;
        stdfs::directory_iterator it(dir, dirOpts, dec);
        if (dec) {
            ++stats.errors;
            continue;
        }

        const stdfs::directory_iterator end;
        for (; it != end; it.increment(dec)) {
            if (dec) {
                // Only this directory's listing is cut short.
                ++stats.errors;
                break;
            }

            const stdfs::directory_entry &de = *it;

            std::error_code eec;
            const bool isLink = de.is_symlink(eec);
            if (eec) {
                ++stats.errors;
                continue;
            }
            if (isLink && !opts.followSymlinks) continue;

            const bool isDir = de.is_directory(eec);
            if (eec) {
                ++stats.errors;
                continue;
            }

            if (isDir) {
                ++stats.dirs;

                const std::string name = de.path().filename().u8string();
                const bool hidden = !name.empty() && name[0] == '.';
                if (opts.skipHiddenDirs && hidden) continue;

                if (opts.reportDirs) {
                    Entry entry;
                    entry.path = de.path().u8string();
                    entry.isDir = true;
                    const WalkAction action = visit(entry);
                    if (action == WalkAction::Stop) {
                        stats.stopped = true;
                        return stats;
                    }
                    if (action == WalkAction::SkipSubtree) continue;
                }

                pending.push_back(de.path());
                continue;
            }

            if (!de.is_regular_file(eec) || eec) {
                if (eec) ++stats.errors;
                continue;
            }

            ++stats.files;

            Entry entry;
            entry.path = de.path().u8string();
            entry.isDir = false;
            if (opts.wantFileStats) {
                // Cached from the directory_entry's own stat — no second syscall.
                std::error_code sec;
                const auto sz = de.file_size(sec);
                if (!sec) entry.size = static_cast<int64_t>(sz);
                std::error_code tec;
                const auto mt = de.last_write_time(tec);
                if (!tec) entry.mtime = ticks(mt);
                if (sec || tec) ++stats.errors;
            }

            if (visit(entry) == WalkAction::Stop) {
                stats.stopped = true;
                return stats;
            }
        }
    }

    return stats;
}

Result<WalkStats> walk(const std::string &root, const Visitor &visit) {
    return walk(root, WalkOptions{}, visit);
}

} // namespace fs
} // namespace arc
