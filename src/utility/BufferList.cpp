/*
	File:		utility/BufferList.cpp

	Contains:	CBufferList (BufferList.h).

	Reconstructed from the MP2x00 US ROM (0x00045bbc-0x00046abc); each
	function cites its origin.
*/

#include "BufferList.h"
#include "List.h"
#include "ListIterator.h"
#include "ItemComparer.h"
#include "NewtonMemory.h"
#include "NewtErrors.h"


// ROM 0x00045bbc __ct__11CBufferListFv
CBufferList::CBufferList()
{
	fList = nil;
	fIter = nil;
	fSegment = nil;
	fFirst = -1;
	fCurrent = -1;
	fLast = -1;
	fDeleteSegments = false;
	fOwnsList = false;
}


// ROM 0x00045c24 __dt__11CBufferListFv
// A list of our own goes, and its segments with it if they are ours; a
// caller's list and its segments are left alone.
CBufferList::~CBufferList()
{
	if (fList != nil && fOwnsList)
	{
		if (fDeleteSegments)
		{
			fIter->ResetBounds(true);
			for (CBuffer* item = (CBuffer*) fIter->FirstItem(); fIter->More(); item = (CBuffer*) fIter->NextItem())
				if (item != nil)
					delete item;
		}
		if (fList != nil)
			delete fList;
	}
	if (fIter != nil)
		delete fIter;
}


// ROM 0x00046354 Init__11CBufferListFUc
NewtonErr
CBufferList::Init(Boolean deleteSegments)
{
	fDeleteSegments = deleteSegments;
	fOwnsList = true;
	fList = new CList;
	if (fList != nil)
	{
		fIter = new CListIterator(fList);
		if (fIter != nil)
			return noErr;
	}
	return MemError();
}


// ROM 0x00046548 Init__11CBufferListFP5CListUc
NewtonErr
CBufferList::Init(CList* bufList, Boolean deleteSegments)
{
	NewtonErr err = noErr;
	fOwnsList = false;
	fDeleteSegments = deleteSegments;
	fList = bufList;
	fIter = new CListIterator(bufList);
	if (fIter == nil)
		err = MemError();
	else
		ResetMark();
	return err;
}


// ---------------------------------------------------------------------------
//	The byte calls: the current segment's, moving on to the next segment
//	when it has nothing more.
// ---------------------------------------------------------------------------

// ROM 0x00046860 Peek__11CBufferListFv
int
CBufferList::Peek()
{
	int result = fSegment->Peek();
	if (result == -1 && NextSegment())
		result = fSegment->Peek();
	return result;
}


// ROM 0x000468d8 Next__11CBufferListFv
int
CBufferList::Next()
{
	int result = fSegment->Next();
	if (result == -1 && NextSegment())
		result = fSegment->Next();
	return result;
}


// ROM 0x00046930 Skip__11CBufferListFv
int
CBufferList::Skip()
{
	int result = fSegment->Skip();
	if (result == -1 && NextSegment())
		result = fSegment->Skip();
	return result;
}


// ROM 0x00046988 Get__11CBufferListFv
int
CBufferList::Get()
{
	int result = fSegment->Get();
	if (result == -1 && NextSegment())
		result = fSegment->Get();
	return result;
}


// ROM 0x000469e0 Getn__11CBufferListFPUcl
Size
CBufferList::Getn(UByte* p, Size n)
{
	Size count = fSegment->Getn(p, n);
	while (count < n && NextSegment())
		count += fSegment->Getn(p + count, n - count);
	return count;
}


// ROM 0x00046a58 CopyOut__11CBufferListFPUcRl
int
CBufferList::CopyOut(UByte* p, Size& n)
{
	n -= Getn(p, n);
	if (fCurrent == fLast && fSegment->AtEOF())
		return -1;
	return 0;
}


// ROM 0x00045d0c Put__11CBufferListFi
int
CBufferList::Put(int dataByte)
{
	int result = fSegment->Put(dataByte);
	if (result == -1 && NextSegment())
		result = fSegment->Put(dataByte);
	return result;
}


// ROM 0x00045d74 Putn__11CBufferListFPCUcl
Size
CBufferList::Putn(const UByte* p, Size n)
{
	Size count = fSegment->Putn(p, n);
	while (count < n && NextSegment())
		count += fSegment->Putn(p + count, n - count);
	return count;
}


// ROM 0x00045dec CopyIn__11CBufferListFPCUcRl
int
CBufferList::CopyIn(const UByte* p, Size& n)
{
	n -= Putn(p, n);
	if (fCurrent == fLast && fSegment->AtEOF())
		return -1;
	return 0;
}


// ROM 0x00045e50 Reset__11CBufferListFv
// Every segment reset, the whole list in view again, at its start.
void
CBufferList::Reset()
{
	fIter->ResetBounds(true);
	for (CBuffer* item = (CBuffer*) fIter->FirstItem(); fIter->More(); item = (CBuffer*) fIter->NextItem())
		item->Reset();
	ResetMark();
}


// ROM 0x00046830 ResetMark__11CBufferListFv
// The whole list in view, at the start of the first segment.  (An empty
// list keeps the segment it had.)
void
CBufferList::ResetMark()
{
	fLast = fList->GetArraySize() - 1;
	if (fLast >= 0)
	{
		fFirst = 0;
		fCurrent = 0;
		fSegment = (CBuffer*) fList->At(0);
		fSegment->Seek(0, kSeekFromBeginning);
	}
	else
	{
		fFirst = -1;
		fCurrent = -1;
	}
}


// ROM 0x00046298 GetSize__11CBufferListCFv
// The size of the segments in view.
Size
CBufferList::GetSize() const
{
	if (fList->GetArraySize() == 1)
		return fSegment->GetSize();
	Size size = 0;
	fIter->ResetBounds(true);
	fIter->InitBounds(fFirst, fLast, true);
	for (CBuffer* item = (CBuffer*) fIter->FirstItem(); fIter->More(); item = (CBuffer*) fIter->NextItem())
		size += item->GetSize();
	return size;
}


// ROM 0x000468b8 AtEOF__11CBufferListCFv
Boolean
CBufferList::AtEOF() const
{
	if (fCurrent == fLast)
		return fSegment->AtEOF();
	return false;
}


// ROM 0x00045ec0 Hide__11CBufferListFli
// Take count bytes out of view from the front (dir kSeekFromBeginning) or
// the back (kSeekFromEnd) - a negative count puts them back - segment by
// segment; the segment it stops in becomes the first (last) in view and the
// current one.  What it answers is how much was hidden.
Long
CBufferList::Hide(Long count, int dir)
{
	if (count == 0)
		return 0;
	if (fList->GetArraySize() == 1)
		return fSegment->Hide(count, dir);

	fIter->ResetBounds(true);
	Long remaining = count;
	ArrayIndex lowBound, highBound;
	Boolean forward;
	if (dir == kSeekFromBeginning)
	{
		if (count > 0)
		{
			lowBound = fFirst;
			highBound = fList->GetArraySize() - 1;
			forward = true;
		}
		else
		{
			lowBound = 0;
			highBound = fFirst;
			forward = false;
		}
	}
	else if (dir == kSeekFromEnd)
	{
		if (count > 0)
		{
			lowBound = 0;
			highBound = fLast;
			forward = false;
		}
		else
		{
			lowBound = fLast;
			highBound = fList->GetArraySize() - 1;
			forward = true;
		}
	}
	else
		return count - remaining;

	fIter->InitBounds(lowBound, highBound, forward);
	for (CBuffer* item = (CBuffer*) fIter->FirstItem(); fIter->More(); item = (CBuffer*) fIter->NextItem())
	{
		remaining -= item->Hide(remaining, dir);
		if (remaining == 0)
			break;
	}
	ArrayIndex index = fIter->CurrentIndex();
	fCurrent = index;
	if (dir == kSeekFromBeginning)
		fFirst = index;
	else
		fLast = index;
	SelectSegment(index);
	if (dir == kSeekFromEnd)
		fSegment->Seek(0, kSeekFromEnd);
	return count - remaining;
}


// ROM 0x0004602c Seek__11CBufferListFli
// Move to a position in the segments in view; what it answers is the
// position (nought for a seek of nought to either end).
Size
CBufferList::Seek(Long off, int dir)
{
	if (fList->GetArraySize() == 1)
		return fSegment->Seek(off, dir);

	Size where = 0;
	Long inSegment;
	if (off == 0)
	{
		if (dir == kSeekFromBeginning)
		{
			SelectSegment(fFirst);
			return 0;
		}
		if (dir != kSeekFromEnd)
			return 0;
		SelectSegment(fLast);
		inSegment = 0;
	}
	else
	{
		Size size = GetSize();
		Size position = Position();
		where = off;
		if (dir != kSeekFromBeginning)
		{
			if (dir == kSeekFromHere)
				where = position + off;
			else
			{
				where = position;
				if (dir == kSeekFromEnd)
					where = size - off;
			}
		}
		if (where < 0)
			where = 0;
		if (where >= size)
			where = size;

		// find the segment it falls in, and how far back from its end
		fIter->ResetBounds(true);
		fIter->InitBounds(fFirst, fLast, true);
		inSegment = where;
		for (CBuffer* item = (CBuffer*) fIter->FirstItem(); fIter->More(); item = (CBuffer*) fIter->NextItem())
		{
			inSegment -= item->GetSize();
			if (inSegment < 1)
				break;
		}
		if (inSegment < 0)
			inSegment = -inSegment;
		SelectSegment(fIter->CurrentIndex());
	}
	fSegment->Seek(inSegment, kSeekFromEnd);
	return where;
}


// ROM 0x000461d0 Position__11CBufferListCFv
// The sizes of the segments in view before the current one, and the
// position in it.
Size
CBufferList::Position() const
{
	if (fList->GetArraySize() == 1 || fCurrent == fFirst)
		return fSegment->Position();
	Size position = 0;
	fIter->ResetBounds(true);
	fIter->InitBounds(fFirst, fCurrent - 1, true);
	for (CBuffer* item = (CBuffer*) fIter->FirstItem(); fIter->More(); item = (CBuffer*) fIter->NextItem())
		position += item->GetSize();
	return fSegment->Position() + position;
}


// ROM 0x000467c4 SelectSegment__11CBufferListFl
void
CBufferList::SelectSegment(ArrayIndex index)
{
	fCurrent = index;
	fSegment = (CBuffer*) fList->At(index);
	fSegment->Seek(0, kSeekFromBeginning);
}


// ROM 0x000467f8 NextSegment__11CBufferListFv
Boolean
CBufferList::NextSegment()
{
	ArrayIndex current = fCurrent;
	Boolean more = current < fLast;
	if (more)
	{
		fCurrent = current + 1;
		SelectSegment(current + 1);
	}
	return more;
}


// ---------------------------------------------------------------------------
//	The list calls: the CList's, the view reset after any change.
// ---------------------------------------------------------------------------

// ROM 0x00046340 At__11CBufferListFl
CBuffer*
CBufferList::At(ArrayIndex index)
{
	void** element = (void**) fList->SafeElementPtrAt(index);
	return (element == nil) ? nil : (CBuffer*) *element;
}


// ROM 0x00046348 First__11CBufferListFv
CBuffer*
CBufferList::First()
{
	void** element = (void**) fList->SafeElementPtrAt(0);
	return (element == nil) ? nil : (CBuffer*) *element;
}


// ROM 0x000463ac Last__11CBufferListFv
CBuffer*
CBufferList::Last()
{
	void** element = (void**) fList->SafeElementPtrAt(fList->GetArraySize() - 1);
	return (element == nil) ? nil : (CBuffer*) *element;
}


// ROM 0x000463bc Insert__11CBufferListFP7CBuffer
NewtonErr
CBufferList::Insert(CBuffer* item)
{
	return InsertLast(item);
}


// ROM 0x000463c0 InsertBefore__11CBufferListFlP7CBuffer
NewtonErr
CBufferList::InsertBefore(ArrayIndex index, CBuffer* item)
{
	NewtonErr err = fList->InsertAt(index, item);
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x000463ec InsertAt__11CBufferListFlP7CBuffer
NewtonErr
CBufferList::InsertAt(ArrayIndex index, CBuffer* item)
{
	NewtonErr err = fList->InsertAt(index, item);
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x00046418 InsertFirst__11CBufferListFP7CBuffer
NewtonErr
CBufferList::InsertFirst(CBuffer* item)
{
	NewtonErr err = fList->InsertAt(0, item);
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x0004644c InsertLast__11CBufferListFP7CBuffer
NewtonErr
CBufferList::InsertLast(CBuffer* item)
{
	NewtonErr err = fList->InsertAt(fList->GetArraySize(), item);
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x00046480 Remove__11CBufferListFP7CBuffer
NewtonErr
CBufferList::Remove(CBuffer* item)
{
	NewtonErr err = fList->Remove(item);
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x000464ac RemoveAt__11CBufferListFl
NewtonErr
CBufferList::RemoveAt(ArrayIndex index)
{
	NewtonErr err = fList->RemoveElementsAt(index, 1);
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x000464dc RemoveFirst__11CBufferListFv
NewtonErr
CBufferList::RemoveFirst()
{
	NewtonErr err = fList->RemoveElementsAt(0, 1);
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x00046510 RemoveLast__11CBufferListFv
NewtonErr
CBufferList::RemoveLast()
{
	NewtonErr err = fList->RemoveElementsAt(fList->GetArraySize() - 1, 1);
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x00046598 RemoveAll__11CBufferListFv
NewtonErr
CBufferList::RemoveAll()
{
	NewtonErr err = fList->RemoveElementsAt(0, fList->GetArraySize());
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x000465cc Delete__11CBufferListFP7CBuffer
NewtonErr
CBufferList::Delete(CBuffer* item)
{
	NewtonErr err = fList->Remove(item);
	if (err == noErr)
	{
		if (item != nil)
			delete item;
		ResetMark();
	}
	return err;
}


// ROM 0x00046614 DeleteAt__11CBufferListFl
NewtonErr
CBufferList::DeleteAt(ArrayIndex index)
{
	CBuffer* item = (CBuffer*) fList->At(index);
	NewtonErr err = fList->RemoveElementsAt(index, 1);
	if (err == noErr)
	{
		if (item != nil)
			delete item;
		ResetMark();
	}
	return err;
}


// ROM 0x00046670 DeleteFirst__11CBufferListFv
NewtonErr
CBufferList::DeleteFirst()
{
	CBuffer* item = (CBuffer*) fList->At(0);
	NewtonErr err = fList->RemoveElementsAt(0, 1);
	if (err == noErr)
	{
		if (item != nil)
			delete item;
		ResetMark();
	}
	return err;
}


// ROM 0x000466cc DeleteLast__11CBufferListFv
NewtonErr
CBufferList::DeleteLast()
{
	CBuffer* item = (CBuffer*) fList->At(fList->GetArraySize() - 1);
	NewtonErr err = fList->RemoveElementsAt(fList->GetArraySize() - 1, 1);
	if (err == noErr)
	{
		if (item != nil)
			delete item;
		ResetMark();
	}
	return err;
}


// ROM 0x00046730 DeleteAll__11CBufferListFv
NewtonErr
CBufferList::DeleteAll()
{
	fIter->ResetBounds(true);
	for (CBuffer* item = (CBuffer*) fIter->FirstItem(); fIter->More(); item = (CBuffer*) fIter->NextItem())
		if (item != nil)
			delete item;
	NewtonErr err = fList->RemoveElementsAt(0, fList->GetArraySize());
	if (err == noErr)
		ResetMark();
	return err;
}


// ROM 0x000467bc GetIndex__11CBufferListFP7CBuffer
ArrayIndex
CBufferList::GetIndex(CBuffer* item)
{
	CItemComparer test(item, nil);
	ArrayIndex index;
	fList->Search(&test, index);
	return index;
}
