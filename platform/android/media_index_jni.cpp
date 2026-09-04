#include "media_index_android.hh"

#include <mutex>
#include <string>
#include <vector>

#include "arc/fs/media_index.hh"
#include "arc/log.hh"

namespace arc {
namespace fs {
namespace {

// Mirrored EXACTLY in MediaIndex.java. Two flat arrays rather than a list of
// objects: one call and two array copies, instead of six accessor hops per row
// over a library that can run to tens of thousands of tracks.
constexpr int kStrideStr = 6;  // path, title, artist, albumArtist, album, genre
constexpr int kStrideNum = 7;  // size, mtimeUnix, track, disc, year, durationMs, bitrate

// Attaches the calling thread for as long as it is in scope, and detaches on
// the way out ONLY if it was this object that attached it. A scan runs on a
// thread the JVM has never seen; detaching a thread that was already attached
// (the UI thread, say) would pull the floor out from under its caller.
class ScopedEnv {
public:
    explicit ScopedEnv(JavaVM *vm) : vm_(vm) {
        if (!vm_) return;
        const jint rc = vm_->GetEnv(reinterpret_cast<void **>(&env_), JNI_VERSION_1_6);
        if (rc == JNI_OK) return;
        if (rc == JNI_EDETACHED && vm_->AttachCurrentThread(&env_, nullptr) == JNI_OK)
            attached_ = true;
        else
            env_ = nullptr;
    }
    ~ScopedEnv() {
        if (attached_ && vm_) vm_->DetachCurrentThread();
    }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

    JNIEnv *get() const { return env_; }
    explicit operator bool() const { return env_ != nullptr; }

private:
    JavaVM *vm_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool attached_ = false;
};

// Clears a pending Java exception and reports it, so the next JNI call is not
// made with one still in flight (which aborts the process).
bool failed(JNIEnv *env, const char *what) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionDescribe();
    env->ExceptionClear();
    ARC_LOGW("media index: %s threw", what);
    return true;
}

std::string toUtf8(JNIEnv *env, jstring s) {
    if (!s) return {};
    const char *chars = env->GetStringUTFChars(s, nullptr);
    if (!chars) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(s, chars);
    return out;
}

class AndroidMediaIndex : public MediaIndexBackend {
public:
    JavaVM *vm = nullptr;
    jobject activity = nullptr;   // global ref
    jclass cls = nullptr;         // global ref to io/nava/archive_engine/MediaIndex
    jmethodID mQuery = nullptr;
    jmethodID mTakeStrings = nullptr;
    jmethodID mTakeNumbers = nullptr;
    jmethodID mGeneration = nullptr;
    jmethodID mObserve = nullptr;

    std::mutex changeMu;
    std::function<void()> onChange;

    bool query(const std::string &root, std::vector<MediaRecord> &out,
               Error &err) override {
        ScopedEnv env(vm);
        if (!env) {
            err.kind = ErrorKind::Io;
            err.message = "media index: cannot attach to the JVM";
            return false;
        }
        JNIEnv *e = env.get();

        jstring jroot = e->NewStringUTF(root.c_str());
        const jint count = e->CallStaticIntMethod(cls, mQuery, activity, jroot);
        e->DeleteLocalRef(jroot);
        if (failed(e, "MediaIndex.query") || count < 0) {
            // Below the Java side, a negative count means the ContentResolver
            // refused — most often because the all-files grant is not in hand
            // yet, which is an ordinary state on a first launch. It is an
            // error here so the caller can tell it apart from "the index knows
            // of nothing under this root", whose answer is different.
            err.kind = ErrorKind::Io;
            err.message = "media index: query refused (no storage access yet?)";
            return false;
        }
        if (count == 0) return true;   // success, and genuinely empty

        auto strs = static_cast<jobjectArray>(
            e->CallStaticObjectMethod(cls, mTakeStrings));
        if (failed(e, "MediaIndex.takeStrings") || !strs) {
            err.kind = ErrorKind::Io;
            err.message = "media index: string results unavailable";
            return false;
        }
        auto nums = static_cast<jlongArray>(
            e->CallStaticObjectMethod(cls, mTakeNumbers));
        if (failed(e, "MediaIndex.takeNumbers") || !nums) {
            e->DeleteLocalRef(strs);
            err.kind = ErrorKind::Io;
            err.message = "media index: numeric results unavailable";
            return false;
        }

        // Guard against a stride mismatch rather than trusting it. The two
        // sides are written to agree; if an edit ever breaks that, reading past
        // the end is a crash somewhere else entirely.
        const jsize haveStr = e->GetArrayLength(strs);
        const jsize haveNum = e->GetArrayLength(nums);
        if (haveStr < count * kStrideStr || haveNum < count * kStrideNum) {
            e->DeleteLocalRef(strs);
            e->DeleteLocalRef(nums);
            err.kind = ErrorKind::Parse;
            err.message = "media index: result stride disagrees with MediaIndex.java";
            return false;
        }

        std::vector<jlong> n(static_cast<size_t>(count) * kStrideNum);
        e->GetLongArrayRegion(nums, 0, count * kStrideNum, n.data());

        out.reserve(out.size() + static_cast<size_t>(count));
        for (jint i = 0; i < count; i++) {
            const jsize s = i * kStrideStr;
            const size_t m = static_cast<size_t>(i) * kStrideNum;

            MediaRecord r;
            auto take = [&](jsize idx) {
                auto js = static_cast<jstring>(e->GetObjectArrayElement(strs, idx));
                std::string v = toUtf8(e, js);
                if (js) e->DeleteLocalRef(js);
                return v;
            };
            r.path        = take(s);
            r.title       = take(s + 1);
            r.artist      = take(s + 2);
            r.albumArtist = take(s + 3);
            r.album       = take(s + 4);
            r.genre       = take(s + 5);

            r.size        = n[m];
            r.mtimeUnix   = n[m + 1];
            r.trackNumber = static_cast<int>(n[m + 2]);
            r.discNumber  = static_cast<int>(n[m + 3]);
            r.year        = static_cast<int>(n[m + 4]);
            r.durationMs  = static_cast<int>(n[m + 5]);
            r.bitrateBps  = static_cast<int>(n[m + 6]);
            // sampleRate/channels/bitDepth stay 0 — no column carries them.

            if (!r.path.empty()) out.push_back(std::move(r));
        }

        e->DeleteLocalRef(strs);
        e->DeleteLocalRef(nums);
        return true;
    }

    uint64_t generation() override {
        ScopedEnv env(vm);
        if (!env) return 0;
        JNIEnv *e = env.get();
        const jlong g = e->CallStaticLongMethod(cls, mGeneration, activity);
        if (failed(e, "MediaIndex.generation")) return 0;
        return g < 0 ? 0 : static_cast<uint64_t>(g);
    }

    bool observe(std::function<void()> cb, Error &err) override {
        {
            std::lock_guard<std::mutex> lk(changeMu);
            onChange = std::move(cb);
        }
        ScopedEnv env(vm);
        if (!env) {
            err.kind = ErrorKind::Io;
            err.message = "media index: cannot attach to the JVM";
            return false;
        }
        JNIEnv *e = env.get();
        const jboolean ok = e->CallStaticBooleanMethod(cls, mObserve, activity);
        if (failed(e, "MediaIndex.observe") || !ok) {
            err.kind = ErrorKind::Io;
            err.message = "media index: could not register a content observer";
            return false;
        }
        return true;
    }

    void fireChange() {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lk(changeMu);
            cb = onChange;
        }
        if (cb) cb();
    }
};

AndroidMediaIndex g_backend;

} // namespace

bool installAndroidMediaIndex(JavaVM *vm, jobject activity) {
    if (!vm || !activity) return false;

    ScopedEnv env(vm);
    if (!env) {
        ARC_LOGW("media index: no JNIEnv; the walk fallback stays in use");
        return false;
    }
    JNIEnv *e = env.get();

    // Reach the class through the ACTIVITY'S classloader, never FindClass().
    // A native thread attached to the JVM gets the system loader, which cannot
    // see classes packaged in the app — the lookup would fail in a way that
    // reads as "the class is missing" rather than "it is not visible from here".
    jclass actCls = e->GetObjectClass(activity);
    jmethodID getLoader = e->GetMethodID(actCls, "getClassLoader",
                                         "()Ljava/lang/ClassLoader;");
    if (failed(e, "getClassLoader lookup") || !getLoader) return false;
    jobject loader = e->CallObjectMethod(activity, getLoader);
    if (failed(e, "getClassLoader") || !loader) return false;

    jclass loaderCls = e->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = e->GetMethodID(loaderCls, "loadClass",
                                         "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = e->NewStringUTF("io.nava.archive_engine.MediaIndex");
    auto cls = static_cast<jclass>(e->CallObjectMethod(loader, loadClass, name));
    e->DeleteLocalRef(name);
    if (failed(e, "loadClass(MediaIndex)") || !cls) {
        // The app did not package the Java half. Not fatal: no backend is
        // installed, and every caller walks the tree exactly as it does on a
        // desktop.
        ARC_LOGI("media index: MediaIndex.java not packaged; walking instead");
        return false;
    }

    g_backend.vm = vm;
    g_backend.activity = e->NewGlobalRef(activity);
    g_backend.cls = static_cast<jclass>(e->NewGlobalRef(cls));
    e->DeleteLocalRef(cls);

    g_backend.mQuery = e->GetStaticMethodID(
        g_backend.cls, "query", "(Landroid/content/Context;Ljava/lang/String;)I");
    g_backend.mTakeStrings = e->GetStaticMethodID(
        g_backend.cls, "takeStrings", "()[Ljava/lang/String;");
    g_backend.mTakeNumbers = e->GetStaticMethodID(
        g_backend.cls, "takeNumbers", "()[J");
    g_backend.mGeneration = e->GetStaticMethodID(
        g_backend.cls, "generation", "(Landroid/content/Context;)J");
    g_backend.mObserve = e->GetStaticMethodID(
        g_backend.cls, "observe", "(Landroid/content/Context;)Z");

    if (failed(e, "MediaIndex method lookup") || !g_backend.mQuery ||
        !g_backend.mTakeStrings || !g_backend.mTakeNumbers ||
        !g_backend.mGeneration || !g_backend.mObserve) {
        ARC_LOGW("media index: MediaIndex.java is present but does not match "
                 "media_index_jni.cpp; walking instead");
        return false;
    }

    setMediaIndexBackend(&g_backend);
    ARC_LOGI("media index: MediaStore backend installed");
    return true;
}

} // namespace fs
} // namespace arc

extern "C" JNIEXPORT void JNICALL
Java_io_nava_archive_1engine_MediaIndex_nativeOnChange(JNIEnv *, jclass) {
    // Arrives on the observer's own HandlerThread. The consumer's callback is
    // expected to be thread-safe — the one this replaces (a FolderWatcher
    // callback) already was, because inotify delivered on its own thread too.
    //
    // Same translation unit as the backend, so the anonymous-namespace object
    // is reachable directly and needs no forwarding shim.
    arc::fs::g_backend.fireChange();
}
