/*
	File:		newt/NewtWorld.cpp

	Contains:	TNewtWorld, TNewtEventHandler, the 'main' task.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "NewtWorld.h"
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
#include "RegionVars.h"
#include "Loader.h"
#include "UserGlobals.h"
#include "NewtonExceptions.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "Screen.h"
#include <string.h>

NewtGlobals*	gNewtGlobals = nil;			// ROM 0x0c1025a4 gNewtGlobals
TUPort*			gNewtPort = nil;			// ROM 0x0c10259c gNewtPort
TTime			gLastWakeupTime;			// ROM 0x0c101d40 gLastWakeupTime
TTime			gTickleTime;				// ROM 0x0c100d00 gTickleTime
Boolean			gGoingToSleep = false;		// ROM 0x0c102614 gGoingToSleep
void			(*gNewtHostBoot)(void) = nil;
const char*		gNewtBootTestScript = nil;
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


// ROM 0x002e7844 GetNewtGlobals__Fv
NewtGlobals*
GetNewtGlobals(void)
{
	return gNewtGlobals;
}


/*------------------------------------------------------------------------------
	T N e w t W o r l d
------------------------------------------------------------------------------*/

// ROM 0x002e76ec GetSizeOf__10TNewtWorldFv
ULong
TNewtWorld::GetSizeOf()
{
	return sizeof(TNewtWorld);
}


// ROM 0x002e7790 MakeFork__10TNewtWorldFv
// A new world of the same kind (the ROM answers the object where the base
// answers an error code; NOT YET: the forks).
long
TNewtWorld::MakeFork()
{
	return (long) (Long) new TNewtWorld;
}


// ROM 0x002e77c8 ForkInit__10TNewtWorldFP10TForkWorld
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


// ROM 0x002e7888 ForkConstructor__10TNewtWorldFP10TForkWorld
// The fork's own globals: an interpreter of its own and QuickDraw's port
// and buffers (NOT YET RECONSTRUCTED: InitForkGlobalsForFrames,
// InitForkGlobalsForQD - the host runs no forks).
long
TNewtWorld::ForkConstructor(TForkWorld* parent)
{
	NewtGlobals* saved = gNewtGlobals;
	gNewtGlobals = &fGlobals;
	long err = TAppWorld::ForkConstructor(parent);
	gNewtGlobals = saved;
	return err;
}


// ROM 0x002e78d0 ForkDestructor__10TNewtWorldFv
void
TNewtWorld::ForkDestructor()
{
	TAppWorld::ForkDestructor();
}


// ROM 0x002e790c ForkSwitch__10TNewtWorldFUc
// Switching in: the fork's globals become the world's, its stack position
// and interpreter the current ones; out: the stack position saved.
void
TNewtWorld::ForkSwitch(Boolean in)
{
	if (!in)
	{
		gNewtGlobals->fStackPos = gCurrentStackPos;
		return;
	}
	gNewtGlobals = &fGlobals;
	gCurrentStackPos = fGlobals.fStackPos;
	gInterpreter = fGlobals.fInterpreter;
}


// ROM 0x002e7ef8 MainConstructor__10TNewtWorldFv
// The world's boot: the app world's own, the alarm message, the object
// system (host: started by the program, which reads the ROM image in
// first), the REP, QuickDraw and the fonts (host: the screen and the
// fonts are HostStartViews'), the Newt port, the event handler (an
// 'idle handler of the 'newt class with an idler not yet started), the
// application - a TARMNotebook, constructed - and the package part
// handlers, the card events, the battery check, the inker calibration,
// the sort tables; drawing started.
// NOT YET RECONSTRUCTED: InitializeCompression, the real-time alarm
// name, InitTranslators, NTKInit, REPInit/ResetREPIdler, InitExternal,
// the part handlers ('form, 'book, 'dict, 'auto, 'comm), HandleCardEvents,
// HandleTestAgentEvent, FMinimumBatteryCheck, LoadInkerCalibration,
// AllocateEarlyStuff (the sort tables).
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
	if (gNewtHostBoot != nil)
		gNewtHostBoot();
	else
	{
		if (gHeap == nil)
			InitObjects();
		InitGraf();
		InitFonts();
	}
	if (gREPout == nil)
		HostInitREP(stdout, nil);
	fGlobals.fInterpreter = gInterpreter;
	fGlobals.fStackPos = gCurrentStackPos;
	fGlobals.fPort = GetCurrentPort();
	gNewtPort = GetMyPort();
	fHandler = new TNewtEventHandler;
	fHandler->Init(kNewtIdleEvent, kNewtEventClass);
	fHandler->InitIdler((TTimeout) 0, 0, false);
	gApplication = new TARMNotebook;
	gApplication->Constructor();
	StartDrawing(nil, nil);
	return noErr;
}


// ROM 0x002e7854 TheMain__10TNewtWorldFv
// The event loop, with 4K of stack locked down (NOT YET: LockStack).
void
TNewtWorld::TheMain()
{
	TAppWorld::TheMain();
}


// ROM 0x002e7a14 PreMain__10TNewtWorldFv
// The boot's second half, before the loop: the strokes blocked, the wakeup
// time noted, the ROM's frames packages loaded, the extras soup marked
// initialised, the application Run, a reboot reason reported, the store
// packages activated, the card events accepted, the boot test script run
// when there is one, the 'aliv system event sent; then the system is
// alive and well, the strokes unblocked and the handler woken in a tick.
// NOT YET RECONSTRUCTED: LoadHighROMFramesPackages, the extras soup, the
// reboot reason (the gestalt), activateStorePackages, the card events,
// the boot test script, the 'aliv event.
long
TNewtWorld::PreMain()
{
	long err = 0;
	gStrokeWorld.BlockStrokes();
	gLastWakeupTime = GetGlobalTime();
	gApplication->Run();
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


// ROM 0x002e7928 AEDispatch__10TNewtWorldFUlP10TUMsgTokenPUlP7TAEvent
// An event dispatched with the port set to the default one, under an
// exception handler: the action description set, the busy box hidden
// (0x36), the app world's dispatch, the delayed actions run, the screen
// lock released; an exception is shown (ExceptionNotify, and the REP's)
// and the idle timer re-armed.  Then the busy box allowed again (0x35),
// the port restored, the ref handles cleared.
// NOT YET RECONSTRUCTED: BusyBoxSend, ReleaseScreenLock, IncrementCurrentStackPos.
long
TNewtWorld::AEDispatch(ULong msgType, TUMsgToken* token, ULong* size, TAEvent* event)
{
	long result = 0;
	GrafPort* savedPort = GetCurrentPort();
	SetPort(&gGrafPort);
	newton_try
	{
		SetActionDescription(-8103);
		result = TAppWorld::AEDispatch(msgType, token, size, event);
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
	SetPort(savedPort);
	gHeap->ClearRefHandles();
	return result;
}


/*------------------------------------------------------------------------------
	T N e w t E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x002e6dcc __ct__17TNewtEventHandlerFv
TNewtEventHandler::TNewtEventHandler()
{ }


// ROM 0x002e6890 AECompletionProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TNewtEventHandler::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x002e8228 IdleProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The idle timer's: the event made an 'idle one and handled as any 'newt
// event is dispatched (the default port, the exception handler, the
// delayed actions after).
void
TNewtEventHandler::IdleProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	GrafPort* savedPort = GetCurrentPort();
	SetPort(&gGrafPort);
	newton_try
	{
		SetActionDescription(-8103);
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
	SetPort(savedPort);
	gHeap->ClearRefHandles();
}


// ROM 0x002e830c AEHandlerProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent
// A 'newt event by its type (the word at +8): 'idle nothing here; 'keyb a
// keyboard event - the event copied, the keyboard tool replied to at
// once with the key repeat rates (the keyRepeatFrequency,
// keyRepeatThreshold and cmdKeyRepeatThreshold preferences, 200, 600
// and 2500 when unset) and the copy handled (HandleKeyEvent); 'draw a
// screen rectangle redrawn; 'ext /'bklt the tickle time noted; 'scpt a
// script run and the tickle time noted; 'alrm an alarm; 'card, 'stor,
// 'rstr, 'powr, 'pwch, 'ic  , 'irMC, 'dead, 'bats, 'scp!, 'xnwt (NOT YET
// RECONSTRUCTED).  Every event but 'keyb and 'idle is replied to as it
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
	case kNewtScriptEvent:
		HandleRunScriptEvent((TRunScriptEvent*) event);
		gTickleTime = GetGlobalTime();
		break;
	case kNewtExternalEvent:
	case kNewtBacklightEvent:
		gTickleTime = GetGlobalTime();
		break;
	default:
		// NOT YET RECONSTRUCTED: 'alrm (HandleAlarmEvent), 'card (HandleNewCard),
		// 'ic   (HandleInterConnect), 'irMC
		// (the root's IRConnectRequest), 'dead/'bats (the alerts), 'pwch
		// (callPowerStatusChangeFns), 'rstr (StorageCardRemoved), 'scp!
		// (HandleSCPEvent), 'xnwt (HandleExternalNewtEvent)
		break;
	}
	if (type != kNewtKeyboardEvent)
	{
		if (type != kNewtIdleEvent)
			SetReply(*size, event);
		if (token != nil && token->GetReplyId() != 0)
			ReplyImmed();
		// NOT YET RECONSTRUCTED: 'powr (GotoSleep after a second awake), 'stor (StorageCardInserted)
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


// ROM 0x002e893c SetWakeupTime__17TNewtEventHandlerFUl
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


// ROM 0x002e76f4 RunDelayedActionProcs__Fv
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


// ROM 0x002e6e0c CheckForDeferredActions__Fv
// The idle timer re-armed for the next delayed action, within a tick.
void
CheckForDeferredActions(void)
{
	TNewtWorld* world = (TNewtWorld*) GetGlobals();
	ArmIdleTimer(world->fHandler, 1);
}


// ROM 0x002e9b70 HandleRedrawEvent__FP18TRedrawScreenEvent
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


// ROM 0x002e62a0 HandleRunScriptEvent__FP15TRunScriptEvent
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
	T h e   ' m a i n '   t a s k
------------------------------------------------------------------------------*/

// ROM 0x002e6894 UserMain__Fv
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
