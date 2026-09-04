#include "arc/fs/volume.hh"

#include "arc/fs/paths.hh"

namespace arc {
namespace fs {
namespace {

// Desktop has nothing to grant, so the answer starts as true and the Android
// platform layer corrects it. Starting false would make every desktop caller
// depend on a setter that only one platform ever calls.
bool g_allFilesAccess = true;

} // namespace

bool hasAllFilesAccess() {
    return g_allFilesAccess;
}

void setAllFilesAccess(bool granted) {
    g_allFilesAccess = granted;
}

AccessMode accessMode(std::string_view path) {
    if (path.empty()) return AccessMode::None;

#if defined(__ANDROID__)
    const PlatformDirs &dirs = platformDirs();

    // The app's own private directory is the one place with no FUSE and no
    // permission attached to it.
    if (!dirs.privateDir.empty() && isAncestor(dirs.privateDir, path))
        return AccessMode::Direct;

    // Everything under shared storage is behind FUSE, and behind the
    // all-files grant. Without the grant it is not merely slow, it is closed.
    if (!g_allFilesAccess) return AccessMode::None;
    return AccessMode::Fuse;
#else
    (void)path;
    // One filesystem, no broker in the middle. Whether a specific path is
    // readable is a question for the open() that follows, not for this.
    return AccessMode::Direct;
#endif
}

} // namespace fs
} // namespace arc
