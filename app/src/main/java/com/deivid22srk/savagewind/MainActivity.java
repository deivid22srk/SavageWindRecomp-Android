package com.deivid22srk.savagewind;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.system.Os;
import android.text.method.ScrollingMovementMethod;
import android.widget.TextView;
import android.widget.Toast;

import com.chaquo.python.android.AndroidPlatform;
import com.chaquo.python.Python;

import org.libsdl.app.SDLActivity;

import org.json.JSONObject;

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
    private static File sBootLog;

    // ------------------------------------------------------------------ restart-loop guard
    // A native death during the libraries' load leaves nothing in the log the app could catch:
    // the process is simply gone, and the system starts it again. If boot fails repeatedly and
    // fast, stop relaunching and show what the boot log recorded instead.
    private static final int FAILS_BEFORE_DIAGNOSTIC = 3;
    private static final long FAIL_WINDOW_MS = 10000;
    private int mBootFails;

    private void noteBootAttempt() {
        SharedPreferences p = getSharedPreferences("sw_boot", MODE_PRIVATE);
        long now = SystemClock.elapsedRealtime();
        long last = p.getLong("fail_t", -FAIL_WINDOW_MS);
        mBootFails = (now - last) < FAIL_WINDOW_MS ? p.getInt("fail_n", 0) + 1 : 1;
        p.edit().putLong("fail_t", now).putInt("fail_n", mBootFails).apply();
    }

    private void clearBootFails() {
        mBootFails = 0;
        getSharedPreferences("sw_boot", MODE_PRIVATE).edit().remove("fail_t").remove("fail_n").apply();
    }

    // The diagnostic screen: what the boot log (and the runtime's own savagewind.log, when it got
    // far enough) recorded, with a copy button. No relaunch loop past this point.
    private void showBootDiagnostic() {
        StringBuilder sb = new StringBuilder();
        sb.append(readTail(sBootLog, 64));
        File swlog = new File(getExternalFilesDir(null), "savagewind.log");
        if (swlog.isFile()) sb.append("\n---- savagewind.log ----\n").append(readTail(swlog, 64));
        String text = sb.length() > 0 ? sb.toString() : "(the boot log is empty)";
        bootLog("loop: " + mBootFails + " failed starts in a row - showing diagnostics instead of restarting");
        runOnUiThread(() -> {
            TextView tv = new TextView(this);
            tv.setText(text);
            tv.setTextIsSelectable(true);
            tv.setMovementMethod(new ScrollingMovementMethod());
            tv.setTextSize(12f);
            tv.setPadding(48, 32, 48, 32);
            new AlertDialog.Builder(this)
                    .setTitle("Savage Wind won't start")
                    .setMessage("The app failed " + mBootFails + " times in a row. Below is what was recorded "
                            + "before each failure (bootlog.txt).")
                    .setView(tv)
                    .setPositiveButton("Copy", (d, w) -> {
                        ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                        cm.setPrimaryClip(ClipData.newPlainText("Savage Wind boot log", text));
                        Toast.makeText(this, "Log copied", Toast.LENGTH_SHORT).show();
                    })
                    .setNeutralButton("Try again", (d, w) -> {
                        clearBootFails();
                        recreate();
                    })
                    .setNegativeButton("Close", (d, w) -> finish())
                    .setCancelable(false)
                    .show();
        });
    }

    private static String readTail(File f, int maxLines) {
        if (f == null || !f.isFile()) return "";
        try (InputStream in = new FileInputStream(f)) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[1 << 16];
            int n, total = 0;
            while ((n = in.read(buf)) > 0 && total < (1 << 20)) { out.write(buf, 0, n); total += n; }
            String[] lines = out.toString("UTF-8").split("\n");
            StringBuilder sb = new StringBuilder();
            int from = Math.max(0, lines.length - maxLines);
            for (int i = from; i < lines.length; i++) sb.append(lines[i]).append('\n');
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }

    static {
        // The libraries themselves are loaded by SDLActivity (getLibraries(): bootlog, SDL3, main).
    }

    // ------------------------------------------------------------------ boot log
    // Every startup step lands here (and in logcat "swboot"), so a failure during boot is
    // visible in the user's own log capture. libbootlog.so appends native crashes to the
    // same file through SAVAGEWIND_CRASHLOG.
    private static void bootLog(String msg) {
        android.util.Log.i("swboot", msg);
        if (sBootLog == null) return;
        try (FileOutputStream o = new FileOutputStream(sBootLog, true)) {
            o.write((System.currentTimeMillis() + " " + msg + "\n").getBytes("UTF-8"));
        } catch (Exception ignored) {
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        sContext = this;  // pickFolder/pickFile (called from native threads) and Swgen need this
        noteBootAttempt();
        File root = getExternalFilesDir(null);
        if (root == null) root = getFilesDir();
        sBootLog = new File(root, "bootlog.txt");
        bootLog("app: onCreate begin (attempt " + mBootFails + ")");
        if (mBootFails >= FAILS_BEFORE_DIAGNOSTIC) {
            showBootDiagnostic();
            return;  // no super.onCreate: the restart loop stops here
        }
        try {
            // Read before the SDL thread starts: the runtime's paths.c picks the data root
            // up from SAVAGEWIND_DATA_ROOT. The compiler is the clang shipped inside the APK
            // (libclang.so in nativeLibraryDir), found through SAVAGEWIND_CC like on the desktop;
            // ld.lld is reached through a symlink in the PATH, since only nativeLibraryDir is
            // executable and the linker's name must be ld.lld for clang's -fuse-ld=lld.
            // (The symlink must live on internal storage: /storage's FUSE rejects symlink(2).)
            String libdir = getApplicationInfo().nativeLibraryDir;
            Os.setenv("SAVAGEWIND_DATA_ROOT", root.getAbsolutePath(), true);
            Os.setenv("SAVAGEWIND_CRASHLOG", sBootLog.getAbsolutePath(), true);
            Os.setenv("SAVAGEWIND_LIB_DIR", libdir, true);
            Os.setenv("SAVAGEWIND_CC", libdir + "/libclang.so", true);
            Os.setenv("SAVAGEWIND_FILES_DIR", getFilesDir().getAbsolutePath(), true);
            File bin = new File(getFilesDir(), "bin");
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
            bootLog("env: data_root=" + root + " libdir=" + libdir + " bin=" + bin
                    + " ld.lld->" + (new File(bin, "ld.lld").exists() ? "ok" : "missing"));
            setupDriverEnv();
        } catch (Exception e) {
            bootLog("env: setup FAILED: " + e);
        }

        copyAssets(root);
        bootLog("assets: copied");
        try {
            Python.start(new AndroidPlatform(getApplicationContext()));
            bootLog("python: started");
        } catch (Throwable t) {
            bootLog("python: start FAILED: " + t);
            throw t;
        }
        ensureStorageAccess();
        bootLog("storage: managed=" + Environment.isExternalStorageManager());
        bootLog("sdl: entering SDLActivity.onCreate (loads bootlog, SDL3, main)");
        super.onCreate(savedInstanceState);
        bootLog("sdl: SDLActivity.onCreate returned");
        clearBootFails();  // the risky part (native library load) is behind us: reset the loop guard
        nativeSetContext(this);
        bootLog("native: context set");
    }

    // ------------------------------------------------------------------ graphics driver (AdrenoTools)
    // The Turnip driver has to be in place before anything touches Vulkan, and the choice is a
    // plain file the start screen's driver panel writes (files/drivers/active.txt: dir= and lib=).
    // Whatever is selected here is exported as environment variables; libmain's Vulkan shims read
    // them the first time the runtime creates a Vulkan instance.
    private void setupDriverEnv() {
        try {
            File active = new File(new File(getFilesDir(), "drivers"), "active.txt");
            if (!active.isFile()) return;
            String dir = null, lib = null;
            try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(active))) {
                for (String line; (line = r.readLine()) != null; ) {
                    if (line.startsWith("dir=")) dir = line.substring(4).trim();
                    else if (line.startsWith("lib=")) lib = line.substring(4).trim();
                }
            }
            if (dir == null || lib == null || dir.isEmpty() || lib.isEmpty()) {
                bootLog("driver: active.txt is incomplete - using the system driver");
                return;
            }
            File dirF = new File(dir), libF = new File(dirF, lib);
            if (!dirF.isDirectory() || !libF.isFile()) {  // the driver was removed under us
                bootLog("driver: " + dir + " is gone - falling back to the system driver");
                active.delete();
                return;
            }
            Os.setenv("SAVAGEWIND_VK_DRIVER_DIR", dirF.getAbsolutePath() + "/", true);
            Os.setenv("SAVAGEWIND_VK_DRIVER_LIB", lib, true);
            bootLog("driver: custom Vulkan driver for this run: " + dir + " (" + lib + ")");
        } catch (Exception e) {
            bootLog("driver: setup FAILED: " + e);
        }
    }

    // Called from libmain (cpp/android/android_bridge.c): validate a Turnip driver .zip chosen in
    // the driver panel and unpack it into destDir. Returns null on success, else the reason.
    // Limits and checks: entry count and sizes, zip-slip (.. / absolute / .. paths), meta.json
    // with the usual driver-package fields, and the library itself must be a 64-bit AArch64 ELF.
    public static String unpackDriverZip(String zipPath, String destDir) {
        final int MAX_ENTRIES = 200, MAX_TOTAL = 512 << 20, MAX_FILE = 400 << 20;
        File dest = new File(destDir);
        try {
            String meta = null;
            long total = 0;
            int entries = 0;
            try (ZipInputStream z = new ZipInputStream(new FileInputStream(zipPath))) {
                for (ZipEntry e; (e = z.getNextEntry()) != null; ) {
                    if (++entries > MAX_ENTRIES) return "the .zip has too many files";
                    String name = e.getName();
                    if (name == null || name.isEmpty()) continue;
                    if (name.contains("..") || name.startsWith("/") || name.contains("\\"))
                        return "unsafe path inside the .zip: " + name;
                    File out = new File(dest, name);
                    if (!out.getCanonicalPath().startsWith(dest.getCanonicalPath() + File.separator))
                        return "unsafe path inside the .zip: " + name;
                    if (e.isDirectory()) { out.mkdirs(); continue; }
                    out.getParentFile().mkdirs();
                    long size = 0;
                    try (OutputStream o = new FileOutputStream(out)) {
                        byte[] buf = new byte[1 << 16];
                        for (int n; (n = z.read(buf)) > 0; ) {
                            size += n;
                            total += n;
                            if (size > MAX_FILE || total > MAX_TOTAL) return "the .zip is too large";
                            o.write(buf, 0, n);
                        }
                    }
                    if (name.equals("meta.json")) meta = out.getPath();
                }
            }
            if (meta == null) return "the .zip has no meta.json";
            String metaText = new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(meta)), "UTF-8");
            JSONObject m = new JSONObject(metaText);
            String lib = m.optString("libraryName", "");
            if (lib.isEmpty()) return "meta.json has no libraryName";
            if (lib.contains("/") || lib.contains("..")) return "bad libraryName: " + lib;
            File libF = new File(dest, lib);
            if (!libF.isFile()) return "the driver library " + lib + " is not in the .zip";
            int minApi = m.optInt("minApi", 0);
            if (minApi > android.os.Build.VERSION.SDK_INT)
                return "this driver needs Android " + minApi + "+";
            // The library must be an ELF64 AArch64 image (e_ident class 2, little-endian, EM_AARCH64).
            try (InputStream in = new FileInputStream(libF)) {
                byte[] h = new byte[20];
                if (in.read(h) != h.length) return lib + " is not a valid ELF";
                if (h[0] != 0x7f || h[1] != 'E' || h[2] != 'L' || h[3] != 'F') return lib + " is not an ELF";
                if (h[4] != 2) return lib + " is not a 64-bit ELF";
                if (h[5] != 1) return lib + " is not a little-endian ELF";
                int machine = (h[19] & 0xff) << 8 | (h[18] & 0xff);
                if (machine != 183) return lib + " is not an AArch64 ELF (this app only supports arm64 devices)";
            }
            // meta.txt: the plain key=value form the native driver panel reads (no JSON parser there)
            try (OutputStream o = new FileOutputStream(new File(dest, "meta.txt"))) {
                String[] keys = {"name", "description", "author", "vendor", "driverVersion", "libraryName"};
                StringBuilder sb = new StringBuilder();
                for (String k : keys) {
                    String v = m.optString(k, "");
                    if (v.indexOf('\n') >= 0) v = v.replace('\n', ' ');
                    sb.append(k).append('=').append(v).append('\n');
                }
                o.write(sb.toString().getBytes("UTF-8"));
            }
            return null;
        } catch (Exception e) {
            deleteTree(dest);
            return e.getMessage() != null ? e.getMessage() : e.toString();
        }
    }

    static boolean deleteTree(File f) {
        if (f == null) return false;
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        return f.delete();
    }

    // libbootlog first: it installs the signal handlers that record a native death while
    // dlopen'ing the libraries below (invisible to uid-filtered log captures otherwise).
    @Override
    protected String[] getLibraries() {
        return new String[] { "bootlog", "SDL3", "main" };
    }

    @Override
    public void loadLibraries() {
        for (String lib : getLibraries()) {
            bootLog("native: loading lib" + lib + ".so ...");
            try {
                System.loadLibrary(lib);
            } catch (Throwable t) {
                bootLog("native: lib" + lib + ".so FAILED: " + t);
                throw t;  // SDLActivity catches this and shows its error dialog
            }
            bootLog("native: lib" + lib + ".so loaded");
        }
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
        if (a == null) {  // native thread got here before onCreate finished: answer "cancelled" instead of dying
            nativeOnPicked(REQ_PICK_FOLDER, null);
            return;
        }
        a.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            a.startActivityForResult(i, REQ_PICK_FOLDER);
        });
    }

    public static void pickFile() {
        final MainActivity a = (MainActivity) sContext;
        if (a == null) {
            nativeOnPicked(REQ_PICK_FILE, null);
            return;
        }
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
