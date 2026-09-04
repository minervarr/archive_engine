#pragma once

// Logging for engine modules: logcat on Android, stderr elsewhere
// (host builds and unit tests).

#ifdef __ANDROID__

#include <android/log.h>

#define ARC_LOG_TAG "archive_engine"
#define ARC_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, ARC_LOG_TAG, __VA_ARGS__)
#define ARC_LOGI(...) __android_log_print(ANDROID_LOG_INFO, ARC_LOG_TAG, __VA_ARGS__)
#define ARC_LOGW(...) __android_log_print(ANDROID_LOG_WARN, ARC_LOG_TAG, __VA_ARGS__)
#define ARC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ARC_LOG_TAG, __VA_ARGS__)

#else

#include <cstdio>

#define ARC_LOG_STDERR(level, ...)                                  \
    do {                                                           \
        std::fprintf(stderr, "[%s] archive_engine: ", level);      \
        std::fprintf(stderr, __VA_ARGS__);                         \
        std::fprintf(stderr, "\n");                                \
    } while (0)

#define ARC_LOGD(...) ARC_LOG_STDERR("D", __VA_ARGS__)
#define ARC_LOGI(...) ARC_LOG_STDERR("I", __VA_ARGS__)
#define ARC_LOGW(...) ARC_LOG_STDERR("W", __VA_ARGS__)
#define ARC_LOGE(...) ARC_LOG_STDERR("E", __VA_ARGS__)

#endif
