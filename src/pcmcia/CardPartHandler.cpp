/*
	File:		pcmcia/CardPartHandler.cpp

	Contains:	The 'cdhl part handler (CardPartHandler.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CardPartHandler.h"
#include "ROMClassInfo.h"
#include "CardServerGlobals.h"
#include "OSErrors.h"

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


// Whether the part's class info (as it lies: ROMClassInfo.h) says it
// implements TCardHandler; its implementation's name in *name.
static Boolean
IsCardHandler(PartInfo* info, const char** name)
{
	ROMClassInfoNames names;
	if (!ReadROMClassInfo((const void*) info->data, info->size, &names))
		return false;
	*name = names.fImplementation;
	return strcmp(names.fInterface, "TCardHandler") == 0;
}


// ROM 0x0004ff30 Install__16TCardPartHandlerFRC6PartId10SourceTypeP8PartInfo
// A card handler part (autoLoad and autoCopy, implementing TCardHandler)
// added to the card server: for the socket a card's own package came
// from (a source on a card device), or else for every socket.  Anything
// else is taken without doing anything.  ==> kError_No_Memory when the
// remove object cannot be made.
// NOT YET: AddCardHandler (the card server); with no sockets, a package
// from anywhere but a card adds it nowhere, as the ROM would.
NewtonErr
TCardPartHandler::Install(const PartId& /*partId*/, SourceType sourceType, PartInfo* partInfo)
{
	if (!partInfo->autoLoad || !partInfo->autoCopy)
		return noErr;
	const char* name = nil;
	if (!IsCardHandler(partInfo, &name))
		return noErr;
	if (sourceType.deviceKind == 1)
		fprintf(stderr, "[pcmcia] card handler %s: the card server is NOT YET; not added for socket %u\n",
				name, (unsigned) sourceType.deviceNumber);
	else
		for (ULong socket = 0; socket < gNumberOfHWSockets; socket++)
			fprintf(stderr, "[pcmcia] card handler %s: the card server is NOT YET; not added for socket %lu\n",
					name, (unsigned long) socket);
	CardPartRemoveObject* removeObject = new CardPartRemoveObject;
	if (removeObject == nil)
		return kError_No_Memory;
	removeObject->fFormat = sourceType.format;
	removeObject->fDeviceKind = sourceType.deviceKind;
	removeObject->fDeviceNumber[0] = (UByte) (sourceType.deviceNumber >> 8);
	removeObject->fDeviceNumber[1] = (UByte) sourceType.deviceNumber;
	removeObject->fClassInfo = (void*) partInfo->data;
	SetRemoveObjPtr((RemoveObjPtr) removeObject);
	return noErr;
}


// ROM 0x00050044 Remove__16TCardPartHandlerFRC6PartIdUll
// The handler taken back from the socket(s) it was added for.
// NOT YET: RemoveCardHandler (the card server).
NewtonErr
TCardPartHandler::Remove(const PartId& /*partId*/, PartType /*partType*/, RemoveObjPtr removePtr)
{
	CardPartRemoveObject* removeObject = (CardPartRemoveObject*) removePtr;
	if (removeObject != nil)
		delete removeObject;
	return noErr;
}


TCardPartHandler*	gCardPartHandler = nil;

// host (DEVIATION): the ROM's card server makes its handler in its
// MainConstructor (0x00054648) with itself as the server; the card server
// is NOT YET, so the newt world makes it, with none
void
InitCardPartHandler(void)
{
	gCardPartHandler = new TCardPartHandler;
	gCardPartHandler->Init('cdhl', nil, nil);
}
