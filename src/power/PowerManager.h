/*
	File:		power/PowerManager.h

	Contains:	The power manager and the machine's sleep.

				The power manager is the 'pg&e world (TPowerManager, started
				by InitPowerManager; its port is gPowerPort).  Two kinds of
				message come to it:
				  - the power switch and the backlight button, which the
				    platform's interrupt (or the keyboard's power key) sends
				    as {'newt, 'pg&e, 'powr} or {..., 'bklt}
				    (SendPowerSwitchEvent).  'powr tells every system event
				    handler that the power is going off ('ppen) and asks the
				    application to go to sleep ({'newt, 'idle, 'powr} to
				    gNewtPort, whose handler runs the root's GotoSleep); if
				    the application does not answer within ten seconds it is
				    taken to be hung and the machine is powered off and
				    rebooted.  'bklt turns the backlight over and tells the
				    application ({'newt, 'idle, 'bklt});
				  - the batteries' readings, asked by RPC as a TPowerEvent
				    with a command: 4 a battery's status as last read, 5 a
				    fresh one, 6 how many batteries, 7 what cells one holds -
				    answered through the battery driver (BatteryDriver.h).
				The machine's sleep itself is CyclePower, which the
				NewtonScript PowerOff comes to (system/SystemNatives.h's
				SleepUntilNextWakeup): every system event handler told
				'pwof, the battery, the screen and the tablet shut down,
				the machine turned off over hal/Power.h until something
				wakes it, and all of it brought back, 'pwon telling what
				woke it.

				Not in the DDK.  Reconstructed from the MP2x00 US ROM
				(0x001924d4-0x00192f7c).  TPowerManager is 0xe4 bytes there.
*/

#ifndef __POWERMANAGER_H
#define __POWERMANAGER_H

#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif
#ifndef __SYSTEMEVENTS_H
#include "SystemEvents.h"
#endif
#ifndef __AEVENTS_H
#include "AEvents.h"
#endif
#ifndef __HAL_POWER_H
#include "hal/Power.h"
#endif

#define kPowerManagerID			'pg&e'
#define kPowerSwitchEvent		'powr'		// the power switch
#define kBacklightEvent			'bklt'		// the backlight button

// The power manager's commands (TPowerManagerEvent::fCommand); anything
// else is a switch event, handed to DoCommand
enum
{
	kPowerCmdStatus			= 4,		// a battery's status as last read
	kPowerCmdRawStatus		= 5,		// ... read afresh
	kPowerCmdCount			= 6,		// how many batteries
	kPowerCmdSetType		= 7			// what cells a battery holds
};

// What comes to the power manager: {'newt, 'pg&e} then the command (or
// the switch event's type) and its arguments; the reply is the same block
// with the result in fCommand (+08) or fWhich (+0C: the count, a type's
// result) and the status after it.
struct TPowerManagerEvent : public TAEvent
{
	ULong			fCommand;			// +08 a kPowerCmd..., or 'powr/'bklt; the result on the way back
	ULong			fWhich;				// +0C which battery; the count on the way back
	union
	{
		ULong				fType;		// +10 the kBattery... kind to set
		PowerPlantStatus	fStatus;	// +10 the 0x34-byte status
	};
};

// How much of it each command sends and is answered with: the ROM's 0x10,
// 0x14 and 0x44.  DEVIATION: the host's sizes, its words being wider.
const ULong	kPowerEventSize			= sizeof(TAEvent) + 2 * sizeof(ULong);
const ULong	kPowerEventTypeSize		= kPowerEventSize + sizeof(ULong);
const ULong	kPowerEventStatusSize	= kPowerEventSize + sizeof(PowerPlantStatus);

// What the power manager sends the application: {'newt, 'idle} and 'powr
// or 'bklt
struct TPowerNewtEvent : public TAEvent
{
	ULong			fType;				// +08
};

class TPowerManager;

class TPowerEventHandler : public TAEventHandler
{
public:
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);		// ROM 0x00192bac AEHandlerProc__18TPowerEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x00192c7c AECompletionProc__18TPowerEventHandlerFP10TUMsgTokenPUlP7TAEvent
};

class TPowerManager : public TAppWorld
{
public:
					TPowerManager();												// ROM 0x00192cac __ct__13TPowerManagerFv
	virtual			~TPowerManager();												// ROM 0x00192d44 __dt__13TPowerManagerFv
	virtual ULong	GetSizeOf();													// ROM 0x00192dc4 GetSizeOf__13TPowerManagerFv
	virtual long	MainConstructor();												// ROM 0x00192dcc MainConstructor__13TPowerManagerFv
	virtual void	MainDestructor();												// ROM 0x00192edc MainDestructor__13TPowerManagerFv

	void			DoCommand(TUMsgToken* token, ULong* size, TPowerManagerEvent* event);	// ROM 0x00192ee0 DoCommand__13TPowerManagerFP10TUMsgTokenPUlP18TPowerManagerEvent
	void			DoReply(TUMsgToken* token, ULong* size, TPowerManagerEvent* event);	// ROM 0x001929ac DoReply__13TPowerManagerFP10TUMsgTokenPUlP18TPowerManagerEvent
	void			BacklightMessage(void);											// ROM 0x001929c0 BacklightMessage__13TPowerManagerFv
	void			PowerOffMessage(void);											// ROM 0x00192a8c PowerOffMessage__13TPowerManagerFv
	void			PowerOffTimeout(void);											// ROM 0x00192b6c PowerOffTimeout__13TPowerManagerFv

	TPowerEventHandler	fHandler;			// +70
	TPowerNewtEvent		fNewtEvent;			// +84 what the application is sent
	TUAsyncMessage		fNewtMessage;		// +90 ... and the message it goes in
	TAESystemEvent		fSysEvent;			// +A0 'ppen, to the system event handlers
	TUAsyncMessage		fSysMessage;		// +AC
	TSendSystemEvent	fSendSysEvent;		// +BC
};

extern TPowerManager*	gPowerMgr;

NewtonErr	InitPowerManager(void);												// ROM 0x0019294c InitPowerManager__Fv
TUPort*		GetPowerPort(void);													// ROM 0x0019299c GetPowerPort__Fv
NewtonErr	InitializePowerInterrupt(void);										// ROM 0x001924d4 InitializePowerInterrupt__Fv
void		SendPowerSwitchEvent(ULong type);									// ROM 0x001926b8 SendPowerSwitchEvent__FUl - 'powr or 'bklt, from an interrupt
ULong		CyclePower(void);													// ROM 0x00192764 CyclePower__Fv - ==> the power event word
long		TranslatePowerEvent(ULong event);									// ROM 0x00192904 TranslatePowerEvent__FUl - ==> a kWoke... reason (hal/Power.h)
long		PowerOffSystem(void);												// ROM 0x001925bc PowerOffSystem__Fv - the generic system call 0x44
long		PowerOnSystem(void);												// ROM 0x001925a4 PowerOnSystem__Fv

#endif	/* __POWERMANAGER_H */
