#pragma once

#include <jni.h>

namespace arc {
namespace fs {

// ---------------------------------------------------------------------------
// Installs the MediaStore-backed arc::fs::MediaIndex backend.
//
// Call once, from the platform layer, before anything scans. After it returns
// true, arc::fs::MediaIndex::available() is true and query() answers from the
// system's catalogue instead of the caller having to walk the tree.
//
// `activity` is the app's Activity (or any Context) as a LOCAL or global
// reference — a global one is taken internally and held for the life of the
// process. `vm` is the process JavaVM; every call below attaches the calling
// thread to it, because a scan runs on a thread the JVM has never seen.
//
// Two things this deliberately does NOT do:
//
//   * It never calls FindClass() from a native thread. A thread attached that
//     way gets the SYSTEM classloader, which cannot see classes packaged in the
//     app — so io/nava/archive_engine/MediaIndex would simply not be found, and
//     the failure looks like the class being absent rather than unreachable.
//     The activity's own getClassLoader() is asked instead.
//
//   * It does not fail the process when the class is missing. An app that has
//     not packaged the Java half gets `false` back, no backend is installed,
//     and every caller falls through to walking the tree — which is exactly
//     what happens on desktop, and is a working app rather than a broken one.
//
// Returns false if the class or its methods cannot be reached.
// ---------------------------------------------------------------------------
bool installAndroidMediaIndex(JavaVM *vm, jobject activity);

} // namespace fs
} // namespace arc
