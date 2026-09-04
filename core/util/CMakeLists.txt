add_library(
    arc_util
    STATIC
    src/md5.cc
    src/base64.cc
    src/sanitize.cc
)

target_include_directories(arc_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(arc_util PUBLIC cxx_std_17)
