package io.nava.archive_engine;

import android.app.Activity;
import android.content.Context;
import android.database.ContentObserver;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.provider.MediaStore;

import java.util.ArrayList;

/**
 * The system's own catalogue of audio files, as one query.
 *
 * <p>This exists because of what it replaces. Building the same list by walking
 * the tree means a readdir and a stat per entry and an open per file to read a
 * header — and since Android 11 every one of those goes through FUSE, a round
 * trip into a userspace daemon. Android 12's FUSE passthrough and the later
 * fuse-bpf work speed up reads on an already-open descriptor; they do nothing
 * for the metadata operations a scan is actually made of. MediaStore already
 * did that work when the files landed, so asking it costs one Binder call for
 * the whole library.
 *
 * <p><b>What it cannot tell you:</b> sample rate, channel count and bit depth.
 * No column carries them. An app that distinguishes a 16/44.1 release from a
 * 24/96 one still has to open those files — it just no longer has to open all
 * of them, and it can do it after the library is already on screen.
 *
 * <p>Results cross to native as two flat arrays rather than a list of objects:
 * one JNI call and two array copies, instead of six accessor hops per row over
 * a library that can run to tens of thousands of tracks. The strides are fixed
 * and are mirrored exactly in media_index_jni.cpp — change one and you must
 * change the other.
 */
public final class MediaIndex {

    private MediaIndex() {}

    /** Strings per row, in order. Mirrored in media_index_jni.cpp. */
    public static final int STRIDE_STR = 6;   // path, title, artist, albumArtist, album, genre
    /** Numbers per row, in order. Mirrored in media_index_jni.cpp. */
    public static final int STRIDE_NUM = 7;   // size, mtimeUnix, track, disc, year, durationMs, bitrate

    /** Filled by query(); read by the two accessors below. Not thread-safe: one query at a time. */
    private static String[] lastStrings = new String[0];
    private static long[] lastNumbers = new long[0];

    private static ContentObserver observer;
    private static HandlerThread observerThread;

    /**
     * Runs the query and returns the row count, or -1 on failure.
     *
     * <p>The results are held until the next call and fetched with
     * {@link #takeStrings()} / {@link #takeNumbers()} — a return value cannot
     * carry two arrays, and two extra JNI hops are cheaper than boxing rows.
     *
     * @param ctx  any Context; the ContentResolver is taken from it
     * @param root absolute directory to restrict to, or null/empty for everything
     */
    public static int query(Context ctx, String root) {
        lastStrings = new String[0];
        lastNumbers = new long[0];
        if (ctx == null) return -1;

        // ALBUM_ARTIST, GENRE and BITRATE are API 30. minSdk here is lower, so
        // the projection is built to match the device rather than assumed — on
        // an older one those three simply come back empty and the caller sees
        // the same "unknown" it sees for a file with no tag.
        final boolean api30 = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R;

        final ArrayList<String> proj = new ArrayList<>();
        proj.add(MediaStore.Audio.Media.DATA);          // 0
        proj.add(MediaStore.Audio.Media.TITLE);         // 1
        proj.add(MediaStore.Audio.Media.ARTIST);        // 2
        proj.add(api30 ? MediaStore.Audio.Media.ALBUM_ARTIST : MediaStore.Audio.Media.ARTIST);
        proj.add(MediaStore.Audio.Media.ALBUM);         // 4
        if (api30) proj.add(MediaStore.Audio.Media.GENRE);
        proj.add(MediaStore.Audio.Media.SIZE);
        proj.add(MediaStore.Audio.Media.DATE_MODIFIED);
        proj.add(MediaStore.Audio.Media.TRACK);
        proj.add(MediaStore.Audio.Media.YEAR);
        proj.add(MediaStore.Audio.Media.DURATION);
        if (api30) proj.add(MediaStore.Audio.Media.BITRATE);

        String sel = MediaStore.Audio.Media.IS_MUSIC + " != 0";
        String[] args = null;
        if (root != null && !root.isEmpty()) {
            // DATA is deprecated but still populated, and readable with the
            // all-files grant this app already holds. It is also the only
            // column that answers "is this file under that directory" for a
            // root the user chose, rather than one of the media buckets
            // RELATIVE_PATH is expressed in.
            //
            // '\' escapes, because '_' is a LIKE wildcard and is ordinary in a
            // directory name: without it, a root of "/sdcard/my_music" would
            // also match "/sdcard/myXmusic".
            sel += " AND " + MediaStore.Audio.Media.DATA + " LIKE ? ESCAPE '\\'";
            String prefix = root.endsWith("/") ? root : root + "/";
            args = new String[] { escapeLike(prefix) + "%" };
        }

        Cursor c = null;
        try {
            c = ctx.getContentResolver().query(
                    MediaStore.Audio.Media.EXTERNAL_CONTENT_URI,
                    proj.toArray(new String[0]), sel, args, null);
            if (c == null) return -1;

            final int n = c.getCount();
            final String[] strs = new String[n * STRIDE_STR];
            final long[] nums = new long[n * STRIDE_NUM];

            final int iData = c.getColumnIndex(MediaStore.Audio.Media.DATA);
            final int iTitle = c.getColumnIndex(MediaStore.Audio.Media.TITLE);
            final int iArtist = c.getColumnIndex(MediaStore.Audio.Media.ARTIST);
            final int iAlbumArtist = api30
                    ? c.getColumnIndex(MediaStore.Audio.Media.ALBUM_ARTIST) : -1;
            final int iAlbum = c.getColumnIndex(MediaStore.Audio.Media.ALBUM);
            final int iGenre = api30 ? c.getColumnIndex(MediaStore.Audio.Media.GENRE) : -1;
            final int iSize = c.getColumnIndex(MediaStore.Audio.Media.SIZE);
            final int iMtime = c.getColumnIndex(MediaStore.Audio.Media.DATE_MODIFIED);
            final int iTrack = c.getColumnIndex(MediaStore.Audio.Media.TRACK);
            final int iYear = c.getColumnIndex(MediaStore.Audio.Media.YEAR);
            final int iDur = c.getColumnIndex(MediaStore.Audio.Media.DURATION);
            final int iBitrate = api30 ? c.getColumnIndex(MediaStore.Audio.Media.BITRATE) : -1;

            int row = 0;
            while (c.moveToNext()) {
                final int s = row * STRIDE_STR;
                final int m = row * STRIDE_NUM;

                strs[s]     = str(c, iData);
                strs[s + 1] = str(c, iTitle);
                strs[s + 2] = str(c, iArtist);
                strs[s + 3] = str(c, iAlbumArtist);
                strs[s + 4] = str(c, iAlbum);
                strs[s + 5] = str(c, iGenre);

                nums[m]     = num(c, iSize);
                nums[m + 1] = num(c, iMtime);          // unix SECONDS

                // TRACK packs the disc into the thousands digit: disc 2 track 5
                // is 2005. Values below 1000 are a plain track number on a
                // single-disc release.
                final long packed = num(c, iTrack);
                nums[m + 2] = packed % 1000;           // track
                nums[m + 3] = packed / 1000;           // disc (0 == not stated)

                nums[m + 4] = num(c, iYear);
                nums[m + 5] = num(c, iDur);
                nums[m + 6] = num(c, iBitrate);
                row++;
            }

            lastStrings = strs;
            lastNumbers = nums;
            return row;
        } catch (Throwable t) {
            // A SecurityException here means the all-files grant is not in hand
            // yet. That is a normal state on a first launch, not a crash, and
            // the caller's answer to it is to walk the tree instead.
            return -1;
        } finally {
            if (c != null) c.close();
        }
    }

    public static String[] takeStrings() {
        String[] s = lastStrings;
        lastStrings = new String[0];
        return s;
    }

    public static long[] takeNumbers() {
        long[] n = lastNumbers;
        lastNumbers = new long[0];
        return n;
    }

    /**
     * A token that changes whenever the catalogue does, so an unchanged library
     * can be recognised without querying it. 0 when the platform cannot say
     * (below API 30), which means "fall back to comparing sizes and times".
     *
     * <p>Documented as more robust than DATE_MODIFIED, which moves in
     * unexpected ways when an app calls File.setLastModified() or the clock is
     * wrong.
     */
    public static long generation(Context ctx) {
        if (ctx == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return 0;
        try {
            return MediaStore.getGeneration(ctx, MediaStore.VOLUME_EXTERNAL_PRIMARY);
        } catch (Throwable t) {
            return 0;
        }
    }

    /**
     * Registers for catalogue changes and calls {@link #nativeOnChange()}.
     *
     * <p>This is the replacement for a directory watch, and not a stylistic
     * one: inotify on a FUSE mount ADDS THE WATCH SUCCESSFULLY and then never
     * fires, so the failure is indistinguishable from a library nobody touched.
     *
     * <p>The observer runs on its own HandlerThread — registering it needs a
     * Looper, and borrowing the main one would put filesystem-change work on
     * the UI thread.
     */
    public static boolean observe(Context ctx) {
        if (ctx == null) return false;
        if (observer != null) return true;   // already watching
        try {
            observerThread = new HandlerThread("arc-media-index");
            observerThread.start();
            observer = new ContentObserver(new Handler(observerThread.getLooper())) {
                @Override public void onChange(boolean selfChange, Uri uri) {
                    nativeOnChange();
                }
            };
            ctx.getContentResolver().registerContentObserver(
                    MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, true, observer);
            return true;
        } catch (Throwable t) {
            observer = null;
            if (observerThread != null) { observerThread.quit(); observerThread = null; }
            return false;
        }
    }

    /** Implemented in media_index_jni.cpp. */
    private static native void nativeOnChange();

    private static String escapeLike(String s) {
        StringBuilder b = new StringBuilder(s.length() + 8);
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch == '%' || ch == '_' || ch == '\\') b.append('\\');
            b.append(ch);
        }
        return b.toString();
    }

    private static String str(Cursor c, int col) {
        if (col < 0 || c.isNull(col)) return "";
        String v = c.getString(col);
        return v == null ? "" : v;
    }

    private static long num(Cursor c, int col) {
        if (col < 0 || c.isNull(col)) return 0;
        return c.getLong(col);
    }
}
