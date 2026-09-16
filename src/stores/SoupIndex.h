/*
	File:		stores/SoupIndex.h

	Contains:	TSoupIndex, the B-tree a soup index is: keys (SKey) with
				their data (usually the entry's unique id) in 512-byte nodes
				on the store, read and written through the store wrapper's
				TNodeCache.  TAbstractSoupIndex is the interface a cursor
				uses (TUnionSoupIndex, the index over the soups of a union
				soup, implements it too: NOT YET RECONSTRUCTED).

	The formats (all big-endian on the store; the in-memory node is the
	same bytes expanded, see below):

	SKey (0x50 bytes) - a key or a datum: two header bytes then up to
	0x4e bytes of data.  Header byte 0 is flags (for a multi-key, bit n
	says sub-key n is missing; bit 7 sorts a shorter key after a longer
	one), byte 1 the size of the data in bytes.  Fixed-size key types
	(long 4, char 2, double 8) have no header: the SKey is the raw
	value; string keys are UniChars, the terminator included; a multi-key
	is the sub-keys' SKeys one after the other, each padded to an even
	size.

	KeyField - a key with its data, as it is kept in a node: a 2-byte
	header (top two bits flags: 1 = the key has duplicates, low 14 bits
	the field's total size), the key (an SKey for a variable-size type,
	the raw value for a fixed-size one, padded to an even size), then the
	data entries one after the other (each padded even).  A field with
	duplicates ends with a 2-byte count of the data entries in the field
	and the 4-byte id of the first duplicate node, a chain of nodes that
	hold the data entries that do not fit in the field (kfAssembleKeyField
	throws kNSErrKeyTooBig when key + data + 4 > 100).

	NodeHeader - a B-tree node: id, parent id, the bytes remaining
	(short), the number of keys (short) and numKeys + 1 offsets (shorts,
	node-relative) to the key fields, which are packed at the end of the
	node, each preceded by the 4-byte id of the child node to its left;
	the last offset is to an empty key field whose left child is the
	rightmost child.  On the store the node is compact - the header and
	offsets, then the key data - and is expanded when read: the key data
	moved to the end and the gap zeroed, so the offsets always mean the
	same thing.

	DupNodeHeader - a duplicates node: id, next dup node id, bytes
	remaining (short), the number of data entries (short), the offset of
	the end of the data (short), 0 (short); the data entries from 0x10.

	IndexInfo (0x1c bytes, in the store object fInfoId): the root node
	id, the node size, the key and data types, whether duplicate keys are
	allowed, the multi-key's sub-key types (4 bits each) and ascending
	bits, and whether the index is descending.

	The ROM's layout: TSoupIndex 0x44 (the vtable, the store wrapper,
	its node cache, fInfoId, the IndexInfo at +0x10, fErr at +0x2c, the
	fixed key and data sizes, the key and data compare functions and the
	sorting table).
*/

#ifndef __SOUPINDEX_H
#define __SOUPINDEX_H

#ifndef __STOREWRAPPER_H
#include "StoreWrapper.h"
#endif
#ifndef __BYTEORDER_H
#include "ByteOrder.h"
#endif

class TSortingTable;		// NOT YET RECONSTRUCTED: the collation tables


/*------------------------------------------------------------------------------
	S K e y
------------------------------------------------------------------------------*/

const int kSKeyDataSize = 0x4e;
const int kSKeySize = 0x50;

struct SKey
{
	void		Set(unsigned int size, const void* data);
	void		SetSize(short size);
	void		SetFlags(unsigned char flags);
	void		SetMissingKey(int subKey);
	Boolean		Equals(const SKey& other) const;
	SKey&		operator=(const SKey& other);
	SKey&		operator=(long value);					// a fixed-size long: the raw value
	SKey&		operator=(unsigned short value);
	SKey&		operator=(const double value);
				operator long() const;
				operator unsigned short() const;
				operator double() const;

	unsigned int	Size(void) const				{ return fHeader[1]; }
	unsigned char	Flags(void) const				{ return fHeader[0]; }
	const UByte*	Data(void) const				{ return fData; }
	UByte*			Data(void)						{ return fData; }
	void		Clear(void)							{ fHeader[0] = 0; fHeader[1] = 0; }

	UByte		fHeader[2];			// flags, size
	UByte		fData[kSKeyDataSize];
};


/*------------------------------------------------------------------------------
	K e y F i e l d
------------------------------------------------------------------------------*/

typedef UByte KeyField;

const int kKeyFieldBufferSize = 100;			// the most a field can be
const unsigned int kKeyFieldHasDups = 1;		// the flag

inline unsigned int	KeyFieldSize(const KeyField* kf)	{ return GetBigEndianHalf(kf) & 0x3fff; }
inline unsigned int	KeyFieldFlags(const KeyField* kf)	{ return GetBigEndianHalf(kf) >> 14; }
inline void	SetKeyFieldSize(KeyField* kf, unsigned int size)
			{ PutBigEndianHalf(kf, (unsigned short) ((GetBigEndianHalf(kf) & 0xc000) | (size & 0x3fff))); }
inline void	SetKeyFieldFlags(KeyField* kf, unsigned int flags)
			{ PutBigEndianHalf(kf, (unsigned short) ((flags << 14) | (GetBigEndianHalf(kf) & 0x3fff))); }
inline void	ClearKeyField(KeyField* kf)			{ SetKeyFieldSize(kf, 0); }
inline const SKey*	KeyFieldKey(const KeyField* kf)		{ return (const SKey*) (kf + 2); }
inline SKey*	KeyFieldKey(KeyField* kf)			{ return (SKey*) (kf + 2); }


/*------------------------------------------------------------------------------
	N o d e s
------------------------------------------------------------------------------*/

struct NodeHeader
{
	UByte		fId[4];				// +0x00
	UByte		fParentId[4];		// +0x04
	UByte		fBytesRemaining[2];	// +0x08
	UByte		fNumKeys[2];		// +0x0a
	UByte		fOffsets[2];		// +0x0c  [numKeys + 1] node-relative offsets of the key fields

	ULong		Id(void) const					{ return GetBigEndianWord(fId); }
	ULong		ParentId(void) const			{ return GetBigEndianWord(fParentId); }
	long		BytesRemaining(void) const		{ return GetBigEndianHalf(fBytesRemaining); }
	long		NumKeys(void) const				{ return GetBigEndianHalf(fNumKeys); }
	long		Offset(long slot) const			{ return GetBigEndianHalf(fOffsets + slot * 2); }
	void		SetId(ULong id)					{ PutBigEndianWord(fId, (unsigned int) id); }
	void		SetParentId(ULong id)			{ PutBigEndianWord(fParentId, (unsigned int) id); }
	void		SetBytesRemaining(long n)		{ PutBigEndianHalf(fBytesRemaining, (unsigned short) n); }
	void		SetNumKeys(long n)				{ PutBigEndianHalf(fNumKeys, (unsigned short) n); }
	void		SetOffset(long slot, long off)	{ PutBigEndianHalf(fOffsets + slot * 2, (unsigned short) off); }
};

const long kNodeHeaderSize = 0x0e;				// before the offsets

struct DupNodeHeader
{
	UByte		fId[4];				// +0x00
	UByte		fNextId[4];			// +0x04
	UByte		fBytesRemaining[2];	// +0x08
	UByte		fCount[2];			// +0x0a
	UByte		fBytesUsed[2];		// +0x0c  the offset of the end of the data
	UByte		fUnused[2];			// +0x0e

	ULong		Id(void) const					{ return GetBigEndianWord(fId); }
	ULong		NextId(void) const				{ return GetBigEndianWord(fNextId); }
	long		BytesRemaining(void) const		{ return GetBigEndianHalf(fBytesRemaining); }
	long		Count(void) const				{ return GetBigEndianHalf(fCount); }
	long		BytesUsed(void) const			{ return GetBigEndianHalf(fBytesUsed); }
	void		SetId(ULong id)					{ PutBigEndianWord(fId, (unsigned int) id); }
	void		SetNextId(ULong id)				{ PutBigEndianWord(fNextId, (unsigned int) id); }
	void		SetBytesRemaining(long n)		{ PutBigEndianHalf(fBytesRemaining, (unsigned short) n); }
	void		SetCount(long n)				{ PutBigEndianHalf(fCount, (unsigned short) n); }
	void		SetBytesUsed(long n)			{ PutBigEndianHalf(fBytesUsed, (unsigned short) n); }
};

const long kDupNodeHeaderSize = 0x10;


/*------------------------------------------------------------------------------
	I n d e x I n f o
------------------------------------------------------------------------------*/

enum
{
	kKeyTypeString = 0,
	kKeyTypeLong,
	kKeyTypeChar,
	kKeyTypeDouble,
	kKeyTypeASCII,			// case-folded bytes
	kKeyTypeRaw,			// memcmp
	kKeyTypeMulti,
	kNumOfKeyTypes
};

enum
{
	kIndexUniqueKeys = 0,			// a duplicate key is an error (the _uniqueID index)
	kIndexUniqueKeysNoDupData,		// likewise (the ROM's 1: any duplicate is an error)
	kIndexDuplicateKeys				// duplicate keys keep every datum
};

struct IndexInfo
{
	ULong32		fRootNodeId;		// +0x00  0: no nodes yet
	Long32		fNodeSize;			// +0x04  0x200
	Long32		fKeyType;			// +0x08  kKeyType...
	Long32		fDataType;			// +0x0c  kKeyType...
	Long32		fDuplicates;		// +0x10  kIndex...
	ULong32		fMultiTypes;		// +0x14  sub-key types, 4 bits each, sub-key 0 lowest; unused nibbles 0xf
	UByte		fMultiAscending;	// +0x18  a bit per sub-key, sub-key 0 lowest: set ascending, clear descending; unused bits set
	UByte		fDescending;		// +0x19  the whole index in descending order
	UByte		fUnused[2];			// +0x1a
};

const long kIndexInfoSize = 0x1c;
const long kSoupIndexNodeSize = 0x200;

// the on-store form (big-endian)
void	IndexInfoToStore(const IndexInfo* info, UByte* bytes);
void	IndexInfoFromStore(const UByte* bytes, long size, IndexInfo* info);


/*------------------------------------------------------------------------------
	R e s u l t s
	The index functions' results.
------------------------------------------------------------------------------*/

enum
{
	kIndexOK = 0,			// found / done
	kIndexNotFound = 2,		// the key is not there (the result key is the one after/before it)
	kIndexEnd = 3			// no key follows/precedes
};

// the errors ThrowOSErr raises for the index
const NewtonErr kIndexErrDuplicateKey = 1;		// a key already there in an index of unique keys
const NewtonErr kIndexErrNotFound = 2;			// the datum to delete is not there


/*------------------------------------------------------------------------------
	I n d e x S t a t e
	Where an iteration is: the node, the slot within it, whether the key
	there has duplicates and, when the current datum is in a dup node,
	that node.
------------------------------------------------------------------------------*/

struct IndexState
{
	NodeHeader*		fNode;			// +0x00
	long			fSlot;			// +0x04
	Boolean			fHasDups;		// +0x08
	DupNodeHeader*	fDupNode;		// +0x0c
};


/*------------------------------------------------------------------------------
	T A b s t r a c t S o u p I n d e x
------------------------------------------------------------------------------*/

// the stop function TSoupIndex::Search calls for each key: ==> true to stop there
typedef int (*IndexStopProcPtr)(SKey* key, SKey* data, void* refCon);

class TAbstractSoupIndex
{
public:
	virtual int	Find(SKey* key, SKey* outKey, SKey* outData, Boolean exact) = 0;
	virtual int	First(SKey* outKey, SKey* outData) = 0;
	virtual int	Last(SKey* outKey, SKey* outData) = 0;
	virtual int	Next(SKey* key, SKey* data, int mode, SKey* outKey, SKey* outData) = 0;
	virtual int	Prior(SKey* key, SKey* data, Boolean skipDups, SKey* outKey, SKey* outData) = 0;

	int			FindPrior(SKey* key, SKey* outKey, SKey* outData, Boolean exact, Boolean prior);
};

// Next's mode
enum
{
	kIndexNextDupOrKey = 0,		// the next datum of the key, else the next key
	kIndexNextKey,				// the next key
	kIndexNextDup				// the next datum of the key only
};


/*------------------------------------------------------------------------------
	T S o u p I n d e x
------------------------------------------------------------------------------*/

class TSoupIndex;
typedef int (TSoupIndex::*KeyCompareProcPtr)(const SKey& a, const SKey& b);

class TSoupIndex : public TAbstractSoupIndex
{
public:
	static ULong	Create(TStoreWrapper* wrapper, IndexInfo* info);		// ==> the info object's id
	void		Init(TStoreWrapper* wrapper, PSSId infoId, const TSortingTable* sortingTable);
	NewtonErr	ReadInfo(void);
	void		StoreAborted(void);
	void		Destroy(void);
	long		TotalSize(void);

	// the interface
	int			Add(SKey* key, SKey* data);
	int			AddInTransaction(SKey* key, SKey* data);
	int			Delete(SKey* key, SKey* data);
	int			Find(SKey* key, SKey* outKey, SKey* outData, Boolean exact);
	int			First(SKey* outKey, SKey* outData);
	int			Last(SKey* outKey, SKey* outData);
	int			Next(SKey* key, SKey* data, int mode, SKey* outKey, SKey* outData);
	int			Prior(SKey* key, SKey* data, Boolean skipDups, SKey* outKey, SKey* outData);
	int			Search(Boolean forward, SKey* key, SKey* data, IndexStopProcPtr stop, void* refCon, SKey* outKey, SKey* outData);

	// iteration by state
	int			FindAndGetState(KeyField* kf, IndexState* state);
	int			FindLastAndGetState(KeyField* kf, IndexState* state);
	int			FindPriorAndGetState(KeyField* kf, Boolean movePrior, IndexState* state);
	int			MoveAndGetState(Boolean forward, int mode, KeyField* kf, IndexState* state);
	int			MoveUsingState(Boolean forward, int mode, KeyField* kf, IndexState* state);

	// key fields
	KeyField*	kfAssembleKeyField(KeyField* kf, const void* key, const void* data);
	void		kfDisassembleKeyField(KeyField* kf, SKey* outKey, SKey* outData);
	long		kfSizeOfKey(const void* key);
	long		kfSizeOfData(const void* data);
	void*		kfFirstDataAdr(KeyField* kf);
	Boolean		kfNextDataAdr(KeyField* kf, void* data, void** next);
	void*		kfLastDataAdr(KeyField* kf);
	long		kfDupCount(KeyField* kf);
	ULong		kfNextDupID(KeyField* kf);
	void		kfSetDupCount(KeyField* kf, short count);
	void		kfSetNextDupID(KeyField* kf, ULong id);
	void		kfInsertData(KeyField* kf, void* where, const void* data);
	void		kfDeleteData(KeyField* kf, void* data);
	void		kfReplaceFirstData(KeyField* kf, const void* data);
	void*		kfFindDataAdr(KeyField* kf, const void* data, void** prior);
	void		kfConvertKeyField(long toDups, KeyField* kf);

	// nodes
	NodeHeader*	ReadRootNode(Boolean create);
	void		SetRootNode(ULong id);
	NodeHeader*	ReadANode(ULong id, ULong parentId);
	DupNodeHeader*	ReadADupNode(ULong id);
	void		ChangeNode(NodeHeader* node);
	void		UpdateNode(NodeHeader* node);
	void		UpdateDupNode(NodeHeader* node);
	NodeHeader*	NewNode(void);
	DupNodeHeader*	NewDupNode(void);
	void		DeleteNode(ULong id);
	void		InitNode(NodeHeader* node, ULong id);
	void		CreateFirstRoot(void);
	void		CreateNewRoot(KeyField* kf, ULong rightId);

	KeyField*	KeyFieldAdr(NodeHeader* node, long slot);
	ULong		LeftNodeNo(NodeHeader* node, long slot);
	ULong		RightNodeNo(NodeHeader* node, long slot);
	ULong		FirstNodeNo(NodeHeader* node);
	ULong		LastNodeNo(NodeHeader* node);
	void		SetNodeNo(NodeHeader* node, long slot, ULong id);
	KeyField*	FirstKeyField(NodeHeader* node);
	KeyField*	LastKeyField(NodeHeader* node);
	UByte*		KeyFieldBase(NodeHeader* node);
	Boolean		MoveKey(KeyField* from, KeyField* to);
	void		CopyKeyFmNode(KeyField* kf, ULong* leftId, NodeHeader* node, long slot);
	KeyField*	KeyAfterNodeNo(NodeHeader* node, ULong id, long* slot);
	KeyField*	KeyBeforeNodeNo(NodeHeader* node, ULong id, long* slot);
	Boolean		KeyInNode(KeyField* kf, NodeHeader* node, ULong* childId, long* slot);
	long		LastSlotInNode(NodeHeader* node);
	long		BytesInNode(NodeHeader* node);
	Boolean		RoomInNode(NodeHeader* node, KeyField* kf);
	Boolean		NodeUnderflow(NodeHeader* node);
	void		PutKeyIntoNode(KeyField* kf, ULong rightId, NodeHeader* node, long slot);
	void		DeleteKeyFromNode(NodeHeader* node, long slot);

	// searching
	Boolean		FindNextKey(KeyField* kf, NodeHeader** node, long* slot);
	Boolean		FindPriorKey(KeyField* kf, NodeHeader** node, long* slot);
	Boolean		FindFirstKey(NodeHeader* node, KeyField* kf);
	Boolean		FindLastKey(NodeHeader* node, KeyField* kf);
	Boolean		Search(KeyField* kf, NodeHeader** node, long* slot);
	int			SearchNext(KeyField* kf, NodeHeader** node, long* slot);
	int			SearchPrior(KeyField* kf, NodeHeader** node, long* slot);
	int			SearchNextDup(KeyField* kf, NodeHeader** node, long* slot, DupNodeHeader** dupNode);
	int			SearchPriorDup(KeyField* kf, NodeHeader** node, long* slot, DupNodeHeader** dupNode);

	// duplicates
	void*		FirstDupDataAdr(DupNodeHeader* node);
	Boolean		NextDupDataAdr(DupNodeHeader* node, void* data, void** next);
	void*		LastDupDataAdr(KeyField* kf, DupNodeHeader** dupNode);
	Boolean		AppendDupData(DupNodeHeader* node, const void* data);
	Boolean		PrependDupData(DupNodeHeader* node, const void* data);
	Boolean		DeleteDupData(DupNodeHeader* node, void* data);
	void*		FindDupDataAdr(DupNodeHeader* node, const void* data, void** prior);
	void*		FindNextDupDataAdr(DupNodeHeader** node, const void* data, Boolean* found);
	void*		FindPriorDupDataAdr(DupNodeHeader** node, const void* data, Boolean* found);
	void		CheckForDupData(KeyField* kf, const void* data);
	void		StoreDupData(KeyField* kf, const void* data);
	void		InsertDupData(KeyField* kf, NodeHeader* node, long slot, ULong* rightId, Boolean* split);

	// insertion and deletion
	Boolean		InsertKey(KeyField* kf, NodeHeader* node, ULong* rightId, Boolean* split);
	void		InsertAfterDelete(KeyField* kf, ULong rightId, NodeHeader* node);
	void		SplitANode(KeyField* kf, ULong* rightId, NodeHeader* node, long slot);
	int			MergeTwoNodes(KeyField* kf, NodeHeader* parent, NodeHeader* left, NodeHeader* right);
	Boolean		BalanceTwoNodes(NodeHeader* parent, NodeHeader* child, long slot);
	Boolean		GetLeafKey(KeyField* kf, NodeHeader* node);
	void		DeleteTheKey(NodeHeader* node, long slot, KeyField* kf);
	Boolean		DeleteKey(KeyField* kf, NodeHeader* node, Boolean* underflow);
	void		FreeNodes(NodeHeader* node);
	void		FreeDupNodes(KeyField* kf);
	void		NodeSize(NodeHeader* node, long& size);
	void		DupNodeSize(KeyField* kf, long& size);

	int			_BTEnterKey(KeyField* kf);
	int			_BTRemoveKey(KeyField* kf);
	int			_BTGetNextKey(KeyField* kf);
	int			_BTGetPriorKey(KeyField* kf);
	int			_BTGetNextDupKey(KeyField* kf);
	int			_BTGetPriorDupKey(KeyField* kf);

	// comparison
	int			CompareKeys(const SKey& a, const SKey& b);
	int			StringKeyCompare(const SKey& a, const SKey& b);
	int			LongKeyCompare(const SKey& a, const SKey& b);
	int			CharacterKeyCompare(const SKey& a, const SKey& b);
	int			DoubleKeyCompare(const SKey& a, const SKey& b);
	int			ASCIIKeyCompare(const SKey& a, const SKey& b);
	int			RawKeyCompare(const SKey& a, const SKey& b);
	int			MultiKeyCompare(const SKey& a, const SKey& b);
	int			CompareData(const void* a, const void* b)	{ return (this->*fDataCompare)(*(const SKey*) a, *(const SKey*) b); }

	static const KeyCompareProcPtr	fKeyCompareFns[kNumOfKeyTypes];
	static const short				fKeySizes[kNumOfKeyTypes];

	TStoreWrapper*	fStoreWrapper;		// +0x04
	TNodeCache*		fNodeCache;			// +0x08  the wrapper's
	PSSId			fInfoId;			// +0x0c
	IndexInfo		fInfo;				// +0x10
	long			fErr;				// +0x2c  _BTEnterKey's / _BTRemoveKey's result
	long			fFixedKeySize;		// +0x30  0 for a variable-size key type
	long			fFixedDataSize;		// +0x34
	KeyCompareProcPtr	fKeyCompare;	// +0x38
	KeyCompareProcPtr	fDataCompare;	// +0x3c
	const TSortingTable*	fSortingTable;	// +0x40

private:
	static KeyField	theKeyField[kKeyFieldBufferSize];		// the field being added, deleted or looked for
	static KeyField	savedKey[kKeyFieldBufferSize];			// DeleteKey's key to re-add after a leaf underflows
	static KeyField	leafKey[kKeyFieldBufferSize];			// the leaf key that replaces a deleted interior key
};

#endif	/* __SOUPINDEX_H */
