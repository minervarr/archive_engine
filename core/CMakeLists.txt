cmake_minimum_required(VERSION 3.22.1)
project(archive_engine_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Platform-agnostic modules only — no platform SDK includes, no JNI. Modules
# are opt-in so consumers via add_subdirectory() build only what they use.
# The standalone Android build (platform/android/) keeps these defaults.
option(AE_BUILD_ARCHIVE "Build the libarchive-based archive module" ON)
option(AE_BUILD_NET "Build the curl-based HTTP/download module" OFF)
option(AE_BUILD_TAG "Build the TagLib-based audio tagging module" OFF)

add_subdirectory(util)

if(AE_BUILD_ARCHIVE)
    add_subdirectory(archive)
endif()

if(AE_BUILD_NET)
    add_subdirectory(net)
endif()

if(AE_BUILD_TAG)
    add_subdirectory(tag)
endif()
