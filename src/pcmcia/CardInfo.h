/*
	File:		pcmcia/CardInfo.h

	Contains:	What a script can ask about the PCMCIA sockets and the cards
				in them.  The NewtonScript boot asks for this while it is
				setting the globals up (Rbootinitnsglobals calls
				getCardInfo), so it has to answer before anything else can
				run, card hardware or no card hardware.

	Not in the DDK's headers as a script function; reconstructed from the
	MP2x00 US ROM (0x00053ccc), citing its origin.  GetCardInfo reads the
	card server's socket states (CardServer.h); before the server has run
	- and on a machine with no sockets - there are none.  NOT YET: the
	functions that ask the server through the application's card event
	handler (CheckCardBattery, GetCardTypes, UnmountCard).
*/

#ifndef __CARDINFO_H
#define __CARDINFO_H

#ifndef __FRAMES_H
#include "Frames.h"
#endif


Ref		FourCharToSymbol(ULong code);		// ROM 0x0030c0bc FourCharToSymbol__FUl
Ref		FGetCardInfo(RefArg rcvr);			// ROM 0x00053ccc FGetCardInfo
Ref		FCheckCardBattery(RefArg rcvr);		// ROM 0x0030c980 FCheckCardBattery
Ref		FGetCardTypes(RefArg rcvr);			// ROM 0x0030c7f4 FGetCardTypes
Ref		FUnmountCard(RefArg rcvr, RefArg callback, RefArg socket);	// ROM 0x0030c6b8 FUnmountCard

// the card functions bound to the ROM's native function objects
void	RegisterCardNatives(void);

#endif	/* __CARDINFO_H */
