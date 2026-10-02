# A reMarkable Paper Pro host

The owner has a reMarkable Paper Pro and running Newton OS on it has been a
goal of the project from early on.  This page is the study and the record of
the port: what the tablet is, how a third-party program gets its screen and
its pen, why the port goes through AppLoad's *qtfb* first and rmkit second,
the build, what was done (on the branch `rmpp`), what the real tablet has
already shown, and what is left.

Status (2026-10-02): **newton runs on the Paper Pro.**  Cross-compiled on
the Windows development machine, copied over SSH and run headless, it boots
to the Setup assistant in about three seconds and its screen is
*byte-for-byte* the Windows build's.  The display backend
(`src/host/remarkable/`) shows the Newton's screen through AppLoad's qtfb and
takes the pen back; it has been run end to end against a stand-in for
AppLoad under qemu (Welcome shown at 4x, the Continue button tapped through
the qtfb pen events, the calibration screen shown), and the rmkit variant the
same way through AppLoad's own qtfb-shim.  The on-glass test needs xovi
activated on the tablet, which restarts its UI - "On the device" below.

## The tablet (as measured on the owner's, 2026-10-02)

`tools/remarkable/rmprobe.c` (built by `build_probe.py`, copied to
`/home/root/newton/` and run over SSH) printed this; the published specs
agree where they overlap.

| | |
|---|---|
| SoC | NXP i.MX 8M Mini, four Cortex-A53 at up to 1.8 GHz (`compatible: fsl,imx8mm`, model `reMarkable Ferrari` - *ferrari* is the Paper Pro's code name, *chiappa* the Paper Pro Move's) |
| RAM / storage | 2 GB (1.6 GB available with the UI running); 33 GB free on `/home`, `/tmp` and `/dev/shm` 981 MB tmpfs |
| OS | Codex Linux 5.5.125 (Yocto *scarthgap*), reMarkable software 3.25.1.1, kernel 6.12.34, **glibc 2.39**, BusyBox userland (no `timeout`; `head -n N`) |
| `char` | unsigned (aarch64 Linux's ABI) |
| display | 11.8" Canvas Color (E Ink Gallery 3), 1620 x 2160 at 229 dpi.  **No `/dev/fb0`**: the panel is `/dev/dri/card0`, held by `xochitl` (the reMarkable UI), which drives it through its own Qt platform plugin (`libepaper.so`) and scene-graph plugin (`libqsgepaper.so`) - the waveforms are worked out in user space |
| pen | `/dev/input/event2` "Elan marker input": ABS_X 0..11180, ABS_Y 0..15340, pressure 0..4096, tilt, hover distance, eraser, side button |
| touch | `/dev/input/event3` "Elan touch input": multitouch, 2064 x 2832 |
| buttons | `/dev/input/event0` the power key; `/dev/input/event1` "Hall effect sensors" (the folio's cover) |
| sound | none (no ALSA library, no loudspeaker) |
| libraries | libstdc++ present; no X11, no ALSA |
| network | Wi-Fi (2.4/5 GHz); USB ethernet at 10.11.99.1 for SSH |

Sources: [reMarkable Paper Pro features](https://remarkable.com/products/remarkable-paper/pro/details/features),
[Good e-Reader's specs](https://goodereader.com/blog/electronic-readers/everything-you-need-to-know-about-the-remarkable-paper-pro-with-color-e-paper),
[developer mode](https://developer.remarkable.com/documentation/developer-mode),
[the SDK](https://developer.remarkable.com/documentation/sdk) (Yocto toolchains named
`meta-toolchain-remarkable-<os>-ferrari-public-x86_64-toolchain.sh`, target
`aarch64-remarkable-linux`, `cortexa53`).

### Running third-party programs

Developer mode (Settings > General > Paper tablet > Software > Advanced >
Developer mode) wipes the tablet once, keeps the disk encrypted, shows a
warning at every boot and gives root over SSH (`ssh root@10.11.99.1` over
the USB cable; the password is under General > Help > About > Copyrights
and licenses; `rm-ssh-over-wlan on` for Wi-Fi).  The owner's tablet is in
developer mode with key authentication.

Because xochitl owns the panel through DRM and a user-space waveform engine,
a program cannot simply map a framebuffer as on the reMarkable 1 and 2.
The community's answer is **xovi** (an `LD_PRELOAD` extension loader for
xochitl) with the **AppLoad** extension
([asivery/rm-appload](https://github.com/asivery/rm-appload), GPL-3): a
launcher inside xochitl that runs "external" applications and lends them a
**qtfb** framebuffer - xochitl draws it with its own waveforms.  Its
`shims/qtfb-shim.so` (an `LD_PRELOAD` library) makes a program written for
the reMarkable 2's `/dev/fb0` and `/dev/input/event*` a qtfb client; that is
how KOReader runs on the owner's tablet.  xovi, AppLoad, the shim and
KOReader are installed there already (`/home/root/xovi`,
`/home/root/shims`, `/home/root/xovi/exthome/appload/koreader`), with
**xovi-tripletap** (a triple press of the power button starts xovi, which
restarts xochitl with the extensions loaded).  xovi does not survive a
reboot.  Toltec, the package manager of the reMarkable 1/2 era, is archived
and does not support the Paper Pro; **Vellum** (apk-based,
[vellum-dev/vellum](https://github.com/vellum-dev/vellum)) is its
successor and is present on the tablet (`/home/root/.vellum`).

### The qtfb protocol

What newton speaks, from AppLoad's `src/qtfb/common.h` (and the C++, Rust
and Zig clients, [zqtfb](https://github.com/0xdeb7ef/zqtfb) among them; all
GPL-3, so newton's client is written afresh from the protocol -
`src/host/remarkable/QTFB.h`, its layouts pinned by `static_assert`s):

- AppLoad starts the application with `QTFB_KEY` in its environment;
- the program connects a `SOCK_SEQPACKET` Unix socket to `/tmp/qtfb.sock`
  and sends a 24-byte message: *initialise* with the key and a pixel format
  (RGB565, RGB888 or RGBA8888 at the Paper Pro's, the Move's or the
  reMarkable 2's size) or *custom initialise* with a size of its own;
- AppLoad answers with a shared-memory key: `shm_open("/qtfb_<n>")`, mapped,
  is the framebuffer;
- *update* (whole, or a rectangle) puts it on the glass; *set refresh mode*
  picks the waveform for the following updates (ufast, fast, animate,
  content, UI); *request full refresh* flashes the panel clean;
- AppLoad sends 32-byte *user input* messages: the pen (press, update,
  release, with pressure 0..100), touches (with an id), keys - the type
  folio's as `INPUT_BTN_*` and AppLoad's own on-screen keyboard's as
  `INPUT_VKB_*`, both with Qt's key code - and rotation changes;
- coordinates are in the framebuffer's own pixels whatever AppLoad does to
  show it (it centres it, or scales it if the app allows), so the client
  never translates.

A quirk of AppLoad's own C++ client, worth knowing if anyone copies it: it
makes the socket non-blocking with `fcntl(fd, ...)` *before* `fd` is set,
so it never is.

### rmkit

[rmkit](https://github.com/rmkit-dev/rmkit) is "a batteries-included library
for building remarkable apps": framebuffer drawing with the mxcfb update
ioctls (waveform and update mode per update), input from the pen, touch and
buttons (`input::Input`, events marshalled to `SynMotionEvent`/`SynKeyEvent`),
widgets, an on-screen keyboard, fonts (stb_truetype), and a desktop
simulator (`TARGET=dev` writes the framebuffer to a file, `pip install
rmkit-sim` gives the `resim` viewer).

- **Licence: MIT** ("MIT except where noted otherwise", `src/rmkit/README.md`;
  the repository has no top-level LICENSE file).  Using it, and shipping a
  binary built with it, is fine; a binary given to others must carry rmkit's
  copyright notice.  It is not committed here: `tools/remarkable/fetch_rmkit.py`
  fetches a pinned commit.
- **Devices**: the reMarkable 1 and 2 (the 2 through rm2fb) and several
  Kobos.  **Not the Paper Pro**: there is no `/dev/fb0` there, nothing in rmkit
  knows the Paper Pro, and the last commits (July 2025) are keyboard fixes.
  On the Paper Pro it works only under AppLoad's qtfb-shim, as KOReader does.
- It is written in **okp**, a Python-like dialect its own transpiler turns
  into C++; `rmkit.h` is the result in one file.  The transpiler runs on
  Linux (on Windows the header it writes does not compile), so
  `fetch_rmkit.py` is run on Linux or WSL.
- Its reMarkable build is **32-bit only**: ioctl arguments are cast to
  `uint32_t`, which does not compile for aarch64; `fetch_rmkit.py` patches
  `pointer_size` to `uintptr_t` (the only change needed for it to build).
- It opens the framebuffer and the input devices in **static constructors**,
  before `main`, and installs handlers for SIGINT/SIGTERM/SIGABRT/SIGSEGV that
  call `exit()` from the handler - which on a SIGTERM aborted newton's exit in
  the spike ("recursive_mutex lock failed").  So it cannot simply be linked
  into every newton: it is an option of the configure, a second binary.

**Verdict.**  rmkit is usable and was used (below), but on the Paper Pro it
only adds a layer: rmkit's fb calls are turned back into qtfb by the shim,
its 32-bit assumptions need patching, it grabs the devices at load time, and
its signal handling fights newton's.  newton's window wants a framebuffer,
the pen and the keys - exactly what qtfb gives - and the Newton has its own
on-screen keyboard and widgets, so rmkit's UI is not needed.  The port
therefore **talks qtfb directly by default** and keeps an rmkit panel beside
it for the reMarkable 1 and 2 (where rmkit's framebuffer is the real one)
and as a cross-check on the Paper Pro.

## The design

A new host backend beside `win32/` and `x11/`, the reconstruction untouched:
`host/HostWindow.h` is answered by `src/host/remarkable/HostWindow.cpp`
(chosen by `-DNEWTON_HOST_WINDOW=remarkable`) over a small panel interface,
`remarkable/Panel.h`, with two implementations:

- `QTFBPanel.cpp` - qtfb as above; it asks for a framebuffer exactly the size
  of the scaled display (*custom initialise*), so AppLoad centres it;
- `RMKitPanel.cpp` - rmkit's `framebuffer::get()` and `ui::MainLoop::in`
  (built only with `-DNEWTON_RMKIT_DIR`): the image centred on rmkit's
  framebuffer, the Wacom events as the pen, the button device's Linux key codes
  as keys.

Which one: `NEWTON_RM_PANEL_KIND=qtfb|rmkit`, else qtfb when `QTFB_KEY` is
set (started by AppLoad), else rmkit if built in; none, and newton runs
headless as on any host without a window.

**The screen.**  The Newton's display is 8-bit grays (0 white..255 black) of
any size (`--display WxH`).  The window keeps the grays it last sent and
each thirtieth of a second sends only the rectangle that changed, written into
the panel at an integer scale as RGB565 grays (the colour panel shows them
as grays):

- **scale**: `--scale n` if more than 1, else the largest that fits the panel -
  the MessagePad's 320 x 480 at 4x on 1620 x 2160 (1280 x 1920, a margin of 170
  and 120), `--display 540x720` filling it exactly at 3x, `--display 405x540`
  at 4x (a Newton screen of another size is a supported thing now - ctest
  `host.NewtonBigScreen`); the Move's 954 x 1696 (`/proc/device-tree/model`,
  or `NEWTON_RM_PANEL=WxH`) gives 320 x 480 at 3x;
- **waveforms**: the pen down - the fast one (qtfb *fast*, rmkit DU), so live
  ink follows the pen; otherwise the gray UI one (no flash); after the pen
  has been up and the screen still for `NEWTON_RM_SETTLE` ms (600) the area
  drawn fast is sent again in the gray waveform; after `NEWTON_RM_FULL`
  screens' worth of change (4; 0 never) one full flashing refresh clears the
  ghosts; the first picture is a full refresh.

**The pen** is the Newton's pen through the same shims as the mouse on the
desktop (`HostWindowPenDown/Move/Up`, the host tablet, the calibration);
pressure, tilt and hover are not used - the Newton's resistive tablet had
none.  **Touch** is ignored, since the hand rests on the glass, unless
`NEWTON_RM_TOUCH=pen` makes the first finger the pen.  **Keys**: the type
folio and AppLoad's on-screen keyboard arrive as Qt key codes, the button
device (rmkit) as Linux key codes; both are turned into the Windows virtual
key codes `host/HostKeyboard.cpp` maps to the Newton's.  The Newton's own
on-screen keyboard works as on a MessagePad, with the pen.  The power key
is mapped to F12, the Newton's power switch - though under AppLoad xochitl
probably keeps it (to be seen on the device).  **Closing**: AppLoad's drag
down from the top centre closes the socket; newton ends the run as a closed
window does.

**Sound**: the tablet has no loudspeaker; the null backend (silent) as on any
host without one.  **Storage**: `--store /home/root/newton-data/internal.store`
(the sparse flash file, 64 MB by default); the log beside it; printing
(`--print-dir`) writes PNGs there too.  **Network**: the host's own TCP/IP
over Wi-Fi as on the desktop - the NIE, NetHopper, IPP printing; the serial
port is TCP 3679, so **a desktop docks with the tablet over the network**
(`tools/dock/dock.py --connect <tablet>:3679`, NCU/NCX the same way), and
`--ir-lan` beams to and from desktop newtons on the same Wi-Fi.  (3679 then
listens on the tablet's network: fine at home; say so in the run script's
comments if it is ever offered to others.)  **Launching**: an AppLoad
application directory - `tools/remarkable/package.py`.

## The build

Cross-compiled from any machine with zig (0.16 here, on Windows):

    cmake -G Ninja -S src -B tmp/build-rmpp
          -DCMAKE_TOOLCHAIN_FILE=<repo>/src/cmake/zig-aarch64-linux.cmake
          -DCMAKE_BUILD_TYPE=Release -DNEWTON_HOST_WINDOW=remarkable
          -DNEWTON_HOST_NEWTONSCRIPT=<repo>/build/host/host/newtonscript.exe
          -DNEWTON_ROM_BUILD=<repo>/build/MP2x00US
    cmake --build tmp/build-rmpp --target newton romsrc

- `src/cmake/zig-aarch64-linux.cmake` sets `CMAKE_SYSTEM_NAME Linux`,
  `ZIG_TARGET aarch64-linux-gnu.2.31` (the glibc floor - 2.31 is far below the
  tablet's 2.39; zig links its libc++ statically, so the program needs the C
  library alone), `-fsigned-char`, and **PIE** (below).
- A cross build cannot run its own `newtonscript` to compile `romsrc/` into
  the object file, so `-DNEWTON_HOST_NEWTONSCRIPT` names one built for the
  build machine; without it the object file is not built and one is copied
  from a native build.  **The object file is host-independent**: the one the
  cross build made is identical, byte for byte, to the Windows build's.
- With rmkit: `python3 tools/remarkable/fetch_rmkit.py -o tmp/rmkit` (Linux
  or WSL), then `-DNEWTON_RMKIT_DIR=<repo>/tmp/rmkit`.
- The official reMarkable SDK (a Yocto toolchain for x86-64 Linux hosts) would
  do as well under WSL; zig was chosen because it is already the project's
  toolchain and runs on Windows.

What the cross build found (all fixed on the branch):

1. **Host natives taken for ROM natives.**  zig links a non-PIE aarch64
   program at 0x200000, so newton's own functions sit below 0x02000000 - and
   `frames/NativeFunctions.h`'s `kROMCodeLimit` says a native function
   pointer below that is a ROM jump-table address.  Every host native was then
   "not reconstructed" (`[frames] native not reconstructed: funcPtr
   0x014c5224`), and the boot went wrong (a To Do roll-over alert over
   Welcome).  Windows' image base (0x140000000) and a distribution compiler's
   PIE on x86-64 Linux are far above it, which is why this had not shown.
   The toolchain file builds PIE.  (A non-PIE build for any host - a static
   x86-64 Linux link, say - has the same trap; a check at start-up, or
   telling host pointers by something other than their value, would close it
   for good.)
2. **`<thread>` after the Newton headers**: on a case-insensitive file
   system libc++'s Linux locale support finds `src/intl/Locale.h` for
   `<locale.h>` - a cross build *for Linux on Windows* is the first to see
   it.  `host/newton.cpp`'s malloc-statistics thread is a `pthread` now.
3. `mallinfo2` is glibc 2.33's: older glibc gets `mallinfo`.
4. `char` is unsigned on aarch64.  Booting to Setup with
   `-funsigned-char` gave the same screen, byte for byte, as with
   `-fsigned-char`; the toolchain keeps `-fsigned-char` anyway, since every
   other host the reconstruction runs on has a signed `char` (see
   `docs/host-macos.md`).

## Tools (`tools/remarkable/`)

| | |
|---|---|
| `rmprobe.c`, `build_probe.py` | the probe: the system, C library, display devices, input devices (with ranges) and AppLoad; `--events S` prints raw input events, `--qtfb S` (from AppLoad) shows a gray ramp and a checkerboard and draws the pen as dots, printing each event and how soon the dot was sent. `build_probe.py -o OUT` builds `OUT/rmprobe` and the AppLoad app `OUT/rmprobe-app/` |
| `qtfbserver.py` | a stand-in for AppLoad's qtfb server on any Linux, so the window can be tested without a tablet: runs a program with `QTFB_KEY`, counts updates by waveform, and carries out steps (`quiet:S`, `snap:FILE.png`, `tap:X,Y`, `stroke:...`, `key:CODE`, `gray:X,Y,W,H,MIN`) |
| `fetch_rmkit.py` | rmkit at a pinned commit, okp from PyPI (no pip), `rmkit.h` made and patched for 64 bits, stb beside it |
| `package.py` | the AppLoad application directory: `newton`, the object file, `run.sh`, `external.manifest.json`, `icon.png`; `--rmkit` for the rmkit variant (`LD_PRELOAD` the shim, as KOReader's manifest does) |

## What the spike showed

All from the branch `rmpp`, built on the Windows machine.

1. **The cross build** links (`file`: ELF 64-bit LSB pie executable, ARM
   aarch64), 37 MB unstripped.
2. **On the tablet, headless** (copied to `/home/root/newton/`, run over SSH,
   no display touched):

        cd /home/root/newton
        ./newton --objects romsrc-objects.bin --display 320x480 --erase \
            --store newton.store --headless 150 --script snap.ns

   with `snap.ns` waiting for the Setup assistant and taking
   `ScreenSnapshot` eight seconds later: the run ends 13 s after it starts
   (eight of them the script's wait, two its quit), and the PGM is
   **identical to the Windows build's** (`cmp`).  The non-PIE build reached
   Welcome with the To Do alert over it and a script exception; the PIE one
   is clean.
3. **Under qemu-user with the qtfb stand-in** (WSL; `qemu-aarch64-static`
   and Ubuntu's arm64 `libc6` unpacked into a directory with `dpkg-deb -x`,
   nothing installed):

        python3 tools/remarkable/qtfbserver.py --socket /tmp/newton-qtfb.sock \
            --step quiet:6 --step snap:welcome.png --step tap:1174,1874 \
            --step quiet:6 --step snap:calibrate.png --step gray:0,0,1280,1920,100 -- \
            ./qemu-aarch64-static -L sysroot ./newton --objects romsrc-objects.bin \
            --display 320x480 --erase --store s.store

   `[host] reMarkable: the display at 4 x on qtfb, 1280 x 1920 at 0,0`;
   Welcome in the shared memory; the tap at Continue (the display's 293,468
   times four) went through qtfb's pen messages and the calibration screen
   came up.  The calibration target blinks, so the stand-in saw an update a
   second until the end (`ui 295`) - on e-ink, a small partial refresh.
4. **rmkit through AppLoad's own qtfb-shim** (the tablet's
   `/home/root/shims/qtfb-shim.so` and `libstdc++` copied into the qemu
   sysroot): `-E LD_PRELOAD=qtfb-shim.so -E NEWTON_RM_PANEL_KIND=rmkit
   -E QTFB_SHIM_MODE=RGB565`: rmkit found a 1620 x 2160 16-bit "framebuffer",
   newton put the display at 4x at 170,120, and Welcome was in the shared
   memory; the input shim could not find the tablet's input devices under
   qemu (expected).  On SIGTERM rmkit's signal handler aborted the exit (see
   rmkit above).

5. **Against the tablet's own AppLoad** (xovi started with
   `/home/root/xovi/start`, which restarts xochitl - the owner's go-ahead
   given): newton run over SSH with a made-up `QTFB_KEY` connected to
   `/tmp/qtfb.sock`, and xochitl's journal (`journalctl -u xochitl`) shows
   AppLoad taking it: `Client is connecting in 3 mode. Resolution is set to
   1280x1920`, `Defined SHM (4915200 bytes)` - the message layouts are right.
   Its updates were refused ("Could not find the framebuffer to act upon"),
   as they must be: AppLoad shows a framebuffer only in a window it opened
   itself for an application started from its launcher.  So putting newton
   on the glass takes one tap in xochitl's UI, which is the owner's (below):
   nothing here reads xochitl's screen or injects input into it, since both
   could reach the owner's documents.

### Watching a tablet one cannot see

The window traces itself for this (`run.sh` turns both on):

- `NEWTON_RM_TRACE=1`: every update (waveform, rectangle in display
  pixels) and for each stroke a line like `[rm] stroke: 0.40 s, 21 pen
  events (52 a second), 7 ink updates; pen to update min 8.8 median 57.6 max
  57.8 ms` - how fast AppLoad delivers the pen and how long newton takes from
  a pen event to the update that shows its ink (its share of the latency;
  the panel's own waveform time comes after).  While the pen is down the
  window polls every `NEWTON_RM_INK_FRAME` ms (8) instead of
  `NEWTON_RM_FRAME` (33).
- `kill -USR2 <newton's pid>` writes the display as it is to
  `$NEWTON_RM_SNAPDIR/panel-N.pgm` (`/home/root/newton-data/`): what newton
  handed the panel.
- A NewtonScript file at `/home/root/newton-data/script.ns` is run at boot
  (`--script`), for tests that need no hand.

## On the device

What only the tablet can tell: the picture on the glass, the waveforms'
look and speed (is live ink quick enough?), the pen's coordinates through
AppLoad, the folio's keys, what the power button and sleep do, and whether
the Marker's palm rejection makes touch-as-pen unnecessary.

Prepared (in `tmp/rmpp-app/` of the branch's build; remade by the commands in
"The build" and `package.py`): `newton/` (qtfb), `newton-rmkit/` and
`rmprobe-app/`.

1. Copy them into AppLoad's application directory (only there and
   `/home/root/newton-data` are written):

        scp -r tmp/rmpp-app/newton tmp/rmpp-app/newton-rmkit tmp/rmpp-app/rmprobe-app \
            root@10.11.99.1:/home/root/xovi/exthome/appload/

2. Start xovi: triple-press the power button (xovi-tripletap) - this
   **restarts xochitl** - or `ssh root@10.11.99.1 /home/root/xovi/start`.
   `ls /tmp/qtfb.sock` then shows AppLoad's socket.
3. Open AppLoad from xochitl's menu; tap **rmprobe**, draw with the Marker for
   30 s; the gray ramp shows how the panel renders the Newton's grays and the
   dots how fast the fast waveform follows the pen.  Send back
   `/home/root/rmprobe-qtfb.log` and a photo.
4. Tap **Newton**: Welcome at 4x.  Tap Continue with the Marker, calibrate
   (hold on each X until it darkens), go through Setup, then open the
   Notepad and write a word.  Photos of Welcome and of the written word, and
   `/home/root/newton-data/newton.log`.  Close it with a drag down from the
   top centre of the screen.
5. The same with **Newton (rmkit)** (it shares the store).
6. Optional: `./rmprobe --events 15` over SSH while drawing, touching and
   typing on the folio - the raw event streams.

To remove everything: `rm -r /home/root/xovi/exthome/appload/{newton,newton-rmkit,rmprobe-app}
/home/root/newton /home/root/newton-data /home/root/rmprobe-qtfb.log`.

## Risks

- **Live ink latency.**  xochitl's own ink is drawn by its compositor with
  prediction; a qtfb client's ink goes newton -> message -> AppLoad -> Qt
  scene graph -> waveform.  The fast mode is AppLoad's best; the probe's
  `--qtfb` mode is there to measure it.  If it is too slow the next step is
  the Newton's live inker drawing into the framebuffer directly and sending
  just its stroke's rectangle (it already blits only a tile), or asking
  AppLoad for *ufast*.
- **Ghosting** on the gray waveform; the settle and full-refresh policy is
  tunable from the environment while it is being tuned.
- **xovi/AppLoad are community software** layered on xochitl by
  `LD_PRELOAD`; a reMarkable software update can break them until they catch
  up (the tablet is on 3.25.1.1; AppLoad's latest commit adds "support for
  3.29").  newton itself depends only on the qtfb protocol.  Without xovi a
  program could own the panel only by stopping xochitl and driving DRM with
  a waveform engine of its own - not attempted.
- **rmkit** has no Paper Pro support, needs the 64-bit patch, and its signal
  handlers conflict with newton's; it stays the secondary path.
- **Power**: what sleeping the tablet does to a running AppLoad app is
  unknown; newton's own sleep (F12) is mapped but probably unreachable.
- **Licences**: AppLoad, the shim and the qtfb clients are GPL-3 - nothing of
  them is in this repository or linked into newton (the shim is the user's
  own, preloaded at run time by the rmkit variant's manifest).  rmkit is MIT,
  fetched rather than committed; a newton-rmkit given to others carries its
  notice.

## Staged plan

1. *(done on the branch)* cross build, PIE fix, qtfb window, rmkit panel,
   probe, stand-in server, packaging; headless boot on the tablet.
2. *(the owner, half an hour)* "On the device" steps 1-5: the glass, the pen,
   the folio.  Small fixes from what they show - orientation, the power key,
   coordinates - a day.
3. Tune the refresh: measure ink latency with the probe and with newton;
   if needed have the live inker's tile updates go out at once in the fast
   waveform (the window already knows the inker's rectangle is all that
   changed) - one to three days.
4. Rotation (AppLoad's DEVICE_STATE_CHANGED -> the Newton's
   `SetScreenOrientation`, the panel image turned), the Move's size, touch
   gestures (two fingers for scrolling?) - two days.
5. Distribution: a `Newton` package for Vellum (apk) or AppLoad's own format,
   with the object file and a first-run store; stripping the binary (37 MB
   with debug info) - a day.
6. Optional: a startup check (or a different test) so that no host build can
   take its own natives for ROM addresses again (`kROMCodeLimit`), merged to
   main independently of the port.
