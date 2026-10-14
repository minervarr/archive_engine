#include "arc/fs/paths.hh"
#include "arc/fs/walk.hh"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <vector>

namespace stdfs = std::filesystem;

namespace arc {
namespace fs {
namespace {

unsigned char foldAscii(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<unsigned char>(c - 'A' + 'a');
    return c;
}

bool nameLess(const std::string &a, const std::string &b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const unsigned char fa = foldAscii(static_cast<unsigned char>(a[i]));
        const unsigned char fb = foldAscii(static_cast<unsigned char>(b[i]));
        if (fa != fb) return fa < fb;
    }
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
}

} // namespace

Result<std::vector<Entry>> listDir(const std::string &path) {
    std::error_code ec;
    const stdfs::path dir = stdfs::u8path(path);
    const stdfs::file_status root = stdfs::status(dir, ec);
    if (ec || !stdfs::is_directory(root)) {
        const int code = ec ? ec.value() : ENOTDIR;
        const std::string message = ec ? ec.message() : std::strerror(ENOTDIR);
        return Error{ErrorKind::Io, code, message};
    }

    stdfs::directory_iterator it(dir, ec);
    if (ec) return Error{ErrorKind::Io, ec.value(), ec.message()};

    struct Item {
        Entry entry;
        std::string name;
    };
    std::vector<Item> dirs;
    std::vector<Item> files;
    while (it != stdfs::directory_iterator()) {
        const stdfs::path child = it->path();
        std::string name;
        try {
            name = child.filename().u8string();
        } catch (const stdfs::filesystem_error &) {
            name.clear();
        }
        it.increment(ec);
        if (ec) break;
        if (name.empty() || name == "." || name == ".." || name[0] == '.') continue;

        std::error_code sec;
        const stdfs::file_status st = stdfs::status(child, sec);
        if (sec || !stdfs::exists(st)) continue;

        Entry entry;
        entry.path = normalize(join(path, name));
        entry.isDir = stdfs::is_directory(st);
        if (entry.isDir) dirs.push_back({std::move(entry), std::move(name)});
        else if (stdfs::is_regular_file(st)) files.push_back({std::move(entry), std::move(name)});
    }

    auto byName = [](const Item &a, const Item &b) { return nameLess(a.name, b.name); };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    std::vector<Entry> out;
    out.reserve(dirs.size() + files.size());
    for (Item &item : dirs) out.push_back(std::move(item.entry));
    for (Item &item : files) out.push_back(std::move(item.entry));
    return out;
}

} // namespace fs
} // namespace arc
