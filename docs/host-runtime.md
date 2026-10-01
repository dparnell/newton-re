# Running the reconstructed OS on a host: the task runtime

How the kernel's tasks execute when the reconstruction is built for Linux,
Windows or macOS instead of the MessagePad's ARM. Established from the ROM's
SWI dispatch and exit path (`SWIBoot`, 0x003a4018-0x003a44c0, assembly - see
`analysis/disasm.py`) and from how the kernel treats a task's saved
registers; the choices below were agreed with the project owner.

## What the ROM does

* Every system call is `swi #n` from user mode. `SWIBoot` saves some or all
  of the caller's registers into its `TTask::fRegister` (which SWIs save what
  is per case: a monitor dispatch saves everything, a port call only r0-r3),
  calls the C glue (`PortSendKernelGlue`, `MonitorDispatchKernelGlue`, …) and
  reaches a common exit at 0x003a40d0.
* The exit path, unless inside an atomic section: runs `DoDeferrals` if
  interrupt level asked for it; if `gSchedule` is set calls `Scheduler()`
  (the pick plus accounting), arms the time slice (`StartScheduler`) if
  `gWantSchedulerToRun`, and if the pick differs from `gCurrentTask` saves
  the old task's registers and PSR, stores the pick in `gCurrentTask`,
  `SwapInGlobals`, loads the pick's registers, programs the MMU's domain
  access register from the pick's environment, its copy environment and the
  environments of the tasks it is serving as a monitor (`fMonitorCaller`
  chain), and returns into the pick. With no switch, the glue's r0 goes back
  to the caller as the result; with one, the switched-out task's saved r0
  is the glue's r0 too (it is still in the register when they are saved).
  The host's stubs therefore store a glue's result in the saved r0 *before*
  the exit (`ExitWithResult`, `GenericSWIStub`), so a task pre-empted at the
  exit - a timer or the time slice delivered there - still gets it, and a
  completion that comes later overwrites it.  (Until 2026-09-29 they did
  not, and a pre-empted call answered whatever r0 held at the call: a
  `GetPortSWI` answered its own selector, and a name server lookup went to
  port 0 - which only a slow host, e.g. under `NEWTON_HEAPCHECK`, showed.)
* Kernel code that completes a blocked task writes its result into the
  task's saved r0 (and r1-r4 for a receive), and sometimes changes its saved
  pc: `TMonitor::Release` points a killed caller at `TaskKillSelf`,
  `MonitorThrowKernelGlue` points the caller at `Throw`, `TSemaphore`'s
  destructor skips the stub's retry by adding 4, and a task dispatched into a
  monitor starts at `MonitorEntryGlue`.
* The machine has one processor. Nothing in the kernel is written for two
  threads running at once; only interrupts, which are kept out with
  `EnterAtomic`/`EnterFIQAtomic`. Interrupt handlers defer real work to the
  exit path (`gWantDeferred`, `gSchedule`).

## The host model

**One host thread per `TTask`; only the thread of `gCurrentTask` ever runs.**
A single "CPU" baton, not fine-grained locking: the kernel is entered only by
the running task's thread, exactly as on the single-core machine, so the
kernel source is unchanged.

* **System-call stubs** (`src/os600/user/host/SWI.cpp`, the `*SWI` functions
  the DDK declares in UserGlobals.h) do what `SWIBoot` does: store the
  arguments and a resume marker in the calling task's `fRegister`, call the
  kernel glue directly, then run the exit path (`SWIExitSchedule`, the C form
  of 0x003a40d0). If it picked another task the stub hands the baton to that
  task's thread and parks its own until it is current again. On return the
  stub reports the glue's r0 if there was no switch, else the task's saved
  r0 - which is what the kernel wrote while the task was blocked - and reads
  any other results (r1-r4) from the saved registers. A stub with no task to
  make the call - before `OsBoot`, in a host program that runs user-side code
  without booting the OS at all (`build/host/host/newtonscript` is one), or
  once the run has ended - refuses it (`kError_Call_Aborted`, or 0) rather
  than crashing on a `gCurrentTask` that is not there; on the Newton there is
  always a current task, so nothing is lost.
* **Registers hold host words.** `TTask::fRegister` is `TRegister`
  (`uintptr_t`), so a pointer passed through a register survives on a 64-bit
  host. Kernel code reading a register as a 32-bit value (an id, a selector)
  casts it to `ULong`; a value written as `long` or `ULong` reads back the
  same way. The fault-monitor register block is therefore wider than the
  ROM's 100 bytes on such a host.
* **The saved pc is a resume address.** A stub sets `fRegister[kcPC]` to a
  marker of its own before the call. When the task is resumed and the pc is
  no longer that marker the kernel redirected it: the stub throws
  no longer that marker the kernel redirected it. A function that never
  returns is entered in place, with the stack as it is - the way the ARM
  resumes it: `Throw(name, data, destructor)` (which longjmps into a
  handler on that stack) and `TaskKillSelf`. `MonitorEntryGlue` needs the
  stack empty, so for it the stub throws `TTaskRedirect`, caught by the
  thread's trampoline, which calls it. Either way the function gets the
  saved r0-r3 as its arguments. A fresh thread starts the same way, at the
  pc `TTask::Init` set; a task proc that returns goes to `BadExit`, which
  is `TaskKillSelf`. The semaphore stub's "+4" is a marker one word higher.
* **Only a semaphore op that blocked is retried.** SWIBoot's case for SWI 11
  (0x003adf04) looks at `gCurrentTask` after `DoSemaphoreOp`: an op that
  could not proceed has unscheduled the task, leaving it nil, and only then
  is the saved pc moved back onto the SWI (`sub lr,lr,#4`) so the op is
  tried again when the task is woken. An op that succeeded takes the
  ordinary exit, and a switch there - the time slice, or the task the op
  itself woke - resumes the task *after* the SWI. `SemaphoreOpGlue` in
  `SWI.cpp` makes the same test. (Host bug fixed 2026-09-30: the stub
  retried whenever its exit switched, so an op that had succeeded was done
  twice. A `TULockingSemaphore` release that woke a waiter raised the
  kernel semaphore again once the waiter had run, and a waiter whose wait
  had succeeded waited again; with the fork world's mutex and
  `gPackageSemaphore` both busy - a script polling `GetPackages()` while the
  docker's forked world read from the desktop - every task ended up waiting
  on one or the other while a wake-up went round them for ever, two or
  three threads each burning a third of a core. `test_HostRuntime` raises
  a semaphore a higher-priority task is blocked on and checks it was raised
  once; ctest `host.NewtonDockGetPackages` is the case that showed it.)
* **Interrupts are delivered by whoever holds the baton, at safe points.**
  The idle task's host body (`HostIdleTask`) waits for the next timer
  deadline - the timer engine's alarm or the scheduler's time slice - then
  runs the handler (`TimerInterruptHandler`,
  `PreEmptiveTimerInterruptHandler`) and the exit path, exactly as the IRQ
  path would. Stubs check for a due alarm before their exit path too. A
  compute-bound task is therefore not pre-empted between system calls (a
  documented limitation of this first runtime; a host timer thread that
  merely sets a flag can be added without touching the kernel).
* **Time** comes from `hal/host/Timer.cpp`: either the controllable clock
  (tests; the idle task advances it straight to the next deadline, making a
  run deterministic) or, with `HostUseRealClock(true)`, the host's steady
  clock scaled to the Newton's 3.6864 MHz.
* **Atomic sections** are the HAL's `EnterAtomic`/`ExitAtomic`; with one
  runner they only need the nesting counts, which the exit path consults
  (`InAtomicSection`).

**Each task's thread waits for the baton on a condition of its own**
(`HostTaskContext::fTurn`), and a handover notifies only the thread that
takes it.  Until 2026-10-01 every thread waited on one shared condition and
every handover woke all of them - some ninety in a booted `newton` - to
look and go back to sleep: an idle Newton used 10-13% of a core (1.0-1.3 s
of processor in 10 s, with some 400 handovers a second from the Newton's
own 64 timer alarms a second), and `drawbench.ns` used 17-19 s of
processor for about 1 s of the newt task's own work.  With one condition
per thread the idle machine uses 0.8-1.3% (78-125 ms in 10 s) and
`drawbench` 0.6 s.  The faster machine showed a race in the demos' Setup
walk (`demo/common.ns`): a tap on Welcome's Continue made before the
machine has finished starting is not taken, and `walkSetup` now taps
again every three seconds while nothing has come of it.

## Interrupt sources: the host drivers' interrupts

The timers' and the real-time clock's interrupts are wired into
`HostDeliverInterrupts` and `HostIdleTask` by name.  Any other hardware a
host driver stands in for (the sound hardware's "this DMA buffer is
played" is the first) registers an *interrupt source* instead
(`hal/host/HostInterruptSources.h`): two functions, `deadline` - whether an
interrupt is due at all and, if so, the system-clock time it falls due -
and `deliver`, the interrupt handler.

* `HostDeliverInterrupts` asks every source at each safe point and runs the
  `deliver` of each one whose time has come, after the timers and the RTC,
  with the baton held and at interrupt level: what an ARM handler may do
  (`SendForInterrupt`, setting a flag the deferred-work path reads) and no
  system call.
* **Interrupt level is supervisor mode.** `HostDeliverInterrupts` raises
  `gHostInterruptLevel` while it runs the handlers, and the host's
  `IsSuperMode` answers true then - as the ROM's (0x00394410: the CPSR's
  mode bits are neither user nor 26-bit user) does in IRQ and FIQ mode - so
  a dual-mode routine a handler calls takes its supervisor path:
  `GetGlobalTime` reads the clock itself, as `TSerTool::IHRequest` needs
  from the serial receive interrupt (`TimeFromNow` for a delayed
  `SendForInterrupt`). A system call made at interrupt level anyway is
  refused, with one line on stderr. (Host bug fixed 2026-09-30:
  `IsSuperMode` always answered false, so that `GetGlobalTime` was a
  `GenericSWI` whose glue wrote the *interrupted* task's saved r1/r2 and
  whose exit path could switch tasks inside the handler. The task
  interrupted was a fork being started, in the middle of its `Receive` of
  the start message: the sender's message id it read back was the clock,
  the fork gave up, `Fork` answered `kError_Receiver_Object_No_Longer_Exists`
  and `GetPackages` threw "couldn't fork it over" with `gInterpreter` no
  longer the one the call had started on - a crash in `RunCall`'s unwind.
  `test_HostInterruptSources` checks both halves.)
* `HostIdleTask` folds the earliest source deadline into the time it sleeps
  until, beside the timer alarm, the time slice and the RTC alarm - so a
  source wakes a machine that has nothing else to do.  On the controllable
  clock that makes a source deterministic: the idle task jumps straight to
  the deadline and delivers it there.
* A host thread that is not a task (an audio device's callback) never calls
  either function; it only leaves something for `deadline` to read, the way
  the window's thread leaves pen records in the tablet buffer.  A driver
  whose hardware finishes on its own clock gives the time it expects the
  hardware to finish as the deadline and checks its own done flag when
  asked.

With no source registered nothing changes (`test_HostInterruptSources`
checks that a source's deadline wakes the idle task at exactly its time and
that its handler runs then; the os600 and newt tests run unchanged).

## Threads that are none of the machine's

A host program may have threads of its own that are not tasks at all: the
window on Windows (`src/host/win32/HostWindow.cpp`) runs its message loop
on one, and that is where the mouse and the keyboard arrive.  Such a
thread must never make a Newton system call.  A stub begins by taking
`gCurrentTask` for itself (`Enter`), and the exit path may then hand the
baton to another task and park the caller in `WaitForBaton` - so the
window's thread would be parked in the runtime's place while the real
task's thread went on running, two threads both believing they are the
current task, over the same heaps.  The result is not a hang but
corruption, and it shows up later as a crash somewhere else entirely.

The rule is therefore that everything such a thread does has to be a plain
memory write into a queue a task reads.  The keyboard's ring is one
(`src/host/HostKeyboard.cpp`); the pen's tablet buffer is the other, and
it stands in for the ROM's tablet interrupt, which is why
`hal/host/HostTablet.cpp` stamps its pen-down and pen-up records with the
clock itself (`HostTabletNow`) rather than letting `InsertTabletSample`
call `Ticks()`: on the ROM that call is made in supervisor mode and reads
the clock directly, while here it would be a `GenericSWI`.

As a backstop the window's thread says what it is once it starts
(`HostAlienThread`, `HostIsAlienThread`), and `Enter` refuses its calls
with a line on stderr instead of corrupting the runtime.

## A restart

**What the machine does.**  `Reboot(error, rebootType, safe)` (ROM
0x000d9884, `os600/kernel/Reboot.cpp`) records why in
`gGlobalsThatLiveAcrossReboot.fRebootReason` (unless the recorded reason
is `kError_Reboot_Calibration_Missing`, which sticks), marks the block
valid with `kRebootMagicNumber`, clears the reason again for a cold-boot
request (`rebootType == kRebootMagicNumber`), and - unless `safe` and a
reboot-protected monitor call is under way, when it only sets
`gWantReboot` for `TMonitor::Release` - jumps to address 0, the reset
vector (`Reset`, `hal/System.h`).  After more than twelve unsuccessful
boots it turns the machine off instead.  `Restart` (ROM 0x000d9984) and
`CantThrowInUndefinedModeReboot` end the same way.  Callers include the
`ReBoot()` NewtonScript function (reason noErr), the system's own
failures (`kError_Sorry_System_Failure`), and drivers - Kallisys's ATA
Support asks for one, reason -1001007, when its card is pulled with a
store mounted on it.

A warm boot runs the whole ROM boot again over the same RAM.  What it keeps
is what the boot does not clear:

| Kept on the machine | How the host keeps it |
|---|---|
| `gGlobalsThatLiveAcrossReboot.fRebootReason` (Gestalt's `rebootReason`) | `NEWTON_REBOOT_REASON` in the environment, put back before the boot |
| the kernel's copy of the tablet calibration (`fTabletValid` ... `fTabletYOffset`, `os600/user/UserPersistent.h`) | `NEWTON_REBOOT_TABLET` |
| the internal flash store, a card's memory | the same files (`--store`, the card image), flushed before the restart |
| the real-time clock | the host's clock (a clock a script set with `SetRealClockSeconds` is not kept) |
| the patch pages, the persistent memory objects, the boot counters (`fWarmBootCount`, `fUnsuccessfulBootCount`) | NOT YET: the host has no patches or persistent objects, and its boot does not count |
| - (the tasks, the heaps, the frames world, RAM packages) | made afresh, as on the machine |
| - (a card in a socket) | taken out: a restarted run starts with the sockets of its command line |

**What the host does.**  `Reset` stops the tasks (`gHostResetHook`, here
`ResetEndsTheRun`), `OsBoot` returns, and `newton` runs itself again with
the same arguments (`--erase` left out) and answers the new run's exit
status - DEVIATION: a new process stands for the jump to the reset vector,
since the host cannot clear a running process's state back to the ROM's
reset (`host/HostRestart.h`).  Before it starts the new run it flushes
every stream (the store and card files are written through them) and
closes every host socket, so the new run listens on the same ports; on
Windows it inherits the standard handles and nothing else.  The new run
reads what was handed across (`HostRestartReceive`) before the boot, and
its window opens where the old one was (`NEWTON_WINDOW_POSITION`,
`HostWindowPosition`).  A restart is not made after the power goes off,
after a script's `HostQuit()`, or when `--headless` runs out; and at most
`NEWTON_REBOOT_LIMIT` times in one run (5 by default; 0 ends the run at
the reset, as before).  Each run's `--headless` limit is its own.  A
script runs again in the new boot, and `HostRebootCount()` says which boot
it is in (ctests `host.NewtonReboot`, `host.NewtonATASupport.pull`).

## Where the pieces live

| ROM | Host |
|---|---|
| `SWIBoot` dispatch cases (save registers, call glue) | `src/os600/user/host/SWI.cpp` |
| exit path decisions (deferrals, `Scheduler()`, `StartScheduler`) | `SWIExitSchedule` in `src/os600/kernel/TaskSwitch.cpp` (C, cites 0x003a40d0) |
| exit path domain access word | `DomainAccessFor` in `TaskSwitch.cpp` |
| register save / restore, `movs pc,lr` | `src/os600/kernel/host/TaskRuntime.cpp` (threads, baton) |
| `MonitorEntryGlue`, `TaskKillSelf`, `BadExit` (user-side assembly) | `src/os600/user/host/MonitorGlue.cpp`, the trampoline |
| `GenericSWIHandler` | `src/os600/kernel/GenericSWI.cpp` (C, as in the ROM) |
| `SleepTask` (the idle loop, wait for interrupt) | `HostIdleTask` in `TaskRuntime.cpp` |
| IRQ entry for the timers | `HostDeliverInterrupts` in `TaskRuntime.cpp` |

An ARM build of the same tree would supply `SWIBoot` in assembly and use
`SWIExitSchedule`/`DomainAccessFor` from it; nothing in `os600/kernel`
knows which it is running on.

## Trying it

`src/os600/tests/test_Boot.cpp` boots the OS the way the ROM does: `OsBoot`
builds the kernel and the first task, `UserBoot` spawns the kernel services
task, and that task (given a scenario through `gHostKernelServicesTask`,
the hook standing in for the services not reconstructed yet) spawns a
`TUTaskWorld` echo server through the object manager, RPCs it over a
`TUPort`, sleeps through the null port and the timer engine and takes a
`TULockingSemaphore` - every object made by the object manager monitor,
every call through the real stubs.  `ctest -R Boot`.

`src/os600/tests/test_HostRuntime.cpp` is the lower-level check: a port, a
monitor and three tasks built by hand, a client/server exchange, monitor
calls and a timeout.  `ctest -R HostRuntime`.

`Wait(ticks)` (`os600/user/UserTime.cpp`) is the ROM's send to the null
port when a task is running.  With no task - the standalone tests, which
boot no kernel - it runs `gHostWaitHook` (`os600/user/UserBoot.h`) when
one is installed, or sleeps the thread: the hook stands in for the tasks
that would run while the caller sleeps (the host tablet installs the
inker's stand-in there, `hal/host/HostTablet.h`, so the views' pen
tracking loops see the stroke grow).

Task stacks on the host come from `NewStack` in
`src/os600/user/host/StackManager.cpp`, a stand-in for the paged stack
manager (page-aligned host allocations; locking is a no-op); before
`gOSIsRunning` they come from `NewPtr` in the kernel heap, as in the ROM
(whose `malloc` at 0x001e5068 *is* `NewPtr`).  Because a stack, the task's globals block
and the copy of its object are host memory, `VAddr` is pointer-sized on the
host (see `host_compat.h`).

Known limits of this first runtime: no pre-emption between system calls
(see above); a `Reset` (an unhandled exception reboots)
ends the run through `gHostResetHook`; the run ends by leaving parked
threads to the process exit.

A deleted task's thread ends (`gTaskDeletedHook` → `HostTaskDeleted`
marks its context dead and wakes it; `WaitForBaton` sees that and the
thread exits where it stands - `ExitThread` on Windows, a raw `exit`
system call on Linux - with its stack *not* unwound, since the destructors
on it belong to the machine another task is running meanwhile).  A task
that deletes itself is still in its own kernel call when the hook runs, so
a thread waits on its own context (`gMyContext`), not on the map's entry
for its task's address.  Left parked, as they first were, the threads of
the tasks a session makes and deletes grew by one or two a minute
(`tools/host/soak.py`).

On Linux the raw exit leaves the stack behind: glibc gives a thread's
stack back only when the thread ends through `pthread_exit`, so with
glibc's own stacks every deleted task kept eight megabytes mapped - an
eight-minute soak under WSL grew newton's data to 2.5 GB (18 GB an hour),
though its thread count stayed flat.  So a task's thread there is a pthread
over a stack of the runtime's own (`StartTaskThread`: `mmap`, a guard page,
`pthread_attr_setstack`), the dead thread puts itself on a graveyard before
it exits, and the next thread made joins each one the kernel has finished
with (`pthread_tryjoin_np`) and unmaps its stack - the same soak then held
at about 340 MB.  macOS has no raw thread exit (`SYS_exit` there ends the
process), so there a deleted task's thread stays parked
(`docs/host-macos.md`).

Because the run ends that way, the baton's `std::mutex` and
`std::condition_variable` are made once and never destroyed
(`TaskRuntime.cpp`).  Destroying a condition variable somebody is still
waiting on is undefined, and glibc's `pthread_cond_destroy` waits for its
waiters to leave: as static objects they were destroyed on the way out of
`main`, so on Linux every program that booted the OS ran its whole
scenario, printed that its checks had passed and then hung for ever with
its task threads parked on the variable being destroyed.  Windows' own
destructor happens not to wait, which is why it was never seen there.
(`docs/host-lp64.md` has the rest of what the Linux build brought out.)

- **A world is copied into its task by `GetSizeOf`.**  A `TUTaskWorld`
  subclass without its own `GetSizeOf` is copied short: its fields lie
  past the task's stack block and writing them damages the next heap
  block (the inker's missing one made the one-off `TUPort::Receive`
  crash).  Every world class needs one answering its `sizeof`; the
  ROM's may be unnamed - look at the vtable's +0x04 with
  `analysis/vtable.py`.  Checked for every world by
  `analysis/worldsizes.py build/MP2x00US` (ctest `tools.WorldSizes`).
