// The text engine's arrays: the growable array everything in the engine
// is built on, the one sorted by a leading long, and the ranges that
// record which run of characters each style, line and paragraph covers.
#include "TXArray.h"
#include "TXAttributes.h"
#include "TXChars.h"
#include "OSErrors.h"
#include "NewtErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// An element of the sort TXLongTagArray sorts: a long and something else.
struct Tagged
{
	long	fTag;
	long	fWhat;
};


static void
TestArray()
{
	TXArray array(sizeof(long), 4);
	EXPECT(array.GetCount() == 0 && array.GetElementSize() == sizeof(long));

	// three elements put in at the end; the array grows by a whole chunk
	long values[3] = { 10, 20, 30 };
	EXPECT(array.Insert(values, 3, -1) != nil);
	EXPECT(array.GetCount() == 3 && array.fPhysicalCount == 4);
	EXPECT(*(long*) array.GetElementPtr(0) == 10);
	EXPECT(*(long*) array.GetElementPtr(2) == 30);
	EXPECT(*(long*) array.GetLastElementPtr() == 30);

	// one opened in the middle: what follows moves up
	long fifteen = 15;
	EXPECT(array.Insert(&fifteen, 1, 1) != nil);
	EXPECT(array.GetCount() == 4);
	long out[4];
	array.CopyTo(0, 4, out);
	EXPECT(out[0] == 10 && out[1] == 15 && out[2] == 20 && out[3] == 30);

	// a nil `data` opens the room without writing anything
	EXPECT(array.Insert(nil, 2, 0) != nil);
	EXPECT(array.GetCount() == 6);
	EXPECT(*(long*) array.GetElementPtr(2) == 10);

	// and taking them out again closes it
	EXPECT(array.Remove(0, 2) == 4);
	array.CopyTo(0, 4, out);
	EXPECT(out[0] == 10 && out[3] == 30);

	// Replace opens or closes by the difference
	long two[2] = { 11, 12 };
	EXPECT(array.Replace(1, 1, two, 2) == noErr);		// one element becomes two
	EXPECT(array.GetCount() == 5);
	array.CopyTo(0, 5, out);
	EXPECT(out[1] == 11 && out[2] == 12 && out[3] == 20);
	long one = 99;
	EXPECT(array.Replace(1, 2, &one, 1) == noErr);		// and two become one
	EXPECT(array.GetCount() == 4);
	EXPECT(*(long*) array.GetElementPtr(1) == 99);

	// SetCount past what the handle holds grows it by a chunk
	EXPECT(array.SetCount(16) == noErr);
	EXPECT(array.GetCount() == 16 && array.fPhysicalCount >= 16);
	// and shrinking gives everything past one chunk of slack back
	EXPECT(array.SetCount(2) == noErr);
	EXPECT(array.GetCount() == 2 && array.fPhysicalCount == 6);
	EXPECT(array.Compact() == noErr);
	EXPECT(array.fPhysicalCount == 2);

	// Reserve makes room without changing the count
	EXPECT(array.Reserve(10) == noErr);
	EXPECT(array.GetCount() == 2 && array.fPhysicalCount >= 12);

	// the lock nests, and only the first one touches the handle
	void* locked = array.Lock(false);
	EXPECT(locked == *array.fData && array.fLockCount == 1);
	EXPECT(array.Lock(true) == locked && array.fLockCount == 2);
	array.Unlock();
	EXPECT(array.fLockCount == 1);
	array.Unlock();
	EXPECT(array.fLockCount == 0);
	array.Unlock();									// one too many is ignored
	EXPECT(array.fLockCount == 0);
}


static void
TestLongTagArray()
{
	TXLongTagArray array(sizeof(Tagged), 4);
	Tagged elements[5];
	for (long i = 0; i < 5; i++)
	{
		elements[i].fTag = i * 10;					// 0, 10, 20, 30, 40
		elements[i].fWhat = i;
	}
	EXPECT(array.Insert(elements, 5, -1) != nil);

	// an exact hit answers its index, and `found` the tag itself
	long found = -1;
	EXPECT(array.Search(20, &found) == 2 && found == 20);
	// a miss answers the first element past it
	EXPECT(array.Search(25, &found) == 3 && found == 30);
	// at or below the first element: index 0
	EXPECT(array.Search(0, &found) == 0 && found == 0);
	EXPECT(array.Search(-5, &found) == 0 && found == 0);
	// past the last: one past the end
	EXPECT(array.Search(50, &found) == 5 && found == 40);
	// an empty array answers 0 and a `found` of -1
	{
		TXLongTagArray empty(sizeof(Tagged), 4);
		EXPECT(empty.Search(7, &found) == 0 && found == -1);
	}

	// SearchBigger steps past an exact hit, and never past the last
	EXPECT(array.SearchBigger(20) == 3);
	EXPECT(array.SearchBigger(25) == 3);
	EXPECT(array.SearchBigger(40) == 4);
	EXPECT(array.SearchBigger(100) == 4);

	// the tags moved along, the rest of each element left alone
	array.AddToElements(2, 5, -1);					// from index 2 to the end
	EXPECT(((Tagged*) array.GetElementPtr(1))->fTag == 10);
	EXPECT(((Tagged*) array.GetElementPtr(2))->fTag == 25);
	EXPECT(((Tagged*) array.GetElementPtr(4))->fTag == 45);
	EXPECT(((Tagged*) array.GetElementPtr(4))->fWhat == 4);
	array.AddToElements(0, -5, 2);					// and just the first two
	EXPECT(((Tagged*) array.GetElementPtr(0))->fTag == -5);
	EXPECT(((Tagged*) array.GetElementPtr(2))->fTag == 25);
	array.AddToElements(0, 0, -1);					// nothing to add: nothing done
	EXPECT(((Tagged*) array.GetElementPtr(0))->fTag == -5);
}


static void
TestRanges()
{
	// four ranges covering characters 0..29: [0,5) [5,12) [12,12) [12,30)
	// - the empty one at index 2 is there because the engine does keep
	// runs of no length (a style with nothing in it yet)
	TXRanges ranges(sizeof(TXOffset), 4);
	TXOffset ends[4] = { 5, 12, 12, 30 };
	EXPECT(ranges.Insert(ends, 4, -1) != nil);
	EXPECT(ranges.GetLastRangeEnd() == 30);

	EXPECT(ranges.GetRangeStart(0) == 0 && ranges.GetRangeEnd(0) == 5);
	EXPECT(ranges.GetRangeStart(1) == 5 && ranges.GetRangeEnd(1) == 12);
	EXPECT(ranges.GetRangeLen(0) == 5 && ranges.GetRangeLen(1) == 7);
	EXPECT(ranges.GetRangeLen(2) == 0 && ranges.GetRangeLen(3) == 18);
	TXOffsetPair bounds;
	ranges.GetRangeBounds(3, &bounds);
	EXPECT(bounds.fStart == 12 && bounds.fEnd == 30);
	ranges.GetRangeBounds(0, &bounds);
	EXPECT(bounds.fStart == 0 && bounds.fEnd == 5);

	// an offset inside a range, and one exactly on a boundary - which
	// belongs to the range that starts there unless `atStart` asks for
	// the one that ends there
	EXPECT(ranges.OffsetToRangeIndex(3, false) == 0);
	EXPECT(ranges.OffsetToRangeIndex(7, false) == 1);
	EXPECT(ranges.OffsetToRangeIndex(5, false) == 1);
	EXPECT(ranges.OffsetToRangeIndex(5, true) == 0);
	EXPECT(ranges.IsRangeStart(5, 1) && !ranges.IsRangeStart(6, 1));
	EXPECT(ranges.IsRangeStart(5, -1));

	// the ends moved and read back
	ranges.SetRangeEnd(0, 6);
	EXPECT(ranges.GetRangeEnd(0) == 6 && ranges.GetRangeStart(1) == 6);
	ranges.AddToRangeEnd(0, -1);
	EXPECT(ranges.GetRangeEnd(0) == 5);

	// what a stretch of text covers.  3..9 starts two characters into
	// range 0 and ends three into range 1, so neither is covered whole
	TXSectRanges sect;
	long spanned = ranges.SectRanges(3, 6, &sect);
	EXPECT(spanned == 2);
	EXPECT(sect.fFirstIndex == 0 && sect.fStartOffset == 3 && sect.fFirstLen == 2);
	EXPECT(sect.fLastIndex == 1 && sect.fEndRemainder == 3 && sect.fLastLen == 4);
	EXPECT(sect.fWholeCount == 0);

	// 0..12 covers ranges 0 and 1 end to end
	spanned = ranges.SectRanges(0, 12, &sect);
	EXPECT(sect.fFirstIndex == 0 && sect.fStartOffset == 0);
	EXPECT(sect.fWholeIndex == 0 && sect.fWholeCount == 2);
	EXPECT(sect.fEndRemainder == 0);

	// 5..12 is the whole of range 1 alone
	spanned = ranges.SectRanges(5, 7, &sect);
	EXPECT(spanned == 1);
	EXPECT(sect.fFirstIndex == 1 && sect.fStartOffset == 0 && sect.fFirstLen == 7);
	EXPECT(sect.fWholeIndex == 1 && sect.fWholeCount == 1 && sect.fEndRemainder == 0);

	// an empty array answers nothing at all
	{
		TXRanges none(sizeof(TXOffset), 4);
		EXPECT(none.GetLastRangeEnd() == 0);
		EXPECT(none.SectRanges(0, 0, &sect) == 0);
		EXPECT(sect.fFirstIndex == 0 && sect.fWholeCount == 0 && sect.fLastLen == 0);
	}

	// and FreeData drops them all
	EXPECT(ranges.FreeData(true) == noErr);
	EXPECT(ranges.GetCount() == 0 && ranges.fPhysicalCount == 0);
	EXPECT(ranges.GetLastRangeEnd() == 0);
}



// A run's attributes: the list of tag-and-value pairs the engine passes
// about, and the reference-counted object a run points at.
// (text/TXAttributes.h)

// A style of the sort a run would point at: three attributes and
// nothing else.  The ROM's own are TXNewtTextRun and the rulers.
class TestStyle : public TXAttrObject
{
public:
					TestStyle(long size, long face) : fSize(size), fFace(face) { }
	virtual TXAttrObject* CreateNew(void) const	{ return new TestStyle(fSize, fFace); }
	virtual long	GetClassId(void) const		{ return 'test'; }
	virtual void	Assign(const TXAttrObject* other)
						{ fSize = ((const TestStyle*) other)->fSize;
						  fFace = ((const TestStyle*) other)->fFace; }
	virtual Ref		GetNSObject(void) const		{ return NILREF; }
	virtual void	SetNSObject(RefArg)			{ }

	virtual Boolean	GetAttributeValue(TXAttrTag tag, void* value) const
					{
						if (tag == kTXAttrSize)	{ *(long*) value = fSize; return true; }
						if (tag == kTXAttrFace)	{ *(long*) value = fFace; return true; }
						return false;
					}
	virtual void	SetAttributeValue(TXAttrTag tag, const void* value)
					{
						if (tag == kTXAttrSize)	fSize = *(const long*) value;
						else if (tag == kTXAttrFace) fFace = *(const long*) value;
					}
	virtual Boolean	GetCommonAttrValue(TXAttrTag tag, void* value) const
					{
						long mine;
						if (!GetAttributeValue(tag, &mine))
							return false;
						return mine == *(long*) value;
					}
	virtual unsigned long GetAttributeFlags(TXAttrTag tag) const
					{ return tag == kTXAttrSize ? 1 : 2; }
	virtual void	GetAttributesValues(TXAttrValues* values)
					{
						values->Add(kTXAttrSize, &fSize, sizeof(fSize), false);
						values->Add(kTXAttrFace, &fFace, sizeof(fFace), false);
					}

	long			fSize;
	long			fFace;
};


// Something a value can own, so that Remove is seen to free it.
static long gOwnedAlive = 0;

class OwnedThing : public TXVirtualObject
{
public:
					OwnedThing()	{ gOwnedAlive++; }
	virtual			~OwnedThing()	{ gOwnedAlive--; }
};


static void
TestAttrValues()
{
	TXAttrValues values;
	EXPECT(values.GetCount() == 0 && values.GetElementSize() == 0x20);

	long size = 12;
	long face = 1;
	values.Add(kTXAttrSize, &size, sizeof(size), false);
	values.Add(kTXAttrFace, &face, sizeof(face), false);
	EXPECT(values.GetCount() == 2);

	long got = 0;
	EXPECT(values.GetValue(kTXAttrSize, &got) && got == 12);
	EXPECT(values.GetValue(kTXAttrFace, &got) && got == 1);
	EXPECT(!values.GetValue(kTXAttrFont, &got));

	// read out and written back by index
	TXAttrTag tag = 0;
	int length = 0;
	values.GetIndAttrData(0, &tag, &got, &length);
	EXPECT(tag == kTXAttrSize && got == 12 && length == (int) sizeof(long));
	long eighteen = 18;
	values.SetIndAttrData(0, kTXAttrSize, &eighteen, sizeof(eighteen));
	EXPECT(values.GetValue(kTXAttrSize, &got) && got == 18);

	// a value the list owns is freed when the entry goes
	OwnedThing* owned = new OwnedThing;
	EXPECT(gOwnedAlive == 1);
	values.Add(kTXAttrFont, &owned, sizeof(owned), true);
	EXPECT(values.GetCount() == 3 && gOwnedAlive == 1);
	EXPECT(values.Remove(2, 1) == 2 && gOwnedAlive == 0);
	// and one it does not own is left alone
	long plain = 7;
	values.Add(kTXAttrFont, &plain, sizeof(plain), false);
	EXPECT(values.Remove(2, 1) == 2);
}


static void
TestAttrObject()
{
	TestStyle* style = new TestStyle(12, 0);
	EXPECT(style->GetCountReferences() == 1);
	style->Reference();
	EXPECT(style->GetCountReferences() == 2);
	style->Free();
	EXPECT(style->GetCountReferences() == 1);

	// what it has to say about itself
	TXAttrValues mine;
	style->GetAttributesValues(&mine);
	EXPECT(mine.GetCount() == 2);
	long got = 0;
	EXPECT(mine.GetValue(kTXAttrSize, &got) && got == 12);

	// a set of values applied; the flags say what has to be laid out
	// again, or-ed over the ones that were applied
	TXAttrValues change;
	long eighteen = 18;
	long bold = 1;
	change.Add(kTXAttrSize, &eighteen, sizeof(eighteen), false);
	change.Add(kTXAttrFace, &bold, sizeof(bold), false);
	EXPECT(style->Update(&change, 0) == 3);				// 1 | 2
	EXPECT(style->fSize == 18 && style->fFace == 1);

	// the list narrowed to what a second style agrees about: they share
	// the face but not the size, so the size is dropped
	TestStyle* other = new TestStyle(24, 1);
	TXAttrValues common;
	style->GetAttributesValues(&common);
	EXPECT(common.GetCount() == 2);
	other->GetCommonAttrValues(&common);
	EXPECT(common.GetCount() == 1);
	EXPECT(common.GetValue(kTXAttrFace, &got) && got == 1);
	EXPECT(!common.GetValue(kTXAttrSize, &got));

	// two objects of the same class are equal as far as the base can
	// tell - a subclass with values to compare says more
	EXPECT(style->IsEqual(style));
	EXPECT(style->IsEqual(other));
	EXPECT(style->GetClassId() == 'test');

	// and the last reference takes it away
	style->Free();
	other->Free();
	EXPECT(gOwnedAlive == 0);
}



// The character storage (text/TXChars.h).  TXChunkedChars leaves the
// chunks themselves to a subclass; this is the simplest one there could
// be - each chunk a block of its own, big enough to be filled.
class TestChars : public TXChunkedChars
{
public:
					TestChars(int chunkSize) : TXChunkedChars(chunkSize), fCount2(0)
					{
						for (long i = 0; i < kMaxChunks; i++)
							fBlocks[i] = nil;
					}
	virtual			~TestChars()
					{
						for (long i = 0; i < fCount2; i++)
							delete[] fBlocks[i];
					}

	virtual UniChar* GetChunkPtr(long chunk, Boolean, Boolean)	{ return fBlocks[chunk]; }
	virtual NewtonErr AllocateChunks(long at, long count)
					{
						if (fCount2 + count > kMaxChunks)
							return kError_No_Memory;
						for (long i = fCount2 - 1; i >= at; i--)
							fBlocks[i + count] = fBlocks[i];
						for (long i = 0; i < count; i++)
							fBlocks[at + i] = new UniChar[fChunkSize];
						fCount2 += count;
						return noErr;
					}
	virtual void	RemoveChunks(long at, long count)
					{
						for (long i = 0; i < count; i++)
							delete[] fBlocks[at + i];
						for (long i = at + count; i < fCount2; i++)
							fBlocks[i - count] = fBlocks[i];
						for (long i = fCount2 - count; i < fCount2; i++)
							fBlocks[i] = nil;
						fCount2 -= count;
					}

	enum { kMaxChunks = 64 };
	UniChar*		fBlocks[kMaxChunks];
	long			fCount2;			// the chunks that exist
};


// What the storage holds, as a C string, for comparing
static Boolean
CharsAre(TXChars& chars, const char* want)
{
	long count = chars.Count();
	if (count != (long) strlen(want))
		return false;
	for (long i = 0; i < count; i++)
		if (chars.GetChar(i) != (UniChar) (unsigned char) want[i])
			return false;
	return true;
}


// Text put into a descriptor, for handing to Replace
static void
SetText(TXTextDescriptor& desc, UniChar* buffer, const char* text)
{
	long i = 0;
	for (; text[i] != 0; i++)
		buffer[i] = (UniChar) (unsigned char) text[i];
	desc.Set(buffer, i);
}


static void
TestSearchHelpers()
{
	UniChar text[8];
	for (long i = 0; i < 7; i++)
		text[i] = (UniChar) "ab\rcdef"[i];
	EXPECT(SearchChar('c', text, 7) == 3);
	EXPECT(SearchChar('z', text, 7) == -1);
	EXPECT(SearchChar('\r', text, 7) == 2);
	// a form feed is looked for as a return, and a line feed counts too
	text[2] = 0x0a;
	EXPECT(SearchChar(0x0c, text, 7) == 2);
	text[2] = 0x0d;
	EXPECT(SearchChar(0x0c, text, 7) == 2);
	// backwards, counting from the end: the last character is 1 back
	EXPECT(SearchCharBack('f', text + 7, 7) == 1);
	EXPECT(SearchCharBack('a', text + 7, 7) == 7);
	EXPECT(SearchCharBack('z', text + 7, 7) == -1);
	// the first control character, and which it was
	UniChar found = 0;
	EXPECT(GetCtrlCharOffset(text, 7, &found) == 2 && found == 0x0d);
	text[2] = 'x';
	EXPECT(GetCtrlCharOffset(text, 7, &found) == -1);
}


static void
TestChunkedChars()
{
	// chunks of eight characters, so that everything has to cross them
	TestChars chars(8);
	EXPECT(chars.Count() == 0);

	UniChar buffer[64];
	TXTextDescriptor source;

	// the first text ever put in: there are no chunks at all, so it
	// goes through InsertUsingExtraChunks
	SetText(source, buffer, "Hello");
	EXPECT(chars.Replace(0, 0, &source) == noErr);
	EXPECT(chars.Count() == 5 && CharsAre(chars, "Hello"));
	EXPECT(chars.fChunks->GetCount() == 1);

	// more, which fits in the chunk that is there
	SetText(source, buffer, ", world");
	EXPECT(chars.Replace(5, 0, &source) == noErr);
	EXPECT(CharsAre(chars, "Hello, world"));
	EXPECT(chars.fChunks->GetCount() == 2);		// eight and four

	// a long stretch, which takes several new chunks
	SetText(source, buffer, " and everything in it, twice over");
	EXPECT(chars.Replace(chars.Count(), 0, &source) == noErr);
	EXPECT(CharsAre(chars, "Hello, world and everything in it, twice over"));
	EXPECT(chars.Count() == 45);

	// in the middle
	SetText(source, buffer, "small ");
	EXPECT(chars.Replace(7, 0, &source) == noErr);
	EXPECT(CharsAre(chars, "Hello, small world and everything in it, twice over"));

	// a character at a time
	EXPECT(chars.GetChar(0) == 'H' && chars.GetChar(7) == 's');
	EXPECT(chars.GetChar(chars.Count() - 1) == 'r');

	// searching across the chunks
	EXPECT(chars.SearchChar('w', 0, chars.Count()) == 13);
	EXPECT(chars.SearchChar('z', 0, chars.Count()) == -1);
	EXPECT(chars.SearchChar(',', 14, chars.Count() - 14) == 25);
	EXPECT(chars.SearchCharBack('H', chars.Count(), chars.Count()) == chars.Count());
	UniChar found = 0;
	EXPECT(chars.GetCtrlCharOffset(0, chars.Count(), &found) == -1);

	// copied out to a plain buffer
	UniChar out[64];
	TXTextDescriptor into;
	into.Set(out, 5);
	EXPECT(chars.CopyTo(&into, 7, 5) == noErr);
	EXPECT(out[0] == 's' && out[4] == 'l');		// "small"

	// a run to look at, which stops at the chunk it is in
	long chunk = -1;
	long have = 0;
	UniChar* run = chars.AcquireCharChunk(0, &chunk, &have);
	EXPECT(run != nil && chunk == 0 && have == 8 && run[0] == 'H');
	chars.ReleaseCharChunk(chunk);
	// and a line's worth, gathered when it crosses one
	run = chars.GetLineChars(6, 20, &chunk);
	EXPECT(run != nil && chunk == -1 && run[1] == 's');

	// taking text out: the middle of the document
	chars.Remove(7, 6);
	EXPECT(CharsAre(chars, "Hello, world and everything in it, twice over"));
	// across a chunk boundary
	chars.Remove(12, 14);
	EXPECT(CharsAre(chars, "Hello, worldg in it, twice over"));
	// and all of it, which leaves no chunks behind
	chars.Remove(0, chars.Count());
	EXPECT(chars.Count() == 0);

	// and the whole thing again, to show the storage came back clean
	SetText(source, buffer, "one two three four five six seven");
	EXPECT(chars.Replace(0, 0, &source) == noErr);
	EXPECT(CharsAre(chars, "one two three four five six seven"));
	// replacing a range by a longer one
	SetText(source, buffer, "TWENTY-TWO");
	EXPECT(chars.Replace(4, 3, &source) == noErr);
	EXPECT(CharsAre(chars, "one TWENTY-TWO three four five six seven"));
	// and by a shorter one
	SetText(source, buffer, "2");
	EXPECT(chars.Replace(4, 10, &source) == noErr);
	EXPECT(CharsAre(chars, "one 2 three four five six seven"));

	// every chunk that exists holds something, and they add up
	long total = 0;
	for (long i = 0; i < chars.fChunks->GetCount(); i++)
	{
		long len = chars.fChunks->GetRangeLen(i);
		EXPECT(len > 0 && len <= 8);
		total += len;
	}
	EXPECT(total == chars.Count());
	EXPECT(chars.fChunks->GetLastRangeEnd() == chars.Count());
}


int
main()
{
	InitHostStandaloneHeap();
	TestArray();
	TestLongTagArray();
	TestRanges();
	TestAttrValues();
	TestAttrObject();
	TestSearchHelpers();
	TestChunkedChars();
	printf("test_TXArray: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
