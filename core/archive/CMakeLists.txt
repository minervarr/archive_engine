include(ExternalProject)

set(LIBARCHIVE_VERSION "3.7.4")
set(LIBARCHIVE_URL "https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/libarchive-${LIBARCHIVE_VERSION}.tar.gz")
set(LIBARCHIVE_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/libarchive-install")

# Cross-compile arguments, and ONLY when cross-compiling. Passing
# -DCMAKE_TOOLCHAIN_FILE= (empty) on a desktop configure is not the same as
# not passing it: CMake takes the flag as "a toolchain was requested", finds
# nothing at that path, and the libarchive configure fails — which is what the
# standalone desktop build did before this guard existed. core/net and core/tag
# already wrap their own ExternalProjects in if(ANDROID) for the same reason.
set(LIBARCHIVE_CROSS_ARGS "")
if(ANDROID)
    # Normalize Windows backslash paths to forward slashes; otherwise the
    # toolchain path (e.g. C:\...\ndk\...) is parsed by CMake as invalid
    # escape sequences.
    file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}" LIBARCHIVE_TOOLCHAIN_FILE)
    file(TO_CMAKE_PATH "${ANDROID_NDK}" LIBARCHIVE_ANDROID_NDK)
    set(LIBARCHIVE_CROSS_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${LIBARCHIVE_TOOLCHAIN_FILE}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DANDROID_NDK=${LIBARCHIVE_ANDROID_NDK}
    )
endif()

ExternalProject_Add(
    libarchive_ext
    URL               ${LIBARCHIVE_URL}
    CMAKE_ARGS
        ${LIBARCHIVE_CROSS_ARGS}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${LIBARCHIVE_INSTALL_DIR}
        -DCMAKE_POLICY_DEFAULT_CMP0057=NEW
        # libarchive 3.7.4 still declares cmake_minimum_required(VERSION 2.8.12),
        # and CMake 4.x refuses to configure a project that old. This is the
        # escape hatch CMake's own error message names. Unknown to CMake 3.x,
        # where it lands as an unused cache entry and costs nothing.
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
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

find_library(z-lib z)

add_library(
    arc_archive
    STATIC
    src/archive.cc
)

target_include_directories(arc_archive PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(arc_archive PUBLIC cxx_std_17)
target_link_libraries(arc_archive PUBLIC arc_util libarchive_static ${z-lib})
if(ANDROID)
    find_library(log-lib log)
    target_link_libraries(arc_archive PUBLIC ${log-lib})
endif()
