# newton-rom — ROM extraction, symbol recovery and Ghidra import

Tools for turning an Apple Newton MP2x00 *debug ROM* image into an annotated
Ghidra project: a ROM image laid out as the CPU sees it, all ~52,000 debug
symbols demangled, the struct/class/enum definitions from the Newton DDK
headers, and a Ghidra program with functions, C++ class namespaces, typed
prototypes and jump-table thunks already in place.

Everything here is plain Python 3 (3.9+). Two steps have dependencies: the
header extraction uses `libclang` (pip, pinned in `requirements.txt`) and the
Ghidra step uses the `pyghidra` package that ships with Ghidra 11.3 or newer
(developed and tested with Ghidra 12.1.3 and Python 3.13).

```
tools/newton-rom/
  newtonrom/            library
    aif.py                AIF container parser (header, RO/RW/debug areas)
    symbols.py            LANG_NONE debug symbol table reader
    rex.py                ROM Extension (REx) block parser
    jumptable.py          patchable jump table decoder / virtual layout
    demangle.py           cfront/ARM C++ demangler (structured output)
    headers.py            DDK header extraction with libclang + prototype matching
    ghidra_import.py      applies layout + symbols + types to a Ghidra program
  extract_rom.py        step 1: rom.bin + layout.json
  dump_symbols.py       step 2: symbols.json (+ text listing)
  parse_headers.py      step 3: types.json from headers/
  ghidra_scripts/
    import_rom.py         step 4: headless project creation + import
    verify_types.py       step 5: check types against the ROM, recover sizes/vtables -> romfacts.json
    apply_romfacts.py     step 6: apply romfacts.json to the project (+ auto-analysis)
    fix_virtual_calls.py  re-mark the virtual call sites of an older project as fall-through calls
    NewtonROMImport.py    the import, as a Script Manager (GUI) script
    check_import.py       report/spot-check an imported project
  pipeline.py           steps 1-6 in one command
  analysis/             study tools over the finished project:
    decompile.py          decompilation (+ disassembly, callers) by class / name / address
    disasm.py             raw disassembly of an address range (SWI cases, vectors, glue)
    swi_table.py          the system-call table -> docs/os600/swi-table.md
    coverage.py           which ROM functions src/ cites, and that the citations are right
    natives.py            which of the ROM's 1326 native functions src/ answers, by area;
                          --unbound [--ready], --check, --csv
    symbols.py            search the symbol table by regex (address, mangled name, signature)
    globals.py            initial values of globals from the ROM's read-write init area
    xrefs.py              who references a symbol (finds who initialises a global)
    memobj_tables.py      the memory object tables -> docs/os600/memobj-tables.md and src/os600/kernel/MemObjTables.cpp
    exception_names.py    the exception name strings -> src/os600/user/ExceptionNames.cpp
    vtable.py             the entries of a vtable (slot -> method), from rom.bin + symbols.json;
                          --find NAME|ADDR [--slot N] finds the vtables a method is in
    classinfo.py          decode protocol class-info tables (names, dispatch slots, monitor selectors);
                          --all -> docs/protocols/classinfos.md
    romtable.py           constant tables from the ROM as C++ (e.g. src/compression/LZTables.cpp,
                          src/frames/PrintLiterals.cpp); u8..i32 or cstr elements, RAM tables too
    romconstants.py       the ROM's frames constants (RSSYM symbols, R/RS objects, the object area)
                          -> src/frames/RSSymbols.h, RSSymbolTable.cpp, ROMConstants.h, ROMConstants.cpp
    nsfunctions.py        the ROM's built-in NewtonScript functions: --list, --refs NAME (who calls it), --natives -> src/frames/ROMNatives.cpp,
                          --disasm NAME (bytecode disassembly), --object NAME (a ROM frame's slots),
                          --binary-classes (the object area's binaries counted by class)
    packages.py           the packages built into the ROM extension: --parts, --extract DIR (.pkg files;
                          --relocatable, --rename OLD=NEW for loadable copies), --doc docs/packages/rex-packages.md
    nsgrammar.py          the NewtonScript parser's yacc tables, tokens, rules and reserved words
                          -> src/frames/ParserTables.h/.cpp, docs/frames/grammar.md
    spellmaps.py          the spelling checker's character maps (what a letter may be read
                          as, and the letter groups written for one another)
                          -> src/recognition/SpellMaps.cpp
    romdicts.py           the table of lexicons built into the ROM, recovered from the code
                          that writes it -> src/recognition/ROMDictionaryTable.cpp
    rosci.py              the handwriting engine's common info and its seventeen character
                          tables -> src/recognition/RosCITables.cpp
    bigrammar.py          the handwriting engine's eight bigram grammars
                          -> src/recognition/ROMGrammar.cpp, docs/recognition/grammar.md
    bpnet.py              the handwriting engine's classifier net and its trained tables
                          -> src/recognition/BPNetTables.cpp
    render.py             the handwriting engine's dot stencils and anti-aliasing tables
                          -> src/recognition/RenderTables.cpp
    mmumap.py             the boot MMU map, and what a virtual address maps to
                          (--where 0x...) -> docs/memory/mmu-map.md
    recite.py             move src/'s `// ROM 0x...` citations from one ROM image to another
                          (--from build/A --to build/B [--check])
    soupdefs.py           the soups the ROM's applications keep -> src/host/FactorySoups.cpp
    regenerate.py         run every generator above against one ROM (--list, --only NAME)
    romid.py              which ROM a build directory holds (the version string it carries)
  requirements.txt      libclang pin
  tests/                unit tests + oracle comparison against mpdumper
```

## Quick start

```powershell
# from the repository root
python -m venv build\venv
build\venv\Scripts\pip install -r tools\newton-rom\requirements.txt
build\venv\Scripts\pip install --no-index --find-links "D:\apps\ghidra_12.1.3_PUBLIC\Ghidra\Features\PyGhidra\pypkg\dist" pyghidra

build\venv\Scripts\python tools\newton-rom\pipeline.py "DebugRom\MP2x00 US" -o build\MP2x00US --name MP2x00US --ghidra D:\apps\ghidra_12.1.3_PUBLIC
```

This writes `build/MP2x00US/{rom.bin,layout.json,symbols.json,symbols.txt,types.json,romfacts.json,verify-report.txt}`
and creates the Ghidra project `build/ghidra/MP2x00US.gpr` (≈ 40 s for the
import, ≈ 1 minute for verification, ≈ 4 minutes for auto-analysis on a
current desktop). Open the project in Ghidra
normally afterwards. `build/` is git-ignored; everything in it is regenerated
by the tools.

Steps individually:

```powershell
python tools\newton-rom\extract_rom.py "DebugRom\MP2x00 US\Senior CirrusNoDebug image" --rex "DebugRom\MP2x00 US\Senior CirrusNoDebug high" -o build\MP2x00US
python tools\newton-rom\dump_symbols.py "DebugRom\MP2x00 US\Senior CirrusNoDebug image" -o build\MP2x00US\symbols.json --text build\MP2x00US\symbols.txt
build\venv\Scripts\python tools\newton-rom\parse_headers.py headers -o build\MP2x00US\types.json
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\import_rom.py build\MP2x00US --project build\ghidra --name MP2x00US --ghidra D:\apps\ghidra_12.1.3_PUBLIC --no-analyze
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\verify_types.py build\MP2x00US --project build\ghidra --name MP2x00US --ghidra D:\apps\ghidra_12.1.3_PUBLIC
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\apply_romfacts.py build\MP2x00US --project build\ghidra --name MP2x00US --ghidra D:\apps\ghidra_12.1.3_PUBLIC --analyze
build\venv\Scripts\python tools\newton-rom\ghidra_scripts\check_import.py --project build\ghidra --name MP2x00US --ghidra D:\apps\ghidra_12.1.3_PUBLIC --lookup InitIdler --type TAEventHandler
```

GUI alternative for step 3: import `rom.bin` in Ghidra with the *Binary*
loader (language `ARM:BE:32:v4`, compiler `apcs`, base 0), decline
auto-analysis, add `tools/newton-rom/ghidra_scripts` as a script directory and
run `NewtonROMImport.py`, then run auto-analysis.

Tests (the oracle test needs the US ROM and the mpdumper output present):

```
python tools/newton-rom/tests/test_demangle.py
python tools/newton-rom/tests/test_rom.py
build/venv/Scripts/python tools/newton-rom/tests/test_headers.py     # needs libclang
```

## What the debug ROM contains

`Senior ... image` is an ARM **AIF** executable (big-endian):

| Region | File offset | Load address | Size (MP2x00 US) |
|---|---|---|---|
| AIF header | 0x0 | — | 0x80 |
| RO area (code + rodata) | 0x80 | 0x00000000 | 0x71A95C |
| RW initialisers | 0x71A9DC | 0x0C100800 (RAM) | 0x52F0 |
| zero-init data | — | 0x0C105AF0 | 0x2324 |
| debug area | 0x71FC4C | — | 0x1CAF30 |

(The MP2100 D is laid out the same way with its own sizes: RO 0x6F0AE8,
RW 0x23B4, ZI 0x2328, debug area at 0x6F2F1C.)

The debug area is a single `LANG_NONE` section: a flat table of 52,751
symbols in the MP2x00 US and 52,150 in the MP2100 D (name, value, flags). There is **no type information** — the C++
structure is recovered purely from the mangled names. The symbol flags are
unreliable (almost everything is marked "code", including NewtonScript data
objects), so the importer decides for itself what is a function (below).

`Senior ... high` is the **ROM Extension** (`RExBlock`). Its `start` field is
exactly `ROM$$Size` = RO + RW sizes, so the physical 8 MB ROM is simply
`RO ‖ RW-init ‖ REx`, which is what `extract_rom.py` writes to `rom.bin`.
The REx config entries (`pkgl` built-in packages, `ptpt`/`glpt` page tables,
`jump` patch table, …) are listed in `layout.json`.

Despite the "NoDebug" in the file names, these images differ from the shipping
ROM only in carrying the symbol table.

## The patchable jump table

76 % of all `BL` instructions in the ROM (and ~6,000 function pointers) do not
target the callee but a slot in a table of `B` instructions, so that a ROM
extension can patch individual functions. Facts established by the tools and
asserted by `tests/test_rom.py`:

* Physically the table is at ROM address **0x2000**, one `B` per entry
  (16,723 entries in MP2100 D, 16,919 in MP2x00 US).
* The MMU maps it at virtual **0x01A00000** sparsely: virtual page *p* is an
  alias of ROM page `0x2000 + (p/32)*0x1000`, and "owns" the 128-byte slice at
  page offset `(p%32)*0x80`. Slot *i* is therefore at
  `0x01A00000 + (i/32)*0x1000 + ((i/32)%32)*0x80 + (i%32)*4`.
* The `B` offsets are relative to the **virtual** slot address; decoding the
  physical table at 0x2000 gives wrong targets.
* Every exported function appears twice in the symbol table, at its body and
  at its slot (same name). All 16,723 slots branch to the identically named
  body — `dump_symbols.py` verifies this and refuses to continue otherwise.
  The single exception, slot `_DebugStr`, branches to the body named `DebugStr`
  (an assembler alias of the same routine).

In Ghidra the table is materialised as the `JT` block (a copy of the aliased
pages at their virtual addresses) and every slot is a *thunk* of its target,
so the decompiler shows `TFoo::Bar()` at call sites instead of an address.

## Name demangling

Newton OS was built with Apple's cfront-derived ARM C++ compiler. The scheme is
documented at the top of `newtonrom/demangle.py`; the one non-obvious part is
how `T<n>`/`N<count><n>` back-references are numbered inside nested function
pointer types. The rule implemented (a nested list inherits the enclosing
list's entries so far, appends its own, and discards them when it closes) was
derived from the ROM symbols and checked against the Newton DDK header
declarations, e.g. `TMonitor::Init` in `headers/OS600/UserMonitor.h`.

`tests/test_demangle.py` compares our output with GNU libiberty's (the
demangler inside `tools/mpdumper`, whose pre-generated output for the US ROM
is checked in): all 52,751 symbols agree exactly, and we additionally demangle
40 symbols that libiberty gives up on (the nested back-reference cases).

## Types from the DDK headers

`headers/` (the Newton Driver Developer Kit) declares ~250 classes and
structs, 60 enums and 190 typedefs, plus 540 C functions and 1,300 methods.
`parse_headers.py` normalises the files (Mac Roman, CR line endings) into a
flat include directory, parses them as one C++98 translation unit with
libclang configured for the MP2x00 build (`forQ` -> hasVoyager/hasCirrus/
forSenior, `__arm`; V1 headers superseded by their V2 versions and the
NewtonScript `.f.h` files are excluded), and writes `types.json`.  A localised
ROM adds its language - the MP2100 D is `--define forGerman` on top of the
defaults, which is all FOR_INTL and useLanguage depend on in ConfigGlobal.h.

Memory layout is taken from clang (`--target=armeb-none-eabi -mabi=apcs-gnu`).
Before trusting it we checked the ROM: `TAEventHandler::TAEventHandler`
allocates 0x14 bytes, stores the vtable pointer at offset 0 and its four
fields at 4..0x10, and `TUObject::fId` is at offset 0 - i.e. Apple's ARM C++
put the vptr first and gave empty bases (`SingleObject`) no space, exactly
clang's Itanium-style layout. (cfront proper put the vptr last; this compiler
did not.) Base-class offsets are computed with the same rules and checked
against the first field. Bit-field positions follow clang's big-endian
convention (first declared bit = most significant), which the ROM confirms:
`TCardPCMCIA::AddFuncSpecificCIS` copies the flags with masks 0x80000000,
0x40000000, 0x20000000, 0x04000000, 0x02000000, 0x01000000 (`fNoAttrMem`,
`fBadCIS`, `fAttrMemWrable`, `f16BitOnlyCard`, `f8BitOnlyCard`, `fNoCIS`) and
sets 0x00400000, the tenth declared bit, `fFuncSpecificCIS`.

In Ghidra every record becomes a structure (class structures for classes
named by symbols, `/Newton/DDK` for the rest) with `__vptr`, `_base_X`
subobjects and named members; enums and typedefs go to `/Newton/DDK`.
Header declarations are then matched to symbols - by class, name and
parameter types after typedef resolution, so `Boolean` matches the mangled
`Uc` and `RefArg` matches `RC6RefVar` - and a unique match supplies what the
mangled name cannot: the return type and parameter names, with the header's
typedef spellings. C-linkage functions (`extern "C"`) have no mangling, so a
plain symbol whose name is declared in the DDK gets the whole prototype.

## Verification against the ROM (`verify_types.py`)

Constructors compiled by this toolchain are self-allocating and regular:

```
teq  r0,#0            (or: movs r4,r0)
bne  have_object
mov  r0,#<sizeof>     ; the true size of the class
bl   operator new
...
ldr  r1,[literal]     ; the class's vtable
str  r1,[this,#0]
str  ...,[this,#off]  ; member initialisers
```

`verify_types.py` walks every constructor in the program (655 classes) and
checks each DDK class for: allocation size == clang's size; a vtable stored at
offset 0 iff the header says the class is polymorphic; the vtable entries
naming the header's virtual methods in declaration order; and every store to
`this` landing on a declared member (recursing into embedded objects and
bases). Results for the MP2100 D (`build/MP2100D/verify-report.txt`, the ROM this was measured on):
129/131 sizes, 131/132 vptr checks, 9/9 vtables and 131/132 field checks
agree. The three genuine differences, i.e. places where the DDK header is
not the ROM's version of the class:

| Class | ROM | DDK header |
|---|---|---|
| `TCardDevice` | 0x20 bytes, members at 0x1A/0x1B | 0x1C bytes |
| `TObjectIterator` | 0x30 bytes, not polymorphic | 0x20 bytes, virtual methods |
| `TCMOSerialChipSpec` | byte stores into the two `UShort`s at 0x1C | (probably `UChar` pairs) |

`CBufferList`, `CBufferSegment` and `TCardSocket` are declared without
members in the DDK; the ROM gives their sizes (0x20, 0x28, 0x84).

**Vtables.** They are not tables of pointers: each entry is a `B` instruction
to the method (pure virtuals branch to `__pvfn`), entries follow declaration
order with the destructor first and base-class entries first, and all vtables
are packed together (about 0x1B000-0x1F000 in the German ROM). A virtual call is

```
ldr  r12,[this]       ; vtable
mov  lr,pc
add  pc,r12,#slot*4   ; branch into the table
```

Ghidra treats `add pc,...` as a terminal jump, which silently truncated every
function after its first virtual call (18 % of the code region was left
undefined). The importer marks the 2,975 such sites as calls that fall
through (`FlowOverride.CALL`) and keeps disassembling (`fix_virtual_calls`),
so the decompiler goes on after the call as well. (Earlier imports used the
call-and-return override, which kept the disassembly going but made the
decompiler show a `return` right after every virtual call - the loop in
`TTimerQueue::Check`, the dispatch in `TAppWorld::AEDispatch`;
`ghidra_scripts/fix_virtual_calls.py --project build/ghidra --name MP2x00US
--ghidra <dir>` re-marks an existing project's sites in ten seconds, no
re-analysis needed.)  A site already marked a call is not necessarily
followed by code: when the override went in before the code after it
had been disassembled, Ghidra never went on, and the rest of the
function stayed bytes - 60 sites in the MP2x00 US project, three whole
`RealDoCommand`s among them.  `fix_virtual_calls` therefore also
disassembles the fall-through of every marked site whose next address
has neither an instruction nor data, and logs each one ("code opened
after the virtual call at X in F"), which is the list of functions to
read again. (One Ghidra process at a time: the project is locked while a script runs, so
run the analysis tools sequentially; `decompile.py --range START END` does a
whole subsystem in one start.)  `analysis/vtable.py build/MP2x00US 0x20730` lists a
vtable's slots by method name (the address is the literal a constructor
stores at `[this,#0]`), which is how a `add pc,r12,#0x50` is resolved.  The
view classes are built by `BuildView` rather than by a self-allocating
constructor, so their vtables are in no constructor and not in
`romfacts.json`; `analysis/vtable.py build/MP2x00US --find
HandleTap__14TParagraphViewFR6TPoint --slot 0x11c` works back from a
method to the table it sits in - it scans the ROM for every `B` that
lands on the method (either form of an exported one's address: a vtable
branches to the jump table slot) and, given the offset a virtual call
uses, prints the table those branches imply.

Protocol implementations (ProtocolGen output) dispatch through a second kind
of table Ghidra leaves as undefined data: the `TClassInfo` (self-relative
offsets to the names, `B` instructions to the code, then the dispatch table
and the monitor entry).  `analysis/classinfo.py build/MP2x00US --name
TSerialChipVoyager` decodes one - names, version, instance size, every
dispatch slot and monitor selector by method name - from `rom.bin` and
`symbols.json` alone; `--all` lists the 101 implementations in the ROM
(`docs/protocols/README.md` explains the mechanism).

Tables of the ROM go into the source through scripts, never by hand:
`analysis/romtable.py build/MP2x00US NAME[@addr][:type[:count]]... -o
file.cpp` emits data symbols as C++ arrays (the compression coders'
tables; type `cstr` for a table of pointers to C strings, such as the
interpreter's opcode names `gPrintLiterals`, which lives in the
initialised RAM area and is read from the ROM's copy of it; a table the
debug symbols do not name is given its address instead and the name is
ours, as `kResampleFilter@0x0036dbe8:i32:262` for the resampler's sinc,
which is then cited `(unnamed)`), and
`analysis/spellmaps.py build/MP2x00US -o src/recognition` is another
that has to follow pointers: the spelling checker's two character maps
are tables of pointers in the initialised RAM area, each entry a pattern
and a list of the spellings it may stand for, and the list overloads a
pointer with a small integer (the cost of the spellings after it, and 9
to end).  The script follows them and writes the strings out, keeping
that overload explicit.

`analysis/romdicts.py build/MP2x00US -o src/recognition` is the odd one
out: the table it emits is not in the ROM to be read.  The 129 lexicons
built into the ROM are reached through `gROMDictionaryData`, a table of
pointers in RAM that `InitROMDictionaryData` fills in at boot with a
straight line of `ldr`/`str` pairs, one per lexicon, so the table has to
be recovered from the code that writes it.  The script decodes those two
instruction forms out of the ROM's own bytes, keeping a value per
register, and refuses anything else, so a ROM whose function is shaped
differently is noticed rather than half-read.

`analysis/rosci.py build/MP2x00US -o src/recognition/RosCITables.cpp`
emits the handwriting engine's common info, which is a template rather
than a table: `CharInitialize` copies the 0x10c bytes at `rosCI` into a
block of its own so that an area may replace the character set in it.
The template holds ten ROM addresses, so the script writes it out as a
`RosCommonInfo` initialiser with those fields named after the tables it
emits beside them (`rosCharParam0`..`7`, the eight numbers every
character code carries; `rosCharLegalNet`/`rosCharLegalUse`, 256 bits
each; and the six byte-per-character tables the classifier and the
capitals hack use). The struct's field order is in the script as
`FIELDS` and in `recognition/RosEngine.h`, and the two must agree.

`analysis/bigrammar.py build/MP2x00US -o src/recognition/ROMGrammar.cpp
--doc docs/recognition/grammar.md` emits the handwriting engine's
bigram grammar, which is a graph rather than a table: eight
`BiGrammar`s, each a list of `BiGSlice`s (a kind of word), each of
those naming the kinds that may follow it. The script walks it from
`ROMGrammar` and refuses a transition that leaves its own grammar, and
because the ROM has a debug symbol on every object in it - the slices
`BiGS*`, their successor lists `BiSL*` and their weights `BiSP*` - the
generated file carries the ROM's own names throughout. The same walk
writes the grammar out as a document.

`analysis/bpnet.py build/MP2x00US -o src/recognition/BPNetTables.cpp`
emits the classifier net the engine reads characters with, in the same
shape as `rosci.py`: a 0x84-byte template that `BPNetCreateNumOut`
copies, and the eight trained tables it points at, of which `bpWeight`
(91 KB, one byte per connection) is much the largest.

`analysis/disasm.py` grew `--force` for this subsystem. Nothing calls
`BPNetEvaluate`'s body directly, so the auto-analysis leaves it as raw
bytes; `--force` clears the range and has Ghidra disassemble it as ARM
before printing. It changes the project, so give it an exact range.

`analysis/mmumap.py build/MP2x00US --where 0x...` is the one to reach
for when an address in ROM code does not make sense. The ROM is mapped
more than once - cached at 0x00100000 and uncached at 0x03500000 - so a
constant that lands far outside the eight megabytes of ROM is usually a
second mapping rather than a mistake, and the flags on the entry
usually say why that mapping was chosen. It decodes
`g8MegContinuousTableStart` (ROM 0x100), the map the machine starts
from, and `--doc` writes it out as `docs/memory/mmu-map.md`.

`analysis/romconstants.py build/MP2x00US -o src/frames` emits the ROM's
frames constants: the 1765 `RSSYM` symbols (reading each symbol's real
name and hash from the object the constant refers to), the 1102 `R`/`RS`
object constants, and where the ROM's object area and tables are, for the
ROM object importer (`docs/frames/README.md`).  Both write a header naming
the command that made them.  `analysis/nsfunctions.py build/MP2x00US
--list` lists the 1352 functions of the ROM's built-in functions frame
(native or NewtonScript, argument counts), `--natives -o
src/frames/ROMNatives.cpp` emits the tables the host binds its native
implementations through (name, jump-table address, target function and its
symbol: the built-in functions, then every other native function object in
the object area - the prototype frames' methods - named by the frame slot
holding it), and `--disasm NAME` disassembles a NewtonScript function's
bytecode as `TInterpreter::SlowRun` decodes it.  `--object NAME` prints
the slots of a ROM frame or array with each function's kind (`--object
unionsoupprototype`: NAME is the `R...` constant's name, a built-in's name
or the `0x` address of the ref, a magic pointer resolved through
`gROMMagicPointerTable`), and `--disasm object.slot` disassembles a
prototype's method (the slot found through `_proto`/`_parent` as a method
lookup would) - how the ROM's NewtonScript methods are read before being
re-expressed as source (`src/frames/ScriptBuiltins.cpp`).  `coverage.py`
accepts such a re-expression's citation of the function object,
`// ROM 0x006278bd (object) unionSoupPrototype.Add` (a ref into the
object area).
`analysis/natives.py` is the other half of that accounting, and the one
that says what is worth writing next.  A ROM function object holds the
address of a C function, and the host looks that address up in a registry
of implementations bound by the symbol the function had in the ROM; so a
native is *answered* when some `RegisterNativeFunction("FFoo", ...)` names
its symbol, and a script calling one nothing names gets
`kNSErrNativeNotReconstructed`.  The script compares the ROM's own tables
(`src/frames/ROMNatives.cpp`, generated by `nsfunctions.py --natives`)
with every registration in `src/`, and groups what is missing by the area
its machinery would belong to.  `--unbound --ready` narrows that to the
natives whose ROM function is *already* reconstructed - only the
registration is missing - and `--check` reports registrations that bind
nothing, which is otherwise silent.  It reads only the repository, so it
needs no build directory.

`analysis/nsgrammar.py build/MP2x00US -o src/frames --doc
docs/frames/grammar.md` reads the NewtonScript compiler's Berkeley yacc
tables (`yylhs`..`yycheck`), token names (`yyname`), rule texts (`yyrule`)
and the lexer's reserved-word table out of the ROM into `ParserTables.h`
(the token enum and the parser's constants) and `ParserTables.cpp`, and
writes the grammar rule by rule as markdown.  `analysis/packages.py
build/MP2x00US --parts` lists the ten packages built into the ROM extension
(the REx's `pkgl` entry: their directories, parts, flags and infos, the
format described in the script), `--extract DIR` writes each as a `.pkg`
file (as in the ROM, its refs image addresses; `--relocatable` rebases them
to offsets in the package as a loaded package has them, and `--rename
OLD=NEW` also renames one so it installs beside the built-in one:
`newton --package`) and `--doc docs/packages/rex-packages.md` the listing as markdown.

`analysis/recite.py --from build/MP2100D --to build/MP2x00US [--check]`
moves the reconstruction's citations from one ROM image to another, which
is what changing which ROM the reconstruction is of amounts to: the two
images are builds of the same source, so a function keeps its mangled name
and only moves.  It rewrites a citation only when the address it carries
really is that name in the ROM it is moving from, so a citation that does
not check out is reported rather than guessed at; `+0x<offset>` citations
keep their offset, and `(object)` ones are resolved as a path in both
ROMs.  `--old-name "MP2100 D" --new-name "MP2x00 US"` additionally renames
the ROM in each file's opening comment and moves the addresses in it,
reading each as an offset into whatever symbol it falls in, which is how
the end of a range (one past the last function) moves with the range.
What it cannot do is the `(unnamed)` citations - a static function with no
symbol has nothing to look it up by - and the handful of names that are
not in both ROMs; it lists them for a person.

The generated files are regenerated from the new ROM rather than recited:
`analysis/regenerate.py build/MP2x00US` runs all twelve commands that make
them (`--list` prints them, `--only NAME` runs one).  All of them read the
ROM rather than any address written into them, and name it through
`analysis/romid.py`, which finds the ROM's own version string, so they run
against either image unchanged.  `docs/os600/swi-table.md` is the
exception: `swi_table.py` needs a Ghidra project for the ROM, so it is run
by hand after `pipeline.py`.

`verify_types.py` writes what it observed to `romfacts.json`: the allocation
size of every class (655), the vtable address of every polymorphic class
(224), and functions only reachable through vtable slots. `apply_romfacts.py`
(or the next `import_rom.py`) sets the class structure sizes from it, labels
each vtable (`TFoo::vtable`), makes each entry a thunk of its method, and
types `__vptr` as a pointer to a `TFoo_vtbl` structure whose members are
named after the methods, so a virtual call decompiles to a named slot.

## What the Ghidra import does

Given `rom.bin` imported with the Binary loader at 0 (`ARM:BE:32:v4`: the
MP2100's StrongARM SA-110 is ARMv4 without Thumb, running big-endian; compiler
spec `apcs`, the ABI of the ARM SDT toolchain of the period):

1. **Memory map** — `ROM_RO`, `ROM_RWINIT`, `REX` (split from the imported
   block), `RAM_RW` at 0x0C100800 initialised from the ROM copy, `RAM_ZI`, and
   the `JT` block described above.
2. **Classes** — a Ghidra class namespace for each of the ~1,050 C++ classes
   named in symbols. Class structures start empty; filling them in is the
   reverse-engineering work proper.
3. **Functions** — created at every C++ function symbol, every jump-table
   target, and every plain symbol whose first word is an unconditional ARM
   instruction (the rest of the plain names — `SYM*`, `RSS*`, `MP0*`, … — are
   NewtonScript objects and become labels). Names go into their class
   namespace; C++ overloads coexist. Where every parameter type is
   representable, the demangled parameter list is applied with an explicit
   `this` (under APCS `this` is simply r0, so no special calling convention is
   used), completed from the DDK declaration when one matches. Functions
   taking an *unknown* class by value keep their name but not the prototype,
   because a wrong size would shift every following parameter; the full
   signature is in the function comment.
4. **Thunks** — all jump-table slots.
5. **Labels** — everything else, including RAM variables and constants; the ~10
   symbols that fall outside any memory block are skipped and counted.

The importer prints a statistics block at the end (functions created per
rule, signatures applied/skipped and why, …); `check_import.py` reports on
an existing project.

## Provenance of the analysis

The layout, jump-table and demangling facts above were established with
throw-away scripts during the initial investigation; they are preserved in
executable form as the tests and the tools themselves rather than as
scripts, so re-running `tests/test_rom.py` re-derives and re-checks them
against the images.
