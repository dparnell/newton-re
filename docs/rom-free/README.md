# Booting with no ROM image: extracting the ROM's objects as sources

The owner's goal is that the reconstructed OS boots without a ROM image.
Everything the ROM's object area holds must then come from source files in
the repository: NewtonScript, resources, and tables. A person edits those
files, and a build step makes the objects the OS loads from them.

The track has four steps (`docs/next-steps.md`, "A long-term track"):

1. **The decompiler.** This is done: every one of the ROM's 5507 top-level
   NewtonScript functions decompiles, and compiles back to the identical
   function (`docs/frames/decompiler.md`, ctest `host.NSDecompileRoundTrip`).
2. **Resource extraction.** This document is the plan for it.
3. **The builder.** It makes the object area back out of what step 2
   writes.
4. **Booting from the builder's output**, with no `--rom`.

Steps 2 and 3 are planned together. Only the builder can prove that an
extraction lost nothing, the way the compiler proves the decompiler's
output.

## What there is to extract

`nsfunctions.py build/MP2x00US --census` gives these counts:

| Kind | Objects | Bytes |
|---|---|---|
| frame | 12838 | 420092 |
| array | 16506 | 786616 |
| symbol | 8623 | 248633 |
| binary | 8571 | 1469507 |
| **all** | **46538** | **2924848** |

- 10679 objects are **shared**, referenced from more than one slot.
- 1012 are referenced from **no object**. These are the roots: the
  magic-pointer table and the ROM's C code reach them.
- The object area is `gROMSoupData`, 0x3afda8, with 0x2cfc98 bytes
  (`frames/ROMConstants.h`).

The binaries by class come from `nsfunctions.py --binary-classes`. Here is
what each class is, and the form planned for it:

| Class | Count | What it is | Extracted as |
|---|---|---|---|
| `instructions` | 4681 | bytecode | NewtonScript source, from the decompiler |
| `string`, `string.noData` | 3157 | UTF-16 text | NewtonScript string literals (UTF-8 source) |
| `bits`, `mask` | 476 | 1-bit bitmaps, their masks | PNG, with a sidecar for the header (bounds, row bytes) |
| `cbits` | 61 | bitmaps of more than one bit | PNG (gray), with a sidecar |
| `samples` | 34 | sound samples | WAV, with the sound frame's own slots staying source |
| `TDTMFCodec` | 16 | the touch tones' codec parameters | a table (below) |
| `sfnt` | 13 | the fonts | `.sfnt` files as they are, which font tools read |
| `picture` | 7 | QuickDraw pictures | PICT files (the 512-byte header added), with a PNG made alongside for looking at |
| `Real`, `fixed`, `boundsrect`, `rectangle`, `roundrectangle`, `polygonshape`, `pattern`, `deskey` | ~90 | small values | source (see below) |
| `UniC`, `Sort`, `kchr`, `Intl`, `table`, `Comp` | 23 | Unicode, collation, keyboard and locale tables | tables (below) |
| `AirusA`, `DTEM`, `DTEH`, `PPDB`, `Trigram`, `Trigrams`, `letterimages` | 13 | the recognisers' dictionaries and trained data | tables (below) |

A **table** is written at first as the raw bytes (a `.bin` file) with a
note of what reads it. Each one gets an editing tool of its own only when
someone wants to change it. The dictionaries, for example, are tries that
`recognition/Airus.h` walks, and a word list is the form to edit them in.
Getting the whole area out and back comes first.

## The form of the extraction

Everything goes into a tree of files (called `romsrc/` below):

- **`objects/*.ns`: the object graph as NewtonScript.** An object
  referenced from one place is written inline where it is used, as the
  decompiler writes literals: `'{...}`, `'[...]`, strings, numbers,
  symbols, `@n` for a magic pointer. A function is its decompiled source.
  - An object referenced from more than one place, or from none, gets a
    name. It takes the ROM's own name where there is one: the debug
    symbols' `R`-names (`Rbuiltinfunctions`), or the name of the
    magic-pointer entry. Otherwise it is named after its address
    (`obj_3c5431`), until someone renames it. Every other object refers to
    it by name.
  - The files are split by the top-level objects they hang from (the
    applications, the protos, the locale bundles, ...), so that a person
    finds things. Which top-level object a file belongs to is decided from
    the graph's dominator tree: an object goes with the only root that
    reaches it.
- **`resources/`: the binaries above as their files.** An object's source
  refers to one by path, for example `resource("bitmaps/obj_4db6cd.png")`.
- **`layout.tsv`: the manifest.** It lists each object's name and address
  in the ROM's order, the magic-pointer table's entries by name, and the
  symbol table.
  - With the manifest, the builder can lay the area out exactly as the ROM
    did. That is the proof below.
  - Without it, the builder lays the area out in an order of its own. That
    is what an edited tree needs: the magic pointers and names are the
    only fixed points (the refs in `frames/ROMConstants.h` then come from
    the builder, not from `romconstants.py` reading the image).

## The proof: a byte-identical object area

The builder (step 3) reads `romsrc/` and writes an object area. Run with
`layout.tsv`, its output must be **byte for byte the ROM's**: every
object's header, class, slots and data, at the same address. This is a
ctest like the decompiler's round trip. Until an extractor is lossless it
fails, and it says which object differs and how.

The compiler is not the builder. The builder compiles each function with
the host's compiler (`CompileFunctionString`, as the round trip does),
then *serialises* the host's objects into the ROM's big-endian layout at
the manifest's addresses. That covers:

- the object header (size, flags);
- the class;
- the frame maps, shared as the ROM shares them;
- strings and the halfword shapes turned back to big-endian (the reverse
  of `frames/ObjectAreaImport.cpp`).

Where the decompiler's source makes an equal object but not the same
bytes, the manifest records what cannot be told from the source (which
map a frame shares, for example). These are the build-time facts the
decompiler's NTK constants already are.

## Order of work

Each step is committed with its test, and the round trip's percentage is
reported as the decompiler's was.

1. **The skeleton, with the resources opaque.** `analysis/romextract.py`
   writes the object graph as source, with every binary other than
   strings and reals as a `.bin` file. `analysis/rombuild.py` (Python at
   first, reading the source through the host's `newtonscript --compile`
   entry to be added) writes the area back. The ctest compares the two,
   and the target is 100% of objects identical.
2. **Bitmaps** (`bits`, `mask`, `cbits`): PNG and back, lossless. The
   first real resource, and the most numerous. `tools/imaging/pgm2png.py`
   is the start of the image side.
3. **Sounds** (`samples`): WAV and back. The codecs a sound frame names
   are the sound area's (`docs/sound/README.md`).
4. **Pictures and fonts**: PICT files, and the `sfnt` files as they are.
5. **The ROM extension's ten packages** (`analysis/packages.py`), each
   extracted to the same form and rebuilt as a package.
6. **The loader side (step 4):** `frames/ROMImport.cpp` reads the builder's
   area instead of the image, then the host programs run with no `--rom`.

The first thing to settle in step 1 is the frame maps. A map is an object
of its own, which frames share (the census does not yet count them). The census should
count the maps, and how often a frame written as `{...}` in source would
get a map other than the one the ROM gave it. That count decides whether
maps are named objects in the source or facts in the manifest.

## Open questions

- **Where the tree lives.** It is Apple's data. The repository already
  keeps the ROM images (`DebugRom/`). While the builder is not yet
  lossless, the tree is generated into `build/<rom>/romsrc/`. It is
  committed to the repository once it round-trips, which is the point at
  which editing it means something.
- **mosrun** (https://github.com/MatthiasWM/mosrun) runs Apple's own
  Newton build tools (the Rex builder, the ARM tools). It could check
  step 5's packages and ROM extension against Apple's builder, byte for
  byte.
