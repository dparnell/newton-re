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
#include "StdioPipe.h"
#include "PackageEvents.h"
#include "PackageIterator.h"
#include "PackagePipe.h"
#include "PackageLoader.h"
#include "ObjectStreamer.h"
#include "PartHandlers.h"
#include "FramePartHandler.h"
#include "PackageStore.h"
#include "StorePackages.h"
#include "PackageArchivalPipe.h"
#include "host/HostStore.h"
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


/*------------------------------------------------------------------------------
	S t r e a m e d
------------------------------------------------------------------------------*/

// A package of one 'form part, the part being a frame flattened (NSOF) -
// the form a frames part takes when a package is streamed.  The package's
// words are big-endian; its name is the only thing in the directory data.
static UByte*
StreamedPackage(const char* name, RefArg frame, ULong* packageSize)
{
	CTestPipe nsof(64);
	{
		TObjectWriter writer(frame, nsof, false);
		writer.Write();
	}
	ULong partSize = nsof.fWriteBuffer->Position();
	const ULong kHeader = 0x34, kEntry = 0x20;
	ULong nameBytes = (strlen(name) + 1) * 2;
	ULong directorySize = (kHeader + kEntry + nameBytes + 3) & ~3u;
	ULong size = directorySize + partSize;
	UByte* package = (UByte*) calloc(size, 1);
	memcpy(package, "package1", 8);
	PutBigEndianWord(package + 0x08, 'xxxx');
	PutBigEndianWord(package + 0x0c, 0x10000000);				// (as the ROM's own NTK packages)
	PutBigEndianWord(package + 0x10, 1);						// version
	PutBigEndianWord(package + 0x14, 0);						// no copyright
	PutBigEndianWord(package + 0x18, (0 << 16) | nameBytes);	// the name, at the start of the data
	PutBigEndianWord(package + 0x1c, size);
	PutBigEndianWord(package + 0x2c, directorySize);
	PutBigEndianWord(package + 0x30, 1);						// parts
	UByte* entry = package + kHeader;
	PutBigEndianWord(entry + 0x00, 0);							// the part's offset from the directory's end
	PutBigEndianWord(entry + 0x04, partSize);
	PutBigEndianWord(entry + 0x08, partSize);
	PutBigEndianWord(entry + 0x0c, 'form');
	PutBigEndianWord(entry + 0x14, 0x81);						// frames, notify
	UByte* data = package + kHeader + kEntry;
	for (ULong i = 0; name[i] != 0; i++)
		data[i * 2 + 1] = (UByte) name[i];
	memcpy(package + directorySize, nsof.fWriteBuffer->fBuffer, partSize);
	*packageSize = size;
	return package;
}


// a package read from a pipe: the bytes written to a memory pipe and the
// pipe handed to the loader, which streams them to the manager
static NewtonErr
StreamLoad(const void* package, ULong size, ULong* packageId)
{
	CTestPipe pipe(size);
	pipe.WriteChunk(package, size, false);
	pipe.Rewind();
	*packageId = 0;
	return LoadPackage(&pipe, packageId, false);
}


static void
TestStreamed(void)
{
	// a protocol part streamed: its code read into memory through the
	// manager's own pipe (LoadProtocolCode)
	ULong screenBuffer = 0;
	TPMIterator iter;
	iter.Init();
	for (; iter.More(); iter.NextPackage())
		if (Same(iter.PackageName(), "ScreenBuffer"))
			screenBuffer = iter.PackageId();
	iter.Done();
	EXPECT(screenBuffer != 0 && DeinstallPackage(screenBuffer) == noErr && Known(screenBuffer) == kError_No_Such_Package);
	ULong streamedScreen = 0;
	ULong screenSize = GetBigEndianWord(gROM + kScreenBuffer + 0x1c);
	EXPECT(StreamLoad(gROM + kScreenBuffer, screenSize, &streamedScreen) == noErr && streamedScreen != 0);
	EXPECT(Known(streamedScreen) == noErr);

	// a frames part streamed: read as one flattened frame by its handler
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RefVar(Intern((char*) "title")), RefVar(MakeString("Streamed")));
	SetFrameSlot(frame, RefVar(Intern((char*) "count")), RefVar(MAKEINT(42)));
	ULong size = 0;
	UByte* package = StreamedPackage("Streamed", frame, &size);
	ULong streamed = 0;
	long given = gGivenCount;
	EXPECT(StreamLoad(package, size, &streamed) == noErr && streamed != 0);
	Given* got = GivenFor(streamed);
	EXPECT(gGivenCount == given + 1 && got != nil && got->fType == 'form' && got->fPartId.partIndex == 0);
	if (got != nil)
	{
		RefVar read(*got->fFrame);
		EXPECT(IsFrame(read) && RINT(GetFrameSlot(read, RefVar(Intern((char*) "count")))) == 42);
		RefVar title(GetFrameSlot(read, RefVar(Intern((char*) "title"))));
		EXPECT(IsString(title) && Length(title) == 18);
	}
	// the same package again from a stream: refused as already there (last,
	// because the ROM leaves the sender's 'pipe' world waiting then)
	EXPECT(DeinstallPackage(streamed) == noErr && got != nil && got->fRemoved);
	ULong setup = 0, id = 0;
	TPMIterator list;
	list.Init();
	for (; list.More(); list.NextPackage())
		if (Same(list.PackageName(), "Setup"))
			setup = list.PackageId();
	list.Done();
	ULong setupSize = GetBigEndianWord(gROM + kSetup + 0x1c);
	EXPECT(setup != 0 && StreamLoad(gROM + kSetup, setupSize, &id) == kError_Package_Already_Exists && id == setup);
	free(package);
}


// A package kept on a store (StorePackages.h): Cardfile, as NTK writes it,
// stored through TLOPackageStore with each of two decompressors, mapped
// back byte for byte (bar the modify date Store sets), installed from
// the store, taken out of use and installed again (as at the next boot),
// written out by BackupPackage, and deleted.
static void
StoreScenario(const char* decompressor, const UByte* ntk, ULong size)
{
	TStore* store = (TStore*) THostStore::ClassInfo()->New();
	EXPECT(store != nil && store->Init(nil, 0x100000, 0, 0, kStoreIsInternal, nil) == noErr);
	if (store == nil)
		return;
	store->Format();
	TLrgObjStore* allocator = (TLrgObjStore*) NewByName("TLrgObjStore", nil, decompressor);
	EXPECT(allocator != nil);
	if (allocator == nil)
		return;
	CTestPipe pipe(size + 16);
	pipe.WriteChunk(ntk, size, false);
	pipe.Rewind();
	ULong id = 0;
	EXPECT(allocator->Create(&id, store, &pipe, 0, true, (char*) decompressor, nil, 0, nil) == noErr && id != 0);
	allocator->Delete();
	EXPECT(PackageAllocationOk(store, id) && IsOnStoreAsPackage(store, id));
	char* name = nil;
	EXPECT(LOCompanderName(&name, store, id) == noErr && name != nil && strcmp(name, decompressor) == 0);
	free(name);

	// read back
	ULong address = 0;
	NewtonErr mapErr = MapLargeObject(&address, store, id, true);
	EXPECT(mapErr == noErr && address != 0);
	if (address != 0)
	{
		const UByte* mapped = (const UByte*) address;
		EXPECT(memcmp(mapped, ntk, 0x24) == 0 && memcmp(mapped + 0x28, ntk + 0x28, size - 0x28) == 0);
		EXPECT(ObjectSize(address) == size);
		EXPECT(UnmapLargeObject(address) == noErr);
	}

	// installed from the store, taken away, installed again
	ULong packageId = 0;
	long given = gGivenCount;
	EXPECT(PackageAvailable(store, id, &packageId) == noErr && packageId != 0);
	EXPECT(Known(packageId) == noErr && gGivenCount == given + 1);
	TStore* whose = nil;
	PSSId whoseId = 0;
	ULong at = 0, pid = 0;
	EXPECT(IdToStore(packageId, &whose, &whoseId) == noErr && whose == store && whoseId == id);
	EXPECT(StoreToId(store, id, &pid) == noErr && pid == packageId);
	EXPECT(IdToVAddr(packageId, &at) == noErr && at != 0 && VAddrToId(&pid, at) == noErr && pid == packageId);
	EXPECT(PackageUnavailable(packageId) == noErr && Known(packageId) == kError_No_Such_Package);
	Given* got = GivenFor(packageId);
	EXPECT(got != nil && got->fRemoved);
	ULong again = 0;
	EXPECT(PackageAvailable(store, id, &again) == noErr && again != 0 && Known(again) == noErr);

	// written out as the package itself
	CTestPipe out(0x1000);
	TLrgObjStore* backup = (TLrgObjStore*) NewByName("TLrgObjStore", nil, decompressor);
	EXPECT(backup != nil && backup->Backup(&out, store, id, false, nil) == noErr);
	if (backup != nil)
		backup->Delete();
	out.Rewind();
	UByte* copy = (UByte*) malloc(size);
	long count = (long) size;
	Boolean eof = false;
	out.ReadChunk(copy, count, eof);
	EXPECT(count == (long) size && memcmp(copy, ntk, 0x24) == 0 && memcmp(copy + 0x28, ntk + 0x28, size - 0x28) == 0);
	free(copy);

	// deleted: taken out of use and off the store
	EXPECT(DeletePackage(again) == noErr && Known(again) == kError_No_Such_Package);
	long gone = 0;
	EXPECT(store->GetObjectSize(id, &gone) != noErr);
	store->Delete();
}


static void
TestOnStore(void)
{
	ULong cardfile = 0;
	TPMIterator iter;
	iter.Init();
	for (; iter.More(); iter.NextPackage())
		if (Same(iter.PackageName(), "Cardfile"))
			cardfile = iter.PackageId();
	iter.Done();
	if (cardfile != 0)
		EXPECT(DeinstallPackage(cardfile) == noErr);
	InitializeStoreDecompressors();
	UByte* ntk = AsNTKWritesIt(kCardfile);
	ULong size = GetBigEndianWord(ntk + 0x1c);
	// (flagged uncompressed, it would be stored by the simple one whatever
	// was asked for)
	EXPECT((GetBigEndianWord(ntk + 0x0c) & 0x10000000) != 0);
	StoreScenario("TSimpleStoreDecompressor", ntk, size);
	PutBigEndianWord(ntk + 0x0c, GetBigEndianWord(ntk + 0x0c) & ~0x10000000);
	StoreScenario("TLZStoreDecompressor", ntk, size);
	// kUseFasterCompressionFlag: Zippy (a third-party Mahjongg is flagged so)
	PutBigEndianWord(ntk + 0x0c, GetBigEndianWord(ntk + 0x0c) | 0x02000000);
	StoreScenario("TZippyStoreDecompressor", ntk, size);
	PutBigEndianWord(ntk + 0x0c, GetBigEndianWord(ntk + 0x0c) & ~0x02000000);

	// read from a file through the C library, as SuckPackageOffDeskTop
	// does (utility/StdioPipe.h): stored, and the same bytes come back
	FILE* f = fopen("test_PackageManager.pkg", "wb");
	EXPECT(f != nil);
	if (f != nil)
	{
		fwrite(ntk, 1, size, f);
		fclose(f);
		TStore* store = (TStore*) THostStore::ClassInfo()->New();
		EXPECT(store != nil && store->Init(nil, 0x100000, 0, 0, kStoreIsInternal, nil) == noErr);
		store->Format();
		ULong id = 0;
		{
			CStdioPipe file("test_PackageManager.pkg", "rb");
			EXPECT(StorePackage(&file, store, nil, &id) == noErr && id != 0);
		}
		ULong address = 0;
		EXPECT(MapLargeObject(&address, store, id, true) == noErr && address != 0);
		if (address != 0)
		{
			EXPECT(memcmp((const UByte*) address, ntk, 0x24) == 0
				   && memcmp((const UByte*) address + 0x28, ntk + 0x28, size - 0x28) == 0);
			UnmapLargeObject(address);
		}
		store->Delete();
		remove("test_PackageManager.pkg");
	}
	free(ntk);
}


// A package archived as chunk entries in a soup (packages/
// PackageArchivalPipe.h): written through the pipe's write side - a new
// entry each time the 4K buffer fills, its unique id added to the keys -
// and read back through the read side, which gives the same bytes.  (The
// native over it, store:RestoreSegmentedPackage, runs the ROM's own
// package scripts, which want the booted machine's globals: the demo
// src/host/demo/packagestore.ns restores a package that way.)
extern const ExceptionName exPipeException;

static void
TestSegmented(void)
{
	UByte* ntk = AsNTKWritesIt(kCardfile);
	ULong size = GetBigEndianWord(ntk + 0x1c);
	TStore* store = (TStore*) THostStore::ClassInfo()->New();
	EXPECT(store != nil && store->Init(nil, 0x200000, 0, 0, kStoreIsInternal, nil) == noErr);
	if (store == nil)
	{
		free(ntk);
		return;
	}
	store->Format();
	RefVar storeObject(RegisterTStore(store));
	RefVar soup(StoreCreateSoup(storeObject, RefVar(MakeString("Segments")), RefVar(NILREF)));
	RefVar keys(MakeArray(0));
	{
		CPackageArchivalPipe out;
		out.Init(soup, keys, false, true);
		out.WriteChunk(ntk, (long) size, false);
		out.FlushWrite();
		// one key a full 4K buffer, and one for what was left
		EXPECT(Length(keys) == (long) ((size + 0xfff) / 0x1000));
	}
	// each entry's PackageEntry binary is its piece, in the keys' order
	{
		CPackageArchivalPipe probe;
		probe.Init(soup, keys, true, false);
		UByte* data = nil;
		ULong length = 0;
		probe.GetPackageChunk(&data, &length);
		EXPECT(length == 0x1000 && memcmp(data, ntk, 0x1000) == 0);
		probe.GetPackageChunk(&data, &length);
		EXPECT(length == 0x1000 && memcmp(data, ntk + 0x1000, 0x1000) == 0);
		EXPECT(probe.fIndex == 2);
	}
	// read back whole, a buffer at a time
	{
		CPackageArchivalPipe in;
		in.Init(soup, keys, true, false);
		UByte* back = (UByte*) malloc(size);
		long count = (long) size;
		Boolean eof = false;
		in.ReadChunk(back, count, eof);
		EXPECT(count == (long) size && memcmp(back, ntk, size) == 0);
		free(back);
		// ... and past the last key: the cursor finds no entry, and that
		// comes out as the pipe exception
		Boolean threw = false;
		newton_try
		{
			Boolean more = false;
			in.Underflow(1, more);
		}
		newton_catch(exPipeException)
		{
			threw = true;
		}
		end_try;
		EXPECT(threw);
	}
	// a pipe made only for writing has nothing to read from
	{
		CPackageArchivalPipe writeOnly;
		writeOnly.Init(soup, keys, false, true);
		Boolean threw = false;
		newton_try
		{
			Boolean eof = false;
			writeOnly.Underflow(1, eof);
		}
		newton_catch(exPipeException)
		{
			threw = ((long) (Long) _info.exception.data == -10006);
		}
		end_try;
		EXPECT(threw);
	}
	RemoveTStore(store);
	free(ntk);
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
			TestOnStore();
			TestSegmented();
			TestStreamed();
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


#ifdef _WIN32
#define NOGDI
#include <windows.h>
// Where a fault happened, relative to the executable, and the return
// addresses on the stack above it - to be looked up in the link map.
static LONG CALLBACK
OnException(EXCEPTION_POINTERS* info)
{
	if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;
	uintptr_t base = (uintptr_t) GetModuleHandleA(nil);
	typedef BOOL (WINAPI *InitProc)(HANDLE, const char*, BOOL);
	typedef BOOL (WINAPI *FromAddrProc)(HANDLE, DWORD64, DWORD64*, void*);
	HMODULE help = LoadLibraryA("dbghelp.dll");
	InitProc init = help ? (InitProc) GetProcAddress(help, "SymInitialize") : nil;
	FromAddrProc from = help ? (FromAddrProc) GetProcAddress(help, "SymFromAddr") : nil;
	HANDLE process = GetCurrentProcess();
	if (init != nil)
		init(process, nil, TRUE);
	static unsigned char buffer[sizeof(DWORD) * 32 + 512];
	fprintf(stderr, "fault at %p\n", info->ExceptionRecord->ExceptionAddress);
	uintptr_t* sp = (uintptr_t*) info->ContextRecord->Rsp;
	uintptr_t addrs[401];
	addrs[0] = (uintptr_t) info->ExceptionRecord->ExceptionAddress;
	long n = 1;
	for (int i = 0; i < 400; i++)
		if (sp[i] > base && sp[i] < base + 0x2000000)
			addrs[n++] = sp[i];
	for (long i = 0; i < n; i++)
	{
		memset(buffer, 0, sizeof(buffer));
		ULONG* sym = (ULONG*) buffer;
		sym[0] = 88;						// SizeOfStruct of SYMBOL_INFO
		sym[20] = 256;						// MaxNameLen
		DWORD64 displacement = 0;
		if (from != nil && from(process, (DWORD64) addrs[i], &displacement, buffer))
			fprintf(stderr, "  %s+0x%llx\n", (const char*) (buffer + 84),
					(unsigned long long) displacement);
		else
			fprintf(stderr, "  +0x%llx (err %lu)\n", (unsigned long long) (addrs[i] - base),
					(unsigned long) GetLastError());
	}
	fflush(stderr);
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif


int
main()
{
#ifdef _WIN32
	AddVectoredExceptionHandler(1, OnException);
#endif
	gHostKernelServicesTask = Scenario;
	OsBoot();
	if (failures == 0)
		printf("test_PackageManager: all passed\n");
	else
		printf("test_PackageManager: %d failures\n", failures);
	return failures != 0;
}
