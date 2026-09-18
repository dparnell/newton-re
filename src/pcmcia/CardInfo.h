/*
	File:		pcmcia/CardInfo.h

	Contains:	What a script can ask about the PCMCIA sockets and the cards
				in them.  The NewtonScript boot asks for this while it is
				setting the globals up (Rbootinitnsglobals calls
				getCardInfo), so it has to answer before anything else can
				run, card hardware or no card hardware.

	Not in the DDK's headers as a script function; reconstructed from the
	MP2100 D ROM (0x000545a4), citing its origin.  NOT YET: the card server
	itself (TCardServer, the socket states and the CIS tuples a real card
	answers with), so the sockets are always empty here - which is what the
	ROM's own code does on a machine whose gNumberOfHWSockets is zero.
*/

#ifndef __CARDINFO_H
#define __CARDINFO_H

#ifndef __FRAMES_H
#include "Frames.h"
#endif


Ref		FGetCardInfo(RefArg rcvr);			// ROM 0x000545a4 FGetCardInfo

// the card functions bound to the ROM's native function objects
void	RegisterCardNatives(void);

#endif	/* __CARDINFO_H */
