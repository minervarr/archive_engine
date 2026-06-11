add_library(
    ae_util
    STATIC
    src/md5.cc
    src/base64.cc
    src/sanitize.cc
)

target_include_directories(ae_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ae_util PUBLIC cxx_std_17)
