/*
	File:		armcpu/ARMLists.cpp

	Contains:	CDynamicArray, CList, CArrayIterator and CListIterator for
				ARM code (ARMProtocols.h): the ROM's classes over ARM
				memory, in the ROM's layouts, which the ARM code also reads
				inline (CDynamicArray::GetArraySize, ElementPtrAt).  The
				logic is the reconstruction's own (utility/DynamicArray.cpp,
				List.cpp, ArrayIterator.cpp, ListIterator.cpp) - the same
				ROM functions, transcribed again over 32-bit words, since a
				host object has host-sized pointers.

				CDynamicArray (0x18): fSize +0, fElementSize +4, fChunkSize
				+8, fAllocatedSize +0xc, fArrayBlock +0x10, fIterator +0x14.
				CArrayIterator (0x1c): fDynamicArray +0, fCurrentIndex +4,
				fLowBound +8, fHighBound +0xc, fIterateForward +0x10 (a
				byte), fPreviousLink +0x14, fNextLink +0x18.

	Written by:	the reconstruction (DEVIATION: the ROM runs these where they
				lie).  Each glue function cites the ROM function it answers
				for.
*/

#include "ARMWorld.h"
#include "ARMProtocols.h"
#include "NewtonTypes.h"
#include "UCErrors.h"
#include "OSErrors.h"

#include <string.h>

const int32_t	kNoIndex = -1;

// the calling world's words
class Mem
{
public:
				Mem(ARMTrapContext& c) : fC(c) { }
	uint32_t	R(uint32_t a)				{ uint32_t v = 0; fC.Read32(a, &v); return v; }
	void		W(uint32_t a, uint32_t v)	{ fC.Write32(a, v); }
	uint8_t		B(uint32_t a)				{ uint8_t v = 0; fC.Read8(a, &v); return v; }
	void		WB(uint32_t a, uint8_t v)	{ fC.Write8(a, v); }
	void		Move(uint32_t from, uint32_t to, uint32_t n)
				{
					if (n == 0 || from == to)
						return;
					if (to > from && to < from + n)
						for (uint32_t i = n; i-- > 0; )
							WB(to + i, B(from + i));
					else
						for (uint32_t i = 0; i < n; i++)
							WB(to + i, B(from + i));
				}
	ARMTrapContext&	fC;
};

// CDynamicArray's fields
enum { kSize = 0x00, kElementSize = 0x04, kChunkSize = 0x08, kAllocated = 0x0c, kBlock = 0x10, kIterator = 0x14 };
// CArrayIterator's
enum { kArray = 0x00, kCurrent = 0x04, kLow = 0x08, kHigh = 0x0c, kForward = 0x10, kPrevious = 0x14, kNext = 0x18 };

static inline int32_t	S(uint32_t v)	{ return (int32_t) v; }


/*------------------------------------------------------------------------------
	C A r r a y I t e r a t o r
------------------------------------------------------------------------------*/

// ROM 0x000383b4 AppendToList__14CArrayIteratorFP14CArrayIterator
static uint32_t
AppendToList(Mem& m, uint32_t self, uint32_t toList)
{
	if (toList == 0)
		return self;
	m.W(self + kPrevious, toList);
	m.W(self + kNext, m.R(toList + kNext));
	m.W(m.R(toList + kNext) + kPrevious, self);
	m.W(toList + kNext, self);
	return self;
}

// ROM 0x00038640 RemoveFromList__14CArrayIteratorFv
static uint32_t
RemoveFromList(Mem& m, uint32_t self)
{
	uint32_t next = m.R(self + kNext);
	uint32_t previous = m.R(self + kPrevious);
	uint32_t head = next != self ? next : 0;
	m.W(next + kPrevious, previous);
	m.W(previous + kNext, next);
	m.W(self + kNext, self);
	m.W(self + kPrevious, self);
	return head;
}

// ROM 0x000383d8 InitBounds__14CArrayIteratorFlT1Uc
static void
InitBounds(Mem& m, uint32_t self, int32_t low, int32_t high, bool forward)
{
	int32_t last = S(m.R(m.R(self + kArray) + kSize));
	if (last < 1)
		high = kNoIndex;
	else
	{
		last--;
		if (high < 0)
			high = 0;
		if (high >= last)
			high = last;
	}
	m.W(self + kHigh, (uint32_t) high);
	if (high < 0)
		low = kNoIndex;
	else
	{
		if (low < 0)
			low = 0;
		if (low >= high)
			low = high;
	}
	m.W(self + kLow, (uint32_t) low);
	m.WB(self + kForward, forward);
	m.W(self + kCurrent, (uint32_t) (forward ? low : high));
}

// ROM 0x00038850 Init__14CArrayIteratorFP13CDynamicArraylT2Uc
static void
InitIterator(Mem& m, uint32_t self, uint32_t array, int32_t low, int32_t high, bool forward)
{
	m.W(self + kNext, self);
	m.W(self + kPrevious, self);
	m.W(self + kArray, array);
	m.W(array + kIterator, AppendToList(m, self, m.R(array + kIterator)));
	InitBounds(m, self, low, high, forward);
}

// ROM 0x00038498 Reset__14CArrayIteratorFv
static void
Reset(Mem& m, uint32_t self)
{
	m.W(self + kCurrent, m.B(self + kForward) ? m.R(self + kLow) : m.R(self + kHigh));
}

// ROM 0x00038478 More__14CArrayIteratorFv
static bool
More(Mem& m, uint32_t self)
{
	return m.R(self + kArray) != 0 && S(m.R(self + kCurrent)) != kNoIndex;
}

// ROM 0x000384e0 Advance__14CArrayIteratorFv
static void
Advance(Mem& m, uint32_t self)
{
	int32_t current = S(m.R(self + kCurrent));
	if (m.B(self + kForward))
	{
		if (current < S(m.R(self + kHigh)))
		{
			m.W(self + kCurrent, (uint32_t) (current + 1));
			return;
		}
	}
	else if (current > S(m.R(self + kLow)))
	{
		m.W(self + kCurrent, (uint32_t) (current - 1));
		return;
	}
	m.W(self + kCurrent, (uint32_t) kNoIndex);
}

// ROM 0x00038528 RemoveElementsAt__14CArrayIteratorFlT1 (shift -count)
// ROM 0x00038594 InsertElementsBefore__14CArrayIteratorFlT1 (shift +count)
static void
ShiftIterators(Mem& m, uint32_t self, int32_t index, int32_t count, bool insert)
{
	uint32_t i = self;
	for (;;)
	{
		int32_t low = S(m.R(i + kLow)), high = S(m.R(i + kHigh)), current = S(m.R(i + kCurrent));
		bool forward = m.B(i + kForward) != 0;
		int32_t d = insert ? count : -count;
		if (insert ? index <= low : index < low)
			m.W(i + kLow, (uint32_t) (low + d));
		if (index <= high)
			m.W(i + kHigh, (uint32_t) (high + d));
		if (forward ? index <= current : index < current)
			m.W(i + kCurrent, (uint32_t) (current + d));
		uint32_t array = m.R(i + kArray);
		if (array == 0)
			return;
		i = m.R(i + kNext);
		if (i == m.R(m.R(i + kArray) + kIterator))
			return;
	}
}

// ROM 0x000384b0 DeleteArray__14CArrayIteratorFv
static void
DeleteArray(Mem& m, uint32_t self)
{
	uint32_t next = m.R(self + kNext);
	if (next != m.R(m.R(self + kArray) + kIterator))
		DeleteArray(m, next);
	m.W(self + kArray, 0);
}


/*------------------------------------------------------------------------------
	C D y n a m i c A r r a y
------------------------------------------------------------------------------*/

static uint32_t
ElementAt(Mem& m, uint32_t self, int32_t index)
{
	return m.R(self + kBlock) + m.R(self + kElementSize) * (uint32_t) index;
}

// ROM 0x000a19b0 SetArraySize__13CDynamicArrayFl (ReallocPtr: a new block of
// the ARM heap, the old one's bytes copied, the old one given back)
static NewtonErr
SetArraySize(Mem& m, uint32_t self, int32_t size)
{
	uint32_t block = m.R(self + kBlock);
	int32_t allocated = S(m.R(self + kAllocated)), chunk = S(m.R(self + kChunkSize));
	if (size == 0)
	{
		if (block != 0)
		{
			ARMFree(block);
			m.W(self + kBlock, 0);
			m.W(self + kAllocated, 0);
		}
	}
	else if (allocated < size || allocated - size >= chunk)
	{
		int32_t newSize = size;
		if (chunk != 0)
			newSize = (size + chunk) - (size + chunk) % chunk;
		if (allocated != newSize)
		{
			uint32_t bytes = (uint32_t) newSize * m.R(self + kElementSize);
			uint32_t fresh = ARMAlloc(bytes, false);
			if (fresh == 0)
				return kError_No_Memory;
			if (block != 0)
			{
				uint32_t old = (uint32_t) allocated * m.R(self + kElementSize);
				m.Move(block, fresh, old < bytes ? old : bytes);
				ARMFree(block);
			}
			m.W(self + kAllocated, (uint32_t) newSize);
			m.W(self + kBlock, fresh);
		}
	}
	return noErr;
}

// ROM 0x000a1864 InsertElementsBefore__13CDynamicArrayFlPvT1
static NewtonErr
InsertElementsBefore(Mem& m, uint32_t self, int32_t at, uint32_t elements, int32_t count)
{
	NewtonErr err = noErr;
	int32_t size = S(m.R(self + kSize));
	if (at > size)
		at = size;
	if (count > 0 && (err = SetArraySize(m, self, size + count)) == noErr)
	{
		uint32_t slot = ElementAt(m, self, at);
		if (at < size)
			m.Move(slot, ElementAt(m, self, at + count), ElementAt(m, self, size) - slot);
		m.Move(elements, slot, (uint32_t) count * m.R(self + kElementSize));
		m.W(self + kSize, (uint32_t) (size + count));
		if (m.R(self + kIterator) != 0)
			ShiftIterators(m, m.R(self + kIterator), at, count, true);
	}
	return err;
}

// ROM 0x000a178c RemoveElementsAt__13CDynamicArrayFlT1
static NewtonErr
RemoveElementsAt(Mem& m, uint32_t self, int32_t index, int32_t count)
{
	NewtonErr err = noErr;
	int32_t size = S(m.R(self + kSize));
	if (size == 0)
		return noErr;
	if (count > 0)
	{
		uint32_t from = ElementAt(m, self, index + count);
		uint32_t end = ElementAt(m, self, size);
		if (from < end)
			m.Move(from, ElementAt(m, self, index), end - from);
		err = SetArraySize(m, self, size - count);
		if (err == noErr)
		{
			m.W(self + kSize, (uint32_t) (size - count));
			if (m.R(self + kIterator) != 0)
				ShiftIterators(m, m.R(self + kIterator), index, count, false);
		}
	}
	return err;
}

// ROM 0x000a175c SafeElementPtrAt__13CDynamicArrayFl
static uint32_t
SafeElementPtrAt(Mem& m, uint32_t self, int32_t index)
{
	int32_t size = S(m.R(self + kSize));
	if (size == 0 || index < 0 || index >= size)
		return 0;
	return ElementAt(m, self, index);
}


/*------------------------------------------------------------------------------
	T h e   g l u e
------------------------------------------------------------------------------*/

// ROM 0x000a16ac __ct__13CDynamicArrayFlT1
static bool
Glue_CDynamicArray_ct(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t self = c.Arg(0);
	if (self == 0)
		self = ARMAlloc(0x18, true);
	m.W(self + kBlock, 0);
	m.W(self + kAllocated, 0);
	m.W(self + kChunkSize, c.Arg(2));
	m.W(self + kElementSize, c.Arg(1));
	m.W(self + kSize, 0);
	m.W(self + kIterator, 0);
	c.Return(self);
	return true;
}

// ROM 0x00113238 __ct__5CListFv (a CDynamicArray of words, in chunks of 4)
static bool
Glue_CList_ct(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t self = c.Arg(0);
	if (self == 0)
		self = ARMAlloc(0x18, true);
	m.W(self + kBlock, 0);
	m.W(self + kAllocated, 0);
	m.W(self + kChunkSize, 4);
	m.W(self + kElementSize, 4);
	m.W(self + kSize, 0);
	m.W(self + kIterator, 0);
	c.Return(self);
	return true;
}

// ROM 0x000a171c __dt__13CDynamicArrayFv
// ROM 0x0011332c __dt__5CListFv
static bool
Glue_CDynamicArray_dt(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t self = c.Arg(0);
	if (self != 0)
	{
		if (m.R(self + kIterator) != 0)
			DeleteArray(m, m.R(self + kIterator));
		if (m.R(self + kBlock) != 0)
			ARMFree(m.R(self + kBlock));
		if (c.Arg(1) & 1)
			ARMFree(self);
	}
	c.Return(0);
	return true;
}

static bool
Glue_SafeElementPtrAt(void*, ARMTrapContext& c)
{
	Mem m(c);
	c.Return(SafeElementPtrAt(m, c.Arg(0), S(c.Arg(1))));
	return true;
}

static bool
Glue_InsertElementsBefore(void*, ARMTrapContext& c)
{
	Mem m(c);
	c.Return((uint32_t) InsertElementsBefore(m, c.Arg(0), S(c.Arg(1)), c.Arg(2), S(c.Arg(3))));
	return true;
}

static bool
Glue_RemoveElementsAt(void*, ARMTrapContext& c)
{
	Mem m(c);
	c.Return((uint32_t) RemoveElementsAt(m, c.Arg(0), S(c.Arg(1)), S(c.Arg(2))));
	return true;
}

// ROM 0x0011341c At__5CListFl
static bool
Glue_CList_At(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t slot = SafeElementPtrAt(m, c.Arg(0), S(c.Arg(1)));
	c.Return(slot != 0 ? m.R(slot) : 0);
	return true;
}

// ROM 0x001134a8 InsertAt__5CListFlPv
static bool
Glue_CList_InsertAt(void*, ARMTrapContext& c)
{
	Mem m(c);
	// (the item through a word of the heap: InsertElementsBefore copies from memory)
	uint32_t element = ARMAlloc(4, false);
	m.W(element, c.Arg(2));
	NewtonErr err = InsertElementsBefore(m, c.Arg(0), S(c.Arg(1)), element, 1);
	ARMFree(element);
	c.Return((uint32_t) err);
	return true;
}

// ROM 0x00112fd0 GetIdentityIndex__5CListFPv
static int32_t
IdentityIndex(Mem& m, uint32_t list, uint32_t item)
{
	int32_t size = S(m.R(list + kSize));
	for (int32_t i = 0; i < size; i++)
		if (m.R(ElementAt(m, list, i)) == item)
			return i;
	return kNoIndex;
}
static bool
Glue_CList_GetIdentityIndex(void*, ARMTrapContext& c)
{
	Mem m(c);
	c.Return((uint32_t) IdentityIndex(m, c.Arg(0), c.Arg(1)));
	return true;
}

// ROM 0x001134ec Remove__5CListFPv
static bool
Glue_CList_Remove(void*, ARMTrapContext& c)
{
	Mem m(c);
	int32_t index = IdentityIndex(m, c.Arg(0), c.Arg(1));
	c.Return(index == kNoIndex ? (uint32_t) eRangeCheck : (uint32_t) RemoveElementsAt(m, c.Arg(0), index, 1));
	return true;
}

// ROM 0x0003878c __ct__14CArrayIteratorFP13CDynamicArray
// ROM 0x0010ed68 __ct__13CListIteratorFP13CDynamicArray
static bool
Glue_CArrayIterator_ct(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t self = c.Arg(0), array = c.Arg(1);
	if (self == 0)
		self = ARMAlloc(0x1c, true);
	InitIterator(m, self, array, 0, S(m.R(array + kSize)) - 1, true);
	c.Return(self);
	return true;
}

// ROM 0x000387e0 __dt__14CArrayIteratorFv
static bool
Glue_CArrayIterator_dt(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t self = c.Arg(0);
	if (self != 0)
	{
		uint32_t array = m.R(self + kArray);
		if (array != 0)
			m.W(array + kIterator, RemoveFromList(m, self));
		if (c.Arg(1) & 1)
			ARMFree(self);
	}
	c.Return(0);
	return true;
}

// ROM 0x00038614 FirstIndex__14CArrayIteratorFv
static bool
Glue_FirstIndex(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t self = c.Arg(0);
	Reset(m, self);
	c.Return(More(m, self) ? m.R(self + kCurrent) : (uint32_t) kNoIndex);
	return true;
}

// ROM 0x00038674 NextIndex__14CArrayIteratorFv
static bool
Glue_NextIndex(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t self = c.Arg(0);
	Advance(m, self);
	c.Return(More(m, self) ? m.R(self + kCurrent) : (uint32_t) kNoIndex);
	return true;
}

// the current item of a list iterator
static uint32_t
CurrentItem(Mem& m, uint32_t self)
{
	uint32_t array = m.R(self + kArray);
	if (array == 0)
		return 0;
	uint32_t slot = SafeElementPtrAt(m, array, S(m.R(self + kCurrent)));
	return slot != 0 ? m.R(slot) : 0;
}

// ROM 0x0010ee64 FirstItem__13CListIteratorFv
static bool
Glue_FirstItem(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t self = c.Arg(0);
	Reset(m, self);
	c.Return(More(m, self) ? CurrentItem(m, self) : 0);
	return true;
}

// ROM 0x0010ee94 NextItem__13CListIteratorFv
static bool
Glue_NextItem(void*, ARMTrapContext& c)
{
	Mem m(c);
	uint32_t self = c.Arg(0);
	Advance(m, self);
	c.Return(More(m, self) ? CurrentItem(m, self) : 0);
	return true;
}


void
InstallARMLists(void)
{
	ARMRegisterGlue("__ct__13CDynamicArrayFlT1", Glue_CDynamicArray_ct);
	ARMRegisterGlue("__dt__13CDynamicArrayFv", Glue_CDynamicArray_dt);
	ARMRegisterGlue("SafeElementPtrAt__13CDynamicArrayFl", Glue_SafeElementPtrAt);
	ARMRegisterGlue("InsertElementsBefore__13CDynamicArrayFlPvT1", Glue_InsertElementsBefore);
	ARMRegisterGlue("RemoveElementsAt__13CDynamicArrayFlT1", Glue_RemoveElementsAt);
	ARMRegisterGlue("__ct__5CListFv", Glue_CList_ct);
	ARMRegisterGlue("__dt__5CListFv", Glue_CDynamicArray_dt);
	ARMRegisterGlue("At__5CListFl", Glue_CList_At);
	ARMRegisterGlue("InsertAt__5CListFlPv", Glue_CList_InsertAt);
	ARMRegisterGlue("GetIdentityIndex__5CListFPv", Glue_CList_GetIdentityIndex);
	ARMRegisterGlue("Remove__5CListFPv", Glue_CList_Remove);
	ARMRegisterGlue("__ct__14CArrayIteratorFP13CDynamicArray", Glue_CArrayIterator_ct);
	ARMRegisterGlue("__ct__13CListIteratorFP13CDynamicArray", Glue_CArrayIterator_ct);
	ARMRegisterGlue("__dt__14CArrayIteratorFv", Glue_CArrayIterator_dt);
	ARMRegisterGlue("FirstIndex__14CArrayIteratorFv", Glue_FirstIndex);
	ARMRegisterGlue("NextIndex__14CArrayIteratorFv", Glue_NextIndex);
	ARMRegisterGlue("FirstItem__13CListIteratorFv", Glue_FirstItem);
	ARMRegisterGlue("NextItem__13CListIteratorFv", Glue_NextItem);
}
