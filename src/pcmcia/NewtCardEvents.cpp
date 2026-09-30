/*
	File:		pcmcia/NewtCardEvents.cpp

	Contains:	The application's side of the card server
				(NewtCardEvents.h): what reaches it and the NewtonScript
				card handler.  The handler's event procs are
				newt/StorageCards.cpp.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "NewtCardEvents.h"
#include "CardInfo.h"
#include "PSSManager.h"
#include "NameServer.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"

TNewtCardEventHandler*	gCardEventHandler = nil;		// ROM 0x0c1054b4 gCardEventHandler


// (the ROM's handler is 0x18 bytes: a TAEventHandler and the server's port)
TNewtCardEventHandler::TNewtCardEventHandler()
	:	fServerPort(nil)
{ }


// ROM 0x0030c140 Init__21TNewtCardEventHandlerFUlT1
// Installed for the events, and the card server's port found.
NewtonErr
TNewtCardEventHandler::Init(ULong eventID, ULong eventClass)
{
	NewtonErr err = TAEventHandler::Init(eventID, eventClass);
	if (err != noErr)
		return err;
	TUNameServer nameServer;
	TObjectId port;
	ULong spec;
	err = nameServer.Lookup("cdsv", "TUPort", &port, &spec);
	if (err == noErr)
		fServerPort = new TUPort(port);
	return err;
}


// ROM 0x0030c1d0 ReadyToAcceptCardEvents__21TNewtCardEventHandlerFv
// The card server given the application's port (message 100), which is
// what starts it watching for cards.
void
TNewtCardEventHandler::ReadyToAcceptCardEvents(void)
{
	TCardMessage reply;
	SendServer(kCardServerClientPort, 0, *((TAppWorld*) GetGlobals())->GetMyPort(), &reply);
}


// ROM 0x0030c258 SendAyncServer__21TNewtCardEventHandlerFP13TCardAsyncMsgUl
// A message sent to the server asynchronously: an RPC whose answer comes
// back to the application (rpc), or just sent.  (The ROM names gNewtPort
// as the collector; the host asks the calling world for its port, which
// is the same one - the newt world's library is above this one.)
long
TNewtCardEventHandler::SendAyncServer(TCardAsyncMsg* message, ULong rpc)
{
	if (rpc != 0)
	{
		message->SendRPC(fServerPort, ((TAppWorld*) GetGlobals())->GetMyPort(), 0, nil);
		return noErr;
	}
	if (message->fAsync.SetCollectorPort(0) != noErr)
		return noErr;
	return fServerPort->Send(&message->fAsync, message, sizeof(TCardMessage), 0, nil);
}


// ROM 0x0030c2ac SendServer__21TNewtCardEventHandlerFUlN21P12TCardMessage
// A message to the server, waited for, its answer in *reply.
long
TNewtCardEventHandler::SendServer(ULong type, ULong socket, ULong data, TCardMessage* reply)
{
	TCardMessage message;
	message.MessageStuff(type, socket, data);
	ULong size;
	return fServerPort->SendRPC(&size, &message, sizeof(TCardMessage), reply, sizeof(TCardMessage), 0);
}


// ROM 0x0030c354 ReplyServer__21TNewtCardEventHandlerFP12TCardMessageUlN22
void
TNewtCardEventHandler::ReplyServer(TCardMessage* message, ULong type, ULong socket, ULong data)
{
	message->Clear();
	message->fType = type;
	message->fData = data;
	message->fSocket = socket;
}


// ROM 0x0030c660 HandleCardEvents__Fv
// The application's 'cdsv' handler made (the ROM's TNewtCardEventHandler::
// Init, inline).
NewtonErr
HandleCardEvents(void)
{
	gCardEventHandler = new TNewtCardEventHandler;
	return gCardEventHandler->Init('cdsv', 'newt');
}


// ROM 0x0030a84c CallNSCardEventHandler__FRC6RefVarT1
// The NewtonScript card handler: HandleCardEvent(event, [a1]).
Ref
CallNSCardEventHandler(RefArg event, RefArg a1)
{
	RefVar args(AllocateArray(RSSYMarray, 1));
	SetArraySlot(args, 0, a1);
	return NSCallGlobalFn(RSSYMhandlecardevent, event, args);
}


// ROM 0x0030a8bc CallNSCardEventHandler__FRC6RefVarN21
Ref
CallNSCardEventHandler(RefArg event, RefArg a1, RefArg a2)
{
	RefVar args(AllocateArray(RSSYMarray, 2));
	SetArraySlot(args, 0, a1);
	SetArraySlot(args, 1, a2);
	return NSCallGlobalFn(RSSYMhandlecardevent, event, args);
}


// ROM 0x0030c07c CardEventPrompt__FRC6RefVarUl
Ref
CardEventPrompt(RefArg event, ULong socket)
{
	return CallNSCardEventHandler(event, RefVar(MAKEINT(socket)));
}


// ROM 0x0030c370 CardEventPrompt__FRC6RefVarUll
Ref
CardEventPrompt(RefArg event, ULong socket, long error)
{
	return CallNSCardEventHandler(event, RefVar(MAKEINT(socket)), RefVar(MAKEINT(error)));
}


// ROM 0x0030cb18 HandleWeirdCardEvent__FRC6RefVarUlT1
void
HandleWeirdCardEvent(RefArg event, ULong socket, RefArg types)
{
	CallNSCardEventHandler(event, RefVar(MAKEINT(socket)), types);
}


// ROM 0x0030c5cc HandleNewCard__FP13TNewCardEvent
// A card with no storage: the NewtonScript card handler told
// ('WeirdCardInserted) with the types of its devices.
void
HandleNewCard(TNewCardEvent* event)
{
	RefVar types(AllocateArray(RSSYMarray, 0));
	for (int i = 0; i < 4; i++)
		if (event->fDeviceTypes[i] != 0)
			AddArraySlot(types, RefVar(FourCharToSymbol(event->fDeviceTypes[i])));
	HandleWeirdCardEvent(RSSYMweirdcardinserted, event->fSocket, types);
}
