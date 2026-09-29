/*
	File:		stores/flash/FlashStore.h

	Contains:	TFlashStore, the TStore the ROM keeps on flash - the internal
				flash, a flash card - and on battery-backed RAM, in one
				log-structured format; its blocks, physical blocks, directory
				iterator, lookup cache, tracker and object references.

				The store is divided into *blocks*, each an erase unit of the
				flash (on the MP2x00's internal flash a TNewInternalFlash
				region, 128 KB).  A block holds, from its start:

				  - (on the internal flash, four bytes that are the flash's own)
				  - the root directory: an object (id 3) of fBucketCount
				    buckets of fBucketSize four-byte directory entries
				    (SDirEnt), hashed by object id;
				  - objects, one after another: an eight-byte header
				    (SObject: the id, a size, the transaction bits, the
				    separate-transaction and execute-in-place flags) and the
				    data rounded up to a word;

				and, in its last 0x400 bytes, a *log* of entries
				(SFlashLogEntry: 'fblk' says which logical block a physical
				block holds, 'eblk' that a physical block was erased and how
				often, 'zblk' a reserved block), each guarded by its own
				address XORed with "dyer" and "foo!" and the word "newt".
				One physical block is always kept spare: a block is
				compacted by copying what is alive in it into the spare and
				then erasing it, which makes it the spare.

				Flash can only clear bits, so every state an object or entry
				goes through is written by clearing more bits of a word that
				started as ones: an object's transaction bits run through
				gObjectStateToTransBits' codes, each a superset of the last,
				and gObjectTransBitsToState reads the state back.  A write
				that fails to clear what it should is answered
				kSError_NotVirgin and the write is made somewhere else.

				On battery-backed RAM (a RAM card, or the internal store when
				there is no flash) the same format is kept through a
				TStoreDriver, with the compaction's progress in an
				SCompactState that survives a reboot.  NOT YET: the RAM
				stores (TStoreDriver) and the cards' power and write-protect
				alerts - Init refuses a store that would need them.

	Reconstructed from the MP2x00 US ROM (0x000bee88-0x000c23b0 the blocks,
	0x000c4a9c-0x000caee0 the store, 0x001482fc-0x00148900 TObjRef); each
	function cites its origin.  The field names are ours.

	DEVIATION: the words of the store's own structures - object headers,
	directory entries, log entries - are big-endian on the flash, as the
	ARM wrote them; the host keeps them in its own order in memory and
	converts them at every read and write of the flash
	(toolbox/ByteOrder.h).  An object's data is bytes and is not touched.
*/

#ifndef __FLASHSTORE_H
#define __FLASHSTORE_H

#ifndef __STORE_H
#include "Store.h"
#endif

#ifndef __FLASH_H
#include "Flash.h"
#endif

class TFlashStore;
class TFlashBlock;

// A word of the flash's structures: 32 bits, whatever a host's ULong is.
typedef uint32_t	FlashWord;
class TFlashTracker;
class TCardHandler;

// the tags of the log entries
enum
{
	kFlashBlockLogTag		= 'fblk',		// a physical block holds a logical one
	kFlashEraseLogTag		= 'eblk',		// a physical block was erased
	kReservedBlockLogTag	= 'zblk'		// a physical block is reserved
};

// the object ids the store keeps for itself
enum
{
	kFlashRootDirectoryId	= 3,			// a block's root directory
	kFlashTransactionId		= 0x17,			// the transaction record
	kFlashRootObjectId		= 0x27			// GetRootId's
};

// the states an object goes through; the RAM store's (1-7) and the flash's
// (8-14) run in parallel, the flash's being the RAM's plus seven.  The
// names are ours, from what the transaction code does in each.
enum
{
	kObjNoState					= 0,
	kRAMObjCloneEmpty			= 1,		// a copy being made to replace a committed object
	kRAMObjCloneOfNew			= 2,		// a copy being made to replace a new object
	kRAMObjNew					= 3,		// made in the transaction under way
	kRAMObjCommitted			= 4,
	kRAMObjSuperceded			= 5,		// committed, replaced by a copy in the transaction
	kRAMObjSuperceder			= 6,		// the copy
	kRAMObjDeleted				= 7,		// committed, deleted in the transaction
	kFlashObjCloneEmpty			= 8,
	kFlashObjCloneOfNew			= 9,
	kFlashObjNew				= 10,
	kFlashObjCommitted			= 11,
	kFlashObjSuperceded			= 12,
	kFlashObjSuperceder			= 13,
	kFlashObjDeleted			= 14
};

extern const unsigned char	gObjectStateToTransBits[16];		// ROM 0x0037142c gObjectStateToTransBits
extern const unsigned char	gObjectTransBitsToState[256];		// ROM 0x0037143c gObjectTransBitsToState

extern ULong	gInternalBlockSize;			// ROM 0x0c100dd8 gInternalBlockSize
extern ULong	gMutableBlockSize;			// ROM 0x0c100ddc gMutableBlockSize
extern ULong	gInternalFlashStoreSlop;	// ROM 0x0c100de0 gInternalFlashStoreSlop


/*------------------------------------------------------------------------------
	S O b j e c t
	An object's header: 8 bytes on the flash.
		word 0:	bits 0-27 the id; bit 29 set while the header has a size
				(a header left half-written is four bytes to step over);
				bit 30 cleared when the object is deleted
		word 1:	bits 16-31 the size; bits 8-15 the transaction bits;
				bit 7 cleared for an execute-in-place object; bits 1-2 the
				separate transaction (2: in one); bit 0 cleared once the
				header is written
------------------------------------------------------------------------------*/

struct SObject
{
	FlashWord		fWord0;
	FlashWord		fWord1;

	Boolean		IsValid(TFlashStore* store);			// ROM 0x000c4e84 IsValid__7SObjectFP11TFlashStore

	PSSId		Id() const				{ return fWord0 & 0x0FFFFFFF; }
	ULong		Size() const			{ return fWord1 >> 16; }
	ULong		SeparateBits() const	{ return (fWord1 & 7) >> 1; }
	Boolean		IsSeparate() const		{ return (fWord1 & 6) == 4; }
};

ULong		ObjectStateToTransBits(int state, TFlashStore* store);	// ROM 0x000c4eb0 ObjectStateToTransBits__FiP11TFlashStore


/*------------------------------------------------------------------------------
	S D i r E n t
	A directory entry: one word.  Bits 8-31 are an object's offset in its
	block, a quarter of it (or, in a migrated entry, the object's number and
	the block it went to); bit 3 cleared once written; bit 7 cleared in the
	last entry of a bucket, which links to the bucket's continuation; bit 2
	cleared in a migrated entry; bit 6 cleared once the entry is in use.
------------------------------------------------------------------------------*/

struct SDirEnt
{
	FlashWord		fWord;

	Boolean		IsValid(TFlashStore* store);										// ROM 0x000c4f48 IsValid__7SDirEntFP11TFlashStore
	static Boolean	IsValidMigratedObjectInfo(long objectNumber, long block);		// ROM 0x000c4f7c IsValidMigratedObjectInfo__7SDirEntSFlT1
	void		SetMigratedObjectInfo(long objectNumber, long block);				// ROM 0x000c4fa0 SetMigratedObjectInfo__7SDirEntFlT1
	void		GetMigratedObjectInfo(long* objectNumber, long* block) const;		// ROM 0x000c4fb8 GetMigratedObjectInfo__7SDirEntCFPlT1
};


/*------------------------------------------------------------------------------
	S F l a s h L o g E n t r y
	The header every log entry starts with (0x20 bytes), and the three kinds.
------------------------------------------------------------------------------*/

struct SFlashLogEntry
{
	FlashWord		fGuard1;		// +0x00  the entry's own address XOR 'dyer'
	FlashWord		fGuard2;		// +0x04  its complement XOR 'foo!'
	FlashWord		fNewt;			// +0x08  'newt'
	FlashWord		fTag;			// +0x0c  'fblk', 'eblk' or 'zblk'
	FlashWord		fSize;			// +0x10  the whole entry's
	FlashWord		fLSN;			// +0x14  the store's log sequence number when written
	FlashWord		fPhysOffset;	// +0x18  the physical block the entry is about
	FlashWord		fVirgin;		// +0x1c  the store's erased word

	Boolean		IsValid(ULong offset);				// ROM 0x000c4ec4 IsValid__14SFlashLogEntryFUl
	ULong		PhysOffset(void);					// ROM 0x000c4f2c PhysOffset__14SFlashLogEntryFv
};

Boolean		PrivateFlashLogEntryIsValid(SFlashLogEntry* entry, ULong offset);	// ROM 0x000c4ec8 PrivateFlashLogEntryIsValid__FP14SFlashLogEntryUl
ULong		PrivateFlashLogEntryPhysOffset(SFlashLogEntry* entry);				// ROM 0x000c4f30 PrivateFlashLogEntryPhysOffset__FP14SFlashLogEntry

// 'fblk': physical block fPhysOffset holds logical block fLogicalOffset (0x4c bytes)
struct SFlashBlockLogEntry : public SFlashLogEntry
{
	FlashWord		fUnknown20;		// +0x20  0
	FlashWord		fEraseCount;	// +0x24
	FlashWord		fLogicalOffset;	// +0x28
	FlashWord		fUnknown2C;		// +0x2c  0
	FlashWord		fUnknown30;		// +0x30  0
	FlashWord		fRandom;		// +0x34  rand() when the store was formatted
	FlashWord		fTime;			// +0x38  and the time, in seconds: the two XORed say which store this is
	FlashWord		fRootDirectory;	// +0x3c  the root directory's offset
	FlashWord		fWritable;		// +0x40  the erased word; bit 0 the polarity the store was written with (a ROM card's is 0)
	FlashWord		fUnknown44;		// +0x44  0
	FlashWord		fUnknown48;		// +0x48  0
};

// 'eblk': physical block fPhysOffset was erased (0x28 bytes)
struct SFlashEraseLogEntry : public SFlashLogEntry
{
	FlashWord		fEraseCount;	// +0x20
	UChar		fUsable;		// +0x24  1; 0 would mark the block out of use
	UChar		fPad[3];
};

// 'zblk': physical block fPhysOffset is reserved (0x30 bytes)
struct SReservedBlockLogEntry : public SFlashLogEntry
{
	FlashWord		fUnknown20;		// +0x20
	FlashWord		fEraseCount;	// +0x24
	FlashWord		fUnknown28;		// +0x28
	FlashWord		fLogicalOffset;	// +0x2c
};

const ULong	kFlashBlockLogEntrySize		= 0x4c;
const ULong	kFlashEraseLogEntrySize		= 0x28;
const ULong	kReservedBlockLogEntrySize	= 0x30;


/*------------------------------------------------------------------------------
	S C o m p a c t S t a t e
	A RAM store's compaction in progress, kept where it survives a reboot
	(DDK PSS/CompactState.h has the magic numbers).  100 bytes.
------------------------------------------------------------------------------*/

class SCompactState
{
public:
	void		Init(void);				// ROM 0x00070ed4 Init__13SCompactStateFv
	Boolean		IsValid(void);			// ROM 0x00070f14 IsValid__13SCompactStateFv
	Boolean		InProgress(void);		// ROM 0x00070f44 InProgress__13SCompactStateFv

	FlashWord		fMagicKey;				// +0x00  'bltg'
	FlashWord		fStep;					// +0x04  where RealContinueCompact has got to; 0 none
	FlashWord		fTransactionState;		// +0x08  a RAM store's transaction state
	FlashWord		fBlockPhysOffset;		// +0x0c
	FlashWord		fBlockOffset;			// +0x10
	FlashWord		fOtherMagicKey;			// +0x14  'zarf'
	FlashWord		fMore[19];				// +0x18  the copy's positions, and the driver it runs through
};


/*------------------------------------------------------------------------------
	T S t o r e D r i v e r
	A RAM store's memory.  NOT YET: TFlashStore reaches one only in a RAM
	store, which Init refuses.
------------------------------------------------------------------------------*/

class TStoreDriver
{
public:
	void		Init(char* base, ULong size, char* persistentBase, ULong persistentSize);	// ROM 0x001faf08 Init__12TStoreDriverFPcUlT1T2
	void		Read(char* buffer, ULong offset, ULong size);	// NOT YET
	void		Write(char* buffer, ULong offset, ULong size);	// NOT YET
	void		Set(ULong offset, ULong size, ULong value);	// NOT YET
	void		Copy(ULong from, ULong to, ULong size);	// NOT YET
	void		PersistentCopy(ULong from, ULong to, ULong size);	// NOT YET
	void		ContinuePersistentCopy(void);	// NOT YET

	char*		fBase;					// +0x00
	ULong		fSize;					// +0x04
	char*		fPersistentBase;		// +0x08
	ULong		fPersistentSize;		// +0x0c
	ULong		fTotalSize;				// +0x10
	ULong		fCopyFrom;				// +0x14
	ULong		fCopyTo;				// +0x18
	long		fCopyIndex;				// +0x1c
	ULong		fCopyCount;				// +0x20
	UChar		fCopying;				// +0x24
	UChar		fOverlapping;			// +0x25
};


/*------------------------------------------------------------------------------
	T O b j R e f
	An object found on the store: its header, where it is, where its
	directory entry is; every one in use is on its store's list, so that a
	compaction can find it again where it moved to.  0x1c bytes.
------------------------------------------------------------------------------*/

class TObjRef : public SObject
{
public:
	void		Init(TFlashStore* store);		// (the ROM has this inline everywhere: id the erased word's, put on the list)

	TObjRef&	operator=(const TObjRef& other);										// ROM 0x001482fc __as__7TObjRefFRC7TObjRef
	NewtonErr	Set(ULong offset, ULong dirEntOffset);								// ROM 0x00148318 Set__7TObjRefFUlT1
	NewtonErr	FindSuperceeded(TObjRef& superceded);									// ROM 0x001483a4 FindSuperceeded__7TObjRefFR7TObjRef
	NewtonErr	FindSuperceeder(TObjRef& superceder);									// ROM 0x001488a0 FindSuperceeder__7TObjRefFR7TObjRef
	NewtonErr	Delete(void);															// ROM 0x001483d0 Delete__7TObjRefFv
	NewtonErr	Write(void* buffer, ULong offset, ULong size);							// ROM 0x00148444 Write__7TObjRefFPvUlT2
	NewtonErr	Read(void* buffer, ULong offset, ULong size);							// ROM 0x00148488 Read__7TObjRefFPvUlT2
	NewtonErr	SetSeparateTranny(void);												// ROM 0x001484cc SetSeparateTranny__7TObjRefFv
	NewtonErr	ClearSeparateTranny(void);												// ROM 0x001485b0 ClearSeparateTranny__7TObjRefFv
	NewtonErr	ReWriteObjHeader(void);													// ROM 0x001485f0 ReWriteObjHeader__7TObjRefFv
	ULong		GetDirEntOffset(void);													// ROM 0x00148624 GetDirEntOffset__7TObjRefFv
	NewtonErr	CloneEmpty(int state, ULong size, TObjRef& clone, UChar separate);		// ROM 0x001486c4 CloneEmpty__7TObjRefFiUlR7TObjRefUc
	NewtonErr	CloneEmpty(int state, TObjRef& clone, UChar separate);					// ROM 0x00148708 CloneEmpty__7TObjRefFiR7TObjRefUc
	NewtonErr	Clone(int state, TObjRef& clone, UChar separate);						// ROM 0x00148734 Clone__7TObjRefFiR7TObjRefUc
	NewtonErr	CopyTo(TObjRef& to, ULong offset, ULong size);							// ROM 0x001487a0 CopyTo__7TObjRefFR7TObjRefUlT2
	NewtonErr	SetState(int state);													// ROM 0x001487fc SetState__7TObjRefFi
	NewtonErr	SetCommittedState(void);												// ROM 0x00148874 SetCommittedState__7TObjRefFv

	int			State(void);				// its transaction bits read as a state (the ROM has it inline)

	ULong		fOffset;				// +0x08  the header's offset in the store; -1 not known
	ULong		fDirEntOffset;			// +0x0c  its directory entry's; -1 not known
	TFlashStore*	fStore;				// +0x10
	TObjRef*	fNext;					// +0x14  on the store's list
	TObjRef*	fPrev;					// +0x18
};


/*------------------------------------------------------------------------------
	T F l a s h P h y s B l o c k
	What the log says about one physical block.  0x18 bytes.
------------------------------------------------------------------------------*/

class TFlashPhysBlock
{
public:
	void		Init(TFlashStore* store, ULong physOffset);							// ROM 0x000c23b0 Init__15TFlashPhysBlockFP11TFlashStoreUl
	void		SetInfo(SFlashBlockLogEntry* entry);									// ROM 0x000c23d8 SetInfo__15TFlashPhysBlockFP19SFlashBlockLogEntry
	void		SetInfo(SFlashEraseLogEntry* entry);									// ROM 0x000c2420 SetInfo__15TFlashPhysBlockFP19SFlashEraseLogEntry
	void		SetInfo(SReservedBlockLogEntry* entry);									// ROM 0x000c2470 SetInfo__15TFlashPhysBlockFP22SReservedBlockLogEntry
	ULong		LogEntryOffset(void);													// ROM 0x000c2418 LogEntryOffset__15TFlashPhysBlockFv
	ULong		EraseCount(void);														// ROM 0x000c24b4 EraseCount__15TFlashPhysBlockFv
	ULong		GetPhysicalOffset(void);												// ROM 0x000c24bc GetPhysicalOffset__15TFlashPhysBlockFv
	Boolean		IsSpare(void);															// ROM 0x000c24c4 IsSpare__15TFlashPhysBlockFv
	NewtonErr	SetSpare(TFlashPhysBlock* newHome, ULong rootDirectory);				// ROM 0x000c24dc SetSpare__15TFlashPhysBlockFP15TFlashPhysBlockUl

	TFlashStore*	fStore;				// +0x00
	ULong		fLogicalOffset;			// +0x04  the logical block it holds; -1: the spare
	ULong		fPhysOffset;			// +0x08
	ULong		fLogEntryOffset;		// +0x0c  the log entry that says so; 0 none
	ULong		fEraseCount;			// +0x10
	UChar		fUnusable;				// +0x14  an erase entry said so
	UChar		fIsReserved;			// +0x15
};


/*------------------------------------------------------------------------------
	T F l a s h B l o c k
	A logical block.  0x20 bytes.
------------------------------------------------------------------------------*/

class TFlashBlock
{
public:
	void		Init(TFlashStore* store);												// ROM 0x000bee88 Init__11TFlashBlockFP11TFlashStore
	NewtonErr	SetInfo(SFlashBlockLogEntry* entry, UChar* separateSeen);				// ROM 0x000beeac SetInfo__11TFlashBlockFP19SFlashBlockLogEntryPUc
	NewtonErr	SetInfo(SReservedBlockLogEntry* entry);									// ROM 0x000bfc7c SetInfo__11TFlashBlockFP22SReservedBlockLogEntry
	NewtonErr	Lookup(PSSId id, int state, TObjRef& ref, long* migratedTo);			// ROM 0x000bf044 Lookup__11TFlashBlockFUliR7TObjRefPl
	NewtonErr	AddObject(PSSId id, int state, ULong size, TObjRef& ref, UChar separate, UChar xip);	// ROM 0x000bf0d0 AddObject__11TFlashBlockFUliT1R7TObjRefUcT5
	NewtonErr	AddDirEnt(PSSId id, ULong objOffset, ULong* dirEntOffset, SDirEnt* dirEnt);	// ROM 0x000bf42c AddDirEnt__11TFlashBlockFUlT1PUlP7SDirEnt
	NewtonErr	ExtendDirBucket(ULong lastEntryOffset, ULong* newBucket);				// ROM 0x000bf6d0 ExtendDirBucket__11TFlashBlockFUlPUl
	NewtonErr	ZapObject(ULong offset);												// ROM 0x000bf824 ZapObject__11TFlashBlockFUl
	NewtonErr	ZapDirEnt(ULong offset);												// ROM 0x000bf8e4 ZapDirEnt__11TFlashBlockFUl
	NewtonErr	NextObject(ULong offset, ULong* nextOffset, UChar allStates);			// ROM 0x000bf910 NextObject__11TFlashBlockFUlPUlUc
	NewtonErr	CompactInPlace(void);													// NOT YET (a RAM store's; the card alerts)
	NewtonErr	StartCompact(SCompactState* state);									// NOT YET (a RAM store's; the card alerts)
	NewtonErr	ContinueCompact(SCompactState* state);									// NOT YET (a RAM store's; the card alerts)
	NewtonErr	RealContinueCompact(SCompactState* state);								// NOT YET (a RAM store's; the card alerts)
	NewtonErr	CompactInto(ULong physOffset);											// ROM 0x000c018c CompactInto__11TFlashBlockFUl
	NewtonErr	CompactInto(TFlashBlock* into);										// ROM 0x000c02bc CompactInto__11TFlashBlockFP11TFlashBlock
	NewtonErr	ReadObjectAt(ULong offset, SObject* object);							// ROM 0x000c0574 ReadObjectAt__11TFlashBlockFUlP7SObject
	NewtonErr	AddMigDirEnt(long objectNumber, long block);							// ROM 0x000c05a8 AddMigDirEnt__11TFlashBlockFlT1
	NewtonErr	ObjectMigrated(PSSId id, long block);									// ROM 0x000c0664 ObjectMigrated__11TFlashBlockFUll
	NewtonErr	ZapMigDirEnt(PSSId id);												// ROM 0x000c06e4 ZapMigDirEnt__11TFlashBlockFUl
	NewtonErr	ReadDirEntAt(ULong offset, SDirEnt* dirEnt);							// ROM 0x000c07d4 ReadDirEntAt__11TFlashBlockFUlP7SDirEnt
	Boolean		IsVirgin(void);															// ROM 0x000c0808 IsVirgin__11TFlashBlockFv
	NewtonErr	WriteRootDirectory(ULong* rootDirectory);								// ROM 0x000c082c WriteRootDirectory__11TFlashBlockFPUl
	Boolean		IsReserved(void);														// ROM 0x000c08b0 IsReserved__11TFlashBlockFv
	ULong		EraseCount(void);														// ROM 0x000c08dc EraseCount__11TFlashBlockFv
	ULong		RootDirEnt(PSSId id);													// ROM 0x000c0904 RootDirEnt__11TFlashBlockFUl
	PSSId		NextPSSID(void);														// ROM 0x000c094c NextPSSID__11TFlashBlockFv
	PSSId		UseNextPSSID(void);														// ROM 0x000c098c UseNextPSSID__11TFlashBlockFv
	NewtonErr	SetDirEntOffset(ULong dirEntOffset, ULong objOffset);					// ROM 0x000c09a0 SetDirEntOffset__11TFlashBlockFUlT1
	NewtonErr	BasicWrite(ULong offset, void* buffer, ULong size);					// ROM 0x000c09f0 BasicWrite__11TFlashBlockFUlPvT1
	long		EraseHeuristic(ULong yield);											// ROM 0x000c0a28 EraseHeuristic__11TFlashBlockFUl
	ULong		BucketSize(void);														// ROM 0x000c0a6c BucketSize__11TFlashBlockFv
	ULong		BucketCount(void);														// ROM 0x000c0a78 BucketCount__11TFlashBlockFv
	ULong		Avail(void);															// ROM 0x000c0a84 Avail__11TFlashBlockFv
	ULong		RootDirSize(void);														// ROM 0x000c0ae8 RootDirSize__11TFlashBlockFv
	ULong		CalcRecoverableBytes(void);												// ROM 0x000c0b20 CalcRecoverableBytes__11TFlashBlockFv
	ULong		Yield(void);															// ROM 0x000c0c44 Yield__11TFlashBlockFv
	ULong		EndOffset(void);														// ROM 0x000c0c78 EndOffset__11TFlashBlockFv
	ULong		LogEntryOffset(void);													// ROM 0x000c0c9c LogEntryOffset__11TFlashBlockFv
	TFlashPhysBlock*	PhysBlock(void);												// ROM 0x000c0cc4 PhysBlock__11TFlashBlockFv

	TFlashStore*	fStore;				// +0x00
	ULong		fLogicalOffset;			// +0x04
	ULong		fPhysOffset;			// +0x08  -1: no physical block
	ULong		fRootDirectory;			// +0x0c
	ULong		fFreeOffset;			// +0x10  where the next object goes
	ULong		fZappedBytes;			// +0x14  deleted objects' bytes, recovered by compacting
	PSSId		fNextPSSID;				// +0x18
	ULong		fStoreId;				// +0x1c  the format's random number XOR its time
};


/*------------------------------------------------------------------------------
	T F l a s h S t o r e L o o k u p C a c h e
	Where objects were last found: a set-associative table of (id, state,
	directory entry offset).  0x10 bytes.
------------------------------------------------------------------------------*/

struct SFlashStoreLookupCacheEntry
{
	PSSId		fId;					// +0x00
	ULong		fDirEntOffset;			// +0x04
	int			fState;					// +0x08

	Boolean		Matches(PSSId id, int state);											// ROM 0x000c4aa4 Matches__27SFlashStoreLookupCacheEntryFUli
};

class TFlashStoreLookupCache
{
public:
	NewtonErr	Init(ULong size);														// ROM 0x000c4b10 Init__22TFlashStoreLookupCacheFUl
	void		Destroy(void);															// ROM 0x000c4bc8 Destroy__22TFlashStoreLookupCacheFv
	ULong		Lookup(PSSId id, int state);											// ROM 0x000c4bec Lookup__22TFlashStoreLookupCacheFUli
	void		Add(PSSId id, ULong dirEntOffset, int state);							// ROM 0x000c4c78 Add__22TFlashStoreLookupCacheFUlT1i
	void		Add(TObjRef& ref);														// ROM 0x000c4e28 Add__22TFlashStoreLookupCacheFR7TObjRef
	void		Change(PSSId id, ULong dirEntOffset, int state);						// ROM 0x000c4dc4 Change__22TFlashStoreLookupCacheFUlT1i
	void		Change(TObjRef& ref);													// ROM 0x000c4b6c Change__22TFlashStoreLookupCacheFR7TObjRef
	void		Forget(PSSId id, int state);											// ROM 0x000c4d48 Forget__22TFlashStoreLookupCacheFUli
	void		ForgetAll(void);														// ROM 0x000c4e10 ForgetAll__22TFlashStoreLookupCacheFv

	SFlashStoreLookupCacheEntry*	fEntries;	// +0x00
	ULong		fSize;					// +0x04
	ULong		fNext;					// +0x08  the way to replace next
	ULong		fWays;					// +0x0c  8
};


/*------------------------------------------------------------------------------
	T F l a s h T r a c k e r
	The objects a transaction has touched, so that a commit need not walk
	the whole store (unless it overflowed).  0x14 bytes.
------------------------------------------------------------------------------*/

class TFlashTracker
{
public:
					TFlashTracker();											// ROM 0x000cadbc __ct__13TFlashTrackerFv
					~TFlashTracker();											// ROM 0x000cae04 __dt__13TFlashTrackerFv

	NewtonErr		Init(ULong size);											// ROM 0x000cae30 Init__13TFlashTrackerFUl
	void			Deinit(void);												// ROM 0x000cae6c Deinit__13TFlashTrackerFv
	void			Add(PSSId id);												// ROM 0x000cae90 Add__13TFlashTrackerFUl
	void			Remove(PSSId id);											// ROM 0x000caec8 Remove__13TFlashTrackerFUl

	ULong			fSize;				// +0x00
	ULong			fCount;				// +0x04
	PSSId*			fIds;				// +0x08
	UChar			fOverflowed;		// +0x0c
	long			fNesting;			// +0x10  operations under way that may add to it
};


/*------------------------------------------------------------------------------
	T F l a s h I t e r a t o r
	Walks a block's objects, a directory's entries or a tracker's ids.
	0x78 bytes.
------------------------------------------------------------------------------*/

enum IterFilterType
{
	kIterAllObjects				= 0,		// every live object of the store
	kIterAllObjectsAllStates	= 1,
	kIterBlockObjects			= 2,		// one block's live objects
	kIterBlockObjectsAllStates	= 3,
	kIterDirectory				= 4,		// a directory bucket and its continuations
	kIterTracker				= 5			// the ids a tracker holds
};

class TFlashIterator
{
public:
					TFlashIterator(TFlashStore* store, TObjRef* ref, IterFilterType filter);						// ROM 0x000c1514 __ct__14TFlashIteratorFP11TFlashStoreP7TObjRef14IterFilterType
					TFlashIterator(TFlashStore* store, TObjRef* ref, ULong start, IterFilterType filter);			// ROM 0x000c155c __ct__14TFlashIteratorFP11TFlashStoreP7TObjRefUl14IterFilterType
					TFlashIterator(TFlashStore* store, TObjRef* ref, TFlashBlock* block, IterFilterType filter);	// ROM 0x000c1890 __ct__14TFlashIteratorFP11TFlashStoreP7TObjRefP11TFlashBlock14IterFilterType
					TFlashIterator(TFlashStore* store, SDirEnt* dirEnt, ULong bucket);								// ROM 0x000c18e0 __ct__14TFlashIteratorFP11TFlashStoreP7SDirEntUl

	void			Start(IterFilterType filter);								// ROM 0x000c15ac Start__14TFlashIteratorF14IterFilterType
	void			Start(ULong start, IterFilterType filter);					// ROM 0x000c15d4 Start__14TFlashIteratorFUl14IterFilterType
	void			Start(TFlashTracker* tracker);								// ROM 0x000c15f0 Start__14TFlashIteratorFP13TFlashTracker
	NewtonErr		Lookup(PSSId id, int state, long* migratedTo);				// ROM 0x000c1608 Lookup__14TFlashIteratorFUliPl
	ULong			GetDirEnt(ULong offset);									// ROM 0x000c1818 GetDirEnt__14TFlashIteratorFUl
	ULong			ReadDirBucket(ULong offset);								// ROM 0x000c20ac ReadDirBucket__14TFlashIteratorFUl
	long			CountUnusedDirEnt(void);									// ROM 0x000c1854 CountUnusedDirEnt__14TFlashIteratorFv
	Boolean			Done(void);													// ROM 0x000c1938 Done__14TFlashIteratorFv
	void			Probe(void);												// ROM 0x000c19d4 Probe__14TFlashIteratorFv
	TObjRef*		Next(void);													// ROM 0x000c20f8 Next__14TFlashIteratorFv
	void			Reset(void);												// ROM 0x000c2114 Reset__14TFlashIteratorFv

	TObjRef*		fRef;				// +0x00
	TFlashStore*	fStore;				// +0x04
	SDirEnt*		fDirEnt;			// +0x08
	long			fUnusedCount;		// +0x0c
	IterFilterType	fFilter;			// +0x10
	ULong			fStart;				// +0x14
	long			fState;				// +0x18  0 start, 1 have one, 2 looking, 3 done
	ULong			fStep;				// +0x1c
	TFlashTracker*	fTracker;			// +0x20
	ULong			fOffset;			// +0x24
	ULong			fDirEntOffset;		// +0x28
	long			fTrackerIndex;		// +0x2c
	ULong			fCompactCount;		// +0x30  the store's when it started: a compaction since moves everything
	ULong			fBucketCacheOffset;	// +0x34  -1: nothing cached
	FlashWord		fBucketCache[16];	// +0x38
};


/*------------------------------------------------------------------------------
	T F l a s h S t o r e
	0xF0 bytes.
------------------------------------------------------------------------------*/

// Init's flags
enum
{
	kFlashStoreIsCard			= 0x01,		// pssInfo is the card's
	kFlashStoreIsInternalRAM	= 0x08,
	kFlashStoreUsesTFlash		= 0x10		// pssInfo is a TFlash
};

PROTOCOL TFlashStore : public TStore
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TFlashStore);

	TFlashStore*	New(void);																	// ROM 0x000c51d8 New__11TFlashStoreFv
	void		Delete(void);																	// ROM 0x000c7c28 Delete__11TFlashStoreFv

	NewtonErr	Init(void* storeAddress, ULong storeSize, ULong arg3, int socketNumber, ULong flags, void* pssInfo);	// ROM 0x000c6bf4 Init__11TFlashStoreFPvUlT2iT2T1
	NewtonErr	NeedsFormat(Boolean* needsFormat);											// ROM 0x000c8fd0 NeedsFormat__11TFlashStoreFPUc
	NewtonErr	Format(void);																	// ROM 0x000c88dc Format__11TFlashStoreFv
	NewtonErr	GetRootId(PSSId* rootId);														// ROM 0x000c9ba4 GetRootId__11TFlashStoreFPUl
	NewtonErr	NewObject(long size, PSSId* id);												// ROM 0x000c543c NewObject__11TFlashStoreFlPUl
	NewtonErr	EraseObject(PSSId id);															// ROM 0x000c544c EraseObject__11TFlashStoreFUl
	NewtonErr	DeleteObject(PSSId id);															// ROM 0x000c55b4 DeleteObject__11TFlashStoreFUl
	NewtonErr	SetObjectSize(PSSId id, long size);												// ROM 0x000c5898 SetObjectSize__11TFlashStoreFUll
	NewtonErr	GetObjectSize(PSSId id, long* size);											// ROM 0x000c5df4 GetObjectSize__11TFlashStoreFUlPl
	NewtonErr	Write(PSSId id, long offset, char* buffer, long count);							// ROM 0x000c5eec Write__11TFlashStoreFUllPcT2
	NewtonErr	Read(PSSId id, long offset, char* buffer, long count);							// ROM 0x000c7200 Read__11TFlashStoreFUllPcT2
	NewtonErr	GetStoreSizes(long* totalSize, long* usedSize);									// ROM 0x000c84a0 GetStoreSizes__11TFlashStoreFPlT1
	NewtonErr	IsReadOnly(Boolean* isReadOnly);												// ROM 0x000c86b4 IsReadOnly__11TFlashStoreFPUc
	NewtonErr	LockStore(void);																// ROM 0x000c873c LockStore__11TFlashStoreFv
	NewtonErr	UnlockStore(void);																// ROM 0x000c8750 UnlockStore__11TFlashStoreFv
	NewtonErr	Abort(void);																	// ROM 0x000c8828 Abort__11TFlashStoreFv
	NewtonErr	Idle(Boolean* arg1, Boolean* arg2);												// ROM 0x000c8fc0 Idle__11TFlashStoreFPUcT1
	NewtonErr	NextObject(PSSId id, PSSId* nextId);											// ROM 0x000c8f04 NextObject__11TFlashStoreFUlPUl
	NewtonErr	CheckIntegrity(ULong* arg);														// ROM 0x000c8f0c CheckIntegrity__11TFlashStoreFPUl
	NewtonErr	SetBuddy(TStore* buddy);														// ROM 0x000c8f14 SetBuddy__11TFlashStoreFP6TStore
	Boolean		OwnsObject(PSSId id);															// ROM 0x000c8fb0 OwnsObject__11TFlashStoreFUl
	void*		Address(PSSId id);																// ROM 0x000c8fc8 Address__11TFlashStoreFUl
	const char*	StoreKind(void);																// ROM 0x000c8f1c StoreKind__11TFlashStoreFv
	NewtonErr	SetStore(TStore* store, ULong arg);												// ROM 0x000c8fa8 SetStore__11TFlashStoreFP6TStoreUl
	Boolean		IsSameStore(void* data, ULong size);											// ROM 0x000c7360 IsSameStore__11TFlashStoreFPvUl
	Boolean		IsLocked(void);																	// ROM 0x000c8ef0 IsLocked__11TFlashStoreFv
	NewtonErr	VppOff(void);																	// ROM 0x000c0d60 VppOff__11TFlashStoreFv
	NewtonErr	Sleep(void);																	// ROM 0x000c8fb8 Sleep__11TFlashStoreFv
	Boolean		IsROM(void);																	// ROM 0x000c8ff0 IsROM__11TFlashStoreFv
	NewtonErr	NewWithinTransaction(long size, PSSId* id);										// ROM 0x000c9ca8 NewWithinTransaction__11TFlashStoreFlPUl
	NewtonErr	StartTransactionAgainst(PSSId id);												// ROM 0x000c9cb4 StartTransactionAgainst__11TFlashStoreFUl
	NewtonErr	SeparatelyAbort(PSSId id);														// ROM 0x000c9ef8 SeparatelyAbort__11TFlashStoreFUl
	NewtonErr	AddToCurrentTransaction(PSSId id);												// ROM 0x000ca298 AddToCurrentTransaction__11TFlashStoreFUl
	Boolean		InSeparateTransaction(PSSId id);												// ROM 0x000ca51c InSeparateTransaction__11TFlashStoreFUl
	NewtonErr	LockReadOnly(void);																// ROM 0x000ca61c LockReadOnly__11TFlashStoreFv
	NewtonErr	UnlockReadOnly(Boolean reset);													// ROM 0x000ca630 UnlockReadOnly__11TFlashStoreFUc
	Boolean		InTransaction(void);															// ROM 0x000ca658 InTransaction__11TFlashStoreFv
	NewtonErr	NewObject(char* data, long size, PSSId* id);									// ROM 0x000c5204 NewObject__11TFlashStoreFPclPUl
	NewtonErr	ReplaceObject(PSSId id, char* data, long size);									// ROM 0x000c696c ReplaceObject__11TFlashStoreFUlPcl
	NewtonErr	CalcXIPObjectSize(long arg1, long arg2, long* size);							// ROM 0x000ca988 CalcXIPObjectSize__11TFlashStoreFlT1Pl
	NewtonErr	NewXIPObject(long size, PSSId* id);												// ROM 0x000caad4 NewXIPObject__11TFlashStoreFlPUl
	NewtonErr	GetXIPObjectInfo(PSSId id, ULong* arg1, ULong* arg2, ULong* arg3);				// ROM 0x000cab40 GetXIPObjectInfo__11TFlashStoreFUlPUlN22

	// the rest
	NewtonErr	VppOn(void);																	// ROM 0x000c0cf0 VppOn__11TFlashStoreFv
	void		VccOn(void);																	// ROM 0x000c12f4 VccOn__11TFlashStoreFv
	void		VccOff(void);																	// ROM 0x000c133c VccOff__11TFlashStoreFv
	NewtonErr	EraseStatus(ULong physOffset);													// ROM 0x000c0d9c EraseStatus__11TFlashStoreFUl
	NewtonErr	WaitForEraseDone(void);															// ROM 0x000c0dcc WaitForEraseDone__11TFlashStoreFv
	NewtonErr	Zap(ULong offset, ULong size);													// ROM 0x000c0e20 Zap__11TFlashStoreFUlT1
	NewtonErr	ChooseWorkingBlock(ULong size, ULong preferred);								// ROM 0x000c0f8c ChooseWorkingBlock__11TFlashStoreFUlT1
	Boolean		IsErased(ULong physOffset);														// ROM 0x000c1350 IsErased__11TFlashStoreFUl
	Boolean		IsErased(ULong offset, ULong size, ULong tolerance);							// ROM 0x000c1364 IsErased__11TFlashStoreFUlN21
	NewtonErr	SyncErase(ULong physOffset);													// ROM 0x000c13dc SyncErase__11TFlashStoreFUl
	NewtonErr	StartErase(ULong physOffset);													// ROM 0x000c1500 StartErase__11TFlashStoreFUl
	NewtonErr	NextLogEntry(ULong offset, ULong* found, ULong tag, void* image);				// ROM 0x000c2120 NextLogEntry__11TFlashStoreFUlPUlT1Pv
	NewtonErr	AddLogEntryToPhysBlock(ULong tag, ULong size, SFlashLogEntry* entry, ULong physOffset, ULong* entryOffset);	// ROM 0x000c2248 AddLogEntryToPhysBlock__11TFlashStoreFUlT1P14SFlashLogEntryT1PUl
	NewtonErr	ZapLogEntry(ULong offset);														// ROM 0x000c23a8 ZapLogEntry__11TFlashStoreFUl
	void		NotifyCompact(TFlashBlock* block);												// ROM 0x000c4fe8 NotifyCompact__11TFlashStoreFP11TFlashBlock
	ULong		Translate(ULong offset);														// ROM 0x000c50d0 Translate__11TFlashStoreFUl
	TFlashBlock*	ExchangeBlock(ULong offset, TFlashBlock* block);							// ROM 0x000c50f4 ExchangeBlock__11TFlashStoreFUlP11TFlashBlock
	ULong		StoreCapacity(void);															// ROM 0x000c510c StoreCapacity__11TFlashStoreFv
	ULong		NextLSN(void);																	// ROM 0x000c512c NextLSN__11TFlashStoreFv
	void		Add(TObjRef* ref);																// ROM 0x000c5174 Add__11TFlashStoreFP7TObjRef
	void		Remove(TObjRef* ref);															// ROM 0x000c51a4 Remove__11TFlashStoreFP7TObjRef
	NewtonErr	AddObject(PSSId id, int state, ULong size, TObjRef& ref, UChar separate, UChar xip);	// ROM 0x000c5454 AddObject__11TFlashStoreFUliT1R7TObjRefUcT5
	NewtonErr	ReplaceObject(TObjRef& old, TObjRef& replacement, char* data, long size, int oldState, int newState, int finalState);	// ROM 0x000c6874 ReplaceObject__11TFlashStoreFR7TObjRefT1PcliN25
	NewtonErr	Lookup(PSSId id, int state, TObjRef& ref);										// ROM 0x000c747c Lookup__11TFlashStoreFUliR7TObjRef
	void		InitBlocks(void);																// ROM 0x000c7650 InitBlocks__11TFlashStoreFv
	NewtonErr	Mount(void);																	// ROM 0x000c76b0 Mount__11TFlashStoreFv
	void		BlockCompacted(void);															// ROM 0x000c7794 BlockCompacted__11TFlashStoreFv
	NewtonErr	ScanLogForLogicalBlocks(UChar* separateSeen);									// ROM 0x000c77b8 ScanLogForLogicalBlocks__11TFlashStoreFPUc
	NewtonErr	ScanLogForErasures(void);														// ROM 0x000c7924 ScanLogForErasures__11TFlashStoreFv
	NewtonErr	ScanLogForReservedBlocks(void);													// ROM 0x000c7a98 ScanLogForReservedBlocks__11TFlashStoreFv
	ULong		FindPhysWritable(ULong offset, ULong end, ULong size);							// ROM 0x000c7bb0 FindPhysWritable__11TFlashStoreFUlN21
	NewtonErr	BasicWrite(ULong offset, void* buffer, ULong size);							// ROM 0x000c7c2c BasicWrite__11TFlashStoreFUlPvT1
	NewtonErr	BasicRead(ULong offset, void* buffer, ULong size);								// ROM 0x000c7d8c BasicRead__11TFlashStoreFUlPvT1
	NewtonErr	BasicCopy(ULong from, ULong to, ULong size);									// ROM 0x000c7f00 BasicCopy__11TFlashStoreFUlN21
	Boolean		IsWriteProtected(void);															// ROM 0x000c8004 IsWriteProtected__11TFlashStoreFv
	Boolean		InternalNeedsFormat(void);														// ROM 0x000c80e4 InternalNeedsFormat__11TFlashStoreFv
	ULong		FindUnusedPhysicalBlock(void);													// ROM 0x000c8204 FindUnusedPhysicalBlock__11TFlashStoreFv
	NewtonErr	BringVirginBlockOnline(ULong physOffset, ULong logicalOffset);					// ROM 0x000c826c BringVirginBlockOnline__11TFlashStoreFUlT1
	void		CalcAverageEraseCount(void);													// ROM 0x000c8384 CalcAverageEraseCount__11TFlashStoreFv
	ULong		AverageEraseCount(void);														// ROM 0x000c83e0 AverageEraseCount__11TFlashStoreFv
	void		Deinit(void);																	// ROM 0x000c83e8 Deinit__11TFlashStoreFv
	TFlashBlock*	DummyBlock(void);															// ROM 0x000c8470 DummyBlock__11TFlashStoreFv
	ULong		ObjectNumberFor(PSSId id);														// ROM 0x000c8484 ObjectNumberFor__11TFlashStoreFUl
	PSSId		PSSIDFor(long block, long objectNumber);										// ROM 0x000c8494 PSSIDFor__11TFlashStoreFlT1
	ULong		Avail(void);																	// ROM 0x000c85c4 Avail__11TFlashStoreFv
	NewtonErr	ValidateIncomingPSSID(PSSId id);												// ROM 0x000c868c ValidateIncomingPSSID__11TFlashStoreFUl
	NewtonErr	RecoveryCheck(UChar doRecovery);												// ROM 0x000c8ff8 RecoveryCheck__11TFlashStoreFUc
	NewtonErr	TransactionState(int* state);													// ROM 0x000c9190 TransactionState__11TFlashStoreFPi
	NewtonErr	StartTransaction(void);															// ROM 0x000c9288 StartTransaction__11TFlashStoreFv
	NewtonErr	DeleteTransactionRecord(void);													// ROM 0x000c933c DeleteTransactionRecord__11TFlashStoreFv
	NewtonErr	MarkCommitPoint(void);															// ROM 0x000c9410 MarkCommitPoint__11TFlashStoreFv
	NewtonErr	DoCommit(UChar useTracker);														// ROM 0x000c94f0 DoCommit__11TFlashStoreFUc
	NewtonErr	DoAbort(UChar all);																// ROM 0x000c97a0 DoAbort__11TFlashStoreFUc
	NewtonErr	LowLevelRecovery(void);															// ROM 0x000c9a50 LowLevelRecovery__11TFlashStoreFv
	void		TouchMe(void);																	// ROM 0x000c9b80 TouchMe__11TFlashStoreFv
	Boolean		IsRangeVirgin(ULong offset, ULong size);										// ROM 0x000c9bb4 IsRangeVirgin__11TFlashStoreFUlT1
	void		GC(void);																		// ROM 0x000c9c2c GC__11TFlashStoreFv
	NewtonErr	NewWithinTransaction(long size, UChar xip, PSSId* id);						// ROM 0x000ca7d0 NewWithinTransaction__11TFlashStoreFlUcPUl
	NewtonErr	SetupForModify(PSSId id, TObjRef* ref, UChar mustBeLocked, UChar startTransaction);	// ROM 0x000ca660 SetupForModify__11TFlashStoreFUlP7TObjRefUcT3
	NewtonErr	SetupForRead(PSSId id, TObjRef* ref);											// ROM 0x000cad4c SetupForRead__11TFlashStoreFUlP7TObjRef
	ULong		InternalStoreSlop(void);														// ROM 0x000ca7b4 InternalStoreSlop__11TFlashStoreFv
	void		SendAlertMgrWPBitch(int);														// NOT YET (a RAM store's; the card alerts)

	// the host's own
	NewtonErr	ReadWords(ULong offset, FlashWord* words, ULong count);			// BasicRead of big-endian words
	NewtonErr	WriteWords(ULong offset, const FlashWord* words, ULong count);	// BasicWrite of them
	TFlashBlock*	BlockAt(ULong offset)		{ return fBlocks[offset >> fBlockShift]; }
	TFlashPhysBlock*	PhysBlockAt(ULong offset)	{ return &fPhysBlocks[offset >> fBlockShift]; }
	ULong		LogSize(void)				{ return fIsSRAM ? 0x100 : 0x400; }
	int			State(int ramState)			{ return fIsSRAM ? ramState : ramState + 7; }	// the flash's is the RAM's plus seven

	TFlash*		fFlash;					// +0x10
	UChar		fIsMounted;				// +0x14
	UChar		fIsCard;				// +0x15
	UChar		fInTransaction;			// +0x16
	UChar		fNeedsRecovery;			// +0x17  a transaction (or a compaction) was left unfinished
	char*		fBase;					// +0x18  where a memory-mapped store is
	int			fSocket;				// +0x1c
	ULong		fLSN;					// +0x20
	TFlashPhysBlock*	fPhysBlocks;	// +0x24
	TFlashBlock*	fWorkingBlock;		// +0x28  where new objects go
	TFlashBlock**	fBlocks;			// +0x2c  by logical block
	TFlashBlock*	fBlockStorage;		// +0x30
	TFlashStoreLookupCache*	fCache;		// +0x34
	ULong		fUnused38;				// +0x38
	UChar		fEraseInProgress;		// +0x3c
	UChar		fIsSRAM;				// +0x3d  kept through a TStoreDriver
	UChar		fNeedsVpp;				// +0x3e
	UChar		fIsROMStore;			// +0x3f
	ULong		fErasingOffset;			// +0x40
	ULong		fAverageEraseCount;		// +0x44
	ULong		fZapWord;				// +0x48  a word with every bit written: 0
	ULong		fVirginWord;			// +0x4c  an erased word: 0xFFFFFFFF
	ULong		fBlockSize;				// +0x50
	ULong		fBlockCount;			// +0x54  physical blocks
	ULong		fBlockShift;			// +0x58
	ULong		fBlockMask;				// +0x5c
	ULong		fObjectNumberShift;		// +0x60  an id's block is its bits above this
	ULong		fQuarterBlock;			// +0x64
	ULong		fSixteenthBlock;		// +0x68
	ULong		fBucketSize;			// +0x6c  entries in a directory bucket
	ULong		fBucketCount;			// +0x70  buckets in a root directory
	long		fLockCount;				// +0x74
	TCardHandler*	fCardHandler;		// +0x78
	TObjRef*	fRefs;					// +0x7c  the references in use
	TObjRef*	fLastRef;				// +0x80
	TFlashTracker*	fTracker;			// +0x84
	TStoreDriver*	fStoreDriver;		// +0x88
	SCompactState*	fCompactState;		// +0x8c
	UChar		fMountFailed;			// +0x90  Mount found the store inconsistent: it needs formatting
	UChar		fIsInternalRAM;			// +0x91
	UChar		fCompactInProgress;		// +0x92
	UChar		fUnused93;				// +0x93
	UChar		fFormatting;			// +0x94
	UChar		fIgnoreSlop;			// +0x95  the size asked for had bit 31 set
	UChar		fSeparateSeen;			// +0x96  a separate transaction was found at mount
	UChar		fUsesTFlash;			// +0x97
	ULong		fUnused98[2];			// +0x98
	ULong		fCompactCount;			// +0xa0  compactions so far: an iterator started before one must start again
	TStoreDriver	fOwnStoreDriver;	// +0xa4
	long		fReadOnlyLockCount;		// +0xd4
	TStore*		fXIPStore;				// +0xd8  the notification's refCon starts here
	ULong		fXIPTime[2];			// +0xdc
	UChar		fXIPTouched;			// +0xe4
	UChar		fXIPNotifyRegistered;	// +0xe5
	UChar		fPadE6[2];
	int			fXIPSocket;				// +0xe8
	long		fCachedFree;			// +0xec  GetStoreSizes' used size; 1: work it out again
};

// what an id is made of: the block it was made in, then a number within it
ULong		HashPSSID(PSSId id);				// ROM 0x000c4fd8 HashPSSID__FUl
Boolean		IsValidPSSID(PSSId id);				// ROM 0x000c509c IsValidPSSID__FUl
ULong		CeilLog2(ULong value);				// ROM 0x000c5140 CeilLog2__FUl

#endif	/* __FLASHSTORE_H */
