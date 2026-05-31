#include <jni.h>
#include <string>
#include <vector>
#include <cstdio>

#include <archive.h>
#include <archive_entry.h>

#include "debug.h"

static std::string jstr(JNIEnv *env, jstring js) {
    if (!js) return {};
    const char *cs = env->GetStringUTFChars(js, nullptr);
    std::string s(cs);
    env->ReleaseStringUTFChars(js, cs);
    return s;
}

static thread_local std::string g_last_error;

static void set_error(const char *ctx, const char *detail) {
    g_last_error = std::string(ctx) + ": " + (detail ? detail : "unknown");
    AE_LOGE("%s", g_last_error.c_str());
}

static int copy_data(archive *src, archive *dst) {
    const void *buf;
    size_t size;
    la_int64_t offset;
    for (;;) {
        int r = archive_read_data_block(src, &buf, &size, &offset);
        if (r == ARCHIVE_EOF) return ARCHIVE_OK;
        if (r < ARCHIVE_OK) return r;
        if (archive_write_data_block(dst, buf, size, offset) < ARCHIVE_OK)
            return ARCHIVE_FATAL;
    }
}

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
    std::string src_path = jstr(env, jArchivePath);
    std::string dest_dir = jstr(env, jDestDir);
    AE_LOGI("extract src=%s dest=%s", src_path.c_str(), dest_dir.c_str());

    archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    archive *out = archive_write_disk_new();
    archive_write_disk_set_options(out,
        ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_NODOTDOT);
    archive_write_disk_set_standard_lookup(out);

    if (archive_read_open_filename(a, src_path.c_str(), 16384) != ARCHIVE_OK) {
        set_error("archive_read_open_filename", archive_error_string(a));
        archive_read_free(a);
        archive_write_free(out);
        return JNI_FALSE;
    }

    archive_entry *entry;
    bool ok = true;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string entry_path = dest_dir + "/" + archive_entry_pathname(entry);
        archive_entry_set_pathname(entry, entry_path.c_str());

        if (archive_write_header(out, entry) != ARCHIVE_OK) {
            set_error("archive_write_header", archive_error_string(out));
            ok = false;
            break;
        }
        if (copy_data(a, out) != ARCHIVE_OK) {
            set_error("copy_data", archive_error_string(out));
            ok = false;
            break;
        }
    }

    archive_read_free(a);
    archive_write_free(out);
    if (ok) AE_LOGI("extract done");
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_io_nava_archive_1engine_ArchiveEngine_compress(
        JNIEnv *env, jclass,
        jobjectArray jSrcPaths, jstring jDestPath, jstring jFormat) {

    AE_TRACE("compress");
    std::string dest_path = jstr(env, jDestPath);
    std::string format    = jstr(env, jFormat);
    AE_LOGI("compress dest=%s format=%s", dest_path.c_str(), format.c_str());

    jsize count = env->GetArrayLength(jSrcPaths);
    std::vector<std::string> src_paths;
    src_paths.reserve(count);
    for (jsize i = 0; i < count; ++i) {
        auto js = (jstring)env->GetObjectArrayElement(jSrcPaths, i);
        src_paths.push_back(jstr(env, js));
        env->DeleteLocalRef(js);
    }

    archive *a = archive_write_new();
    if (format == "zip") {
        archive_write_set_format_zip(a);
    } else {
        archive_write_set_format_gnutar(a);
        archive_write_add_filter_gzip(a);
    }

    if (archive_write_open_filename(a, dest_path.c_str()) != ARCHIVE_OK) {
        set_error("archive_write_open_filename", archive_error_string(a));
        archive_write_free(a);
        return JNI_FALSE;
    }

    bool ok = true;
    for (const auto &src : src_paths) {
        archive *disk = archive_read_disk_new();
        archive_read_disk_set_standard_lookup(disk);

        if (archive_read_disk_open(disk, src.c_str()) != ARCHIVE_OK) {
            set_error("archive_read_disk_open", archive_error_string(disk));
            archive_read_free(disk);
            ok = false;
            break;
        }

        archive_entry *entry = archive_entry_new();
        int r;
        while ((r = archive_read_next_header2(disk, entry)) == ARCHIVE_OK) {
            if (archive_entry_filetype(entry) == AE_IFDIR)
                archive_read_disk_descend(disk);

            if (archive_write_header(a, entry) != ARCHIVE_OK) {
                set_error("archive_write_header", archive_error_string(a));
                ok = false;
                break;
            }

            if (archive_entry_filetype(entry) == AE_IFREG) {
                const char *path = archive_entry_sourcepath(entry);
                if (path) {
                    FILE *f = fopen(path, "rb");
                    if (f) {
                        char buf[65536];
                        size_t n;
                        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
                            archive_write_data(a, buf, n);
                        fclose(f);
                    } else {
                        AE_LOGW("fopen failed for %s errno=%d", path, errno);
                    }
                }
            }
        }
        archive_entry_free(entry);
        archive_read_free(disk);
        if (!ok) break;
    }

    archive_write_close(a);
    archive_write_free(a);
    if (ok) AE_LOGI("compress done dest=%s", dest_path.c_str());
    return ok ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
