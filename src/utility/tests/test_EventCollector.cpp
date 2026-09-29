// THistoryCollector (utility/EventCollector.h): an event trace ring.
//
// Covered: Init's sizes (a word entry, a one-byte entry sharing the time's
// word), the collection flags each entry size allows, registration in
// gEventTraceBufArray and its removal, longs recorded after the time's
// word (low bit set) and the ring wrapping at the limit, a one-byte
// collector's byte in the time's word, an entry of several bytes,
// AddDescriptions (five at most; answering false, the ROM's bug), and
// CollectionControl(0) stopping collection.

#include "EventCollector.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int
Registered(TEventCollector* collector)
{
	int n = 0;
	for (int i = 0; i < 32; i++)
		if (gEventTraceBufArray[i] == &collector->fData)
			n++;
	return n;
}

static unsigned int
Word(TEventCollector* collector, int index)
{
	return ((unsigned int*) collector->fData.fEventBuffer)[index];
}


static void
Scenario(void)
{
	// the class registered (as the loader's InitEvents does - again, so as
	// not to depend on the loader having run yet), and made by name
	InitEvents();
	// longs: (4 + 4) * 3 bytes, every flag but the byte-in-time one
	TEventCollector* longs = TEventCollector::New(kTHistoryCollector);
	EXPECT(longs != nil);
	longs->Init(4, (char*) "\t%bd %bx", (char*) "Test Trace", 3, eNormalBuffer);
	EXPECT(longs->fData.fEntrySize == 4 && longs->fData.fBufferSize == 24 && longs->fData.fNumberOfEntries == 3);
	EXPECT(longs->fData.fDoCollect == 0x3e);
	EXPECT(strcmp(longs->fData.fName, "Test Trace") == 0 && strcmp(longs->fData.fDataFormat, "\t%bd %bx") == 0);
	EXPECT(Registered(longs) == 1);
	for (unsigned long v = 1; v <= 4; v++)
		longs->Add((unsigned long) (v << 24 | 0x410000));
	// the fourth went back to the start
	EXPECT((Word(longs, 0) & 1) == 1 && Word(longs, 1) == (4u << 24 | 0x410000));
	EXPECT(Word(longs, 3) == (2u << 24 | 0x410000) && Word(longs, 5) == (3u << 24 | 0x410000));
	EXPECT(longs->fData.fCurrentPos == longs->fData.fEventBuffer + 8);

	// descriptions: five, and false every time
	EventTraceCauseDesc descs[2] = { { 0, (char*) "start" }, { 1, (char*) "byte" } };
	for (int i = 0; i < 6; i++)
		EXPECT(longs->AddDescriptions(descs, 2) == false);
	EXPECT(longs->fData.fActualDescCount == 5 && longs->fData.fDescInfo[4].desc == descs);

	// stopped: nothing more is recorded
	longs->CollectionControl(eDontCollect);
	EXPECT(longs->fData.fDoCollect == 0);
	char* pos = longs->fData.fCurrentPos;
	longs->Add((unsigned long) 99);
	EXPECT(longs->fData.fCurrentPos == pos);
	longs->Delete();
	EXPECT(Registered(longs) == 0);

	// one-byte entries: the byte in the time's word, no longs
	TEventCollector* bytes = TEventCollector::New(kTHistoryCollector);
	bytes->Init(1, (char*) "%bx", (char*) "Bytes", 2, eNormalBuffer);
	EXPECT(bytes->fData.fEntrySize == 0 && bytes->fData.fBufferSize == 8 && bytes->fData.fDoCollect == 6);
	bytes->Add((unsigned char) 0xab);
	EXPECT((Word(bytes, 0) & 0xff) == 0xab);
	pos = bytes->fData.fCurrentPos;
	bytes->Add((unsigned long) 5);								// not allowed: ignored
	EXPECT(bytes->fData.fCurrentPos == pos);
	bytes->Delete();

	// entries of six bytes (rounded to eight)
	TEventCollector* entries = TEventCollector::New(kTHistoryCollector);
	entries->Init(6, (char*) "%x", (char*) "Entries", 4, eNormalBuffer);
	EXPECT(entries->fData.fEntrySize == 8 && entries->fData.fBufferSize == 48 && entries->fData.fDoCollect == 0x2e);
	const char six[6] = { 1, 2, 3, 4, 5, 6 };
	entries->Add((const void*) six);
	EXPECT(memcmp(entries->fData.fEventBuffer + 4, six, 6) == 0 && entries->fData.fCurrentPos == entries->fData.fEventBuffer + 12);
	entries->Delete();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;
	OsBoot();
	if (failures == 0)
		printf("test_EventCollector: all passed\n");
	else
		printf("test_EventCollector: %d failures\n", failures);
	return failures != 0;
}
