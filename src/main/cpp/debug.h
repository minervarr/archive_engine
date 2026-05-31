#pragma once
#include <android/log.h>

#define AE_TAG "archive_engine"
#define AE_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, AE_TAG, __VA_ARGS__)
#define AE_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  AE_TAG, __VA_ARGS__)
#define AE_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  AE_TAG, __VA_ARGS__)
#define AE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, AE_TAG, __VA_ARGS__)

// Logs entry + exit of a scope; use at top of JNI functions.
#define AE_TRACE(func) AE_LOGD("[TRACE] " func " enter")
