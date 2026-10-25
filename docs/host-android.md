# An Android host (the Viwoods AiPaper Mini)

Status (2026-10-25): **runs on the Viwoods AiPaper Mini and the Supernote Manta**
(the owner's report: fast on both, the Manta a little faster).  Not yet done:
keyboard, sound, pressure and the eraser, palm rejection ("Not yet verified").

The target is the Viwoods AiPaper Mini: an 8.2" black-and-white e-ink tablet,
1920x1440, Android 13, an octa-core 2 GHz SoC, 4 GB, an EMR stylus (specs from
the reviews at parkablogs.com and the Galaxus listing; the stylus model's name
differs between them).

## Build

NDK r27 or later (Homebrew's `android-ndk` cask; r20 is too old for this
code), the Android SDK's build-tools and a JDK:

    cmake -G Ninja -S src -B build/android -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$PWD/src/cmake/android-ndk.cmake \
        -DNEWTON_HOST_NEWTONSCRIPT=$PWD/build/host/host/newtonscript
    cmake --build build/android --target newton_android romsrc
    python3 tools/android/build_apk.py
    adb install -r build/android/newton.apk
    adb logcat -s newton

- `src/cmake/android-ndk.cmake` wraps the NDK's toolchain file (arm64-v8a,
  android-26, a static libc++, `char` kept signed as on the other hosts).
- `NEWTON_HOST_NEWTONSCRIPT` is a `newtonscript` that runs on the build
  machine: the object file (`romsrc-objects.bin`) is compiled by it, as in the
  reMarkable cross build.
- `newton` (the executable) builds too and runs headless under `adb shell`;
  `newton_android` is the same program as `libnewton.so`, a NativeActivity's
  library.
- `tools/android/build_apk.py` packs `libnewton.so`, the object file (as an
  asset) and `tools/android/AndroidManifest.xml` into a signed APK with the
  SDK's own tools (aapt2, zipalign, apksigner), no Gradle.  It makes
  `~/.android/debug.keystore` when there is none.

## How it runs

- `host/android/AndroidMain.cpp`: `android_main` copies the object file out of
  the assets into the app's private directory, sends stdout and stderr to
  logcat, runs newton's `main` on a thread of its own (16 MB stack) with
  `--objects ... --store <dir>/Internal.store`, and pumps the activity's events.
- `host/android/HostWindow.cpp`: the host window (`host/HostWindow.h`) on the
  activity's `ANativeWindow`.  A render thread looks thirty times a second and
  draws only when the display changed (an e-ink panel should not be redrawn
  for nothing); each display pixel is a whole number of screen pixels,
  centred, the rest white (the 320x480 display is shown 4x on 1440x1920).  The
  stylus, a finger or a mouse is the pen, in eighths of a display pixel, with
  the stroke's history between events; the eraser end is ignored.

## Fast ink

- **Supernote Manta**: `host/android/EinkDirect.cpp` sends changed regions
  straight to the e-ink driver `/dev/ebc` (the interface of MPdairy's
  fasteink library); only when the driver reports the Manta's exact layout.
  Confirmed by the owner: writing is fast.  `NEWTON_EINK_DIRECT=0` turns it off.
- **Viwoods AiPaper Mini**: the hidden `android.os.enote.ENoteSetting` is
  called through java reflection *from native code* (`HostWindow.cpp`,
  InkOpen/InkMode), so the APK has no Java and Play Protect does not warn (a
  `classes.dex` made it block the install).  The window is drawn as usual; the
  panel's picture mode is FAST (4) while the pen is down, GL16 (3) to read, and
  one GC (17) full refresh 1.2 s after the pen rests, to clear the ghosts.
  `initWriting()` also switches on the system's own instant pen stroke
  (jdkruzr/ViwoodsAppDev documents it); it is set to a pen of narrow width
  (`setAutoDrawToolType(2)`, `setAutoDrawPenWidthRange(2,5)`, a guess in the
  API's units; `NEWTON_PEN_WIDTH=min,max`) so that it matches the Newton's ink -
  with its default thick stroke writing felt slow.  The app cannot reach the
  panel driver itself (a MediaTek board; /dev is closed to apps - `build_apk.py
  --diag` makes an APK that shows what an app can open).  Tried and dropped: the
  writing overlay Bitmap (`renderWriting`) - slow, ghosts; a Java class
  (`classes.dex`) - Play Protect blocks the install.
  Confirmed by the owner: much faster (as a dex build).  The writing overlay
  of the same API (a Bitmap drawn into and `renderWriting`) was tried first:
  unusable, bad ghosting.  `NEWTON_FAST_INK=0` turns it off.

## Not yet verified / left to do

- Running on the tablet at all.  The tablet's adbd refuses `shell`, `push`,
  `logcat` and `install` ("not support command ..."), so the APK has to be
  copied over (file transfer or a browser download) and installed from the
  tablet's file manager, with no logcat.
- **E-ink refresh**: the window is drawn as an ordinary Android surface.
  Whether the tablet's UI does partial refresh for it, how much it ghosts and
  whether Viwoods has a refresh API or mode for apps are unknown.
- Keyboard (none), sound (none), pressure and the eraser, palm rejection, the
  display size (320x480 is small on 1920x1440: `--display` could be larger),
  landscape (the manifest locks portrait), an app icon, packages by the file
  picker, and suspending the machine when the app is in the background.
- targetSdk is 30: reports on Viwoods' fast-ink mode say the app must run in
  the `untrusted_app_30` SELinux context to use it.  Fast ink is
  `android.os.enote.ENoteSetting`, a hidden API called by reflection
  (`setPictureMode`: 4 fast, 3 GL16, 17 full refresh), so it needs a small
  Java (dex) part; the APK is pure native (`hasCode="false"`) for now.
