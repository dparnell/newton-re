# Frames: the NewtonScript object system

Reverse-engineering notes for the object system the NewtonScript
interpreter, the soups and the views are built on: refs, the object heap
and its collector, symbols, arrays, frames and their maps.  Reconstructed
in `src/frames/` (`Frames.h` for clients, `ObjectHeap.h` for the object
system's own files) with the host test `src/frames/tests/test_Frames.cpp`.
The public interface is the DDK's `objects.h`; the ROM code is at
0x002f420c-0x002fb24c (objects, maps, paths, clones, iteration),
0x002bd4f8-0x002be600 (the collector), 0x0032d4a8-0x0032dd00 (symbols) and
0x00299d30-0x00299eac (classes); every function cites its address.

## Refs

A `Ref` is one ARM word, tagged in its low two bits: 0 an integer (30
bits), 1 a pointer (the object's address plus 1), 2 an immediate (the next
two bits say: 0 special - `NILREF` is 2, the symbol class `kSymbolClass`
0x55552, the function class 0x32, the weak-array marker 0x12, the
fault-block marker 0x22, the declawed ref 0x42 - 1 a character, 2 a
boolean: `TRUEREF` 0x1a), 3 a magic pointer.  On a host a Ref is
pointer-sized (`sync_ddk_headers.py` makes `objects.h`'s `Ref` a `Long`),
so the layouts below are in terms of `sizeof(Ref)`; on a 32-bit host they
are the ROM's numbers.

A magic pointer names a table (value >> 12) and an entry: table 0 is the
ROM's table (at 0x01d80000, in the same diagonal page layout as the jump
table; its entries are refs, resolved again), table 1 has the global
variable frame (1) and the ROM's built-in functions frame (2, the object
at 0x0062418d), even tables 2-8 are the four REx export tables (0x01ee0000
+ n·0x100000) and odd tables 3-9 the RAM tables their imports resolve
into (`InitRExMagicPointerTables`, 0x000d218c, not reconstructed yet).  On
the host they are plain arrays the ROM/REx importer will fill.

## Objects

An object is a block in the object heap: an 8-byte header - size << 8 |
flags, and a GC word whose top byte is the lock count (0xff: never
unlocked) and whose low 24 bits the collector uses - then the class slot,
then the body.  Flags: 1 slotted, 2 frame (with 1; alone: an *indirect
binary*), 4 free block, 8 marked, 0x10 locked, 0x20 forwarding, 0x40
read-only, 0x80 dirty.  Sizes are rounded to 4 (`sizeof(Ref)` here) and
the size field is 24 bits, hence `AllocateBinary`'s limit of 0xfffff3 bytes
and `AllocateArray`'s of 0x3fffc slots.

* A **binary** holds bytes from +0xc; its class slot names its class
  (`'string`, `'real`, ...).  A **symbol** is a binary of class
  `kSymbolClass` holding a 32-bit hash then the C string of its name.
* An **indirect binary** (large binaries, `AllocateCObjectBinary`) holds a
  pointer to a table of eight procedures - length, data pointer, set
  length, clone, delete, set class, mark, update - followed by their data;
  `Length`, `BinaryData`, `SetLength`, `SetClass`, `Clone` and the
  collector go through the table.
* An **array** holds Refs from +0xc; its class slot is its class, or 0x12
  for a weak array (`'_weakarray` to `ClassOf`), whose slots do not keep
  objects alive.
* A **frame** is an array whose class slot holds its **map**: a slotted
  object whose class slot holds flags (4 sorted, 8 shared, 0x10 has
  `_proto`), whose slot 0 is the supermap (or NILREF) and whose remaining
  slots are the tags.  A frame's slot i is named by the i-th tag of the
  map chain counted from the root supermap down (`FindOffset1`,
  0x002fad78; `GetTag` the other way round).  A map with more than 20 tags
  is sorted (`ConvertToSortedMap` permutes the frame's slots with it) and
  searched by bisection in `SymbolCompare` order (hash, then name).
* A **forwarding object** (flag 0x20) is what is left at an object's old
  address when it moved for a resize (`ResizeObject`) or was replaced
  (`ReplaceObject`): its class slot holds the ref to use; `ForwardReference`
  follows chains and shortens them, and the next GC removes them.
* A **fault block** is a frame of class 0x22 standing for a soup entry:
  its slots are the entry's handler, its store, its id and the object once
  read.  `ObjectPtr` goes through it (`FollowFaultBlock`, which reads the
  entry from the store - not reconstructed yet); `NoFaultObjectPtr` gives
  the block itself; `FaultCheckObjectPtr` answers nil for one not yet read,
  which is how `GetFrameSlotRef`/`FrameHasSlotRef` know to lock the slot
  symbol while the read may collect (`SlowGetFrameSlot`).

`ObjectPtr` (0x002f8a50) resolves a ref: a pointer ref below 0x03800000 or
in 0x60000000-0x67ffffff (the ROM and the packages) is used as it is, since
only heap objects forward or fault; otherwise the forwarding chain and the
fault block are followed, with a one-entry cache (`gCacheObjPtrRef`).  The
host's rule is "outside the heap: use as is".  `Length` has a one-entry
cache of its own.

### Symbols and EQ

`Intern` (0x0032d674) hashes the name (the sum of the upper-cased bytes
times 0x9E3779B9, 32 bits; the hash is stored in the symbol, so the host
keeps it 32-bit) and looks in the ROM's symbol table (the read-only array
at 0x00570da1: 32768 slots), then in the RAM table (`gSymbolTable`, an
array of 128 slots at start), else makes the symbol.  Both are open
addressed: slot = hash >> shift, step = (hash & 7)·2 + 1, NILREF empty, an
integer deleted.  The RAM table doubles past 85 % full (`EnlargeSymbolTable`),
is rehashed when deleted slots take it past 85 %, and shrinks to the
smallest power of two holding its symbols at half full when it falls under
a quarter full (`AdjustSymbolTableSize`).  The table is a GC root the
collector treats specially: `GCTWA` (0x0032d870) first drops every RAM
symbol nothing else marked, so the table alone does not keep a symbol.

`EQRef` (0x002f43c8): the same ref, or the same object after forwarding,
or two symbols of the same name - except that two symbols below the ROM's
`RExBlock` (0x006f2e9c, the end of the ROM's symbols) are unique and
compare by identity.  On the host that space is `InitROMSymbols`' (below).

### The RefHandle table and RefVars

A `RefVar` holds a `RefHandle*` into the table at the top of the heap
(`fRefHandleTable`: a slotted object of 0x808 bytes - 256 handles - at
creation, so the collector marks and updates the refs like slots).  Free
handles chain through their ref (the index of the next as an integer, -1
at the end) with stack position -1; an allocated one records
`gCurrentStackPos` (a generation in the high 16 bits, a depth below; the
interpreter increments it per call).  `ClearRefHandles` frees the handles
deeper than the current position in the current generation - what an
exception unwound past; a `RefStruct` records 0 and is never cleared.
When the chain runs out, `AllocateRefHandle` asks for 0x100 bytes more
and collects (`ExpandObjectTable`): `SweepAndCompact` moves the table's
start down into the free space below it and chains the new handles before
the old ones (which do not move, so handles stay valid).

## The collector (`GC.cpp`)

`TObjectHeap::GC` (0x002bdd6c): mark from the RefHandle table, the roots
(`AddGCRoot`: `Ref*` entries in a Handle) and the DIY markers
(`DIYGCRegister`: refCon, mark, update); then `GCTWA` and the symbol table;
`CleanUpWeakChain`; `SweepAndCompact`; `DeclawRefsInRegisteredRanges`;
the GC procs (`GCRegister`).  A GC inside a GC throws.

* **Mark** (0x002bd590) is Deutsch-Schorr-Waite pointer reversal: going
  down, an object's slot being followed holds the ref to its parent and
  its GC word the slot's index; coming back up the slot is restored and
  the next taken.  Indirect binaries get their mark procedure called.  A
  weak array is marked but not followed: it is chained through its class
  slot into `fWeakChain`, and `CleanUpWeakChain` nils its refs to
  unmarked heap objects.
* **SweepAndCompact** (0x002bd868): pass one assigns each marked, unlocked
  object its new address (in its GC word): objects slide down over the
  dead, first-fit into the gaps left below locked objects (32 gaps
  remembered; one that cannot be is noted in the locked object's GC word
  and freed in pass three).  `UpdateRef` (0x002bd78c) then maps every ref
  in every marked object, root and DIY entry: forwarding followed, NILREF
  for an unmarked heap object, the new address unless locked.  Pass three
  moves the objects, calls the delete procedure of dead indirect binaries
  and rebuilds the free blocks; the rover restarts at the first gap.
* **Declawing**: when a package goes, `RegisterRangeForDeclawing` records
  its address range and the next GC replaces every ref into it by 0x42
  (`kDeclawedRef`), which `ObjectPtr` reports as `kNSErrBadPackageRef`.

## Errors

The object system throws frames: `ThrowRefException` (0x002cfebc) throws a
`RefStruct*` to `{errorCode, value}` under `evt.ex.fr;type.ref.frame`
(`ThrowExFramesWithBadValue`), `evt.ex.fr.type;type.ref.frame`
(`ThrowBadTypeWithFrameData`) or `evt.ex.fr.intrp;type.ref.frame`
(`ThrowExInterpreterWithSymbol`), with `DeleteRefStruct` as the destructor;
out of bounds adds an `index` slot.  Out of memory and the declawed ref
throw the bare error under `evt.ex.fr` / `evt.ex.outofmem`.  The codes are
`NSErrors.h` (ERRBASE_FRAMES -48000): the DDK has no header for them, so
the names there are ours, from what each is thrown for (-48200 object
pointer of non-pointer, -48205 out of bounds, -48209 GC during GC, -48214
read-only, -48216 out of object memory, -48220 could not resize a locked
object, -48221 bad package ref; -48400 not a frame, -48401 not an array,
-48410 not a symbol, ...).

## Host re-expression

* `ObjHeader.h`/`ObjectHeap.h`: both header words are pointer-sized (the
  GC word holds an address during compaction) and slots are Refs; the
  ROM's offsets 0x8/0xc and 4-byte rounding are `sizeof(ObjHeader)`,
  `kObjBodySize` and `kObjAlign`.  Persistent forms (packages, stores,
  the ROM's object graph) keep the ARM layout and will be imported.
* The ROM's own objects come from a ROM image (below); without one,
  `InitROMSymbols` (Symbols.cpp) builds a read-only symbol space outside
  the heap holding the 1768 symbols the C++ refers to and a table over
  them as `gROMSymbolTable`.  The constants the C++ names - the ROM's
  `RSSYM` symbol RefStructs and its `R`/`RS` object constants - are `Ref`s
  here (`RSSymbols.h`, `ROMConstants.h`, generated with their ROM values by
  `tools/newton-rom/analysis/romconstants.py`).
* `OnStack` uses hal's `GetStackBounds`; `TObjectIterator` registers an
  exception cleanup (the `ExceptionCleanup` the DDK header lacks, added by
  the sync) when it is on the stack, so a Throw frees its handles.
* `TFramesObjectPtr`/`TBinaryDataPtr` are the DDK's names for the ROM's
  `TObjectPtr`/`DataPtr`.
* `gObjectHeapSize` (1 MB) sizes the heap; the ROM asks `InternalRAMInfo`.

`DEVIATION`s: the RefHandle table's growth when the space left is under one
handle (the ROM would write the chain's end over the table's header), and
the free-handle index after a failed growth (the ROM leaves an index below
the table).

## The ROM's objects (`ROMImport.cpp`)

The ROM keeps its frames in one area, `gROMSoupData` (0x003afda8,
0x2cfc98 bytes: 46538 consecutive objects - the 8623 symbols (`SYM*` in
the debug symbols), the symbol table (the 32768-slot array at 0x00570da1
that `InitSymbols` names), the built-in functions frame (0x00639581,
`Rbuiltinfunctions`), the protos, strings, bitmaps and bytecode of the
built-in applications) followed by the `R`/`RS` constants and the `RSSYM`
constants.  `gROMMagicPointerTable` (0x003af000) is magic pointer table 0:
a count (873) then the refs, which the MMU also maps at 0x01d80000 in the
jump table's diagonal page layout (`ResolveMagicPtr` reads it there).

`ImportROMObjects` reads the area out of a ROM image (`rom.bin` or the AIF
image in `DebugRom/`) with the object area importer
(`ObjectAreaImport.h`, `TImportedObjectArea::Import`): into a host area
outside the object heap, one host object per ROM object (ARM layout in,
host layout out), then every ref translated (a pointer into the area
becomes the host object's; a pointer elsewhere goes to the caller's
translator - none for the ROM; integers, immediates and magic pointers
are the same on both) and the binary data the host reads as words -
symbol hashes, `'real`s and the UniChars of `'string`s; other binaries
(bitmaps, bytecode, sounds) keep their persistent big-endian format for
their readers.  It then sets the symbol table (`gROMSymbolTableRef`,
which `InitSymbols` takes over), magic pointer table 0,
`gROMBuiltinFunctions`, and every `R`/`RSSYM` constant.  Objects there
count as ROM (`InROMObjectArea`): read-only, never moved, their symbols
unique.  `test_ROMImport` runs the object system over the ROM image
in the repository.

## Frames parts (`FramesPart.cpp`)

A package part of kind `kFrames` (`docs/packages/README.md`) is an object
area in the same layout whose first object is an array holding the
part's top-level frame (`FramePartToplevelFrame`, 0x000d2898: NTK's
partFrame - `{installScript, removeScript, partData, _ImportTable,
_ExportTable}` for an `'auto` part, `{app, text, icon, theForm, ...}` for a
`'form` part), its pointer refs the addresses the objects have once the
package is loaded (a package built into the ROM extension is linked at
its ROM address; NTK's are relative to the package's start, relocated at
load).  The MessagePad uses the part where it lies; the host imports it
first (`ImportFramesPart(part, size, refBase)`, the same importer, refs
to ROM objects translated to the imported ROM's, the part's own symbols
kept - `EQRef` and the slot lookups compare symbols by hash and name) and
`RemoveFramesPart` declaws the heap's refs to it
(`RegisterRangeForDeclawing`, `kDeclawedRef`) as the ROM does for a
removed package.  `test_FramesPart` imports the Names application's part
from the ROM extension and runs NewtonScript over its frame.

## The interpreter (`Interpreter.cpp`, `VariableLookup.cpp`)

`TInterpreter` (0x80 bytes; `Interpreter.h` lists the fields) runs
NewtonScript bytecode over two stacks of refs: the value stack (operands,
and a function's arguments and locals) and the control stack, whose
entries are pointer-stack RefHandles grouped six at a time into a
`VMState` - pc, function, locals, implementor, receiver and the
stack-frame word `MAKEINT(base << 6 | flags)` (flag 1: locals on the
value stack from index `base`, 2: entered by a send).  `fExceptionContext`
chains the handler records (`new-handlers`); `fLiterals`, `fInstructions`
and `fPC` cache the running function.  As in the ROM, `Run` loops
`AlternatingLoops` (FastRun/SlowRun; FastRun runs SlowRun here, the ROM's
FastRun1 is its inlined copy for functions without tracing) until the
control stack is back at the depth the call was made at.

Bytecodes (as `SlowRun` 0x002cc66c decodes them; `Interpreter.h`
`kBC...`): an instruction byte is `a << 3 | b`, and `b == 7` means a
16-bit big-endian operand follows.  `a == 0`: pop, dup, return, push-self,
set-lex-scope, iter-next, iter-done, pop-handlers.  Then push (literal),
push-constant (the operand is the ref, 16-bit signed), call (the global
function named by the symbol on the stack), invoke (the function on the
stack), send, send-if-defined, resend, resend-if-defined, branch,
branch-if-true, branch-if-false, find-var, get-var (a local: 0-2 are
hidden, arguments from 3), make-frame (its map on the stack), make-array
(its class on the stack; b == 0xffff: the length too), get-path, set-path,
set-var, find-and-set-var, incr-var, branch-if-loop-not-done (incr, limit,
index), freq-func (0-6 inline: `+ - aref setAref = not <>`; the rest of
the 25 through `gFreqFuncs`, the function objects named in
`gFreqFuncInfo`) and new-handlers (b pairs of symbol and pc on the
stack).  Operand order on the stack is `args... name` for call, `args...
receiver name` for send and `args... name` for resend
(`tools/newton-rom/analysis/nsfunctions.py --disasm` prints the ROM's
functions this way).

Function objects: a NewtonScript function is `[class 0x32, instructions,
literals, argFrame, numArgs | numLocals << 16]`; its arguments stay on the
value stack and the locals are pushed after them, and its `argFrame`
(`{_nextArgFrame, _parent, _implementor, captured variables...}`) is
cloned on entry when there is one - `set-lex-scope` makes a closure by
cloning the function and pointing the clone's argFrame at the running
function's, so `find-var` walks `_nextArgFrame` chains before the
receiver's `_proto` and `_parent` chains.  A native function is `[class
0x132, funcPtr, numArgs(, docString)]`.  1.x CodeBlocks (`'CodeBlock`
frames, everything in the cloned argFrame) and binary natives (0x232 /
`'binCFunction`, ARM code in a binary) are recognised but not run.

Variable lookup (`VariableLookup.cpp`) is the ROM's: `XGetVariable`
(locals, then the receiver's `_proto` chain, then each `_parent`'s),
`XFindImplementor`/`XFindProtoImplementor` (sends and resends),
`SetVariableOrGlobal`, all through the four `TICache`s (`gGetVarCache`,
`gFindImpCache`, `gProtoCache` and `gROProtoCache` for frames in the ROM
area), which `SetFrameSlot`/`AddSlot`/`RemoveSlot` invalidate
(`ICacheClear...`).

Exceptions: a NewtonScript `try` pushes a handler record (an array: next,
value depth, control depth, function, receiver, implementor, the
(symbol, pc) pairs, the current exception, locals); `Run` catches every
`Throw`, unwinds the stacks to the innermost record whose symbol
`Subexception` matches, translates the exception into the frame
`CurrentException()` returns (`{name, error|data|message}`) and resumes
at the handler's pc; nothing matching resets the stacks and rethrows to
C++.  Calls from C++ (`NSCall...`, `NSSend...`, `NSCallGlobalFn...`,
`DoCall`/`DoSend`/`DoBlock`/`DoScript`, `DoMessage...`) push the
arguments, `Call`/`Send`, `Run` and pop the result; an exception out of
them unwinds the control states and, as in the ROM, leaves the arguments
on the value stack.

### Natives on the host (`NativeFunctions.cpp`, `Builtins.cpp`, `Munger.cpp`)

The ROM's native function objects hold jump-table addresses of its C
functions (`FLength`, `FAdd`, ...).  `nsfunctions.py --natives` lists the
869 of the built-in functions frame in `ROMNatives.cpp` (name, jump-table
address, target, argument count, C symbol) and, in a second table, the 457
other native function objects of the ROM's object area - the methods of
the store, soup, cursor and entry prototype frames (`storePrototype`'s
`GetName` -> `StoreGetName`, `plainSoupPrototype`'s `Add` ->
`PlainSoupAdd`, ...), each named by the slot that holds it;
`RegisterNativeFunction("FLength", fn, n)` binds a host implementation to
that symbol in either table and `CallCFuncPtr`
resolves a funcPtr below `kROMCodeLimit` (0x02000000) through the
bindings - an unbound one throws `kNSErrNativeNotReconstructed` (-48899).
A funcPtr above the limit is a host function pointer (`MakeCFunction`).
`InitInterpreter` (which `InitObjects` calls, as in the ROM) binds the
reconstructed built-ins first (`RegisterBuiltinNatives`: arithmetic,
comparison and bit operations, objects, slots and paths, arrays and
strings' mungers, variables, apply/perform, exceptions, the foreach
iterator, symbols) and, when no ROM image is imported, gives each a
function object in `gFunctionFrame` (`InstallHostNatives`) so that the
frequently called functions exist.  `test_Interpreter` runs assembled
bytecode and the ROM's own NewtonScript functions (`GetGlobalVar`,
`DefGlobalVar`, `IsNameRef`, ...) over the ROM image; `test_Frames`
runs the natives without one.

Some of the ROM's built-ins are NewtonScript, not native (`nsfunctions.py
--list` marks them `script`: `GlobalFnExists`, `GetGlobalFn`, the union
soup registry's `RegUnionSoup`, ...), as are some methods of its
prototype frames (`unionSoupPrototype`'s `Add`, `GetMember`, ...).  These
are re-expressed as NewtonScript source read from their bytecode
(`--disasm NAME`, `--disasm object.slot`), each citing the function
object (`// ROM 0x005d206d (object) GlobalFnExists`), and compiled on a
host without the ROM's objects: `ScriptBuiltins.cpp` has the
interpreter's own (`InstallHostScriptBuiltins`, after
`InstallHostNatives`) and `InstallScriptFunctions(table)` /
`CompileScriptFunction(source)` for the other areas' tables
(`stores/UnionSoups.cpp`).

`DEVIATION`s: the value stacks throw `exOutOfStack` when full (the ROM
runs off the end); `FDiv`/`FMod` throw `exDivideByZero` on zero (an ARM
trap in the ROM); `GlobalFunctionLookup` accepts a missing built-in
functions frame.

### Strings and arrays (`RichString.cpp`, `StringNatives.cpp`, `ArrayNatives.cpp`)

`TRichString` (the ROM's, 0x28 bytes; no DDK header) is the view the string
functions work through: a string object (locked while its text is in
use) or a C UniChar string, its length in characters and its format - a
rich string keeps ink words after the text and ends in a trailer word
(`text length << 4 | 1`, the low two bits of the last UniChar say the
format); `MungeRange` replaces a range of characters from another
TRichString, growing or shrinking the object (the ink is NOT YET
RECONSTRUCTED: a munged rich string comes out plain), and
`CompareSubStringCommon` compares a range with `CompareUnicodeText` (the
collation tables below; NOT YET: `CompareInkProc` 0x001ade0c, so ink
collates as the character standing for it).
`StringNatives.cpp` has the string functions over it - `StrLen`,
`StrConcat`, `SubStr`, `StrEqual`/`StrExactCompare`/`StrCompare`,
`BeginsWith`/`EndsWith`, `Upcase`/`Downcase`/`Capitalize`/`CapitalizeWords`
(over `UToUpper`/`UToLower`; the ROM's case tables are not here),
`TrimString`, `CharPos`/`StrPos`/`StrReplace`, `GetChar`/`SetChar`,
`FindStringInArray`, `FindStringInFrame` (each string looked for at the
start of words in a frame's strings to ten levels, `true`/`nil` or an array
of `[string, path, position]`), `NumberStr`/`StringToNumber`/
`FormattedNumberStr` (with `strtod` and `snprintf`: `TNumberParser` and
the locale's number format are not here) and `ParamStr` (`^0`..`^9`
substituted in three passes then `^^`/`^|` stripped; `^?N<yes>|<no>|`
by whether parameter N is present).

`ArrayNatives.cpp` has `TGeneralizedTestFnVar`, the comparison object
behind the sorts, searches and ordered set operations: a test symbol
(`'|<|` `'|>|` numbers, `'|str<|` `'|str>|` strings, `'|chr<|` `'|chr>|`
characters, `'|sym<|` `'|sym>|` symbols; in a search also `'|=|` and
`'|str=|`) or a two-argument function answering an integer, and a key
(nil, a slot symbol, path or index taken with `GetFramePath`, or a
function of the element).  `Sort`/`QuickSort` is the ROM's iterative
median-of-three quicksort finished by insertion (`QSUtil`), `ShellSort`
and `InsertionSort` share `ShellSortUtil`, `StableSort` is `MergeSort`
(blocks merged through a temporary array as large as half the array or
as memory allows); `LSearch`/`LFetch` scan from an index, `BSearchLeft`/
`BSearchRight` bisect a sorted array to the first not-less or last
not-greater element, and `BFind`/`BFetch`/`BInsert`/`BDelete` (and their
`Right` forms) build on them; `BMerge`/`BIntersect`/`BDifference` walk two
sorted arrays through `GenOrderedSetOp` with an action function per
comparison (advance/copy/skip-duplicates bits), `SetUnion`/`SetDifference`/
`SetOverlaps` are the unordered ones.  The binary accessors
`ExtractByte`/`Word`/`Long`/`XLong`/`Char`/`UniChar`/`CString`/`PString`/
`Bytes` and their `Stuff...` partners read and write big-endian data
through `BoundsCheck`/`BoundsWriteCheck` (`kNSErrBadArgs` past the end,
`kNSErrObjectReadOnly` for a read-only object; `StuffLong` checks only the
bounds, as the ROM does).  `Builtins.cpp` gained the unordered
comparisons (`UnorderedOrGreater`, ..., `LessEqualOrGreater`: the IEEE
relation with NaN unordered), `forLoop` and `getSiblingSlot`/
`hasSiblingSlot`, the rest of the real functions (`acosh` ... `fdim`,
`compound`/`annuity`, `remquo` and `randomx` answering pairs), the
floating-point environment natives over `<fenv.h>` (DEVIATION: the host's
flag and rounding values), `Random`/`GetRandomState`/`SetRandomState`
(over a host generator whose one-word state round-trips; the ROM's C
library `rand` is not here) and `GetFunctionArgCount`.  `test_Strings`
runs them all from NewtonScript source.

## The printer and the REP (`Printer.cpp`, `REPTranslators.cpp`)

The read-eval-print loop reads forms through a `PInTranslator` and prints
through a `POutTranslator` - protocols (no DDK header has them; the
interfaces come from the class-info tables, `classinfo.py --name
PStdioOutTranslator`) whose implementations the ROM chooses at boot: the
null ones (nothing read, nothing printed), the stdio ones (the debugger's
console), Hammer's, the serial debugger's and the NTK nub's.  `gREPout`
is the out translator everything prints through; its `Print` is printf
with the ROM's `%U` (a UniChar string; `REPFormat` is the host's
version), and `PrintObject(obj, indent)` is its `ConsumeFrame`.

`PrintObjectAux` prints an object as NewtonScript source: integers,
`NIL`/`TRUE`, characters (`$a`, `$\0A`, `$\u263A`), symbols (`|quoted|`
unless an identifier), strings (quoted, 250 characters at a time), reals
(`%#g`), other binaries (`<class, length n>`), path expressions
(`a.b`), arrays (`[class: a, b]`) and frames (`{tag: value}`), functions
as `<function, n arg(s) #addr>`.  `printDepth` (default 3) bounds the
nesting (deeper aggregates print as `[#addr]`/`{#addr}`, `{@n}` for a
magic pointer), `printLength` the elements (then `...`), `prettyPrint`
puts each element on a line of its own when one is an aggregate,
`printInstructions` disassembles `'instructions` binaries in place;
`gPrintPrecedents` (16 by depth) turns a cycle into `<depth>`.
`Disassemble` lists a function's bytecode one instruction per line,
naming the literal, constant, local (from the argFrame of a 1.x code
block or the `'dbg1` debug information of a 2.x function) or frequent
function an operand refers to; the opcode names are the ROM's
`gPrintLiterals` (`PrintLiterals.cpp`, generated by `romtable.py
gPrintLiterals:cstr:35`, a table in the initialised RAM area).

`StringObject`/`SPrintObject` make the text of a string, number,
character or symbol (`IntegerString`, `NumberString` - the international
munging NOT YET), and `REPExceptionNotify` reports an exception the REP's
way (`!!! Exception: name {frame}`, with the file and line a compiled form
carries).  Host: `InitObjects` calls `InitPrinter`, which installs a null
out translator until `HostInitREP(FILE*)` starts the REP on a stdio
stream (what `TNewtWorld::MainConstructor` does after `InitObjects`);
the stdio translator writes the Newton's carriage returns as newlines.
`test_Printer` checks the output against the ROM's formats.  The UniChar
string functions and the conversions to and from 8-bit text are
`utility/Unicode.h` (`Ustrlen` and friends; `ConvertToUnicode`/
`ConvertFromUnicode`).

### The collation tables (`SortTables.h`)

Which of two pieces of text comes first is decided by a *sorting table*, a
binary object holding, for every character it knows, a four-byte
**projection entry**: the primary weight the character sorts as, and the
second-order weight that separates the characters sharing it.  Small
letters project to their capitals - `a` and `A` both project to `A`, with
second orders 7 and 0, and `a-grave` joins them with 9 - so a plain
compare folds case and diacriticals and an exact one does not.

A table is laid out as a 0x44-byte header and then its data:

| offset | |
| --- | --- |
| +0x00 | the table's id (a short) |
| +0x06 | how many ranges |
| +0x08 | up to six ranges, `{first, last}` halfwords |
| +0x20 | how many single characters |
| +0x24 | where the ligatures are, from +0x44, and how many |
| +0x28 | where the lowest-sort table is, from +0x44, and how many |
| +0x44 | each range's projection entries, then the singles, the ligatures, the lowest-sort halfwords |

`GetProjectionEntry` 0x00256270 indexes the ranges - below 0x80 it goes
straight to the first one, which is why every table starts with the ASCII
block - and then binary searches the single characters, six bytes each
`{character, primary, secondOrder}`; a character in neither is unknown and
answers nil.  A primary of 0xffff means the character is really two:
`GetLigatureEntry` 0x0025635c walks the ligature table (eight bytes,
`{character, first, second, lowest}`, not counted - a character that is not
there runs off the end) so that `ae` sorts as `AE` and a sharp s as `ss`.
A primary of 0 means the character is ignored altogether.
`ConvertTextToLowestSort` 0x00256384 replaces each character by the least
one sharing its primary weight, which is what a soup index stores so that
its keys compare with a memcmp, and `CalcSize` 0x00256428 works the binary's
length out from the header (1484 bytes for the ROM's own table, which sits
in a 1496-byte binary).

The tables are a persistent format - they come out of a ROM object or off a
store - so every halfword in one is big-endian whatever the host is.

`TSortTables` (`gSortTables`, 0x0c1048c8) holds five of them and remembers
which is the default: `AddSortTable` 0x0025659c registers one in the first
free slot with one user (and throws when all five are taken),
`Subscribe`/`Unsubscribe` 0x00256630/0x00256654 count the indexes wanting
it and dispose of it when the last has gone, and `SetDefaultTableId`
0x00256698 chooses the default, refusing an id that names no table (0,
meaning none at all, is always taken).  The ROM's one table, id 1, is the
`sortTables` array of the `unicode` frame, registered by `InitUnicode`;
`GetSortID`/`SetSortID` (`FGetSortID` 0x002566e0, `FSetSortID` 0x00256710)
are the script's way at the default, and the boot's `bootInitNSGlobals`
calls `SetSortID` while it is setting its globals up.

`CompareUnicodeText` 0x00255d6c is the comparison everything that orders
text goes through.  It walks both strings through a `TStringToSort`
(0x18 bytes: the table, the text left, the current character, a ligature's
second character waiting for the next `Fetch`, and the character the two
first differed at) and answers the *sign* of the first difference in the
primary weights, not its size.  A character that projects to nothing is
skipped and the other string's kept for the next turn.  If the primaries
never differ, an exact compare then asks `CalcSecondOrderResult`
0x00255fd0 for the second-order weights of the characters they first
differed at, and failing those the string that had a ligature in it comes
second.  Two quirks are worth knowing: the leftover of a longer string is
only examined for ignorable characters once the two have differed
somewhere, so `"a­"` sorts after `"a"` although the soft hyphen is
ignored; and the table argument 1 is not a table but "the default one".

With no table at all - which is how the system starts, and how an index
with no `sortId` compares - `OldCompareText` 0x00255bf8 answers instead:
character by character, each taken to Mac Roman and folded through
`charClass` and `upperNoMarkList` unless the compare is exact, and the
folded characters compared *as bytes* - so two characters with no Mac
Roman form compare on their low bytes alone.  `CompareStringNoCase`
0x00255750 and `CompareTextNoCase` 0x002557a4 are the wrappers over the
default table.

`test_SortTables` reads the ROM's table, checks its shape and its
projections, collates through it and drives the registry.


### The character tables (`UnicodeTables.h`, `utility/Unicode.h`)

`InitUnicode` 0x00254b80 (the ROM: `TNewtWorld::MainConstructor` after
`InitObjects`; the host: at the end of `InitObjects`, when the ROM's
objects are imported - `Runicode`, the magic pointer @283, is the ROM's
`unicode` frame) installs the character tables:

- `charEncodings`: frames `{encodingID, mapFromUnicode, mapToUnicode}`
  for the encodings 1 (Mac Roman), 2, 3 and 4.  `GetMappingInfo`
  0x00256014 reads a mapping binary's header - halfword 0 the kind, 2
  the size (256), 4 flags, 6 the segment count - into a `TEncodingMap`
  (0x20 bytes) and picks the converter: kind 0 "contiguous 8"
  (`ConvertToUnicodeFunc_Contiguous8` 0x00256548: 256 big-endian UniChars
  indexed by the byte) or kind 4 "segmented 16"
  (`ConvertFromUnicodeFunc_Segmented16` 0x00256784: the segments' ends,
  starts and offsets - `count` halfwords each - then the byte table; a
  character's segment is the first whose end is not below it, a
  character below its start has no byte, 0x1a, else the byte at the
  character plus the offset).  `InstallCharEncoding` 0x002555ec puts the
  maps and converters in `gUnicode` (0x0c104858, five entries of
  {fromMap, fromFn, toMap, toFn}); `ConvertToUnicode` 0x002553a0 and
  `ConvertFromUnicode` 0x002568b8 go through them once `gUnicodeInited`
  is set (an encoding without a table converts nothing - so encoding 0),
  and before that widen bytes as they are / narrow characters over 0x7f
  to 0x1a.  Host: the binaries are copied out of the object heap (the
  ROM points into its own objects, which never move).
- `charClass` (a class per Mac Roman character), `typelist`, and the
  per-class deltas `upperList`, `lowerList`, `upperNoMarkList`,
  `noMarkList` (70 classes): `ConvertTextCase` 0x002557ec takes each
  character to Mac Roman (`A_CONST_CHAR` over 0x7f; 0x1a - no such
  character - is left), adds its class's delta and takes the result back
  through `U_CONST_CHAR`; `UppercaseText` 0x0025587c, `LowercaseText`
  0x0025588c, `NoDiacriticsText` 0x0025589c, `UppercaseNoDiacriticsText`
  0x002559f4 are its uses, `ToggleCase` 0x00255a04, `UToLower` 0x00255a54
  and `IsAlphabet` 0x00255428 (uppercased without diacriticals it is A-Z
  or the sharp s) are built on them.  Before `InitUnicode` the host's
  versions know Latin-1's letters (the ROM would read through null
  pointers).
- `Rasciibreak` (the magic pointer @6): `gASCIIBreakTable`, a byte per
  Mac Roman character - `IsDelimiter` 0x00255678 - the word breakers of
  `StrCapitalizeWords` and `FindWordsInString` (space and the punctuation
  below `0`, `:`-`@`, and some of the high characters; not `_`).

NOT YET: the sort tables (`TSortTables`, `gSortTables`).  `test_Strings`
converts Mac Roman each way, cases and un-accents text, and asks the
break table.

### The debugger's view of the stack (`DebugAPI.cpp`)

`TNSDebugAPI` (one word: the interpreter) reads and writes the call stack
through the six-ref `VMState`s on the control stack: `NumStackFrames`
(`(depth - 1) / 6`), `Function`/`PC`/`Receiver`/`Implementor` of call *i*
(0 the outermost; a native call's pc is -1), `Locals` and `GetVar`/`SetVar`
(a 2.x function's arguments and locals are on the value stack from the
frame's base + 3, a native's arguments from the base, a CodeBlock's in its
argFrame after the three hidden slots), `FindVar`/`SetFindVar` by name
through the argFrame, `StackStart`/`NumTemps`/`TempValue` for the
temporaries above the variables (`FunctionStackSize`: how many slots the
variables take, -1 for a CodeBlock).  On it: `REPStackTrace` (what the
REP prints for `StackTrace()` - each call's function by its slot in the
implementor, its well-known name or its address, the pc or `[native]`, the
receiver and the variables with arguments marked, at the globals'
`stackTracePrintDepth`, 0 by default, with a warning unless `SetDebugMode`
has made the stack accurate), `NTKStackFrameInfo` (the NTK's `{codeBlock,
programCounter, receiver, implementor}` from the `stackFrameInfo`
prototype; names from `'DebuggerInfo`/`'debug` slots, `GetNameFromDebugHash`
asking the global `DebugHashToName` when defined), `SearchForObjectName`/
`CheckForObjectName` (`"vars"`, `"vars.foo"`, `"functions.bar"`, the
built-in functions) and `PrintWellKnownObject`; the natives `StackTrace`,
`SetDebugMode`, `BreakLoop` (a nested REP in the receiver's context, run by
`REPBreakLoop`/`BreakLoop` until `ExitBreakLoop` sets the done flag),
`Write`, `Load` (`ParseFile`) and `stats`.  `TInterpreter::GetLocalFromStack`,
`SetLocalOnStack` and `GetSelfFromStack` go through it.  NOT YET
RECONSTRUCTED: `TNSDebugAPI::Return` (unwinding to a call), `NTKStackTrace`,
`Uriah` (the heap dump).  `test_Printer` inspects the stack from a native
and checks the trace.

## The compiler (`Compiler.cpp`, `Parser.cpp`, `Lexer.cpp`)

`TCompiler` turns NewtonScript source into a function object: `ParseString`
compiles a string's forms into one function of no arguments, `ParseFile`
compiles and runs a file form by form (the NTK's way of loading a text
file), `FCompile` is the `Compile` native.  The lexer (`TCompiler::GetToken`,
`yylex0`) reads UniChars from a `TInputStream` (a string or a stdio file;
the file's line feeds become the Newton's carriage returns) and answers
the tokens `ParserTables.h` names, with `NIL`/`TRUE` constants, the
reserved words (the ROM's table, `gReservedWords`), `|symbols|`, numbers
(0x hex, reals with fraction or exponent), `"strings"` with `\n \t \\`
and the `\u` hex mode, `$chars`, `@n` magic pointers and `#line`
directives; a `;` before `end`, `else`, `)`, `]`, `}`, `,`, `until` or
`onexception` and a `,` before `]` or `}` are dropped by a one-token
lookahead.

The parser is the ROM's Berkeley yacc parser: its tables (`yylhs` ...
`yycheck`), token names and rule texts are read out of the ROM by
`tools/newton-rom/analysis/nsgrammar.py` into `ParserTables.cpp` (and the
grammar into `grammar.md`), and `TCompiler::Parser` is byacc's skeleton
with the grammar's 151 actions building a parse tree of arrays
`[MAKEINT(kind), children...]` whose kind is the construct's token
(`'+'`, `tokenIF`, ...).  The value stack is an object (`'yaccStack`,
locked), so the tree survives collections; in interactive mode the parser
returns after each command so the REP can run one at a time.

The code generator walks the tree three times.  Declarations
(`DeclarationWalker`) collect each function's locals, constants and loop
variables (`i|limit`, `i|incr`; `v|iter`, and for `collect` `v|index`,
`v|result`) and give each nested `func` a `TFunctionState` (kept in the
node's slot 5).  Closures (`ClosureWalker`, `ComputeArgFrame`) decide where
variables live: with `compilerCompatibility` 1 (the default) arguments and
locals go on the value stack (`fVarLocs`: name to index) and only the
variables an inner function reaches into the argFrame (`'closed`), which
is left out altogether when nothing is closed over and `self`/`inherited`
are unused; with 0 every variable goes into the argFrame and the result
is a 1.x `'CodeBlock`.  Code (`WalkForCode`) emits the bytecodes through
`TFunctionState::Emit` (`push`/`push-constant` for literals - an
immediate that fits 16 bits or a magic pointer under 0x1000 is a
constant - `get-var`/`set-var` for stack variables, `find-var`/
`set-find-var` for the rest, `freq-func` for the 25 frequent functions
with the right argument count, `call`/`invoke`/`send`/`resend`, the
loops with `branch-if-loop-not-done` and `incr-var` (stack: incr, index,
limit), `new-handlers`/`pop-handlers` for `try`, `make-frame`/`make-array`,
`set-lex-scope` after a nested function with an argFrame) and warns about
statements without effect and `=` where `:=` was meant.  `MakeCodeBlock`
clones the code block prototype (`CodeBlock::fgPrototype`, the debug one
when names are kept) and fills instructions, literals, argFrame, `numArgs
| numLocals << 16` and the `'dbg1` variable names.  `TCompiler::Error`
throws `evt.ex.fr.comp;type.ref.frame` with `{errorCode, value, filename,
linenumber}` (`NSErrors.h` -48601..-48628).

The REP's input side is `PStdioInTranslator` (a line at a time, compiled
by `ParseString`) and `REPAcceptLine`; `host/newtonscript.cpp` builds the
`newtonscript` program: the object system over the ROM image, files
loaded with `ParseFile`, `-e` for an expression, stdin as the REP, and
the host function `ROMConstant("name")` giving a ROM R constant by name
(`ROMConstant("canonicalTextShape")`; the magic pointers print as `@n`,
so `@547` is the ROM's globals template with `fonts`, `international`,
the registries).
`test_Compiler` compiles and runs source for every construct, both
function kinds, the errors, `ParseFile` and the `Compile` native.

## Not yet

The interpreter's FastRun1 (the inlined, trace-free copy of SlowRun),
tracing and breakpoints (`TInterpreter::Trace...`, `HandleBreakPoints`),
running 1.x CodeBlocks and binary natives, the natives not bound yet
(261 of the 869 are) (`Sleep`, printing, stores, views, ...),
`TRichString`'s ink (`MakeRichString`, `StripInk`, the ink words in
`MungeRange`; the mungers treat strings as plain UniChars), the Unicode
case, break and sort tables (`UppercaseText`, `IsDelimiter`,
`CompareUnicodeText`), `TNumberParser` and the number formats,
the interpreter's `GetTaskStackInfo`, `TNSDebugAPI::Return`, `NTKStackTrace`
and `Uriah`, the Hammer, serial and NTK translators, the
compiler's rich-string ink in `Stringer` and the encoding of source
text (`IsFirstByteOf2Byte`), `TCompiler::Simplify` (nothing in this ROM);
then the object system's: stores
(`FollowFaultBlock`, `FIsValid`, large binaries, `NoTouchObjectPtr`'s
large-object check), the Unicode encoders (`MakeString` and `Intern` widen
and narrow bytes as they are), `AllocateCObjectBinary`'s procedure table,
the heap dump `Uriah` (the printer), the REx magic pointer tables
(`InitRExMagicPointerTables`), the frames function profiler hooks in
`GC`, and what `InitObjects` starts around the interpreter:
`InitPrinter`, `MakeEntryCache`, the package store part handler.
