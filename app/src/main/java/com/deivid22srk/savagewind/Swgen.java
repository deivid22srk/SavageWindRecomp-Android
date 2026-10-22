package com.deivid22srk.savagewind;

import com.chaquo.python.PyObject;
import com.chaquo.python.Python;

/**
 * Runs the game code generator (tools/gen.py, unchanged from SavageWindRecomp) inside the
 * app through Chaquopy. gamebuild.c starts one per module, exactly like the desktop
 * versions spawn the frozen swgen helper; its ": progress N/M files K" lines land in the
 * same log files, so the original progress loop reads them unchanged.
 */
public final class Swgen {

    private Swgen() {}

    /** Runs gen.py with argv on a worker thread; reports back through nativeSwgenDone. */
    public static void runAsync(final int handle, final String[] args, final String logPath) {
        new Thread(() -> {
            int exit = 1;
            try {
                if (!Python.isStarted()) Python.start(new AndroidPlatform(MainActivity.sContext));
                PyObject runner = Python.getInstance().getModule("gen_runner");
                PyObject r = runner.callAttr("run", args, logPath);
                exit = r.toInt();
            } catch (Throwable t) {
                try {
                    java.io.FileOutputStream out = new java.io.FileOutputStream(logPath, true);
                    out.write(("\ngen_runner: " + t + "\n").getBytes("UTF-8"));
                    java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream();
                    t.printStackTrace(new java.io.PrintStream(bo, false, "UTF-8"));
                    out.write(bo.toByteArray());
                    out.close();
                } catch (Exception ignored) {
                }
            }
            MainActivity.nativeSwgenDone(handle, exit);
        }, "swgen").start();
    }
}
