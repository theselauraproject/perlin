package io.perlin.client;

import android.Manifest;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Environment;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

final class PerlinStorage {
    static final String PREFS = "perlin_storage";
    static final String KEY_ASKED = "asked";
    static final String KEY_DIR = "data_dir";
    static final String MOVED_MARKER = ".perlin-moved-here";

    interface Progress {
        void update(long done, long total);
    }

    private PerlinStorage() {}

    static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    static File sharedDir() {
        return new File(new File(Environment.getExternalStorageDirectory(), "games"), "Perlin");
    }

    static File privateDir(Context context) {
        File external = context.getExternalFilesDir(null);
        return new File(external != null ? external : context.getFilesDir(), "Perlin");
    }

    static boolean sharedAccess(Context context) {
        if (Build.VERSION.SDK_INT >= 30)
            return Environment.isExternalStorageManager();
        return context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    static boolean sharedUsable(Context context) {
        if (!sharedAccess(context))
            return false;
        File dir = sharedDir();
        return (dir.isDirectory() || dir.mkdirs()) && dir.canWrite();
    }

    static File chosenDir(Context context) {
        return sharedDir();
    }

    static boolean needsMove(Context context) {
        if (!sharedUsable(context))
            return false;
        return !sharedDir().getAbsolutePath().equals(prefs(context).getString(KEY_DIR, null))
                && hasContent(privateDir(context));
    }

    static boolean hasContent(File dir) {
        String[] names = dir.list();
        return names != null && names.length > 0;
    }

    static long size(File f) {
        if (f.isFile())
            return f.length();
        long total = 0;
        File[] children = f.listFiles();
        if (children != null)
            for (File c : children)
                total += size(c);
        return total;
    }

    static void move(File from, File to, Progress progress) throws IOException {
        long total = size(from);
        long[] done = {0};
        copyTree(from, to, total, done, progress);
        new File(to, MOVED_MARKER).createNewFile();
        deleteTree(from);
    }

    private static void copyTree(File from, File to, long total, long[] done, Progress progress) throws IOException {
        if (from.isDirectory()) {
            if (!to.isDirectory() && !to.mkdirs())
                throw new IOException("cannot create " + to);
            File[] children = from.listFiles();
            if (children != null)
                for (File c : children)
                    copyTree(c, new File(to, c.getName()), total, done, progress);
            return;
        }
        if (to.exists() && to.length() == from.length() && to.lastModified() >= from.lastModified()) {
            done[0] += from.length();
            progress.update(done[0], total);
            return;
        }
        byte[] buffer = new byte[1 << 18];
        try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(to)) {
            int n;
            while ((n = in.read(buffer)) > 0) {
                out.write(buffer, 0, n);
                done[0] += n;
                progress.update(done[0], total);
            }
        }
        to.setLastModified(from.lastModified());
    }

    static void deleteTree(File f) {
        File[] children = f.listFiles();
        if (children != null)
            for (File c : children)
                deleteTree(c);
        f.delete();
    }
}
