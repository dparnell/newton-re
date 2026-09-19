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


// The version the frame says it is (the ROM's literal, 0x00080800 as a Ref).
enum { kCardInfoVersion = 0x20200 };


// ROM 0x00053ccc FGetCardInfo
// A clone of the canonical card info frame, with a socketInfos entry for
// each hardware socket the card server knows about.
//
// NOT YET: the sockets themselves.  The ROM walks gSocketStates for each of
// gNumberOfHWSockets, and for a socket with a card in it clones
// canonicalSocketInfo and fills in what the card's CIS tuples say - the
// manufacturer and product names, the product and manufacturer ids, and a
// canonicalCISCardFunctionInfo for each function it declares.  A machine
// with no card hardware runs none of that and answers an empty socketInfos
// with totalSockets and totalCards both zero, which is the case the host is
// in until the card server is reconstructed.
Ref
FGetCardInfo(RefArg /*rcvr*/)
{
	RefVar info(DeepClone(RefVar(Rcanonicalcardinfo)));
	if (ISNIL(info))
		return NILREF;
	SetFrameSlot(info, RSSYMcardinfoversion, RefVar(MAKEINT(kCardInfoVersion)));
	SetFrameSlot(info, RSSYMtotalsockets, RefVar(MAKEINT(0)));
	SetFrameSlot(info, RSSYMtotalcards, RefVar(MAKEINT(0)));
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

void
RegisterCardNatives(void)
{
	RegisterNativeFunction("FGetCardInfo", (void*) FGetCardInfo, 0);
	RegisterNativeFunction("FCheckCardBattery", (void*) FCheckCardBattery, 0);
}
