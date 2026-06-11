cmake_minimum_required(VERSION 3.22.1)
project(archive_engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Modules are opt-in so consumers via add_subdirectory() build only what
# they use. The standalone Gradle build keeps the defaults (archive + JNI).
option(AE_BUILD_ARCHIVE "Build the libarchive-based archive module" ON)
option(AE_BUILD_NET "Build the curl-based HTTP/download module" OFF)
option(AE_BUILD_TAG "Build the TagLib-based audio tagging module" OFF)
option(AE_BUILD_JNI "Build the archive_engine JNI shared library" ON)

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

if(AE_BUILD_JNI AND AE_BUILD_ARCHIVE)
    find_library(log-lib log)

    add_library(archive_engine SHARED jni/archive_jni.cpp)

    # Release-only optimizations: aggressive opt, thin LTO, hidden symbols and
    # dead-code stripping for smaller, faster .so files. Debug stays untouched.
    target_compile_options(archive_engine PRIVATE
        $<$<CONFIG:Release>:-O3 -flto=thin -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -fomit-frame-pointer>
    )
    target_link_options(archive_engine PRIVATE
        $<$<CONFIG:Release>:-flto=thin -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--strip-all>
    )

    target_link_libraries(archive_engine ae_archive ${log-lib})
endif()
