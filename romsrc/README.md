# romsrc: the ROM's NewtonScript world as source

This tree is everything the Newton MessagePad 2x00 US ROM (2.1, build
717006) keeps as NewtonScript objects, written as files a person can
read, edit and build:

- the object area: 46538 frames, arrays, symbols and binaries;
- its magic-pointer table;
- the recognisers' lexicons;
- the ROM extension with its ten built-in packages - and, added to it,
  Apple's Newton Internet Enabler 2.0 (below).

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
| `resources/` | The binaries: bitmaps as PNG (`bits`, `mask`, `cbits`), simple sounds as WAV, pictures as PICT, fonts as directories - a BDF file per bitmap strike and a text file per other table, which the builder packs back into the `'sfnt` binary (`tools/fonts/README.md`; what the tables hold, `docs/qd/fonts-sfnt.md`). The Unicode frame's tables (the encodings' maps, the character classes and case tables), the sorting tables and the locales' word- and line-break tables are text too, `texttable(...)`, each saying its format (`tools/tables/README.md`). What has no editable form yet (compressed sounds, the recognisers' trained data) is `.bin`. |
| `lexicons/`, `lexicons.tsv` | The recognisers' lexicons, with their ROM addresses: the word lists as `.words` files (a word to a line, with its attribute after a tab where the list gives one - add or take out a line to add or take out a word) and the lexical grammars - dates, times, numbers, money, phone numbers - as `.lex` graphs of character sets. The builder makes the ROM's Airus dictionaries out of them, byte for byte (`tools/lexicons/README.md`); a list grown past its room in the ROM needs `--relayout`, which moves it. |
| `romdata/`, `romdata.tsv` | The ROM's other data that code reads at its ROM address: the parameter block `gParamBlock` (0x1000, a page - `gROMVersion`, `gROMStage`, `gHardwareType`, ...), whose version words a package's native code reads (`armcpu`). Added with `romsrc.py romdata build/MP2x00US romsrc`, the extractor's own code for it. |
| `rex/`, `rex.tsv` | The ROM extension, in pieces: its header and config entries, and the ten packages. A package with a frames part is `<Package>.head.bin` (its directory), the part as a tree of its own in `<Package>/` (the same layout as this one), and `<Package>.tail.bin`. The three packages added to it (`newtdev.pkg`, `inetenbl.pkg`, `inetstup.pkg`, at address `-`) are ordinary package files, with `inetenbl.patches.tsv` beside the one that needed changing. |
| `magic.tsv` | The magic-pointer table: `@index` and the object it names. |
| `layout.tsv` | The manifest: every object's address, path in the source and header flags, each frame's map, and aliases (shared objects that a compiled function makes afresh). |
| `bytecode.tsv` (in a part's tree) | The functions kept as bytecode, with the reason, when any are. None are now. |

The notation, `same("path")`, `function("…")`, `bitmap(…)` and the rest,
is described in the documentation of `romsrc.py` (`python
tools/newton-rom/analysis/romsrc.py --help`).

## Building it

**The default build builds it**: `cmake --build <build>` writes
`<build>/romsrc-objects.bin`, which is what `newton` and `newtonscript`
boot from, and makes it again whenever a file here, the builder's Python
or `newtonscript` changes (`cmake --build <build> --target romsrc` builds
it alone). It needs Python 3 and the host's own `newtonscript` - no ROM
image and no `build/MP2x00US`. By hand:

```
python tools/newton-rom/analysis/romsrc.py build romsrc --relayout -o build/objects.bin --newtonscript build/host/host/newtonscript
```

- The builder compiles the functions with the host's own compiler
  (`newtonscript --compile-records`, run with no ROM image).
- It lays the objects out as `layout.tsv` says and writes the object file
  the host loads.
- The tree is no longer the ROM byte for byte: the Newton Internet
  Enabler has been added to its extension (below), and it may be changed
  further to enhance the system. The git tag `romsrc-rom` keeps the last
  tree that is the ROM's but for the additions: `--git-ref romsrc-rom`
  builds that tree out of git, `--original` leaves the additions out, and
  `--check build/MP2x00US` compares the rest with the ROM, which needs the
  extracted ROM (`build/<rom>`, `tools/newton-rom/pipeline.py`).

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

**It is what the OS boots from by default:**

```
build/host/host/newton
```

boots `<build>/romsrc-objects.bin`, with no ROM image anywhere. newton
looks for the file in `NEWTON_OBJECTS`, beside itself, in the directory
above and at the build's path (`src/host/HostObjectsFile.h`); when there is
none it says how to build it and stops - it does not go looking for a ROM
image. `--objects <file>` boots another object file (an edited copy's);
`--rom build/MP2x00US/rom.bin` boots the original ROM image instead, for
checking. `newtonscript` finds its objects the same way.

With the tree as committed, the boot draws exactly what the boot on the
ROM image draws: ctest `host.NewtonNoROMSameScreen`. The host's demo tests
all boot the default, so they run on this tree; a checkout without the ROM
image builds, boots and runs them (the tests that compare with the ROM are
then not registered).

## The ctests

| Test | What it checks |
|---|---|
| `host.ROMSourceCommitted` | The tree as the git tag `romsrc-rom` has it - the last tree that is the ROM's - less what was added to it on purpose (`--original`: the Newton Internet Enabler), still builds byte for byte the ROM's with today's builder (`romsrc.py build romsrc --git-ref romsrc-rom --original --check build/MP2x00US`). The working tree is not checked, so it can be changed to enhance the system; the test is registered only when the tag is in the clone (`git fetch --tags`). |
| `host.ROMSourceRoundTrip` | The extractor itself: a fresh extraction into the build directory, built back byte for byte. It does not touch this tree. |
| `host.NewtonNoROM`, `host.NewtonNoROMSameScreen` | The OS booted from this tree's object file, and its screen against the ROM image boot's (the Setup assistant's Welcome, which the built-in NIE does not change). |
| `host.NewtonNIEBuiltIn`, `host.NewtonNIEOverBuiltIn` | The Newton Internet Enabler built in: its packages active on no store, Internet Setup in Extras, the link grabbed and a name looked up; and the fixtures' copies installed over it (kept on the store, not activated). The NIE's other tests (`host.NewtonInet`, `...InetSetup`, `...InetHostSetup`, `...InetFSM`, `...NetHopper`) use the built-in one too. |
| `host.ROMSourceEdit`, `host.ROMSourceEditValue`, `host.ROMSourceEditMoved`, `host.NewtonEditedSameScreen` | A copy of this tree edited (strings lengthened, slots swapped, an array element inserted, a slot holding a new frame added), built laid out afresh, the edits read back, and booted. |

## The Newton Internet Enabler, built in

**The owner's decision:** Apple's Newton Internet Enabler 2.0 is part of
every boot, with no store needed, as the extension's own packages
(Setup, Cardfile, ...) are. It is the first change made to this tree on
purpose, and the reason it no longer builds the original ROM byte for
byte. Three of the NIE's packages are in the extension, after WorldData,
in this order (`rex.tsv`):

| File | Package | Why |
|---|---|---|
| `rex/newtdev.pkg` | " Newton Devices" | its units (`NameServerInterface:NSG`, `Lantern:NSG`) are what the NIE's modules import; the host's tests have always installed it with the Enabler |
| `rex/inetenbl.pkg` | Newton Internet Enabler | the Enabler: `InetGrabLink`, the domain manager, the link manager, `protoEndpointFSM` (`Inet Protos:NIE`) |
| `rex/inetstup.pkg` | Internet Setup | the application, in the Extras drawer's Setup folder as a built-in |

Each file is the one in `fixtures/packages/apple/NIE2/` (the NIE's own
modules - Ethernet, LocalTalk, Modem & Serial, the ISP templates - stay
packages a user installs; they import the Enabler's units from the ROM).
The order is the NIE's own: each package's units are exported before a
package that imports them is loaded (Internet Setup imports its own; the
modules import the Enabler's and Newton Devices').

**How they are laid out.** A package in the ROM is not in the form one
arriving from outside is (`tools/newton-rom/analysis/packages.py`), and
the ROM's loader (`LoadHighROMFramesPackages`, then the frames part
handler) reads a package in the ROM where it lies, relocating nothing.
So `romsrc.py build --relayout` puts each into the ROM's form at the
address it falls at (`packages.py`'s `rom_form_package`), which is what
Apple's ROM build did to the ten:

- its relocation chunk (Newton Devices and the Enabler have one: their
  native code holds addresses in the package) applied to that address
  and taken out, and the flag cleared - the ROM's own packages have none;
- every pointer ref of its frames parts made the object's address;
- each unit it exports given entries in the extension's frame export
  table `'fexp` (magic pointers `@0x2000 + n`, 28 of them after the ROM's
  166), and each import resolved to those entries: a part in the ROM
  never has its `_ImportTable` installed (only a part above 0x037fffff),
  so its imports must be resolved when the ROM is built, as the ROM's own
  Connection and Cardfile parts' were.

The extension grows by 0x92000 bytes (598,016) and ends at 0x880048: on a
MessagePad it would no longer fit the 8MB ROM, which the host does not
have to care about. The padding in front of the page tables and the patch
table keeps them on pages. The page tables themselves (`ptpt`, `glpt`:
MMU entries naming the patch table's physical page, 0x7ee000) are carried
unchanged - nothing on the host reads them; a MessagePad would need them
to name the table's new page, 0x880000.

**What the NIE needed changing** (`rex/inetenbl.patches.tsv`, applied by
the builder, each change checked against the bytes it replaces): the NIE
keeps its state in a global named after its package's id,
`GetPkgRefInfo(ObjectPkgRef(...)).id`, and a package in the ROM has no
pkgRef - `ObjectPkgRef` answers nil and `GetPkgRefInfo(nil)` throws, so
`InetStartUp` could not start. Four functions' five bytes each make the
name `"PkgVars_id"` instead. Nothing else in the three packages minds
where it lies: the NTK unit glue waits on the "Packages" soup as it does
for a stored copy (a change to it during the boot sets it off), and
marking a package in the ROM busy does nothing.

**Installing a copy over it.** A copy of one of the three stored on a
card or the internal store is kept there but not activated: the package
manager refuses a second package of the same name and version
(`kError_Package_Already_Exists`), and the ROM's own NewtonScript tells
the user "The package "Internet Setup" (on store "Internal") was not
activated because a package by the same name (on store "Built-In") is
already in use." (ctest `host.NewtonNIEOverBuiltIn`).

**The cost.** The object file grows by the same 598,016 bytes. The boot
to its first deferred call took 0.99 s rather than 0.84 s (the host,
three runs each), and after the NIE's units have run the frames heap has
18 KB less free and the host process's peak working set is 1.3 MB larger
(the parts' objects imported into host areas).

`--original` builds the tree without them.
