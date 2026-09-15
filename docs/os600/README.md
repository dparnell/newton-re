# OS600 — the Newton kernel layer

Working notes for the lowest layer of Newton OS 2.x as found in the MP2100 D
ROM. Facts here were established with the tools in `tools/newton-rom/` and
the Ghidra project they produce; where a table can be regenerated from the
ROM it is (see "Regenerating"), everything else says how it was found.

## Shape of the layer

```
 user code (NewtonScript runtime, comms, UI, ...)
   │  C++ user-side classes from the DDK headers (headers/OS600):
   │  TUObject, TUTask, TUPort, TUSharedMem(Msg), TUMonitor, TUSemaphore*,
   │  TUDomain, TUEnvironment, TUPhys, TUNameServer, TUAsyncMessage ...
   │  each wraps a TObjectId and calls a stub
   ▼
 SWI stubs (0x3A3F78-0x3A4E28): `swi #n` — see swi-table.md
   ▼
 SWIBoot (SWI vector handler, assembly) → 35-way dispatch
   │   SWI 5 = GenericSWI → GenericSWIHandler → 70 selectors
   ▼
 kernel C++ ("*KernelGlue" routines and the kernel objects):
   TObjectTable (ids → objects), TTask, TScheduler, TPort, TSharedMem(Msg),
   TMonitor, TSemaphore(Group), TDomain, TEnvironment, TTimerEngine,
   TNameServer, TDoubleQContainer, TObjectHeap, TPhys ...
```

User-side objects are handles: a `TUObject` holds a `TObjectId` (`fId`) and a
`Boolean fObjectCreatedByUs`; every operation is a SWI that the kernel
resolves through `TObjectTable::Get` (GenericSWI selector 27) to the kernel
object. Ports carry `TSharedMemMsg`s; monitors are the kernel's
synchronous-call mechanism (`MonitorDispatchSWI` / `MonitorExitSWI`);
`TDomain`/`TEnvironment` are the MMU protection domains a task runs in.

## Entry points and boot

Exception vectors at 0 (all `B`): `BootOS` (reset), `FP_UndefHandlers_Start`
(undefined instruction, via the jump table), **`SWIBoot`** (SWI),
`PrefetchAbortHandler`, `DataAbortHandler`, `IRQHandler`, `FIQHandler`.

Boot as far as traced: `BootOS` (0x18688) — reboot reason, bus control
registers, memory test, `CopyRAMTableToKernelArea`, `InitTheMMUTables`,
special stacks, `InitCGlobals` (0x45C84: kernel heap area, kernel globals
page, jump tables, FPE, REx config, internal flash + patch installation),
`PostCGlobalsHWInit`, then assembly (`FUN_00018b78`: FIQ/IRQ stack setup)
that continues into the kernel start. `UserBoot` (0x2D1860) is the first
user-mode code: static semaphores, `InitMemArchObjs`,
`InitDomainsAndEnvironments`, `InitROMDomainManager`, then it creates and
starts the first `TUTask`. The hand-off between the two still has to be
traced.

## Kernel classes

None of the kernel-side classes are declared in the DDK (it only ships the
user-side API), so their layouts have to be recovered from the code. Sizes
below come from `romfacts.json` (constructor allocations); method counts from
the symbol table.

| kernel class | size (ROM) | vtable | methods | DDK header |
|---|---|---|---|---|
| `TObjectTable` | ? | — | 11 | — |
| `TObjectTableIterator` | 0x1c | — | 6 | — |
| `TObjectManager` | 0x4 | — | 2 | — |
| `TTask` | 0x104 | — | 5 | — |
| `TTaskContainer` | ? | — | 1 | — |
| `TTaskQueue` | 0x8 | — | 7 | — |
| `TScheduler` | 0x120 | yes | 7 | — |
| `TPort` | ? | — | 5 | — |
| `TSharedMem` | ? | — | 1 | — |
| `TSharedMemMsg` | 0xa8 | — | 6 | — |
| `TMonitor` | 0x48 | — | 11 | — |
| `TSemaphore` | 0x28 | yes | 7 | — |
| `TSemaphoreGroup` | ? | — | 4 | — |
| `TSemaphoreOpList` | ? | — | 2 | — |
| `TDomain` | 0x24 | yes | 21 | — |
| `TEnvironment` | ? | — | 7 | — |
| `TTimerEngine` | 0x14 | — | 9 | — |
| `TTimerQueue` | 0x10 | — | 7 | TimerQueue.h |
| `TTimerElement` | 0x18 | yes | 4 | TimerQueue.h |
| `TNameServer` | ? | — | 21 | — |
| `TDoubleQContainer` | 0x14 | — | 13 | — |
| `TDoubleQItem` | 0xc | — | 1 | — |
| `TObjectHeap` | 0x34 | — | 36 | — |
| `TPhys` | 0x18 | — | 8 | — |
| `TTaskSafeRingBuffer` | 0x34 | yes | 32 | — |

"?" = no self-allocating constructor observed (the object is embedded,
placement-constructed or created by a factory), so the size must come from
elsewhere. The user-side classes are all in the DDK; the ROM confirmed their
layouts (`TUPort` 8 bytes, `TUNameServer` 0x10, `TUAsyncMessage` 0x10,
`TUTaskWorld` 0x18, ...).

## Reconstruction status

Reconstructed in `src/os600/kernel/` (with host tests): `TDoubleQItem` /
`TDoubleQContainer` (DoubleQ.*), `TKernelObject` (KernelObject.h),
`TObjectTable` / `TObjectTableIterator` (ObjectTable.*), `RegisterObject` /
`GiveObject` / `AcceptObject` (KernelObjects.*), and the first HAL interface,
`hal/Atomic.h` (EnterAtomic & co., host implementation in `hal/host`);
`TTaskQItem` / `TTaskQueue` / `TTaskContainer` and the `TTask` layout
(Task.*), `TScheduler` with `ScheduleTask` / `UnScheduleTask` /
`WantSchedule` / `StartScheduler` / `StopScheduler` (Scheduler.*), and
`hal/Interrupts.h` (interrupt enable/disable and the time-slice alarm);
`TSemaphore` / `TSemaphoreGroup` / `TSemaphoreOpList` with `DoSemaphoreOp`
(SWI 11), `SemGroupSetRefCon`/`GetRefCon` (GenericSWI 40/41), `DeleteSemList`
/ `DeleteSemGroup` and `MarkMessageDone` (Semaphore.*).

Semaphores: a group is an array of counting semaphores (0x28 bytes each:
`TKernelObject`, vptr, value, two `TTaskQueue`s for tasks waiting on zero /
on increment); an op list is `MAKESEMLISTITEM(sem, delta)` words applied
all-or-nothing, unwinding and blocking the task (state bit 0x100000,
`fContainer` = the semaphore) when an op cannot proceed. Destroying a group
resumes its waiters with `kError_Semaphore_Group_No_Longer_Exists` in r0
and their saved pc advanced past the retry. Two ROM quirks: `SemOp` accepts
semaphore index == count (one past the array), and `TSemaphore::Remove`
unlinks through the wrong queue for tasks waiting on increment (see the
`DEVIATION` note in Semaphore.cpp).

Layouts established on the way: `TKernelObject` {fId, fNext, fOwnerId,
fAssignedOwnerId}; `TObjectTable` = scavenge proc + cursor + 128 buckets
(0x210 bytes); ids are `(unique << 4) | KernelTypes` with the bucket
`(id >> 4) & 0x7f`. Ownership: an object is alive while its owner exists
(or it owns itself); `Scavenge` walks one bucket per call removing the rest
through the scavenge proc's destructor. Oddity kept as found: `GiveObject`
refuses a target task that exists and is alive.

Scheduling: 32 priority buckets of FIFO `TTaskQueue`s in `TScheduler`
(0x120 bytes: `TKernelObject`, vptr at +0x10, fCurrentBucket, fPriorityMask,
32 queues, fPreferredTask); `Schedule()` re-queues the running task at the
back of its bucket and takes the head of the highest non-empty bucket, or
the idle task. `StartScheduler` arms a 20 ms (0x12000-tick) time slice on
the Voyager timer (match register 0x0F182C00 from counter 0x0F181800) through
`gSchedulerIntObj`; `WantSchedule` is deferred while `gHoldScheduleLevel`
is non-zero. `TTask` is 0x104 bytes: `TKernelObject`, r0-r15 + PSR at +0x10,
state bits at +0x6c (0x20000 = scheduled, 0x400000 = kill pending,
0x2000000 = stack from NewStack), environment +0x74, priority +0x80,
name +0x84, stack top/base +0x88/+0x8c, container +0x90, queue links +0x94,
globals +0xa0, run time +0xa4, memory accounting +0xb0, two TDoubleQItems
at +0xbc/+0xc8, monitor +0xd4/+0xd8, shared mem/msg ids +0xf0/+0xf4,
initial sp +0xf8, bequeath ids +0xfc/+0x100. Kernel "local ids" 1, 2, 3
(`LocalToGlobalId`) stand for the current task's shared-memory message,
shared memory, and the current monitor's caller.

## Kernel globals

The kernel keeps its state in RAM at 0x0C100800-0x0C104EDB (`RAM_RW` and
`RAM_ZI` in Ghidra, all named from the symbol table): `gObjectTable`,
`gCurrentTask`, `gSchedule`/`gWantSchedulerToRun`, the atomic nesting
counters (`gAtomicNestCount`, `gAtomicFIQNestCount`, the `*Fast` variants),
`gWantDeferred`, `gCopyDone`, `gFIQSavedStackHead`/`gIRQSavedStackHead`, and
so on. `SWIBoot` reads several of them on every SWI (see its head at
0x3A4018).

## Regenerating

```
build\venv\Scripts\python tools\newton-rom\analysis\swi_table.py build\MP2100D --project build\ghidra --name MP2100D --ghidra <ghidra> -o docs\os600\swi-table.md
```

## Open questions (next steps)

1. Trace the boot hand-off from the assembly after `PostCGlobalsHWInit` to
   the first task, and document `SWIBoot`'s non-SWI paths.
2. Recover the kernel object layouts (`TTask`, `TPort`, `TObjectTable`,
   `TSharedMemMsg`, `TMonitor`, `TSemaphore*`, `TDomain`, `TEnvironment`) from
   their methods; feed them back as Ghidra structures.
3. Document the argument conventions of each SWI (what r0-r3 carry) from the
   user-side wrappers in `headers/OS600/*.h` and the stubs.
