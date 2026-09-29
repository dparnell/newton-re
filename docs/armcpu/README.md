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
| The adapter: a package's native function run through `SetPackageNativeFallback` | `src/armcpu/PackageNativeCPU.h` | in progress |

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
