/*
	File:		pcmcia/CardProcessor.cpp

	Contains:	The card server's other pieces (CardServer.h): the card
				processor ('cdpr'), the event handlers of both worlds, and
				TCardDomains.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CardServer.h"
#include "CardSocket.h"
#include "CardHandler.h"
#include "PCMCIA20Parser.h"
#include "CardPower.h"
#include "Atomic.h"
#include "OSErrors.h"
#include "UserGlobals.h"


/* -------------------------------------------------------------------------------
	TCardDomains

	DEVIATION: the ROM runs a fault monitor (CardFaultMonProc, a TUMonitor in
	the card server's environment 'cdfm') over the sockets' domain ('csk0')
	and each socket's client domain ('ccl0'): a task that touches a card's
	memory while the card is out is held there - the monitor answers the
	abort by suspending it, telling the server (0x35) so it can ask for the
	card back - or given a bus error.  The host has no MMU domains and its
	card memory is host memory, which never faults, so there is no monitor:
	the fault states are kept, and a held task is never there to release.
------------------------------------------------------------------------------- */

static TUPort			sCardServerPort;						// ROM 0x0c10095c fCardServerPort__12TCardDomains
static TObjectId		sCardFaultMonitor = 0;					// ROM 0x0c100954 fCardFaultMonitor__12TCardDomains
static TObjectId		sCardSocketDomains = 0;					// ROM 0x0c10094c fCardSocketDomains__12TCardDomains
static TObjectId		sCardClientDomains[kMaxCardSockets];	// ROM 0x0c105f44 fCardClientDomains__12TCardDomains
static TCardFaultStates	sCardFaultStates[kMaxCardSockets];		// ROM 0x0c105f34 fCardFaultStates__12TCardDomains
static Boolean			sTaskSuspended[kMaxCardSockets];		// ROM 0x0c100948 fTaskSuspended__12TCardDomains
static TUAsyncMessage*	sCDCardAsyncMsg = nil;					// ROM 0x0c100964 sCDCardAsyncMsg
static TCardMessage*	sCDCardMsg = nil;						// ROM 0x0c100968 sCDCardMsg


// ROM 0x0004e40c __ct__12TCardDomainsFv
TCardDomains::TCardDomains()
{ }


// ROM 0x0004e41c __dt__12TCardDomainsFv
TCardDomains::~TCardDomains()
{ }


// ROM 0x0004e674 Init__12TCardDomainsFv
// The fault monitor made and set on the domains (DEVIATION: see above);
// the message a held task is told to the server with.
long
TCardDomains::Init(void)
{
	for (ULong i = 0; i < kMaxCardSockets; i++)
	{
		sCardFaultStates[i] = kCardFaultNone;
		sCardClientDomains[i] = 0;
	}
	sCardServerPort.CopyObject(0);
	sCDCardAsyncMsg = new TUAsyncMessage;
	long err = noErr;
	if (sCDCardAsyncMsg != nil && (err = sCDCardAsyncMsg->Init(true)) == noErr)
		sCDCardMsg = new TCardMessage;
	return err;
}


// ROM 0x0004e428 NotifyTaskBlocked__12TCardDomainsSFUl
void
TCardDomains::NotifyTaskBlocked(ULong socket)
{
	sCDCardMsg->MessageStuff(kCardMessageTaskBlocked, socket, 0);
	sCardServerPort.Send(sCDCardAsyncMsg, sCDCardMsg, sizeof(TCardMessage), 0, nil);
}


// ROM 0x0004e830 SocketDomain__12TCardDomainsSFUl
TObjectId
TCardDomains::SocketDomain(ULong /*socket*/)
{
	return sCardSocketDomains;
}


// ROM 0x0004e840 ClientDomain__12TCardDomainsSFUl
TObjectId
TCardDomains::ClientDomain(ULong socket)
{
	return sCardClientDomains[socket];
}


// ROM 0x0004e850 CardFaultMonitor__12TCardDomainsSFv
TObjectId
TCardDomains::CardFaultMonitor(void)
{
	return sCardFaultMonitor;
}


// ROM 0x0004e860 SetCardServerPort__12TCardDomainsSFUl
void
TCardDomains::SetCardServerPort(TObjectId port)
{
	if (sCardServerPort == port)
		return;
	sCardServerPort.DestroyObject();
	sCardServerPort.CopyObject(port);
}


// ROM 0x0004e870 SetCardFaultState__12TCardDomainsSFUl16TCardFaultStates
// A task held on the socket is told the server about once the socket is
// to let it go.
NewtonErr
TCardDomains::SetCardFaultState(ULong socket, TCardFaultStates state)
{
	sCardFaultStates[socket] = state;
	if (state == kCardFaultRelease && sTaskSuspended[socket])
	{
		NotifyTaskBlocked(socket);
		sTaskSuspended[socket] = false;
	}
	return noErr;
}


// ROM 0x0004e8c0 ReleaseBlockedTask__12TCardDomainsSFv
// DEVIATION: the ROM lets the monitor's held task go (SWI 0x1b) and
// answers the monitor's id; the host has no monitor.
TObjectId
TCardDomains::ReleaseBlockedTask(void)
{
	return sCardFaultMonitor;
}


/* -------------------------------------------------------------------------------
	TCardEventHandler
------------------------------------------------------------------------------- */

// ROM 0x0004e8d8 __ct__17TCardEventHandlerFv
TCardEventHandler::TCardEventHandler()
	:	fServer(nil)
{ }


// ROM 0x0004e918 Init__17TCardEventHandlerFP11TCardServer
// For 'cdsv' events, with an idler every two seconds that runs the card
// power down.
NewtonErr
TCardEventHandler::Init(TCardServer* server)
{
	fServer = server;
	NewtonErr err = TAEventHandler::Init('cdsv', 'newt');
	if (err == noErr)
		InitIdler(2, kSeconds, 0, true);
	return err;
}


// ROM 0x0004ead0 AEHandlerProc__17TCardEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TCardEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	fServer->DoCommand(token, size, (TCardMessage*) event, false);
	fServer->AESetReply(sizeof(TCardMessage));
}


// ROM 0x0004eb08 AECompletionProc__17TCardEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The answer to one of the server's messages (it is the message itself):
// dealt with, and the message free again (TCardAsyncMsg::Free, inline).
void
TCardEventHandler::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	fServer->DoCommand(token, size, (TCardMessage*) event, true);
	((TCardAsyncMsg*) event)->Free();
}


// ROM 0x0004eb38 IdleProc__17TCardEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The supplies with nobody using them counted down; the idler goes on
// while any is still on.
void
TCardEventHandler::IdleProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{
	Boolean vpp = VppIdleOff((Boolean) false);
	Boolean vcc = VccIdleOff((Boolean) false);
	Boolean internalVpp = InternalVppIdleOff(false);
	if (!internalVpp && !vcc && !vpp)
		return;
	StartIdle();
}


/* -------------------------------------------------------------------------------
	TCardSystemEventHandler
------------------------------------------------------------------------------- */

// ROM 0x0004ec60 __ct__23TCardSystemEventHandlerFv
TCardSystemEventHandler::TCardSystemEventHandler()
	:	fServer(nil)
{ }


// ROM 0x0004e970 Init__23TCardSystemEventHandlerFP11TCardServer
// Power on and off, and the 'card' events too (registered by hand: the
// ROM would install the handler itself if the first two had not).
NewtonErr
TCardSystemEventHandler::Init(TCardServer* server)
{
	fServer = server;
	NewtonErr err = TSystemEventHandler::Init(kSysEvent_PowerOn, 0);
	if (err != noErr)
		return err;
	err = TSystemEventHandler::Init(kSysEvent_PowerOff, 0);
	if (err != noErr)
		return err;
	TSystemEvent event(kSysEvent_NewICCard);
	return event.RegisterForSystemEvent(*((TAppWorld*) GetGlobals())->GetMyPort(), 0, 0);
}


// ROM 0x0004e9d0 NewCard__23TCardSystemEventHandlerFP7TAEvent
// The server's own new card event: answered with itself.
void
TCardSystemEventHandler::NewCard(TAEvent* event)
{
	((TAppWorld*) GetGlobals())->AESetReply(sizeof(TAESystemEvent), event);
}


// ROM 0x0004e9dc PowerOn__23TCardSystemEventHandlerFP7TAEvent
// Answered at once; then each socket's power restored and its card
// handler's services resumed.
void
TCardSystemEventHandler::PowerOn(TAEvent* event)
{
	SetReply(sizeof(TAESystemEvent), event);
	ReplyImmed();
	newton_try
	{
		for (ULong socket = 0; socket < gNumberOfHWSockets; socket++)
		{
			RestoreCardPower(socket);
			TCardSocketState* state = gSocketStates[socket];
			if (state->fHandler != nil)
				state->fHandler->ResumeServices(gCardSockets[socket], state->fCard, state->fConfigNumber);
		}
	}
	newton_catch_all
	{ }
	end_try;
}


// ROM 0x0004ea14 PowerOff__23TCardSystemEventHandlerFP7TAEvent
// The answer held back until every card handler has suspended its
// services and the sockets' power is off.
void
TCardSystemEventHandler::PowerOff(TAEvent* /*event*/)
{
	fPowerOffToken = *fServer->AEGetMsgToken();
	fPowerOffEvent = *(TAESystemEvent*) fServer->AEGetAEvent();
	fServer->AEDeferReply();
	newton_try
	{
		for (ULong socket = 0; socket < gNumberOfHWSockets; socket++)
		{
			if (gSocketStates[socket]->fHandler != nil)
				gSocketStates[socket]->fHandler->SuspendServices();
			VccIdleOff(socket);
		}
	}
	newton_catch_all
	{ }
	end_try;
	fServer->fSystemHandler->ReplyPowerOff();
}


// ROM 0x0004ea68 ReplyPowerOff__23TCardSystemEventHandlerFv
void
TCardSystemEventHandler::ReplyPowerOff(void)
{
	TAESystemEvent reply;
	reply.fSysEventType = kSysEvent_PowerOff;
	fPowerOffToken.ReplyRPC(&reply, sizeof(TAESystemEvent), noErr);
}


// ROM 0x0004eaa8 AECompletionProc__23TCardSystemEventHandlerFP10TUMsgTokenPUlP7TAEvent
// (the answers to the server's new card events)
void
TCardSystemEventHandler::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	fServer->DoCommand(token, size, (TCardMessage*) event, true);
}


/* -------------------------------------------------------------------------------
	TCardProcessorEventHandler
------------------------------------------------------------------------------- */

// ROM 0x0004eb84 __ct__26TCardProcessorEventHandlerFv
TCardProcessorEventHandler::TCardProcessorEventHandler()
{ }


// ROM 0x0004ebc4 Init__26TCardProcessorEventHandlerFv
// ('cdsv' events: the ROM sets the class and id itself and installs it)
NewtonErr
TCardProcessorEventHandler::Init(void)
{
	return TAEventHandler::Init('cdsv', 'newt');
}


// ROM 0x0004ebd8 AEHandlerProc__26TCardProcessorEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TCardProcessorEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	TCardProcessor* processor = (TCardProcessor*) GetGlobals();
	processor->DoCommand(token, size, (TCardMessage*) event, false);
	processor->AESetReply(sizeof(TCardMessage));
}


// ROM 0x0004ec20 AECompletionProc__26TCardProcessorEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TCardProcessorEventHandler::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	TCardProcessor* processor = (TCardProcessor*) GetGlobals();
	processor->DoCommand(token, size, (TCardMessage*) event, true);
	((TCardAsyncMsg*) event)->Free();
}


/* -------------------------------------------------------------------------------
	TCardProcessor
------------------------------------------------------------------------------- */

// ROM 0x00054ca4 __ct__14TCardProcessorFv
TCardProcessor::TCardProcessor()
	:	fHandler(nil)
{
	fServerPort.CopyObject(0);
	fPort.CopyObject(0);
}


// ROM 0x00051158 __dt__14TCardProcessorFv
TCardProcessor::~TCardProcessor()
{ }


// ROM 0x0038aad4 (unnamed) GetSizeOf - the vtable's +0x04
ULong
TCardProcessor::GetSizeOf()
{
	return sizeof(TCardProcessor);		// (the ROM: 0x84)
}


// ROM 0x000511b0 MainConstructor__14TCardProcessorFv
long
TCardProcessor::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	gCardProcessorId = gCurrentTaskId;		// (the ROM reads it out of the globals' header)
	fHandler = new TCardProcessorEventHandler;
	if (fHandler == nil)
		return kError_No_Memory;
	if ((err = fHandler->Init()) != noErr)
		return err;
	TUNameServer nameServer;
	TObjectId serverPort = 0;
	ULong spec;
	if ((err = nameServer.Lookup("cdsv", "TUPort", &serverPort, &spec)) == noErr)
	{
		fPort.CopyObject(*GetMyPort());
		fServerPort.CopyObject(serverPort);
	}
	return err;
}


// ROM 0x00051298 MainDestructor__14TCardProcessorFv
void
TCardProcessor::MainDestructor()
{
	if (fHandler != nil)
		delete fHandler;
	TAppWorld::MainDestructor();
}


// ROM 0x000512d0 MakeFork__14TCardProcessorFv
TForkWorld*
TCardProcessor::MakeFork()
{
	return new TCardProcessor;
}


// ROM 0x000512d8 ForkInit__14TCardProcessorFP10TForkWorld
long
TCardProcessor::ForkInit(TForkWorld* parent)
{
	long err = TAppWorld::ForkInit(parent);
	if (err == noErr)
	{
		TCardProcessor* processor = (TCardProcessor*) parent;
		fPort.CopyObject(processor->fPort);
		fServerPort.CopyObject(processor->fServerPort);
		fHandler = processor->fHandler;
	}
	return err;
}


// ROM 0x000512cc TheMain__14TCardProcessorFv
// The event loop (the ROM's is TAppWorld::AEventLoop written out).
void
TCardProcessor::TheMain()
{
	AEventLoop();
}


// ROM 0x0005394c DoCommand__14TCardProcessorFP10TUMsgTokenPUlP12TCardMessageUc
// What the card server hands on: recognising a new card (0xfc), writing a
// new CIS (0xfa), the battery (0xfb), a card back (0xfd), a card gone or
// put away (0xfe, 0xff).  The error goes back in the message's data.
long
TCardProcessor::DoCommand(TUMsgToken* /*token*/, ULong* /*size*/, TCardMessage* message, UChar /*completion*/)
{
	long result = noErr;
	ULong socketNumber = message->fSocket;
	// ROM BUG: > where >= was meant, so a message for the socket one past
	// the last goes through, with a socket and a state of nil
	if (socketNumber > gNumberOfHWSockets)
		return kError_Bad_Parameters;
	TCardSocket* socket = gCardSockets[socketNumber];
	TCardSocketState* state = gSocketStates[socketNumber];

	switch (message->fType)
	{
	case kCardProcessorRecognize:
		VccOn(socketNumber, false);
		// a socket whose last card's handler is still there, the card gone
		// or not yet let go, waits a moment and asks again
		if (state->fHandler == nil
		||  (state->fCardState != TCardSocketState::kCardRemoved
		  && (state->fHandler == nil || state->fCardState != TCardSocketState::kCardNone)))
			result = gCardServer->DoCardRecognition(socketNumber, socket, state);
		else
		{
			TTime when = TimeFromNow(0x59fd8);
			result = gCardServer->SendMessage(&gCardServer->fProcessorPort, kCardProcessorRecognize, socketNumber, 0, 0, &when);
		}
		VccOff(socketNumber);
		break;

	case kCardProcessorFormatCIS:
		VccOn(socketNumber, false);
		newton_try
		{
			result = state->fHandler->FormatCIS(socket, state->fCard);
			if (result == noErr)
				result = gPCMCIA20Parser.ParsePCCardCIS(state->fCard, socket);
		}
		newton_catch_all
		{
			result = kError_Bus_Access;
		}
		end_try;
		VccOff(socketNumber);
		break;

	case kCardProcessorBattery:
		if (gCardServer->fClientPort != 0)
		{
			ULong status = gCardServer->CheckCardStatus(socketNumber);
			result = gCardServer->SendMessage(&gCardServer->fClientPort, kCardServerBatteryReply, socketNumber, status, 0, nil);
		}
		break;

	case kCardProcessorResume:
		if (gCardServer->fField1A24 != 0)
			break;
		VccOn(socketNumber, false);
		EnterAtomic();
		gCardServer->ResumeSocketAccess(socket, state);
		TCardDomains::SetCardFaultState(socket->SocketNumber(), kCardFaultNone);
		ExitAtomic();
		if (state->fHandler != nil)
		{
			RestoreCardPower(socketNumber);
			newton_try
			{
				state->fHandler->ResumeServices(socket, state->fCard, state->fConfigNumber);
			}
			newton_catch_all
			{ }
			end_try;
		}
		// ROM BUG: the error answered is ReleaseBlockedTask's - the fault
		// monitor's id, which the server tells the application as an error
		result = TCardDomains::ReleaseBlockedTask();
		VccOff(socketNumber);
		break;

	case kCardProcessorRemove:
	case kCardProcessorUnmount:
		TCardDomains::SetCardFaultState(socket->SocketNumber(), kCardFaultNone);
		TCardDomains::ReleaseBlockedTask();
		result = gCardServer->DeactivateCardHandler(state->fHandler, socket, state);
		if (message->fType == kCardProcessorRemove)
		{
			gCardServer->InitializePCMCIABus(socket);
			if (state->fParsedCard != nil)
				delete state->fParsedCard;
			state->fParsedCard = nil;
			for (ULong i = 0; i < 4; i++)
				state->fDeviceTypes[i] = 0;
		}
		break;
	}
	message->fData = result;
	return result;
}
