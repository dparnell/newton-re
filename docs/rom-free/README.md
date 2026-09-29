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

1. **The skeleton, with the resources opaque. Done:**
   `analysis/romsrc.py`, ctest `host.ROMSourceRoundTrip`. `extract` writes
   the object graph as source, with every binary other than strings and
   reals as a `.bin` file, and functions as frames whose instructions are
   one of those. `build` writes the area back, and **all 46538 objects
   come out byte for byte the ROM's**. The notation is in the tool's own
   documentation; see "Stage 1" below for what it is not yet.
2. **Functions as source. Done.** Each of the 5480 top-level functions the
   graph reaches is `functions/<addr>.ns`, the decompiler's record (its
   constants, then the function).
   - The builder stays in Python and hands every function, in one run, to
     `newtonscript --compile-records`. That is the host's own compiler, as
     the decompiler's round trip uses it, and it writes each compiled
     function back in the tree's notation. Strings, reals and the shapes
     kept as shorts go back into the ROM's byte order, the reverse of the
     importer.
   - The run has **no ROM image**, so nothing is read but the tree. The
     object area still comes out byte for byte the ROM's.
   - Where the ROM shares an object that a compiled function makes afresh
     (a string two functions push, for example), an `alias` line in the
     manifest says which named object that path is. There are 3784 of
     them.
   - Symbols are matched whatever their case, since the host's compiler
     spells a symbol as the host first interned it.
   - Left as bytecode: 362 functions that are named objects inside others
     (shared, or held in a slot as well as being a literal). They are not
     top-level, so the decompiler writes them inside their parent, and
     the tree keeps their definitions as frames of bytecode.
3. **Bitmaps** (`bits`, `mask`, `cbits`): PNG and back, lossless. The
   first real resource, and the most numerous. `tools/imaging/pgm2png.py`
   is the start of the image side.
4. **Sounds** (`samples`): WAV and back. The codecs a sound frame names
   are the sound area's (`docs/sound/README.md`).
5. **Pictures and fonts**: PICT files, and the `sfnt` files as they are.
6. **The ROM extension's ten packages** (`analysis/packages.py`), each
   extracted to the same form and rebuilt as a package.
7. **The loader side (step 4):** `frames/ROMImport.cpp` reads the builder's
   area instead of the image, then the host programs run with no `--rom`.

## The frame maps

A frame's class word points at its **map**: an array of the frame's slot
names, whose first slot is a supermap (nil, or a map whose names come
first). `nsfunctions.py --census` counts them:

- There are **8336 maps** for 12838 frames. 282 of them are supermaps that
  no frame uses directly.
- Only 359 maps are shared by more than one frame. Between them they carry
  5143 frames: one map of `left, top, right, bottom` alone carries 1096.
- There are 2249 different tag lists, and 23 of them have more than one
  map, 6110 maps in all. **5783 of those are the functions' own maps:**
  every one of the NTK's code blocks (`class, instructions, literals,
  argFrame, numArgs`) has a map to itself, where a builder that made maps
  from the tags would give them one between them.
- 318 maps are held in a slot other than a supermap's, as values a script
  uses.

So which map a frame has cannot be told from its slots, and a map is
sometimes a value in its own right. **The maps are named objects** in the
source (`maps.ns`: `map_<addr> := map(class, supermap, 'tag, ...)`), and
**which map each frame uses is a fact in the manifest** (`map=` on the
frame's line). A frame's source stays `{tag: value, ...}`. The builder
checks that its tags are its map's, and will make a map for a frame the
manifest does not know (a frame added by an edit).

## Stage 1: what the source is and is not yet

`python tools/newton-rom/analysis/romsrc.py extract build/MP2x00US -o <dir>`
(or `cmake --build <build> --target romsrc`) writes the tree. It takes
about 4 seconds, and `build <dir> --check build/MP2x00US` takes about 2.

The tree holds:

- 3038 definitions in `objects/NNN.ns`, 400 to a file in address order;
- the 8336 maps in `maps.ns`;
- `layout.tsv`, one line per object;
- about 3900 `.bin` files: the 4681 instruction strings are most of
  them.

On disk it is about 2.3 MB of object source, 0.8 MB of maps, 1.8 MB of
manifest and 7 MB of resources.

What it is not yet:

- 362 nested functions are still bytecode (stage 2).
- The files are cut by address, not by what they belong to (the
  dominator grouping above).
- Resources are opaque, and inline objects are named by path, so an edit
  that moves a slot also has to move the manifest's line.

In short, it is a faithful and readable dump, and not yet a tree to edit.

## Where the tree lives

**Decision: the tree is generated at build time, not committed, until it
is a tree worth editing.** What is committed now is the tools, the
ctest, and the `romsrc` make target.

- **Size.** A generated tree is about 12 MB in some 12,000 files. Each
  re-extraction after a tool change would rewrite most of it, so every
  improvement to the extractor would be a multi-MB commit of churn, and
  the history would be the extractor's rather than anyone's edits.
- **Nothing reads it yet.** While the OS still loads the ROM image, an
  edit to the tree changes nothing that runs, so committing it would
  invite edits that are silently lost at the next extraction.
- **When it is committed.** The tree goes into the repository once, as
  `romsrc/`, when three things are true:
  - its functions are source (stage 2);
  - its common resources are files a person edits (bitmaps and sounds);
  - the host builds its object area from the committed tree (step 3's
    builder in the build).
  From then on the tree is the source and the extractor is not run again,
  so later commits are people's edits, which git keeps as small deltas.
  The one-time addition is of the order of `DebugRom/`, which the
  repository already carries.
- **It is Apple's data**, as the ROM images in `DebugRom/` are. Keeping
  it in the repository is no different in kind.

## Open questions
- **mosrun** (https://github.com/MatthiasWM/mosrun) runs Apple's own
  Newton build tools (the Rex builder, the ARM tools). It could check
  step 5's packages and ROM extension against Apple's builder, byte for
  byte.
