# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A reverse-engineering project for the Apple Newton MessagePad 2100 D (ARM StrongARM SA-110, big-endian, NewtonOS 2.x), aiming eventually at a retargetable re-implementation. It holds the ROM images, Apple's internal SDK headers, documentation, and our own tooling under `tools/newton-rom/` that extracts the ROM, demangles the debug symbols and builds an annotated Ghidra project.

**Project rule (from the owner):** every tool used in the process must live in this repo, be fully documented (purpose, inputs, outputs, exact invocation) and be reproducible by others from a clean checkout. Don't leave analysis in scratch scripts — promote it into `tools/` or record the finding in a README/test.

## Commands

All tooling is stdlib Python 3.9+; only the Ghidra step needs `pyghidra` (bundled with Ghidra 11.3+). Ghidra 12.1.3 is installed at `D:\apps\ghidra_12.1.3_PUBLIC`; a venv with pyghidra lives in `build/venv` (git-ignored, recreate with the commands in `tools/newton-rom/README.md`).

```powershell
# whole pipeline: rom.bin + layout.json + symbols.json + Ghidra project build/ghidra/MP2100D.gpr
build\venv\Scripts\python tools\newton-rom\pipeline.py "DebugRom\MP2100 D" -o build\MP2100D --ghidra D:\apps\ghidra_12.1.3_PUBLIC [--no-analyze] [--no-ghidra]

# individual steps
python tools\newton-rom\extract_rom.py "<image>" --rex "<high>" -o build\MP2100D
python tools\newton-rom\dump_symbols.py "<image>" -o build\MP2100D\symbols.json --text build\MP2100D\symbols.txt
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\import_rom.py build\MP2100D --project build\ghidra --name MP2100D --ghidra <ghidra> [--no-analyze]
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\check_import.py --project build\ghidra --name MP2100D --ghidra <ghidra> --lookup <Name>...

# tests (unittest, no pytest needed; oracle test uses the US ROM + tools/mpdumper output)
python tools\newton-rom\tests\test_demangle.py
python tools\newton-rom\tests\test_rom.py
```

The import step refuses to overwrite an existing program of the same name — delete `build/ghidra` (or use another `--name`) to re-import. Import takes ~30 s, auto-analysis ~3 min more.

## Architecture of the tooling (`tools/newton-rom/`)

Pipeline: `extract_rom.py` → `dump_symbols.py` → `ghidra_scripts/import_rom.py`, glued by `pipeline.py`. The library `newtonrom/` has one module per concept: `aif` (container), `symbols` (debug table), `rex` (ROM extension), `jumptable` (virtual↔physical slot maths + verification), `demangle` (parser producing a type AST, rendered to text *and* JSON), `ghidra_import` (consumes layout.json + symbols.json; only imports Ghidra classes inside functions so the rest works without Ghidra). `ghidra_scripts/NewtonROMImport.py` is a thin PyGhidra Script-Manager wrapper around `ghidra_import.apply`.

Key facts the code relies on (all asserted by `tests/test_rom.py`; details in `tools/newton-rom/README.md`):
- Physical ROM = `RO ‖ RW-init ‖ REx`; REx `start` == `ROM$$Size`. RW data lives in RAM at 0x0C100800.
- Debug area is one `LANG_NONE` section: names + addresses only, **no types**; flags are unreliable (data marked as code).
- Patchable jump table: physically at ROM 0x2000, virtually at 0x01A00000 with 32 slots per 4 KB page in a diagonal alias layout; branch offsets are relative to the *virtual* slot address. Each exported function has a body symbol and a same-named slot symbol; `dump_symbols.py` verifies all slots resolve.
- Demangling is cfront/ARM style; nested-list back-references inherit the enclosing list (documented in `demangle.py`). Our output matches libiberty exactly on all 52,751 US symbols it handles — keep `tests/test_demangle.py` green when touching the demangler.
- Ghidra: language `ARM:BE:32:v4`, compiler `apcs`; there is no `__thiscall` on ARM, so member functions get an explicit `this` parameter. By-value class parameters make the prototype unsafe (unknown size), so those functions get name + comment only.
- Don't name a directory `ghidra` anywhere on `sys.path` — it shadows the Java `ghidra` package under PyGhidra (hence `ghidra_scripts/`).

## Working with the repository files

- **Header files use classic Mac CR-only line endings (`\r`).** `grep`/`head`/`sed` see one giant line; pipe through `tr '\r' '\n'` first, e.g. `tr '\r' '\n' < headers/OS600/ROMExtension.h | grep kRExSignature`.
- `tools/mpdumper` (2004, Cygwin): options must be combined (`mpdumper -dx <image>`, not `-d -x`); the `.exe` needs the bundled `cygwin1.dll`. Its `Symbols_demangled_by_address.txt` (`Code symbol: Name = HEXADDR`) is the demangler oracle — grep it or `build/MP2100D/symbols.txt` (`ADDR class mangled demangled`) instead of re-deriving symbols.
- ROM images and symbol files are multi-MB; use `xxd`/`grep`/ranged reads or the `newtonrom` library rather than reading them whole.
