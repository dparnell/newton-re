/*
	File:		comms/ModemNavigator.h

	Contains:	The modem navigator hook every endpoint made from a script's
				(or the docker's) options goes through: RunModemNavigator
				finds a modem service ('sid ' = 'mods) among the options
				and, if the modem set up is the Newton's own, opens a modem
				endpoint and lets the script navigator (vars.navigator's
				modemNavigator, voiceNavigator or faxNavigator) choose
				before the real endpoint is made.  Any other service is let
				through at once.  (The modem options it reads are
				ModemOptions.h's.)

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __COMMS_MODEMNAVIGATOR_H
#define __COMMS_MODEMNAVIGATOR_H

#ifndef __COMMS_OPTIONS_H
#include "Options.h"
#endif
#include "ModemOptions.h"

Boolean		UseModemNavigator(void);
NewtonErr	RunModemNavigator(TOptionArray* options);
// ROM 0x00067dec ContainsModemService__FP12TOptionArray - a 'mods service
// named and no 'mpro option: the modem navigator is to be run
Boolean		ContainsModemService(TOptionArray* options);

#endif	/* __COMMS_MODEMNAVIGATOR_H */
