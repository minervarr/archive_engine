#pragma once

// Logging for engine modules: logcat on Android, stderr elsewhere
// (host builds and unit tests).

#ifdef __ANDROID__

#include <android/log.h>

#define AE_LOG_TAG "archive_engine"
#define AE_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, AE_LOG_TAG, __VA_ARGS__)
#define AE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, AE_LOG_TAG, __VA_ARGS__)
#define AE_LOGW(...) __android_log_print(ANDROID_LOG_WARN, AE_LOG_TAG, __VA_ARGS__)
#define AE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, AE_LOG_TAG, __VA_ARGS__)

#else

#include <cstdio>

#define AE_LOG_STDERR(level, ...)                                  \
    do {                                                           \
        std::fprintf(stderr, "[%s] archive_engine: ", level);      \
        std::fprintf(stderr, __VA_ARGS__);                         \
        std::fprintf(stderr, "\n");                                \
    } while (0)

#define AE_LOGD(...) AE_LOG_STDERR("D", __VA_ARGS__)
#define AE_LOGI(...) AE_LOG_STDERR("I", __VA_ARGS__)
#define AE_LOGW(...) AE_LOG_STDERR("W", __VA_ARGS__)
#define AE_LOGE(...) AE_LOG_STDERR("E", __VA_ARGS__)

#endif
