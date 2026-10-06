/*
	File:		text/TXObjectRange.cpp

	Contains:	Which run of the text points at which attribute object -
				see TXObjectRange.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXObjectRange.h"
#include "OSErrors.h"
#include "host/RomBugs.h"


#pragma mark -
/*--------------------------------------------------------------------
	Making and unmaking.
--------------------------------------------------------------------*/

// ROM 0x0024055c __ct__13TXObjectRangeFi
TXObjectRange::TXObjectRange(int chunk)
	: TXRanges(sizeof(TXObjectRangeEntry), chunk)	// the ROM's element is 8
{
	fLastObject = nil;
	fOwnsObjects = true;
}


// ROM 0x002405b8 __dt__13TXObjectRangeFv
TXObjectRange::~TXObjectRange()
{
	if (fOwnsObjects)
		FreeObjects(0, -1);
}


// ROM 0x00240fac FreeObjects__13TXObjectRangeFlT1
// `to` is the last range to free, not one past it; -1 means the end.
// The array is locked because Free may run a destructor that allocates.
void
TXObjectRange::FreeObjects(long from, long to)
{
	if (to >= 0)
		to = to + 1;
	else
		to = fCount;

	Lock(false);
	TXObjectRangeEntry* entry = (TXObjectRangeEntry*) GetElementPtr(from);
	for (long n = to - from - 1; n >= 0; n--, entry++)
		entry->fObject->Free();
	Unlock();
	fLastObject = nil;
}


// ROM 0x002410fc FreeData__13TXObjectRangeFUc
NewtonErr
TXObjectRange::FreeData(Boolean compact)
{
	FreeObjects(0, -1);
	return TXRanges::FreeData(compact);
}


// ROM 0x002408e8 Remove__13TXObjectRangeFlT1
long
TXObjectRange::Remove(long at, long count)
{
	FreeObjects(at, at + count - 1);
	return TXArray::Remove(at, count);
}


#pragma mark -
/*--------------------------------------------------------------------
	Reading.
--------------------------------------------------------------------*/

// ROM 0x00241258 RangeIndexToObject__13TXObjectRangeCFl
TXAttrObject*
TXObjectRange::RangeIndexToObject(long index) const
{
	return ((const TXObjectRangeEntry*) GetElementPtr(index))->fObject;
}


// ROM 0x0024112c OffsetToObject__13TXObjectRangeF8TXOffset
TXAttrObject*
TXObjectRange::OffsetToObject(TXOffset offset, Boolean atStart)
{
	long index = OffsetToRangeIndex(offset, atStart);
	if (index < 0)
		return nil;
	return ((TXObjectRangeEntry*) GetElementPtr(index))->fObject;
}


// ROM 0x00241160 GetNextObjectRange__13TXObjectRangeCFlPl
TXAttrObject*
TXObjectRange::GetNextObjectRange(TXOffset offset, long* length) const
{
	if (GetLastRangeEnd() <= offset)
	{
		*length = 0;
		return nil;
	}
	long index = OffsetToRangeIndex(offset, false);
	const TXObjectRangeEntry* entry = (const TXObjectRangeEntry*) GetElementPtr(index);
	*length = entry->fEnd - offset;
	return entry->fObject;
}


// ROM 0x002411c4 CountRangeObjects__13TXObjectRangeFlT1
// How many ranges a stretch of text crosses.
long
TXObjectRange::CountRangeObjects(TXOffset at, long length)
{
	if (GetLastRangeEnd() <= at)
		return 0;

	TXOffset end = at + length;
	long index = OffsetToRangeIndex(at, false);
	const TXObjectRangeEntry* entry = (const TXObjectRangeEntry*) GetElementPtr(index);
	const TXObjectRangeEntry* last = (const TXObjectRangeEntry*) GetElementPtr(fCount - 1);
	long count = 1;
	while (entry < last)
	{
		if (entry->fEnd >= end)
			break;
		entry++;
		count++;
	}
	return count;
}


// ROM 0x00241270 SearchObject__13TXObjectRangeFPC12TXAttrObject
// The first object already in the ranges that this one is equal to -
// which is what stops a document ending up with a hundred styles all
// saying the same thing.  Asked of the *incoming* object, so the
// comparison is its class's.
TXAttrObject*
TXObjectRange::SearchObject(const TXAttrObject* object)
{
	Lock(false);
	TXObjectRangeEntry* entry = (TXObjectRangeEntry*) GetElementPtr(0);
	for (long n = fCount - 1; n >= 0; n--, entry++)
	{
		if (object->IsEqual(entry->fObject))
		{
			Unlock();
			return entry->fObject;
		}
	}
	Unlock();
	return nil;
}


// ROM 0x002412f0 MapObject__13TXObjectRangeFP12TXAttrObjectUcPUc
// What to actually point a range at.  `reference` says the caller is
// keeping its own reference to `object`, so this must not take it over.
TXAttrObject*
TXObjectRange::MapObject(TXAttrObject* object, Boolean reference, Boolean* found)
{
	if ((object->GetObjFlags() & kTXObjIndivisible) == 0)
	{
		// the last one mapped is tried before the whole array is walked,
		// because a run of text is usually all of one style
		TXAttrObject* match = nil;
		if (fLastObject != nil && fLastObject->IsEqual(object))
			match = fLastObject;
		else
			match = SearchObject(object);

		if (match != nil)
		{
			// an equal one was already here; the caller's, if it was
			// handed over, is not wanted
			if (!reference)
				object->Free();
			*found = true;
			fLastObject = match;
			return match;
		}
	}

	*found = false;
	TXAttrObject* result;
	if (reference)
	{
		// the caller is keeping its own, so this needs one of its own
		result = object->CreateNew();
		if (result == nil)
			return nil;
		result->Assign(object);
	}
	else
		result = object;
	fLastObject = result;
	return result;
}


#pragma mark -
/*--------------------------------------------------------------------
	Writing.
--------------------------------------------------------------------*/

// ROM 0x00240614 SetObjectRange__13TXObjectRangeFlT1P12TXAttrObjectUc
TXAttrObject*
TXObjectRange::SetObjectRange(long index, TXOffset end, TXAttrObject* object, Boolean reference)
{
	if (reference)
		object = object->Reference();

	TXObjectRangeEntry* entry = (TXObjectRangeEntry*) GetElementPtr(index);
	entry->fEnd = end;
	TXAttrObject* old = entry->fObject;
	entry->fObject = object;
	old->Free();
	fLastObject = nil;
	return object;
}


// ROM 0x00240688 InsertObjectRange__13TXObjectRangeFlT1P12TXAttrObjectUc
// `index` of -1 puts the new range on the end.
TXAttrObject*
TXObjectRange::InsertObjectRange(long index, TXOffset end, TXAttrObject* object, Boolean reference)
{
	if (reference)
		object = object->Reference();

	TXObjectRangeEntry* entry = (TXObjectRangeEntry*) Insert(nil, 1, index);
	if (entry != nil)
	{
		entry->fObject = object;
		entry->fEnd = end;
		return object;
	}
	// there was no room, so the reference just taken goes back
	if (reference && object != nil)
		object->Free();
	return nil;
}


// ROM 0x00240714 UpdateRangesBounds__13TXObjectRangeFlT1P12TXAttrObjectPlT4
// The ranges either side of a stretch about to be given an object of
// its own are pulled back off it, and `firstIndex`/`lastIndex` come
// back as the ranges the stretch now takes up.  If a neighbour already
// points at an equal object the stretch is simply given to it, and the
// answer is true: the caller has nothing left to do but take the
// covered ranges out.
Boolean
TXObjectRange::UpdateRangesBounds(TXOffset start, TXOffset end, TXAttrObject* object, long* firstIndex, long* lastIndex)
{
	TXSectRanges sect;
	SectRanges(start, end - start, &sect);
	*firstIndex = sect.fFirstIndex;
	*lastIndex = sect.fLastIndex;

	// the range before the stretch - which is the one the stretch
	// starts inside, if it does not start on a boundary
	long first = sect.fFirstIndex;
	if (sect.fStartOffset == 0)
		first--;
	TXAttrObject* before = (first >= 0) ? RangeIndexToObject(first) : nil;

	// and the range after it
	long last;
	TXAttrObject* after;
	if (GetLastRangeEnd() <= end)
	{
		last = -1;
		after = nil;
	}
	else
	{
		last = sect.fLastIndex;
		if (sect.fEndRemainder == 0 && fCount - 1 > last)
			last++;
		after = RangeIndexToObject(last);
	}

	unsigned long indivisible = object->GetObjFlags() & kTXObjIndivisible;

	if (first == *firstIndex)
	{
		// the stretch starts inside a range, so that range is cut short
		// at `start` - unless it already holds the object, in which case
		// it simply keeps going
		if (before != object || indivisible != 0)
		{
			if (last != first)
				SetRangeEnd(sect.fFirstIndex, start);
			else
				// the stretch is wholly inside one range: the part
				// before it becomes a range of its own
				InsertObjectRange(sect.fFirstIndex, start, before, true);
		}
		(*firstIndex)++;
	}
	if (last == *lastIndex)
		(*lastIndex)--;

	if (before == object && indivisible == 0)
	{
		// the range before already holds it, so it grows over the
		// stretch - and over the range after it too, if that holds it
		// as well
		TXOffset newEnd;
		if (after == object)
		{
			*lastIndex = last;
			newEnd = GetRangeEnd(last);
		}
		else
			newEnd = end;
		SetRangeEnd(first, newEnd);
	}
	else if (indivisible != 0)
		return false;

	return before == object || after == object;
}


// ROM 0x00240924 ReplaceRangeObj__13TXObjectRangeFlT1P12TXAttrObjectUc
void
TXObjectRange::ReplaceRangeObj(TXOffset at, long length, TXAttrObject* object, Boolean reference)
{
	if (object == nil)
		return;

	TXOffset end = at + length;
	long firstIndex;
	long lastIndex;
	if (!UpdateRangesBounds(at, end, object, &firstIndex, &lastIndex))
	{
		if (firstIndex <= lastIndex)
		{
			// there is a range to write over
			SetObjectRange(firstIndex, end, object, reference);
			firstIndex++;
		}
		else
			InsertObjectRange(firstIndex, end, object, reference);
	}
	// whatever the stretch covered end to end goes
	long covered = lastIndex - firstIndex + 1;
	if (covered > 0)
		Remove(firstIndex, covered);
}


// ROM 0x002409f4 ClearRange__13TXObjectRangeFlT1
// The ranges follow the text: `length` characters at `at` have gone.
void
TXObjectRange::ClearRange(TXOffset at, long length)
{
	if (length == 0)
		return;
	// as much text as there is, or more: nothing is left to point at
	if (GetLastRangeEnd() <= length)
	{
		FreeData(true);
		return;
	}

	TXSectRanges sect;
	SectRanges(at, length, &sect);

	// which range is to be stretched over the hole: the one the removal
	// started inside, or the one it ended inside, or the one before it
	long keeper;
	if (sect.fStartOffset != 0)
		keeper = sect.fFirstIndex;
	else if (sect.fEndRemainder != 0)
		keeper = sect.fLastIndex;
	else
	{
		keeper = sect.fFirstIndex - 1;
		// an object that stands for itself does not stretch
		if (keeper >= 0 && (RangeIndexToObject(keeper)->GetObjFlags() & kTXObjIndivisible) != 0)
			keeper = -1;
	}

	long index;
	if (keeper >= 0)
	{
		ReplaceRangeObj(at, length, RangeIndexToObject(keeper), true);
		index = OffsetToRangeIndex(at, false);
	}
	else
	{
		// nothing can take it over, so the covered ranges go
		Remove(sect.fWholeIndex, sect.fWholeCount);
		index = sect.fWholeIndex;
	}
	AddToElements(index, -length, -1);
}


// ROM 0x00240b20 ReplaceRange__13TXObjectRangeFlN21P12TXAttrObjectUc
NewtonErr
TXObjectRange::ReplaceRange(TXOffset at, long oldLen, long newLen, TXAttrObject* object, Boolean reference)
{
	TXOffset lastEnd = GetLastRangeEnd();
	long available = lastEnd - at;
	if (oldLen > available)
		oldLen = available;

	if (newLen == 0)
	{
		ClearRange(at, oldLen);
		// the object was handed over and is not wanted after all
		if (object != nil && !reference)
			object->Free();
		return noErr;
	}

	Boolean shared;
	if (object == nil)
	{
		// no object asked for: the new text takes whatever was there
		object = OffsetToObject(at, false);
		shared = true;
	}
	else
		object = MapObject(object, reference, &shared);
	if (object == nil)
		return kError_No_Memory;

	if (oldLen != 0)
	{
		ReplaceRangeObj(at, oldLen, object, shared);
		long index = OffsetToRangeIndex(at, false);
		AddToElements(index, newLen - oldLen, -1);
	}
	else if (lastEnd == 0)
		// nothing here at all: the first range
		InsertObjectRange(-1, newLen, object, shared);
	else
	{
		// text put in without taking any out: everything after it moves
		// up first, and then the new stretch is given its object
		long index = OffsetToRangeIndex(at, false);
		AddToElements(index, newLen, -1);
		ReplaceRangeObj(at, newLen, object, shared);
	}
	return noErr;
}


// ROM 0x00240cf0 ReplaceRange__13TXObjectRangeFlT1P13TXObjectRangeUc
// A whole run of ranges taken from another object range - which is how
// a paste keeps the styles of what was copied.
NewtonErr
TXObjectRange::ReplaceRange(TXOffset at, long oldLen, TXObjectRange* source, Boolean reference)
{
	long count = source->GetCount();
	if (count == 0)
		return ReplaceRange(at, oldLen, 0, nil, true);

	if (reference && oldLen != 0)
	{
		// The whole run goes in at once: the elements are copied
		// straight across and their ends are then moved to where they
		// now stand.  The objects are *moved*, not referenced, so the
		// source gives them up.
		//
		// This arm takes it that the stretch covers whole ranges: it
		// frees the objects of every range from the first to the last
		// but replaces only the `fWholeCount` ranges that are covered
		// end to end, so a stretch that starts or ends inside a range
		// would leave that range pointing at an object it had already
		// given back.  The ROM does not check, and its callers replace
		// whole paragraphs.
		TXSectRanges sect;
		SectRanges(at, oldLen, &sect);
		FreeObjects(sect.fFirstIndex, sect.fLastIndex);
		void* data = source->Lock(false);
		Replace(sect.fFirstIndex, sect.fWholeCount, data, count);
		source->Unlock();
		AddToElements(sect.fFirstIndex, at, count);
		AddToElements(sect.fFirstIndex + count, source->GetLastRangeEnd() - oldLen, -1);
	}
	else
	{
		// one range at a time, each one taking the text of its own
		// length; only the first of them replaces anything
		for (long i = 0; i < count; i++)
		{
			TXAttrObject* object = source->RangeIndexToObject(i);
			long length = source->GetRangeLen(i);
			ReplaceRange(at, oldLen, length, object, false);
			oldLen = 0;
			at += length;
		}
	}
	return noErr;
}


// ROM 0x00240e58 UpdateRangeObjects__13TXObjectRangeFlT1PC12TXAttrValuesT1
// Every object a stretch of text points at changed by the attribute
// list.  An object that stands for itself is changed where it lies; any
// other is *copied* first, because it may be shared with text outside
// the stretch.
unsigned long
TXObjectRange::UpdateRangeObjects(TXOffset at, long length, const TXAttrValues* values, long how)
{
	unsigned long changed = 0;
	while (length > 0)
	{
		long runLength;
		TXAttrObject* object = GetNextObjectRange(at, &runLength);
		if ((object->GetObjFlags() & kTXObjIndivisible) != 0)
			changed |= object->Update(values, how);
		else
		{
			TXAttrObject* copy = object->CreateNew();
			copy->Assign(object);
			changed |= copy->Update(values, how);

			Boolean found;
			TXAttrObject* mapped = MapObject(copy, false, &found);
			long covered = (length < runLength) ? length : runLength;
			ReplaceRangeObj(at, covered, mapped, found);
		}
		at += runLength;
		length -= runLength;
	}
	return changed;
}


#pragma mark -
/*--------------------------------------------------------------------
	TXObjectIterator.
--------------------------------------------------------------------*/

// ROM 0x00240f60 __ct__16TXObjectIteratorFPC13TXObjectRangel
TXObjectIterator::TXObjectIterator(const TXObjectRange* range, TXOffset offset)
{
	fRange = range;
	fCount = range->GetCount();
	SetOffset(offset);
}


// ROM 0x00241024 SetOffset__16TXObjectIteratorFl
void
TXObjectIterator::SetOffset(TXOffset offset)
{
	fOffset = offset;
	long index = fRange->OffsetToRangeIndex(offset, false);
	fIndex = index;
	if (index < 0)
	{
		// past the end: the walk is over before it began
		fIndex = fCount;
		fLength = 0;
		fObject = nil;
		return;
	}
	const TXObjectRangeEntry* entry = (const TXObjectRangeEntry*) fRange->GetElementPtr(index);
	fLength = entry->fEnd - offset;
	fObject = entry->fObject;
}


// ROM 0x0024109c Next__16TXObjectIteratorFv
void
TXObjectIterator::Next(void)
{
	long index = ++fIndex;
	if (index >= fCount)
	{
		fLength = 0;
		fObject = nil;
		return;
	}
	fOffset = fOffset + fLength;
	const TXObjectRangeEntry* entry = (const TXObjectRangeEntry*) fRange->GetElementPtr(index);
	fLength = entry->fEnd - fOffset;
	fObject = entry->fObject;
}


#pragma mark -
/*--------------------------------------------------------------------
	TXRegisteredObjects.
--------------------------------------------------------------------*/

// ROM 0x002359e4 __ct__19TXRegisteredObjectsFv
TXRegisteredObjects::TXRegisteredObjects()
{
	fCount = 0;
}


// ROM 0x00235a20 __dt__19TXRegisteredObjectsFv
TXRegisteredObjects::~TXRegisteredObjects()
{
	for (long i = 0; i < fCount; i++)
		fObjects[i]->Free();
}


// ROM 0x00235a84 Add__19TXRegisteredObjectsFP12TXAttrObject
// ROM BUG (fixed): nothing looks at whether there is room.  There are
// six slots and the ROM registers five things in them, so the seventh
// would write over the object that follows; it never happens because
// the list is a fixed one built at start-up.  The fix refuses an object
// when the pool is full, and - the pool owning what it is given - frees
// it, as it would have done when the pool went.
void
TXRegisteredObjects::Add(TXAttrObject* object)
{
	if (RomBugFixed() && fCount >= kTXRegisteredObjectsMax)
	{
		object->Free();
		return;
	}
	fObjects[fCount++] = object;
}


// ROM 0x00235aa4 GetIndObject__19TXRegisteredObjectsCFi
TXAttrObject*
TXRegisteredObjects::GetIndObject(int index) const
{
	return fObjects[index];
}
