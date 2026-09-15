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
  to the caller as the result.
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
  any other results (r1-r4) from the saved registers.
* **Registers hold host words.** `TTask::fRegister` is `TRegister`
  (`uintptr_t`), so a pointer passed through a register survives on a 64-bit
  host. Kernel code reading a register as a 32-bit value (an id, a selector)
  casts it to `ULong`; a value written as `long` or `ULong` reads back the
  same way. The fault-monitor register block is therefore wider than the
  ROM's 100 bytes on such a host.
* **The saved pc is a resume address.** A stub sets `fRegister[kcPC]` to a
  marker of its own before the call. When the task is resumed and the pc is
  no longer that marker the kernel redirected it: the stub throws
  `TTaskRedirect`, caught by the thread's trampoline, which calls the
  function at the new pc (`Throw`, `TaskKillSelf`, `MonitorEntryGlue` -
  the host versions read their arguments from `gCurrentTask->fRegister`).
  A fresh thread starts the same way, at the pc `TTask::Init` set. The
  semaphore stub's "+4" is a marker one word higher.
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

## Where the pieces live

| ROM | Host |
|---|---|
| `SWIBoot` dispatch cases (save registers, call glue) | `src/os600/user/host/SWI.cpp` |
| exit path decisions (deferrals, `Scheduler()`, `StartScheduler`) | `SWIExitSchedule` in `src/os600/kernel/TaskSwitch.cpp` (C, cites 0x003a40d0) |
| exit path domain access word | `DomainAccessFor` in `TaskSwitch.cpp` |
| register save / restore, `movs pc,lr` | `src/hal/host/TaskRuntime.cpp` (threads, baton) |
| `MonitorEntryGlue`, `TaskKillSelf` (user-side assembly) | `src/os600/user/host/MonitorGlue.cpp` |
| `SleepTask` (the idle loop, wait for interrupt) | `HostIdleTask` in `TaskRuntime.cpp` |
| IRQ entry for the timers | `HostDeliverInterrupts` in `TaskRuntime.cpp` |

An ARM build of the same tree would supply `SWIBoot` in assembly and use
`SWIExitSchedule`/`DomainAccessFor` from it; nothing in `os600/kernel`
knows which it is running on.
