package com.matthiasm.newtonre;

import android.app.Activity;

import java.lang.reflect.Method;

/**
 * The Viwoods AiPaper Mini's refresh modes (android.os.enote.ENoteSetting, a hidden API called
 * by reflection - docs/host-android.md).  Called from native code (host/android/HostWindow.cpp)
 * only; every call is harmless on a device without the API.  The window is drawn as usual: this
 * only says how the panel refreshes it - FAST (4) while the pen is down, GL16 (3) to read, GC
 * (17) to clear ghosts.  (The writing overlay of the same API was tried and ghosted badly.)
 */
public final class FastInk {
    private static Object enote;
    private static Class<?> type;

    private FastInk() {}

    private static void call(String name, Object... args) throws Exception {
        for (Method m : type.getMethods())
            if (m.getName().equals(name) && m.getParameterTypes().length == args.length) {
                m.invoke(enote, args);
                return;
            }
        throw new NoSuchMethodException(name);
    }

    /** True when the API is there. */
    public static synchronized boolean init(Activity activity) {
        try {
            type = Class.forName("android.os.enote.ENoteSetting");
            enote = type.getMethod("getInstance").invoke(null);
            call("setApplicationContext", activity.getApplicationContext());
            call("initWriting");
            call("setPictureMode", 3);
            return true;
        } catch (Throwable t) {
            enote = null;
            return false;
        }
    }

    /** 4 FAST, 3 GL16, 17 GC. */
    public static synchronized void mode(int mode) {
        if (enote == null) return;
        try { call("setPictureMode", mode); } catch (Throwable t) { enote = null; }
    }
}
