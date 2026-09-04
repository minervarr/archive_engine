# arc_tag — TagLib-based audio tagging module. The source (src/tag.cc) is
# fully portable; what differs per platform is where TagLib comes from.
#   Android: no system TagLib — cross-compiled here via ExternalProject_Add.
#   Desktop: the consumer already has a TagLib (e.g. streamer's vendored
#            third_party/taglib submodule) — reuse its `tag` target instead
#            of fetching/building a second copy.

add_library(arc_tag STATIC src/tag.cc)
target_include_directories(arc_tag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(arc_tag PUBLIC cxx_std_17)
target_link_libraries(arc_tag PUBLIC arc_util)

if(ANDROID)
    include(ExternalProject)

    # Normalize Windows backslash paths to forward slashes; otherwise the
    # toolchain path (e.g. C:\...\ndk\...) is parsed by CMake as invalid
    # escape sequences.
    file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}" ARC_TAG_TOOLCHAIN_FILE)
    file(TO_CMAKE_PATH "${ANDROID_NDK}" ARC_TAG_ANDROID_NDK)

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
            -DCMAKE_TOOLCHAIN_FILE=${ARC_TAG_TOOLCHAIN_FILE}
            -DANDROID_ABI=${ANDROID_ABI}
            -DANDROID_PLATFORM=${ANDROID_PLATFORM}
            -DANDROID_NDK=${ARC_TAG_ANDROID_NDK}
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
    target_link_libraries(arc_tag PRIVATE taglib_static ${z-lib} ${log-lib})
else()
    # Desktop: the consumer must add_subdirectory() (or find_package) a
    # TagLib providing a `tag` target BEFORE add_subdirectory()'ing this
    # directory — see streamer's root CMakeLists.txt for the reference setup
    # (third_party/taglib, vendored submodule).
    if(NOT TARGET tag)
        message(FATAL_ERROR
            "arc_tag (desktop): no 'tag' (TagLib) target found. Provide one "
            "(find_package(TagLib) or add_subdirectory a TagLib build) "
            "before add_subdirectory()'ing ${CMAKE_CURRENT_SOURCE_DIR}.")
    endif()
    target_link_libraries(arc_tag PRIVATE tag)
endif()
