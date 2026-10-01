// Protocol parts on the ARM interpreter (ARMProtocols.h, ARMWorld.h), over
// a small part assembled here: a class info, its dispatch table, a sizeof,
// a New that sets a field, a Delete, and one method that adds its two
// arguments and the field.  It is loaded (copied into the ARM world and
// relocated against a package with a relocation chunk), registered, made
// from host code through a proxy and called; made from the ARM side
// through the glue (NewByName, FreeInstance) and called ARM to ARM; the
// name server glue round-trips a thing; and a region maps host memory.
// Then the user-side OS objects ARM code makes (ARMKernelGlue.cpp,
// ARMLists.cpp), each over a small snippet of ARM code or called through
// the public jump table as ARM code calls it: a CList filled, searched,
// walked and emptied; a locking semaphore; the global time; an async
// message; and an event handler whose AEHandlerProc is ARM code, handed a
// host event (narrowed for it) and changing it (widened back).
// Run as the kernel services task (the protocol registry is a monitor).

#include "ARMProtocols.h"
#include "ARMWorld.h"
#include "PublicJumpTable.h"
#include "Protocols.h"
#include "OSErrors.h"
#include "AEventHandler.h"
#include "AEvents.h"
#include "UserPorts.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// the interface the part implements, and its proxy
PROTOCOL TTestThing : public TProtocol
{
public:
	void			Delete(void)	{ GlueDelete(); }
	VIRTUAL long	Add(long a, long b) ENDVIRTUAL;
};

class TTestThingARM : public TTestThing
{
public:
	long	Add(long a, long b)
			{
				uint32_t args[2] = { (uint32_t) a, (uint32_t) b };
				return (long) (int32_t) ARMCallSlot(this, 4, args, 2);
			}
};
static TProtocol*	MakeTestThingARM(void* at)	{ return new (at) TTestThingARM; }
static size_t		SizeTestThingARM(void)		{ return sizeof(TTestThingARM); }


// the part, as NTK would have built it
static uint8_t	gPart[0xc0];
static const uint32_t kPartOffset = 0x100;		// where it lies in the package

static void
Put(uint32_t at, uint32_t w)
{
	gPart[at] = (uint8_t) (w >> 24); gPart[at + 1] = (uint8_t) (w >> 16);
	gPart[at + 2] = (uint8_t) (w >> 8); gPart[at + 3] = (uint8_t) w;
}
static uint32_t
B(uint32_t at, uint32_t target)
{
	return 0xEA000000 | (((target - at - 8) >> 2) & 0xFFFFFF);
}

static void
AssemblePart(void)
{
	memset(gPart, 0, sizeof(gPart));
	Put(0x04, 0x40 - 0x04);				// the implementation's name
	Put(0x08, 0x50 - 0x08);				// the interface's
	Put(0x0c, 0x60 - 0x0c);				// the capabilities
	Put(0x10, 0x70 - 0x10);				// the dispatch table
	Put(0x18, B(0x18, 0x90));			// sizeof
	Put(0x24, B(0x24, 0x98));			// New
	Put(0x28, B(0x28, 0xa4));			// Delete
	Put(0x2c, 0x00010000);				// version
	strcpy((char*) gPart + 0x40, "TTestImpl");
	strcpy((char*) gPart + 0x50, "TTestThing");
	memcpy(gPart + 0x60, "cap\0val\0\0", 9);
	// the dispatch table: 0 unused, 1 ClassInfo (none here), 2 New, 3 Delete, 4 Add
	Put(0x78, B(0x78, 0x98));
	Put(0x7c, B(0x7c, 0xa4));
	Put(0x80, B(0x80, 0xa8));
	Put(0x90, 0xE3A00018);				// sizeof: mov r0, #0x18
	Put(0x94, 0xE1A0F00E);				//         mov pc, lr
	Put(0x98, 0xE3A0102A);				// New:    mov r1, #42
	Put(0x9c, 0xE5801010);				//         str r1, [r0, #0x10]
	Put(0xa0, 0xE1A0F00E);				//         mov pc, lr
	Put(0xa4, 0xE1A0F00E);				// Delete: mov pc, lr
	Put(0xa8, 0xE5903010);				// Add:    ldr r3, [r0, #0x10]
	Put(0xac, 0xE0810002);				//         add r0, r1, r2
	Put(0xb0, 0xE0800003);				//         add r0, r0, r3
	Put(0xb4, 0xE1A0F00E);				//         mov pc, lr
	Put(0xb8, kPartOffset + 0xbc);		// a word the relocation moves (an offset in the package)
}

// a package with a relocation chunk naming the word at part + 0xb8
static uint8_t	gPackage[0x200];

static void
MakePackage(void)
{
	memset(gPackage, 0, sizeof(gPackage));
	memcpy(gPackage, "package1", 8);
	gPackage[0x0c] = 0x04;				// kRelocationFlag
	gPackage[0x2f] = 0x40;				// the directory's size: the chunk follows
	uint8_t* chunk = gPackage + 0x40;
	chunk[7] = 28;						// the chunk's size
	chunk[10] = 0x04;					// pages of 0x400
	// linked at 0 (+16); one entry: page 0, one word
	chunk[20] = 0; chunk[21] = 0; chunk[22] = 0; chunk[23] = 1;
	chunk[24] = (uint8_t) ((kPartOffset + 0xb8) / 4);
}


static uint32_t
JumpTableEntry(const char* name)
{
	for (unsigned long i = 0; i < kPublicJumpTableCount; i++)
		if (strcmp(kPublicJumpTable[i].fName, name) == 0)
			return kPublicJumpTableBase + kPublicJumpTable[i].fOffset;
	return 0;
}


static void		KernelScenario(void);

static void
ProtocolScenario(void)
{
	InstallARMProtocols();
	RegisterARMProxyKind("TTestThing", MakeTestThingARM, SizeTestThingARM);
	AssemblePart();
	MakePackage();

	// loaded, relocated and registered
	const TClassInfo* info = LoadARMProtocolPart(gPart, sizeof(gPart), gPackage, kPartOffset);
	EXPECT(info != nil);
	if (info == nil)
		return;
	EXPECT(strcmp(info->ImplementationName(), "TTestImpl") == 0 && strcmp(info->InterfaceName(), "TTestThing") == 0);
	EXPECT(info->Version() == 0x10000);
	EXPECT(info->GetCapability("cap") != nil && strcmp(info->GetCapability("cap"), "val") == 0);
	EXPECT(info->Register() == noErr);

	// made from host code: a proxy over an ARM instance
	TTestThing* thing = (TTestThing*) NewByName("TTestThing", "TTestImpl");
	EXPECT(thing != nil);
	if (thing != nil)
	{
		uint32_t instance = ARMInstanceOf(thing);
		EXPECT(instance != 0);
		uint32_t realThis = 0, btable = 0, relocated = 0;
		EXPECT(ARMRead32(instance + 4, &realThis) && realThis == instance);
		EXPECT(ARMRead32(instance + 8, &btable) && btable != 0);
		// the relocated word points into the copy: the table's base - 0x70 + 0xbc
		EXPECT(ARMRead32(btable - 0x70 + 0xb8, &relocated) && relocated == btable - 0x70 + 0xbc);
		EXPECT(thing->Add(2, 3) == 47);
		EXPECT(thing->Add(-50, 1) == -7);
		thing->Delete();
	}

	// made from the ARM side: NewByName's glue, a call ARM to ARM, FreeInstance's
	uint32_t interface = ARMCString("TTestThing"), implementation = ARMCString("TTestImpl");
	uint32_t args[2] = { interface, implementation };
	uint32_t instance = ARMCall(JumpTableEntry("NewByName__FPCcT1"), args, 2);
	EXPECT(instance != 0);
	uint32_t addArgs[2] = { 10, 20 };
	EXPECT(ARMCallInstanceSlot(instance, 4, addArgs, 2) == 72);
	uint32_t freeArgs[1] = { instance };
	ARMCall(JumpTableEntry("FreeInstance__FP9TProtocol"), freeArgs, 1);
	// AllocInstanceByName: an instance New has not run on
	instance = ARMCall(JumpTableEntry("AllocInstanceByName__FPCcT1"), args, 2);
	uint32_t field = 1;
	EXPECT(instance != 0 && ARMRead32(instance + 0x10, &field));

	// the name server: a thing registered from the ARM side and looked up
	uint32_t ns = ARMCall(JumpTableEntry("__ct__12TUNameServerFv"), nil, 0);
	EXPECT(ns != 0);
	uint32_t name = ARMCString("Test thing"), type = ARMCString("TTestThing");
	uint32_t reg[5] = { ns, name, type, instance, 7 };
	EXPECT(ARMCall(JumpTableEntry("RegisterName__12TUNameServerFPcT1UlT3"), reg, 5) == noErr);
	uint32_t thingAt = ARMAlloc(8, true);
	uint32_t look[5] = { ns, name, type, thingAt, thingAt + 4 };
	EXPECT(ARMCall(JumpTableEntry("Lookup__12TUNameServerFPcT1PUlT3"), look, 5) == noErr);
	uint32_t found = 0, spec = 0;
	EXPECT(ARMRead32(thingAt, &found) && found == instance && ARMRead32(thingAt + 4, &spec) && spec == 7);
	uint32_t unreg[3] = { ns, name, type };
	EXPECT(ARMCall(JumpTableEntry("UnRegisterName__12TUNameServerFPcT1"), unreg, 3) == noErr);
	EXPECT(ARMCall(JumpTableEntry("Lookup__12TUNameServerFPcT1PUlT3"), look, 5) != noErr);

	// a region of host memory, big-endian to the ARM code
	uint8_t host[16] = { 1, 2, 3, 4 };
	uint32_t region = ARMMapRegion(host, sizeof(host), kARMRegionMemory);
	uint32_t w = 0;
	EXPECT(region != 0 && ARMRead32(region, &w) && w == 0x01020304);
	EXPECT(ARMWrite32(region + 4, 0xA0B0C0D0) && host[4] == 0xA0 && host[7] == 0xD0);
	ARMUnmapRegion(region);
	EXPECT(!ARMRead32(region, &w));

	// a mirror stands for its host object
	int hostThing = 5;
	uint32_t mirror = ARMMirrorFor(&hostThing, 16, 'test');
	EXPECT(mirror != 0 && ARMMirrorFor(&hostThing, 16, 'test') == mirror);
	EXPECT(ARMHostOf(mirror, 'test') == &hostThing && ARMHostOf(mirror, 'xxxx') == nil);
	ARMForgetMirror(&hostThing);
	EXPECT(ARMHostOf(mirror, 'test') == nil);

	EXPECT(info->DeRegister() == noErr);
	KernelScenario();
	HostStopTasks();
}


static uint32_t
Call(const char* name, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
{
	uint32_t args[4] = { a, b, c, d };
	uint32_t entry = JumpTableEntry(name);
	EXPECT(entry != 0);
	return ARMCall(entry, args, 4);
}

// an event a handler is given, with four bytes after the header (only the
// header is widened and narrowed: the rest goes as the bytes it is)
struct TTestEvent : public TAEvent
{
	uint8_t	fValue[4];
};

static void
KernelScenario(void)
{
	// a CList: three items in, found, walked, one taken out
	uint32_t list = Call("__ct__5CListFv", 0);
	EXPECT(list != 0);
	for (uint32_t i = 0; i < 3; i++)
		EXPECT(Call("InsertAt__5CListFlPv", list, i, 0x100 + i) == noErr);
	EXPECT(Call("At__5CListFl", list, 1) == 0x101);
	EXPECT(Call("GetIdentityIndex__5CListFPv", list, 0x102) == 2);
	EXPECT((int32_t) Call("GetIdentityIndex__5CListFPv", list, 0x999) == -1);
	uint32_t iter = Call("__ct__13CListIteratorFP13CDynamicArray", 0, list);
	uint32_t sum = 0;
	for (uint32_t item = Call("FirstItem__13CListIteratorFv", iter); item != 0; item = Call("NextItem__13CListIteratorFv", iter))
		sum += item;
	EXPECT(sum == 0x100 + 0x101 + 0x102);
	Call("__dt__14CArrayIteratorFv", iter, 1);
	EXPECT(Call("Remove__5CListFPv", list, 0x101) == noErr);
	EXPECT(Call("Remove__5CListFPv", list, 0x101) != noErr);
	EXPECT(Call("At__5CListFl", list, 1) == 0x102);
	uint32_t size = 0;
	EXPECT(ARMRead32(list, &size) && size == 2);
	Call("__dt__5CListFv", list, 1);

	// a locking semaphore (the ROM's fields: id, a byte, the semaphore)
	uint32_t sem = ARMAlloc(12, true);
	EXPECT(Call("Init__18TULockingSemaphoreFv", sem) == noErr);
	uint32_t semId = 0;
	EXPECT(ARMRead32(sem, &semId) && semId != 0);
	EXPECT(Call("Acquire__18TULockingSemaphoreF8SemFlags", sem, kWaitOnBlock) == noErr);
	EXPECT(Call("Release__18TULockingSemaphoreFv", sem) == noErr);
	Call("__dt__18TULockingSemaphoreFv", sem, 0);

	// the time, returned through a hidden pointer
	uint32_t t = ARMAlloc(8, true);
	EXPECT(Call("GetGlobalTime", t) == t);
	uint32_t lo = 0;
	EXPECT(ARMRead32(t + 4, &lo) && lo != 0);

	// an async message: its ids written where the ROM keeps them
	uint32_t msg = Call("__ct__14TUAsyncMessageFv", 0);
	EXPECT(Call("Init__14TUAsyncMessageFUc", msg, 1) == noErr);
	uint32_t msgId = 0, replyId = 0;
	EXPECT(ARMRead32(msg, &msgId) && msgId != 0 && ARMRead32(msg + 8, &replyId) && replyId != 0);
	Call("__dt__14TUAsyncMessageFv", msg, 1);

	// an event handler whose AEHandlerProc is ARM code: it reads the event's
	// word after the header, keeps it and adds one to it (not SetReply: the
	// kernel services task has no app world to reply through)
	uint8_t code[0x60];
	memset(code, 0, sizeof(code));
	uint32_t region = ARMMapRegion(code, sizeof(code), kARMRegionMemory);
	uint32_t keep = ARMAlloc(4, true);
	// +0x00: the vtable - dtor, AETestEvent, AEHandlerProc (b +0x20)
	uint32_t words[] = {
		0xE1A0F00E, 0xE3A00001, 0xEA000004, 0xE1A0F00E,	// mov pc,lr; mov r0,#1 (unreached); b +0x20; mov pc,lr
		0, 0, 0, 0,
		0xE5934008,		// +0x20 AEHandlerProc(this, token, size*, event): ldr r4,[r3,#8]
		0xE59F500C,		// ldr r5,=keep
		0xE5854000,		// str r4,[r5]
		0xE2844001,		// add r4,r4,#1
		0xE5834008,		// str r4,[r3,#8]
		0xE1A0F00E,		// mov pc,lr
		keep,
	};
	for (unsigned i = 0; i < sizeof(words) / 4; i++)
		ARMWrite32(region + i * 4, words[i]);
	uint32_t handler = Call("__ct__14TAEventHandlerFv", 0);
	EXPECT(handler != 0);
	ARMWrite32(handler, region);				// (the subclass's vtable)
	// (not Init: the kernel services task has no app world to install it in)
	TAEventHandler* host = ARMEventHandlerOf(handler);
	EXPECT(host != nil);
	if (host != nil)
	{
		TTestEvent event;
		event.fAEventClass = 'evnt';
		event.fAEventID = 'test';
		event.fValue[0] = 0; event.fValue[1] = 0; event.fValue[2] = 0; event.fValue[3] = 41;
		ULong eventSize = sizeof(TTestEvent);
		host->AEHandlerProc(nil, &eventSize, &event);
		uint32_t kept = 0;
		EXPECT(ARMRead32(keep, &kept) && kept == 41);
		// the event as the handler left it, widened back: the class, the id and the word
		EXPECT(event.fValue[3] == 42 && event.fValue[0] == 0 && event.fAEventClass == 'evnt' && event.fAEventID == 'test');
		EXPECT(eventSize == sizeof(TTestEvent));
	}
	Call("__dt__14TAEventHandlerFv", handler, 1);
	EXPECT(ARMEventHandlerOf(handler) == nil);
	ARMUnmapRegion(region);
}


int
main()
{
	gHostKernelServicesTask = ProtocolScenario;
	OsBoot();
	if (failures == 0)
		printf("test_ARMProtocols: all passed\n");
	else
		printf("test_ARMProtocols: %d failures\n", failures);
	return failures != 0;
}
