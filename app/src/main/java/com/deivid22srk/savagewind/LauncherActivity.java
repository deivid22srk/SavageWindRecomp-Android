package com.deivid22srk.savagewind;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.FileOutputStream;

import java.util.ArrayList;
import java.util.HashMap;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/**
 * The start screen, shown before any native code runs. Here the player picks the Vulkan driver
 * the game will use - normally a Turnip (Mesa freedreno) driver .zip on an Adreno device.
 *
 * Why this lives in Java instead of the in-game panel: the game's own UI already goes through
 * its Vulkan backend, so on devices whose system driver stops short of Vulkan 1.3 (an Adreno
 * 660 reports Vulkan 1.1) the game dies with "FATAL: no Vulkan 1.3 device" before the in-game
 * panel could ever be reached. The driver choice has to exist before the game launches, from
 * somewhere that never touches Vulkan - hence plain Java UI.
 *
 * The selection lands in files/drivers/active.txt (dir= / lib=), the same file the in-game
 * panel writes; MainActivity.setupDriverEnv exports it as environment variables and libmain's
 * Vulkan shims apply it when the first Vulkan instance is created. Importing, selecting,
 * removing - all of it only takes effect on the next start of the game, which is exactly what
 * the START GAME button is for.
 */
public class LauncherActivity extends Activity {

    private static final int REQ_IMPORT_DRIVER = 43;
    private static final int MAX_ZIP_BYTES = 512 << 20;  // same ceiling the unpacker enforces

    private RadioGroup mRows;         // one radio row per driver + the system driver
    private final HashMap<Integer, DriverInfo> mRowDriver = new HashMap<>();
    private TextView mStatus;
    private Button mImportBtn, mRemoveBtn;
    private File mDriversDir;
    private String mActiveId = "";    // "" = the system driver
    private boolean mImporting;

    // One imported driver package: files/drivers/<id>/ + its meta.txt
    private static class DriverInfo {
        String id, name = "", version = "", author = "", library = "";

        String label() {
            String s = name + (version.isEmpty() ? "" : " " + version);
            if (!author.isEmpty()) s += "  -  " + author;
            return s;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mDriversDir = new File(getFilesDir(), "drivers");
        setContentView(buildUi());
        reload();
    }

    @Override
    protected void onResume() {
        super.onResume();
        reload();  // the in-game driver panel may have changed the selection
    }

    // ------------------------------------------------------------------ UI
    private View buildUi() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(0xFF101216);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(22), dp(28), dp(22), dp(22));
        scroll.addView(root, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView title = new TextView(this);
        title.setText("Savage Wind");
        title.setTextSize(30);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText("Start screen - graphics driver (Vulkan)");
        subtitle.setTextSize(14);
        subtitle.setTextColor(0xFF8B95A1);
        subtitle.setPadding(0, dp(2), 0, dp(14));
        root.addView(subtitle);

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackgroundColor(0xFF1A1F26);
        card.setPadding(dp(16), dp(14), dp(16), dp(14));
        root.addView(card, matchWrapWithTop(dp(2)));

        TextView info = new TextView(this);
        info.setText("The game requires Vulkan 1.3. Many system drivers fall short (an Adreno 6xx "
                + "reports Vulkan 1.1, and the game then stops with \"no Vulkan 1.3 device\"). "
                + "Import a Turnip driver .zip below and select it here - the game picks it up "
                + "when it starts.");
        info.setTextSize(14);
        info.setTextColor(0xFFC7D0D9);
        card.addView(info);

        mStatus = new TextView(this);
        mStatus.setTextSize(13);
        mStatus.setPadding(0, dp(10), 0, dp(4));
        card.addView(mStatus);

        mRows = new RadioGroup(this);
        mRows.setOrientation(LinearLayout.VERTICAL);
        card.addView(mRows);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        mImportBtn = new Button(this);
        mImportBtn.setText("Import driver (.zip)...");
        mImportBtn.setOnClickListener(v -> startImport());
        row.addView(mImportBtn, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        mRemoveBtn = new Button(this);
        mRemoveBtn.setText("Remove");
        mRemoveBtn.setOnClickListener(v -> confirmRemove());
        LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        rp.setMargins(dp(10), 0, 0, 0);
        row.addView(mRemoveBtn, rp);
        root.addView(row, matchWrapWithTop(dp(12)));

        Button start = new Button(this);
        start.setText("START GAME");
        start.setTextSize(18);
        start.setTypeface(Typeface.DEFAULT_BOLD);
        start.setOnClickListener(v -> startActivity(new Intent(this, MainActivity.class)));
        root.addView(start, matchWrapWithTop(dp(18)));

        TextView note = new TextView(this);
        note.setText("Turnip driver .zips are the same packages other Android emulators import "
                + "(meta.json + the driver .so; see the project README). Changing or removing "
                + "the driver applies to the next start. Game files (exefs/ + romfs/) are chosen "
                + "inside the game, as before.");
        note.setTextSize(12);
        note.setTextColor(0xFF77808A);
        note.setPadding(0, dp(14), 0, 0);
        root.addView(note);

        return scroll;
    }

    private LinearLayout.LayoutParams matchWrapWithTop(int topPx) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        p.topMargin = topPx;
        return p;
    }

    private RadioButton radio(String text) {
        RadioButton rb = new RadioButton(this);
        rb.setText(text);
        rb.setTextSize(15);
        rb.setTextColor(0xFFE6EAEE);
        rb.setId(View.generateViewId());
        rb.setPadding(rb.getPaddingLeft(), dp(6), rb.getPaddingRight(), dp(6));
        return rb;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    // ------------------------------------------------------------------ driver list
    private ArrayList<DriverInfo> listDrivers() {
        ArrayList<DriverInfo> out = new ArrayList<>();
        File[] dirs = mDriversDir.listFiles();
        if (dirs != null) {
            for (File d : dirs) {
                if (!d.isDirectory()) continue;
                DriverInfo di = readMeta(new File(d, "meta.txt"));
                if (di != null) {
                    di.id = d.getName();
                    out.add(di);
                }
            }
        }
        return out;
    }

    // meta.txt: the plain key=value form the native driver panel also reads (written by
    // MainActivity.unpackDriverZip from the package's meta.json)
    private static DriverInfo readMeta(File meta) {
        if (!meta.isFile()) return null;
        DriverInfo di = new DriverInfo();
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(meta))) {
            for (String line; (line = r.readLine()) != null; ) {
                int eq = line.indexOf('=');
                if (eq <= 0) continue;
                String k = line.substring(0, eq), v = line.substring(eq + 1).trim();
                switch (k) {
                    case "name":          di.name = v; break;
                    case "driverVersion": di.version = v; break;
                    case "author":        di.author = v; break;
                    case "libraryName":   di.library = v; break;
                }
            }
        } catch (Exception e) {
            return null;
        }
        if (di.name.isEmpty() || di.library.isEmpty()) return null;
        return di;
    }

    private String activeId() {  // "" = the system driver
        File f = new File(mDriversDir, "active.txt");
        if (!f.isFile()) return "";
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(f))) {
            for (String line; (line = r.readLine()) != null; ) {
                if (line.startsWith("dir=")) {
                    String dir = line.substring(4).trim();
                    int slash = dir.lastIndexOf('/');
                    return slash >= 0 ? dir.substring(slash + 1) : dir;
                }
            }
        } catch (Exception ignored) {
        }
        return "";
    }

    private void writeActive(DriverInfo di) {  // null = back to the system driver
        File f = new File(mDriversDir, "active.txt");
        if (di == null) {
            f.delete();
            return;
        }
        try (OutputStream o = new FileOutputStream(f)) {
            o.write(("dir=" + new File(mDriversDir, di.id).getAbsolutePath()
                    + "\nlib=" + di.library + "\n").getBytes("UTF-8"));
        } catch (Exception e) {
            Toast.makeText(this, "Could not save the selection: " + e, Toast.LENGTH_SHORT).show();
        }
    }

    private void reload() {
        mActiveId = activeId();
        ArrayList<DriverInfo> drivers = listDrivers();
        // the recorded selection only counts while the folder still exists
        boolean haveActive = mActiveId.isEmpty();
        for (DriverInfo di : drivers) if (di.id.equals(mActiveId)) haveActive = true;
        if (!haveActive) mActiveId = "";

        mRowDriver.clear();
        mRows.removeAllViews();
        int checkedId = View.NO_ID;

        RadioButton sys = radio("System driver (default)");
        mRows.addView(sys);
        if (mActiveId.isEmpty()) checkedId = sys.getId();

        for (DriverInfo di : drivers) {
            RadioButton rb = radio(di.label());
            mRowDriver.put(rb.getId(), di);
            mRows.addView(rb);
            if (di.id.equals(mActiveId)) checkedId = rb.getId();
        }

        mRows.setOnCheckedChangeListener((g, id) -> {
            DriverInfo di = mRowDriver.get(id);
            mActiveId = di == null ? "" : di.id;
            writeActive(di);  // null removes active.txt -> the system driver
            updateStatus();
        });
        if (checkedId != View.NO_ID) mRows.check(checkedId);
        updateStatus();
    }

    private void updateStatus() {
        String what = "System driver (default)";
        for (DriverInfo di : mRowDriver.values())
            if (di.id.equals(mActiveId)) what = di.label();
        mStatus.setText("For the next start: " + what);
        mStatus.setTextColor(mActiveId.isEmpty() ? 0xFF8B95A1 : 0xFF7DC871);
        mRemoveBtn.setEnabled(!mActiveId.isEmpty());
    }

    // ------------------------------------------------------------------ import (SAF -> validated unpack)
    private void startImport() {
        if (mImporting) return;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/zip", "application/x-zip-compressed", "application/octet-stream"});
        try {
            startActivityForResult(i, REQ_IMPORT_DRIVER);
        } catch (Exception e) {
            Toast.makeText(this, "No file picker available on this device", Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_IMPORT_DRIVER || resultCode != RESULT_OK
                || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        mImporting = true;
        mImportBtn.setEnabled(false);
        Toast.makeText(this, "Importing driver...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            String err = importDriver(uri);
            mImporting = false;
            runOnUiThread(() -> {
                mImportBtn.setEnabled(true);
                Toast.makeText(this,
                        err == null ? "Driver imported and selected" : "The driver package was rejected: " + err,
                        err == null ? Toast.LENGTH_SHORT : Toast.LENGTH_LONG).show();
                reload();
            });
        }, "driver-import").start();
    }

    // Copies the picked .zip to the cache (any document provider works - Downloads, Files,
    // whatever), then runs the same validated unpacker the in-game panel uses (meta.json with
    // libraryName, minApi, the library must be an ELF64 AArch64 image, zip-slip checks, size
    // limits). The imported driver becomes the selection right away.
    private String importDriver(Uri uri) {
        File tmp = new File(getCacheDir(), "driver-import.zip");
        try {
            // folder id: the .zip's display name, sanitized, plus the time so two imports of
            // the same package never overwrite each other (the native panel hashes the path)
            String base = "driver";
            try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
                if (c != null && c.moveToFirst()) {
                    int col = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (col >= 0) {
                        String n = c.getString(col);
                        if (n != null && !n.isEmpty()) base = n;
                    }
                }
            } catch (Exception ignored) {
            }
            if (base.toLowerCase().endsWith(".zip")) base = base.substring(0, base.length() - 4);
            base = base.replaceAll("[^A-Za-z0-9._-]", "_");
            if (base.length() > 40) base = base.substring(0, 40);
            if (base.isEmpty()) base = "driver";
            File dest = new File(mDriversDir, base + "-" + Long.toHexString(System.currentTimeMillis()));

            long size = 0;
            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(tmp)) {
                if (in == null) return "could not read the picked file";
                byte[] buf = new byte[1 << 16];
                for (int n; (n = in.read(buf)) > 0; ) {
                    size += n;
                    if (size > MAX_ZIP_BYTES) return "the .zip is too large";
                    out.write(buf, 0, n);
                }
            }

            MainActivity.deleteTree(dest);
            String err = MainActivity.unpackDriverZip(tmp.getPath(), dest.getPath());
            if (err != null) {
                MainActivity.deleteTree(dest);
                return err;
            }
            DriverInfo di = readMeta(new File(dest, "meta.txt"));
            if (di == null) {  // cannot really happen (the unpacker validated everything)
                MainActivity.deleteTree(dest);
                return "the unpacked package is incomplete";
            }
            di.id = dest.getName();
            writeActive(di);  // imported -> selected for the next start
            mActiveId = di.id;
            return null;
        } catch (Exception e) {
            return e.getMessage() != null ? e.getMessage() : e.toString();
        } finally {
            tmp.delete();
        }
    }

    // ------------------------------------------------------------------ remove
    private void confirmRemove() {
        if (mActiveId.isEmpty()) return;
        DriverInfo found = null;
        for (DriverInfo d : mRowDriver.values()) if (d.id.equals(mActiveId)) found = d;
        if (found == null) return;
        final DriverInfo di = found;  // captured by the dialog's lambda: must be effectively final
        new AlertDialog.Builder(this)
                .setTitle("Remove driver")
                .setMessage("Remove \"" + di.label() + "\" from the app? The game goes back to "
                        + "the system driver the next time it starts.")
                .setPositiveButton("Remove", (d, w) -> {
                    MainActivity.deleteTree(new File(mDriversDir, di.id));
                    if (di.id.equals(activeId())) writeActive(null);
                    reload();
                })
                .setNegativeButton("Cancel", null)
                .show();
    }
}
