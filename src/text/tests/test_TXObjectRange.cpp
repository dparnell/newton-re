// Which run of the text points at which attribute object: building the
// ranges, splitting one to give a stretch an object of its own, running
// neighbours together when they say the same thing, taking text out and
// putting it in, a whole run of ranges pasted from another, changing
// every object a stretch points at, the iterator, and the small pool of
// shared objects.
#include "TXObjectRange.h"
#include "OSErrors.h"
#include "memory/host/KernelHeap.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const TXAttrTag	kSize	= 0x73697a65;		// 'size'


// The simplest thing a run of text can point at: one number, and a flag
// saying whether it stands for itself the way an embedded picture does.
class TestStyle : public TXAttrObject
{
public:
			TestStyle(long size = 0, Boolean indivisible = false)
				: fSize(size), fIndivisible(indivisible)	{ sLive++; }
	virtual	~TestStyle()								{ sLive--; }

	virtual TXAttrObject* CreateNew(void) const			{ return new TestStyle(0, fIndivisible); }
	virtual long	GetClassId(void) const				{ return 'test'; }
	virtual unsigned long GetObjFlags(void) const		{ return fIndivisible ? kTXObjIndivisible : 0; }
	virtual void	Assign(const TXAttrObject* other)
			{
				fSize = ((const TestStyle*) other)->fSize;
				fIndivisible = ((const TestStyle*) other)->fIndivisible;
			}
	virtual Boolean	IsEqual(const TXAttrObject* other) const
			{
				if (other == nil || other->GetClassId() != GetClassId())
					return false;
				return fSize == ((const TestStyle*) other)->fSize
					&& fIndivisible == ((const TestStyle*) other)->fIndivisible;
			}
	virtual Boolean	GetAttributeValue(TXAttrTag tag, void* value) const
			{
				if (tag != kSize)
					return false;
				*(long*) value = fSize;
				return true;
			}
	virtual void	SetAttributeValue(TXAttrTag tag, const void* value)
			{
				if (tag == kSize)
					fSize = *(const long*) value;
			}
	virtual unsigned long GetAttributeFlags(TXAttrTag tag) const	{ return tag == kSize ? 8 : 0; }
	virtual Ref		GetNSObject(void) const				{ return NILREF; }
	virtual void	SetNSObject(RefArg)					{ }

	long		fSize;
	Boolean		fIndivisible;
	static long	sLive;
};

long	TestStyle::sLive = 0;


// Three ranges, ten characters each, of three different styles.
static void
BuildThree(TXObjectRange& ranges)
{
	ranges.InsertObjectRange(-1, 10, new TestStyle(10), false);
	ranges.InsertObjectRange(-1, 20, new TestStyle(20), false);
	ranges.InsertObjectRange(-1, 30, new TestStyle(30), false);
}


static long
SizeAt(TXObjectRange& ranges, TXOffset offset)
{
	TXAttrObject* object = ranges.OffsetToObject(offset, false);
	return object == nil ? -1 : ((TestStyle*) object)->fSize;
}


static void
TestReading()
{
	TXObjectRange ranges(4);
	EXPECT(ranges.GetCount() == 0 && ranges.GetLastRangeEnd() == 0);
	BuildThree(ranges);
	EXPECT(ranges.GetCount() == 3 && ranges.GetLastRangeEnd() == 30);

	EXPECT(((TestStyle*) ranges.RangeIndexToObject(1))->fSize == 20);
	EXPECT(SizeAt(ranges, 0) == 10);
	EXPECT(SizeAt(ranges, 9) == 10);
	EXPECT(SizeAt(ranges, 10) == 20);
	EXPECT(SizeAt(ranges, 29) == 30);
	// (OffsetToObject is not asked past the end: TXRanges would answer
	// an index one past the last element.  Its callers guard with
	// GetLastRangeEnd, as GetNextObjectRange does below.)

	long length = -1;
	TXAttrObject* object = ranges.GetNextObjectRange(12, &length);
	EXPECT(object != nil && ((TestStyle*) object)->fSize == 20 && length == 8);
	EXPECT(ranges.GetNextObjectRange(30, &length) == nil && length == 0);

	EXPECT(ranges.CountRangeObjects(0, 5) == 1);
	EXPECT(ranges.CountRangeObjects(5, 20) == 3);
	EXPECT(ranges.CountRangeObjects(30, 5) == 0);

	// an equal object already in the ranges is found
	TestStyle wanted(20);
	EXPECT(ranges.SearchObject(&wanted) == ranges.RangeIndexToObject(1));
	TestStyle missing(99);
	EXPECT(ranges.SearchObject(&missing) == nil);
}


static void
TestSplitAndMerge()
{
	{
		// a stretch inside one range takes an object of its own, and the
		// range is cut in two around it
		TXObjectRange ranges(4);
		BuildThree(ranges);
		TestStyle wanted(99);
		ranges.ReplaceRangeObj(12, 4, &wanted, true);
		EXPECT(ranges.GetCount() == 5);
		EXPECT(SizeAt(ranges, 11) == 20);
		EXPECT(SizeAt(ranges, 12) == 99);
		EXPECT(SizeAt(ranges, 15) == 99);
		EXPECT(SizeAt(ranges, 16) == 20);
		EXPECT(ranges.GetLastRangeEnd() == 30);
	}
	{
		// and giving it back the style it already had runs the three
		// pieces together again - which is what stops a document that
		// has been edited back and forth ending up made of crumbs
		TXObjectRange ranges(4);
		BuildThree(ranges);
		TestStyle wanted(99);
		ranges.ReplaceRangeObj(12, 4, &wanted, true);
		EXPECT(ranges.GetCount() == 5);
		TestStyle back(20);
		// through ReplaceRange, which looks for an equal object first
		EXPECT(ranges.ReplaceRange(12, 4, 4, &back, true) == noErr);
		EXPECT(ranges.GetCount() == 3);
		EXPECT(SizeAt(ranges, 12) == 20 && ranges.GetLastRangeEnd() == 30);
	}
	{
		// a whole range given another object keeps the count
		TXObjectRange ranges(4);
		BuildThree(ranges);
		TestStyle wanted(99);
		ranges.ReplaceRangeObj(10, 10, &wanted, true);
		EXPECT(ranges.GetCount() == 3);
		EXPECT(SizeAt(ranges, 9) == 10 && SizeAt(ranges, 10) == 99 && SizeAt(ranges, 20) == 30);
	}
	{
		// an object that stands for itself is never run together with a
		// neighbour, however equal the two are
		TXObjectRange ranges(4);
		ranges.InsertObjectRange(-1, 10, new TestStyle(7, true), false);
		TestStyle same(7, true);
		ranges.ReplaceRange(10, 0, 10, &same, true);
		EXPECT(ranges.GetCount() == 2);
		EXPECT(SizeAt(ranges, 5) == 7 && SizeAt(ranges, 15) == 7);
		EXPECT(ranges.RangeIndexToObject(0) != ranges.RangeIndexToObject(1));
	}
}


static void
TestTextComingAndGoing()
{
	{
		// text put in takes the style it is put into
		TXObjectRange ranges(4);
		BuildThree(ranges);
		EXPECT(ranges.ReplaceRange(15, 0, 6, nil, true) == noErr);
		EXPECT(ranges.GetLastRangeEnd() == 36);
		EXPECT(ranges.GetCount() == 3);
		EXPECT(SizeAt(ranges, 15) == 20 && SizeAt(ranges, 20) == 20 && SizeAt(ranges, 26) == 30);
	}
	{
		// text taken out of the middle of a range shortens it
		TXObjectRange ranges(4);
		BuildThree(ranges);
		ranges.ClearRange(12, 4);
		EXPECT(ranges.GetLastRangeEnd() == 26);
		EXPECT(ranges.GetCount() == 3);
		EXPECT(SizeAt(ranges, 11) == 20 && SizeAt(ranges, 15) == 20 && SizeAt(ranges, 16) == 30);
	}
	{
		// a whole range taken out goes
		TXObjectRange ranges(4);
		BuildThree(ranges);
		ranges.ClearRange(10, 10);
		EXPECT(ranges.GetLastRangeEnd() == 20);
		EXPECT(ranges.GetCount() == 2);
		EXPECT(SizeAt(ranges, 9) == 10 && SizeAt(ranges, 10) == 30);
	}
	{
		// and clearing as much as there is drops the lot
		TXObjectRange ranges(4);
		BuildThree(ranges);
		ranges.ClearRange(0, 30);
		EXPECT(ranges.GetCount() == 0 && ranges.GetLastRangeEnd() == 0);
	}
	{
		// the first range of an empty one
		TXObjectRange ranges(4);
		TestStyle first(12);
		EXPECT(ranges.ReplaceRange(0, 0, 8, &first, true) == noErr);
		EXPECT(ranges.GetCount() == 1 && ranges.GetLastRangeEnd() == 8);
		EXPECT(SizeAt(ranges, 0) == 12);
	}
	{
		// text replaced by more text, with a style of its own
		TXObjectRange ranges(4);
		BuildThree(ranges);
		TestStyle wanted(99);
		EXPECT(ranges.ReplaceRange(12, 4, 10, &wanted, true) == noErr);
		EXPECT(ranges.GetLastRangeEnd() == 36);
		EXPECT(SizeAt(ranges, 11) == 20 && SizeAt(ranges, 12) == 99
			&& SizeAt(ranges, 21) == 99 && SizeAt(ranges, 22) == 20);
	}
}


static void
TestPaste()
{
	// a run of ranges taken from another object range - which is how a
	// paste keeps the styles of what was copied.  The stretch replaced
	// has to cover whole ranges (see the .cpp), so a whole range is
	// what is replaced here.
	TXObjectRange source(4);
	source.InsertObjectRange(-1, 3, new TestStyle(101), false);
	source.InsertObjectRange(-1, 7, new TestStyle(102), false);
	// the objects are moved across without a reference being taken, so
	// the source must not give them back
	source.fOwnsObjects = false;

	TXObjectRange ranges(4);
	BuildThree(ranges);
	EXPECT(ranges.ReplaceRange(10, 10, &source, true) == noErr);
	EXPECT(ranges.GetLastRangeEnd() == 27);
	EXPECT(ranges.GetCount() == 4);
	EXPECT(SizeAt(ranges, 9) == 10);
	EXPECT(SizeAt(ranges, 10) == 101);
	EXPECT(SizeAt(ranges, 13) == 102);
	EXPECT(SizeAt(ranges, 17) == 30);

	// an empty source is simply a removal
	TXObjectRange empty(4);
	TXObjectRange other(4);
	BuildThree(other);
	EXPECT(other.ReplaceRange(10, 10, &empty, true) == noErr);
	EXPECT(other.GetLastRangeEnd() == 20 && other.GetCount() == 2);
}


static void
TestUpdate()
{
	TXObjectRange ranges(4);
	BuildThree(ranges);

	TXAttrValues values;
	long size = 42;
	values.Add(kSize, &size, sizeof(size), false);

	// the first two ranges' objects are copied, changed and mapped, and
	// because the two end up equal they are run together
	unsigned long changed = ranges.UpdateRangeObjects(0, 20, &values, 0);
	EXPECT(changed == 8);
	EXPECT(SizeAt(ranges, 0) == 42 && SizeAt(ranges, 19) == 42 && SizeAt(ranges, 20) == 30);
	EXPECT(ranges.GetCount() == 2);

	// an object that stands for itself is changed where it lies
	TXObjectRange own(4);
	TestStyle* picture = new TestStyle(5, true);
	own.InsertObjectRange(-1, 4, picture, false);
	EXPECT(own.UpdateRangeObjects(0, 4, &values, 0) == 8);
	EXPECT(own.RangeIndexToObject(0) == picture && picture->fSize == 42);
}


static void
TestIterator()
{
	TXObjectRange ranges(4);
	BuildThree(ranges);

	TXObjectIterator iter(&ranges, 0);
	EXPECT(iter.fObject != nil && ((TestStyle*) iter.fObject)->fSize == 10 && iter.fLength == 10);
	iter.Next();
	EXPECT(((TestStyle*) iter.fObject)->fSize == 20 && iter.fOffset == 10 && iter.fLength == 10);
	iter.Next();
	EXPECT(((TestStyle*) iter.fObject)->fSize == 30 && iter.fOffset == 20);
	iter.Next();
	EXPECT(iter.fObject == nil && iter.fLength == 0);

	// started in the middle of a range, the first step is the rest of it
	TXObjectIterator middle(&ranges, 15);
	EXPECT(((TestStyle*) middle.fObject)->fSize == 20 && middle.fLength == 5);
	middle.Next();
	EXPECT(((TestStyle*) middle.fObject)->fSize == 30 && middle.fOffset == 20 && middle.fLength == 10);
}


static void
TestRegisteredObjects()
{
	TestStyle::sLive = 0;
	{
		TXRegisteredObjects pool;
		EXPECT(pool.GetCount() == 0);
		pool.Add(new TestStyle(1));
		pool.Add(new TestStyle(2));
		EXPECT(pool.GetCount() == 2);
		EXPECT(((TestStyle*) pool.GetIndObject(1))->fSize == 2);
		EXPECT(TestStyle::sLive == 2);
	}
	// the pool gives its objects back when it goes
	EXPECT(TestStyle::sLive == 0);

	// a full pool (six): the ROM writes a seventh past its end (the ROM
	// bug); fixed, the seventh is refused and given back
	SetRomBugFixed(true);
	{
		TXRegisteredObjects pool;
		for (long i = 0; i < kTXRegisteredObjectsMax + 1; i++)
			pool.Add(new TestStyle(i));
		EXPECT(pool.GetCount() == kTXRegisteredObjectsMax);
		EXPECT(TestStyle::sLive == kTXRegisteredObjectsMax);
	}
	EXPECT(TestStyle::sLive == 0);
}


static void
TestOwnership()
{
	TestStyle::sLive = 0;
	{
		TXObjectRange ranges(4);
		BuildThree(ranges);
		EXPECT(TestStyle::sLive == 3);
	}
	// the ranges own their objects, so all three go with them
	EXPECT(TestStyle::sLive == 0);

	{
		TXObjectRange ranges(4);
		BuildThree(ranges);
		ranges.FreeData(true);
		EXPECT(ranges.GetCount() == 0);
		EXPECT(TestStyle::sLive == 0);
	}
}


int
main()
{
	InitHostStandaloneHeap();
	TestReading();
	TestSplitAndMerge();
	TestTextComingAndGoing();
	TestPaste();
	TestUpdate();
	TestIterator();
	TestRegisteredObjects();
	TestOwnership();
	printf("test_TXObjectRange: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
