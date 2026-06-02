cmake_minimum_required(VERSION 3.22.1)
project(archive_engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)

set(LIBARCHIVE_VERSION "3.7.4")
set(LIBARCHIVE_URL "https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/libarchive-${LIBARCHIVE_VERSION}.tar.gz")
set(LIBARCHIVE_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/libarchive-install")

# Normalize Windows backslash paths to forward slashes; otherwise the toolchain
# path (e.g. C:\...\ndk\...) is parsed by CMake as invalid escape sequences.
file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}" LIBARCHIVE_TOOLCHAIN_FILE)
file(TO_CMAKE_PATH "${ANDROID_NDK}" LIBARCHIVE_ANDROID_NDK)

ExternalProject_Add(
    libarchive_ext
    URL               ${LIBARCHIVE_URL}
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${LIBARCHIVE_TOOLCHAIN_FILE}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DANDROID_NDK=${LIBARCHIVE_ANDROID_NDK}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${LIBARCHIVE_INSTALL_DIR}
        -DCMAKE_POLICY_DEFAULT_CMP0057=NEW
        -DENABLE_TEST=OFF
        -DENABLE_TAR=OFF
        -DENABLE_CAT=OFF
        -DENABLE_CPIO=OFF
        -DENABLE_XATTR=OFF
        -DENABLE_ACL=OFF
        -DENABLE_ICONV=OFF
        -DENABLE_EXPAT=OFF
        -DENABLE_OPENSSL=OFF
        -DENABLE_MBEDTLS=OFF
        -DENABLE_NETTLE=OFF
        -DENABLE_LIBXML2=OFF
        -DENABLE_LZMA=ON
        -DENABLE_ZLIB=ON
        -DENABLE_BZ2=ON
        -DENABLE_ZSTD=OFF
        -DBUILD_SHARED_LIBS=OFF
    BUILD_BYPRODUCTS ${LIBARCHIVE_INSTALL_DIR}/lib/libarchive.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
)

file(MAKE_DIRECTORY ${LIBARCHIVE_INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${LIBARCHIVE_INSTALL_DIR}/lib)

add_library(libarchive_static STATIC IMPORTED GLOBAL)
add_dependencies(libarchive_static libarchive_ext)
set_target_properties(libarchive_static PROPERTIES
    IMPORTED_LOCATION ${LIBARCHIVE_INSTALL_DIR}/lib/libarchive.a
    INTERFACE_INCLUDE_DIRECTORIES ${LIBARCHIVE_INSTALL_DIR}/include
)

find_library(log-lib log)
find_library(z-lib z)

add_library(
    archive_engine
    SHARED
    archive_jni.cpp
    debug.cpp
)

# Release-only optimizations: aggressive opt, thin LTO, hidden symbols and
# dead-code stripping for smaller, faster .so files. Debug stays untouched.
target_compile_options(archive_engine PRIVATE
    $<$<CONFIG:Release>:-O3 -flto=thin -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -fomit-frame-pointer>
)
target_link_options(archive_engine PRIVATE
    $<$<CONFIG:Release>:-flto=thin -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--strip-all>
)

target_include_directories(archive_engine PRIVATE
    ${LIBARCHIVE_INSTALL_DIR}/include
)

target_link_libraries(archive_engine
    libarchive_static
    ${log-lib}
    ${z-lib}
)
