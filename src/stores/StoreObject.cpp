/*
	File:		stores/StoreObject.cpp

	Contains:	TStoreObjectWriter, TStoreObjectReader, the precedent tables
				and StorePermObject/LoadPermObject/DeletePermObject
				(StoreObject.h): frames objects to and from store objects.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	NOT YET RECONSTRUCTED: large binaries (tag 12: LoadLargeBinary,
	DuplicateLargeBinary, CommitLargeBinary, FinalizeLargeObjectWrites,
	ZapLargeBinaries) - a large binary in an object to write, or met in an
	object read, throws kNSErrNativeNotReconstructed; the word hints
	(TWordHintsHandler: GetNumHintChunks, SetHints) - objects are written
	with no hint chunks, and read with any number.
*/

#include "StoreObject.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "ByteOrder.h"

#include <stdlib.h>
#include <string.h>

extern const ExceptionName exOutOfMemory;

// the tables the writer and reader share when no other is using them
TPrecedentsForWriting*	gPrecedentsForWriting = nil;		// 0x0c102a30
Boolean					gPrecedentsForWritingUsed = false;	// 0x0c102a34
TPrecedentsForReading*	gPrecedentsForReading = nil;		// 0x0c102a28
Boolean					gPrecedentsForReadingUsed = false;	// 0x0c102a2d
int						gDefaultHintsHandlerId = 0;			// 0x0c1024e8


static void
NotYetLargeBinaries(void)
{
	Throw(exStoreError, (void*) (Long) kNSErrNativeNotReconstructed, nil);
}


/* -------------------------------------------------------------------------------
	StoreObjectHeader
------------------------------------------------------------------------------- */

// ROM 0x002b69a4 GetHintsHandlerId__17StoreObjectHeaderFv
// The id in bits 1 and 3 of the flags.
int
StoreObjectHeader::GetHintsHandlerId(void) const
{
	return ((fFlags & 2) >> 1) | ((fFlags & 8) >> 2);
}


// ROM 0x002b70e8 SetHintsHandlerId__17StoreObjectHeaderFi
void
StoreObjectHeader::SetHintsHandlerId(int id)
{
	fFlags = (UByte) ((fFlags & ~0x0a) | ((id & 1) << 1) | ((id & 2) << 2));
}


// the header from its bytes on the store
void
StoreObjectHeader::ReadFrom(const void* bytes)
{
	const UByte* b = (const UByte*) bytes;
	fUniqueId = (Long32) GetBigEndianWord(b);
	fModTime = GetBigEndianWord(b + 4);
	fTextBlockId = GetBigEndianWord(b + 8);
	fNumHints = b[0xc];
	fFlags = b[0xd];
	fTextSizeHi = b[0xe];
	fTextSizeLo = b[0xf];
}


void
StoreObjectHeader::WriteTo(void* bytes) const
{
	UByte* b = (UByte*) bytes;
	PutBigEndianWord(b, (ULong32) fUniqueId);
	PutBigEndianWord(b + 4, fModTime);
	PutBigEndianWord(b + 8, fTextBlockId);
	b[0xc] = fNumHints;
	b[0xd] = fFlags;
	b[0xe] = fTextSizeHi;
	b[0xf] = fTextSizeLo;
}


// ROM 0x002b69bc ClearHintBits__FPl
static void
ClearHintBits(long* chunk)
{
	chunk[0] = 0;
	chunk[1] = 0;
}


/* -------------------------------------------------------------------------------
	Small rects and references
------------------------------------------------------------------------------- */

// ROM 0x0032aec4 PackSmallRect__FlPl
// A frame of exactly {top, left, bottom, right}, each an integer 0..255,
// packed into a long (top the high byte); ==> whether it is one.
Boolean
PackSmallRect(Ref frame, long* packed)
{
	static Ref* const sides[4] = { &RSSYMtop, &RSSYMleft, &RSSYMbottom, &RSSYMright };
	if (Length(frame) != 4)
		return false;
	long value = 0;
	for (int i = 0; i < 4; i++)
	{
		Ref side = GetFrameSlotRef(frame, *sides[i]);
		if (!ISINT(side) || RINT(side) < 0 || RINT(side) > 0xff)
			return false;
		value = (value << 8) | RINT(side);
	}
	if (packed != nil)
		*packed = value;
	return true;
}


// ROM 0x0032aff0 UnpackSmallRect__Fl
Ref
UnpackSmallRect(long packed)
{
	RefVar rect(Clone(RefVar(Rcanonicalrect != NILREF ? Rcanonicalrect : AllocateFrame())));
	SetFrameSlot(rect, RSSYMright, RefVar(MAKEINT(packed & 0xff)));
	SetFrameSlot(rect, RSSYMbottom, RefVar(MAKEINT((packed >> 8) & 0xff)));
	SetFrameSlot(rect, RSSYMleft, RefVar(MAKEINT((packed >> 16) & 0xff)));
	SetFrameSlot(rect, RSSYMtop, RefVar(MAKEINT((packed >> 24) & 0xff)));
	return rect;
}


// ROM 0x002b79bc WriteReference__FPcl
// A map or symbol reference: three bytes, big-endian.
void
WriteReference(char* bytes, long reference)
{
	bytes[0] = (char) (reference >> 16);
	bytes[1] = (char) (reference >> 8);
	bytes[2] = (char) reference;
}


// ROM 0x002b79e8 WriteReference__FR15TStoreWritePipel
void
WriteReference(TStoreWritePipe& pipe, long reference)
{
	char bytes[3];
	WriteReference(bytes, reference);
	pipe.Write(bytes, 3);
}


// ROM 0x002b7a08 ReadReference__FR14TStoreReadPipe
long
ReadReference(TStoreReadPipe& pipe)
{
	UByte bytes[3];
	pipe.Read((char*) bytes, 3);
	return ((long) bytes[0] << 16) | ((long) bytes[1] << 8) | bytes[2];
}


/* -------------------------------------------------------------------------------
	TBucketArray
------------------------------------------------------------------------------- */

// ROM 0x0032a574 __ct__12TBucketArrayFl
TBucketArray::TBucketArray(long elementSize)
{
	fNumElements = 0;
	fElementSize = elementSize;
	fNumBuckets = 0;
	fBuckets = nil;
}


// ROM 0x0032a5b4 __dt__12TBucketArrayFv
TBucketArray::~TBucketArray()
{
	for (long i = 0; i < fNumBuckets; i++)
		free(fBuckets[i]);
	free(fBuckets);
}


// ROM 0x0032aaa8 ElementAt__12TBucketArrayFl
void*
TBucketArray::ElementAt(long index)
{
	if (index < 0 || index >= fNumElements)
		Throw(exFrames, (void*) (Long) kNSErrOutOfRange, nil);
	return (char*) fBuckets[index >> 6] + fElementSize * (index & 0x3f);
}


// ROM 0x0032ad20 SetNumberOfElements__12TBucketArrayFl
// Buckets for count elements (one more than needed, so that the next
// element has room); extra buckets freed.
void
TBucketArray::SetNumberOfElements(long count)
{
	long buckets = (count >> 6) + 1;
	if (buckets < fNumBuckets)
	{
		for (long i = buckets; i < fNumBuckets; i++)
			free(fBuckets[i]);
		void** grown = (void**) realloc(fBuckets, buckets * sizeof(void*));
		if (grown == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		fBuckets = grown;
		fNumBuckets = buckets;
	}
	else if (buckets > fNumBuckets)
	{
		void** grown = (void**) realloc(fBuckets, buckets * sizeof(void*));
		if (grown == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		fBuckets = grown;
		for (long i = fNumBuckets; i < buckets; i++)
		{
			fBuckets[i] = malloc(fElementSize << 6);
			if (fBuckets[i] == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			fNumBuckets = i + 1;
		}
		fNumBuckets = buckets;
	}
	fNumElements = count;
}


/* -------------------------------------------------------------------------------
	TPrecedentsForWriting
	Element 0 is the trie root of the ROM; elements 1.. are the objects
	(12 bytes each: the ref and the trie's two link words).  The hash
	table (DEVIATION, see StoreObject.h) maps a ref to its element.
------------------------------------------------------------------------------- */

struct WritingPrecedent
{
	Ref		fRef;			// +0x00
	long	fLink0;			// +0x04  (the ROM's trie links; unused here)
	long	fLink1;			// +0x08
};

// ROM 0x0032a610 __ct__21TPrecedentsForWritingFv
TPrecedentsForWriting::TPrecedentsForWriting()
	: TBucketArray(sizeof(WritingPrecedent))
{
	fHashTable = nil;
	fHashIndexes = nil;
	fHashSize = 0;
	Reset();
	GCRegister(this, GCOccured);
	DIYGCRegister(this, GCMark, GCUpdate);
}


// ROM 0x0032a678 __dt__21TPrecedentsForWritingFv
TPrecedentsForWriting::~TPrecedentsForWriting()
{
	DIYGCUnregister(this);
	GCUnregister(this);
	free(fHashTable);
	free(fHashIndexes);
}


static inline long
HashRef(Ref r, long size)
{
	ULong h = (ULong) r;
	h ^= h >> 7;
	h *= 0x9e3779b1UL;
	return (long) ((h >> 8) & (ULong) (size - 1));
}


// the ref entered in the hash table (which doubles when half full)
static void
HashInsert(Ref** table, long** indexes, long* size, Ref ref, long index)
{
	if (*size == 0 || index * 2 >= *size)
	{
		long newSize = *size == 0 ? 64 : *size * 2;
		Ref* newTable = (Ref*) calloc(newSize, sizeof(Ref));
		long* newIndexes = (long*) calloc(newSize, sizeof(long));
		if (newTable == nil || newIndexes == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		for (long i = 0; i < *size; i++)
			if ((*indexes)[i] != 0)
			{
				long slot = HashRef((*table)[i], newSize);
				while (newIndexes[slot] != 0)
					slot = (slot + 1) & (newSize - 1);
				newTable[slot] = (*table)[i];
				newIndexes[slot] = (*indexes)[i];
			}
		free(*table);
		free(*indexes);
		*table = newTable;
		*indexes = newIndexes;
		*size = newSize;
	}
	long slot = HashRef(ref, *size);
	while ((*indexes)[slot] != 0)
		slot = (slot + 1) & (*size - 1);
	(*table)[slot] = ref;
	(*indexes)[slot] = index;
}


// ROM 0x0032a6b8 Append__21TPrecedentsForWritingFRC6RefVar
// ==> the object's index (its element number less one)
long
TPrecedentsForWriting::Append(RefArg obj)
{
	long element = fNumElements;
	SetNumberOfElements(element + 1);
	WritingPrecedent* p = (WritingPrecedent*) ElementAt(element);
	p->fRef = obj;
	p->fLink0 = 0;
	p->fLink1 = 0;
	HashInsert(&fHashTable, &fHashIndexes, &fHashSize, obj, element);
	return element - 1;
}


// ROM 0x0032a704 Find__21TPrecedentsForWritingFRC6RefVar
// The object's index, -1 when it has not been written.
long
TPrecedentsForWriting::Find(RefArg obj)
{
	if (fHashSize == 0)
		return -1;
	Ref ref = obj;
	long slot = HashRef(ref, fHashSize);
	while (fHashIndexes[slot] != 0)
	{
		if (fHashTable[slot] == ref)
			return fHashIndexes[slot] - 1;
		slot = (slot + 1) & (fHashSize - 1);
	}
	return -1;
}


// ROM 0x0032a748 Reset__21TPrecedentsForWritingFv
// Just the root.
void
TPrecedentsForWriting::Reset(void)
{
	SetNumberOfElements(1);
	WritingPrecedent* root = (WritingPrecedent*) ElementAt(0);
	root->fRef = NILREF;
	root->fLink0 = 0;
	root->fLink1 = 0x1f000000;
	if (fHashSize != 0)
	{
		memset(fHashTable, 0, fHashSize * sizeof(Ref));
		memset(fHashIndexes, 0, fHashSize * sizeof(long));
	}
}


// ROM 0x0032ab04 RebuildTable__21TPrecedentsForWritingFv
// After a collection moved the objects: every element found again.
void
TPrecedentsForWriting::RebuildTable(void)
{
	if (fHashSize != 0)
	{
		memset(fHashTable, 0, fHashSize * sizeof(Ref));
		memset(fHashIndexes, 0, fHashSize * sizeof(long));
	}
	for (long i = 1; i < fNumElements; i++)
		HashInsert(&fHashTable, &fHashIndexes, &fHashSize, ((WritingPrecedent*) ElementAt(i))->fRef, i);
}


// ROM 0x0032ab80 MarkAllRefs__21TPrecedentsForWritingFv
void
TPrecedentsForWriting::MarkAllRefs(void)
{
	for (long i = 1; i < fNumElements; i++)
		DIYGCMark(((WritingPrecedent*) ElementAt(i))->fRef);
}


// ROM 0x0032abcc UpdateAllRefs__21TPrecedentsForWritingFv
void
TPrecedentsForWriting::UpdateAllRefs(void)
{
	for (long i = 1; i < fNumElements; i++)
	{
		WritingPrecedent* p = (WritingPrecedent*) ElementAt(i);
		p->fRef = DIYGCUpdate(p->fRef);
	}
}


// ROM 0x0032aaa4 GCOccured__21TPrecedentsForWritingSFPv
void
TPrecedentsForWriting::GCOccured(void* refCon)
{
	((TPrecedentsForWriting*) refCon)->RebuildTable();
}


// ROM 0x0032ab7c GCMark__21TPrecedentsForWritingSFPv
void
TPrecedentsForWriting::GCMark(void* refCon)
{
	((TPrecedentsForWriting*) refCon)->MarkAllRefs();
}


// ROM 0x0032abc8 GCUpdate__21TPrecedentsForWritingSFPv
void
TPrecedentsForWriting::GCUpdate(void* refCon)
{
	((TPrecedentsForWriting*) refCon)->UpdateAllRefs();
}


/* -------------------------------------------------------------------------------
	TPrecedentsForReading: the objects read so far, by index
------------------------------------------------------------------------------- */

// ROM 0x0032ac28 __ct__21TPrecedentsForReadingFv
TPrecedentsForReading::TPrecedentsForReading()
	: TBucketArray(sizeof(Ref))
{
	Reset();
	DIYGCRegister(this, GCMark, GCUpdate);
}


// ROM 0x0032ac80 __dt__21TPrecedentsForReadingFv
TPrecedentsForReading::~TPrecedentsForReading()
{
	DIYGCUnregister(this);
}


// ROM 0x0032acb8 Append__21TPrecedentsForReadingFRC6RefVar
long
TPrecedentsForReading::Append(RefArg obj)
{
	long index = fNumElements;
	SetNumberOfElements(index + 1);
	*(Ref*) ElementAt(index) = obj;
	return index;
}


// ROM 0x0032acf8 Replace__21TPrecedentsForReadingFlRC6RefVar
void
TPrecedentsForReading::Replace(long index, RefArg obj)
{
	*(Ref*) ElementAt(index) = obj;
}


// ROM 0x0032ad18 Reset__21TPrecedentsForReadingFv
// Empty, with one bucket kept.
void
TPrecedentsForReading::Reset(void)
{
	if (fNumBuckets < 1)
	{
		void** grown = (void**) realloc(fBuckets, sizeof(void*));
		if (grown == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		fBuckets = grown;
		fBuckets[0] = malloc(fElementSize << 6);
		if (fBuckets[0] == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		fNumBuckets = 1;
	}
	else if (fNumBuckets > 1)
	{
		for (long i = 1; i < fNumBuckets; i++)
			free(fBuckets[i]);
		void** grown = (void**) realloc(fBuckets, sizeof(void*));
		if (grown == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		fBuckets = grown;
		fNumBuckets = 1;
	}
	fNumElements = 0;
}


// ROM 0x0032ae28 MarkAllRefs__21TPrecedentsForReadingFv
void
TPrecedentsForReading::MarkAllRefs(void)
{
	for (long i = 0; i < fNumElements; i++)
		DIYGCMark(*(Ref*) ElementAt(i));
}


// ROM 0x0032ae74 UpdateAllRefs__21TPrecedentsForReadingFv
void
TPrecedentsForReading::UpdateAllRefs(void)
{
	for (long i = 0; i < fNumElements; i++)
	{
		Ref* p = (Ref*) ElementAt(i);
		*p = DIYGCUpdate(*p);
	}
}


// ROM 0x0032ae24 GCMark__21TPrecedentsForReadingSFPv
void
TPrecedentsForReading::GCMark(void* refCon)
{
	((TPrecedentsForReading*) refCon)->MarkAllRefs();
}


// ROM 0x0032ae70 GCUpdate__21TPrecedentsForReadingSFPv
void
TPrecedentsForReading::GCUpdate(void* refCon)
{
	((TPrecedentsForReading*) refCon)->UpdateAllRefs();
}


/* -------------------------------------------------------------------------------
	TStoreObjectWriter
------------------------------------------------------------------------------- */

// the bytes a long takes in the stream
static inline long
LongSize(long l)
{
	return (l >= 0 && l < 0xff) ? 1 : 5;
}


// ROM 0x002b7a30 __ct__18TStoreObjectWriterFRC6RefVarP13TStoreWrapperUl
// To write obj to the wrapper's store as object id (-1: a new one).
TStoreObjectWriter::TStoreObjectWriter(RefArg obj, TStoreWrapper* wrapper, PSSId id)
{
	fRootObject = obj;
	fObject = obj;
	fStreamSize = 0;
	fWrapper = wrapper;
	fTextSize = 0;
	fHintWords = 0;
	fObjectId = id;
	fHeader = nil;
	if (!gPrecedentsForWritingUsed)
	{
		if (gPrecedentsForWriting == nil)
			gPrecedentsForWriting = new TPrecedentsForWriting;
		fPrecedents = gPrecedentsForWriting;
		gPrecedentsForWritingUsed = true;
	}
	else
	{
		fPrecedents = new TPrecedentsForWriting;
		if (fPrecedents == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
	fLargeBinaries = nil;
	fDuplicatedLargeBinary = false;
	fHasLargeBinaries = false;
	fLargeBinaryIsString = false;
}


// ROM 0x002b7b3c __dt__18TStoreObjectWriterFv
TStoreObjectWriter::~TStoreObjectWriter()
{
	if (fHeader != nil && fHeader != fPipe.GetDataPtr(0))
		delete[] fHeader;
	if (fPrecedents == gPrecedentsForWriting)
	{
		gPrecedentsForWritingUsed = false;
		fPrecedents->Reset();
	}
	else if (fPrecedents != nil)
		delete fPrecedents;
}


// the object being scanned pushed while a part of it is scanned
#define DESCEND(part)		{ *fStack.fTop++ = fObject; fObject = (part); Scan(); fObject = *--fStack.fTop; }
#define PRESCEND(part)		{ *fStack.fTop++ = fObject; fObject = (part); Prescan(); fObject = *--fStack.fTop; }


// ROM 0x002b801c Prescan1__18TStoreObjectWriterFv
// The stream's size counted (an upper bound: what the pipe's buffer must
// hold), and the text's; every pointer object entered as a precedent.
void
TStoreObjectWriter::Prescan(void)
{
	if (fStack.fTop + 1 >= fStack.fLimit)
		Throw(exOutOfStack, nil, nil);
	Ref ref = fObject;
	if (!ISPTR(ref))
	{
		if (ref == NILREF)
			fStreamSize += 1;
		else if (ISCHAR(ref))
			fStreamSize += 1 + (RCHAR(ref) < 0x100 ? 1 : 2);
		else
			fStreamSize += 1 + LongSize((long) ref);
		return;
	}
	long precedent = fPrecedents->Find(fObject);
	if (precedent != -1)
	{
		fStreamSize += 1 + LongSize(precedent);
		return;
	}
	fPrecedents->Append(fObject);
	ULong flags = ObjectFlags(ref);
	if ((flags & kObjSlotted) == 0)
	{
		if ((flags & 3) == kObjFrame)		// an indirect binary: a large binary
		{
			fStreamSize += 9;
			return;
		}
		if (IsSymbol(ref))
		{
			fStreamSize += 4;
			return;
		}
		long length = Length(ref);
		fStreamSize += 1 + LongSize(length);
		PRESCEND(ObjClass(OBJ(ref)));
		if (IsInstance(fObject, RSSYMstring))
			fTextSize += length;
		else
			fStreamSize += length;
		return;
	}
	if ((flags & kObjFrame) == 0)
	{
		// an array
		long length = Length(ref);
		fStreamSize += 1 + LongSize(length);
		if (!EQRef(ObjClass(OBJ(ref)), RSSYMarray))
			PRESCEND(ObjClass(OBJ((Ref) fObject)));
		for (long i = 0; i < length; i++)
			PRESCEND(GetArraySlotRef(fObject, i));
		return;
	}
	// a frame
	if (!PackSmallRect(ref, nil))
	{
		fStreamSize += 5;
		long length = Length(ref);
		RefVar map(ObjClass(OBJ(ref)));
		long protoSlot = FindOffset(map, RSSYM_proto);
		long idSlot = -1, modTimeSlot = -1;
		if (fStack.fTop == fStack.fBase)		// the root: an entry's _uniqueID and _modTime go in the header
		{
			idSlot = FindOffset(map, RSSYM_uniqueid);
			modTimeSlot = FindOffset(map, RSSYM_modtime);
		}
		for (long slot = 0; slot < length; slot++)
			if (slot != protoSlot && slot != idSlot && slot != modTimeSlot)
				PRESCEND(GetArraySlotRef(fObject, slot));
	}
	fStreamSize += 5;
}


// ROM 0x002b85f4 Scan1__18TStoreObjectWriterFv
// The object written to the stream (StoreObject.h has the tags).
void
TStoreObjectWriter::Scan(void)
{
	if (fStack.fTop + 1 >= fStack.fLimit)
		Throw(exOutOfStack, nil, nil);
	Ref ref = fObject;
	if (!ISPTR(ref))
	{
		if (ref == NILREF)
			fPipe << (UByte) kSONil;
		else if (ISCHAR(ref))
		{
			UniChar c = RCHAR(ref);
			if (c < 0x100)
				fPipe << (UByte) kSOChar << (UByte) c;
			else
				fPipe << (UByte) kSOUniChar << (UByte) (c >> 8) << (UByte) c;
		}
		else
			fPipe << (UByte) kSOImmediate << (long) ref;
		return;
	}
	long precedent = fPrecedents->Find(fObject);
	if (precedent != -1)
	{
		fPipe << (UByte) kSOPrecedent << precedent;
		return;
	}
	UndirtyObject(ref);
	fPrecedents->Append(fObject);
	ULong flags = ObjectFlags(ref);
	if ((flags & kObjSlotted) != 0)
	{
		if ((flags & kObjFrame) != 0)
		{
			long packed;
			if (PackSmallRect(ref, &packed))
			{
				fPipe << (UByte) kSOSmallRect;
				char bytes[4] = { (char) (packed >> 24), (char) (packed >> 16), (char) (packed >> 8), (char) packed };
				fPipe.Write(bytes, 4);
				return;
			}
			fPipe << (UByte) kSOFrame;
			long count;
			long* indexes;
			long reference = fWrapper->FrameToMapReference(fObject, fStack.fTop == fStack.fBase, &count, &indexes);
			WriteReference(fPipe, reference);
			for (long i = 0; i < count; i++)
				DESCEND(GetArraySlotRef(fObject, indexes[i]));
			delete[] indexes;
			return;
		}
		// an array
		Boolean plain = EQRef(ObjClass(OBJ(ref)), RSSYMarray);
		fPipe << (UByte) (plain ? kSOPlainArray : kSOArray);
		long length = Length(ref);
		fPipe << length;
		if (!plain)
			DESCEND(ObjClass(OBJ((Ref) fObject)));
		for (long i = 0; i < length; i++)
			DESCEND(GetArraySlotRef(fObject, i));
		return;
	}
	if ((flags & 3) == kObjFrame)
	{
		WriteLargeBinary();
		return;
	}
	if (IsSymbol(ref))
	{
		fPipe << (UByte) kSOSymbol;
		WriteReference(fPipe, fWrapper->SymbolToReference(fObject));
		return;
	}
	// a binary: its class, then its data - a string's text to the text object
	Boolean isString = IsInstance(fObject, RSSYMstring);
	fPipe << (UByte) (isString ? kSOString : kSOBinary);
	long length = Length(ref);
	fPipe << length;
	DESCEND(ObjClass(OBJ((Ref) fObject)));
	LockRef(fObject);
	char* data = BinaryData(fObject);
	if (isString)
	{
		// the text as the MessagePad keeps it, UniChars high byte first
		char* text = data;
		if (!HostIsBigEndian())
		{
			text = new char[length];
			memcpy(text, data, length);
			SwapUniChars(text, length / 2);
		}
		fTextPipe.Write(text, length);
		if (text != data)
			delete[] text;
		// NOT YET RECONSTRUCTED: the word hints (unless the class is 'string.nohint)
	}
	else
		fPipe.Write(data, length);
	UnlockRef(fObject);
}


// ROM 0x002b84c0 WriteLargeBinary__18TStoreObjectWriterFv
// NOT YET RECONSTRUCTED: tag 12, the large binary's store object id and
// size, duplicated onto this store when it lives elsewhere.
void
TStoreObjectWriter::WriteLargeBinary(void)
{
	NotYetLargeBinaries();
}


// ROM 0x002b7f8c NextHintChunk__18TStoreObjectWriterFv
// The current hint chunk closed (-1 marks its end) and the next begun.
void
TStoreObjectWriter::NextHintChunk(void)
{
	long* chunk = fHintChunk;
	fHintChunk = chunk + 2;
	ClearHintBits(fHintChunk);
	chunk[1] = -1;
	fHintChunk[0] = -1;
	fHintWords = 0;
}


// ROM 0x002b7c14 Write__18TStoreObjectWriterFv
// The object written: the header (uniqueID, modTime, text object, hints,
// flags, text size), the hint chunks and the stream; the strings' text
// to its own compressed object.  ==> the store object's id.
PSSId
TStoreObjectWriter::Write(void)
{
	if (fStreamSize == 0)
		Prescan();
	fNumHints = 0;								// NOT YET RECONSTRUCTED: gHintsHandlers[gDefaultHintsHandlerId]->GetNumHintChunks(fTextSize >> 1, &fHintTextSize)
	long headerSize = kStoreObjectHeaderSize + fNumHints * kStoreObjectHintChunkSize;
	fStreamSize += headerSize;
	TStore* store = fWrapper->Store();
	Boolean keepTextBlock = false;
	if (fObjectId != (PSSId) -1)
	{
		// rewriting: the old text object goes, or is rewritten when there is text again
		char oldTextBlock[4];
		OSErrIf(store->Read(fObjectId, 8, oldTextBlock, 4));
		fTextBlockId = GetBigEndianWord(oldTextBlock);
		if (fTextBlockId != 0)
		{
			if (fTextSize == 0)
				OSErrIf(store->DeleteObject(fTextBlockId));
			else
				keepTextBlock = true;
		}
	}
	if (!keepTextBlock)
		fTextBlockId = (PSSId) -1;
	fPrecedents->Reset();
	fPipe.Init(fWrapper, fObjectId, fStreamSize, kNoCompression);
	if (fTextSize != 0)
		fTextPipe.Init(fWrapper, fTextBlockId, fTextSize + 4, kUnicodeCompression);
	Boolean ownHeader = false;
	fHeader = fPipe.GetDataPtr(0);
	if (fHeader == nil)
	{
		fHeader = new char[headerSize];
		if (fHeader == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		ownHeader = true;
	}
	fHints = fHintChunk = (long*) (fHeader + kStoreObjectHeaderSize);
	if (fNumHints != 0)
		ClearHintBits(fHintChunk);
	fPipe.SetPosition(headerSize);
	Scan();
	if (fTextSize == 0)
		fTextBlockId = 0;
	else
	{
		fTextPipe.Complete();
		fTextBlockId = fTextPipe.fObjectId;
	}
	while (fHintChunk < fHints + fNumHints * 2 - 2)
		NextHintChunk();
	StoreObjectHeader header;
	Ref root = fRootObject;
	header.fUniqueId = -1;
	header.fModTime = (ULong32) -1;
	if ((ObjectFlags(root) & 3) == 3)
	{
		Ref id = GetFrameSlotRef(root, RSSYM_uniqueid);
		header.fUniqueId = id == NILREF ? -1 : (Long32) RINT(id);
		Ref modTime = GetFrameSlotRef(root, RSSYM_modtime);
		header.fModTime = modTime == NILREF ? (ULong32) -1 : (ULong32) (RINT(modTime) & 0x3fffffff);
	}
	header.fTextBlockId = (StorePSSId) fTextBlockId;
	header.fNumHints = fNumHints;
	header.fTextSizeLo = (UByte) fTextSize;
	header.fTextSizeHi = (UByte) (fTextSize >> 8);
	header.fFlags = 0;
	header.SetHintsHandlerId(gDefaultHintsHandlerId);
	if (fHasLargeBinaries)
	{
		header.fFlags |= kSOFlagsHasLargeBinaries;
		if (fLargeBinaryIsString)
			header.fFlags |= kSOFlagsLargeBinaryIsString;
	}
	header.WriteTo(fHeader);
	// the hint chunks' words big-endian too
	for (long i = 0; i < fNumHints * 2; i++)
		PutBigEndianWord(fHeader + kStoreObjectHeaderSize + i * 4, (ULong32) fHints[i]);
	fPipe.Complete();
	fObjectId = fPipe.fObjectId;
	if (ownHeader)
		OSErrIf(store->Write(fObjectId, 0, fHeader, headerSize));
	return fObjectId;
}


/* -------------------------------------------------------------------------------
	TStoreObjectReader
------------------------------------------------------------------------------- */

// ROM 0x002b8b8c __ct__18TStoreObjectReaderFP13TStoreWrapperUlPP13CDynamicArray
// To read store object id; with largeBinaries, the large binaries met are
// listed there instead of loaded.
TStoreObjectReader::TStoreObjectReader(TStoreWrapper* wrapper, PSSId id, CDynamicArray** largeBinaries)
	: fPipe(wrapper, kNoCompression), fTextPipe(wrapper, kUnicodeCompression)
{
	fEntry = NILREF;
	fWrapper = wrapper;
	fLargeBinaries = largeBinaries;
	if (largeBinaries != nil)
		*largeBinaries = nil;
	fPipe.SetPSSID(id);
	char headerBytes[kStoreObjectHeaderSize];
	OSErrIf(wrapper->Store()->Read(id, 0, headerBytes, kStoreObjectHeaderSize));
	StoreObjectHeader header;
	header.ReadFrom(headerBytes);
	fUniqueId = header.fUniqueId;
	fModTime = (long) (Long32) header.fModTime;
	fPipe.SetPosition(kStoreObjectHeaderSize + header.fNumHints * kStoreObjectHintChunkSize);
	if (header.fTextBlockId != 0)
		fTextPipe.SetPSSID(header.fTextBlockId);
	if (!gPrecedentsForReadingUsed)
	{
		if (gPrecedentsForReading == nil)
			gPrecedentsForReading = new TPrecedentsForReading;
		fPrecedents = gPrecedentsForReading;
		gPrecedentsForReadingUsed = true;
	}
	else
	{
		fPrecedents = new TPrecedentsForReading;
		if (fPrecedents == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
}


// ROM 0x002b8cf8 __dt__18TStoreObjectReaderFv
TStoreObjectReader::~TStoreObjectReader()
{
	if (fPrecedents == gPrecedentsForReading)
	{
		gPrecedentsForReadingUsed = false;
		fPrecedents->Reset();
	}
	else if (fPrecedents != nil)
		delete fPrecedents;
}


// ROM 0x002b8d80 Read__18TStoreObjectReaderFv
// The object; a frame gets the header's _uniqueID and _modTime back.
Ref
TStoreObjectReader::Read(void)
{
	RefVar obj(Scan());
	if ((ObjectFlags(obj) & 3) == 3)
	{
		if (fUniqueId != -1)
			SetFrameSlot(obj, RSSYM_uniqueid, RefVar(MAKEINT(fUniqueId)));
		if (fModTime != -1)
			SetFrameSlot(obj, RSSYM_modtime, RefVar(MAKEINT(fModTime)));
	}
	UndirtyObject(obj);
	return obj;
}


// ROM 0x002b8e4c Scan1__18TStoreObjectReaderFv
// One object from the stream.
Ref
TStoreObjectReader::Scan(void)
{
	UByte tag;
	fPipe >> tag;
	RefVar obj;
	switch (tag)
	{
	case kSOImmediate:
		{
			long value;
			fPipe >> value;
			return (Ref) value;
		}
	case kSOChar:
		{
			UByte c;
			fPipe >> c;
			return MAKECHAR(c);
		}
	case kSOUniChar:
		{
			UByte hi, lo;
			fPipe >> hi >> lo;
			return MAKECHAR((UniChar) ((hi << 8) | lo));
		}
	case kSOBinary:
	case kSOString:
		{
			long length;
			fPipe >> length;
			obj = AllocateBinary(RefVar(NILREF), length);
			fPrecedents->Append(obj);
			RefVar theClass(Scan());
			LockRef(obj);
			ObjClass(OBJ((Ref) obj)) = theClass;
			if (tag == kSOString)
			{
				fTextPipe.Read(BinaryData(obj), length);
				SwapUniChars(BinaryData(obj), length / 2);
			}
			else
				fPipe.Read(BinaryData(obj), length);
			UnlockRef(obj);
			return obj;
		}
	case kSOArray:
	case kSOPlainArray:
		{
			long length;
			fPipe >> length;
			obj = AllocateArray(RefVar(NILREF), length);
			fPrecedents->Append(obj);
			if (tag == kSOPlainArray)
				ObjClass(OBJ((Ref) obj)) = RSSYMarray;
			else
			{
				RefVar theClass(Scan());
				ObjClass(OBJ((Ref) obj)) = theClass;
			}
			for (long i = 0; i < length; i++)
			{
				RefVar element(Scan());
				SetArraySlotRef(obj, i, element);
			}
			UndirtyObject(obj);
			return obj;
		}
	case kSOFrame:
		{
			long reference = ReadReference(fPipe);
			long index = fPrecedents->Append(RefVar(NILREF));
			obj = fWrapper->ReferenceToMap(reference);
			obj = AllocateFrameWithMap(obj);
			fPrecedents->Replace(index, obj);
			if ((Ref) fEntry == NILREF)
				fEntry = obj;
			long length = Length(obj);
			for (long i = 0; i < length; i++)
			{
				RefVar slot(Scan());
				SetArraySlotRef(obj, i, slot);
			}
			UndirtyObject(obj);
			return obj;
		}
	case kSOSymbol:
		{
			long reference = ReadReference(fPipe);
			obj = fWrapper->ReferenceToSymbol(reference);
			fPrecedents->Append(obj);
			return obj;
		}
	case kSOPrecedent:
		{
			long index;
			fPipe >> index;
			return fPrecedents->Get(index);
		}
	case kSONil:
		return NILREF;
	case kSOSmallRect:
		{
			UByte bytes[4];
			fPipe.Read((char*) bytes, 4);
			long packed = ((long) bytes[0] << 24) | ((long) bytes[1] << 16) | ((long) bytes[2] << 8) | bytes[3];
			obj = UnpackSmallRect(packed);
			fPrecedents->Append(obj);
			UndirtyObject(obj);
			return obj;
		}
	case kSOLargeBinary:
		NotYetLargeBinaries();
		return NILREF;
	default:
		Throw(exStoreError, (void*) (Long) kNSErrBadStoreObject, nil);
	}
	return NILREF;
}


// ROM 0x002b937c EachLargeObjectDo__18TStoreObjectReaderFPFP13TStoreWrapperUllPv_UcPv
// The stream skipped through, fn called with each large binary's id and
// size (NOT YET RECONSTRUCTED: one is met).
Boolean
TStoreObjectReader::EachLargeObjectDo(Boolean (*fn)(TStoreWrapper*, PSSId, long, void*), void* refCon)
{
	UByte tag;
	fPipe >> tag;
	long length;
	switch (tag)
	{
	case kSOImmediate:
		{
			UByte b;
			fPipe >> b;
			if (b == 0xff)
				fPipe.Skip(4);
			return false;
		}
	case kSOChar:
		fPipe.SkipUByte();
		return false;
	case kSOUniChar:
		fPipe.Skip(2);
		return false;
	case kSOBinary:
	case kSOString:
		fPipe >> length;
		if (EachLargeObjectDo(fn, refCon))
			return true;
		if (tag != kSOString)
			fPipe.Skip(length);
		return false;
	case kSOArray:
	case kSOPlainArray:
		fPipe >> length;
		if (tag != kSOPlainArray && EachLargeObjectDo(fn, refCon))
			return true;
		for (long i = 0; i < length; i++)
			if (EachLargeObjectDo(fn, refCon))
				return true;
		return false;
	case kSOFrame:
		{
			long reference = ReadReference(fPipe);
			RefVar map(fWrapper->ReferenceToMap(reference));
			long count = Length(map) - 1;
			for (long i = 0; i < count; i++)
				if (EachLargeObjectDo(fn, refCon))
					return true;
			return false;
		}
	case kSOSymbol:
		fPipe.Skip(3);
		return false;
	case kSOPrecedent:
		fPipe >> length;
		return false;
	case kSONil:
		return false;
	case kSOSmallRect:
		fPipe.Skip(4);
		return false;
	case kSOLargeBinary:
		{
			UByte bytes[8];
			fPipe.Read((char*) bytes, 8);
			PSSId id = ((ULong) bytes[0] << 24) | ((ULong) bytes[1] << 16) | ((ULong) bytes[2] << 8) | bytes[3];
			long size = (long) (Long32) (((ULong32) bytes[4] << 24) | ((ULong32) bytes[5] << 16) | ((ULong32) bytes[6] << 8) | bytes[7]);
			return fn(fWrapper, id, size, refCon);
		}
	default:
		Throw(exStoreError, (void*) (Long) kNSErrBadStoreObject, nil);
	}
	return false;
}


/* -------------------------------------------------------------------------------
	Permanent objects
------------------------------------------------------------------------------- */

// ROM 0x002b96cc LoadPermObject__FP13TStoreWrapperUlPP13CDynamicArray
// Store object id read back as a frames object.
Ref
LoadPermObject(TStoreWrapper* wrapper, PSSId id, CDynamicArray** largeBinaries)
{
	TStoreObjectReader reader(wrapper, id, largeBinaries);
	return reader.Read();
}


// ROM 0x002b976c CopyObjectReferences__FR14TStoreReadPipeP13TStoreWrapperT2
// One object of the stream (recursively its parts) skipped through, its
// map and symbol references translated in place from the store from to
// the store to (the pipe reads memory: the three reference bytes just
// read are overwritten).  A large binary cannot be copied this way.
void
CopyObjectReferences(TStoreReadPipe& pipe, TStoreWrapper* from, TStoreWrapper* to)
{
	UByte tag;
	pipe >> tag;
	long length;
	switch (tag)
	{
	case kSOImmediate:
	case kSOPrecedent:
		{
			UByte b;
			pipe >> b;
			if (b == 0xff)
				pipe.Skip(4);
		}
		break;
	case kSOChar:
		pipe.SkipUByte();
		break;
	case kSOUniChar:
		pipe.Skip(2);
		break;
	case kSOBinary:
		pipe >> length;
		CopyObjectReferences(pipe, from, to);
		pipe.Skip(length);
		break;
	case kSOArray:
	case kSOPlainArray:
		pipe >> length;
		if (tag != kSOPlainArray)
			CopyObjectReferences(pipe, from, to);
		for (long i = 0; i < length; i++)
			CopyObjectReferences(pipe, from, to);
		break;
	case kSOFrame:
		{
			long reference = ReadReference(pipe);
			long count;
			reference = to->CopyMap(reference, from, &count);
			WriteReference(pipe.fData + (pipe.fObjectOffset - (pipe.fBufferEnd - pipe.fBufferPos)) - 3, reference);
			for (long i = 0; i < count; i++)
				CopyObjectReferences(pipe, from, to);
		}
		break;
	case kSOSymbol:
		{
			long reference = ReadReference(pipe);
			reference = to->CopySymbol(reference, from);
			WriteReference(pipe.fData + (pipe.fObjectOffset - (pipe.fBufferEnd - pipe.fBufferPos)) - 3, reference);
		}
		break;
	case kSOString:
		{
			UByte b;
			pipe >> b;
			if (b == 0xff)
				pipe.Skip(4);
			CopyObjectReferences(pipe, from, to);		// the class; the text is in the text object
		}
		break;
	case kSONil:
		break;
	case kSOSmallRect:
		pipe.Skip(4);
		break;
	default:
		Throw(exStoreError, (void*) (Long) kNSErrBadStoreObject, nil);
	}
}


// ROM 0x002b99ec CopyPermObject__FUlP13TStoreWrapperT2
// Store object id of the store from copied to the store to as it lies
// (the text object copied, its id patched into the header; the map and
// symbol references translated); one with large binaries is read and
// written as a frames object.  ==> the new object's id.
PSSId
CopyPermObject(PSSId id, TStoreWrapper* from, TStoreWrapper* to)
{
	char headerBytes[kStoreObjectHeaderSize];
	OSErrIf(from->Store()->Read(id, 0, headerBytes, kStoreObjectHeaderSize));
	StoreObjectHeader header;
	header.ReadFrom(headerBytes);
	if (header.fFlags & kSOFlagsHasLargeBinaries)
	{
		RefVar obj(LoadPermObject(from, id, nil));
		PSSId newId = (PSSId) -1;
		StorePermObject(obj, to, newId, nil, nil);
		return newId;
	}
	PSSId newId = (PSSId) -1;
	TCachedReadStore cache;
	PSSId newTextId = 0;
	if (header.fTextBlockId != 0)
	{
		cache.Init(from->Store(), header.fTextBlockId, -1);
		void* text;
		OSErrIf(cache.GetDataPtr(0, cache.fSize, &text));
		OSErrIf(to->Store()->NewObject((char*) text, cache.fSize, &newTextId));
	}
	cache.Init(from->Store(), id, -1);
	long size = cache.fSize;
	void* data;
	OSErrIf(cache.GetDataPtr(0, size, &data));
	if (newTextId != 0)
		PutBigEndianWord((char*) data + 8, newTextId);
	TStoreReadPipe pipe((char*) data, size);
	pipe.Skip(header.fNumHints * kStoreObjectHintChunkSize + kStoreObjectHeaderSize);
	CopyObjectReferences(pipe, from, to);
	OSErrIf(to->Store()->NewObject((char*) data, size, &newId));
	return newId;
}


// ROM 0x002b9c10 StorePermObject__FRC6RefVarP13TStoreWrapperRUlP13CDynamicArrayPUc
// obj written to the store as object id (-1: a new one, whose id comes
// back in id); the store must be writable.
void
StorePermObject(RefArg obj, TStoreWrapper* wrapper, PSSId& id, CDynamicArray* /*largeBinaries*/, Boolean* duplicatedLargeBinary)
{
	if (wrapper == nil)
		Throw(exStoreError, (void*) (Long) kNSErrEntryStoreGone, nil);
	Boolean readOnly;
	OSErrIf(wrapper->Store()->IsReadOnly(&readOnly));
	if (readOnly)
		Throw(exStoreError, (void*) (Long) kSError_WriteProtected, nil);
	TStoreObjectWriter writer(obj, wrapper, id);
	id = writer.Write();
	if (duplicatedLargeBinary != nil)
		*duplicatedLargeBinary = writer.fDuplicatedLargeBinary;
	// NOT YET RECONSTRUCTED: FinalizeLargeObjectWrites when the wrapper has an ephemeral tracker
}


// ROM 0x002b9dcc DeletePermObject__FP13TStoreWrapperUl
// The store object and its text object deleted (and, NOT YET, its large
// binaries zapped).
void
DeletePermObject(TStoreWrapper* wrapper, PSSId id)
{
	char headerBytes[kStoreObjectHeaderSize];
	OSErrIf(wrapper->Store()->Read(id, 0, headerBytes, kStoreObjectHeaderSize));
	StoreObjectHeader header;
	header.ReadFrom(headerBytes);
	if (header.fFlags & kSOFlagsHasLargeBinaries)
		NotYetLargeBinaries();
	OSErrIf(wrapper->Store()->DeleteObject(id));
	if (header.fTextBlockId != 0)
		OSErrIf(wrapper->Store()->DeleteObject(header.fTextBlockId));
}
