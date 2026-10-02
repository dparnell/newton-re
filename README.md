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
wants the X11 and ALSA development headers).  The build makes
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
| `--rom IMAGE` | boot the original ROM image instead, for comparison |

Handwriting is read by both of the ROM's recognisers, ink is kept, the
built-in applications work, packages install (third-party native ARM code
runs on a built-in ARM interpreter), and the Newton beams, docks with a
desktop over TCP port 3679, faxes, prints (PostScript and HP PCL, by IPP)
and reaches the Internet through the host's own network.  What each part
does and what is still NOT YET is in [docs/](docs/) -
[docs/newt/README.md](docs/newt/README.md) for the boot, and
[docs/next-steps.md](docs/next-steps.md) for what is left.

Run the tests with `ctest --test-dir build/host` (about 400 of them; a
build without the ROM image simply skips the few that compare against it).

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
