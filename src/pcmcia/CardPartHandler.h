/*
	File:		pcmcia/CardPartHandler.h

	Contains:	The 'cdhl part handler (TCardPartHandler): a package's card
				handler - a protocol part implementing TCardHandler, the
				code that recognises a kind of PCMCIA card - handed to the
				card server for each hardware socket, and taken back when
				the package goes.  The ROM's card server makes and
				registers it in its own world (TCardServer::MainConstructor).

				Reconstructed from the MP2x00 US ROM (0x0004feb4-0x000500b0).
				A part's class info is ARM, which the host cannot run; a
				part with a host stand-in (packages/ProtocolStandIns.h - the
				Newton Internet Enabler's TLanternCardHandler, thirdparty/nie)
				arrives with the stand-in's class info registered in its
				place and is added as the ROM adds it; any other is read
				where it lies (packages/ROMClassInfo.h), reported and added
				nowhere.
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

#include "CardServerGlobals.h"

// the handler made and registered from the world that calls it - the card
// server's (TCardServer::MainConstructor), or with none the newt world's
// (host, DEVIATION); once only
void	InitCardPartHandler(TCardServer* server);

#endif	/* __CARDPARTHANDLER_H */
