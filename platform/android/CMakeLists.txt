cmake_minimum_required(VERSION 3.22.1)
project(archive_engine_android LANGUAGES C CXX)

set(ARC_BUILD_ARCHIVE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../core ${CMAKE_CURRENT_BINARY_DIR}/core)

find_library(log-lib log)

add_library(archive_engine SHARED archive_jni.cpp)
target_include_directories(archive_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Release-only optimizations: aggressive opt, thin LTO, hidden symbols and
# dead-code stripping for smaller, faster .so files. Debug stays untouched.
target_compile_options(archive_engine PRIVATE
    $<$<CONFIG:Release>:-O3 -flto=thin -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -fomit-frame-pointer>
)
target_link_options(archive_engine PRIVATE
    $<$<CONFIG:Release>:-flto=thin -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--strip-all>
)

target_link_libraries(archive_engine arc_archive ${log-lib})
