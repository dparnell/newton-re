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
  call lasts (the object locked).
- **The ROM image is mapped at 0** (read-only) so the version-dependent
  stubs' reads of the ROM work, and the few RAM globals they read (the
  interpreter, the global function frame) are answered by the adapter.

Only the entry points a package's code actually uses are implemented on the
host side; an unimplemented one stops the CPU and reports its name.

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
  entry points).

NOT YET: frames in the code binary; protocol parts.

## Which fixtures have native code

`gluetable.py build/MP2x00US --package` and `classinfo.py --package` over
the 19 packages in `fixtures/packages/` (2026-09-29):

| Package | NTK native functions | Protocol parts |
|---|---|---|
| Mahjongg 2.1 | binary 0xa021 | - |
| NewtHack 1.1 (`newthack.pkg`) | binary 0x17309 | - |
| NIE 2 `modmsup.pkg` | binary 0x3199 | PPPPLinkModule, PSLPLinkModule |
| NIE 2 `inetenbl.pkg` | binary 0x2f89 | PInetToolMux, PInetToolCCE, PInetToolCE |
| NIE 2 `enetsup.pkg` | - | PEnetLinkModule, PDhcpDynAddrModule, PLanternDriverModule |
| NIE 2 `newtdev.pkg` | - | TDriverAPI, TClientAPI, TLanternCardHandler ('cdhl) |
| NIE 2 `loctsup.pkg` | - | PMacIPLinkModule, PMacIPDriverModule |

(Every NTK native binary starts with the same runtime: code from 0x29ec after
128 stubs.)  Every protocol part among the fixtures is the NIE's, which is
re-expressed natively (`src/thirdparty/nie/`) rather than run on the CPU, so
no fixture yet needs protocol parts through the interpreter; a driver or
comms tool package without a host re-expression is what would.
