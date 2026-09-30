/*
	File:		pcmcia/CardServerGlobals.h

	Contains:	The card server's globals the rest of the card code reads:
				the sockets there are and their objects.  The card server
				(TCardServer::MainConstructor) makes a TCardSocket for each
				socket whose Init succeeds, stopping at the first that does
				not, and counts them in gNumberOfHWSockets; before it runs -
				and on a machine with no sockets - the count is nought.

	Reconstructed from the MP2x00 US ROM.
*/

#ifndef __CARDSERVERGLOBALS_H
#define __CARDSERVERGLOBALS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TCardSocket;

const ULong	kMaxCardSockets = 4;					// the ROM's tables have four

extern ULong			gNumberOfHWSockets;						// ROM 0x0c100ab4 gNumberOfHWSockets
extern TCardSocket*		gCardSockets[kMaxCardSockets];			// ROM 0x0c105fd4 gCardSockets

#endif	/* __CARDSERVERGLOBALS_H */
