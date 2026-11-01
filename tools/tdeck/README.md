# tools/tdeck - the LilyGO T-Deck Pro

Tools for the small-device investigation (`docs/host-tdeck-pro.md`): the
reconstruction on an ESP32-S3 with 8 MB of PSRAM, a 240x320 e-paper, a
touch panel and a keyboard.  Nothing here is the OS; these measure what a
port of it needs to know.

Everything builds with PlatformIO (its own Python, ESP-IDF and toolchains -
`~/.platformio`), so a checkout and PlatformIO are all that is needed:

    ~/.platformio/penv/Scripts/pio run -d tools/tdeck/spike                         # build
    ~/.platformio/penv/Scripts/pio run -d tools/tdeck/spike -t upload --upload-port COM4
    ~/.platformio/penv/Scripts/python tools/tdeck/serialcapture.py COM4 120 --until "spike: done" --reset

(On Linux and macOS `penv/bin/` for `penv/Scripts/`, and the port's own
name.)  The first build installs ESP-IDF and its toolchains, about 1.5 GB:
install them on their own first, with no time limit, so that a long
download never cuts a build short - an Xtensa toolchain cut off part way
leaves the linker unable to find `-lc`; delete that package and install
again:

    ~/.platformio/penv/Scripts/pio pkg install -d tools/tdeck/spike

After that a rebuild takes about 20 s.  The board shows up as the ESP32-S3's own USB serial/JTAG port
(USB 303A:1001); flashing it replaces what was there.

## spike/ - what the board is like

A PlatformIO project, framework ESP-IDF (`platform = espressif32@6.5.0`,
ESP-IDF 5.1; the board's PSRAM is quad, not the octal LilyGO's README
says), that prints lines beginning `spike:` over the USB serial port:

1. **The board**: the chip's revision, the flash and PSRAM sizes, the free
   heaps, and an I2C scan - which also says the board revision (LilyGO's
   own firmware tells them apart the same way: V1.1 adds a DRV2605 haptic
   driver at 0x5A, MAX an XL9555 expander at 0x20 as well).
2. **Memory**: for internal SRAM, PSRAM and flash mapped through the cache
   (the `bench` partition), at sizes inside and outside the cache - the
   time a *dependent* read takes (each address made from the word read
   before it, which is how the NewtonScript interpreter walks objects) and
   a sequential sum's bandwidth.
3. **The panel**: a Newton screen (`src/newton_screen.c`, below) drawn with
   the panel's own full waveform; with the temperature forced to three
   values (`CCSET`/`TSSET`: a forced temperature picks a shorter waveform
   from the panel's OTP); and a 64x64 window refreshed on its own
   (`PTIN`/`PTL`) - each refresh timed from `DRF` to the end of BUSY.
   Measured once (docs/host-tdeck-pro.md, "What the device spike
   showed"), so off by default now: `kPanelTimings` in `spike.c` runs them
   again.  The panel is powered off at the end, never put into deep sleep:
   without its reset line, deep sleep lasts until the board loses power.
4. **Input**, for a minute and a half, over five black targets drawn on
   the panel (the four corners clockwise from the top left, then the
   centre, 16 x 16 pixels): the touch panel's reports as they change
   (position, finger count, the raw bytes) and the keyboard's keys (down
   and up, row and column, the character).

Sources of the facts it relies on:

- pins and I2C addresses: LilyGO's [T-Deck-Pro](https://github.com/Xinyuan-LilyGO/T-Deck-Pro)
  schematic (`hardware/T-Deckpro v1.0 24-05-16/`) and `examples/factory/utilities.h`;
- the panel driver (`src/epd.c`): the UltraChip UC8253 datasheet
  (`hardware/UC8253.pdf`) - its command table; written from it, not from
  any library (LilyGO's code is GPL-3.0, so none of it is copied here);
  one thing the datasheet does not say plainly, found on the board: in
  this black-and-white mode the image shown is the one sent with DTM1
  (0x10), the frame before it goes to DTM2 (0x13);
- the touch report (register 0xD000, seven bytes ending 0xAB, 0xD109 for
  normal mode): read off LilyGO's `examples/touch_hyn_core/arduino/hyn_cst3xx.c`;
- the keyboard (a 4x10 matrix on a TCA8418, an event `0x80 | row * 10 +
  column + 1` with the columns counted from the right, the layout): the
  owner's own lilyphone1 project (`src/peripherals/peri_keypad.cpp`).

## pgm2epd.py, screenshot.ns - a Newton screen for the panel

    SHOT=tmp/tdeck/screen.pgm build/host/host/newton --display 240x320x1 --erase \
        --store tmp/tdeck/screen.store --headless 40 --script tools/tdeck/screenshot.ns
    python tools/tdeck/pgm2epd.py tmp/tdeck/screen.pgm tools/tdeck/spike/src/newton_screen.c

`screenshot.ns` photographs the desktop newton six seconds after boot into
the file `SHOT` names; `pgm2epd.py` packs a 240x320 PGM into the panel's
format (one bit a pixel, a set bit white, rows top to bottom, each byte's
top bit leftmost) as a C array.  `newton_screen.c` is generated - remake
it, don't edit it.

## serialcapture.py - what the board prints

    ~/.platformio/penv/Scripts/python tools/tdeck/serialcapture.py <port> <seconds> \
        [--out tmp/tdeck/serial.log] [--until TEXT] [--reset] [--send LINE]... [--after TEXT]

`pio device monitor` is interactive; this captures for a while (until a
line holding TEXT), echoing and writing to a file, reopening the port when
the board resets; `--reset` resets the board first through the USB
serial/JTAG port's RTS line, so the capture starts at boot.  `--send`
types lines to the board once a line holding `--after` ("Welcome to
NewtonScript" unless given) has come, each when the board has been quiet
for a moment - how the NewtonScript prompt of `src/host/esp32` is driven
from a script; `--after ""` sends at once, to a board already running.
The port is opened with DTR and RTS low, which does not reset the board
(opening it otherwise could).

`--send-file FILE`: every line appended to FILE while the capture runs is
sent too - so one long capture in the background can be typed into from
outside without opening the port again (the file is emptied at the
start).  The way the board was worked with:

    timeout 3700 ~/.platformio/penv/Scripts/python tools/tdeck/serialcapture.py COM4 3600 \
        --after "" --send-file tmp/tdeck/send.txt --quiet-ms 1500 --out tmp/tdeck/session.log &
    echo 'DevicePanelSet("trace", 1);' >> tmp/tdeck/send.txt

A line the board does not read within two seconds is dropped (said in
the log) rather than blocking the capture.  Stop the capture (its Python
process, not only the shell around it) before flashing: it holds the port.

## camshot.py - a photograph of the panel

    python -m venv build/tdeck-venv && build/tdeck-venv/Scripts/python -m pip install opencv-python   # once
    build/tdeck-venv/Scripts/python tools/tdeck/camshot.py [--camera N] [--crop X,Y,W,H] [--zoom N] \
        [--after S] [--every S --count N] [--sheet] [--out tmp/tdeck/cam.png] [--list]

The panel cannot be read back (the board only writes to it), so what it
really shows is photographed by a webcam pointed at it.  `--list` says
which cameras open (a virtual one, such as OBS's, opens too and shows its
own picture: on the owner's machine the Logitech BRIO is 0, at 1920 x
1080); `--crop` keeps the panel's rectangle and `--zoom` enlarges it;
`--every`/`--count` take a series (a refresh as it happens) and `--sheet`
lays the series out numbered in one picture.

## panelprobe.py - which way of driving the panel works

    build/tdeck-venv/Scripts/python tools/tdeck/panelprobe.py --send tmp/tdeck/send.txt \
        --log tmp/tdeck/session.log [--crop X,Y,W,H] [--screen X0,Y0,X1,Y1] \
        [--set name=v,name=v]... [--keep DIR]

For each `--set` (panel settings, `DevicePanelSet` names) runs the board's
test pattern (a full refresh to left-black/right-white, a partial one
flipping the top half of a middle square, a partial one back) through a
running serialcapture.py, photographs each picture when the board's log
says its refresh is done, reads the square's four quarters dark or light,
and prints PASS when the three pictures were right.  `--crop` is the
panel in the camera's frame, `--screen` the panel's pixels' corners in
the crop (read off one camshot.py picture).  It found the panel's
set-up (src/host/esp32/main/tdeck_panel.cpp; docs/host-tdeck-pro.md):
28 tried, one right.

## panelshot.py - what the panel shows, without a camera

    python tools/tdeck/panelshot.py tmp/tdeck/session.log [-o tmp/tdeck/panel.png] [--scale 2]

`!shot` typed at the board's console (src/host/esp32/main/esp32main.cpp:
the board answers it whatever the Newton is doing) prints the frame the
panel driver last sent, as hex rows; this turns the last one in a log into
a PNG.  What was sent, not what the glass shows (ghosts are the camera's,
camshot.py).

## profile.py - where the board's time goes

    python tools/tdeck/profile.py tmp/tdeck/session.log [--top 30] [--by function|file|area] [--phases]

The board samples core 1's program counter every millisecond from the
start (src/host/esp32/main/tdeck_profile.cpp: the ESP32-S3's assist-debug
block records a core's pc, which the other core reads); `DeviceProfileDump()`
at the NewtonScript prompt prints the samples into the console log.  This
names them with the firmware's ELF (PlatformIO's addr2line) and ranks the
functions (or files, or `src/` areas), overall and, with `--phases`, for
each stretch between the boot's steps.  A sample in FreeRTOS's idle task is
core 1 waiting; one at 0x4000_0000-0x4006_0000 is the chip's mask ROM
(0x40055448: `strcmp`'s body).  It found the boot's minute: C++ exception
tables searched linearly (78%), the ROM extensions' headers searched for in
megabytes of flash at each call (half of what was left), and native
functions registered by comparing each name with every entry.

## powermeter.py - what the board draws from its battery

    ~/.platformio/penv/Scripts/python tools/tdeck/powermeter.py COM4 [--reset] [--window 60] [--settle 15]
        [--only keylight,gps,lora,cell,wifisave,wifi] [--keep-usb] [--out tmp/tdeck/power.log]

The board's own fuel gauge (the BQ27220's Current, sampled once a second by
the firmware: `DevicePower()` at the prompt gives [mA now, the mean since
last asked, samples, mV]) measures the board in one state after another,
each switching one more thing off: as found, the keyboard's light, the GPS,
the LoRa radio, the 4G module, the WiFi's power save at its most, the WiFi.
For the run the charger is told to leave the USB alone
(`DevicePowerSet('battery, true)`, the BQ25896's EN_HIZ), so the board runs
from its battery while the USB still carries the prompt; everything is put
back at the end, and the board's next start puts the charger's input back
if a run is cut short (the radios' switches are kept in NVS: one left off
is switched on again in Prefs). Prints a table of mA, each step's saving
and mV; `--out` keeps the whole conversation. Needs a battery fitted. With
the board on its battery the console loses a character now and then (a
reply's first, a digit of the mV), so the replies carry a doubled marker.
The figures are in docs/host-tdeck-pro.md, "Battery life".

## storebench.py - how long the store takes to write

    ~/.platformio/penv/Scripts/python tools/tdeck/storebench.py COM4 [--count 10]

Ten (`--count`) Names cards named "Bench <n>" are added and removed again
at the board's NewtonScript prompt, timed by the Newton's own `Ticks`;
prints `add N: T ms` and `remove N: T ms`.  The Newton's sleep is turned
off for the run and put back after.  Each statement is typed by
serialcapture.py (one capture each, logs kept as
`tmp/tdeck/storebench-<n>.log`) and kept short: the board's console reads
a long line in pieces.  A run cut short leaves its cards behind, and the
next run's remove takes them too (its count says so) - run it again for a
clean figure.  It measured the batched store writes (docs/host-tdeck-pro.md,
"Batched store writes").

## xtensa_compile.py - the tree with the ESP32-S3's compiler

    cmake -S src -B tmp/build-arm -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...   # (src/cmake/zig-arm-linux.cmake)
    python tools/tdeck/xtensa_compile.py tmp/build-arm/compile_commands.json [--only DIR,...] [--jobs N] [--opt=-Os] [--warn REGEX]

Every file of a 32-bit CMake build (tests left out) compiled again with
ESP-IDF's GCC for the ESP32-S3 (PlatformIO's `toolchain-xtensa-esp32s3`),
keeping the build's `-I`, `-D` and `-include` and adding the flags the tree
needs (C++17 with exceptions, `-fwrapv`, `-fsigned-char`,
`-falign-functions=4`) and ESP-IDF's (`-mlongcalls`, function and data
sections).  Nothing is linked.  **Output**: `tmp/tdeck/xtensa/report.txt`
- how many files compiled, by area; the failures grouped by their first
error (a missing header by its name); the compiled code and data by area -
with the objects and each failure's full messages beside it.  About 30 s
for the whole tree.

`--warn REGEX` compiles without `-w` and adds to the report every warning
whose text matches: `--opt=-O2 --warn "control reaches end|no return
statement"` finds functions that fall off their end, which GCC for Xtensa
compiles as unreachable (none, 2026-10-11).  (`--opt=-O2`, with `=`: a
value that starts with `-` needs it.)

## devicerun.sh - the walkthroughs on a device's budget

    tools/tdeck/devicerun.sh <newton> [newton's options...]
    # e.g. under WSL 2, the 32-bit build:
    tools/tdeck/devicerun.sh tmp/build-x86/host/newton --objects build/host/romsrc-objects.bin \
        --kernel-heap 2 --heap 524288 --flash-size 4 --task-stack 32

Runs both user walkthroughs (`src/host/demo/walkthrough.ns`,
`walkthrough2.ns`), each on a new store and then on that store again (the
restart checks), with the options given and `NEWTON_MEMORY_REPORT` set;
prints for each its result, the memory report (the kernel heap's peak and
what the ROM's objects hold) and the peak resident set (GNU time).  Each
run's whole output is `tmp/devicerun-<n>.log`.  Linux only.
`--kernel-heap` and `--task-stack` are newton's (src/host/newton.cpp).

## idf_sources.py - the tree's sources for the ESP32-S3 build

    python tools/tdeck/idf_sources.py <a 32-bit build's compile_commands.json> \
        [-o src/host/esp32/main/newton_sources.cmake]

Writes the list of the tree's sources the ESP-IDF component builds
(`src/host/esp32`), each with the include directories the desktop build
gives it, grouped - from a 32-bit build's compile commands (configure
`src/cmake/zig-arm-linux.cmake` with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`).
Left out: tests, generated sources, `src/host/` but for `HostNatives.cpp`,
and the host's task runtime and sockets.  Sources whose file name another
shares are compiled through `src/host/esp32/main/forwarded/<dir>_<name>`
(PlatformIO names objects by file name alone).  Run it again when sources
are added or taken away; its output is generated, not edited.

