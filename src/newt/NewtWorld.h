/*
	File:		newt/NewtWorld.h

	Contains:	TNewtWorld, the NewtonScript world: the application world
				('newt) the 'main' task runs (UserMain), whose MainConstructor
				boots the object system, the REP, QuickDraw, the fonts, the
				Newt port (gNewtPort), the TNewtEventHandler, the
				application (gApplication, a TARMNotebook, with its
				InitToolbox) and the package part handlers, whose PreMain
				finishes the boot (the ROM packages loaded, the extras
				initialised, the application Run, the boot test script, the
				'aliv system event, gNewtIsAliveAndWell) and whose event
				loop then dispatches the 'newt events (AEDispatch: under an
				exception handler that shows the exception, the delayed
				actions run after each).  TNewtEventHandler takes the 'newt
				events: 'idle (nothing of its own: every event ends in the application's Run, then the idle timer
				re-armed for the next delayed action), 'keyb (a keyboard
				event: HandleKeyEvent, the key repeat rates from the
				preferences replied), 'draw (a rectangle of the screen
				redrawn), 'alrm, 'card, 'stor/'rstr, 'powr/'pwch, 'scpt (a
				run-script event), 'ic  , 'irMC, 'bklt, 'ext , 'dead, 'bats,
				'xnwt.  NewtGlobals is the world's per-fork state (the
				interpreter, the current port, the temporary buffers) the
				forks switch (ForkSwitch).  The ROM's TNewtWorld is 0x94
				bytes: a TAppWorld (0x70) then a shared memory message, the
				handler and the globals.

				'xnwt and the script events from outside are
				ExternalNewtEvents.h.  NOT YET RECONSTRUCTED: the stack
				locked for the event loop (LockStack); on the host
				the object system is started by the program before the
				world (InitObjects needs the ROM image read in) and the
				screen by HostStartViews.

	Reconstructed from the MP2x00 US ROM (0x0030ba08-0x0030bc14,
	0x0030c0e0-0x0030c140, 0x0030ca08-0x0030d304, 0x0030d53c-0x0030dd14,
	0x0030ee84-0x0030eee0); each function cites its origin.
*/

#ifndef __NEWTWORLD_H
#define __NEWTWORLD_H

#include "AppWorld.h"
#include "Ports.h"
#include "objects.h"

class TNewtEventHandler;

// the 'newt event ids (fAEventID of a 'newt-class event)
enum
{
	kNewtIdleEvent			= 'idle',
	kNewtKeyboardEvent		= 'keyb',
	kNewtRedrawEvent		= 'draw',
	kNewtAlarmEvent			= 'alrm',
	kNewtCardEvent			= 'card',
	kNewtStoreEvent			= 'stor',
	kNewtStoreRemovedEvent	= 'rstr',
	kNewtPowerEvent			= 'powr',
	kNewtPowerChangeEvent	= 'pwch',
	kNewtScriptEvent		= 'scpt',
	kNewtSCPEvent			= 'scp!',
	kNewtInterConnectEvent	= 'ic  ',
	kNewtIRConnectEvent		= 'irMC',
	kNewtBacklightEvent		= 'bklt',
	kNewtExternalEvent		= 'ext ',
	kNewtDeadAdapterEvent	= 'dead',
	kNewtBatteryEvent		= 'bats',
	kNewtExternalNewtEvent	= 'xnwt',
	kNewtInkerEvent			= 'inkr'		// the inker's wake-up when a stroke changes (TInker::LCDEntry 0x002150ec): nothing but the idle pass
};

// a 'newt/'idle/'draw event: the screen rectangle to redraw (0x14 bytes)
class TRedrawScreenEvent : public TAEvent
{
public:
						TRedrawScreenEvent(const Rect& rect)	{ fAEventClass = kNewtEventClass; fAEventID = kNewtIdleEvent; fEvent = kNewtRedrawEvent; fRect = rect; }

	ULong				fEvent;			// +0x08  'draw
	Rect				fRect;			// +0x0c
};

// a 'newt/'idle/'alrm event: the system alarm (0x1c bytes).  The one the
// machine has is a member of the application (Notebook.h), because there is
// only ever the one: SetSysAlarm fills it in and hands it to the real-time
// clock, whose alarm sends it to the newt port when its second arrives, and
// HandleAlarmEvent then calls the function it carries.
class TAlarmEvent : public TAEvent
{
public:
	ULong				fEvent;			// +0x08  'alrm
	ULong				fTime;			// +0x0c  the second it was set for (the low word of the time)
	RefStruct			fArgs;			// +0x10  the arguments the function is called with
	RefStruct			fFunc;			// +0x14  the function
	RefStruct			fUnused18;		// +0x18
};

void	HandleAlarmEvent(TAlarmEvent* event);		// ROM 0x0030eee0 HandleAlarmEvent__FP11TAlarmEvent

// a 'newt/'inkr event asking the inker to turn the busy box on or off
// (0x10 bytes; the commands are 0x33 to 0x37, what BusyBoxControl's
// -2 to 2 become)
class TBusyBoxEvent : public TAEvent
{
public:
	ULong				fCommand;		// +0x08 (a ULong on the host, as the inker reads it: recognition/Inker.h's TInkerEvent)
	ULong				fUnused0c;		// +0x0c
};

const long kBusyBoxAllow = 0x35;				// BusyBoxControl(0)
const long kBusyBoxHide = 0x36;					// (0x34 shows it)
const TTimeout kBusyBoxSendTimeout = 0xa8c000;	// (the ROM's: about three seconds)

extern TUPort*	gTheInkerPort;					// ROM 0x0c101658 gTheInkerPort - the inker's, once InkerPort has looked it up (recognition/InkerNatives.cpp)
void	BusyBoxSend(long command);				// ROM 0x0030dd60 BusyBoxSend__Fl
Ref		FBusyBoxControl(RefArg rcvr, RefArg what);	// ROM 0x0030ddec FBusyBoxControl
void	RegisterBusyBoxNatives(void);

// the NewtonScript side
Ref		FSetSysAlarm(RefArg rcvr, RefArg time, RefArg func, RefArg args);	// ROM 0x0030eeec FSetSysAlarm
void	RegisterAlarmNatives(void);

// the power switch as a script sees it (system/SystemNatives.h says why
// it is here rather than with the machine's other natives)
Ref		FPowerOff(RefArg rcvr);							// ROM 0x00201b00 FPowerOff
void	RegisterPowerNatives(void);


// a 'newt/'idle/'scpt event: a method of a root variable run with a binary
// of the data (0x9c bytes); the error and the result (an integer) come
// back in it
class TRunScriptEvent : public TAEvent
{
public:
						TRunScriptEvent(const char* variable, const char* method);

	ULong				fEvent;			// +0x08  'scpt
	char				fVariable[64];	// +0x0c  the root variable's name (the frame the method is sent to)
	char				fMethod[64];	// +0x4c  the method's name
	void*				fData;			// +0x8c  the data (nil: none) - a binary of it is the argument
	long				fSize;			// +0x90
	long				fError;			// +0x94  0, -48204 (kNSErrPathFailed: no such variable), or a thrown exception's code
	long				fResult;		// +0x98  the method's answer as an integer
};

// the world's per-fork globals (0x18 bytes at TNewtWorld +0x7c; the ROM's
// gNewtGlobals points at the running fork's)
struct NewtGlobals
{
	ULong				fStackPos;		// +0x00  the interpreter's id << 16 (gCurrentStackPos while the fork runs)
	class TInterpreter*	fInterpreter;	// +0x04  the fork's interpreter (gInterpreter)
	ULong				fStackBase;		// +0x08  the task's stack (GetTaskStackInfo)
	GrafPort*			fPort;			// +0x0c  the current port (SetPort keeps it here)
	void*				fTempBuf;		// +0x10  a temporary drawing buffer (AllocNewTempBuf)
	void*				fTempBuf2;		// +0x14
};
extern NewtGlobals*	gNewtGlobals;						// ROM 0x0c1054b0 gNewtGlobals
NewtGlobals*	GetNewtGlobals(void);					// ROM 0x0030cb58 GetNewtGlobals__Fv

// a fork's own globals (ForkGlobals.cpp)
long	InitForkGlobalsForFrames(NewtGlobals* parent);	// ROM 0x002f6bb4 InitForkGlobalsForFrames__FP11NewtGlobals - an interpreter with an id of its own
void	DestroyForkGlobalsForFrames(NewtGlobals* globals);	// ROM 0x002f6cc0 DestroyForkGlobalsForFrames__FP11NewtGlobals
long	InitForkGlobalsForQD(NewtGlobals* parent);		// ROM 0x002e46ec InitForkGlobalsForQD__FP11NewtGlobals - a port and a temporary buffer
void	DestroyForkGlobalsForQD(NewtGlobals* globals);	// ROM 0x002e474c DestroyForkGlobalsForQD__FP11NewtGlobals
void	InvalidateQDTempBuf(void);						// ROM 0x0033f658 InvalidateQDTempBuf__Fv - the task has no temporary buffer (-0x400)

// the modal dialogs and script forks (ModalDialogNatives.cpp)
Ref		FModalDialog(RefArg rcvr);						// ROM 0x0030de88 FModalDialog
Ref		FFilterDialog(RefArg rcvr);						// ROM 0x0030e054 FFilterDialog
Ref		FExitModalDialog(RefArg rcvr);					// ROM 0x0030e284 FExitModalDialog
Ref		FForkScript(RefArg rcvr, RefArg fn, RefArg args);	// ROM 0x0030e2a0 FForkScript
Ref		FYieldToFork(RefArg rcvr);						// ROM 0x0030e390 FYieldToFork
void	RegisterModalDialogNatives(void);
Ref		FGetFrameStuff(RefArg rcvr, RefArg object, RefArg which);	// ROM 0x001ea21c FGetFrameStuff (DebugNatives.cpp)
void	RegisterAppDebugNatives(void);						// GetFrameStuff (DebugNatives.cpp)

class TNewtWorld : public TAppWorld			// 0x94 bytes
{
public:
	virtual ULong		GetSizeOf();							// ROM 0x0030ca00 GetSizeOf__10TNewtWorldFv
	virtual long		ForkInit(TForkWorld* parent);			// ROM 0x0030cadc ForkInit__10TNewtWorldFP10TForkWorld - the message, handler and port shared with the parent
	virtual long		ForkConstructor(TForkWorld* parent);	// ROM 0x0030cb9c ForkConstructor__10TNewtWorldFP10TForkWorld - the fork's own frames and QD globals
	virtual void		ForkDestructor();						// ROM 0x0030cbe4 ForkDestructor__10TNewtWorldFv
	virtual void		ForkSwitch(Boolean in);					// ROM 0x0030cc20 ForkSwitch__10TNewtWorldFUc - the globals of the fork switched in or saved
	virtual long		MainConstructor();						// ROM 0x0030d20c MainConstructor__10TNewtWorldFv
	virtual void		TheMain();								// ROM 0x0030cb68 TheMain__10TNewtWorldFv - the event loop with the stack locked (NOT YET: LockStack)
	virtual long		AEDispatch(ULong msgType, TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x0030cc3c AEDispatch__10TNewtWorldFUlP10TUMsgTokenPUlP7TAEvent
	virtual long		PreMain();								// ROM 0x0030cd28 PreMain__10TNewtWorldFv - ==> 0, or the boot test script's error
	virtual TForkWorld*	MakeFork();								// ROM 0x0030caa4 MakeFork__10TNewtWorldFv - a new TNewtWorld

	TUSharedMemMsg*		fMessage;		// +0x70  the message the alarms are sent with
	ULong				fUnused74;		// +0x74
	TNewtEventHandler*	fHandler;		// +0x78
	NewtGlobals			fGlobals;		// +0x7c
};

class TNewtEventHandler : public TAEventHandler		// 0x14 bytes
{
public:
						TNewtEventHandler();					// ROM 0x0030c0e0 __ct__17TNewtEventHandlerFv
	virtual void		AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x0030d620 AEHandlerProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void		AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x0030bba4 AECompletionProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent (nothing)
	virtual void		IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x0030d53c IdleProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent - the idle timer's: an 'idle event handled under the exception handler

	void				SetWakeupTime(ULong ticks);				// ROM 0x0030dc50 SetWakeupTime__17TNewtEventHandlerFUl - the idle timer re-armed for the earliest of the application's next idle, ticks from now (when not 0) and the next delayed action
};

extern TUPort*			gNewtPort;							// ROM 0x0c1054a8 gNewtPort - the world's port
extern ULong			NewtAlarmName;						// ROM 0x0c10551c NewtAlarmName - the world's real-time clock alarm
extern Boolean			gNewtIsAliveAndWell;				// ROM 0x0c105510 gNewtIsAliveAndWell (views/Application.cpp)
extern TTime			gLastWakeupTime;					// ROM 0x0c104c4c gLastWakeupTime
extern TTime			gTickleTime;						// ROM 0x0c100d04 gTickleTime - the last user activity (the 'ext / 'bklt events)
extern TTime			gLastIOEvent;						// ROM 0x0c100d0c gLastIOEvent - the last event that came in while vars.ioBusy was set
extern TTime			gLastPenupTime;						// ROM 0x0c100d14 gLastPenupTime - the stroke world's last pen-up, as a time
extern Boolean			gGoingToSleep;						// ROM 0x0c105520 gGoingToSleep

void	RunDelayedActionProcs(void);						// ROM 0x0030ca08 RunDelayedActionProcs__Fv - up to ten delayed actions run (the root view updated and the application idled after each), the idle timer re-armed
void	CheckForDeferredActions(void);						// ROM 0x0030c120 CheckForDeferredActions__Fv - the idle timer re-armed for the next delayed action (within a tick)
void	HandleRedrawEvent(TRedrawScreenEvent* event);		// ROM 0x0030ee84 HandleRedrawEvent__FP18TRedrawScreenEvent - the rectangle invalidated and the root view updated
void	HandleRunScriptEvent(TRunScriptEvent* event);		// ROM 0x0030b5b4 HandleRunScriptEvent__FP15TRunScriptEvent - root.variable:method(data) run, the error or result kept in the event

// host: what the world's MainConstructor runs in place of the ROM's
// InitObjects/InitGraf/InitFonts when set - the program's boot of the
// object system (the ROM image read in), the screen, the fonts and its
// natives (host/HostViews.h: HostBootNewtWorld); nil to do the ROM's
extern void	(*gNewtHostBoot)(void);
// host: the boot test script's path (the ROM's PreMain runs the file
// "bootTestScript" when there is one); nil for none
extern const char*	gNewtBootTestScript;
// host: what PreMain runs once the boot is done, before the boot test
// script - the program's own globals (host/HostPackages.h); nil for none
void	AllocateEarlyStuff(void);								// ROM 0x0030d19c AllocateEarlyStuff__Fv - the locale's sorting table made the default
extern void	(*gNewtHostPreMain)(void);
// host: what MainConstructor runs just before the tablet's calibration is
// read back from the System soup (the Host panel's screen size: a
// calibration kept at another display size reset to the factory one,
// host/HostSettings.h); nil for nothing
extern void	(*gNewtHostBeforeCalibration)(void);
void	NewtUserMain(void);									// ROM 0x0030bba8 UserMain__Fv - the 'main' task: a TNewtWorld made and run (installed as the loader's gHostUserMain by NewtInstallUserMain)
void	NewtInstallUserMain(void);							// host: the loader's 'main' task runs NewtUserMain

#endif	/* __NEWTWORLD_H */
