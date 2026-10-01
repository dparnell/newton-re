/*
	File:		newt/NewtWorld.cpp

	Contains:	TNewtWorld, TNewtEventHandler, the 'main' task.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CardPartHandler.h"
#include "CardServer.h"
#include "NewtWorld.h"
#include "Inker.h"
#include "StorageCards.h"
#include "AlertManager.h"
#include "NewtCardEvents.h"
#include "SoundCodec.h"
#include "SystemNatives.h"
#include "TestAgent.h"		// HandleTestAgentEvent
#include "SortTables.h"		// gSortTables
#include "Locale.h"
#include "hal/Power.h"
#include "power/PowerManager.h"
#include "power/host/HostBatteryDriver.h"
#include "Notebook.h"
#include "RootView.h"
#include "Keyboard.h"
#include "Commands.h"
#include "StrokeCentral.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "REPTranslators.h"
#include "Locale.h"
#include "Fonts.h"
#include "Screen.h"
#include "SCPEvents.h"
#include "ExternalNewtEvents.h"
#include "NTK.h"
#include "CommManager.h"
#include "HostServices.h"
#include "Translators.h"
#include "RegionVars.h"
#include "Loader.h"
#include "ROMPackages.h"
#include "FramePartHandler.h"
#include "DictPartHandler.h"
#include "Librarian.h"
#include "PackageNativeCPU.h"
#include "ARMProtocols.h"
#include "ARMCardHandler.h"
#include "Soups.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Compression.h"
#include "NativeFunctions.h"
#include "Dates.h"
#include "LongTime.h"
#include "CompMath.h"
#include "UserGlobals.h"
#include "NewtonExceptions.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "Screen.h"
#include "NTK.h"
#include <string.h>

NewtGlobals*	gNewtGlobals = nil;			// ROM 0x0c1054b0 gNewtGlobals
TUPort*			gNewtPort = nil;			// ROM 0x0c1054a8 gNewtPort
TTime			gLastWakeupTime;			// ROM 0x0c104c4c gLastWakeupTime
// (gTickleTime, ROM 0x0c100d04, is defined in views/Keyboard.cpp: the key
// events below the newt world set it too)
TTime			gLastIOEvent;				// ROM 0x0c100d0c gLastIOEvent
TTime			gLastPenupTime;				// ROM 0x0c100d14 gLastPenupTime
Boolean			gGoingToSleep = false;		// ROM 0x0c105520 gGoingToSleep
void			(*gNewtHostBoot)(void) = nil;
const char*		gNewtBootTestScript = nil;
void			(*gNewtHostPreMain)(void) = nil;
static const Int64	kZero = { 0, 0 };

// the keyboard tool's reply to a 'keyb event: the repeat rates (over the
// event it sent, 0x2c bytes)
struct KeyRepeatReply
{
	AEEventClass	fAEventClass;	// +0x00
	AEEventID		fAEventID;		// +0x04
	ULong			fType;			// +0x08
	ULong			fReply;			// +0x0c  0x24
	ULong			fRepeatFrequency;	// +0x10  keyRepeatFrequency (200)
	ULong			fRepeatThreshold;	// +0x14  keyRepeatThreshold (600)
	ULong			fSixteen;		// +0x18  16
	ULong			fCommandThreshold;	// +0x1c  cmdKeyRepeatThreshold (2500)
};


// ROM 0x0030cb58 GetNewtGlobals__Fv
NewtGlobals*
GetNewtGlobals(void)
{
	return gNewtGlobals;
}


/*------------------------------------------------------------------------------
	T N e w t W o r l d
------------------------------------------------------------------------------*/

// ROM 0x0030ca00 GetSizeOf__10TNewtWorldFv
ULong
TNewtWorld::GetSizeOf()
{
	return sizeof(TNewtWorld);
}


// ROM 0x0030caa4 MakeFork__10TNewtWorldFv
// A new world of the same kind, for Fork to start.
TForkWorld*
TNewtWorld::MakeFork()
{
	return new TNewtWorld;
}


// ROM 0x0030cadc ForkInit__10TNewtWorldFP10TForkWorld
// A fork shares the parent's message, handler and port.
long
TNewtWorld::ForkInit(TForkWorld* parent)
{
	long err = TAppWorld::ForkInit(parent);
	if (err == noErr)
	{
		TNewtWorld* world = (TNewtWorld*) parent;
		fMessage = world->fMessage;
		fUnused74 = world->fUnused74;
		fHandler = world->fHandler;
	}
	return err;
}


// ROM 0x0030cb9c ForkConstructor__10TNewtWorldFP10TForkWorld
// The fork's own globals: an interpreter of its own and QuickDraw's port
// and buffers, made while its globals are the current ones.
long
TNewtWorld::ForkConstructor(TForkWorld* parent)
{
	NewtGlobals* saved = gNewtGlobals;
	// DEVIATION: the host's current port is one global, not the running
	// fork's (ForkSwitch): opening the fork's port makes it current, which
	// on the MessagePad changes only the fork's own globals.  The world
	// that forked is still running, so its port is put back.
	GrafPort* port = GetCurrentPort();
	gNewtGlobals = &fGlobals;
	long err = TAppWorld::ForkConstructor(parent);
	if (err == noErr && (err = InitForkGlobalsForFrames(saved)) == noErr)
		err = InitForkGlobalsForQD(saved);
	gNewtGlobals = saved;
	SetPort(port);
	return err;
}


// ROM 0x0030cbe4 ForkDestructor__10TNewtWorldFv
// (a world that forked becomes a fork itself, so this is also how the
// main world's task ends once a fork has taken its loop over)
void
TNewtWorld::ForkDestructor()
{
	AcquireMutex();
	DestroyForkGlobalsForFrames(&fGlobals);
	DestroyForkGlobalsForQD(&fGlobals);
	ReleaseMutex();
	TAppWorld::ForkDestructor();
}


// ROM 0x0030cc20 ForkSwitch__10TNewtWorldFUc
// Switching in: the fork's globals become the world's, its stack position
// and interpreter the current ones; out: the stack position saved.
// DEVIATION: the ROM's current port is the globals' fPort, so it follows
// gNewtGlobals by itself; the host's QuickDraw (below this library) keeps
// it in a global of its own, which is moved in and out with the rest.
void
TNewtWorld::ForkSwitch(Boolean in)
{
	if (!in)
	{
		gNewtGlobals->fStackPos = gCurrentStackPos;
		gNewtGlobals->fPort = GetCurrentPort();
		return;
	}
	gNewtGlobals = &fGlobals;
	gCurrentStackPos = fGlobals.fStackPos;
	gInterpreter = fGlobals.fInterpreter;
	if (fGlobals.fPort != nil)
		SetPort(fGlobals.fPort);
}


static void	ArmDelayedActionIdle(void);		// (the delayed actions, below)


// ROM 0x0030d19c AllocateEarlyStuff__Fv
// The locale's sortId, when it names one, made the default sorting table.
void
AllocateEarlyStuff(void)
{
	RefVar locale(GetCurrentLocale());
	Ref sortId = GetProtoVariable(locale, RSSYMsortid, nil);
	if (NOTNIL(sortId))
	{
		long id = RINT(sortId);
		if (id != 0)
			gSortTables.SetDefaultTableId(id);
	}
}


// ROM 0x0030d20c MainConstructor__10TNewtWorldFv
// The world's boot: the app world's own, the alarm message, the object
// system (host: started by the program, which reads the ROM image in
// first), the REP, QuickDraw and the fonts (host: the screen and the
// fonts are HostStartViews'), the Newt port, the event handler (an
// 'idle handler of the 'newt class with an idler not yet started), the
// application - a TARMNotebook, constructed - and the package part
// handlers, the card events, the battery check, the inker calibration,
// the sort tables; drawing started.  The compressors go in the protocol
// registry first: a store cannot write an object without them, because
// with the OS running NewCoder makes them by name through the registry
// rather than straight off their class info.
// (Host: InitExternal's work - the precedents, the word hints' handlers,
// the large objects - is done by stores/Soups.cpp's InitQueries, the
// internal store registered by host/HostStores.h.)
long
TNewtWorld::MainConstructor()
{
	gNewtGlobals = &fGlobals;
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	fMessage = new TUSharedMemMsg;
	fUnused74 = 0;
	if (fMessage == nil)
		return kError_No_Memory;
	if ((err = fMessage->Init()) != noErr)
		return err;
	TURealTimeAlarm::NewName(&NewtAlarmName);	// (0, and the slot it takes still reads as free:
												// see TRealTimeClock::NewName)
	RegisterAlarmNatives();
	RegisterPowerNatives();
	RegisterBusyBoxNatives();		// (host/HostNatives.h's RegisterAllNatives is below this library)
	RegisterModalDialogNatives();
	RegisterAppDebugNatives();
	RegisterBookNatives();
	// a package's native functions that have no host re-expression run on
	// the ARM interpreter (armcpu/PackageNativeCPU.h; host only)
	InstallPackageNativeCPU();
	// and a package's protocol parts that have no host stand-in
	// (armcpu/ARMProtocols.h, armcpu/ARMCardHandler.h)
	InstallARMProtocols();
	InstallARMCardHandlers();
	InitializeCompression();
	// DEVIATION: the ROM starts the sound manager from the loader
	// (TLoader::TheMain 0x0011401c), whose services are all NOT YET; the
	// codecs and the volume information it registers are wanted by the
	// time a script runs, so the newt world does it here instead.
	InitializeSound();
	// DEVIATION: the communications manager likewise (TLoader::TheMain
	// starts it in the ROM), with the host's own services behind it - the
	// network is the host's TCP/IP (comms/host/HostServices.h)
	InitializeCommManager();
	RegisterHostCommServices();
	// DEVIATION: the alert manager likewise (TLoader::TheMain starts it
	// before the card server, which looks for it)
	InitAlertManager();
	// DEVIATION: the power manager likewise (TLoader::TheMain starts it),
	// with the host's battery driver registered first where a machine's own
	// would be (power/host/HostBatteryDriver.h)
	HostRegisterBatteryDriver();
	InitPowerManager();
	if (gNewtHostBoot != nil)
		gNewtHostBoot();
	else
	{
		if (gHeap == nil)
			InitObjects();
		InitGraf();
		InitFonts();
	}
	InitTranslators();			// (comms/Translators.h: its flatten and stream translators NOT YET)
	NTKInit();					// ROM 0x0030d280: the REP translators registered, the REP's idler made
	if (gREPout == nil)
		HostInitREP(stdout, nil);
	// ROM 0x0030d29c: the REP's idler set for its translators.  (The ROM
	// does this between InitREPIn/InitREPOut and REPInit, which HostInitREP
	// makes one call; REPInit leaves the translators' idle times as they
	// are, so after it comes to the same.)
	ResetREPIdler();
	fGlobals.fInterpreter = gInterpreter;
	fGlobals.fStackPos = gCurrentStackPos;
	fGlobals.fPort = GetCurrentPort();
	// (the ROM's InitGraf does this, into the globals of the task that
	// runs it; the host's InitGraf is below the newt world)
	InvalidateQDTempBuf();
	gNewtPort = GetMyPort();
	fHandler = new TNewtEventHandler;
	fHandler->Init(kNewtIdleEvent, kNewtEventClass);
	fHandler->InitIdler((TTimeout) 0, 0, false);
	gArmDelayedActionIdleProc = ArmDelayedActionIdle;
	gApplication = new TARMNotebook;
	gApplication->Constructor();
	// the part handlers whose parts come to this world
	TPartHandler* handler = new TFormPartHandler;
	handler->Init('form');
	handler = new TBookPartHandler;
	handler->Init('book');
	handler = new TDictPartHandler;
	handler->Init('dict');
	handler = new TAutoScriptPartHandler;
	handler->Init('auto');
	handler = new TCommPartHandler;
	handler->Init('comm');
	HandleCardEvents();
	// DEVIATION: 'cdhl belongs to the card server's world, which makes its
	// part handler itself; a host with no card server has this one
	if (gCardServer == nil)
		InitCardPartHandler(nil);
	HandleTestAgentEvent();
	// the machine has enough power to go on (it sleeps again if not)
	FMinimumBatteryCheck(RefVar(NILREF));
	// the tablet's calibration read back, unless the setup assistant is
	// still to ask for it
	if (!EQRef(GetPreference(RSSYMblessedapp), RSSYMsetup))
		LoadInkerCalibration();
	AllocateEarlyStuff();
	StartDrawing(nil, nil);
	return noErr;
}


// ROM 0x0030cb68 TheMain__10TNewtWorldFv
// The event loop, with 4K of stack locked down (NOT YET: LockStack).
void
TNewtWorld::TheMain()
{
	TAppWorld::TheMain();
}


// ROM 0x0030cd28 PreMain__10TNewtWorldFv
// The boot's second half, before the loop: the strokes blocked, the wakeup
// time noted, the ROM's frames packages loaded, the extras soup marked
// initialised, the application Run, a reboot reason reported, the store
// packages activated, the card events accepted, the boot test script run
// when there is one, the 'aliv system event sent; then the system is
// alive and well, the strokes unblocked and the handler woken in a tick.
// Loading the packages forks the world (LoadHighROMFramesPackages): from
// then on the fork runs the event loop, and this task ends when PreMain
// does.
// NOT YET RECONSTRUCTED: the
// reboot reason (the gestalt), the boot test script, the
// 'aliv event.
long
TNewtWorld::PreMain()
{
	long err = 0;
	gStrokeWorld.BlockStrokes();
	gLastWakeupTime = GetGlobalTime();
	LoadHighROMFramesPackages();
	// the Extras drawer's entries for the ROM's packages are made: the
	// drawer's HandleNewHighROMPart makes one for each form part it is told
	// of while the soup's extrasState is unset
	RefVar store(GetArraySlotRef(RefVar(GetStores()), 0));
	RefVar soup(StoreGetSoup(store, RefVar(Rextrassoupname)));
	SoupSetInfo(soup, RSSYMextrasstate, RSSYMinitialized);
	gApplication->Run();
	// the packages kept on the internal store activated (the ROM's
	// ActivateStorePackages walks its "Packages" soup)
	RefVar stores(GetStores());
	RefVar internal(GetArraySlotRef(stores, 0));
	NSCallGlobalFn(RSSYMactivatestorepackages, internal);
	// DEVIATION: a host without the card server (no store file mounted
	// with the OS running) has no server for the handler to talk to
	if (gCardEventHandler != nil && gCardEventHandler->fServerPort != nil)
		gCardEventHandler->ReadyToAcceptCardEvents();
	if (gNewtHostPreMain != nil)		// host: the program's globals (HostInstallPackageGlobal)
		gNewtHostPreMain();
	if (gNewtBootTestScript != nil)		// (the ROM: a "bootTestScript" file, with the REP's output to files)
	{
		newton_try
		{
			ParseFile(gNewtBootTestScript);
		}
		newton_catch_all
		{
			ExceptionNotify(CurrentException());
			if (gREPout != nil)
				gREPout->ExceptionNotify(CurrentException());
		}
		end_try;
	}
	gNewtIsAliveAndWell = true;
	gRootView->Dirty(nil);			// DEVIATION: the splash the root drew while booting goes at the next update (the ROM's boot scripts redraw)
	gStrokeWorld.UnblockStrokes();
	fHandler->SetWakeupTime(1);
	return err;
}


// ROM 0x0030cc3c AEDispatch__10TNewtWorldFUlP10TUMsgTokenPUlP7TAEvent
// An event dispatched with the port set to the default one, under an
// exception handler: the action description set, the busy box hidden
// (0x36), the app world's dispatch, the delayed actions run, the screen
// lock released - which is what gives back the lock MainConstructor takes,
// and so lets the display be updated at all; an exception is shown (ExceptionNotify, and the REP's)
// and the idle timer re-armed.  Then the busy box allowed again (0x35),
// the port restored, the ref handles cleared.
// NOT YET RECONSTRUCTED: IncrementCurrentStackPos.
long
TNewtWorld::AEDispatch(ULong msgType, TUMsgToken* token, ULong* size, TAEvent* event)
{
	long result = 0;
	GrafPort* savedPort = GetCurrentPort();
	SetPort(&gGrafPort);
	newton_try
	{
		SetActionDescription(-8103);
		BusyBoxSend(0x36);
		result = TAppWorld::AEDispatch(msgType, token, size, event);
		RunDelayedActionProcs();
		ReleaseScreenLock();
	}
	newton_catch_all
	{
		ExceptionNotify(CurrentException());
		if (gREPout != nil)
			gREPout->ExceptionNotify(CurrentException());
		CheckForDeferredActions();
	}
	end_try;
	BusyBoxSend(0x35);
	SetPort(savedPort);
	gHeap->ClearRefHandles();
	return result;
}


/*------------------------------------------------------------------------------
	T N e w t E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x0030c0e0 __ct__17TNewtEventHandlerFv
TNewtEventHandler::TNewtEventHandler()
{ }


// ROM 0x0030bba4 AECompletionProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TNewtEventHandler::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x0030d53c IdleProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The idle timer's: the event made an 'idle one and handled as any 'newt
// event is dispatched (the default port, the exception handler, the
// delayed actions after, the busy box shown if it takes a second and
// allowed again after).
void
TNewtEventHandler::IdleProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	GrafPort* savedPort = GetCurrentPort();
	SetPort(&gGrafPort);
	newton_try
	{
		SetActionDescription(-8103);
		BusyBoxSend(0x36);
		((TNewtEvent*) event)->fType = kNewtIdleEvent;
		AEHandlerProc(token, size, event);
		RunDelayedActionProcs();
	}
	newton_catch_all
	{
		ExceptionNotify(CurrentException());
		if (gREPout != nil)
			gREPout->ExceptionNotify(CurrentException());
		CheckForDeferredActions();
	}
	end_try;
	BusyBoxSend(0x35);
	SetPort(savedPort);
	gHeap->ClearRefHandles();
}


// ROM 0x0030d620 AEHandlerProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent
// A 'newt event by its type (the word at +8): 'idle nothing here; 'keyb a
// keyboard event - the event copied, the keyboard tool replied to at
// once with the key repeat rates (the keyRepeatFrequency,
// keyRepeatThreshold and cmdKeyRepeatThreshold preferences, 200, 600
// and 2500 when unset) and the copy handled (HandleKeyEvent); 'draw a
// screen rectangle redrawn; 'ext /'bklt the tickle time noted; 'scpt a
// script run and the tickle time noted; 'alrm an alarm; 'card a card
// with no storage; 'rstr a card's stores to be unmounted, and 'stor (after
// the reply) to be mounted; 'ic   the interconnect port
// (HandleInterConnect); 'scp! a device's package (HandleSCPEvent);
// 'powr, 'pwch, 'dead, 'bats; 'irMC (the root's IRConnectRequest), 'xnwt
// (ExternalNewtEvents.h).  Every event but 'keyb and 'idle is replied to as it
// came; a 'powr event more than a second after the last wakeup runs the
// root's GotoSleep.  Then the application is Run (the idle passes and the
// root view's update) and the idle timer re-armed for the next delayed
// action (stopped when there is none).
void
TNewtEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	TNewtEvent* newtEvent = (TNewtEvent*) event;
	ULong type = newtEvent->fType;
	switch (type)
	{
	case kNewtIdleEvent:
	case kNewtInkerEvent:		// (the inker's: the application's Run below idles the strokes)
		break;
	case kNewtKeyboardEvent:
		{
			KeyboardEvent copy = *(KeyboardEvent*) event;
			KeyRepeatReply* reply = (KeyRepeatReply*) event;
			reply->fReply = 0x24;
			RefVar pref(GetPreference(RSSYMkeyrepeatfrequency));
			reply->fRepeatFrequency = ISINT(pref) ? RINT(pref) : 200;
			pref = GetPreference(RSSYMkeyrepeatthreshold);
			reply->fRepeatThreshold = ISINT(pref) ? RINT(pref) : 600;
			pref = GetPreference(RSSYMcmdkeyrepeatthreshold);
			reply->fCommandThreshold = ISINT(pref) ? RINT(pref) : 2500;
			reply->fSixteen = 16;
			SetReply(*size, event);
			if (token != nil && token->GetReplyId() != 0)
				ReplyImmed();
			HandleKeyEvent(&copy);
		}
		break;
	case kNewtRedrawEvent:
		HandleRedrawEvent((TRedrawScreenEvent*) event);
		break;
	case kNewtAlarmEvent:
		HandleAlarmEvent((TAlarmEvent*) event);
		break;
	case kNewtScriptEvent:
		HandleRunScriptEvent((TRunScriptEvent*) event);
		gTickleTime = GetGlobalTime();
		break;
	case kNewtExternalEvent:
	case kNewtBacklightEvent:
		gTickleTime = GetGlobalTime();
		break;
	case 'card':
		HandleNewCard((TNewCardEvent*) event);
		break;
	case 'pwch':				// the power coming in changed (the battery driver)
		NSCallGlobalFn(RSSYMcallpowerstatuschangefns);
		break;
	case 'dead':				// the adapter is not the right one
		NSCallGlobalFn(RSSYMbadadapteralert);
		break;
	case 'bats':				// the batteries are not the kind the machine was told
		NSCallGlobalFn(RSSYMbadbatteryalert);
		break;
	case kNewtInterConnectEvent:
		HandleInterConnect((TInterConnectEvent*) event);
		break;
	case kNewtSCPEvent:
		HandleSCPEvent((TSCPEvent*) event);
		break;
	case kNewtStoreRemovedEvent:
		StorageCardRemoved((TNewStoreEvent*) event);
		break;
	case kNewtIRConnectEvent:	// another machine is beaming at this one
		gRootView->RunScript(RSSYMirconnectrequest, RefVar(NILREF), true);
		break;
	case kNewtExternalNewtEvent:
		HandleExternalNewtEvent((TExternalNewtEvent*) event);
		break;
	default:
		break;
	}
	if (type != kNewtKeyboardEvent)
	{
		if (type != kNewtIdleEvent)
			SetReply(*size, event);
		if (token != nil && token->GetReplyId() != 0)
			ReplyImmed();
		// the power switch (the power manager's 'powr): the root's GotoSleep,
		// unless the machine is going to sleep already or woke less than a
		// second ago (the press that woke it)
		if (type == kNewtPowerEvent && !gGoingToSleep)
		{
			TTime now = GetGlobalTime();
			Int64 awake = now.time;
			CompSub(&gLastWakeupTime.time, &awake);
			TTime second(1, kSeconds);
			if (CompCompare(&awake, &second.time) > 0)
			{
				gGoingToSleep = true;
				gRootView->RunScript(RSSYMgotosleep, RefVar(NILREF), true);
				gGoingToSleep = false;
			}
		}
		if (type == kNewtStoreEvent)
			StorageCardInserted((TNewStoreEvent*) event);
	}
	gApplication->Run();
	TTime next = gApplication->NextDelayedActionTime(gApplication->fNextIdleTime);
	if (CompCompare(&next.time, &kZero) == 0)
		StopIdle();
	else
	{
		TTime now = GetGlobalTime();
		Int64 wait = next.time;
		CompSub(&now.time, &wait);
		TTime delay;
		delay.time = wait;
		long ms = (long) delay.ConvertTo(kMilliseconds);
		if (ms < 1)
			ms = 1;
		ResetIdle(ms, kMilliseconds);
	}
}


// ROM 0x0030dc50 SetWakeupTime__17TNewtEventHandlerFUl
// The idle timer re-armed for the earliest of the application's next idle
// time, the given ticks from now (when not 0), and the next delayed
// action; stopped when there is nothing to wake for.
void
TNewtEventHandler::SetWakeupTime(ULong ticks)
{
	TTime when = gApplication->fNextIdleTime;
	if (ticks != 0)
	{
		TTime soon = TimeFromNow(ticks * kMacTicks);
		if (CompCompare(&soon.time, &when.time) < 0)
			when = soon;
	}
	when = gApplication->NextDelayedActionTime(when);
	if (CompCompare(&when.time, &kZero) == 0)
	{
		StopIdle();
		return;
	}
	TTime now = GetGlobalTime();
	Int64 wait = when.time;
	CompSub(&now.time, &wait);
	TTime delay;
	delay.time = wait;
	long ms = (long) delay.ConvertTo(kMilliseconds);
	if (ms < 1)
		ms = 1;
	ResetIdle(ms, kMilliseconds);
}


/*------------------------------------------------------------------------------
	T h e   d e l a y e d   a c t i o n s
------------------------------------------------------------------------------*/

// AddDelayedAction's tail (ROM 0x00033af0, from 0x00033c18): the current
// world's idle timer re-armed for the earliest delayed action - stopped
// when there is none, else set for the time left, a millisecond at least.
// (Views/Application.h's gArmDelayedActionIdleProc; DEVIATION: layering.)
static void
ArmDelayedActionIdle(void)
{
	TNewtEventHandler* handler = ((TNewtWorld*) GetGlobals())->fHandler;
	TTime next = gApplication->NextDelayedActionTime(gApplication->fNextIdleTime);
	if (CompCompare(&next.time, &kZero) == 0)
	{
		handler->StopIdle();
		return;
	}
	TTime now = GetGlobalTime();
	Int64 wait = next.time;
	CompSub(&now.time, &wait);
	TTime delay;
	delay.time = wait;
	long ms = (long) delay.ConvertTo(kMilliseconds);
	if (ms < 1)
		ms = 1;
	handler->ResetIdle(ms, kMilliseconds);
}

// the idle timer re-armed for a time: the world's handler, the earliest of
// the application's next idle, a delay from now (when not 0) and the next
// delayed action (the tail RunDelayedActionProcs, CheckForDeferredActions
// and the handler's own share)
static void
ArmIdleTimer(TNewtEventHandler* handler, TTimeout delay)
{
	handler->SetWakeupTime(0);
	if (delay != 0)
	{
		TTime soon = TimeFromNow(delay);
		TTime when = gApplication->NextDelayedActionTime(gApplication->fNextIdleTime);
		if (CompCompare(&when.time, &kZero) == 0 || CompCompare(&soon.time, &when.time) < 0)
		{
			TTime now = GetGlobalTime();
			Int64 wait = soon.time;
			CompSub(&now.time, &wait);
			TTime d;
			d.time = wait;
			long ms = (long) d.ConvertTo(kMilliseconds);
			if (ms < 1)
				ms = 1;
			handler->ResetIdle(ms, kMilliseconds);
		}
	}
}


// ROM 0x0030ca08 RunDelayedActionProcs__Fv
// While the system is alive, up to ten delayed actions are run, the root
// view updated and the application idled after each; when actions were
// run the idle timer is re-armed for the next one - within a tick when
// ten ran (more may be due).
void
RunDelayedActionProcs(void)
{
	long left = 10;
	while (gNewtIsAliveAndWell)
	{
		if (!gApplication->RunNextDelayedAction())
			break;
		left--;
		if (left == 0)
			break;
		gRootView->Update(nil);
		gApplication->Idle();
	}
	if (left >= 10)
		return;
	TNewtWorld* world = (TNewtWorld*) GetGlobals();
	ArmIdleTimer(world->fHandler, left == 0 ? 1 : 0);
}


// ROM 0x0030c120 CheckForDeferredActions__Fv
// The idle timer re-armed for the next delayed action, within a tick.
void
CheckForDeferredActions(void)
{
	TNewtWorld* world = (TNewtWorld*) GetGlobals();
	ArmIdleTimer(world->fHandler, 1);
}


// ROM 0x0030ee84 HandleRedrawEvent__FP18TRedrawScreenEvent
// The rectangle invalidated in the root view and the view updated.
void
HandleRedrawEvent(TRedrawScreenEvent* event)
{
	TRectangularRegion rgn(event->fRect);
	gRootView->Invalidate(rgn, nil);
	gRootView->Update(nil);
}


TRunScriptEvent::TRunScriptEvent(const char* variable, const char* method)
{
	fAEventClass = kNewtEventClass;
	fAEventID = kNewtIdleEvent;
	fEvent = kNewtScriptEvent;
	memset(fVariable, 0, sizeof(fVariable));
	memset(fMethod, 0, sizeof(fMethod));
	strncpy(fVariable, variable, sizeof(fVariable) - 1);
	strncpy(fMethod, method, sizeof(fMethod) - 1);
	fData = nil;
	fSize = 0;
	fError = 0;
	fResult = 0;
}


// ROM 0x0030b5b4 HandleRunScriptEvent__FP15TRunScriptEvent
// The root view's variable named in the event (kNSErrPathFailed when
// there is none) sent the method named, with a binary of the event's
// data as the argument (nil for none); the answer, an integer, and an
// exception's error code (a message exception's data, a frames
// exception's errorCode) come back in the event.
void
HandleRunScriptEvent(TRunScriptEvent* event)
{
	event->fError = 0;
	newton_try
	{
		RefVar receiver(gRootView->GetVar(RefVar(Intern(event->fVariable))));
		if (ISNIL(receiver))
			event->fError = kNSErrPathFailed;
		else
		{
			RefVar method(Intern(event->fMethod));
			RefVar data;
			if (event->fData != nil)
			{
				data = AllocateBinary(RSSYMdata, event->fSize);
				memmove(BinaryData(data), event->fData, event->fSize);
			}
			RefVar args(MakeArray(1));
			SetArraySlotRef(args, 0, data);
			RefVar result(DoMessage(receiver, method, args));
			event->fResult = ISINT(result) ? RINT(result) : 0;
		}
	}
	newton_catch_all
	{
		Exception* exception = CurrentException();
		if (Subexception(exception->name, "evt.ex.msg"))
			event->fError = (long) (Long) exception->data;
		else if (Subexception(exception->name, "type.ref.frame"))
		{
			RefVar code(GetFrameSlotRef(**(Ref**) exception->data, RSSYMerrorcode));
			event->fError = ISINT(code) ? RINT(code) : (long) (Long) exception->data;
		}
		else
			event->fError = (long) (Long) exception->data;
	}
	end_try;
}


/*------------------------------------------------------------------------------
	T h e   b u s y   b o x

	The little box that blinks in the corner while the machine is working.
	It belongs to the inker task, which is the only thing that keeps
	drawing while a script has the processor, so turning it on and off is a
	message to the inker's port rather than a call.
------------------------------------------------------------------------------*/

// (gTheInkerPort, ROM 0x0c101658, is recognition/InkerNatives.cpp's:
// InkerPort looks the inker up and keeps it there.  Until then the busy
// box is sent nowhere, as on the Newton before the inker is asked for.
// The inker's TBusyBox shows it.)


// (BusyBoxSend itself is recognition/InkerNatives.cpp's, beside the
// inker's port, so that the views below this layer can send it.)


// ROM 0x0030ddec FBusyBoxControl
// BusyBoxControl(n): the busy box turned on or off.  The argument is one
// of -2..2 and the command sent is 0x35 away from it (kBusyBoxAllow and
// its neighbours); anything else is ignored.
Ref
FBusyBoxControl(RefArg /*rcvr*/, RefArg what)
{
	long command = RINT(what);
	if (command > -3 && command < 3)
		BusyBoxSend(command + kBusyBoxAllow);
	return NILREF;
}


void
RegisterBusyBoxNatives(void)
{
	RegisterNativeFunction("FBusyBoxControl", (void*) FBusyBoxControl, 1);
}


/*------------------------------------------------------------------------------
	T h e   s y s t e m   a l a r m

	One alarm, kept in the application (TNotebook::fAlarmEvent), on one slot
	of the real-time clock (NewtAlarmName, taken in MainConstructor).  A
	script sets it with SetSysAlarm(seconds, func, args): the second is a
	TimeInSeconds and the function is what to run when it arrives.  The
	clock's alarm interrupt sends the event to the newt port, and the event
	loop then runs the function.  The alarm the machine keeps is always the
	*next* one of the alarm soup's; choosing it is NewtonScript's
	(SetNextAlarm), which asks for this one as it goes.
------------------------------------------------------------------------------*/

ULong	NewtAlarmName = 0;			// ROM 0x0c10551c NewtAlarmName


// ROM 0x0030eee0 HandleAlarmEvent__FP11TAlarmEvent
// The alarm arrived: the function it carries, called with its arguments.
void
HandleAlarmEvent(TAlarmEvent* event)
{
	DoBlock(event->fFunc, event->fArgs);
}


// ROM 0x0030eeec FSetSysAlarm
// SetSysAlarm(time, func, args): the alarm set for the second, or only
// cleared when the time is nil.  The second is a TimeInSeconds - seconds
// from the start of 1993, in local time - while the clock counts seconds
// from 1904 in GMT, so the epoch goes back on (ClockSecondsFromScriptSeconds)
// and the time zone comes off.
// The event is the application's own, which is why setting an alarm
// replaces the one before it rather than adding to it.
Ref
FSetSysAlarm(RefArg /*rcvr*/, RefArg time, RefArg func, RefArg args)
{
	TURealTimeAlarm::ClearAlarm(NewtAlarmName);
	if (ISNIL(time))
		return NILREF;
	// (the year-2010 fix, intl/Dates.h: the seconds read back as the time
	// they stand for rather than as seconds after 1993 - the epoch comes
	// with them)
	TTime when(ClockSecondsFromScriptSeconds(RINT(time)), kSeconds);
	TTime zone(GMTOffset() + DaylightSavingsOffset(), kSeconds);
	Int64 alarm = when.time;
	CompSub(&zone.time, &alarm);
	TAlarmEvent* event = &((TNotebook*) gApplication)->fAlarmEvent;
	event->fAEventClass = kNewtEventClass;
	event->fAEventID = kNewtIdleEvent;
	event->fEvent = kNewtAlarmEvent;
	event->fTime = alarm.lo;
	event->fFunc = func;
	event->fArgs = args;
	TTime at;
	at.time = alarm;
	TURealTimeAlarm::SetAlarm(NewtAlarmName, at, gNewtPort->fId, ((TNewtWorld*) GetGlobals())->fMessage->fId,
							  event, sizeof(TAlarmEvent), 1);
	return NILREF;
}


// ROM 0x000afaac FEventPause
// EventPause(tickle): how long, in seconds, the machine has been left
// alone - which is what the power manager sleeps on.  With an argument
// that is not nil it instead marks the machine as used just now (the
// tickle) and answers 0.
//
// The moment it measures from is the latest of four: the last event that
// came in while `vars.ioBusy` was set, the last pen-up (the stroke
// world's tick count, turned into a time), the last wake-up and the last
// tickle.
static Ref
FEventPause(RefArg /*rcvr*/, RefArg tickle)
{
	TTime now = GetGlobalTime();
	ULong lastUp = gStrokeWorld.fLastUpTime;
	if (NOTNIL(GetFrameSlotRef(gVarFrame, RSSYMiobusy)))
		gLastIOEvent = now;
	TTime* latest;
	if (ISNIL(tickle))
	{
		if (lastUp != 0)
		{
			TTime ago((Ticks() - lastUp) / 60, kSeconds);
			gLastPenupTime = now;
			CompSub(&ago.time, &gLastPenupTime.time);
		}
		latest = &gLastPenupTime;
		if (CompCompare(&gLastIOEvent.time, &latest->time) > 0)
			latest = &gLastIOEvent;
		if (CompCompare(&gLastWakeupTime.time, &latest->time) > 0)
			latest = &gLastWakeupTime;
		if (CompCompare(&gTickleTime.time, &latest->time) > 0)
			latest = &gTickleTime;
	}
	else
	{
		gTickleTime = now;
		latest = &gTickleTime;
	}
	TTime since = now;
	CompSub(&latest->time, &since.time);
	return MAKEINT(since.ConvertTo(kSeconds));
}


// ROM 0x00201b00 FPowerOff
// PowerOff(): the machine asleep until something wakes it, and then put
// back the way it was.  ==> a symbol saying what woke it.
//
// It belongs with the machine's other natives (system/SystemNatives.h)
// and is here because of its last line: the time the machine woke is
// kept in this world's gLastWakeupTime, which is what holds the
// automatic power-off off until the machine has been left alone again.
// Without it the machine tries to power off over and over.
//
// The order is the ROM's, and it matters: the machine sleeps first and
// everything after that is the waking up.  There has to be enough power
// to go on (FMinimumBatteryCheck sleeps again if there is not), and the
// tablet's calibration is read back unless the blessed application is
// the setup assistant, which is still asking for it.
Ref
FPowerOff(RefArg rcvr)
{
	long reason = SleepUntilNextWakeup();
	FMinimumBatteryCheck(rcvr);
	if (!EQRef(GetPreference(RSSYMblessedapp), RSSYMsetup))
		LoadInkerCalibration();
	gLastWakeupTime = GetGlobalTime();
	switch (reason)
	{
	case kWokeSerialGPI:		return RSSYMserialgpi;
	case kWokeAlarm:			return RSSYMalarm;
	case kWokeUser:				return RSSYMuser;
	case kWokeCardLock:			return RSSYMcardlock;
	case kWokeInterconnect:		return RSSYMinterconnect;
	default:					return RSSYMbecause;
	}
}


void
RegisterPowerNatives(void)
{
	RegisterNativeFunction("FPowerOff", (void*) FPowerOff, 0);
}


void
RegisterAlarmNatives(void)
{
	RegisterNativeFunction("FSetSysAlarm", (void*) FSetSysAlarm, 3);
	RegisterNativeFunction("FEventPause", (void*) FEventPause, 1);
}


/*------------------------------------------------------------------------------
	T h e   ' m a i n '   t a s k
------------------------------------------------------------------------------*/

// ROM 0x0030bba8 UserMain__Fv
// The 'main' task: a TNewtWorld named 'newt, registered with the name
// server, with a 10K stack (host: the task's own), made and run - Init
// constructs it (MainConstructor), runs PreMain and the event loop, and
// the world is destructed when the loop ends.
void
NewtUserMain(void)
{
	TNewtWorld world;
	world.Init('newt', true, 0x2800);
}


void
NewtInstallUserMain(void)
{
	gHostUserMain = NewtUserMain;
}
