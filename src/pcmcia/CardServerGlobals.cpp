/*
	File:		pcmcia/CardServerGlobals.cpp

	Contains:	The card server's globals (CardServerGlobals.h).
*/

#include "CardServerGlobals.h"

ULong			gNumberOfHWSockets = 0;					// ROM 0x0c100ab4 gNumberOfHWSockets
TCardSocket*	gCardSockets[kMaxCardSockets];			// ROM 0x0c105fd4 gCardSockets
