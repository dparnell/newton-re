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

				NOT YET RECONSTRUCTED: the forks (ForkConstructor and the
				fork globals - the host runs one), the package part handlers
				('form, 'book, 'dict, 'auto, 'comm), the card, battery,
				power, alarm, interconnect, IR, store, backlight and
				script-file events, LoadHighROMFramesPackages, the extras
				soup, activateStorePackages, the boot test script, the
				'aliv system event, the inker calibration, the sort tables,
				NTKInit, InitExternal, the REP's translators; on the host
				the object system is started by the program before the
				world (InitObjects needs the ROM image read in) and the
				screen by HostStartViews.

	Reconstructed from the MP2100 D ROM (0x002e66f4-0x002e6900,
	0x002e6dcc-0x002e6e2c, 0x002e76f4-0x002e7ff0, 0x002e8228-0x002e8a00,
	0x002e9b70-0x002e9bcc); each function cites its origin.
*/

#ifndef __NEWTWORLD_H
#define __NEWTWORLD_H

#include "AppWorld.h"
#include "Ports.h"

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
extern NewtGlobals*	gNewtGlobals;						// ROM 0x0c1025a4 gNewtGlobals
NewtGlobals*	GetNewtGlobals(void);					// ROM 0x002e7844 GetNewtGlobals__Fv

class TNewtWorld : public TAppWorld			// 0x94 bytes
{
public:
	virtual ULong		GetSizeOf();							// ROM 0x002e76ec GetSizeOf__10TNewtWorldFv
	virtual long		ForkInit(TForkWorld* parent);			// ROM 0x002e77c8 ForkInit__10TNewtWorldFP10TForkWorld - the message, handler and port shared with the parent
	virtual long		ForkConstructor(TForkWorld* parent);	// ROM 0x002e7888 ForkConstructor__10TNewtWorldFP10TForkWorld - the fork's own frames and QD globals (NOT YET)
	virtual void		ForkDestructor();						// ROM 0x002e78d0 ForkDestructor__10TNewtWorldFv
	virtual void		ForkSwitch(Boolean in);					// ROM 0x002e790c ForkSwitch__10TNewtWorldFUc - the globals of the fork switched in or saved
	virtual long		MainConstructor();						// ROM 0x002e7ef8 MainConstructor__10TNewtWorldFv
	virtual void		TheMain();								// ROM 0x002e7854 TheMain__10TNewtWorldFv - the event loop with the stack locked (NOT YET: LockStack)
	virtual long		AEDispatch(ULong msgType, TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x002e7928 AEDispatch__10TNewtWorldFUlP10TUMsgTokenPUlP7TAEvent
	virtual long		PreMain();								// ROM 0x002e7a14 PreMain__10TNewtWorldFv - ==> 0, or the boot test script's error
	virtual long		MakeFork();								// ROM 0x002e7790 MakeFork__10TNewtWorldFv - a new TNewtWorld (answered as the ROM does: the object itself)

	TUSharedMemMsg*		fMessage;		// +0x70  the message the alarms are sent with
	ULong				fUnused74;		// +0x74
	TNewtEventHandler*	fHandler;		// +0x78
	NewtGlobals			fGlobals;		// +0x7c
};

class TNewtEventHandler : public TAEventHandler		// 0x14 bytes
{
public:
						TNewtEventHandler();					// ROM 0x002e6dcc __ct__17TNewtEventHandlerFv
	virtual void		AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x002e830c AEHandlerProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void		AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x002e6890 AECompletionProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent (nothing)
	virtual void		IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x002e8228 IdleProc__17TNewtEventHandlerFP10TUMsgTokenPUlP7TAEvent - the idle timer's: an 'idle event handled under the exception handler

	void				SetWakeupTime(ULong ticks);				// ROM 0x002e893c SetWakeupTime__17TNewtEventHandlerFUl - the idle timer re-armed for the earliest of the application's next idle, ticks from now (when not 0) and the next delayed action
};

extern TUPort*			gNewtPort;							// ROM 0x0c10259c gNewtPort - the world's port
extern Boolean			gNewtIsAliveAndWell;				// ROM 0x0c102604 gNewtIsAliveAndWell (views/Application.cpp)
extern TTime			gLastWakeupTime;					// ROM 0x0c101d40 gLastWakeupTime
extern TTime			gTickleTime;						// ROM 0x0c100d00 gTickleTime - the last user activity (the 'ext / 'bklt events)
extern Boolean			gGoingToSleep;						// ROM 0x0c102614 gGoingToSleep

void	RunDelayedActionProcs(void);						// ROM 0x002e76f4 RunDelayedActionProcs__Fv - up to ten delayed actions run (the root view updated and the application idled after each), the idle timer re-armed
void	CheckForDeferredActions(void);						// ROM 0x002e6e0c CheckForDeferredActions__Fv - the idle timer re-armed for the next delayed action (within a tick)
void	HandleRedrawEvent(TRedrawScreenEvent* event);		// ROM 0x002e9b70 HandleRedrawEvent__FP18TRedrawScreenEvent - the rectangle invalidated and the root view updated
void	HandleRunScriptEvent(TRunScriptEvent* event);		// ROM 0x002e62a0 HandleRunScriptEvent__FP15TRunScriptEvent - root.variable:method(data) run, the error or result kept in the event

// host: what the world's MainConstructor runs in place of the ROM's
// InitObjects/InitGraf/InitFonts when set - the program's boot of the
// object system (the ROM image read in), the screen, the fonts and its
// natives (host/HostViews.h: HostBootNewtWorld); nil to do the ROM's
extern void	(*gNewtHostBoot)(void);
// host: the boot test script's path (the ROM's PreMain runs the file
// "bootTestScript" when there is one); nil for none
extern const char*	gNewtBootTestScript;
void	NewtUserMain(void);									// ROM 0x002e6894 UserMain__Fv - the 'main' task: a TNewtWorld made and run (installed as the loader's gHostUserMain by NewtInstallUserMain)
void	NewtInstallUserMain(void);							// host: the loader's 'main' task runs NewtUserMain

#endif	/* __NEWTWORLD_H */
