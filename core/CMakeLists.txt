cmake_minimum_required(VERSION 3.22.1)
project(archive_engine_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Platform-agnostic modules only — no platform SDK includes, no JNI. Modules
# are opt-in so consumers via add_subdirectory() build only what they use.
# The standalone Android build (platform/android/) keeps these defaults.
option(ARC_BUILD_ARCHIVE "Build the libarchive-based archive module" ON)
option(ARC_BUILD_NET "Build the curl-based HTTP/download module" OFF)
option(ARC_BUILD_TAG "Build the TagLib-based audio tagging module" OFF)
# ON by default, unlike the three above: fs depends on nothing but the standard
# library, so a consumer never has to supply anything for it to build.
option(ARC_BUILD_FS "Build the storage/paths/enumeration module" ON)
option(ARC_BUILD_FS_TESTS "Build arc_fs's assert-based tests (Debug, desktop)" OFF)

add_subdirectory(util)

if(ARC_BUILD_FS)
    add_subdirectory(fs)
endif()

if(ARC_BUILD_ARCHIVE)
    add_subdirectory(archive)
endif()

if(ARC_BUILD_NET)
    add_subdirectory(net)
endif()

if(ARC_BUILD_TAG)
    add_subdirectory(tag)
endif()
