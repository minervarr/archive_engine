#pragma once

#include <string>
#include <string_view>

namespace arc {
namespace fs {

// How expensive it is to touch a path, and whether it can be touched at all.
//
// This exists so a caller can CHOOSE a strategy instead of guessing at one. The
// difference is not academic: on Android since 11, shared storage
// (/storage/emulated/0) is a FUSE mount, so every open, stat and readdir is a
// round trip through a userspace daemon. Android 12's FUSE passthrough and the
// later fuse-bpf work speed up read and write on an ALREADY-OPEN descriptor;
// they do nothing for the metadata operations a directory walk is made of. The
// app's own private directory is ordinary ext4/f2fs underneath, with none of
// that in the way.
//
// So: bulk sequential reads are fine anywhere. Anything chatty — a database, an
// index, thousands of stats — belongs where accessMode() says Direct.
enum class AccessMode {
    Direct,  // ordinary filesystem; syscalls cost what they normally cost
    Fuse,    // reachable, but every metadata operation is expensive
    None,    // not reachable at all (no permission, or not mounted)
};

AccessMode accessMode(std::string_view path);

// Whether this process can read the user's own files outside its private
// directory. Always true on desktop. On Android it is the
// MANAGE_EXTERNAL_STORAGE ("All files access") grant, which core/ cannot query
// — that needs Environment.isExternalStorageManager() and therefore a JVM — so
// the platform layer states it.
bool hasAllFilesAccess();
void setAllFilesAccess(bool granted);

} // namespace fs
} // namespace arc
