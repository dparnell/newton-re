/*
	File:		pcmcia/CardPartHandler.cpp

	Contains:	The 'cdhl part handler (CardPartHandler.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CardPartHandler.h"
#include "ROMClassInfo.h"
#include "CardServerGlobals.h"
#include "OSErrors.h"
#include "CardServer.h"
#include "ProtocolStandIns.h"
#include "Protocols.h"

#include <stdio.h>
#include <string.h>


// what a card handler part is removed with (0xc bytes)
struct CardPartRemoveObject
{
	UByte		fFormat;				// +0x00  the source's
	UByte		fDeviceKind;			// +0x01
	UByte		fDeviceNumber[2];		// +0x02  big-endian: the socket, for a card's own handler
	void*		fClassInfo;				// +0x08 (host: the part's class info as it lies)
};


// ROM 0x0004feb4 __ct__16TCardPartHandlerFv
TCardPartHandler::TCardPartHandler()
	:	fCardServer(nil)
{ }


// ROM 0x0004fef4 Init__16TCardPartHandlerFUlPcT1
// TPartHandler::Init (inline in the ROM) with the card server kept.
NewtonErr
TCardPartHandler::Init(ULong type, char* /*unused*/, TCardServer* server)
{
	fCardServer = server;
	return TPartHandler::Init(type);
}


// The part's class info when it is one the host can use: its stand-in's,
// registered in the part's place (packages/ProtocolStandIns.h); nil when
// the part's own ARM table is all there is.
static const TClassInfo*
HostClassInfo(PartInfo* info)
{
	return IsProtocolStandIn((const void*) info->data) ? (const TClassInfo*) info->data : nil;
}


// ROM 0x0004ff30 Install__16TCardPartHandlerFRC6PartId10SourceTypeP8PartInfo
// A card handler part (autoLoad and autoCopy, implementing TCardHandler)
// added to the card server: for the socket a card's own package came
// from (a source on a card device), or else for every socket.  Anything
// else is taken without doing anything.  ==> kError_No_Memory when the
// remove object cannot be made.
// DEVIATION: a part the host has no stand-in for is ARM, which the host
// cannot run: it is read where it lies (packages/ROMClassInfo.h), said so
// and added nowhere.
NewtonErr
TCardPartHandler::Install(const PartId& /*partId*/, SourceType sourceType, PartInfo* partInfo)
{
	if (!partInfo->autoLoad || !partInfo->autoCopy)
		return noErr;
	const TClassInfo* info = HostClassInfo(partInfo);
	if (info == nil)
	{
		ROMClassInfoNames names;
		if (ReadROMClassInfo((const void*) partInfo->data, partInfo->size, &names)
		&&  strcmp(names.fInterface, "TCardHandler") == 0)
			fprintf(stderr, "[pcmcia] card handler %s: ARM code with no host stand-in; not added\n", names.fImplementation);
		return noErr;
	}
	if (strcmp(info->InterfaceName(), "TCardHandler") != 0)
		return noErr;
	info->ImplementationName();			// (the ROM asks and does nothing with it)
	if (sourceType.deviceKind == 1)
		fCardServer->AddCardHandler(sourceType.deviceNumber, info);
	else
		for (ULong socket = 0; socket < gNumberOfHWSockets; socket++)
			fCardServer->AddCardHandler(socket, info);
	CardPartRemoveObject* removeObject = new CardPartRemoveObject;
	if (removeObject == nil)
		return kError_No_Memory;
	removeObject->fFormat = sourceType.format;
	removeObject->fDeviceKind = sourceType.deviceKind;
	removeObject->fDeviceNumber[0] = (UByte) (sourceType.deviceNumber >> 8);
	removeObject->fDeviceNumber[1] = (UByte) sourceType.deviceNumber;
	removeObject->fClassInfo = (void*) info;
	SetRemoveObjPtr((RemoveObjPtr) removeObject);
	return noErr;
}


// ROM 0x00050044 Remove__16TCardPartHandlerFRC6PartIdUll
// The handler taken back from the socket(s) it was added for.
NewtonErr
TCardPartHandler::Remove(const PartId& /*partId*/, PartType /*partType*/, RemoveObjPtr removePtr)
{
	CardPartRemoveObject* removeObject = (CardPartRemoveObject*) removePtr;
	if (removeObject != nil)
	{
		const TClassInfo* info = (const TClassInfo*) removeObject->fClassInfo;
		info->ImplementationName();		// (as Install)
		if (removeObject->fDeviceKind == 1)
			fCardServer->RemoveCardHandler((removeObject->fDeviceNumber[0] << 8) | removeObject->fDeviceNumber[1], info);
		else
			for (ULong socket = 0; socket < gNumberOfHWSockets; socket++)
				fCardServer->RemoveCardHandler(socket, info);
		delete removeObject;
	}
	return noErr;
}


TCardPartHandler*	gCardPartHandler = nil;

// host (DEVIATION): the ROM's card server makes its handler in its
// MainConstructor (0x00054648) with itself as the server, as the host's
// does (CardServer.cpp); a host with no card server (nothing registered
// the OS's protocols) has the newt world make one with none, so that a
// package with a card handler part still installs - it adds the handler
// nowhere, as a machine with no sockets would
void
InitCardPartHandler(TCardServer* server)
{
	if (gCardPartHandler != nil)
		return;
	gCardPartHandler = new TCardPartHandler;
	gCardPartHandler->Init('cdhl', nil, server);
}
