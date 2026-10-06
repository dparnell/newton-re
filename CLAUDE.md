# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A reverse-engineering project for the Apple Newton MessagePad 2x00 (ARM StrongARM SA-110, big-endian, NewtonOS 2.x), aiming eventually at a retargetable re-implementation. Two debug ROMs are in `DebugRom/` and the tooling works with either; `src/` is a reconstruction of the US English one, `MP2x00 US` (2.1, build 717006), because the owner does not read German and the `MP2100 D` image (D-2.1, build 747129) has no English locale and German strings built into its extension's applications. Switching which ROM `src/` is of is `analysis/recite.py` for the citations, `analysis/regenerate.py` for the generated tables, and the three `NEWTON_ROM*` lines at the top of `src/CMakeLists.txt`. It holds the ROM images, the Newton Driver Developer Kit (DDK) headers (`headers/` — struct and class declarations to use when reconstructing types), documentation, and our own tooling under `tools/newton-rom/` that extracts the ROM, demangles the debug symbols and builds an annotated Ghidra project.

**Project rule (from the owner):** every tool used in the process must live in this repo, be fully documented (purpose, inputs, outputs, exact invocation) and be reproducible by others from a clean checkout. Don't leave analysis in scratch scripts — promote it into `tools/` or record the finding in a README/test.

## Commands

All tooling is Python 3.9+; the header step needs `libclang` (`tools/newton-rom/requirements.txt`) and the Ghidra step needs `pyghidra` (bundled with Ghidra 11.3+). Ghidra 12.1.3 is installed at `D:\apps\ghidra_12.1.3_PUBLIC`; a venv with pyghidra lives in `build/venv` (git-ignored, recreate with the commands in `tools/newton-rom/README.md`).

```powershell
# whole pipeline: rom.bin + layout.json + symbols.json + Ghidra project build/ghidra/MP2x00US.gpr
build\venv\Scripts\python tools\newton-rom\pipeline.py "DebugRom\MP2x00 US" -o build\MP2x00US --name MP2x00US --ghidra D:\apps\ghidra_12.1.3_PUBLIC [--no-analyze] [--no-ghidra]

# individual steps
python tools\newton-rom\extract_rom.py "<image>" --rex "<high>" -o build\MP2x00US
python tools\newton-rom\dump_symbols.py "<image>" -o build\MP2x00US\symbols.json --text build\MP2x00US\symbols.txt
build\venv\Scripts\python tools\newton-rom\parse_headers.py headers -o build\MP2x00US\types.json
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\import_rom.py build\MP2x00US --project build\ghidra --name MP2x00US --ghidra <ghidra> --no-analyze
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\verify_types.py build\MP2x00US --project build\ghidra --name MP2x00US --ghidra <ghidra>   # -> romfacts.json, verify-report.txt
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\apply_romfacts.py build\MP2x00US --project build\ghidra --name MP2x00US --ghidra <ghidra> --analyze
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\check_import.py --project build\ghidra --name MP2x00US --ghidra <ghidra> --lookup <Name>... --type <Struct>...

# tests (unittest, no pytest needed; oracle test uses the US ROM + tools/mpdumper output)
python tools\newton-rom\tests\test_demangle.py
python tools\newton-rom\tests\test_rom.py
build\venv\Scripts\python tools\newton-rom\tests\test_headers.py
```

The import step refuses to overwrite an existing program of the same name — delete `build/ghidra` (or use another `--name`) to re-import. Import ~40 s, verification ~1 min, auto-analysis ~4 min.

## Architecture of the tooling (`tools/newton-rom/`)

Pipeline: `extract_rom.py` → `dump_symbols.py` → `parse_headers.py` → `ghidra_scripts/import_rom.py` → `verify_types.py` → `apply_romfacts.py`, glued by `pipeline.py`. The library `newtonrom/` has one module per concept: `aif` (container), `symbols` (debug table), `rex` (ROM extension), `jumptable` (virtual↔physical slot maths + verification), `demangle` (parser producing a type AST, rendered to text *and* JSON), `headers` (libclang extraction of the DDK headers into the same JSON type schema, plus `HeaderTypes` prototype matching), `ghidra_import` (consumes layout.json + symbols.json + types.json; only imports Ghidra classes inside functions so the rest works without Ghidra). `ghidra_scripts/NewtonROMImport.py` is a thin PyGhidra Script-Manager wrapper around `ghidra_import.apply`.

Key facts the code relies on (all asserted by `tests/test_rom.py`; details in `tools/newton-rom/README.md`):
- Physical ROM = `RO ‖ RW-init ‖ REx`; REx `start` == `ROM$$Size`. RW data lives in RAM at 0x0C100800.
- Debug area is one `LANG_NONE` section: names + addresses only, **no types**; flags are unreliable (data marked as code).
- Patchable jump table: physically at ROM 0x2000, virtually at 0x01A00000 with 32 slots per 4 KB page in a diagonal alias layout; branch offsets are relative to the *virtual* slot address. Each exported function has a body symbol and a same-named slot symbol; `dump_symbols.py` verifies all slots resolve.
- Demangling is cfront/ARM style; nested-list back-references inherit the enclosing list (documented in `demangle.py`). Our output matches libiberty exactly on all 52,751 US symbols it handles — keep `tests/test_demangle.py` green when touching the demangler.
- DDK header layout: Apple's ARM C++ put the vptr at offset 0 and gave empty bases no space (verified on `TAEventHandler`/`TUObject` constructors), so clang's layout (`armeb`, `-mabi=apcs-gnu`) is used as-is; bit-fields are MSB-first (verified on `TCardPCMCIA::AddFuncSpecificCIS`). The MP2x00 US config is `forQ __arm` (a localised ROM adds its language: the MP2100 D is `forGerman` on top of these); V1 headers (`CommToolProtocol.h`, `SerialChip.h`) and `.f.h` files are excluded. Known header/ROM differences: `TCardDevice`, `TObjectIterator`, `TCMOSerialChipSpec` (see `verify-report.txt`).
- Constructors self-allocate (`teq r0,#0; bne; mov r0,#size; bl operator_new`) and store the vtable at [this,#0]; `verify_types.py` reads sizes/vtables from them into `romfacts.json`. Vtables are arrays of `B` instructions in declaration order (dtor first, base entries first); virtual calls are `mov lr,pc; add pc,rN,#slot*4`, which Ghidra treats as a terminal jump — `fix_virtual_calls` marks them fall-through calls (`FlowOverride.CALL`), otherwise functions are truncated after their first virtual call (`ghidra_scripts/fix_virtual_calls.py` re-marks an older project's sites).
- Header/symbol matching resolves typedefs first (`Boolean`→`Uc`, `RefArg`→`RC6RefVar`); a unique match supplies return type and parameter names. Plain-name symbols are matched to `extern "C"` declarations by name.
- Ghidra: language `ARM:BE:32:v4`, compiler `apcs`; there is no `__thiscall` on ARM, so member functions get an explicit `this` parameter. Unknown classes passed by value make the prototype unsafe, so those functions get name + comment only.
- Don't name a directory `ghidra` anywhere on `sys.path` — it shadows the Java `ghidra` package under PyGhidra (hence `ghidra_scripts/`).

## Reconstructed source (`src/`)

Organised by functional area (`os600/kernel`, `os600/user`, `hal/`, `utility/`, `frames/`, …) — never by ROM address — because the owner wants to retarget the OS to other hardware and run the user-mode side on other host OSes: hardware code goes behind `hal/`, the user-side API talks to the kernel only via an explicit syscall interface.

**`docs/codebase-map.md` is the area-by-area map** — what each area holds, its entry points, tools, ctests, demos, environment variables and pitfalls. Read the section for the area you are touching before working in it, and add to it (and to the area's `docs/<area>/README.md`) as the area grows. Keep this file to what applies everywhere.

### Conventions

- Keep Apple's names and layouts (`src/README.md`). Every reconstructed function has a `// ROM 0x<addr> <mangled>` citation (checked by `tools/newton-rom/analysis/coverage.py build/MP2x00US --check`); each unit has a host test.
- Reproduce the ROM's behaviour exactly, bugs included; mark departures `DEVIATION`, unfinished pieces `NOT YET` (`analysis/notyet.py [src] [--area A]` finds stale ones), ROM bugs as `ROM BUG` (and interesting findings in `docs/curiosities.md`). A ROM bug is ported as the ROM has it and then fixed beside it behind `RomBugFixed()` (`host/RomBugs.h`; fixed by default, `NEWTON_ROM_BUGS=1` for the ROM's behaviour), its marker becoming `ROM BUG (fixed)`; tests pin both (`docs/rom-bugs.md`, `analysis/rombugs.py`).
- `src/ddk/` is generated by `tools/newton-rom/sync_ddk_headers.py headers src/ddk` (CLibrary headers excluded; `Protocols.h` and `BufferSegment.h` replaced by the reconstruction's own; a short list of documented patches) — regenerate, don't edit.
- Protocols (`PROTOCOL TFoo : public TProtocol`) are re-expressed portably in `src/protocols/Protocols.h` — interface methods are virtual, `TClassInfo` holds function pointers, `PROTOCOL_CLASSINFO` makes an implementation's class info; `analysis/classinfo.py build/MP2x00US --name TFooImpl` decodes a ROM implementation's table, `--all` lists all 101 (`docs/protocols/`).
- Constant tables from the ROM go through `analysis/romtable.py build/MP2x00US NAME[:type[:count]]... -o file.cpp` — never typed in by hand (`cstr` for string-pointer tables; tables in the initialised RAM area are read from the ROM's copy). Other generated sources say which script made them; regenerate, don't edit.
- Words of persistent formats (packages, stores, NSOF, compressed chunks, sound samples, bitmaps' halfwords) are big-endian on every host: read/write them with `toolbox/ByteOrder.h`, never by casting.
- Large or third-party ROM subsystems sit behind an explicit seam (e.g. `TInkCodec`, `TWRecognizer`, `TDotPrinterDriver`), so a modern implementation can be registered beside the ROM's.

### Host portability (Windows LLP64, Linux LP64)

- `ULong`/`Long`/`Ref`/`TRegister` are pointer-sized on the host. A ROM byte count used as the size of a struct holding them truncates it: use `sizeof`/`offsetof` (DEVIATION; check with `analysis/romsizes.py [--lp64]`). A world class's `GetSizeOf` must be its `sizeof` (`analysis/worldsizes.py`, ctest `tools.WorldSizes`). Read ids/selectors out of a `TRegister` with a `(ULong)` cast; word-level pixel code uses `uint32_t`.
- A value that must wrap as the ARM's 32-bit word does is `Long32`/`ULong32` (`host_compat.h`); `Fixed`/`Fract` are pinned to 32 bits. Everything is built with `-fwrapv` (`-DNEWTON_WRAPV=OFF` finds overflowing sums); a shift of a negative value still traps, so make 16.16 values with `ToFixed` and friends (`qd/Ports.h`), never `(Fixed) x << 16`.
- A ROM index past an array that only the ARM's 32-bit address wrap kept harmless crashes the host (`TVStrTail`) — suspect the same in any ROM code that indexes with a sentinel.
- The heap compacts handles: a pointer into a handle's block is stale after any allocation (likewise an `LBData*`).
- Don't include `<map>`, `<string>` or `<vector>` in Newton libraries (see below); read an environment variable once into a static, never on a hot path (the Windows CRT's `getenv` locks).
- The host runs one thread per task with only `gCurrentTask`'s running; read `docs/host-runtime.md` before touching `os600/user/host/SWI.cpp` or `os600/kernel/host/TaskRuntime.cpp`. Tests that make protocol instances by name run as the kernel services task (`gHostKernelServicesTask` + `OsBoot()`, see `stores/tests/test_Store.cpp`). A driver's deliberate busy-wait calls `HostTaskBusy()`. Details: `docs/codebase-map.md` "The host task runtime", `docs/host-lp64.md`.
- `NEWTON_NS64` builds a 64-bit NewtonScript flavour (`newton64`/`newtonscript64`): code that must differ is `#if NEWTON_NS64`, and anything that writes an integer into a 32-bit format goes through `frames/NarrowRef.h` (`docs/frames/64bit.md`).

### Build and test

```sh
cmake -G Ninja -S src -B build/host -DCMAKE_TOOLCHAIN_FILE=$PWD/src/cmake/zig-toolchain.cmake && cmake --build build/host && ctest --test-dir build/host
```

- On Linux use the system compiler (`-DCMAKE_CXX_COMPILER=clang++`): `newton` wants the X11 and ALSA development headers, which zig's linker cannot take. The Linux build is checked under WSL 2 on WSL's own case-sensitive file system (/mnt/f hides include mistakes; ctest `tools.IncludeCase` catches a mis-cased include on Windows) — `docs/host-lp64.md` "Building under WSL".
- A build in a git worktree points at the checkout's ROM extraction with `-DNEWTON_ROM_BUILD=<absolute path>` (`F:/development/newtwon-re/build/MP2x00US`) — never a junction inside the worktree, which `git worktree remove --force` would delete through. Without a ROM image the build still builds and boots; tests that read the image are simply not registered.
- Five ctests want `build/<ROM>/symbols.json`: run `dump_symbols.py` as well as `extract_rom.py` on a fresh checkout.
- `cmake --build build/host --target newton64` builds the 64-bit flavour in `build/host/ns64` (`-DNEWTON_BUILD_NS64=ON` every time).
- A CMake `PASS_REGULAR_EXPRESSION` must not contain `;` (CMake splits it into alternatives).
- Other hosts: the reMarkable Paper Pro (`docs/host-remarkable.md`), macOS planned (`docs/host-macos.md`).

### Running it

- `build/host/host/newton [--rom image | --objects file] [--display WxH[xdepth]] [--script file.ns] [--headless seconds] [--store f] [--package x.pkg]...` is the OS. By default it boots from the reconstructed data, `<build>/romsrc-objects.bin`, built from the committed `romsrc/` tree (`romsrc/README.md`; edit it, then `cmake --build <dir> --target romsrc`); `--rom build/MP2x00US/rom.bin` boots the original image for cross-checking.
- `build/host/host/newtonscript [--rom image] [-e source] [file.ns ...]` runs NewtonScript on the host; `ROMConstant("name")` and `Disasm(fn)` help look at the ROM's objects.
- Host demos live in `src/host/demo/`: a demo starts with `HostInclude("common.ns")`, waits on a condition (`waitFor`), never a fixed time, prints `<name>: done` (its ctest fails on "waited in vain"), and gives its globals non-generic names (ROM scripts referencing a free variable find them).

### Debugging

- `NEWTON_TRACE_MISSING` — an unbound native prints the script stack that asked for it; `NEWTON_TRACE_EXCEPTIONS` (`=2` with the C stack) — each throw with the script stack at the throw. `natives.py --unbound` lists natives still unbound.
- `tools/host/stacksample.py <pid>` samples a host that seems locked up; `NEWTON_TRACE_UPDATE` shows every region the root repaints; `NEWTON_HEAPCHECK=N` walks the heap every Nth allocation to catch damage where it is done. More in `tools/host/README.md` and each area's section of `docs/codebase-map.md`.
- ROM study: `analysis/decompile.py --project build/ghidra --name MP2x00US --ghidra <ghidra> --class TFoo [--asm] [--callers]`; `analysis/disasm.py ... --start A --end B` for ranges the decompiler cannot see as functions (`--force` to have Ghidra disassemble them); `analysis/romdisasm.py build/MP2x00US START END` from rom.bin with capstone when the Ghidra project is locked or not built; `analysis/framewalk.py` follows stack slots; `analysis/vtable.py build/MP2x00US <vtable>` names a vtable's slots (a project imported before the fall-through fix needs `ghidra_scripts/fix_virtual_calls.py`); `analysis/nsfunctions.py build/MP2x00US --disasm <name|0xref>` disassembles a ROM NewtonScript function; `analysis/callgraph.py` asks what reaches what.
- Choosing the next piece of work: `docs/next-steps.md`, `analysis/coverage.py build/MP2x00US --left N` (the largest uncited classes and functions), `natives.py --unbound`, and a `demo/sweep.ns` run ranked by `tools/host/sweeprank.py`.


## Reverse-engineering notes

**An address in ROM code that looks out of range is usually a second MMU mapping, not a mistake.** The ROM is mapped twice - cached at 0x00100000 and *uncached* at 0x03500000 - and ROM code picks the mapping it wants (the handwriting engine's `BPNetEvaluate` streams its 91KB of weights through the uncached alias so as not to flush the StrongARM's 16KB data cache). `analysis/mmumap.py build/MP2x00US --where 0x...` says which entry covers an address and what it maps to; it decodes `g8MegContinuousTableStart` (ROM 0x100), the map the machine starts from, and the flags on an entry usually say why that alias was chosen (`docs/memory/mmu-map.md`). Chase an unexplained constant there before recording it as unknown.

**Ghidra's decompile names `GrafPort`'s +0x24 `clipRgn`, but in the ROM's layout (with `QD_Gray`) +0x24 is `visRgn` and +0x28 is `clipRgn`.** Check a port field against the disassembly's offset before transcribing it, and before calling a drawing oddity a ROM bug. `analysis/portfields.py build/MP2x00US --project build/ghidra --name MP2x00US --ghidra <ghidra> --exclude comms --exclude host` audits them (OK / DIFFERS / NO-PORT: check NO-PORT in the disassembly); rerun it after reconstructing anything that touches a port's regions.

`docs/next-steps.md` says where the last piece of work left off and what is next; read it when picking the work up again, and keep it current as pieces are finished (a finished piece is recorded, newest first, in `docs/work-log.md`, the history of the work and the bugs found on the way). RE findings go under `docs/<subsystem>/` (kernel: `docs/os600/`). Tables derived from the ROM must be produced by a script in `tools/newton-rom/analysis/` (e.g. `swi_table.py` → `docs/os600/swi-table.md`) and say so in their header, so they can be regenerated after a re-import; hand-written pages state how each fact was established. Kernel-side classes (`TTask`, `TPort`, `TObjectTable`, …) have no DDK headers; only the user-side `TU*` API does.

## Working with the repository files

- **Header files use classic Mac CR-only line endings (`\r`).** `grep`/`head`/`sed` see one giant line; pipe through `tr '\r' '\n'` first, e.g. `tr '\r' '\n' < headers/OS600/ROMExtension.h | grep kRExSignature`.
- Don't include `<map>`, `<string>` or `<vector>` in Newton libraries: they reach `<locale.h>`, which `src/intl/Locale.h` shadows on a case-insensitive file system.
- `tools/mpdumper` (2004, Cygwin): options must be combined (`mpdumper -dx <image>`, not `-d -x`); the `.exe` needs the bundled `cygwin1.dll`. Its `Symbols_demangled_by_address.txt` (`Code symbol: Name = HEXADDR`) is the demangler oracle — grep it or `build/MP2x00US/symbols.txt` (`ADDR class mangled demangled`) instead of re-deriving symbols.
- ROM images and symbol files are multi-MB; use `xxd`/`grep`/ranged reads or the `newtonrom` library rather than reading them whole.
