# AGENTS — archive_engine

This is a shared native library, not an app. Consumers pin it as a git submodule
and `add_subdirectory(core)` it; there is no packaging step. The detailed manual
is [`CLAUDE.md`](CLAUDE.md) — read it before changing a module. This file is the
short version that applies to every change.

## Modules

| Module | Target | Default | What it is |
|---|---|---|---|
| `core/util` | `arc_util` | always | `Result<T>`/`Error`, logging, md5, base64, filename sanitisation |
| `core/fs` | `arc_fs` | ON | paths/layout, non-throwing walks, the MediaIndex seam |
| `core/archive` | `arc_archive` | ON | libarchive extract/compress |
| `core/net` | `arc_net` | OFF | libcurl HTTP + segmented downloader |
| `core/tag` | `arc_tag` | OFF | TagLib 2 read/write, cover art |

## Non-negotiables

1. **Namespace `arc::`, targets `arc_*`.** Never reintroduce `ae::`/`ae_*`;
   those belong to `audio_engine`, and two `ae::Result` definitions are a silent
   ODR violation. `AE_IFDIR`/`AE_IFREG` are libarchive's names and stay.
2. **`core/` does no platform work.** No OS SDK includes, no JNI, no
   `#ifdef _WIN32` around UI code. Android facts are pushed in from the platform
   layer through `setPlatformDirs()` and `setMediaIndexBackend()`.
3. **Modules stay independently switchable.** Do not make `arc_fs` require
   libarchive, or `arc_util` require curl. A consumer builds only what it uses.
4. **Directory walks cannot throw.** `walk()` and `listDir()` exist because the
   `std::filesystem` recursive iterator throws on an unreadable directory, and
   the real call site is a scan thread with no handler above it.
5. **`Layout` separates user data from machine data.** `home`/`config`/`data`
   are visible, portable, and on Android sit under FUSE; `state`/`cache` are
   private and fast. A database belongs in `state`.
6. **User directories belong to the user.** Resolve `Music`, `Downloads`,
   `Pictures`, and `Documents` from `user-dirs.dirs`; never invent English
   names or create them as a side effect of opening a database.
7. **Errors are values at module boundaries.** Use `arc::Result<T>` /
   `arc::Error`; do not throw across a module API.
8. **Tests are plain `assert()` binaries.** Debug-only, registered with CTest,
   and a behavior change needs a test that would fail without it.
9. **Commit and push with [`./git_wrapper`](git_wrapper).** It forces the
   `nava` identity, strips `Co-Authored-By`/generated trailers, and pushes
   submodules before their parent. Shared changes land here first.

## Build and test

```bash
cmake -S core -B build_test -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DARC_BUILD_ARCHIVE=OFF -DARC_BUILD_NET=OFF -DARC_BUILD_TAG=OFF \
      -DARC_BUILD_FS_TESTS=ON
cmake --build build_test
ctest --test-dir build_test --output-on-failure
```

`core/archive` is ON by default only for consumers that want it; the standalone
smoke build above turns it off so no configure reaches the network.

## Consumers

```
Klend-ario   -> libs/firstparty/archive_engine          (arc_fs)
Matrix_Player -> framework/archive_engine              (arc_fs)
streamer     -> first_party/KawusapiCC -> engine/archive_engine
                                                    (arc_util, arc_net, arc_tag)
```

Changes here are pushed before the consuming repository advances its pin. If a
consumer needs something different, change the library first and record the
decision there.
