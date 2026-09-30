/*
	File:		thirdparty/nie/LanternCardHandler.cpp

	Contains:	TLanternCardHandler (LanternCardHandler.h), re-expressed from
				newtdev.pkg part 4's ARM (tools/newton-rom/analysis/
				pkgdisasm.py fixtures/packages/apple/NIE2/ENETSUP/newtdev.pkg
				--part 4 --rom build/MP2x00US).
*/

#include "LanternCardHandler.h"
#include "ProtocolStandIns.h"
#include "CardSocket.h"
#include "CardPower.h"
#include "UserPorts.h"
#include "NameServer.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "ByteOrder.h"
#include "../../ddk/CardPCMCIA.h"

#include <stdio.h>
#include <string.h>

// the card's interrupt, as the socket numbers it (TSocketInt 4)
const TSocketInt	kLanternInterrupt = (TSocketInt) 4;

// what the handler answers for a card that is not its
const NewtonErr		kLanternNotMine = -10501;
// a card that would not come ready
const NewtonErr		kLanternNotReady = -10502;
// no event world to make
const NewtonErr		kLanternNoWorld = -61001;


/*------------------------------------------------------------------------------
	T h e   e v e n t   w o r l d ' s   p r o t o c o l

	The Ethernet driver's event world (newtdev.pkg part 5, TLanternEventWorld)
	implements TEventWorldAPI; the handler asks it only its first method, with
	the card's id string and type.  Its name is not in the part; NOT YET (the
	world itself): the host has no implementation, so NewByName answers nil.
------------------------------------------------------------------------------*/

PROTOCOL TEventWorldAPI : public TProtocol
{
public:
	VIRTUAL NewtonErr	Start(char* cardId, ULong cardType) ENDVIRTUAL;
};


/*------------------------------------------------------------------------------
	M e s s a g e s   t o   t h e   e v e n t   w o r l d

	Each is a type ('lant'), a kind ('newC', 'delC', 'susp', 'resm'), a
	count and that many words, sent to the world's port.
------------------------------------------------------------------------------*/

// NIE newtdev.pkg part 4 +0x0d8 (unnamed) - LookupPort
// The port the name server has under the name, as a TUPort made for it if
// there is none yet (nought when there is no such port).
static TUPort*
LookupPort(TUPort* port, char* name)
{
	if (port == nil)
	{
		port = new TUPort;
		if (port == nil)
			return nil;
	}
	TUNameServer nameServer;
	ULong value, spec;
	if (nameServer.Lookup(name, (char*) "TUPort", &value, &spec) != noErr)
		value = 0;
	TUPort found((TObjectId) value);
	port->CopyObject(found);
	return port;
}


// NIE newtdev.pkg part 4 +0x194 (unnamed) - SendToWorld
// The message sent asynchronously on the async message, its content kept in
// a buffer the message holds as its reference constant (made the first time).
static NewtonErr
SendToWorld(TUPort* port, TUAsyncMessage* message, ULong type, ULong kind, ULong count, const ULong* args)
{
	ULong size = 12 + count * 4;
	ULong refCon;
	message->GetUserRefCon(&refCon);
	ULong* buffer = (ULong*) refCon;
	if (buffer == nil)
	{
		buffer = (ULong*) operator new(size);
		message->SetUserRefCon((ULong) buffer);
	}
	buffer[0] = type;
	buffer[1] = kind;
	buffer[2] = count;
	for (ULong i = 0; i < count; i++)
		buffer[3 + i] = args[i];
	return port->Send(message, buffer, size);
}


// NIE newtdev.pkg part 4 +0x238 (unnamed) - CallWorld
// The message sent as an RPC with no reply.
static NewtonErr
CallWorld(TUPort* port, ULong type, ULong kind, ULong count, const ULong* args)
{
	ULong content[3 + 8];
	content[0] = type;
	content[1] = kind;
	content[2] = count;
	for (ULong i = 0; i < count; i++)
		content[3 + i] = args[i];
	ULong returnSize;
	return port->SendRPC(&returnSize, content, 12 + count * 4, nil, 0);
}


/*------------------------------------------------------------------------------
	T L a n t e r n C a r d H a n d l e r
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TLanternCardHandler)						// NIE newtdev.pkg part 4 +0x2a8 Sizeof
PROTOCOL_CLASSINFO(TLanternCardHandler, "TCardHandler", "", 0x10000, 0, nil)	// NIE newtdev.pkg part 4 +0x000 ClassInfo


// NIE newtdev.pkg part 4 +0x2b0 New
// Everything nought, then four async messages, the last three with no
// reference constant yet.  ==> nil when one cannot be made (the ones made
// kept, as the part keeps them).
TLanternCardHandler*
TLanternCardHandler::New(void)
{
	fInstalled = false;
	fWorldPort = nil;
	fIdString = nil;
	fInterruptMessage = nil;
	fSuspendMessage = nil;
	fRemoveMessage = nil;
	fResumeMessage = nil;
	if ((fInterruptMessage = new TUAsyncMessage) == nil)
		return nil;
	fInterruptMessage->Init(false);
	if ((fSuspendMessage = new TUAsyncMessage) == nil)
		return nil;
	fSuspendMessage->Init(false);
	fSuspendMessage->SetUserRefCon(0);
	if ((fRemoveMessage = new TUAsyncMessage) == nil)
		return nil;
	fRemoveMessage->Init(false);
	fRemoveMessage->SetUserRefCon(0);
	if ((fResumeMessage = new TUAsyncMessage) == nil)
		return nil;
	fResumeMessage->Init(false);
	fResumeMessage->SetUserRefCon(0);
	return this;
}


// NIE newtdev.pkg part 4 +0x4c8 Delete
// The port and the messages given back, each message's buffer with it (the
// remove message's once its send is done), and the id string.
void
TLanternCardHandler::Delete(void)
{
	fInstalled = false;
	if (fWorldPort != nil)
		delete fWorldPort;
	if (fInterruptMessage != nil)
		delete fInterruptMessage;
	ULong refCon;
	if (fSuspendMessage != nil)
	{
		fSuspendMessage->GetUserRefCon(&refCon);
		if (refCon != 0)
			operator delete((void*) refCon);
		delete fSuspendMessage;
	}
	if (fResumeMessage != nil)
	{
		fResumeMessage->GetUserRefCon(&refCon);
		if (refCon != 0)
			operator delete((void*) refCon);
		delete fResumeMessage;
	}
	if (fRemoveMessage != nil)
	{
		fRemoveMessage->BlockTillDone(nil, nil, nil, nil);
		fRemoveMessage->GetUserRefCon(&refCon);
		if (refCon != 0)
			operator delete((void*) refCon);
		delete fRemoveMessage;
	}
	if (fIdString != nil)
	{
		DisposPtr(fIdString);
		fIdString = nil;
	}
}


// NIE newtdev.pkg part 4 +0x694 RecognizeCard
// The card is this handler's when the name server has its id string under
// "Lantern"; the first word of what is registered is the card's type.
NewtonErr
TLanternCardHandler::RecognizeCard(TCardSocket* /*socket*/, TCardPCMCIA* card)
{
	TUNameServer nameServer;
	CardIdString(card);
	// (the part's printf, which goes to the debugging serial port: the
	// host's stderr)
	fprintf(stderr, "RecognizeCardID: [%s]\n", fIdString);
	ULong spec;
	if (nameServer.Lookup(fIdString, (char*) "Lantern", &fLookedUp, &spec) != noErr)
		return kLanternNotMine;
	fCardType = GetBigEndianWord((const void*) spec);
	return noErr;
}


// NIE newtdev.pkg part 4 +0x438 ParseUnrecognizedCard
// A card with no CIS is never one.
NewtonErr
TLanternCardHandler::ParseUnrecognizedCard(TCardSocket* /*socket*/, TCardPCMCIA* /*card*/)
{
	return kLanternNotMine;
}


// NIE newtdev.pkg part 4 +0x75c InstallServices
// The card's services started: its id string, how long a reset waits for
// it, its I/O interface selected; the event world's port looked up by the
// id string - an old world there told to die ('lant' 'die!') - and, when
// there is none (or it would not die), a TLanternEventWorld made and started
// for the card and its port looked up again (four times, a twentieth of a
// second apart); then the card reset, its interrupt registered, and the
// world told of it ('lant' 'newC': the handler, the id string, what the
// name server had, the socket, the card and the configuration), the
// interrupt taken back when that fails.
// PART QUIRKS kept: a port that cannot be made, or a world whose port never
// appears, answers noErr with nothing started.
NewtonErr
TLanternCardHandler::InstallServices(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber)
{
	NewtonErr err = noErr;
	CardIdString(card);
	fInstalled = true;
	fCard = card;
	fSocket = socket;
	fReadyRetries = ReadyRetries();
	SelectIO();
	if ((fWorldPort = LookupPort(nil, fIdString)) == nil)
		return err;
	Boolean haveWorld = false;
	if (fWorldPort->fId != 0)
	{
		ULong none[1] = { 0 };
		NewtonErr dieErr = CallWorld(fWorldPort, 'lant', 'die!', 1, none);
		if (fWorldPort->fId != 0 && dieErr == noErr)
			haveWorld = true;
	}
	if (!haveWorld)
	{
		TEventWorldAPI* world = (TEventWorldAPI*) NewByName("TEventWorldAPI", "TLanternEventWorld");
		if (world == nil)
			return kLanternNoWorld;
		err = world->Start(fIdString, fCardType);
		world->GlueDelete();
		if (err != noErr)
			return err;
		ULong tries;
		for (tries = 0; tries < 4; tries++)
		{
			if (fWorldPort != nil)
				LookupPort(fWorldPort, fIdString);
			if (fWorldPort->fId != 0)
				break;
			Sleep(0x2cfec);				// (a twentieth of a second)
		}
		if (tries >= 4)
			return err;
	}
	if ((err = ResetCard(fSocket)) != noErr)
		return err;
	if ((err = CardSpecific(0xa, (void*) CardInterrupt, (ULong) this)) != noErr)
		return err;
	ULong args[6] = { (ULong) this, (ULong) fIdString, fLookedUp, (ULong) socket, (ULong) card, configNumber };
	if ((err = CallWorld(fWorldPort, 'lant', 'newC', 6, args)) == noErr)
		return noErr;
	CardSpecific(0xb, nil, 0);
	return err;
}


// NIE newtdev.pkg part 4 +0x95c RemoveServices
// The interrupt taken back and the world told the card has gone ('lant'
// 'delC' with the id string).
// PART BUG kept: the count says ten words where one is given; the part
// sends whatever lies on its stack after the id string, the host noughts
// (DEVIATION: it cannot send another stack's words).
NewtonErr
TLanternCardHandler::RemoveServices(void)
{
	CardSpecific(0xb, nil, 0);
	ULong args[10] = { (ULong) fIdString, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	return SendToWorld(fWorldPort, fRemoveMessage, 'lant', 'delC', 10, args);
}


// NIE newtdev.pkg part 4 +0xafc SuspendServices
NewtonErr
TLanternCardHandler::SuspendServices(void)
{
	return SendToWorld(fWorldPort, fSuspendMessage, 'lant', 'susp', 0, nil);
}


// NIE newtdev.pkg part 4 +0xb30 ResumeServices
// The I/O interface selected again and the world told ('lant' 'resm').
NewtonErr
TLanternCardHandler::ResumeServices(TCardSocket* /*socket*/, TCardPCMCIA* /*card*/, ULong /*configNumber*/)
{
	SelectIO();
	return SendToWorld(fWorldPort, fResumeMessage, 'lant', 'resm', 0, nil);
}


// NIE newtdev.pkg part 4 +0x94c EmergencyShutdown
NewtonErr
TLanternCardHandler::EmergencyShutdown(void)
{
	fInstalled = false;
	return noErr;
}


// NIE newtdev.pkg part 4 +0x444 FormatCIS
NewtonErr
TLanternCardHandler::FormatCIS(TCardSocket* /*socket*/, TCardPCMCIA* /*card*/)
{
	return noErr;
}


// NIE newtdev.pkg part 4 +0x38c CardIdString
// The card's product name, copied the first time it is asked (nil for no
// card).
char*
TLanternCardHandler::CardIdString(TCardPCMCIA* card)
{
	if (card == nil)
		return nil;
	if (fIdString == nil)
	{
		const char* product = card->GetCardProduct();
		fIdString = (char*) NewPtr(strlen(product) + 1);
		strcpy(fIdString, product);
	}
	return fIdString;
}


// NIE newtdev.pkg part 4 +0x384 CardStatus
ULong
TLanternCardHandler::CardStatus(void)
{
	return 3;
}


// NIE newtdev.pkg part 4 +0x3e0 GetNumberOfDevice
ULong
TLanternCardHandler::GetNumberOfDevice(void)
{
	return 1;
}


// NIE newtdev.pkg part 4 +0x3e8 GetDeviceInfo
// Its one device: the card's type, and nothing else.
void
TLanternCardHandler::GetDeviceInfo(ULong deviceNumber, ULong* cardType, TObjectId* cardPhys, void** cardDriverInfo, ULong* deviceOffset, ULong* deviceSize)
{
	if (deviceNumber != 0)
		return;
	*cardType = fCardType;
	*cardPhys = 0;
	*cardDriverInfo = nil;
	*deviceOffset = 0;
	*deviceSize = 0;
}


// NIE newtdev.pkg part 4 +0x428 SetCardServerPort
void
TLanternCardHandler::SetCardServerPort(TObjectId port)
{
	fServerPort = port;
}


// NIE newtdev.pkg part 4 +0x430 SetRemovableHandler
void
TLanternCardHandler::SetRemovableHandler(Boolean removable)
{
	fRemovable = removable;
}


// NIE newtdev.pkg part 4 +0x420 GetRemovableHandler
Boolean
TLanternCardHandler::GetRemovableHandler(void)
{
	return fRemovable;
}


// NIE newtdev.pkg part 4 +0x9a8 CardSpecific
// The world's requests: 1 power on and reset the card (off again when it
// will not come ready), 2 power off, 9 the card's hardware location ('slt1'
// + the socket), 0xa register the card's interrupt (ptr the procedure,
// something its object), 0xb deregister it, 0xc enable it (ptr non-nil:
// the I/O interface selected and bits 0x41 set in the card's configuration
// option register) or disable and clear it, 0x80 whether the services are
// installed.  ==> kError_Call_Not_Implemented (-10005) for anything else.
long
TLanternCardHandler::CardSpecific(ULong selector, void* ptr, ULong something)
{
	NewtonErr err = noErr;
	if (selector == 9)
	{
		*(ULong*) ptr = 'slt1' + fSocket->SocketNumber();
		return noErr;
	}
	if (selector == 0x80)
	{
		*(UByte*) ptr = fInstalled;
		return noErr;
	}
	switch (selector)
	{
	case 1:
		VccOn((int) fSocket->SocketNumber(), false);
		if ((err = ResetCard(fSocket)) == noErr)
			break;
		VccOff((int) fSocket->SocketNumber(), (TTimeout) 0);
		break;
	case 2:
		VccOff((int) fSocket->SocketNumber(), (TTimeout) 0);
		break;
	case 0xa:
		err = fSocket->RegisterSocketInterrupt(kLanternInterrupt, (IntProcPtr) ptr, (void*) something);
		break;
	case 0xb:
		fSocket->DeregisterSocketInterrupt(kLanternInterrupt);
		break;
	case 0xc:
		if (ptr != nil)
		{
			fSocket->EnableSocketInterrupt(kLanternInterrupt);
			SelectIO();
			// the configuration option register, in the byte lane the bus
			// puts it in (pcmcia/CardCISIterator.cpp's Lane)
			UByte* cor = (UByte*) ((fSocket->AttributeMemBaseAddr() + fCard->fRegisterBaseAddress) ^ 3);
			*cor |= 0x41;
		}
		else
		{
			fSocket->DisableSocketInterrupt(kLanternInterrupt);
			fSocket->ClearSocketInterrupt(kLanternInterrupt);
		}
		break;
	default:
		err = kError_Call_Not_Implemented;
		break;
	}
	return err;
}


// NIE newtdev.pkg part 4 +0x44c (unnamed) - ResetCard
// The PC card bus reset, then up to fReadyRetries tenths of a second for
// the card to come ready, and the I/O interface selected.  ==> -10502 when
// it never did.
// PART QUIRK kept: it waits on the socket it keeps (fSocket) but asks the
// one it was given whether the card came ready.
NewtonErr
TLanternCardHandler::ResetCard(TCardSocket* socket)
{
	socket->PCMCIAReset();
	for (ULong tries = 0; !fSocket->IsReady() && fReadyRetries > tries; tries++)
		Sleep(0x59fd8);					// (a tenth of a second)
	if (!socket->IsReady())
		return kLanternNotReady;
	SelectIO();
	return noErr;
}


// NIE newtdev.pkg part 4 +0x5d8 (unnamed) - SelectIO
// The socket's control register's bit 4 set, and its I/O interface selected.
void
TLanternCardHandler::SelectIO(void)
{
	fSocket->SetControl(fSocket->GetControl() | 0x10);
	fSocket->SelectIOInterface();
}


// NIE newtdev.pkg part 4 +0x608 (unnamed) - ReadyRetries
// How many tenths of a second a reset waits: the card's maximum busy time
// (its first configuration's, in nanoseconds) as milliseconds plus half a
// second, at least five seconds.
ULong
TLanternCardHandler::ReadyRetries(void)
{
	TCardConfiguration* config = fCard->GetCardConfiguration(0);
	ULong milliseconds = (ULong) config->fRdyBsyTimeNSecs / 1000000 + 500;
	if (milliseconds < 5000)
		milliseconds = 5000;
	return milliseconds / 100;
}


// NIE newtdev.pkg part 4 +0x650 (unnamed) - CardInterrupt
// The card's interrupt: the interrupt message sent to the event world.
NewtonErr
TLanternCardHandler::CardInterrupt(void* handler, TCardSocket* /*socket*/)
{
	TLanternCardHandler* self = (TLanternCardHandler*) handler;
	return SendForInterrupt(self->fWorldPort->fId, self->fInterruptMessage->GetMsgId(), 0, nil, 0,
							0x4000000, 0, nil, true);
}


void
RegisterNIEProtocolStandIns(void)
{
	RegisterProtocolStandIn(TLanternCardHandler::ClassInfo());
}
