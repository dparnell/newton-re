# newton-rom — ROM extraction, symbol recovery and Ghidra import

Tools for turning an Apple Newton MP2x00 *debug ROM* image into an annotated
Ghidra project: a ROM image laid out as the CPU sees it, all ~52,000 debug
symbols demangled, and a Ghidra program with functions, C++ class namespaces,
parameter lists and jump-table thunks already in place.

Everything here is plain Python 3 (3.9+) with no third-party dependencies,
except the final Ghidra step which uses the `pyghidra` package that ships with
Ghidra 11.3 or newer (developed and tested with Ghidra 12.1.3 and Python 3.13).

```
tools/newton-rom/
  newtonrom/            library
    aif.py                AIF container parser (header, RO/RW/debug areas)
    symbols.py            LANG_NONE debug symbol table reader
    rex.py                ROM Extension (REx) block parser
    jumptable.py          patchable jump table decoder / virtual layout
    demangle.py           cfront/ARM C++ demangler (structured output)
    ghidra_import.py      applies layout + symbols to a Ghidra program
  extract_rom.py        step 1: rom.bin + layout.json
  dump_symbols.py       step 2: symbols.json (+ text listing)
  ghidra_scripts/
    import_rom.py         step 3: headless project creation + import + analysis
    NewtonROMImport.py    same import, as a Script Manager (GUI) script
    check_import.py       report/spot-check an imported project
  pipeline.py           steps 1-3 in one command
  tests/                unit tests + oracle comparison against mpdumper
```

## Quick start

```powershell
# from the repository root
python -m venv build\venv
build\venv\Scripts\pip install --no-index --find-links "D:\apps\ghidra_12.1.3_PUBLIC\Ghidra\Features\PyGhidra\pypkg\dist" pyghidra

build\venv\Scripts\python tools\newton-rom\pipeline.py "DebugRom\MP2100 D" -o build\MP2100D --ghidra D:\apps\ghidra_12.1.3_PUBLIC
```

This writes `build/MP2100D/{rom.bin,layout.json,symbols.json,symbols.txt}` and
creates the Ghidra project `build/ghidra/MP2100D.gpr` (≈ 30 s for the import,
≈ 3 minutes more for auto-analysis on a current desktop). Open the project in Ghidra
normally afterwards. `build/` is git-ignored; everything in it is regenerated
by the tools.

Steps individually:

```powershell
python tools\newton-rom\extract_rom.py "DebugRom\MP2100 D\Senior DCirrusNoDebug image" --rex "DebugRom\MP2100 D\Senior DCirrusNoDebug high" -o build\MP2100D
python tools\newton-rom\dump_symbols.py "DebugRom\MP2100 D\Senior DCirrusNoDebug image" -o build\MP2100D\symbols.json --text build\MP2100D\symbols.txt
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\import_rom.py build\MP2100D --project build\ghidra --name MP2100D --ghidra D:\apps\ghidra_12.1.3_PUBLIC
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\check_import.py --project build\ghidra --name MP2100D --ghidra D:\apps\ghidra_12.1.3_PUBLIC --lookup Init DebugStr
```

GUI alternative for step 3: import `rom.bin` in Ghidra with the *Binary*
loader (language `ARM:BE:32:v4`, compiler `apcs`, base 0), decline
auto-analysis, add `tools/newton-rom/ghidra_scripts` as a script directory and
run `NewtonROMImport.py`, then run auto-analysis.

Tests (the oracle test needs the US ROM and the mpdumper output present):

```
python tools/newton-rom/tests/test_demangle.py
python tools/newton-rom/tests/test_rom.py
```

## What the debug ROM contains

`Senior ... image` is an ARM **AIF** executable (big-endian):

| Region | File offset | Load address | Size (MP2100 D) |
|---|---|---|---|
| AIF header | 0x0 | — | 0x80 |
| RO area (code + rodata) | 0x80 | 0x00000000 | 0x6F0AE8 |
| RW initialisers | 0x6F0B68 | 0x0C100800 (RAM) | 0x23B4 |
| zero-init data | — | 0x0C102BB4 | 0x2328 |
| debug area | 0x6F2F1C | — | 0x1C4024 |

The debug area is a single `LANG_NONE` section: a flat table of 52,150
symbols (name, value, flags). There is **no type information** — the C++
structure is recovered purely from the mangled names. The symbol flags are
unreliable (almost everything is marked "code", including NewtonScript data
objects), so the importer decides for itself what is a function (below).

`Senior ... high` is the **ROM Extension** (`RExBlock`). Its `start` field is
exactly `ROM$$Size` = RO + RW sizes, so the physical 8 MB ROM is simply
`RO ‖ RW-init ‖ REx`, which is what `extract_rom.py` writes to `rom.bin`.
The REx config entries (`pkgl` built-in packages, `ptpt`/`glpt` page tables,
`jump` patch table, …) are listed in `layout.json`.

Despite the "NoDebug" in the file names, these images differ from the shipping
ROM only in carrying the symbol table.

## The patchable jump table

76 % of all `BL` instructions in the ROM (and ~6,000 function pointers) do not
target the callee but a slot in a table of `B` instructions, so that a ROM
extension can patch individual functions. Facts established by the tools and
asserted by `tests/test_rom.py`:

* Physically the table is at ROM address **0x2000**, one `B` per entry
  (16,723 entries in MP2100 D, 16,919 in MP2x00 US).
* The MMU maps it at virtual **0x01A00000** sparsely: virtual page *p* is an
  alias of ROM page `0x2000 + (p/32)*0x1000`, and "owns" the 128-byte slice at
  page offset `(p%32)*0x80`. Slot *i* is therefore at
  `0x01A00000 + (i/32)*0x1000 + ((i/32)%32)*0x80 + (i%32)*4`.
* The `B` offsets are relative to the **virtual** slot address; decoding the
  physical table at 0x2000 gives wrong targets.
* Every exported function appears twice in the symbol table, at its body and
  at its slot (same name). All 16,723 slots branch to the identically named
  body — `dump_symbols.py` verifies this and refuses to continue otherwise.
  The single exception, slot `_DebugStr`, branches to the body named `DebugStr`
  (an assembler alias of the same routine).

In Ghidra the table is materialised as the `JT` block (a copy of the aliased
pages at their virtual addresses) and every slot is a *thunk* of its target,
so the decompiler shows `TFoo::Bar()` at call sites instead of an address.

## Name demangling

Newton OS was built with Apple's cfront-derived ARM C++ compiler. The scheme is
documented at the top of `newtonrom/demangle.py`; the one non-obvious part is
how `T<n>`/`N<count><n>` back-references are numbered inside nested function
pointer types. The rule implemented (a nested list inherits the enclosing
list's entries so far, appends its own, and discards them when it closes) was
derived from the ROM symbols and checked against the Apple header
declarations, e.g. `TMonitor::Init` in `headers/OS600/UserMonitor.h`.

`tests/test_demangle.py` compares our output with GNU libiberty's (the
demangler inside `tools/mpdumper`, whose pre-generated output for the US ROM
is checked in): all 52,751 symbols agree exactly, and we additionally demangle
40 symbols that libiberty gives up on (the nested back-reference cases).

## What the Ghidra import does

Given `rom.bin` imported with the Binary loader at 0 (`ARM:BE:32:v4`: the
MP2100's StrongARM SA-110 is ARMv4 without Thumb, running big-endian; compiler
spec `apcs`, the ABI of the ARM SDT toolchain of the period):

1. **Memory map** — `ROM_RO`, `ROM_RWINIT`, `REX` (split from the imported
   block), `RAM_RW` at 0x0C100800 initialised from the ROM copy, `RAM_ZI`, and
   the `JT` block described above.
2. **Classes** — a Ghidra class namespace for each of the ~1,050 C++ classes
   named in symbols. Class structures start empty; filling them in is the
   reverse-engineering work proper.
3. **Functions** — created at every C++ function symbol, every jump-table
   target, and every plain symbol whose first word is an unconditional ARM
   instruction (the rest of the plain names — `SYM*`, `RSS*`, `MP0*`, … — are
   NewtonScript objects and become labels). Names go into their class
   namespace; C++ overloads coexist. Where every parameter type is
   representable, the demangled parameter list is applied with an explicit
   `this` (under APCS `this` is simply r0, so no special calling convention is
   used). Functions taking a class *by value* keep their name but not the
   prototype, because the class size is unknown and a wrong size would shift
   every following parameter; the full signature is in the function comment.
4. **Thunks** — all jump-table slots.
5. **Labels** — everything else, including RAM variables and constants; the ~10
   symbols that fall outside any memory block are skipped and counted.

The importer prints a statistics block at the end (functions created per
rule, signatures applied/skipped and why, …); `check_import.py` reports on
an existing project.

## Provenance of the analysis

The layout, jump-table and demangling facts above were established with
throw-away scripts during the initial investigation; they are preserved in
executable form as the tests and the tools themselves rather than as
scripts, so re-running `tests/test_rom.py` re-derives and re-checks them
against the images.
