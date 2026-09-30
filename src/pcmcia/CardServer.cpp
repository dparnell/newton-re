/*
	File:		pcmcia/CardServer.cpp

	Contains:	The card server (CardServer.h): its messages, the socket
				states, and TCardServer itself.  The card processor, the
				event handlers and TCardDomains are CardProcessor.cpp.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	A message is sizeof(TCardMessage) where the ROM's is 0xb8 bytes
	(DEVIATION: a host word that holds an id or an address is wider).
*/

#include "CardServer.h"
#include "CardAlerts.h"
#include "CardSocket.h"
#include "CardHandler.h"
#include "CHMemModem.h"
#include "CardFlash.h"
#include "PCMCIA20Parser.h"
#include "CardPower.h"
#include "PackageManager.h"
#include "Atomic.h"
#include "System.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "NewtErrors.h"
#include "UserGlobals.h"

#include <string.h>

void	RemovePackage(TObjectId packageId);		// (packages/PackageManager.cpp)


TCardServer*		gCardServer = nil;						// ROM 0x0c100a7c gCardServer
TObjectId			gCardServerId = 0;						// ROM 0x0c100a80 gCardServerId
TObjectId			gCardProcessorId = 0;					// ROM 0x0c100a84 gCardProcessorId
TCardSocketState*	gSocketStates[kMaxCardSockets];			// ROM 0x0c105fe4 gSocketStates
TPCMCIA20Parser		gPCMCIA20Parser;						// ROM 0x0c100a88 gPCMCIA20Parser

const ULong	kCardMessageSize = sizeof(TCardMessage);		// (the ROM's 0xb8)

// the delays the server waits (in the timer's 3.6864 MHz ticks)
const TTimeout	kPollDelay = 0x11ff8;			// 20 ms: the pins settle after an interrupt
const TTimeout	kReadyDelay = 0x59fd8;			// 100 ms: between asking whether the card is ready
const TTimeout	kFirstPollDelay = 0xb3fb0;		// 200 ms: a card already in when the application starts

// the card's battery is dead (TNewtCardEventHandler::HandleCardEvent says
// so for it; the name is ours)
const NewtonErr	kCardErrorNotReady = ERRBASE_NEWT - 1;


/* -------------------------------------------------------------------------------
	TCardAsyncMsg
------------------------------------------------------------------------------- */

// ROM 0x0004b0d0 __ct__13TCardAsyncMsgFv
TCardAsyncMsg::TCardAsyncMsg()
	:	fFlags(0)		// (the ROM leaves the flags as the memory had them; Free clears the in-use one)
{
	Free();
}


// ROM 0x0004b114 Init__13TCardAsyncMsgFv
long
TCardAsyncMsg::Init(void)
{
	return fAsync.Init(true);
}


// ROM 0x0004b120 Free__13TCardAsyncMsgFv
// No longer in use: TCardMessage::Clear, inline, with the in-use flag off.
void
TCardAsyncMsg::Free(void)
{
	fFlags &= ~kInUse;
	fType = 0;
	fSocket = 0;
	fData = 0;
	fCardHandler = nil;
	fField2C = 0;
	fField2D = 1;
	fField14 = 0;
	fField18 = 0;
	fField20 = 0;
	fField24 = 0;
	fField28 = 0;
	memset(fDevices, 0, sizeof(fDevices));
}


// ROM 0x0004b130 SendRPC__13TCardAsyncMsgFP6TUPortT1UlP5TTime
// Sent, the answer coming back into the message itself and the completion
// to the collector's port.
void
TCardAsyncMsg::SendRPC(TUPort* port, TUPort* collector, ULong timeout, TTime* when)
{
	if (fAsync.SetCollectorPort(collector->fId) != noErr)
		return;
	port->SendRPC(&fAsync, this, kCardMessageSize, this, kCardMessageSize, timeout, when);
}


// ROM 0x0004b1b4 Send__13TCardAsyncMsgFP6TUPortUlP5TTime
void
TCardAsyncMsg::Send(TUPort* port, ULong timeout, TTime* when)
{
	if (fAsync.SetCollectorPort(0) != noErr)
		return;
	port->Send(&fAsync, this, kCardMessageSize, timeout, when);
}


/* -------------------------------------------------------------------------------
	TNewCardAsyncMsg
------------------------------------------------------------------------------- */

// ROM 0x0004b224 __ct__16TNewCardAsyncMsgFv
TNewCardAsyncMsg::TNewCardAsyncMsg()
	:	fEvent(0)
{ }


// ROM 0x0004b284 Init__16TNewCardAsyncMsgFv
NewtonErr
TNewCardAsyncMsg::Init(void)
{
	Clear();
	NewtonErr err = fEvent.Init();
	if (err != noErr)
		return err;
	err = fAsync.Init(true);
	if (err != noErr)
		return err;
	fEvent.SetEvent(kSysEvent_NewICCard);
	return noErr;
}


// ROM 0x0004b2dc SendSystemEvent__16TNewCardAsyncMsgFv
NewtonErr
TNewCardAsyncMsg::SendSystemEvent(void)
{
	return fEvent.SendSystemEvent(&fAsync, this, kCardMessageSize, this, kCardMessageSize);
}


/* -------------------------------------------------------------------------------
	TCardSocketState
------------------------------------------------------------------------------- */

// ROM 0x0005105c __ct__16TCardSocketStateFv
// (NOT YET: the ATA partition info, boot parameter block and loader at
// +0x314, +0x334 and +0x348)
TCardSocketState::TCardSocketState()
	:	fReadyTries(0), fField310(0)
{ }


// ROM 0x00051ca0 __dt__16TCardSocketStateFv
TCardSocketState::~TCardSocketState()
{
	if (fParsedCard != nil)
		delete fParsedCard;
	fPhys.CopyObject(0);
}


// ROM 0x000510e4 Init__16TCardSocketStateFv
long
TCardSocketState::Init(void)
{
	fCard = nil;
	fParsedCard = nil;
	fPhys.CopyObject(0);
	fClientDomain = 0;
	fFlags &= 0xCFFFFFFF;
	fState = kSocketEmpty;
	fField310 = 0;
	fCardState = kCardNone;
	fHandlerIndex = 0;
	Clear();
	long err = fMessage.Init();
	if (err != noErr)
		return err;
	return fMessage2.Init();
}


// ROM 0x00051600 Clear__16TCardSocketStateFv
void
TCardSocketState::Clear(void)
{
	fFlags &= 0x31FFFFFF;
	fCard = nil;
	fConfigNumber = 0xFFFFFFFF;
	fHandler = nil;
	fIdString[0] = 0;
	fClientPort.CopyObject(0);
	fPSSBase = 0;
	fPSSSize = 0;
	fFlags &= ~kATACard;
	for (ULong i = 0; i < 4; i++)
		fDeviceTypes[i] = 0;
	for (ULong i = 0; i < 8; i++)
		fPackages[i] = 0;
}


/* -------------------------------------------------------------------------------
	TCardServer
------------------------------------------------------------------------------- */

// ROM 0x00052938 __ct__11TCardServerFv
TCardServer::TCardServer()
	:	fEventHandler(nil), fField1A24(0), fSystemHandler(nil), fField2968(0), fReinsertEvent(nil), fPositionEvent(nil), fAlertFlags(0), fField29DC(0)
{
	fPort.CopyObject(0);
	fProcessorPort.CopyObject(0);
	fClientPort.CopyObject(0);
	fAlertPort.CopyObject(0);
	for (ULong i = 0; i < kMaxCardSockets; i++)
	{
		gCardSockets[i] = nil;
		gSocketStates[i] = nil;
	}
}


// ROM 0x0005455c __dt__11TCardServerFv
TCardServer::~TCardServer()
{ }


// ROM 0x0038aab4 (unnamed) GetSizeOf - the vtable's +0x04
ULong
TCardServer::GetSizeOf()
{
	return sizeof(TCardServer);			// (the ROM: 0x29e8)
}


// DEVIATION: the ROM registers CardIntHandler itself - a member function
// whose object comes in the first register, as the socket's interrupt proc
// calls it.  C++ on the host cannot take a member function as a plain one.
static NewtonErr
CardIntProc(void* server, TCardSocket* socket)
{
	((TCardServer*) server)->CardIntHandler(socket);
	return noErr;
}


// DEVIATION: the ROM gives the alerts CardReinsertAlertProc and
// CardPositionAlertProc themselves - member functions whose object comes
// in the first register, as the alert's filter proc calls them.
static UChar
ReinsertAlertProc(void* server, ULong button, void* socket)
{
	return ((TCardServer*) server)->CardReinsertAlertProc(button, (ULong) (uintptr_t) socket);
}


static UChar
PositionAlertProc(void* server, ULong button, void* socket)
{
	return ((TCardServer*) server)->CardPositionAlertProc(button, (ULong) (uintptr_t) socket);
}


// ROM 0x000525f4 CardReinsertAlertProc__11TCardServerFUlT1
// (the reinsert alert's filter) Done when the socket's card is back and
// active again.
UChar
TCardServer::CardReinsertAlertProc(ULong /*button*/, ULong socket)
{
	Boolean back = gSocketStates[socket]->fCardState == TCardSocketState::kCardActive;
	if (back)
	{
		fAlertFlags &= 0x7FFFFFFF;
		((TCardReinsertAlertDialog*) fReinsertEvent->fDialog)->Done();
	}
	return back;
}


// ROM 0x00052648 CardPositionAlertProc__11TCardServerFUlT1
UChar
TCardServer::CardPositionAlertProc(ULong /*button*/, ULong /*socket*/)
{
	return (fAlertFlags & 0x40000000) == 0;
}


// ROM 0x00054648 MainConstructor__11TCardServerFv
// The server's world set up: its port, its handlers, its messages, the
// card handlers it knows, a TCardSocket and a TCardSocketState for each
// socket the machine has (the first whose Init fails ends them), the card
// power, and the card processor's world started.  The sockets' interrupts
// are let through last.
long
TCardServer::MainConstructor()
{
	gCardServer = this;
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	fPort.CopyObject(*GetMyPort());
	fClientPort.CopyObject(0);
	TCardDomains::SetCardServerPort(fPort);
	gCardServerId = gCurrentTaskId;		// (the ROM reads it out of the globals' header)
	fField1A24 = 0;

	fEventHandler = new TCardEventHandler;
	if (fEventHandler == nil)
		return kError_No_Memory;
	if ((err = fEventHandler->Init(this)) != noErr)
		return err;
	fSystemHandler = new TCardSystemEventHandler;
	if (fSystemHandler == nil)
		return kError_No_Memory;
	if ((err = fSystemHandler->Init(this)) != noErr)
		return err;
	for (ULong socket = 0; socket < kMaxCardSockets; socket++)
		for (ULong i = 0; i < 4; i++)
			if ((err = fNewCardMessages[socket][i].Init()) != noErr)
				return err;
	for (ULong i = 0; i < kMessages; i++)
	{
		err = fMessages[i].Init();
		fMessages[i].fFlags |= TCardAsyncMsg::kPermanent;
		if (err != noErr)
			return err;
	}
	TCHMemModem::ClassInfo()->Register();
	TFlashSeries2::ClassInfo()->Register();
	// NOT YET: TFlashAMD and TATASimple, registered here too

	for (ULong socketNumber = 0; socketNumber < kMaxCardSockets; socketNumber++)
	{
		TCardSocket* socket = new TCardSocket(socketNumber);
		if (socket->Init() != noErr)
		{
			delete socket;
			break;
		}
		gCardSockets[socketNumber] = socket;
		TCardSocketState* state = new TCardSocketState;
		gSocketStates[socketNumber] = state;
		if (state == nil)
			return kError_No_Memory;
		if ((err = state->Init()) != noErr)
			return err;
		// DEVIATION: the ROM makes a physical memory object of the socket's
		// common memory (gCardSocketPAddr + its offset in the socket's
		// window), finds the socket's client domain and maps the card's
		// package window (gCardPackageVAddr) through it - answering at once,
		// with nothing more set up, when there is no such domain.  A host
		// socket's memory is host memory already and there are no domains
		// (TCardDomains), so there is nothing to make or map.
		state->fClientDomain = TCardDomains::ClientDomain(socketNumber);
		socket->EnableSocketAccess();
		if ((err = socket->MakeSocketInaccessible(socket->SocketBaseAddr(), 0xC000000)) != noErr)
			return err;
		AddCardHandler(socketNumber, TCHMemModem::ClassInfo());
		LockPtr((Ptr) socket);
		socket->RegisterSocketInterrupt(kSocketCardDetectedInt, CardIntProc, this);
		socket->RegisterSocketInterrupt(kSocketCardLockInt, CardIntProc, this);
		gNumberOfHWSockets = socketNumber + 1;
	}

	// the alert manager, and the two alerts
	TUNameServer nameServer;
	TObjectId alertPort = 0;
	ULong spec;
	if ((err = nameServer.Lookup("alrt", "TUPort", &alertPort, &spec)) != noErr)
		return err;
	fAlertPort.CopyObject(alertPort);
	fAlertFlags &= 0x3FFFFFFF;
	fField29DC = 0x19;
	fReinsertEvent = new TCardAlertEvent;
	if (fReinsertEvent == nil)
		return kError_No_Memory;
	fPositionEvent = new TCardAlertEvent;
	if (fPositionEvent == nil)
		return kError_No_Memory;
	((TCardReinsertAlertDialog*) fReinsertEvent->fDialog)->Init(ReinsertAlertProc, this);
	((TCardPositionAlertDialog*) fPositionEvent->fDialog)->Init(PositionAlertProc, this);
	if ((err = fReinsertAsync.Init(true)) != noErr || (err = fPositionAsync.Init(true)) != noErr)
		return err;
	// DEVIATION: the 'cdhl part handler (TCardPartHandler) is made and
	// registered by the newt world (CardPartHandler.h's InitCardPartHandler).
	if ((err = InitVppManager()) != noErr)
		return err;
	InitializePCMCIABus();
	{
		TCardProcessor processor;
		if ((err = processor.Init('cdpr', true, 6000)) != noErr)
			return err;
	}
	TObjectId processorPort = 0;
	if ((err = nameServer.Lookup("cdpr", "TUPort", &processorPort, &spec)) != noErr)
		return err;
	fProcessorPort.CopyObject(processorPort);
	gCardSockets[0]->EnableSocketInterrupt(kSocketOkToEnableInt);
	return noErr;
}


// ROM 0x00054bc4 MainDestructor__11TCardServerFv
void
TCardServer::MainDestructor()
{
	while (fExtraMessages.Count() != 0)
	{
		TCardAsyncMsg* message = (TCardAsyncMsg*) fExtraMessages.At(fExtraMessages.Count() - 1);
		if (message == nil)
			break;
		fExtraMessages.RemoveElementsAt(fExtraMessages.Count() - 1, 1);
		delete message;
	}
	for (ULong i = 0; i < kMaxCardSockets; i++)
	{
		if (gCardSockets[i] != nil)
		{
			gCardSockets[i]->DisableSocketAccess();
			UnlockPtr((Ptr) gCardSockets[i]);
			delete gCardSockets[i];
		}
		if (gSocketStates[i] != nil)
			delete gSocketStates[i];
	}
	TAppWorld::MainDestructor();
}


// ROM 0x00051320 SendMessage__11TCardServerFP6TUPortUlN32P5TTime
long
TCardServer::SendMessage(TUPort* port, ULong type, ULong socket, ULong data, ULong timeout, TTime* when)
{
	TCardAsyncMsg* message = NewMessage();
	if (message == nil)
		return kError_No_Memory;
	message->MessageStuff(type, socket, data);
	return SendMessage(port, message, timeout, when);
}


// ROM 0x00051390 SendMessage__11TCardServerFP6TUPortP13TCardAsyncMsgUlP5TTime
// An RPC whose answer comes back to the server's own port - or, from an
// interrupt (a socket's), one sent the way an interrupt may.
long
TCardServer::SendMessage(TUPort* port, TCardAsyncMsg* message, ULong timeout, TTime* when)
{
	if (!IsSuperMode())
	{
		message->SendRPC(port, &fPort, timeout, when);
		return noErr;
	}
	SendForInterrupt(port->fId, message->fAsync.GetMsgId(), message->fAsync.GetReplyMemId(), message,
					 kCardMessageSize, kMsgType_FromInterrupt, timeout, when, false);
	return noErr;
}


// ROM 0x00051418 SendNewCardMessage__11TCardServerFP6TUPortUlP12TCardHandlerPUl
// The system told of the new card in the socket: a 'card' system event
// carrying the devices its handler installed (four at most), the first of
// them also where a 1.x client looks for it.  The socket remembers the
// devices' types; *phys (when asked) is the first one's physical memory.
long
TCardServer::SendNewCardMessage(TUPort* /*port*/, ULong socket, TCardHandler* handler, ULong* phys)
{
	long err = noErr;
	TCardSocketState* state = gSocketStates[socket];
	newton_try
	{
		ULong count = handler->GetNumberOfDevice();
		if (count > 4)
			count = 4;
		TNewCardAsyncMsg* message = &fNewCardMessages[socket][0];
		message->MessageStuff(kSysEvent_NewICCard, socket, 0);
		message->fAEventID = kAESystemEventID;
		message->fCardHandler = handler;
		message->fField2C = state->fCard->fBadCIS != 0;
		for (ULong i = 0; i < 4; i++)
			state->fDeviceTypes[i] = 0;
		for (ULong i = 0; i < count && i < 4; i++)
		{
			SCardMessageDevice* device = &message->fDevices[i];
			void* driver = nil;
			handler->GetDeviceInfo(i, &device->fType, &device->fPhys, &driver, &device->fOffset, &device->fSize);
			device->fDriver = driver;
			device->fHandler = handler;
			state->fDeviceTypes[i] = device->fType;
			if (phys != nil && i == 0)
				*phys = message->fDevices[0].fPhys;
		}
		message->fField14 = message->fDevices[0].fType;
		message->fField18 = message->fDevices[0].fPhys;
		message->fCardHandler = message->fDevices[0].fHandler;
		message->fField20 = (ULong) message->fDevices[0].fDriver;
		message->fField24 = message->fDevices[0].fOffset;
		message->fField28 = message->fDevices[0].fSize;
		err = message->SendSystemEvent();
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	return err;
}


// ROM 0x000515cc SendSelfMessage__11TCardServerFUlN31P5TTime
long
TCardServer::SendSelfMessage(ULong type, ULong socket, ULong data, ULong timeout, TTime* when)
{
	return SendMessage(&fPort, type, socket, data, timeout, when);
}


// ROM 0x00051688 ReplyMessage__11TCardServerFP12TCardMessageUlN22
void
TCardServer::ReplyMessage(TCardMessage* message, ULong type, ULong socket, ULong data)
{
	message->Clear();
	message->fType = type;
	message->fData = data;
	message->fSocket = socket;
}


// ROM 0x000516a4 GetFreeMessage__11TCardServerFv
TCardAsyncMsg*
TCardServer::GetFreeMessage(void)
{
	for (ULong i = 0; i < kMessages; i++)
		if ((fMessages[i].fFlags & TCardAsyncMsg::kInUse) == 0)
		{
			fMessages[i].fFlags |= TCardAsyncMsg::kInUse;
			return &fMessages[i];
		}
	return nil;
}


// ROM 0x000516f8 NewMessage__11TCardServerFv
// One of the server's own messages, or when they are all out a new one,
// kept on a list until it comes back (DoCommand frees it then).
TCardAsyncMsg*
TCardServer::NewMessage(void)
{
	TCardAsyncMsg* message = GetFreeMessage();
	if (message != nil)
		return message;
	message = new TCardAsyncMsg;
	if (message != nil)
	{
		if (message->Init() == noErr)
		{
			message->fFlags |= TCardAsyncMsg::kInUse;
			fExtraMessages.InsertAt(fExtraMessages.Count(), message);
		}
		else
		{
			delete message;
			message = nil;
		}
	}
	return message;
}


// ROM 0x00051784 AddCardHandler__11TCardServerFUlPC10TClassInfo
// A card handler added in front of the socket's list - unless one of the
// same implementation, of this version or later, is there already.
NewtonErr
TCardServer::AddCardHandler(ULong socket, const TClassInfo* info)
{
	Boolean found = false;
	CListIterator iter(&gSocketStates[socket]->fHandlers);
	for (TClassInfo* each = (TClassInfo*) iter.FirstItem(); each != nil && !found; each = (TClassInfo*) iter.NextItem())
	{
		if (strcmp(info->ImplementationName(), each->ImplementationName()) == 0
		&&  info->Version() <= each->Version())
			found = true;
	}
	if (found)
		return kError_Already_Registered;
	gSocketStates[socket]->fHandlers.InsertAt(0, (void*) info);
	return noErr;
}


// ROM 0x00051858 RemoveCardHandler__11TCardServerFUlPC10TClassInfo
NewtonErr
TCardServer::RemoveCardHandler(ULong socket, const TClassInfo* info)
{
	gSocketStates[socket]->fHandlers.Remove((void*) info);
	return noErr;
}


// ROM 0x00051884 FirstCardHandler__11TCardServerFP16TCardSocketState
TCardHandler*
TCardServer::FirstCardHandler(TCardSocketState* state)
{
	state->fHandlerIndex = 0;
	return NextCardHandler(state);
}


// ROM 0x00051890 NextCardHandler__11TCardServerFP16TCardSocketState
// The next of the socket's card handlers made, told where the server is.
TCardHandler*
TCardServer::NextCardHandler(TCardSocketState* state)
{
	TCardHandler* handler = nil;
	TClassInfo* info;
	if (state->fHandlerIndex < state->fHandlers.Count()
	&&  (info = (TClassInfo*) state->fHandlers.At(state->fHandlerIndex)) != nil)
	{
		handler = TCardHandler::New((char*) info->ImplementationName());
		if (handler != nil)
		{
			handler->SetCardServerPort(fPort);
			state->fHandlerIndex++;
		}
	}
	return handler;
}


// ROM 0x000518f8 ActivateCardHandler__11TCardServerFP12TCardHandlerP11TCardSocketP16TCardSocketState
// The handler that recognised the card installs its services; if the card
// then says it is there (CardStatus bit 0) the socket keeps the handler and
// the system is told of the new card, otherwise the services come out
// again.
NewtonErr
TCardServer::ActivateCardHandler(TCardHandler* handler, TCardSocket* socket, TCardSocketState* state)
{
	NewtonErr err = noErr;
	ULong socketNumber = socket->SocketNumber();
	TCardPCMCIA* card = state->fCard;
	if (card == nil || handler == nil || socket == nil)
		return kError_Bad_Parameters;
	ULong configNumber = SelectCardConfiguration(card, socket);
	if (configNumber == 0xFFFFFFFF)
		return err;
	newton_try
	{
		// NOT YET: an ATA card's partition info handed to its handler
		// (CardSpecific kCardSpecificATASetPartitionInfo)
		err = handler->InstallServices(socket, card, configNumber);
		if (err == noErr)
		{
			if ((handler->CardStatus() & 1) == 0)
			{
				handler->RemoveServices();
				err = kCardErrorNotReady;
			}
			else
			{
				state->fHandler = handler;
				state->fConfigNumber = configNumber;
				strncpy(state->fIdString, handler->CardIdString(card), 0x1F);
				state->fIdString[0x1F] = 0;
				const TClassInfo* info = handler->ClassInfo();
				Boolean pager = strcmp(info->ImplementationName(), "TCardHandlerPager") == 0 && info->Version() == 0;
				state->fFlags = (state->fFlags & ~TCardSocketState::kPagerHandler) | (pager ? TCardSocketState::kPagerHandler : 0);
				err = SendNewCardMessage(&state->fClientPort, socketNumber, handler, nil);
			}
		}
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	return err;
}


// ROM 0x00051ad8 DeactivateCardHandler__11TCardServerFP12TCardHandlerP11TCardSocketP16TCardSocketState
// The handler's services removed and the handler gone, with the card's
// packages; the socket cleared and its power off.
NewtonErr
TCardServer::DeactivateCardHandler(TCardHandler* handler, TCardSocket* socket, TCardSocketState* state)
{
	NewtonErr err = noErr;
	if (handler != nil)
	{
		newton_try
		{
			err = handler->RemoveServices();
		}
		newton_catch_all
		{
			err = kError_Bus_Access;
		}
		end_try;
		handler->Delete();
		// NOT YET: an ATA card's packages (TCardATALoader::RemoveATAPackages)
	}
	for (ULong i = 0; i < 8; i++)
		if (state->fPackages[i] != 0)
		{
			RemovePackage(state->fPackages[i]);
			state->fPackages[i] = 0;
		}
	state->Clear();
	socket->ResetInterrupts();
	int socketNumber = socket->SocketNumber();
	VppOff(socketNumber, kResetTimeOut);
	VccOff(socketNumber, kResetTimeOut);
	return err;
}


// ROM 0x00051bec SelectCardCISPower__11TCardServerFP11TCardSocket
// The voltage to read the card's CIS at, from its voltage sense pins: 3.3 V
// (with a slower attribute memory) where the card and the socket can both
// take it, 5 V otherwise.  ==> kError_Card_Bad_Power for a card the socket
// cannot power.
NewtonErr
TCardServer::SelectCardCISPower(TCardSocket* socket)
{
	ULong sense = socket->GetVPCPins() & 0x30;
	ULong vccSpec = socket->VccVoltageSpec();
	TNanoSecond speed = 300;
	TSocketPowerLevels vcc;
	if (sense == 0x10)
		return kError_Card_Bad_Power;
	if (sense == 0 || sense == 0x20)
	{
		if ((vccSpec & kPCMCIA3p3VAvailable) != 0)
		{
			vcc = kSocketPowerLevelVcc3p3V;
			speed = 600;
			goto power;
		}
	}
	else if (sense != 0x30)
		return kError_Card_Bad_Power;
	if ((vccSpec & kPCMCIA5VAvailable) == 0)
		return kError_Card_Bad_Power;
	vcc = kSocketPowerLevelVcc5V;
power:
	TSocketPowerLevels vpp;
	if ((socket->VppVoltageSpec() & kPCMCIA0VAvailable) == 0)
		vpp = (TSocketPowerLevels) (vcc | kSocketPowerLevelVpp);
	else
		vpp = kSocketPowerLevelVpp0V;
	socket->SelectVoltageLevel(vcc);
	socket->SelectVoltageLevel(vpp);
	socket->SetAttributeMemSpeed(speed);
	return noErr;
}


// ROM 0x00051d84 SelectCardConfiguration__11TCardServerFP11TCardPCMCIAP11TCardSocket
ULong
TCardServer::SelectCardConfiguration(TCardPCMCIA* /*card*/, TCardSocket* /*socket*/)
{
	return 0;
}


// ROM 0x00051d8c SelectCardPower__11TCardServerFP11TCardSocketP11TCardPCMCIA
NewtonErr
TCardServer::SelectCardPower(TCardSocket* socket, TCardPCMCIA* /*card*/)
{
	if ((socket->VppVoltageSpec() & kPCMCIA12VAvailable) != 0)
		socket->SelectVoltageLevel(kSocketPowerLevelVpp12V);
	return noErr;
}


// ROM 0x00051dbc CheckCardStatus__11TCardServerFUl
// The card handler's CardStatus for an active card (3 - both batteries
// good - for a socket without one; 0 if asking faults).
ULong
TCardServer::CheckCardStatus(ULong socket)
{
	ULong status = 3;
	TCardSocketState* state = gSocketStates[socket];
	if (state->fHandler != nil && state->fCardState == TCardSocketState::kCardActive)
	{
		newton_try
		{
			status = state->fHandler->CardStatus();
		}
		newton_catch_all
		{
			status = 0;
		}
		end_try;
	}
	return status;
}


// ROM 0x00051e60 InitializePCMCIABus__11TCardServerFP11TCardSocket
// The socket back to how an empty one is: power off, default speeds, the
// memory interface, 5 V.
void
TCardServer::InitializePCMCIABus(TCardSocket* socket)
{
	VccIdleOff(socket->SocketNumber());
	socket->SetDefaultSpeeds();
	socket->SelectMemoryInterface();
	socket->SetRdWrQueueControl(4);
	socket->SetRdWrQueueControl(8);
	socket->SelectVoltageLevel(kSocketPowerLevelVcc5V);
	socket->SetPullupControl(0x1C00);
	// DEVIATION: the ROM sets three of the Voyager's registers (+0x2000,
	// +0x2400, +0x2c00 from the socket's base) directly; the host's socket
	// has no such registers.
}


// ROM 0x00051e88 InitializePCMCIABus__11TCardServerFv
void
TCardServer::InitializePCMCIABus(void)
{
	for (ULong i = 0; i < gNumberOfHWSockets; i++)
		InitializePCMCIABus(gCardSockets[i]);
}


// ROM 0x00051ed8 InitializeCardDetection__11TCardServerFP11TCardSocketP16TCardSocketState
// A card that is in (and locked in) looked at a little later; otherwise the
// card-detect interrupt enabled to say when one goes in.
long
TCardServer::InitializeCardDetection(TCardSocket* socket, TCardSocketState* /*state*/)
{
	if (socket->IsCardDetected() && (socket->GetPCPins() & kCardEjectSwitchUp) == 0)
	{
		TTime when = TimeFromNow(kFirstPollDelay);
		return SendSelfMessage(kCardServerPollSocket, socket->SocketNumber(), 0, 0, &when);
	}
	socket->ClearSocketInterrupt(kSocketCardDetectedInt);
	socket->SetSocketInterruptFlags(kSocketCardDetectedInt, kSocketIntEnableFalling);
	// DEVIATION: the ROM enables the interrupt in the Voyager's own
	// registers (+0x800 cleared, +0x400 or'd with the socket's bit, under
	// EnterAtomic); the host's socket enables it through its interface.
	EnterAtomic();
	socket->EnableSocketInterrupt(kSocketCardDetectedInt);
	ExitAtomic();
	return noErr;
}


// ROM 0x00051f84 InitializeCardDetection__11TCardServerFv
void
TCardServer::InitializeCardDetection(void)
{
	for (ULong i = 0; i < gNumberOfHWSockets; i++)
		InitializeCardDetection(gCardSockets[i], gSocketStates[i]);
}


// ROM 0x00051fe0 SetPCMCIAWaitStates__11TCardServerFP11TCardSocketP11TCardPCMCIA
// The socket's speeds from the card's devices: the slowest attribute and
// common memory devices (300 ns if it has none or they are faster) and the
// slowest I/O device (165 ns at least).
void
TCardServer::SetPCMCIAWaitStates(TCardSocket* socket, TCardPCMCIA* card)
{
	ULong attrSpeed = 0;
	ULong commonSpeed = 0;
	ULong ioSpeed = 0;
	if (card->fNumOfDevice != 0)
	{
		for (ULong i = 0; i < card->fNumOfDevice; i++)
		{
			TCardDevice* device = card->GetCardDevice(i);
			if (device == nil)
				continue;
			ULong speed = device->fnsecSpeed;
			if (device->fDeviceType == 0x0D)
			{
				if (ioSpeed < speed)
					ioSpeed = speed;
			}
			else if (device->fAttributeMemoryDescr == 0)
			{
				if (commonSpeed < speed)
					commonSpeed = speed;
			}
			else if (attrSpeed < speed)
				attrSpeed = speed;
		}
		if (attrSpeed < 300)
			attrSpeed = 300;
	}
	else
		attrSpeed = 300;
	socket->SetAttributeMemSpeed(attrSpeed);
	if (commonSpeed == 0)
		commonSpeed = 300;
	socket->SetCommonMemSpeed(commonSpeed);
	if (ioSpeed < 0xA5)
		ioSpeed = 0xA5;
	ioSpeed = socket->ConvertWaitCount(ioSpeed);
	// DEVIATION: the ROM puts the count in the Voyager's register +0x3800
	// (its low six bits); the host's socket has none.
	(void) ioSpeed;
}


// ROM 0x000520b4 LoadCardPackage__11TCardServerFP11TCardPCMCIAP11TCardSocketP16TCardSocketState
// The packages the card's CIS says it carries (eight at most) loaded, each
// one that starts "package" as a package does; the socket remembers their
// ids.
NewtonErr
TCardServer::LoadCardPackage(TCardPCMCIA* card, TCardSocket* socket, TCardSocketState* state)
{
	NewtonErr err = noErr;
	ULong socketNumber = socket->SocketNumber();
	newton_try
	{
		for (ULong i = 0; i < card->fNumOfPackage && i < 8; i++)
		{
			TCardPackage* package = card->GetCardPackage(i);
			if (package == nil)
				continue;
			if (package->fAttribute != 0)
			{
				// NOT YET: a package in attribute memory, which the ROM loads
				// through a TCardPipe (every other byte, from the odd ones)
				continue;
			}
			// DEVIATION: the ROM maps the card's package window
			// (gCardPackageVAddr) through the socket's client domain and
			// finds the package there; the host's common memory is host
			// memory.
			Ptr buffer = (Ptr) socket->CommonMemBaseAddr() + package->fAddress;
			static const char kSignature[] = "package";
			ULong n = 0;
			while (n < 7 && (UByte) buffer[n] == (UByte) kSignature[n])
				n++;
			if (n < 7)
				continue;
			fField2968 = 0;
			SourceType source;
			source.format = 3;
			source.deviceKind = 1;
			source.deviceNumber = (UShort) socketNumber;
			source.deviceId = 0;
			ULong packageId = 0;
			if ((err = LoadPackage(buffer, source, &packageId)) != noErr)
				break;
			state->fPackages[i] = packageId;
		}
		// NOT YET: an ATA card (TCardATALoader::GetCardType, LoadATAPackages)
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	return err;
}


// ROM 0x000523c8 PatchPoint__Fv
static void
PatchPoint(void)
{ }


// ROM 0x000523cc CardIntHandler__11TCardServerFP11TCardSocket
// A socket's card-detect or lock interrupt: both off until the pins have
// been looked at (a moment later: they bounce); a card in use that is
// being taken out has its memory made inaccessible at once.
void
TCardServer::CardIntHandler(TCardSocket* socket)
{
	// NOT YET: a paging tracker on the card unhooked (gExternPageObjectId,
	// UnHookTracker)
	socket->DisableSocketInterrupt(kSocketCardLockInt);
	socket->DisableSocketInterrupt(kSocketCardDetectedInt);
	ULong socketNumber = socket->SocketNumber();
	TCardSocketState* state = gSocketStates[socketNumber];
	if (state->fState == TCardSocketState::kSocketCardIn)
	{
		state->fState = TCardSocketState::kSocketSuspended;
		SuspendSocketAccess(socket, state, kCardFaultRelease);
	}
	TTime when = TimeFromNow(kPollDelay);
	SendSelfMessage(kCardServerPollSocket, socketNumber, 0, 0, &when);
}


// ROM 0x0005248c SuspendSocketAccess__11TCardServerFP11TCardSocketP16TCardSocketStateUl
// The card's memory made inaccessible (the client's window, the package
// window, the whole socket) and a task that touches it held; the handler
// told to stop at once (unless it is the pager's, which cannot).
NewtonErr
TCardServer::SuspendSocketAccess(TCardSocket* socket, TCardSocketState* state, ULong faultState)
{
	socket->MakeSocketInaccessible(state->fPSSBase, state->fPSSSize);
	// (the package window, gCardPackageVAddr, which the host has not got)
	NewtonErr err = socket->MakeSocketInaccessible(socket->AttributeMemBaseAddr(), 0xC000000);
	TCardDomains::SetCardFaultState(socket->SocketNumber(), (TCardFaultStates) faultState);
	newton_try
	{
		if (state->fHandler != nil && (state->fFlags & TCardSocketState::kPagerHandler) == 0)
			err = state->fHandler->EmergencyShutdown();
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	return err;
}


// ROM 0x00052590 ResumeSocketAccess__11TCardServerFP11TCardSocketP16TCardSocketState
NewtonErr
TCardServer::ResumeSocketAccess(TCardSocket* socket, TCardSocketState* state)
{
	socket->MakeSocketAccessible(state->fPSSBase, state->fPSSSize);
	// (the package window, gCardPackageVAddr, which the host has not got;
	// and the ROM changes the socket's own physical mapping back directly,
	// TUPhys::ChangeVirtualMapping, which the socket's interface does here)
	return socket->MakeSocketAccessible(socket->AttributeMemBaseAddr(), 0xC000000);
}


// ROM 0x00052664 DoCardEjection__11TCardServerFUlP11TCardSocketP16TCardSocketState
long
TCardServer::DoCardEjection(ULong /*socketNumber*/, TCardSocket* /*socket*/, TCardSocketState* /*state*/)
{
	return noErr;
}


// ROM 0x0005266c DoPollLockSwitchAndCardDetected__11TCardServerFUlP11TCardSocketP16TCardSocketState
// The socket's pins looked at after an interrupt.  A card in and locked:
// if it was suspended it is simply resumed, and if it is new it is powered
// up to have its CIS read once it is ready (0xcb).  No card (or not
// locked): whoever had it is told it has gone - the application, and the
// socket's client or, with none, the server itself (0x34) - and the
// socket goes back to waiting for one.
long
TCardServer::DoPollLockSwitchAndCardDetected(ULong socketNumber, TCardSocket* socket, TCardSocketState* state)
{
	long result = noErr;
	// DEVIATION: the ROM locks its stack's pages down first (LockStack),
	// since it runs atomically below; a host stack does not page.
	Boolean removed = false;
	Boolean inserted = false;
	Boolean reset = false;
	EnterAtomic();
	ULong pins = socket->GetPCPins();
	PatchPoint();
	ULong socketState = state->fState;
	if ((pins & 3) == 3 && (pins & kCardEjectSwitchUp) == 0)
	{
		if (socketState == TCardSocketState::kSocketSuspended)
		{
			state->fState = TCardSocketState::kSocketCardIn;
			ResumeSocketAccess(socket, state);
			socket->ClearSocketInterrupt(kSocketCardLockInt);
			socket->ClearSocketInterrupt(kSocketCardDetectedInt);
			socket->SetSocketInterruptFlags(kSocketCardDetectedInt, (TSocketIntFlags) (kSocketIntWakeup | kSocketIntEnableRising));
			socket->EnableSocketInterrupt(kSocketCardLockInt);
			socket->EnableSocketInterrupt(kSocketCardDetectedInt);
		}
		else if (socketState != TCardSocketState::kSocketCardIn)
		{
			inserted = true;
			state->fState = TCardSocketState::kSocketCardIn;
			if (state->fCardState == TCardSocketState::kCardNone)
				state->fCardState = TCardSocketState::kCardActive;
		}
	}
	else
	{
		removed = socketState != TCardSocketState::kSocketEmpty;
		reset = true;
		if (removed)
			state->fState = TCardSocketState::kSocketEmpty;
		if (state->fCardState == TCardSocketState::kCardActive)
			state->fCardState = TCardSocketState::kCardRemoved;
	}
	ExitAtomic();
	PatchPoint();
	if (reset)
	{
		InitializePCMCIABus(socket);
		InitializeCardDetection(socket, state);
	}
	if (inserted)
	{
		long err = SelectCardCISPower(socket);
		Boolean failed = err != noErr;
		if (!failed)
		{
			RestoreCardPower(socketNumber);
			VccOn(socketNumber, false);
			socket->PCMCIAReset();
			state->fReadyTries = 0x32;
			TTime when = TimeFromNow(kReadyDelay);
			err = SendSelfMessage(kCardServerCardReady, socketNumber, 0, 0, &when);
		}
		socket->ClearSocketInterrupt(kSocketCardLockInt);
		socket->ClearSocketInterrupt(kSocketCardDetectedInt);
		socket->SetSocketInterruptFlags(kSocketCardDetectedInt, (TSocketIntFlags) (kSocketIntWakeup | kSocketIntEnableRising));
		EnterAtomic();
		socket->EnableSocketInterrupt(kSocketCardLockInt);
		socket->EnableSocketInterrupt(kSocketCardDetectedInt);
		ExitAtomic();
		if (failed)
			return err;
		result = TCardDomains::ReleaseBlockedTask();
		VccOff(socketNumber);
	}
	if (removed)
	{
		SendMessage(&fClientPort, kCardServerCardRemoved, socketNumber, 0, 0, nil);
		if (state->fHandler == nil || state->fClientPort == 0)
			result = SendSelfMessage(kCardServerClientDone, socketNumber, 0, 0, nil);
		else
			result = SendMessage(&state->fClientPort, kCardServerCardRemoved, socketNumber, 0, 0, nil);
	}
	return result;
}


// The part of DoCardRecognition inside its exception handler.
static long
RecognizeCard(TCardServer* server, ULong socketNumber, TCardSocket* socket, TCardSocketState* state)
{
	long result;
	TCardPCMCIA* card = new TCardPCMCIA;
	if (card == nil)
		return kError_No_Memory;
	if (state->fParsedCard != nil)
		delete state->fParsedCard;
	state->fParsedCard = card;
	card->fSocketNumber = socketNumber;
	ULong control = socket->GetControl();
	socket->SetControl(control | 0x10);
	TCardHandler* handler = nil;
	result = gPCMCIA20Parser.ParsePCCardCIS(card, socket);
	if (result != noErr)
	{
		// a CIS the parser could not read: a handler may know the card anyway
		for (handler = server->FirstCardHandler(state); handler != nil; handler = server->NextCardHandler(state))
		{
			result = handler->ParseUnrecognizedCard(socket, card);
			if (result == noErr)
				break;
			handler->Delete();
		}
		if (handler == nil)
			return result;
	}

	server->SetPCMCIAWaitStates(socket, card);
	server->SelectCardPower(socket, card);
	if (state->fCardState == TCardSocketState::kCardNone)
		state->fCardState = TCardSocketState::kCardActive;
	else if (state->fCardState != TCardSocketState::kCardActive)
	{
		// a card back in a socket whose card was taken out while in use: if
		// the handler that had it says it is the same card, whoever took it
		// on is told it is back
		if (handler != nil)
			handler->Delete();
		result = noErr;
		if (state->fHandler != nil && state->fHandler->RecognizeCard(socket, card) == noErr)
		{
			if (strcmp(state->fHandler->CardIdString(card), state->fIdString) == 0 && state->fClientPort != 0)
				result = server->SendMessage(&state->fClientPort, kCardServerCardBack, socketNumber, socket->CommonMemBaseAddr(), 0, nil);
		}
		return result;
	}

	result = server->LoadCardPackage(card, socket, state);
	if (result != noErr && result != kError_ATA_No_Partition)
		return result;
	if (handler == nil)
	{
		for (handler = server->FirstCardHandler(state); handler != nil; handler = server->NextCardHandler(state))
		{
			if (handler->RecognizeCard(socket, card) == noErr)
				break;
			handler->Delete();
		}
	}
	if (handler != nil)
	{
		state->fCard = card;
		socket->SetControl(control);
		result = server->ActivateCardHandler(handler, socket, state);
		if (result == noErr)
			return noErr;
		handler->Delete();
	}
	// nobody would have the card
	if (state->fParsedCard != nil)
		delete state->fParsedCard;
	state->fParsedCard = nil;
	state->fCard = nil;
	for (ULong i = 0; i < 8; i++)
		if (state->fPackages[i] != 0)
		{
			RemovePackage(state->fPackages[i]);
			state->fPackages[i] = 0;
		}
	// NOT YET: an ATA card's packages (TCardATALoader::RemoveATAPackages)
	if (result == noErr)
		result = kError_Unrecognized_Card;
	return result;
}


// ROM 0x00052a70 DoCardRecognition__11TCardServerFUlP11TCardSocketP16TCardSocketState
// (the card processor's, 0xfc) The card's CIS read and the card offered to
// the socket's card handlers until one recognises it, which then installs
// its services; a card whose CIS will not parse is offered to them to
// parse instead.  The socket is reset after a failure.
long
TCardServer::DoCardRecognition(ULong socketNumber, TCardSocket* socket, TCardSocketState* state)
{
	long result = noErr;
	EnterAtomic();
	ULong pins = socket->GetPCPins();
	if ((pins & 3) == 3 && (pins & kCardEjectSwitchUp) == 0)
		socket->EnableSocketAccess();
	ExitAtomic();
	newton_try
	{
		result = RecognizeCard(this, socketNumber, socket, state);
	}
	newton_catch_all
	{
		result = kError_Bus_Access;
	}
	end_try;
	if (result != noErr)
		InitializePCMCIABus(socket);
	return result;
}


// Where a message the server holds back is kept: the token to answer and a
// copy of the message.
static void
DeferMessage(TCardServer* server, TUMsgToken* token, TCardMessage* message, TCardSocketState* state)
{
	state->fDeferred = *token;
	*(TCardMessage*) &state->fDeferredMsg = *message;
	// the ROM copies the word after the message too - the flags of a
	// TCardAsyncMsg, which the sender did not send, so whatever the
	// receive buffer last held there - and the two object ids after that
	// into the copy's asynchronous message (DEVIATION: not those: they are
	// never used, and a host object's ids are its own to keep)
	state->fDeferredMsg.fFlags = ((TCardAsyncMsg*) message)->fFlags;
	server->AEDeferReply();
}


// ROM 0x00052e10 DoCommand__11TCardServerFP10TUMsgTokenPUlP12TCardMessageUc
// A message to the server (or, completion, the answer to one it sent).  The
// server answers most at once; what it has to do something about, it
// does; an error is told to the application (3).
long
TCardServer::DoCommand(TUMsgToken* token, ULong* /*size*/, TCardMessage* message, UChar completion)
{
	long result = noErr;
	long replyData;
	ULong replyType;

	// the messages made when the server's own were out, back and free
	CListIterator iter(&fExtraMessages);
	for (TCardAsyncMsg* extra = (TCardAsyncMsg*) iter.FirstItem(); extra != nil; extra = (TCardAsyncMsg*) iter.NextItem())
	{
		if ((extra->fFlags & TCardAsyncMsg::kInUse) == 0)
		{
			fExtraMessages.Remove(extra);
			delete extra;
		}
	}

	ULong socketNumber = message->fSocket;
	if (socketNumber >= gNumberOfHWSockets)
	{
		ReplyMessage(message, message->fType, socketNumber, (ULong) kError_Bad_Parameters);
		return kError_Bad_Parameters;
	}
	TCardSocket* socket = gCardSockets[socketNumber];
	TCardSocketState* state = gSocketStates[socketNumber];
	TUPort* port = &fProcessorPort;
	ULong type;

	switch (message->fType)
	{
	case kCardProcessorFormatCIS:
		// the processor has written the card a new CIS: the deferred 0x3c answered
		ReplyMessage(&state->fDeferredMsg, message->fData == 0 ? 2 : 3, socketNumber, message->fData);
		result = state->fDeferred.ReplyRPC(&state->fDeferredMsg, kCardMessageSize, noErr);
		state->fDeferredMsg.Free();
		goto tell;

	case kCardServerPollSocket:
		ReplyMessage(message, 2, socketNumber, 0);
		result = DoPollLockSwitchAndCardDetected(socketNumber, socket, state);
		goto tell;

	case kCardServerDeviceTypes:
		ReplyMessage(message, kCardServerDeviceTypes, socketNumber, state->fDeviceTypes[0]);
		for (ULong i = 0; i < 4; i++)
			message->fDevices[i].fType = state->fDeviceTypes[i];
		goto tell;

	case kCardServerEject:
		result = DoCardEjection(socketNumber, socket, state);
		goto tell;

	case kCardServerCardReady:
	{
		// waiting for the card to be ready for its CIS to be read, a try
		// every 100 ms for five seconds - then it is read anyway
		ReplyMessage(message, 2, socketNumber, 0);
		ULong pins = socket->GetPCPins();
		if ((pins & 3) != 3 || (pins & kCardEjectSwitchUp) != 0)
			goto tell;
		VccOn(socketNumber, false);
		if (state->fReadyTries != 0)
			state->fReadyTries--;
		Boolean ready = socket->IsReady();
		if (fField1A24 != 0 || (!ready && state->fReadyTries != 0))
		{
			TTime when = TimeFromNow(kReadyDelay);
			result = SendSelfMessage(kCardServerCardReady, socketNumber, 0, 0, &when);
		}
		else
			result = SendMessage(port, kCardProcessorRecognize, socketNumber, 0, 0, nil);
		VccOff(socketNumber);
		goto tell;
	}

	case kCardServerCardGone:
		goto tell;

	case kCardProcessorBattery:
	case kCardProcessorRecognize:
	case kCardProcessorResume:
	case kCardProcessorRemove:
		// the processor's answer: its error
		result = message->fData;
		goto tell;

	case kCardServerUnmount:
	case kCardProcessorUnmount:
		VccOn(socketNumber, false);
		if (!completion)
		{
			// put the card away: its client (the PSS manager) lets it go
			// first, then the processor removes its handler; the answer
			// waits for both
			if ((state->fDeferredMsg.fFlags & TCardAsyncMsg::kInUse) == 0)
			{
				DeferMessage(this, token, message, state);
				if (state->fClientPort != 0)
				{
					type = kCardServerUnmount;
					port = &state->fClientPort;
				}
				else
					type = kCardProcessorUnmount;
				result = SendMessage(port, type, socketNumber, 0, 0, nil);
			}
			else
				ReplyMessage(message, kCardServerUnmount, socketNumber, (ULong) kError_Message_Already_Posted);
		}
		else if (message->fType == kCardServerUnmount)
		{
			// the client has let it go: now the processor
			state->fDeferredMsg.fData = message->fData;
			result = SendMessage(port, kCardProcessorUnmount, socketNumber, 0, 0, nil);
		}
		else
		{
			// the processor has done it: the unmount answered
			if (state->fDeferredMsg.fData == 0)
				state->fDeferredMsg.fData = message->fData;
			result = state->fDeferred.ReplyRPC(&state->fDeferredMsg, kCardMessageSize, noErr);
			state->fDeferredMsg.Free();
		}
		VccOff(socketNumber);
		goto tell;

	case kCardServerCardBackDone:
		ReplyMessage(message, 2, socketNumber, 0);
		state->fCardState = TCardSocketState::kCardActive;
		result = SendMessage(port, kCardProcessorResume, socket->SocketNumber(), 0, 0, nil);
		goto tell;

	case 0x38:
		goto tell;

	case kCardServerFormatCIS:
	{
		TCardPCMCIA* card = state->fCard;
		result = noErr;
		if (card == nil || state->fHandler == nil)
		{
			replyData = result = kError_Format_Failed;
			break;
		}
		if (!card->fBadCIS)
			goto done;
		if ((state->fDeferredMsg.fFlags & TCardAsyncMsg::kInUse) != 0)
		{
			replyData = result = kError_Message_Already_Posted;
			break;
		}
		DeferMessage(this, token, message, state);
		replyData = result = SendMessage(port, kCardProcessorFormatCIS, socketNumber, 0, 0, nil);
		if (replyData == noErr)
			goto done;
		break;
	}

	case kCardServerResetIdle:
		fEventHandler->ResetIdle();
		goto done;

	case kCardServerClientPort:
		if (fClientPort == 0)
		{
			fClientPort.CopyObject((TObjectId) message->fData);
			ReplyMessage(message, 2, socketNumber, 0);
			InitializeCardDetection();
			goto tell;
		}
		if (fClientPort != (TObjectId) message->fData)
		{
			replyData = kError_Already_Registered;
			break;
		}
		goto done;

	case kCardServerBatteryCheck:
		ReplyMessage(message, 2, socketNumber, 0);
		result = SendMessage(port, kCardProcessorBattery, socketNumber, 0, 0, nil);
		goto tell;

	case kCardMessageCardAvailable:
		// the PSS manager takes the card on, with the window it sees it through
		if (state->fClientPort != 0)
		{
			if (state->fClientPort != (TObjectId) message->fData)
			{
				replyData = kError_Already_Registered;
				break;
			}
			goto done;
		}
		if ((message->fField24 & 0xFFF) == 0 && (message->fField28 & 0xFFF) == 0)
		{
			state->fClientPort.CopyObject((TObjectId) message->fData);
			state->fPSSBase = message->fField24;
			state->fPSSSize = message->fField28;
			goto done;
		}
		replyData = kError_Bad_Parameters;
		break;

	case 0:
	case 2:
		goto tell;

	case kCardServerCheckBattery:
		result = SendMessage(&fClientPort, kCardServerCheckBattery, socketNumber, message->fData, 0, nil);
		goto done;

	case kCardServerClientDone:
		ReplyMessage(message, 2, socketNumber, 0);
		state->fCardState = TCardSocketState::kCardNone;
		result = SendMessage(port, kCardProcessorRemove, socketNumber, 0, 0, nil);
		goto tell;

	case kCardMessageTaskBlocked:
		ReplyMessage(message, 2, socketNumber, 0);
		state->fCardState = TCardSocketState::kCardTaskBlocked;
		// the reinsert alert put up, unless it is up already
		if ((fAlertFlags & 0x80000000) == 0 && fAlertPort != 0)
		{
			TCardReinsertAlertDialog* dialog = (TCardReinsertAlertDialog*) fReinsertEvent->fDialog;
			dialog->SetFilterData((void*) (uintptr_t) socketNumber);
			dialog->Setup();
			result = fAlertPort.SendRPC(&fReinsertAsync, fReinsertEvent, sizeof(TCardAlertEvent), nil, 0);
			fAlertFlags |= 0x80000000;
		}
		goto tell;

	default:
		// (0x33, 0x36, 0x69 and anything else: taken)
		goto done;
	}
	// refused
	replyType = 3;
	ReplyMessage(message, replyType, socketNumber, replyData);
	goto tell;

done:
	ReplyMessage(message, 2, socketNumber, 0);

tell:
	if (result != noErr)
		SendMessage(&fClientPort, kCardServerCheckBattery, socketNumber, result, 0, nil);
	return result;
}


// ROM 0x000537ac DoSysEventPowerOff__11TCardServerFP7TAEvent
// The machine going to sleep: every card handler suspends its services and
// the sockets' power goes off; the power-off answered.
void
TCardServer::DoSysEventPowerOff(TAEvent* /*event*/)
{
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
	fSystemHandler->ReplyPowerOff();
}


// ROM 0x0005387c DoSysEventPowerOn__11TCardServerFP7TAEvent
// Waking up: each socket's power restored and its handler's services
// resumed.
NewtonErr
TCardServer::DoSysEventPowerOn(TAEvent* /*event*/)
{
	NewtonErr err = noErr;
	newton_try
	{
		for (ULong socket = 0; socket < gNumberOfHWSockets; socket++)
		{
			RestoreCardPower(socket);
			TCardSocketState* state = gSocketStates[socket];
			if (state->fHandler != nil)
				err = state->fHandler->ResumeServices(gCardSockets[socket], state->fCard, state->fConfigNumber);
		}
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	return err;
}


// ROM 0x000544ec GetSocketInfo__FUlPP11TCardSocketPP11TCardPCMCIA
// A socket's TCardSocket and the card in it as last read.
NewtonErr
GetSocketInfo(ULong socket, TCardSocket** cardSocket, TCardPCMCIA** card)
{
	if (socket < gNumberOfHWSockets && cardSocket != nil && card != nil)
	{
		*cardSocket = gCardSockets[socket];
		*card = gSocketStates[socket]->fParsedCard;
		return noErr;
	}
	if (cardSocket != nil)
		*cardSocket = nil;
	if (card == nil)
		return kError_Bad_Parameters;
	*card = nil;
	return kError_Bad_Parameters;
}


// ROM 0x00054d10 InitCardServices__Fv
// The card server started (TLoader::TheMain does it): the domains' fault
// monitor first, then the 'cdsv' world.
NewtonErr
InitCardServices(void)
{
	TCardDomains domains;
	TCardServer server;
	NewtonErr err = domains.Init();
	if (err == noErr)
		err = server.Init('cdsv', true, 6000);
	return err;
}
