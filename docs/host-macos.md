# The macOS host

newton runs on macOS as **Newton.app**: AppKit's window, AudioToolbox's
sound, IOKit's battery, a fat binary (arm64 and x86_64) if asked for,
signed ad hoc.  First built on a Mac on 2026-10-17 (an Apple-silicon
MacBook, macOS 26, Xcode's clang); before that this page was a plan written
on Windows, and what it got wrong is under "What the first build found".

macOS is LP64 like Linux, so `docs/host-lp64.md`'s whole list applies
(`Long32`/`ULong32`, `Fixed` pinned to 32 bits, `sizeof` for ROM byte
counts).  The kernel's host runtime (`std::thread` per task, the baton),
the timers, the sockets (the POSIX branch, `SIGPIPE` ignored - macOS has no
`MSG_NOSIGNAL`), the files, the object file and everything above them needed
nothing.

## Building

    cmake -G Ninja -S src -B build/host -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build/host && ctest --test-dir build/host

The system's clang (Xcode or the Command Line Tools), Homebrew's CMake and
Ninja, Python 3 for the object file.  `build/host/host/` then has `newton`
(the program, as on every host) and `Newton.app` (the same program in a
bundle).

A **fat binary** is a build of both architectures:

    cmake -G Ninja -S src -B build/fat -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
    cmake --build build/fat

- the whole tree builds for both; the object file (`romsrc.py` running the
  fat `newtonscript`, natively) is the same bytes either way;
- `print/CMakeLists.txt` looks for no OpenSSL in a build of more than one
  architecture (Homebrew's is the machine's own architecture only, and the
  other slice's link fails), so a fat newton has no `ipps://` printing
  unless `OPENSSL_ROOT_DIR` names a universal OpenSSL - see "Left to do";
- `newton64` (`NEWTON_BUILD_NS64`) is given the same architectures and
  oldest system;
- ctest `host.NewtonRosetta` boots the Intel half under Rosetta
  (`arch -x86_64`); the rest of the suite runs the native half.  Both
  halves were seen to draw the same screen byte for byte.

`CMAKE_OSX_DEPLOYMENT_TARGET` defaults to 11.0 (`src/CMakeLists.txt`), the
first macOS on Apple silicon.  Apple's linker's "ignoring duplicate
libraries" warning (CMake names the mutually dependent libraries more than
once) is turned off; `ld64` resolves archives as lld does, so there is no
`--start-group` either.

## Newton.app

`src/host/CMakeLists.txt` (target `newton_app`, built with everything)
makes `build/host/host/Newton.app`:

| | |
|---|---|
| `Contents/MacOS/newton` | the `newton` program, unchanged |
| `Contents/Info.plist` | `src/host/cocoa/Info.plist.in`: identifier `com.matthiasm.newton-re` (`NEWTON_APP_IDENTIFIER`), version `NEWTON_APP_VERSION`, the microphone's reason, `.nspkg` documents |
| `Contents/Resources/romsrc-objects.bin` | the object file it boots: `HostObjectsFile.cpp` looks in `../Resources` on macOS |
| `Contents/Resources/Newton.icns` | `tools/host/macicon.py`: the ROM's `smallLogoBitmap` (the lightbulb) on a tile the 2x00 screen's green |

and signs it ad hoc (`codesign --force --sign -`), which is all an
Apple-silicon Mac asks of a program built on it.  ctests `host.NewtonApp`
(the bundle's program boots its own object file) and `host.NewtonAppSigned`
(`codesign --verify --strict`).  It is not notarised and has no hardened
runtime: copied to another Mac it has to be let through Gatekeeper by hand
(right-click > Open).

**Opened in the Finder** (`HostAppDefaults.h` - only the bundle's program,
only when launchd started it) it has a home of its own, named by its
identifier rather than "Newton", which NewtPlay's internal store already
uses:

- `~/Library/Application Support/com.matthiasm.newton-re/` - its working
  directory;
- `~/Library/Logs/com.matthiasm.newton-re/newton.log` - its stdout and
  stderr, appended to.

Its internal store is newton's own default for a windowed run,
`~/.local/newton/Internal.store` - the same machine newton run from a
shell finds (`--store` names another; `open -a Newton --args ...` adds
arguments).  An `Internal.store` an earlier Newton.app left in
Application Support is not moved: copy it, and the files beside it named
`Internal.store.*`, into `~/.local/newton/` to keep that machine.

Run from a shell (`Newton.app/Contents/MacOS/newton ...`) it is plain
newton.

**Packages** install by dropping them on the window, by File > Install
Package... (Cmd-O), and by opening a `.nspkg` in the Finder.  A `.pkg` is
macOS Installer's whatever an application declares, so the app opens
`.nspkg` files only.  The `.nspkg` type is newton-framework's
(`org.newton-framework.newton-package`, which NewtPlay exports): Newton.app
imports it under that identifier rather than declaring another, which left
a `.nspkg` NewtPlay's type and Newton.app's document type unmatched.  Which
of the two opens a `.nspkg` on a double-click is macOS's choice (Get Info >
Open with > Change All); `open -a Newton x.nspkg` always works.  A package
opened as the application starts waits in the queue until the Newton is
up.

**Restart** (the machine's Reboot): the Cocoa window replaces the process
(`execv`) instead of starting a second one beside it, which would be a
second application with its own Dock icon (`HostRestart.cpp`,
`HostWindowOwnsProcess`).

## The window: host/cocoa/HostWindow.mm

AppKit must have the process's main thread, where the Windows and X11
windows run on a thread of their own.  `newton.cpp`'s `main` therefore hands
the machine - `OsBoot` and everything after it - to
`HostWindowRunMachine(RunMachine, windowed)` (`HostWindow.h`): the other
windows simply call it; the Cocoa one starts a thread for it (with the main
thread's stack size: the boot context becomes the idle task, and
`GetStackBounds` reads that thread's stack) and runs `[NSApp run]` on the
main thread until the machine has ended.  `HostWindowStart`/`Stop`, which the
machine calls, have the main thread open and close the window
(`dispatch_sync`).  The main thread calls `HostWindowThreadStarted` first,
so it is an alien thread to the kernel: the pen, the keys and the drops are
plain memory writes, as the other windows' threads' are.

- **The display**: each thirtieth of a second, if the display's bytes (or
  the palette) changed, they are copied and made a `CGImage` in an indexed
  colour space (the grays, 0 white, or `HostWindowSetPalette`'s colours) and
  given to the view's layer, which scales it nearest-neighbour - sharp on a
  Retina screen.  The window can be resized, its shape kept.
- **The pen**: the mouse, in eighths of a display pixel
  (`HostWindowPenDownFine`); a click into an inactive window is a tap.
- **The keys**: a Mac key code *is* the ADB code the Newton keyboard sends,
  so the table is `HostKeyboard.cpp`'s the other way round (by position);
  Command and Control are the Newton's Command, Caps Lock a press and a
  release each time it locks or unlocks, F11 the backlight, F12 the power
  switch, repeats dropped (the Newton repeats its own).  Cmd-Q, Cmd-W,
  Cmd-H, Cmd-M and Cmd-O are the menus'.
- **Quit, closing the window, logging out**: the run is ended as on the
  other hosts (`HostWindowClosed`); Quit waits (`NSTerminateLater`) until the
  machine has ended.
- `NEWTON_WINDOW_SNAPSHOT=<file>` writes, as the window closes, the image
  the window last showed, drawn back through a gray context as a PGM.
  ctests `host.NewtonWindowShows` and `.compare`
  (`src/host/demo/windowshow.ns`): it is byte for byte the OS's own
  `ScreenSnapshot`.  `host.NewtonWindowPen` drives the pen through the window.

X11 through XQuartz is still there: `-DNEWTON_HOST_WINDOW=x11` builds the
X11 window and ALSA's place is taken by no sound.

## Sound: host/coreaudio/HostAudio.cpp

The loudspeaker is an output audio queue, 16-bit mono at the Newton's 21600
Hz (the queue converts), refilled on its own thread from a ring a fifth of a
second long; `HostAudioPlay` never waits (what does not fit is dropped, as
ALSA's full buffer drops it) and what the queue finds missing is silence.

The microphone is an input queue filling a ring that `HostMicrophoneRecord`
takes from.  It is started by the first record, not by
`HostMicrophoneOpen`: macOS asks the user for the microphone the first time
a program listens, and newton opens the microphone at every start - the
question should come when the Newton records.  (`Info.plist` has the reason
macOS shows; run from Terminal, Terminal's permission is asked for.)

## The battery: power/host/iokit/HostBatteryIOKit.cpp

IOKit's power sources (the first internal battery present).  A file - and a
library, `power_host_iokit` - of its own, compiled without the Newton
include paths: CoreFoundation brings `MacTypes.h`, whose `Point`, `Rect`,
`Byte`, `Debugger` and `DebugStr` are the DDK's names too.  (The page's plan
had put it in `HostBattery.cpp`, which then did not compile.)

## What the first build found

1. **`GenericSWI` on Apple's arm64.**  The DDK declares
   `extern "C" long GenericSWI(...)`; the host's stub
   (`os600/user/host/GenericSWIStub.cpp`) is `GenericSWI(ULong selector,
   ...)`.  Where variadic arguments travel as named ones do (x86-64,
   Windows, Linux's arm64) the two agreed by accident; Apple's arm64 ABI
   passes *every* variadic argument on the stack, so the stub read its
   selector from `x0`, which held nothing, every memory-object request
   failed quietly and the boot fell over in `TTask::Init` on a nil
   environment.  `sync_ddk_headers.py` now names the selector in the
   declaration.  `CodeProcPtr` (`protocols/Protocols.h`,
   `void* (*)(int selector, ...)`) is the same shape; nothing on the host
   calls through one with arguments yet - a host hunk implementing it must
   be variadic too.
2. **The battery's IOKit headers** (above).
3. **A use-after-free in `armcpu`** (`PackageNativeCPU.cpp`): an ARM throw
   out to the host was named by the native world's own buffer, which
   `RunPackageNativeOnCPU`'s cleanup deletes on the way out - then the
   catcher read the name.  Linux's and Windows' allocators left the freed
   bytes alone; macOS's writes into a freed block, and
   `armcpu.PackageNativeCPU`'s exception arrived named "ck_s_$" and was
   unhandled.  Exception names thrown to the host are now kept for the
   process.
4. **`int`s read as `ULong` from a variadic** - undefined, and on Apple's
   arm64 an `int`'s eight-byte stack slot may have anything in its upper
   half: `UserEnvironment.cpp` passed GenericSWI an `int` of flags (cast
   now), and `TUSemaphoreOpList::Init` read its `MAKESEMLISTITEM` ints as
   `ULong`s (it reads 32-bit words now; a garbage upper half would have made
   `OpSemaphore` skip the op).  The compiler happened to store those
   constants as whole words, so neither was seen to go wrong.
5. **AddressSanitizer** (a build with `-fsanitize=address`, the boot,
   the window, the pen) found two more host bugs: `TCardServer::
   MainConstructor` called `LockPtr` on a socket the host's `operator new`
   made, which reads a heap block's header from before a block that has
   none (the ROM's `operator new` is the pointer heap's; DEVIATION, left
   out - host memory is never paged out); and `OsBoot`'s boot task was
   destroyed after its environment at the end of a run (the boot task now
   lets go of it before `OsBoot` returns - declaring the two the other way
   round instead had `~TTask` delete the stack environment as its last
   user).  Both now run clean under ASan.
6. **`pthread_create` on macOS stores the new thread's id after the thread
   has started.**  `TaskRuntime.cpp`'s `StartTaskThread` handed it
   `&t->fThread`, where `t` is the `TaskThread` the new thread copies and
   deletes: when the new thread ran first, `pthread_create` then wrote into
   a freed block, and macOS 26's allocator stopped newton at the next
   `new TaskThread` - "BUG IN CLIENT OF LIBMALLOC: memory corruption of
   free block" (`~/Library/Logs/DiagnosticReports/newton-*.ips`).  It
   showed only under load - one run in tens alone, several per suite at
   `ctest -j8` - and AddressSanitizer did not see it.  A twelve-way test
   of `pthread_create` alone had the child run first, and the id written
   after it, in about 2.5% of creations.  The id now goes into a variable
   of `StartTaskThread`'s own and the thread takes its own
   (`pthread_self`).  Linux never showed it.
7. `ld` warns of duplicate libraries (off), `sprintf` is deprecated (93
   warnings, left), Homebrew's libX11 would have been found (the Cocoa
   window is now the default).

The suite, headless and windowed, passes on arm64 but for
`host.NewtonAlignPen.restart`, which fails in a RelWithDebInfo build on
every host (`docs/next-steps.md`).

## The crash tools

- A crash prints the faulting instruction and the C stack as image offsets
  (`uc_mcontext->__ss.__pc`/`__rip`).  `tools/host/whichfunction.py` reads
  PE and ELF, not Mach-O; on a Mac `atos` does it:
  `atos -o build/host/host/newton -l 0x100000000 0x100000000+<offset>...`.
- `lldb` works on one's own processes with the developer tools; `sample
  <pid> <seconds>` is macOS's sampler (the Linux self-sampler, a real-time
  signal, does not exist here).

## Left to do

- **ipps:// in a fat build**: a TLS branch of `print/host/tls/HostTLS.cpp`
  over Apple's own TLS (Network.framework, or Secure Transport over the
  existing memory-buffer interface) instead of OpenSSL - which would also
  free the arm64 build of Homebrew's OpenSSL dylibs, a path that exists
  only on a Mac with Homebrew.
- **Network printers**: `print/host/dnssd/HostDNSSD.cpp`'s macOS branch
  (`DNSServiceBrowse`/`DNSServiceResolve` from `<dns_sd.h>`), today the
  `#else` that finds none.
- **A deleted task's thread** is parked with its stack rather than ended
  (`TaskRuntime.cpp`'s `EndThisThread`: Linux's raw exit would end the whole
  process here).
- **The host's fonts** (`HostFontRaster.h`): GDI on Windows, none here;
  Core Text would do it.
- **Mach-O in `whichfunction.py`**, and a `macsample.py` beside
  `linuxsample.py` running `sample` for `stacksample.py`/`profile.py`.
- **Distribution**: a Developer ID, the hardened runtime (with the
  `com.apple.security.device.audio-input` entitlement) and notarisation -
  not wanted for now; ad-hoc signing is.
