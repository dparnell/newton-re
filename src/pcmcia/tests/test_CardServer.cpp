// The card server (CardServer.h) over the host's sockets: started as the
// ROM starts it (InitCardServices), told the application's port (message
// 100), and then a card put in - the server polls the pins, powers the
// card, has the card processor read its CIS and the memory card handler
// install its flash, and tells the system of a new card (a 'card' system
// event with the flash device in it).  Taken out: the application is told
// (0x33), and with nobody holding the card the processor removes the
// handler's services.  Put back: a new card again.  Run as the kernel
// services task (the server and the processor are app worlds).

#include "CardServer.h"
#include "CardSocket.h"
#include "HostCard.h"
#include "CardPower.h"
#include "CHMemModem.h"
#include "OSErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "UserPorts.h"
#include "SystemEvents.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char*	kCardFile = "test_CardServer.card";

extern ULong	CardVccCount(int socket);		// CardPower.cpp


// The next message on the port (answered as the server's own handlers
// answer: with the message itself), or false after a few seconds.
static Boolean
Next(TUPort& port, TCardMessage* message)
{
	char buffer[0x400];
	ULong size = 0;
	TUMsgToken token;
	ULong type = 0;
	if (port.Receive(&size, buffer, sizeof(buffer), &token, &type, 5 * kSeconds) != noErr)
		return false;
	memcpy(message, buffer, size < sizeof(TCardMessage) ? size : sizeof(TCardMessage));
	if (token.GetReplyId() != 0)
		token.ReplyRPC(buffer, size < sizeof(TCardMessage) ? size : sizeof(TCardMessage), noErr);
	return true;
}


// The next message of the type (others answered and passed over).
static Boolean
Expect(TUPort& port, ULong type, TCardMessage* message)
{
	for (int i = 0; i < 20; i++)
	{
		if (!Next(port, message))
			return false;
		printf("  got %08lx socket %lu data %lx\n", (unsigned long) message->fType,
			   (unsigned long) message->fSocket, (unsigned long) message->fData);
		if (message->fType == type)
			return true;
	}
	return false;
}


static void
CardServerScenario(void)
{
	EXPECT(HostCardCreate(kCardFile, 4, "Test") == noErr);
	EXPECT(InitCardServices() == noErr);
	EXPECT(gCardServer != nil);
	EXPECT(gNumberOfHWSockets == kHostCardSockets);
	EXPECT(gSocketStates[0] != nil && gSocketStates[0]->fHandlers.Count() == 1);

	// the application: a port for the server's news, and the new card events
	TUPort app;
	EXPECT(app.Init() == noErr);
	TSystemEvent cardEvent(kSysEvent_NewICCard);
	EXPECT(cardEvent.RegisterForSystemEvent(app.fId, 0, kNoTimeout) == noErr);
	TUNameServer nameServer;
	TObjectId serverId = 0;
	ULong spec;
	EXPECT(nameServer.Lookup("cdsv", "TUPort", &serverId, &spec) == noErr);
	TUPort server(serverId);
	TCardMessage hello;
	hello.MessageStuff(kCardServerClientPort, 0, app.fId);
	TCardMessage reply;
	ULong replySize = 0;
	EXPECT(server.SendRPC(&replySize, &hello, sizeof(hello), &reply, sizeof(reply)) == noErr);
	EXPECT(reply.fType == 2 && reply.fData == 0);
	// a second application is refused
	hello.MessageStuff(kCardServerClientPort, 0, app.fId + 1);
	EXPECT(server.SendRPC(&replySize, &hello, sizeof(hello), &reply, sizeof(reply)) == noErr);
	EXPECT(reply.fType == 3 && (long) reply.fData == kError_Already_Registered);

	// in: the handler installed, the system told
	printf("inserting\n");
	EXPECT(HostCardInsert(0, kCardFile) == noErr);
	TCardMessage news;
	EXPECT(Expect(app, kSysEvent_NewICCard, &news));
	EXPECT(news.fAEventID == kAESystemEventID && news.fSocket == 0);
	EXPECT(news.fDevices[0].fType == 'flsh' && news.fDevices[0].fSize == 0x400000 && news.fDevices[0].fDriver != nil);
	EXPECT(news.fField14 == 'flsh' && news.fField28 == 0x400000);
	EXPECT(news.fDevices[1].fType == 0);
	TCardSocketState* state = gSocketStates[0];
	EXPECT(state->fHandler != nil && state->fCard != nil && state->fParsedCard != nil);
	EXPECT(state->fDeviceTypes[0] == 'flsh');
	EXPECT(state->fCardState == TCardSocketState::kCardActive && state->fState == TCardSocketState::kSocketCardIn);
	EXPECT(strcmp(state->fIdString, "Newton hostTest150 ns 4096K byt") == 0);
	TCardSocket* socket = nil;
	TCardPCMCIA* card = nil;
	EXPECT(GetSocketInfo(0, &socket, &card) == noErr && socket == gCardSockets[0] && card == state->fParsedCard);
	EXPECT(GetSocketInfo(2, &socket, &card) == kError_Bad_Parameters && socket == nil && card == nil);

	// the device types, asked
	TCardMessage ask;
	ask.MessageStuff(kCardServerDeviceTypes, 0, 0);
	EXPECT(server.SendRPC(&replySize, &ask, sizeof(ask), &reply, sizeof(reply)) == noErr);
	EXPECT(reply.fType == kCardServerDeviceTypes && reply.fData == 'flsh' && reply.fDevices[0].fType == 'flsh');

	// out: the application told, the handler's services removed
	printf("removing\n");
	HostCardRemove(0);
	EXPECT(Expect(app, kCardServerCardRemoved, &news));
	EXPECT(news.fSocket == 0);
	for (int i = 0; i < 50 && state->fHandler != nil; i++)
		Sleep(20 * kMilliseconds);
	EXPECT(state->fHandler == nil && state->fCard == nil && state->fParsedCard == nil);
	EXPECT(state->fCardState == TCardSocketState::kCardNone && state->fState == TCardSocketState::kSocketEmpty);
	EXPECT(CardVccCount(0) == 0);

	// in again
	printf("inserting again\n");
	EXPECT(HostCardInsert(0, kCardFile) == noErr);
	EXPECT(Expect(app, kSysEvent_NewICCard, &news));
	EXPECT(news.fDevices[0].fType == 'flsh');
	EXPECT(state->fHandler != nil);

	HostCardRemove(0);
	EXPECT(Expect(app, kCardServerCardRemoved, &news));
	remove(kCardFile);
	printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = CardServerScenario;
	OsBoot();
	return failures != 0;
}
