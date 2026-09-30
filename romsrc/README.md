# romsrc: the ROM's NewtonScript world as source

This tree is everything the Newton MessagePad 2x00 US ROM (2.1, build
717006) keeps as NewtonScript objects, written as files a person can
read, edit and build:

- the object area: 46538 frames, arrays, symbols and binaries;
- its magic-pointer table;
- the recognisers' lexicons;
- the ROM extension with its ten built-in packages.

The OS boots from it with no ROM image. It is Apple's data, kept here
as the ROM images in `DebugRom/` are.

**The tree is now the source.** It was extracted once from the ROM by
`tools/newton-rom/analysis/romsrc.py extract` and committed. The
extractor is not run over it again: changes are made here, by hand, and
are kept. How it was made, and why it is shaped as it is, is in
`docs/rom-free/README.md`.

## What is where

| Path | What it is |
|---|---|
| `objects/*.ns` | The objects, as definitions `name := value;` in a notation that is a subset of NewtonScript's literals. A file holds the definitions one root object dominates, named after it (`Rbuiltinfunctions.ns`, `calendar_mp18.ns`). What several roots share is in `misc-NNN.ns`. |
| `functions/*.ns` | Each NewtonScript function, as the decompiler wrote it (`tools/newton-rom/analysis/nsdecompile.py`), named by the slot that holds it: `Rbuiltinfunctions.AddAlarm.ns`. |
| `maps.ns` | The frame maps (the slot names of the ROM's frames). |
| `resources/` | The binaries: bitmaps as PNG (`bits`, `mask`, `cbits`), simple sounds as WAV, pictures as PICT, fonts as `.sfnt` (sfnt containers of Apple's bitmap and metric tables, no outlines). What has no editable form yet (compressed sounds, tables) is `.bin`. |
| `lexicons/`, `lexicons.tsv` | The recognisers' word tries, with their ROM addresses. |
| `rex/`, `rex.tsv` | The ROM extension, in pieces: its header and config entries, and the ten packages. A package with a frames part is `<Package>.head.bin` (its directory), the part as a tree of its own in `<Package>/` (the same layout as this one), and `<Package>.tail.bin`. |
| `magic.tsv` | The magic-pointer table: `@index` and the object it names. |
| `layout.tsv` | The manifest: every object's address, path in the source and header flags, each frame's map, and aliases (shared objects that a compiled function makes afresh). |
| `bytecode.tsv` (in a part's tree) | The functions kept as bytecode, with the reason, when any are. None are now. |

The notation, `same("path")`, `function("…")`, `bitmap(…)` and the rest,
is described in the documentation of `romsrc.py` (`python
tools/newton-rom/analysis/romsrc.py --help`).

## Building it

```
python tools/newton-rom/analysis/romsrc.py build romsrc -o build/objects.bin --newtonscript build/host/host/newtonscript
```

or `cmake --build <build> --target romsrc`, which writes
`<build>/romsrc-objects.bin`.

- The builder compiles the functions with the host's own compiler
  (`newtonscript --compile-records`, run with no ROM image).
- It lays the objects out as `layout.tsv` says and writes the object file
  the host loads.
- As committed, the result is byte for byte the ROM's. Add `--check
  build/MP2x00US` to compare, which needs the extracted ROM (`build/<rom>`,
  `tools/newton-rom/pipeline.py`).

## Editing it

Edit the files: a string, a slot, a function's source, a bitmap in any
paint program, a sound. Then build with `--relayout`:

```
python tools/newton-rom/analysis/romsrc.py build romsrc --relayout -o build/objects.bin --newtonscript build/host/host/newtonscript
```

- `--relayout` lays the objects out afresh at the sizes they now have, so
  an object that grows moves every one after it.
- It makes maps for frames whose slots changed and for new frames, and
  symbol objects for new names.
- It relays out the extension's packages and keeps their directories,
  the extension's header and its export table in step.
- The object file records where each object moved. The host's constants
  still name the ROM's addresses and follow that record, so a data edit
  needs no new build of the host.

`romsrc.py edit-test` is the test that this works: it edits a copy and
the copy still boots (ctests `host.ROMSourceEdit`,
`host.ROMSourceEditValue`, `host.NewtonEditedSameScreen`).

Nothing in `layout.tsv` needs changing by hand. Slots can be put in
another order, elements inserted into an array, slots added, renamed or
taken away, and new frames added; `--relayout` lays out what is there
now. `layout.tsv` only keeps the unedited tree's addresses and the facts
the source cannot say (which map a frame shares, a header's flags).

## Booting from it

```
build/host/host/newton --objects build/objects.bin
```

This is the OS with no ROM image. With the tree as committed, it draws
exactly what the boot on the ROM image draws: ctest
`host.NewtonNoROMSameScreen`.

## The ctests

| Test | What it checks |
|---|---|
| `host.ROMSourceCommitted` | The committed tree still builds byte for byte the ROM's. It passes as the tree was committed. Once someone edits the tree on purpose, it stops being a regression test: it then says only that the tree has left the ROM, and it should be retired, or kept for a branch that tracks the original. |
| `host.ROMSourceRoundTrip` | The extractor itself: a fresh extraction into the build directory, built back byte for byte. It does not touch this tree. |
| `host.NewtonNoROM`, `host.NewtonNoROMSameScreen` | The OS booted from this tree's object file, and its screen against the ROM image boot's. |
| `host.ROMSourceEdit`, `host.ROMSourceEditValue`, `host.ROMSourceEditMoved`, `host.NewtonEditedSameScreen` | A copy of this tree edited (strings lengthened, slots swapped, an array element inserted, a slot holding a new frame added), built laid out afresh, the edits read back, and booted. |
