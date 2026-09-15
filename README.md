# Newton MessagePad 2x00 ROM reverse engineering

Reverse engineering of the Apple Newton MessagePad 2100 (German, "MP2100 D")
ROM, with the long-term goal of a retargetable, maintainable re-implementation
of Newton OS.

## Contents

| Path | What it is |
|---|---|
| `DebugRom/` | Debug ROM images (`... image`, AIF format with symbol table) and ROM extensions (`... high`) for the MP2100 D (2001) and MP2x00 US (1997) |
| `headers/` | Apple's internal NewtonOS C/C++ SDK headers (classic Mac CR line endings) |
| `documentation/` | Newton Programmer's Guide / Reference, NewtonScript language and bytecode specs |
| `tools/newton-rom/` | **Our tooling**: ROM extraction, demangling, Ghidra import — see its [README](tools/newton-rom/README.md) |
| `tools/mpdumper/` | Alexey Danilchenko's 2004 symbol dumper (libiberty demangler) and its pre-generated symbol listings for the US ROM; used as the reference oracle for our demangler |

## Reproducing the Ghidra project

Requirements: Python 3.9+, [Ghidra](https://ghidra-sre.org/) 11.3+ (we use 12.1.3)
and a JDK 21 for it.

```powershell
python -m venv build\venv
build\venv\Scripts\pip install --no-index --find-links "<ghidra>\Ghidra\Features\PyGhidra\pypkg\dist" pyghidra
build\venv\Scripts\python tools\newton-rom\pipeline.py "DebugRom\MP2100 D" -o build\MP2100D --ghidra <ghidra>
```

Result: `build/ghidra/MP2100D.gpr`, a Ghidra project of the ROM as the CPU sees
it, with ~35,000 functions named and placed in ~1,050 C++ class namespaces,
16,000+ prototypes, and the patchable jump table resolved to thunks. Details,
the memory layout, and the analysis behind it are in
[tools/newton-rom/README.md](tools/newton-rom/README.md).

Run the tests with `python tools/newton-rom/tests/test_demangle.py` and
`python tools/newton-rom/tests/test_rom.py`.

## Project rules

* Every tool used in the process lives in this repository under `tools/`, is
  documented (purpose, inputs, outputs, exact invocation) and is runnable from a
  clean checkout so that others can reproduce each step.
* Findings about the ROM (layout, tables, conventions) are recorded in the
  tool READMEs and, where possible, asserted by tests against the images.
