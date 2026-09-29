// CBufferList and CShadowBufferSegment test (utility/BufferList.h,
// ShadowBufferSegment.h): three segments read and written as one - bytes
// across the joins, seeking and the position, hiding a header off the front
// and a trailer off the back and putting them back - and a buffer over a
// shared-memory object, alone and as a segment of a list.  The shared-memory
// object is a real one, so the test runs as the kernel services task of a
// booted OS.

#include "BufferList.h"
#include "ShadowBufferSegment.h"
#include "UserSharedMem.h"
#include "SharedTypes.h"
#include "NewtErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
TestList()
{
	static UByte a[] = "abc", b[] = "defg", c[] = "hi";
	CBufferSegment* sa = new CBufferSegment;
	CBufferSegment* sb = new CBufferSegment;
	CBufferSegment* sc = new CBufferSegment;
	EXPECT(sa->Init(a, 3) == noErr && sb->Init(b, 4) == noErr && sc->Init(c, 2) == noErr);

	CBufferList* list = CBufferList::New();
	EXPECT(list->Init(true) == noErr);
	EXPECT(list->Insert(sa) == noErr && list->Insert(sb) == noErr && list->Insert(sc) == noErr);
	EXPECT(list->First() == sa && list->Last() == sc && list->At(1) == sb && list->GetIndex(sb) == 1);
	EXPECT(list->GetSize() == 9 && list->Position() == 0);

	UByte out[16];
	memset(out, 0, sizeof(out));
	EXPECT(list->Getn(out, 5) == 5 && memcmp(out, "abcde", 5) == 0);
	EXPECT(list->Position() == 5);
	EXPECT(list->Get() == 'f' && list->Peek() == 'g');
	Size n = 10;
	EXPECT(list->CopyOut(out, n) == -1 && n == 7 && memcmp(out, "ghi", 3) == 0);
	EXPECT(list->AtEOF() && list->Get() == -1);

	// seeking
	EXPECT(list->Seek(4, kSeekFromBeginning) == 4 && list->Get() == 'e');
	EXPECT(list->Seek(2, kSeekFromEnd) == 7 && list->Get() == 'h');
	EXPECT(list->Seek(-5, kSeekFromHere) == 3 && list->Get() == 'd');
	EXPECT(list->Seek(0, kSeekFromBeginning) == 0 && list->Get() == 'a');

	// writing across the joins
	list->Seek(2, kSeekFromBeginning);
	EXPECT(list->Putn((const UByte*) "XYZ", 3) == 3);
	EXPECT(a[2] == 'X' && b[0] == 'Y' && b[1] == 'Z');

	// hiding a header of four bytes and a trailer of three
	EXPECT(list->Hide(4, kSeekFromBeginning) == 4);
	EXPECT(list->GetSize() == 5 && list->Position() == 0 && list->Get() == 'Z');
	EXPECT(list->Hide(3, kSeekFromEnd) == 3);
	EXPECT(list->GetSize() == 2);
	list->Seek(0, kSeekFromBeginning);
	EXPECT(list->Get() == 'Z' && list->Get() == 'f' && list->Get() == -1);
	// and putting them back
	list->Reset();
	EXPECT(list->GetSize() == 9 && list->Get() == 'a');

	// removing and deleting
	EXPECT(list->RemoveFirst() == noErr && list->First() == sb);
	EXPECT(list->GetSize() == 6);
	delete sa;
	EXPECT(list->DeleteLast() == noErr && list->Last() == sb);
	list->Delete();							// takes sb with it (deleteSegments)
}


static void
TestShadow()
{
	UByte storage[8];
	memcpy(storage, "12345678", 8);
	TUSharedMem mem;
	EXPECT(mem.Init() == noErr);
	EXPECT(mem.SetBuffer(storage, sizeof(storage), kSMemReadWrite | kSMemNoSizeChangeOnCopyTo) == noErr);

	CShadowBufferSegment shadow;
	shadow.Init(mem.fId, 2, 4);				// "3456"
	EXPECT(shadow.GetSize() == 4 && shadow.Position() == 0);
	EXPECT(shadow.Peek() == '3' && shadow.Get() == '3' && shadow.Next() == '5');
	EXPECT(shadow.Skip() == 4 && shadow.Get() == '6' && shadow.Get() == -1 && shadow.AtEOF());
	EXPECT(shadow.Seek(1, kSeekFromBeginning) == 1);
	EXPECT(shadow.Put('x') == 'x' && storage[3] == 'x');
	UByte out[8];
	EXPECT(shadow.Getn(out, 8) == 2 && memcmp(out, "56", 2) == 0);
	EXPECT(shadow.Hide(1, kSeekFromBeginning) == 1 && shadow.GetSize() == 3);
	EXPECT(shadow.Hide(-5, kSeekFromBeginning) == -3 && shadow.GetSize() == 6);	// only back to the object's start
	EXPECT(shadow.GetByteAt(7) == '8' && shadow.PutByteAt('y', 7) == 'y' && storage[7] == 'y');
	shadow.Reset();
	EXPECT(shadow.GetSize() == 8);

	// as a segment of a list
	CShadowBufferSegment* seg = new CShadowBufferSegment;
	seg->Init(mem.fId, 0, 2);
	CBufferList* list = CBufferList::New();
	EXPECT(list->Init(true) == noErr && list->Insert(seg) == noErr);
	EXPECT(list->GetSize() == 2 && list->Get() == '1' && list->Get() == '2' && list->Get() == -1);
	list->Delete();
}


static void
Scenario(void)
{
	TestList();
	TestShadow();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;
	OsBoot();
	if (failures == 0)
		printf("test_BufferList: all passed\n");
	else
		printf("test_BufferList: %d failures\n", failures);
	return failures != 0;
}
