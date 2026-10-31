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
| `objects/` | The objects, as definitions `name := value;` in a notation that is NewtonScript's literals plus a few constructors (below), every function written in the frame that holds it. A file holds a root object - one a magic pointer or the ROM's C code names - and what only it reaches, and is named after it; the directories say what the roots are: `apps/` (`calendar.ns`, `cardfile.ns`, ...), `protos/` (`protoFloatNGo.ns`, ...), `newtapp/` (the NewtApp framework's protos), `canonical/` (the frames the C code makes its objects from), `slips/`, `views/` (pickers, popups, filters), `pictures/`, `sounds/`, `soups/`, `recognition/`, `system/` (the rest: `builtinFunctions.ns`, `viewRoot.ns`, ...). `values.ns` holds the roots that are plain values (strings, numbers), `shared/` what several roots reach and none alone. A definition may be moved to any file: the files only group them. |
| `maps.ns` | The frame maps (the slot names of the ROM's frames). |
| `constants.tsv` | The constants the notation and the functions may name: the views' flags, justification, formats and classes (from `src/views/ViewFlags.h`), the packed font specs' families, sizes and faces, and the error codes (the DDK headers', then `src/frames/NSErrors.h`'s and the reconstruction's other names). |
| `renames.tsv` | Each definition's name as the extractor wrote it (`obj_4f901d`, its address; `Rbuiltinfunctions`, its R-constant) and its name now - for reading older notes, `docs/work-log.md` among them. A package's part tree has its own. |
| `resources/` | The binaries: bitmaps as PNG (`bits`, `mask`, `cbits`), simple sounds as WAV, QuickDraw pictures as directories - `picture.txt`, the opcodes as text, and a PNG per bitmap (`tools/pict/README.md`) - fonts as directories - a BDF file per bitmap strike and a text file per other table, which the builder packs back into the `'sfnt` binary (`tools/fonts/README.md`; what the tables hold, `docs/qd/fonts-sfnt.md`). The Unicode frame's tables (the encodings' maps, the character classes and case tables), the sorting tables, the locales' word- and line-break tables, the ink codec's code books, the letter trigrams, the Letter Shapes pictures and the cursive recogniser's trained tables (DTEHeader, DTEMain, PPDMain, DTETrigrams) are text too, `texttable(...)`, each saying its format (`tools/tables/README.md`). The locales' Airus dictionaries (`'AirusA`) are `.lex` graphs as the lexicons are (`lexicon(...)`). Nothing here is `.bin` any more. |
| `lexicons/`, `lexicons.tsv` | The recognisers' lexicons, with their ROM addresses: the word lists as `.words` files (a word to a line, with its attribute after a tab where the list gives one - add or take out a line to add or take out a word) and the lexical grammars - dates, times, numbers, money, phone numbers - as `.lex` graphs of character sets. The builder makes the ROM's Airus dictionaries out of them, byte for byte (`tools/lexicons/README.md`); a list grown past its room in the ROM needs `--relayout`, which moves it. |
| `romdata/`, `romdata.tsv` | The ROM's other data that code reads at its ROM address: the parameter block `gParamBlock` (0x1000, a page - `gROMVersion`, `gROMStage`, `gHardwareType`, ...), whose version words a package's native code reads (`armcpu`), as text: `gParamBlock.txt` names each word by the ROM's symbol (`0x13dc gROMVersion 0x00020002`) with the runs of nought one line each (`tools/rex/README.md`, format `words`). Added with `romsrc.py romdata build/MP2x00US romsrc`, the extractor's own code for it. |
| `rex/`, `rex.tsv` | The ROM extension, in pieces: its header and config entries, and the ten packages. The pieces around the packages are text (`tools/rex/README.md`): the header (`header.txt` - its config entries name the pieces they cover, and its length and offsets are worked out on build), the config entries `dio`, `gpio` and `ralc` as rows of words, the page tables `ptpt`/`glpt`, the export table `fexp` (each ref with its package and object), the patch table `jump` (each branch's target) and the padding (a fill byte and an alignment). A package with a frames part is `<Package>.head.txt` (its directory: names, flags and strings, the sizes and offsets worked out from the part) and the part as a tree of its own in `<Package>/` (the same layout as this one). WorldData is `WorldData.head.txt` and its soup part `WorldData/` (`rex.tsv`: `store`), the store as text - its 753 countries and cities one entry to a line in `entries.ns`, its indexes and word hints made on build (`tools/stores/README.md`). ScreenBuffer and ScreenDrivers are directories (`package`): `package.txt` and each protocol part as ARM source, `part0.s`, assembled where the package falls (`tools/armpart/README.md`). The three packages added to it (`newtdev/`, `inetenbl/`, `inetstup/`, at address `-`, `rom-form`) are directories too: `package.txt`, each frames part a tree (`partN/`), each protocol part and native module ARM source; each has a README, which for the two that needed changing says what was changed and why. |
| `magic.tsv` | The magic-pointer table: `@index`, the object it names, and any other names it goes by (the ROM's R-constants that are magic pointers name them). |
| `layout.tsv` | The manifest: every object's address, path in the source and header flags, each frame's map, and aliases (shared objects that a compiled function makes afresh). |
| `bytecode.tsv` (in a part's tree) | The functions kept as bytecode, with the reason, when any are. None are now. |

## Reading it

A file of `objects/` reads as NewtonScript a person might have written:

```
protoFloatNGo := {_proto: @protoFloater, viewChildren: [|Array|: {_proto: @protoClosebox}], debug: 363081};

calendar := {
	...
	prefsView: {
		viewBounds: {left: 0, top: 0, right: 200, bottom: 200},
		viewJustify: vjParentCenterH + vjParentCenterV,
		viewQuitScript: func() begin
			(theApp.prefsOpen := nil)
		end,
		ReOrientToScreen: @defRotateFunc,
		...
```

- **A function is written where it is**, `slot: func(...) begin ... end`,
  as the decompiler (`tools/newton-rom/analysis/nsdecompile.py`) wrote
  it, and the builder compiles it with the host's own compiler. The ROM
  keeps no names for arguments and locals; most now have one
  (`tools/newton-rom/analysis/naturalcode.py`): a script's after the
  system's (`viewClickScript: func(unit)`), another argument after what
  the calls of its function pass (`FindInSoup(title, findWords, dateRange,
  soup, ...)`), a local after what it is first given (`pt := GetPoint(0,
  unit)`, `mtgDuration := self:GetMeetingSlot(m, 'mtgDuration)`). Those
  it could not name are still the decompiler's `a1`, `l1`; a name can be
  changed freely, as the compiler keeps none - but not to a word the
  function uses as a variable, and not in a function whose argument frame
  passes 20 slots (its arguments, locals and the hidden ones of its
  loops, and three more), whose map the compiler keeps sorted by name.
  Comments in it are kept. A function that had constants of
  its own when it was built (a function it makes into a closure, a value
  computed at build time), options for the compiler, or that is a value
  computed rather than a `func`, is `compile([constants: {name: value,
  ...},] [options: "...",] source)`.
- **`@name` is a magic pointer** (`magic.tsv`), in the objects and in the
  functions alike: `_proto: @protoFloater`, `GetUnionSoupAlways(@calendarSoupName)`.
  `@387` still works. A bare name is the definition of that name.
- **Integers may be sums of constants** (`constants.tsv`): `viewFlags:
  vVisible + vClickable`, `viewFormat: vfFillWhite + vfFrameBlack + vfPen(1)`,
  `titleFont: tsSystem + tsSize(9) + tsBold`, `errorCode:
  kEndpointErrNoOptions` - in the functions too (`.viewFont := tsHWFont +
  tsSize(10)`, `Throw('|evt.ex.comm|, kCommErrRequestCanceled)`), where the
  builder makes a sum that is a whole operand one number before compiling,
  so it compiles to what the number did.
- **`same("path")`** is an object only compiled functions hold and share:
  the one a function holds at that path. These come last in a file.
- The binaries are files of `resources/`: `bitmap(...)`, `sound(...)`,
  `picture(...)`, `sfnt(...)`, `texttable(...)` and the rest.

The notation is described in full in the documentation of `romsrc.py`
(`python tools/newton-rom/analysis/romsrc.py --help`). The tree was put
in this form by `tools/newton-rom/analysis/naturalize.py`, once, from the
extractor's (a definition named after its address, each function in a
file of its own under `functions/`, magic pointers and flags as numbers);
the two build to the same object file byte for byte.

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
  Enabler has been added to its extension (below), the letter-set box of
  the Handwriting Recognition slip and Setup's handwriting page offers the
  host's handwriting engines (`docs/recognition/engines.md`: the cluster
  `LetterSetCluster`, the box `protoRecognitionCluster`, the example
  `wordExample`), the help
  book has two topics on writing with them (`rex/help_book`: the
  `unistrokeHelp*` items and their two pages; the pictures made by
  `test_Unistroke --pict`), `InstallFormPart` writes a card's socket from
  the install info rather than a variable that does not exist - a ROM
  bug fixed behind `RomBugFixed()`, the host's switch
  (`docs/rom-bugs.md`; `NEWTON_ROM_BUGS=1` runs the ROM's code), and a
  screen smaller than any MessagePad's (the T-Deck Pro's 240x320) is
  allowed for: an application is not asked to turn an upright screen
  upright (`viewRoot.Open`), the Typewriter keyboard is made the
  screen's width when its small size does not fit
  (`alphaKeyboard.viewSetupFormScript`), Internet Setup's edit slip and
  its keyboard fit the screen (`rex/inetstup/README.md`), and so does
  the slip that asks for a missing piece of a worksite or the owner's
  card - Time Zones' "choose the closest city" (`protoValidateSlip`, @508) - and it
  may be changed further to enhance the
  system. The git tag `romsrc-rom` keeps the last
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

A definition can be moved to another file, or a new file made under
`objects/`, freely. Renaming one needs the layout to follow, as the
layout knows the objects by their names, so it is done with

```
python tools/newton-rom/analysis/romsrc.py rename romsrc OLD NEW
```

which renames the definition, every reference to it, the `same("...")`
paths that start at it and its lines in `layout.tsv` and `magic.tsv`
(for a magic pointer's object, every `@OLD` too, the packages' trees
included). The object file it builds is the same.

`romsrc.py edit-test` is the test that this works: it edits a copy and
the copy still boots (ctests `host.ROMSourceEdit`,
`host.ROMSourceEditValue`, `host.NewtonEditedSameScreen`).

Nothing in `layout.tsv` needs changing by hand.  That includes an edit
to a function that moves its literals: a `same("...")` definition and the
layout's alias lines name an object by the path a compiled function holds
it at (`...literals[3]`), and the builder takes the object from wherever
a compiled function still holds it - that path or any alias line's, the
reading most agree on - and finds a moved literal's alias by its contents
(ctest `tools.ROMSourceFunctionEdit`).  An object no function holds any
more is simply not laid out. Slots can be put in
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

**What the NIE needed changing** (`rex/inetenbl/README.md`; first a
byte patch, now an edit to the source - four places marked `//
DEVIATION`): the NIE keeps its state in a global named after its
package's id, `GetPkgRefInfo(ObjectPkgRef(...)).id`, and a package in the
ROM has no pkgRef - `ObjectPkgRef` answers nil and `GetPkgRefInfo(nil)`
throws, so `InetStartUp` could not start. The four functions that make
the name use `'id` instead, so the name is `"PkgVars_id"`. Nothing else in the three packages minds
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
