/*
	File:		pcmcia/CardInfo.cpp

	Contains:	FGetCardInfo (CardInfo.h).

	Reconstructed from the MP2x00 US ROM; the function cites its origin.
*/

#include "CardInfo.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "OSErrors.h"
#include "CardServer.h"
#include "NewtCardEvents.h"
#include "CardHandler.h"
#include "CardPCMCIA.h"
#include "host/RomBugs.h"


// The version the frame says it is (the ROM's literal, 0x00080800 as a Ref).
enum { kCardInfoVersion = 0x20200 };


// ROM 0x0030c0bc FourCharToSymbol__FUl
// A four-character code as a symbol ('flsh -> 'flsh).
Ref
FourCharToSymbol(ULong code)
{
	char name[5];
	name[0] = (char) (code >> 24);
	name[1] = (char) (code >> 16);
	name[2] = (char) (code >> 8);
	name[3] = (char) code;
	name[4] = 0;
	return Intern(name);
}


// ROM 0x00053ccc FGetCardInfo
// A clone of the canonical card info frame, with a socketInfos entry for
// each socket that has a card in it (the CIS as it was last read): the
// types of the devices its handler installed, the CIS's names and ids, a
// canonicalCISCardFunctionInfo for each function it declares with its
// extensions, the types of its common memory devices, and the hardware
// location of each of its handler's devices.
Ref
FGetCardInfo(RefArg /*rcvr*/)
{
	ULong cards = 0;
	RefVar info(DeepClone(RefVar(Rcanonicalcardinfo)));
	if (ISNIL(info))
		return NILREF;
	SetFrameSlot(info, RSSYMcardinfoversion, RefVar(MAKEINT(kCardInfoVersion)));
	RefVar socketInfos(GetFrameSlotRef(info, RSSYMsocketinfos));
	for (ULong socket = 0; socket < gNumberOfHWSockets; socket++)
	{
		TCardSocketState* state = gSocketStates[socket];
		TCardPCMCIA* card = state->fParsedCard;
		if (card == nil)
			continue;
		RefVar socketInfo(DeepClone(RefVar(Rcanonicalsocketinfo)));
		if (ISNIL(socketInfo))
			continue;
		AddArraySlot(socketInfos, socketInfo);
		SetFrameSlot(socketInfo, RSSYMsocketnumber, RefVar(MAKEINT(socket)));
		RefVar cardTypes(GetFrameSlotRef(socketInfo, RSSYMcardtypes));
		for (ULong i = 0; i < 4; i++)
			if (state->fDeviceTypes[i] != 0)
				AddArraySlot(cardTypes, RefVar(FourCharToSymbol(state->fDeviceTypes[i])));
		if (card->fManufacturerName != nil)
			SetFrameSlot(socketInfo, RSSYMcismanufacturername, RefVar(MakeString(card->fManufacturerName)));
		if (card->fProductName != nil)
			SetFrameSlot(socketInfo, RSSYMcisproductname, RefVar(MakeString(card->fProductName)));
		if (card->fV1String3 != nil)
			SetFrameSlot(socketInfo, RSSYMcisproductinfo0, RefVar(MakeString(card->fV1String3)));
		if (card->fV1String4 != nil)
			SetFrameSlot(socketInfo, RSSYMcisproductinfo1, RefVar(MakeString(card->fV1String4)));
		if (card->fManufactureId != 0)
		{
			SetFrameSlot(socketInfo, RSSYMcismanufacturerid, RefVar(MAKEINT(card->fManufactureId)));
			// (the ROM reads the id info as a word at +0x1a, unaligned: the
			// StrongARM rotates it into the half it wants)
			SetFrameSlot(socketInfo, RSSYMcismanufactureridinfo, RefVar(MAKEINT(card->fManufactureIdInfo)));
		}
		RefVar functions(GetFrameSlotRef(socketInfo, RSSYMcisfunctions));
		TCardPCMCIA* cis = card;
		ULong cisNumber = 0;
		if (card->GetNumOfCISs() != 0)
		{
			for ( ; ; )
			{
				for (ULong f = 0; f < cis->GetNumOfCardFunctions(); f++)
				{
					TCardFunction* function = cis->GetCardFunction(f);
					if (function == nil)
						continue;
					RefVar functionInfo(DeepClone(RefVar(Rcanonicalciscardfunctioninfo)));
					if (ISNIL(functionInfo))
						continue;
					AddArraySlot(functions, functionInfo);
					SetFrameSlot(functionInfo, RSSYMcisfunctionid, RefVar(MAKEINT(function->fFuncId)));
					RefVar exts(GetFrameSlotRef(functionInfo, RSSYMcisfunctionexts));
					if (ISNIL(exts))
						continue;
					ULong count = function->GetNumOfFuncExts();
					for (ULong x = 0; x < count; x++)
					{
						TCardFuncExt* ext = function->GetFuncExt(x);
						if (ext == nil)
							continue;
						RefVar data(MakeArray(0));
						if (ISNIL(data))
							continue;
						AddArraySlot(exts, data);
						for (ULong b = 0; b < ext->fFuncExtDataSize; b++)
							AddArraySlot(data, RefVar(MAKEINT(ext->fFuncExtData[b])));
					}
				}
				cisNumber++;
				// ROM BUG (fixed): both the count and the next CIS are asked
				// of the CIS just done rather than of the card, so a card
				// with more than two function CISs has the third asked of
				// the second.  The fix asks the card (and stops at a CIS
				// the card does not have).
				if (RomBugFixed())
				{
					if (card->GetNumOfCISs() <= cisNumber)
						break;
					cis = card->GetCardCIS(cisNumber);
					if (cis == nil)
						break;
					continue;
				}
				if (cis->GetNumOfCISs() <= cisNumber)
					break;
				if (cisNumber != 0)
					cis = cis->GetCardCIS(cisNumber);
			}
		}
		RefVar deviceTypes(GetFrameSlotRef(socketInfo, RSSYMcisdevicetypes));
		for (ULong d = 0; (long) d < (long) card->fNumOfDevice; d++)
		{
			TCardDevice* device = card->GetCardDevice(d);
			if (device != nil && device->fAttributeMemoryDescr == 0)
				AddArraySlot(deviceTypes, RefVar(MAKEINT(device->fDeviceType)));
		}
		RefVar locations(MakeArray(0));
		TCardHandler* handler = state->fHandler;
		if (NOTNIL(locations) && handler != nil)
		{
			ULong count = handler->GetNumberOfDevice();
			ULong d;
			for (d = 0; d < count; d++)
			{
				ULong location = 0;
				if (handler->CardSpecific(kCardSpecificGetDeviceHWLocationId, &location, d) != noErr)
					break;
				char name[5];
				name[0] = (char) (location >> 24);
				name[1] = (char) (location >> 16);
				name[2] = (char) (location >> 8);
				name[3] = (char) location;
				name[4] = 0;
				AddArraySlot(locations, RefVar(MakeString(name)));
			}
			if (d == count)
				SetFrameSlot(socketInfo, RSSYMcardhwlocationids, locations);
		}
		cards++;
	}
	SetFrameSlot(info, RSSYMtotalsockets, RefVar(MAKEINT(gNumberOfHWSockets)));
	SetFrameSlot(info, RSSYMtotalcards, RefVar(MAKEINT(cards)));
	return info;
}


// ROM 0x0030c980 FCheckCardBattery
// CheckCardBattery(): every socket's card asked how its battery is doing -
// a 0x68 to the card server for each socket; what it finds comes back to
// the application's card handler later (a 0x69).  ==> nil.
Ref
FCheckCardBattery(RefArg /*rcvr*/)
{
	for (ULong socket = 0; socket < gNumberOfHWSockets; socket++)
	{
		TCardMessage reply;
		gCardEventHandler->SendServer(kCardServerBatteryCheck, socket, 0, &reply);
	}
	return NILREF;
}


// ROM 0x0030c7f4 FGetCardTypes
// GetCardTypes(): an array with one entry per socket saying what kind of
// card is in it - the types of the devices its handler installed (0x6e to
// the card server), as a symbol when there is one and an array of them
// when there are more; nil for an empty socket.
Ref
FGetCardTypes(RefArg /*rcvr*/)
{
	RefVar result(AllocateArray(RSSYMarray, gNumberOfHWSockets));
	for (ULong socket = 0; socket < gNumberOfHWSockets; socket++)
	{
		TCardMessage reply;
		if (gCardEventHandler->SendServer(kCardServerDeviceTypes, socket, 0, &reply) != noErr)
			continue;
		RefVar types(AllocateArray(RSSYMarray, 0));
		for (int i = 0; i < 4; i++)
			if (reply.fDevices[i].fType != 0)
				AddArraySlot(types, RefVar(FourCharToSymbol(reply.fDevices[i].fType)));
		long count = Length(types);
		if (count > 0)
		{
			if (count == 1)
				SetArraySlot(result, socket, RefVar(GetArraySlot(types, 0)));
			else
				SetArraySlot(result, socket, types);
		}
	}
	return result;
}


// ROM 0x0030c6b8 FUnmountCard
// UnmountCard(callback, socket): the card in the socket put away - its
// stores unmounted and its handler's services removed - and the callback
// called with [error] when the card server has done it (the application's
// card handler's completion, HandleCardEvent's 0x6f).  ==> nil when the
// request went, else the error.
Ref
FUnmountCard(RefArg /*rcvr*/, RefArg callback, RefArg socket)
{
	if (!IsFunction(callback) || !ISINT((Ref) socket))
		return MAKEINT(kError_Bad_Parameters);
	// DEVIATION: a host with no card server (newtonscript) has no
	// application card handler to send through, where the ROM always has
	if (gCardEventHandler == nil || gCardEventHandler->fServerPort == nil)
		return MAKEINT(kError_Call_Not_Implemented);
	TCardAsyncMsg* message = new TCardAsyncMsg;
	RefStruct* holder = new RefStruct(callback);
	NewtonErr err;
	if (message == nil || holder == nil)
		err = kError_No_Memory;
	else if ((err = message->Init()) == noErr)
	{
		message->fType = kCardServerUnmount;
		message->fSocket = RINT(socket);
		message->fField20 = (ULong) holder;
		err = gCardEventHandler->SendAyncServer(message, 1);
	}
	if (err == noErr)
		return NILREF;
	delete message;
	delete holder;
	return MAKEINT(err);
}


void
RegisterCardNatives(void)
{
	RegisterNativeFunction("FGetCardInfo", (void*) FGetCardInfo, 0);
	RegisterNativeFunction("FCheckCardBattery", (void*) FCheckCardBattery, 0);
	RegisterNativeFunction("FGetCardTypes", (void*) FGetCardTypes, 0);
	RegisterNativeFunction("FUnmountCard", (void*) FUnmountCard, 2);
}
