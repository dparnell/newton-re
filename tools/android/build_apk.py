#!/usr/bin/env python3
"""Pack newton for an Android tablet into an APK (docs/host-android.md).

Purpose: the Android build's libnewton.so (cmake target newton_android) and the
object file the Newton boots from (target romsrc) into one signed APK, by the
Android SDK's own command line tools - no Gradle: the application is a
NativeActivity (tools/android/AndroidManifest.xml) plus one small Java class,
tools/android/java/.../FastInk.java (the Viwoods fast ink), compiled by javac
and turned into classes.dex by the SDK's d8.

Inputs:  --lib <libnewton.so>          build/android/host/libnewton.so
         --objects <romsrc-objects.bin> build/android/romsrc-objects.bin
         --out <file.apk>              default build/android/newton.apk
         --sdk <dir>                   the Android SDK (default $ANDROID_HOME, else ~/Library/Android/sdk)
         --ndk <dir>                   the NDK, for llvm-strip (default $ANDROID_NDK_HOME, else /opt/homebrew/share/android-ndk)
         --keystore <file>             the signing key (default ~/.android/debug.keystore, made when missing)
Output:  the signed, aligned APK; `adb install -r <file.apk>` puts it on a tablet and
         `adb logcat -s newton` shows what the machine prints.
Needs:   the SDK's build-tools (aapt2, zipalign, apksigner) and platforms/android-28 or later, a JDK
         (javac, keytool, java).

    python3 tools/android/build_apk.py --lib build/android/host/libnewton.so \
        --objects build/android/romsrc-objects.bin
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ABI = "arm64-v8a"


def newest(pattern):
    found = sorted(glob.glob(pattern), key=lambda p: [int(x) if x.isdigit() else x for x in p.replace("-", ".").split(".")])
    if not found:
        sys.exit("nothing matches " + pattern)
    return found[-1]


def run(*cmd):
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lib", default="build/android/host/libnewton.so")
    ap.add_argument("--objects", default="build/android/romsrc-objects.bin")
    ap.add_argument("--out", default="build/android/newton.apk")
    ap.add_argument("--sdk", default=os.environ.get("ANDROID_HOME") or os.path.expanduser("~/Library/Android/sdk"))
    ap.add_argument("--ndk", default=os.environ.get("ANDROID_NDK_HOME") or "/opt/homebrew/share/android-ndk")
    ap.add_argument("--target-sdk", help="targetSdkVersion in place of the manifest's (30: the fast ink wants it)")
    ap.add_argument("--keystore", default=os.path.expanduser("~/.android/debug.keystore"))
    args = ap.parse_args()

    tools = newest(os.path.join(args.sdk, "build-tools", "*"))
    jar = newest(os.path.join(args.sdk, "platforms", "android-*", "android.jar"))
    strip = glob.glob(os.path.join(args.ndk, "toolchains/llvm/prebuilt/*/bin/llvm-strip"))
    if not strip:
        sys.exit("no llvm-strip in the NDK " + args.ndk)

    if not os.path.exists(args.keystore):
        os.makedirs(os.path.dirname(args.keystore), exist_ok=True)
        run("keytool", "-genkeypair", "-keystore", args.keystore, "-storepass", "android", "-keypass", "android",
            "-alias", "androiddebugkey", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
            "-dname", "CN=Android Debug,O=Android,C=US")

    work = tempfile.mkdtemp(prefix="newton-apk-")
    try:
        stripped = os.path.join(work, "libnewton.so")
        run(strip[0], "--strip-unneeded", "-o", stripped, args.lib)
        base = os.path.join(work, "base.apk")
        manifest = os.path.join(HERE, "AndroidManifest.xml")
        if args.target_sdk:
            text = open(manifest).read().replace('android:targetSdkVersion="30"', 'android:targetSdkVersion="%s"' % args.target_sdk)
            text = text.replace('<activity\n', '<activity\n            android:exported="true"\n')
            manifest = os.path.join(work, "AndroidManifest.xml")
            open(manifest, "w").write(text)
        run(os.path.join(tools, "aapt2"), "link", "--manifest", manifest, "-I", jar, "-o", base)
        classes = os.path.join(work, "classes")
        os.makedirs(classes)
        sources = glob.glob(os.path.join(HERE, "java", "**", "*.java"), recursive=True)
        run("javac", "--release", "8", "-Xlint:-options", "-cp", jar, "-d", classes, *sources)
        class_files = glob.glob(os.path.join(classes, "**", "*.class"), recursive=True)
        run(os.path.join(tools, "d8"), "--lib", jar, "--output", work, *class_files)
        unaligned = os.path.join(work, "unaligned.apk")
        with zipfile.ZipFile(base) as src, zipfile.ZipFile(unaligned, "w", zipfile.ZIP_DEFLATED) as dst:
            for item in src.infolist():
                dst.writestr(item, src.read(item.filename))
            dst.write(os.path.join(work, "classes.dex"), "classes.dex")
            dst.write(stripped, "lib/%s/libnewton.so" % ABI)
            dst.write(args.objects, "assets/romsrc-objects.bin")
        aligned = os.path.join(work, "aligned.apk")
        run(os.path.join(tools, "zipalign"), "-f", "-p", "4", unaligned, aligned)
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        run(os.path.join(tools, "apksigner"), "sign", "--ks", args.keystore, "--ks-pass", "pass:android",
            "--key-pass", "pass:android", "--out", args.out, aligned)
        run(os.path.join(tools, "apksigner"), "verify", args.out)
        print("%s: %.1f MB" % (args.out, os.path.getsize(args.out) / 1e6))
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
