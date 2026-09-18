/*
	File:		recognition/RecObject.cpp

	Contains:	TRecObject, TArray, TDArray, TArrayIterator and the
				recogniser's handle functions.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RecObject.h"
#include "NewtonTime.h"
#include <string.h>
#include <stdio.h>


/*------------------------------------------------------------------------------
	H a n d l e s
------------------------------------------------------------------------------*/

// ROM 0x0011b870 MakeHandle__Fl
// A handle of the size, named 'rcog.
Handle
MakeHandle(long size)
{
	Handle h = NewHandle(size);
	NameHandle(h, 'rcog');
	return h;
}


// ROM 0x0011b910 ResizeHandle__FPPcl
long
ResizeHandle(Handle h, long size)
{
	return SetHandleSize(h, size);
}


// ROM 0x0011b8f0 DeleteHandle__FPPc
void
DeleteHandle(Handle h)
{
	if (h != nil)
		DisposHandle(h);
}


// ROM 0x0011b900 SizeOfHandle__FPPc
long
SizeOfHandle(Handle h)
{
	return h != nil ? GetHandleSize(h) : 0;
}


// ROM 0x0011b8cc CopyHandle__FPPPc
// *h replaced by a copy of it; ==> 0, or the memory error.
long
CopyHandle(Handle* h)
{
	long size = GetHandleSize(*h);
	Handle copy = NewHandle(size);
	if (copy == nil)
		return MemoryError();
	memcpy(*copy, **h, size);
	*h = copy;
	return 0;
}


// ROM 0x0011b858 NamePtr__FPcUl
// The recogniser tags its blocks for the heap's accounting; the host has no
// pointer names.
void
NamePtr(char* /*ptr*/, ULong /*name*/)
{ }


// ROM 0x0011b8dc GetTicks__Fv
// The recogniser's clock: the Macintosh tick count.
ULong
GetTicks(void)
{
	return Ticks();
}


// ROM 0x0011b930 NameHandle__FPPcUl
// The ROM tags its handles for the memory reports; nothing here.
void
NameHandle(Handle /*h*/, ULong /*name*/)
{ }


// ROM 0x0011b868 MoveBlock__FPcT1l
void
MoveBlock(const void* src, void* dst, long size)
{
	memmove(dst, src, size);
}


// ROM 0x0011b864 MemoryError__Fv
// The recogniser's "no memory" error code (the ROM's is what
// SignalMemoryError leaves; -1 here).
long
MemoryError(void)
{
	return -1;
}


/*------------------------------------------------------------------------------
	T R e c O b j e c t
------------------------------------------------------------------------------*/

// ROM 0x0021c984 __ct__10TRecObjectFv
TRecObject::TRecObject()
{
	fFlags = 0;
}


// ROM 0x0021c9b8 __dt__10TRecObjectFv
TRecObject::~TRecObject()
{ }


// ROM 0x0021ca50 Dispose__10TRecObjectFv
// The object deleted (the ROM: the vtable reset and the block freed).
void
TRecObject::Dispose(void)
{
	delete this;
}


// ROM 0x0021ca48 Dump__10TRecObjectFP4TMsg
void
TRecObject::Dump(TMsg* /*msg*/)
{ }


// ROM 0x0021ca4c SizeInBytes__10TRecObjectFv
long
TRecObject::SizeInBytes(void)
{
	return 0;
}


// ROM 0x0021ca60 CopyInto__10TRecObjectFP10TRecObject
long
TRecObject::CopyInto(TRecObject* other)
{
	other->fFlags = fFlags;
	return 0;
}


// ROM 0x0021c9d0 SetFlags__10TRecObjectFUl
void
TRecObject::SetFlags(ULong flags)
{
	fFlags |= flags;
}


// ROM 0x0021c9e0 UnsetFlags__10TRecObjectFUl
void
TRecObject::UnsetFlags(ULong flags)
{
	fFlags &= ~flags;
}


// ROM 0x0021c9f0 TestFlags__10TRecObjectFUl
Boolean
TRecObject::TestFlags(ULong flags)
{
	return (fFlags & flags) != 0;
}


// ROM 0x0021ca04 DumpObject__10TRecObjectFPc
// The object dumped through a message (TMsg, NOT YET RECONSTRUCTED: the
// title printed, the Dump virtual given no message).
void
TRecObject::DumpObject(char* title)
{
	if (title != nil)
		printf("%s", title);
	Dump(nil);
}


/*------------------------------------------------------------------------------
	T A r r a y
------------------------------------------------------------------------------*/

// ROM 0x00208e98 __ct__6TArrayFv
TArray::TArray()
{
	fElementSize = 0;
	fCount = 0;
	fFree = 0;
	fChunk = 0;
	fUsers = 0;
	fData = nil;
}


// ROM 0x00208ed8 __dt__6TArrayFv
TArray::~TArray()
{ }


// ROM 0x00209260 Make__6TArraySFUlT1
// A new array of count entries of the element size; nil for no memory.
TArray*
TArray::Make(ULong elementSize, ULong count)
{
	TArray* array = new TArray;
	if (array != nil)
	{
		array->fData = nil;
		if (array->IArray(elementSize, count) != 0)
		{
			array->Dispose();
			array = nil;
		}
	}
	return array;
}


// ROM 0x00209484 IArray__6TArrayFUlT1
// The array set up: a chunk of 6, spare slots (6 for an empty array),
// the data handle made (or resized when there is one); ==> 0, or the
// memory error (the count 0 then).
long
TArray::IArray(ULong elementSize, ULong count)
{
	long err = 0;
	fChunk = 6;
	fFlags = 0;
	fFree = count != 0 ? 0 : 6;
	fElementSize = elementSize;
	fCount = count;
	if (fData == nil)
	{
		fData = MakeHandle(elementSize * (fFree + count));
		NameHandle(fData, 'adta');
		if (fData == nil)
			err = MemoryError();
	}
	else
	{
		err = ResizeHandle(fData, elementSize * (fFree + count));
		if (err != 0)
		{
			DeleteHandle(fData);
			fData = nil;
		}
	}
	if (err != 0)
		fCount = 0;
	fUsers = 0;
	return err;
}


// ROM 0x002095ac Dispose__6TArrayFv
// One user fewer; the array goes when none is left.
void
TArray::Dispose(void)
{
	if (Release())
		IDispose();
}


// ROM 0x002095d8 IDispose__6TArrayFv
// The data freed and the object deleted.
void
TArray::IDispose(void)
{
	if (fData != nil)
		DeleteHandle(fData);
	fData = nil;
	delete this;
}


// ROM 0x00209540 Dump__6TArrayFP4TMsg
void
TArray::Dump(TMsg* /*msg*/)
{
	printf("\r\tes: %ld  cnt: %ld  free: %ld\n", fElementSize, fCount, fFree);
}


// ROM 0x00208f18 SizeInBytes__6TArrayFv
long
TArray::SizeInBytes(void)
{
	return TRecObject::SizeInBytes() + (fData != nil ? SizeOfHandle(fData) : 0);
}


// ROM 0x00208f54 CopyInto__6TArrayFP10TRecObject
// The other array given a copy of the data and the sizes (no users);
// ==> 0, or an error (1 for no data or no other).
long
TArray::CopyInto(TRecObject* other)
{
	if (other == nil)
		return 1;
	long err = TRecObject::CopyInto(other);
	if (err != 0)
		return err;
	TArray* copy = (TArray*) other;
	Handle data = fData;
	err = data == nil ? 1 : CopyHandle(&data);
	copy->fData = data;
	copy->fElementSize = fElementSize;
	copy->fCount = err == 0 ? fCount : 0;
	copy->fFree = fFree;
	copy->fChunk = fChunk;
	copy->fUsers = 0;
	return err;
}


// ROM 0x00208fe4 Reuse__6TArrayFUl
// The array emptied and sized for count entries (plus a chunk).
void
TArray::Reuse(ULong count)
{
	fFree = 0;
	long err = fData == nil ? 1 : ResizeHandle(fData, (fChunk + count) * fElementSize);
	fCount = err != 0 ? 0 : count;
}


// ROM 0x00209038 Compact__6TArrayFv
// The spare slots given back.
void
TArray::Compact(void)
{
	if (fFree != 0)
	{
		fFree = 0;
		if (fData != nil)
			SetHandleSize(fData, fElementSize * fCount);
	}
}


// ROM 0x00209068 Load__6TArrayFUlN31
long
TArray::Load(ULong, ULong, ULong, ULong)
{
	return 0;
}


// ROM 0x00209070 LoadFromSoup__6TArrayFRC6RefVarT1Ul
// The array's sizes read from a header binary and its data taken from
// a data binary (the ROM: a "fake" handle over the object's bytes; the
// host copies them).
long
TArray::LoadFromSoup(RefArg headers, RefArg datas, ULong index)
{
	RefVar header(GetArraySlotRef(headers, index));
	const unsigned char* h = (const unsigned char*) BinaryData(header);
	RefVar data(GetArraySlotRef(datas, index));
	long size = Length(data);
	Handle copy = NewHandle(size);
	if (copy != nil)
		memcpy(*copy, BinaryData(data), size);
	fElementSize = (h[0] << 24) | (h[1] << 16) | (h[2] << 8) | h[3];
	fCount = (h[4] << 24) | (h[5] << 16) | (h[6] << 8) | h[7];
	fFree = 0;
	fChunk = (h[8] << 24) | (h[9] << 16) | (h[10] << 8) | h[11];
	DeleteHandle(fData);
	fData = copy;
	return 1;
}


// ROM 0x00209114 Save__6TArrayFUlN31
// Compacted; the ROM writes a 12-byte header and the data as resources
// (SaveResource does nothing in the ROM either).
long
TArray::Save(ULong, ULong, ULong, ULong)
{
	Compact();
	return 1;
}


// ROM 0x002091bc GetEntry__6TArrayFUl
char*
TArray::GetEntry(ULong index)
{
	if (fData != nil && index < (ULong) fCount)
		return *fData + index * fElementSize;
	return nil;
}


// ROM 0x002091f4 GetIterator__6TArrayFP14TArrayIterator
// The iterator set at the first entry (none for an empty array); ==>
// that entry.
static char* IteratorGetNext(TArrayIterator* iter);
static char* IteratorGetCur(TArrayIterator* iter);

char*
TArray::GetIterator(TArrayIterator* iter)
{
	iter->fGetNext = IteratorGetNext;
	iter->fGetCur = IteratorGetCur;
	if (fData == nil || fCount == 0)
	{
		iter->fHandle = nil;
		iter->fBase = nil;
		iter->fEntry = nil;
	}
	else
	{
		iter->fHandle = fData;
		iter->fBase = *fData;
		iter->fEntry = *fData;
	}
	iter->fIndex = 0;
	iter->fElementSize = fElementSize;
	iter->fCount = fCount;
	return iter->fEntry;
}


// ROM 0x002092bc GetNext__FP14TArrayIterator
// The next entry (the entry address moved along with the data when the
// handle moved).
static char*
IteratorGetNext(TArrayIterator* iter)
{
	if (iter->fHandle != nil && iter->fBase != *iter->fHandle)
	{
		iter->fEntry += *iter->fHandle - iter->fBase;
		iter->fBase = *iter->fHandle;
	}
	iter->fIndex++;
	iter->fEntry += iter->fElementSize;
	return iter->fEntry;
}


// ROM 0x00209330 GetCur__FP14TArrayIterator
static char*
IteratorGetCur(TArrayIterator* iter)
{
	if (iter->fHandle != nil && iter->fBase != *iter->fHandle)
	{
		iter->fEntry += *iter->fHandle - iter->fBase;
		iter->fBase = *iter->fHandle;
	}
	return iter->fEntry;
}


// ROM 0x00209304 RemoveCurrent__FP14TArrayIterator
// The current entry is gone (deleted by the caller): the cursor steps back.
void
TArrayIterator::RemoveCurrent(void)
{
	fCount--;
	fIndex--;
	fEntry -= fElementSize;
}


// ROM 0x0020935c Clear__6TArrayFv
void
TArray::Clear(void)
{
	CutToIndex(0);
}


// ROM 0x00209368 CutToIndex__6TArrayFUl
// The entries from the index dropped: they become spare slots.
void
TArray::CutToIndex(ULong index)
{
	long dropped = fCount - index;
	fCount -= dropped;
	fFree += dropped;
}


// ROM 0x00209388 Add__6TArrayFv
// A slot added at the end: a spare one when there is one, else the
// data grown by a chunk (by one when that fails); ==> the new index, -1
// when there is no memory.  The array's flag 1 says it changed.
long
TArray::Add(void)
{
	long count = fCount;
	long chunk = fChunk;
	long free = fFree - 1;
	if (fFree == 0)
	{
		if (ResizeHandle(fData, fElementSize * (count + chunk + 1)) == 0)
			free = chunk;
		else
		{
			free = 0;
			if (ResizeHandle(fData, fElementSize * (count + 1)) != 0)
			{
				fFree = 0;
				return -1;
			}
		}
	}
	fCount = count + 1;
	fFree = free;
	fFlags |= 1;
	return count;
}


// ROM 0x0020941c AddEntry__6TArrayFv
char*
TArray::AddEntry(void)
{
	return GetEntry(Add());
}


// ROM 0x0020944c SetEntry__6TArrayFUlPc
// The entry's bytes copied in from the data; ==> the entry (nil past
// the count).
char*
TArray::SetEntry(ULong index, const char* data)
{
	char* entry = GetEntry(index);
	if (entry == nil)
		return nil;
	memmove(entry, data, fElementSize);
	return entry;
}


// ROM 0x00209608 Clone__6TArrayFv
void
TArray::Clone(void)
{
	fUsers++;
}


// ROM 0x00209618 Release__6TArrayFv
Boolean
TArray::Release(void)
{
	return --fUsers < 0;
}


// ROM 0x00209634 Lock__6TArrayFv
char*
TArray::Lock(void)
{
	if (fData == nil)
		return nil;
	return (char*) HLock(fData);
}


// ROM 0x00209644 Unlock__6TArrayFv
void
TArray::Unlock(void)
{
	if (fData != nil)
		HUnlock(fData);
}


/*------------------------------------------------------------------------------
	T D A r r a y
------------------------------------------------------------------------------*/

// ROM 0x0020c764 __ct__7TDArrayFv
TDArray::TDArray()
{ }


// ROM 0x0020c7a4 Make__7TDArraySFUlT1
TDArray*
TDArray::Make(ULong elementSize, ULong count)
{
	TDArray* array = new TDArray;
	if (array != nil)
	{
		array->fData = nil;
		if (array->IArray(elementSize, count) != 0)
		{
			array->Dispose();
			array = nil;
		}
	}
	return array;
}


// ROM 0x0020c800 IDArray__7TDArrayFUlT1
long
TDArray::IDArray(ULong elementSize, ULong count)
{
	long err = IArray(elementSize, count);
	NameHandle(fData, 'dDta');
	return err;
}


// ROM 0x0020c830 Delete__7TDArrayFUl
void
TDArray::Delete(ULong index)
{
	DeleteEntries(index, 1);
}


// ROM 0x0020c83c DeleteEntries__7TDArrayFUlT1
// count entries from the index taken out (the rest moved down, the
// slots kept as spare); ==> the index, -1 for an index past the end.
ULong
TDArray::DeleteEntries(ULong index, ULong count)
{
	if (count == 0)
		return 0;
	if (index >= (ULong) fCount)
		return (ULong) -1;
	if (index + count >= (ULong) fCount)
		count = fCount - index;
	char* dst = GetEntry(index);
	if (dst != nil)
	{
		char* src = GetEntry(index + count);
		if (src != nil)
			MoveBlock(src, dst, fElementSize * (fCount - (index + count)));
		fCount -= count;
		fFree += count;
	}
	return index;
}


// ROM 0x0020c8f8 Insert__7TDArrayFUl
// A slot opened at the index (at the end for an index past the count);
// ==> its index, -1 for no memory.
ULong
TDArray::Insert(ULong index)
{
	Boolean inside = GetEntry(index) != nil;
	if (Add() == -1)
		return (ULong) -1;
	if (!inside)
		return fCount - 1;
	char* src = GetEntry(index);
	char* dst = GetEntry(index + 1);
	MoveBlock(src, dst, (fCount - (index + 1)) * fElementSize);
	return index;
}


// ROM 0x0020c9a0 InsertEntry__7TDArrayFUlPc
// ==> the index, -1 for no memory
ULong
TDArray::InsertEntry(ULong index, const char* data)
{
	ULong at = Insert(index);
	if (at != (ULong) -1)
		MoveBlock(data, GetEntry(at), fElementSize);
	return at;
}


// ROM 0x0020c9f8 InsertEntries__7TDArrayFUlPcT1
// count entries put in at the index (the data grown by them plus a
// chunk; host: the entries located after the growth, as the handle may
// move); ==> the index, -1 for no memory.
ULong
TDArray::InsertEntries(ULong index, const char* data, ULong count)
{
	long oldCount = fCount;
	if (ResizeHandle(fData, fElementSize * (oldCount + fChunk) + count * fElementSize) != 0)
		return (ULong) -1;
	char* src = *fData + index * fElementSize;
	char* dst = *fData + (index + count) * fElementSize;
	MoveBlock(src, dst, fElementSize * (oldCount - index));
	MoveBlock(data, src, count * fElementSize);
	fCount += count;
	fFlags |= 1;
	return index;
}
