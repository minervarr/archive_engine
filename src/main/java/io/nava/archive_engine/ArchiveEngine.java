package io.nava.archive_engine;

public final class ArchiveEngine {

    static {
        System.loadLibrary("archive_engine");
    }

    private ArchiveEngine() {}

    /**
     * Extract an archive into destDir.
     * Supported formats: tar, tar.gz, tar.bz2, tar.xz, zip (auto-detected).
     *
     * @param archivePath absolute path to the archive file
     * @param destDir     absolute path to the destination directory (must exist)
     * @return true on success; call getLastError() on failure
     */
    public static native boolean extract(String archivePath, String destDir);

    /**
     * Compress one or more source paths into a single archive.
     *
     * @param srcPaths  absolute paths to files or directories to include
     * @param destPath  absolute path for the output archive
     * @param format    "tar.gz" or "zip"
     * @return true on success; call getLastError() on failure
     */
    public static native boolean compress(String[] srcPaths, String destPath, String format);

    /**
     * Returns a human-readable error message from the last failed operation.
     * Empty string if no error has occurred.
     */
    public static native String getLastError();
}
