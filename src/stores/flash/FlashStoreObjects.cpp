/*
	File:		stores/flash/FlashStoreObjects.cpp

	Contains:	TFlashStore's objects (FlashStore.h): finding them, making,
				changing and deleting them within a transaction, committing
				and aborting it, and the separate transactions.

				Flash cannot be written over, so an object changed in a
				transaction is not changed in place: a copy is made with the
				change (a *superceder*), the original is marked *superceded*,
				and the commit deletes the original and marks the copy
				committed - the abort the other way about.  An object made in
				the transaction is *new* and can be written where it lies for
				as long as the bytes being written are still blank.  The
				tracker notes what a transaction touched, so a commit need not
				walk every object.

				Each entry point runs with an exception handler: an abort
				(a card pulled out) forgets every object reference in use and
				passes the exception on.

	Reconstructed from the MP2x00 US ROM (0x000c5204-0x000c6bf4,
	0x000c747c-0x000c7650, 0x000c8750-0x000c88dc, 0x000c94f0-0x000c9a50,
	0x000c9ca8-0x000cadbc); each function cites its origin.
*/

#include "FlashStore.h"

#include <string.h>


// the abort handler every entry point has
#define FLASH_STORE_ABORTED(store)	\
		newton_catch(exAbort)		\
		{							\
			(store)->VppOff();		\
			(store)->fLastRef = nil;	\
			(store)->fRefs = nil;	\
			rethrow;				\
		}

#define FLASH_STORE_ABORTED_READING(store)	\
		newton_catch(exAbort)		\
		{							\
			(store)->fLastRef = nil;	\
			(store)->fRefs = nil;	\
			(store)->VccOff();		\
			rethrow;				\
		}


/*------------------------------------------------------------------------------
	F i n d i n g   o b j e c t s
------------------------------------------------------------------------------*/

// ROM 0x000c747c Lookup__11TFlashStoreFUliR7TObjRef
// Where the cache says; else in the block the id was made in (or the one
// its directory says it migrated to); else in every other block in turn.
NewtonErr
TFlashStore::Lookup(PSSId id, int state, TObjRef& ref)
{
	ULong dirEntOffset = fCache->Lookup(id, state);
	if (dirEntOffset != 0xFFFFFFFF)
	{
		ref.Set(0xFFFFFFFF, dirEntOffset);
		return noErr;
	}
	TFlashBlock* home = fBlocks[id >> fObjectNumberShift];
	if (home->IsVirgin())
		return kSError_ObjectNotFound;
	long migratedTo;
	NewtonErr err = home->Lookup(id, state, ref, &migratedTo);
	if (err == kSError_ObjectNotFound && migratedTo >= 0
	 && (state < 1 || state == kRAMObjCommitted || state == kFlashObjCommitted))
	{
		TFlashBlock* there = fBlocks[migratedTo];
		if (!there->IsVirgin())
			err = there->Lookup(id, state, ref, nil);
	}
	if (err == noErr)
	{
		fCache->Add(ref);
		return noErr;
	}
	if (err != kSError_ObjectNotFound)
		return err;
	ULong start = home->fLogicalOffset;
	ULong offset = fBlockSize + start;
	for (;;)
	{
		if (StoreCapacity() <= offset || BlockAt(offset)->IsVirgin())
			offset = 0;
		if ((~fBlockMask & offset) == (~fBlockMask & start))
			return kSError_ObjectNotFound;
		err = BlockAt(offset)->Lookup(id, state, ref, nil);
		if (err == noErr)
			break;
		if (err != kSError_ObjectNotFound)
			return err;
		offset += fBlockSize;
	}
	fCache->Add(ref);
	return noErr;
}


// ROM 0x000cad4c SetupForRead__11TFlashStoreFUlP7TObjRef
// A transaction left unfinished is finished first.  (The ROM has Lookup
// inline, for state 0.)
NewtonErr
TFlashStore::SetupForRead(PSSId id, TObjRef* ref)
{
	NewtonErr err;
	if (fNeedsRecovery && (err = RecoveryCheck(true)) != noErr)
		return err;
	if (id == 0xFFFFFFFF)
		return noErr;
	err = ValidateIncomingPSSID(id);
	if (err != noErr)
		return err;
	return Lookup(id, kObjNoState, *ref);
}


// ROM 0x000ca660 SetupForModify__11TFlashStoreFUlP7TObjRefUcT3
// The checks every change makes: not read-only, locked if it must be, an
// unfinished transaction finished, a transaction started if asked; then
// the object found.
NewtonErr
TFlashStore::SetupForModify(PSSId id, TObjRef* ref, UChar mustBeLocked, UChar startTransaction)
{
	NewtonErr err = noErr;
	if (IsROM())
		return kSError_WriteProtected;
	if (IsWriteProtected())
		return fNeedsRecovery ? kSError_WPButNeedsRepair : kSError_WriteProtected;
	if (mustBeLocked && fLockCount == 0)
		return kSError_NotInTransaction;
	// (NOT YET: a RAM store still on its own driver moves to the one in its
	// compaction state here)
	if ((!fNeedsRecovery || (err = RecoveryCheck(true)) == noErr)
	 && (!startTransaction || fInTransaction || (err = StartTransaction()) == noErr)
	 && id != 0xFFFFFFFF
	 && (err = ValidateIncomingPSSID(id)) == noErr
	 && id != 0xFFFFFFFF)
		err = Lookup(id, kObjNoState, *ref);
	return err;
}


/*------------------------------------------------------------------------------
	M a k i n g   o b j e c t s
------------------------------------------------------------------------------*/

// ROM 0x000c5454 AddObject__11TFlashStoreFUliT1R7TObjRefUcT5
// Into the working block, choosing another (and compacting) when it is
// full; an id of -1 is the block's next.
NewtonErr
TFlashStore::AddObject(PSSId id, int state, ULong size, TObjRef& ref, UChar separate, UChar xip)
{
	Boolean anyId = (id == 0xFFFFFFFF);
	ULong need = xip ? size + 0x1000 : size;
	NewtonErr err;
	do
	{
		if (fWorkingBlock == nil)
		{
			err = ChooseWorkingBlock(need, 0xFFFFFFFF);
			if (err == kSError_StoreFull)
			{
				if (!fIsSRAM)
					return kSError_StoreFull;
				GC();
				err = ChooseWorkingBlock(need, 0xFFFFFFFF);
			}
			if (err != noErr)
				return err;
		}
		if (anyId)
			id = fWorkingBlock->NextPSSID();
		err = fWorkingBlock->AddObject(id, state, size, ref, separate, xip);
		if (err != kSError_BlockFull)
		{
			if (err != noErr)
				return err;
			fCache->Add(ref);
			if (anyId)
				fWorkingBlock->UseNextPSSID();
			return noErr;
		}
		err = ChooseWorkingBlock(need, 0xFFFFFFFF);
		if (err == kSError_StoreFull)
		{
			if (!fIsSRAM)
				return kSError_StoreFull;
			GC();
			err = ChooseWorkingBlock(need, 0xFFFFFFFF);
		}
	} while (err == noErr);
	return err;
}


// The two NewObjects' common body: data nil for an empty object.
static NewtonErr
MakeObject(TFlashStore* store, char* data, long size, PSSId* id)
{
	NewtonErr err = noErr;
	store->fIgnoreSlop = (size & 0x80000000) != 0;
	ULong objectSize = size & 0x7FFFFFFF;
	if (store->fBlockSize - 0x400 < objectSize)
		return kSError_ObjectTooBig;
	store->VppOn();
	newton_try
	{
		TObjRef ref;
		ref.Init(store);
		store->LockStore();
		err = store->SetupForModify(0xFFFFFFFF, nil, true, true);
		if (id != nil && (*id = 0, err == noErr))
		{
			if (!store->fIgnoreSlop && store->Avail() < store->InternalStoreSlop())
			{
				store->GC();
				if (store->Avail() < store->InternalStoreSlop())
				{
					err = kSError_StoreFull;
					goto unlock;
				}
			}
			store->fTracker->fNesting++;
			err = store->AddObject(0xFFFFFFFF, store->State(kRAMObjNew), objectSize, ref, false, false);
			if (err == noErr && (data == nil || (err = ref.Write(data, 0, objectSize)) == noErr))
			{
				*id = ref.Id();
				store->fTracker->Add(ref.Id());
			}
			if (store->fTracker->fNesting != 0)
				store->fTracker->fNesting--;
		}
	unlock:
		store->UnlockStore();
		store->Remove(&ref);
	}
	FLASH_STORE_ABORTED(store)
	end_try;
	store->VppOff();
	return err;
}


// ROM 0x000c543c NewObject__11TFlashStoreFlPUl
// A new object in the transaction.  Bit 31 of the size says the slop kept
// back may be used.
NewtonErr
TFlashStore::NewObject(long size, PSSId* id)
{
	return MakeObject(this, nil, size, id);
}


// ROM 0x000c5204 NewObject__11TFlashStoreFPclPUl
NewtonErr
TFlashStore::NewObject(char* data, long size, PSSId* id)
{
	return MakeObject(this, data, size, id);
}


// ROM 0x000ca7d0 NewWithinTransaction__11TFlashStoreFlUcPUl
// A new object in a separate transaction of its own (a large object's
// block, say), with no lock needed.
NewtonErr
TFlashStore::NewWithinTransaction(long size, UChar xip, PSSId* id)
{
	if (fBlockSize - 0x400 < (ULong) size)
		return kSError_ObjectTooBig;
	NewtonErr err = noErr;
	VppOn();
	newton_try
	{
		TObjRef ref;
		ref.Init(this);
		err = SetupForModify(0xFFFFFFFF, nil, false, false);
		if (id != nil && err == noErr)
		{
			if (Avail() < InternalStoreSlop())
			{
				GC();
				if (Avail() < InternalStoreSlop())
				{
					err = kSError_StoreFull;
					goto done;
				}
			}
			err = AddObject(0xFFFFFFFF, State(kRAMObjNew), size, ref, true, xip);
			*id = (err == noErr) ? ref.Id() : 0;
		}
	done:
		Remove(&ref);
	}
	FLASH_STORE_ABORTED(this)
	end_try;
	VppOff();
	return err;
}


// ROM 0x000c9ca8 NewWithinTransaction__11TFlashStoreFlPUl
NewtonErr
TFlashStore::NewWithinTransaction(long size, PSSId* id)
{
	return NewWithinTransaction(size, false, id);
}


// ROM 0x000caad4 NewXIPObject__11TFlashStoreFlPUl
// An execute-in-place object: on a card only.
NewtonErr
TFlashStore::NewXIPObject(long size, PSSId* id)
{
	if (fIsInternalRAM || fUsesTFlash)
		return kError_XIP_Not_Possible;
	return NewWithinTransaction(size, true, id);
}


// ROM 0x000ca988 CalcXIPObjectSize__11TFlashStoreFlT1Pl
// How much of a package of `size` bytes can go in one execute-in-place
// piece: the free space of the blocks, in 4 KB steps, counted, and the
// largest piece enough blocks could take.
NewtonErr
TFlashStore::CalcXIPObjectSize(long size, long extra, long* pieceSize)
{
	if (fIsInternalRAM || fUsesTFlash)
		return kError_XIP_Not_Possible;
	long counts[16];
	for (int i = 1; i < 16; i++)
		counts[i] = 0;
	for (ULong offset = 0; offset < StoreCapacity(); offset += fBlockSize)
	{
		TFlashBlock* block = BlockAt(offset);
		long avail = ((long) block->Yield() - 0x1000) - extra;
		if (block->IsVirgin())
			avail -= block->RootDirSize() + 4;
		if (avail > 0x4000)
		{
			long pages = avail >> 12;
			if (pages > 15)
				pages = 15;
			counts[pages]++;
		}
	}
	for (int k = 14; k >= 0; k--)
	{
		long piece = k * 0x1000 + 0x1000;
		if (size <= piece * counts[k + 1])
		{
			if (piece < size)
				*pieceSize = piece;
			else if (size < 0x1001)
				*pieceSize = 0x1000;
			else
				*pieceSize = size & 0xFFFFF000;
			return noErr;
		}
	}
	return kSError_StoreFull;
}


// ROM 0x000cab40 GetXIPObjectInfo__11TFlashStoreFUlPUlN22
// Where an execute-in-place object's data is on the card.
// NOT YET: the card's power-off notification and its timeout.
NewtonErr
TFlashStore::GetXIPObjectInfo(PSSId id, ULong* base, ULong* size, ULong* offset)
{
	NewtonErr err = noErr;
	VccOn();
	newton_try
	{
		TObjRef ref;
		ref.Init(this);
		err = SetupForRead(id, &ref);
		if (err == noErr)
		{
			*base = (ULong) (uintptr_t) fBase;
			*size = StoreCapacity();
			*offset = Translate(ref.fOffset) + 8;
		}
		Remove(&ref);
	}
	FLASH_STORE_ABORTED_READING(this)
	end_try;
	VccOff();
	return err;
}


/*------------------------------------------------------------------------------
	R e a d i n g   a n d   c h a n g i n g   o b j e c t s
------------------------------------------------------------------------------*/

// ROM 0x000c5df4 GetObjectSize__11TFlashStoreFUlPl
NewtonErr
TFlashStore::GetObjectSize(PSSId id, long* size)
{
	NewtonErr err = noErr;
	VccOn();
	newton_try
	{
		TObjRef ref;
		ref.Init(this);
		*size = 0;
		err = SetupForRead(id, &ref);
		if (err == noErr)
			*size = ref.Size();
		Remove(&ref);
	}
	FLASH_STORE_ABORTED_READING(this)
	end_try;
	VccOff();
	return err;
}


// ROM 0x000c7200 Read__11TFlashStoreFUllPcT2
// A read past the end reads what there is and answers kSError_ObjectOverRun.
NewtonErr
TFlashStore::Read(PSSId id, long offset, char* buffer, long count)
{
	NewtonErr err = noErr;
	VccOn();
	newton_try
	{
		TObjRef ref;
		ref.Init(this);
		err = SetupForRead(id, &ref);
		if (err == noErr)
		{
			ULong size = ref.Size();
			if (offset < 0 || size < (ULong) offset)
				err = kSError_ObjectOverRun;
			else if (size < (ULong) (offset + count))
			{
				err = ref.Read(buffer, offset, size - offset);
				if (err == noErr)
					err = kSError_ObjectOverRun;
			}
			else
				err = ref.Read(buffer, offset, count);
		}
		Remove(&ref);
	}
	FLASH_STORE_ABORTED_READING(this)
	end_try;
	VccOff();
	return err;
}


// A copy of `from` in `state`, `size` long, with its bytes up to `offset`,
// the new ones, and its bytes after them; made again elsewhere if a write
// did not take.  (The ROM has it inline, in Write and SetObjectSize.)
static NewtonErr
CopyWithChange(TObjRef& from, int state, TObjRef& to, Boolean separate, long offset, char* buffer, long count)
{
	NewtonErr err;
	ULong end = offset + count;
	for (;;)
	{
		err = from.CloneEmpty(state, to, separate);
		if (err != noErr)
			break;
		if (offset < 1 || (err = from.CopyTo(to, 0, offset)) == noErr)
			err = to.Write(buffer, offset, count);
		if (err == noErr)
		{
			if (from.Size() <= end)
				break;
			err = from.CopyTo(to, end, from.Size() - end);
		}
		if (err != kSError_WriteError)
			break;
		to.Delete();
	}
	return err;
}


static inline void
EndNesting(TFlashStore* store, Boolean counted)
{
	if (counted && store->fTracker->fNesting != 0)
		store->fTracker->fNesting--;
}


// ROM 0x000c5eec Write__11TFlashStoreFUllPcT2
// A new object is written where it lies while the bytes are blank; anything
// else is copied with the change.
NewtonErr
TFlashStore::Write(PSSId id, long offset, char* buffer, long count)
{
	NewtonErr err = noErr;
	VppOn();
	newton_try
	{
		TObjRef ref, clone, other;
		ref.Init(this);
		clone.Init(this);
		other.Init(this);
		LockStore();
		err = SetupForModify(id, &ref, true, true);
		ULong end = offset + count;
		if (err == noErr)
		{
			ULong size = ref.Size();
			if (size < (ULong) offset || size < (ULong) count || size < end)
				err = kSError_ObjectOverRun;
		}
		Boolean notSeparate = (ref.fWord1 & 6) != 4;
		if (err == noErr)
		{
			switch (ref.State())
			{
			case kRAMObjNew:
			case kRAMObjSuperceder:
			writeInPlace:
				err = ref.Write(buffer, offset, count);
				break;

			case kRAMObjCommitted:
				if (notSeparate)
				{
					fTracker->Add(id);
					fTracker->fNesting++;
				}
				err = ref.CloneEmpty(State(kRAMObjCloneEmpty), clone, (ref.fWord1 & 6) == 4);
				if (err == noErr
				 && (offset < 1 || (err = ref.CopyTo(clone, 0, offset)) == noErr)
				 && (err = clone.Write(buffer, offset, count)) == noErr
				 && (ref.Size() <= end || (err = ref.CopyTo(clone, end, ref.Size() - end)) == noErr)
				 && (err = ref.SetState(State(kRAMObjSuperceded))) == noErr
				 && (err = clone.SetState(State(kRAMObjSuperceder))) == noErr)
				{
					EndNesting(this, notSeparate);
					fCache->Change(clone);
				}
				break;

			case kRAMObjSuperceded:
				err = ref.FindSuperceeder(clone);
				if (err == noErr)
					err = clone.Write(buffer, offset, count);
				break;

			case kFlashObjNew:
				if (IsRangeVirgin(ref.fOffset + offset + 8, count))
					goto writeInPlace;
				if (notSeparate)
					fTracker->fNesting++;
				err = CopyWithChange(ref, State(kRAMObjCloneOfNew), clone, ref.SeparateBits() == 2, offset, buffer, count);
				if (err == noErr && (err = ref.Delete()) == noErr && (err = clone.SetState(State(kRAMObjNew))) == noErr)
				{
					EndNesting(this, notSeparate);
					fCache->Change(clone);
				}
				break;

			case kFlashObjCommitted:
				if (notSeparate)
				{
					fTracker->Add(id);
					fTracker->fNesting++;
				}
				err = CopyWithChange(ref, State(kRAMObjCloneEmpty), clone, ref.SeparateBits() == 2, offset, buffer, count);
				if (err == noErr
				 && (err = ref.SetState(State(kRAMObjSuperceded))) == noErr
				 && (err = clone.SetState(State(kRAMObjSuperceder))) == noErr)
				{
					EndNesting(this, notSeparate);
					fCache->Change(clone);
				}
				break;

			case kFlashObjSuperceded:
				err = ref.FindSuperceeder(clone);
				if (err == noErr)
				{
					if (notSeparate)
						fTracker->fNesting++;
					err = CopyWithChange(clone, State(kRAMObjCloneEmpty), other, ref.SeparateBits() == 2, offset, buffer, count);
					if (err == noErr && (err = clone.Delete()) == noErr && (err = other.SetState(State(kRAMObjSuperceder))) == noErr)
					{
						EndNesting(this, notSeparate);
						fCache->Change(other);
					}
				}
				break;

			case kFlashObjSuperceder:
				if (IsRangeVirgin(ref.fOffset + offset + 8, count))
					goto writeInPlace;
				if (notSeparate)
					fTracker->fNesting++;
				err = CopyWithChange(ref, State(kRAMObjCloneEmpty), clone, ref.SeparateBits() == 2, offset, buffer, count);
				if (err == noErr && (err = ref.Delete()) == noErr && (err = clone.SetState(State(kRAMObjSuperceder))) == noErr)
				{
					EndNesting(this, notSeparate);
					fCache->Change(clone);
				}
				break;
			}
		}
		UnlockStore();
		Remove(&other);
		Remove(&clone);
		Remove(&ref);
	}
	FLASH_STORE_ABORTED(this)
	end_try;
	VppOff();
	return err;
}


// A copy `size` long of as much of `from` as fits; made again if a write
// did not take.  (Inline in SetObjectSize.)
static NewtonErr
CopyResized(TObjRef& from, int state, long size, TObjRef& to, Boolean separate)
{
	NewtonErr err;
	for (;;)
	{
		err = from.CloneEmpty(state, size, to, separate);
		if (err == noErr)
		{
			ULong n = size;
			if (from.Size() < (ULong) size)
				n = from.Size();
			err = from.CopyTo(to, 0, n);
		}
		if (err != kSError_WriteError)
			break;
		to.Delete();
	}
	return err;
}


// ROM 0x000c5898 SetObjectSize__11TFlashStoreFUll
// Always a copy of the new size.  Bit 31 of the id says the slop kept back
// may be used to grow it.
NewtonErr
TFlashStore::SetObjectSize(PSSId id, long size)
{
	NewtonErr err = noErr;
	fIgnoreSlop = (id & 0x80000000) != 0;
	VppOn();
	newton_try
	{
		TObjRef ref, clone;
		ref.Init(this);
		clone.Init(this);
		LockStore();
		err = SetupForModify(id & 0x7FFFFFFF, &ref, true, true);
		if (!fIgnoreSlop && ref.Size() < (ULong) size && Avail() < InternalStoreSlop())
			err = kSError_StoreFull;
		Boolean notSeparate = ref.SeparateBits() != 2;
		if (err == noErr && ref.Size() != (ULong) size)
		{
			switch (ref.State())
			{
			case kRAMObjNew:
			case kFlashObjNew:
				if (notSeparate)
					fTracker->fNesting++;
				err = CopyResized(ref, State(kRAMObjCloneOfNew), size, clone, ref.SeparateBits() == 2);
				if (err == noErr && (err = ref.Delete()) == noErr && (err = clone.SetState(State(kRAMObjNew))) == noErr)
				{
					EndNesting(this, notSeparate);
					fCache->Change(clone);
				}
				break;

			case kRAMObjCommitted:
			case kFlashObjCommitted:
				if (notSeparate)
				{
					fTracker->fNesting++;
					fTracker->Add(id & 0x7FFFFFFF);
				}
				err = CopyResized(ref, State(kRAMObjCloneEmpty), size, clone, ref.SeparateBits() == 2);
				if (err == noErr
				 && (err = ref.SetState(State(kRAMObjSuperceded))) == noErr
				 && (err = clone.SetState(State(kRAMObjSuperceder))) == noErr)
				{
					EndNesting(this, notSeparate);
					fCache->Change(clone);
				}
				break;

			case kRAMObjSuperceder:
			case kFlashObjSuperceder:
				if (notSeparate)
					fTracker->fNesting++;
				err = CopyResized(ref, State(kRAMObjCloneEmpty), size, clone, ref.SeparateBits() == 2);
				if (err == noErr && (err = ref.Delete()) == noErr && (err = clone.SetState(State(kRAMObjSuperceder))) == noErr)
				{
					EndNesting(this, notSeparate);
					fCache->Change(clone);
				}
				break;
			}
		}
		UnlockStore();
		Remove(&clone);
		Remove(&ref);
	}
	FLASH_STORE_ABORTED(this)
	end_try;
	VppOff();
	return err;
}


// ROM 0x000c6874 ReplaceObject__11TFlashStoreFR7TObjRefT1PcliN25
// A copy made with the new contents; the original deleted (oldState 0)
// or marked, and the copy put in its final state.
NewtonErr
TFlashStore::ReplaceObject(TObjRef& old, TObjRef& replacement, char* data, long size, int oldState, int newState, int finalState)
{
	NewtonErr err;
	for (;;)
	{
		err = AddObject(old.Id(), newState, size, replacement, (old.fWord1 & 6) == 4, false);
		if (err == noErr)
			err = replacement.Write(data, 0, size);
		if (err != kSError_WriteError)
			break;
		replacement.Delete();
	}
	if (err == noErr)
	{
		err = (oldState == 0) ? old.Delete() : old.SetState(oldState);
		if (err == noErr && (err = replacement.SetState(finalState)) == noErr)
			fCache->Change(replacement);
	}
	return err;
}


// ROM 0x000c696c ReplaceObject__11TFlashStoreFUlPcl
NewtonErr
TFlashStore::ReplaceObject(PSSId id, char* data, long size)
{
	NewtonErr err = noErr;
	VppOn();
	newton_try
	{
		TObjRef ref, replacement;
		ref.Init(this);
		replacement.Init(this);
		LockStore();
		err = SetupForModify(id, &ref, true, true);
		if (err == noErr)
		{
			Boolean notSeparate = (ref.fWord1 & 6) != 4;
			if (notSeparate)
				fTracker->fNesting++;
			switch (ref.State())
			{
			case kRAMObjNew:
			case kFlashObjNew:
				err = ReplaceObject(ref, replacement, data, size, 0, State(kRAMObjCloneOfNew), State(kRAMObjNew));
				break;
			case kRAMObjCommitted:
			case kFlashObjCommitted:
				if (notSeparate)
					fTracker->Add(id);
				err = ReplaceObject(ref, replacement, data, size, State(kRAMObjSuperceded), State(kRAMObjCloneEmpty), State(kRAMObjSuperceder));
				break;
			case kRAMObjSuperceder:
			case kFlashObjSuperceder:
				err = ReplaceObject(ref, replacement, data, size, 0, State(kRAMObjCloneEmpty), State(kRAMObjSuperceder));
				break;
			}
			if (err == noErr)
				EndNesting(this, notSeparate);
		}
		UnlockStore();
		Remove(&replacement);
		Remove(&ref);
	}
	FLASH_STORE_ABORTED(this)
	end_try;
	VppOff();
	return err;
}


// ROM 0x000c55b4 DeleteObject__11TFlashStoreFUl
// A new object goes at once; a committed one is marked deleted for the
// commit; a copy goes and its original is marked deleted.
NewtonErr
TFlashStore::DeleteObject(PSSId id)
{
	if (id == 0xFFFFFFFF)
		return noErr;
	NewtonErr err = noErr;
	VppOn();
	newton_try
	{
		TObjRef ref, other;
		ref.Init(this);
		other.Init(this);
		LockStore();
		err = SetupForModify(id, &ref, true, true);
		if (err == noErr)
		{
			switch (ref.State())
			{
			case kRAMObjNew:
			case kFlashObjNew:
				if (ref.SeparateBits() == 2)
					err = ref.SetState(State(kRAMObjDeleted));
				else
				{
					fTracker->fNesting++;
					err = ref.Delete();
					if (err == noErr)
					{
						fTracker->Remove(id);
						EndNesting(this, true);
					}
				}
				break;
			case kRAMObjCommitted:
			case kFlashObjCommitted:
				if (ref.SeparateBits() != 2)
				{
					fTracker->fNesting++;
					fTracker->Add(id);
				}
				err = ref.SetState(State(kRAMObjDeleted));
				if (err == noErr)
					EndNesting(this, ref.SeparateBits() != 2);
				break;
			case kRAMObjSuperceder:
			case kFlashObjSuperceder:
				err = ref.FindSuperceeded(other);
				if (err == noErr && (err = ref.Delete()) == noErr)
					err = other.SetState(State(kRAMObjDeleted));
				break;
			}
		}
		fCache->Forget(id, kObjNoState);
		UnlockStore();
		Remove(&other);
		Remove(&ref);
	}
	FLASH_STORE_ABORTED(this)
	end_try;
	VppOff();
	return err;
}


/*------------------------------------------------------------------------------
	T h e   t r a n s a c t i o n
------------------------------------------------------------------------------*/

// ROM 0x000c873c LockStore__11TFlashStoreFv
NewtonErr
TFlashStore::LockStore(void)
{
	fLockCount++;
	return noErr;
}


// ROM 0x000c8750 UnlockStore__11TFlashStoreFv
// The last unlock commits a transaction under way: its commit point
// marked, then everything it did made permanent.
NewtonErr
TFlashStore::UnlockStore(void)
{
	long count = fLockCount;
	if (count == 1)
	{
		if (fInTransaction)
		{
			VppOn();
			newton_try
			{
				MarkCommitPoint();
				DoCommit(true);
				fLockCount = 0;
			}
			FLASH_STORE_ABORTED(this)
			end_try;
			VppOff();
			return noErr;
		}
	}
	else if (count == 0)
		return noErr;
	fLockCount = count - 1;
	return noErr;
}


// ROM 0x000c8828 Abort__11TFlashStoreFv
NewtonErr
TFlashStore::Abort(void)
{
	NewtonErr err = noErr;
	if (fInTransaction)
	{
		VppOn();
		newton_try
		{
			err = DoAbort(false);
		}
		FLASH_STORE_ABORTED(this)
		end_try;
		VppOff();
	}
	fLockCount = 0;
	return err;
}


// ROM 0x000c94f0 DoCommit__11TFlashStoreFUc
// Every object the transaction touched (the tracker's, when it has them
// all and nothing is half done; otherwise every object of the store) put
// in its committed state: new ones committed, superceded ones deleted in
// favour of their copies, deleted ones deleted.  Separate transactions are
// left alone.
NewtonErr
TFlashStore::DoCommit(UChar useTracker)
{
	TObjRef ref, other;
	ref.Init(this);
	other.Init(this);
	TFlashIterator iter(this, &ref, kIterAllObjects);
	Boolean tracked = false;
	if (useTracker && !fTracker->fOverflowed && fTracker->fNesting == 0)
	{
		iter.Start(fTracker);
		tracked = true;
	}
	while (!iter.Done())
	{
		iter.Next();
		PSSId id = ref.Id();
		if (ref.SeparateBits() == 2)
			continue;
		switch (ref.State())
		{
		case kRAMObjNew:
		case kFlashObjNew:
			ref.SetState(State(kRAMObjCommitted));
			fCache->Forget(ref.Id(), kObjNoState);
			break;
		case kRAMObjSuperceded:
		case kFlashObjSuperceded:
		{
			if (ref.FindSuperceeder(other) == noErr)
				other.SetState(State(kRAMObjCommitted));
			ref.Delete();
			fCache->Forget(id, kObjNoState);
			ULong block = other.fOffset >> fBlockShift;
			if (block != ref.fOffset >> fBlockShift)
				fBlocks[id >> fObjectNumberShift]->ObjectMigrated(id, block);
			break;
		}
		case kRAMObjSuperceder:
		case kFlashObjSuperceder:
			if (ref.FindSuperceeded(other) == noErr)
				other.Delete();
			ref.SetState(State(kRAMObjCommitted));
			fCache->Forget(id, kObjNoState);
			break;
		case kRAMObjDeleted:
		case kFlashObjDeleted:
		{
			ULong home = id >> fObjectNumberShift;
			if (ref.fOffset >> fBlockShift != home)
				fBlocks[home]->ZapMigDirEnt(id);
			ref.Delete();
			if (tracked)
				fTracker->Remove(id);
			fCache->Forget(id, kObjNoState);
			break;
		}
		}
	}
	NewtonErr err = DeleteTransactionRecord();
	Remove(&other);
	Remove(&ref);
	return err;
}


// ROM 0x000c97a0 DoAbort__11TFlashStoreFUc
// Everything the transaction made is deleted, then everything it marked
// is put back - on flash by copying, since the marks cannot be undone -
// again from the start whenever a compaction moved things meanwhile.
// Separate transactions are left alone unless `all`.
NewtonErr
TFlashStore::DoAbort(UChar all)
{
	TObjRef ref, other;
	ref.Init(this);
	other.Init(this);
	TFlashIterator iter(this, &ref, kIterAllObjects);
	NewtonErr err = noErr;
	while (!iter.Done())
	{
		iter.Next();
		PSSId id = ref.Id();
		switch (ref.State())
		{
		case kRAMObjCloneEmpty:
		case kRAMObjCloneOfNew:
		case kRAMObjNew:
		case kRAMObjSuperceder:
		case kFlashObjCloneEmpty:
		case kFlashObjCloneOfNew:
		case kFlashObjNew:
		case kFlashObjSuperceder:
			if (all || ref.SeparateBits() != 2)
			{
				ref.Delete();
				fCache->Forget(id, kObjNoState);
			}
			break;
		}
	}
	for (;;)
	{
		ULong compactCount = fCompactCount;
		iter.Reset();
		while (!iter.Done() && err == noErr)
		{
			iter.Next();
			PSSId id = ref.Id();
			int state = ref.State();
			if (state == kRAMObjDeleted || state == kRAMObjSuperceded)
			{
				if (all || ref.SeparateBits() != 2)
					ref.SetCommittedState();
			}
			else if (state == kFlashObjSuperceded || state == kFlashObjDeleted)
			{
				if (all || ref.SeparateBits() != 2)
				{
					err = ref.Clone(State(kRAMObjCloneEmpty), other, false);
					if (err == noErr && (err = ref.Delete()) == noErr
					 && (err = other.SetState(State(kRAMObjCommitted))) == noErr)
						fCache->Forget(id, kObjNoState);
				}
			}
		}
		if (fCompactCount == compactCount || err != noErr)
		{
			if (err == noErr)
				err = DeleteTransactionRecord();
			fCache->ForgetAll();
			Remove(&other);
			Remove(&ref);
			return err;
		}
	}
}


/*------------------------------------------------------------------------------
	S e p a r a t e   t r a n s a c t i o n s
------------------------------------------------------------------------------*/

// ROM 0x000c9cb4 StartTransactionAgainst__11TFlashStoreFUl
// An object put in a transaction of its own (a committed one by way of a
// copy, as in the store's).
NewtonErr
TFlashStore::StartTransactionAgainst(PSSId id)
{
	NewtonErr err = noErr;
	VppOn();
	newton_try
	{
		TObjRef ref, other;
		ref.Init(this);
		other.Init(this);
		err = SetupForModify(id, &ref, false, false);
		if (err == noErr && (ref.fWord1 & 6) != 4)
		{
			fTracker->fNesting++;
			switch (ref.State())
			{
			case kRAMObjNew:
			case kFlashObjNew:
				err = ref.SetSeparateTranny();
				break;
			case kRAMObjCommitted:
			case kFlashObjCommitted:
				ref.SetState(State(kRAMObjSuperceded));
				ref.Clone(State(kRAMObjSuperceder), other, true);
				err = ref.SetSeparateTranny();
				fCache->Change(other);
				break;
			case kRAMObjSuperceder:
			case kFlashObjSuperceder:
				err = ref.FindSuperceeded(other);
				if (err == noErr)
				{
					ref.SetSeparateTranny();
					other.SetSeparateTranny();
				}
				break;
			}
			EndNesting(this, true);
		}
		Remove(&other);
		Remove(&ref);
	}
	FLASH_STORE_ABORTED(this)
	end_try;
	VppOff();
	return err;
}


// ROM 0x000c9ef8 SeparatelyAbort__11TFlashStoreFUl
// An object's own transaction undone.
NewtonErr
TFlashStore::SeparatelyAbort(PSSId id)
{
	NewtonErr err = noErr;
	VppOn();
	newton_try
	{
		TObjRef ref, other, third;
		ref.Init(this);
		other.Init(this);
		third.Init(this);
		err = SetupForModify(id, &ref, false, false);
		if (err == kSError_ObjectNotFound)
			err = Lookup(id, State(kRAMObjDeleted), ref);
		if (err == noErr && ref.IsSeparate())
		{
			switch (ref.State())
			{
			case kRAMObjNew:
			case kFlashObjNew:
				err = ref.Delete();
				break;
			case kRAMObjSuperceder:
				err = ref.FindSuperceeded(other);
				if (err == noErr && (err = ref.Delete()) == noErr && (err = other.ClearSeparateTranny()) == noErr
				 && (err = other.SetState(State(kRAMObjCommitted))) == noErr)
					fCache->Forget(id, kObjNoState);
				break;
			case kRAMObjDeleted:
				err = ref.ClearSeparateTranny();
				if (err == noErr && (err = ref.SetState(State(kRAMObjCommitted))) == noErr)
					fCache->Forget(id, kObjNoState);
				break;
			case kFlashObjSuperceder:
				err = ref.FindSuperceeded(other);
				if (err == noErr && (err = ref.Delete()) == noErr
				 && (err = other.Clone(State(kRAMObjCloneEmpty), third, false)) == noErr
				 && (err = third.SetState(State(kRAMObjCommitted))) == noErr
				 && (err = other.Delete()) == noErr)
					fCache->Forget(id, kObjNoState);
				break;
			case kFlashObjDeleted:
				err = ref.ClearSeparateTranny();
				if (err == noErr
				 && (err = ref.Clone(State(kRAMObjCloneEmpty), other, false)) == noErr
				 && (err = other.SetState(State(kRAMObjCommitted))) == noErr
				 && (err = ref.Delete()) == noErr)
					fCache->Forget(id, kObjNoState);
				break;
			}
			fCache->Forget(id, kObjNoState);
		}
		Remove(&third);
		Remove(&other);
		Remove(&ref);
	}
	FLASH_STORE_ABORTED(this)
	end_try;
	VppOff();
	return err;
}


// ROM 0x000ca298 AddToCurrentTransaction__11TFlashStoreFUl
// An object's own transaction joined to the store's.
NewtonErr
TFlashStore::AddToCurrentTransaction(PSSId id)
{
	NewtonErr err = noErr;
	VppOn();
	newton_try
	{
		TObjRef ref, other;
		ref.Init(this);
		other.Init(this);
		LockStore();
		err = SetupForModify(id, &ref, true, true);
		if (err == kSError_ObjectNotFound)
			err = Lookup(id, State(kRAMObjDeleted), ref);
		if (err == noErr && ref.IsSeparate())
		{
			switch (ref.State())
			{
			case kRAMObjNew:
			case kRAMObjDeleted:
			case kFlashObjNew:
			case kFlashObjDeleted:
				fTracker->fNesting++;
				err = ref.ClearSeparateTranny();
				if (err == noErr)
					fTracker->Add(id);
				fCache->Change(ref);
				EndNesting(this, true);
				break;
			case kRAMObjSuperceder:
			case kFlashObjSuperceder:
				fTracker->fNesting++;
				ref.FindSuperceeded(other);
				ref.ClearSeparateTranny();
				err = other.ClearSeparateTranny();
				if (err == noErr)
					fTracker->Add(id);
				fCache->Change(ref);
				EndNesting(this, true);
				break;
			}
		}
		UnlockStore();
		Remove(&other);
		Remove(&ref);
	}
	FLASH_STORE_ABORTED(this)
	end_try;
	VppOff();
	return err;
}


// ROM 0x000ca51c InSeparateTransaction__11TFlashStoreFUl
Boolean
TFlashStore::InSeparateTransaction(PSSId id)
{
	Boolean separate = false;
	VppOn();
	newton_try
	{
		TObjRef ref;
		ref.Init(this);
		separate = Lookup(id, kObjNoState, ref) == noErr && (ref.fWord1 & 6) == 4;
		Remove(&ref);
	}
	FLASH_STORE_ABORTED(this)
	end_try;
	VppOff();
	return separate;
}


// ROM 0x000ca61c LockReadOnly__11TFlashStoreFv
NewtonErr
TFlashStore::LockReadOnly(void)
{
	fReadOnlyLockCount++;
	return noErr;
}


// ROM 0x000ca630 UnlockReadOnly__11TFlashStoreFUc
NewtonErr
TFlashStore::UnlockReadOnly(Boolean reset)
{
	if (!reset)
	{
		if (fReadOnlyLockCount != 0)
			fReadOnlyLockCount--;
	}
	else
		fReadOnlyLockCount = 0;
	return noErr;
}


// ROM 0x000ca658 InTransaction__11TFlashStoreFv
Boolean
TFlashStore::InTransaction(void)
{
	return fInTransaction;
}
