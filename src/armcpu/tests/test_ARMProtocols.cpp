// Protocol parts on the ARM interpreter (ARMProtocols.h, ARMWorld.h), over
// a small part assembled here: a class info, its dispatch table, a sizeof,
// a New that sets a field, a Delete, and one method that adds its two
// arguments and the field.  It is loaded (copied into the ARM world and
// relocated against a package with a relocation chunk), registered, made
// from host code through a proxy and called; made from the ARM side
// through the glue (NewByName, FreeInstance) and called ARM to ARM; the
// name server glue round-trips a thing; and a region maps host memory.
// Run as the kernel services task (the protocol registry is a monitor).

#include "ARMProtocols.h"
#include "ARMWorld.h"
#include "PublicJumpTable.h"
#include "Protocols.h"
#include "OSErrors.h"
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
	HostStopTasks();
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
