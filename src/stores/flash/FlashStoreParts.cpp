/*
	File:		stores/flash/FlashStoreParts.cpp

	Contains:	The flash store's small parts (FlashStore.h): the header and
				directory entry tests, the log entries' guards, the lookup
				cache, the tracker, the compaction state, and TObjRef.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "FlashStore.h"
#include "NewtonMemory.h"
#include "NewtonDebug.h"

#include <string.h>


/*------------------------------------------------------------------------------
	S O b j e c t ,   S D i r E n t ,   i d s
------------------------------------------------------------------------------*/

// ROM 0x000c509c IsValidPSSID__FUl
// Not nought, not all ones, and more than one bit set and clear: an id
// that a single bit going wrong on the flash could turn into another is
// never handed out.
Boolean
IsValidPSSID(PSSId id)
{
	if (id == 0 || (id & (id - 1)) == 0 || id == 0x0FFFFFFF)
		return false;
	return ((~id - 1) & ~id) != 0;
}


// ROM 0x000c4e84 IsValid__7SObjectFP11TFlashStore
// A header that has been written (bit 0 of its second word cleared) with
// an id that could be one.
Boolean
SObject::IsValid(TFlashStore* store)
{
	if ((store->fVirginWord & 1) == (fWord1 & 1))
		return false;
	return IsValidPSSID(fWord0 & 0x0FFFFFFF);
}


// ROM 0x000c4eb0 ObjectStateToTransBits__FiP11TFlashStore
// The bits a state is written as: its code's complement, since on flash
// they are cleared rather than set.
ULong
ObjectStateToTransBits(int state, TFlashStore*)
{
	return ~(ULong) gObjectStateToTransBits[state];
}


// ROM 0x000c4f48 IsValid__7SDirEntFP11TFlashStore
// Neither blank (all ones) nor zapped (noughts), and written.
Boolean
SDirEnt::IsValid(TFlashStore* store)
{
	if ((fWord & 0xFFFFFF00) == 0 || (fWord >> 8) == 0x00FFFFFF)
		return false;
	return (store->fZapWord & 1) == ((fWord & 0xF) >> 3);
}


// ROM 0x000c4f7c IsValidMigratedObjectInfo__7SDirEntSFlT1
// An object number that fits its fourteen bits and a block that fits its
// ten: `subs r12,r0,#0x3fc0; cmpge r12,#0x3f` leaves "le" for every number
// up to 0x3fff (below 0x3fc0 the subtraction is already negative), and
// `cmple r1,#0x3ff` then passes blocks up to 0x3ff.  (This read "< 0x3FC0
// && < 0x3FF" until 2026-09-30, which dropped the migration entries of
// numbers 0x3fc0-0x3fff and of block 0x3ff.)  So at most 1024 blocks can
// be named - 128 MB of 128 KB blocks; an object that migrates to a block
// past that, or whose number is past 0x3fff, is left without an entry and
// found by TFlashStore::Lookup's search of every block.
Boolean
SDirEnt::IsValidMigratedObjectInfo(long objectNumber, long block)
{
	return objectNumber <= 0x3FFF && block <= 0x3FF;
}


// ROM 0x000c4fa0 SetMigratedObjectInfo__7SDirEntFlT1
void
SDirEnt::SetMigratedObjectInfo(long objectNumber, long block)
{
	fWord = (fWord & 0xFF) | (((ULong) objectNumber | ((ULong) block << 14)) << 8);
}


// ROM 0x000c4fb8 GetMigratedObjectInfo__7SDirEntCFPlT1
void
SDirEnt::GetMigratedObjectInfo(long* objectNumber, long* block) const
{
	*block = (long) (fWord >> 8) >> 14;
	*objectNumber = (fWord >> 8) & 0x3FFF;
}


// ROM 0x000c4fd8 HashPSSID__FUl
ULong
HashPSSID(PSSId id)
{
	return id ^ (id >> 24) ^ (id >> 16) ^ (id >> 8);
}


// ROM 0x000c5140 CeilLog2__FUl
ULong
CeilLog2(ULong value)
{
	ULong log = 0;
	for (ULong v = value; v > 1; v >>= 1)
		log++;
	if (((ULong) 1 << log) < value)
		log++;
	return log;
}


/*------------------------------------------------------------------------------
	S F l a s h L o g E n t r y
------------------------------------------------------------------------------*/

// ROM 0x000c4ec8 PrivateFlashLogEntryIsValid__FP14SFlashLogEntryUl
// The entry at offset guards itself with its own address, twice, and says
// "newt"; and no entry is as big as 256 bytes.
Boolean
PrivateFlashLogEntryIsValid(SFlashLogEntry* entry, ULong offset)
{
	return entry->fGuard1 == (FlashWord) (offset ^ 'dyer')
		&& entry->fGuard2 == (FlashWord) (~offset ^ 'foo!')
		&& entry->fNewt == 'newt'
		&& entry->fSize < 0x100;
}


// ROM 0x000c4ec4 IsValid__14SFlashLogEntryFUl
Boolean
SFlashLogEntry::IsValid(ULong offset)
{
	return PrivateFlashLogEntryIsValid(this, offset);
}


// ROM 0x000c4f30 PrivateFlashLogEntryPhysOffset__FP14SFlashLogEntry
// Where the entry itself is.
ULong
PrivateFlashLogEntryPhysOffset(SFlashLogEntry* entry)
{
	return entry->fGuard1 ^ 'dyer';
}


// ROM 0x000c4f2c PhysOffset__14SFlashLogEntryFv
ULong
SFlashLogEntry::PhysOffset(void)
{
	return PrivateFlashLogEntryPhysOffset(this);
}


/*------------------------------------------------------------------------------
	S C o m p a c t S t a t e
------------------------------------------------------------------------------*/

// ROM 0x00070ed4 Init__13SCompactStateFv
void
SCompactState::Init(void)
{
	memset(this, 0, 100);
	fStep = 0;
	fMagicKey = 'bltg';
	fOtherMagicKey = 'zarf';
}


// ROM 0x00070f14 IsValid__13SCompactStateFv
Boolean
SCompactState::IsValid(void)
{
	return fMagicKey == 'bltg' && fOtherMagicKey == 'zarf';
}


// ROM 0x00070f44 InProgress__13SCompactStateFv
Boolean
SCompactState::InProgress(void)
{
	return IsValid() && fStep != 0;
}


/*------------------------------------------------------------------------------
	T S t o r e D r i v e r
	NOT YET but for Init: only a RAM store reaches one, and TFlashStore::Init
	refuses those.
------------------------------------------------------------------------------*/

// ROM 0x001faf08 Init__12TStoreDriverFPcUlT1T2
void
TStoreDriver::Init(char* base, ULong size, char* persistentBase, ULong persistentSize)
{
	fBase = base;
	fSize = size;
	fPersistentBase = persistentBase;
	fPersistentSize = persistentSize;
	fTotalSize = size + persistentSize;
}

static void
StoreDriverNotYet(void)
{
	DebugStr("TStoreDriver: the RAM stores are NOT YET");
}

void	TStoreDriver::Read(char*, ULong, ULong)			{ StoreDriverNotYet(); }
void	TStoreDriver::Write(char*, ULong, ULong)		{ StoreDriverNotYet(); }
void	TStoreDriver::Set(ULong, ULong, ULong)			{ StoreDriverNotYet(); }
void	TStoreDriver::Copy(ULong, ULong, ULong)			{ StoreDriverNotYet(); }
void	TStoreDriver::PersistentCopy(ULong, ULong, ULong)	{ StoreDriverNotYet(); }
void	TStoreDriver::ContinuePersistentCopy(void)		{ StoreDriverNotYet(); }


/*------------------------------------------------------------------------------
	T F l a s h S t o r e L o o k u p C a c h e
------------------------------------------------------------------------------*/

// ROM 0x000c4aa4 Matches__27SFlashStoreLookupCacheEntryFUli
// The same id in the same state; or, asked for state 0 (whatever a reader
// should see), in a state a reader sees.
Boolean
SFlashStoreLookupCacheEntry::Matches(PSSId id, int state)
{
	if (fId != id)
		return false;
	if (fState == state)
		return true;
	if (state == kObjNoState)
	{
		switch (fState)
		{
		case kRAMObjNew:
		case kRAMObjCommitted:
		case kRAMObjSuperceder:
		case kFlashObjNew:
		case kFlashObjCommitted:
		case kFlashObjSuperceder:
			return true;
		}
	}
	return false;
}


// The set an id's entries go in.
// ROM BUG: the set is the hash masked with ~fWays where ~(fWays - 1) was
// meant, so only bit 3 is cleared and the eight-entry sets overlap.
static inline SFlashStoreLookupCacheEntry*
CacheSet(TFlashStoreLookupCache* cache, PSSId id)
{
	return cache->fEntries + (HashPSSID(id) & (cache->fSize - 1) & ~cache->fWays);
}


// ROM 0x000c4b10 Init__22TFlashStoreLookupCacheFUl
NewtonErr
TFlashStoreLookupCache::Init(ULong size)
{
	fSize = size;
	fNext = 0;
	fWays = 8;
	fEntries = (SFlashStoreLookupCacheEntry*) NewPtr(size * sizeof(SFlashStoreLookupCacheEntry));
	if (fEntries == nil)
		return -1;
	memset(fEntries, 0, fSize * sizeof(SFlashStoreLookupCacheEntry));
	return noErr;
}


// ROM 0x000c4bc8 Destroy__22TFlashStoreLookupCacheFv
void
TFlashStoreLookupCache::Destroy(void)
{
	DisposePtr((Ptr) fEntries);
	fEntries = nil;
}


// ROM 0x000c4bec Lookup__22TFlashStoreLookupCacheFUli
ULong
TFlashStoreLookupCache::Lookup(PSSId id, int state)
{
	SFlashStoreLookupCacheEntry* set = CacheSet(this, id);
	for (ULong i = 0; i < fWays; i++)
		if (set[i].Matches(id, state))
			return set[i].fDirEntOffset;
	return 0xFFFFFFFF;
}


// ROM 0x000c4c78 Add__22TFlashStoreLookupCacheFUlT1i
// An entry that matches is brought up to date; otherwise the next way in
// turn is replaced (the turn is the cache's, not the set's).
void
TFlashStoreLookupCache::Add(PSSId id, ULong dirEntOffset, int state)
{
	SFlashStoreLookupCacheEntry* set = CacheSet(this, id);
	for (ULong i = 0; i < fWays; i++)
	{
		if (set[i].Matches(id, state))
		{
			set[i].fDirEntOffset = dirEntOffset;
			return;
		}
	}
	set[fNext].fId = id;
	set[fNext].fState = state;
	set[fNext].fDirEntOffset = dirEntOffset;
	fNext = (fNext + 1) & (fWays - 1);
}


// ROM 0x000c4e28 Add__22TFlashStoreLookupCacheFR7TObjRef
void
TFlashStoreLookupCache::Add(TObjRef& ref)
{
	int state = ref.State();
	ULong dirEntOffset = ref.GetDirEntOffset();
	Add(ref.Id(), dirEntOffset, state);
}


// ROM 0x000c4d48 Forget__22TFlashStoreLookupCacheFUli
// Every entry for the id, whatever its state.
void
TFlashStoreLookupCache::Forget(PSSId id, int)
{
	SFlashStoreLookupCacheEntry* set = CacheSet(this, id);
	for (ULong i = 0; i < fWays; i++)
	{
		if (set[i].fId == id)
		{
			set[i].fId = 0;
			set[i].fDirEntOffset = 0;
		}
	}
}


// ROM 0x000c4dc4 Change__22TFlashStoreLookupCacheFUlT1i
void
TFlashStoreLookupCache::Change(PSSId id, ULong dirEntOffset, int state)
{
	HashPSSID(id);
	Forget(id, state);
	Add(id, dirEntOffset, state);
}


// ROM 0x000c4b6c Change__22TFlashStoreLookupCacheFR7TObjRef
void
TFlashStoreLookupCache::Change(TObjRef& ref)
{
	int state = ref.State();
	ULong dirEntOffset = ref.GetDirEntOffset();
	PSSId id = ref.Id();
	HashPSSID(id);
	Forget(id, state);
	Add(id, dirEntOffset, state);
}


// ROM 0x000c4e10 ForgetAll__22TFlashStoreLookupCacheFv
void
TFlashStoreLookupCache::ForgetAll(void)
{
	memset(fEntries, 0, fSize * sizeof(SFlashStoreLookupCacheEntry));
}


/*------------------------------------------------------------------------------
	T F l a s h T r a c k e r
------------------------------------------------------------------------------*/

// ROM 0x000cadbc __ct__13TFlashTrackerFv
TFlashTracker::TFlashTracker()
{
	fSize = 0;
	fCount = 0;
	fIds = nil;
	Init(0x80);
}


// ROM 0x000cae04 __dt__13TFlashTrackerFv
TFlashTracker::~TFlashTracker()
{
	Deinit();
}


// ROM 0x000cae30 Init__13TFlashTrackerFUl
// ROM QUIRK: the count is not set back (only the constructor clears it).
NewtonErr
TFlashTracker::Init(ULong size)
{
	Deinit();
	fOverflowed = false;
	fNesting = 0;
	fSize = size;
	fIds = (PSSId*) NewPtr(size * sizeof(PSSId));
	return MemError();
}


// ROM 0x000cae6c Deinit__13TFlashTrackerFv
void
TFlashTracker::Deinit(void)
{
	DisposePtr((Ptr) fIds);
	fIds = nil;
}


// ROM 0x000cae90 Add__13TFlashTrackerFUl
// Once it is full the commit has to walk the whole store.
void
TFlashTracker::Add(PSSId id)
{
	if (fOverflowed)
		return;
	fIds[fCount++] = id;
	if (fSize <= fCount)
		fOverflowed = true;
}


// ROM 0x000caec8 Remove__13TFlashTrackerFUl
void
TFlashTracker::Remove(PSSId id)
{
	for (ULong i = 0; i < fCount; i++)
	{
		if (fIds[i] == id)
		{
			fIds[i] = 0xFFFFFFFF;
			return;
		}
	}
}


/*------------------------------------------------------------------------------
	T O b j R e f
------------------------------------------------------------------------------*/

void
TObjRef::Init(TFlashStore* store)
{
	fWord0 = store->fVirginWord & 0x0FFFFFFF;
	fWord1 = 0;
	fOffset = 0xFFFFFFFF;
	fDirEntOffset = 0xFFFFFFFF;
	fStore = store;
	store->Add(this);
}


int
TObjRef::State(void)
{
	return gObjectTransBitsToState[(fStore->fVirginWord ^ ((fWord1 & 0xFFFF) >> 8)) & 0xFF];
}


// ROM 0x001482fc __as__7TObjRefFRC7TObjRef
// The header and where it is; not the store or the list.
TObjRef&
TObjRef::operator=(const TObjRef& other)
{
	fWord0 = other.fWord0;
	fWord1 = other.fWord1;
	fOffset = other.fOffset;
	fDirEntOffset = other.fDirEntOffset;
	return *this;
}


// ROM 0x00148318 Set__7TObjRefFUlT1
// The object at offset, or the one its directory entry points at, read.
// (The ROM has BasicRead inline.)
NewtonErr
TObjRef::Set(ULong offset, ULong dirEntOffset)
{
	fOffset = offset;
	fDirEntOffset = dirEntOffset;
	if (offset == 0xFFFFFFFF && dirEntOffset != 0xFFFFFFFF)
	{
		FlashWord entry;
		fStore->ReadWords(fStore->Translate(dirEntOffset), &entry, 1);
		fOffset = (entry >> 8) << 2;
	}
	fStore->ReadWords(fStore->Translate(fOffset), &fWord0, 2);
	return noErr;
}


// ROM 0x001483a4 FindSuperceeded__7TObjRefFR7TObjRef
// The committed object this copy replaces.  (The ROM has the store's
// Lookup inline.)
NewtonErr
TObjRef::FindSuperceeded(TObjRef& superceded)
{
	return fStore->Lookup(Id(), fStore->State(kRAMObjSuperceded), superceded);
}


// ROM 0x001488a0 FindSuperceeder__7TObjRefFR7TObjRef
// The copy that replaces this committed object.  (Lookup inline again.)
NewtonErr
TObjRef::FindSuperceeder(TObjRef& superceder)
{
	return fStore->Lookup(Id(), fStore->State(kRAMObjSuperceder), superceder);
}


// ROM 0x001483d0 Delete__7TObjRefFv
// Its directory entry and its header zapped.
NewtonErr
TObjRef::Delete(void)
{
	ULong dirEntOffset = GetDirEntOffset();
	if (dirEntOffset != 0xFFFFFFFF)
		fStore->BlockAt(dirEntOffset)->ZapDirEnt(dirEntOffset);
	fStore->BlockAt(fOffset)->ZapObject(fOffset);
	fWord0 |= 0x0FFFFFFF;
	return noErr;
}


// ROM 0x00148444 Write__7TObjRefFPvUlT2
// (The ROM has the store's BasicWrite inline.)
NewtonErr
TObjRef::Write(void* buffer, ULong offset, ULong size)
{
	return fStore->BasicWrite(fStore->Translate(fOffset + offset + 8), buffer, size);
}


// ROM 0x00148488 Read__7TObjRefFPvUlT2
// (BasicRead inline.)
NewtonErr
TObjRef::Read(void* buffer, ULong offset, ULong size)
{
	fStore->BasicRead(fStore->Translate(fOffset + offset + 8), buffer, size);
	return noErr;
}


// ROM 0x001484cc SetSeparateTranny__7TObjRefFv
// Into a transaction of its own: a header whose two bits are still both
// set has one cleared; one that has been through a separate transaction
// already cannot go back, so it is copied and the copy put in one.
NewtonErr
TObjRef::SetSeparateTranny(void)
{
	NewtonErr err = noErr;
	ULong bits = SeparateBits();
	if (bits == 0)
	{
		TObjRef copy;
		copy.Init(fStore);
		err = Clone(State(), copy, true);
		if (err == noErr)
		{
			Delete();
			*this = copy;
		}
		fStore->Remove(&copy);
	}
	else if (bits == 3)
	{
		fWord1 = (fWord1 & ~6) | 4;
		err = ReWriteObjHeader();
	}
	return err;
}


// ROM 0x001485b0 ClearSeparateTranny__7TObjRefFv
NewtonErr
TObjRef::ClearSeparateTranny(void)
{
	if (SeparateBits() == 2)
	{
		fWord1 &= ~6;
		return ReWriteObjHeader();
	}
	return noErr;
}


// ROM 0x001485f0 ReWriteObjHeader__7TObjRefFv
// (BasicWrite inline.)
NewtonErr
TObjRef::ReWriteObjHeader(void)
{
	return fStore->WriteWords(fStore->Translate(fOffset), &fWord0, 2);
}


// ROM 0x00148624 GetDirEntOffset__7TObjRefFv
// Found by looking the object up again when it was reached by its header.
ULong
TObjRef::GetDirEntOffset(void)
{
	if (fDirEntOffset == 0xFFFFFFFF)
	{
		TObjRef found;
		found.Init(fStore);
		if (fStore->Lookup(Id(), State(), found) == noErr)
			fDirEntOffset = found.fDirEntOffset;
		fStore->Remove(&found);
	}
	return fDirEntOffset;
}


// ROM 0x001486c4 CloneEmpty__7TObjRefFiUlR7TObjRefUc
// A new object with the same id, in the state given, not yet filled.
NewtonErr
TObjRef::CloneEmpty(int state, ULong size, TObjRef& clone, UChar separate)
{
	return fStore->AddObject(Id(), state, size, clone, separate, false);
}


// ROM 0x00148708 CloneEmpty__7TObjRefFiR7TObjRefUc
NewtonErr
TObjRef::CloneEmpty(int state, TObjRef& clone, UChar separate)
{
	return CloneEmpty(state, Size(), clone, separate);
}


// ROM 0x00148734 Clone__7TObjRefFiR7TObjRefUc
// Copied whole; a copy whose bytes did not all go down is thrown away and
// made again somewhere else.
NewtonErr
TObjRef::Clone(int state, TObjRef& clone, UChar separate)
{
	NewtonErr err;
	for (;;)
	{
		err = CloneEmpty(state, clone, separate);
		if (err == noErr)
			err = CopyTo(clone, 0, Size());
		if (err != kSError_WriteError)
			break;
		clone.Delete();
	}
	return err;
}


// ROM 0x001487a0 CopyTo__7TObjRefFR7TObjRefUlT2
// Part of this object's data into another's.
// ROM BUG: on a memory-mapped store the write's result, and on a TFlash
// the copy's, are dropped: the answer is always noErr.
NewtonErr
TObjRef::CopyTo(TObjRef& to, ULong offset, ULong size)
{
	ULong dst = fStore->Translate(to.fOffset + offset + 8);
	ULong src = fStore->Translate(fOffset + offset + 8);
	NewtonErr err = noErr;
	if (fStore->fStoreDriver == nil)
	{
		if (!fStore->fUsesTFlash)
			fStore->BasicWrite(dst, fStore->fBase + src, size);
		else
			fStore->fFlash->Copy(src, dst, size);
	}
	else
	{
		for (;;)
		{
			newton_try
			{
				fStore->fStoreDriver->Copy(src, dst, size);
			}
			newton_catch(exAbort)
			{
				err = kSError_WriteProtected;
			}
			end_try;
			if (err == noErr)
				break;
			fStore->SendAlertMgrWPBitch(0);
		}
	}
	return err;
}


// The header written in its new state: on flash the state's bits are
// cleared among those already cleared, in RAM they are simply set.
static NewtonErr
WriteState(TObjRef* ref, int state)
{
	if (!ref->fStore->fIsSRAM)
		ref->fWord1 &= ~((ULong) gObjectStateToTransBits[state] << 8);
	else
		ref->fWord1 = (ref->fWord1 & 0xFFFF00FF) | ((~(ULong) gObjectStateToTransBits[state] & 0xFF) << 8);
	return ref->fStore->WriteWords(ref->fStore->Translate(ref->fOffset), &ref->fWord0, 2);
}


// ROM 0x001487fc SetState__7TObjRefFi
// (BasicWrite inline.)
NewtonErr
TObjRef::SetState(int state)
{
	return WriteState(this, state);
}


// ROM 0x00148874 SetCommittedState__7TObjRefFv
// Committed, and out of any separate transaction.  (BasicWrite inline.)
NewtonErr
TObjRef::SetCommittedState(void)
{
	if (IsSeparate())
		fWord1 &= ~6;
	return WriteState(this, fStore->State(kRAMObjCommitted));
}
