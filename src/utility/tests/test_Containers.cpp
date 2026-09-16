// Host unit test for the utility containers (src/utility): CDynamicArray,
// CArrayIterator, CList, CListIterator, CSortedList, CItemComparer.
// Exercises the reconstructed behaviour: chunked growth, insertion and
// removal with live iterators following the elements, searching, and the
// sorted list's bisection; and the NArray family (NSortedArray, NIterator).

#include "DynamicArray.h"
#include "ArrayIterator.h"
#include "List.h"
#include "ListIterator.h"
#include "ItemComparer.h"
#include "SortedList.h"
#include "NArray.h"
#include "UCErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// a comparer ordering items by the int they point at
class IntComparer : public CItemComparer
{
public:
	CompareResult TestItem(const void* criteria) const override
	{
		int a = *(const int*) fItem, b = *(const int*) criteria;
		return a < b ? kItemLessThanCriteria : a == b ? kItemEqualCriteria : kItemGreaterThanCriteria;
	}
};

static void TestDynamicArray()
{
	CDynamicArray a(sizeof(int), 4);
	EXPECT(a.IsEmpty());
	EXPECT(a.GetArraySize() == 0);
	EXPECT(a.SafeElementPtrAt(0) == nil);

	int v[] = { 10, 20, 30 };
	EXPECT(a.InsertElementsBefore(0, v, 3) == noErr);
	EXPECT(a.GetArraySize() == 3);
	EXPECT(*(int*) a.ElementPtrAt(1) == 20);
	EXPECT(a.SafeElementPtrAt(3) == nil);
	EXPECT(a.SafeElementPtrAt(-1) == nil);

	// insert in the middle, and past the end (appends)
	int w = 15;
	EXPECT(a.InsertElementsBefore(1, &w, 1) == noErr);
	int x = 40;
	EXPECT(a.InsertElementsBefore(100, &x, 1) == noErr);
	EXPECT(a.GetArraySize() == 5);
	int expect[] = { 10, 15, 20, 30, 40 };
	for (int i = 0; i < 5; i++)
		EXPECT(*(int*) a.ElementPtrAt(i) == expect[i]);

	int out[2];
	EXPECT(a.GetElementsAt(2, out, 2) == noErr);
	EXPECT(out[0] == 20 && out[1] == 30);
	int r[] = { 21, 31 };
	EXPECT(a.ReplaceElementsAt(2, r, 2) == noErr);
	EXPECT(*(int*) a.ElementPtrAt(3) == 31);

	EXPECT(a.RemoveElementsAt(1, 2) == noErr);
	EXPECT(a.GetArraySize() == 3);
	EXPECT(*(int*) a.ElementPtrAt(0) == 10 && *(int*) a.ElementPtrAt(1) == 31 && *(int*) a.ElementPtrAt(2) == 40);

	// merging needs equal element sizes
	CDynamicArray b(sizeof(short), 4);
	EXPECT(a.Merge(&b) == eElementSizeMismatch);
	CDynamicArray c(sizeof(int), 4);
	int y[] = { 50, 60 };
	c.InsertElementsBefore(0, y, 2);
	EXPECT(a.Merge(&c) == noErr);
	EXPECT(a.GetArraySize() == 5 && *(int*) a.ElementPtrAt(4) == 60);

	EXPECT(a.RemoveAll() == noErr);
	EXPECT(a.IsEmpty());
	EXPECT(a.SetElementCount(3) == noErr);
	EXPECT(a.GetArraySize() == 3);
}

static void TestIterator()
{
	CDynamicArray a(sizeof(int), 4);
	int v[] = { 1, 2, 3, 4, 5 };
	a.InsertElementsBefore(0, v, 5);

	CArrayIterator forward(&a);
	EXPECT(forward.More());
	EXPECT(forward.FirstIndex() == 0);
	EXPECT(forward.NextIndex() == 1);
	EXPECT(forward.CurrentIndex() == 1);

	CArrayIterator backward(&a, kIterateBackward);
	EXPECT(backward.FirstIndex() == 4);
	EXPECT(backward.NextIndex() == 3);

	CArrayIterator bounded(&a, 1, 3, kIterateForward);
	int count = 0;
	for (bounded.FirstIndex(); bounded.More(); bounded.NextIndex())
		count++;
	EXPECT(count == 3);
	EXPECT(!bounded.More());
	EXPECT(bounded.NextIndex() == 0);		// a step past the end lands on 0, below the low bound (ROM quirk: -1 + 1)

	// removing in front of the live iterators moves them with their elements
	forward.Reset();
	forward.NextIndex();
	forward.NextIndex();		// at index 2 (element 3)
	backward.Reset();			// at index 4 (element 5)
	a.RemoveElementsAt(0, 1);
	EXPECT(forward.CurrentIndex() == 1 && *(int*) a.ElementPtrAt(forward.CurrentIndex()) == 3);
	EXPECT(backward.CurrentIndex() == 3 && *(int*) a.ElementPtrAt(backward.CurrentIndex()) == 5);

	// and inserting
	int z = 0;
	a.InsertElementsBefore(0, &z, 1);
	EXPECT(forward.CurrentIndex() == 2 && *(int*) a.ElementPtrAt(forward.CurrentIndex()) == 3);
	EXPECT(backward.CurrentIndex() == 4);

	// bounds out of range are clipped
	CArrayIterator clipped(&a, -5, 100, kIterateForward);
	EXPECT(clipped.FirstIndex() == 0);
	int n = 0;
	for (clipped.FirstIndex(); clipped.More(); clipped.NextIndex())
		n++;
	EXPECT(n == 5);

	// an iterator on an empty array
	CDynamicArray empty(sizeof(int), 4);
	CArrayIterator none(&empty);
	EXPECT(!none.More());
	EXPECT(none.FirstIndex() == kEmptyIndex);

	// the array's death cuts the iterators loose
	CDynamicArray* dying = new CDynamicArray(sizeof(int), 4);
	dying->InsertElementsBefore(0, v, 2);
	CArrayIterator orphan(dying);
	EXPECT(orphan.More());
	delete dying;
	EXPECT(!orphan.More());
	EXPECT(orphan.CurrentIndex() == kEmptyIndex);
}

static void TestList()
{
	int a = 1, b = 2, c = 3, d = 4;
	CList list;
	EXPECT(list.Empty());
	EXPECT(list.Insert(&a) == noErr);
	EXPECT(list.InsertLast(&c) == noErr);
	EXPECT(list.InsertAt(1, &b) == noErr);
	EXPECT(list.Count() == 3);
	EXPECT(list.At(0) == &a && list.At(1) == &b && list.At(2) == &c);
	EXPECT(list.First() == &a && list.Last() == &c);
	EXPECT(list.At(3) == nil);

	EXPECT(list.GetIdentityIndex(&c) == 2);
	EXPECT(list.GetIdentityIndex(&d) == kEmptyIndex);
	EXPECT(list.Contains(&b));
	EXPECT(!list.InsertUnique(&b));
	EXPECT(list.InsertUnique(&d));
	EXPECT(list.Count() == 4);

	CListIterator iter(&list);
	int sum = 0;
	for (void* item = iter.FirstItem(); iter.More(); item = iter.NextItem())
		sum += *(int*) item;
	EXPECT(sum == 10);
	EXPECT(iter.CurrentItem() == nil);

	EXPECT(list.Remove(&b) == noErr);
	EXPECT(list.Remove(&b) == eRangeCheck);
	EXPECT(list.Count() == 3 && list.At(1) == &c);
	EXPECT(list.Replace(&c, &b) == noErr);
	EXPECT(list.At(1) == &b);
	EXPECT(list.Replace(&c, &a) == eRangeCheck);
	EXPECT(list.ReplaceAt(0, &c) == noErr && list.At(0) == &c);

	// Search with a tester
	CItemComparer same(&d);
	ArrayIndex index;
	EXPECT(list.Search(&same, index) == &d && index == 2);
	CItemComparer other(&c);
	list.Remove(&c);
	EXPECT(list.Search(&other, index) == nil && index == kEmptyIndex);

	CList* made = CList::Make();
	EXPECT(made != nil && made->Empty());
	delete made;
}

static void TestSortedList()
{
	int values[] = { 5, 1, 4, 2, 3, 3 };
	IntComparer comparer;
	CSortedList sorted(&comparer);
	for (int& v : values)
		sorted.Insert(&v);
	EXPECT(sorted.Count() == 6);
	int last = 0;
	for (ArrayIndex i = 0; i < sorted.Count(); i++)
	{
		int v = *(int*) sorted.At(i);
		EXPECT(v >= last);
		last = v;
	}

	int three = 3, seven = 7;
	EXPECT(!sorted.InsertUnique(&three));
	EXPECT(sorted.InsertUnique(&seven));
	EXPECT(*(int*) sorted.Last() == 7);

	ArrayIndex index;
	comparer.SetTestItem(&three);
	void* found = sorted.Search(&comparer, index);
	EXPECT(found != nil && *(int*) found == 3);
	int six = 6;
	comparer.SetTestItem(&six);
	EXPECT(sorted.Search(&comparer, index) == nil);
	EXPECT(index == 6);		// where 6 would go: after 5, before 7
	int zero = 0;
	comparer.SetTestItem(&zero);
	EXPECT(sorted.Search(&comparer, index) == nil && index == 0);

	CSortedList empty(&comparer);
	EXPECT(empty.Search(&comparer, index) == nil && index == 0);
}

// NArray / NSortedArray / NIterator: chunked growth that only shrinks by
// whole chunks, sorted insertion after equals, and the iterator ring.
class LongComparator : public NComparator
{
public:
	int CompareKeys(const void* a, const void* b) const
	{
		long x = *(const long*) a, y = *(const long*) b;
		return x < y ? -1 : (x > y ? 1 : 0);
	}
};

static void TestNArray()
{
	NArray a;
	EXPECT(a.fCount == 0 && a.fArray == nil && a.fShrink);
	EXPECT(a.Init(sizeof(long), 4, 1, true) == noErr);
	EXPECT(a.fPhysicalCount == 4 && a.fArray != nil);			// rounded up to a chunk
	long v[6] = { 10, 20, 30, 40, 50, 60 };
	EXPECT(a.InsertElements(0, 3, v) == noErr && a.fCount == 3);
	EXPECT(a.InsertElements(99, 3, v + 3) == noErr && a.fCount == 6);		// past the end: appended
	EXPECT(a.fPhysicalCount == 8);
	EXPECT(*(long*) a.At(0) == 10 && *(long*) a.At(5) == 60 && a.At(6) == nil && a.At(-1) == nil);
	long key = 40;
	EXPECT(a.Contains(&key) == 3);
	key = 41;
	EXPECT(a.Contains(&key) == -1);
	EXPECT(a.Where(&key) == 6);
	long mid[2] = { 25, 26 };
	EXPECT(a.InsertElements(2, 2, mid) == noErr && *(long*) a.At(2) == 25 && *(long*) a.At(4) == 30 && a.fCount == 8);
	EXPECT(a.RemoveElements(1, 3) == noErr && a.fCount == 5 && *(long*) a.At(1) == 30);
	EXPECT(a.RemoveElements(4, 2) == eRangeCheck);
	EXPECT(a.InsertElements(-1, 1, v) == eRangeCheck && a.InsertElements(0, 0, nil) == noErr);
	EXPECT(a.fPhysicalCount == 8);				// 5 of 8: less than a chunk free, kept
	EXPECT(a.SetCount(1) == noErr && a.fPhysicalCount == 4);	// a whole chunk went
	EXPECT(a.SetCount(0) == noErr && a.fArray == nil && a.fPhysicalCount == 0);

	NArray keep;
	EXPECT(keep.Init(sizeof(long), 4, 8, false) == noErr && keep.fPhysicalCount == 8);
	EXPECT(keep.InsertElements(0, 6, v) == noErr && keep.SetCount(1) == noErr && keep.fPhysicalCount == 8);	// no shrinking

	// sorted: equal keys go after the ones already there
	LongComparator comparator;
	NSortedArray s;
	EXPECT(s.Init(nil, sizeof(long), 4, 4, true) == -1);
	EXPECT(s.Init(&comparator, sizeof(long), 4, 4, true) == noErr);
	long order[7] = { 50, 10, 30, 30, 20, 60, 30 };
	for (long x : order)
		EXPECT(s.InsertElements(s.Where(&x), 1, &x) == noErr);
	long expect[7] = { 10, 20, 30, 30, 30, 50, 60 };
	for (int i = 0; i < 7; i++)
		EXPECT(*(long*) s.At(i) == expect[i]);
	key = 30;
	EXPECT(s.Where(&key) == 5 && s.Contains(&key) == 4);
	key = 40;
	EXPECT(s.Where(&key) == 5 && s.Contains(&key) == -1);
	key = 5;
	EXPECT(s.Where(&key) == 0 && s.Contains(&key) == -1);
	key = 70;
	EXPECT(s.Where(&key) == 7);

	// an iterator ring of two keeps positions in step
	NIterator i1, i2;
	i1.fArray = &s; i1.fCurrent = 3; i1.fLow = 0; i1.fHigh = 6; i1.fReverse = false; i1.fNext = &i2;
	i2.fArray = &s; i2.fCurrent = 3; i2.fLow = 2; i2.fHigh = 6; i2.fReverse = true; i2.fNext = &i1;
	s.fIterators = &i1;
	key = 15;
	EXPECT(s.InsertElements(s.Where(&key), 1, &key) == noErr);		// at 1: before both
	EXPECT(i1.fCurrent == 4 && i1.fHigh == 7 && i1.fLow == 0 && i2.fCurrent == 4 && i2.fLow == 3);
	EXPECT(s.RemoveElements(4, 1) == noErr);							// at the current position
	EXPECT(i1.fCurrent == 4 && i2.fCurrent == 3);					// forward stays, reverse steps back
	EXPECT(i1.fHigh == 6 && i2.fHigh == 6);
	s.fIterators = nil;
}


int main()
{
	InitHostStandaloneHeap();		// the containers live in NewPtr blocks
	TestDynamicArray();
	TestIterator();
	TestList();
	TestSortedList();
	TestNArray();
	if (failures == 0)
		printf("test_Containers: all passed\n");
	return failures != 0;
}
