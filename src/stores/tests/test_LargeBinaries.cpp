// Large binaries test (src/stores/LargeBinaries.h, Ephemerals.h): with the
// companders registered (so the test runs as the kernel services task), a
// THostStore is registered as a store frame - its ephemeral tracker made
// and the persistent frame given its `ephemerals` list - and a large
// binary (a VBO) made on it with NewVBO: an indirect binary whose bytes are
// a mapped large object, ephemeral until an entry takes it.  Written into a
// soup entry it is committed (tag 12 of the store object format) and is no
// longer ephemeral; asked its store, compander and stored size; changed
// and the change undone (VBOUndoChanges); resized; cloned (a duplicate
// large object, ephemeral); the store taken away and registered again over
// the same bytes, the entry read back with its large binary loaded through
// gLBCache and its bytes read through the compander; a VBO no entry took
// deleted when the store is registered again; the entry removed (its large
// object with it); a large binary streamed through NSOF and read back onto
// the store; and one made with the LZ compander.

#include "LargeBinaries.h"
#include "LargeObjects.h"
#include "Ephemerals.h"
#include "Soups.h"
#include "Entries.h"
#include "StoreObject.h"
#include "ObjectStreamer.h"
#include "StoreCompander.h"
#include "host/HostStore.h"
#include "Compiler.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "Compression.h"
#include "Protocols.h"
#include "Boot.h"
#include "UserBoot.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"
#include "AppWorld.h"
#include "UserTasks.h"
#include "../../utility/tests/TestPipe.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref SYMBOL(const char* name) { return Intern((char*) name); }


static Ref
Eval(const char* source)
{
	if (getenv("EVAL_TRACE"))
		fprintf(stderr, "eval: %s\n", source);
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static void
SetGlobal(const char* name, RefArg value)
{
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL(name)), value);
}


static UByte
Pattern(long i, long seed)
{
	return (UByte) ((i * 13 + seed + (i >> 7)) & 0xff);
}


static void
Fill(RefArg vbo, long length, long seed)
{
	UByte* data = (UByte*) BinaryData(vbo);
	for (long i = 0; i < length; i++)
		data[i] = Pattern(i, seed);
}


static Boolean
Holds(RefArg vbo, long length, long seed)
{
	if (Length(vbo) != length)
		return false;
	const UByte* data = (const UByte*) BinaryData(vbo);
	for (long i = 0; i < length; i++)
		if (data[i] != Pattern(i, seed))
			return false;
	return true;
}


static Boolean
ObjectExists(TStore* store, PSSId id)
{
	long size;
	return store->GetObjectSize(id, &size) == noErr;
}


static void
TestLargeBinaries(void)
{
	TStore* store = TStore::New("THostStore");
	EXPECT(store != nil);
	EXPECT(store->Init(nil, 0x100000, 0, 0, kStoreIsInternal, nil) == noErr);
	EXPECT(store->Format() == noErr);

	// the store frame: a tracker, and the persistent frame's list of ephemerals
	RefVar storeObject(RegisterTStore(store));
	TStoreWrapper* wrapper = GetStoreWrapper(storeObject);
	EXPECT(wrapper->fEphemeralTracker != nil);
	RefVar persistent(GetFrameSlotRef(storeObject, RSSYM_proto));
	EXPECT(ISINT(GetFrameSlotRef(persistent, RSSYMephemerals)));
	SetGlobal("theStore", storeObject);
	SetGlobal("theSoup", RefVar(Eval("theStore:CreateSoup(\"Samples\", [])")));

	// a VBO: mapped, an ephemeral, on this store, the simple compander's
	RefVar vbo(Eval("theStore:NewVBO('samples, 5000)"));
	EXPECT(IsLargeBinary(vbo) && Length(vbo) == 5000);
	EXPECT(EQRef(ClassOf(vbo), SYMBOL("samples")));
	// (an object's body moves when the heap is compacted: LargeBinaryData is
	// asked afresh each time rather than kept)
	PSSId id = LargeBinaryData(vbo)->fId;
	EXPECT(LargeBinaryData(vbo)->GetStore() == wrapper && LargeBinaryData(vbo)->fAddress != 0 && LargeBinaryData(vbo)->fEntry == NILREF);
	EXPECT(wrapper->fEphemeralTracker->IsEphemeral(id));
	Fill(vbo, 5000, 1);
	SetGlobal("theVBO", vbo);
	EXPECT(Eval("IsVBO(theVBO)") == TRUEREF && Eval("IsVBO(\"not one\")") == NILREF);
	EXPECT(EQRef(Eval("GetVBOStore(theVBO)"), storeObject));
	RefVar compander(Eval("GetVBOCompander(theVBO)"));
	EXPECT(IsString(compander) && Length(compander) == (long) (strlen("TSimpleStoreCompander") + 1) * 2);
	EXPECT(Eval("GetVBOCompanderData(theVBO)") == NILREF);

	// into an entry: committed (tag 12) and no longer ephemeral
	RefVar entry(Eval("theSoup:Add({title: \"one\", data: theVBO})"));
	EXPECT(IsFaultBlock(entry));
	EXPECT(!wrapper->fEphemeralTracker->IsEphemeral(id));
	EXPECT(LargeBinaryData(vbo)->fEntry != NILREF && LargeBinaryData(vbo)->IsSameEntry(entry));
	EXPECT(EQRef(FindLargeBinaryInCache(wrapper, id), vbo));
	EXPECT(RINT(Eval("GetVBOStoredSize(theVBO)")) > 0);
	SetGlobal("theEntry", entry);
	// EntrySize counts what the VBO takes up on the store; EntrySizeWithoutVBOs does not
	EXPECT(RINT(Eval("EntrySize(theEntry)")) == RINT(Eval("EntrySizeWithoutVBOs(theEntry)")) + RINT(Eval("GetVBOStoredSize(theVBO)")));
	EXPECT(Eval("ClearVBOCache(theVBO)") == NILREF);
	EXPECT(Holds(vbo, 5000, 1));

	// a change undone: the committed bytes come back
	((UByte*) BinaryData(vbo))[10] ^= 0xff;
	EXPECT(Eval("VBOUndoChanges(theVBO)") == NILREF);
	EXPECT(LargeBinaryData(vbo)->fAddress == 0);
	EXPECT(Holds(vbo, 5000, 1));		// mapped again on the asking

	// resized, changed, and the entry written again
	SetLength(vbo, 7000);
	EXPECT(Length(vbo) == 7000);
	Fill(vbo, 7000, 2);
	Eval("EntryChange(theEntry)");
	EXPECT(EQRef(FindLargeBinaryInCache(wrapper, id), vbo));

	// a clone: another large object, ephemeral, the same bytes
	RefVar clone(Clone(vbo));
	EXPECT(IsLargeBinary(clone) && LargeBinaryData(clone)->fId != id);
	EXPECT(wrapper->fEphemeralTracker->IsEphemeral(LargeBinaryData(clone)->fId));
	EXPECT(Holds(clone, 7000, 2));

	// one no entry takes: on the store's list of ephemerals, and taken off it
	// when the store comes back.  (Here the store is taken away before the
	// list has been written back, so the orphan was never on the store's
	// own list and stays; one on it would be deleted by DeleteLargeObject's
	// default, LODefaultDelete - DeallocatePackage.)
	RefVar orphan(Eval("theStore:NewCompressedVBO('orphan, 1234, nil, nil)"));
	PSSId orphanId = LargeBinaryData(orphan)->fId;
	EXPECT(ObjectExists(store, orphanId));
	EXPECT(wrapper->fEphemeralTracker->IsEphemeral(orphanId));

	// the store taken away and registered again over the same bytes
	RemoveTStore(store);
	EXPECT(LargeBinaryData(vbo)->fAddress == 0 && LargeBinaryData(vbo)->GetStore() == nil);
	storeObject = RegisterTStore(store);
	wrapper = GetStoreWrapper(storeObject);
	EXPECT(!wrapper->fEphemeralTracker->IsEphemeral(orphanId));
	EXPECT(ObjectExists(store, orphanId));
	SetGlobal("theStore", storeObject);
	RefVar again(Eval("local c := Query(theStore:GetSoup(\"Samples\"), {}); c:Entry()"));
	EXPECT(IsFaultBlock(again));
	RefVar data(GetFrameSlotRef(again, SYMBOL("data")));
	EXPECT(IsLargeBinary(data) && LargeBinaryData(data)->fId == id);
	EXPECT(LargeBinaryData(data)->GetStore() == wrapper);
	EXPECT(EQRef(ClassOf(data), SYMBOL("samples")));
	EXPECT(Holds(data, 7000, 2));

	// streamed through NSOF and read back onto the store: a new large object
	{
		CTestPipe pipe(0x4000);
		RefVar frame(AllocateFrame());
		SetFrameSlot(frame, RefVar(SYMBOL("v")), data);
		TObjectWriter writer(frame, pipe, false);
		long size = writer.Size();
		writer.Write();
		EXPECT(size == pipe.WritePosition());
		pipe.Rewind();
		TObjectReader reader(pipe, storeObject);
		RefVar read(reader.Read());
		RefVar v(GetFrameSlotRef(read, SYMBOL("v")));
		EXPECT(IsLargeBinary(v) && LargeBinaryData(v)->fId != id);
		EXPECT(EQRef(ClassOf(v), SYMBOL("samples")));
		EXPECT(Holds(v, 7000, 2));
	}
	// ... and streamed compressed (SetCompressLargeBinaries: the blocks as
	// they lie on the store), read back by LODefCreateFromComp
	{
		CTestPipe pipe(0x4000);
		RefVar frame(AllocateFrame());
		SetFrameSlot(frame, RefVar(SYMBOL("v")), data);
		TObjectWriter writer(frame, pipe, false);
		writer.SetCompressLargeBinaries();
		long size = writer.Size();
		writer.Write();
		EXPECT(size == pipe.WritePosition());
		pipe.Rewind();
		TObjectReader reader(pipe, storeObject);
		RefVar read(reader.Read());
		RefVar v(GetFrameSlotRef(read, SYMBOL("v")));
		EXPECT(IsLargeBinary(v) && LargeBinaryData(v)->fId != id);
		EXPECT(EQRef(ClassOf(v), SYMBOL("samples")));
		EXPECT(Holds(v, 7000, 2));
	}

	// the entry removed: its large object is deleted - the binary still in
	// memory makes it an ephemeral again, until it goes
	SetGlobal("again", again);
	Eval("EntryRemoveFromSoup(again)");
	EXPECT(wrapper->fEphemeralTracker->IsEphemeral(id));
	EXPECT(LargeBinaryData(data)->fEntry == NILREF);

	// the LZ compander
	RefVar lz(Eval("theStore:NewCompressedVBO('lz, 3000, \"TLZStoreCompander\", nil)"));
	Fill(lz, 3000, 3);
	SetGlobal("theLZ", lz);
	EXPECT(Eval("GetVBOCompander(theLZ)") != NILREF);
	Eval("theSoup := theStore:GetSoup(\"Samples\"); theSoup:Add({title: \"lz\", data: theLZ})");
	EXPECT(Eval("ClearVBOCache(theLZ)") == NILREF);
	EXPECT(Holds(lz, 3000, 3));

	RemoveTStore(store);
	store->Delete();
}


// the test runs in an application world of its own (the package store's
// part handler, which InitQueries registers, wants a world's port)
static volatile Boolean gDone = false;

class TTestWorld : public TAppWorld
{
public:
	virtual TForkWorld*	MakeFork()			{ return new TTestWorld; }
	virtual long		MainConstructor()
	{
		long err = TAppWorld::MainConstructor();
		if (err != noErr)
			return err;
		gObjectHeapSize = 0x200000;
		InitObjects();
		InitializeCompression();
		RegisterLargeBinaryNatives();
		InitQueries();
		SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
		SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
		return noErr;
	}
	virtual long		PreMain()
	{
		newton_try
		{
			TestLargeBinaries();
		}
		newton_catch_all
		{
			failures++;
			fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
		}
		end_try;
		gDone = true;
		return noErr;
	}
};


static void
Scenario(void)
{
	RegisterStoreImplementations();
	InitializeStoreCompanders();
	TTestWorld world;
	EXPECT(world.Init('lbts', true, 0x4000) == noErr);
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
		printf("test_LargeBinaries: all passed\n");
	else
		printf("test_LargeBinaries: %d failures\n", failures);
	return failures != 0;
}
