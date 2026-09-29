/*
	File:		stores/flash/FlashStore.cpp

	Contains:	TFlashStore (FlashStore.h): making and mounting the store,
				its log, its blocks' bookkeeping, the flash underneath, the
				sizes, formatting, and the transaction record.  The object
				operations are FlashStoreObjects.cpp.

	Reconstructed from the MP2x00 US ROM (0x000c0cf0-0x000c2390,
	0x000c4a9c-0x000cadbc); each function cites its origin.
*/

#include "FlashStore.h"
#include "LargeObjects.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"
#include "UserTasks.h"
#include "ByteOrder.h"
#include "Random.h"

#include <string.h>

ULong	gInternalBlockSize		= 0x8000;		// ROM 0x0c100dd8 gInternalBlockSize
ULong	gMutableBlockSize		= 0x8000;		// ROM 0x0c100ddc gMutableBlockSize
ULong	gInternalFlashStoreSlop	= 0x9800;		// ROM 0x0c100de0 gInternalFlashStoreSlop


/*------------------------------------------------------------------------------
	The words of the flash's structures.  DEVIATION: big-endian on the
	flash, the host's order in memory (FlashStore.h).
------------------------------------------------------------------------------*/

NewtonErr
TFlashStore::ReadWords(ULong offset, FlashWord* words, ULong count)
{
	NewtonErr err = BasicRead(offset, words, count * 4);
	for (ULong i = 0; i < count; i++)
		words[i] = GetBigEndianWord(&words[i]);
	return err;
}


NewtonErr
TFlashStore::WriteWords(ULong offset, const FlashWord* words, ULong count)
{
	FlashWord flash[32];
	for (ULong i = 0; i < count; i++)
		PutBigEndianWord(&flash[i], words[i]);
	return BasicWrite(offset, flash, count * 4);
}


/*------------------------------------------------------------------------------
	M a k i n g   i t
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TFlashStore)		// ROM 0x000c4a9c Sizeof__11TFlashStoreSFv
PROTOCOL_CLASSINFO(TFlashStore, "TStore", "", 0, 0, nil)	// ROM 0x003871d4 ClassInfo__11TFlashStoreSFv


// ROM 0x000c51d8 New__11TFlashStoreFv
TFlashStore*
TFlashStore::New(void)
{
	fBlocks = nil;
	fPhysBlocks = nil;
	fBlockStorage = nil;
	fCache = nil;
	fTracker = nil;
	fLockCount = 0;
	fFlash = nil;
	fSocket = -1;
	return this;
}


// ROM 0x000c7c28 Delete__11TFlashStoreFv
void
TFlashStore::Delete(void)
{
	Deinit();
}


// ROM 0x000c6bf4 Init__11TFlashStoreFPvUlT2iT2T1
// The kind of store from the flags - a card, the internal RAM store, or
// (kFlashStoreUsesTFlash) the internal flash, pssInfo being the TFlash -
// then the geometry: blocks of the flash's erase region, ids whose top
// bits say the block they were made in; the blocks, physical blocks,
// lookup cache and tracker made; and the store mounted and checked for a
// transaction to finish.  A store that will not mount is marked as
// needing formatting.
// NOT YET: a card (its pssInfo) and a RAM store (TStoreDriver).
NewtonErr
TFlashStore::Init(void* storeAddress, ULong storeSize, ULong, int socketNumber, ULong flags, void* pssInfo)
{
	fSocket = socketNumber;
	fIsCard = (flags & kFlashStoreIsCard) == kFlashStoreIsCard;
	fUsesTFlash = (flags & kFlashStoreUsesTFlash) == kFlashStoreUsesTFlash;
	TFlash* flash;
	if (!fIsCard)
	{
		flash = nil;
		if (fUsesTFlash)
		{
			fSocket = -1;
			flash = (TFlash*) pssInfo;
		}
	}
	else
		return kError_Call_Not_Implemented;		// NOT YET: the cards (step 5)
	VccOn();
	if (flash != nil)
		flash->AcknowledgeReset();
	fLSN = 0;
	fPhysBlocks = nil;
	fWorkingBlock = nil;
	fBlocks = nil;
	fBlockStorage = nil;
	fCache = nil;
	fUnused38 = 0;
	fAverageEraseCount = 0;
	fEraseInProgress = false;
	fErasingOffset = 0;
	fLockCount = 0;
	fRefs = nil;
	fLastRef = nil;
	fInTransaction = false;
	fNeedsRecovery = false;
	fMountFailed = false;
	fCompactInProgress = false;
	fFormatting = false;
	fSeparateSeen = false;
	fReadOnlyLockCount = 0;
	fFlash = flash;
	fCardHandler = nil;
	fIsSRAM = !fUsesTFlash;
	if (fIsSRAM)
		return kError_Call_Not_Implemented;		// NOT YET: the RAM stores
	fNeedsVpp = !fIsSRAM && (flash->GetAttributes() & kFlashAttrNoVpp) == 0;
	fIsInternalRAM = (flags & kFlashStoreIsInternalRAM) == kFlashStoreIsInternalRAM;
	fIsROMStore = false;
	fBase = (char*) storeAddress;
	fIsMounted = false;
	fZapWord = 0;
	fVirginWord = 0xFFFFFFFF;
	if (flash == nil)
		fBlockSize = fIsInternalRAM ? gInternalBlockSize : gMutableBlockSize;
	else
		fBlockSize = flash->GetEraseRegionSize();
	fBlockShift = CeilLog2(fBlockSize);
	ULong blockSize = fBlockSize;
	fBucketSize = 0x10;
	fQuarterBlock = blockSize >> 2;
	fBlockMask = blockSize - 1;
	fSixteenthBlock = blockSize >> 4;
	fBucketCount = blockSize >> 11;
	fStoreDriver = nil;
	fCachedFree = 1;
	fCompactState = nil;
	fCompactCount = 0;

	if (flash != nil)
		storeSize = flash->GetTotalSize();
	fBlockCount = storeSize / fBlockSize;
	fObjectNumberShift = 0x1C - CeilLog2(fBlockCount);
	NewtonErr err;
	fPhysBlocks = (TFlashPhysBlock*) NewPtr(fBlockCount * sizeof(TFlashPhysBlock));
	if (fPhysBlocks != nil)
	{
		for (ULong i = 0; i < fBlockCount; i++)
			fPhysBlocks[i].Init(this, i * fBlockSize);
		fCache = new TFlashStoreLookupCache;
		if (fCache != nil && (err = fCache->Init(0x40)) != noErr)
			goto done;
		fBlocks = (TFlashBlock**) NewPtr(fBlockCount * sizeof(TFlashBlock*));
		if (fBlocks != nil)
		{
			fBlockStorage = (TFlashBlock*) NewPtrClear(fBlockCount * sizeof(TFlashBlock));
			if (fBlockStorage != nil)
			{
				for (ULong i = 0; i < fBlockCount; i++)
					fBlocks[i] = &fBlockStorage[i];
				InitBlocks();
				fTracker = new TFlashTracker;
				if (fTracker != nil)
				{
					err = fTracker->Init(0x80);
					if (err != noErr)
						goto done;
					if (!fCompactInProgress)
					{
						newton_try
						{
							err = Mount();
							if (err == kSError_NeedsFormat)
								fMountFailed = true;
							else if (err != noErr)
								goto mounted;
							err = RecoveryCheck(false);
							if (err == kSError_NeedsFormat)
							{
								fMountFailed = true;
								err = noErr;
							}
						mounted: ;
						}
						newton_catch(exAbort)
						{
							err = noErr;
						}
						end_try;
					}
					goto done;
				}
			}
		}
	}
	err = MemError();
done:
	VccOff();
	return err;
}


// ROM 0x000c83e8 Deinit__11TFlashStoreFv
// NOT YET: a card's power notification.
void
TFlashStore::Deinit(void)
{
	DisposePtr((Ptr) fBlocks);
	fBlocks = nil;
	DisposePtr((Ptr) fPhysBlocks);
	fPhysBlocks = nil;
	DisposePtr((Ptr) fBlockStorage);
	fBlockStorage = nil;
	if (fCache != nil)
	{
		fCache->Destroy();
		delete fCache;
		fCache = nil;
	}
	if (fTracker != nil)
	{
		delete fTracker;
		fTracker = nil;
	}
}


// ROM 0x000c7650 InitBlocks__11TFlashStoreFv
void
TFlashStore::InitBlocks(void)
{
	for (ULong i = 0; i < fBlockCount; i++)
	{
		fPhysBlocks[i].Init(this, i * fBlockSize);
		fBlocks[i]->Init(this);
	}
}


// ROM 0x000c76b0 Mount__11TFlashStoreFv
// Everything the logs say, read afresh; a block in use after one that is
// not (bar a reserved one) means the store is not what it should be.
NewtonErr
TFlashStore::Mount(void)
{
	InitBlocks();
	fCache->ForgetAll();
	NewtonErr err = ScanLogForLogicalBlocks(&fSeparateSeen);
	if (err == noErr && (err = ScanLogForErasures()) == noErr && (err = ScanLogForReservedBlocks()) == noErr)
	{
		CalcAverageEraseCount();
		Boolean virginSeen = false;
		for (ULong offset = 0; offset < StoreCapacity(); offset += fBlockSize)
		{
			if (!BlockAt(offset)->IsVirgin())
			{
				if (virginSeen && !BlockAt(offset)->IsReserved())
				{
					err = kSError_NeedsFormat;
					break;
				}
			}
			else
				virginSeen = true;
		}
		fIsMounted = true;
	}
	return err;
}


// ROM 0x000c77b8 ScanLogForLogicalBlocks__11TFlashStoreFPUc
// Every 'fblk entry: the latest (by log sequence number) for each logical
// block says where it is.  An entry written with the other polarity makes
// this a ROM store.
NewtonErr
TFlashStore::ScanLogForLogicalBlocks(UChar* separateSeen)
{
	ULong at = 0;
	for (;;)
	{
		NewtonErr err = NextLogEntry(at, &at, kFlashBlockLogTag, nil);
		if (err != noErr)
			return err == kSError_NoMoreObjects ? noErr : err;
		SFlashBlockLogEntry entry;
		ReadWords(at, (FlashWord*) &entry, kFlashBlockLogEntrySize / 4);
		ULong block = entry.fLogicalOffset >> fBlockShift;
		if (fBlockCount <= block)
			return kSError_NeedsFormat;
		ULong existing = fBlocks[block]->LogEntryOffset();
		if (fLSN < entry.fLSN)
			fLSN = entry.fLSN;
		if ((fZapWord & 1) == (entry.fWritable & 1))
			fIsROMStore = true;
		if (existing == 0)
			BlockAt(entry.fLogicalOffset)->SetInfo(&entry, separateSeen);
		else
		{
			SFlashBlockLogEntry other;
			ReadWords(existing, (FlashWord*) &other, kFlashBlockLogEntrySize / 4);
			if (fBlockCount <= other.fLogicalOffset >> fBlockShift)
				return kSError_NeedsFormat;
			if (other.fLSN < entry.fLSN)
				BlockAt(entry.fLogicalOffset)->SetInfo(&entry, nil);
		}
	}
}


// ROM 0x000c7924 ScanLogForErasures__11TFlashStoreFv
NewtonErr
TFlashStore::ScanLogForErasures(void)
{
	ULong at = 0;
	for (;;)
	{
		NewtonErr err = NextLogEntry(at, &at, kFlashEraseLogTag, nil);
		if (err != noErr)
			return err == kSError_NoMoreObjects ? noErr : err;
		SFlashEraseLogEntry entry;
		ReadWords(at, (FlashWord*) &entry, kFlashEraseLogEntrySize / 4);
		entry.fUsable = (UChar) (((FlashWord*) &entry)[9] >> 24);	// the byte after the erase count
		if (fBlockCount <= entry.fPhysOffset >> fBlockShift)
			return kSError_NeedsFormat;
		if (fLSN < entry.fLSN)
			fLSN = entry.fLSN;
		TFlashPhysBlock* phys = PhysBlockAt(entry.fPhysOffset);
		if (phys->LogEntryOffset() == 0)
			phys->SetInfo(&entry);
		else
		{
			SFlashEraseLogEntry other;
			ReadWords(phys->LogEntryOffset(), (FlashWord*) &other, kFlashEraseLogEntrySize / 4);
			if (fBlockCount <= other.fPhysOffset >> fBlockShift)
				return kSError_NeedsFormat;
			if (other.fLSN < entry.fLSN)
				phys->SetInfo(&entry);
		}
	}
}


// ROM 0x000c7a98 ScanLogForReservedBlocks__11TFlashStoreFv
NewtonErr
TFlashStore::ScanLogForReservedBlocks(void)
{
	ULong at = 0;
	NewtonErr err;
	for (;;)
	{
		err = NextLogEntry(at, &at, kReservedBlockLogTag, nil);
		if (err != noErr)
			break;
		SReservedBlockLogEntry entry;
		ReadWords(at, (FlashWord*) &entry, kReservedBlockLogEntrySize / 4);
		ULong existing = BlockAt(entry.fLogicalOffset)->LogEntryOffset();
		if (fLSN < entry.fLSN)
			fLSN = entry.fLSN;
		if (existing == 0)
			BlockAt(entry.fLogicalOffset)->SetInfo(&entry);
		else
		{
			SReservedBlockLogEntry other;
			ReadWords(existing, (FlashWord*) &other, kReservedBlockLogEntrySize / 4);
			if (other.fLSN < entry.fLSN)
				BlockAt(entry.fLogicalOffset)->SetInfo(&entry);
		}
	}
	if (err == kSError_NoMoreObjects)
		err = noErr;
	return err;
}


/*------------------------------------------------------------------------------
	T h e   l o g
------------------------------------------------------------------------------*/

// ROM 0x000c2120 NextLogEntry__11TFlashStoreFUlPUlT1Pv
// The next valid entry (with the tag, unless it is nought) after the one at
// offset (nought: from the first block's log), on word boundaries, block
// by block; from a copy of the store's image if one is given.
NewtonErr
TFlashStore::NextLogEntry(ULong offset, ULong* found, ULong tag, void* image)
{
	ULong pos = (offset == 0) ? fBlockSize - LogSize() : offset + 4;
	for (;;)
	{
		while ((fBlockMask & pos) < fBlockSize - 0x20)
		{
			SFlashLogEntry header;
			if (image == nil)
				ReadWords(pos, (FlashWord*) &header, 8);
			else
			{
				for (int i = 0; i < 8; i++)
					((FlashWord*) &header)[i] = GetBigEndianWord((char*) image + pos + i * 4);
			}
			if (header.IsValid(pos) && (tag == 0 || header.fTag == tag))
			{
				*found = pos;
				return noErr;
			}
			pos += 4;
		}
		pos = (fBlockSize + pos - 1) & ~(fBlockSize - 1);
		ULong end = fBlockCount * fBlockSize;
		if (end < pos || end - pos == 0)
			return kSError_NoMoreObjects;
		pos = (fBlockSize - LogSize()) + pos;
	}
}


// ROM 0x000c7bb0 FindPhysWritable__11TFlashStoreFUlN21
// size bytes of blank words between offset and end; nought if there are none.
ULong
TFlashStore::FindPhysWritable(ULong offset, ULong end, ULong size)
{
	ULong candidate = offset;
	for (;;)
	{
		if (end - size <= offset)
			return 0;
		if (size <= offset - candidate)
			break;
		FlashWord word;
		ReadWords(offset, &word, 1);
		if (fVirginWord != word)
			candidate = offset + 4;
		offset += 4;
	}
	return candidate;
}


// ROM 0x000c2248 AddLogEntryToPhysBlock__11TFlashStoreFUlT1P14SFlashLogEntryT1PUl
// Into the log of the physical block at physOffset: the entry's body first
// and its first word - which makes it valid - last; an entry that does not
// write correctly is zapped and the next blank space tried.
NewtonErr
TFlashStore::AddLogEntryToPhysBlock(ULong tag, ULong size, SFlashLogEntry* entry, ULong physOffset, ULong* entryOffset)
{
	if ((fBlockMask & physOffset) < fBlockSize - LogSize())
		physOffset = (fBlockSize + (physOffset & ~fBlockMask)) - LogSize();
	NewtonErr err;
	for (;;)
	{
		physOffset = FindPhysWritable(physOffset, (fBlockMask + physOffset) & ~fBlockMask, size);
		if (physOffset == 0)
			return kSError_BlockFull;
		entry->fGuard1 = physOffset ^ 'dyer';
		entry->fGuard2 = ~physOffset ^ 'foo!';
		entry->fNewt = 'newt';
		entry->fTag = tag;
		entry->fSize = size;
		entry->fLSN = NextLSN();
		entry->fVirgin = fVirginWord;
		FlashWord flash[0x100 / 4];
		for (ULong i = 0; i < size / 4; i++)
			PutBigEndianWord(&flash[i], ((FlashWord*) entry)[i]);
		if (tag == kFlashEraseLogTag)
		{
			// the erase entry's usable flag is a byte
			UChar* bytes = (UChar*) flash;
			bytes[0x24] = ((SFlashEraseLogEntry*) entry)->fUsable;
			bytes[0x25] = ((SFlashEraseLogEntry*) entry)->fPad[0];
			bytes[0x26] = ((SFlashEraseLogEntry*) entry)->fPad[1];
			bytes[0x27] = ((SFlashEraseLogEntry*) entry)->fPad[2];
		}
		err = BasicWrite(physOffset + 4, (char*) flash + 4, size - 4);
		if (err == noErr)
			err = BasicWrite(physOffset, flash, 4);
		if (err != kSError_WriteError)
			break;
		err = ZapLogEntry(physOffset);
		if (err != noErr)
			return err;
	}
	if (entryOffset != nil)
		*entryOffset = physOffset;
	return err;
}


// ROM 0x000c23a8 ZapLogEntry__11TFlashStoreFUl
// Its first word written to noughts: no longer valid.
NewtonErr
TFlashStore::ZapLogEntry(ULong offset)
{
	if (!fIsSRAM)
		{ FlashWord zap = (FlashWord) fZapWord; return WriteWords(offset, &zap, 1); }
	Boolean done = false;
	do
	{
		newton_try
		{
			fStoreDriver->Set(offset, 4, fZapWord);
			done = true;
		}
		newton_catch(exAbort)
		{
			SendAlertMgrWPBitch(0);
		}
		end_try;
	} while (!done);
	return noErr;
}


// ROM 0x000c512c NextLSN__11TFlashStoreFv
ULong
TFlashStore::NextLSN(void)
{
	return ++fLSN;
}


/*------------------------------------------------------------------------------
	B l o c k s
------------------------------------------------------------------------------*/

// ROM 0x000c50d0 Translate__11TFlashStoreFUl
// A logical offset to where it is: its block's physical block.
ULong
TFlashStore::Translate(ULong offset)
{
	return BlockAt(offset)->fPhysOffset + (fBlockMask & offset);
}


// ROM 0x000c50f4 ExchangeBlock__11TFlashStoreFUlP11TFlashBlock
TFlashBlock*
TFlashStore::ExchangeBlock(ULong offset, TFlashBlock* block)
{
	ULong index = offset >> fBlockShift;
	TFlashBlock* old = fBlocks[index];
	fBlocks[index] = block;
	return old;
}


// ROM 0x000c510c StoreCapacity__11TFlashStoreFv
// On flash one block is kept to compact into.
ULong
TFlashStore::StoreCapacity(void)
{
	ULong capacity = fBlockCount * fBlockSize;
	if (!fIsSRAM)
		capacity -= fBlockSize;
	return capacity;
}


// ROM 0x000c8470 DummyBlock__11TFlashStoreFv
// The last logical block, which on flash is never used.
TFlashBlock*
TFlashStore::DummyBlock(void)
{
	return fBlocks[fBlockCount - 1];
}


// ROM 0x000c8484 ObjectNumberFor__11TFlashStoreFUl
ULong
TFlashStore::ObjectNumberFor(PSSId id)
{
	return id & ~((ULong) 0x0FFFFFFF << fObjectNumberShift);
}


// ROM 0x000c8494 PSSIDFor__11TFlashStoreFlT1
PSSId
TFlashStore::PSSIDFor(long block, long objectNumber)
{
	return (ULong) objectNumber | ((ULong) block << fObjectNumberShift);
}


// ROM 0x000c5174 Add__11TFlashStoreFP7TObjRef
void
TFlashStore::Add(TObjRef* ref)
{
	ref->fNext = fRefs;
	ref->fPrev = nil;
	if (fRefs != nil)
		fRefs->fPrev = ref;
	fRefs = ref;
	if (fLastRef == nil)
		fLastRef = ref;
}


// ROM 0x000c51a4 Remove__11TFlashStoreFP7TObjRef
void
TFlashStore::Remove(TObjRef* ref)
{
	if (ref->fPrev == nil)
		fRefs = ref->fNext;
	else
		ref->fPrev->fNext = ref->fNext;
	if (ref->fNext == nil)
		fLastRef = ref->fPrev;
	else
		ref->fNext->fPrev = ref->fPrev;
}


// ROM 0x000c4fe8 NotifyCompact__11TFlashStoreFP11TFlashBlock
// Every reference in use to an object of the block found again where it
// moved to; and every walk under way told to start again.
void
TFlashStore::NotifyCompact(TFlashBlock* block)
{
	for (TObjRef* ref = fRefs; ref != nil; ref = ref->fNext)
	{
		if (ref->IsValid(this) && (~fBlockMask & ref->fOffset) == (~fBlockMask & block->fLogicalOffset))
			block->Lookup(ref->Id(), ref->State(), *ref, nil);
	}
	fCompactCount++;
}


// ROM 0x000c7794 BlockCompacted__11TFlashStoreFv
void
TFlashStore::BlockCompacted(void)
{
	fCache->ForgetAll();
	CalcAverageEraseCount();
}


// ROM 0x000c8384 CalcAverageEraseCount__11TFlashStoreFv
void
TFlashStore::CalcAverageEraseCount(void)
{
	ULong total = 0;
	for (ULong i = 0; i < fBlockCount; i++)
		total += fPhysBlocks[i].EraseCount();
	fAverageEraseCount = total / fBlockCount;
}


// ROM 0x000c83e0 AverageEraseCount__11TFlashStoreFv
ULong
TFlashStore::AverageEraseCount(void)
{
	return fAverageEraseCount;
}


// ROM 0x000c8204 FindUnusedPhysicalBlock__11TFlashStoreFv
ULong
TFlashStore::FindUnusedPhysicalBlock(void)
{
	ULong blockSize = fBlockSize;
	ULong end = fBlockCount * blockSize;
	for (ULong offset = 0; offset < end; offset += blockSize)
		if (PhysBlockAt(offset)->IsSpare())
			return offset;
	return 0xFFFFFFFF;
}


// ROM 0x000c826c BringVirginBlockOnline__11TFlashStoreFUlT1
// A logical block never used given the erased physical block: its entry,
// its root directory, and the entry written to the block's log.
// (The ROM's entry lies on its stack, so its guard words are whatever was
// there when SetInfo first reads them; the host's are nought.)
NewtonErr
TFlashStore::BringVirginBlockOnline(ULong physOffset, ULong logicalOffset)
{
	TTime now = GetGlobalTime();
	SFlashBlockLogEntry entry;
	memset(&entry, 0, sizeof(entry));
	entry.fEraseCount = 1;
	entry.fLogicalOffset = logicalOffset;
	entry.fUnknown20 = 0;
	entry.fUnknown2C = 0;
	entry.fUnknown30 = 0;
	entry.fPhysOffset = physOffset;
	entry.fRandom = NewtonRand();
	entry.fTime = now.ConvertTo(kSeconds);
	entry.fRootDirectory = logicalOffset + 4;
	entry.fUnknown44 = 0;
	entry.fUnknown48 = 0;
	entry.fWritable = fVirginWord;
	NewtonErr err = BlockAt(logicalOffset)->SetInfo(&entry, nil);
	if (err == noErr)
	{
		ULong rootDirectory = entry.fRootDirectory;
		err = BlockAt(logicalOffset)->WriteRootDirectory(&rootDirectory);
		entry.fRootDirectory = (FlashWord) rootDirectory;
		if (err == noErr)
		{
			err = AddLogEntryToPhysBlock(kFlashBlockLogTag, kFlashBlockLogEntrySize, &entry, physOffset, nil);
			if (err == noErr)
				err = BlockAt(logicalOffset)->SetInfo(&entry, nil);
		}
	}
	return err;
}


// ROM 0x000c0f8c ChooseWorkingBlock__11TFlashStoreFUlT1
// A block with room for an object of size and its directory entries: the
// preferred one, any block in use, a block never used brought online, or
// (on flash) the block compacting would do most for - by what it yields
// and how little it has been erased - compacted into the spare, up to
// twenty times.
// ROM QUIRK: should every block's heuristic be negative, none is chosen
// and block -1 is compacted.
NewtonErr
TFlashStore::ChooseWorkingBlock(ULong size, ULong preferred)
{
	TFlashBlock* virgin = nil;
	ULong virginOffset = 0;
	ULong need = ((size + 3) & ~3) + fBucketSize * 4 + 0x10;
	NewtonErr err;
	if (preferred != 0xFFFFFFFF)
	{
		TFlashBlock* block = BlockAt(preferred);
		if (!block->IsVirgin() && need <= block->Avail())
		{
			fWorkingBlock = block;
			return noErr;
		}
	}
	if (StoreCapacity() != 0)
	{
		ULong offset = 0;
		do
		{
			TFlashBlock* block = BlockAt(offset);
			if (!block->IsVirgin())
			{
				if (need <= block->Avail())
				{
					fWorkingBlock = block;
					return noErr;
				}
			}
			else if (virgin == nil)
			{
				virgin = block;
				virginOffset = offset;
			}
			offset += fBlockSize;
		} while (offset < StoreCapacity());
		if (virgin != nil)
		{
			ULong phys = FindUnusedPhysicalBlock();
			if (!IsErased(phys) && (err = SyncErase(phys)) != noErr)
				return err;
			err = BringVirginBlockOnline(phys, virginOffset);
			if (err == noErr)
			{
				fWorkingBlock = virgin;
				return noErr;
			}
			return err;
		}
	}
	if (!fIsSRAM)
	{
		long tries = 0x13;
		ULong best;
		for (;;)
		{
			Boolean enough = false;
			long bestHeuristic = 0;
			best = 0xFFFFFFFF;
			ULong phys = FindUnusedPhysicalBlock();
			if (StoreCapacity() == 0)
				return kSError_StoreFull;
			ULong offset = 0;
			do
			{
				ULong yield = BlockAt(offset)->Yield();
				long heuristic = BlockAt(offset)->EraseHeuristic(yield);
				if (bestHeuristic <= heuristic)
				{
					bestHeuristic = heuristic;
					best = offset;
				}
				if (need <= yield)
					enough = true;
				offset += fBlockSize;
			} while (offset < StoreCapacity());
			if (!enough)
				return kSError_StoreFull;
			if (!IsErased(phys) && (err = SyncErase(phys)) != noErr)
				return err;
			BlockAt(best)->CompactInto(phys);
			if (!BlockAt(best)->IsReserved() && need <= BlockAt(best)->Avail())
				break;
			Boolean last = (tries == 0);
			tries--;
			if (last)
				return kSError_StoreFull;
		}
		fWorkingBlock = BlockAt(best);
		return noErr;
	}
	return kError_Call_Not_Implemented;		// NOT YET: a RAM store compacts in place
}


/*------------------------------------------------------------------------------
	T h e   f l a s h
------------------------------------------------------------------------------*/

// ROM 0x000c0cf0 VppOn__11TFlashStoreFv
// NOT YET: a card's programming voltage (the internal flash's is switched
// by TNewInternalFlash itself).
NewtonErr
TFlashStore::VppOn(void)
{
	return noErr;
}


// ROM 0x000c0d60 VppOff__11TFlashStoreFv
NewtonErr
TFlashStore::VppOff(void)
{
	return noErr;
}


// ROM 0x000c12f4 VccOn__11TFlashStoreFv
// NOT YET: a card's power.
void
TFlashStore::VccOn(void)
{
}


// ROM 0x000c133c VccOff__11TFlashStoreFv
void
TFlashStore::VccOff(void)
{
}


// ROM 0x000c0d9c EraseStatus__11TFlashStoreFUl
NewtonErr
TFlashStore::EraseStatus(ULong physOffset)
{
	NewtonErr status = fFlash->Status(physOffset);
	if (status == 1)
		return noErr;
	if (status == 3)
		status = kSError_EraseInProgress;
	return status;
}


// ROM 0x000c0dcc WaitForEraseDone__11TFlashStoreFv
NewtonErr
TFlashStore::WaitForEraseDone(void)
{
	if (!fEraseInProgress)
		return noErr;
	while (EraseStatus(fErasingOffset) == kSError_EraseInProgress)
		;
	fEraseInProgress = false;
	NewtonErr status = fFlash->Status(fErasingOffset);
	if (status == 1)
		return noErr;
	if (status == 3)
		status = kSError_EraseInProgress;
	return status;
}


// ROM 0x000c1500 StartErase__11TFlashStoreFUl
NewtonErr
TFlashStore::StartErase(ULong physOffset)
{
	fErasingOffset = physOffset;
	fEraseInProgress = true;
	return fFlash->Erase(physOffset);
}


// ROM 0x000c13dc SyncErase__11TFlashStoreFUl
// Tried four times, a while apart, asking to have the write protection
// taken off meanwhile.
NewtonErr
TFlashStore::SyncErase(ULong physOffset)
{
	NewtonErr err = noErr;
	long tries = 3;
	for (;;)
	{
		if (!fIsSRAM)
		{
			err = WaitForEraseDone();
			if (err == noErr)
				err = StartErase(physOffset);
			if (err == noErr)
				err = WaitForEraseDone();
		}
		else
		{
			newton_try
			{
				fStoreDriver->Set(physOffset, fBlockSize, fVirginWord);
			}
			newton_catch_all
			{
				err = -10059;		// the ROM's -0x274b
			}
			end_try;
		}
		if (err == noErr)
			break;
		::Sleep(0x59fd8);
		if (IsWriteProtected())
			SendAlertMgrWPBitch(0);
		if (--tries < 0)
			return err;
	}
	return noErr;
}


// ROM 0x000c1350 IsErased__11TFlashStoreFUl
// The whole physical block.
Boolean
TFlashStore::IsErased(ULong physOffset)
{
	return IsErased(physOffset & ~fBlockMask, fBlockSize, 0);
}


// ROM 0x000c1364 IsErased__11TFlashStoreFUlN21
// Blank, or blank but for up to `tolerance` bytes; a RAM store never is.
Boolean
TFlashStore::IsErased(ULong offset, ULong size, ULong tolerance)
{
	if (fIsSRAM)
		return false;
	if (fUsesTFlash)
		return fFlash->IsVirgin(offset, size);
	UChar* p = (UChar*) fBase + offset;
	ULong bad = 0;
	for ( ; size != 0; size--, p++)
	{
		if (*p != (fVirginWord & 0xFF) && ++bad > tolerance)
			return false;
	}
	return true;
}


// ROM 0x000c0e20 Zap__11TFlashStoreFUlT1
// Written over with noughts, 64 bytes at a time.
NewtonErr
TFlashStore::Zap(ULong offset, ULong size)
{
	NewtonErr err = noErr;
	if (!fIsSRAM)
	{
		if (size < 5)
		{
			ULong word = fZapWord;
			err = BasicWrite(offset, &word, size);
		}
		else
		{
			UChar noughts[0x40];
			memset(noughts, fZapWord & 0xFF, sizeof(noughts));
			for ( ; size > 0x3F; size -= 0x40)
			{
				err = BasicWrite(offset, noughts, 0x40);
				if (err != noErr)
					return err;
				offset += 0x40;
			}
			err = noErr;
			if (size != 0)
				err = BasicWrite(offset, noughts, size);
		}
	}
	else
	{
		Boolean done = false;
		do
		{
			newton_try
			{
				fStoreDriver->Set(offset, size, fZapWord);
				done = true;
			}
			newton_catch(exAbort)
			{
				SendAlertMgrWPBitch(0);
			}
			end_try;
		} while (!done);
	}
	return err;
}


// ROM 0x000c7c2c BasicWrite__11TFlashStoreFUlPvT1
// Through the TFlash (for a card, checked against its memory afterwards:
// a byte that did not take is kSError_WriteError, and the caller writes it
// somewhere else); write protection that goes away is waited for.
NewtonErr
TFlashStore::BasicWrite(ULong offset, void* buffer, ULong size)
{
	fCachedFree = 1;
	NewtonErr err;
	for (;;)
	{
		err = noErr;
		newton_try
		{
			if (!fIsSRAM)
			{
				if (!fUsesTFlash)
				{
					err = fFlash->Write(offset, size, (char*) buffer);
					if (err == noErr && memcmp(fBase + offset, buffer, size) != 0)
						err = kError_Flash_Write_Failed;
				}
				else
					err = fFlash->Write(offset, size, (char*) buffer);
			}
			else
				fStoreDriver->Write((char*) buffer, offset, size);
		}
		newton_catch(exAbort)
		{
			err = kSError_WriteProtected;
		}
		end_try;
		if (err == noErr)
			return noErr;
		if (err == kError_Flash_Write_Failed)
			return kSError_WriteError;
		if (err != kSError_WriteProtected && err != -10065)
			break;
		SendAlertMgrWPBitch(0);
	}
	return err;
}


// ROM 0x000c7d8c BasicRead__11TFlashStoreFUlPvT1
// (A ROM store is read a word at a time where it can be; the result is the
// same bytes.)
NewtonErr
TFlashStore::BasicRead(ULong offset, void* buffer, ULong size)
{
	if (fIsROMStore)
		BlockMove(fBase + offset, buffer, size);
	else if (fIsInternalRAM)
		fStoreDriver->Read((char*) buffer, offset, size);
	else if (!fUsesTFlash)
		BlockMove(fBase + offset, buffer, size);
	else
		fFlash->Read(offset, size, (char*) buffer);
	return noErr;
}


// ROM 0x000c7f00 BasicCopy__11TFlashStoreFUlN21
// ROM BUG: on a TFlash, or a memory-mapped card, the copy's result is
// dropped.
NewtonErr
TFlashStore::BasicCopy(ULong from, ULong to, ULong size)
{
	NewtonErr err = noErr;
	if (fStoreDriver == nil)
	{
		if (!fUsesTFlash)
			BasicWrite(to, fBase + from, size);
		else
			fFlash->Copy(from, to, size);
	}
	else
	{
		for (;;)
		{
			newton_try
			{
				fStoreDriver->Copy(from, to, size);
			}
			newton_catch(exAbort)
			{
				err = kSError_WriteProtected;
			}
			end_try;
			if (err == noErr)
				break;
			SendAlertMgrWPBitch(0);
		}
	}
	return err;
}


// ROM 0x000c9bb4 IsRangeVirgin__11TFlashStoreFUlT1
Boolean
TFlashStore::IsRangeVirgin(ULong offset, ULong size)
{
	if (fUsesTFlash)
		return fFlash->IsVirgin(Translate(offset), size);
	char* p = fBase + Translate(offset);
	while (size != 0)
	{
		size--;
		if (*p++ != (char) 0xFF)
			return false;
	}
	return true;
}


// ROM 0x000c9b80 TouchMe__11TFlashStoreFv
// A card's first byte read, to keep it awake.
void
TFlashStore::TouchMe(void)
{
	char byte;
	memcpy(&byte, fBase, 1);
	(void) byte;
}


// ROM 0x000c8004 IsWriteProtected__11TFlashStoreFv
// NOT YET: a card with no TFlash asks its card handler.
Boolean
TFlashStore::IsWriteProtected(void)
{
	if (fReadOnlyLockCount != 0)
		return true;
	UChar isProtected = false;
	if (fIsCard && fFlash != nil)
	{
		TouchMe();
		fFlash->GetWriteProtected(&isProtected);
	}
	return isProtected;
}


// NOT YET: the alert asking for a card's write protection to be taken off
// (ROM 0x00277b6c), which waits until it is.
void
TFlashStore::SendAlertMgrWPBitch(int)
{
}


/*------------------------------------------------------------------------------
	S i z e s   a n d   s t a t e
------------------------------------------------------------------------------*/

// ROM 0x000c8fd0 NeedsFormat__11TFlashStoreFPUc
NewtonErr
TFlashStore::NeedsFormat(Boolean* needsFormat)
{
	*needsFormat = InternalNeedsFormat();
	return noErr;
}


// ROM 0x000c80e4 InternalNeedsFormat__11TFlashStoreFv
// Unless a transaction is waiting to be finished: when it would not mount,
// when either of the first two blocks has never been used, or when a block
// in use has no root directory where its log says.
Boolean
TFlashStore::InternalNeedsFormat(void)
{
	if (fNeedsRecovery)
		return false;
	if (fMountFailed || BlockAt(0)->LogEntryOffset() == 0 || BlockAt(0)->IsVirgin() || BlockAt(fBlockSize)->IsVirgin())
		return true;
	for (ULong offset = 0; offset < StoreCapacity(); offset += fBlockSize)
	{
		TFlashBlock* block = BlockAt(offset);
		if (block->IsVirgin())
			return false;
		if (!block->IsReserved())
		{
			SObject object;
			if (block->ReadObjectAt(block->fRootDirectory, &object) != noErr || object.Id() != kFlashRootDirectoryId)
				return true;
		}
	}
	return false;
}


// ROM 0x000c88dc Format__11TFlashStoreFv
// Every log entry zapped but a reserved block's that is marked to be kept,
// the first two physical blocks erased and made logical blocks 0 and 1 with
// their root directories, and the root object (0x27) made.
// NOT YET: a RAM card asks the card server to format it first.
NewtonErr
TFlashStore::Format(void)
{
	TTime now = GetGlobalTime();
	VppOn();
	Boolean readOnly;
	IsReadOnly(&readOnly);
	NewtonErr err;
	if (readOnly)
	{
		err = kSError_WriteProtected;
		goto done;
	}
	fFormatting = true;
	fLockCount = 1;
	if (fFlash != nil)
		fFlash->AcknowledgeReset();
	if (BlockAt(0)->LogEntryOffset() != 0)
		ZapLogEntry(BlockAt(0)->LogEntryOffset());
	{
		ULong at = 0;
		for (;;)
		{
			err = NextLogEntry(at, &at, 0, nil);
			if (err != noErr)
			{
				if (err != kSError_NoMoreObjects)
					goto done;
				break;
			}
			Boolean zap = true;
			SReservedBlockLogEntry entry;
			err = ReadWords(at, (FlashWord*) &entry, 8);
			if (err != noErr)
				goto done;
			if (entry.fTag == kReservedBlockLogTag)
			{
				err = ReadWords(at, (FlashWord*) &entry, kReservedBlockLogEntrySize / 4);
				if (err != noErr)
					goto done;
				if (entry.fUnknown28 & 1)
					zap = false;
			}
			if (zap && (err = ZapLogEntry(at)) != noErr)
				goto done;
		}
	}
	if (!IsErased(0) && (err = SyncErase(0)) != noErr)
		goto done;
	if (!IsErased(fBlockSize) && (err = SyncErase(fBlockSize)) != noErr)
		goto done;
	{
		fLSN = 1;
		SFlashBlockLogEntry entry;
		memset(&entry, 0, sizeof(entry));
		entry.fUnknown20 = 0;
		entry.fPhysOffset = 0;
		entry.fLogicalOffset = 0;
		entry.fEraseCount = 1;
		entry.fUnknown2C = 0;
		entry.fUnknown30 = 0;
		entry.fRandom = NewtonRand();
		entry.fTime = now.ConvertTo(kSeconds);
		entry.fRootDirectory = entry.fLogicalOffset + 4;
		entry.fUnknown44 = 0;
		entry.fUnknown48 = 0;
		entry.fWritable = fVirginWord;
		err = AddLogEntryToPhysBlock(kFlashBlockLogTag, kFlashBlockLogEntrySize, &entry, 0, nil);
		if (err != noErr)
			goto done;
		entry.fPhysOffset = fBlockSize;
		entry.fLogicalOffset = fBlockSize;
		entry.fRandom = NewtonRand();
		entry.fTime = now.ConvertTo(kSeconds);
		entry.fRootDirectory = entry.fLogicalOffset + 4;
		err = AddLogEntryToPhysBlock(kFlashBlockLogTag, kFlashBlockLogEntrySize, &entry, fBlockSize, nil);
		if (err != noErr)
			goto done;
	}
	if ((err = Mount()) != noErr
	 || (err = BlockAt(0)->WriteRootDirectory(nil)) != noErr
	 || (err = BlockAt(fBlockSize)->WriteRootDirectory(nil)) != noErr
	 || (err = Mount()) != noErr)
		goto done;
	{
		TObjRef root;
		root.Init(this);
		err = BlockAt(0)->AddObject(kFlashRootObjectId, State(kRAMObjCommitted), 0, root, false, false);
		Remove(&root);
		if (err != noErr)
			goto done;
	}
	fInTransaction = false;
	fNeedsRecovery = false;
	fCompactInProgress = false;
	fTracker->fCount = 0;
	fTracker->fOverflowed = false;
	fTracker->fNesting = 0;
	fWorkingBlock = nil;
	fMountFailed = false;
done:
	fFormatting = false;
	fLockCount = 0;
	VppOff();
	return err;
}


// ROM 0x000c84a0 GetStoreSizes__11TFlashStoreFPlT1
// The total less every block's overhead and the slop; what is used worked
// out block by block when anything has been written since.
NewtonErr
TFlashStore::GetStoreSizes(long* totalSize, long* usedSize)
{
	long overhead = BlockAt(0)->RootDirSize() + LogSize() + 0xC;
	*totalSize = (long) StoreCapacity() - overhead * (long) fBlockCount - (long) InternalStoreSlop();
	if (fCachedFree == 1)
	{
		long blockSize = fBlockSize;
		VccOn();
		*usedSize = 0;
		for (ULong offset = 0; offset < StoreCapacity(); offset += fBlockSize)
		{
			if (!BlockAt(offset)->IsVirgin())
				*usedSize = ((blockSize - overhead) - (long) BlockAt(offset)->CalcRecoverableBytes()) + *usedSize;
		}
		VccOff();
		if (*totalSize < *usedSize)
			*usedSize = *totalSize;
		fCachedFree = *usedSize;
	}
	else
		*usedSize = fCachedFree;
	return noErr;
}


// ROM 0x000c85c4 Avail__11TFlashStoreFv
ULong
TFlashStore::Avail(void)
{
	ULong avail = 0;
	for (ULong offset = 0; offset < StoreCapacity(); offset += fBlockSize)
		avail += BlockAt(offset)->Yield();
	if (avail < InternalStoreSlop() && fIsSRAM)
	{
		GC();
		avail = 0;
		for (ULong offset = 0; offset < StoreCapacity(); offset += fBlockSize)
			avail += BlockAt(offset)->Yield();
	}
	return avail;
}


// ROM 0x000ca7b4 InternalStoreSlop__11TFlashStoreFv
// What is kept back so that a transaction can always be finished.
ULong
TFlashStore::InternalStoreSlop(void)
{
	return fUsesTFlash ? gInternalFlashStoreSlop : 0x1800;
}


// ROM 0x000c868c ValidateIncomingPSSID__11TFlashStoreFUl
NewtonErr
TFlashStore::ValidateIncomingPSSID(PSSId id)
{
	if (id != 0 && (id >> fObjectNumberShift) < fBlockCount)
		return noErr;
	return kSError_BadPSSID;
}


// ROM 0x000c86b4 IsReadOnly__11TFlashStoreFPUc
NewtonErr
TFlashStore::IsReadOnly(Boolean* isReadOnly)
{
	VccOn();
	Boolean cardSaysSo = fIsCard && fFlash != nil && (fFlash->GetAttributes() & 0x80) != 0;
	*isReadOnly = cardSaysSo || IsROM() || IsWriteProtected();
	VccOff();
	return noErr;
}


// ROM 0x000c8ff0 IsROM__11TFlashStoreFv
Boolean		TFlashStore::IsROM(void)							{ return fIsROMStore; }
// ROM 0x000c8ef0 IsLocked__11TFlashStoreFv
Boolean		TFlashStore::IsLocked(void)							{ return fLockCount != 0; }
// ROM 0x000c9ba4 GetRootId__11TFlashStoreFPUl
NewtonErr	TFlashStore::GetRootId(PSSId* rootId)				{ *rootId = kFlashRootObjectId; return noErr; }
// ROM 0x000c8f04 NextObject__11TFlashStoreFUlPUl
NewtonErr	TFlashStore::NextObject(PSSId, PSSId*)				{ return noErr; }
// ROM 0x000c8f0c CheckIntegrity__11TFlashStoreFPUl
NewtonErr	TFlashStore::CheckIntegrity(ULong*)					{ return noErr; }
// ROM 0x000c8f14 SetBuddy__11TFlashStoreFP6TStore
NewtonErr	TFlashStore::SetBuddy(TStore*)						{ return noErr; }
// ROM 0x000c8fa8 SetStore__11TFlashStoreFP6TStoreUl
NewtonErr	TFlashStore::SetStore(TStore*, ULong)				{ return noErr; }
// ROM 0x000c8fb0 OwnsObject__11TFlashStoreFUl
Boolean		TFlashStore::OwnsObject(PSSId)						{ return true; }
// ROM 0x000c8fb8 Sleep__11TFlashStoreFv
NewtonErr	TFlashStore::Sleep(void)							{ return noErr; }
// ROM 0x000c8fc0 Idle__11TFlashStoreFPUcT1
NewtonErr	TFlashStore::Idle(Boolean*, Boolean*)				{ return noErr; }
// ROM 0x000c8fc8 Address__11TFlashStoreFUl
void*		TFlashStore::Address(PSSId)							{ return nil; }
// ROM 0x000c544c EraseObject__11TFlashStoreFUl
NewtonErr	TFlashStore::EraseObject(PSSId)						{ return noErr; }


// ROM 0x000c8f1c StoreKind__11TFlashStoreFv
const char*
TFlashStore::StoreKind(void)
{
	if (!fIsCard)
		return "Internal";
	if (IsROM())
		return "Application card";
	return fIsSRAM ? "Storage card" : "Flash storage card";
}


// ROM 0x000c7360 IsSameStore__11TFlashStoreFPvUl
// Whether an image (a card's first bytes, say) is this store: every
// logical block its log names that this store has in use must carry the
// same format stamp.  While formatting, anything is.
Boolean
TFlashStore::IsSameStore(void* data, ULong)
{
	Boolean same = true;
	VccOn();
	if (!fFormatting)
	{
		newton_try
		{
			ULong at = 0;
			for (;;)
			{
				if (NextLogEntry(at, &at, kFlashBlockLogTag, data) == kSError_NoMoreObjects)
					break;
				const char* entry = (const char*) data + at;
				TFlashBlock* block = fBlocks[GetBigEndianWord(entry + 0x28) >> fBlockShift];
				if (block != nil && block->fStoreId != 0
				 && block->fStoreId != (GetBigEndianWord(entry + 0x38) ^ GetBigEndianWord(entry + 0x34)))
				{
					same = false;
					break;
				}
			}
		}
		newton_catch(exAbort)
		{
			same = false;
		}
		end_try;
	}
	VccOff();
	return same;
}


/*------------------------------------------------------------------------------
	T h e   t r a n s a c t i o n   r e c o r d
	On flash, object 0x17: four bytes, all ones while a transaction is under
	way, written to noughts at its commit point.
------------------------------------------------------------------------------*/

// ROM 0x000c9190 TransactionState__11TFlashStoreFPi
// 0 none, 1 under way, 2 past its commit point.
NewtonErr
TFlashStore::TransactionState(int* state)
{
	NewtonErr err;
	if (!fIsSRAM)
	{
		TObjRef record;
		record.Init(this);
		err = Lookup(kFlashTransactionId, kObjNoState, record);
		if (err == noErr)
		{
			UChar bytes[4];
			err = record.Read(bytes, 0, 4);
			if (err == noErr)
			{
				ULong word = GetBigEndianWord(bytes);
				*state = (word == 0 || (word & (word - 1)) == 0) ? 2 : 1;
			}
		}
		else if (err == kSError_ObjectNotFound)
		{
			err = noErr;
			*state = 0;
		}
		Remove(&record);
	}
	else
	{
		err = noErr;
		*state = fCompactState->fTransactionState;
	}
	return err;
}


// ROM 0x000c9288 StartTransaction__11TFlashStoreFv
NewtonErr
TFlashStore::StartTransaction(void)
{
	if (!fIsSRAM)
	{
		TObjRef record;
		record.Init(this);
		NewtonErr err = AddObject(kFlashTransactionId, State(kRAMObjNew), 4, record, false, false);
		Remove(&record);
		if (err != noErr)
			return err;
	}
	else
		fCompactState->fTransactionState = 1;
	fInTransaction = true;
	return noErr;
}


// ROM 0x000c933c DeleteTransactionRecord__11TFlashStoreFv
NewtonErr
TFlashStore::DeleteTransactionRecord(void)
{
	if (!fIsSRAM)
	{
		TObjRef record;
		record.Init(this);
		NewtonErr err = Lookup(kFlashTransactionId, kObjNoState, record);
		if (err == kSError_ObjectNotFound)
			err = noErr;
		else if (err == noErr)
			err = record.Delete();
		Remove(&record);
		if (err != noErr)
			return err;
	}
	else
		fCompactState->fTransactionState = 0;
	fInTransaction = false;
	fNeedsRecovery = false;
	fTracker->fCount = 0;
	fTracker->fOverflowed = false;
	fTracker->fNesting = 0;
	fSeparateSeen = false;
	return noErr;
}


// ROM 0x000c9410 MarkCommitPoint__11TFlashStoreFv
NewtonErr
TFlashStore::MarkCommitPoint(void)
{
	if (!fIsSRAM)
	{
		TObjRef record;
		record.Init(this);
		NewtonErr err = Lookup(kFlashTransactionId, kObjNoState, record);
		FlashWord word;
		if (err == noErr)
			err = record.Read(&word, 0, 4);
		if (err == noErr)
		{
			word = fZapWord;
			err = record.Write(&word, 0, 4);
		}
		Remove(&record);
		if (err != noErr)
			return err;
	}
	else
		fCompactState->fTransactionState = 2;
	fNeedsRecovery = true;
	return noErr;
}


// ROM 0x000c8ff8 RecoveryCheck__11TFlashStoreFUc
// A transaction left under way is aborted, one past its commit point
// finished; so are separate transactions.  Only noted unless doRecovery.
NewtonErr
TFlashStore::RecoveryCheck(UChar doRecovery)
{
	TObjRef ref;
	ref.Init(this);
	VppOn();
	if (fCompactInProgress)
	{
		LowLevelRecovery();
		Mount();
	}
	int state;
	NewtonErr err = TransactionState(&state);
	if (err != noErr)
	{
		VppOff();
		Remove(&ref);
		return err;
	}
	if (state == 0 && !fSeparateSeen)
	{
		fNeedsRecovery = false;
		VppOff();
		Remove(&ref);
		return noErr;
	}
	fNeedsRecovery = true;
	if (!doRecovery)
	{
		VppOff();
		Remove(&ref);
		return noErr;
	}
	Boolean readOnly;
	IsReadOnly(&readOnly);
	if (readOnly)
	{
		VppOff();
		Remove(&ref);
		return kSError_WPButNeedsRepair;
	}
	err = noErr;
	if (state == 0)
	{
		if (fSeparateSeen)
			err = DoAbort(true);
	}
	else if (state == 1)
		err = DoAbort(true);
	else if (state == 2)
	{
		Boolean separate = fSeparateSeen;
		err = DoCommit(false);
		if (err == noErr && separate)
			err = DoAbort(true);
	}
	VppOff();
	Remove(&ref);
	return err;
}


// ROM 0x000c9a50 LowLevelRecovery__11TFlashStoreFv
// NOT YET: a RAM store's compaction interrupted by a reboot is finished.
NewtonErr
TFlashStore::LowLevelRecovery(void)
{
	fCompactInProgress = false;
	return noErr;
}


// ROM 0x000c9c2c GC__11TFlashStoreFv
// A RAM store's blocks compacted in place (NOT YET there: see TFlashBlock).
void
TFlashStore::GC(void)
{
	if (!fIsSRAM)
		return;
	for (ULong offset = 0; offset < StoreCapacity(); offset += fBlockSize)
		if (!BlockAt(offset)->IsVirgin())
			BlockAt(offset)->CompactInPlace();
}
