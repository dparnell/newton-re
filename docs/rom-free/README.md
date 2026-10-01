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
| `sfnt` | 13 | the fonts | a directory each: a BDF file per bitmap strike, a text file per other table (`tools/fonts/newtonsfnt.py`) |
| `picture` | 7 | QuickDraw pictures | PICT files (the 512-byte header added), with a PNG made alongside for looking at |
| `Real`, `fixed`, `boundsrect`, `rectangle`, `roundrectangle`, `polygonshape`, `pattern`, `deskey` | ~90 | small values | source (see below) |
| `UniC`, `Sort`, `kchr`, `Intl`, `table`, `Comp` | 23 | Unicode, collation, keyboard and locale tables | tables (below) |
| `AirusA`, `DTEM`, `DTEH`, `PPDB`, `Trigram`, `Trigrams`, `letterimages` | 13 | the recognisers' dictionaries and trained data | tables (below) |

A **table** is written as text where it has a structure worth reading,
and otherwise as its raw bytes (a `.bin` file). Each form is checked:
the extractor rebuilds the bytes from the text and compares before
choosing it.

- **As text now:**
  - the touch-tone scores (`TDTMFCodec` and the dialler's `samples`, 36 of
    them) as `tonescore(...)`: the header (version, algorithm, repeats)
    and each tone's frequency, peak, envelope times in milliseconds and
    sustain level, as `sound/DTMFCodec.cpp`'s `Produce` reads them;
  - the shapes of 16-bit values (`boundsRect`, `roundRectangle`,
    `polygonShape`) as `shorts(...)`;
  - the 16.16 numbers as `fixed(...)`;
  - the three keyboard layouts (`kchr`, Macintosh KCHR) and the two
    256-entry `table`s as `hexfile(...)` text files. Their bytes are in
    hex, 16 to a line, under comments saying what each part is (the
    modifier table; each key table with its characters spelt out beside
    it).
- **Still `.bin` (32), and why:**
  - the recognisers' data, where the edit form would be a generator, not
    the bytes: the Airus dictionaries (`AirusA`, 7, tries that
    `recognition/Airus.h` walks; a word list and a trie builder are the
    form to edit them in), the trained tables (`DTEM`, `DTEH`, `PPDB`,
    `Trigram`, `Trigrams`, `letterimages`);
  - the Unicode, collation and locale data (`UniC` 12, `Sort` 2, `Intl`
    2), whose formats (`frames/UnicodeTables.h`, `frames/SortTables.h`)
    are known but not yet written out as text;
  - the compression dictionaries (`Comp`, 2);
  - one shared `instructions` binary in Cardfile, referenced from outside
    functions as well.

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
   - **The 12 IMA/DVI ADPCM sounds are WAV files too.** They are the
     frames with `codecName "TIMACodec"`, in the Newton's own blocks: 0x40
     samples with a big-endian header, `sound/IMACodec.h`.
     - The extractor expands each with the ROM's own codec (`newtonscript
       --ima-expand`, `ExpandIMA`) into a 16-bit PCM WAV. It keeps that
       only if compressing it again (`--ima-compress`, `CompressIMA`)
       gives back the very bytes, and all 12 do.
     - They are written `imasound('samples, "...wav")`. The builder
       compresses the WAV with the same codec as it builds, bringing an
       8-bit or stereo edit to 16-bit mono and padding it with silence to
       whole blocks.
     - An edited sound gets new bytes, compressed as the Newton would
       have compressed it.
     - `host/IMATool.cpp` is the tool.
   - 19 stay `.bin`: `TDTMFCodec` parameters, the ring tones as tone
     sequences, 50 bytes each. That is a table, not sampled sound.
5. **Pictures and fonts. Done.**
   - The 7 QuickDraw pictures (version 1, opcode 0x1101) are `.pict`
     files: 512 bytes of nought, then the picture, which is the form a
     Macintosh drawing program reads and writes. They are written as
     `pict('picture, "resources/picture/<addr>.pict")`, and the builder
     drops the 512 bytes. No PNG is made beside them yet: that waits on
     the host drawing a picture's text (`qd/PicPlay.h`).
   - The 13 fonts are sfnt containers (version 0x00010000) with no
     outlines, so not TrueType fonts: five bitmap fonts (`bdat`, `bloc`,
     `cmap`, `head`, `hhea`, `hmtx`, `hsty`, `maxp`, `name`, `post`) and
     eight 2080-byte metric-only fonts (`cmap`, `head`, `hhea`, `hmtx`,
     `hsty`) - `docs/qd/fonts-sfnt.md`. Each is a directory,
     `sfnt('sfnt, "resources/sfnt/<addr>")`: every bitmap strike a BDF
     file (its `bloc` metrics as properties), every other table a text
     file, which `tools/fonts/newtonsfnt.py` packs back byte for byte
     (ctests `tools.NewtonFonts`, and `host.ROMSourceFontEdit`, where a
     pixel flipped in a BDF glyph reaches the screen). A font the text
     form did not reproduce would stay a `.sfnt` file, byte for byte the
     binary; none does.
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

- 3038 definitions in `objects/`, grouped by the root they belong to (see
  "The files, by what they belong to");
- 5480 functions in `functions/`;
- the 8336 maps in `maps.ns`;
- `layout.tsv`, one line per object, and the aliases;
- the resources: 525 PNGs, 14 WAVs, 7 PICTs, 13 fonts, and 87 `.bin`
  files (the compressed sounds, the ring tones' parameters, the tables and
  a few small shapes).

There are some 6,500 files, about 19 MB on disk (NTFS). In bytes that is
1.2 MB of object source, 8.3 MB of function source, 0.8 MB of maps,
2.1 MB of manifest and 2 MB of resources.

What it is not yet:

- The compressed sounds (IMA ADPCM), the ring tones' `TDTMFCodec`
  parameters and the tables are still `.bin` files.
- Inline objects are named by path in the manifest (`obj_x.slot`,
  `obj_x[2]`), and **an edit that moves them needs no change to the
  manifest**; `--relayout` does the rest:
  - a frame's slots put in another order keep their paths, which go by
    tag, and the frame gets a map in the new order;
  - an element put into an array moves every later element's path along.
    Each path is laid out as the object now at it, whatever the manifest
    said was there: its size, and the kind bits of its header's flags.
    The paths that are new go at the end;
  - a renamed slot is a path the manifest does not know, and goes at the
    end; the old path, gone, is dropped.

  `edit-test` does both of the first two, on @271 (the card types frame:
  its first two slots swapped) and @113 (an array of month names: an
  element put at its front). `host.ROMSourceEditMoved` reads them back.

## Where the tree lives

**The tree is committed, as `romsrc/` (2026-09-30).** The owner decided
so once the three conditions below held. It was extracted once, at the
commit that added it, and is now the source: the extractor is not run
over it again. `romsrc/README.md` says how to edit it, build it and boot
from it.

- The build uses the committed tree: **the default build makes
  `<build>/romsrc-objects.bin` from it** (a custom command, made again
  when a file of `romsrc/`, the builder's Python or `newtonscript`
  changes; the `romsrc` target builds it alone), and **newton and
  newtonscript boot it by default** (2026-09-30; "The default boot"
  below).
- `host.ROMSourceRoundTrip` still tests the extractor, by extracting
  afresh into the build directory (the `romsrc-extract` target does the
  same by hand).
- `host.ROMSourceCommitted` says whether the committed tree still builds
  byte for byte the ROM's. Since the first intentional edit (below: the
  Newton Internet Enabler built into the extension) it builds the tree
  less what was added (`romsrc.py build --original`), so it still
  catches an unintended change to the rest.
- `.gitattributes` keeps its resources binary and its `.ns`/`.tsv` LF on
  every system.
- It is 7696 files, 20 MB on disk.

What follows is the reasoning from before, when the tree was generated
at build time:

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
| **The ROM parameter block** (`gParamBlock`, 0x1000, a page: `gROMVersion`, `gROMStage`, ...) that a package's native code reads - NTK's stubs choose their entry points by `gROMVersion` | `armcpu/PackageNativeCPU.cpp` (the emulated CPU maps the ROM at 0) | yes | **the object file's block**: `romsrc/romdata/gParamBlock.bin` (`romdata.tsv`), read through `ROMBytesAt` when there is no image. Traced over the fixtures (Mahjongg, NewtHack): 0x13dc is the only ROM address their code reads. Any other ROM address stops the CPU as an unanswered read |
| **The other generated tables** (grammar, recognition nets, fonts' metrics tables, CRC, the codecs' tables, ...) | `src/*/…Tables.cpp` | no: compiled in | unchanged |

So the first no-`--rom` boot needs only the first two rows from the built
tree. The later rows are things the machine can do without, and each
came back as its own piece of work.

**Now (2026-09-30) nothing reads the ROM image at run time when the OS
boots from the object file.** `ROMImageBase` answers nil; its users and
`ROMBytesAt`/`ROMRegion`'s all find their data in the object file's
blocks - the lexicons, the parameter block, the ROM extension - and the
object area and magic pointers are the file's own. What still needs the
ROM image is only what checks the reconstruction against it: the tests
that name `build/MP2x00US` or `DebugRom/`, and the unit tests compiled
with `NEWTON_ROM_BIN`/`NEWTON_ROM_IMAGE` (they import the image
themselves; a configure without the image does not register them).

### The default boot

- `newton` and `newtonscript` with neither `--rom` nor `--objects` boot
  the object file: `NEWTON_OBJECTS`, else `romsrc-objects.bin` beside the
  program or in the directory above it, else the build's own path,
  compiled in (`src/host/HostObjectsFile.h`).
- **No fallback to the ROM image.** With no object file, both say how to
  build one (`cmake --build <dir> --target romsrc`, or `romsrc.py build`
  by hand) and stop. Falling back would hide a broken build behind a boot
  that looks the same; `--rom` asks for the image explicitly, and newton
  says on stderr that it booted the image, not the reconstructed data.
- `newtonscript --no-objects` runs without the ROM's objects (all the
  builder's compiling needs; it also names a file that is not there
  through `NEWTON_ROM`, which works as before).
- The build needs no ROM: configured with `-DNEWTON_ROM_BUILD=<a
  directory that is not there>`, it builds newton, newtonscript and the
  object file. `src/CMakeLists.txt` wraps `add_test`: a test whose
  command names `build/<rom>` or `DebugRom/`, or whose program is
  compiled with `NEWTON_ROM_BIN`/`NEWTON_ROM_IMAGE`, is not registered,
  and one that needs a fixture only such a test sets up is disabled.
- The host's demo ctests boot the default. On the image on purpose, as
  the cross-check: `host.Newton` (the demo boot), `host.NewtonNoROMSameScreen`
  and `host.NewtonEditedSameScreen` (both ways, pixel for pixel), and the
  decompiler's `host.NSDecompileRoundTrip`.
- The unit tests that need only the ROM's objects (fonts, locale bundles,
  prototypes - 39 of them: `test_Views`, `test_Text`, `test_Dates`, the
  text engine's, recognition's, ...) import the object file too, compiled
  with `NEWTON_OBJECTS` (`NEWTON_OBJECTS_FILE` in `src/CMakeLists.txt`);
  `test_PackageManager` reads the extension's packages through
  `ROMBytesAt`. `host.NewtonPackage.extract` makes its loadable
  Formulas2.pkg out of the object file (`packages.py <object file>`: the
  extension is one of its blocks), byte for byte the one it made out of
  `build/MP2x00US`.
- **With no ROM image, 248 of the 259 ctests run and pass.** The 11 left
  out are the ones whose point is the ROM: the importer's own test
  (`frames.ROMImport`), the ROM code run as an oracle (`compression.LZOracle`,
  `utility.DES`), the tests that read packages out of the raw image
  (`frames.FramesPart`, `packages.PackageIterator`), the extractor and
  decompiler round trips (`host.ROMSourceRoundTrip`, `host.ROMSourceCommitted`,
  `host.NSDecompileRoundTrip`), and the boots on the image
  (`host.Newton`, the two SameScreen comparisons).

### Where it stands: the OS boots with no ROM image

`newton --objects <file>` boots the OS on the object file built from the
tree, with no image anywhere it could find one - and since 2026-09-30
plain `newton` does, on the build's own `romsrc-objects.bin` (below, "The
default boot"). ctest `host.NewtonNoROM` boots that default.

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
- (Later, 2026-10-01) The lexicons are text now: the word lists as
  `lexicons/<name>.words`, the lexical grammars as `.lex` graphs of
  character sets, built into the ROM's bytes by
  `tools/lexicons/newtonlex.py` - all 40 byte for byte, because a word
  trie is a function of its set of words (each row in character order,
  each sibling offset as short as will hold it) and a graph is written in
  the order it lies. A list an edit grows past its room is moved
  (`--relayout`; the host's `ROMMovedAddress`). Ctests
  `tools.NewtonLexicons`, `host.ROMSourceLexiconEdit`.
- (2026-10-01) So are the Unicode frame's tables, the sorting tables and
  the locales' break tables: `texttable(class, "....txt")`, each file
  naming its format (`tools/tables/newtontables.py`: to-unicode,
  from-unicode, char-classes, class-types, class-deltas, sort-table,
  break-table), all 16 byte for byte. Ctests `tools.NewtonTables`,
  `host.ROMSourceTableEdit` (a and z swapped in the sorting table, and
  StrCompare follows).

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
| Cardfile | 3445 | 336 | 0 |
| Connection | 3059 | 417 | 0 |
| FaxViewer | 666 | 77 | 0 |
| Formulas | 1188 | 125 | 0 |
| help book | 834 | (no functions) | |
| ListView | 2967 | 266 | 0 |
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
  - **every function is source.** The last 3 (2 in Cardfile, 1 in
    Connection) had loops whose hidden locals the sorted order pins close
    together:
    - a foreach's `val|iter` just before its variable, with `|index` just
      after and `|result` further on;
    - a foreach over a slot and a value, neither named yet;
    - a loop over a variable an inner function closes over, whose name is
      therefore fixed.

    `greedy_sorted_sums` solves these when the search gives up. It goes
    position by position: each loop variable is chosen to fix its hidden
    names together, since their hashes differ from the variable's by
    constants mod 2^32. Of the choices that work, it keeps the one that
    leaves the most room above. Names are kept under the lexer's 253
    letters ("Symbol too big"). The committed tree's two package trees
    were brought up to date with it; nothing else in it changed.
  - **ListView was built with debugging information, and now round-trips
    too.** Every one of its functions has a sixth slot, `DebuggerInfo`:
    nil, or a `'dbg1` array that holds
    - the count of names from the enclosing argFrames,
    - those names,
    - then each stack variable's name by its index.

    It is exactly what the compiler makes when variables' names are kept
    (`TFunctionState::MakeCodeBlock`, with `dbgNoVarNames` nil); no new
    flag was needed.
    - The decompiler reads those names, so **ListView's source has its
      real variable names** (`func(aList, anIndex, aLevel) ... for ti :=
      ...`). It marks such a record `@@ <addr> names`, and the round trip
      and `--compile-records` compile that record with names kept.
    - One more NTK habit shows in the debug build. A function inside
      another that closes over nothing was compiled inside it all the
      same, so its `'dbg1` names the enclosing function's variables, and
      it is still pushed with no `set-lex-scope`, having no argFrame of its
      own. Such a function is written inline rather than as a
      `kFunction_` constant.
    - `CompareCode` now compares `DebuggerInfo` as well.
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

### An edit that moves objects still boots

`romsrc.py build --relayout` lays the objects out afresh instead of at
the layout's addresses. They go one after another in the layout's order,
at the sizes they now have, so an edit that grows or shrinks one object
moves every object after it. An object the layout does not know goes at
the end.

The builder also makes what an edit needs that the layout does not have:

- **A map for a frame whose slots are no longer its map's**, or for a new
  frame. This happens when a slot is added, taken away or renamed. The
  builder reuses a map already there with those very tags and no
  supermap, or makes a new one (`romsrc_map_N`, class 0: unsorted).
- **A symbol object** for each name the tree now uses that the area has
  none for.

New objects take the flags the ROM's own of their kind have: 0x43 for a
frame, 0x41 for an array or map, 0x40 for a binary or symbol.

**The extension's parts are laid out afresh too.** With `--relayout`,
each package's frames part is built at wherever it now falls, at its own
alignment (8 in a version 0 package, from the part's start), so a part
that grows or shrinks moves every package after it. The builder keeps
everything that records where things are in step:

- **each package's directory**: its size, and its part entry's size
  (twice);
- **the extension's header**: its length, each config entry's offset, and
  the package list's size;
- **the frame export table `'fexp`**: the refs of the objects the parts
  export (through magic-pointer table 2), each moved as its object was.

The header's checksum word (`RExHeader.checksum`, +8, 0x98e6 in this
ROM) is left as it was, because **nothing in the ROM reads it**:

- **The only code that looks at an extension's header** is the boot's
  scan: `RExScanner` 0x00313888, over `ScanForREx` 0x00313818 and
  `TestForREx` 0x003137dc. `TestForREx` accepts a block on its two
  signature words and an id below 4 alone. `ScanForREx` records the
  block's `start` (+0x20), its address and its `length` (+0x18), and
  steps on by the length.
- **The ROM's own check for a changed ROM or extension is something
  else.** `OSCalibrationParameters::CalculateROMREXCheckSums` 0x001a71b8
  sums the whole ROM and extension, word by word, as two 32-bit sums:
  the low halves and the high halves of each word. It keeps them with
  the calibration data in flash, and a change forces recalibration
  (`operator==(TROMREXCheckSums, …)` 0x001a7150). That sum is computed
  when it is needed, so it follows an edited extension by itself.
- **None of the usual sums gives 0x98e6** over the block: of its bytes,
  its words, its halfwords, the low or high halves, their XOR or the
  ROM's `ChecksumRotateRightXOR`, with the field zeroed, set to
  0xffffffff or left as it is. It was presumably written by Apple's
  build tools, and it matters to no code in the ROM.

An unedited tree laid out afresh is byte for byte the ROM's, extension
and all.

`edit-test` now also lengthens a string in the first package's part
(Cardfile's "Cards and Notes"). The extension grows by 20 bytes, every
package after Cardfile moves (Setup among them), and the boot still
draws the same Setup Welcome.

**Decision: the constants stay the ROM's addresses, and the object file
says where each object went. The builder does not generate
`ROMConstants.h`.**

- The C++ names objects by the ROM's addresses: `ROMConstants.h`,
  `RSSymbols.h`, `kROMSymbolTable`.
- The object file (version 3) carries a table of every object that is
  not where the ROM has it: the ROM's ref, then the ref now.
- `frames/ROMImport.cpp` looks each constant up in that table (`MovedRef`)
  as it imports, and so does `TranslateROMRef`.
- Generating `ROMConstants.h` instead would tie each build of the host to
  one layout of the data. With the table, an edit to the tree needs a
  new object file and nothing else, which is the point of the tree.

The test of editability is ctest `host.ROMSourceEdit` followed by
`host.NewtonEditedSameScreen`:

- `romsrc.py edit-test` copies the tree and makes two edits:
  - it lengthens one string near the area's start (`obj_3c5f05`, "28.8
    and faster", gains " (edited)");
  - it adds a slot `romsrcEdited` to `Rcanonicalinkshape` holding a new
    frame `{romsrcNote: "added by edit-test"}`. That needs two new maps and
    two new symbols.
- It builds the copy with `--relayout`, and **43751 objects move**.
- The OS booted on the result draws the Setup Welcome pixel for pixel as
  the ROM image's boot does.
- `host.ROMSourceEditValue` reads the edit back:
  `ROMConstant("canonicalInkShape").romsrcEdited.romsrcNote` is "added by
  edit-test".

### The first intentional edit: the Newton Internet Enabler built in

On 2026-09-30, by the owner's decision, the tree stopped being the ROM:
three of Apple's Newton Internet Enabler 2.0 packages (Newton Devices,
the Enabler, Internet Setup) were added to the extension's package list,
so that every boot has the NIE with no store (`romsrc/README.md`, "The
Newton Internet Enabler, built in", has the whole of it).

- They are kept as the package files the NIE ships as (`rex/*.pkg`,
  `rex.tsv` lines at address `-` marked `rom-form`), and the builder puts
  each into the form the ROM keeps its own packages in, at the address
  it falls at (`packages.py`'s `rom_form_package`): refs made addresses,
  the relocation chunk applied and taken out, exports given `'fexp`
  entries and imports resolved to them - what Apple's ROM build did to
  the ten, since the ROM's loader relocates nothing in the ROM and never
  installs a ROM part's imports.
- One change to the NIE itself was needed, kept as a checked byte patch
  beside it (`rex/inetenbl.patches.tsv`): it names its state after its
  package's id, and a package in the ROM has no pkgRef.
- The extension grows by 0x92000 bytes to end at 0x880048; the padding
  keeps the page tables and the patch table on pages.
- `build --original` leaves the additions out, which is how
  `host.ROMSourceCommitted` still compares the rest with the ROM; the
  extractor's own byte-for-byte test (`host.ROMSourceRoundTrip`) is
  untouched, and the boot's first screen is still the image's
  (`host.NewtonNoROMSameScreen`).

### The files, by what they belong to

The definitions are no longer cut by address:

- **Objects: each file is the definitions one root dominates**, meaning
  every path from the roots to them passes through that root. The roots
  are the top-level objects: those reached from the magic pointers, from
  an R constant, or from no object at all.
  - The file is named after the root. That is its R constant's name
    (`Rbuiltinfunctions.ns`), or what the root says it is: a template's
    `debug` name, an application's symbol or a title, with its
    magic-pointer index (`calendar_mp18.ns`, `worldClock_mp298.ns`,
    `ExtrasDrawer_mp832.ns`). Failing that, it is `mp<index>.ns`.
  - What more than one root shares, and the groups of fewer than eight
    definitions, go into `misc-NNN.ns` in address order.
- **Functions: each file is named by the path that holds the function.**
  For example, `functions/Rbuiltinfunctions.AddAlarm.ns`, where it was
  the address.

The builder reads whatever files are there, so the grouping is only for
people. `edit-test` finds its string by its address in the layout,
whatever file it is in.

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
  - the area laid out freely (done: `--relayout`, and the object file's
    table of moved objects rather than a generated `ROMConstants.h`).

## Open questions
- **mosrun** (https://github.com/MatthiasWM/mosrun) runs Apple's own
  Newton build tools (the Rex builder, the ARM tools). It could check
  step 5's packages and ROM extension against Apple's builder, byte for
  byte.
