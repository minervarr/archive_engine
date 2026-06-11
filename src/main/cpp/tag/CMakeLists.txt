include(ExternalProject)

# Normalize Windows backslash paths to forward slashes; otherwise the toolchain
# path (e.g. C:\...\ndk\...) is parsed by CMake as invalid escape sequences.
file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}" AE_TAG_TOOLCHAIN_FILE)
file(TO_CMAKE_PATH "${ANDROID_NDK}" AE_TAG_ANDROID_NDK)

# utfcpp is a header-only dependency TagLib expects at 3rdparty/utfcpp
# (normally a git submodule, absent from the release tarball).
set(UTFCPP_VERSION "4.0.6")
set(UTFCPP_URL "https://github.com/nemtrif/utfcpp/archive/refs/tags/v${UTFCPP_VERSION}.tar.gz")
set(UTFCPP_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/utfcpp-src")

ExternalProject_Add(
    utfcpp_ext
    URL               ${UTFCPP_URL}
    SOURCE_DIR        ${UTFCPP_SOURCE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ""
    INSTALL_COMMAND   ""
    LOG_DOWNLOAD ON
)

set(TAGLIB_VERSION "2.0.2")
set(TAGLIB_URL "https://taglib.org/releases/taglib-${TAGLIB_VERSION}.tar.gz")
set(TAGLIB_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/taglib-install")

ExternalProject_Add(
    taglib_ext
    URL               ${TAGLIB_URL}
    DEPENDS           utfcpp_ext
    PATCH_COMMAND     ${CMAKE_COMMAND} -E copy_directory ${UTFCPP_SOURCE_DIR} <SOURCE_DIR>/3rdparty/utfcpp
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${AE_TAG_TOOLCHAIN_FILE}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DANDROID_NDK=${AE_TAG_ANDROID_NDK}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${TAGLIB_INSTALL_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTING=OFF
        -DBUILD_EXAMPLES=OFF
        -DBUILD_BINDINGS=OFF
    BUILD_BYPRODUCTS ${TAGLIB_INSTALL_DIR}/lib/libtag.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
)

file(MAKE_DIRECTORY ${TAGLIB_INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${TAGLIB_INSTALL_DIR}/lib)

add_library(taglib_static STATIC IMPORTED GLOBAL)
add_dependencies(taglib_static taglib_ext)
set_target_properties(taglib_static PROPERTIES
    IMPORTED_LOCATION ${TAGLIB_INSTALL_DIR}/lib/libtag.a
    INTERFACE_INCLUDE_DIRECTORIES ${TAGLIB_INSTALL_DIR}/include
)

find_library(z-lib z)
find_library(log-lib log)

add_library(
    ae_tag
    STATIC
    src/tag.cc
)

target_include_directories(ae_tag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ae_tag PUBLIC cxx_std_17)
target_link_libraries(ae_tag
    PUBLIC ae_util
    PRIVATE taglib_static ${z-lib} ${log-lib}
)
