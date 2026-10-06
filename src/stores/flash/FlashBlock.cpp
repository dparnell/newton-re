/*
	File:		stores/flash/FlashBlock.cpp

	Contains:	TFlashPhysBlock and TFlashBlock (FlashStore.h): what a
				physical block is, and a logical block's objects, directory
				and compaction.

	Reconstructed from the MP2x00 US ROM (0x000bee88-0x000c0cf0,
	0x000c23b0-0x000c2690); each function cites its origin.
*/

#include "FlashStore.h"
#include "LargeObjects.h"
#include "ByteOrder.h"
#include "host/RomBugs.h"

#include <string.h>


// A word written the way the flash keeps it.  DEVIATION: see FlashStore.h.
static inline void
ToFlashWord(void* p, ULong word)
{
	PutBigEndianWord(p, word);
}


/*------------------------------------------------------------------------------
	T F l a s h P h y s B l o c k
------------------------------------------------------------------------------*/

// ROM 0x000c23b0 Init__15TFlashPhysBlockFP11TFlashStoreUl
void
TFlashPhysBlock::Init(TFlashStore* store, ULong physOffset)
{
	fStore = store;
	fLogicalOffset = 0xFFFFFFFF;
	fPhysOffset = physOffset;
	fLogEntryOffset = 0;
	fUnusable = false;
	fEraseCount = 1;
	fIsReserved = false;
}


// ROM 0x000c23d8 SetInfo__15TFlashPhysBlockFP19SFlashBlockLogEntry
void
TFlashPhysBlock::SetInfo(SFlashBlockLogEntry* entry)
{
	fLogEntryOffset = entry->PhysOffset();
	fEraseCount = entry->fEraseCount;
	fLogicalOffset = entry->fLogicalOffset;
	fUnusable = false;
	fIsReserved = false;
}


// ROM 0x000c2420 SetInfo__15TFlashPhysBlockFP19SFlashEraseLogEntry
// Erased, and so the spare.
void
TFlashPhysBlock::SetInfo(SFlashEraseLogEntry* entry)
{
	fLogEntryOffset = entry->PhysOffset();
	fEraseCount = entry->fEraseCount;
	fUnusable = (entry->fUsable == 0);
	fLogicalOffset = 0xFFFFFFFF;
	fIsReserved = false;
}


// ROM 0x000c2470 SetInfo__15TFlashPhysBlockFP22SReservedBlockLogEntry
void
TFlashPhysBlock::SetInfo(SReservedBlockLogEntry* entry)
{
	fLogEntryOffset = entry->PhysOffset();
	fEraseCount = entry->fEraseCount;
	fLogicalOffset = entry->fLogicalOffset;
	fUnusable = false;
	fIsReserved = true;
}


// ROM 0x000c2418 LogEntryOffset__15TFlashPhysBlockFv
ULong		TFlashPhysBlock::LogEntryOffset(void)		{ return fLogEntryOffset; }
// ROM 0x000c24b4 EraseCount__15TFlashPhysBlockFv
ULong		TFlashPhysBlock::EraseCount(void)			{ return fEraseCount; }
// ROM 0x000c24bc GetPhysicalOffset__15TFlashPhysBlockFv
ULong		TFlashPhysBlock::GetPhysicalOffset(void)	{ return fPhysOffset; }
// ROM 0x000c24c4 IsSpare__15TFlashPhysBlockFv
Boolean		TFlashPhysBlock::IsSpare(void)				{ return fLogicalOffset == 0xFFFFFFFF; }


// ROM 0x000c24dc SetSpare__15TFlashPhysBlockFP15TFlashPhysBlockUl
// The logical block this physical block held has been copied into
// newHome: newHome's log gets the block's entry (with its new root
// directory and newHome's erase count) and an entry saying this block was
// erased one time more - written there because this block's own log is
// about to go - the old entry is zapped, and this block is erased, which
// makes it the spare.
// (The ROM leaves the erase entry's other words as its stack held them;
// the host writes noughts.)
NewtonErr
TFlashPhysBlock::SetSpare(TFlashPhysBlock* newHome, ULong rootDirectory)
{
	ULong newPhysOffset = newHome->GetPhysicalOffset();
	NewtonErr err;
	if (fIsReserved)
	{
		SReservedBlockLogEntry entry;
		fStore->ReadWords(fLogEntryOffset, (FlashWord*) &entry, kReservedBlockLogEntrySize / 4);
		entry.fPhysOffset = newPhysOffset;
		entry.fEraseCount = newHome->EraseCount();
		err = fStore->AddLogEntryToPhysBlock(kReservedBlockLogTag, kReservedBlockLogEntrySize, &entry, newPhysOffset, nil);
		if (err != noErr)
			return err;
		fStore->BlockAt(fLogicalOffset)->SetInfo(&entry);
	}
	else
	{
		SFlashBlockLogEntry entry;
		fStore->ReadWords(fLogEntryOffset, (FlashWord*) &entry, kFlashBlockLogEntrySize / 4);
		entry.fRootDirectory = rootDirectory;
		entry.fPhysOffset = newPhysOffset;
		entry.fEraseCount = newHome->EraseCount();
		err = fStore->AddLogEntryToPhysBlock(kFlashBlockLogTag, kFlashBlockLogEntrySize, &entry, newPhysOffset, nil);
		if (err != noErr)
			return err;
		fStore->BlockAt(fLogicalOffset)->SetInfo(&entry, nil);
	}
	err = fStore->ZapLogEntry(fLogEntryOffset);
	if (err != noErr)
		return err;
	SFlashEraseLogEntry erasure;
	memset(&erasure, 0, sizeof(erasure));
	erasure.fPhysOffset = fPhysOffset;
	erasure.fEraseCount = fEraseCount + 1;
	erasure.fUsable = 1;
	err = fStore->AddLogEntryToPhysBlock(kFlashEraseLogTag, kFlashEraseLogEntrySize, &erasure, newPhysOffset, nil);
	if (err != noErr)
		return err;
	SetInfo(&erasure);
	return fStore->SyncErase(fPhysOffset);
}


/*------------------------------------------------------------------------------
	T F l a s h B l o c k
------------------------------------------------------------------------------*/

// ROM 0x000bee88 Init__11TFlashBlockFP11TFlashStore
void
TFlashBlock::Init(TFlashStore* store)
{
	fStore = store;
	fLogicalOffset = 0;
	fPhysOffset = 0xFFFFFFFF;
	fRootDirectory = 0;
	fFreeOffset = 0;
	fZappedBytes = 0;
	fNextPSSID = 0;
}


// ROM 0x000beeac SetInfo__11TFlashBlockFP19SFlashBlockLogEntryPUc
// What the log entry says, and then the objects walked to find where the
// free space starts, how much the deleted ones take, whether any is in a
// separate transaction, and the highest id made in each block.
NewtonErr
TFlashBlock::SetInfo(SFlashBlockLogEntry* entry, UChar* separateSeen)
{
	ULong start = entry->fLogicalOffset;
	fLogicalOffset = start;
	fPhysOffset = entry->fPhysOffset;
	fRootDirectory = entry->fRootDirectory;
	fFreeOffset = entry->fLogicalOffset + 4;
	fZappedBytes = 0;
	fStoreId = entry->fTime ^ entry->fRandom;
	if (fNextPSSID == 0)
		fNextPSSID = ((start >> fStore->fBlockShift) << fStore->fObjectNumberShift) + 0x31;
	PhysBlock()->SetInfo(entry);

	ULong offset = fLogicalOffset;
	NewtonErr err;
	for (;;)
	{
		err = NextObject(offset, &offset, true);
		if (err != noErr)
		{
			if (err == kSError_NoMoreObjects)
				err = noErr;
			return err;
		}
		SObject object;
		err = ReadObjectAt(offset, &object);
		if (err != noErr)
			break;
		fFreeOffset = ((object.Size() + 3) & ~3) + 8 + offset;
		if ((fStore->fZapWord & 1) == ((object.fWord0 & 0x7FFFFFFF) >> 30))
			fZappedBytes = ((object.Size() + 3) & ~3) + 8 + fZappedBytes;
		else if (separateSeen != nil && object.SeparateBits() == 2)
			*separateSeen = true;
		PSSId id = object.Id();
		if (id > 0x30)
		{
			TFlashBlock* home = fStore->fBlocks[id >> fStore->fObjectNumberShift];
			if (home->fNextPSSID <= id)
				home->fNextPSSID = id + 1;
		}
	}
	return err;
}


// ROM 0x000bfc7c SetInfo__11TFlashBlockFP22SReservedBlockLogEntry
NewtonErr
TFlashBlock::SetInfo(SReservedBlockLogEntry* entry)
{
	fLogicalOffset = entry->fLogicalOffset;
	fPhysOffset = entry->fPhysOffset;
	PhysBlock()->SetInfo(entry);
	return noErr;
}


// ROM 0x000bf044 Lookup__11TFlashBlockFUliR7TObjRefPl
// Through the root directory's bucket for the id.
NewtonErr
TFlashBlock::Lookup(PSSId id, int state, TObjRef& ref, long* migratedTo)
{
	if (IsReserved())
	{
		if (migratedTo != nil)
			*migratedTo = -1;
		return kSError_ObjectNotFound;
	}
	TFlashIterator iter(fStore, &ref, RootDirEnt(id), kIterBlockObjects);
	return iter.Lookup(id, state, migratedTo);
}


// ROM 0x000bf0d0 AddObject__11TFlashBlockFUliT1R7TObjRefUcT5
// A directory entry first (for an object that has one: the store's own
// objects below 0x11 do not), then the header at the free offset - which
// for an execute-in-place object is first moved on so that the object's
// data ends on a 4 KB page boundary.  A header that does not write
// correctly is zapped and the next eight bytes tried; the directory entry
// is pointed at the header last.
NewtonErr
TFlashBlock::AddObject(PSSId id, int state, ULong size, TObjRef& ref, UChar separate, UChar xip)
{
	UChar isSeparate = separate;
	if (IsReserved())
		return kSError_BlockFull;
	ULong dirEntOffset = 0xFFFFFFFF;
	NewtonErr err;
	if (id > 0x10 && (err = AddDirEnt(id, 0xFFFFFFFF, &dirEntOffset, nil)) != noErr)
		return err;
	ULong objectSize = ((size + 3) & ~3) + 8;
	ULong pad;
	if (!xip)
		pad = 0;
	else
	{
		ULong head = (size & 0xFFF) + 8;
		pad = ((((fFreeOffset + head + 0xFFF) & 0xFFFFF000) - head) - fFreeOffset + 3) & ~3;
	}
	ULong total = objectSize + pad;
	ULong sizeBits = size & 0xFFFF;
	ULong idBits = id & 0x0FFFFFFF;
	do
	{
		if (Avail() < total)
			return kSError_BlockFull;
		if (pad != 0)
		{
			ULong at = fStore->Translate(fFreeOffset);
			err = fStore->Zap(at, pad);
			fFreeOffset = fFreeOffset + pad;
			if (err != noErr)
				return err;
		}
		memset(&ref, fStore->fVirginWord & 0xFF, 8);
		ref.fWord1 = (ref.fWord1 & 0xFFFF) | (sizeBits << 16);
		ref.fWord1 = (ref.fWord1 & 0xFFFF00FF) | ((ObjectStateToTransBits(state, fStore) & 0xFF) << 8);
		ref.fWord0 = (ref.fWord0 & 0xF0000000) | idBits;
		ULong word1 = ref.fWord1;
		ULong zap = fStore->fZapWord & 1;
		ref.fWord1 = (word1 & ~1) | zap;
		if (isSeparate)
			ref.fWord1 = (word1 & ~7) | zap | 4;
		if (xip)
			ref.fWord1 = (ref.fWord1 & ~0x80) | (zap << 7);
		if (id < 0x31 && fStore->fIsSRAM)
			fStore->fStoreDriver->Set(fStore->Translate(fFreeOffset) + 8, size, fStore->fVirginWord);
		UChar header[8];
		ToFlashWord(header, ref.fWord0);
		ToFlashWord(header + 4, ref.fWord1);
		err = BasicWrite(fFreeOffset, header, 8);
		if (err == noErr)
		{
			ULong at = fFreeOffset;
			fFreeOffset = at + objectSize;
			if (dirEntOffset != 0xFFFFFFFF)
			{
				while ((err = SetDirEntOffset(dirEntOffset, at)) == kSError_WriteError)
				{
					err = AddDirEnt(id, 0xFFFFFFFF, &dirEntOffset, nil);
					if (err == kSError_BlockFull)
					{
						ZapObject(at);
						return kSError_BlockFull;
					}
				}
				if (err != noErr)
					return err;
			}
			ref.Set(at, dirEntOffset);
			return noErr;
		}
		NewtonErr zapErr = fStore->Zap(fStore->Translate(fFreeOffset), 8);
		fFreeOffset = fFreeOffset + 8;
		if (zapErr != noErr)
			return zapErr;
	} while (!xip && err == kSError_WriteError);
	return err;
}


// ROM 0x000bf42c AddDirEnt__11TFlashBlockFUlT1PUlP7SDirEnt
// A slot in the id's bucket - a blank one (or, in RAM, one not in use) -
// or in its continuations, following each bucket's link; a full bucket
// gets a continuation.  The last two slots of a bucket are for the link.
// A slot that does not write correctly is zapped and the next tried.
NewtonErr
TFlashBlock::AddDirEnt(PSSId id, ULong objOffset, ULong* dirEntOffset, SDirEnt* dirEnt)
{
	ULong bucket = RootDirEnt(id);
	NewtonErr err;
	SDirEnt entry;
	for (;;)
	{
		ULong zap;
		ULong pos = bucket + 4;
		Boolean followed = false;
		while (pos < bucket + BucketSize() * 4 - 8)
		{
			err = ReadDirEntAt(pos, &entry);
			if (err != noErr)
				return err;
			zap = fStore->fZapWord & 1;
			if (fStore->fVirginWord == entry.fWord
			 || (fStore->fIsSRAM
				 && (!entry.IsValid(fStore)
					 || (zap == ((entry.fWord & 0x7F) >> 6) && zap != ((entry.fWord & 7) >> 2)))))
			{
				entry.fWord = fStore->fVirginWord;
				if (objOffset != 0xFFFFFFFF)
					entry.fWord = (entry.fWord & 0xFF) | ((objOffset >> 2) << 8);
				entry.fWord = (entry.fWord & ~8) | ((fStore->fZapWord & 1) << 3);
				UChar word[4];
				ToFlashWord(word, entry.fWord);
				err = BasicWrite(pos, word, 4);
				if (err != kSError_WriteError)
				{
					if (err != noErr)
						return err;
					if (dirEntOffset != nil)
						*dirEntOffset = pos;
					if (dirEnt != nil)
						*dirEnt = entry;
					return noErr;
				}
				ZapDirEnt(pos);
			}
			else if (entry.IsValid(fStore)
				  && (zap = fStore->fZapWord & 1, zap != ((entry.fWord & 0x7F) >> 6))
				  && zap == ((entry.fWord & 0xFF) >> 7))
			{
				bucket = (entry.fWord >> 8) << 2;
				followed = true;
				break;
			}
			pos += 4;
		}
		if (followed)
			continue;
		for (pos = bucket + BucketSize() * 4 - 8; pos < bucket + BucketSize() * 4; pos += 4)
		{
			err = ReadDirEntAt(pos, &entry);
			if (err != noErr)
				return err;
			zap = fStore->fZapWord & 1;
			if (entry.IsValid(fStore)
			 && zap != ((entry.fWord & 0x7F) >> 6)
			 && zap == ((entry.fWord & 0xFF) >> 7))
			{
				bucket = (entry.fWord >> 8) << 2;
				followed = true;
				break;
			}
		}
		if (followed)
			continue;
		err = ExtendDirBucket(bucket + BucketSize() * 4 - 8, &bucket);
		if (err != noErr)
			return err;
	}
}


// ROM 0x000bf6d0 ExtendDirBucket__11TFlashBlockFUlPUl
// A continuation bucket (an object of id 3), linked from the full bucket's
// next-to-last slot - or, if that will not write, its last.
NewtonErr
TFlashBlock::ExtendDirBucket(ULong lastEntryOffset, ULong* newBucket)
{
	TObjRef ref;
	TFlashStore* store = fStore;
	ref.Init(store);
	NewtonErr err = AddObject(kFlashRootDirectoryId, kObjNoState, BucketSize() << 2, ref, false, false);
	if (err == noErr)
	{
		if (newBucket != nil)
			*newBucket = ref.fOffset + 8;
		ULong link = (fStore->fVirginWord & 0x77) | (((ref.fOffset + 8) >> 2) << 8)
				   | ((fStore->fZapWord & 1) << 7) | ((fStore->fZapWord & 1) << 3);
		UChar word[4];
		ToFlashWord(word, link);
		err = BasicWrite(lastEntryOffset, word, 4);
		if (err == kSError_WriteError)
		{
			ZapDirEnt(lastEntryOffset);
			err = BasicWrite(lastEntryOffset + 4, word, 4) != noErr ? kSError_BlockFull : noErr;
		}
	}
	store->Remove(&ref);
	return err;
}


// ROM 0x000bf824 ZapObject__11TFlashBlockFUl
// Deleted: bit 30 of its first word cleared, and its bytes counted as
// recoverable.
NewtonErr
TFlashBlock::ZapObject(ULong offset)
{
	ULong at = fStore->Translate(offset);
	SObject object;
	NewtonErr err = ReadObjectAt(offset, &object);
	if (err == noErr)
	{
		err = noErr;
		if (object.IsValid(fStore) && (fStore->fVirginWord & 1) == ((object.fWord0 & 0x3FFFFFFF) >> 29))
		{
			fZappedBytes = ((object.Size() + 3) & ~3) + 8 + fZappedBytes;
			object.fWord0 = (object.fWord0 & ~0x40000000) | ((fStore->fZapWord & 1) << 30);
			err = fStore->WriteWords(at, &object.fWord0, 2);
		}
	}
	return err;
}


// ROM 0x000bf8e4 ZapDirEnt__11TFlashBlockFUl
NewtonErr
TFlashBlock::ZapDirEnt(ULong offset)
{
	ULong at = fStore->Translate(offset);
	TFlashStore* store = fStore;
	NewtonErr err = noErr;
	if (!store->fIsSRAM)
		{ FlashWord zap = (FlashWord) store->fZapWord; err = store->WriteWords(at, &zap, 1); }
	else
	{
		Boolean done = false;
		do
		{
			newton_try
			{
				store->fStoreDriver->Set(at, 4, store->fZapWord);
				done = true;
			}
			newton_catch(exAbort)
			{
				store->SendAlertMgrWPBitch(0);
			}
			end_try;
		} while (!done);
	}
	return err;
}


// ROM 0x000bf910 NextObject__11TFlashBlockFUlPUlUc
// The object after the one at offset (from the block's start: the first),
// leaving out deleted ones unless allStates.  A blank word is the end.
NewtonErr
TFlashBlock::NextObject(ULong offset, ULong* nextOffset, UChar allStates)
{
	Boolean take = (fStore->fBlockMask & offset) == 0;
	if (take)
		offset = fLogicalOffset + 4;
	while (offset < EndOffset())
	{
		SObject object;
		NewtonErr err = ReadObjectAt(offset, &object);
		if (err != noErr)
			return err;
		if (!object.IsValid(fStore))
		{
			if (fStore->fVirginWord == object.fWord0)
				return kSError_NoMoreObjects;
			offset += 4;
			continue;
		}
		if (take && (allStates || (fStore->fZapWord & 1) != ((object.fWord0 & 0x7FFFFFFF) >> 30)))
		{
			*nextOffset = offset;
			return noErr;
		}
		if ((fStore->fVirginWord & 1) != ((object.fWord0 & 0x3FFFFFFF) >> 29))
		{
			offset += 4;
			continue;
		}
		offset = ((object.Size() + 3) & ~3) + 8 + offset;
		take = true;
	}
	return kSError_NoMoreObjects;
}


// NOT YET: a RAM store's compaction in place, which survives a reboot
// through its SCompactState.  TFlashStore::Init refuses RAM stores.
NewtonErr	TFlashBlock::CompactInPlace(void)						{ return kError_Call_Not_Implemented; }
NewtonErr	TFlashBlock::StartCompact(SCompactState*)				{ return kError_Call_Not_Implemented; }
NewtonErr	TFlashBlock::ContinueCompact(SCompactState*)			{ return kError_Call_Not_Implemented; }
NewtonErr	TFlashBlock::RealContinueCompact(SCompactState*)		{ return kError_Call_Not_Implemented; }


// ROM 0x000c018c CompactInto__11TFlashBlockFUl
// The block's live objects copied into the erased physical block at
// physOffset, through the store's spare logical block (DummyBlock) made to
// look like this one with a fresh root directory there; then this block's
// physical block becomes the spare.  A reserved block is copied as it is.
NewtonErr
TFlashBlock::CompactInto(ULong physOffset)
{
	TFlashPhysBlock* oldPhys = PhysBlock();
	TFlashBlock* into = fStore->DummyBlock();
	*into = *this;
	into->fRootDirectory = 0;
	into->fPhysOffset = physOffset;
	into->fZappedBytes = 0;
	into->fFreeOffset = fLogicalOffset + 4;
	NewtonErr err;
	if (!IsReserved())
	{
		fStore->ExchangeBlock(fLogicalOffset, into);
		into->WriteRootDirectory(&into->fRootDirectory);
		fStore->ExchangeBlock(fLogicalOffset, this);
	}
	if (!IsReserved())
	{
		err = CompactInto(into);
		if (err != noErr)
			return err;
	}
	else
	{
		err = fStore->BasicCopy(fPhysOffset, physOffset, fStore->fBlockSize - fStore->LogSize());
		if (err != noErr)
			return err;
	}
	err = oldPhys->SetSpare(fStore->PhysBlockAt(physOffset), into->fRootDirectory);
	if (err == noErr)
	{
		fStore->BlockCompacted();
		fStore->NotifyCompact(this);
	}
	return err;
}


// ROM 0x000c02bc CompactInto__11TFlashBlockFP11TFlashBlock
// Every live object but the directories copied into the other block (made
// the block's own for the moment, so that the copies' offsets translate
// there), then the migrated-object entries of the root directory.
// ROM BUG (fixed): only the last object's copy is checked for an error,
// and the copies' AddObject is not checked at all.  The fix stops at the
// first AddObject or copy that fails (before copying into an object that
// was not made) and answers its error.
NewtonErr
TFlashBlock::CompactInto(TFlashBlock* into)
{
	TObjRef from;
	TFlashStore* fromStore = fStore;
	from.Init(fromStore);
	TObjRef to;
	TFlashStore* toStore = fStore;
	to.Init(toStore);
	SDirEnt dirEnt;
	TFlashIterator iter(fStore, &from, this, kIterBlockObjectsAllStates);
	NewtonErr err = noErr;
	while (!iter.Done())
	{
		iter.Next();
		if (from.Id() != kFlashRootDirectoryId)
		{
			fStore->ExchangeBlock(fLogicalOffset, into);
			Boolean xip = (fromStore->fZapWord & 1) == ((from.fWord1 & 0xFF) >> 7);
			NewtonErr addErr = into->AddObject(from.Id(), from.State(), from.Size(), to, from.SeparateBits() == 2, xip);
			ULong toAt = fStore->Translate(to.fOffset);
			fStore->ExchangeBlock(fLogicalOffset, this);
			if (RomBugFixed() && addErr != noErr)
			{
				err = addErr;
				break;
			}
			if ((fromStore->fZapWord & 1) == ((from.fWord1 & 0xFF) >> 7))
				XIPObjectHasMoved(fStore, from.Id());
			ULong size = from.Size();
			err = fStore->BasicCopy(fStore->Translate(from.fOffset) + 8, toAt + 8, size);
			if (RomBugFixed() && err != noErr)
				break;
		}
	}
	if (err == noErr)
	{
		ULong bucket = fRootDirectory + 8;
		ULong bucketSize = BucketSize();
		for (long i = (long) BucketCount() - 1; i >= 0; i--)
		{
			TFlashIterator entries(fStore, &dirEnt, bucket);
			while (!entries.Done())
			{
				entries.Next();
				if ((fStore->fZapWord & 1) == ((dirEnt.fWord & 7) >> 2))
				{
					long objectNumber, block;
					dirEnt.GetMigratedObjectInfo(&objectNumber, &block);
					fStore->ExchangeBlock(fLogicalOffset, into);
					into->AddMigDirEnt(objectNumber, block);
					fStore->ExchangeBlock(fLogicalOffset, this);
				}
			}
			bucket += bucketSize * 4;
		}
	}
	toStore->Remove(&to);
	fromStore->Remove(&from);
	return err;
}


// ROM 0x000c0574 ReadObjectAt__11TFlashBlockFUlP7SObject
// (The ROM has BasicRead inline.)
NewtonErr
TFlashBlock::ReadObjectAt(ULong offset, SObject* object)
{
	fStore->ReadWords(fStore->Translate(offset), &object->fWord0, 2);
	return noErr;
}


// ROM 0x000c05a8 AddMigDirEnt__11TFlashBlockFlT1
// An entry saying that object number objectNumber of this block now lives
// in block `block`.
NewtonErr
TFlashBlock::AddMigDirEnt(long objectNumber, long block)
{
	SDirEnt entry;
	ULong at;
	PSSId id = fStore->PSSIDFor(fLogicalOffset >> fStore->fBlockShift, objectNumber);
	NewtonErr err = AddDirEnt(id, 0xFFFFFFFF, &at, &entry);
	if (err == noErr)
	{
		entry.SetMigratedObjectInfo(objectNumber, block);
		ULong zap = fStore->fZapWord & 1;
		entry.fWord = (entry.fWord & ~0x44) | (zap << 2) | (zap << 6);
		UChar word[4];
		ToFlashWord(word, entry.fWord);
		err = BasicWrite(at, word, 4);
		if (err != noErr)
			ZapDirEnt(at);
	}
	return err;
}


// ROM 0x000c0664 ObjectMigrated__11TFlashBlockFUll
// The object with id, made in this block, was committed in block `block`:
// the old migration entry goes, and a new one is made unless it has come
// home.
// ROM QUIRK: a virgin block answers IsVirgin's true, and an object come
// home answers the block's offset, where noErr is meant; nothing reads the
// answer.
NewtonErr
TFlashBlock::ObjectMigrated(PSSId id, long block)
{
	Boolean virgin = IsVirgin();
	if (virgin)
		return virgin;
	long objectNumber = fStore->ObjectNumberFor(id);
	if (!SDirEnt::IsValidMigratedObjectInfo(objectNumber, block))
		return noErr;
	NewtonErr err = ZapMigDirEnt(id);
	if (err != noErr)
		return err;
	if ((ULong) block == fLogicalOffset >> fStore->fBlockShift)
		return fLogicalOffset;
	return AddMigDirEnt(objectNumber, block);
}


// ROM 0x000c06e4 ZapMigDirEnt__11TFlashBlockFUl
NewtonErr
TFlashBlock::ZapMigDirEnt(PSSId id)
{
	if (IsVirgin() || IsReserved())
		return noErr;
	long objectNumber = fStore->ObjectNumberFor(id);
	SDirEnt entry;
	TFlashIterator iter(fStore, &entry, RootDirEnt(id));
	while (!iter.Done())
	{
		iter.Next();
		if ((fStore->fZapWord & 1) == ((entry.fWord & 7) >> 2))
		{
			long number, block;
			entry.GetMigratedObjectInfo(&number, &block);
			if (number == objectNumber)
				return ZapDirEnt(iter.fDirEntOffset);
		}
	}
	return noErr;
}


// ROM 0x000c07d4 ReadDirEntAt__11TFlashBlockFUlP7SDirEnt
// (BasicRead inline.)
NewtonErr
TFlashBlock::ReadDirEntAt(ULong offset, SDirEnt* dirEnt)
{
	fStore->ReadWords(fStore->Translate(offset), &dirEnt->fWord, 1);
	return noErr;
}


// ROM 0x000c0808 IsVirgin__11TFlashBlockFv
// Never brought into use: no log entry says where it is.
Boolean
TFlashBlock::IsVirgin(void)
{
	return LogEntryOffset() == 0;
}


// ROM 0x000c082c WriteRootDirectory__11TFlashBlockFPUl
NewtonErr
TFlashBlock::WriteRootDirectory(ULong* rootDirectory)
{
	TObjRef ref;
	TFlashStore* store = fStore;
	ref.Init(store);
	NewtonErr err = AddObject(kFlashRootDirectoryId, kObjNoState, RootDirSize(), ref, false, false);
	if (rootDirectory != nil)
		*rootDirectory = ref.fOffset;
	store->Remove(&ref);
	return err;
}


// ROM 0x000c08b0 IsReserved__11TFlashBlockFv
Boolean
TFlashBlock::IsReserved(void)
{
	if (fPhysOffset == 0xFFFFFFFF)
		return false;
	return PhysBlock()->fIsReserved;
}


// ROM 0x000c08dc EraseCount__11TFlashBlockFv
ULong
TFlashBlock::EraseCount(void)
{
	if (fPhysOffset != 0xFFFFFFFF)
		return PhysBlock()->fEraseCount;
	return 0;
}


// ROM 0x000c0904 RootDirEnt__11TFlashBlockFUl
// The bucket an id hashes to (past the root directory's header).
ULong
TFlashBlock::RootDirEnt(PSSId id)
{
	ULong hash = HashPSSID(id);
	ULong count = BucketCount();
	ULong size = BucketSize();
	return fRootDirectory + size * (hash & (count - 1)) * 4 + 8;
}


// ROM 0x000c094c NextPSSID__11TFlashBlockFv
PSSId
TFlashBlock::NextPSSID(void)
{
	while (!IsValidPSSID(fNextPSSID))
		fNextPSSID++;
	return fNextPSSID;
}


// ROM 0x000c098c UseNextPSSID__11TFlashBlockFv
PSSId
TFlashBlock::UseNextPSSID(void)
{
	return fNextPSSID++;
}


// ROM 0x000c09a0 SetDirEntOffset__11TFlashBlockFUlT1
NewtonErr
TFlashBlock::SetDirEntOffset(ULong dirEntOffset, ULong objOffset)
{
	SDirEnt entry;
	ReadDirEntAt(dirEntOffset, &entry);
	entry.fWord = (entry.fWord & 0xFF) | ((objOffset >> 2) << 8);
	UChar word[4];
	ToFlashWord(word, entry.fWord);
	return BasicWrite(dirEntOffset, word, 4);
}


// ROM 0x000c09f0 BasicWrite__11TFlashBlockFUlPvT1
// (The store's BasicWrite, inline.)
NewtonErr
TFlashBlock::BasicWrite(ULong offset, void* buffer, ULong size)
{
	return fStore->BasicWrite(fStore->Translate(offset), buffer, size);
}


// ROM 0x000c0a28 EraseHeuristic__11TFlashBlockFUl
// How good a block is to compact: what it would yield (in KB, squared)
// plus how far below the average its erase count is (cubed).
long
TFlashBlock::EraseHeuristic(ULong yield)
{
	long difference = (long) fStore->AverageEraseCount() - (long) PhysBlock()->EraseCount();
	return (long) ((yield >> 10) * (yield >> 10)) + difference * difference * difference;
}


// ROM 0x000c0a6c BucketSize__11TFlashBlockFv
ULong		TFlashBlock::BucketSize(void)		{ return fStore->fBucketSize; }
// ROM 0x000c0a78 BucketCount__11TFlashBlockFv
ULong		TFlashBlock::BucketCount(void)		{ return fStore->fBucketCount; }


// ROM 0x000c0a84 Avail__11TFlashBlockFv
ULong
TFlashBlock::Avail(void)
{
	if (IsReserved())
		return 0;
	ULong avail = fStore->fBlockSize - fStore->LogSize();
	if (!IsVirgin())
		avail -= fFreeOffset - fLogicalOffset;
	return avail;
}


// ROM 0x000c0ae8 RootDirSize__11TFlashBlockFv
ULong
TFlashBlock::RootDirSize(void)
{
	ULong size = BucketSize();
	return BucketCount() * size * 4 + 4;
}


// ROM 0x000c0b20 CalcRecoverableBytes__11TFlashBlockFv
// What compacting would give back: the deleted objects and the free space
// (Yield), and the directory slots not in use in the buckets' continuations.
// ROM QUIRK: a link slot that cannot be read is not stepped over.
ULong
TFlashBlock::CalcRecoverableBytes(void)
{
	if (IsReserved())
		return 0;
	ULong size = BucketSize();
	ULong pos = fRootDirectory;
	long unused = 0;
	for (long bucket = (long) BucketCount() - 1; bucket >= 0; bucket--)
	{
		pos += size * 4;
		ULong slot = pos;
		for (long k = 1; k >= 0; k--)
		{
			SDirEnt entry;
			if (ReadDirEntAt(slot, &entry) == noErr)
			{
				ULong zap = fStore->fZapWord & 1;
				if (entry.IsValid(fStore) && zap != ((entry.fWord & 0x7F) >> 6) && zap == ((entry.fWord & 0xFF) >> 7))
				{
					TFlashIterator iter(fStore, &entry, (entry.fWord >> 8) << 2);
					unused += iter.CountUnusedDirEnt();
					break;
				}
				slot += 4;
			}
		}
	}
	return Yield() + unused * 4 + (unused / BucketSize()) * 8;
}


// ROM 0x000c0c44 Yield__11TFlashBlockFv
ULong
TFlashBlock::Yield(void)
{
	if (IsReserved())
		return 0;
	return Avail() + fZappedBytes;
}


// ROM 0x000c0c78 EndOffset__11TFlashBlockFv
// Where the log starts.
ULong
TFlashBlock::EndOffset(void)
{
	return fLogicalOffset + fStore->fBlockSize - fStore->LogSize();
}


// ROM 0x000c0c9c LogEntryOffset__11TFlashBlockFv
ULong
TFlashBlock::LogEntryOffset(void)
{
	if (fPhysOffset != 0xFFFFFFFF)
		return PhysBlock()->fLogEntryOffset;
	return 0;
}


// ROM 0x000c0cc4 PhysBlock__11TFlashBlockFv
TFlashPhysBlock*
TFlashBlock::PhysBlock(void)
{
	if (fPhysOffset != 0xFFFFFFFF)
		return fStore->PhysBlockAt(fPhysOffset);
	return nil;
}
