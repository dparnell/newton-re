# Newton MessagePad 2x00 ROM reverse engineering

Reverse engineering of the Apple Newton MessagePad 2x00 ROM, with the
long-term goal of a retargetable, maintainable re-implementation of Newton
OS.  Two debug ROMs are in the repository and the tooling works with
either; the reconstruction in `src/` is of the US English one, `MP2x00 US`
(2.1, build 717006).  The other, `MP2100 D` (D-2.1, build 747129), is the
newer operating system but a German one, with no English locale and German
strings built into the applications in its ROM extension, so it is kept as
a second opinion rather than as the subject.

## Where it has got to

**The reconstructed Newton OS boots and runs without a ROM image.**  By
default `newton` boots from data built from the committed, editable
`romsrc/` tree - the ROM's NewtonScript world as decompiled source, its
bitmaps, sounds, fonts, dictionaries and tables as ordinary files - and the
reconstructed C++ in `src/`.  The ROM images in `DebugRom/` are only needed
to check the reconstruction against the original (`--rom`) and to run the
analysis tools.

Build it and run it:

```powershell
cmake -G Ninja -S src -B build/host -DCMAKE_TOOLCHAIN_FILE=%CD%/src/cmake/zig-toolchain.cmake
cmake --build build/host
build\host\host\newton --store my.store
```

On Linux use the system compiler instead
(`cmake -G Ninja -S src -B build/host -DCMAKE_CXX_COMPILER=clang++`; `newton`
wants the X11 and ALSA development headers).  On macOS the system clang needs
nothing more (`cmake -G Ninja -S src -B build/host`), and the build makes
`build/host/host/Newton.app` beside `newton`.  The build makes
`build/host/romsrc-objects.bin` from `romsrc/` with Python and the
reconstruction's own NewtonScript compiler; no ROM image is read.

`newton` opens a window on the Newton's screen - the mouse is the pen, the
keyboard the keyboard - and the machine starts as a new MessagePad does: the
Setup assistant, then the Notepad.  Useful options:

| Option | What it does |
|---|---|
| `--store FILE` | keep the internal store (flash) in a file, so notes, settings and installed packages survive; a new one is a 64 MB sparse image |
| `--display WxH` | a screen of any size, e.g. `1024x768`; a wider window starts in landscape |
| `--package FILE.pkg` | install a package (or drop one onto the window) |
| `--card FILE` | a PC Card (memory or ATA) from a host image |
| `--ipp-printer URI` | print to a network printer by IPP (printers can also be found and added from the Newton itself) |
| `--cell-port PORT` | a cellular modem (or `tools/modem/fakecell.py`) for calls and text messages; the Messages application appears with it |
| `--mesh-air lan[:port]` | a pretend LoRa air over UDP multicast, for the Mesh application (MeshCore) between desktops |
| `--rom IMAGE` | boot the original ROM image instead, for comparison |

Handwriting is read by both of the ROM's recognisers, ink is kept, the
built-in applications work, packages install (third-party native ARM code
runs on a built-in ARM interpreter), and the Newton beams, docks with a
desktop over TCP port 3679, faxes, prints (PostScript and HP PCL, by IPP)
and reaches the Internet through the host's own network.  What each part
does and what is still NOT YET is in [docs/](docs/) -
[docs/newt/README.md](docs/newt/README.md) for the boot, and
[docs/next-steps.md](docs/next-steps.md) for what is left.

Run the tests with `ctest --test-dir build/host` (about 460 of them; a
build without the ROM image simply skips the few that compare against it).

### Where it runs

| Host | State | Page |
|---|---|---|
| Windows (x86-64) | the development host: zig toolchain, a window, sound, printing, docking | this README |
| Linux (x86-64) | the system's clang; X11 and ALSA; every ctest passes (checked under WSL 2) | [docs/host-lp64.md](docs/host-lp64.md) |
| 32-bit x86 Linux | built and tested under WSL by `tools/host/wsl32.sh`; the proof that the tree is correct at 32 bits | [docs/host-lp64.md](docs/host-lp64.md) |
| reMarkable Paper Pro | runs from AppLoad on the tablet's e-paper, written on with the Marker | [docs/host-remarkable.md](docs/host-remarkable.md) |
| LilyGO T-Deck Pro | **boots and runs on the board** (ESP32-S3, e-paper, touch, keyboard) - below | [docs/host-tdeck-pro.md](docs/host-tdeck-pro.md) |
| macOS (Apple silicon, Intel) | `Newton.app`: the system clang, an AppKit window, audio queues; one binary for both with `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, signed ad hoc; every ctest passes but the two flaky on every host | [docs/host-macos.md](docs/host-macos.md) |

Pushing to `main` builds and smoke-tests the Windows, Linux, reMarkable and
macOS builds on GitHub's runners, and a `v*` tag publishes them as a release
([docs/releases.md](docs/releases.md)).

### The reMarkable Paper Pro

On the owner's reMarkable Paper Pro, newton is an AppLoad application: an
810x1080 Newton screen drawn at twice its size on the tablet's 1620x2160
e-paper, the Marker as the pen
([docs/host-remarkable.md](docs/host-remarkable.md)).

<p align="center"><img src="docs/images/remarkable-notepad.png" width="405" alt="The Notepad on the reMarkable Paper Pro, with handwritten lines recognised as text and the Extras drawer open below"></p>

*The Notepad on the tablet, written in with the Marker and read by the
ROM's recogniser, the Extras drawer open below (newton's own snapshot of
its display - `kill -USR2` writes it on the tablet - turned into a PNG by
`tools/imaging/pgm2png.py`).*

### The T-Deck Pro

The [LilyGO T-Deck Pro](https://lilygo.cc/products/t-deck-pro) - an
ESP32-S3 (two 240 MHz Xtensa cores, 8 MB of PSRAM, 16 MB of flash) with a
240x320 one-bit e-paper panel, a capacitive touch screen and a keyboard -
runs the same tree as the desktops, built with ESP-IDF through PlatformIO
([src/host/esp32/README.md](src/host/esp32/README.md) says how to build,
bake and flash it; [tools/tdeck/](tools/tdeck/README.md) has the tools).

<p align="center"><img src="docs/images/tdeck-pro-extras.png" width="240" alt="The Extras drawer on the T-Deck Pro's 240x320 e-paper panel, with the Mesh and Messages applications beside the built-in ones"></p>

*The Extras drawer as the board's panel shows it (the panel's frame,
read back over the serial port by `tools/tdeck/panelshot.py`, at twice its
size).*

On the owner's board:

* **The OS boots in about 3 seconds**: the ROM's objects baked on the
  development machine (`newton --bake-objects`) and mapped from flash
  read-only, the kernel's tasks on FreeRTOS, a 1 MB NewtonScript heap in
  PSRAM.
* **The e-paper panel** is the Newton's screen (partial refreshes of only
  what changed, full ones to clear the ghosts), the **touch panel** its pen
  and the **keyboard** its keyboard; the screen turns to landscape when
  the Newton asks.
* **The internal store** is a 4 MB partition of the board's flash, read in
  place and written in batched runs, so notes, cards and settings survive a
  restart.
* **Wi-Fi** is set up from Internet Setup (scan, password, remembered
  networks), the clock set by SNTP; the Newton reaches the Internet and
  **prints** to the network's IPP printers, found by mDNS.
* **The battery** (`BatteryStatus`) is the board's fuel gauge and charger;
  the **GPS** receiver gives a fix.
* **The 4G variant's modem** (A7682E) is driven through the ROM's own
  Phone transport and Calls log, and text messages go through the In/Out
  Box and a **Messages** application
  ([docs/host-tdeck-pro-phone.md](docs/host-tdeck-pro-phone.md)).
* **The LoRa radio** (SX1262) is a [MeshCore](https://github.com/meshcore-dev/MeshCore)
  node with a **Mesh** chat application
  ([docs/host-tdeck-pro-mesh.md](docs/host-tdeck-pro-mesh.md)); the radio,
  the modem and the GPS can each be switched off in the Host preferences
  to save the battery.

#### Using it on the board

What the T-Deck Pro has that a desktop newton does not:

* **The quick sheet**: a finger pulled down from the screen's top edge
  opens a sheet across the top with **Lock screen** and a checkbox each
  for the 4G module, the GPS and the LoRa radio (only those the board
  has); pulled again, it closes.  A tap at the very top still reaches the
  Newton - only a pull 40 px down is the sheet's.
* **The lock screen** covers everything with the time, the date, the
  notifications applications have left on it and a slider: drag its knob
  from the left end all the way to the right to unlock (a tap, or a knob
  let go short of the end, does nothing).  While locked the keyboard is
  ignored and its light is off.  It is on every host, and locks by itself
  after the time chosen in Prefs, Host, **Auto-lock**
  ([docs/host-lock-screen.md](docs/host-lock-screen.md)).
* **The keyboard**: Shift is the Newton's Shift; **Sym** held gives the
  digits and punctuation; **Alt** held gives Esc (Q), End (E), Up (U),
  Page Up (P), Home (A) and Down (D), and with any other key is the
  Newton's Command key (Alt-C copy, Alt-V paste ...).
* **Prefs, Host page** (Extras → Prefs → Host) has the board's settings:
  the 4G module and the GPS switched on or off (kept across restarts),
  the e-paper panel's refreshes - *A new page drawn in full*, *Clean up*
  after so many quick updates, *... and when still*, *... once* so much
  has changed, *Small changes cleaned up* - and **Clear ghosts**, one full
  redraw of the panel ([docs/host-settings.md](docs/host-settings.md)).
  The LoRa radio's power, its settings and MeshCore's regional presets
  are the Mesh application's own Prefs (its "i" button).
* **Messages** and **Mesh** appear in Extras only on hardware that has
  the modem and the radio; a message arriving while its application is
  closed puts up a slip with **Show** to open the conversation - with the
  screen locked, a notification on the lock screen instead (tap it, then
  unlock, to open the conversation).
* **The serial console** (the board's USB port, any serial terminal) is a NewtonScript
  prompt once the OS is up, plus the board's own commands: `!shot` (what
  the panel shows, for `tools/tdeck/panelshot.py`), `!tap x y`,
  `!time <seconds since 1970>`, `!prof`, `!reset`; and
  `DevicePanelSet(name, value)` for the panel's finer settings
  ([src/host/esp32/README.md](src/host/esp32/README.md)).

On a desktop the lock screen and its auto-lock are always there (Prefs,
Host, Auto-lock; `ScreenLockNow()` from a script), and the quick sheet can
be opened from a script with `GetGlobalVar('|QuickSheet:host|):Pulled()`
(`src/host/demo/quicksheet.ns`).  Any application can leave a
notification on the lock screen: [docs/host-lock-screen.md](docs/host-lock-screen.md).

The board found bugs of the reconstruction that x86 had hidden (a glue
function dropping its result, a one-byte overrun, an unwinder that cannot
leave a task's first frame), all fixed in the tree.  Still open: a real
call and a message exchanged with another MeshCore node on the board,
the OS's applications at 240x320 (several have been fitted, others not),
and NewtonScript's speed in PSRAM.

### Two executables: `newton` and `newton64`

`newton` is the faithful machine: a NewtonScript integer is the ROM's 30
bits and wraps where the MessagePad's does, bugs and all.  `newton64` (and
`newtonscript64`) is the 64-bit NewtonScript flavour, `NEWTON_NS64`:

* integers are 62 bits - sums no longer wrap at ±2^29, literals run to
  2^61-1, `ExtractLong` reads any signed word;
* time is 64-bit aware - `TimeInSeconds()` is the true count of seconds
  since 1993, not the value that wrapped in January 2010;
* every persistent and wire format stays 32-bit - stores, packages, NSOF
  (beaming, docking, endpoints), soup index keys and the ARM interpreter's
  package code - with one narrowing policy where a wider value crosses
  (`src/frames/NarrowRef.h`: wrapped to the device's 30 bits, or
  `NEWTON_NS64_STRICT` to throw instead).

So a store is the same bytes whichever wrote it: one store file can be used
by `newton` and `newton64` in turn (a value wider than 30 bits that
`newton64` stored reads back in `newton` as the device would hold it - ctest
`host.NewtonSharedStore`), though only one program may have a store file
open at a time.  NS64 is a compile-time change across every library, so
`newton64` is a second build of the tree, made on demand beside `newton`:

```powershell
cmake --build build/host --target newton64      # build\host\host\newton64, newtonscript64
build\host\host\newton64 --store my.store
```

Configure with `-DNEWTON_BUILD_NS64=ON` to build it with every build (and to
register its ctests, `host.Newton64Setup` and `host.NewtonSharedStore`; after
an on-demand build, configure again to register them).  The whole story -
what changes, the boundary, time, the tests - is
[docs/frames/64bit.md](docs/frames/64bit.md).

## Contents

| Path | What it is |
|---|---|
| `DebugRom/` | Debug ROM images (`... image`, AIF format with symbol table) and ROM extensions (`... high`) for the MP2100 D (2001) and MP2x00 US (1997) - not needed to boot; used to check the reconstruction against the original and by the analysis tools |
| `headers/` | C/C++ headers from the Newton Driver Developer Kit (DDK): kernel (OS600), Frames object model, CommAPI, PCMCIA, QD, UtilityClasses, … — the primary source of struct/class layouts for reconstruction (classic Mac CR line endings) |
| `documentation/` | Newton Programmer's Guide / Reference, NewtonScript language and bytecode specs |
| `tools/newton-rom/` | **Our tooling**: ROM extraction, demangling, Ghidra import — see its [README](tools/newton-rom/README.md) |
| `src/` | The reconstruction itself, organised by functional area and buildable on a host — see [src/README.md](src/README.md) |
| `romsrc/` | The ROM's NewtonScript world and resources as editable source, built into what `newton` boots — see [romsrc/README.md](romsrc/README.md) |
| `fixtures/packages/` | Third-party Newton packages the tests install and use |
| `docs/` | Reverse-engineering notes per subsystem, starting with the kernel ([docs/os600](docs/os600/README.md)); generated tables are marked as such |
| `docs/curiosities.md` | The findings worth telling somebody about: clever tricks, shipped bugs, and the compiler idioms that are easy to misread |
| `docs/next-steps.md` | Where the last piece of work left off and what is obviously next, with the groundwork already read out of the ROM |
| `tools/host/`, `tools/remarkable/`, `tools/tdeck/` | Tools for the host builds: sampling, profiling and the 32-bit build; the reMarkable's packaging and probes; the T-Deck Pro's source list, serial console, profiler and panel screenshots |
| `tools/mpdumper/` | Alexey Danilchenko's 2004 symbol dumper (libiberty demangler) and its pre-generated symbol listings for the US ROM; used as the reference oracle for our demangler |

## Reproducing the Ghidra project

Requirements: Python 3.9+, [Ghidra](https://ghidra-sre.org/) 11.3+ (we use 12.1.3)
and a JDK 21 for it.

```powershell
python -m venv build\venv
build\venv\Scripts\pip install -r tools\newton-rom\requirements.txt
build\venv\Scripts\pip install --no-index --find-links "<ghidra>\Ghidra\Features\PyGhidra\pypkg\dist" pyghidra
build\venv\Scripts\python tools\newton-rom\pipeline.py "DebugRom\MP2x00 US" -o build\MP2x00US --ghidra <ghidra> --name MP2x00US
```

Result: `build/ghidra/MP2x00US.gpr`, a Ghidra project of the ROM as the CPU sees
it, with ~35,000 functions named and placed in ~1,050 C++ class namespaces,
16,000+ prototypes (1,500 of them complete with return types and parameter
names from the DDK headers), the DDK's ~250 structs/classes and 60 enums as
Ghidra data types, class sizes and vtables recovered from the constructors
(and checked against the headers), and the patchable jump table resolved to
thunks. Details,
the memory layout, and the analysis behind it are in
[tools/newton-rom/README.md](tools/newton-rom/README.md).

Run the tests with `python tools/newton-rom/tests/test_demangle.py`,
`python tools/newton-rom/tests/test_rom.py` and (with libclang installed)
`python tools/newton-rom/tests/test_headers.py`.

## Project rules

* Every tool used in the process lives in this repository under `tools/`, is
  documented (purpose, inputs, outputs, exact invocation) and is runnable from a
  clean checkout so that others can reproduce each step.
* Findings about the ROM (layout, tables, conventions) are recorded in the
  tool READMEs and, where possible, asserted by tests against the images.
