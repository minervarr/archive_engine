# arc_fs — one storage vocabulary for desktop and Android.
#
# Portable by construction: no platform SDK includes, no JNI (the rule at the
# top of ../CMakeLists.txt). The two things that genuinely need a JVM — the
# app's private directory, and the system media catalogue — are pushed in from
# the platform layer through setPlatformDirs() and setMediaIndexBackend()
# rather than probed here.

add_library(
    arc_fs
    STATIC
    src/paths.cc
    src/walk.cc
    src/volume.cc
    src/media_index.cc
)

target_include_directories(arc_fs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(arc_fs PUBLIC cxx_std_17)
target_link_libraries(arc_fs PUBLIC arc_util)

if(ANDROID)
    find_library(log-lib log)
    target_link_libraries(arc_fs PUBLIC ${log-lib})
endif()

# GCC 8 and Clang 8 need this to link std::filesystem.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(arc_fs PUBLIC stdc++fs)
endif()

# ---------------------------------------------------------------------------
# Tests. Debug-only and desktop-only, matching the convention the rest of this
# workspace uses: plain assert(), no framework, run the binary directly. The
# test sources #undef NDEBUG themselves so the asserts survive an optimized
# build, but there is no reason to build them in a Release consumer.
# ---------------------------------------------------------------------------
if(NOT ANDROID AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND ARC_BUILD_FS_TESTS)
    foreach(t paths_test walk_test media_index_test)
        add_executable(arc_${t} tests/${t}.cc)
        target_link_libraries(arc_${t} PRIVATE arc_fs)
        set_target_properties(arc_${t} PROPERTIES OUTPUT_NAME ${t})
    endforeach()
endif()
