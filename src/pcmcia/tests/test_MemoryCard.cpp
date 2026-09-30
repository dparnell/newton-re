// A memory card from the socket up to its store: the host's blank flash
// card (hal/host/HostCard.h) parsed (PCMCIA20Parser.h), recognised and
// installed by the ROM's memory card handler (CHMemModem.h), which makes a
// TFlashSeries2 (stores/flash/CardFlash.h) of the geometry the ROM works
// out for two 28F016SA chips; a TFlashStore made on it as the PSS manager
// makes a card's (kFlashStoreIsCard, an SPSSStoreInfo), formatted, written,
// and read back after the card has been out and in again; the card power
// counted on and off.  Run as the kernel services task.

#include "CardSocket.h"
#include "HostCard.h"
#include "PCMCIA20Parser.h"
#include "CHMemModem.h"
#include "CardPower.h"
#include "CardServerGlobals.h"
#include "CardFlash.h"
#include "FlashStore.h"
#include "PSSManager.h"
#include "OSErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char*	kCardFile = "test_MemoryCard.card";

extern ULong	CardVccCount(int socket);		// CardPower.cpp
extern Boolean	CardVccIsOn(int socket);


// the card's handler installed and its one device answered, as the card
// server does for a new card
static TCHMemModem*
InstallCard(TCardSocket* socket, TCardPCMCIA* card, TFlash** flash, SPSSStoreInfo* info)
{
	TPCMCIA20Parser parser;
	EXPECT(parser.ParsePCCardCIS(card, socket) == noErr);
	TCHMemModem* handler = (TCHMemModem*) TCardHandler::New((char*) "TCHMemModem");
	EXPECT(handler != nil);
	if (handler == nil)
		return nil;
	EXPECT(handler->RecognizeCard(socket, card) == noErr);
	EXPECT(handler->InstallServices(socket, card, 0) == noErr);
	EXPECT(handler->GetNumberOfDevice() == 1);
	ULong type = 0, offset = 0, size = 0;
	TObjectId phys = 1;
	void* driver = nil;
	handler->GetDeviceInfo(0, &type, &phys, &driver, &offset, &size);
	EXPECT(type == 'flsh' && driver != nil && offset == 0 && size == 0x400000 && phys == 0);
	*flash = (TFlash*) driver;
	info->Clear();
	info->fFlash = (TFlash*) driver;
	info->fSize = size;
	info->fSocket = 0;
	info->fOffset = offset;
	info->fBase = (char*) socket->CommonMemBaseAddr() + offset;
	info->fType = type;
	info->fCardHandler = handler;
	return handler;
}


static void
MemoryCardScenario(void)
{
	TFlashStore::ClassInfo()->Register();
	TFlashSeries2::ClassInfo()->Register();
	TCHMemModem::ClassInfo()->Register();

	TCardSocket* socket = new TCardSocket(0);
	EXPECT(socket->Init() == noErr);
	gCardSockets[0] = socket;
	gNumberOfHWSockets = 1;
	EXPECT(InitVppManager() == noErr);

	EXPECT(HostCardCreate(kCardFile, 4, "Test") == noErr);
	EXPECT(HostCardInsert(0, kCardFile) == noErr);

	TCardPCMCIA card;
	TFlash* flash = nil;
	SPSSStoreInfo info;
	TCHMemModem* handler = InstallCard(socket, &card, &flash, &info);
	if (handler == nil || flash == nil)
	{
		HostStopTasks();
		return;
	}
	EXPECT(strcmp(handler->CardIdString(&card), "Newton hostTest150 ns 4096K byt") == 0);
	EXPECT(handler->CardStatus() == 3);				// both batteries good, not protected, ready
	EXPECT(flash->GetVendorInfo() == 0x89A0);
	EXPECT(flash->GetEraseRegionSize() == 0x20000);	// two chips side by side, 64 KB blocks
	EXPECT(flash->GetGroupSize() == 0x400000);
	EXPECT(flash->GetTotalSize() == 0x400000 && flash->GetDataOffset() == 0);
	EXPECT(flash->GetReadAccessTime() == 150);
	UChar isProtected = true;
	flash->GetWriteProtected(&isProtected);
	EXPECT(!isProtected);

	// a flash write clears bits; an erase sets a region
	char bytes[4] = { 0x12, 0x34, 0x56, 0x78 };
	EXPECT(flash->Write(0x20004, 4, bytes) == noErr);
	char back[4];
	EXPECT(flash->Read(0x20004, 4, back) == noErr && memcmp(back, bytes, 4) == 0);
	EXPECT(flash->Erase(0x3FFFF) == noErr);
	EXPECT(flash->Read(0x20004, 4, back) == noErr && (UChar) back[0] == 0xFF);

	// the card's store
	TStore* store = TStore::New("TFlashStore");
	EXPECT(store != nil);
	EXPECT(store->Init(info.fBase, info.fSize, 0, 0, kFlashStoreIsCard, &info) == noErr);
	Boolean needsFormat = false;
	EXPECT(store->NeedsFormat(&needsFormat) == noErr && needsFormat);
	EXPECT(store->Format() == noErr);
	EXPECT(store->NeedsFormat(&needsFormat) == noErr && !needsFormat);
	PSSId id = 0;
	char text[] = "written on a card";
	EXPECT(store->NewObject(text, sizeof(text), &id) == noErr && id != 0);
	long total = 0, used = 0;
	EXPECT(store->GetStoreSizes(&total, &used) == noErr && total > 0x300000);
	EXPECT(!CardVccIsOn(0) || CardVccCount(0) == 0);	// counted off again (the countdown may keep it on)
	store->Delete();
	handler->Delete();

	// out, and in again
	HostCardRemove(0);
	EXPECT(!socket->IsCardDetected());
	EXPECT(HostCardInsert(0, kCardFile) == noErr);
	TCardPCMCIA again;
	handler = InstallCard(socket, &again, &flash, &info);
	store = TStore::New("TFlashStore");
	EXPECT(store->Init(info.fBase, info.fSize, 0, 0, kFlashStoreIsCard, &info) == noErr);
	EXPECT(store->NeedsFormat(&needsFormat) == noErr && !needsFormat);
	char read[sizeof(text)];
	memset(read, 0, sizeof(read));
	EXPECT(store->Read(id, 0, read, sizeof(read)) == noErr && strcmp(read, text) == 0);
	store->Delete();
	handler->Delete();

	// write-protected: the store says so
	HostCardRemove(0);
	EXPECT(HostCardInsert(0, kCardFile, true) == noErr);
	TCardPCMCIA locked;
	handler = InstallCard(socket, &locked, &flash, &info);
	store = TStore::New("TFlashStore");
	EXPECT(store->Init(info.fBase, info.fSize, 0, 0, kFlashStoreIsCard, &info) == noErr);
	Boolean readOnly = false;
	EXPECT(store->IsReadOnly(&readOnly) == noErr && readOnly);
	memset(read, 0, sizeof(read));
	EXPECT(store->Read(id, 0, read, sizeof(read)) == noErr && strcmp(read, text) == 0);
	store->Delete();
	handler->Delete();
	HostCardRemove(0);
	remove(kCardFile);
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = MemoryCardScenario;
	OsBoot();
	if (failures == 0)
		printf("test_MemoryCard: all passed\n");
	else
		printf("test_MemoryCard: %d failures\n", failures);
	return failures != 0;
}
