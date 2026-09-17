// RecObject test: TArray/TDArray over the standalone heap - entries added,
// set, read, inserted, deleted, iterated, cleared; Clone/Release; a copy.
#include "RecObject.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct Entry { long a; long b; };


int
main()
{
	InitHostStandaloneHeap();
	TArray* array = TArray::Make(sizeof(Entry), 0);
	EXPECT(array != nil && array->Count() == 0 && array->fFree == 6 && array->fChunk == 6 && array->ElementSize() == sizeof(Entry));
	for (long i = 0; i < 10; i++)
	{
		Entry e = { i, i * 10 };
		long index = array->Add();
		EXPECT(index == i);
		array->SetEntry(index, (const char*) &e);
	}
	EXPECT(array->Count() == 10 && (array->fFlags & 1) != 0);
	Entry* e = (Entry*) array->GetEntry(7);
	EXPECT(e != nil && e->a == 7 && e->b == 70 && array->GetEntry(10) == nil);
	Entry* added = (Entry*) array->AddEntry();
	EXPECT(added != nil && array->Count() == 11);
	added->a = 11;
	// the iterator walks the entries
	TArrayIterator iter;
	Entry* p = (Entry*) array->GetIterator(&iter);
	long seen = 0;
	long sum = 0;
	while (iter.fIndex < iter.fCount)
	{
		sum += p->b;
		seen++;
		p = (Entry*) iter.GetNext();
	}
	EXPECT(seen == 11 && sum == 450);
	// cut and compact
	array->CutToIndex(4);
	EXPECT(array->Count() == 4 && array->fFree > 0);
	array->Compact();
	EXPECT(array->fFree == 0 && GetHandleSize(array->fData) == 4 * (long) sizeof(Entry));
	array->Clone();
	EXPECT(!array->Release() && array->Release());
	// a copy
	TArray* copy = TArray::Make(sizeof(Entry), 0);
	EXPECT(array->CopyInto(copy) == 0 && copy->Count() == 4 && ((Entry*) copy->GetEntry(3))->b == 30 && copy->fData != array->fData);
	copy->Dispose();
	array->Clear();
	EXPECT(array->Count() == 0);
	array->Dispose();
	// TDArray: insertions and deletions in the middle
	TDArray* d = TDArray::Make(sizeof(long), 0);
	for (long i = 0; i < 5; i++)
	{
		long v = i;
		d->SetEntry(d->Add(), (const char*) &v);
	}
	long v = 99;
	EXPECT(d->InsertEntry(2, (const char*) &v) == 2 && d->Count() == 6 && *(long*) d->GetEntry(2) == 99 && *(long*) d->GetEntry(3) == 2 && *(long*) d->GetEntry(5) == 4);
	EXPECT(d->DeleteEntries(1, 2) == 1 && d->Count() == 4 && *(long*) d->GetEntry(1) == 2 && *(long*) d->GetEntry(3) == 4);
	EXPECT(d->DeleteEntries(10, 1) == (ULong) -1);
	long more[3] = { 7, 8, 9 };
	EXPECT(d->InsertEntries(0, (const char*) more, 3) == 0 && d->Count() == 7 && *(long*) d->GetEntry(0) == 7 && *(long*) d->GetEntry(2) == 9 && *(long*) d->GetEntry(3) == 0);
	d->Delete(6);
	EXPECT(d->Count() == 6 && *(long*) d->GetEntry(5) == 3);
	EXPECT(d->Insert(100) == 6 && d->Count() == 7);		// past the end: at the end
	d->Dispose();
	if (failures == 0)
		printf("test_RecObject: all passed\n");
	else
		printf("test_RecObject: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
