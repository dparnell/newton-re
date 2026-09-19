// Airus test (recognition/Airus.h): the dictionary container - one made
// and what its first two bytes say about it, the block's pointers kept
// right when the Handle moves, the data grown, slid and read.  The
// walkers themselves are NOT YET, so the selector call is only checked
// for the two that clear the error.
#include "Airus.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


int
main()
{
	InitHostStandaloneHeap();

	// an empty dictionary of the kind the machine writes into
	// (the type InitDictionaries asks for: the walkers the machine writes
	// with, and the Handle locked while they run)
	Handle dictionary = NewDictionary(kAirusKindEnumRAM | kAirusLockedBit, 1);
	EXPECT(dictionary != nil && airusResult == 0);
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	EXPECT(parms->fSelf == dictionary && parms->fAttributeSize == 1 && parms->fGrowBy == 100);
	EXPECT(parms->fSize == 2 && parms->fDataEnd - parms->fData == 2);
	EXPECT((UByte) parms->fData[0] == 'a');
	// the kind, with the size of each word's attribute above it
	EXPECT((UByte) parms->fData[1] == (kAirusKindEnumRAM | kAirusLockedBit | (1 << 4)));
	// and that byte asks for the Handle to be locked while a walker runs
	EXPECT(((UByte) parms->fData[1] & kAirusLockedBit) != 0);

	// the pointers follow the Handle, and the block becomes the one the
	// walkers work on
	AE_Parms = nil;
	CheckDictPtrs(parms);
	EXPECT(AE_Parms == parms);
	long used = parms->fDataEnd - parms->fData;
	Ptr was = parms->fData;
	parms->fData = was - 100;						// as if it had moved
	parms->fDataEnd = parms->fDataEnd - 100;
	CheckDictPtrs(parms);
	EXPECT(parms->fData == was && parms->fDataEnd - parms->fData == used);

	// grown a hundred at a time until there is room
	EXPECT(ExpandDict(50) == 0 && parms->fSize == 102 && parms->fError == 0);
	EXPECT(ExpandDict(300) == 0 && parms->fSize >= 302);
	EXPECT(parms->fDataEnd - parms->fData == 2);	// growing does not fill it

	// the bytes slid apart and back together
	parms->fData[0] = 'a';
	parms->fData[1] = (char) 0x1f;
	SlideDown(2, 3);								// three bytes of room at the end
	EXPECT(parms->fDataEnd - parms->fData == 5);
	parms->fData[2] = (char) 0x12;
	parms->fData[3] = (char) 0x34;
	parms->fData[4] = (char) 0x56;
	SlideDown(2, 1);								// one more, before them
	EXPECT(parms->fDataEnd - parms->fData == 6);
	EXPECT((UByte) parms->fData[3] == 0x12 && (UByte) parms->fData[5] == 0x56);
	SlideUp(3, 1);									// and taken out again
	EXPECT(parms->fDataEnd - parms->fData == 5);
	EXPECT((UByte) parms->fData[2] == 0x12 && (UByte) parms->fData[4] == 0x56);

	// read big-endian, as it lies
	EXPECT(GetDictBytes(2, 1) == 0x12);
	EXPECT(GetDictBytes(2, 2) == 0x1234);
	EXPECT(GetDictBytes(2, 3) == 0x123456);
	EXPECT(GetDictBytes(2, 0) == 0);

	// the two selectors that only say nothing has gone wrong
	parms->fError = 7;
	CallAirusA(dictionary, kAirusStartA);
	EXPECT(((AirusAParmBlock*) *dictionary)->fError == 0);
	((AirusAParmBlock*) *dictionary)->fError = 7;
	CallAirusANoLock(dictionary, kAirusExitA);
	EXPECT(((AirusAParmBlock*) *dictionary)->fError == 0);

	printf("test_Airus: %d failures\n", failures);
	return failures != 0;
}
