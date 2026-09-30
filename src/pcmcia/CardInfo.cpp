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
#include "CardHandler.h"
#include "CardPCMCIA.h"


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
				// ROM BUG: both the count and the next CIS are asked of the
				// CIS just done rather than of the card, so a card with more
				// than two function CISs has the third asked of the second
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
// CheckCardBattery(): every socket asked how its card's battery is
// doing - a 0x68 to the card server for each of gNumberOfHWSockets.
// ==> nil either way.
//
// NOT YET RECONSTRUCTED: the card server.  A machine with no sockets
// asks nobody, which is what the ROM does too and is what this answers.
Ref
FCheckCardBattery(RefArg /*rcvr*/)
{
	return NILREF;
}


// ROM 0x0030c7f4 FGetCardTypes
// GetCardTypes(): an array with one entry per hardware socket, saying what
// kind of card is in it.  The ROM makes an array of gNumberOfHWSockets
// slots and, for each socket, asks the card server (a 0x6e message) for
// the card's function types; the four function ids the answer carries are
// turned into symbols (FourCharToSymbol) and the socket's slot is set to
// the one symbol when there is only one, or to the array of them when
// there are more.  A socket the server would not answer for is left nil.
//
// NOT YET RECONSTRUCTED: the card server.  A machine with no card hardware
// has gNumberOfHWSockets zero and answers an empty array without asking
// anybody, which is what this does.
Ref
FGetCardTypes(RefArg /*rcvr*/)
{
	return AllocateArray(RefVar(RSSYMarray), 0);
}


// ROM 0x0030c6b8 FUnmountCard
// UnmountCard(callback, socket): the card in the socket put away, the
// callback called when the card server has done it.  The ROM checks its
// arguments - a function and an integer, else kError_Bad_Parameters - and
// sends a TCardAsyncMsg (0x6f, the socket, the callback held in a RefHandle)
// to the card server through gCardEventHandler, answering nil when it went
// and the error when it did not (kError_No_Memory when the message or the
// holder could not be made).
//
// NOT YET RECONSTRUCTED: the card server and its event handler.  A machine
// with no card hardware has no socket to put a card away from, so the
// message has nowhere to go: this answers kError_Call_Not_Implemented after
// the ROM's own argument checks, and the callback is never called.
Ref
FUnmountCard(RefArg /*rcvr*/, RefArg callback, RefArg socket)
{
	if (!IsFunction(callback) || !ISINT((Ref) socket))
		return MAKEINT(kError_Bad_Parameters);
	return MAKEINT(kError_Call_Not_Implemented);
}


void
RegisterCardNatives(void)
{
	RegisterNativeFunction("FGetCardInfo", (void*) FGetCardInfo, 0);
	RegisterNativeFunction("FCheckCardBattery", (void*) FCheckCardBattery, 0);
	RegisterNativeFunction("FGetCardTypes", (void*) FGetCardTypes, 0);
	RegisterNativeFunction("FUnmountCard", (void*) FUnmountCard, 2);
}
