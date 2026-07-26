#!/usr/bin/env bash
# archive_engine standalone build (Linux) — smoke-builds core/ with its
# default module set (util + archive; net/tag are Android-only by default
# here since a desktop consumer, e.g. streamer, normally add_subdirectory()s
# core/ itself with its own curl/TagLib already in scope — see
# core/net/CMakeLists.txt and core/tag/CMakeLists.txt's desktop branch).
#
# This script isn't how streamer consumes archive_engine (streamer's own
# root CMakeLists.txt add_subdirectory()s first_party/KawusapiCC/core, which
# in turn add_subdirectory()s this repo's core/) — it just proves core/
# configures and compiles on its own.
set -e

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$root/build"

cmake -S "$root/core" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" -j"$(nproc)"

echo
echo "Done: $build (ae_util, ae_archive)"
echo "Pass -DAE_BUILD_NET=ON/-DAE_BUILD_TAG=ON only if CURL::libcurl / a 'tag'"
echo "target are already in scope (they are not, standalone) — see core/net"
echo "and core/tag CMakeLists.txt for what a desktop consumer must provide."
