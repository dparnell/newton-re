/*
	File:		stores/SoupIndex.cpp

	Contains:	TSoupIndex (SoupIndex.h), the soup index B-tree: SKey, the
				key field and node primitives, the search, insertion and
				deletion of keys, duplicate data and the interface the
				cursors use; TNodeCache's Commit and DeleteNode, which write
				and delete nodes through the index.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The ROM keeps nodes and keys in its own (big-endian) byte order; on
	the host every on-store word is read and written big-endian
	(ByteOrder.h) so that a store image means the same thing everywhere.
	The one other deviation: the data copied into a field is copied
	unpadded and the pad byte zeroed (the ROM copies the padded size,
	taking a byte past the datum).
*/

#include "SoupIndex.h"
#include "RichString.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include <string.h>
#include <stdint.h>

extern const ExceptionName exStoreError;		// "evt.ex.fr.store"



// the key types' compare functions and fixed sizes
// ROM 0x0c102508 fKeyCompareFns__10TSoupIndex
// ROM 0x0c102524 fKeySizes__10TSoupIndex
// (read with tools/newton-rom/analysis/romtable.py build/MP2100D
// fKeyCompareFns__10TSoupIndex:u32:7 fKeySizes__10TSoupIndex:u16:7 - the
// functions are jump table slots, named in build/MP2100D/symbols.txt)
const KeyCompareProcPtr TSoupIndex::fKeyCompareFns[kNumOfKeyTypes] = {
	&TSoupIndex::StringKeyCompare,
	&TSoupIndex::LongKeyCompare,
	&TSoupIndex::CharacterKeyCompare,
	&TSoupIndex::DoubleKeyCompare,
	&TSoupIndex::ASCIIKeyCompare,
	&TSoupIndex::RawKeyCompare,
	&TSoupIndex::MultiKeyCompare
};

const short TSoupIndex::fKeySizes[kNumOfKeyTypes] = { 0, 4, 2, 8, 0, 0, 0 };

// ROM 0x0c104b50 keyFieldBuffer__10TSoupIndex (theKeyField, savedKey and
// leafKey point at three of these)
KeyField	TSoupIndex::theKeyField[kKeyFieldBufferSize];
KeyField	TSoupIndex::savedKey[kKeyFieldBufferSize];
KeyField	TSoupIndex::leafKey[kKeyFieldBufferSize];


static inline long
PadEven(long size)
{
	return (size & 1) ? size + 1 : size;
}


/*------------------------------------------------------------------------------
	S K e y
------------------------------------------------------------------------------*/

// ROM 0x002c0a10 Set__4SKeyFUiPv
// size bytes of data, at most kSKeyDataSize.
void
SKey::Set(unsigned int size, const void* data)
{
	if (size > kSKeyDataSize)
		size = kSKeyDataSize;
	SetSize((short) size);
	memcpy(fData, data, size);
}


// ROM 0x002c15d4 SetSize__4SKeyFs
// The size; the flags are kept.
void
SKey::SetSize(short size)
{
	fHeader[1] = (UByte) size;
	fHeader[0] |= (UByte) ((unsigned short) size >> 8);
}


// ROM 0x002c6440 SetFlags__4SKeyFUc
void
SKey::SetFlags(unsigned char flags)
{
	fHeader[0] = flags;
}


// ROM 0x002c0a78 SetMissingKey__4SKeyFi
// Sub-key n of a multi-key is missing.
void
SKey::SetMissingKey(int subKey)
{
	fHeader[0] |= (UByte) (1 << subKey);
}


// ROM 0x002c5ac0 Equals__4SKeyCFRC4SKey
// The same flags, size and data.
Boolean
SKey::Equals(const SKey& other) const
{
	return fHeader[1] == other.fHeader[1]
		&& fHeader[0] == other.fHeader[0]
		&& memcmp(fData, other.fData, fHeader[1]) == 0;
}


// ROM 0x002c0a58 __as__4SKeyFRC4SKey
SKey&
SKey::operator=(const SKey& other)
{
	memcpy((void*) this, (const void*) &other, kSKeySize);
	return *this;
}


// ROM 0x002c3e60 __as__4SKeyFl
// A fixed-size long key is the raw value, from the first byte.
SKey&
SKey::operator=(long value)
{
	PutBigEndianWord(this, (unsigned int) value);
	return *this;
}


// ROM 0x002c47f4 __as__4SKeyFUs
SKey&
SKey::operator=(unsigned short value)
{
	PutBigEndianHalf(this, value);
	return *this;
}


// ROM 0x002c5340 __as__4SKeyFCd
// Eight big-endian bytes (the ARM FPA's double is IEEE, high word first).
SKey&
SKey::operator=(const double value)
{
	UByte bytes[8];
	memcpy(bytes, &value, 8);
	UByte* dst = (UByte*) this;
	if (HostIsBigEndian())
		memcpy(dst, bytes, 8);
	else
		for (int i = 0; i < 8; i++)
			dst[i] = bytes[7 - i];
	return *this;
}


// ROM 0x002c44e8 __opl__4SKeyCFv
SKey::operator long() const
{
	return (long) (Long32) GetBigEndianWord(this);
}


// ROM 0x002c4e1c __opUs__4SKeyCFv
SKey::operator unsigned short() const
{
	return GetBigEndianHalf(this);
}


// ROM 0x002c558c __opd__4SKeyCFv
SKey::operator double() const
{
	const UByte* src = (const UByte*) this;
	UByte bytes[8];
	if (HostIsBigEndian())
		memcpy(bytes, src, 8);
	else
		for (int i = 0; i < 8; i++)
			bytes[i] = src[7 - i];
	double value;
	memcpy(&value, bytes, 8);
	return value;
}


/*------------------------------------------------------------------------------
	I n d e x I n f o
	The on-store form: the ROM writes the struct's bytes; the host writes
	the words big-endian.
------------------------------------------------------------------------------*/

void
IndexInfoToStore(const IndexInfo* info, UByte* bytes)
{
	PutBigEndianWord(bytes + 0x00, info->fRootNodeId);
	PutBigEndianWord(bytes + 0x04, (unsigned int) info->fNodeSize);
	PutBigEndianWord(bytes + 0x08, (unsigned int) info->fKeyType);
	PutBigEndianWord(bytes + 0x0c, (unsigned int) info->fDataType);
	PutBigEndianWord(bytes + 0x10, (unsigned int) info->fDuplicates);
	PutBigEndianWord(bytes + 0x14, info->fMultiTypes);
	bytes[0x18] = info->fMultiAscending;
	bytes[0x19] = info->fDescending;
	bytes[0x1a] = info->fUnused[0];
	bytes[0x1b] = info->fUnused[1];
}


// size bytes of the object are there (an older, shorter info leaves the
// rest as it was)
void
IndexInfoFromStore(const UByte* bytes, long size, IndexInfo* info)
{
	UByte whole[kIndexInfoSize];
	IndexInfoToStore(info, whole);
	if (size > kIndexInfoSize)
		size = kIndexInfoSize;
	memcpy(whole, bytes, size);
	info->fRootNodeId = GetBigEndianWord(whole + 0x00);
	info->fNodeSize = (Long32) GetBigEndianWord(whole + 0x04);
	info->fKeyType = (Long32) GetBigEndianWord(whole + 0x08);
	info->fDataType = (Long32) GetBigEndianWord(whole + 0x0c);
	info->fDuplicates = (Long32) GetBigEndianWord(whole + 0x10);
	info->fMultiTypes = GetBigEndianWord(whole + 0x14);
	info->fMultiAscending = whole[0x18];
	info->fDescending = whole[0x19];
	info->fUnused[0] = whole[0x1a];
	info->fUnused[1] = whole[0x1b];
}


/*------------------------------------------------------------------------------
	T A b s t r a c t S o u p I n d e x
------------------------------------------------------------------------------*/

// ROM 0x002c41c4 FindPrior__18TAbstractSoupIndexFP4SKeyN21UcT4
// Find the key; when it is not there, the key before it (prior) or after
// it; when it is there, the one before or after it.  ==> Find's result.
int
TAbstractSoupIndex::FindPrior(SKey* key, SKey* outKey, SKey* outData, Boolean exact, Boolean prior)
{
	int result = Find(key, outKey, outData, exact);
	if (result == kIndexOK)
	{
		if (prior)
			Prior(outKey, outData, false, outKey, outData);
		else
			Next(outKey, outData, kIndexNextKey, outKey, outData);
	}
	else if (result == kIndexNotFound)
		Prior(outKey, outData, false, outKey, outData);
	else if (result == kIndexEnd)
		Last(outKey, outData);
	return result;
}


/*------------------------------------------------------------------------------
	T S o u p I n d e x
	Creation.
------------------------------------------------------------------------------*/

// ROM 0x002c1bf8 Create__10TSoupIndexSFP13TStoreWrapperP9IndexInfo
// A new index's info object: no root, 512-byte nodes.  ==> its id.
ULong
TSoupIndex::Create(TStoreWrapper* wrapper, IndexInfo* info)
{
	info->fRootNodeId = 0;
	info->fNodeSize = kSoupIndexNodeSize;
	info->fUnused[0] = 0;
	info->fUnused[1] = 0;
	UByte bytes[kIndexInfoSize];
	IndexInfoToStore(info, bytes);
	PSSId id;
	OSErrIf(wrapper->Store()->NewObject((char*) bytes, kIndexInfoSize, &id));
	return id;
}


// ROM 0x002c1c3c ReadInfo__10TSoupIndexFv
// The info from the store; an older, shorter one is not descending.
NewtonErr
TSoupIndex::ReadInfo(void)
{
	long size;
	NewtonErr err = fStoreWrapper->Store()->GetObjectSize(fInfoId, &size);
	if (err == noErr)
	{
		if (size < kIndexInfoSize + 1)
			fInfo.fDescending = 0;
		else
			size = kIndexInfoSize;
		UByte bytes[kIndexInfoSize];
		err = fStoreWrapper->Store()->Read(fInfoId, 0, (char*) bytes, size);
		IndexInfoFromStore(bytes, size, &fInfo);
	}
	return err;
}


// The info to the store (SetRootNode's and CreateFirstRoot's write).
static void
WriteInfo(TStoreWrapper* wrapper, PSSId infoId, const IndexInfo* info)
{
	long size;
	OSErrIf(wrapper->Store()->GetObjectSize(infoId, &size));
	if (size > kIndexInfoSize)
		size = kIndexInfoSize;
	UByte bytes[kIndexInfoSize];
	IndexInfoToStore(info, bytes);
	OSErrIf(wrapper->Store()->Write(infoId, 0, (char*) bytes, size));
}


// ROM 0x002c1cac Init__10TSoupIndexFP13TStoreWrapperUlPC13TSortingTable
// The index over the info object infoId of the wrapper's store.
void
TSoupIndex::Init(TStoreWrapper* wrapper, PSSId infoId, const TSortingTable* sortingTable)
{
	fStoreWrapper = wrapper;
	fInfoId = infoId;
	fSortingTable = sortingTable;
	fNodeCache = &wrapper->fNodeCache;
	OSErrIf(ReadInfo());
	fErr = 0;
	fKeyCompare = fKeyCompareFns[fInfo.fKeyType];
	fDataCompare = fKeyCompareFns[fInfo.fDataType];
	fFixedKeySize = fKeySizes[fInfo.fKeyType];
	fFixedDataSize = fKeySizes[fInfo.fDataType];
}


// ROM 0x002c1d44 StoreAborted__10TSoupIndexFv
// The store's transaction was aborted: the cached nodes are dropped and
// the info re-read.
void
TSoupIndex::StoreAborted(void)
{
	fNodeCache->Abort(this);
	ReadInfo();
}


// ROM 0x002c2c50 Destroy__10TSoupIndexFv
// Every node freed (the info object stays, rootless).
void
TSoupIndex::Destroy(void)
{
	if (fInfo.fRootNodeId != 0)
	{
		newton_try
		{
			NodeHeader* root = ReadRootNode(false);
			if (root != nil)
				FreeNodes(root);
			SetRootNode(0);
		}
		newton_catch_all
		{
			fNodeCache->Abort(this);
			rethrow;
		}
		end_try;
	}
	fNodeCache->Commit(this);
}


// ROM 0x002c2e4c TotalSize__10TSoupIndexFv
// The size of the index's objects on the store.
long
TSoupIndex::TotalSize(void)
{
	long size;
	OSErrIf(fStoreWrapper->Store()->GetObjectSize(fInfoId, &size));
	if (fInfo.fRootNodeId != 0)
	{
		newton_try
		{
			NodeHeader* root = ReadRootNode(false);
			if (root != nil)
				NodeSize(root, size);
		}
		newton_catch_all
		{
			fNodeCache->Abort(this);
			rethrow;
		}
		end_try;
	}
	fNodeCache->Commit(this);
	return size;
}


// ROM 0x002c2ce8 NodeSize__10TSoupIndexFP10NodeHeaderRl
// The node's object size plus its dup nodes' and its subtrees'; the node
// forgotten from the cache.
void
TSoupIndex::NodeSize(NodeHeader* node, long& size)
{
	long objectSize;
	OSErrIf(fStoreWrapper->Store()->GetObjectSize(node->Id(), &objectSize));
	size += objectSize;
	long numKeys = node->NumKeys();
	for (long slot = 0; slot <= numKeys; slot++)
	{
		KeyField* kf = KeyFieldAdr(node, slot);
		if (KeyFieldFlags(kf) == kKeyFieldHasDups && KeyFieldSize(kf) != 0)
			DupNodeSize(kf, size);
		ULong childId = LeftNodeNo(node, slot);
		if (childId != 0)
			NodeSize(ReadANode(childId, node->Id()), size);
	}
	fNodeCache->ForgetNode(node->Id());
}


// ROM 0x002c2dcc DupNodeSize__10TSoupIndexFP8KeyFieldRl
void
TSoupIndex::DupNodeSize(KeyField* kf, long& size)
{
	ULong id = kfNextDupID(kf);
	while (id != 0)
	{
		DupNodeHeader* dupNode = ReadADupNode(id);
		id = dupNode->NextId();
		long objectSize;
		OSErrIf(fStoreWrapper->Store()->GetObjectSize(dupNode->Id(), &objectSize));
		size += objectSize;
		fNodeCache->ForgetNode(dupNode->Id());
	}
}


/*------------------------------------------------------------------------------
	C o m p a r i s o n
------------------------------------------------------------------------------*/

// ROM 0x002c17fc CompareKeys__10TSoupIndexFRC4SKeyT1
// The key type's compare, reversed for a descending index.
int
TSoupIndex::CompareKeys(const SKey& a, const SKey& b)
{
	int result = (this->*fKeyCompare)(a, b);
	if (fInfo.fDescending)
		result = -result;
	return result;
}


// ROM 0x002c1824 StringKeyCompare__10TSoupIndexFRC4SKeyT1
// The collation compare of the texts (the sorting table: NOT YET
// RECONSTRUCTED - letters are folded); the keys' UniChars are big-endian.
int
TSoupIndex::StringKeyCompare(const SKey& a, const SKey& b)
{
	long aLength = a.Size() / sizeof(UniChar);
	long bLength = b.Size() / sizeof(UniChar);
	UniChar aText[kSKeyDataSize / sizeof(UniChar)];
	UniChar bText[kSKeyDataSize / sizeof(UniChar)];
	memcpy(aText, a.Data(), aLength * sizeof(UniChar));
	memcpy(bText, b.Data(), bLength * sizeof(UniChar));
	SwapUniChars(aText, aLength);
	SwapUniChars(bText, bLength);
	return CompareUnicodeText(aText, aLength, bText, bLength, false);
}


// ROM 0x002c1880 LongKeyCompare__10TSoupIndexFRC4SKeyT1
int
TSoupIndex::LongKeyCompare(const SKey& a, const SKey& b)
{
	return (int) ((long) a - (long) b);
}


// ROM 0x002c18ac CharacterKeyCompare__10TSoupIndexFRC4SKeyT1
int
TSoupIndex::CharacterKeyCompare(const SKey& a, const SKey& b)
{
	int ca = (unsigned short) a;
	int cb = (unsigned short) b;
	if (ca < cb)
		return -1;
	return ca > cb ? 1 : 0;
}


// ROM 0x002c18e8 DoubleKeyCompare__10TSoupIndexFRC4SKeyT1
int
TSoupIndex::DoubleKeyCompare(const SKey& a, const SKey& b)
{
	double da = (double) a;
	double db = (double) b;
	if (da < db)
		return -1;
	return da > db ? 1 : 0;
}


// ROM 0x002c192c ASCIIKeyCompare__10TSoupIndexFRC4SKeyT1
// Byte by byte with upper case folded to lower; a shorter key is less.
int
TSoupIndex::ASCIIKeyCompare(const SKey& a, const SKey& b)
{
	long aSize = a.Size();
	long bSize = b.Size();
	long i = 0;
	for ( ; i < aSize && i < bSize; i++)
	{
		unsigned char ca = a.Data()[i];
		if (ca >= 'A' && ca <= 'Z')
			ca += 'a' - 'A';
		unsigned char cb = b.Data()[i];
		if (cb >= 'A' && cb <= 'Z')
			cb += 'a' - 'A';
		if (ca < cb)
			return -1;
		if (ca > cb)
			return 1;
	}
	if (i < aSize)
		return 1;
	if (i < bSize)
		return -1;
	return 0;
}


// ROM 0x002c19d0 RawKeyCompare__10TSoupIndexFRC4SKeyT1
// memcmp over the shorter size; a shorter key is less.
int
TSoupIndex::RawKeyCompare(const SKey& a, const SKey& b)
{
	unsigned int aSize = a.Size();
	unsigned int bSize = b.Size();
	int sizeOrder;
	unsigned int size;
	if (aSize < bSize)
		sizeOrder = -1, size = aSize;
	else if (aSize > bSize)
		sizeOrder = 1, size = bSize;
	else
		sizeOrder = 0, size = aSize;
	int result = memcmp(a.Data(), b.Data(), size);
	return result != 0 ? result : sizeOrder;
}


// ROM 0x002c1a2c MultiKeyCompare__10TSoupIndexFRC4SKeyT1
// Sub-key by sub-key, each by its type; a missing sub-key is less than a
// present one; a key that runs out first is less unless its flag bit 7
// says otherwise; each sub-key's order is reversed unless its ascending
// bit is set.
int
TSoupIndex::MultiKeyCompare(const SKey& a, const SKey& b)
{
	ULong types = fInfo.fMultiTypes;
	unsigned int ascending = fInfo.fMultiAscending;
	const UByte* pa = a.Data();
	const UByte* aEnd = pa + a.Size();
	const UByte* pb = b.Data();
	const UByte* bEnd = pb + b.Size();
	unsigned int aMissing = a.Flags();
	unsigned int bMissing = b.Flags();
	for ( ; ; )
	{
		if (pa == aEnd)
		{
			if (pb == bEnd)
				return 0;
			return (a.Flags() & 0x80) ? 1 : -1;
		}
		if (pb == bEnd)
			return (b.Flags() & 0x80) ? -1 : 1;
		int result;
		if (((aMissing | bMissing) & 1) == 0)
		{
			long type = types & 0x0f;
			result = (this->*fKeyCompareFns[type])(*(const SKey*) pa, *(const SKey*) pb);
			if (result == 0)
			{
				long size = fKeySizes[type];
				if (size == 0)
				{
					pa += PadEven(pa[1] + 2);
					pb += PadEven(pb[1] + 2);
				}
				else
				{
					pa += size;
					pb += size;
				}
			}
		}
		else if ((aMissing & 1) == 0)
			result = 1;
		else if ((bMissing & 1) == 0)
			result = -1;
		else
			result = 0;
		if (result != 0)
			return (ascending & 1) ? result : -result;
		types >>= 4;
		ascending >>= 1;
		aMissing >>= 1;
		bMissing >>= 1;
	}
}


/*------------------------------------------------------------------------------
	K e y   f i e l d s
------------------------------------------------------------------------------*/

// ROM 0x002c46e0 kfSizeOfKey__10TSoupIndexFPv
// The size of a key: the fixed size, else the SKey's header and data.
long
TSoupIndex::kfSizeOfKey(const void* key)
{
	if (fFixedKeySize != 0)
		return fFixedKeySize;
	return ((const UByte*) key)[1] + 2;
}


// ROM 0x002c4704 kfSizeOfData__10TSoupIndexFPv
long
TSoupIndex::kfSizeOfData(const void* data)
{
	if (fFixedDataSize != 0)
		return fFixedDataSize;
	return ((const UByte*) data)[1] + 2;
}


// ROM 0x002c4728 kfFirstDataAdr__10TSoupIndexFP8KeyField
// The first datum: after the key, padded even.
void*
TSoupIndex::kfFirstDataAdr(KeyField* kf)
{
	return kf + 2 + PadEven(kfSizeOfKey(kf + 2));
}


// ROM 0x002c4760 kfNextDataAdr__10TSoupIndexFP8KeyFieldPvPPv
// The datum after data (the first for nil) in next; ==> whether there
// is one (the data end 6 bytes before the field's end when it has
// duplicates; a single datum ends at the end).
Boolean
TSoupIndex::kfNextDataAdr(KeyField* kf, void* data, void** next)
{
	if (data == nil)
	{
		*next = kfFirstDataAdr(kf);
		return true;
	}
	UByte* p = (UByte*) data + PadEven(kfSizeOfData(data));
	*next = p;
	return p < kf + KeyFieldSize(kf) - 6;
}


// ROM 0x002c483c kfLastDataAdr__10TSoupIndexFP8KeyField
void*
TSoupIndex::kfLastDataAdr(KeyField* kf)
{
	UByte* end = kf + KeyFieldSize(kf) - 6;
	UByte* p = (UByte*) kfFirstDataAdr(kf);
	UByte* last;
	do {
		last = p;
		p += PadEven(kfSizeOfData(last));
	} while (p < end);
	return last;
}


// ROM 0x002c48a0 kfDupCount__10TSoupIndexFP8KeyField
// The number of data in the field.
long
TSoupIndex::kfDupCount(KeyField* kf)
{
	if (KeyFieldFlags(kf) != kKeyFieldHasDups)
		return 1;
	return (short) GetBigEndianHalf(kf + KeyFieldSize(kf) - 6);
}


// ROM 0x002c48c8 kfNextDupID__10TSoupIndexFP8KeyField
// The first dup node's id; 0 for none.
ULong
TSoupIndex::kfNextDupID(KeyField* kf)
{
	if (KeyFieldFlags(kf) != kKeyFieldHasDups)
		return 0;
	return GetBigEndianWord(kf + KeyFieldSize(kf) - 4);
}


// ROM 0x002c49f8 kfSetDupCount__10TSoupIndexFP8KeyFields
void
TSoupIndex::kfSetDupCount(KeyField* kf, short count)
{
	if (KeyFieldFlags(kf) == kKeyFieldHasDups)
		PutBigEndianHalf(kf + KeyFieldSize(kf) - 6, (unsigned short) count);
}


// ROM 0x002c4a30 kfSetNextDupID__10TSoupIndexFP8KeyFieldUl
void
TSoupIndex::kfSetNextDupID(KeyField* kf, ULong id)
{
	if (KeyFieldFlags(kf) == kKeyFieldHasDups)
		PutBigEndianWord(kf + KeyFieldSize(kf) - 4, (unsigned int) id);
}


// The datum copied into a field: its bytes, the pad byte zeroed.
static void
CopyDatum(void* to, const void* data, long size)
{
	memcpy(to, data, size);
	if (size & 1)
		((UByte*) to)[size] = 0;
}


// ROM 0x002c492c kfInsertData__10TSoupIndexFP8KeyFieldPvT2
// The datum inserted at where in a field with duplicates.
void
TSoupIndex::kfInsertData(KeyField* kf, void* where, const void* data)
{
	long size = KeyFieldSize(kf);
	long dataSize = kfSizeOfData(data);
	long paddedSize = PadEven(dataSize);
	if (KeyFieldFlags(kf) != kKeyFieldHasDups)
		return;
	memmove((UByte*) where + paddedSize, where, kf + size - (UByte*) where);
	CopyDatum(where, data, dataSize);
	SetKeyFieldSize(kf, size + paddedSize);
	kfSetDupCount(kf, (short) (kfDupCount(kf) + 1));
}


// ROM 0x002c4aa0 kfDeleteData__10TSoupIndexFP8KeyFieldPv
// The datum removed from a field with duplicates.
void
TSoupIndex::kfDeleteData(KeyField* kf, void* data)
{
	long paddedSize = PadEven(kfSizeOfData(data));
	if (KeyFieldFlags(kf) != kKeyFieldHasDups)
		return;
	UByte* after = (UByte*) data + paddedSize;
	memmove(data, after, kf + KeyFieldSize(kf) - after);
	SetKeyFieldSize(kf, KeyFieldSize(kf) - paddedSize);
	kfSetDupCount(kf, (short) (kfDupCount(kf) - 1));
}


// ROM 0x002c4b50 kfReplaceFirstData__10TSoupIndexFP8KeyFieldPv
// The field's first datum replaced by data (the rest moved to fit).
void
TSoupIndex::kfReplaceFirstData(KeyField* kf, const void* data)
{
	long dataOffset = PadEven(kfSizeOfKey(kf + 2) + 2);
	long dataSize = kfSizeOfData(data);
	long paddedSize = PadEven(dataSize);
	unsigned int flags = KeyFieldFlags(kf);
	if (flags == 0)
	{
		SetKeyFieldSize(kf, dataOffset + paddedSize);
		CopyDatum(kf + dataOffset, data, dataSize);
	}
	else if (flags == kKeyFieldHasDups)
	{
		UByte* first = kf + dataOffset;
		long oldPaddedSize = PadEven(kfSizeOfData(first));
		long delta = paddedSize - oldPaddedSize;
		if (delta != 0)
		{
			UByte* rest = first + oldPaddedSize;
			memmove(rest + delta, rest, kf + KeyFieldSize(kf) - rest);
		}
		CopyDatum(first, data, dataSize);
		SetKeyFieldSize(kf, KeyFieldSize(kf) + delta);
	}
}


// ROM 0x002c4c7c kfFindDataAdr__10TSoupIndexFP8KeyFieldPvPPv
// The field's datum equal to data (nil for none); the one before it in
// prior when wanted.
void*
TSoupIndex::kfFindDataAdr(KeyField* kf, const void* data, void** prior)
{
	if (prior != nil)
		*prior = nil;
	void* p = nil;
	Boolean more = kfNextDataAdr(kf, nil, &p);
	while (more)
	{
		if (CompareData(data, p) == 0)
			return p;
		if (prior != nil)
			*prior = p;
		more = kfNextDataAdr(kf, p, &p);
	}
	return nil;
}


// ROM 0x002c4d14 kfAssembleKeyField__10TSoupIndexFP8KeyFieldPvT2
// The field for a key and (optionally) its datum: no duplicates.
KeyField*
TSoupIndex::kfAssembleKeyField(KeyField* kf, const void* key, const void* data)
{
	long keySize = kfSizeOfKey(key);
	long dataOffset = PadEven(keySize + 2);
	long dataSize = 0;
	if (data != nil)
		dataSize = kfSizeOfData(data);
	if (keySize + PadEven(dataSize) + 4 > kKeyFieldBufferSize)
		Throw(exStoreError, (void*) kNSErrKeySizeTooBig, nil);
	SetKeyFieldFlags(kf, 0);
	SetKeyFieldSize(kf, dataOffset + PadEven(dataSize));
	memcpy(kf + 2, key, keySize);
	if (keySize & 1)
		kf[2 + keySize] = 0;
	if (dataSize != 0)
		CopyDatum(kf + dataOffset, data, dataSize);
	return kf;
}


// ROM 0x002c4e58 kfDisassembleKeyField__10TSoupIndexFP8KeyFieldP4SKeyT2
// The field's key and first datum into the keys wanted.
void
TSoupIndex::kfDisassembleKeyField(KeyField* kf, SKey* outKey, SKey* outData)
{
	if (outKey != nil)
		memcpy((void*) outKey, kf + 2, kfSizeOfKey(kf + 2));
	if (outData != nil)
	{
		void* data = kfFirstDataAdr(kf);
		memcpy((void*) outData, data, kfSizeOfData(data));
	}
}


// ROM 0x002c4ed0 kfConvertKeyField__10TSoupIndexFlP8KeyField
// A field without duplicates made one with: the count (1) and dup node
// id (none) added.
void
TSoupIndex::kfConvertKeyField(long toDups, KeyField* kf)
{
	if (KeyFieldFlags(kf) != 0 || toDups != 1)
		return;
	SetKeyFieldFlags(kf, kKeyFieldHasDups);
	SetKeyFieldSize(kf, KeyFieldSize(kf) + 6);
	kfSetDupCount(kf, 1);
	kfSetNextDupID(kf, 0);
}


/*------------------------------------------------------------------------------
	N o d e s
------------------------------------------------------------------------------*/

// ROM 0x002c1740 ReadRootNode__10TSoupIndexFUc
// The root node, from the cache or the store (created when there is none
// and create says to); nil when there is none.
NodeHeader*
TSoupIndex::ReadRootNode(Boolean create)
{
	if (fInfo.fRootNodeId == 0)
	{
		if (!create)
			return nil;
		CreateFirstRoot();
	}
	return ReadANode(fInfo.fRootNodeId, 0);
}


// ROM 0x002c1784 SetRootNode__10TSoupIndexFUl
void
TSoupIndex::SetRootNode(ULong id)
{
	fInfo.fRootNodeId = id;
	WriteInfo(fStoreWrapper, fInfoId, &fInfo);
}


// ROM 0x002c4344 ReadANode__10TSoupIndexFUlT1
// The node, from the cache or the store, its id and parent set.  A node
// read from the store is expanded: the key data moved to the end of the
// buffer and the gap after the offsets zeroed.
NodeHeader*
TSoupIndex::ReadANode(ULong id, ULong parentId)
{
	NodeHeader* node = fNodeCache->FindNode(this, id);
	if (node == nil)
	{
		long size;
		OSErrIf(fStoreWrapper->Store()->GetObjectSize(id, &size));
		node = fNodeCache->RememberNode(this, id, fInfo.fNodeSize, 0, 0);
		OSErrIf(fStoreWrapper->Store()->Read(id, 0, (char*) node, size));
		UByte* keyData = (UByte*) node + kNodeHeaderSize + node->NumKeys() * 2;
		long gap = node->BytesRemaining();
		memmove(keyData + gap, keyData, fInfo.fNodeSize - gap - (keyData - (UByte*) node));
		memset(keyData, 0, gap);
	}
	node->SetId(id);
	node->SetParentId(parentId);
	return node;
}


// ROM 0x002c443c ReadADupNode__10TSoupIndexFUl
DupNodeHeader*
TSoupIndex::ReadADupNode(ULong id)
{
	DupNodeHeader* node = (DupNodeHeader*) fNodeCache->FindNode(this, id);
	if (node == nil)
	{
		long size;
		OSErrIf(fStoreWrapper->Store()->GetObjectSize(id, &size));
		node = (DupNodeHeader*) fNodeCache->RememberNode(this, id, fInfo.fNodeSize, 1, 0);
		OSErrIf(fStoreWrapper->Store()->Read(id, 0, (char*) node, size));
	}
	node->SetId(id);
	return node;
}


// ROM 0x002c451c ChangeNode__10TSoupIndexFP10NodeHeader
// Nothing (the node will be dirtied when done with).
void
TSoupIndex::ChangeNode(NodeHeader* /*node*/)
{ }


// ROM 0x002c4520 UpdateNode__10TSoupIndexFP10NodeHeader
// The node written to the store compact: the header and offsets, then
// the key data.
void
TSoupIndex::UpdateNode(NodeHeader* node)
{
	UByte buffer[kSoupIndexNodeSize];
	long size = BytesInNode(node);
	long headerSize = kNodeHeaderSize + node->NumKeys() * 2;
	memcpy(buffer, node, headerSize);
	memcpy(buffer + headerSize, (UByte*) node + node->BytesRemaining() + headerSize, size - headerSize);
	OSErrIf(fStoreWrapper->Store()->ReplaceObject(node->Id(), (char*) buffer, size));
}


// ROM 0x002c459c UpdateDupNode__10TSoupIndexFP10NodeHeader
// A dup node is compact already.
void
TSoupIndex::UpdateDupNode(NodeHeader* node)
{
	OSErrIf(fStoreWrapper->Store()->ReplaceObject(node->Id(), (char*) node, BytesInNode(node)));
}


// ROM 0x002c45dc NewNode__10TSoupIndexFv
// A new, empty, dirty node in the cache with a new store object.
NodeHeader*
TSoupIndex::NewNode(void)
{
	PSSId id;
	OSErrIf(fStoreWrapper->Store()->NewObject(0, &id));
	NodeHeader* node = fNodeCache->RememberNode(this, id, fInfo.fNodeSize, 0, 1);
	InitNode(node, id);
	return node;
}


// ROM 0x002c4648 NewDupNode__10TSoupIndexFv
DupNodeHeader*
TSoupIndex::NewDupNode(void)
{
	PSSId id;
	OSErrIf(fStoreWrapper->Store()->NewObject(0, &id));
	DupNodeHeader* node = (DupNodeHeader*) fNodeCache->RememberNode(this, id, fInfo.fNodeSize, 1, 1);
	node->SetId(id);
	node->SetNextId(0);
	node->SetBytesRemaining(fInfo.fNodeSize - kDupNodeHeaderSize);
	node->SetCount(0);
	node->SetBytesUsed(kDupNodeHeaderSize);
	node->fUnused[0] = 0;
	node->fUnused[1] = 0;
	return node;
}


// ROM 0x002c46d8 DeleteNode__10TSoupIndexFUl
void
TSoupIndex::DeleteNode(ULong id)
{
	fNodeCache->DeleteNode(id);
}


// ROM 0x002c59e4 InitNode__10TSoupIndexFP10NodeHeaderUl
// An empty node: the one offset to the empty key field at the end, with
// no child.
void
TSoupIndex::InitNode(NodeHeader* node, ULong id)
{
	node->SetId(id);
	node->SetParentId(0);
	node->SetBytesRemaining(fInfo.fNodeSize - 0x14);
	node->SetNumKeys(0);
	node->SetOffset(0, fInfo.fNodeSize - 2);
	KeyField* kf = FirstKeyField(node);
	kf[0] = 0;
	kf[1] = 0;
	SetNodeNo(node, 0, 0);
}


// ROM 0x002c5b28 CreateFirstRoot__10TSoupIndexFv
void
TSoupIndex::CreateFirstRoot(void)
{
	NodeHeader* node = NewNode();
	fInfo.fRootNodeId = node->Id();
	WriteInfo(fStoreWrapper, fInfoId, &fInfo);
}


// ROM 0x002c5a5c CreateNewRoot__10TSoupIndexFP8KeyFieldUl
// A new root over the old one and rightId, with the key that separates
// them.
void
TSoupIndex::CreateNewRoot(KeyField* kf, ULong rightId)
{
	NodeHeader* node = NewNode();
	PutKeyIntoNode(kf, rightId, node, 0);
	SetNodeNo(node, 0, fInfo.fRootNodeId);
	fInfo.fRootNodeId = node->Id();
	WriteInfo(fStoreWrapper, fInfoId, &fInfo);
}


// ROM 0x002c52dc KeyFieldAdr__10TSoupIndexFP10NodeHeaderl
KeyField*
TSoupIndex::KeyFieldAdr(NodeHeader* node, long slot)
{
	return (KeyField*) node + node->Offset(slot);
}


// ROM 0x002c52ec LeftNodeNo__10TSoupIndexFP10NodeHeaderl
// The id of the child to the left of the key at slot (before the field).
ULong
TSoupIndex::LeftNodeNo(NodeHeader* node, long slot)
{
	return GetBigEndianWord(KeyFieldAdr(node, slot) - 4);
}


// ROM 0x002c537c RightNodeNo__10TSoupIndexFP10NodeHeaderl
ULong
TSoupIndex::RightNodeNo(NodeHeader* node, long slot)
{
	return GetBigEndianWord(KeyFieldAdr(node, slot + 1) - 4);
}


// ROM 0x002c53d4 FirstNodeNo__10TSoupIndexFP10NodeHeader
ULong
TSoupIndex::FirstNodeNo(NodeHeader* node)
{
	return GetBigEndianWord(KeyFieldAdr(node, 0) - 4);
}


// ROM 0x002c53dc LastNodeNo__10TSoupIndexFP10NodeHeader
ULong
TSoupIndex::LastNodeNo(NodeHeader* node)
{
	return GetBigEndianWord(KeyFieldAdr(node, node->NumKeys()) - 4);
}


// ROM 0x002c53e8 SetNodeNo__10TSoupIndexFP10NodeHeaderlUl
void
TSoupIndex::SetNodeNo(NodeHeader* node, long slot, ULong id)
{
	PutBigEndianWord(KeyFieldAdr(node, slot) - 4, (unsigned int) id);
}


// ROM 0x002c5440 FirstKeyField__10TSoupIndexFP10NodeHeader
KeyField*
TSoupIndex::FirstKeyField(NodeHeader* node)
{
	return (KeyField*) node + node->Offset(0);
}


// ROM 0x002c5448 LastKeyField__10TSoupIndexFP10NodeHeader
KeyField*
TSoupIndex::LastKeyField(NodeHeader* node)
{
	return (KeyField*) node + node->Offset(node->NumKeys() - 1);
}


// ROM 0x002c5464 KeyFieldBase__10TSoupIndexFP10NodeHeader
// The lowest key field: the end of the free space.
UByte*
TSoupIndex::KeyFieldBase(NodeHeader* node)
{
	return (UByte*) node + node->BytesRemaining() + node->NumKeys() * 2 + kNodeHeaderSize;
}


// ROM 0x002c5480 MoveKey__10TSoupIndexFP8KeyFieldT1
// The field copied; an empty one leaves to empty.  ==> whether there
// was one.
Boolean
TSoupIndex::MoveKey(KeyField* from, KeyField* to)
{
	unsigned int size = KeyFieldSize(from);
	if (size != 0)
	{
		memcpy(to, from, size);
		return true;
	}
	ClearKeyField(to);
	return false;
}


// ROM 0x002c54cc CopyKeyFmNode__10TSoupIndexFP8KeyFieldPUlP10NodeHeaderl
// The field at slot and its left child's id.
void
TSoupIndex::CopyKeyFmNode(KeyField* kf, ULong* leftId, NodeHeader* node, long slot)
{
	KeyField* src = KeyFieldAdr(node, slot);
	memcpy(kf, src, KeyFieldSize(src));
	*leftId = LeftNodeNo(node, slot);
}


// ROM 0x002c5528 KeyAfterNodeNo__10TSoupIndexFP10NodeHeaderUlPl
// The field whose left child is id (the key after that subtree).
KeyField*
TSoupIndex::KeyAfterNodeNo(NodeHeader* node, ULong id, long* slot)
{
	*slot = 0;
	while (LeftNodeNo(node, *slot) != id)
		(*slot)++;
	return KeyFieldAdr(node, *slot);
}


// ROM 0x002c55c0 KeyBeforeNodeNo__10TSoupIndexFP10NodeHeaderUlPl
// The field whose right child is id (the key before that subtree); nil
// when the subtree is the leftmost.
KeyField*
TSoupIndex::KeyBeforeNodeNo(NodeHeader* node, ULong id, long* slot)
{
	long i = 0;
	while (LeftNodeNo(node, i) != id)
		i++;
	*slot = i - 1;
	if (i == 0)
		return nil;
	return KeyFieldAdr(node, i - 1);
}


// ROM 0x002c5630 KeyInNode__10TSoupIndexFP8KeyFieldP10NodeHeaderPUlPl
// A binary search of the node for the key: found, its slot and left
// child; not found, the slot it would be at and the child to look in.
Boolean
TSoupIndex::KeyInNode(KeyField* kf, NodeHeader* node, ULong* childId, long* slot)
{
	long lo = 0;
	long hi = node->NumKeys() - 1;
	long mid = 0;
	int cmp = 0;
	while (lo <= hi)
	{
		mid = lo + (hi - lo) / 2;
		cmp = CompareKeys(*KeyFieldKey(kf), *KeyFieldKey(KeyFieldAdr(node, mid)));
		if (cmp == 0)
		{
			*slot = mid;
			*childId = LeftNodeNo(node, mid);
			return true;
		}
		if (cmp < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	if (cmp > 0)
		mid++;
	*slot = mid;
	*childId = LeftNodeNo(node, mid);
	return false;
}


// ROM 0x002c5700 LastSlotInNode__10TSoupIndexFP10NodeHeader
long
TSoupIndex::LastSlotInNode(NodeHeader* node)
{
	return node->NumKeys() - 1;
}


// ROM 0x002c5710 BytesInNode__10TSoupIndexFP10NodeHeader
// The node's compact size.
long
TSoupIndex::BytesInNode(NodeHeader* node)
{
	return fInfo.fNodeSize - node->BytesRemaining();
}


// ROM 0x002c5720 RoomInNode__10TSoupIndexFP10NodeHeaderP8KeyField
// Room for the field, its child id and its offset.
Boolean
TSoupIndex::RoomInNode(NodeHeader* node, KeyField* kf)
{
	return (long) KeyFieldSize(kf) + 6 < node->BytesRemaining();
}


// ROM 0x002c5748 NodeUnderflow__10TSoupIndexFP10NodeHeader
// Less than half full.
Boolean
TSoupIndex::NodeUnderflow(NodeHeader* node)
{
	return node->BytesRemaining() > fInfo.fNodeSize / 2;
}


// ROM 0x002c576c PutKeyIntoNode__10TSoupIndexFP8KeyFieldUlP10NodeHeaderl
// The field inserted at slot with rightId the child to its right: the
// field goes below the others, the offsets after slot move up; the
// field takes the left child of the one it displaces, which takes
// rightId.
void
TSoupIndex::PutKeyIntoNode(KeyField* kf, ULong rightId, NodeHeader* node, long slot)
{
	if (!RoomInNode(node, kf))
		Throw(exStoreError, (void*) kNSErrKeySizeTooBig, nil);
	long size = KeyFieldSize(kf);
	UByte* where = KeyFieldBase(node) - size;
	memcpy(where, kf, size);
	long numKeys = node->NumKeys();
	memmove(node->fOffsets + (slot + 1) * 2, node->fOffsets + slot * 2, (numKeys - slot + 1) * 2);
	node->SetOffset(slot, where - (UByte*) node);
	ULong leftId = RightNodeNo(node, slot);
	SetNodeNo(node, slot, leftId);
	SetNodeNo(node, slot + 1, rightId);
	node->SetNumKeys(numKeys + 1);
	node->SetBytesRemaining(node->BytesRemaining() - (size + 6));
}


// ROM 0x002c5888 DeleteKeyFromNode__10TSoupIndexFP10NodeHeaderl
// The field at slot removed: the one after it takes its left child, the
// fields below it move up over it and their offsets follow.
void
TSoupIndex::DeleteKeyFromNode(NodeHeader* node, long slot)
{
	long numKeys = node->NumKeys();
	if (slot < 0 || slot >= numKeys)
		Throw(exStoreError, (void*) kNSErrKeySizeTooBig, nil);
	SetNodeNo(node, slot + 1, LeftNodeNo(node, slot));
	KeyField* kf = KeyFieldAdr(node, slot);
	long removed = KeyFieldSize(kf) + 4;
	UByte* base = KeyFieldBase(node);
	long deletedOffset = node->Offset(slot);
	memmove(base + removed, base, (kf - 4) - base);
	memmove(node->fOffsets + slot * 2, node->fOffsets + (slot + 1) * 2, (numKeys - slot) * 2);
	numKeys--;
	node->SetNumKeys(numKeys);
	node->SetBytesRemaining(node->BytesRemaining() + removed + 2);
	for (long i = 0; i < numKeys; i++)
	{
		long offset = node->Offset(i);
		if (offset < deletedOffset + removed)
			node->SetOffset(i, offset + removed);
	}
}


/*------------------------------------------------------------------------------
	S e a r c h i n g
------------------------------------------------------------------------------*/

// ROM 0x002c5b4c FindNextKey__10TSoupIndexFP8KeyFieldPP10NodeHeaderPl
// The key after the one at slot of node into kf, node and slot moved to
// it: the leftmost key of the subtree to its right, else the next key
// in the node, else the key after this subtree in an ancestor.  ==>
// whether there is one.
Boolean
TSoupIndex::FindNextKey(KeyField* kf, NodeHeader** node, long* slot)
{
	if (KeyFieldSize(KeyFieldAdr(*node, *slot)) == 0)
	{
		ClearKeyField(kf);
		return false;
	}
	(*slot)++;
	ULong childId = LeftNodeNo(*node, *slot);
	KeyField* next;
	if (childId == 0)
	{
		next = KeyFieldAdr(*node, *slot);
		if (KeyFieldSize(next) != 0)
			MoveKey(next, kf);
		// past the end of this node: up to the key after it
		while (KeyFieldSize(next) == 0)
		{
			ULong parentId = (*node)->ParentId();
			if (parentId == 0)
				break;
			ULong thisId = (*node)->Id();
			*node = fNodeCache->FindNode(this, parentId);
			next = KeyAfterNodeNo(*node, thisId, slot);
		}
	}
	else
	{
		do {
			*node = ReadANode(childId, (*node)->Id());
			*slot = 0;
			next = FirstKeyField(*node);
			childId = FirstNodeNo(*node);
		} while (childId != 0);
	}
	unsigned int size = KeyFieldSize(next);
	if (size == 0)
	{
		ClearKeyField(kf);
		return false;
	}
	memcpy(kf, next, size);
	return true;
}


// ROM 0x002c5ca8 FindPriorKey__10TSoupIndexFP8KeyFieldPP10NodeHeaderPl
// The key before the one at slot into kf (its last datum when it has
// duplicates): the rightmost key of the subtree to its left, else the
// key before it in the node, else the key before this subtree in an
// ancestor.
Boolean
TSoupIndex::FindPriorKey(KeyField* kf, NodeHeader** node, long* slot)
{
	NodeHeader* theNode = *node;
	ULong childId = LeftNodeNo(theNode, *slot);
	KeyField* prior;
	if (*slot == 0 || childId != 0)
	{
		if (childId == 0)
		{
			// the first key of this node: up to the key before it
			prior = nil;
			while (theNode->ParentId() != 0 && prior == nil)
			{
				ULong thisId = theNode->Id();
				theNode = fNodeCache->FindNode(this, theNode->ParentId());
				prior = KeyBeforeNodeNo(theNode, thisId, slot);
			}
			if (prior == nil)
			{
				ClearKeyField(kf);
				return false;
			}
		}
		else
		{
			do {
				theNode = ReadANode(childId, theNode->Id());
				prior = LastKeyField(theNode);
				childId = LastNodeNo(theNode);
			} while (childId != 0);
			*slot = LastSlotInNode(theNode);
		}
	}
	else
	{
		(*slot)--;
		prior = KeyFieldAdr(theNode, *slot);
	}
	MoveKey(prior, kf);
	if (KeyFieldFlags(prior) == kKeyFieldHasDups)
		kfReplaceFirstData(kf, LastDupDataAdr(prior, nil));
	*node = theNode;
	return true;
}


// ROM 0x002c5e14 FindFirstKey__10TSoupIndexFP10NodeHeaderP8KeyField
// The leftmost key under node.
Boolean
TSoupIndex::FindFirstKey(NodeHeader* node, KeyField* kf)
{
	ULong childId = FirstNodeNo(node);
	while (childId != 0)
	{
		node = ReadANode(childId, node->Id());
		childId = FirstNodeNo(node);
	}
	return MoveKey(FirstKeyField(node), kf);
}


// ROM 0x002c5e80 FindLastKey__10TSoupIndexFP10NodeHeaderP8KeyField
// The rightmost key under node, with its last datum.
Boolean
TSoupIndex::FindLastKey(NodeHeader* node, KeyField* kf)
{
	ULong childId = LastNodeNo(node);
	while (childId != 0)
	{
		node = ReadANode(childId, node->Id());
		childId = LastNodeNo(node);
	}
	KeyField* last = LastKeyField(node);
	Boolean found = MoveKey(last, kf);
	if (KeyFieldFlags(last) == kKeyFieldHasDups)
		kfReplaceFirstData(kf, LastDupDataAdr(last, nil));
	return found;
}


// ROM 0x002c5f24 Search__10TSoupIndexFP8KeyFieldPP10NodeHeaderPl
// The key under node: found, kf is the field there; not found, kf is the
// key after it (empty for none); node and slot say where.
Boolean
TSoupIndex::Search(KeyField* kf, NodeHeader** node, long* slot)
{
	ULong childId;
	Boolean found = KeyInNode(kf, *node, &childId, slot);
	if (!found && childId != 0)
	{
		*node = ReadANode(childId, (*node)->Id());
		return Search(kf, node, slot);
	}
	KeyField* here = KeyFieldAdr(*node, *slot);
	if (KeyFieldSize(here) == 0)
	{
		*slot = LastSlotInNode(*node);
		FindNextKey(kf, node, slot);
	}
	else
		MoveKey(here, kf);
	return found;
}


// ROM 0x002c6464 SearchNext__10TSoupIndexFP8KeyFieldPP10NodeHeaderPl
// The key after kf's into kf: ==> 0 for none, -1 when kf's key was not
// in the index (kf is the key after where it would be), 1 otherwise.
int
TSoupIndex::SearchNext(KeyField* kf, NodeHeader** node, long* slot)
{
	ULong childId;
	if (KeyInNode(kf, *node, &childId, slot))
		return FindNextKey(kf, node, slot) ? 1 : 0;
	if (childId != 0)
	{
		*node = ReadANode(childId, (*node)->Id());
		return SearchNext(kf, node, slot);
	}
	KeyField* here = KeyFieldAdr(*node, *slot);
	if (KeyFieldSize(here) == 0)
	{
		*slot = LastSlotInNode(*node);
		FindNextKey(kf, node, slot);
	}
	else
		MoveKey(here, kf);
	return KeyFieldSize(kf) == 0 ? 0 : -1;
}


// ROM 0x002c6560 SearchPrior__10TSoupIndexFP8KeyFieldPP10NodeHeaderPl
int
TSoupIndex::SearchPrior(KeyField* kf, NodeHeader** node, long* slot)
{
	ULong childId;
	if (KeyInNode(kf, *node, &childId, slot))
		return FindPriorKey(kf, node, slot) ? 1 : 0;
	if (childId != 0)
	{
		*node = ReadANode(childId, (*node)->Id());
		return SearchPrior(kf, node, slot);
	}
	if (*slot == 0)
		FindPriorKey(kf, node, slot);
	else
		MoveKey(KeyFieldAdr(*node, *slot - 1), kf);
	return KeyFieldSize(kf) == 0 ? 0 : -1;
}


// ROM 0x002c625c SearchNextDup__10TSoupIndexFP8KeyFieldPP10NodeHeaderPlPP13DupNodeHeader
// The datum after kf's for kf's key into kf: ==> 0 for none (or when the
// key's only datum is kf's), -1 when the key or datum is not in the
// index, 1 otherwise; dupNode is the dup node the datum came from.
int
TSoupIndex::SearchNextDup(KeyField* kf, NodeHeader** node, long* slot, DupNodeHeader** dupNode)
{
	*dupNode = nil;
	ULong childId;
	if (!KeyInNode(kf, *node, &childId, slot))
	{
		if (childId == 0)
			return -1;
		*node = ReadANode(childId, (*node)->Id());
		return SearchNextDup(kf, node, slot, dupNode);
	}
	void* data = kfFirstDataAdr(kf);
	KeyField* here = KeyFieldAdr(*node, *slot);
	if (KeyFieldFlags(here) == 0)
		return CompareData(data, kfFirstDataAdr(here)) == 0 ? 0 : -1;

	void* next = kfFindDataAdr(here, data, nil);
	if (next != nil && kfNextDataAdr(here, next, &next))
	{
		kfReplaceFirstData(kf, next);
		return 1;
	}
	// the datum is the field's last, or not in the field: on to the dup nodes
	ULong dupId = kfNextDupID(here);
	if (dupId == 0)
		return next != nil ? 0 : -1;
	*dupNode = ReadADupNode(dupId);
	if (next == nil)
	{
		Boolean found;
		next = FindNextDupDataAdr(dupNode, data, &found);
	}
	else
		next = FirstDupDataAdr(*dupNode);
	if (next == nil)
		return 0;
	kfReplaceFirstData(kf, next);
	return 1;
}


// ROM 0x002c6648 SearchPriorDup__10TSoupIndexFP8KeyFieldPP10NodeHeaderPlPP13DupNodeHeader
int
TSoupIndex::SearchPriorDup(KeyField* kf, NodeHeader** node, long* slot, DupNodeHeader** dupNode)
{
	*dupNode = nil;
	ULong childId;
	if (!KeyInNode(kf, *node, &childId, slot))
	{
		if (childId == 0)
			return -1;
		*node = ReadANode(childId, (*node)->Id());
		return SearchPriorDup(kf, node, slot, dupNode);
	}
	void* data = kfFirstDataAdr(kf);
	KeyField* here = KeyFieldAdr(*node, *slot);
	if (KeyFieldFlags(here) == 0)
		return CompareData(data, kfFirstDataAdr(here)) == 0 ? 0 : -1;

	void* prior;
	if (kfFindDataAdr(here, data, &prior) != nil)
	{
		if (prior == nil)
			return 0;						// the field's first datum: nothing before it
		kfReplaceFirstData(kf, prior);
		return 1;
	}
	// not in the field: in the dup nodes
	ULong dupId = kfNextDupID(here);
	if (dupId == 0)
		return -1;
	*dupNode = ReadADupNode(dupId);
	Boolean found;
	prior = FindPriorDupDataAdr(dupNode, data, &found);
	if (prior == nil)
	{
		if (!found)
			return -1;
		prior = kfLastDataAdr(here);		// the first dup node datum: the field's last is before it
	}
	kfReplaceFirstData(kf, prior);
	return 1;
}


/*------------------------------------------------------------------------------
	D u p l i c a t e   d a t a
------------------------------------------------------------------------------*/

// ROM 0x002c4f68 FirstDupDataAdr__10TSoupIndexFP13DupNodeHeader
void*
TSoupIndex::FirstDupDataAdr(DupNodeHeader* node)
{
	return (UByte*) node + kDupNodeHeaderSize;
}


// ROM 0x002c4f70 NextDupDataAdr__10TSoupIndexFP13DupNodeHeaderPvPPv
// The datum after data (the first for nil) in next; ==> whether there
// is one.
Boolean
TSoupIndex::NextDupDataAdr(DupNodeHeader* node, void* data, void** next)
{
	if (data == nil)
	{
		*next = FirstDupDataAdr(node);
		return true;
	}
	UByte* p = (UByte*) data + PadEven(kfSizeOfData(data));
	*next = p;
	return p < (UByte*) node + node->BytesUsed();
}


// ROM 0x002c4fd8 LastDupDataAdr__10TSoupIndexFP8KeyFieldPP13DupNodeHeader
// The key's last datum: the last in the last dup node (in dupNode when
// wanted), else the last in the field.
void*
TSoupIndex::LastDupDataAdr(KeyField* kf, DupNodeHeader** dupNode)
{
	ULong dupId = kfNextDupID(kf);
	if (dupId == 0)
	{
		if (dupNode != nil)
			*dupNode = nil;
		return kfLastDataAdr(kf);
	}
	DupNodeHeader* node = ReadADupNode(dupId);
	while (node->NextId() != 0)
		node = ReadADupNode(node->NextId());
	UByte* end = (UByte*) node + node->BytesUsed();
	UByte* p = (UByte*) FirstDupDataAdr(node);
	UByte* last;
	do {
		last = p;
		p += PadEven(kfSizeOfData(last));
	} while (p < end);
	if (dupNode != nil)
		*dupNode = node;
	return last;
}


// ROM 0x002c50a4 AppendDupData__10TSoupIndexFP13DupNodeHeaderPv
// ==> whether it fitted.
Boolean
TSoupIndex::AppendDupData(DupNodeHeader* node, const void* data)
{
	long dataSize = kfSizeOfData(data);
	long paddedSize = PadEven(dataSize);
	if (paddedSize > node->BytesRemaining())
		return false;
	CopyDatum((UByte*) node + node->BytesUsed(), data, dataSize);
	node->SetBytesRemaining(node->BytesRemaining() - paddedSize);
	node->SetBytesUsed(node->BytesUsed() + paddedSize);
	node->SetCount(node->Count() + 1);
	return true;
}


// ROM 0x002c5154 PrependDupData__10TSoupIndexFP13DupNodeHeaderPv
Boolean
TSoupIndex::PrependDupData(DupNodeHeader* node, const void* data)
{
	UByte* first = (UByte*) FirstDupDataAdr(node);
	long dataSize = kfSizeOfData(data);
	long paddedSize = PadEven(dataSize);
	if (paddedSize >= node->BytesRemaining())
		return false;
	memmove(first + paddedSize, first, (UByte*) node + node->BytesUsed() - first);
	CopyDatum(first, data, dataSize);
	node->SetBytesRemaining(node->BytesRemaining() - paddedSize);
	node->SetBytesUsed(node->BytesUsed() + paddedSize);
	node->SetCount(node->Count() + 1);
	return true;
}


// ROM 0x002c5224 DeleteDupData__10TSoupIndexFP13DupNodeHeaderPv
// ==> whether the node is now empty.
Boolean
TSoupIndex::DeleteDupData(DupNodeHeader* node, void* data)
{
	ChangeNode((NodeHeader*) node);
	long paddedSize = PadEven(kfSizeOfData(data));
	UByte* after = (UByte*) data + paddedSize;
	memmove(data, after, (UByte*) node + node->BytesUsed() - after);
	node->SetBytesRemaining(node->BytesRemaining() + paddedSize);
	node->SetBytesUsed(node->BytesUsed() - paddedSize);
	node->SetCount(node->Count() - 1);
	return node->Count() == 0;
}


// ROM 0x002c5ffc FindDupDataAdr__10TSoupIndexFP13DupNodeHeaderPvPPv
// The node's datum equal to data (nil for none); the one before it in
// prior when wanted.
void*
TSoupIndex::FindDupDataAdr(DupNodeHeader* node, const void* data, void** prior)
{
	if (prior != nil)
		*prior = nil;
	void* p = nil;
	Boolean more = NextDupDataAdr(node, nil, &p);
	while (more)
	{
		if (CompareData(data, p) == 0)
			return p;
		if (prior != nil)
			*prior = p;
		more = NextDupDataAdr(node, p, &p);
	}
	return nil;
}


// ROM 0x002c6094 FindNextDupDataAdr__10TSoupIndexFPP13DupNodeHeaderPvPUc
// The datum after data in the chain from node (node moved to the one it
// is in); found says whether data itself was there.
void*
TSoupIndex::FindNextDupDataAdr(DupNodeHeader** node, const void* data, Boolean* found)
{
	void* p = FindDupDataAdr(*node, data, nil);
	while (p == nil)
	{
		if ((*node)->NextId() == 0)
		{
			if (found != nil)
				*found = false;
			return nil;
		}
		*node = ReadADupNode((*node)->NextId());
		p = FindDupDataAdr(*node, data, nil);
	}
	if (found != nil)
		*found = true;
	if (NextDupDataAdr(*node, p, &p))
		return p;
	if ((*node)->NextId() != 0)
	{
		*node = ReadADupNode((*node)->NextId());
		return FirstDupDataAdr(*node);
	}
	return nil;
}


// ROM 0x002c618c FindPriorDupDataAdr__10TSoupIndexFPP13DupNodeHeaderPvPUc
// The datum before data in the chain from node (node moved to the one it
// is in); nil when data is the first (found true) or not there (found
// false).
void*
TSoupIndex::FindPriorDupDataAdr(DupNodeHeader** node, const void* data, Boolean* found)
{
	void* lastOfPrior = nil;
	DupNodeHeader* priorNode = nil;
	void* prior;
	void* p = FindDupDataAdr(*node, data, &prior);
	for ( ; ; )
	{
		if (p != nil)
		{
			if (found != nil)
				*found = true;
			if (prior != nil)
				return prior;
			// the first of this node: the last of the one before
			if (lastOfPrior != nil)
				*node = priorNode;
			return lastOfPrior;
		}
		lastOfPrior = prior;
		priorNode = *node;
		if ((*node)->NextId() == 0)
		{
			if (found != nil)
				*found = false;
			return nil;
		}
		*node = ReadADupNode((*node)->NextId());
		p = FindDupDataAdr(*node, data, &prior);
	}
}


// ROM 0x002c69b8 CheckForDupData__10TSoupIndexFP8KeyFieldPv
// A datum being added to the key at kf: an error when the index takes no
// duplicate keys, or the datum is already there.
void
TSoupIndex::CheckForDupData(KeyField* kf, const void* data)
{
	if (fInfo.fDuplicates == kIndexUniqueKeys)
		ThrowOSErr(kIndexErrDuplicateKey);
	if (kfFindDataAdr(kf, data, nil) != nil)
		ThrowOSErr(kIndexErrDuplicateKey);
	if (KeyFieldFlags(kf) != 0)
	{
		ULong dupId = kfNextDupID(kf);
		while (dupId != 0)
		{
			DupNodeHeader* dupNode = ReadADupNode(dupId);
			void* p = nil;
			Boolean more = NextDupDataAdr(dupNode, nil, &p);
			while (more)
			{
				if (CompareData(data, p) == 0)
					ThrowOSErr(kIndexErrDuplicateKey);
				more = NextDupDataAdr(dupNode, p, &p);
			}
			dupId = dupNode->NextId();
		}
	}
}


// ROM 0x002c6aac StoreDupData__10TSoupIndexFP8KeyFieldPv
// Another datum for the key: at the end of its last dup node (a new one
// when full), else at the end of the field while that stays under 100
// bytes, else in a first dup node.
void
TSoupIndex::StoreDupData(KeyField* kf, const void* data)
{
	ULong dupId = kfNextDupID(kf);
	if (dupId != 0)
	{
		DupNodeHeader* dupNode;
		do {
			dupNode = ReadADupNode(dupId);
			dupId = dupNode->NextId();
		} while (dupId != 0);
		ChangeNode((NodeHeader*) dupNode);
		if (kfSizeOfData(data) > dupNode->BytesRemaining())
		{
			DupNodeHeader* newNode = NewDupNode();
			AppendDupData(newNode, data);
			dupNode->SetNextId(newNode->Id());
		}
		else
			AppendDupData(dupNode, data);
		fNodeCache->DirtyNode((NodeHeader*) dupNode);
		return;
	}
	if (kfSizeOfData(data) + (long) KeyFieldSize(kf) < kKeyFieldBufferSize)
	{
		void* end = nil;
		while (kfNextDataAdr(kf, end, &end))
			;
		kfInsertData(kf, end, data);
	}
	else
	{
		DupNodeHeader* newNode = NewDupNode();
		AppendDupData(newNode, data);
		kfSetNextDupID(kf, newNode->Id());
	}
}


// ROM 0x002c6bf8 InsertDupData__10TSoupIndexFP8KeyFieldP10NodeHeaderlPUlPUc
// kf's key is already at slot of node: its datum added to the field
// there (which is taken out, converted to one with duplicates, and put
// back - splitting the node when it no longer fits).
void
TSoupIndex::InsertDupData(KeyField* kf, NodeHeader* node, long slot, ULong* rightId, Boolean* split)
{
	*split = false;
	UByte data[kKeyFieldBufferSize];
	void* kfData = kfFirstDataAdr(kf);
	memcpy(data, kfData, kfSizeOfData(kfData));
	KeyField* here = KeyFieldAdr(node, slot);
	CheckForDupData(here, data);
	if (fInfo.fDuplicates == kIndexUniqueKeysNoDupData)
		ThrowOSErr(kIndexErrDuplicateKey);
	ChangeNode(node);
	*rightId = RightNodeNo(node, slot);
	MoveKey(here, kf);
	DeleteKeyFromNode(node, slot);
	kfConvertKeyField(1, kf);
	StoreDupData(kf, data);
	if (!RoomInNode(node, kf))
	{
		*split = true;
		SplitANode(kf, rightId, node, slot);
	}
	else
	{
		PutKeyIntoNode(kf, *rightId, node, slot);
		fNodeCache->DirtyNode(node);
	}
}


/*------------------------------------------------------------------------------
	I n s e r t i o n   a n d   d e l e t i o n
------------------------------------------------------------------------------*/

// ROM 0x002c6d5c InsertKey__10TSoupIndexFP8KeyFieldP10NodeHeaderPUlPUc
// The field inserted under node: down to the leaf it belongs in, then
// into the nodes on the way back up while they split (kf becomes the key
// pushed up and rightId the new node to its right; split says one is
// pending for the caller).  ==> whether all went well (fErr is 0).
Boolean
TSoupIndex::InsertKey(KeyField* kf, NodeHeader* node, ULong* rightId, Boolean* split)
{
	ULong childId;
	long slot;
	if (KeyInNode(kf, node, &childId, &slot))
	{
		ChangeNode(node);
		InsertDupData(kf, node, slot, rightId, split);
	}
	else
	{
		if (childId == 0)
		{
			*split = true;
			*rightId = 0;
		}
		else
			InsertKey(kf, ReadANode(childId, node->Id()), rightId, split);
		if (*split)
		{
			KeyInNode(kf, node, &childId, &slot);
			if (!RoomInNode(node, kf))
			{
				*split = true;
				SplitANode(kf, rightId, node, slot);
			}
			else
			{
				*split = false;
				ChangeNode(node);
				PutKeyIntoNode(kf, *rightId, node, slot);
				fNodeCache->DirtyNode(node);
			}
		}
	}
	return fErr == 0;
}


// ROM 0x002c6ed8 InsertAfterDelete__10TSoupIndexFP8KeyFieldUlP10NodeHeader
// The field put back into node (or an ancestor) after a deletion,
// splitting up the tree as needed; a new root when it splits the root.
void
TSoupIndex::InsertAfterDelete(KeyField* kf, ULong rightId, NodeHeader* node)
{
	ULong childId;
	long slot;
	while (node != nil && !RoomInNode(node, kf))
	{
		KeyInNode(kf, node, &childId, &slot);
		SplitANode(kf, &rightId, node, slot);
		if (node->ParentId() == 0)
			node = nil;
		else
			node = fNodeCache->FindNode(this, node->ParentId());
	}
	if (node != nil)
	{
		ChangeNode(node);
		KeyInNode(kf, node, &childId, &slot);
		PutKeyIntoNode(kf, rightId, node, slot);
		fNodeCache->DirtyNode(node);
	}
	else
		CreateNewRoot(kf, rightId);
}


// ROM 0x002c67fc SplitANode__10TSoupIndexFP8KeyFieldPUlP10NodeHeaderl
// The field will not fit at slot of node: a new node takes the keys from
// the end of node until it is half full (the field among them when slot
// is there); the key left at the boundary comes out into kf, with the
// new node's id in rightId, for the caller to put in the parent.
void
TSoupIndex::SplitANode(KeyField* kf, ULong* rightId, NodeHeader* node, long slot)
{
	NodeHeader* newNode = NewNode();
	long numKeys = node->NumKeys();
	if (slot == numKeys)
		PutKeyIntoNode(kf, *rightId, newNode, 0);
	long last = numKeys - 1;
	while (last > 1 && NodeUnderflow(newNode))
	{
		PutKeyIntoNode(KeyFieldAdr(node, last), RightNodeNo(node, last), newNode, 0);
		ChangeNode(node);
		DeleteKeyFromNode(node, last);
		if (slot == last)
			PutKeyIntoNode(kf, *rightId, newNode, 0);
		last--;
	}
	if (slot <= last)
	{
		ChangeNode(node);
		PutKeyIntoNode(kf, *rightId, node, slot);
		last++;
	}
	SetNodeNo(newNode, 0, RightNodeNo(node, last));
	MoveKey(KeyFieldAdr(node, last), kf);
	ChangeNode(node);
	DeleteKeyFromNode(node, last);
	*rightId = newNode->Id();
	fNodeCache->DirtyNode(node);
}


// ROM 0x002c7000 MergeTwoNodes__10TSoupIndexFP8KeyFieldP10NodeHeaderN22
// The key kf (taken out of parent) and the siblings left and right
// either side of it: when they fit in one node, right's keys go into
// left and right is freed; otherwise keys move between them until
// neither underflows and the key at the boundary goes back into parent.
int
TSoupIndex::MergeTwoNodes(KeyField* kf, NodeHeader* parent, NodeHeader* left, NodeHeader* right)
{
	Boolean leftUnderflow = NodeUnderflow(left);
	if (!leftUnderflow)
	{
		// the key goes at the front of right
		PutKeyIntoNode(kf, FirstNodeNo(right), right, 0);
		SetNodeNo(right, 0, LastNodeNo(left));
	}
	else
		// at the end of left
		PutKeyIntoNode(kf, FirstNodeNo(right), left, left->NumKeys());

	if ((unsigned long) (BytesInNode(left) + BytesInNode(right) - 0x1c) > (unsigned long) (fInfo.fNodeSize - kNodeHeaderSize))
	{
		// they will not fit in one: balance them
		long slot;
		NodeHeader* from;
		if (!leftUnderflow)
		{
			slot = left->NumKeys() - 1;
			while (slot > 0 && NodeUnderflow(right))
			{
				PutKeyIntoNode(KeyFieldAdr(left, slot), RightNodeNo(left, slot), right, 0);
				DeleteKeyFromNode(left, slot);
				slot--;
			}
			MoveKey(KeyFieldAdr(left, slot), kf);
			SetNodeNo(right, 0, LastNodeNo(left));
			from = left;
		}
		else
		{
			slot = 0;
			while (slot < right->NumKeys() && NodeUnderflow(left))
			{
				PutKeyIntoNode(KeyFieldAdr(right, 0), RightNodeNo(right, 0), left, left->NumKeys());
				DeleteKeyFromNode(right, 0);
			}
			MoveKey(KeyFieldAdr(right, 0), kf);
			SetNodeNo(right, 0, RightNodeNo(right, 0));
			from = right;
		}
		DeleteKeyFromNode(from, slot);
		fNodeCache->DirtyNode(left);
		fNodeCache->DirtyNode(right);
		InsertAfterDelete(kf, right->Id(), parent);
		return 0;
	}

	// all of right into left
	for (long slot = 0; slot < right->NumKeys(); slot++)
		PutKeyIntoNode(KeyFieldAdr(right, slot), RightNodeNo(right, slot), left, left->NumKeys());
	fNodeCache->DirtyNode(left);
	DeleteNode(right->Id());
	Boolean underflow = NodeUnderflow(parent);
	if (!underflow)
		fNodeCache->DirtyNode(parent);
	return underflow;
}


// ROM 0x002c0a94 BalanceTwoNodes__10TSoupIndexFP10NodeHeaderT1l
// child, at slot of parent, has underflowed: the key at slot comes out
// of parent and child is merged with the sibling to its right (to its
// left when it is the last).  ==> whether parent now underflows.
Boolean
TSoupIndex::BalanceTwoNodes(NodeHeader* parent, NodeHeader* child, long slot)
{
	KeyField kf[kKeyFieldBufferSize];
	ULong leftId;
	NodeHeader* left;
	NodeHeader* right;
	if (KeyFieldSize(KeyFieldAdr(parent, slot)) == 0)
	{
		// child is the rightmost: its left sibling
		slot = LastSlotInNode(parent);
		CopyKeyFmNode(kf, &leftId, parent, slot);
		left = ReadANode(leftId, parent->Id());
		right = child;
	}
	else
	{
		CopyKeyFmNode(kf, &leftId, parent, slot);
		right = ReadANode(RightNodeNo(parent, slot), parent->Id());
		left = child;
	}
	ChangeNode(parent);
	DeleteKeyFromNode(parent, slot);
	fNodeCache->DirtyNode(parent);
	ChangeNode(left);
	ChangeNode(right);
	return (Boolean) MergeTwoNodes(kf, parent, left, right);
}


// ROM 0x002c0bd0 GetLeafKey__10TSoupIndexFP8KeyFieldP10NodeHeader
// The leftmost key under node taken out of its leaf into kf; when the
// leaf underflows its (new) first key is saved for DeleteKey to remove
// and re-add.  ==> whether the leaf underflows.
Boolean
TSoupIndex::GetLeafKey(KeyField* kf, NodeHeader* node)
{
	ULong childId = FirstNodeNo(node);
	while (childId != 0)
	{
		node = ReadANode(childId, node->Id());
		childId = FirstNodeNo(node);
	}
	CopyKeyFmNode(kf, &childId, node, 0);
	ChangeNode(node);
	DeleteKeyFromNode(node, 0);
	fNodeCache->DirtyNode(node);
	ClearKeyField(savedKey);
	Boolean underflow = NodeUnderflow(node);
	if (underflow)
		CopyKeyFmNode(savedKey, &childId, node, 0);
	return underflow;
}


// ROM 0x002c0cd8 DeleteTheKey__10TSoupIndexFP10NodeHeaderlP8KeyField
// kf's datum removed from the key at slot of node: the field goes when
// it was the only datum; otherwise the datum goes from the field (the
// first datum of the first dup node moving in when the field's was the
// last) or from its dup node (freed when emptied).
void
TSoupIndex::DeleteTheKey(NodeHeader* node, long slot, KeyField* kf)
{
	ChangeNode(node);
	void* data = kfFirstDataAdr(kf);
	KeyField* here = KeyFieldAdr(node, slot);
	// where the datum is in the field
	int cmp = 1;
	long dataOffset = 0;
	void* p = nil;
	Boolean more = kfNextDataAdr(here, p, &p);
	while (more)
	{
		cmp = CompareData(data, p);
		if (cmp == 0)
		{
			dataOffset = (UByte*) p - here;
			break;
		}
		more = kfNextDataAdr(here, p, &p);
	}

	if (KeyFieldFlags(here) == 0)
	{
		if (cmp != 0)
			ThrowOSErr(kIndexErrNotFound);
		DeleteKeyFromNode(node, slot);
		fNodeCache->DirtyNode(node);
	}
	else if (cmp == 0)
	{
		// in the field
		fNodeCache->DirtyNode(node);
		if (kfDupCount(here) > 1)
		{
			ULong rightId = RightNodeNo(node, slot);
			MoveKey(here, kf);
			kfDeleteData(kf, kf + dataOffset);
			DeleteKeyFromNode(node, slot);
			PutKeyIntoNode(kf, rightId, node, slot);
			return;
		}
		ULong dupId = kfNextDupID(here);
		if (dupId == 0)
		{
			DeleteKeyFromNode(node, slot);
			return;
		}
		// the field's only datum goes: the first dup node's first datum replaces it
		DupNodeHeader* dupNode = ReadADupNode(dupId);
		MoveKey(here, kf);
		void* first = FirstDupDataAdr(dupNode);
		kfReplaceFirstData(kf, first);
		if (dupNode->Count() == 1)
			kfSetNextDupID(kf, dupNode->NextId());
		ULong rightId = RightNodeNo(node, slot);
		DeleteKeyFromNode(node, slot);
		PutKeyIntoNode(kf, rightId, node, slot);
		DeleteDupData(dupNode, first);
		if (dupNode->Count() == 0)
			DeleteNode(dupNode->Id());
		else
			fNodeCache->DirtyNode((NodeHeader*) dupNode);
	}
	else
	{
		// in a dup node
		ULong dupId = kfNextDupID(here);
		DupNodeHeader* dupNode = nil;
		DupNodeHeader* priorNode;
		void* found;
		do {
			priorNode = dupNode;
			if (dupId == 0)
				ThrowOSErr(kIndexErrNotFound);
			dupNode = ReadADupNode(dupId);
			dupId = dupNode->NextId();
			found = FindDupDataAdr(dupNode, data, nil);
		} while (found == nil);
		DeleteDupData(dupNode, found);
		if (dupNode->Count() == 0)
		{
			// unlink the emptied node
			if (priorNode == nil)
			{
				kfSetNextDupID(here, dupNode->NextId());
				fNodeCache->DirtyNode(node);
			}
			else
			{
				priorNode->SetNextId(dupNode->NextId());
				fNodeCache->DirtyNode((NodeHeader*) priorNode);
			}
			DeleteNode(dupNode->Id());
		}
		else
			fNodeCache->DirtyNode((NodeHeader*) dupNode);
	}
}


// ROM 0x002c103c DeleteKey__10TSoupIndexFP8KeyFieldP10NodeHeaderPUc
// kf's key (and datum) removed from the tree under node; underflow says
// whether node has underflowed.  ==> whether the key was there.
Boolean
TSoupIndex::DeleteKey(KeyField* kf, NodeHeader* node, Boolean* underflow)
{
	ULong childId;
	long slot;
	Boolean found = KeyInNode(kf, node, &childId, &slot);
	if (!found)
	{
		if (childId != 0)
		{
			NodeHeader* child = ReadANode(childId, node->Id());
			if (child == nil)
				return false;
			Boolean result = DeleteKey(kf, child, underflow);
			if (*underflow)
				*underflow = BalanceTwoNodes(node, child, slot);
			return result;
		}
		// not in this leaf: only DeleteKey's own re-removal of savedKey gets here
		if (KeyFieldSize(savedKey) == 0)
			return false;
	}
	else if (childId != 0)
	{
		// in an interior node
		KeyField* here = KeyFieldAdr(node, slot);
		if (KeyFieldFlags(here) == kKeyFieldHasDups
		&& (kfDupCount(here) >= 2 || kfNextDupID(here) != 0))
		{
			// other data stay: the field stays
			DeleteTheKey(node, slot, kf);
			*underflow = false;
			return true;
		}
		// the field goes: the leftmost leaf key to its right replaces it
		ULong rightId = RightNodeNo(node, slot);
		NodeHeader* right = ReadANode(rightId, node->Id());
		*underflow = GetLeafKey(leafKey, right);
		DeleteTheKey(node, slot, kf);
		InsertAfterDelete(leafKey, rightId, node);
		if (!*underflow)
			return true;
		// the leaf underflowed
		if (right->NumKeys() != 0 || KeyFieldSize(savedKey) != 0)
		{
			// the leaf's first key is taken out and re-added from the top
			MoveKey(savedKey, leafKey);
			_BTRemoveKey(leafKey);
			*underflow = false;
			if (right->NumKeys() != 0)
				return true;
		}
		if (KeyFieldSize(savedKey) != 0)
			return true;
		// the right subtree is empty: delete through it and rebalance
		rightId = RightNodeNo(node, slot);
		right = ReadANode(rightId, node->Id());
		DeleteKey(kf, right, underflow);
		if (!*underflow)
			return true;
		NodeHeader* left = ReadANode(LeftNodeNo(node, slot), node->Id());
		*underflow = BalanceTwoNodes(node, left, slot);
		return true;
	}

	// in a leaf
	if (KeyFieldSize(savedKey) == 0)
		DeleteTheKey(node, slot, kf);
	else
		ClearKeyField(savedKey);
	*underflow = NodeUnderflow(node);
	if (!*underflow)
		fNodeCache->DirtyNode(node);
	return true;
}


// ROM 0x002c1344 FreeNodes__10TSoupIndexFP10NodeHeader
// The node, its dup nodes and its subtrees deleted from the store.
void
TSoupIndex::FreeNodes(NodeHeader* node)
{
	long numKeys = node->NumKeys();
	for (long slot = 0; slot <= numKeys; slot++)
	{
		KeyField* kf = KeyFieldAdr(node, slot);
		if (KeyFieldFlags(kf) == kKeyFieldHasDups && KeyFieldSize(kf) != 0)
			FreeDupNodes(kf);
		ULong childId = LeftNodeNo(node, slot);
		if (childId != 0)
			FreeNodes(ReadANode(childId, node->Id()));
	}
	fNodeCache->DeleteNode(node->Id());
}


// ROM 0x002c13ec FreeDupNodes__10TSoupIndexFP8KeyField
void
TSoupIndex::FreeDupNodes(KeyField* kf)
{
	ULong dupId = kfNextDupID(kf);
	while (dupId != 0)
	{
		DupNodeHeader* dupNode = ReadADupNode(dupId);
		dupId = dupNode->NextId();
		DeleteNode(dupNode->Id());
	}
}


/*------------------------------------------------------------------------------
	T h e   B - t r e e   o p e r a t i o n s
	==> kIndexOK, else kIndexNotFound / kIndexEnd.
------------------------------------------------------------------------------*/

// ROM 0x002c15fc _BTEnterKey__10TSoupIndexFP8KeyField
// The field added; a new root when the root splits.
int
TSoupIndex::_BTEnterKey(KeyField* kf)
{
	fErr = 0;
	int result = kIndexNotFound;
	NodeHeader* root = ReadRootNode(true);
	if (root != nil)
	{
		ULong rightId;
		Boolean split = false;
		if (InsertKey(kf, root, &rightId, &split))
		{
			if (split)
				CreateNewRoot(kf, rightId);
			result = kIndexOK;
		}
	}
	fErr = result;
	return fErr;
}


// ROM 0x002c168c _BTRemoveKey__10TSoupIndexFP8KeyField
// The field removed; an emptied root gives way to its only child.
int
TSoupIndex::_BTRemoveKey(KeyField* kf)
{
	int result = kIndexNotFound;
	Boolean underflow = false;
	NodeHeader* root = ReadRootNode(false);
	if (root != nil && DeleteKey(kf, root, &underflow))
	{
		if (underflow)
		{
			if (root->NumKeys() == 0)
			{
				SetRootNode(FirstNodeNo(root));
				DeleteNode(root->Id());
			}
			else
				fNodeCache->DirtyNode(root);
		}
		result = kIndexOK;
	}
	fErr = result;
	return fErr;
}


// ROM 0x002c1514 _BTGetNextKey__10TSoupIndexFP8KeyField
int
TSoupIndex::_BTGetNextKey(KeyField* kf)
{
	NodeHeader* node = ReadRootNode(false);
	if (node == nil)
		return kIndexNotFound;
	long slot;
	int result = SearchNext(kf, &node, &slot);
	if (result == 0)
		return kIndexEnd;
	return result == -1 ? kIndexNotFound : kIndexOK;
}


// ROM 0x002c157c _BTGetPriorKey__10TSoupIndexFP8KeyField
int
TSoupIndex::_BTGetPriorKey(KeyField* kf)
{
	NodeHeader* node = ReadRootNode(false);
	long slot;
	int result = SearchPrior(kf, &node, &slot);
	if (result == 0)
		return kIndexEnd;
	return result == -1 ? kIndexNotFound : kIndexOK;
}


// ROM 0x002c142c _BTGetNextDupKey__10TSoupIndexFP8KeyField
int
TSoupIndex::_BTGetNextDupKey(KeyField* kf)
{
	NodeHeader* node = ReadRootNode(false);
	if (node == nil)
		return kIndexNotFound;
	long slot;
	DupNodeHeader* dupNode;
	int result = SearchNextDup(kf, &node, &slot, &dupNode);
	if (result == 0)
		return kIndexEnd;
	return result == -1 ? kIndexNotFound : kIndexOK;
}


// ROM 0x002c14a0 _BTGetPriorDupKey__10TSoupIndexFP8KeyField
int
TSoupIndex::_BTGetPriorDupKey(KeyField* kf)
{
	NodeHeader* node = ReadRootNode(false);
	if (node == nil)
		return kIndexNotFound;
	long slot;
	DupNodeHeader* dupNode;
	int result = SearchPriorDup(kf, &node, &slot, &dupNode);
	if (result == 0)
		return kIndexEnd;
	return result == -1 ? kIndexNotFound : kIndexOK;
}


/*------------------------------------------------------------------------------
	T h e   i n t e r f a c e
	Each operation is a node cache transaction: committed when it
	succeeds, aborted when it throws.
------------------------------------------------------------------------------*/

// ROM 0x002c1d6c Add__10TSoupIndexFP4SKeyT1
// The key and its datum added.  ==> kIndexOK.
int
TSoupIndex::Add(SKey* key, SKey* data)
{
	kfAssembleKeyField(theKeyField, key, data);
	volatile int result = kIndexNotFound;
	newton_try
	{
		result = _BTEnterKey(theKeyField);
	}
	newton_catch_all
	{
		fNodeCache->Abort(this);
		rethrow;
	}
	end_try;
	if (result == kIndexOK)
		fNodeCache->Commit(this);
	else
		fNodeCache->Abort(this);
	return result;
}


// ROM 0x002c1dfc AddInTransaction__10TSoupIndexFP4SKeyT1
// Added within a larger transaction: the nodes are only committed when
// the cache grows past 32 of them.
int
TSoupIndex::AddInTransaction(SKey* key, SKey* data)
{
	kfAssembleKeyField(theKeyField, key, data);
	int result = _BTEnterKey(theKeyField);
	if (fNodeCache->fNumEntries > 32)
		fNodeCache->Commit(this);
	return result;
}


// ROM 0x002c283c Delete__10TSoupIndexFP4SKeyT1
// The key's datum removed.  ==> kIndexOK, kIndexNotFound.
int
TSoupIndex::Delete(SKey* key, SKey* data)
{
	kfAssembleKeyField(theKeyField, key, data);
	volatile int result = kIndexNotFound;
	newton_try
	{
		ClearKeyField(savedKey);
		result = _BTRemoveKey(theKeyField);
	}
	newton_catch_all
	{
		fNodeCache->Abort(this);
		rethrow;
	}
	end_try;
	if (result == kIndexOK)
		fNodeCache->Commit(this);
	else
		fNodeCache->Abort(this);
	return result;
}


// ROM 0x002c1e50 Find__10TSoupIndexFP4SKeyN21Uc
// The key looked up: kIndexOK when it is there (outKey and outData its
// key and first datum), kIndexNotFound when the key after it is
// (outKey/outData that), kIndexEnd when nothing follows.  Not exact, a
// string key is matched at the lowest sort order (NOT YET RECONSTRUCTED:
// TSortingTable).  An exception's error is the result.
int
TSoupIndex::Find(SKey* key, SKey* outKey, SKey* outData, Boolean exact)
{
	if (fInfo.fRootNodeId == 0)
		return kIndexEnd;
	kfAssembleKeyField(theKeyField, key, nil);
	long lowestSortLength = 0;
	if (fSortingTable != nil && !exact && fInfo.fKeyType == kKeyTypeString)
	{
		lowestSortLength = key->Size() / sizeof(UniChar);
		// TSortingTable::ConvertTextToLowestSort(fSortingTable, theKeyField + 4, lowestSortLength): NOT YET
	}
	volatile int result = kIndexOK;
	volatile Boolean failed = false;
	newton_try
	{
		NodeHeader* node = ReadRootNode(false);
		if (node == nil)
			result = kIndexNotFound;
		else
		{
			long slot;
			if (!Search(theKeyField, &node, &slot))
				result = KeyFieldSize(theKeyField) == 0 ? kIndexEnd : kIndexNotFound;
			fNodeCache->Commit(this);
		}
	}
	newton_catch_all
	{
		failed = true;
		result = (int) (intptr_t) _info.exception.data;
		fNodeCache->Abort(this);
	}
	end_try;
	if (failed)
		return result;
	if (lowestSortLength != 0 && result == kIndexNotFound)
	{
		// the next key may be the same text at another sort order
		if (StringKeyCompare(*key, *KeyFieldKey(theKeyField)) == 0)
			result = kIndexOK;
	}
	if (result == kIndexOK || result == kIndexNotFound)
		kfDisassembleKeyField(theKeyField, outKey, outData);
	return result;
}


// ROM 0x002c28e8 First__10TSoupIndexFP4SKeyT1
int
TSoupIndex::First(SKey* outKey, SKey* outData)
{
	volatile int result = kIndexNotFound;
	newton_try
	{
		NodeHeader* root = ReadRootNode(false);
		if (root != nil && FindFirstKey(root, theKeyField))
			result = kIndexOK;
	}
	newton_catch_all
	{
		fNodeCache->Abort(this);
		rethrow;
	}
	end_try;
	if (result == kIndexOK)
		kfDisassembleKeyField(theKeyField, outKey, outData);
	fNodeCache->Commit(this);
	return result;
}


// ROM 0x002c29b8 Last__10TSoupIndexFP4SKeyT1
int
TSoupIndex::Last(SKey* outKey, SKey* outData)
{
	volatile int result = kIndexNotFound;
	newton_try
	{
		NodeHeader* root = ReadRootNode(false);
		if (root != nil && FindLastKey(root, theKeyField))
			result = kIndexOK;
	}
	newton_catch_all
	{
		fNodeCache->Abort(this);
		rethrow;
	}
	end_try;
	fNodeCache->Commit(this);
	if (result == kIndexOK)
		kfDisassembleKeyField(theKeyField, outKey, outData);
	return result;
}


// ROM 0x002c2a88 Next__10TSoupIndexFP4SKeyT1iN21
// The entry after key/data: by mode, the key's next datum then the next
// key, the next key only, or the next datum only.
int
TSoupIndex::Next(SKey* key, SKey* data, int mode, SKey* outKey, SKey* outData)
{
	kfAssembleKeyField(theKeyField, key, data);
	volatile int result = kIndexNotFound;
	newton_try
	{
		Boolean nextKey = true;
		if (mode != kIndexNextKey)
		{
			result = _BTGetNextDupKey(theKeyField);
			nextKey = (result != kIndexOK && result != kIndexNotFound) && mode != kIndexNextDup;
		}
		if (nextKey)
			result = _BTGetNextKey(theKeyField);
	}
	newton_catch_all
	{
		fNodeCache->Abort(this);
		rethrow;
	}
	end_try;
	fNodeCache->Commit(this);
	if (result == kIndexOK)
		kfDisassembleKeyField(theKeyField, outKey, outData);
	return result;
}


// ROM 0x002c2b70 Prior__10TSoupIndexFP4SKeyT1UcN21
// The entry before key/data: the key's prior datum unless skipDups, then
// the prior key.
int
TSoupIndex::Prior(SKey* key, SKey* data, Boolean skipDups, SKey* outKey, SKey* outData)
{
	kfAssembleKeyField(theKeyField, key, data);
	volatile int result = kIndexNotFound;
	newton_try
	{
		result = kIndexOK;
		if (skipDups || _BTGetPriorDupKey(theKeyField) != kIndexOK)
			result = _BTGetPriorKey(theKeyField);
	}
	newton_catch_all
	{
		fNodeCache->Abort(this);
		rethrow;
	}
	end_try;
	fNodeCache->Commit(this);
	if (result == kIndexOK)
		kfDisassembleKeyField(theKeyField, outKey, outData);
	return result;
}


// ROM 0x002c265c Search__10TSoupIndexFiP4SKeyT2PFP4SKeyT1Pv_iPvN22
// Forward (or backward) from key/data (from the first entry for a nil
// key), each entry to stop until it says to stop there: outKey/outData
// that entry.  ==> kIndexOK when stopped, kIndexEnd when the index ran
// out, kIndexNotFound for an empty index.
int
TSoupIndex::Search(Boolean forward, SKey* key, SKey* data, IndexStopProcPtr stop, void* refCon, SKey* outKey, SKey* outData)
{
	if (key == nil)
	{
		SKey firstKey, firstData;
		firstKey.Clear();
		firstData.Clear();
		int result = First(&firstKey, &firstData);
		if (result != kIndexOK)
			return result;
		if (stop(&firstKey, &firstData, refCon))
			return kIndexOK;
		kfAssembleKeyField(theKeyField, &firstKey, &firstData);
	}
	else
		kfAssembleKeyField(theKeyField, key, data);

	volatile int result = kIndexOK;
	newton_try
	{
		IndexState state;
		result = MoveAndGetState(forward, kIndexNextDupOrKey, theKeyField, &state);
		while (result == kIndexOK)
		{
			if (stop(KeyFieldKey(theKeyField), (SKey*) kfFirstDataAdr(theKeyField), refCon))
				break;
			if (fNodeCache->fNumEntries > 32)
			{
				fNodeCache->Commit(this);
				result = MoveAndGetState(forward, kIndexNextDupOrKey, theKeyField, &state);
			}
			else
				result = MoveUsingState(forward, kIndexNextDupOrKey, theKeyField, &state);
		}
		if (result == kIndexOK)
			kfDisassembleKeyField(theKeyField, outKey, outData);
	}
	newton_catch_all
	{
		fNodeCache->Abort(this);
		rethrow;
	}
	end_try;
	fNodeCache->Commit(this);
	return result;
}


/*------------------------------------------------------------------------------
	I t e r a t i o n   b y   s t a t e
	A cursor keeps an IndexState so that the next step need not search
	from the root while the cache still holds the nodes.
------------------------------------------------------------------------------*/

// The state's dup flag from the key field at its position.
static inline void
SetStateAtKey(TSoupIndex* index, IndexState* state)
{
	state->fHasDups = KeyFieldFlags(index->KeyFieldAdr(state->fNode, state->fSlot)) != 0;
	state->fDupNode = nil;
}


// ROM 0x002c202c FindAndGetState__10TSoupIndexFP8KeyFieldP10IndexState
// ==> kIndexOK found, kIndexNotFound (kf is the key after), kIndexEnd.
int
TSoupIndex::FindAndGetState(KeyField* kf, IndexState* state)
{
	state->fNode = ReadRootNode(false);
	if (state->fNode == nil)
		return kIndexEnd;
	int result = kIndexOK;
	if (!Search(kf, &state->fNode, &state->fSlot))
	{
		if (KeyFieldSize(kf) == 0)
			return kIndexEnd;
		result = kIndexNotFound;
	}
	SetStateAtKey(this, state);
	return result;
}


// ROM 0x002c20c8 FindLastAndGetState__10TSoupIndexFP8KeyFieldP10IndexState
// The last key with its last datum.
int
TSoupIndex::FindLastAndGetState(KeyField* kf, IndexState* state)
{
	state->fNode = ReadRootNode(false);
	if (state->fNode == nil)
		return kIndexEnd;
	ULong childId = LastNodeNo(state->fNode);
	while (childId != 0)
	{
		state->fNode = ReadANode(childId, state->fNode->Id());
		childId = LastNodeNo(state->fNode);
	}
	state->fSlot = LastSlotInNode(state->fNode);
	KeyField* last = LastKeyField(state->fNode);
	MoveKey(last, kf);
	state->fHasDups = KeyFieldFlags(last) == kKeyFieldHasDups;
	if (state->fHasDups)
	{
		state->fDupNode = nil;
		kfReplaceFirstData(kf, LastDupDataAdr(last, &state->fDupNode));
	}
	return kIndexOK;
}


// ROM 0x002c21b4 FindPriorAndGetState__10TSoupIndexFP8KeyFieldUcP10IndexState
// The key (with its last datum), or the one before where it would be;
// movePrior: the one before the key when it is there.
int
TSoupIndex::FindPriorAndGetState(KeyField* kf, Boolean movePrior, IndexState* state)
{
	int result = FindAndGetState(kf, state);
	if (result == kIndexOK)
	{
		if (movePrior)
			result = MoveUsingState(false, kIndexNextDupOrKey, kf, state);
		if (state->fHasDups)
		{
			KeyField* here = KeyFieldAdr(state->fNode, state->fSlot);
			kfReplaceFirstData(kf, LastDupDataAdr(here, &state->fDupNode));
		}
	}
	else if (result == kIndexNotFound)
	{
		result = MoveUsingState(false, kIndexNextDupOrKey, kf, state);
		if (result == kIndexOK)
			result = kIndexNotFound;
	}
	else if (result == kIndexEnd)
		result = FindLastAndGetState(kf, state);
	return result;
}


// ROM 0x002c229c MoveAndGetState__10TSoupIndexFUciP8KeyFieldP10IndexState
// The entry after (before) kf's, searching from the root: by mode as
// Next.  ==> kIndexOK, kIndexNotFound when kf's entry is not there,
// kIndexEnd.
int
TSoupIndex::MoveAndGetState(Boolean forward, int mode, KeyField* kf, IndexState* state)
{
	state->fNode = ReadRootNode(false);
	if (state->fNode == nil)
		return kIndexEnd;
	int result;
	if (mode == kIndexNextKey)
	{
		result = forward ? SearchNext(kf, &state->fNode, &state->fSlot)
						 : SearchPrior(kf, &state->fNode, &state->fSlot);
		if (result == -1)
			return kIndexNotFound;
		if (result == 0)
			return kIndexEnd;
	}
	else
	{
		result = forward ? SearchNextDup(kf, &state->fNode, &state->fSlot, &state->fDupNode)
						 : SearchPriorDup(kf, &state->fNode, &state->fSlot, &state->fDupNode);
		if (result != 0)
		{
			if (result == -1)
				return kIndexNotFound;
			state->fHasDups = true;
			return kIndexOK;
		}
		if (mode == kIndexNextDup)
			return kIndexEnd;
		result = forward ? FindNextKey(kf, &state->fNode, &state->fSlot)
						 : FindPriorKey(kf, &state->fNode, &state->fSlot);
		if (result == 0)
			return kIndexEnd;
	}
	SetStateAtKey(this, state);
	return kIndexOK;
}


// ROM 0x002c23d4 MoveUsingState__10TSoupIndexFUciP8KeyFieldP10IndexState
// The entry after (before) kf's from where the state says it is.
int
TSoupIndex::MoveUsingState(Boolean forward, int mode, KeyField* kf, IndexState* state)
{
	if (state->fHasDups && mode != kIndexNextKey)
	{
		void* data = kfFirstDataAdr(kf);
		void* next = nil;
		if (state->fDupNode == nil)
		{
			// the datum is in the field
			KeyField* here = KeyFieldAdr(state->fNode, state->fSlot);
			Boolean more;
			if (!forward)
				more = kfFindDataAdr(here, data, &next) != nil;
			else
				more = kfNextDataAdr(here, kfFindDataAdr(here, data, nil), &next);
			if (!more)
			{
				next = nil;
				if (!forward)
					return MoveAndGetState(false, mode, kf, state);
				ULong dupId = kfNextDupID(here);
				if (dupId != 0)
				{
					state->fDupNode = ReadADupNode(dupId);
					next = FirstDupDataAdr(state->fDupNode);
				}
			}
		}
		else if (!forward)
		{
			KeyField* here = KeyFieldAdr(state->fNode, state->fSlot);
			FindDupDataAdr(state->fDupNode, data, &next);
			if (next == nil)
			{
				// the first of this dup node: the last of the one before
				ULong dupId = kfNextDupID(here);
				if (state->fDupNode->Id() == dupId)
				{
					state->fDupNode = nil;
					kfFindDataAdr(here, data, &next);
				}
				else
				{
					state->fDupNode = ReadADupNode(dupId);
					next = FindPriorDupDataAdr(&state->fDupNode, data, nil);
				}
			}
		}
		else
			next = FindNextDupDataAdr(&state->fDupNode, data, nil);
		if (next != nil)
		{
			kfReplaceFirstData(kf, next);
			return kIndexOK;
		}
		if (mode == kIndexNextDup)
			return kIndexEnd;
	}
	else if (mode == kIndexNextDup)
		return kIndexEnd;

	Boolean found = forward ? FindNextKey(kf, &state->fNode, &state->fSlot)
							: FindPriorKey(kf, &state->fNode, &state->fSlot);
	if (!found)
		return kIndexEnd;
	SetStateAtKey(this, state);
	return kIndexOK;
}


/*------------------------------------------------------------------------------
	T N o d e C a c h e
	The functions that write through the index.
------------------------------------------------------------------------------*/

// ROM 0x002c3e9c DeleteNode__10TNodeCacheFUl
// The cached node forgotten and its object deleted from its index's store.
void
TNodeCache::DeleteNode(ULong id)
{
	NodeCacheEntry* entry = (NodeCacheEntry*) *fEntries;
	NodeCacheEntry* end = entry + fNumEntries;
	for (; entry < end; entry++)
		if (entry->fId == id)
		{
			TSoupIndex* index = entry->fIndex;
			entry->fId = 0;
			entry->fStamp = 0;
			entry->fIndex = nil;
			entry->fInUse = false;
			OSErrIf(index->fStoreWrapper->Store()->DeleteObject(id));
			fModCount++;
			return;
		}
}


// ROM 0x002c3fcc Commit__10TNodeCacheFP10TSoupIndex
// The index's dirty nodes written and its entries released; when no
// other index has entries in use the cache is trimmed back to 8.
void
TNodeCache::Commit(TSoupIndex* index)
{
	HLock(fEntries);
	NodeCacheEntry* entries = (NodeCacheEntry*) *fEntries;
	Boolean othersInUse = false;
	for (long i = 0; i < fNumEntries; i++)
	{
		NodeCacheEntry* entry = &entries[i];
		if (entry->fIndex == index)
		{
			entry->fInUse = false;
			if (entry->fIsDirty)
			{
				if (entry->fIsDup)
					index->UpdateDupNode(entry->fNode);
				else
					index->UpdateNode(entry->fNode);
				entry->fIsDirty = false;
			}
		}
		else if (entry->fInUse)
			othersInUse = true;
	}
	if (!othersInUse && fNumEntries > kNodeCacheShrinkEntries)
	{
		fModCount++;
		for (long i = kNodeCacheShrinkEntries; i < fNumEntries; i++)
			DisposPtr((Ptr) entries[i].fNode);
		HUnlock(fEntries);
		SetHandleSize(fEntries, kNodeCacheShrinkEntries * sizeof(NodeCacheEntry));
		fNumEntries = kNodeCacheShrinkEntries;
		return;
	}
	HUnlock(fEntries);
}
