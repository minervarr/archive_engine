# archive_engine

Reusable native modules for the minervarr desktop and Android apps: storage and
path layout, directory walks that cannot throw, archive extraction, HTTP
downloads, and audio tag read/write.

The library is consumed as a **git submodule** and added with
`add_subdirectory(core)`. There is no package step between this repository and
its consumers.

| Module | Target | Default | What it is |
|---|---|---|---|
| `core/util` | `arc_util` | always | `Result<T>`/`Error`, logging, md5, base64, filename sanitisation |
| `core/fs` | `arc_fs` | ON | paths/layout, non-throwing walks, the MediaIndex seam |
| `core/archive` | `arc_archive` | ON | libarchive extract/compress |
| `core/net` | `arc_net` | OFF | libcurl HTTP + segmented downloader |
| `core/tag` | `arc_tag` | OFF | TagLib 2 read/write, cover art |

`net`, `tag`, and `archive` are opt-in where their system dependencies are not
already in scope. A consumer builds only the modules it names.

## Documentation

- [`AGENTS.md`](AGENTS.md) — the short rules for changing the library.
- [`CLAUDE.md`](CLAUDE.md) — the detailed manual: module map, the FUSE/storage
  rationale, MediaIndex behaviour, Android JNI, and the known gaps.

## Build and test

```bash
cmake -S core -B build_test -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DARC_BUILD_ARCHIVE=OFF -DARC_BUILD_NET=OFF -DARC_BUILD_TAG=OFF \
      -DARC_BUILD_FS_TESTS=ON
cmake --build build_test
./build_test/fs/paths_test
./build_test/fs/walk_test
./build_test/fs/media_index_test
./build_test/fs/list_test
```

Tests are plain `assert()` binaries, Debug-only, run directly. The standalone
build is a compile check, not how consumers link the library.

## License

Copyright (C) 2026 nava.

This library is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version. See the [`LICENSE`](LICENSE) file for the full text.

It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the GNU Affero General Public License for more details.
