package com.matthiasm.newtonre;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Rect;

import java.lang.reflect.Method;

/**
 * The Viwoods AiPaper Mini's fast-ink overlay (android.os.enote.ENoteSetting, a hidden API
 * called by reflection - docs/host-android.md).  Called from native code
 * (host/android/HostWindow.cpp) only; every call is harmless on a device without the API.
 *
 * A stroke is: begin(), then the display's changed rectangles drawn by native code into
 * bitmap() and sent with render(), then end().
 */
public final class FastInk {
    private static Object enote;
    private static Class<?> type;
    private static Bitmap bitmap;
    private static boolean on;

    private FastInk() {}

    private static Method find(String name, int args) throws NoSuchMethodException {
        for (Method m : type.getMethods())
            if (m.getName().equals(name) && m.getParameterTypes().length == args)
                return m;
        throw new NoSuchMethodException(name);
    }

    private static void call(String name, Object... args) throws Exception {
        find(name, args.length).invoke(enote, args);
    }

    /** True when the overlay is there and set up. */
    public static synchronized boolean init(Activity activity) {
        try {
            type = Class.forName("android.os.enote.ENoteSetting");
            enote = type.getMethod("getInstance").invoke(null);
            call("setApplicationContext", activity.getApplicationContext());
            on = false;
            resume();
            return on;
        } catch (Throwable t) {
            enote = null;
            return false;
        }
    }

    /** The bitmap the overlay shows, the size of the screen. */
    public static synchronized Bitmap bitmap(int width, int height) {
        if (bitmap == null || bitmap.getWidth() != width || bitmap.getHeight() != height)
            bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        return bitmap;
    }

    public static synchronized void begin() {
        if (!on || bitmap == null) return;
        try {
            call("setPictureMode", 4);      // FAST while the pen is down
            call("setWritingJavaBitmap", bitmap, 0, 0, 0);
            call("onWritingStart");
        } catch (Throwable t) { on = false; }
    }

    public static synchronized void render(int left, int top, int right, int bottom) {
        if (!on) return;
        try { call("renderWriting", new Rect(left, top, right, bottom)); }
        catch (Throwable t) { on = false; }
    }

    public static synchronized void end() {
        if (!on) return;
        try { call("onWritingEnd"); call("setPictureMode", 3); } catch (Throwable t) { on = false; }
    }

    /** The activity was paused: the system's writing queue is not ours while in the background. */
    public static synchronized void pause() {
        if (enote == null || !on) return;
        try { call("setWritingEnabled", Boolean.FALSE); call("setPictureMode", 3); } catch (Throwable t) {}
        on = false;
    }

    public static synchronized void resume() {
        if (enote == null || on) return;
        try {
            call("initWriting");
            call("setPictureMode", 3);      // GL16 for reading
            call("setRenderWritingDelayCount", 0);
            call("setWritingEnabled", Boolean.TRUE);
            on = true;
        } catch (Throwable t) { on = false; }
    }
}
