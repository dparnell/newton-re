/*
	File:		ObjectTable.cpp

	Contains:	TObjectTable and TObjectTableIterator.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "ObjectTable.h"
#include "KernelGlobals.h"
#include "OSErrors.h"
#include "hal/Atomic.h"

#include <stdint.h>


/* -------------------------------------------------------------------------------
	The default scavenge proc: remove the object, run a destructor that does
	nothing.  Two unnamed routines in the ROM at 0x002f4770 (returns the
	address of) 0x002f476c (`mov pc,lr`).
------------------------------------------------------------------------------- */

static void
NoOpDestructor(TKernelObject* /*object*/)
{
}

static ObjectDestructorProcPtr
DefaultScavengeProc(TKernelObject* /*object*/, ULong /*unused*/)
{
	return NoOpDestructor;
}


/* -------------------------------------------------------------------------------
	TObjectTable
------------------------------------------------------------------------------- */

// ROM 0x002f4af0 Init__12TObjectTableFv
NewtonErr
TObjectTable::Init()
{
	fScavengeProc = DefaultScavengeProc;
	fIndex = 0;
	for (int i = 0; i < kObjectTableSize; i++)
		fEntry[i] = nil;
	return noErr;
}


// ROM 0x002f4b24 SetScavengeProc__12TObjectTableFPFP13TKernelObjectUl_PFP13TKernelObject_v
void
TObjectTable::SetScavengeProc(ScavengeProcPtr proc)
{
	fScavengeProc = proc;
}


// ROM 0x002f4bd4 NextGlobalUniqueId__12TObjectTableFv
ULong
TObjectTable::NextGlobalUniqueId()
{
	EnterAtomic();
	ULong id = gNextGlobalUniqueId + 1;
	if (id == 0x100)
		gWrappedGlobalUniqueId = true;
	gNextGlobalUniqueId = id;
	ExitAtomic();
	return id;
}


// ROM 0x002f4b2c NewId__12TObjectTableF11KernelTypes
// Once the unique counter has wrapped, keep drawing until the id is unused.
// Physical page ids must also be unique across the memory-architecture table.
TObjectId
TObjectTable::NewId(KernelTypes type)
{
	TObjectId id = (NextGlobalUniqueId() << kTypeBits) | type;
	if (gWrappedGlobalUniqueId)
	{
		TObjectTable* otherTable = (this == gObjectTable) ? gTheMemArchObjTbl : gObjectTable;
		while (Exists(id)
			|| ((type == kPhysType || type == kExtPhysType) && otherTable->Exists(id)))
		{
			id = (NextGlobalUniqueId() << kTypeBits) | type;
		}
	}
	return id;
}


// ROM 0x002f4c10 Get__12TObjectTableFUl
TKernelObject*
TObjectTable::Get(TObjectId id)
{
	for (TKernelObject* object = fEntry[ObjectTableIndex(id)]; object != nil; object = object->fNext)
	{
		if (object->fId == id)
			return (id == 0) ? nil : object;
	}
	return nil;
}


// ROM 0x002f4c5c Exists__12TObjectTableFUl
Boolean
TObjectTable::Exists(TObjectId id)
{
	for (TKernelObject* object = fEntry[ObjectTableIndex(id)]; object != nil; object = object->fNext)
	{
		if (object->fId == id)
			return true;
	}
	return false;
}


// ROM 0x002f4c98 Add__12TObjectTableFP13TKernelObject11KernelTypesUl
// Gives the object its id and links it in at the head of its bucket.
// An owner of 1 means the object owns itself.
TObjectId
TObjectTable::Add(TKernelObject* object, KernelTypes type, TObjectId owner)
{
	TObjectId id = NewId(type);
	object->fId = id;
	if (owner == 1)
		owner = id;
	object->fOwnerId = owner;
	object->fAssignedOwnerId = owner;

	ULong index = ObjectTableIndex(id);
	EnterFIQAtomic();
	object->fNext = fEntry[index];
	fEntry[index] = object;
	ExitFIQAtomic();
	return object->fId;
}


// Call the destructor a scavenge proc returned.  The ROM clears the low two
// bits of the pointer first (`bic #3`), as TDoubleQContainer does.
static inline void
CallDestructor(ObjectDestructorProcPtr destructor, TKernelObject* object)
{
	ObjectDestructorProcPtr proc = (ObjectDestructorProcPtr) ((uintptr_t) destructor & ~(uintptr_t) 3);
	proc(object);
}


// ROM 0x002f4cf4 Remove__12TObjectTableFUl
// Unlinks the object if the scavenge proc agrees, keeping the scavenge cursor
// consistent, and runs the destructor.  Ids of kNoType are never in the table.
NewtonErr
TObjectTable::Remove(TObjectId id)
{
	if (ObjectType(id) == kNoType)
		return kError_Item_Not_Found;

	ULong index = ObjectTableIndex(id);
	TKernelObject* prev = nil;
	for (TKernelObject* object = fEntry[index]; object != nil; prev = object, object = object->fNext)
	{
		if (object->fId != id)
			continue;
		object->fOwnerId = object->fId;
		ObjectDestructorProcPtr destructor = fScavengeProc(object, 0);
		if (destructor != nil)
		{
			if (prev == nil)
				fEntry[index] = object->fNext;
			else
				prev->fNext = object->fNext;
			if (fThis == object)
				fThis = object->fNext;
			else if (fPrev == object)
				fPrev = prev;
			CallDestructor(destructor, object);
		}
		return noErr;
	}
	return kError_Item_Not_Found;
}


// ROM 0x002f477c Scavenge__12TObjectTableFv
// Walks the next bucket removing objects whose owner has disappeared.
void
TObjectTable::Scavenge()
{
	if (fScavengeProc == nil)
		return;

	fThis = fEntry[fIndex];
	fPrev = nil;
	while (fThis != nil)
	{
		TObjectId owner = fThis->fOwnerId;
		if (owner == fThis->fId || Exists(owner))
		{
			// alive: step over it
			fPrev = fThis;
			fThis = fThis->fNext;
		}
		else
		{
			TKernelObject* object = fThis;
			fThis = object->fNext;
			object->fOwnerId = object->fId;
			ObjectDestructorProcPtr destructor = fScavengeProc(object, 0);
			if (destructor != nil)
			{
				if (fPrev == nil)
					fEntry[fIndex] = object->fNext;
				else
					fPrev->fNext = object->fNext;
				CallDestructor(destructor, object);
			}
		}
	}
	fPrev = nil;
	fIndex = (fIndex + 1) & kObjectTableIndexMask;
}


// ROM 0x002f4864 ScavengeAll__12TObjectTableFv
void
TObjectTable::ScavengeAll()
{
	for (int i = 0; i < kObjectTableSize; i++)
		Scavenge();
}


// ROM 0x002f4890 ReassignOwnership__12TObjectTableFUlT1
void
TObjectTable::ReassignOwnership(TObjectId fromOwner, TObjectId toOwner)
{
	if (!Exists(toOwner))
		return;
	for (int i = 0; i < kObjectTableSize; i++)
	{
		for (TKernelObject* object = fEntry[i]; object != nil; object = object->fNext)
		{
			if (object->fOwnerId == fromOwner)
				object->fOwnerId = toOwner;
		}
	}
}


/* -------------------------------------------------------------------------------
	TObjectTableIterator
------------------------------------------------------------------------------- */

// ROM 0x002f48f4 __ct__20TObjectTableIteratorFP12TObjectTableUl
TObjectTableIterator::TObjectTableIterator(TObjectTable* table, TObjectId startId)
{
	fStartIndex = ObjectTableIndex(startId);
	fStartId = startId;
	fEntry = nil;
	fTable = table;
	SetCurrentPosition(startId);
}


// ROM 0x002f4954 GetThisLineNextEntry__20TObjectTableIteratorFv
// Advances to the next object of the current bucket; at the end of a bucket
// moves on to the next one.  Returns false once the walk has come round.
Boolean
TObjectTableIterator::GetThisLineNextEntry()
{
	fEntry = (fEntry != nil) ? fEntry->fNext : fTable->fEntry[fIndex];
	if (fEntry != nil)
	{
		fCurrentId = fEntry->fId;
		return true;
	}
	fCurrentId = 0;
	if (fWrapped)
		return false;
	fIndex = (fIndex + 1) & kObjectTableIndexMask;
	if (fStartIndex == fIndex)
		fWrapped = true;
	return true;
}


// ROM 0x002f49cc SetCurrentPosition__20TObjectTableIteratorFUl
Boolean
TObjectTableIterator::SetCurrentPosition(TObjectId id)
{
	fEntry = nil;
	fIndex = ObjectTableIndex(id);
	fWrapped = (fStartIndex == fIndex && fStartId < id);
	do
	{
		if (!GetThisLineNextEntry())
			return false;
	} while (fCurrentId != 0 && id < fCurrentId);
	return true;
}


// ROM 0x002f4a48 GetNextTableId__20TObjectTableIteratorFv
TObjectId
TObjectTableIterator::GetNextTableId()
{
	do
	{
		if (!GetThisLineNextEntry())
			return fCurrentId;
	} while (fCurrentId == 0);
	if (fWrapped && fCurrentId <= fStartId)
		fCurrentId = 0;
	return fCurrentId;
}


// ROM 0x002f4a9c GetNextTypedId__20TObjectTableIteratorF11KernelTypes
TObjectId
TObjectTableIterator::GetNextTypedId(KernelTypes type)
{
	TObjectId id;
	do
	{
		id = GetNextTableId();
	} while (id != 0 && ObjectType(id) != type);
	return id;
}


// ROM 0x002f4acc GetNextTypedObject__20TObjectTableIteratorF11KernelTypes
TKernelObject*
TObjectTableIterator::GetNextTypedObject(KernelTypes type)
{
	TObjectId id = GetNextTypedId(type);
	for (TKernelObject* object = fTable->fEntry[ObjectTableIndex(id)]; object != nil; object = object->fNext)
	{
		if (object->fId == id)
			return (id == 0) ? nil : object;
	}
	return nil;
}
