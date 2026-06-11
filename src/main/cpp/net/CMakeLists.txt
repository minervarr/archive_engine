include(ExternalProject)

# Normalize Windows backslash paths to forward slashes; otherwise the toolchain
# path (e.g. C:\...\ndk\...) is parsed by CMake as invalid escape sequences.
file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}" AE_NET_TOOLCHAIN_FILE)
file(TO_CMAKE_PATH "${ANDROID_NDK}" AE_NET_ANDROID_NDK)

# --- mbedTLS (TLS backend for curl; far simpler to cross-compile than OpenSSL)

set(MBEDTLS_VERSION "3.6.2")
set(MBEDTLS_URL "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${MBEDTLS_VERSION}/mbedtls-${MBEDTLS_VERSION}.tar.bz2")
set(MBEDTLS_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/mbedtls-install")

ExternalProject_Add(
    mbedtls_ext
    URL               ${MBEDTLS_URL}
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${AE_NET_TOOLCHAIN_FILE}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DANDROID_NDK=${AE_NET_ANDROID_NDK}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${MBEDTLS_INSTALL_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DENABLE_TESTING=OFF
        -DENABLE_PROGRAMS=OFF
        -DGEN_FILES=OFF
    BUILD_BYPRODUCTS
        ${MBEDTLS_INSTALL_DIR}/lib/libmbedtls.a
        ${MBEDTLS_INSTALL_DIR}/lib/libmbedx509.a
        ${MBEDTLS_INSTALL_DIR}/lib/libmbedcrypto.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
)

file(MAKE_DIRECTORY ${MBEDTLS_INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${MBEDTLS_INSTALL_DIR}/lib)

foreach(lib mbedtls mbedx509 mbedcrypto)
    add_library(${lib}_static STATIC IMPORTED GLOBAL)
    add_dependencies(${lib}_static mbedtls_ext)
    set_target_properties(${lib}_static PROPERTIES
        IMPORTED_LOCATION ${MBEDTLS_INSTALL_DIR}/lib/lib${lib}.a
        INTERFACE_INCLUDE_DIRECTORIES ${MBEDTLS_INSTALL_DIR}/include
    )
endforeach()

# --- libcurl (HTTP only, static, mbedTLS backend)

set(CURL_VERSION "8.11.1")
set(CURL_URL "https://curl.se/download/curl-${CURL_VERSION}.tar.gz")
set(CURL_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/curl-install")

ExternalProject_Add(
    curl_ext
    URL               ${CURL_URL}
    DEPENDS           mbedtls_ext
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${AE_NET_TOOLCHAIN_FILE}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DANDROID_NDK=${AE_NET_ANDROID_NDK}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${CURL_INSTALL_DIR}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        # Let curl's find modules see the mbedTLS staging prefix despite the
        # NDK toolchain restricting searches to the sysroot.
        -DCMAKE_FIND_ROOT_PATH=${MBEDTLS_INSTALL_DIR}
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_CURL_EXE=OFF
        -DBUILD_TESTING=OFF
        -DBUILD_EXAMPLES=OFF
        -DBUILD_LIBCURL_DOCS=OFF
        -DBUILD_MISC_DOCS=OFF
        -DENABLE_CURL_MANUAL=OFF
        -DHTTP_ONLY=ON
        -DCURL_USE_MBEDTLS=ON
        -DCURL_USE_OPENSSL=OFF
        -DMBEDTLS_INCLUDE_DIR=${MBEDTLS_INSTALL_DIR}/include
        -DMBEDTLS_INCLUDE_DIRS=${MBEDTLS_INSTALL_DIR}/include
        -DMBEDTLS_LIBRARY=${MBEDTLS_INSTALL_DIR}/lib/libmbedtls.a
        -DMBEDX509_LIBRARY=${MBEDTLS_INSTALL_DIR}/lib/libmbedx509.a
        -DMBEDCRYPTO_LIBRARY=${MBEDTLS_INSTALL_DIR}/lib/libmbedcrypto.a
        -DCURL_USE_LIBPSL=OFF
        -DCURL_USE_LIBSSH2=OFF
        -DCURL_BROTLI=OFF
        -DCURL_ZSTD=OFF
        -DCURL_ZLIB=ON
        -DUSE_NGHTTP2=OFF
        -DUSE_LIBIDN2=OFF
        -DCURL_DISABLE_NTLM=ON
        # No baked-in trust store on Android; the app provides a PEM bundle
        # at runtime via HttpClient::Options::ca_bundle_path (CURLOPT_CAINFO).
        -DCURL_CA_BUNDLE=none
        -DCURL_CA_PATH=none
    BUILD_BYPRODUCTS ${CURL_INSTALL_DIR}/lib/libcurl.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
)

file(MAKE_DIRECTORY ${CURL_INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${CURL_INSTALL_DIR}/lib)

add_library(curl_static STATIC IMPORTED GLOBAL)
add_dependencies(curl_static curl_ext)
set_target_properties(curl_static PROPERTIES
    IMPORTED_LOCATION ${CURL_INSTALL_DIR}/lib/libcurl.a
    INTERFACE_INCLUDE_DIRECTORIES ${CURL_INSTALL_DIR}/include
    INTERFACE_COMPILE_DEFINITIONS CURL_STATICLIB
)

# --- ae_net

find_library(z-lib z)
find_library(log-lib log)

add_library(
    ae_net
    STATIC
    src/http.cc
)

target_include_directories(ae_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ae_net PUBLIC cxx_std_17)
target_link_libraries(ae_net
    PUBLIC ae_util
    PRIVATE curl_static mbedtls_static mbedx509_static mbedcrypto_static ${z-lib} ${log-lib}
)
