// Package manager test (src/packages/PackageManager.h): the OS booted
// (OsBoot) starts the package manager from the kernel services task as
// the ROM does, and the pieces are checked on their own first - the
// events as they are made, a package block and an installed part, and
// CPackagePipe giving a package back through a pipe after its directory
// has been read.  Then an application world of the test's own (as the
// newt world is) registers frames part handlers for 'form and 'auto -
// the test's, which note the frames they are given - and the package
// store's 'soup handler (InitQueries), and loads packages through the
// manager, from bytes:
//
//   - the registry: a part type registered, refused a second time, and
//     taken out again;
//   - Cardfile and Setup where they lie in the ROM image (their refs are
//     ROM addresses), their frames parts handed to the handlers; Setup a
//     second time refused as already there, with the same id;
//   - WorldData copied into memory of its own, its soup part mounted as a
//     package store;
//   - the help book refused, nobody having registered 'book;
//   - ScreenBuffer, a protocol part, which goes in with no handler;
//   - the package list walked (TPMIterator, as GetPackages does);
//   - Cardfile removed (its handler told) and then loaded again as NTK
//     writes a package: a copy whose refs are offsets from the package's
//     start, which is the same frame to the handler.
//
// The ROM image is build/MP2x00US/rom.bin.

#include "PackageManager.h"
#include "PackageEvents.h"
#include "PackageIterator.h"
#include "PackagePipe.h"
#include "PartHandlers.h"
#include "FramePartHandler.h"
#include "PackageStore.h"
#include "Soups.h"
#include "Compression.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "ByteOrder.h"
#include "Boot.h"
#include "UserBoot.h"
#include "UserTasks.h"
#include "UserPorts.h"
#include "KernelGlobals.h"
#include "host/TaskRuntime.h"
#include "../../utility/tests/TestPipe.h"
#include "OSErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// the packages of the ROM extension (packages.py --parts)
const ULong kCardfile = 0x7201f4;
const ULong kHelpBook = 0x773f60;
const ULong kScreenBuffer = 0x7a5554;
const ULong kSetup = 0x7a67fc;
const ULong kWorldData = 0x7b4d20;

static const UByte*	gROM = nil;
static ULong		gROMSize = 0;
static volatile Boolean	gDone = false;


// a UniChar string against ASCII
static Boolean
Same(const UniChar* s, const char* text)
{
	for (; *text != 0; s++, text++)
		if (*s != (UniChar) *text)
			return false;
	return *s == 0;
}


/*------------------------------------------------------------------------------
	T h e   p i e c e s
------------------------------------------------------------------------------*/

static void
TestEvents(void)
{
	TPkRegisterEvent registerEvent('form', 1234);
	EXPECT(registerEvent.fAEventClass == kNewtEventClass && registerEvent.fAEventID == kPackageEventId);
	EXPECT(registerEvent.fEventCode == kPkRegisterEvent && registerEvent.fEventError == noErr);
	EXPECT(registerEvent.fPartType == 'form' && registerEvent.fPortId == 1234);
	TPkUnregisterEvent unregisterEvent('auto');
	EXPECT(unregisterEvent.fEventCode == kPkUnregisterEvent && unregisterEvent.fPartType == 'auto');

	PartSource source;
	source.stream.bufferId = 0x1000;
	source.stream.messagePortId = 7;
	SourceType type = { kFixedMemory, kNoDevice, 0, 0 };
	TPkBeginLoadEvent load(type, source, 11, 12, true);
	EXPECT(load.fEventCode == kPkBeginLoadEvent && load.fSenderPortId == 11 && load.fForwardPortId == 12);
	EXPECT(load.fSourceType.format == kFixedMemory && load.fSource.stream.bufferId == 0x1000 && load.fPackageId == 0);

	TPkRemoveEvent remove(99, 11, 12);
	EXPECT(remove.fEventCode == kPkRemoveEvent && remove.fPackageId == 99 && remove.fSenderPortId == 11 && remove.fForwardPortId == 12);
	PartId partId = { 99, 3 };
	TPkPartRemoveEvent partRemove(partId, kFrames, 'form', 55);
	EXPECT(partRemove.fEventCode == kPkPartRemoveEvent && partRemove.fPartId.packageId == 99 && partRemove.fPartId.partIndex == 3);
	EXPECT(partRemove.fPartKind == kFrames && partRemove.fPartType == 'form' && partRemove.fRemoveObjPtr == 55);
	TPkSafeToDeactivate safe(99);
	EXPECT(safe.fEventCode == kPkSafeToDeactivateEvent && safe.fPackageId == 99 && !safe.fSafe);
	TPkBackupEvent backup(0, 0xffffffff, true, source, 11, 12);
	EXPECT(backup.fEventCode == kPkBackupEvent && backup.fIndex == 0 && backup.fLastBackupDate == 0xffffffff);

	// a part's info and its compressor's name travel in the event
	ExtendedPartInfo info;
	memset(&info, 0, sizeof(info));
	info.kind = kFrames;
	info.type = 'form';
	info.infoSize = 5;
	info.info = (void*) "hello";
	info.compressor = (char*) "streamed";
	info.compressed = true;
	Ustrcpy(info.packageName, (const UniChar*) u"Pkg");
	TPkPartInstallEvent install(partId, info, type, source);
	EXPECT(install.fEventCode == kPkPartInstallEvent && install.fPartInfo.type == 'form');
	EXPECT(memcmp(install.fInfo, "hello", 5) == 0 && strcmp(install.fCompressor, "streamed") == 0);
	EXPECT(Same(install.fPartInfo.packageName, "Pkg"));
}


static void
TestListEntries(void)
{
	// a package block copies the name and the copyright
	TPackageBlock block;
	SourceType type = { kFixedMemory, kNoDevice, 0, 0 };
	UniChar name[] = { 'N', 'a', 'm', 'e', 0 };
	EXPECT(block.Init(42, 3, 1000, type, kCopyProtectFlag, name, nil, 2, 7) == noErr);
	EXPECT(block.fPackageId == 42 && block.fVersion == 3 && block.fSize == 1000 && block.fFlags == kCopyProtectFlag && block.fModifyDate == 7);
	EXPECT(block.fName != name && Same(block.fName, "Name") && block.fCopyright == nil);
	EXPECT(block.fParts != nil && block.fParts->GetArraySize() == 0 && block.fState == 0);
	delete block.fParts;
	delete[] block.fName;

	TInstalledPart part('form', kFrames, 77, true, false, true, true, 0);
	EXPECT(part.fType == 'form' && part.fKind == kFrames && part.fRemoveObjPtr == 77);
	EXPECT(part.fAutoLoad && part.fNotify && part.fAccepted && !part.fOwnsCode && part.fClassInfo == 0);

	TRegistryInfo registered('auto', 5);
	EXPECT(registered.fPartType == 'auto' && registered.fPortId == 5);
}


// A package read through a pipe: the directory's copy first, then the
// pipe's bytes.
static void
TestPackagePipe(void)
{
	const UByte* package = gROM + kSetup;
	ULong size = GetBigEndianWord(package + 0x1c);
	ULong directorySize = GetBigEndianWord(package + 0x2c);
	CTestPipe source(size);
	long count = size;
	source.WriteChunk(package, count, false);
	source.Rewind();
	CPackagePipe pipe;
	pipe.Init(&source);
	EXPECT(pipe.fSize == (long) directorySize);
	UByte* bytes = (UByte*) malloc(size);
	// ROM BUG: a read served from the copy alone says it read nothing
	count = directorySize;
	Boolean eof = false;
	pipe.ReadChunk(bytes, count, eof);
	EXPECT(count == 0);
	EXPECT(memcmp(bytes, package, directorySize) == 0);
	count = size - directorySize;
	pipe.ReadChunk(bytes + directorySize, count, eof);
	EXPECT(count == (long) (size - directorySize));
	EXPECT(memcmp(bytes, package, size) == 0);
	free(bytes);
	// it cannot be written
	Boolean threw = false;
	newton_try
	{
		pipe.WriteChunk("x", 1, false);
	}
	newton_catch_all
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);
}


/*------------------------------------------------------------------------------
	T h e   w o r l d
------------------------------------------------------------------------------*/

// what the test's frames part handlers were given
struct Given
{
	PartType		fType;
	PartId			fPartId;
	RefStruct*		fFrame;
	Boolean			fRemoved;
};
const long kMaxGiven = 16;
static Given	gGiven[kMaxGiven];
static long		gGivenCount = 0;

class TTestFramePartHandler : public TFramePartHandler
{
public:
	virtual	NewtonErr	InstallFrame(RefArg frame, const PartId& partId, SourceType /*sourceType*/, PartInfo* partInfo)
	{
		SetFrameRemoveObject(frame);
		if (gGivenCount < kMaxGiven)
		{
			Given* given = &gGiven[gGivenCount++];
			given->fType = partInfo->type;
			given->fPartId = partId;
			given->fFrame = new RefStruct(frame);
			given->fRemoved = false;
		}
		return noErr;
	}
	virtual	NewtonErr	RemoveFrame(RefArg /*removeObject*/, const PartId& partId, PartType /*partType*/)
	{
		for (long i = 0; i < gGivenCount; i++)
			if (gGiven[i].fPartId.packageId == partId.packageId && gGiven[i].fPartId.partIndex == partId.partIndex)
				gGiven[i].fRemoved = true;
		return noErr;
	}
};


static Given*
GivenFor(ULong packageId)
{
	for (long i = gGivenCount - 1; i >= 0; i--)
		if (gGiven[i].fPartId.packageId == packageId)
			return &gGiven[i];
	return nil;
}


// Whether the manager knows the package: its answer to a 'pksc event
// (SafeToDeactivatePackage answers only whether the event was sent).
static NewtonErr
Known(ULong packageId)
{
	TPkSafeToDeactivate event(packageId);
	TUPort manager(PackageManagerPortId());
	ULong size;
	manager.SendRPC(&size, &event, sizeof(event), &event, sizeof(event));
	return event.fEventError;
}


// a package in memory loaded through the manager
static NewtonErr
Load(const void* package, ULong* packageId)
{
	SourceType type = { kFixedMemory, kNoDevice, 0, 0 };
	*packageId = 0;
	return LoadPackage((Ptr) package, type, packageId);
}


// A copy of a ROM package as NTK would have written it: every pointer ref
// in its frames part an offset from the package's start rather than a ROM
// address.  (A version 1 package's objects are packed to four bytes.)
static UByte*
AsNTKWritesIt(ULong romAddress)
{
	const UByte* package = gROM + romAddress;
	ULong size = GetBigEndianWord(package + 0x1c);
	UByte* copy = (UByte*) malloc(size);
	memcpy(copy, package, size);
	TPackageIterator iter(copy);
	if (iter.Init() != noErr)
		return copy;
	ULong partOffset = iter.GetPartDataOffset(0);
	PartInfo info;
	iter.GetPartInfo(0, &info);
	UByte* part = copy + partOffset;
	ULong32 from = romAddress + partOffset;
	for (ULong a = 0; a < info.size; )
	{
		ULong32 header = GetBigEndianWord(part + a);
		ULong32 objSize = header >> 8;
		ULong end = (header & kObjSlotted) != 0 ? objSize : 12;
		for (ULong o = 8; o < end; o += 4)
		{
			ULong32 ref = GetBigEndianWord(part + a + o);
			if ((ref & 3) == 1 && ref >= from && ref < from + info.size)
				PutBigEndianWord(part + a + o, ref - romAddress);
		}
		a += (objSize + 3) & ~3u;
	}
	return copy;
}


static void
TestManager(void)
{
	TAppWorld* world = (TAppWorld*) GetGlobals();
	TUPort manager(PackageManagerPortId());
	EXPECT(PackageManagerPortId() != 0);

	// the registry
	{
		TPkRegisterEvent event('tst1', *world->GetMyPort());
		ULong size;
		EXPECT(manager.SendRPC(&size, &event, sizeof(event), &event, sizeof(event)) == noErr && event.fEventError == noErr);
		TPkRegisterEvent again('tst1', *world->GetMyPort());
		EXPECT(manager.SendRPC(&size, &again, sizeof(again), &again, sizeof(again)) == noErr && again.fEventError == kError_PartType_Already_Registered);
		TPkUnregisterEvent out('tst1');
		EXPECT(manager.SendRPC(&size, &out, sizeof(out), &out, sizeof(out)) == noErr && out.fEventError == noErr);
		TPkRegisterEvent afterwards('tst1', *world->GetMyPort());
		EXPECT(manager.SendRPC(&size, &afterwards, sizeof(afterwards), &afterwards, sizeof(afterwards)) == noErr && afterwards.fEventError == noErr);
	}
	// nothing installed yet
	UChar safe = false;
	EXPECT(SafeToDeactivatePackage(12345, &safe) == noErr && safe);
	EXPECT(Known(12345) == kError_No_Such_Package);

	// Cardfile and Setup, where they lie in the ROM
	ULong cardfile = 0, setup = 0, id = 0;
	EXPECT(Load(gROM + kCardfile, &cardfile) == noErr && cardfile != 0);
	Given* given = GivenFor(cardfile);
	EXPECT(given != nil && given->fType == 'auto' && given->fPartId.partIndex == 0 && !given->fRemoved);
	if (given != nil)
	{
		RefVar frame(*given->fFrame);
		EXPECT(IsFrame(frame) && NOTNIL(GetFrameSlotRef(frame, RSSYMinstallscript)) && NOTNIL(GetFrameSlotRef(frame, RSSYMremovescript)));
	}
	EXPECT(Load(gROM + kSetup, &setup) == noErr && setup != 0 && setup != cardfile);
	given = GivenFor(setup);
	EXPECT(given != nil && given->fType == 'form');
	if (given != nil)
		EXPECT(IsFrame(RefVar(*given->fFrame)));
	long before = gGivenCount;
	EXPECT(Load(gROM + kSetup, &id) == kError_Package_Already_Exists && id == setup && gGivenCount == before);

	// WorldData from memory of its own: a package store
	ULong worldDataSize = GetBigEndianWord(gROM + kWorldData + 0x1c);
	void* worldData = malloc(worldDataSize);
	memcpy(worldData, gROM + kWorldData, worldDataSize);
	long stores = Length(gPackageStores);
	ULong worldDataId = 0;
	EXPECT(Load(worldData, &worldDataId) == noErr && worldDataId != 0);
	EXPECT(Length(gPackageStores) == stores + 1);

	// nobody takes a 'book part; a protocol part needs nobody
	ULong helpBook = 0, screenBuffer = 0;
	EXPECT(Load(gROM + kHelpBook, &helpBook) == kError_PartType_Not_Registered);
	EXPECT(Load(gROM + kScreenBuffer, &screenBuffer) == noErr && screenBuffer != 0);
	EXPECT(Known(screenBuffer) == noErr && Known(helpBook) == kError_No_Such_Package);

	// the list, as GetPackages walks it
	TPMIterator iter;
	iter.Init();
	long count = 0;
	Boolean sawCardfile = false, sawSetup = false, sawWorldData = false, sawHelp = false, sawScreen = false;
	for (; iter.More(); iter.NextPackage())
	{
		count++;
		if (iter.PackageId() == cardfile)
			sawCardfile = Same(iter.PackageName(), "Cardfile") && iter.PackageSize() == 144604 && iter.fVersion == 1;
		if (iter.PackageId() == setup)
			sawSetup = Same(iter.PackageName(), "Setup") && iter.PackageSize() == 58660;
		if (iter.PackageId() == worldDataId)
			sawWorldData = Same(iter.PackageName(), "WorldData");
		if (iter.PackageId() == screenBuffer)
			sawScreen = Same(iter.PackageName(), "ScreenBuffer");
		if (Same(iter.PackageName(), "help book"))
			sawHelp = true;
		EXPECT(!iter.IsCopyProtected());
	}
	iter.Done();
	EXPECT(count == 4 && sawCardfile && sawSetup && sawWorldData && sawScreen && !sawHelp);

	// Cardfile removed, its handler told; then loaded as NTK writes it
	given = GivenFor(cardfile);
	long romSlots = 0, romScript = 0;
	if (given != nil)
	{
		RefVar romFrame(*given->fFrame);
		romSlots = Length(romFrame);
		romScript = Length(RefVar(GetFrameSlotRef(romFrame, RSSYMinstallscript)));
	}
	EXPECT(DeinstallPackage(cardfile) == noErr);
	EXPECT(given != nil && given->fRemoved);
	EXPECT(Known(cardfile) == kError_No_Such_Package);
	UByte* ntk = AsNTKWritesIt(kCardfile);
	ULong again = 0;
	EXPECT(Load(ntk, &again) == noErr && again != 0);
	Given* reloaded = GivenFor(again);
	EXPECT(reloaded != nil && reloaded != given && reloaded->fType == 'auto');
	if (reloaded != nil && given != nil)
	{
		// (the ROM part's own objects went with it: refs to them are declawed)
		RefVar ntkFrame(*reloaded->fFrame);
		EXPECT(IsFrame(ntkFrame) && Length(ntkFrame) == romSlots);
		RefVar script(GetFrameSlotRef(ntkFrame, RSSYMinstallscript));
		EXPECT(NOTNIL(script) && Length(script) == romScript);
	}
	// and taken out again
	EXPECT(DeinstallPackage(again) == noErr && reloaded != nil && reloaded->fRemoved);
}


// the test's own application world: part handlers for 'form and 'auto
// (the test's) and 'soup (the package store's), then the tests run from
// PreMain as the newt world loads the ROM's packages; loading forks the
// world, and the fork takes the part events meanwhile
class TTestWorld : public TAppWorld
{
public:
	virtual TForkWorld*	MakeFork()			{ return new TTestWorld; }
	virtual long		MainConstructor()
	{
		long err = TAppWorld::MainConstructor();
		if (err != noErr)
			return err;
		if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
			return kError_Bad_Parameters;
		gObjectHeapSize = 0x400000;
		InitObjects();
		InitializeCompression();		// (as the newt world does: a store part's objects are compressed)
		InitQueries();
		(new TTestFramePartHandler)->Init('form');
		(new TTestFramePartHandler)->Init('auto');
		return noErr;
	}
	virtual long		PreMain()
	{
		gROM = (const UByte*) ROMImageBase(&gROMSize);
		EXPECT(gROM != nil);
		if (gROM != nil)
		{
			TestPackagePipe();
			TestManager();
		}
		gDone = true;
		return noErr;
	}
};


static void
Scenario(void)
{
	TestEvents();
	TestListEntries();
	TTestWorld world;
	EXPECT(world.Init('ptst', true, 0x4000) == noErr);
	for (long tries = 0; tries < 600 && !gDone; tries++)
		Sleep(50 * kMilliseconds);
	EXPECT(gDone);
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;
	OsBoot();
	if (failures == 0)
		printf("test_PackageManager: all passed\n");
	else
		printf("test_PackageManager: %d failures\n", failures);
	return failures != 0;
}
