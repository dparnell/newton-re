/*
	File:		comms/ModemNavigator.h

	Contains:	The modem navigator hook every endpoint made from a script's
				(or the docker's) options goes through: RunModemNavigator
				finds a modem service ('sid ' = 'mods) among the options
				and, if the modem set up is the Newton's own, opens a modem
				endpoint and lets the script navigator (vars.navigator's
				modemNavigator, voiceNavigator or faxNavigator) choose
				before the real endpoint is made.  Any other service is let
				through at once.  And the two modem options it reads (their
				declarations are not in the DDK; the names of the fields
				are ours, the layouts the ROM's).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __COMMS_MODEMNAVIGATOR_H
#define __COMMS_MODEMNAVIGATOR_H

#ifndef __COMMS_OPTIONS_H
#include "Options.h"
#endif

#define kCMOModemPrefs			'mpre'
#define kCMOModemConnectType	'mcto'

// 'mpre: the modem's preferences
class TCMOModemPrefs : public TOption
{
public:
					TCMOModemPrefs();

	Boolean			fField0C;			// +0x0c
	Boolean			fField0D;			// +0x0d  (1)
	Boolean			fField0E;			// +0x0e
	Boolean			fField0F;			// +0x0f  (1)
	Boolean			fField10;			// +0x10
	Boolean			fField11;			// +0x11  (1)
	Boolean			fField12;			// +0x12  (1)
	Boolean			fField13;			// +0x13  (1)
	Boolean			fNavigatorOpen;		// +0x14  set by the navigator's endpoint
	Boolean			fField15;			// +0x15
	ULong			fField18;			// +0x18  (19200)
	ULong			fField1C;			// +0x1c  (3)
	ULong			fField20;			// +0x20  (15)
	Boolean			fField24;			// +0x24  (1)
};

// 'mcto: what the connection is for
class TCMOModemConnectType : public TOption
{
public:
					TCMOModemConnectType();

	Boolean			fVoice;				// +0x0c
	Boolean			fFax;				// +0x0d
	Boolean			fField0E;			// +0x0e  (1)
	Boolean			fField0F;			// +0x0f
	Boolean			fField10;			// +0x10
};

Boolean		UseModemNavigator(void);
NewtonErr	RunModemNavigator(TOptionArray* options);

#endif	/* __COMMS_MODEMNAVIGATOR_H */
