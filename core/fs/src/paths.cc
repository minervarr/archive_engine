#include "arc/fs/paths.hh"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "arc/log.hh"

#ifdef _WIN32
#include <windows.h>
#endif

namespace stdfs = std::filesystem;

namespace arc {
namespace fs {
namespace {

PlatformDirs g_dirs;

// The default external root. Correct on effectively every Android device, and
// the same fallback the other apps in this workspace already hardcode. It is
// still overridable through setPlatformDirs(), because "effectively every" is
// not "every".
constexpr const char *kDefaultExternalRoot = "/storage/emulated/0";

// The directory `home` gets at the top of Android's shared storage.
//
// Android's shared root is a fixed set of media buckets — Alarms, Android,
// DCIM, Documents, Download, Movies, Music, Notifications, Pictures, Podcasts,
// Ringtones — with no place for an application to keep anything structured. So
// this makes one, and gives it the name and the interior layout of a Linux home
// directory: the same .config/ and .local/share/ an app already writes to on the
// desktop, at the same relative paths, so path-building code stops needing to
// know which platform it is on.
//
// It is visible in a file manager on purpose. Everything under it belongs to the
// listener: it survives uninstall, it can be copied to another device, and it can
// be edited by hand. Nothing the app needs to be FAST goes here — see the
// comment on Layout.
constexpr const char *kAndroidHomeDirName = "home";

std::string envOr(const char *name, const std::string &fallback) {
#ifdef _WIN32
    // Deliberately the wide API. getenv() hands back the process's ANSI
    // codepage, so a profile directory containing a character outside it comes
    // out mojibake — and then every path built from it points nowhere. This is
    // the same trap the workspace's asset readers document for fopen().
    int wideLen = 0;
    {
        wchar_t wname[256];
        int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 256);
        if (n <= 0) return fallback;
        const wchar_t *w = _wgetenv(wname);
        if (!w || !*w) return fallback;
        wideLen = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (wideLen <= 1) return fallback;
        std::string out(static_cast<size_t>(wideLen - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), wideLen, nullptr, nullptr);
        return out;
    }
#else
    const char *v = std::getenv(name);
    if (!v || !*v) return fallback;
    return std::string(v);
#endif
}

// True for "/" and for a Windows root like "C:/" — the places parent() must
// stop at rather than climb past.
bool isRoot(const stdfs::path &p) {
    return p.empty() || p == p.root_path() || p == p.parent_path();
}

stdfs::path normalizedPath(std::string_view path) {
    if (path.empty()) return {};
    stdfs::path p = stdfs::u8path(std::string(path));
    p = p.lexically_normal();
    // lexically_normal() leaves a trailing separator on a directory-shaped
    // input ("/a/b/" -> "/a/b/"), which would make "/a/b" and "/a/b/" two
    // different cache keys for one directory.
    std::string s = p.u8string();
    while (s.size() > 1) {
        const char c = s.back();
        if (c != '/' && c != '\\') break;
        // Do not eat the separator off a root: "/" and "C:/" are not "" and "C:".
        stdfs::path trimmed = stdfs::u8path(s.substr(0, s.size() - 1));
        if (trimmed.empty() || trimmed == trimmed.root_name()) break;
        s.pop_back();
    }
    return stdfs::u8path(s);
}

} // namespace

void setPlatformDirs(PlatformDirs dirs) {
    g_dirs = std::move(dirs);
}

const PlatformDirs &platformDirs() {
    return g_dirs;
}

// ---------------------------------------------------------------------------
// Pure path arithmetic
// ---------------------------------------------------------------------------

std::string normalize(std::string_view path) {
    return normalizedPath(path).u8string();
}

std::string join(std::string_view a, std::string_view b) {
    if (a.empty()) return normalize(b);
    if (b.empty()) return normalize(a);
    stdfs::path p = stdfs::u8path(std::string(a)) / stdfs::u8path(std::string(b));
    return normalize(p.u8string());
}

std::string parent(std::string_view path) {
    stdfs::path p = normalizedPath(path);
    if (isRoot(p)) return p.u8string();
    return p.parent_path().u8string();
}

std::string filename(std::string_view path) {
    return normalizedPath(path).filename().u8string();
}

bool isAncestor(std::string_view ancestor, std::string_view path) {
    if (ancestor.empty() || path.empty()) return false;
    const stdfs::path a = normalizedPath(ancestor);
    const stdfs::path p = normalizedPath(path);
    if (a == p) return true;

    // Component-wise, never a string prefix. A prefix test reports "/music" as
    // an ancestor of "/musicvideos", which is how a search bounded by a music
    // root silently escapes into a sibling directory.
    auto ai = a.begin();
    auto pi = p.begin();
    for (; ai != a.end(); ++ai, ++pi) {
        if (pi == p.end()) return false;
        if (*ai != *pi) return false;
    }
    return true;
}

std::string relativeTo(std::string_view base, std::string_view path) {
    if (!isAncestor(base, path)) return {};
    const stdfs::path b = normalizedPath(base);
    const stdfs::path p = normalizedPath(path);
    if (b == p) return {};
    return p.lexically_relative(b).generic_u8string();
}

std::vector<std::string> ancestorsUpTo(std::string_view dir, std::string_view bound) {
    std::vector<std::string> out;
    if (dir.empty()) return out;

    const stdfs::path start = normalizedPath(dir);
    out.push_back(start.u8string());
    if (bound.empty()) return out;

    const stdfs::path stop = normalizedPath(bound);

    stdfs::path cur = start;
    bool reached = (cur == stop);
    while (!reached) {
        stdfs::path up = cur.parent_path();
        if (up.empty() || up == cur) {
            // Ran out of path without ever meeting the bound, so the bound is
            // not an ancestor of dir. Refuse the walk rather than hand back a
            // chain climbing to the filesystem root.
            return {start.u8string()};
        }
        cur = up;
        out.push_back(cur.u8string());
        reached = (cur == stop);
    }

    // One PAST the bound: the layout where the app's own root sits inside the
    // companion tree rather than above it.
    stdfs::path above = cur.parent_path();
    if (!above.empty() && above != cur) out.push_back(above.u8string());

    return out;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

Layout layout(std::string_view appName) {
    const std::string app(appName);
    Layout l;

#if defined(__ANDROID__)
    const std::string external =
        g_dirs.externalRoot.empty() ? std::string(kDefaultExternalRoot) : g_dirs.externalRoot;

    l.home      = join(external, kAndroidHomeDirName);
    l.config    = join(join(l.home, ".config"), app);
    l.data      = join(join(l.home, ".local/share"), app);
    l.music     = join(l.home, "Music");
    l.downloads = join(l.home, "Downloads");
    l.pictures  = join(l.home, "Pictures");
    l.documents = join(l.home, "Documents");

    if (g_dirs.privateDir.empty()) {
        // Degrade loudly. Everything still works, just slowly and in the wrong
        // place — which is exactly the failure that is invisible until someone
        // wonders why the database got sluggish.
        ARC_LOGW("fs: no privateDir set - state/cache fall back to shared storage "
                 "(FUSE). Call arc::fs::setPlatformDirs() with internalDataPath.");
        l.state = join(join(l.home, ".local/state"), app);
        l.cache = join(join(l.home, ".cache"), app);
    } else {
        // Already per-application (it carries the package name), so no app
        // subdirectory is added — that would only move an existing database.
        l.state = normalize(g_dirs.privateDir);
        l.cache = join(l.state, "cache");
    }

#elif defined(_WIN32)
    l.home   = envOr("USERPROFILE", ".");
    l.config = join(envOr("APPDATA", join(l.home, "AppData/Roaming")), app);
    l.data   = join(envOr("LOCALAPPDATA", join(l.home, "AppData/Local")), app);
    l.state  = join(l.data, "state");
    l.cache  = join(l.data, "cache");
    l.music     = join(l.home, "Music");
    l.downloads = join(l.home, "Downloads");
    l.pictures  = join(l.home, "Pictures");
    l.documents = join(l.home, "Documents");

#else
    l.home = envOr("HOME", ".");
    // XDG where the user set it, the XDG defaults otherwise. Not a Linux
    // flourish: it is the layout the Android branch above imitates, which is
    // what lets a path-building expression be written once.
    l.config = join(envOr("XDG_CONFIG_HOME", join(l.home, ".config")), app);
    l.data   = join(envOr("XDG_DATA_HOME", join(l.home, ".local/share")), app);
    l.state  = join(envOr("XDG_STATE_HOME", join(l.home, ".local/state")), app);
    l.cache  = join(envOr("XDG_CACHE_HOME", join(l.home, ".cache")), app);
    l.music     = join(l.home, "Music");
    l.downloads = join(l.home, "Downloads");
    l.pictures  = join(l.home, "Pictures");
    l.documents = join(l.home, "Documents");
#endif

    return l;
}

bool ensureLayout(const Layout &l) {
    bool allOk = true;
    // state and cache first: they are the ones an app cannot run without, and
    // on Android they are the ones that do not need a permission. A failure to
    // create a music directory must not stop the database from opening.
    for (const std::string *d : {&l.state, &l.cache, &l.config, &l.data, &l.home,
                                 &l.music, &l.downloads, &l.pictures, &l.documents}) {
        if (d->empty()) continue;
        std::error_code ec;
        stdfs::create_directories(stdfs::u8path(*d), ec);
        if (ec && !stdfs::exists(stdfs::u8path(*d))) {
            ARC_LOGW("fs: cannot create %s (%s)", d->c_str(), ec.message().c_str());
            allOk = false;
        }
    }
    return allOk;
}

} // namespace fs
} // namespace arc
