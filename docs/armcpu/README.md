# The ARM interpreter (`src/armcpu/`)

A fallback for ARM code in third-party packages that the reconstruction has
no host re-expression of: NTK's *native-compiled* NewtonScript functions
first, protocol parts later.  (The Newton Internet Enabler's native code is
re-expressed by hand instead - `src/comms`.)

## Status (2026-09-29)

| Piece | Where | State |
|---|---|---|
| The CPU: ARMv4, ARM state, the StrongARM's instruction set | `src/armcpu/ARMCPU.h` | done, `armcpu.ARMCPU` |
| The public jump table's names | `tools/newton-rom/analysis/gluetable.py` | done |
| The adapter: a package's native function run through `SetPackageNativeFallback` | `src/armcpu/PackageNativeCPU.h` | Mahjongg's two native functions run, `armcpu.Mahjongg`; NewtHack's five-argument one, `armcpu.NewtHack`; objects in the code binary, exceptions' data both ways and a native of another code binary over hand-assembled code, `armcpu.PackageNativeCPU` |
| Newton C++ Tools code (`BinCFunction` frames over a `'nativeModule` binary) | the same adapter | NetHopper 3.2's four binaries (`pkgns.py --natives` lists them; their stubs are anywhere in the binary, `gluetable.py --package` reads it whole) |
| Protocol parts (a part's class info, its methods dispatched into ARM code) | - | NOT YET: only the NIE's packages have them among the fixtures (below) |

Mahjongg Solitaire 2.1 (`fixtures/packages/games/Mahjongg2.1`) has two
NewtonScript functions NTK compiled native, which set the game up; with the
fallback installed (`TNewtWorld::MainConstructor` calls
`InstallPackageNativeCPU`) they run to their ends on the interpreter - 18,567
and 323 instructions - and the game deals its board.
`NEWTON_TRACE_ARMCPU=1` prints a line per native call, `=2` every call out.

## The CPU

`TARMCPU` runs ARM code over an `ARMMemory` the caller supplies (words,
halfwords and bytes at 32-bit addresses; big-endian is the memory's
business).  It has no Newton dependencies.  One register bank - the code it
runs is user code - so the exception modes and SPSR are not modelled;
coprocessor instructions stop it as undefined.  Before each instruction it
asks the memory whether the pc is a *trap* - an address with no ARM code
behind it that the host implements - which is how calls out of the ARM code
reach host functions.

Details that matter for NTK's code, all from the ARM Architecture Reference
Manual (ARMv4) and checked by `test_ARMCPU`:

- the pc reads as the instruction's address + 8 (+12 as a register-specified
  shift's operand, and when stored by STR/STM);
- an unaligned LDR reads the word it is in, rotated right by 8 x the low
  address bits (the data bus's lanes; on the big-endian Newton the addressed
  byte is not brought to the bottom);
- a halfword access ignores address bit 0, as the SA-110 does;
- LDM/STM put the lowest register at the lowest address; a stored base that
  is not the first register stored is the written-back value.

## How native code reaches the ROM

NTK compiles a NewtonScript function marked native into ARM code, all of a
package's into one binary (`pkgns.py --natives`); the function object is
`[0x232, binary, numArgs, closure, offset, ...]`.  The binary starts with
stubs, each `ldr pc,[pc,#-4]; .word 0x018xxxxx`, and a few version-dependent
ones that read ROM globals.  0x01800000 is where the MMU maps the ROM's
*public jump table*, `gROMPublicJumpTable` (physical 0x00013000-0x00015e0c,
2947 entries, mapped linearly through `gROMPublicJumpTablePageTable`): one
`B` per entry into the private patchable jump table at 0x01A00000, whose
slots are named.  `gluetable.py build/MP2x00US` lists the table;
`--package x.pkg` lists a package's stubs and what each reaches, e.g. for
Mahjongg `0x01800818 AllocateRefHandle`, `0x0180092c GetFrameSlotRef`,
`0x01801ed8 TInterpreter::PushValue`.  The public offsets are the same on
every 2.x ROM - that is what the table is for.

The ROM calls a native function with the receiver and the arguments *by
reference*: r0 the receiver's `RefVar`, r1.. the arguments' (the closure
last, when the function has one), more on the stack; the answer comes back
in r0.

## Refs in the 32-bit world (the adapter's design)

On the host a `Ref` is pointer-sized; the ARM code is 32-bit and keeps Refs
in words, in registers and in its own stack frames.  The adapter gives the
ARM code a 32-bit view:

- **Immediates pass as they are.**  Integers (30 bits), characters, nil,
  true and magic pointers have the same bit patterns in both worlds.
- **Pointer refs go through a handle table.**  An ARM-side pointer ref is
  `(index << 2) | 1`: slot `index` of a table of host `RefStruct`s (so the
  collector sees them).  A host function's answer is entered in the table
  (the same object gets the same index while the call lasts) and an ARM ref
  passed to a host function is looked up.  The table lives for one native
  call: ARM code can keep a Ref only in its own frame, and anything it
  stores in the object world goes through a host call that translates it.
- **RefHandles and RefVars live in the ARM world.**  `AllocateRefHandle`
  answers the ARM address of a two-word block `{ref, stackPos}` in the
  call's arena; a `RefVar const&` is the ARM address of a word holding such a
  RefHandle's address, which is how the ROM's calling convention passes
  every argument.  `DisposeRefHandle` gives the block back.
- **Object data is mapped on demand.**  `BinaryData` and the like answer an
  ARM address in a window onto the host object's bytes, kept while the
  call lasts (the object locked).  The bytes are seen as they lie, a word or
  halfword big-endian as on the Newton - except a string's, whose UniChars
  the host keeps in its own order, so a halfword of a string is the
  character.  (NetHopper's GIF reader keeps its LZW tables in binaries,
  written a halfword at a time and read a byte at a time.)
- **The glue answers `EQ` and `BlockMove` too.**  `EQ(RefArg, RefArg)`
  (ROM 0x0031c820) is `EQRef` of the two refs; `BlockMove` takes the
  source first and is safe for overlapping blocks.  Newt's Cape's GIF
  decoder (a BinCFunction in `nwcp20r2.pkg`'s 7 KB nativeModule) needed
  both (aa6c590e).
- **The code is relocated as the ROM maps it.**  A package with native code
  has a relocation chunk (the words to move, page by page, and the address
  it was linked at - 0 for the Newton C++ Tools); the ROM applies it to
  each page as it maps the package (`TSimpleCRelocator::Relocate`
  0x0004a148).  The host maps packages unrelocated, and the ARM world maps
  only the code binary, at 0x20000000, so the adapter applies the chunk's
  entries that fall in the binary as though the package lay where that
  puts it (`RelocateCode`, over `FramesPartObjectSource`: the binary's
  own bytes in the package and its offset there).  NetHopper's code
  reaches its constant data (a character table, a static initialiser)
  through such words.
- **The ROM image is mapped at 0** (read-only) so the version-dependent
  stubs' reads of the ROM work - or, booted on the object file with no
  image, the ROM data it carries (`ROMBytesAt`: the parameter block,
  `romsrc/romdata/`), and the few RAM globals they read (the
  interpreter, the global function frame) are answered by the adapter.

Only the entry points a package's code actually uses are implemented on the
host side; an unimplemented one stops the CPU and reports its name.  All
the entries the fixtures' native code reaches (`gluetable.py --package`
over inetenbl, modmsup, Mahjongg, newthack and nethopper) and the host has
a function for are answered, and `test_PackageNativeCPU` checks each by
name; four are not, for want of one: `Debugger`,
`EnableFramesFunctionProfiling`, `GetGlobals` (it would hand the ARM code a
host pointer) and `PublicFiller_236`.

- **The heap outlives the call.**  `NewPtr`/`NewPtrClear`, `malloc` and
  `operator new` (`__nw__FUi`, ROM 0x00318ee8: malloc of the size, one byte
  for nought) are blocks of one ARM-visible heap at 0x80000000 that every
  call and every package shares (a size word and a check word in front of
  each block, the free blocks kept in address order and run together);
  `DisposPtr`, `free` and `operator delete` give them back, `GetPtrSize`
  answers a block's size.  A C++ object a native makes on one call is still
  there on the next, as on the Newton.
- **A locked binary stays mapped.**  `LockedBinaryPtr` (ROM 0x0031c9f0:
  LockRef, then BinaryData) answers a window at 0x58000000 onto the
  binary's own bytes that lasts, across calls, until the last
  `UnlockRefArg`/`UnlockRef` of it; `BinaryData`'s windows (0x50000000)
  are the call's own.
- **RefVar and RefStruct** (the C++ classes the Newton C++ Tools' code keeps
  its Refs in: a word holding a RefHandle's address) are answered as ROM
  0x00079d74-0x00079fac: the constructors make the object with operator
  new when given none, the destructors dispose of the handle and delete
  the object when their flags' bit 0 says so.  NOT YET: a RefHandle (and the
  ref in it) is still the call's own, so a RefVar the ARM code keeps in a
  heap object past its call no longer holds its object.
- **The C library** the NCT code links against: `strcat`, `strncat`,
  `strncpy`, `strcmp`/`strncmp` (the difference of the first bytes that
  differ, unsigned), `strchr`, `strpbrk`, `strstr`, `strtok` (its place
  kept between calls), `atoi`/`atol`, `sprintf` (a double two words, the
  high first), over the ARM world's bytes.

The applications among the fixtures are used for real by
`src/host/demo/thirdparty-apps.ns` (ctest `host.NewtonThirdPartyApps`):
Mahjongg's two natives deal the board and NewtHack's runs each turn.

## Calls out, as the adapter answers them

- **By name** (`InitGlue`): the object functions (`AllocateRefHandle`,
  `GetFrameSlotRef`, `Slots`, `BinaryData`, `Clone`, ...), the interpreter's
  (`GetGInterpreter` answers an opaque address, `IsSend`, `GetReceiver`,
  `SetCallEnv`, `Call`, `Send`, `Run`, the stack-state blocks), the C library
  (`memcpy`, `memset`, `strlen`, Norcroft's `__rt_sdiv` and friends:
  divisor in r0, dividend in r1, quotient and remainder back in r0/r1), and
  exceptions.
- **Any native the host has**: an entry with no handler of its own whose
  private jump-table slot (`PublicJumpTable.cpp` records it) resolves through
  `ResolveNativeFunction` - the same resolution a ROM function object's
  funcPtr gets - is called as natives are: the receiver in r0 and the
  arguments by reference.  NTK passes nought as the receiver of a function
  that has none (`FAref` and the other frequently called functions); that is
  nil.
- **`NativeEntry`** hands the code something to call for a function object:
  a ROM native becomes a trap address (0x70000000 + n) that calls the host
  function; a function in the same code binary its ARM address (and its
  closure); a function of another code binary (another package's or part's:
  only one binary is mapped) a trap address too, which calls it as the
  interpreter would - its host re-expression if registered, else a world of
  its own on another CPU - its closure coming as the last argument; a
  NewtonScript function nought, so the code goes through the interpreter.
- **Objects in the code binary**: a ref to an address inside the binary is
  an object NTK put there, in the ROM's layout (header word, the GC's word,
  the class, the data); it comes back to the host as a host object, made
  once a call - a symbol interned, a binary copied (a string's big-endian
  UniChars swapped), an array with its slots translated.  NOT YET: frames.
  The fixtures' binaries hold only four symbols (`_proto`, `CFunction`,
  `binCFunction`, `string`); a function's literals reach it through its
  closure.
- **Exceptions**: NTK's code keeps `ExceptionHandler` records
  (`{CatchHeader, jmp_buf of 0x58 bytes, Exception}`, the DDK's layout) on its
  stack.  The adapter keeps the chain (`AddExceptionHandler`/`ExitHandler`);
  `setjmp`/`longjmp` save and restore r4-r11, sp and lr; a throw - by the ARM
  code, or out of a host function it called - fills in the innermost
  handler's `Exception` (the name copied into the arena) and longjmps to it,
  or with no ARM handler goes on out to the host.  The data goes with it,
  translated: a ref exception's (the ROM's is a `RefStruct*`) becomes a
  RefVar of the ARM world's, a message exception's (a C string) a copy in
  the arena, anything else (an error number) passes as it is; going out,
  the same in reverse (`DeliverHost`/`ThrowToHost`).
  `ThrowRefException` makes the RefVar as the ROM's makes its RefStruct.
- **The ROM image at 0**: NTK's runtime routines choose their path by the
  ROM's version words at 0x13dc/0x13e0 (0x00020002 on this ROM: the 2.x
  entry points). Traced over Mahjongg and NewtHack, `gROMVersion` (0x13dc)
  is the only ROM address the fixtures' code reads; the object file
  carries the page it is in (`gParamBlock`, 0x1000-0x2000), so the
  fixtures run the same with no image.

NOT YET: frames in the code binary.

## Protocol parts (2026-10-01; layer by layer)

A package's protocol part with no host stand-in is run on the interpreter
(`ARMProtocols.h`, `ARMWorld.h`; the package manager falls back on it
through `packages/ProtocolStandIns.h`'s `SetARMProtocolPartLoader`):

- **The world that outlives a call** (`ARMWorld.h`).  A native function's
  world lasts one call; a protocol's instances and code are there for as
  long as the part is installed.  Shared by every world: *regions* (host
  memory at ARM addresses from 0x90000000 - a part's code, a card socket's
  window; a card-bus region's accesses go through `hal/CardBus.h`, so the
  ARM code reaching an ATA card's registers reaches `hal/host/HostATA.cpp`),
  *host traps* (host functions at 0x71000000 the ARM code may branch to)
  and *registered glue* (`ARMRegisterGlue`: more of the public jump table
  answered from outside `PackageNativeCPU.cpp`).  `ARMCall` runs ARM code
  on the calling task's own world (its own arena and stack; a call made
  while one is under way - ARM to host to ARM - goes on below it), and the
  handles, windows and code objects of a call go when the outermost call
  returns.  A glue function reads and writes through the calling world
  (`ARMTrapContext::Read32` and its kin), so a native function's strings in
  its code binary are seen.
- **The class** (`LoadARMProtocolPart`).  The part's bytes are copied into
  a region (relocated where the package's relocation chunk names words in
  it - the fixtures' protocol parts have none, NTK's protocol code being
  position-independent), the ROM's TClassInfo read (names, the capability
  list, the dispatch table, sizeof/alloc/free branches - sizeof run once
  for the instance size), and a host TClassInfo made and registered as the
  part's stand-in.
- **Instances.**  One the host makes is a *proxy* - a host object of a
  class written for the interface (`RegisterARMProxyKind`), which answers
  each method by calling its ARM instance's dispatch table slot
  (`ARMCallSlot`; slot 4 is the first method) - its ARM instance made
  beside it as `PrivateClassInfoMakeAt` makes one (fRuntime 0, fRealThis
  itself, fBTable the table, fMonitorId 0) and its ARM `New`/`Delete` called
  from the proxy's.  One the ARM code makes (`AllocInstanceByName`,
  `NewByName`, `FreeInstance` - glue) is the ARM instance alone, called
  ARM to ARM.  A host implementation handed to ARM code is NOT YET; monitor
  parts are NOT YET.
- **Host objects** the ARM code sees are *mirrors* (`ARMMirrorFor`): a
  block of the ARM heap in the ROM's layout, filled in when handed over,
  whose methods' glue maps it back to the host object (`ARMHostOf`).
- **The name server** (`TUNameServer`'s constructor, destructor, `Lookup`,
  `RegisterName`, `UnRegisterName`): a thing registered is an ARM word,
  kept as it is.
- **Card handlers** (`ARMCardHandler.h`): the `TCardHandler` proxy (its
  sixteen methods, `GetDeviceInfo`'s out-parameters through ARM words,
  `CardSpecific(kCardSpecificATASetPartitionInfo)` handing over a mirror of
  the partition info with copies of its entries), and mirrors of
  `TCardSocket` (a handle: `SocketNumber`, the three windows as card-bus
  regions), `TCardPCMCIA` (its CIS numbers and flags at the ROM's offsets;
  its strings, functions and configurations through the methods' glue),
  `TCardFunction` and `TCardConfiguration`.

`test_ARMProtocols` (ctest `armcpu.ARMProtocols`) assembles a small part -
class info, table, sizeof, New, Delete and a method - and loads, relocates,
registers, makes and calls it both ways, round-trips the name server and
maps a region.  `NEWTON_TRACE_ARMPROTOCOLS` prints each part loaded, each
proxy call and the protocol glue's answers.

- **The user-side OS objects** ARM code makes (`ARMKernelGlue.cpp`):
  each is its bytes in the ARM heap in the ROM's layout with a host object
  of the class bound to it, on which each call is done - `TAEventHandler`
  (0x14 bytes; its host object's `AETestEvent`, `AEHandlerProc`,
  `AECompletionProc` and `IdleProc` call the ARM object's own vtable slots
  1-4, so an ARM subclass is called back by the host's event loop - a
  message token handed over as a mirror, `TUMsgToken::ReplyRPC` answered
  on it; `Init`, the idler, `SetReply`), `TAEvent`, `TUAsyncMessage` (its
  ids at +0 and +8), `TUPort`'s `SendGoo`/`SendRPCGoo` (sync and async),
  `TULockingSemaphore`, `GetGlobalTime`/`GetTaskTime` (a TTime returned
  through r0), `TTime::ConvertTo`, `TDelayTimer`, `ShortTimerDelay`, and
  `DebugStr`, `ZeroBytes`, `GC`, `rand`/`srand`, `printf`,
  `MemObjManager::FindEnvironmentId`; `Reboot` is logged, not done (NOT
  YET).  **Events cross between the worlds widened and narrowed**: a
  TAEvent's header is two 32-bit words on the ARM and two pointer-sized
  ULongs on the host, so every message the ARM code sends is taken to begin
  with one (DEVIATION) and widened on the way out, and a message handed to
  an ARM handler, or a reply handed back, is narrowed again; the bytes after
  the header go as they are.  An async message keeps its host copies of
  the content and reply buffer until it is done, and the completion is
  narrowed into the ARM code's own reply buffer.
- **Lists** (`ARMLists.cpp`): `CDynamicArray`, `CList` and their iterators
  as ARM code uses them, on ARM memory in the ROM's layout (0x18-byte
  arrays, 0x1c-byte iterators), transcribed from `utility/` - the ROM's own
  code cannot run here when the boot has no ROM image.
- **Code binaries** a package's relocation applies to (Newton C++ Tools
  'nativeModule code) are copied into the ARM world once and kept, at an
  address that does not move, since the C++ objects their code makes - a
  vtable in the binary, an event handler called back later from another
  task - outlive the call; a binary with nothing to relocate (an NTK native
  function's) lies at 0x20000000 for its call, as before.
- **ATA drivers**: the `TATA` proxy (`ARMCardHandler.cpp`, slots 4-22; a
  host buffer lent to the ARM code as a region for the call, a command
  block copied into the ROM's layout and back), so the ROM's ATA loader's
  `TATA::New("TATASimple")` gets a driver package's own.

`test_ARMProtocols` (ctest `armcpu.ARMProtocols`) assembles a small part -
class info, table, sizeof, New, Delete and a method - and loads, relocates,
registers, makes and calls it both ways, round-trips the name server and
maps a region; then fills, searches, walks and empties a CList through the
jump table, takes and gives back a locking semaphore, reads the time, makes
an async message, and hands a host event to an event handler whose
`AEHandlerProc` is a few ARM instructions (the event narrowed for it and
the change it made widened back).  `NEWTON_TRACE_ARMPROTOCOLS` prints each
part loaded, each proxy call and the protocol and kernel glue's answers.

ATA Support (ctests `host.NewtonATASupport`, `.restart`, `.card`): its
InstallScript native makes its server object and event handlers and
registers itself with the name server; with an ATA card put in, the ROM's
loader gets its TATASimple (version 2: it answers the loader with no
partition, so the ROM's own loader stands aside) and the card server
offers the card to its TATACardHandler, which recognises it (slot 4),
takes the partition info (19) and installs its services (6): they take
the card's IREQ interrupt (`TCardSocket::RegisterSocketInterrupt` and its
kin - the ARM interrupt proc called through a host one, the socket handed
over as its mirror), switch its power (`CardPower.h`'s Vcc/Vpp calls and
the Vcc-off notification), reset it through its option register, wait on
its ready bit and its interrupt, identify the drive and read its
partition map (block 0) - over the host's model of the card
(`hal/host/HostATA.cpp`: its INTRQ and configuration registers,
`docs/stores/README.md` "ATA cards").  A driver waits in a loop round
`TDelayTimer::TimedOut` or a short delay, and on the machine the card's
interrupt comes in meanwhile; the host delivers interrupts only at its
safe points, so those calls are made safe points too (DEVIATION,
`ARMSafePoint`).  `printf` prints on stderr (DEVIATION: the ROM's goes to
the serial debugger).

### The PSS manager's slots

ATA Support keeps its own stores and puts them in the PSS manager's slot
table itself, so that the ROM's code that asks it about a card's stores
(`GetStorePSSInfo`, `GetCardSlotStores`, the card-gone and unmount paths)
finds them: it reads the ROM global `gPSSManager` (RW data, 0x0c1016bc in
this ROM - the package picks the address for the ROM version Gestalt
names) and writes into the object at the ROM's offsets.  DEVIATION: the
host's `TPSSManager` is not in the ROM's layout and stays the one truth;
`ARMPSSManager.cpp` presents the global and the object as devices
(`ARMWorld.h`'s `ARMMapDevice`: ARM addresses whose every access the host
answers) for exactly these fields, and turns exactly these writes into
calls on the host's (`TPSSManager::HostDriverSetSlotCount`,
`HostDriverSetSlotState`, `HostDriverSetStoreInfo`):

| Where (the ROM's TPSSManager) | Read | Write |
|---|---|---|
| `gPSSManager` (a word) | the view's address, 0 with no PSS manager | refused |
| +0x304 the slot count (word) | the host's `fSlotCount` | `HostDriverSetSlotCount` (0-4) |
| +0x308 + slot x 0x1fc, the slot's state (word) | the host's `fState` | `HostDriverSetSlotState` (0-8) |
| slot +0xbc + n x 0x50, an `SPSSStoreInfo` | +0x10 only, the store: its ARM instance when it is a proxy, else a handle standing for the host store | words or bytes, staged and handed over (`HostDriverSetStoreInfo`) when the info's last byte is written; all nought clears the place |

An info handed over has its stores and card handler (+0x10, +0x28, +0x40)
made host proxies over the ARM instances (`ARMProtocols.h`'s
`ARMProxyFor`: the proxy standing for an instance the ARM code made),
+0x1c (where the device is mapped) the host address of an ARM one, and the
rest copied as numbers; a TFlash (+0x2c) is NOT YET.  Any other access -
another field, another size, a value out of range - is refused with a
trace ("[armpss] ... not presented (NOT YET)"), so the ARM code faults
there.  `test_ARMProtocols` checks each row.  So far ATA Support writes
the slot count and state and clears the four infos for a card with no
store on it.

Next: a `TStore` proxy over its TATAStore (the 42 methods; the info's
+0x10), `ToObject(TStore*)` (a store frame for it - the package's own
NewtonScript mounts its stores), PATACardServer's messages, and the
store formatted from the package's ATA Support application, written, read
back after a restart, removed and reinserted.
`gluetable.py build/MP2x00US --package x.pkg --whole --unanswered
src/armcpu` lists the public jump table entries anywhere in a package that
no glue answers yet (for ATA Support, `ToObject(TStore*)`).

## Which fixtures have native code

`gluetable.py build/MP2x00US --package` and `classinfo.py --package` over
the 19 packages in `fixtures/packages/` (2026-09-29):

| Package | NTK native functions | Protocol parts |
|---|---|---|
| Mahjongg 2.1 | binary 0xa021 | - |
| NewtHack 1.1 (`newthack.pkg`) | binary 0x17309 | - |
| NetHopper 3.2 (`nethopper.pkg`) | four `'nativeModule` binaries (BinCFunctions: the HTML parser 0x1da4d, ReadGIF 0x3c69d, 0x44585, 0x47941), 59 entries | - |
| NIE 2 `modmsup.pkg` | binary 0x3199 | PPPPLinkModule, PSLPLinkModule |
| NIE 2 `inetenbl.pkg` | binary 0x2f89 | PInetToolMux, PInetToolCCE, PInetToolCE |
| NIE 2 `enetsup.pkg` | - | PEnetLinkModule, PDhcpDynAddrModule, PLanternDriverModule |
| NIE 2 `newtdev.pkg` | - | TDriverAPI, TClientAPI, TLanternCardHandler ('cdhl) |
| NIE 2 `loctsup.pkg` | - | PMacIPLinkModule, PMacIPDriverModule |
| Kallisys ATA Support 1.0 (`drivers/ATA-Support-1.0.pkg`, an installer) | `'nativeModule` BinCFunctions CheckMachine, DecompressPackage | - |
| the driver package it installs ("ATA Support:Kallisys", 179540 bytes; `NEWTON_SAVE_PACKAGE` writes it out) | binary 0xb149 (105 entries: semaphores, ports, async messages, the name server, an event handler, CList/CDynamicArray, NewByName) | PATACardServer (PATACardServerIntf, 11 methods), TATAStore (TStore, 42), TATASimple (TATA, 19), TATACardHandler (TCardHandler, 16; 'cdhl) |

(Every NTK native binary starts with the same runtime: code from 0x29ec after
128 stubs.)  Every protocol part among the fixtures is the NIE's, which is
re-expressed natively (`src/thirdparty/nie/`) rather than run on the CPU, so
no fixture needed protocol parts through the interpreter until ATA
Support (2026-10-01): its installer's natives run (Gestalt's system info
marshalled field by field into the ROM's layout - `Glue_TUGestalt_Gestalt`
-, NSSend of two and four arguments, MemError, a TUObject's destructor;
the decompression is 3.7 million instructions), and the driver package is
stored (ctest `host.NewtonATASupport`), but its four protocol parts - all
ARM, with no re-expression (the owner's decision: the package's own code)
- are reported and not run: that is the "protocol parts through the CPU"
item.
