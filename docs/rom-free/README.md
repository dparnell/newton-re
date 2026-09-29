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
   - **No bytecode is left in the tree.** The 362 `instructions` binaries
     that stage 2 still kept were shared: the ROM's build stored equal
     objects once, so functions with the same code share one binary of
     bytecode, and 10 nested functions are literals of more than one
     function.
     - A named object that is referenced only from inside compiled
       functions is now written `same("<path>")`: the object that a
       compiled function holds at that path. For example,
       `obj_3c5405 := same("obj_6475c5.ReadPreferences.instructions")`.
     - 721 definitions are written this way: bytecode, shared function
       literals, and strings and frames that only functions push.
     - Their source is the functions', so an edit to a function is not
       overridden by a copy elsewhere.
3. **Bitmaps. Done:** `bits`, `mask` and `cbits` are PNG and back,
   lossless.
   - The tree has 344 + 121 + 60 PNGs; the other 12 are literals inside
     functions, which the decompiler builds.
   - Each is written as `bitmap('bits, "resources/bits/<addr>.png",
     "<header>", <depth>)`:
     - the rows are a grayscale PNG of the bitmap's own bit depth, with
       the Newton's set pixels black;
     - the 16-byte header (a FramBitmap, `qd/Pictures.h`) stays in hex,
       because its pad word is not always nought and the builder must
       reproduce it;
     - a `cbits`' depth comes from its row bytes: the one depth whose
       rows fit with less than a word over.
   - A bitmap goes to PNG only when the PNG holds it exactly: one
     possible depth, rows that fill its bytes, nothing set in the
     padding. The extractor packs the rows back and compares before
     choosing the PNG; every one of the ROM's qualifies.
   - An edited PNG may be any PNG a program writes (RGB, a palette,
     alpha, filters). `tools/imaging/png.py` reads it by luminance into
     the bitmap's levels, and `tools/imaging/test_png.py` tests that. A
     PNG of a new size is refused until its header's bounds are changed
     to match.
4. **Sounds: the simple sounds done.** The ROM has 34 `samples`
   binaries.
   - The simple sounds' samples (8-bit and uncompressed, `sndFrameType
     'simpleSound`) are WAV files, written as `sound('samples,
     "resources/samples/<addr>.wav")`. Newton 8-bit samples are offset
     binary, as a WAV file's 8-bit samples are, so the bytes go across
     unchanged. The WAV's rate is the frame's `samplingRate`, rounded,
     for a player's sake only: the frame keeps the real rate. Two of them
     are in the tree; the third is a literal inside a function.
   - An edited WAV of 16 bits or two channels is brought down to 8-bit
     mono by the builder.
   - The other 31 stay `.bin`, and this is deliberate:
     - 12 are IMA ADPCM in the Newton's own blocks (0x40 samples with a
       big-endian header, `sound/IMACodec.h`). A WAV holds IMA ADPCM in
       blocks of its own, so decoding and encoding again would not give
       the same bytes. They can become an editable WAV once the builder
       compresses a sound with `sound/IMACodec.h`, accepting new bytes
       for an edited sound.
     - 19 are `TDTMFCodec` parameters: the ring tones as tone sequences,
       50 bytes each. That is a table, not sampled sound.
5. **Pictures and fonts. Done.**
   - The 7 QuickDraw pictures (version 1, opcode 0x1101) are `.pict`
     files: 512 bytes of nought, then the picture, which is the form a
     Macintosh drawing program reads and writes. They are written as
     `pict('picture, "resources/picture/<addr>.pict")`, and the builder
     drops the 512 bytes. No PNG is made beside them yet: that waits on
     the host drawing a picture's text (`qd/PicPlay.h`).
   - The 13 fonts are `.ttf` files, byte for byte the `sfnt` binary.
     fontTools opens them:
     - five are bitmap fonts (`bdat`, `bloc`, `cmap`, `head`, `hhea`,
       `hmtx`, `hsty`, `maxp`, `name`, `post`; "Roman Regular" is the
       first);
     - eight are 2080-byte metric-only fonts with no `name` table.
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
(or `cmake --build <build> --target romsrc`) writes the tree, and `build
<dir> --check build/MP2x00US --newtonscript <exe>` builds it back. The
ctest `host.ROMSourceRoundTrip` does both in about 25 seconds, most of it
the decompiler.

The tree holds:

- 3038 definitions in `objects/NNN.ns`, 400 to a file in address order;
- 5480 functions in `functions/`;
- the 8336 maps in `maps.ns`;
- `layout.tsv`, one line per object, and the aliases;
- the resources: 525 PNGs, 2 WAVs, 7 PICTs, 13 fonts, and 99 `.bin`
  files (the compressed sounds, the ring tones' parameters, the tables and
  a few small shapes).

There are some 6,500 files, about 19 MB on disk (NTFS). In bytes that is
1.2 MB of object source, 8.3 MB of function source, 0.8 MB of maps,
2.1 MB of manifest and 2 MB of resources.

What it is not yet:

- The compressed sounds (IMA ADPCM), the ring tones' `TDTMFCodec`
  parameters and the tables are still `.bin` files.
- The files are cut by address, not by what they belong to (the
  dominator grouping above).
- Resources are opaque, and inline objects are named by path, so an edit
  that moves a slot also has to move the manifest's line.

In short, it is a faithful and readable dump, and not yet a tree to edit.

## Where the tree lives

**Decision: the tree is generated at build time, not committed, until it
is a tree worth editing.** What is committed now is the tools, the
ctest, and the `romsrc` make target.

- **Size.** A generated tree is about 19 MB on disk in some 6,500 files. Each
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

## Step 3: booting without `--rom`

The OS reads more out of the image than the object area. Here is
everything it reads, found by searching `src/` for `ROMImageBase` and the
importer. A table generated into `src/` is compiled in and needs nothing
at run time.

| What | Where it is read | Out of the image today | With no image |
|---|---|---|---|
| **The object area**: frames, functions, symbols, strings, bitmaps, sounds, the fonts' `sfnt`s, the locale bundles and their `Intl`/`Sort`/`UniC`/`kchr` tables | `frames/ROMImport.cpp` (`ImportROMObjects`, at `gROMSoupData`) | yes | **the built area** (step 2's output) |
| **The magic-pointer table**: 873 refs, the count first, at `gROMMagicPointerTable` 0x3af000, just *before* the object area | `ImportROMObjects` | yes | extracted with the area (3a below) |
| **The ROM's constants**: the RSSYM symbols, the R/RS objects, `kROMSymbolTable`, `Rbuiltinfunctions` | `frames/ROMConstants.h`, `RSSymbols` (generated by `romconstants.py`) | **no**: generated into `src/`, as addresses the importer translates | the same, while the built area keeps the ROM's layout; a freely laid-out area will have the builder generate them |
| **The native functions** | `frames/ROMNatives.cpp` (generated), bound by name | no | unchanged |
| **The ROM extension**: its ten packages (`pkgl`, `GetRExConfigEntry`) | `packages/ROMPackages.cpp`, `FramePartHandler.cpp` (a part in the image is read where it lies), from the "high" file spliced in by `SpliceROMExtension` | yes | nothing at first: the host already runs without an extension. Then the packages as sources (step 2's item 6), installed from the built tree |
| **The recognisers' lexicons**: 129 tries, C data outside the object area | `recognition/ROMDictionaryData.cpp` (`gROMDictionaryTable`: addresses into the image) | yes | empty at first: every ROM dictionary answers nothing, which that file already allows. Then extracted into `src/` as a generated table, or into the tree as resources |
| **ROM code a package's native code calls** | `armcpu/PackageNativeCPU.cpp` (the emulated CPU reads the ROM) | yes | only a package with ARM code needs it; without the image such a package's native calls fail, as they do now for code the host does not answer |
| **The other generated tables** (grammar, recognition nets, fonts' metrics tables, CRC, the codecs' tables, ...) | `src/*/…Tables.cpp` | no: compiled in | unchanged |

So the first no-`--rom` boot needs only the first two rows from the built
tree. The later rows are things the machine can do without, and each
comes back as its own piece of work.

### Where it stands: the OS boots with no ROM image

`newton --objects <file>` boots the OS on the object file built from the
tree, with no image anywhere it could find one. ctest `host.NewtonNoROM`
does it, on the file `host.ROMSourceRoundTrip` writes.

- The world comes up on the **Notepad**: its button bar (Extras, InOut,
  Names, Dates, Undo, Find, Assist), the date and battery, the Unfiled
  Notes folder tab, and the demo slip with its buttons and a paragraph to
  type in.
- **With the ROM extension in the tree (below), the boot comes up on the
  Setup assistant's Welcome**, as the boot on the image does, and **the
  two screens are the same, pixel for pixel**. ctest
  `host.NewtonNoROMSameScreen` (`tools/host/samescreen.py`) boots both
  ways and compares the snapshots. Before the extension came into the
  tree, the no-image boot came up on the Notepad instead, because the
  Setup assistant is one of the extension's packages.
- A first try showed that the recognisers could not do without the
  lexicons (`ReplaceDictionary` over no data). So the lexicons came
  forward from 3d. The tree has them as `lexicons/<name>.bin`, 40 tries
  in their ROM form, listed with their addresses in `lexicons.tsv`. The
  object file (version 2) carries them as "other blocks of ROM data",
  and `frames/ROMImport.h`'s `ROMBytesAt(address, length)` answers ROM
  bytes out of the image or out of those blocks.
  `InitROMDictionaryData` asks it, where it read the image. `--check`
  compares the lexicons with the ROM's too.

### The ROM extension: kept as package files first

The extension ("the high file" in `DebugRom/`, at `ROM$$Size`,
0x71fc4c, 0xce3fc bytes) is in the tree as `rex/` and `rex.tsv`. It is
cut at every config entry and every package of the package list, in
address order:

- the header;
- `dio`, `gpio`, `ralc`;
- the ten packages as `.pkg` files (Cardfile, Connection, FaxViewer,
  Formulas, help book, ListView, ScreenBuffer, ScreenDrivers, Setup,
  WorldData);
- `ptpt`, `glpt`, `fexp`, `jump` and the padding between them.

The builder puts the pieces back together as one more block of the
object file, and `--check` compares it with the ROM's.

On the host:

- `GetRExConfigEntry` looks for the extension's header in every place
  the ROM's bytes are, through `frames/ROMImport.h`'s `ROMRegion`: the
  image, or the object file's blocks.
- A frames part in any of those places is imported at its ROM address
  (`ROMAddressOf`), as it is from the image.

**Decision: the packages are kept as package files first, not rebuilt
from decompiled source.**

- A package in the ROM is not in the form an outside package is: its
  frames parts' refs are ROM addresses. So a `.pkg` here is the ROM's
  bytes, the unit the package manager loads, and `packages.py
  --relocatable` makes a loadable copy of one.
- Kept as bytes, the extension reaches the goal directly: the same boot
  with and without the image.
- Rebuilding a package from source is the object area's machinery again,
  applied to each frames part: its objects as definitions and maps, its
  functions decompiled, its bitmaps as PNG, laid out at the part's
  address. It is the next piece of work (step 2's item 6). It touches
  the four frames packages that are applications (Cardfile, Connection,
  Formulas, Setup), the help book, FaxViewer, ListView and WorldData's
  soup. ScreenBuffer and ScreenDrivers are ARM protocol code, which stays
  bytes until the host has an ARM story for them (the host registers its
  own screen driver).

### The extension's frames packages from source

Each package's frames part is now a tree of its own, `rex/<Package>/`:
objects, functions, maps, a layout and resources, exactly as the object
area's tree is. The package's bytes around the part are
`rex/<Package>.head.bin` (its directory) and `.tail.bin`, and the
builder builds the part and splices it back in. The whole extension is
still byte for byte the ROM's.

| Package | Objects | Functions as source | Kept as bytecode |
|---|---|---|---|
| Cardfile | 3445 | 334 | 2 |
| Connection | 3059 | 416 | 1 |
| FaxViewer | 666 | 77 | 0 |
| Formulas | 1188 | 125 | 0 |
| help book | 834 | (no functions) | |
| ListView | 2967 | 0 | 266 |
| Setup | 1061 | 119 | 0 |

- **Which functions are source.** A function is written as source only
  when it compiles back the same. The extractor now checks this for
  every tree:
  - it compiles each tree's decompiled functions in one run of
    `newtonscript --compile-records`;
  - it compares what comes back with the ROM's objects (`same_object`),
    because the decompiler's round trip only knows the object area;
  - it keeps any that differ as bytecode, listed with the reason in the
    tree's `bytecode.tsv`.

  In the object area all 5480 pass. In the packages:
  - 3 do not decompile: corner cases of the sorted variable order;
  - **ListView's 266 are all different**: that package was built with
    debugging information, so every function carries a `DebuggerInfo`
    slot, which the NTK's debug build wrote and the compiler does not
    make. They stay bytecode until the compiler's debug information is
    matched to it.
- **Differences from the object area:**
  - A part's objects are aligned from the part's start, to 8 bytes in a
    version 0 package.
  - The gaps hold that package's fill byte (0xbf), and in ListView some
    hold a word of 0xbeacebad. The layout records such a gap on its
    object's line (`gap=`), and records the header's second word where
    it is not nought (a part's first object has 1: `gc=`).
  - Paths are matched whatever their case, because a compiled frame's
    tags are spelt as the host first interned them.
- **Still bytes:** ScreenBuffer and ScreenDrivers (ARM protocol code) and
  WorldData (a soup part).

### The plan

- **3a. The builder writes a loadable object file. Done.**
  - The extraction also writes the magic-pointer table into the tree,
    as `magic.ns`: each entry is a named object or a value.
  - `romsrc.py build -o objects.bin` writes a container:
    - a header: its signature, the area's base and size, and the count
      of magic pointers;
    - the area, byte for byte what `--check` compares;
    - the magic pointers, as big-endian words.
  - The byte-identical ctest checks the table too.
- **3b. The host loads it. Done.** `ImportROMObjectsFromFile` also knows
  the object file by its signature, so `--rom` works as well as
  `--objects`.
  - `frames/ROMImport.cpp` gets `ImportBuiltObjects(path)`: the import
    `ImportROMObjects` does, from the container's area and table.
    `ROMImageBase` then answers nil: there is no image, and everything
    that reads one already copes with that.
  - `newtonscript` and `newton` take `--objects <file>` in place of
    `--rom`.
- **3c. The proof. Done.**
  - `host.NewtonNoROM` boots `newton --headless` on the built objects,
    with no image anywhere it could be found.
  - `host.NewtonNoROMSameScreen` compares its screen with the `--rom`
    boot's, pixel for pixel.
  - A `newtonscript -e` over the built objects must answer as it does
    over the image, for example `ROMConstant("canonicalTextShape")`.
- **3d. Then the rows that come back one at a time:**
  - the lexicons (done, above);
  - the ROM extension's packages, from the tree (done, as package files;
    from source is next);
  - the area laid out freely, with the builder generating
    `ROMConstants.h`.

## Open questions
- **mosrun** (https://github.com/MatthiasWM/mosrun) runs Apple's own
  Newton build tools (the Rex builder, the ARM tools). It could check
  step 5's packages and ROM extension against Apple's builder, byte for
  byte.
