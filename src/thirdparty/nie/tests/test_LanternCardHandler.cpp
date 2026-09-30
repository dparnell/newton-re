/*
	File:		thirdparty/nie/tests/test_LanternCardHandler.cpp

	Contains:	The NIE's card handler (LanternCardHandler.h), made by name
				as the card server makes it once the stand-in is registered:
				a card whose product name the name server does not have
				under "Lantern" declined (-10501), one it has taken as the
				card's type, what it answers of itself and the card's
				the stand-in found by its part's names
				and not by others.
*/

#include "LanternCardHandler.h"
#include "ProtocolStandIns.h"
#include "CardSocket.h"
#include "HostCard.h"
#include "PCMCIA20Parser.h"
#include "../../../ddk/CardPCMCIA.h"
#include "NameServer.h"
#include "OSErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char*	kCardFile = "test_LanternCardHandler.card";


static void
Scenario(void)
{
	RegisterNIEProtocolStandIns();
	const TClassInfo* info = ProtocolStandInFor("TLanternCardHandler", "TCardHandler");
	EXPECT(info == TLanternCardHandler::ClassInfo());
	EXPECT(ProtocolStandInFor("TLanternCardHandler", "TDriverAPI") == nil);
	EXPECT(IsProtocolStandIn(info) && !IsProtocolStandIn(&failures));
	EXPECT(info != nil && info->Register() == noErr);

	// a memory card in socket 0, its CIS parsed
	TCardSocket socket(0);
	EXPECT(socket.Init() == noErr);
	EXPECT(HostCardCreate(kCardFile, 4, "Blank") == noErr);
	EXPECT(HostCardInsert(0, kCardFile) == noErr);
	TCardPCMCIA card;
	TPCMCIA20Parser parser;
	EXPECT(parser.ParsePCCardCIS(&card, &socket) == noErr);

	TCardHandler* handler = TCardHandler::New((char*) "TLanternCardHandler");
	EXPECT(handler != nil);
	if (handler != nil)
	{
		// not a Lantern card: declined
		EXPECT(handler->RecognizeCard(&socket, &card) == -10501);
		EXPECT(strcmp(handler->CardIdString(&card), "Blank") == 0);
		EXPECT(handler->ParseUnrecognizedCard(&socket, &card) == -10501);
		EXPECT(handler->CardStatus() == 3 && handler->GetNumberOfDevice() == 1);
		EXPECT(handler->FormatCIS(&socket, &card) == noErr);
		handler->SetRemovableHandler(true);
		EXPECT(handler->GetRemovableHandler());
		EXPECT(handler->CardSpecific(0x42) == kError_Call_Not_Implemented);
		handler->Delete();
	}

	// the driver package's registration: the card's name under "Lantern",
	// its spec pointing at the card's type
	static const unsigned char kType[4] = { 'e', 'n', 'e', 't' };
	TUNameServer nameServer;
	EXPECT(nameServer.RegisterName((char*) "Blank", (char*) "Lantern", 7, (ULong) kType) == noErr);
	handler = TCardHandler::New((char*) "TLanternCardHandler");
	EXPECT(handler != nil);
	if (handler != nil)
	{
		EXPECT(handler->RecognizeCard(&socket, &card) == noErr);
		ULong type = 0, offset = 1, size = 1;
		TObjectId phys = 1;
		void* driverInfo = &type;
		handler->GetDeviceInfo(0, &type, &phys, &driverInfo, &offset, &size);
		EXPECT(type == 'enet' && phys == 0 && driverInfo == nil && offset == 0 && size == 0);
		// (InstallServices wants the card's configuration entry, which a
		// Lantern card's CIS has and the host's memory card has not; the
		// requests about the socket want the one it keeps from there)
		UByte installed = 0;
		EXPECT(handler->CardSpecific(0x80, &installed) == noErr && installed == 0);
		EXPECT(handler->EmergencyShutdown() == noErr);
		EXPECT(handler->CardSpecific(0x80, &installed) == noErr && installed == 0);
		handler->Delete();
	}
	nameServer.UnRegisterName((char*) "Blank", (char*) "Lantern");
	info->DeRegister();

	HostCardRemove(0);
	remove(kCardFile);
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;
	OsBoot();
	if (failures == 0)
		printf("test_LanternCardHandler: all passed\n");
	else
		printf("test_LanternCardHandler: %d failures\n", failures);
	return failures != 0;
}
