/*
	File:		pcmcia/CardPartHandler.h

	Contains:	The 'cdhl part handler (TCardPartHandler): a package's card
				handler - a protocol part implementing TCardHandler, the
				code that recognises a kind of PCMCIA card - handed to the
				card server for each hardware socket, and taken back when
				the package goes.  The ROM's card server makes and
				registers it in its own world (TCardServer::MainConstructor).

				Reconstructed from the MP2x00 US ROM (0x0004feb4-0x000500b0).
				NOT YET: the card server itself (TCardServer,
				AddCardHandler, RemoveCardHandler).  The host has no card
				sockets (gNumberOfHWSockets 0), so a handler for every
				socket is a handler for none, as it would be on such a
				machine; and a part's class info is ARM, which the host
				cannot run - it is read where it lies (packages/
				ROMClassInfo.h) and reported.
*/

#ifndef __CARDPARTHANDLER_H
#define __CARDPARTHANDLER_H

#ifndef __PARTHANDLER_H
#include "PartHandler.h"
#endif

class TCardServer;

// 0x3c bytes in the ROM: a TPartHandler and the card server.
class TCardPartHandler : public TPartHandler
{
public:
					TCardPartHandler();
	NewtonErr		Init(ULong type, char* unused, TCardServer* server);
	virtual	NewtonErr	Install(const PartId& partId, SourceType sourceType, PartInfo* partInfo);
	virtual	NewtonErr	Remove(const PartId& partId, PartType partType, RemoveObjPtr removePtr);

	TCardServer*	fCardServer;			// +0x38
};

extern ULong	gNumberOfHWSockets;		// host: 0, no card hardware

// host (DEVIATION): the handler made and registered from the world that
// calls it, the card server's being NOT YET
void	InitCardPartHandler(void);

#endif	/* __CARDPARTHANDLER_H */
