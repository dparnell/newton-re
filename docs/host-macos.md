# A macOS host: the plan

The owner wants the user-mode side of the OS to run on other host operating
systems (`docs/host-lp64.md` is Linux).  macOS is the one left; it cannot be
built or run on the development machine, so this page is the plan for it,
written so that the port is *new files behind seams that already exist*
plus a handful of `#elif defined(__APPLE__)` lines - and those lines have
been put in already (2026-10-02), so a first macOS configure should build
and run headless and silent with nothing more than the system compiler.
Nothing here has been compiled on a Mac: the first job on one is to find
out what this page got wrong.

## What carries over unchanged

Everything below the host seams is portable C++17 and has been run on two
hosts with different `long`s (Windows LLP64, Linux LP64); macOS is LP64
like Linux, so `docs/host-lp64.md`'s whole list already applies to it
(`Long32`/`ULong32`, `Fixed` pinned to 32 bits, `sizeof` for ROM byte
counts).  The kernel's host runtime (`os600/kernel/host/TaskRuntime.cpp`:
one `std::thread` per task, a baton on `std::mutex`/`std::condition_variable`),
the timers (`std::chrono`), the sockets (`hal/host/HostSockets.cpp`, BSD
sockets - the POSIX branch), the files, the restart (`host/HostRestart.cpp`:
fork/execv), the object file, the stores and everything above them need
nothing.

The POSIX code that was Linux-only, and what it now does on macOS:

| Where | Linux | macOS (already in the source) |
|---|---|---|
| `hal/host/System.cpp` `GetStackBounds` | `pthread_getattr_np` | `pthread_get_stackaddr_np`/`pthread_get_stacksize_np` (the top and the size) |
| `host/HostCStack.h` (a crash's, a traced throw's, the heap check's C stack) | glibc `backtrace`, `dladdr` | the same calls - macOS has `<execinfo.h>` and `dladdr` |
| `host/newton.cpp` the crash handler's faulting instruction | `uc_mcontext.gregs[REG_RIP]` / `.pc` | `uc_mcontext->__ss.__rip` / `__ss.__pc` |
| `host/newton.cpp` the self-sampler (`SIGRTMIN+3`, `gettid`) | built | left out: macOS has no real-time signals (see "The crash tools") |
| `host/HostRestart.cpp` the program's own path | `/proc/self/exe` | `_NSGetExecutablePath` |
| `host/HostObjectsFile.cpp` the program's directory | `/proc/self/exe` | `_NSGetExecutablePath` (was already there) |
| `hal/host/HostSockets.cpp` a send to a reset connection | `MSG_NOSIGNAL`, `SIGPIPE` ignored | `SIGPIPE` ignored (macOS has no `MSG_NOSIGNAL`) |
| `print/host/dnssd/HostDNSSD.cpp` printers on the network | `avahi-browse` | nothing found (the `#else` branch) until a `dns_sd.h` browse is written |
| `print/host/tls/HostTLS.cpp` ipps:// | OpenSSL | OpenSSL if CMake finds one (Homebrew's), else no ipps |
| `src/host/CMakeLists.txt` the window and the sound | X11, ALSA | neither found: no window (headless) and silent - the `win32` files' `#else` branches |

(The `SIGPIPE` row was a latent Linux bug too: a send to a connection the
other end had reset would have ended newton with `SIGPIPE`.)

## The build

The system compiler, as on Linux:

    cmake -G Ninja -S src -B build/host -DCMAKE_CXX_COMPILER=clang++
    cmake --build build/host && ctest --test-dir build/host

- Apple's `ld64` (and the newer `ld-prime`) resolves the mutually dependent
  static libraries without `--start-group`; `src/CMakeLists.txt` only adds
  the group for GNU ld (`CMAKE_CXX_COMPILER_LINKER_ID`), so nothing to do -
  but check that CMake reports `AppleClang`'s linker as not GNU.
- The case-insensitive APFS default is the Windows situation again:
  `src/intl/Locale.h` shadows `<locale.h>`, so the rule "no `<map>`,
  `<string>` or `<vector>` in Newton libraries" applies, and
  `tools.IncludeCase` keeps the includes right for Linux.
- `char` is signed on Apple's arm64 (as on x86-64), so the reconstruction's
  assumptions hold there; a Linux arm64 build would be the first with an
  unsigned `char` (`-fsigned-char` is the escape hatch if anything shows).
- Threads: a secondary thread's default stack is 512 KB on macOS, but the
  task threads are made by `TaskRuntime.cpp`'s `StartTaskThread` with a
  stack of their own, 8 MB on every POSIX host, so that does not apply.
  What does: a deleted task's thread cannot end where it stands
  (`EndThisThread`: Linux uses the raw exit system call, which on macOS
  would end the process), so on macOS it stays parked with its stack - the
  one or two threads a minute the Linux and Windows hosts no longer keep.
  The macOS answer is `__bsdthread_terminate` or a `pthread_exit` made
  safe by first switching to a small stack of its own; worth doing once
  there is a Mac to test it on.
- The Python tools are the standard library only and run as they are; the
  ROM tooling's Ghidra step is the user's own Ghidra.

## New files: the window, the sound, the network printers

Each is one implementation of an interface that already has two:

- **`host/cocoa/HostWindow.mm`** (Objective-C++, linked with `-framework
  Cocoa`) answering `host/HostWindow.h`: `HostWindowStart` (an `NSWindow`
  with an `NSView` subclass whose `drawRect:` scales the display's grays
  into a `CGImage` thirty times a second), `HostWindowStop`,
  `HostWindowPostPen`, `HostWindowPosition`/`HostWindowSetPosition` (the
  restart's), and the shims it calls back - `HostWindowPenDown/Move/Up`
  from `mouseDown:`/`mouseDragged:`/`mouseUp:`, `HostWindowKey` with the
  Windows virtual key code from `keyDown:`/`keyUp:` (a `kVK_*` table, as
  the X11 file has a keysym table), `HostWindowClosed` from
  `windowWillClose:`, and `HostWindowFileDropped` from the view's
  `NSDraggingDestination` (`performDragOperation:`, the file URLs off the
  pasteboard) - the drop the X11 window does by XDND.  The one real
  difference: AppKit wants the **main thread**, where the other two run
  their window on a thread of their own.  So on macOS newton's `main` has to
  run `OsBoot` on a second thread and give the main thread to
  `[NSApp run]` - a `HostWindowRunMain(void (*boot)(void))` in
  `HostWindow.h` that the other two implement as a plain call, and
  `newton.cpp` calling it instead of `OsBoot()` directly.  That is the only
  change outside the new file.  (Until then, XQuartz's X11 works with the
  X11 file as it is: CMake finds X11 and builds it.)
- **`host/coreaudio/HostAudio.cpp`** answering `host/HostAudio.h`: an
  `AudioQueue` (AudioToolbox) for the loudspeaker - 16-bit mono at the rate
  asked, `HostAudioPlay` copying into queue buffers, a few buffers ahead
  as waveOut and ALSA are fed - and an input `AudioQueue` for
  `HostMicrophone*` (which needs the microphone permission: an
  `NSMicrophoneUsageDescription` in an `Info.plist` once newton is a
  bundle; from a terminal, the terminal's permission).
- **`print/host/dnssd/HostDNSSD.cpp`'s macOS branch**: `DNSServiceBrowse`
  and `DNSServiceResolve` from `<dns_sd.h>` (in libSystem; the same API the
  Windows branch's `DnsServiceBrowse` imitates) for `_ipp._tcp`, filling the
  same list the other two branches fill.
- **ipps:// without OpenSSL** (optional): a `HOST_TLS_SECURETRANSPORT` or
  Network.framework branch of `HostTLS.cpp` over the same memory-buffer
  interface (`HostTLSPutReceived`/`HostTLSTakeToSend`) - Secure Transport
  is deprecated but does exactly that; with Homebrew's OpenSSL found, the
  existing branch already works.
- `src/host/CMakeLists.txt`: `elseif(APPLE)` choosing the two new files
  and linking `-framework Cocoa -framework AudioToolbox -framework
  CoreAudio`, as the Windows branch links `user32 gdi32 shell32 winmm`.

## The crash tools

- **A crash** prints the faulting instruction and the C stack as image
  offsets already (the rows above).  `tools/host/whichfunction.py` reads
  PE and ELF; macOS needs **Mach-O**: the symbol table is `LC_SYMTAB`
  (`nlist_64` entries, names in the string table, a function's end the next
  symbol's start), the offset relative to the `__TEXT` segment's vmaddr
  (the crash line's base is `dladdr`'s `dli_fbase`, the Mach header) - or
  simply `atos -o build/host/host/newton -l 0x100000000 <address>` /
  `nm -n` + `c++filt`, which every Mac has.
- **stacksample.py / profile.py**: macOS has its own sampler, `sample <pid>
  <seconds>` (a call tree of every thread, symbolised), and `spindump`;
  both need no setup for a process of one's own.  The Linux route (the
  process samples itself on a signal) does not carry over: macOS has no
  real-time signals and no way to send a signal to one thread from outside.
  So on macOS the two tools should run `sample` and summarise its output
  (the busiest thread's stacks; self and inclusive counts), a
  `tools/host/macsample.py` beside `linuxsample.py`.
- **gdb** is lldb there (`lldb -p <pid>` needs no ptrace dance for one's
  own process, given the developer tools).

## Tests

Nothing in the suite is Windows- or Linux-specific except where it says so:
`host.NewtonWindowDrop` is X11's (it builds only with X11) and skips with no
display; the tests that need ALSA or waveOut do not exist - the suite runs
the null sound backend.  A macOS window drop test would want an
`NSDraggingSource` program, as `xdnddrop` is X11's; until then the Cocoa
drop is tested by hand.  The suite should pass headless on a Mac as it is.

## Order of work, when there is a Mac

1. Configure and build with the system clang; run the suite headless (no
   window, no sound).  Fix what the compiler finds - this page's guesses.
2. The task threads' stack size, if the suite finds it short.
3. `host/cocoa/HostWindow.mm` and `HostWindowRunMain`.
4. `host/coreaudio/HostAudio.cpp`.
5. The `dns_sd.h` browse; Mach-O in whichfunction.py and a `sample`
   wrapper for stacksample/profile.
6. A `docs/host-macos.md` rewritten from plan to record, as
   `docs/host-lp64.md` is for Linux.
