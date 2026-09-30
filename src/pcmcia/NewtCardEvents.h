/*
	File:		pcmcia/NewtCardEvents.h

	Contains:	The application's side of the card server: the newt world's
				'cdsv' handler (TNewtCardEventHandler, gCardEventHandler),
				through which it tells the server its port and asks it for a
				socket's device types, a battery check or to put a card
				away, and hears back that a card has gone, that its battery
				is low or that something went wrong - each told to the
				NewtonScript card handler (HandleCardEvent) - and the 'card'
				event of a card with no storage on it (HandleNewCard).

				Not in the DDK.  Reconstructed from the MP2x00 US ROM
				(0x0030a84c-0x0030cb18).  The handler's event procs, which
				report an exception through the application, are the newt
				world's (newt/StorageCards.cpp); the rest is here so that the
				script functions (CardInfo.h) can reach the server.
*/

#ifndef __NEWTCARDEVENTS_H
#define __NEWTCARDEVENTS_H

#ifndef __CARDSERVER_H
#include "CardServer.h"
#endif
#ifndef __FRAMES_H
#include "Frames.h"
#endif

struct TNewCardEvent;


// (0x18 bytes)
class TNewtCardEventHandler : public TAEventHandler
{
public:
					TNewtCardEventHandler();
	NewtonErr		Init(ULong eventID, ULong eventClass);						// ROM 0x0030c140 Init__21TNewtCardEventHandlerFUlT1
	void			ReadyToAcceptCardEvents(void);								// ROM 0x0030c1d0 ReadyToAcceptCardEvents__21TNewtCardEventHandlerFv
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);		// ROM 0x0030c224 AEHandlerProc__21TNewtCardEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x0030c250 AECompletionProc__21TNewtCardEventHandlerFP10TUMsgTokenPUlP7TAEvent
	long			SendAyncServer(TCardAsyncMsg* message, ULong rpc);			// ROM 0x0030c258 SendAyncServer__21TNewtCardEventHandlerFP13TCardAsyncMsgUl
	long			SendServer(ULong type, ULong socket, ULong data, TCardMessage* reply);	// ROM 0x0030c2ac SendServer__21TNewtCardEventHandlerFUlN21P12TCardMessage
	void			ReplyServer(TCardMessage* message, ULong type, ULong socket, ULong data);	// ROM 0x0030c354 ReplyServer__21TNewtCardEventHandlerFP12TCardMessageUlN22
	long			HandleCardEvent(TCardMessage* message);						// ROM 0x0030c3d0 HandleCardEvent__21TNewtCardEventHandlerFP12TCardMessage

	TUPort*			fServerPort;		// +14 the card server's
};

extern TNewtCardEventHandler*	gCardEventHandler;		// ROM 0x0c1054b4 gCardEventHandler

NewtonErr	HandleCardEvents(void);									// ROM 0x0030c660 HandleCardEvents__Fv
void		HandleNewCard(TNewCardEvent* event);					// ROM 0x0030c5cc HandleNewCard__FP13TNewCardEvent
Ref			CallNSCardEventHandler(RefArg event, RefArg a1);		// ROM 0x0030a84c CallNSCardEventHandler__FRC6RefVarT1
Ref			CallNSCardEventHandler(RefArg event, RefArg a1, RefArg a2);	// ROM 0x0030a8bc CallNSCardEventHandler__FRC6RefVarN21
Ref			CardEventPrompt(RefArg event, ULong socket);			// ROM 0x0030c07c CardEventPrompt__FRC6RefVarUl
Ref			CardEventPrompt(RefArg event, ULong socket, long error);	// ROM 0x0030c370 CardEventPrompt__FRC6RefVarUll
void		HandleWeirdCardEvent(RefArg event, ULong socket, RefArg types);	// ROM 0x0030cb18 HandleWeirdCardEvent__FRC6RefVarUlT1

#endif	/* __NEWTCARDEVENTS_H */
