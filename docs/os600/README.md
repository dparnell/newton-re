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
that continues into the kernel start, `OsBoot` (0x149C1C).

`OsBoot` (reconstructed, Boot.cpp) runs with a `TTask` and `TEnvironment` on
its own stack as the current ones: `HInitInterrupts`, `InitInterruptTables`,
the object table, `InitMemArchCore` (the memory architecture's object table
and manager, the page managers, the fault-monitor table),
`InitKernelDomainAndEnvironment` (the 'krnl' environment on the kernel heap
with domain 2), then with FIQs off `InitGlobalWorld` (scheduler, the copy /
blocked-on-memory / deferred-send queues, `InitObjectManager`, the null
port), `InitTime`, the real-time clock, `UserInit` (the user side's handles
on the object manager monitor and null port). The idle task is made out of
the boot context (`TTask::Init` with `OsBoot` as its proc, then
`SwapInGlobals`), the first task 'user' (priority 20, 0x800 stack) is made
to run `UserBoot`, `StartTime` starts the timer engine and the hourly
overflow detector, `TabBoot` the tablet, time accounting is switched on, the
scheduler's time slice armed, the 'user' task added, and the boot context
becomes the idle loop (`SleepTask`: `PauseSystem` forever).

`UserBoot` (0x2D1860, user side) is the first user-mode code: the semaphore
classes' shared op lists, the kernel heap's semaphore, `InitMemArchObjs`
(page manager and page-table manager monitors, the stack manager),
`InitDomainsAndEnvironments` (from the memory object database),
`InitROMDomainManager`, `gOSIsRunning = true`, seed `rand` from the
real-time clock, then it makes and starts the kernel services task 'ksrv'
(0x6800 stack, priority 10, environment 'ksrv') running `InitialKSRVTask` -
protocol registry, stdio, the name server, the ROM domain manager, the
package manager, and the first `TAppWorld` ('drvl') - and leaves its own
objects to the idle task.  On the host `test_Boot` boots this way and lets
the 'ksrv' task run a scenario with the real `TU*` classes.

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
| `TNameServer` | 0x1a8 | — | 21 | — (NameServerImpl.h) |
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
/ `DeleteSemGroup` and `MarkMessageDone` (Semaphore.*); `CompAdd`/`CompSub`/
`CompCompare` (toolbox/CompMath.cpp); `hal/Timer.h` (GetClock, SetAlarm,
DisableAlarm1 - a controllable clock on the host); `TSharedMem` /
`TSharedMemMsg` layouts (SharedMem.*); `TTimerEngine` with the alarm
interrupt, `SetAlarmAtomic`/`ClearAlarmAtomic` and `QueueNotify`
(TimerEngine.*); `TPort` with `Send`/`Receive`/`Reset`/`ResetFilter`,
message completion (`CompleteMsg`/`CompleteSender`/`CompleteReceiver`), the
port and shared-memory system calls (SWI 0, 1, 2, 13, 14, 17-23, 26, 33,
GenericSWI 67), `NotifySend`/`NotifyTimeout`/`DeferredNotify`/
`PortDeferredSendNotify`, `CheckCopyTask` and the `Delete*` destructors
(Port.*); `LocalToGlobalId`/`ConvertIdToObj`/`ConvertMemOrMsgIdToObj`
(KernelObjects.*); `TMonitor` with `Aquire`/`Release`/`Suspend`/
`SetUpEntry`/`FlushTasksOnMonitor` and the monitor system calls (SWI 27,
28, 29, 32), `DeleteMonitor` (Monitor.*); `TObjectManager::MonitorProc`
with `ObjectDestroy`/`ObjectStart`/`ObjectSuspend`/`ObjectGetRegister`/
`ObjectSetRegister`/`GetObjectContent`/`SetDomainFaultMonitor` and
`ObjectScavenger` (ObjectManager.*), `HoldSchedule`/`AllowSchedule`
(Scheduler.*); `Scheduler()`, `SwapInGlobals`, `DoDeferrals`,
`ResetAccountTimeKernelGlue`/`GetNextTaskIdKernelGlue` (GenericSWI 5/6)
(TaskSwitch.*); `Swap`/`SwapByte` in hal/Atomic.h; `TTask::Init`/`FreeStack`/
`~TTask`/`SetBequeathId`, `TMonitor::Init`, `ObjectAlloc`, `DeleteTask`,
`InitObjectManager`, `SMemCopyTo/FromKernelGlue` (SWI 15/16),
`GenericSWIHandler` (GenericSWI.*), the SWI exit path as `SWIExitSchedule`
and `DomainAccessFor`, `OsBoot` with `InitGlobalWorld`,
`InitKernelDomainAndEnvironment`, `InitMemArchCore`, `StartTime`, `InitTime`
(Boot.*, TimerEngine.*); the per-task globals block (`os600/TaskGlobals.h`);
and in `src/os600/user/`: `TUObject`, `TUSharedMem(Msg)`, `TUPort`,
`TUMsgToken`, `TUAsyncMessage`, the semaphore classes, `TUMonitor`, `TUTask`,
`TUTaskWorld`, `UserInit`/`UserBoot`/`InitialKSRVTask`, the task helpers
(`Sleep`, `Yield`, `TaskGiveObject`, ...), `InitializeExceptionGlobals`;
`MemObjManager` with `DomainInfo`/`EnvironmentInfo`/`PersistentDBEntry`,
`BuildMemObjDatabase`, `PrimGetMemObjInfo` (GenericSWI 0x2c) and the
generated `MemObjTables.cpp` (MemObjManager.*); `TSingleQContainer`
(SingleQ.cpp); `TKDomain::Init`/`InitWithDomainNumber`/`~TKDomain` over
`hal/MMU.h`'s `SetDomainRange`/`ClearDomainRange`, the domain case of
`ObjectAlloc`; `TUDomain`, `TUEnvironment` (the DDK lacks its header;
`src/os600/user/UserEnvironment.h` declares it), `InitDomainsAndEnvironments`
with `BuildDomainsAndHeaps` (plain domains only - the heap domains' areas
and heaps are the paged memory system, NOT YET) and `BuildEnvironments`
(user/BuildEnvironments.cpp); the exception system - `Throw`, `ThrowMsg`,
`Subexception`, `AddExceptionHandler`/`RemoveExceptionHandler`/
`SetExceptionHandler`/`GetExceptionHandler`, `ExitHandler`, `NextHandler` -
and the generated exception names (user/Exceptions.cpp, ExceptionNames.cpp); `Reboot`/`Restart`/
`CantThrowInUndefinedModeReboot` (Reboot.*) over `hal/System.h` (Reset,
DisableAllInterrupts, IOPowerOffAll); the user-mode entry points the kernel
points tasks at, `MonitorEntryGlue`/`TaskKillSelf`/`Throw`
(`src/os600/user/MonitorGlue.h`, host stand-ins); `TKDomain` (layout,
constructor, `SetFaultMonitor`, `Intersects`), the domain access control
word helpers and the fault monitor table (Domain.*), `TMemArchManager`
(MemArchManager.*), `TEnvironment` with the environment system calls
(GenericSWI 0x23-0x27) (Environment.*); the name server - `TNameServer`
with `TObjectNameList`/`TObjectNameEntry`, the system-event registrations
and `Gestalt`, `InitNameServer` (user/NameServer.cpp, NameServerImpl.h),
its clients `TUNameServer` (UserNameServer.cpp), `TSystemEvent`/
`TSendSystemEvent` (SystemEvents.cpp) and `TUGestalt` (UserGestalt.cpp) -
over the utility containers (`src/utility`: `CDynamicArray`,
`CArrayIterator`, `CList`, `CListIterator`, `CSortedList`, `CItemTester`/
`CItemComparer`).

The name server, as established: `InitialKSRVTask` spawns it as a
`TUTaskWorld` named 'name' (6000-byte stack, priority 10); its port becomes
the kernel's `gNameServer`, the well-known port `GetPortSWI(2)` hands out,
and every client (`TUNameServer`, `TSystemEvent`, `TUGestalt`) is an RPC
to it with a `TNameServerRequest` (NameServer.h: the command word, then the
request's fields; the name and type strings travel in two shared-memory
objects the client keeps). Names hash (byte sum mod 16) into
`TObjectNameList` buckets of `TObjectNameEntry` (name, type, thing, spec);
`WaitForRegister`/`WaitForUnregister` callers are parked in the bucket
(their message tokens kept) and answered when the name appears or goes -
`TObjectNameList::Remove` matches on the name alone, a ROM quirk kept.
System events: per event an `EventMasterListItem` holds a `CSortedList` of
(port, timeout, filter) registrants; `SendSystemEvent` delivers the
sender's message to them one at a time by asynchronous send collected back
on the server's port (refcon `TRPCInfo`), taking no further event sends
meanwhile (filter `~1`), and replies to the sender when all have had it.
Gestalt: system selectors are answered from the kernel's globals
(`kGestalt_SystemInfo` and `kGestalt_RexInfo` NOT YET - they need the
display, tablet and ROM-extension code); registered ones are names
"<selector in hex>" of type "GSLT" whose thing is the block's address, and
`TUGestalt` copies the block itself. Resource arbitration (the comm tools'
claim/unclaim of a registered resource, `TResArbitrationInfo`,
0x00131498-0x00131b50) is NOT YET RECONSTRUCTED: the server answers
`kError_Call_Not_Implemented`.

Memory architecture, as established so far: a `TKDomain` (0x24 bytes) is a
1 MB-aligned range of virtual space tied to one of the ARM MMU's 16
protection domains; `gTheMemArchManager` (`TMemArchManager`, 0xc bytes)
hands out domain numbers from a DACR-shaped bitmap (never number 15; the
default word 5 reserves 0 and 1 for the kernel), keeps a list of domains
that must not overlap and a list of environments. A `TEnvironment` (0x2c
bytes, no constructor) is a domain access control word - two bits per
domain, 01 client, 11 manager - plus the ids of its heap and stack domains
and a reference count of the tasks running in it; `SetEnvironment` moves
the current task between environments, and an environment removed from the
manager frees itself when its last user lets go. A domain's number indexes
a 16-entry fault-monitor table at 0x0c1030b8 (unnamed in the symbol table)
that the abort handler uses to find the monitor for a fault. ROM bug found:
`TMemArchManager::RemoveEnvironment` never advances along its list (see
the `DEVIATION` note).

The memory object database (`MemObjManager`, docs/os600/memobj-tables.md
for the tables): the machine's memory layout is data - a domain table
(`g1MegDomainTable`/`g4MegDomainTable`, chosen by `InitCGlobals` from the RAM
size: name, 1 MB-aligned base and size, heap and handle-heap sizes, flags),
the environment table (`gEnvTable`: each environment's default heap and
heap/stack domains and its lists of client and manager domains) and the
data-area table (`DataAreaTable`, ROM 0x40: where a domain's initialised
globals come from and go). `BuildMemObjDatabase` lays four tables of entries
out in RAM (`gMemObjHeap`): domain ids, heap addresses, persistent-heap
records (`PersistentDBEntry`, plus ten spare 'emty' slots) and environment
ids, all 0 until the kernel makes the objects and registers them
(`RegisterEnvironmentId`, `RegisterDomainId`, `RegisterHeapRef`).
`FindEnvironmentId('krnl')` & co. read them. `UserBoot`'s
`InitDomainsAndEnvironments` walks the tables: a kernel domain for every
entry (heap domains get their heaps and globals areas there too - not yet
reconstructed) and an environment for every entry with its domains added
as client or manager and its stack and heap domains flagged. A correction
established on the way: `TEnvironment` +0x18 is the *stack* domain
(`TTask::Init` takes new stacks from it) and +0x1c the *heap* domain;
`AddDomainToEnvironment`'s flag bits are 1 heap, 2 stack, 4 manager, and
`TEnvironment::Add`'s arguments run (manager, stack, heap). The public calls are
dual-mode: in user mode the request goes through GenericSWI 0x2c with its
selector, arguments and answer in the first 0x30 bytes of the caller's task
globals block (`MemObjRequest`) - `PrimGetMemObjInfo` serves it.  On the
host `IsSuperMode()` is false, so this path is the one exercised.

Exceptions, as established: `NewtonExceptions.h`'s try/catch is a chain
of `CatchHeader`s (try handlers with a setjmp buffer, `unwind_protect`
cleanups, boundary markers) hanging off `gFirstCatch`, the last word of the
task globals, in user mode - or off one global each for FIQ and IRQ mode
(0x0c100d1c/0x0c100d20; `GetCPUMode`'s low bits pick). `Throw` runs the
cleanups on its way to the first try handler, unlinks it, fills in the
exception and longjmps into it; `ExitHandler` at `end_try` either destroys
a caught exception's data or unlinks a handler that ended normally. With no
handler left, a task inside a monitor passes the exception to its caller
(`MonitorThrowSWI`: the caller is resumed at `Throw`), and otherwise the
machine warm-reboots with `kError_Sorry_System_Error` after "Unhandled
exception %s". Names are dotted paths matched by prefix (`Subexception`;
';' separates alternatives); the 35 the ROM defines are generated into
`ExceptionNames.cpp` by `analysis/exception_names.py`.

Object manager, as established: user code makes, destroys and manipulates
kernel objects through one monitor, `gTheObjectManagerMonitor` ('OBJM',
`TObjectManager::MonitorProc`), with an `ObjectMessage` (size, then the
object type or id at +0x08, then per-request fields - see
`src/os600/ObjectMessage.h`) and a selector: alloc, destroy (owners only),
start/suspend a task, get/set one of its registers, add/remove a domain in an
environment, read a task's accounting figures, set a domain's fault monitor.
The object table's scavenge proc is `ObjectScavenger`: it maps each object
type to its `Delete*` destructor, defers a task that is inside a monitor
(marking it kill-pending) and a monitor with a call in progress (suspending
it), and never scavenges environments or domains. `TaskKillSelf` asks the
monitor with selector 0xff: the caller is marked (state bit 0x2), never
resumed by `TMonitor::Release`, and removed at the next request; every
request ends with `ScavengeAll` if a task was destroyed (`gTaskDestroyed`).
Still to come there: `ObjectAlloc` (waits for `TTask::Init`,
`TMonitor::Init`, `TKDomain::Init`, `TPhys`), `DeleteTask` (waits for
`~TTask`), the external page trackers.

Task switching, as established: the SWI exit path (0x003a40d0, assembly)
runs `DoDeferrals` if interrupt level asked for it, then `Scheduler()` when
`gSchedule` is set; `Scheduler()` takes `TScheduler::Schedule`'s pick and
charges the outgoing task for heap growth since the last swap (`gPtrsUsed`/
`gHandlesUsed` against the saved totals, `gNumberOfTaskSwaps`) and, when
`gCountTaskTime` is on, for run time (the clock advance less the
interrupt-handler time accumulated in `gIRQInterruptOverHead`/
`gFIQInterruptOverHead`, atomically swapped out). The exit path stores the
pick in `gCurrentTask`, and `SwapInGlobals` sets `gCurrentTaskId`,
`gCurrentGlobals` (the task's globals block) and `gCurrentMonitorId`. The
glue's r0 is what the caller sees unless a switch intervenes, in which case
its saved r0 is. `TaskKillSelf` is `GetPortInfo(0)` (the object manager
monitor's id) then a monitor call with selector 0xff.

Monitors, as established: a `TMonitor` (0x48 bytes) owns a task at
`gMonitorTaskPriority` and a `TSharedMemMsg`. `MonitorDispatchSWI` saves all
of the caller's registers, then `Aquire` takes the caller off the run queues
and either dispatches at once (`SetUpEntry`: the monitor task's r0-r3 become
monitor object, selector, user object and proc, its pc `MonitorEntryGlue`,
its sp the top of its stack, and it is scheduled) or queues the caller
through `TTask::fMonitorQItem`. `MonitorEntryGlue` calls the proc in user
mode and issues `MonitorExitSWI` with the result; `Release` writes it into
the caller's r0, makes the caller the scheduler's preferred task, stops the
monitor task and dispatches the next waiter. A caller killed meanwhile
(state 0x400000 or 0x2) resumes in `TaskKillSelf` instead. Fault monitors
are entered by the abort handler (state bit 0x800000): the monitor's message
is pointed at the caller's saved registers (100 bytes from r0), the selector
is `kMonitorFaultSelector`, and the result chooses resume (0 keeps r0), kill
(4) or park on `gBlockedOnMemory` (5). `MonitorThrowSWI` makes the caller
resume in `Throw` with the exception (or, if it was copying shared memory,
fails the copy with `kError_UnResolvedFault`); a caller not in user mode
means `CantThrowInUndefinedModeReboot`. `kSuspendMonitor` (-1) from the
owner marks the monitor suspended without running the proc; later callers
get `kError_No_Such_Monitor` and the destructor fails the waiters. A
reboot-protected monitor holds `gRebootProtectCount` during each call, and
`Reboot(..., safe)` only sets `gWantReboot` while it is non-zero - `Release`
restarts when the last such call ends, and `SetUpEntry` dispatches nothing
more. `Reboot` records the reason in `gGlobalsThatLiveAcrossReboot`
(`kError_Reboot_Calibration_Missing` is sticky), and after more than 12
unsuccessful boots powers the machine off instead of resetting. Noted, not
fixed: `Release` does not reschedule a queued `kSuspendMonitor` caller when
its turn comes (`Aquire` does for an idle monitor).

IPC, as established: a port holds two `TDoubleQContainer`s of messages
(senders waiting for a receiver, receivers waiting for a sender, both linked
through `TSharedMemMsg::fPortQItem` at +0x80). A receive meets the first
sender whose message type matches its filter (`kMsgType_MatchAll` = any);
`CompleteReceiver` records the sender's id/reply memory/type in the receive
message, queues the sender on the receiver's own `fSenders` list with a
sequence number, and `CompleteMsg` delivers: for a task notify target it
writes r0 = result, r1 = sender msg id, r2 = reply mem id, r3 = msg type,
r4 = sequence into the task's saved registers, makes it the scheduler's
preferred task and schedules it; for a port notify target (asynchronous
calls) it re-sends the message to that port flagged
`kSMemMsgFlags_CompleteTo{Receiver,Sender}Port`, to be collected by
`SMemMsgCheckForDone`. The receiver answers with `MsgDone(senderMsgId,
result, sequence)` (the `TUMsgToken`), which completes the sender the same
way. Synchronous calls block the caller with `UnScheduleTask`
(`kPortFlags_CanRemoveTask`). Timeouts and delayed sends go through the
timer engine: expired messages are moved to `gTimerDeferred` from the
interrupt and completed later by `DeferredNotify` on the scheduler path.
Shared-memory copies (SWI 15/16) are set up in the calling task's registers
and performed by the SWI handler in the caller's environment; `gCopyTasks`
tracks them and `LowLevelCopyDone` (SWI 26) finishes them. The kernel's
time base is a 64-bit tick count whose high word counts wraps of the
32-bit counter at 0x0F181800 (`GetClock`); the timer engine's alarm is
"alarm 1" (`SetAlarm1` 0x003a3d10, interrupt bit 0x20 in the controller at
0x0F184000), while the scheduler's 20 ms slice uses match register
0x0F182C00.

Two host-layout lessons: intrusive queues must take their item offset from
`offsetof()` rather than the ROM's literal (the ROM passes 0x80 for
`TSharedMemMsg::fPortQItem`, which is not where it lands on a 64-bit host),
and the running task is never in a scheduler bucket - `Schedule()` dequeues
it and a blocking call leaves `gCurrentTask` nil.

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
at +0xbc/+0xc8, monitor being called / served +0xd4/+0xd8, monitor caller +0x7c, shared mem/msg ids +0xf0/+0xf4,
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
build\venv\Scripts\python tools\newton-rom\analysis\memobj_tables.py build\MP2100D -o docs\os600\memobj-tables.md --cpp src\os600\kernel\MemObjTables.cpp
```

## Open questions (next steps)

1. Trace the assembly between `PostCGlobalsHWInit` and `OsBoot` (stack
   setup, `InitCGlobals`), and document `SWIBoot`'s non-SWI paths (aborts,
   the copy engines).
2. Recover the kernel object layouts (`TTask`, `TPort`, `TObjectTable`,
   `TSharedMemMsg`, `TMonitor`, `TSemaphore*`, `TDomain`, `TEnvironment`) from
   their methods; feed them back as Ghidra structures.
3. Document the argument conventions of each SWI (what r0-r3 carry) from the
   user-side wrappers in `headers/OS600/*.h` and the stubs.
