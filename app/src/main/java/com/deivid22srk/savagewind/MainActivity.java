package com.deivid22srk.savagewind;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import android.app.AlertDialog;
import android.content.Intent;
import android.content.Context;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.system.Os;
import android.widget.Toast;

import com.chaquo.python.AndroidPlatform;
import com.chaquo.python.Python;

import org.libsdl.app.SDLActivity;

/**
 * Savage Wind (Android port of SavageWindRecomp).
 *
 * Runs the original C runtime through SDL3's SDLActivity. The player's own game files
 * (exefs/ + romfs/, extracted from their own copy of the game) are picked with the system
 * folder picker and read from wherever they already are - nothing is decrypted, no keys
 * are needed, and no game data ships with the APK.
 */
public class MainActivity extends SDLActivity {

    private static final int REQ_PICK_FOLDER = 41;
    private static final int REQ_PICK_FILE = 42;

    static Context sContext;  // package-private: Swgen reads it

    static {
        // The libraries themselves are loaded by SDLActivity (getLibraries(): SDL3, main).
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File root = getExternalFilesDir(null);
        if (root == null) root = getFilesDir();
        try {
            // Read before the SDL thread starts: the runtime's paths.c picks the data root
            // up from SAVAGEWIND_DATA_ROOT. The compiler is the clang shipped inside the APK
            // (libclang.so in nativeLibraryDir), found through SAVAGEWIND_CC like on the desktop;
            // ld.lld is reached through a symlink in the PATH, since only nativeLibraryDir is
            // executable and the linker's name must be ld.lld for clang's -fuse-ld=lld.
            Os.setenv("SAVAGEWIND_DATA_ROOT", root.getAbsolutePath(), true);
            String libdir = getApplicationInfo().nativeLibraryDir;
            Os.setenv("SAVAGEWIND_LIB_DIR", libdir, true);
            Os.setenv("SAVAGEWIND_CC", libdir + "/libclang.so", true);
            File bin = new File(root, "bin");
            bin.mkdirs();
            try {
                Os.symlink(libdir + "/libld.lld.so", new File(bin, "ld.lld").getAbsolutePath());
            } catch (Exception ignored) {  // exists from an earlier run
            }
            String path = System.getenv("PATH");
            Os.setenv("PATH", bin.getAbsolutePath() + ":" + (path != null ? path : ""), true);
            Os.setenv("LD_LIBRARY_PATH", libdir, true);
            Os.setenv("HOME", root.getAbsolutePath(), true);
            Os.setenv("TMPDIR", getCacheDir().getAbsolutePath(), true);
        } catch (Exception ignored) {
        }

        copyAssets(root);
        Python.start(new AndroidPlatform(getApplicationContext()));
        ensureStorageAccess();
        super.onCreate(savedInstanceState);
        nativeSetContext(this);
    }

    // ------------------------------------------------------------------ storage permission
    // The setup copies from the folder the player chose with the system picker. Reading it
    // needs All files access (a real filesystem path is resolved from the picker result).
    private void ensureStorageAccess() {
        if (Environment.isExternalStorageManager()) return;
        try {
            Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                  Uri.parse("package:" + getPackageName()));
            startActivity(i);
        } catch (Exception e) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (Exception ignored) {
            }
        }
    }

    // ------------------------------------------------------------------ assets -> data root
    // Shipped resources (fonts, icon, the game-code build headers, TCC's runtime) unpack
    // into the data root, where the runtime's rt_resource() finds them.
    private void copyAssets(File root) {
        copyTree("gamelib-src", new File(root, "gamelib-src"));
        // clang's freestanding builtin headers (CI-extracted) land in the same folder the
        // on-device compile gets via -I, next to the runtime's own headers and the shims.
        copyTree("clang-include", new File(root, "gamelib-src"));
        copyTree("fonts", new File(root, "fonts"));
        copyOne("icon.png", new File(root, "icon.png"));
    }

    private void copyTree(String assetDir, File targetDir) {
        try {
            String[] names = getAssets().list(assetDir);
            if (names == null) return;
            targetDir.mkdirs();
            for (String name : names) {
                File f = new File(targetDir, name);
                if (names.length > 0 && assetDir.length() > 0) {
                    String[] sub = getAssets().list(assetDir + "/" + name);
                    if (sub != null && sub.length > 0) {
                        copyTree(assetDir + "/" + name, f);
                        continue;
                    }
                }
                copyOne(assetDir + "/" + name, f);
            }
        } catch (IOException ignored) {
        }
    }

    private void copyOne(String assetPath, File target) {
        try {
            if (target.isFile()) {
                long existing = target.length();
                long inApk = -1;
                try (InputStream in = getAssets().open(assetPath)) {
                    inApk = in.available();
                } catch (IOException ignored) {
                }
                if (inApk >= 0 && existing == inApk) return;  // same size: keep the copy
            }
            target.getParentFile().mkdirs();
            try (InputStream in = getAssets().open(assetPath);
                 OutputStream out = new FileOutputStream(target)) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
        } catch (IOException ignored) {
        }
    }

    // ------------------------------------------------------------------ file / folder picking
    // Called from native (runtime/platform/android.c) when the start screen asks for a
    // folder. The picker result is turned into a real filesystem path when it lives on the
    // primary storage, so the original C setup code can read it directly.
    public static void pickFolder() {
        final MainActivity a = (MainActivity) sContext;
        a.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            a.startActivityForResult(i, REQ_PICK_FOLDER);
        });
    }

    public static void pickFile() {
        final MainActivity a = (MainActivity) sContext;
        a.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            a.startActivityForResult(i, REQ_PICK_FILE);
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        String path = null;
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            path = realPath(data.getData());
            if (path == null) {
                Toast.makeText(this,
                    "Only folders on the phone's internal storage are supported (move the game files there).",
                    Toast.LENGTH_LONG).show();
            }
        }
        nativeOnPicked(requestCode, path);
    }

    // SAF tree/document URI -> real path, for the primary storage only.
    private String realPath(Uri uri) {
        try {
            String docId;
            if ("com.android.externalstorage.documents".equals(uri.getAuthority())) {
                docId = "tree".equals(uri.getPathSegments().get(0))
                        ? DocumentsContract.getTreeDocumentId(uri)
                        : DocumentsContract.getDocumentId(uri);
            } else {
                return null;
            }
            if (!docId.startsWith("primary:")) return null;
            String rel = docId.substring("primary:".length());
            File base = Environment.getExternalStorageDirectory();
            return rel.isEmpty() ? base.getAbsolutePath() : new File(base, rel).getAbsolutePath();
        } catch (Exception e) {
            return null;
        }
    }

    // ------------------------------------------------------------------ JNI (libmain.so)
    static native void nativeSetContext(Context context);
    static native void nativeOnPicked(int requestCode, String path);
    static native void nativeSwgenDone(int handle, int exitCode);
}
