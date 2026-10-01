// An ATA card (pcmcia/ATA.h, CardATALoader.h) over the host's model of one
// (hal/host/HostATA.cpp): the card's CIS read, TATASimple put in the
// card's memory-mapped configuration and the drive identified, sectors
// read and written by logical block address and by cylinder, head and
// sector, a sector past the end refused, the drive reset and asked its
// power mode; then TCardATALoader's partition map read (a plain one, and
// one inside a PC partition) and its helpers.  What is written is still
// there when the card comes back.  The images are made by
// tools/cards/atacard.py (the ctest's fixtures), with no packages on them -
// the loader's packages are host.NewtonATACard's.  Run as the kernel
// services task (TATA::New asks the protocol registry, a monitor).
//
//   test_ATACard plain.card mbr.card

#include "ATA.h"
#include "CardATALoader.h"
#include "CardSocket.h"
#include "CardPCMCIA.h"
#include "HostCard.h"
#include "PCMCIA20Parser.h"
#include "CardPower.h"
#include "CardServerGlobals.h"
#include "OSErrors.h"
#include "ByteOrder.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"
#include "Host.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char*	gPlainCard = nil;
static const char*	gMBRCard = nil;


static void
TestDriver(TCardSocket* socket)
{
	EXPECT(HostCardInsert(0, gPlainCard) == noErr);
	EXPECT(HostCardIsATA(0) && HostCardATASectors(0) == 4 * 2048);
	TCardPCMCIA card;
	TPCMCIA20Parser parser;
	EXPECT(parser.ParsePCCardCIS(&card, socket) == noErr);
	EXPECT(card.fFunctionId == 4 && card.fNumOfConfigEntry == 1);
	TCardConfiguration* config = card.GetCardConfiguration(0);
	EXPECT(config != nil && config->fInterfaceType == 0 && config->fMemAddresses[0] == 0);
	TCardATALoader loader;
	EXPECT(loader.GetCardType(&card));

	TATA* ata = TATA::New("TATASimple");
	EXPECT(ata != nil);
	if (ata == nil)
		return;
	EXPECT(ata->Initialize(socket, &card, 0) == noErr);
	TATASimple* simple = (TATASimple*) ata;
	EXPECT((simple->fFlags & TATASimple::kIdentified) != 0);
	TATADriveBasicInfo* info = &simple->fDriveInfo[0];
	EXPECT(GetBigEndianHalf(&info->fCapabilities) == 0x0200);
	EXPECT(GetBigEndianHalf(&info->fCurrentHeads) == 4 && GetBigEndianHalf(&info->fCurrentSectorsPerTrack) == 32);
	EXPECT(memcmp(info->fModelNumber, "Newton host ATA card", 20) == 0);

	// the driver descriptor block
	UByte block[0x200 * 3];
	EXPECT(ata->Read(block, 0, 1, kATACmdReadSectors, 0) == noErr);
	EXPECT(block[0] == 'E' && block[1] == 'R' && GetBigEndianWord(block + 4) == 4 * 2048);

	// three sectors written and read back
	UByte pattern[0x200 * 3];
	for (ULong i = 0; i < sizeof(pattern); i++)
		pattern[i] = (UByte) (i * 7 + i / 512);
	EXPECT(ata->Write(pattern, 100, 3, kATACmdWriteSectors, 0) == noErr);
	memset(block, 0, sizeof(block));
	EXPECT(ata->Read(block, 100, 3, kATACmdReadSectors, 0) == noErr);
	EXPECT(memcmp(block, pattern, sizeof(block)) == 0);
	// one in the middle, by cylinder, head and sector
	PutBigEndianHalf(&info->fCapabilities, 0);
	memset(block, 0, sizeof(block));
	EXPECT(ata->Read(block, 101, 1, kATACmdReadSectors, 0) == noErr);
	EXPECT(memcmp(block, pattern + 0x200, 0x200) == 0);
	PutBigEndianHalf(&info->fCapabilities, 0x0200);
	// a drive that is not there
	EXPECT(ata->Read(block, 0, 1, kATACmdReadSectors, 1) == kError_Bad_Parameters);
	UByte mode = 0;
	EXPECT(ata->CheckPowerMode(&mode, 0) == noErr && mode == 0xFF);
	// past the end; and (ROM QUIRK) the error is still in the status
	// register when the next command waits for the drive to be ready, so
	// that fails too until the drive is reset
	EXPECT(ata->Read(block, 4 * 2048, 1, kATACmdReadSectors, 0) == kError_ATA_Sector_Id_Not_Found);
	EXPECT(ata->Read(block, 100, 1, kATACmdReadSectors, 0) == kError_ATA_Sector_Id_Not_Found);
	EXPECT(ata->Reset(true) == noErr);
	EXPECT(ata->Read(block, 100, 1, kATACmdReadSectors, 0) == noErr && memcmp(block, pattern, 0x200) == 0);
	// a command the drive does not know is aborted
	EXPECT(ata->SetPowerMode(0x55, 0, 0) == kError_ATA_Aborted_Command);
	EXPECT(ata->Reset(true) == noErr);

	// the partition map: the map, a free entry, Apple_Newton
	TATAPartitionInfo partitions;
	TATABootParamBlock boot;
	boot.fPartitionInfo = &partitions;
	EXPECT(loader.LoadATAPackages(socket, &card, &boot, ata, 1) == noErr);
	EXPECT(boot.fSocket == socket && boot.fATA == ata);
	EXPECT(partitions.fPCPartition == -1 && partitions.fMapBlock == 0);
	EXPECT(partitions.fDriverBlock == -1 && partitions.fDriverEntry == nil);
	EXPECT(partitions.fNewtonBlock == 3 && partitions.fNewtonEntry != nil);
	EXPECT(partitions.fNewtonEntry != nil && memcmp(partitions.fNewtonEntry + 0x30, "Apple_Newton", 13) == 0);
	EXPECT(loader.fNewtonPackageId == 0 && loader.fBootCode == nil);
	EXPECT(loader.LoadATAPackages(socket, &card, &boot, ata, 2) == kError_Bad_Parameters);
	EXPECT(loader.RemoveATAPackages(&boot, nil, 1) == noErr);
	EXPECT(partitions.fNewtonBlock == -1 && partitions.fNewtonEntry == nil && boot.fATA == nil);
	ata->Delete();
	HostCardRemove(0);

	// what was written is on the card when it comes back
	EXPECT(HostCardInsert(0, gPlainCard) == noErr);
	ata = TATA::New("TATASimple");
	EXPECT(ata != nil && ata->Initialize(socket, &card, 0) == noErr);
	memset(block, 0, sizeof(block));
	EXPECT(ata != nil && ata->Read(block, 100, 3, kATACmdReadSectors, 0) == noErr);
	EXPECT(memcmp(block, pattern, sizeof(block)) == 0);
	if (ata != nil)
		ata->Delete();
	HostCardRemove(0);
}


// the map inside a PC partition of type 0x83, and the loader making its
// own TATASimple
static void
TestMBR(TCardSocket* socket)
{
	EXPECT(HostCardInsert(0, gMBRCard) == noErr);
	TCardPCMCIA card;
	TPCMCIA20Parser parser;
	EXPECT(parser.ParsePCCardCIS(&card, socket) == noErr);
	TCardATALoader loader;
	TATAPartitionInfo partitions;
	TATABootParamBlock boot;
	boot.fPartitionInfo = &partitions;
	EXPECT(loader.LoadATAPackages(socket, &card, &boot, nil, 1) == noErr);
	EXPECT(partitions.fPCPartition == 0 && partitions.fMapBlock == 1);
	EXPECT(partitions.fNewtonBlock == 4);
	EXPECT(loader.RemoveATAPackages(&boot, nil, 1) == noErr);
	HostCardRemove(0);
}


static void
TestHelpers(void)
{
	TCardATALoader loader;
	EXPECT(loader.SameStrings((char*) "Apple_Newton", (char*) "APPLE_newton", 0x20));
	EXPECT(!loader.SameStrings((char*) "Apple_Newton", (char*) "Apple_Newton_Driver", 0x20));
	// ROM QUIRK: agreeing for all the length without ending is not the same
	EXPECT(!loader.SameStrings((char*) "ARM610", (char*) "ARM610", 6));
	EXPECT(loader.SameStrings((char*) "ARM610", (char*) "ARM610", 7));
	UByte ab[2] = { 'A', 'B' };
	EXPECT(loader.ChecksumOf(ab, 2) == 0x188);
	EXPECT(loader.ChecksumOf(ab, 0) == 0xFFFF);
}


static void
ATAScenario(void)
{
	TATASimple::ClassInfo()->Register();
	TCardSocket* socket = new TCardSocket(0);
	EXPECT(socket->Init() == noErr);
	gCardSockets[0] = socket;
	gNumberOfHWSockets = 1;
	EXPECT(InitVppManager() == noErr);		// the card's power, which each command switches on
	TestDriver(socket);
	TestMBR(socket);
	TestHelpers();
	HostStopTasks();
}


int
main(int argc, char** argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: test_ATACard plain.card mbr.card\n");
		return 2;
	}
	gPlainCard = argv[1];
	gMBRCard = argv[2];
	// TATASimple's reset and its waits count the free-running timer, which
	// a test's controllable clock does not move while a task spins
	HostUseRealClock(true);
	gHostKernelServicesTask = ATAScenario;
	OsBoot();
	if (failures == 0)
		printf("test_ATACard: all passed\n");
	else
		printf("test_ATACard: %d failures\n", failures);
	return failures != 0;
}
