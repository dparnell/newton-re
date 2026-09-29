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
(see above); a deleted task's thread is left parked (`gTaskDeletedHook` →
`HostTaskDeleted` forgets it); a `Reset` (an unhandled exception reboots)
ends the run through `gHostResetHook`; the run ends by leaving parked
threads to the process exit.
