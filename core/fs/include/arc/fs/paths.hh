#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace arc {
namespace fs {

// ---------------------------------------------------------------------------
// Where an application's files live, per platform, under ONE vocabulary.
//
// The split that matters is not "desktop vs phone" — it is USER DATA vs
// MACHINE DATA, and Android is the platform where getting it wrong is
// expensive rather than merely untidy:
//
//   home/config/data   the listener's own files. Visible in a file manager,
//                      editable, portable between devices, and on Android they
//                      SURVIVE UNINSTALL. On Android these sit in shared
//                      storage, which since Android 11 is a FUSE mount: every
//                      open/stat/readdir is a round trip through a userspace
//                      daemon. Fine for music and documents, which are read in
//                      bulk; ruinous for anything chatty.
//
//   state/cache        the machine's own files: databases, indexes, glyph
//                      atlases, logs. Regenerable, uninteresting to a human,
//                      and read and written constantly. On Android these go to
//                      the app's PRIVATE directory, which is real ext4/f2fs
//                      with no FUSE in the way and no permission to ask for.
//
// A SQLite database is the worst thing that can be put in the first group, and
// it is exactly the thing an app is most tempted to put there. Hence the split
// is in the type rather than left to each caller's judgement.
// ---------------------------------------------------------------------------
struct Layout {
    // The user-visible root. $HOME on Linux, %USERPROFILE% on Windows, and on
    // Android a real directory named `home` at the top of shared storage — see
    // the note on androidHome() below for why that is a deliberate invention.
    std::string home;

    // Under `home`, and therefore portable and user-editable.
    std::string config;   // settings a human might reasonably open
    std::string data;     // things the app made that are worth keeping

    // Machine-private and FAST. Never under `home` on Android.
    std::string state;    // databases, indexes — written constantly
    std::string cache;    // regenerable; safe for anything to delete

    // Well-known user directories, for defaults and first-run seeding.
    std::string music;
    std::string downloads;
    std::string pictures;
    std::string documents;
};

// ---------------------------------------------------------------------------
// Facts only the platform layer can know, stated ONCE before any layout() call.
//
// core/ does no JNI — that rule is stated at the top of core/CMakeLists.txt and
// is what lets every module here compile for a desktop with no SDK in sight. So
// the two Android values that genuinely require a JVM are pushed in from
// outside rather than probed:
//
//   privateDir    ANativeActivity::internalDataPath, or getFilesDir(). There is
//                 no way to derive it — it carries the package name and the
//                 user id (/data/user/0/<pkg>/files).
//
//   externalRoot  Environment.getExternalStorageDirectory(). Defaults to
//                 "/storage/emulated/0", which is correct on essentially every
//                 device and is the same fallback the other apps in this
//                 workspace already use. Stating it is still better than
//                 assuming it.
//
// Both are ignored on desktop, where the environment answers both questions.
// Calling this is optional; the defaults are documented and degrade loudly
// rather than silently.
// ---------------------------------------------------------------------------
struct PlatformDirs {
    std::string privateDir;
    std::string externalRoot;
};
void setPlatformDirs(PlatformDirs dirs);

// The values currently in force, defaults included. Mostly for diagnostics —
// an app that logs this once has a much easier time explaining a phone.
const PlatformDirs &platformDirs();

// Resolves the layout for one application. `appName` is the directory-safe
// short name ("matrix_player", "streamer") used for the per-app subdirectories.
//
// PURE with respect to the filesystem: it computes strings and creates nothing.
// Use ensureLayout() when the directories must actually exist.
Layout layout(std::string_view appName);

// Creates every directory in `l` that does not exist yet. Returns false if any
// could not be created, having still attempted the rest — a missing cache
// directory should not stop an app whose state directory is fine.
bool ensureLayout(const Layout &l);

// ---------------------------------------------------------------------------
// Pure path arithmetic. No I/O, nothing is opened or stat'ed, so these are
// assertable without a fixture and behave identically on every platform.
// Separators are normalized to '/' on POSIX and kept as given on Windows only
// where the drive letter requires it; every function below accepts either.
// ---------------------------------------------------------------------------

// Lexically normal, with any trailing separator removed. "/a/b/", "/a/b" and
// "/a/./b" all produce "/a/b", which is what makes a path usable as a cache key.
std::string normalize(std::string_view path);

// Joins with exactly one separator. An absolute `b` replaces `a` entirely,
// matching std::filesystem::path::operator/.
std::string join(std::string_view a, std::string_view b);

// The containing directory, normalized. Returns the input for a root.
std::string parent(std::string_view path);

// The final component ("/a/b/c.flac" -> "c.flac").
std::string filename(std::string_view path);

// True when `ancestor` is `path` itself or one of its parents. Compares whole
// COMPONENTS, so "/music" is not an ancestor of "/musicvideos" — the mistake a
// plain prefix test makes, and one this workspace has shipped.
bool isAncestor(std::string_view ancestor, std::string_view path);

// `path` expressed relative to `base`, or "" when `base` is not an ancestor.
std::string relativeTo(std::string_view base, std::string_view path);

// ---------------------------------------------------------------------------
// The ancestor chain from `dir` up to and including `bound`, then ONE past it,
// nearest first.
//
// This is the "where is the sidecar?" walk, generalised out of Matrix_Player's
// streamerSearchPath(). The problem it solves: a companion file — a downloader's
// database, a manifest, a settings file — sits at the top of ITS OWN tree, and
// where that tree sits inside the user's is the user's filing, not something an
// app may assume. Probing a fixed depth is a guess, and on Android that guess
// was wrong in the one direction nobody looked: the downloader wrote to
// <external>/Music/streamer while the player was pointed at <external>/Music,
// so every artist photo was silently missing on the phone while album sidecars
// kept working and made the result look merely sparse.
//
// The one-past-the-bound entry covers the layout where the app's root is a
// subdirectory of the companion tree (a per-country folder, say).
//
// If `bound` is not an ancestor of `dir` the whole walk is REFUSED — only
// `dir` itself comes back — rather than returning a chain that climbs to the
// filesystem root probing directories the user never mentioned.
//
// An empty `bound` means "no bound": just `dir`.
// ---------------------------------------------------------------------------
std::vector<std::string> ancestorsUpTo(std::string_view dir, std::string_view bound);

} // namespace fs
} // namespace arc
