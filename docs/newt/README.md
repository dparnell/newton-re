# The NewtonScript world

Reverse-engineering notes on the Newton's application layer - the task
that runs NewtonScript, its boot and its event loop - and its
reconstruction in `src/newt/`.  How each fact was established is stated
with it; the reconstruction cites the ROM function each of its functions
comes from.

## The boot chain

`InitialKSRVTask` 0x002d1954 (the kernel services task) starts the
loader world (`TLoader`, 'drvl); `TLoader::TheMain` 0x00115690 starts
the drivers and the services and ends by starting the 'main' task, whose
entry is `UserMain` 0x002e6894: a `TNewtWorld` on the stack, `Init('newt,
true, 0x2800)` - the app world's Init runs `MainConstructor`, `PreMain`
and `TheMain` (the event loop) under the fork mutex, and the world is
destructed when the loop ends (`TForkWorld::TaskMain` 0x000cc...,
`src/utility/ForkWorld.cpp`).

`TNewtWorld` is 0x94 bytes: a `TAppWorld` (0x70), +0x70 a
`TUSharedMemMsg` (the alarms are sent with it: `FSetSysAlarm`
0x002e9bd8), +0x78 the `TNewtEventHandler`, +0x7c `NewtGlobals` (0x18
bytes: the fork's stack position and interpreter, its stack base, the
current QuickDraw port - `SetPort` 0x002bea14 keeps it there - and two
temporary buffers; `gNewtGlobals` 0x0c1025a4 points at the running
fork's, `ForkSwitch` 0x002e790c swaps them with `gCurrentStackPos` and
`gInterpreter`).  `MakeFork` 0x002e7790 answers a new world of 0x94
bytes; `ForkInit` 0x002e77c8 shares the message, handler and port;
`ForkConstructor` 0x002e7888 gives a fork its own interpreter
(`InitForkGlobalsForFrames` 0x002d1370) and port and buffers
(`InitForkGlobalsForQD` 0x002be96c).

`TNewtWorld::MainConstructor` 0x002e7ef8 is the NewtonScript boot:
`TAppWorld::MainConstructor` (the port, registered as "newt"), the
message, the real-time alarm name, `InitializeCompression`,
**`InitObjects`** (the object system: `docs/frames/README.md`),
`InitTranslators`, `NTKInit`, the REP (`InitREPIn`/`InitREPOut`,
`ResetREPIdler`, `REPInit`), `InitUnicode`, `InitExternal`, `InitGraf`,
`InitFonts`, `gNewtPort` (0x0c10259c) = the world's port, the
`TNewtEventHandler` for ('newt, 'idle) with an idler not started, then
**`gApplication`** = a `TARMNotebook` (`TxObject::operator new(0x3c)`,
its RefStructs at +0xc, +0x10, +0x18, +0x30, +0x34, +0x38, an event at
+0x20) and its `Constructor`, five `TPartHandler`s ('form, 'book, 'dict,
'auto, 'comm - the package parts), `HandleCardEvents`,
`HandleTestAgentEvent`, `FMinimumBatteryCheck`, `LoadInkerCalibration`
(unless the blessed app is set), `AllocateEarlyStuff` (the sort table of
the locale), `StartDrawing(nil, nil)`.

`TNewtWorld::PreMain` 0x002e7a14: the strokes blocked
(`StrokeCentral::BlockStrokes`), `gLastWakeupTime` (0x0c101d40) noted,
`LoadHighROMFramesPackages`, the extras soup's `extrasState` set to
'initialized (`SoupSetInfo`), **`gApplication->Run()`** (the first idle
passes: the boot's views built), a reboot reason reported
(`ErrorNotify`, from the gestalt 0x1000004 unless the blessed app is
'ROM), `ResetRebootReason`, `activateStorePackages`, the card event
handler told it is ready, the `bootTestScript` file run when present
(its output to `bootCOutput`/`bootScriptOutput` through a
`PHammerOutTranslator`), the 'aliv system event sent, then
`gNewtIsAliveAndWell` (0x0c102604) = 1, the strokes unblocked and
`SetWakeupTime(1)` on the handler.

## The event loop

`TAppWorld::TheMain` (`src/utility/AppWorld.cpp`) receives the 'newt
events on the port and dispatches them to the handlers by (class, id).
`TNewtWorld::AEDispatch` 0x002e7928 wraps the app world's: the stack
position incremented, the port set to `gGrafPort`, an exception handler
(`SetActionDescription(-8103)`, `BusyBoxSend(0x36)`, the dispatch,
`RunDelayedActionProcs`, `ReleaseScreenLock`; on an exception
`ExceptionNotify` and `gREPout->ExceptionNotify`, then
`CheckForDeferredActions`), `BusyBoxSend(0x35)`, the port restored, the
ref handles cleared.

`TNewtEventHandler` (0x14 bytes, a `TAEventHandler`) takes every
('newt, 'idle) event and looks at the word at +8 for what it is
(`AEHandlerProc` 0x002e830c): 'idle nothing; 'keyb a `KeyboardEvent`
from the keyboard tool - copied, the incoming event overwritten with the
reply (+0xc 0x24, +0x10 the keyRepeatFrequency preference or 200, +0x14
keyRepeatThreshold or 600, +0x18 16, +0x1c cmdKeyRepeatThreshold or
2500) and replied at once, the copy handled (`HandleKeyEvent`,
`docs/views/README.md`); 'draw `HandleRedrawEvent` 0x002e9b70 (the
rectangle at +0xc invalidated, the root view updated); 'ext  and 'bklt
`gTickleTime` (0x0c100d00) = now; 'scpt `HandleRunScriptEvent`
0x002e62a0 (a `TRunScriptEvent`, 0x9c bytes: the root variable named at
+0xc sent the method named at +0x4c with a binary of the data at
+0x8c/+0x90; the error at +0x94, the integer result at +0x98) and the
tickle time; 'alrm `HandleAlarmEvent`; 'card `HandleNewCard`; 'rstr
`StorageCardRemoved`; 'ic   `HandleInterConnect`; 'irMC the root's
`IRConnectRequest`; 'pwch `callPowerStatusChangeFns`; 'dead and 'bats the
adapter and battery alerts; 'scp! `HandleSCPEvent`; 'xnwt
`HandleExternalNewtEvent`.  Every event but 'keyb and 'idle is replied
to as it came; a 'powr event more than a second after the wakeup runs
the root's `GotoSleep` (`gGoingToSleep` 0x0c102614); 'stor
`StorageCardInserted`.  Then `gApplication->Idle()` and the idle timer
re-armed for `NextDelayedActionTime(fNextIdleTime)` in milliseconds from
now (stopped when zero).  `IdleProc` 0x002e8228 (the idle timer's) makes
the event an 'idle one and runs `AEHandlerProc` under the same wrapper
as `AEDispatch`.  `SetWakeupTime(ticks)` 0x002e893c re-arms the timer
for the earliest of the application's next idle time, ticks from now
and the next delayed action.  `RunDelayedActionProcs` 0x002e76f4 runs up
to ten delayed actions (`TApplication::RunNextDelayedAction`: one per
call), the root view updated and the application idled after each, and
re-arms the timer (within a tick when ten ran); `CheckForDeferredActions`
0x002e6e0c re-arms it within a tick.

## The notebook

`TNotebook` (`TApplication` + nothing: 0x3c bytes; `TARMNotebook` class
id 0x46, `TNotebook` 0x44) - the vtable (0x1b964): ClassID, DerivedFrom,
dtor, `TxObject::Key`, DoCommand, Constructor, Run, Idle, Quit,
InitToolbox, NeedsIdle, InitOffscreenBitmaps.  `Constructor` 0x00148350:
`TApplication::Constructor` (which runs `InitToolbox`), the root view
(`TxObject::operator new(0xa0)`, its RefStructs, `TRootView::Constructor
(Rviewroot)` - the ROM's root template, a frame of the whole system's
methods and the applications' slots), the librarian
(`TLibrarian::gLibrarian`, its library soup from the root's
`copperfield:createGetSoup`).  `InitToolbox` 0x00148680:
`InitOffscreenBitmaps` (the fork's port copied into `gGrafPort`, the
screen region and a copy of `wideHandle` at 0x0c103abc/0x0c103ac0),
`InitScriptGlobals` 0x001f3c40 (vars from `varsMapStarter`, the classes
from `initialInheritanceFrame`, `InitFormFunctions`, the slot cache, the
`gfunky` functions, `bootInitNSGlobals`), `InitInker` (a `TInker` fork
'inkr over the Newt port), the screen orientation from the
`screenOrientation` preference (else `GetGrafInfo(4)`),
`DrawSplashScreen`, the `bootSound`, `InitPrintDrivers`,
`InitFontLoader`, `InitInternationalUtils`, `TRecognitionManager::Init
(2)`, `RunInitScripts` (`bootRunInitScripts`), `InitDarkStar`.  `Run`
0x00147f68: up to ten passes of `Idle` and the root view's update while
`NeedsIdle` (an idle time set and past).  `Idle` 0x00147fd0:
`TApplication::Idle`, `TRecognitionManager::Idle` (`IdleStrokes`, the
ink compression, the controller), `TRootView::IdleViews`, the next idle
time from the recogniser's `NextIdle`.  `Quit` disposes the regions.

The notifiers: `ExceptionNotify` 0x0014842c (a 1K probe for memory,
vars.lastEx/lastExMessage/lastExError/lastExData, the port and screen
lock, `ActionErrorNotify(err, 3)`), `GetExceptionErr` 0x00148244 (out of
memory's data or kError_No_Memory, a frames exception's errorCode, a
message exception's data, else -8007), `Notify` 0x001480dc (the root's
`notify`), `ErrorNotify` 0x001480fc and `ActionErrorNotify` 0x001481a0
(`notify`/`actionNotify` with [kind, error, nil]), `SetActionDescription`
0x00148080 (vars.actionDescription).

## Reconstruction (`src/newt/`)

`NewtWorld.h` (TNewtWorld, NewtGlobals, TNewtEventHandler, the event
ids, TRedrawScreenEvent, TRunScriptEvent, RunDelayedActionProcs,
CheckForDeferredActions, HandleRedrawEvent, HandleRunScriptEvent,
`NewtUserMain` - installed as the loader's `gHostUserMain` by
`NewtInstallUserMain`) and `Notebook.h` (TNotebook, TARMNotebook, the
notifiers).  Host: `gNewtHostBoot` (a hook `MainConstructor` runs in
place of the ROM's InitObjects/InitGraf/InitFonts) lets the program
read the ROM image in and make the display (`host/HostViews.h`:
`HostConfigureNewtWorld`, `HostBootNewtWorld`); the notebook's root view
is `InitViewSystem`'s (the host's root template - the ROM's needs the
whole system), the recognition system starts at the clicks level, the
inker is the host tablet's stand-in, and the kernel heap is 32 MB (the
object heap comes out of it).  `test_Newt` boots the OS (`OsBoot`), the
loader's 'main' task runs the world over the host display, and the
kernel services task drives it through the 'newt port: 'scpt events
(a test frame's methods), a tap through the tablet idled by the world's
timer, a 'keyb event typed into a paragraph (the repeat rates replied),
a 'draw event, and a 'host/'quit event of the test's own that ends the
loop.

`host/newton.cpp` runs it all interactively: `OsBoot`, the world booted
over the host display with `--script` as the boot test script, a Win32
window (`host/win32/HostWindow.cpp`) showing the display thirty times a
second, its mouse the pen (records into the tablet buffer, read by the
host inker task `HostInkerStart` every tick - which, when a stroke
changes, wakes the event loop with an `'inkr` event on the Newt port as
the ROM's `TInker::LCDEntry` 0x002150ec does, so a click reaches the
views even while the idle timer is stopped) and its keys the keyboard
(`host/HostKeyboard.cpp`: a task sending 'keyb events to the newt port
like the ROM's keyboard tool, the keyboard connected first); closing the
window ends the run.  `--headless seconds` runs without the window
(`host.Newton` test: `demo/newton.ns`, which writes the display half a
second in through a delayed action).

## The NewtonScript boot (`frames/ScriptBoot.h`)

`InitScriptGlobals` 0x001f3c40 makes the global frames what the ROM's own
scripts expect before any of them runs.  `gVarFrame` is not grown slot by
slot: a clone of `Rvarsmapstarter` takes whatever is already in it and
then becomes it, so that the globals the ROM looks up sit in the map the
ROM built for them.  Then `vars.classes` is a clone of
`Rinitialinheritanceframe`, the ROM's own function frame (`Rgfunky`) is
added to `gFunctionFrame` and hung on `vars.functions`, and the ROM's boot
block `Rbootinitnsglobals` is run.  `RunInitScripts` 0x001f3eec runs
`Rbootruninitscripts`, which asks each installed part for its
InstallScript.  Both swallow an `evt.ex` out of the script, as the ROM
does: a boot that fails part way is better than none.

`InitFormFunctions` 0x001ef108 is a stub in this ROM and is kept as one.

`TNotebook::InitToolbox` runs `InitScriptGlobals` where the ROM runs it,
after the offscreen bitmaps, so booting `build/host/host/newton` runs the
ROM's own boot block: it takes `GetStores()[0]`, gets or creates the
System soup, defines `vars.userConfiguration` from its entry and then sets
the sort id, the LCD contrast and the system volume out of it.  The OS
comes up with one store, one soup, a real user configuration, sort id 1
and volume 4.

`TNotebook::Constructor` builds the root view from the ROM's own
`Rviewroot` (`MakeRootTemplate`, `views/BuildView.cpp`): 263 slots - the
methods the applications send to the root (`Notify`, `BlessApp`,
`CloseSlips`, `GotoSleep`), its setup scripts, and a `viewChildren` naming
the fifty-nine views a Newton boots with.  Only the visible ones are
built; the applications are opened later and have no `vVisible` until they
are.  The C view methods (`MakeViewMethods`) go under it as its `_proto`,
the ROM's copy having none, and `InitViewSystem` takes the template so the
view-only host programs keep the plain root they had.  **DEVIATION:** that
`viewChildren` is a ROM object and so read-only, and `FAddView` appends to
whatever the proto chain answers, so a script adding a view to the root
gets `kNSErrObjectReadOnly`.  Nothing in the ROM adds to the root that way
- its applications are opened, not added - but the host's demos and tests
do, so the array is copied into the clone.

Two things have to be there first.  **The compressors**:
`InitializeCompression` (`compression/Compression.h`) puts every
compressor in the protocol registry, and `TNewtWorld::MainConstructor`
calls it before `InitObjects` as the ROM does.  Without it a store cannot
write an object at all once the OS is running - `NewCoder`
(`stores/StorePipes.cpp`) makes a coder by name through the registry when
`gProtocolRegistry` is set and only falls back on the class info when it
is not, so the same code that works in `newtonscript`, which boots no OS,
threw "Couldn't create compressor" inside the 'newt world (and the abort
that followed rolled back objects the cleanup then read, which is why it
looked like `kSError_ObjectNotFound`).  **A store**: `HostMountStores`
(`host/HostStores.h`) and `RegisterAllNatives` (`host/HostNatives.h`),
both from `HostBootNewtWorld`.

**How far the boot block gets.**  Running it by hand
(`newtonscript --rom <image> -e 'call ROMConstant(bootinitnsglobals) with
()'`) is the quickest way to find the next thing to reconstruct: an
unbound native names itself on stderr rather than answering a bare error
code.  It now runs to the end, and `TNotebook::InitToolbox` goes on to
`RunInitScripts`, which asks each installed part for its InstallScript:
that is what takes the store from one soup to thirteen.  Those scripts
want `GetSerialNumber` (`system/SystemNatives.h`), `SetInkerPenSize`
(`recognition/UnitPublic.h`), `TableLookup` and `ModalState`
(`views/ViewNatives.cpp`), `IsValid` (`stores/Entries.h`), `LockScreen`
(`qd/ScreenNatives.cpp`), the sound play group (`sound/SoundSettings.h`)
and `CSInstantiate` (`system/ConfigServer.h`).  With those the boot runs
with nothing unreconstructed but `setdefaultConfig`, which wants the name
server's configuration registry (`TUConfigServer::SetDefaultConfig`).

**Where the boot stops, and why.**  `Rbootruninitscripts` does two
things: it makes the twelve soups of the ROM's soupDef table (`@548`) on
the internal store, with their initial entries - which is where the
thirteen soups come from, the System soup being the twelfth's neighbour -
and then invokes the seven init functions of `@549` one after another:
`PreSetupUserConfig`, `StartAutoFaxReceive`, `StartSniffing`,
`StartAutoCallReceive`, `SetBatteryTypes`, `CheckSerialNumber`,
`ReadPreferences`.  Neither loop is guarded, so one throw costs the rest.

`PreSetupUserConfig`, the first of them, builds the owner personae: it
asks the internal store for a soup called `"Names"` and sends `Query` to
what it gets, with a tagspec of `_ownerNames`.  The boot table does not
make a `"Names"` soup, and neither does anything else the boot does:
`"Names"` is the **Cardfile**'s own soup, the Cardfile makes it when it is
first used rather than when it installs, and the ROM extension's packages
do not install until `PreMain`, which is after this.  So the soup is nil,
`Query` is sent to nil, and the other six init functions never run.  The
second exception - index nought of an empty array - is the same script on
the idle pass afterwards.

Both ROMs are the same here, so it is not a localisation slip; what the
machine gets away with is that the internal store of a Newton which has
ever run the Names application already has the soup.  The host starts
with a store that has never been written to, and there the ROM's own
omission shows.

So the host prepares its store rather than mounting an empty one
(`host/HostStores.h`): before anything runs, the soups the ROM's own
applications keep are made on it, from the names and index lists those
applications carry (`host/FactorySoups.h`, generated from the ROM
extension's packages by `analysis/soupdefs.py` - there is exactly one,
the Cardfile's `"Names"`).  All seven init functions then run.

What is left of the two exceptions is the second one, and it belongs to
the view system rather than the store: a boot script opens a view and
then indexes `GetRoot():ChildViewFrames()[0]`, and the host's root view
has no children because its root template is the reconstruction's own
(`views/BuildView.cpp`'s `MakeRootTemplate`, a DEVIATION - the ROM's
`viewRoot` wants the whole system).  Give the root a child, as
`host/demo/newton.ns` does, and it does not happen.

NOT YET: the forks, the package part handlers, the card, battery, power,
alarm, interconnect, IR, store and backlight events, the ROM packages
and the extras soup, activateStorePackages, the boot test script, the
'aliv event, the inker calibration, the librarian, the splash screen, the
boot sound, the print drivers, the font loader, RunInitScripts, DarkStar
and the busy box.
