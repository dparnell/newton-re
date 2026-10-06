/*
	File:		stores/LargeBinaries.cpp

	Contains:	Large binaries - see LargeBinaries.h - and the NewtonScript
				functions over them: NewVBO, NewCompressedVBO, IsVBO,
				GetVBOStore, GetVBOCompander, GetVBOCompanderData,
				GetVBOStoredSize, ClearVBOCache, VBOUndoChanges.

	Reconstructed from the MP2x00 US ROM (0x000fff5c-0x00101460); each
	function cites its origin.
*/

#include "LargeBinaries.h"
#include "NarrowRef.h"
#include "LargeObjects.h"
#include "Ephemerals.h"
#include "Entries.h"			// MakeEntryCache, PutEntryIntoCache, FaultBlockObject
#include "Soups.h"				// ToObject, CheckWriteProtect, IsValidStore
#include "PackageIterator.h"	// IsPackageHeader
#include "FramesPart.h"			// RemoveProvisionalFramesParts
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "DynamicArray.h"
#include "host/RomBugs.h"

#include <string.h>

extern const ExceptionName exOutOfMemory;


// ROM 0x0c1010c0 gLBCache
// Every large binary read from a store, or committed to one: an entry cache
// (a weak array), so the same large object read twice is the same binary.
Ref		gLBCache = NILREF;

// ROM 0x0c1010c4 (unnamed)
// The stores large binaries are on, each with how many large binaries are
// (an LBData's fStoreIndex is an index into it).  A slot whose count has
// gone to nought is used again.  DEVIATION: an element is sizeof bytes on
// the host (the ROM's are 8: a pointer and a count).
struct LBStoreSlot
{
	TStoreWrapper*	fStore;
	long			fCount;
};
static CDynamicArray*	gLBStores = nil;

extern IndirectBinaryProcs	gLBProcs;			// frames/Objects.cpp (DEVIATION: filled in by InitLargeObjects)


// DEVIATION: a large binary of a string class is kept in the host's order
// while it is mapped (frames/HostOrder.h), the store holding a MessagePad's
// big-endian UniChars - its class told to the host's domain manager each
// time it is mapped, or changes
static void
LBSetHostOrder(ULong address, RefArg theClass)
{
	if (address != 0)
		SetLargeObjectHostOrder(address, HostOrderOfClass(theClass));
}

static void
LBSetHostOrder(LBData* lb, TStoreWrapper* wrapper)
{
	LBSetHostOrder(lb->fAddress, RefVar(wrapper->ReferenceToSymbol(lb->fClassRef)));
}


/*------------------------------------------------------------------------------
	L B D a t a
------------------------------------------------------------------------------*/

// ROM 0x00100fa0 GetStore__6LBDataCFv
// The store wrapper the large binary is on; nil when it has none (or its
// store has gone: LargeBinariesStoreRemoved).
TStoreWrapper*
LBData::GetStore(void) const
{
	if (fStoreIndex != -1)
		return ((LBStoreSlot*) gLBStores->SafeElementPtrAt(fStoreIndex))->fStore;
	return nil;
}


// ROM 0x00100fd4 SetStore__6LBDataFP13TStoreWrapper
// The large binary put on wrapper's store - its slot's count raised, or a
// free slot taken, or a slot added - or, with nil, taken off its store (a
// slot whose count comes to nought forgets its store).
void
LBData::SetStore(TStoreWrapper* wrapper)
{
	if (wrapper == nil)
	{
		LBStoreSlot* slot = (LBStoreSlot*) gLBStores->SafeElementPtrAt(fStoreIndex);
		if (--slot->fCount == 0)
			slot->fStore = nil;
		fStoreIndex = -1;
		return;
	}
	long freeSlot = -1;
	ArrayIndex count = gLBStores->GetArraySize();
	for (ArrayIndex i = 0; i < count; i++)
	{
		LBStoreSlot* slot = (LBStoreSlot*) gLBStores->SafeElementPtrAt(i);
		if (slot->fStore == wrapper)
		{
			slot->fCount++;
			fStoreIndex = (long) i;
			return;
		}
		if (slot->fCount == 0)
			freeSlot = (long) i;
	}
	if (freeSlot != -1)
	{
		fStoreIndex = freeSlot;
		LBStoreSlot* slot = (LBStoreSlot*) gLBStores->SafeElementPtrAt(freeSlot);
		slot->fStore = wrapper;
		slot->fCount = 1;
		return;
	}
	LBStoreSlot slot = { wrapper, 1 };
	gLBStores->InsertElementsBefore(count, &slot, 1);
	fStoreIndex = (long) count;
}


// ROM 0x001010c0 IsSameEntry__6LBDataFl
// Whether the large binary is held by entry - both sides taken through
// their fault blocks to the objects in memory; nil is nobody's entry.
Boolean
LBData::IsSameEntry(Ref entry)
{
	if (IsFaultBlock(entry))
		entry = FaultBlockObject(entry);
	Ref mine = fEntry;
	if (IsFaultBlock(mine))
		mine = FaultBlockObject(mine);
	return !(mine == NILREF || entry == NILREF || mine != entry);
}


/*------------------------------------------------------------------------------
	T h e   i n d i r e c t   b i n a r y   p r o c e d u r e s
------------------------------------------------------------------------------*/

// ROM 0x000fff5c LBLength__FPc
static long
LBLength(void* data)
{
	return ((LBData*) data)->fLength;
}


// ROM 0x000fff64 LBDataPtr__FPc
// The bytes: the large object mapped the first time they are asked for.
static char*
LBDataPtr(void* data)
{
	LBData* lb = (LBData*) data;
	TStoreWrapper* wrapper = lb->GetStore();
	if (wrapper == nil)
		Throw(exStoreError, (void*) (Long) kNSErrInvalidStore, nil);		// (0xffff446f)
	if (lb->fAddress == 0)
	{
		OSErrIf(MapLargeObject(&lb->fAddress, wrapper->fStore, lb->fId, false));
		LBSetHostOrder(lb, wrapper);
	}
	return (char*) lb->fAddress;
}


// ROM 0x0010073c LBSetLength__FPcl
// The large object resized to length (at its end).
static void
LBSetLength(void* data, long length)
{
	LBData* lb = (LBData*) data;
	if (lb->GetStore() == nil)
		Throw(exStoreError, (void*) (Long) kNSErrInvalidStore, nil);
	OSErrIf(ResizeLargeObject(&lb->fAddress, lb->fAddress, length, -1));
	lb->fLength = length;
}


// ROM 0x00101220 LBClone__FPcl
// A copy: a new large binary of theClass on the same store, its large
// object a duplicate of this one's, ephemeral until an entry takes it.
// (The class reference is this binary's, whatever theClass is - as the
// ROM has it.)
static Ref
LBClone(void* data, Ref theClass)
{
	LBData copy = *(LBData*) data;
	TStoreWrapper* wrapper = copy.GetStore();
	if (wrapper == nil)
		Throw(exStoreError, (void*) (Long) kNSErrInvalidStore, nil);
	CheckWriteProtect(wrapper->fStore);
	RefVar cls(theClass);
	RefVar obj(AllocateLargeBinary(cls, copy.fClassRef, copy.fLength, wrapper));
	LBData* lb = LargeBinaryData(obj);
	OSErrIf(DuplicateLargeObject(&lb->fId, wrapper->fStore, copy.fId, wrapper->fStore));
	OSErrIf(MapLargeObject(&lb->fAddress, wrapper->fStore, lb->fId, false));
	LBSetHostOrder(lb, wrapper);
	wrapper->fEphemeralTracker->AddEphemeral(lb->fId);
	return obj;
}


// ROM 0x00101368 LBDestroy__FPc
// The binary collected: taken off its store, its uncommitted changes
// thrown away, any Ref into its bytes declawed, and - when no entry ever
// took it - its large object deleted (DeleteEphemeral1, inline in the
// ROM).
static void
LBDestroy(void* data)
{
	LBData* lb = (LBData*) data;
	TStoreWrapper* wrapper = lb->GetStore();
	lb->SetStore(nil);
	if (wrapper == nil)
		return;
	AbortObject(wrapper->fStore, lb->fId);
	if (!IsValidStore(wrapper->fStore))
		return;
	RegisterLargeBinaryForDeclawing(lb);
	TEphemeralTracker* tracker = wrapper->fEphemeralTracker;
	if (tracker == nil || !tracker->IsEphemeral(lb->fId))
		return;
	tracker->DeleteEphemeral1(lb->fId);
}


// ROM 0x001013e4 LBSetClass__FPcRC6RefVar
// The class changed: its reference in the store's symbol table, made under
// a lock of the store (an error aborts it).
static void
LBSetClass(void* data, RefArg theClass)
{
	LBData* lb = (LBData*) data;
	TStoreWrapper* wrapper = lb->GetStore();
	if (wrapper == nil)
		Throw(exStoreError, (void*) (Long) kNSErrInvalidStore, nil);
	if (lb->fAddress == 0)
		MapLargeObject(&lb->fAddress, wrapper->fStore, lb->fId, false);
	OSErrIf(wrapper->LockStore());
	newton_try
	{
		lb->fClassRef = wrapper->SymbolToReference(theClass);
		// (the bytes turned from the old class's order to the new one's -
		// FSetClass leaves an indirect binary's to this)
		LBSetHostOrder(lb->fAddress, theClass);
	}
	newton_catch_all
	{
		OSErrIf(wrapper->Abort());
		rethrow;
	}
	end_try;
	OSErrIf(wrapper->UnlockStore());
}


// ROM 0x000fffcc LBMark__FPc
// The entry that holds it kept alive (the ROM's is DIYGCMark, inline).
static void
LBMark(void* data)
{
	DIYGCMark(((LBData*) data)->fEntry);
}


// ROM 0x000fffd4 LBUpdate__FPc
static void
LBUpdate(void* data)
{
	LBData* lb = (LBData*) data;
	lb->fEntry = DIYGCUpdate(lb->fEntry);
}


/*------------------------------------------------------------------------------
	M a k i n g   a n d   f i n d i n g   t h e m
------------------------------------------------------------------------------*/

// ROM 0x00101134 InitLargeObjects__Fv
// The cache and the table of stores.  (DEVIATION: and gLBProcs, the frames
// layer's, filled in - the ROM's is initialised data.)
void
InitLargeObjects(void)
{
	if (gLBStores != nil)		// DEVIATION: a host program may set the stores up twice (InitQueries)
		return;
	gLBProcs.fLength = LBLength;
	gLBProcs.fDataPtr = LBDataPtr;
	gLBProcs.fSetLength = LBSetLength;
	gLBProcs.fClone = LBClone;
	gLBProcs.fDelete = LBDestroy;
	gLBProcs.fSetClass = LBSetClass;
	gLBProcs.fMark = LBMark;
	gLBProcs.fUpdate = LBUpdate;
	AddGCRoot(gLBCache);
	gLBCache = MakeEntryCache();
	gLBStores = new CDynamicArray(sizeof(LBStoreSlot), 1);
	if (gLBStores == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
}


// ROM 0x00100dbc AllocateLargeBinary__FRC6RefVarlT2P13TStoreWrapper
// A large binary of theClass (classRef in wrapper's symbol table) and
// length on wrapper's store, with no large object yet and no entry.
Ref
AllocateLargeBinary(RefArg theClass, long classRef, long length, TStoreWrapper* wrapper)
{
	Ref obj = gHeap->AllocateIndirectBinary(theClass, sizeof(LBData));
	ObjIndirectProcs(OBJ(obj)) = &gLBProcs;
	LBData* lb = LargeBinaryData(obj);
	lb->fStoreIndex = -1;
	lb->SetStore(wrapper);
	lb->fId = 0;
	lb->fEntry = NILREF;
	lb->fLength = length;
	lb->fClassRef = classRef;
	lb->fAddress = 0;
	return obj;
}


// ROM 0x00100e5c WrapLargeObject
// A large binary for the large object id of store, already mapped at
// address (0: not mapped - its length then 0): ephemeral until an entry
// takes it, and cached.
Ref
WrapLargeObject(TStore* store, RefArg theClass, PSSId id, ULong address)
{
	RefVar storeObject(ToObject(store));
	if (ISNIL(storeObject))
		Throw(exStoreError, (void*) (Long) kNSErrInvalidStore, nil);
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
	if (wrapper->fEphemeralTracker == nil)
		Throw(exStoreError, (void*) (Long) kNSErrNoEphemeralTracker, nil);
	CheckWriteProtect(wrapper->fStore);
	wrapper->fEphemeralTracker->AddEphemeral(id);
	long length = address == 0 ? 0 : ObjectSize(address);
	long classRef = wrapper->SymbolToReference(theClass);
	RefVar obj(AllocateLargeBinary(theClass, classRef, length, wrapper));
	LBData* lb = LargeBinaryData(obj);
	lb->fId = id;
	lb->fClassRef = classRef;
	lb->fAddress = address;
	LBSetHostOrder(address, theClass);
	PutEntryIntoCache(RefVar(gLBCache), obj);
	return obj;
}


// ROM 0x00100414 FindLargeBinaryInCache__FP13TStoreWrapperUl
// The cached large binary of large object id on wrapper's store, or nil.
Ref
FindLargeBinaryInCache(TStoreWrapper* wrapper, PSSId id)
{
	long count = Length(gLBCache);
	for (long i = 0; i < count; i++)
	{
		Ref obj = GetArraySlotRef(gLBCache, i);
		if (obj != NILREF)
		{
			LBData* lb = LargeBinaryData(obj);
			if (lb->fId == id && lb->GetStore() == wrapper)
				return obj;
		}
	}
	return NILREF;
}


// ROM 0x0010049c LoadLargeBinary__FP13TStoreWrapperUll
// The large binary of large object id read from wrapper's store (its class
// classRef): the cached one, or a new one mapped now and cached.
Ref
LoadLargeBinary(TStoreWrapper* wrapper, PSSId id, long classRef)
{
	RefVar obj(FindLargeBinaryInCache(wrapper, id));
	if (ISNIL(obj))
	{
		ULong address;
		OSErrIf(MapLargeObject(&address, wrapper->fStore, id, false));
		long length = ObjectSize(address);
		RefVar cls(wrapper->ReferenceToSymbol(classRef));
		obj = AllocateLargeBinary(cls, classRef, length, wrapper);
		LBData* lb = LargeBinaryData(obj);
		lb->fId = id;
		lb->fAddress = address;
		LBSetHostOrder(address, cls);
		PutEntryIntoCache(RefVar(gLBCache), obj);
	}
	return obj;
}


// ROM 0x00100598 DuplicateLargeBinary__FRC6RefVarP13TStoreWrapper
// A copy of the large binary on wrapper's store (the one an entry is being
// written to): its large object duplicated there under a lock of the store
// (an error aborts it), mapped, and ephemeral until the entry takes it.
Ref
DuplicateLargeBinary(RefArg obj, TStoreWrapper* wrapper)
{
	LBData from = *LargeBinaryData(obj);
	TStoreWrapper* fromWrapper = from.GetStore();
	if (fromWrapper == nil)
		Throw(exStoreError, (void*) (Long) kNSErrInvalidStore, nil);
	CheckWriteProtect(wrapper->fStore);
	long length = Length(obj);
	RefVar cls(ClassOf(obj));
	long classRef = wrapper->SymbolToReference(cls);
	RefVar copy(AllocateLargeBinary(RefVar(ClassOf(obj)), classRef, length, wrapper));
	LBData* lb = LargeBinaryData(copy);
	OSErrIf(wrapper->LockStore());
	newton_try
	{
		OSErrIf(DuplicateLargeObject(&lb->fId, fromWrapper->fStore, from.fId, wrapper->fStore));
		OSErrIf(MapLargeObject(&lb->fAddress, wrapper->fStore, lb->fId, false));
		LBSetHostOrder(lb->fAddress, cls);
		wrapper->fEphemeralTracker->AddEphemeral(lb->fId);
	}
	newton_catch_all
	{
		OSErrIf(wrapper->Abort());
		rethrow;
	}
	end_try;
	OSErrIf(wrapper->UnlockStore());
	return copy;
}


// ROM 0x0010079c DeleteLargeBinary__FP13TStoreWrapperUl
// Large object id no longer held by the entry that held it: deleted, or -
// while a large binary of it is still in memory - made ephemeral again
// and let go of its entry.
void
DeleteLargeBinary(TStoreWrapper* wrapper, PSSId id)
{
	RefVar obj(FindLargeBinaryInCache(wrapper, id));
	if (ISNIL(obj))
		DeleteLargeObject(wrapper->fStore, id);
	else
	{
		wrapper->fEphemeralTracker->AddEphemeral(id);
		LargeBinaryData(obj)->fEntry = NILREF;
	}
}


// ROM 0x00100804 FinalizeLargeObjectWrites__FP13TStoreWrapperP13CDynamicArrayT2
// An entry written: each large object it held before (previous) and does
// not now (written) deleted, then the store's ephemerals flushed (the
// ROM's FlushEphemerals, inline).  Nothing at all when previous is nil.
void
FinalizeLargeObjectWrites(TStoreWrapper* wrapper, CDynamicArray* previous, CDynamicArray* written)
{
	if (previous == nil)
		return;
	for (long i = (long) previous->GetArraySize() - 1; i >= 0; i--)
	{
		StorePSSId id = *(StorePSSId*) previous->SafeElementPtrAt(i);
		Boolean found = false;
		if (written != nil)
		{
			for (long j = (long) written->GetArraySize() - 1; j >= 0; j--)
				if (*(StorePSSId*) written->SafeElementPtrAt(j) == id)
				{
					found = true;
					break;
				}
		}
		if (!found)
			DeleteLargeBinary(wrapper, id);
	}
	wrapper->fEphemeralTracker->FlushEphemerals();
}


// ROM 0x001008a4 AbortLargeBinaries__FRC6RefVar
// The changes to entry undone: each of its large binaries that is mapped
// has its changes thrown away and is let go of (any Ref into its bytes
// declawed).
void
AbortLargeBinaries(RefArg entry)
{
	Boolean declaw = false;
	for (long i = Length(gLBCache) - 1; i >= 0; i--)
	{
		Ref obj = GetArraySlotRef(gLBCache, i);
		if (obj == NILREF)
			continue;
		LBData* lb = LargeBinaryData(obj);
		TStoreWrapper* wrapper;
		if (lb->fAddress != 0 && lb->IsSameEntry(entry) && (wrapper = lb->GetStore()) != nil)
		{
			OSErrIf(AbortObject(wrapper->fStore, lb->fId));
			declaw = RegisterLargeBinaryForDeclawing(lb) || declaw;
			lb->fAddress = 0;
		}
	}
	if (declaw)
		DeclawRefsInRegisteredRanges();		// (inline in the ROM)
}


// ROM 0x00100970 CommitLargeBinary__FRC6RefVar
// The large binary written into an entry: its changes committed to the
// store, cached, and no longer ephemeral.
void
CommitLargeBinary(RefArg obj)
{
	LBData* lb = LargeBinaryData(obj);
	OSErrIf(CommitObject(lb->fAddress));
	PSSId id = lb->fId;
	TStoreWrapper* wrapper = lb->GetStore();
	if (FindLargeBinaryInCache(wrapper, id) == NILREF)
		PutEntryIntoCache(RefVar(gLBCache), obj);
	if (wrapper->fEphemeralTracker->IsEphemeral(id))
		wrapper->fEphemeralTracker->RemoveEphemeral(id);
}


// ROM 0x00100a24 LargeBinariesStoreRemoved__FP13TStoreWrapper
// A store gone: its large binaries let go of their bytes (any Ref into
// them declawed), and its changes to large objects thrown away.
void
LargeBinariesStoreRemoved(TStoreWrapper* wrapper)
{
	Boolean declaw = false;
	long count = Length(gLBCache);
	for (long i = 0; i < count; i++)
	{
		Ref obj = GetArraySlotRef(gLBCache, i);
		if (obj == NILREF)
			continue;
		LBData* lb = LargeBinaryData(obj);
		if (lb->GetStore() == wrapper)
		{
			declaw = RegisterLargeBinaryForDeclawing(lb) || declaw;
			lb->fAddress = 0;
		}
	}
	ArrayIndex stores = gLBStores->GetArraySize();
	for (ArrayIndex i = 0; i < stores; i++)
	{
		LBStoreSlot* slot = (LBStoreSlot*) gLBStores->SafeElementPtrAt(i);
		if (slot->fStore == wrapper)
		{
			AbortObjects(wrapper->fStore);
			slot->fStore = nil;
		}
	}
	if (declaw)
		DeclawRefsInRegisteredRanges();
}


// ROM 0x00100b24 BreakLargeObjectToEntryLink__FUlP13TStoreWrapper
// The cached large binary of large object id no longer held by an entry.
void
BreakLargeObjectToEntryLink(PSSId id, TStoreWrapper* wrapper)
{
	RefVar obj(FindLargeBinaryInCache(wrapper, id));
	if (NOTNIL(obj))
		LargeBinaryData(obj)->fEntry = NILREF;
}


// ROM 0x00100b70 GetEntryFromLargeObjectVAddr
// The cached large binary mapped at address, or nil.
Ref
GetEntryFromLargeObjectVAddr(ULong address)
{
	long count = Length(gLBCache);
	for (long i = 0; i < count; i++)
	{
		Ref obj = GetArraySlotRef(gLBCache, i);
		if (obj != NILREF && LargeBinaryData(obj)->fAddress == address)
			return obj;
	}
	return NILREF;
}


// ROM 0x00101310 RegisterLargeBinaryForDeclawing__FPC6LBData
// The large binary's bytes registered to have every Ref into them declawed
// at the next GC - when it is mapped at an address that is no longer a
// large object's, or its bytes are a package.  ==> whether it was.
Boolean
RegisterLargeBinaryForDeclawing(const LBData* data)
{
	if (data->fAddress != 0
	&& (!LargeObjectAddressIsValid(data->fAddress) || IsPackageHeader((const void*) data->fAddress, 0x34)))
	{
		// DEVIATION: a part of the package only looked at was imported,
		// and goes with it (frames/FramesPart.h)
		RemoveProvisionalFramesParts((const void*) data->fAddress, (const void*) (data->fAddress + data->fLength));
		return RegisterRangeForDeclawing(data->fAddress, data->fAddress + data->fLength);
	}
	return false;
}


// ROM 0x00100320 MungeLargeBinary__FRC6RefVarlT2
// count bytes put in (or, negative, taken out) at offset of large binary b.
void
MungeLargeBinary(RefArg b, long offset, long count)
{
	LBData* lb = LargeBinaryData(b);
	if (lb->GetStore() == nil)
		Throw(exStoreError, (void*) (Long) kNSErrInvalidStore, nil);
	long length = lb->fLength;
	OSErrIf(ResizeLargeObject(&lb->fAddress, lb->fAddress, length + count, offset));
	lb->fLength = length + count;
}


/*------------------------------------------------------------------------------
	L a r g e   b i n a r i e s ,   f r o m   a   s c r i p t
------------------------------------------------------------------------------*/

// ROM 0x000ffff4 FLBAllocCompressed
// store:NewCompressedVBO(class, length, companderName, companderData): a
// large binary of length bytes on the store, packed by the compander named
// (nil: TSimpleStoreCompander) made with companderData (nil: none) - made
// under a lock of the store (an error aborts it), mapped, and ephemeral
// until an entry takes it.
Ref
FLBAllocCompressed(RefArg rcvr, RefArg theClass, RefArg length, RefArg companderName, RefArg companderData)
{
	GC();
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMstore);
	CheckWriteProtect(wrapper->fStore);
	if (wrapper->fEphemeralTracker == nil)
		Throw(exStoreError, (void*) (Long) kNSErrNoEphemeralTracker, nil);
	const char* compander = "TSimpleStoreCompander";
	RefVar name;
	if (NOTNIL(companderName))
	{
		name = ASCIIString(companderName);
		compander = BinaryData(name);
	}
	void* parameters = nil;
	long parametersSize = 0;
	if (NOTNIL(companderData))
	{
		parametersSize = Length(companderData);
		parameters = BinaryData(companderData);
	}
	OSErrIf(wrapper->LockStore());
	ULong id = 0;
	ULong address = 0;
	newton_try
	{
		OSErrIf(CreateLargeObject(&id, wrapper->fStore, LongArg(RINT(length)), (char*) compander, parameters, parametersSize));
		NewtonErr err = MapLargeObject(&address, wrapper->fStore, id, false);
		if (err != noErr)
		{
			DeleteLargeObject(wrapper->fStore, id);
			ThrowOSErr(err);
		}
	}
	newton_catch_all
	{
		OSErrIf(wrapper->Abort());
		rethrow;
	}
	end_try;
	OSErrIf(wrapper->UnlockStore());
	wrapper->fEphemeralTracker->AddEphemeral(id);
	RefVar obj(AllocateLargeBinary(theClass, wrapper->SymbolToReference(theClass), LongArg(RINT(length)), wrapper));
	LBData* lb = LargeBinaryData(obj);
	lb->fId = id;
	lb->fAddress = address;
	LBSetHostOrder(address, theClass);
	return obj;
}


// ROM 0x00100234 FLBAlloc
// store:NewVBO(class, length): NewCompressedVBO with the simple compander
// and no data.
Ref
FLBAlloc(RefArg rcvr, RefArg theClass, RefArg length)
{
	RefVar none;
	return FLBAllocCompressed(rcvr, theClass, length, none, none);
}


// ROM 0x001011fc FIsLargeBinary
// IsVBO(obj): whether the object is kept on a store.
static Ref
FIsLargeBinary(RefArg /*rcvr*/, RefArg obj)
{
	return MAKEBOOLEAN(IsLargeBinary(obj));
}


// ROM 0x00100c04 FGetBinaryStore
// GetVBOStore(obj): the store frame it lives on - the one of `gStores`
// whose wrapper holds the same TStore - or nil.
Ref
FGetBinaryStore(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return NILREF;
	TStoreWrapper* wrapper = LargeBinaryData(obj)->GetStore();
	if (wrapper == nil)
		return NILREF;
	TStore* store = wrapper->fStore;
	RefVar storeObject;
	long count = Length(gStores);
	for (long i = 0; i < count; i++)
	{
		storeObject = GetArraySlotRef(gStores, i);
		if (((TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore))->fStore == store)
			return storeObject;
	}
	return NILREF;
}


// ROM 0x00100c50 FGetBinaryCompander
// GetVBOCompander(obj): the name of the compander that packs it (at most
// 256 characters - more is out of memory).
Ref
FGetBinaryCompander(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return NILREF;
	LBData* lb = LargeBinaryData(obj);
	TStoreWrapper* wrapper = lb->GetStore();
	if (wrapper == nil)
		return NILREF;
	long length;
	OSErrIf(LOCompanderNameStrLen(wrapper->fStore, lb->fId, &length));
	if (length > 0x100)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	char name[0x100 + 1];
	OSErrIf(LOCompanderName(wrapper->fStore, lb->fId, name));
	return MakeString(name);
}


// ROM 0x00100d00 FGetBinaryCompanderData
// GetVBOCompanderData(obj): the data that compander was made with, as a
// binary of class 'none; nil when there is none.
Ref
FGetBinaryCompanderData(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return NILREF;
	LBData* lb = LargeBinaryData(obj);
	TStoreWrapper* wrapper = lb->GetStore();
	if (wrapper == nil)
		return NILREF;
	long size;
	OSErrIf(LOCompanderParameterSize(wrapper->fStore, lb->fId, &size));
	if (size == 0)
		return NILREF;
	RefVar data(AllocateBinary(RSSYMnone, size));
	OSErrIf(LOCompanderParameters(wrapper->fStore, lb->fId, BinaryData(data)));
	return data;
}


// ROM 0x00100e38 FGetBinaryStoredSize
// GetVBOStoredSize(obj): how much room it takes on the store, which is
// not the same as its size in memory because it is compressed there.
//
// ROM BUG (fixed): this one does not ask whether the object is a large
// binary at all.  It hands the binary's data pointer to
// StorageSizeOfLargeObject as a mapped address - right for a large binary,
// whose data pointer is exactly that - and for an ordinary binary no
// large object is mapped there, so the answer is 0.  The fix asks first,
// and answers 0 (what an ordinary binary got anyway) for anything that is
// not a large binary, without taking a pointer out of it.
static Ref
FGetBinaryStoredSize(RefArg /*rcvr*/, RefArg obj)
{
	if (RomBugFixed() && !IsLargeBinary(obj))
		return MAKEINT(0);
	return MAKEINT(StorageSizeOfLargeObject((ULong) BinaryData(obj)));
}


// ROM 0x0010039c FLBClearCache
// ClearVBOCache(obj): the paged-in copy written back and let go.
static Ref
FLBClearCache(RefArg /*rcvr*/, RefArg obj)
{
	if (IsLargeBinary(obj))
	{
		LBData* lb = LargeBinaryData(obj);
		TStoreWrapper* wrapper = lb->GetStore();
		if (wrapper == nil)
			Throw(exStoreError, (void*) (Long) kNSErrInvalidStore, nil);
		OSErrIf(FlushLargeObject(wrapper->fStore, lb->fId));
	}
	return NILREF;
}


// ROM 0x001002a4 FLBRollback
// VBOUndoChanges(obj): the changes made since it was paged in thrown
// away, and every Ref into it declawed.
//
// ROM BUG (fixed): like GetVBOStoredSize this does not check that the
// object is a large binary first - it reads the LBData out of any object
// it is given.  DEVIATION: on the host that would read past an ordinary
// binary's bytes, so anything that is not a large binary answers nil -
// which is also the fix, so both settings of NEWTON_ROM_BUGS do it.
static Ref
FLBRollback(RefArg /*rcvr*/, RefArg obj)
{
	if (!IsLargeBinary(obj))
		return NILREF;
	LBData* lb = LargeBinaryData(obj);
	TStoreWrapper* wrapper = lb->GetStore();
	if (wrapper == nil)
		Throw(exStoreError, (void*) (Long) kNSErrInvalidStore, nil);
	OSErrIf(AbortObject(wrapper->fStore, lb->fId));
	if (RegisterLargeBinaryForDeclawing(lb))
		DeclawRefsInRegisteredRanges();
	lb->fAddress = 0;
	return NILREF;
}


void
RegisterLargeBinaryNatives(void)
{
	RegisterNativeFunction("FLBAllocCompressed", (void*) FLBAllocCompressed, 4);
	RegisterNativeFunction("FLBAlloc", (void*) FLBAlloc, 2);
	RegisterNativeFunction("FIsLargeBinary", (void*) FIsLargeBinary, 1);
	RegisterNativeFunction("FGetBinaryStore", (void*) FGetBinaryStore, 1);
	RegisterNativeFunction("FGetBinaryCompander", (void*) FGetBinaryCompander, 1);
	RegisterNativeFunction("FGetBinaryCompanderData", (void*) FGetBinaryCompanderData, 1);
	RegisterNativeFunction("FGetBinaryStoredSize", (void*) FGetBinaryStoredSize, 1);
	RegisterNativeFunction("FLBClearCache", (void*) FLBClearCache, 1);
	RegisterNativeFunction("FLBRollback", (void*) FLBRollback, 1);
}
