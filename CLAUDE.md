# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## What this is

`archive_engine` is a set of **reusable native modules** shared by this
workspace's apps across Linux, Windows and Android. It is consumed only as a
git submodule, never cloned side by side, and it is `add_subdirectory`'d rather
than published — there is no packaging step between it and its consumers.

Four modules, each independently switchable:

| Module | Target | What it is | Default |
|---|---|---|---|
| `core/util` | `arc_util` | md5, base64, filename sanitisation, `Result<T>`/`Error`, the logging macros | always |
| `core/fs` | `arc_fs` | **storage**: paths, layout, a walk that cannot throw, the media-index seam | ON |
| `core/archive` | `arc_archive` | libarchive extract/compress | ON |
| `core/net` | `arc_net` | libcurl HTTP + segmented downloader | OFF |
| `core/tag` | `arc_tag` | TagLib 2 read/write, cover art | OFF |

`net` and `tag` are OFF because they need a curl and a TagLib the consumer must
already have in scope on desktop (see each one's `CMakeLists.txt` — they
`FATAL_ERROR` rather than silently fetching a second copy). On Android they
cross-compile their own.

## The namespace is `arc`, and that is not cosmetic

Everything here is `arc::` and every target is `arc_*`. It used to be `ae::` and
`ae_*` — which is exactly what **`audio_engine`** uses (`ae::AlsaSink`,
`ae::TpdfQuantizer`, targets `ae_core`/`ae_usb`/`ae_flac`). Matrix_Player links
both. No exact collision existed at the time of the rename; `ae_core` appearing
on either side would have broken the build, and two `ae::Result` definitions
would have been an ODR violation the linker resolves silently, picking one.

Do not reintroduce the `ae` prefix here. `AE_IFDIR`/`AE_IFREG` in
`core/archive/src/archive.cc` are **libarchive's own** constants and stay.

## `core/` does no JNI, and that rule is load-bearing

Stated at the top of `core/CMakeLists.txt`: platform-agnostic modules only, no
platform SDK includes, no JNI. It is what lets every module here compile for a
desktop with no SDK in sight, and what lets `core/fs`'s tests run without a
device.

Anything that genuinely needs a JVM lives in `platform/android/` and is
**installed into** `core/` at runtime rather than compiled into it:

- `arc::fs::setPlatformDirs()` — the app's private directory, which carries the
  package name and the user id and cannot be derived.
- `arc::fs::setMediaIndexBackend()` — MediaStore, which has no NDK API.

Both are pointers/values pushed in from outside. With neither supplied, `arc_fs`
still works: paths fall back to documented defaults, and `MediaIndex::available()`
is false so callers walk the tree. **That is the desktop's permanent state**, so
the fallback is not an edge case — it is the common path.

## `arc_fs`, and the two things it exists to prevent

### 1. A walk that takes the process down

`arc::fs::walk()` **cannot throw**. `std::filesystem`'s two-argument
`recursive_directory_iterator` throws on an unreadable directory — from the
constructor *and* from `operator++` — and the call site that matters in practice
is a detached scan thread with no `try`/`catch` anywhere above it. A permission
change, a card pulled mid-scan, or a folder deleted underneath it killed the
process.

It uses an **explicit stack**, not the iterator, because the iterator's
`error_code` overload gives no portable way to step *past* the entry that
failed: one bad directory would either abort or truncate the whole remaining
walk. Here it costs exactly itself and is counted in `WalkStats::errors`.

Do not "simplify" this back to `recursive_directory_iterator`.

### 2. A database behind FUSE

`arc::fs::Layout` splits **user data** (`home`/`config`/`data`) from **machine
data** (`state`/`cache`). On Android the first group lives in shared storage,
which since Android 11 is a FUSE mount where every metadata operation is a round
trip through a userspace daemon; the second lives in the app's private
directory, real ext4 with nothing in the way and no permission attached.

A SQLite database is the worst thing that can go in the first group and the
thing an app is most tempted to put there. The split is in the *type* rather
than left to each caller's judgement, which is the only reason it holds.

Android also gets a real `home` directory at the top of shared storage, laid out
like a Linux one (`.config/`, `.local/share/`) so path-building code stops
needing to know the platform. It is visible in a file manager on purpose:
everything under it belongs to the user, survives uninstall, and can be copied
to another device.

### `ancestorsUpTo()`

The nearest-first ancestor chain up to a bound and one past it, refusing to
climb when the bound is not an ancestor. It is the "find a companion tree
without assuming how deep it sits" walk — a downloader's database, a manifest, a
sidecar. It is here rather than in a consumer because the question is not about
music, and because it is pure path arithmetic that opens nothing and can
therefore be asserted exactly.

## `MediaIndex`: what it can and cannot tell you

One `ContentResolver.query()` returns every audio file's path plus title,
artist, albumArtist, album, genre, track, disc, year, duration and bitrate. It
does **not** carry `sampleRate`, `channels` or `bitDepth` — no media index
does. Those fields exist on `MediaRecord`, are always 0, and are named rather
than omitted so the gap is visible at the point where someone would assume
otherwise.

Consequence: an app that distinguishes 16/44.1 from 24/96 still opens files. It
just no longer opens *all* of them.

Three outcomes and they are **not** the same value:

| Outcome | Means | Caller does |
|---|---|---|
| error | query refused (usually: no storage grant yet) | walk |
| ok, empty | the index knows nothing under this root | walk |
| ok, rows | usable | use it |

Collapsing "empty" into "error" (or either into "no music") is how a working
library renders as an empty screen.

`observe()` registers a `ContentObserver`. This replaces a directory watch and
is not a stylistic choice: **inotify on a FUSE mount adds the watch successfully
and then never fires**, so the failure is indistinguishable from a library
nobody touched.

## Java that ships as source

`platform/android/src/main/java/io/nava/archive_engine/` is compiled by the
consumer as an extra source directory, not published as an AAR. `MediaIndex.java`
and `media_index_jni.cpp` must agree on two things by hand:

- `STRIDE_STR` (6) and `STRIDE_NUM` (7), the flat-array layout. The JNI side
  **checks** the array lengths rather than trusting them.
- The method signatures, which the JNI side looks up and verifies at install
  time. A mismatch installs no backend and logs why, rather than crashing.

The class is reached through the **activity's classloader**, never `FindClass()`:
a native thread attached to the JVM gets the system loader, which cannot see
classes packaged in the app, and the failure reads as the class being absent
rather than unreachable.

A consumer that does not want the libarchive wrapper should exclude
`ArchiveEngine.java` — its static block calls
`System.loadLibrary("archive_engine")`, which is not present unless the AAR is.

## Build

```bash
./scripts/linux/build.sh          # smoke-builds core/ with its defaults
                                  # -> build/ (arc_util, arc_fs, arc_archive)
```

Standalone is a compile check, not how consumers use it — they
`add_subdirectory(core)` with their own module flags. `core/net` and `core/tag`
are OFF here because a desktop has no curl/TagLib target in scope.

Two things in `core/archive/CMakeLists.txt` are worth not undoing: the
cross-compile arguments are inside `if(ANDROID)` (passing an empty
`-DCMAKE_TOOLCHAIN_FILE=` is *not* the same as not passing it — CMake reads it
as "a toolchain was requested" and fails), and `CMAKE_POLICY_VERSION_MINIMUM=3.5`
is required because libarchive 3.7.4 still declares
`cmake_minimum_required(2.8.12)`, which CMake 4 refuses.

## Tests

`arc_fs` has three; the rest of the repo has none. Convention matches the
workspace: plain `assert()`, `#undef NDEBUG` in the source, no framework,
Debug-only, run directly.

```bash
cmake -S core -B build_test -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DARC_BUILD_ARCHIVE=OFF -DARC_BUILD_FS_TESTS=ON
cmake --build build_test
./build_test/fs/paths_test        # layout, join/normalize/isAncestor, ancestorsUpTo
./build_test/fs/walk_test         # prune, permission-denied, symlink loop, no-throw
./build_test/fs/media_index_test  # the backend seam, and its three outcomes
```

`walk_test` skips its unreadable-directory case when run as root and says so.

## Consumers

```
streamer -> first_party/KawusapiCC -> engine/archive_engine   (arc_util, arc_net, arc_tag)
Matrix_Player -> framework/archive_engine                     (arc_fs)
```

Two levels of nesting on the streamer side, so a change here needs the pin
advanced in `KawusapiCC` and then in `streamer`. **Push in submodule order** —
this repo, then `KawusapiCC`, then `streamer` — or a fresh recursive clone
points at a gitlink the remote does not have.

## Committing

Use `./git_wrapper`, never plain `git commit`/`git push`. It forces
author/committer to `nava <nava@noreply.com>`, strips `Co-Authored-By:` and
"Generated with" trailers, and pushes submodules before the parent. Do not add
those trailers here.

## Known gaps

- `archive_extract()` treats any non-`ARCHIVE_OK` header return as end of
  archive, so a truncated archive extracts "successfully"
  (`core/archive/src/archive.cc`).
- `archive_compress()` logs and continues when a member file cannot be opened,
  writing a zero-length entry rather than failing.
- `Result<T>::value()` only `assert()`s, so reading an error `Result` is UB
  under `NDEBUG` rather than a throw.
- Every vendored dependency is fetched by URL at configure time with **no
  checksum**.
- The MediaStore backend has **never run on hardware**. It compiles for all
  three ABIs in Matrix_Player and its logic is exercised on the desktop through
  a fake backend (`Matrix_Player/core/tests/scan_source_test.cc`); nothing more
  is claimed.
