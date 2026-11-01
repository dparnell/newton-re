# A LilyGO T-Deck Pro host: what it would take

*Begun as an investigation (2026-10-10, branch `tdeck-pro-investigation`);
the OS has since been booted and run on the owner's board - the dated
sections from "The ESP-IDF skeleton" on are that work.  What was measured
on the development machine says so.*

The question: the MessagePad 2x00 ran this OS in a few megabytes, so could
the reconstruction run on a small device like the
[LilyGO T-Deck Pro](https://lilygo.cc/products/t-deck-pro) - an ESP32-S3
with an e-paper screen, a keyboard and a touch panel?

**The short answer: the memory fits, the code fits, and the code is
correct at 32 bits** - both user walkthroughs pass on a 32-bit `-Os` build,
with a 1 MB NewtonScript heap and task stacks no deeper than 24 KB, once
one alignment assumption was made explicit (below).
What stands in the way is not size but three pieces of work and one
question of fit:

1. **The ROM's objects must run from flash.** Today the 5 MB object file
   is read into RAM and a second, converted copy is made of it; on the
   device that alone would use the PSRAM. It needs to be baked on the
   development machine into the device's own layout and mapped from flash
   read-only.
2. **The stores must be flash partitions**, not a whole flash image held
   in RAM.
3. **A FreeRTOS task runtime** in place of one host thread per Newton
   task, plus drivers for the panel, the touch screen and the keyboard.
4. **The screen is smaller than any the OS 2.x applications were laid out
   for** (240x320 against 320x480), one bit deep, and touched with a
   finger. The OS boots and draws at that size; its applications are a
   different matter.

## The machines

| | MessagePad 2100 | T-Deck Pro |
|---|---|---|
| Processor | StrongARM SA-110, 162 MHz, 16 KB I + 16 KB D cache | ESP32-S3: two Xtensa LX7 cores, 240 MHz |
| Working memory | 4 MB DRAM (`hal/host/System.cpp` `GetRamSize`) | ~512 KB internal SRAM + 8 MB **quad** (QSPI) PSRAM |
| ROM | 8 MB mask ROM | - |
| Flash | 4 MB (the internal store, `hal/host/HostFlash.h`) | 16 MB, code and data both mapped through the cache |
| Card | PC Card slots | microSD |
| Screen | 320x480, 16 grays, backlit LCD | 240x320 e-paper (Good Display GDEQ031T10, UC8253 over SPI), 1 bit |
| Input | resistive pen; optional keyboard | capacitive touch (CST328, finger); keyboard (TCA8418) |
| Sound | speaker, microphone | variant: I2S DAC, mic, speaker (the other variant has a 4G modem instead) |
| Radios | IrDA | Wi-Fi, BLE, LoRa (SX1262), GPS; 4G on one variant |

The device facts are from LilyGO's product page and its
[T-Deck-Pro repository](https://github.com/Xinyuan-LilyGO/T-Deck-Pro)
(pins, ICs, board revisions V1.0/V1.1/MAX, which need different
firmware). LilyGO's README calls the PSRAM octal; **on the owner's board
it is quad** (octal does not start: "PSRAM ID read error"; the owner's own
board file says `qio_qspi`) - what the device spike found is below. The
ESP32-S3 maps flash and PSRAM through one 32 MB window, which the 8 MB
PSRAM and a mapped part of flash share (ESP-IDF's external-RAM guide).

The MessagePad's "8 MB of RAM" is really 4 MB of DRAM and a 4 MB flash
store; the T-Deck Pro's 8 MB PSRAM is the DRAM's counterpart, with the
store going into its own 16 MB flash (or the microSD card).

## What the reconstruction needs (measured on the development machine)

### Code: 4 to 5 MB - fits

`newton` built for 32-bit ARM Linux at `-Os` (`src/cmake/zig-arm-linux.cmake`,
`-DCMAKE_BUILD_TYPE=MinSizeRel`, stripped; `tools/host/codesize.py <build>`
reads it): **4.7 MB** (4,956,276 bytes), of which 3.6 MB code, 0.66 MB
read-only data, 0.5 MB exception tables, 13 KB initialised data and 280 KB
zeroed data.
Xtensa code is usually denser than ARM's 32-bit instructions, so the
device's should be no bigger. Before the linker drops what is unused, the
libraries come to 4.0 MB, the largest being handwriting recognition
(1.0 MB, mostly the recogniser's network weights), the views (0.55 MB),
NewtonScript (0.43 MB), the stores (0.22 MB) and the ARM interpreter for
packages' native code (0.18 MB).

(The 27 MB `newton.exe` of the usual Windows build is unoptimised and
carries debug information; it says nothing about a device build.)

### Working memory: about 1.5 MB at rest, plus the ROM data

A headless boot on Windows has a working set of about 100 MB, but almost
none of that is the Newton's:

- **The kernel heap** is a 32 MB allocation (`memory/host/KernelHeap.cpp`
  `kHostKernelHeapSize`). A heap walk after a boot (NEWTON_HEAPCHECK with
  NEWTON_HEAPDUMP, totalled) finds **5.24 MB** reached, **4 MB of it the
  NewtonScript object heap** (`--heap`, default 4 MB), and that object heap
  has **4,073,144 bytes free** after the boot (`Stats()`) - about 120 KB
  used. Everything else the kernel and the services allocate comes to
  about 1.2 MB.
- **The flash store** is held whole in RAM (`hal/host/HostFlash.cpp`, a
  `std::vector` the size of the flash - 64 MB by default). On the device
  it would be a flash partition read through the cache and written with
  the flash driver.
- **The ROM's objects**: `romsrc-objects.bin` (5.1 MB) is read whole into
  RAM and kept (`frames/ROMImport.cpp`), and a converted copy - host byte
  order, every reference turned into a host address, an index of the
  46,538 objects - is made beside it (`frames/ObjectAreaImport.cpp`). The
  objects are never written once imported (`kObjReadOnly`; `LockRef` and
  `DirtyObject` pass them by), so a copy baked on the development machine
  in the device's layout could be mapped from flash and used in place.
  The file itself is the same on every host (`docs/host-remarkable.md`).
- **Task stacks**: one host thread per Newton task, 8 MB reserved each on
  Linux; the walkthroughs peak at 21 threads, and at `-Os` none goes
  deeper than 24 KB (below).

So a device budget of roughly: 1-2 MB object heap, 1.5-2 MB for the rest
of the kernel heap, under 1 MB of task stacks, the screen (240x320 at one bit is
9.6 KB; the driver's own copy and QuickDraw's map a few tens of KB), all
in PSRAM, with the ROM objects (about 3 MB baked) and the code in flash -
leaves most of the 8 MB PSRAM spare.

### Correct at 32 bits: yes, with one fix

Nothing in the tree had been built for a 32-bit target before. Built for
32-bit ARM and for 32-bit x86 (`src/cmake/zig-x86-linux.cmake`, which WSL 2
runs - `--objects build/host/romsrc-objects.bin`, the object file being
the same on every host), the whole tree
compiles but for one call (`recognition/Areas.cpp`: `nil` passed for an
integer, an error where `nil` is `nullptr` - fixed). Then:

- **zig places a 32-bit program at 0x10000**, so every host function falls
  below `kROMCodeLimit` (0x02000000, `frames/NativeFunctions.h`) and is
  taken for a ROM address - the reMarkable's trap (`docs/host-remarkable.md`,
  PIE). Linked at 0x08000000 the 32-bit debug build boots, and **the first
  user walkthrough (`src/host/demo/walkthrough.ns`) passes, 0 failed**.
  On the ESP32-S3 code lives at 0x42000000 and up, so the device is not
  affected.
- **A 32-bit `-Os` build crashed at once**, jumping into its own heap from
  `ObjectDestroy` → `TObjectTable::Remove` → `CallDestructor`. The ROM
  clears the low two bits of a destructor's address before calling it
  (`bic #3`; `os600/kernel/ObjectTable.cpp`, `DoubleQ.cpp`), and a
  native's function pointer sits in a NewtonScript array slot as a raw Ref
  whose low bits are its tag (`frames/Interpreter.cpp` `MakeCFunction`):
  both assume code addresses are multiples of four, as the ARM's are. x86
  at `-O0`, `-O2` and `-O3` aligns functions to 16; at `-Os` it does not
  align them at all. **Every build now asks for four-byte alignment**
  (`-falign-functions=4`, `src/CMakeLists.txt`); Xtensa aligns function
  entries to four anyway. A Thumb build could never keep this (bit 0 of a
  Thumb function pointer is set): build ARM code.

What the crash took to find is now in the tools: the crash handler names
the faulting instruction on 32-bit x86 and ARM Linux as well as 64-bit,
and when a fault is a jump to somewhere outside the program - where the
unwinder stops - it prints the words on the stack that point into the
program, the return addresses (`host/newton.cpp`); `tools/host/whichfunction.py`
reads 32-bit ELF images and programs that are not position-independent.

With four-byte alignment the 32-bit `-Os` build (`i386-linux-gnu`, zig,
run under WSL 2) passes **both user walkthroughs and their restarts, 0
failed**, and the first again with the NewtonScript heap cut to 1 MB
(`--heap 1048576`).

### Task stacks: tens of kilobytes

Each task's stack on Linux is its own mapping, so the pages a run touches
show how deep it went (`tools/host/stackdepth.sh <newton> <arguments>`
samples `/proc/<pid>/smaps` each second through a run). Built at `-Os`, 32-bit: **the deepest of the 16-17 task
stacks reached 24 KB, most 8 KB, 156-180 KB in all**, with at most 21
threads. (Unoptimised, every stack reached 260-300 KB: an `-O0` build's
frames are no guide.) So 32 KB stacks with a guard, measured again on the
device, rather than the 8 MB a Linux thread reserves.

### The screen: the OS fits, its applications may not

Booted at 240x320 (`--display 240x320x4`, and `x1`), the OS comes up and
draws: the button bar, the alerts, Setup's Welcome page. But the first
thing it shows is Setup refusing the portrait screen ("This cannot operate
in this orientation. Should the screen be rotated before opening?"), and
rotated it refuses again ("The backdrop icon, Setup, cannot operate while
rotated"). The OS 2.x applications were made for 320x480 and the eMate's
480x320; 240x320 is smaller than both in both directions, and each
application decides for itself what it will open at (`GetAppParams` here:
240 wide, 274 high above the button bar). At one bit, the button bar's
gray turns to a black dither and its labels are lost.

Three ways on, not exclusive:

- **Run the applications at the size they ask for and fix what does not
  fit**, one by one, as host-side layout (each a DEVIATION). The most work,
  and the best result on the panel.
- **Draw a 320x480 Newton and scale it to 240x320** (three quarters) for
  the panel. Everything fits at once, but espy 9 becomes about 7 pixels
  high, which one bit does not render legibly.
- **A larger e-paper device** for the same work (the reMarkable host shows
  the e-ink side is solved).

Whichever is chosen, the e-paper lessons of the reMarkable host carry
over: redraw only what changed, a fast waveform for ink and typing and a
clean full refresh now and then (`docs/host-remarkable.md`, "Lag and
full-screen redraws", "Only what changed"). And grays become a dither (or
the panel's gray modes, if it has them).

### Input

- **Touch is a finger, not a pen.** Taps and drags work as a pen's do, but
  a finger cannot write: the handwriting recogniser (a quarter of the code)
  is of little use, and ink notes are coarse. With the keyboard it makes
  sense to leave recognition out of a device build, or keep it for drawn
  shapes only.
- **The keyboard** is a TCA8418 matrix: its key events map to the Newton's
  keyboard codes as the host's keyboard does (`host/HostKeyboard.cpp`).

## What a port would be made of

An ESP-IDF application (not Arduino: the tree's CMake builds as
components), C++17 with exceptions (`TTaskRedirect` and a few re-expressions
use real ones; `hasCppExceptions`), `-fwrapv`, `-fsigned-char`,
`-falign-functions=4`.

| Seam | Today | On the device |
|---|---|---|
| Task runtime (`os600/kernel/host/TaskRuntime.cpp`) | a host thread per task, a baton on `std::mutex`/`condition_variable` | a FreeRTOS task per Newton task, all on one core, the baton a semaphore; stacks in PSRAM |
| Timers (`hal/host/Timer.cpp`, `HostFIQTimer.cpp`) | `std::chrono`, waitable timers | `esp_timer` |
| ROM objects (`frames/ROMImport.cpp`, `ObjectAreaImport.cpp`) | read and converted into RAM | a baked image in a flash partition, mapped read-only |
| Flash store (`hal/host/HostFlash.cpp`) | the whole flash in RAM | a 4 MB flash partition, mapped for reading, written and erased through `esp_partition` |
| Cards (`hal/host/HostCard.cpp`) | image files | files on the microSD card |
| Screen (`hal/host/HostScreen.h`, a `host/HostWindow.h`) | a window, or the reMarkable's panel | the UC8253 over SPI, partial refresh of what changed |
| Pen (`hal/host/HostTablet.h`) | mouse, the reMarkable's Marker | the CST328 |
| Keyboard (`host/HostKeyboard.cpp`) | the window's keys | the TCA8418 |
| Network (`hal/host/HostSockets.cpp`) | BSD sockets | lwIP's BSD sockets over Wi-Fi - most of it as it is |
| Sound (`hal/host/HostSoundDriver.h`) | waveOut, ALSA | none, or I2S on the audio variant |
| Files, `getenv`, restart | 25 files open files, 54 read the environment | the SD card or nothing; options compiled in |

Left out of a first device build: printing, fax and the modem, the NIE,
the ARM interpreter for packages' native code, the host's own fonts - none
can be left out by a CMake option today (the libraries reach each other by
design); each needs a stub.

## What the device spike showed (the owner's board, 2026-10-10)

`tools/tdeck/spike/` (`tools/tdeck/README.md`), run on the owner's
T-Deck Pro; every figure below is the spike's own output.

- **The board**: a **V1.0** (neither the DRV2605 at 0x5A nor the XL9555
  at 0x20), ESP32-S3 revision 0.2, 16 MB flash, **8 MB quad PSRAM** (8,189
  KB free), 339 KB internal RAM free with 248 KB the largest block. On
  I2C: touch 0x1A, the IMU 0x28, the keyboard 0x34, the fuel gauge 0x55,
  the charger 0x6B (the light sensor at 0x23 does not answer).
- **The panel works from the UC8253 datasheet alone**: the Newton screen
  (`tools/tdeck/pgm2epd.py` of a 240x320 one-bit desktop boot) is upright,
  black on white and crisp (the owner's eye). Refresh times, BUSY from
  `DRF` to its end:

  | Refresh | Time |
  |---|---|
  | full (the OTP waveform) | 3.08 s |
  | forced temperature 0x5A (CCSET TSFIX, TSSET) | 1.01 s |
  | forced temperature 0x6E | 1.54 s |
  | **forced temperature 0x7F** | **0.65 s** |
  | a 64x64 window (PTIN/PTL), the full waveform | 3.07 s |
  | the window at 0x5A | 1.01 s |

  The fast refreshes looked clean, the window refreshed alone. A window
  costs as long as the whole screen at the same waveform: partial
  refresh saves the flash, not the time. So with the OTP's waveforms the
  quickest update is **0.65 s**; anything faster needs a waveform of our
  own (`REG` = LUT from registers) - not tried.
- **A full refresh over a window greys the rest of the panel**
  (2026-10-13, the panel's set-up of `tdeck_panel.cpp`, `DevicePanelSet("test",
  2)` photographed every half second with `tools/tdeck/camshot.py`): the
  window went through the full waveform's flash and came out clean, but
  the white outside it - rows the window does not scan included - went
  from 160 to 135 in the camera's levels and stayed there, while the
  white inside came back to 153 as before (so not the camera's exposure).
  The full waveform swings the common electrode, which is the whole
  panel's.  Two partial refreshes over the window instead, to it inverted
  and back (`"test", 3`), left the white outside at 155 as it was; the
  black inside came back a little lighter than around it (31 against 24).
  So the panel's local clean-up (`CleanArea`, setting `localClean`) is the
  flip and back.
- **What the refresh shows is what was sent with DTM1 (0x10)**, not DTM2:
  the datasheet calls DTM1 "B/W or OLD" and DTM2 "Red or NEW", and sending
  the new frame to DTM2 showed, every time, the frame sent to DTM1 - the
  refreshes looked one behind, and a screen drawn from white came out
  blank. Every run's screens fit that once looked at together; the
  spike now sends the image to DTM1 and the frame before it to DTM2, and
  draws what it is asked to.
- **Never deep-sleep the panel**: the controller leaves DSLP only by a
  hardware reset, and the T-Deck Pro does not wire up its RES#; after
  DSLP it stayed busy until the board lost power. Power it off (POF).
- **Memory** (built `-O2`; 200,000 dependent reads, each address made
  from the word read before, xorshift; then a sequential sum):

  | | hits the cache | misses | sequential |
  |---|---|---|---|
  | internal SRAM | 58 ns | - | 240 MB/s |
  | PSRAM (quad, 80 MHz) | 63 ns | **1.0-1.15 µs** | 27.5 MB/s |
  | flash, mapped | 63 ns | **1.8-2.1 µs** | 15.3 MB/s |

  **A miss costs 250-500 processor cycles.** This is the port's largest
  performance risk: the interpreter walks objects spread over megabytes
  (the object heap in PSRAM, the ROM's objects in flash) through a 32 KB
  data cache. What helps: the hottest data in the 512 KB of internal
  SRAM (the interpreter's stacks, the symbol table, the most used ROM
  objects), a 64 KB data cache with 64-byte lines (`CONFIG_ESP32S3_DATA_CACHE_64KB`,
  `_LINE_64B`), the ROM's objects copied into PSRAM rather than read from
  flash where they fit (PSRAM misses cost half a flash miss). Only real
  NewtonScript on the board will say how slow it is in practice.
- **The keyboard** works as the owner's lilyphone1 project has it (a 4x10
  matrix on the TCA8418, its layout): keys and their releases arrive as
  events about 10 ms apart.
- **Touch** (CST328, reports as LilyGO's code reads them) is in the
  panel's own pixels - no rotation, mirroring or scale - at about 100
  reports a second while a finger is down. Five 16x16 targets tapped
  five times each by the owner:

  | target | where the taps were read (mean) | offset |
  |---|---|---|
  | (16, 16) top left | (21, 6) | about +5, -10 |
  | (224, 16) top right | (237, 4) | +13, -12 |
  | (224, 304) bottom right | (238, 301) | +14, -3 |
  | (16, 304) bottom left | (20, 308) | +4, +4 |
  | (120, 160) centre | (127, 168) | +7, +8 |

  Repeated taps fall within a few pixels of each other, with a steady
  pull of about 7 px (1.5 mm) down and to the right away from the
  edges; within about 10 px of an edge the position runs into the limit
  (x 236-239, y 1). A tap is one or two reports. So a finger hits within
  about 2 mm: the OS 2.x's checkboxes and scroll arrows (10-13 px, 2-2.5
  mm here) can be hit, not easily; a calibration offset helps; the edges
  need a margin; and a finger cannot write.
- **The tree compiles for the ESP32-S3**: every file of a 32-bit build
  compiled again with ESP-IDF's GCC 12.2 (`tools/tdeck/xtensa_compile.py`
  over the ARM build's compile commands: C++17 with exceptions, `-Os`,
  `-fwrapv`, `-fsigned-char`, `-falign-functions=4`, `-mlongcalls`) -
  **674 of 678 files**, 3.46 MB of code and data before linking (32-bit
  ARM's: 4.0 MB). The four left are the host's own platform code, which
  a device port replaces: `os600/kernel/host/TaskRuntime.cpp` (Linux
  threads), `hal/host/System.cpp` (`pthread_getattr_np`),
  `hal/host/HostSockets.cpp` (BSD sockets - lwIP's instead),
  `host/newton.cpp` (`dlfcn.h`). On the way GCC found two things clang
  had let pass, both fixed: `ULong32` typedef'd a second time as
  `unsigned int` in three files (in the ESP32's C library `uint32_t` is
  an `unsigned long`, so the two conflict), and a member with a
  constructor inside an anonymous union (`MemObjRequest`'s
  `fPersistentEntry`, now its bytes). Nothing is linked yet: this says
  the code compiles, not that it runs.
- **Builds**: PlatformIO installs ESP-IDF 5.1 and its toolchains once
  (about 1.5 GB); after that the spike rebuilds in about 20 s.

## A device-sized run (on the development machine, 2026-10-10)

The 32-bit build (`src/cmake/zig-x86-linux.cmake`, `-Os`, run under WSL 2)
given a device's budget - `newton --kernel-heap MB --heap bytes
--flash-size 4 --task-stack KB` - and both user walkthroughs with their
restarts run on it (`tools/tdeck/devicerun.sh <newton> <options>`, which
prints each run's result, `NEWTON_MEMORY_REPORT`'s figures and GNU time's
peak resident set). Every run below: **4 of 4 passed, 0 failed**.

| kernel heap | NewtonScript heap | task stacks | flash | kernel heap used at most | peak resident (process) |
|---|---|---|---|---|---|
| 32 MB (default) | 4 MB | 8 MB | 64 MB | 4.8-4.9 MB | 82-88 MB |
| 3 MB | 1 MB | 32 KB | 4 MB | 1.69-1.79 MB | 20-25 MB |
| 2 MB | 1 MB | 32 KB | 4 MB | 1.69-1.79 MB | - |
| **2 MB** | **512 KB** | **32 KB** | **4 MB** | **1.18-1.27 MB** | - |

So the OS itself - everything in the kernel heap but the NewtonScript
heap - comes to about **0.7 MB** at its peak through a user's first hour,
and the NewtonScript heap the walkthroughs need fits in 512 KB (the
MessagePad 2100 had 4 MB of DRAM for both). What a real user's notes and
packages need is more; the point is that the OS leaves nearly all of the
device's 8 MB to them.

What the process holds besides, on the desktop, and what the device does
instead:

| | the desktop (32-bit) | the device |
|---|---|---|
| the ROM's objects | **9.4 MB**: the object file kept whole (5.0 MB) and the converted area with its index (4.4 MB; 6.9 MB on a 64-bit host) | a baked image in flash: nothing in RAM, or the hottest objects copied to PSRAM for speed (the spike: a PSRAM miss costs half a flash miss) |
| the flash store | the whole flash as a `std::vector` (`hal/host/HostFlash.cpp`): 4 MB here, 64 MB by default | a flash partition, read through the cache |
| the code | the program's pages (5.8 MB file) | in flash, executed in place |
| task stacks | 21 threads x 32 KB | 21 FreeRTOS tasks x 32 KB, in PSRAM or internal SRAM |

So, on the device: about 2-3 MB of PSRAM for the kernel heap with the
NewtonScript heap inside it, under 1 MB of task stacks, the screen's few
tens of KB - and the ROM's objects either left in flash or (all 4.4 MB,
or the most used of them) copied into the PSRAM. All of it in 8 MB, with
room over.

## The baked object image (2026-10-10)

`newton --bake-objects <image> [--bake-base <address>]` imports the object
file as a boot does, but lays everything the import makes out in one
region reserved at the address given - the host's objects, their sorted
index, the magic pointers, the lexicons and the other blocks, the moved
objects' table - with a header in front, and writes the region out as it
lies (`frames/ROMImport.h` `BakeBuiltObjectsFile`; the importer's
allocations go through a hook, `gImportAlloc`). `newton --objects
<image>` maps the file read-only at that address (`host/HostMapping.h`:
`MapViewOfFileEx`, `mmap`) and points the importer's globals into it; the
constants the C++ names are looked up as after an import. Nothing is
read, converted or held: on a device the image is a flash partition,
mapped where it was baked.

- **It is the host's own layout** (pointer size, object header, object
  alignment, byte order), so it is baked by a build of the kind that maps
  it; the header records the layout and the object file's builder stamp,
  and a mismatch is refused with a message. For the ESP32-S3: a 32-bit
  little-endian build (the i386 one) has the same layout, baked with
  `--bake-base` set to the address the device maps the partition at.
- **Nothing writes to a ROM object**: both walkthroughs and their
  restarts pass with the image mapped read-only (a write would fault) -
  on the desktop (64-bit; ctest `host.NewtonBakedObjects`, with the bake
  as its fixture `host.NewtonBakedObjects.bake`) and on the 32-bit build
  with the device budget (`tools/tdeck/devicerun.sh ... --objects
  <image> --kernel-heap 2 --heap 524288 --flash-size 4 --task-stack 32`:
  4 of 4, 0 failed).
- **What it saves**: the image is 5.4 MB at 32 bits (7.2 MB at 64), in
  place of 9.4 MB of RAM (the object file kept and its converted copy).
  On the 32-bit device run the RAM left is the 2 MB kernel heap and
  1.2 MB of frames imported from the **built-in packages** (the ROM
  extension's, installed at boot) - which could be baked the same way;
  not done yet.
- On the way: `TImportedObjectArea::Dispose` freed the area's aligned
  pointer, not the one `malloc` gave (harmless while `malloc`'s pointers
  are already aligned); it now keeps and frees the latter.

## The ESP-IDF skeleton: milestone A (2026-10-11)

`src/host/esp32` (its README says how to build, bake and flash it) is an
ESP-IDF application, built with PlatformIO, that runs **NewtonScript on
the T-Deck Pro**: the ROM's objects mapped from flash as a baked image
(baked by the 32-bit x86 build for the address the board maps the
partition at), the object system, every area's natives, and the
NewtonScript prompt on the USB serial port. The OS's tasks are not
started yet - that is milestone B.

- **The tree as it is**: one component of 650 of the tree's sources with
  the desktop's flags, listed by `tools/tdeck/idf_sources.py` from a
  32-bit build's compile commands. Left out: tests, `src/host/` (but for
  `HostNatives.cpp`), and the host's task runtime and sockets; added: a
  board side and a Newton side (their headers do not mix) and the task
  runtime's entry points for one task. **The program links with 13
  symbols supplied** - nothing else in the tree had to change for the
  board but:
  - `hal/host/System.cpp`: an `ESP_PLATFORM` branch (the stack's bounds
    from FreeRTOS);
  - two includes made to name their directory (`TaskRuntime.cpp`'s
    `RealTimeClock.h`, `HostFlashDriver.cpp`'s `Flash.h`): three header
    names exist in two directories each, and PlatformIO puts include
    directories in alphabetical order rather than the build's;
  - the baked image's layout check measures the header to its last field
    (32-bit x86 and Xtensa pad its end differently).
- **What the build had to get round** (all in the component's
  CMakeLists): PlatformIO names an object by its source's file name alone
  (five pairs of files share one: `forwarded/` compiles each through a
  file of its own name); it sorts and splits flags (`-include` joined to
  its path, the source directory made absolute); the tree's include
  directories go in as `-iquote`, so that `src/intl/Locale.h` is not found
  for the C library's `<locale.h>` on a case-insensitive file system.
- **On the board**: the program is 2.76 MB; with the REP running 128 KB of
  internal RAM and 5 MB of PSRAM are free. The baked objects take 11 ms to
  bring up (mapped, not read), the object system 0.7 s, the natives 1.1 s.
- **NewtonScript's speed** (`DeviceMilliseconds()`, against the 32-bit
  `-Os` desktop build on the development machine, timed 100 times larger
  and divided):

  | | the T-Deck | desktop (32-bit) | ratio |
  |---|---|---|---|
  | a 100,000-pass integer loop | 437 ms | 3.7 ms | ~120x |
  | 2,000 strings made (`NumberStr`, `&`) and stored | 979 ms | 1.9 ms | ~500x |
  | 2,000 integers made and `Sort`ed | 49 ms | 0.6 ms | ~80x |

  A 240 MHz Xtensa core is some tens of times slower than the desktop's;
  the loop and the sort are about that. The allocating test is several
  times worse: the NewtonScript heap is in PSRAM, where a cache miss costs
  a microsecond, and a 1 MB heap is collected far more often than the
  desktop's 4 MB. That is where tuning starts (the object heap's hottest
  parts in internal SRAM, the allocator's and the collector's walks).
- **Flashing**: esptool's stub sometimes stops answering after the
  program has run ("No serial data received"); taking power off the board
  clears it. `upload_speed = 115200` (no baud change) is set.

## The OS booted: milestone B (2026-10-11)

The same application now **boots the OS**: the kernel and its tasks on
FreeRTOS, the newt world with a 240x320 one-bit display (in memory, not
on the panel yet) and the store in RAM, with the NewtonScript prompt on the
USB serial port - each line the console types is handed to the newt world
as a `'host/'eval` event by the kernel services task, evaluated there and
its result printed. `2 + 3;` answers 5 in a millisecond;
`GetRoot().viewBounds` is 240x320.

- **The task runtime on FreeRTOS**: `os600/kernel/host/TaskRuntime.cpp`'s
  `ESP_PLATFORM` branch makes a Newton task's thread a FreeRTOS task (the
  board's side makes it: 48 KB stack in PSRAM, control block in internal
  RAM, all on core 1 at one priority); the runtime's baton - a mutex and a
  condition variable per task - is unchanged, through ESP-IDF's pthreads.
  Now built for the board too: `HostViews.cpp` (the newt world's host boot
  and display) with the card and interconnect natives it registers,
  `HostStores.cpp` with `FactorySoups.cpp`, and the sockets on lwIP
  (`HostSockets.cpp`: no interface list, no reverse lookup, no SIGPIPE).
- **Start**: the kernel and its tasks 10 s, the newt world another 17.5 s,
  the OS up (`gNewtIsAliveAndWell`: the built-in applications installed)
  at 62 s. 29 KB of internal RAM and 2.6 MB of PSRAM are left; 285 KB of
  the internal RAM is the tree's static data. The start is the next thing
  to make faster.

What the board found that the desktop had not - three of them bugs of the
reconstruction that x86's conventions hid:

1. **A catch in a FreeRTOS task's outermost function is never reached.**
   The task runtime's one C++ throw (`TTaskRedirect`, a blocked task sent
   elsewhere by the kernel) ended in `std::terminate`: its catch had been
   inlined into the thread's entry function, and the Xtensa unwinder
   cannot step through a task's first frame. The same catch a call
   further in works (shown with a probe on both cores, on internal and
   PSRAM stacks). `Trampoline` is now never inlined.
2. **The object manager's monitor answered with its own address.**
   `TObjectManager::MonitorProcGlue` was `void` (the DDK's
   `MonitorProcPtr` returns nothing) and dropped `MonitorProc`'s result;
   `MonitorEntryGlue` passes on whatever the proc left in the result
   register. On the ARM and on x86 that is still `MonitorProc`'s result;
   with Xtensa's windowed registers it is the glue's first argument, the
   manager, so every object request failed with an internal-RAM address
   for an error and the boot rebooted itself (`Reset` on the board prints
   a backtrace, which found it). The glue now returns the result.
3. **A config server wrote one byte past itself.** `TNSConfigServer`
   took its service type's four characters into the ULong at the end of
   the object with `ConvertFromUnicode(..., 4)`, which writes a fifth
   byte, the terminator (the ROM's own conversion, which the reconstruction
   stands in for, does not). The desktop's allocator rounds blocks up and
   hid it; ESP-IDF's heap - with `CONFIG_HEAP_POISONING_COMPREHENSIVE` -
   named the block. Now converted through a buffer.
4. **A FreeRTOS task that deletes itself must not be freed on `eDeleted`.**
   FreeRTOS answers `eDeleted` as soon as such a task is queued for the
   idle task, which still reads its control block afterwards; freeing a
   static task's memory then corrupted the heap. An ended thread now
   suspends itself, and the next thread started deletes it and frees it.

Also: the baked image is mapped at a fixed 0x3d000000. ESP-IDF maps a
partition after the program's mappings and the PSRAM's, so the address
moved with every change in the program's size (`esp32main.cpp`'s
`MapObjects` fills the space below with the partition's own first pages -
flash is mapped shared, so the whole partition's mapping is a new one,
above them). `tools/tdeck/xtensa_compile.py --warn` lists warnings
matching a pattern; it showed that no function in the tree falls off its
end (which GCC for Xtensa compiles as unreachable).

## The panel, the touch screen and the keyboard (2026-10-11)

`src/host/esp32/main/tdeck_panel.cpp` and `tdeck_input.cpp`: the Newton's
display on the e-paper panel, the touch panel as its pen and the keyboard
as its keyboard, through the same shims every host's window uses
(host/HostKeyboard.cpp). Typing, taps, the calibration screen and the
Extras drawer work on the owner's board.

**The panel took most of the day**, and what finally worked is the panel
maker's own way, all of it:

- black and white with the controller's OTP waveforms, PSR `0x1F 0x0D` (the
  spike had `0x8D`: then the refresh seemed to show DTM1, and an inverted
  "old" frame seemed to help - both artefacts of the set-up);
- a full refresh sends the picture to **both** DTM1 and DTM2 (the full
  waveform darkens a pixel black in either: the previous frame there came
  out over the new one, inverted popups);
- a partial refresh sends the old frame to DTM1 and the new to DTM2, CDI
  `0xD7`, the OTP waveform at the forced temperature `0x79` (0.65 s), **over
  only the rectangle that changed** (PTIN/PTL - the waveform drives every
  pixel it covers: over the whole screen, the whole screen blinked);
  0x79 is already in the OTP's fastest temperature band - a sweep of
  `partialTemperature` 0x79 to 0x7F with `tools/tdeck/panelprobe.py`
  (2026-10-13) passed the test pattern and took 650 ms at every value, so
  the forced temperature has nothing more to give;
- **the controller reset (PSR's RST_N) and set up again before every
  refresh**, which GxEPD2's driver for this panel does with the comment
  "needed, reason unknown": without it the first partial refresh after a
  full one works and the next does nothing - the controller keeps a state
  of its own across refreshes.
- A waveform of our own in the LUT registers left the controller busy
  until the board lost power; it is gone.

It was found by looking: a webcam pointed at the board
(`tools/tdeck/camshot.py`) photographed a test pattern the board draws
(`DevicePanelSet("test", 1)`: a full refresh, a partial one flipping half
a square, a partial one back), and `tools/tdeck/panelprobe.py` read the
photographs and tried 28 set-ups - only the one above went through all
three pictures. Before the camera, each try needed the owner's eyes.

**Two more bugs the board found** (the desktop's memory layout or C
library had hidden them):

1. **A click's unit came back as its address shifted down by two.** A
   command's parameter is a Newton integer; a host pointer that does not
   fit is kept as an address Ref, told apart by its width - which a 32-bit
   host's pointers do not have, and the ESP32-S3's PSRAM (0x3C000000) does
   not fit in thirty bits (the desktop's 32-bit builds' heaps are lower).
   Tapping a button crashed the OS. `views/Commands.cpp` now remembers the
   Refs it kept as addresses (test: views.Views).
2. **Every extended Gestalt answered nil**: the first buffer is
   `malloc(0)`, which newlib answers with nil; the Extras drawer's volume
   slider threw. Fixed there, and `malloc` is wrapped on the board so that
   `malloc(0)` answers a block everywhere, as the desktops' do.

Debugging on the board: `DevicePanelSet("trace", 1)` logs each refresh;
every NewtonScript throw is printed with its script stack
(`NEWTON_TRACE_EXCEPTIONS`, set by newtonmain.cpp); `serialcapture.py
--send-file` keeps one capture open that can be typed into (opening the
port again could reset the board).

## The store on the board's flash (2026-10-11)

The Newton's internal flash is the board's `store` partition (4 MB at
0xC00000; the baked objects' partition shrank to 5.9 MB to make room), so
the internal store - the ROM's TFlashStore, as on the desktop with a
`--store` file - survives a restart: an entry added to a soup reads back
after a reset.

- **No copy in RAM**: the desktop holds the flash's 4 MB in memory and
  writes it through to its file; the board has 2.5 MB of PSRAM free with
  the OS up. So hal/host/HostFlash.h gained a *backing*
  (HostFlashSetBacking): a flash whose bytes are read in place - the
  partition, memory-mapped read-only - and whose writes and erases are
  the backing's to do. The desktop's flash store test runs its scenario
  on a backing too (ctest stores.FlashStore).
- **Writing from the right task**: a task whose stack is in PSRAM - every
  Newton task's - must not write the flash (the cache, PSRAM's way in, is
  off while it does), so `tdeck_store.cpp` hands each write and erase to a
  task of its own with an internal stack, and waits. An erase of some
  lanes (the Newton's flash is two 16-bit chips side by side) reads, erases
  and rewrites the 4 KB sectors.
- **The mapping's cache**: ESP-IDF's refresh after a write did not reach
  this mapping - a word written read back as erased until the next start,
  and the new store would not mount (`kSError_ObjectNotFound`). The writer
  invalidates the cache lines itself; a self-test at each start checks
  write, rewrite, lanes erase and sector erase through the mapping.
- **Times**: the first start (the partition erased, 11 s, and the store
  formatted word by word) brings the OS up in about 170 s; later ones in
  about 60 s, as before. Writes are synchronous and word by word - creating
  a soup and adding an entry took 2 s. Batching them is the obvious next
  step if it matters (done: "Batched store writes", below).

## Batched store writes (2026-10-13)

The store's driver wrote its flash a word at a time, and on the board each
word was a hand-over to the writer task and an SPI program of its own.
Now a run of words is written in one go:

- **What a run is**: everything the store's driver writes between its
  `BeginWrite` and its `ReportWriteResult` (`THostFlashDriver`, which now
  calls `HostFlashBeginRun`/`HostFlashEndRun`, hal/host/HostFlash.h).
  Nothing reads the flash in between - the ROM's `T32BitFlashRange::DoWrite`
  (ROM 0x000c2abc) reads only its source - so the words can wait.
- **Gathered in HostFlash.cpp**: with a backing, consecutive words collect
  in a 1 KB buffer and go to the backing in one write; a word that is not
  the next, a full buffer or an erase writes what was gathered first, and
  a run that changes no bit is not written at all.  Without a backing
  (the desktop's file or memory flash) nothing changes.  A host DEVIATION.
- **On the board** (`tdeck_store.cpp`): a request carries the run's bytes
  by pointer, not 16 bytes copied in; the writer copies them into its
  internal-RAM sector buffer a sector at a time (the bytes may be in
  PSRAM, out of reach while the flash is written) and programs each piece
  with one `esp_partition_write`.  The start's self-test still passes.
- **Measured** (`tools/tdeck/storebench.py COM4`: ten Names cards added,
  then removed, timed by the Newton's `Ticks`, two runs each on a clean
  soup):

  | | word by word | batched |
  |---|---|---|
  | add 10 cards | 400-433 ms | 233-266 ms |
  | remove 10 cards | 300-333 ms | 183-200 ms |

  About 1.7x; what is left is mostly the store's own work, not the flash.
  On the desktop's backed scenario (ctest `stores.FlashStore`) 1,262,950
  words became 28,303 backing writes, the longest 1 KB.

## A faster start (2026-10-11)

The OS came up a minute after the start; now **3.2 s**. Found with a
sampling profiler (`src/host/esp32/main/tdeck_profile.cpp`: the
ESP32-S3's assist-debug block records core 1's program counter, which a
task on core 0 reads every millisecond; `tools/tdeck/profile.py` names
the samples):

| | before | after |
|---|---|---|
| C++ exception tables searched one entry at a time (`next_fde`, `linear_search_fdes`) | 50 s | - |
| the ROM extensions' headers searched for in the baked image (`FindRExHeader`) | 7.4 s | 0.2 s |
| native-function names compared with every entry (`RegisterNativeFunction`, `strcmp`) | 2.6 s | - |
| the OS up | 61.7 s | 3.2 s |

- **Espressif's libgcc does not sort the exception tables** unless
  `_Unwind_SetEnableExceptionFdeSorting(1)` is called (to save RAM: 4
  bytes an entry, ~22,000 entries, made at the first throw in 154 ms);
  without it every throw counts and searches them all - 22 ms - and the
  task runtime throws a `TTaskRedirect` at many task switches. A throw now
  costs 62 µs. This was most of the "120 times slower than the desktop"
  of milestone B's boot, and NewtonScript's own exceptions benefit as well.
- `packages/ROMPackages.cpp` remembers each extension header search (a
  scan of megabytes, read from flash on the board).
- `frames/NativeFunctions.cpp` and `armcpu/PackageNativeCPU.cpp` index the
  ROM's native entries and the public jump table by a hash of the name
  (registration was quadratic).

## WiFi (2026-10-11)

The board joins WiFi networks it has been told of, and Internet Setup -
the NIE's own application - is where they are told of: its Host network
setup (comms/host/HostLink.ns) has a **Wi-Fi** line whose slip scans,
takes a password and remembers or forgets a network.

- **Portable**: host/HostWifi.h is a provider interface and its
  NewtonScript natives (HostWifiScan, HostWifiKnown, HostWifiRemember,
  HostWifiForget, HostWifiStatus); a desktop has no provider - its own
  operating system manages its network - and shows no Wi-Fi line, but
  `NEWTON_FAKE_WIFI=1` gives it a pretend one for the slip's test
  (demo/wifisetup.ns, ctest `host.NewtonWifiSetup`).
- **The board** (`src/host/esp32/main/tdeck_wifi.cpp`): ESP-IDF's
  station; up to 8 networks in NVS; the strongest remembered network in
  range joined, retried 30 s after it is lost or when none is in range; a
  task with an internal stack owns it (NVS writes the flash).
- **Internal RAM**: WiFi needs some, and the tree's statics had used
  almost all of it - `HOST_LARGE_STATIC` (host/host_compat.h) puts the
  six largest (200 KB) in PSRAM on the board; the WiFi's and lwIP's
  buffers are in PSRAM too. Internal RAM free with the OS up: 13 KB
  before, 53 KB now.
- **Turning the screen**: Internet Setup will not open at 240x320 upright
  and asks to turn it; the board now follows the Newton to landscape (the
  panel draws the display a quarter turn round, the touch panel turns
  back), and the Newton asks for the pen to be calibrated that way round.
- **The clock** set from the network: once joined, SNTP (`pool.ntp.org`)
  sets the board's clock, and the Newton's is set from it each time
  (`newtonmain.cpp`'s DeviceServices loop, `TURealTimeAlarm::SetTime`).
  The console's `!time <seconds>` does the same by hand; tried that way
  (the Newton then read 1:00 PM at 20:00 GMT, Cupertino being its home
  city).
- **The small screen** (2026-10-12): Internet Setup opens at once (no
  "turn the screen?" for an upright screen), its slips and their
  keyboards fit 240 wide, and so do the Typewriter keyboard and Time
  Zones' "choose the closest city" slip (src/host/esp32/README.md, "The
  small screen"); the board's keyboard is
  built in as an eMate's is (the keyboard gestalt), so the on-screen
  keyboards are left out until asked for.
- **Joined a real network** (2026-10-12, WPA2): the address got, the
  clock set by SNTP. On the way: the board had never started the NIE's
  host link (Internet Setup: "no module packages installed"); the
  Wi-Fi slip ran off the 240-wide screen (Internet Setup's slip is 320
  wide); ESP-IDF's event task overflowed its 2304-byte stack when the
  address came (4 KB now), restarting the board at every join.

## The 4G module (2026-10-12)

The owner's board is the 4G variant (an A7682E): it is driven from the
reconstruction and, once the battery was fitted, registered on its
network - the start of making the board a phone, with the ROM's own
Phone transport and Calls log: `docs/host-tdeck-pro-phone.md`.

## The battery (2026-10-14)

`BatteryStatus(0)` on the board is the board's own battery
(`src/host/esp32/main/tdeck_battery.cpp`, a source for
`power/host/HostBattery.h`): the BQ27220 fuel gauge at 0x55 (state of
charge, voltage, current, temperature, and BATTPRES for whether a battery
is fitted) and the BQ25896 charger at 0x6B (REG0B: power good - USB is the
Newton's mains - and charging or done). On the owner's board, on USB with
the battery in: 100%, 4127 mV, 0 mA, the charger done, then topping up
(the BQ25896 restarts a charge by itself).

The gauge stretches the clock while it wakes from sleep, far beyond the
ESP32-S3 I2C controller's default timeout (2^10 cycles of 40 MHz, 26 us):
the first read after the gauge has been idle failed with
`ESP_ERR_TIMEOUT` and the next one worked. The bus's timeout is now 2^19
cycles, 13 ms (`tdeck_input.cpp`, where the bus is installed), and a
gauge read is tried twice.

## Printing (2026-10-14)

The board prints to the network's IPP printers as the desktops do
(`docs/print/README.md`): `DevicePreMain` installs them
(`HostInstallIPPPrinters`; the Network Printers panel's source,
`print/host/HostPrinters.ns`, is embedded by `main/CMakeLists.txt`), so
the Print slip offers the printers found on the network and the Prefs
have the Network Printers panel. Before, the print code was compiled in
but never installed: `HostLookForPrinters` was an undefined function and
the slip offered no network printer. The PNG host printer is left out (the
board has nowhere to put the pages). ESP-IDF has no DNS-SD browse of its
own to ask, so the printers are found by the mDNS question of
`print/host/dnssd/HostDNSSD.cpp` over lwIP's sockets; its buffers are
static because the browse's thread has ESP-IDF's default 3 KB stack.
There is no TLS on the board, so an `ipps://` printer cannot be used.

Tried on the owner's board (Wi-Fi at 192.168.1.191), at the NewtonScript
prompt through `serialcapture.py --send-file`: `HostLookForPrinters()`
found the owner's HP Color LaserJet Pro M479 at
`ipp://192.168.1.120:631/ipp/print`, PostScript and PCL, within the
browse's 2.5 s; `GetGlobalVar('|NetworkPrinters:host|):AddPrinter(found,
'postScript)` made it the current printer; a note put in the Out Box with
`Send('print, note)` and sent with `TransportNotify('|print:newton|,
'SendRequest, ...)` (as `src/host/demo/fax-send.ns` sends a fax) went as
PostScript, and the printer answered successful-ok and printed it. (The
board's prompt cuts a long line, so the note was built over several.)

## Battery life (2026-11-01)

The owner found the battery did not last. Measured by the board's own fuel
gauge with `tools/tdeck/powermeter.py` (its README entry says how: the
charger's input cut with EN_HIZ, so the board runs from its battery while
the USB carries the prompt; one-second samples averaged over 60 s after
15 s to settle), each row switching one more thing off, the board left
alone with WiFi joined and the 4G module registered:

| State | mA | saved |
|---|---|---|
| as found | 130 | |
| keyboard light off | 123 | 7 |
| GPS off | 123 | 0 |
| LoRa radio off | 133 | -10 |
| 4G module off | 109 | 24 |
| WiFi power save max modem | 102 | 7 |
| WiFi off | 100 | 2 |

(A first run: 126, 124, 125, 134, then cut short; another of the first two
rows: 123, 123. Differences of 2-3 mA are within the noise.) On a 1400 mAh
battery (LilyGO's figure, not checked on this board's gauge) that is about
11 hours as found, and no more than 14 with every radio off: **the floor is the
board itself** - the ESP32-S3 at 240 MHz all the time (power management
is not enabled: no frequency scaling, no light sleep, a 1000 Hz tick),
woken every 10 ms by the input task's I2C polls and the services task,
with the PSRAM and the panel's controller (powered on once and never
off: `POF` is not sent) behind it. The Newton's own sleep is no help: no
window ever calls `HostPowerWindowOpened` on the board, so
`PlatformPowerOffSystem` (hal/host/Power.cpp) returns at once.

Two things the table says beyond that:

- **Switching the LoRa radio off draws more, 10 mA more, both runs.** The
  mesh's loop is not spinning (it waits 200 ms at a time with the radio
  off); the likely cause is the radio's lines still driven into a chip
  whose power (GPIO 46) is cut - its chip select parked high by the panel
  (GPIO 3), its reset (GPIO 4) - powering it through its pins. Not yet
  tried: the radio's lines let go (inputs) while it is unpowered, or its
  power left on and the radio in cold sleep (under a microamp).
- **The GPS saves nothing** when switched off: either the receiver drew
  little (no fix being sought indoors) or it is powered through its UART's
  lines (GPIO 43) as the radio may be. Not looked at yet.

What would help, largest first: power management with automatic light
sleep (the touch panel's and keyboard's interrupt lines waking the board
instead of polls - the USB console stops while asleep, so a switch for
it); a real sleep for the Newton's power-off on the board (the panel
keeps its picture for nothing); the 4G module's own sleep (`AT+CSCLK`
with its DTR line, GPIO 8, which nothing drives yet) or off by default;
max-modem WiFi power save; the keyboard light off after a while; the
radio's pins fixed when it is off; `POF` after each refresh.

### The Newton never waited, and power management

Before any of that: profiling the board at rest (`tools/tdeck/profile.py`)
found core 1 waiting 0.5% of the time. The reconstruction's
`TNotebook::Idle` kept a next idle time that had passed, so the notebook
idled every 6.5 ms for nothing (`docs/work-log.md`, 2026-11-01;
`NEWTON_TRACE_IDLE` found it). Fixed as the ROM has it, the Newton is idle
93% of the time at rest.

Then power management (`src/host/esp32/main/tdeck_power.cpp`,
`sdkconfig.defaults`): ESP-IDF's frequency scaling, 80 MHz unless the
Newton is running (it holds 240 MHz; `gHostIdleHook` in `hal/host/Host.h`
lets go while its idle task waits) or a driver is working, and automatic
light sleep between FreeRTOS's ticks. ESP-IDF's GPIO isolation in sleep is
off: it would have let go of the radios' power switches. Three things keep
the chip out of light sleep: the USB console while connected (the USB
serial/JTAG does not work across it - ESP-IDF's connection monitor), and
the GPS's and the 4G module's UARTs while they are powered (what they send
meanwhile would be lost). So on the cable the board is measured awake;
`powermeter.py --usb-away` lets go of the USB for each measurement (the
D+ pull-up taken away; the board restarts if the computer does not find it
again, which happened once, with the port still held open by the tool).

With the USB away, each row switching one more thing off:

| State | mA | saved |
|---|---|---|
| as found | 113 | |
| keyboard light off | 113 | 0 |
| GPS off | 115 | -2 |
| LoRa radio off | 123 | -8 |
| 4G module off | 94 | 29 |
| WiFi power save max modem | 79 | 15 |
| WiFi off | 63 | 16 |

With the GPS and the 4G module off and the rest on, 113 mA without light
sleep and 70 with it (`--no-sleep` against not). As found, nothing is
gained yet: **the 4G module keeps the chip awake.** Its own sleep (DTR,
GPIO 8, with `AT+CSCLK=1`, and its RI line to wake the board for a call or
a message) would let the chip sleep with it on. Max-modem WiFi power save
is now worth 15 mA, since the chip sleeps between the beacons it skips.
The LoRa radio still draws more switched off. The Newton still goes idle
about 95 times a second, so something still wakes it every 10 ms; the
input task's 10 ms poll of the touch panel and keyboard is another wake-up.

### The 4G module's sleep, and the USB on the cable

The 4G module now sleeps (`src/host/esp32/main/tdeck_modem.cpp`): the
engine's setup ends with `AT+CSCLK=1` (`HostCellularLink`'s new `setup`,
`hal/host/HostCellular.h`), after which the module sleeps while DTR (GPIO 8)
is high. The board pulls DTR low and keeps the chip awake while the engine
talks to the module, and lets both sleep after half a second with no byte
either way (15 s while a command has had no answer). The module pulls RI
(GPIO 7) low when it has something to say, a call or a message; RI is a
GPIO wake-up from light sleep. Awake spells come to about 0.65 s in each
15 s, the engine's status poll.

With the USB away, the GPS off, everything else on:

| State | mA |
|---|---|
| 4G module asleep | 88 |
| 4G module off | 80 |

(The first run of the same, before the fix below, gave 93 and 64 with the
4G and everything else on, as found.) The module asleep costs 8 mA over
having it off, where it cost about 30 awake.

**The USB serial/JTAG did not survive light sleep on the cable**, and
ESP-IDF's connection monitor (`USJ_NO_AUTO_LS_ON_CONNECTION`), which should
hold light sleep off while a host is connected, did not reliably: its lock
was let go 2,107 times in ten minutes (`!pm`, `CONFIG_PM_PROFILING`), and
once the 4G no longer held the chip awake the board's USB went dead within
minutes, the board otherwise running. So `tdeck_battery.cpp` now keeps the
chip awake while the charger says the USB gives power (REG0B `PG_STAT`),
unless its input was cut for measuring; on the cable the battery is no
matter. Measuring cuts it, so after `--usb-away` gives the USB back the
board stays awake until it is let go again (10 minutes at most): before
that, it slept 2 s after coming back and the tool's next question never
reached it.

The console's `!pm` prints what keeps the chip awake (`keepers`, the
`kAwake...` bits in `esp32host.h`) and ESP-IDF's lock and mode profile,
with the number of light sleeps. A change to `sdkconfig.defaults` only
reaches the build once `src/host/esp32/sdkconfig.tdeck-pro` is moved aside
(a full rebuild).

Not yet confirmed: a text or call arriving while the board is on its
battery and asleep (RI's wake-up).

## Order of work

The owner already has a T-Deck Pro (bought for another project), so the
device's unknowns can be measured on it early rather than argued about:
a **device spike** - a small ESP-IDF program, not the OS - comes first,
as the reMarkable work began with one (`docs/host-remarkable.md`, "What
the spike showed"). It should answer:

- **the board revision** (V1.0, V1.1 or MAX: the I2C scan LilyGO's own
  firmware does - DRV2605 at 0x5A, XL9555 at 0x20), which decides the pins;
- **the panel**: full and partial refresh times over SPI as actually
  driven, whether the UC8253 can show any grays, and how a 240x320 one-bit
  frame looks;
- **the touch panel**: how fine and how steady a finger's position is at
  the CST328's report rate - whether taps on the Newton's small targets
  (a checkbox, a scroll arrow) are possible;
- **the memory**: PSRAM and flash-mapped read speed through the cache
  for a pointer-chasing walk over a few megabytes (the interpreter's
  pattern), against internal SRAM;
- **the toolchain**: ESP-IDF's Xtensa compiler on the tree (C++17,
  exceptions, `-fwrapv`, `-falign-functions=4`) and the code size it
  gives - done: 674 of 678 files, 3.46 MB (above).

Then:

1. **32 bits as a supported host** - done: the whole ctest suite passes
   on the 32-bit x86 build, built and run under WSL by
   `tools/host/wsl32.sh` (`docs/host-lp64.md`, "The 32-bit build under
   WSL", for what it found).
2. **A device-sized desktop run** - done (above): a 2 MB kernel heap with
   a 512 KB NewtonScript heap in it, a 4 MB store and 32 KB task stacks
   pass both walkthroughs; what remains in RAM that the device must not
   hold is the ROM's objects (9.4 MB) and the flash image - the next two
   steps.
3. **The baked object image** - done (above): `newton --bake-objects`,
   mapped read-only by `--objects`; both walkthroughs pass on it, 32-bit
   and 64-bit. Left: the built-in packages' frames (1.2 MB of RAM at 32
   bits) baked the same way.
4. **An ESP-IDF skeleton** - milestone A done (above): NewtonScript on
   the board with the baked objects. Milestone B done (above): the OS
   booted on FreeRTOS to a NewtonScript prompt in the newt world. Left:
   the store in the `store` partition (it is in RAM), the start made
   faster (a minute), NewtonScript's speed worked on, the internal RAM's
   static data looked at.
5. **The panel, the touch screen, the keyboard** - done (above), and the
   store on the board's flash, and a faster start (3 s). Left: the
   applications at 240x320. Later, for
   the panel:
   the owner pointed to a video of an e-paper display driven at 30 frames
   a second with four levels of gray, by driving each pixel only part of
   the way, which needs a frame buffer of the panel's state kept in
   memory (https://www.youtube.com/watch?v=XiZmM6hwwAU) - to be looked at
   for faster, grayer updates than the OTP's waveforms give.
6. **The applications at 240x320** (or the scaled screen) - the part
   whose size is least known.

## Risks

- **The screen** (above): the largest unknown is how many of the built-in
  applications can be made to fit.
- **E-paper speed**: 0.65 s an update with the panel's own waveforms (the
  spike) suits reading and typing, not scrolling or ink; faster needs a
  waveform of our own.
- **Caches**: measured - a miss costs about 1 µs from PSRAM and 2 µs from
  flash (above), and the interpreter walks objects all over megabytes.
  Internal SRAM for the hottest data and a 64 KB cache are the remedies;
  NewtonScript on the board is the only real measurement.
- **Board revisions**: LilyGO's V1.0, V1.1 and MAX need different firmware.
