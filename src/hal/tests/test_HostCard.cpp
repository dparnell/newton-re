// The host's PC card and socket (hal/host/HostCard.h, HostCardSocket.cpp):
// a blank card made in Einstein's container, its CIS read back through the
// socket's attribute window the way the ROM reads it ((address ^ 3) at the
// even addresses), the pins, the flash rules (writes AND, erases set to
// 0xFF, written through to the file), and the card-detect interrupt
// delivered to the registered proc once the server says it may be.  No OS
// boot.

#include "CardSocket.h"
#include "HostCard.h"
#include "HostInterruptSources.h"
#include "NewtErrors.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char*	kCardFile = "test_HostCard.card";

static int		gDetected = 0;
static int		gLocked = 0;

static long
Detected(void* object, TCardSocket* socket)
{
	gDetected += (object == (void*) 1 && socket != nil);
	return 0;
}

static long
Locked(void*, TCardSocket*)
{
	gLocked++;
	return 0;
}

static void
DeliverAll(void)
{
	Int64 now;
	now.hi = 0x7fffffff;
	now.lo = 0;
	HostDeliverInterruptSources(&now);
}

static ULong
FileWord(ULong offset)
{
	FILE* f = fopen(kCardFile, "rb");
	unsigned char b[4] = { 0, 0, 0, 0 };
	if (f != nil)
	{
		fseek(f, (long) offset, SEEK_SET);
		fread(b, 1, 4, f);
		fclose(f);
	}
	return ((ULong) b[0] << 24) | ((ULong) b[1] << 16) | ((ULong) b[2] << 8) | b[3];
}


int
main()
{
	EXPECT(HostCardCreate(kCardFile, 3, "odd") == kError_Bad_Parameters);
	EXPECT(HostCardCreate(kCardFile, 2, "Host card") == noErr);

	// the footer: Einstein's ImageInfo, big-endian, "TLinearCard"
	FILE* f = fopen(kCardFile, "rb");
	EXPECT(f != nil);
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	unsigned char info[kHostCardImageInfoSize];
	fseek(f, size - (long) sizeof(info), SEEK_SET);
	fread(info, 1, sizeof(info), f);
	fclose(f);
	EXPECT(memcmp(info + 40, "TLinearCard", 12) == 0);
	EXPECT(FileWord(size - 52 + 24) == 0x200000);		// the data's size
	EXPECT(FileWord(size - 52 + 28) == 0);				// and start
	EXPECT(FileWord(size - 52 + 32) == 5);				// a flash card

	TCardSocket socket(0);
	EXPECT(socket.Init() == noErr);
	TCardSocket missing(1);
	EXPECT(missing.Init() != noErr);					// one socket
	EXPECT(!socket.IsCardDetected());
	EXPECT(socket.RegisterSocketInterrupt(kSocketCardDetectedInt, Detected, (void*) 1) == noErr);
	EXPECT(socket.RegisterSocketInterrupt(kSocketCardLockInt, Locked, nil) == noErr);
	EXPECT(socket.RegisterSocketInterrupt(kSocketIntCount, Locked, nil) == kError_Bad_Parameters);

	EXPECT(HostCardInsert(0, "no such card") == kError_Bad_Parameters);
	EXPECT(HostCardInsert(0, kCardFile) == noErr);
	EXPECT(socket.IsCardDetected() && socket.IsReady() && !socket.IsWriteProtected());
	EXPECT(strcmp(HostCardName(0), "Host card") == 0);

	// the interrupts wait for the server's go-ahead and for being enabled
	DeliverAll();
	EXPECT(gDetected == 0);
	socket.EnableSocketInterrupt(kSocketOkToEnableInt);
	DeliverAll();
	EXPECT(gDetected == 0);
	socket.EnableSocketInterrupt(kSocketCardDetectedInt);
	socket.EnableSocketInterrupt(kSocketCardLockInt);
	DeliverAll();
	EXPECT(gDetected == 1 && gLocked == 1);
	DeliverAll();
	EXPECT(gDetected == 1);								// delivered once

	// the CIS as the ROM reads it: CISTPL_DEVICE, flash, 2 MB
	UByte* attr = (UByte*) socket.AttributeMemBaseAddr();
	EXPECT(attr[(0 * 2) ^ 3] == 0x01);
	EXPECT(attr[(1 * 2) ^ 3] == 3);
	EXPECT(attr[(2 * 2) ^ 3] == 0x53);
	EXPECT(attr[(3 * 2) ^ 3] == 0x06);
	EXPECT(attr[(5 * 2) ^ 3] == 0x18);					// CISTPL_JEDEC_C
	EXPECT(attr[(7 * 2) ^ 3] == 0x89 && attr[(8 * 2) ^ 3] == 0xA0);
	// and as Einstein answers: byte o is CIS byte (o / 2) ^ 1
	EXPECT(attr[1] == 3 && attr[3] == 0x01);

	// the common memory, erased; the flash's rules
	UByte* common = (UByte*) socket.CommonMemBaseAddr();
	EXPECT(HostCardCommonSize(0) == 0x200000 && common[0] == 0xFF && common[0x1FFFFF] == 0xFF);
	UByte bytes[4] = { 0x12, 0x34, 0xF0, 0x0F };
	EXPECT(HostCardFlashWrite(0, 0x10000, bytes, 4) == noErr);
	UByte more[4] = { 0xFF, 0x0F, 0xFF, 0xFF };
	EXPECT(HostCardFlashWrite(0, 0x10000, more, 4) == noErr);
	EXPECT(common[0x10000] == 0x12 && common[0x10001] == 0x04 && common[0x10002] == 0xF0);
	EXPECT(HostCardFlashWrite(0, 0x1FFFFE, bytes, 4) == kError_Bad_Parameters);
	HostCardFlush(0);
	EXPECT(FileWord(0x10000) == 0x1204F00F);

	// out, in again: it is still there
	gDetected = 0;
	HostCardRemove(0);
	EXPECT(!socket.IsCardDetected() && socket.CommonMemBaseAddr() == 0);
	DeliverAll();
	EXPECT(gDetected == 1);
	EXPECT(HostCardInsert(0, kCardFile) == noErr);
	common = (UByte*) socket.CommonMemBaseAddr();
	EXPECT(common[0x10000] == 0x12 && common[0x10003] == 0x0F);
	EXPECT(HostCardFlashErase(0, 0x10000, 0x10000) == noErr);
	EXPECT(common[0x10000] == 0xFF && common[0x10003] == 0xFF);
	HostCardRemove(0);
	EXPECT(FileWord(0x10000) == 0xFFFFFFFF);

	// write-protected
	EXPECT(HostCardInsert(0, kCardFile, true) == noErr);
	EXPECT(socket.IsWriteProtected() && HostCardIsWriteProtected(0));
	EXPECT(HostCardFlashWrite(0, 0, bytes, 4) == kError_Bad_Parameters);
	HostCardRemove(0);
	remove(kCardFile);

	if (failures == 0)
		printf("test_HostCard: all passed\n");
	else
		printf("test_HostCard: %d failures\n", failures);
	return failures != 0;
}
