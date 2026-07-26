#include <jni.h>
#include <string>
#include <vector>

#include "ae/archive.hh"
#include "debug.h"

static std::string jstr(JNIEnv *env, jstring js) {
    if (!js) return {};
    const char *cs = env->GetStringUTFChars(js, nullptr);
    std::string s(cs);
    env->ReleaseStringUTFChars(js, cs);
    return s;
}

static thread_local std::string g_last_error;

extern "C" {

JNIEXPORT jstring JNICALL
Java_io_nava_archive_1engine_ArchiveEngine_getLastError(JNIEnv *env, jclass) {
    return env->NewStringUTF(g_last_error.c_str());
}

JNIEXPORT jboolean JNICALL
Java_io_nava_archive_1engine_ArchiveEngine_extract(
        JNIEnv *env, jclass,
        jstring jArchivePath, jstring jDestDir) {

    AE_TRACE("extract");
    auto result = ae::archive_extract(jstr(env, jArchivePath), jstr(env, jDestDir));
    if (!result.ok()) {
        g_last_error = result.error().message;
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_io_nava_archive_1engine_ArchiveEngine_compress(
        JNIEnv *env, jclass,
        jobjectArray jSrcPaths, jstring jDestPath, jstring jFormat) {

    AE_TRACE("compress");
    jsize count = env->GetArrayLength(jSrcPaths);
    std::vector<std::string> src_paths;
    src_paths.reserve(count);
    for (jsize i = 0; i < count; ++i) {
        auto js = (jstring)env->GetObjectArrayElement(jSrcPaths, i);
        src_paths.push_back(jstr(env, js));
        env->DeleteLocalRef(js);
    }

    std::string format = jstr(env, jFormat);
    ae::ArchiveFormat fmt =
        (format == "zip") ? ae::ArchiveFormat::Zip : ae::ArchiveFormat::TarGz;

    auto result = ae::archive_compress(src_paths, jstr(env, jDestPath), fmt);
    if (!result.ok()) {
        g_last_error = result.error().message;
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

} // extern "C"
