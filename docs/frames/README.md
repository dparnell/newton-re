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
at 0x0053eba1: 32768 slots), then in the RAM table (`gSymbolTable`, an
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
  the heap holding the 1765 symbols the C++ refers to and a table over
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

The ROM keeps its frames in one area, `gROMSoupData` (0x003a5da8,
0x292180 bytes: 40597 consecutive objects - the 8634 symbols (`SYM*` in
the debug symbols), the symbol table (the 32768-slot array at 0x0053eba1
that `InitSymbols` names), the built-in functions frame (0x0062418d,
`Rbuiltinfunctions`), the protos, strings, bitmaps and bytecode of the
built-in applications) followed by the `R`/`RS` constants and the `RSSYM`
constants.  `gROMMagicPointerTable` (0x003a5000) is magic pointer table 0:
a count (873) then the refs, which the MMU also maps at 0x01d80000 in the
jump table's diagonal page layout (`ResolveMagicPtr` reads it there).

`ImportROMObjects` reads the area out of a ROM image (`rom.bin` or the AIF
image in `DebugRom/`) into a host area outside the object heap, one host
object per ROM object (ARM layout in, host layout out), then translates
every ref (a pointer into the area becomes the host object's; integers,
immediates and magic pointers are the same on both) and the binary data
the host reads as words - symbol hashes, `'real`s and the UniChars of
`'string`s; other binaries (bitmaps, bytecode, sounds) keep their
persistent big-endian format for their readers.  It then sets the symbol
table (`gROMSymbolTableRef`, which `InitSymbols` takes over), magic
pointer table 0, `gROMBuiltinFunctions`, and every `R`/`RSSYM` constant.
Objects there count as ROM (`InROMObjectArea`): read-only, never moved,
their symbols unique.  `test_ROMImport` runs the object system over the
MP2100 D image in the repository.

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
address, target, argument count, C symbol); `RegisterNativeFunction("FLength",
fn, n)` binds a host implementation to that symbol and `CallCFuncPtr`
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
`DefGlobalVar`, `IsNameRef`, ...) over the MP2100 D image; `test_Frames`
runs the natives without one.

`DEVIATION`s: the value stacks throw `exOutOfStack` when full (the ROM
runs off the end); `FDiv`/`FMod` throw `exDivideByZero` on zero (an ARM
trap in the ROM); `GlobalFunctionLookup` accepts a missing built-in
functions frame.

## Not yet

The interpreter's FastRun1 (the inlined, trace-free copy of SlowRun),
tracing and breakpoints (`TInterpreter::Trace...`, `HandleBreakPoints`),
running 1.x CodeBlocks and binary natives, the natives not bound yet
(134 of the 869 are) (`Sleep`, the strings' `TRichString` functions, printing,
stores, views, ...), `TRichString` (the mungers treat strings as plain
UniChars), the interpreter's `GetTaskStackInfo`; then the object
system's: stores
(`FollowFaultBlock`, `FIsValid`, large binaries, `NoTouchObjectPtr`'s
large-object check), the Unicode encoders (`MakeString` and `Intern` widen
and narrow bytes as they are), `AllocateCObjectBinary`'s procedure table,
the heap dump `Uriah` (the printer), the REx magic pointer tables
(`InitRExMagicPointerTables`), the frames function profiler hooks in
`GC`, and what `InitObjects` starts around the interpreter:
`InitPrinter`, `MakeEntryCache`, the package store part handler.
