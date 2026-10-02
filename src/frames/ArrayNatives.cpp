/*
	File:		frames/ArrayNatives.cpp

	Contains:	The array searching, sorting and set functions of NewtonScript
				(Sort, StableSort, LSearch, BSearchLeft, BInsert, BMerge,
				SetUnion, ...) over TGeneralizedTestFnVar - the ROM's
				comparison object (a test symbol such as '|<| or '|str<|, or
				a closure, with an optional key path or key function) - and
				the binary accessors (ExtractLong, StuffByte, ...).

	The ROM sorts and searches the array's slots in place through raw Ref
	pointers with the array locked; so do these.
*/

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "RichString.h"
#include "Unicode.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "BinaryBytes.h"

#include <string.h>

int		SymbolCompareLexRef(Ref sym1, Ref sym2);						// Symbols.cpp
void	ArrayInsert(RefArg array, RefArg element, long index);			// Munger.cpp
Ref		FSetAdd(RefArg rcvr, RefArg members, RefArg member, RefArg unique);	// Munger.cpp

/* -------------------------------------------------------------------------------
	TGeneralizedTestFnVar
	The comparison the sort and search functions apply: a test - one of the
	symbols '|<| '|>| (numbers), '|str<| '|str>| (strings), '|chr<| '|chr>|
	(characters), '|sym<| '|sym>| (symbols), in EQ mode also '|str=| and
	'|=| - or a function of two arguments answering an integer (in EQ mode
	true/nil will do) - and a key: nil for the element itself, a symbol,
	path or integer taken with GetFramePath, or a function applied to it.
	The ROM's layout (0x24).
------------------------------------------------------------------------------- */

class TGeneralizedTestFnVar
{
public:
	TGeneralizedTestFnVar(RefArg test, RefArg key, int eqMode);

	Ref		ApplyKey(Ref element);
	Ref		ApplyKey(RefArg element);
	int		Test(Ref a, Ref b)					{ return (this->*fTestFn)(a, b); }

	int		TestNumsRealUtil(Ref a, Ref b);
	int		TestNumbers(Ref a, Ref b);
	int		TestUniStrings(Ref a, Ref b);
	int		TestUniChars(Ref a, Ref b);
	int		TestSymbols(Ref a, Ref b);
	int		TestEQ(Ref a, Ref b);
	int		TestClosure(Ref a, Ref b);
	int		TestEQClosure(Ref a, Ref b);

	long		fSense;				// +0x00  0 ascending, 1 descending (the test negated), 2 a closure
	long		fKeyMode;			// +0x04  0 none, 1 GetFramePath, 2 a function
	RefStruct	fTest;				// +0x08  the closure
	RefStruct	fArg1;				// +0x0c  its arguments
	RefStruct	fArg2;				// +0x10
	RefStruct	fKey;				// +0x14
	RefStruct	fKeyArg;			// +0x18  the key function's argument
	RefStruct	fTemp;				// +0x1c  the callers' scratch key
	int (TGeneralizedTestFnVar::*fTestFn)(Ref, Ref);	// +0x20
};


// ROM 0x0031661c __ct__21TGeneralizedTestFnVarFRC6RefVarT1i
TGeneralizedTestFnVar::TGeneralizedTestFnVar(RefArg test, RefArg key, int eqMode)
{
	if (!IsSymbol(test))
	{
		fTest = test;
		fTestFn = eqMode ? &TGeneralizedTestFnVar::TestEQClosure : &TGeneralizedTestFnVar::TestClosure;
		fSense = 2;
	}
	else if (EQRef(test, RSSYM_3C))
	{
		fTestFn = &TGeneralizedTestFnVar::TestNumbers;
		fSense = 0;
	}
	else if (EQRef(test, RSSYM_3E))
	{
		fTestFn = &TGeneralizedTestFnVar::TestNumbers;
		fSense = 1;
	}
	else if (EQRef(test, RSSYMstr_3C))
	{
		fTestFn = &TGeneralizedTestFnVar::TestUniStrings;
		fSense = 0;
	}
	else if (EQRef(test, RSSYMstr_3E))
	{
		fTestFn = &TGeneralizedTestFnVar::TestUniStrings;
		fSense = 1;
	}
	else if (EQRef(test, RSSYMstr_3D))
	{
		if (!eqMode)
			ThrowExFramesWithBadValue(kNSErrBadArgs, test);
		fTestFn = &TGeneralizedTestFnVar::TestUniStrings;
		fSense = 2;
	}
	else if (EQRef(test, RSSYMchr_3C))
	{
		fTestFn = &TGeneralizedTestFnVar::TestUniChars;
		fSense = 0;
	}
	else if (EQRef(test, RSSYMchr_3E))
	{
		fTestFn = &TGeneralizedTestFnVar::TestUniChars;
		fSense = 1;
	}
	else if (EQRef(test, RSSYMsym_3C))
	{
		fTestFn = &TGeneralizedTestFnVar::TestSymbols;
		fSense = 0;
	}
	else if (EQRef(test, RSSYMsym_3E))
	{
		fTestFn = &TGeneralizedTestFnVar::TestSymbols;
		fSense = 1;
	}
	else if (EQRef(test, RSSYM_3D))
	{
		if (!eqMode)
			ThrowExFramesWithBadValue(kNSErrBadArgs, test);
		fTestFn = &TGeneralizedTestFnVar::TestEQ;
		fSense = 2;
	}
	else
		ThrowExFramesWithBadValue(kNSErrBadArgs, test);

	Ref keyRef = key;
	if (keyRef == NILREF)
		fKeyMode = 0;
	else if (!ISINT(keyRef) && !IsSymbol(keyRef) && !EQRef(ClassOf(key), RSSYMpathexpr))
	{
		fKey = key;
		fKeyMode = 2;
	}
	else
	{
		fKey = key;
		fKeyMode = 1;
	}
}


// ROM 0x00316960 ApplyKey__21TGeneralizedTestFnVarFRC6RefVar
// The element's key (the element held by the caller).
Ref
TGeneralizedTestFnVar::ApplyKey(RefArg element)
{
	if (fKeyMode == 0)
		return element;
	if (fKeyMode == 1)
		return GetFramePath(element, fKey);
	if (fKeyMode == 2)
		return NSCall(fKey, element);
	return NILREF;
}


// ROM 0x003169a0 ApplyKey__21TGeneralizedTestFnVarFPl
// The element's key.
Ref
TGeneralizedTestFnVar::ApplyKey(Ref element)
{
	if (fKeyMode == 0)
		return element;
	if (fKeyMode == 1)
		return GetFramePath(RefVar(element), fKey);
	if (fKeyMode == 2)
	{
		fKeyArg = element;
		return NSCall(fKey, fKeyArg);
	}
	return NILREF;
}


// ROM 0x00316a2c TestNumsRealUtil__21TGeneralizedTestFnVarFPlT1
int
TGeneralizedTestFnVar::TestNumsRealUtil(Ref a, Ref b)
{
	double da = CoerceToDouble(RefVar(a));
	double db = CoerceToDouble(RefVar(b));
	return da > db ? 1 : da < db ? -1 : 0;
}


// ROM 0x00316aa4 TestNumbers__21TGeneralizedTestFnVarFPlT1
int
TGeneralizedTestFnVar::TestNumbers(Ref a, Ref b)
{
	int result;
	if (ISINT(a) && ISINT(b))
		result = (int) (RINT(a) - RINT(b));
	else
		result = TestNumsRealUtil(a, b);
	return fSense != 0 ? -result : result;
}


// ROM 0x00316b14 TestUniStrings__21TGeneralizedTestFnVarFPlT1
int
TGeneralizedTestFnVar::TestUniStrings(Ref a, Ref b)
{
	TRichString sa{RefVar(a)};
	TRichString sb{RefVar(b)};
	int result = sa.CompareSubStringCommon(sb, 0, -1, false);
	return fSense != 0 ? -result : result;
}


// ROM 0x00316bb8 TestUniChars__21TGeneralizedTestFnVarFPlT1
int
TGeneralizedTestFnVar::TestUniChars(Ref a, Ref b)
{
	int result = (int) RCHAR(a) - (int) RCHAR(b);
	return fSense != 0 ? -result : result;
}


// ROM 0x00316c48 TestSymbols__21TGeneralizedTestFnVarFPlT1
int
TGeneralizedTestFnVar::TestSymbols(Ref a, Ref b)
{
	int result = SymbolCompareLexRef(a, b);
	return fSense != 0 ? -result : result;
}


// ROM 0x00316f48 TestEQ__21TGeneralizedTestFnVarFPlT1
// ==> 0 when equal.
int
TGeneralizedTestFnVar::TestEQ(Ref a, Ref b)
{
	return EQRef(a, b) == 0;
}


// ROM 0x00316f70 TestClosure__21TGeneralizedTestFnVarFPlT1
// The closure's integer result.
int
TGeneralizedTestFnVar::TestClosure(Ref a, Ref b)
{
	fArg1 = a;
	fArg2 = b;
	RefVar result(NSCall(fTest, fArg1, fArg2));
	if (!ISINT((Ref) result))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, result);
	return (int) RINT(result);
}


// ROM 0x00316fc0 TestEQClosure__21TGeneralizedTestFnVarFPlT1
// The closure's integer result, or 0 (equal) for true and 1 for nil.
int
TGeneralizedTestFnVar::TestEQClosure(Ref a, Ref b)
{
	fArg1 = a;
	fArg2 = b;
	Ref result = NSCall(fTest, fArg1, fArg2);
	if (ISINT(result))
		return (int) RINT(result);
	return result == NILREF;
}


/* -------------------------------------------------------------------------------
	Sorting
------------------------------------------------------------------------------- */

// ROM 0x00313bf4 QSUtil__FPlT1P21TGeneralizedTestFnVar
// Quicksort of the slots lo..hi inclusive: median-of-three partitioning,
// the smaller part iterated and the larger pushed, runs of ten or fewer
// finished by insertion.
static void
QSUtil(Ref* lo, Ref* hi, TGeneralizedTestFnVar* test)
{
	Ref* stack[64];
	int depth = 0;
	RefVar pivotKey, loKey, hiKey, elem, elemKey;
	for (;;)
	{
		if (hi - lo <= 10)
		{
			for (Ref* p = lo; p <= hi; p++)
			{
				elem = *p;
				elemKey = test->ApplyKey(elem);
				Ref* q = p;
				for (; q - 1 >= lo; q--)
				{
					test->fTemp = test->ApplyKey(q[-1]);
					if (test->Test(elemKey, test->fTemp) >= 0)
						break;
					*q = q[-1];
				}
				*q = elem;
			}
			if (depth < 2)
				return;
			hi = stack[depth - 1];
			lo = stack[depth - 2];
			depth -= 2;
			continue;
		}

		// the median of the run to hi, the pivot; lo <= hi-1
		Ref* mid = lo + (hi - lo) / 2;
		Ref t = *mid; *mid = *hi; *hi = t;
		loKey = test->ApplyKey(*lo);
		hiKey = test->ApplyKey(hi[-1]);
		if (test->Test(loKey, hiKey) > 0)
		{
			t = *lo; *lo = hi[-1]; hi[-1] = t;
			pivotKey = loKey; loKey = hiKey; hiKey = pivotKey;
		}
		pivotKey = test->ApplyKey(*hi);
		if (test->Test(pivotKey, hiKey) > 0)
		{
			t = *hi; *hi = hi[-1]; hi[-1] = t;
			pivotKey = hiKey;
		}
		else if (test->Test(pivotKey, loKey) < 0)
		{
			t = *hi; *hi = *lo; *lo = t;
			pivotKey = loKey;
		}

		// partition
		Ref* i = lo;
		Ref* j = hi - 1;
		for (;;)
		{
			do
			{
				i++;
				if (i >= j)
					break;
				test->fTemp = test->ApplyKey(*i);
			} while (test->Test(pivotKey, test->fTemp) > 0);
			do
			{
				j--;
				if (j <= i)
					break;
				test->fTemp = test->ApplyKey(*j);
			} while (test->Test(pivotKey, test->fTemp) < 0);
			if (i >= j)
				break;
			t = *i; *i = *j; *j = t;
		}
		t = *hi; *hi = *i; *i = t;

		if (hi - i < i - lo)
		{
			stack[depth++] = lo;
			stack[depth++] = i - 1;
			lo = i + 1;
		}
		else
		{
			stack[depth++] = i + 1;
			stack[depth++] = hi;
			hi = i - 1;
		}
	}
}


// ROM 0x00313fe0 QSort__FRC6RefVarP21TGeneralizedTestFnVar
static void
QSort(RefArg array, TGeneralizedTestFnVar* test)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	LockRef(array);
	newton_try
	{
		Ref* slots = Slots(array);
		QSUtil(slots, slots + Length(array) - 1, test);
	}
	cleanup
	{
		UnlockRef(array);
	}
	end_try;
	UnlockRef(array);
}


// ROM 0x00314098 SortArray__FRC6RefVarN21
void
SortArray(RefArg array, RefArg test, RefArg key)
{
	TGeneralizedTestFnVar testFn(test, key, 0);
	QSort(array, &testFn);
}


// ROM 0x003140f8 FQuickSort
// Sort(array, test, key): the array sorted in place (not stably).
Ref
FQuickSort(RefArg /*rcvr*/, RefArg array, RefArg test, RefArg key)
{
	TGeneralizedTestFnVar testFn(test, key, 0);
	QSort(array, &testFn);
	return array;
}


// ROM 0x00318698 ShellSortUtil__FRC6RefVarP21TGeneralizedTestFnVarl
// Shell sort from the gap given, the gaps dividing by three.
static void
ShellSortUtil(RefArg array, TGeneralizedTestFnVar* test, long gap)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	LockRef(array);
	newton_try
	{
		long count = Length(array);
		Ref* slots = Slots(array);
		RefVar elem, elemKey;
		for (; gap > 0; gap /= 3)
		{
			for (Ref* p = slots + gap; p < slots + count; p++)
			{
				elem = *p;
				elemKey = test->ApplyKey(elem);
				Ref* q = p - gap;
				for (; q >= slots; q -= gap)
				{
					test->fTemp = test->ApplyKey(*q);
					if (test->Test(elemKey, test->fTemp) >= 0)
						break;
					q[gap] = *q;
				}
				q[gap] = elem;
			}
		}
	}
	cleanup
	{
		UnlockRef(array);
	}
	end_try;
	UnlockRef(array);
}


// ROM 0x00318858 FShellSort
Ref
FShellSort(RefArg /*rcvr*/, RefArg array, RefArg test, RefArg key)
{
	TGeneralizedTestFnVar testFn(test, key, 0);
	long count = Length(array);
	long gap = 1;
	while (gap <= count)
		gap = gap * 3 + 1;
	ShellSortUtil(array, &testFn, gap / 3);
	return array;
}


// ROM 0x00318900 FInsertionSort
Ref
FInsertionSort(RefArg /*rcvr*/, RefArg array, RefArg test, RefArg key)
{
	TGeneralizedTestFnVar testFn(test, key, 0);
	ShellSortUtil(array, &testFn, 1);
	return array;
}


// ROM 0x00318978 MergeUtil__FPllT1T2T1P21TGeneralizedTestFnVar
// The sorted runs a and b merged into dest (which may be where b starts,
// b being consumed no faster than dest fills); equal keys keep a first.
static void
MergeUtil(Ref* a, long aCount, Ref* b, long bCount, Ref* dest, TGeneralizedTestFnVar* test)
{
	RefVar aKey(test->ApplyKey(*a));
	RefVar bKey(test->ApplyKey(*b));
	Ref* aEnd = a + aCount;
	Ref* bEnd = b + bCount;
	for (;;)
	{
		if (test->Test(aKey, bKey) > 0)
		{
			*dest++ = *b++;
			if (b >= bEnd)
			{
				memcpy(dest, a, (aEnd - a) * sizeof(Ref));
				return;
			}
			bKey = test->ApplyKey(*b);
		}
		else
		{
			*dest++ = *a++;
			if (a >= aEnd)
			{
				memcpy(dest, b, (bEnd - b) * sizeof(Ref));
				return;
			}
			aKey = test->ApplyKey(*a);
		}
	}
}


// ROM 0x00318a7c MergeSortUtil__FPlT1lT3P21TGeneralizedTestFnVar
// count slots at src sorted: in place (the halves sorted into temp and
// merged back) or into temp (the halves sorted in place and merged out).
static void
MergeSortUtil(Ref* src, Ref* temp, long count, long inPlace, TGeneralizedTestFnVar* test)
{
	if (count == 0)
		return;
	if (count == 1)
	{
		if (!inPlace)
			*temp = *src;
		return;
	}
	if (count == 2)
	{
		RefVar aKey(test->ApplyKey(src[0]));
		RefVar bKey(test->ApplyKey(src[1]));
		Boolean swap = test->Test(aKey, bKey) > 0;
		if (!inPlace)
		{
			temp[0] = swap ? src[1] : src[0];
			temp[1] = swap ? src[0] : src[1];
		}
		else if (swap)
		{
			Ref t = src[0]; src[0] = src[1]; src[1] = t;
		}
		return;
	}
	long half = count / 2;
	long rest = count - half;
	if (inPlace)
	{
		MergeSortUtil(src, temp, half, 0, test);
		MergeSortUtil(src + half, temp + half, rest, 0, test);
		MergeUtil(temp, half, temp + half, rest, src, test);
	}
	else
	{
		MergeSortUtil(src, temp, half, 1, test);
		MergeSortUtil(src + half, temp + half, rest, 1, test);
		MergeUtil(src, half, src + half, rest, temp, test);
	}
}


// ROM 0x00318c2c MergeSort__FRC6RefVarP21TGeneralizedTestFnVar
// A stable sort: blocks of up to half the array (as much as a temporary
// array can hold, after a collection if need be) are merge-sorted through
// the temporary and merged from the end into the sorted tail.
static void
MergeSort(RefArg array, TGeneralizedTestFnVar* test)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	long count = Length(array);
	if (count < 4)
	{
		ShellSortUtil(array, test, 1);
		return;
	}
	long blockSize = (count + 1) / 2;
	ULong freeSpace, largestFree;
	gHeap->Statistics(&freeSpace, &largestFree);
	ULong needed = (count / 4) * 4 + 0x3f0;
	if (largestFree < needed)
	{
		GC();
		gHeap->Statistics(&freeSpace, &largestFree);
		if (largestFree < needed)
			AllocateArray(RSSYMarray, 0x7fffffff);		// throws out of memory
	}
	long available = (long) ((largestFree - 0x3f0) >> 2);
	if (available < blockSize)
		blockSize = available;
	RefVar temp(AllocateArray(RSSYMarray, blockSize));
	long numBlocks = (count + blockSize - 1) / blockSize;
	LockRef(array);
	LockRef(temp);
	newton_try
	{
		Ref* slots = Slots(array);
		Ref* tempSlots = Slots(temp);
		Ref* tail = slots + blockSize * (numBlocks - 1);
		MergeSortUtil(tail, tempSlots, (slots + count) - tail, 1, test);
		for (Ref* block = tail - blockSize; block >= slots; block -= blockSize)
		{
			MergeSortUtil(block, tempSlots, blockSize, 0, test);
			MergeUtil(tempSlots, blockSize, tail, (slots + count) - tail, block, test);
			tail = block;
		}
	}
	cleanup
	{
		UnlockRef(array);
		UnlockRef(temp);
	}
	end_try;
	UnlockRef(array);
	UnlockRef(temp);
}


// ROM 0x00313b80 FStableSort
Ref
FStableSort(RefArg /*rcvr*/, RefArg array, RefArg test, RefArg key)
{
	TGeneralizedTestFnVar testFn(test, key, 0);
	MergeSort(array, &testFn);
	return array;
}


/* -------------------------------------------------------------------------------
	Searching
------------------------------------------------------------------------------- */

// ROM 0x00317028 LSearch__FRC6RefVarN41
// The index of the first element from start whose key the test finds
// equal to item, -1 for none.  A plain '|=| search compares the elements
// with EQ directly.
long
LSearch(RefArg array, RefArg item, RefArg start, RefArg test, RefArg key)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	long count = Length(array);
	Long index = RINT(start);
	if (index >= count)
		return -1;
	if (index < 0)
		ThrowExFramesWithBadValue(kNSErrOutOfBounds, start);
	long found = -1;
	if (IsSymbol(test) && (Ref) key == NILREF && EQRef(test, RSSYM_3D))
	{
		Ref* slots = Slots(array);
		for (Ref* p = slots + index; p < slots + count; p++)
			if (EQRef(item, *p))
			{
				found = p - slots;
				break;
			}
		return found;
	}
	LockRef(array);
	newton_try
	{
		Ref* slots = Slots(array);
		TGeneralizedTestFnVar testFn(test, key, 1);
		for (Ref* p = slots + index; p < slots + count; p++)
		{
			testFn.fTemp = testFn.ApplyKey(*p);
			if (testFn.Test(item, testFn.fTemp) == 0)
			{
				found = p - slots;
				break;
			}
		}
	}
	cleanup
	{
		UnlockRef(array);
	}
	end_try;
	UnlockRef(array);
	return found;
}


// ROM 0x00317298 FLSearch
Ref
FLSearch(RefArg /*rcvr*/, RefArg array, RefArg item, RefArg start, RefArg test, RefArg key)
{
	long index = LSearch(array, item, start, test, key);
	return index < 0 ? NILREF : MAKEINT(index);
}


// ROM 0x003172e0 FLFetch
// The element LSearch finds, nil for none.
Ref
FLFetch(RefArg /*rcvr*/, RefArg array, RefArg item, RefArg start, RefArg test, RefArg key)
{
	long index = LSearch(array, item, start, test, key);
	if (index < 0)
		return NILREF;
	return GetArraySlotRef(array, index);
}


// ROM 0x0031732c BSearchRight__FRC6RefVarT1P21TGeneralizedTestFnVar
// In a sorted array, the index of the last element whose key is not
// greater than the key given (-1 when all are greater).
static long
BSearchRight(RefArg array, RefArg key, TGeneralizedTestFnVar* test)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	long lo = 0;
	long hi = Length(array) - 1;
	LockRef(array);
	newton_try
	{
		Ref* slots = Slots(array);
		while (lo <= hi)
		{
			long mid = (lo + hi) / 2;
			test->fTemp = test->ApplyKey(slots[mid]);
			if (test->Test(key, test->fTemp) < 0)
				hi = mid - 1;
			else
				lo = mid + 1;
		}
	}
	cleanup
	{
		UnlockRef(array);
	}
	end_try;
	UnlockRef(array);
	return hi;
}


// ROM 0x0031744c FBSearchRight
Ref
FBSearchRight(RefArg /*rcvr*/, RefArg array, RefArg key, RefArg test, RefArg keyPath)
{
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	return MAKEINT(BSearchRight(array, key, &testFn));
}


// ROM 0x003174c4 BSearchLeft__FRC6RefVarT1P21TGeneralizedTestFnVar
// In a sorted array, the index of the first element whose key is not less
// than the key given (the length when all are less).
static long
BSearchLeft(RefArg array, RefArg key, TGeneralizedTestFnVar* test)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	long lo = 0;
	long hi = Length(array) - 1;
	LockRef(array);
	newton_try
	{
		Ref* slots = Slots(array);
		while (lo <= hi)
		{
			long mid = (lo + hi) / 2;
			test->fTemp = test->ApplyKey(slots[mid]);
			if (test->Test(key, test->fTemp) < 1)
				hi = mid - 1;
			else
				lo = mid + 1;
		}
	}
	cleanup
	{
		UnlockRef(array);
	}
	end_try;
	UnlockRef(array);
	return lo;
}


// ROM 0x003175e4 FBSearchLeft
Ref
FBSearchLeft(RefArg /*rcvr*/, RefArg array, RefArg key, RefArg test, RefArg keyPath)
{
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	return MAKEINT(BSearchLeft(array, key, &testFn));
}


// the key of the element at index equals key
static Boolean
KeyMatchesAt(RefArg array, long index, RefArg key, TGeneralizedTestFnVar* test)
{
	RefVar element(GetArraySlotRef(array, index));
	test->fTemp = test->ApplyKey(element);
	return test->Test(key, test->fTemp) == 0;
}


// ROM 0x003178ec FBInsert
// element inserted before the first element not less than it; with
// uniqueOnly an equal element already there is not added (nil, or the
// existing element for 'returnElt).  ==> the index, or the element for
// 'returnElt.
Ref
FBInsert(RefArg /*rcvr*/, RefArg array, RefArg element, RefArg test, RefArg keyPath, RefArg uniqueOnly)
{
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	RefVar key(testFn.ApplyKey(element));
	long index = BSearchLeft(array, key, &testFn);
	if ((Ref) uniqueOnly != NILREF && index < Length(array) && KeyMatchesAt(array, index, key, &testFn))
		return EQRef(uniqueOnly, RSSYMreturnelt) ? GetArraySlotRef(array, index) : NILREF;
	ArrayInsert(array, element, index);
	return EQRef(uniqueOnly, RSSYMreturnelt) ? (Ref) element : MAKEINT(index);
}


// ROM 0x00317768 FBInsertRight
// element inserted after the last element not greater than it.
Ref
FBInsertRight(RefArg /*rcvr*/, RefArg array, RefArg element, RefArg test, RefArg keyPath, RefArg uniqueOnly)
{
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	RefVar key(testFn.ApplyKey(element));
	long index = BSearchRight(array, key, &testFn);
	if ((Ref) uniqueOnly != NILREF && index >= 0 && KeyMatchesAt(array, index, key, &testFn))
		return EQRef(uniqueOnly, RSSYMreturnelt) ? GetArraySlotRef(array, index) : NILREF;
	ArrayInsert(array, element, index + 1);
	return EQRef(uniqueOnly, RSSYMreturnelt) ? (Ref) element : MAKEINT(index + 1);
}


// ROM 0x00317a78 FBDelete
// The elements whose key equals key removed (up to count of them, all for
// nil); ==> how many.
Ref
FBDelete(RefArg /*rcvr*/, RefArg array, RefArg key, RefArg test, RefArg keyPath, RefArg count)
{
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	long start = BSearchLeft(array, key, &testFn);
	long end;
	if (ISINT((Ref) count))
	{
		end = start + RINT(count);
		if (end > Length(array))
			end = Length(array);
	}
	else
		end = Length(array);
	long index = start;
	for (; index < end; index++)
		if (!KeyMatchesAt(array, index, key, &testFn))
			break;
	ArrayRemoveCount(array, start, index - start);
	return MAKEINT(index - start);
}


// ROM 0x00317ca8 FBFind
// The index of the first element whose key equals key, nil for none.
Ref
FBFind(RefArg /*rcvr*/, RefArg array, RefArg key, RefArg test, RefArg keyPath)
{
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	long index = BSearchLeft(array, key, &testFn);
	if (index < Length(array) && KeyMatchesAt(array, index, key, &testFn))
		return MAKEINT(index);
	return NILREF;
}


// ROM 0x00317bc8 FBFindRight
// The index of the last element whose key equals key, nil for none.
Ref
FBFindRight(RefArg /*rcvr*/, RefArg array, RefArg key, RefArg test, RefArg keyPath)
{
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	long index = BSearchRight(array, key, &testFn);
	if (index >= 0 && KeyMatchesAt(array, index, key, &testFn))
		return MAKEINT(index);
	return NILREF;
}


// ROM 0x00317e8c FBFetch
Ref
FBFetch(RefArg /*rcvr*/, RefArg array, RefArg key, RefArg test, RefArg keyPath)
{
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	long index = BSearchLeft(array, key, &testFn);
	if (index < Length(array) && KeyMatchesAt(array, index, key, &testFn))
		return GetArraySlotRef(array, index);
	return NILREF;
}


// ROM 0x00317d98 FBFetchRight
Ref
FBFetchRight(RefArg /*rcvr*/, RefArg array, RefArg key, RefArg test, RefArg keyPath)
{
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	long index = BSearchRight(array, key, &testFn);
	if (index >= 0 && KeyMatchesAt(array, index, key, &testFn))
		return GetArraySlotRef(array, index);
	return NILREF;
}


/* -------------------------------------------------------------------------------
	Ordered set operations
	GenOrderedSetOp walks two sorted arrays together; at each step the
	operation function turns the comparison of the current elements (and
	the uniqueOnly flag) into what to do: bits
		1  advance a       4  copy a's element      0x10  skip a's duplicates
		2  advance b       8  copy b's element      0x20  skip b's duplicates
------------------------------------------------------------------------------- */

const long kGOSOPAdvanceA = 0x01;
const long kGOSOPAdvanceB = 0x02;
const long kGOSOPCopyA = 0x04;
const long kGOSOPCopyB = 0x08;
const long kGOSOPSkipDupsA = 0x10;
const long kGOSOPSkipDupsB = 0x20;

typedef long (*OrderedSetOpFn)(long comparison, long uniqueOnly);


// ROM 0x00317f90 GenOrderedSetOp__FRC6RefVarN31lPFlT1_iN35
// ==> a new array of resultSize (trimmed) built by op from the sorted
// arrays a and b; a's rest and b's rest are copied after one runs out
// when copyRestA/copyRestB say so.
static Ref
GenOrderedSetOp(RefArg a, RefArg b, RefArg test, RefArg keyPath, long uniqueOnly, OrderedSetOpFn op, long resultSize, long copyRestA, long copyRestB)
{
	if (!IsArray(a))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, a);
	if (!IsArray(b))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, b);
	TGeneralizedTestFnVar testFn(test, keyPath, 0);
	RefVar result(AllocateArray(RSSYMarray, resultSize));
	LockRef(a);
	LockRef(b);
	LockRef(result);
	newton_try
	{
		Ref* pa = Slots(a);
		Ref* pb = Slots(b);
		Ref* pr = Slots(result);
		Ref* aEnd = pa + Length(a);
		Ref* bEnd = pb + Length(b);
		Ref* rEnd = pr + resultSize;
		RefVar aKey, bKey, lastKey;
		if (pa < aEnd && pb < bEnd)
		{
			aKey = testFn.ApplyKey(*pa);
			bKey = testFn.ApplyKey(*pb);
			Boolean done = false, aDone = false, bDone = false;
			do
			{
				long action = op(testFn.Test(aKey, bKey), uniqueOnly);
				if (action & kGOSOPAdvanceA)
				{
					lastKey = aKey;
					do
					{
						if (action & kGOSOPCopyA)
							*pr++ = *pa;
						pa++;
						if (pa < aEnd)
							aKey = testFn.ApplyKey(*pa);
						else
							done = aDone = true;
					} while ((action & kGOSOPSkipDupsA) && !aDone && testFn.Test(lastKey, aKey) == 0);
				}
				if (action & kGOSOPAdvanceB)
				{
					lastKey = bKey;
					do
					{
						if (action & kGOSOPCopyB)
							*pr++ = *pb;
						pb++;
						if (pb < bEnd)
							bKey = testFn.ApplyKey(*pb);
						else
							done = bDone = true;
					} while ((action & kGOSOPSkipDupsB) && !bDone && testFn.Test(lastKey, bKey) == 0);
				}
			} while (!done);
		}
		if (copyRestA && pa < aEnd)
		{
			memcpy(pr, pa, (aEnd - pa) * sizeof(Ref));
			pr += aEnd - pa;
		}
		if (copyRestB && pb < bEnd)
		{
			memcpy(pr, pb, (bEnd - pb) * sizeof(Ref));
			pr += bEnd - pb;
		}
		if (pr < rEnd)
			SetLength(result, pr - Slots(result));
	}
	cleanup
	{
		UnlockRef(a);
		UnlockRef(b);
		UnlockRef(result);
	}
	end_try;
	UnlockRef(a);
	UnlockRef(b);
	UnlockRef(result);
	return result;
}


// ROM 0x00318404 GOSOP_Merge__FlT1
static long
GOSOP_Merge(long comparison, long uniqueOnly)
{
	long action;
	if (comparison > 0)
		action = kGOSOPAdvanceB | kGOSOPCopyB;
	else if (comparison == 0 && uniqueOnly)
		action = kGOSOPAdvanceA | kGOSOPAdvanceB | kGOSOPCopyA;
	else
		action = kGOSOPAdvanceA | kGOSOPCopyA;
	if (!uniqueOnly)
		action |= kGOSOPSkipDupsA | kGOSOPSkipDupsB;
	return action;
}


// ROM 0x00318438 GOSOP_Intersection__FlT1
static long
GOSOP_Intersection(long comparison, long uniqueOnly)
{
	long action;
	if (comparison < 0)
		action = kGOSOPAdvanceA;
	else if (comparison == 0)
		action = kGOSOPAdvanceA | kGOSOPAdvanceB | kGOSOPCopyA | (uniqueOnly ? 0 : kGOSOPCopyB);
	else
		action = kGOSOPAdvanceB;
	if (!uniqueOnly)
		action |= kGOSOPSkipDupsA | kGOSOPSkipDupsB;
	return action;
}


// ROM 0x00318510 GOSOP_Difference__FlT1
static long
GOSOP_Difference(long comparison, long /*uniqueOnly*/)
{
	if (comparison < 0)
		return kGOSOPAdvanceA | kGOSOPCopyA | kGOSOPSkipDupsA;
	if (comparison == 0)
		return kGOSOPAdvanceA | kGOSOPAdvanceB | kGOSOPSkipDupsA | kGOSOPSkipDupsB;
	return kGOSOPAdvanceB | kGOSOPSkipDupsB;
}


// ROM 0x00318528 FBMerge
// The sorted arrays merged into a new sorted array; with uniqueOnly one
// of each pair of equal elements.
Ref
FBMerge(RefArg /*rcvr*/, RefArg a, RefArg b, RefArg test, RefArg keyPath, RefArg uniqueOnly)
{
	return GenOrderedSetOp(a, b, test, keyPath, (Ref) uniqueOnly != NILREF, GOSOP_Merge, Length(a) + Length(b), 1, 1);
}


// ROM 0x003185a8 FBIntersect
// The elements the sorted arrays share (both copies unless uniqueOnly).
Ref
FBIntersect(RefArg /*rcvr*/, RefArg a, RefArg b, RefArg test, RefArg keyPath, RefArg uniqueOnly)
{
	long aLength = Length(a);
	long bLength = Length(b);
	long resultSize;
	if ((Ref) uniqueOnly == NILREF)
		resultSize = aLength + bLength;
	else
		resultSize = bLength < aLength ? aLength : bLength;
	return GenOrderedSetOp(a, b, test, keyPath, (Ref) uniqueOnly != NILREF, GOSOP_Intersection, resultSize, 0, 0);
}


// ROM 0x0031863c FBDifference
// The elements of sorted a not in sorted b.
Ref
FBDifference(RefArg /*rcvr*/, RefArg a, RefArg b, RefArg test, RefArg keyPath)
{
	return GenOrderedSetOp(a, b, test, keyPath, 0, GOSOP_Difference, Length(a), 1, 0);
}


/* -------------------------------------------------------------------------------
	Unordered sets
------------------------------------------------------------------------------- */

// ROM 0x0031643c FSetOverlaps
// The index of the first element of array that is in targetArray (EQ),
// nil for none.
Ref
FSetOverlaps(RefArg /*rcvr*/, RefArg array, RefArg targetArray)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	if (!IsArray(targetArray))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, targetArray);
	long count = Length(array);
	long targetCount = Length(targetArray);
	for (long i = 0; i < count; i++)
		for (long j = 0; j < targetCount; j++)
			if (EQRef(GetArraySlotRef(targetArray, j), GetArraySlotRef(array, i)))
				return MAKEINT(i);
	return NILREF;
}


// ROM 0x00316c74 FSetUnion
// A new array of both arrays' elements (each once with uniqueOnly); a nil
// array counts as empty, a lone array is cloned.
Ref
FSetUnion(RefArg rcvr, RefArg array1, RefArg array2, RefArg uniqueOnly)
{
	if ((Ref) array1 == NILREF)
	{
		if ((Ref) array2 == NILREF)
			return AllocateArray(RSSYMarray, 0);
		if (!IsArray(array2))
			ThrowBadTypeWithFrameData(kNSErrNotAnArray, array2);
		return Clone(array2);
	}
	if ((Ref) array2 == NILREF)
	{
		if (!IsArray(array1))
			ThrowBadTypeWithFrameData(kNSErrNotAnArray, array1);
		return Clone(array1);
	}
	if (!IsArray(array1))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array1);
	if (!IsArray(array2))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array2);
	long count1 = Length(array1);
	long count2 = Length(array2);
	RefVar result;
	RefVar element;
	if ((Ref) uniqueOnly == NILREF)
	{
		result = AllocateArray(RSSYMarray, count1 + count2);
		long slot = 0;
		for (long i = 0; i < count1; i++)
			SetArraySlotRef(result, slot++, GetArraySlotRef(array1, i));
		for (long i = 0; i < count2; i++)
			SetArraySlotRef(result, slot++, GetArraySlotRef(array2, i));
	}
	else
	{
		result = AllocateArray(RSSYMarray, 0);
		for (long i = 0; i < count1; i++)
		{
			element = GetArraySlotRef(array1, i);
			FSetAdd(rcvr, result, element, uniqueOnly);
		}
		for (long i = 0; i < count2; i++)
		{
			element = GetArraySlotRef(array2, i);
			FSetAdd(rcvr, result, element, uniqueOnly);
		}
	}
	return result;
}


// ROM 0x0031765c FSetDifference
// A clone of array1 without the elements of array2 (nil for a nil array1).
Ref
FSetDifference(RefArg /*rcvr*/, RefArg array1, RefArg array2)
{
	if ((Ref) array1 == NILREF)
		return NILREF;
	if (!IsArray(array1))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array1);
	RefVar result(Clone(array1));
	if ((Ref) array2 == NILREF)
		return result;
	if (!IsArray(array2))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array2);
	long count = Length(array2);
	RefVar element;
	for (long i = 0; i < count; i++)
	{
		element = GetArraySlotRef(array2, i);
		ArrayRemove(result, element);
	}
	return result;
}


/* -------------------------------------------------------------------------------
	Binary objects
------------------------------------------------------------------------------- */

// ROM 0x00318468 BinEqual__FRC6RefVarT1
Boolean
BinEqual(RefArg a, RefArg b)
{
	if (!IsBinary(a))
		ThrowBadTypeWithFrameData(kNSErrNotABinaryObject, a);
	if (!IsBinary(b))
		ThrowBadTypeWithFrameData(kNSErrNotABinaryObject, b);
	long length = Length(a);
	if (Length(b) != length)
		return false;
	return memcmp(BinaryData(a), BinaryData(b), length) == 0;
}


// ROM 0x00313b58 FBinEqual
Ref
FBinEqual(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return BinEqual(a, b) ? TRUEREF : NILREF;
}


// ROM 0x00315534 BoundsCheck__FRC6RefVarlUi
// obj must be a binary with size bytes at offset.
static void
BoundsCheck(RefArg obj, long offset, ULong size)
{
	Ref ref = obj;
	if (ISPTR(ref) && (ObjectFlags(ref) & kObjSlotted) == 0 && offset >= 0 && offset + (long) size <= Length(ref))
		return;
	Throw(exFrames, (void*) kNSErrBadArgs, nil);
}


// ROM 0x003155a8 BoundsWriteCheck__FRC6RefVarlUi
// ... and writable.
static void
BoundsWriteCheck(RefArg obj, long offset, ULong size)
{
	Ref ref = obj;
	ULong flags = ObjectFlags(ref);
	if (!ISPTR(ref) || (flags & kObjSlotted) != 0 || offset < 0 || Length(ref) < offset + (long) size)
		Throw(exFrames, (void*) kNSErrBadArgs, nil);
	if (flags & kObjReadOnly)
	{
		RefVar frame(AllocateFrame());
		SetFrameSlot(frame, RSSYMerrorcode, RefVar(MAKEINT(kNSErrObjectReadOnly)));
		SetFrameSlot(frame, RSSYMvalue, obj);
		ThrowRefException(exFramesWithFrameData, frame);
	}
}


// ROM 0x00315638 FExtractChar
// The byte at offset as a character (through the Mac Roman encoding).
Ref
FExtractChar(RefArg /*rcvr*/, RefArg obj, RefArg offset)
{
	Long index = RINT(offset);
	BoundsCheck(obj, index, 1);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	char byte[2] = { BinaryData(obj)[index], 0 };
	UniChar c[2];
	ConvertToUnicode(byte, c, kMacRomanEncoding, 1);
	return MAKECHAR(c[0]);
}


// ROM 0x003156c4 FStuffChar
Ref
FStuffChar(RefArg /*rcvr*/, RefArg obj, RefArg offset, RefArg c)
{
	Long index = RINT(offset);
	BoundsWriteCheck(obj, index, 1);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	Ref ref = c;
	UniChar ch[2] = { (UniChar) (ISINT(ref) ? RINT(ref) : RCHAR(ref)), 0 };
	char byte[4];
	ConvertFromUnicode(ch, byte, kMacRomanEncoding, 1);
	BinaryData(obj)[index] = byte[0];
	return NILREF;
}


// ROM 0x0031579c FExtractUniChar
Ref
FExtractUniChar(RefArg /*rcvr*/, RefArg obj, RefArg offset)
{
	Long index = RINT(offset);
	BoundsCheck(obj, index, 2);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	unsigned char* data = (unsigned char*) BinaryData(obj) + index;
	return MAKECHAR((UniChar) ((data[0] << 8) | data[1]));
}


// ROM 0x00315814 FStuffUniChar
Ref
FStuffUniChar(RefArg /*rcvr*/, RefArg obj, RefArg offset, RefArg c)
{
	Long index = RINT(offset);
	BoundsWriteCheck(obj, index, 2);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	Ref ref = c;
	UniChar ch = (UniChar) (ISINT(ref) ? RINT(ref) : RCHAR(ref));
	unsigned char* data = (unsigned char*) BinaryData(obj) + index;
	data[0] = (unsigned char) (ch >> 8);
	data[1] = (unsigned char) ch;
	return NILREF;
}


// ROM 0x003158d8 FExtractByte
Ref
FExtractByte(RefArg /*rcvr*/, RefArg obj, RefArg offset)
{
	Long index = RINT(offset);
	BoundsCheck(obj, index, 1);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	return MAKEINT((unsigned char) BinaryData(obj)[index]);
}


// ROM 0x0031592c FStuffByte
Ref
FStuffByte(RefArg /*rcvr*/, RefArg obj, RefArg offset, RefArg value)
{
	Long index = RINT(offset);
	BoundsWriteCheck(obj, index, 1);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	if (ObjectFlags(obj) & kObjReadOnly)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, obj);
	BinaryData(obj)[index] = (char) RINT(value);
	return NILREF;
}


// ROM 0x003159c0 FExtractWord
// The signed big-endian 16-bit word at offset.
Ref
FExtractWord(RefArg /*rcvr*/, RefArg obj, RefArg offset)
{
	Long index = RINT(offset);
	BoundsCheck(obj, index, 2);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	unsigned char* data = (unsigned char*) BinaryData(obj) + index;
	return MAKEINT((short) ((data[0] << 8) | data[1]));
}


// ROM 0x00315a38 FStuffWord
Ref
FStuffWord(RefArg /*rcvr*/, RefArg obj, RefArg offset, RefArg value)
{
	Long index = RINT(offset);
	BoundsWriteCheck(obj, index, 2);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	Long v = RINT(value);
	unsigned char* data = (unsigned char*) BinaryData(obj) + index;
	data[0] = (unsigned char) (v >> 8);
	data[1] = (unsigned char) v;
	return NILREF;
}


// The big-endian 32-bit word at offset (BoundsCheck done).
static ULong
LongAt(RefArg obj, long index)
{
	unsigned char* data = (unsigned char*) BinaryData(obj) + index;
	return ((ULong) data[0] << 24) | ((ULong) data[1] << 16) | ((ULong) data[2] << 8) | data[3];
}


// ROM 0x00315b20 FExtractLong
// The signed 32-bit word at offset; one that does not fit an integer ref
// (30 bits) is an error.
Ref
FExtractLong(RefArg /*rcvr*/, RefArg obj, RefArg offset)
{
	Long index = RINT(offset);
	BoundsCheck(obj, index, 4);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	ULong value = LongAt(obj, index);
#if !NEWTON_NS64		// (a 62-bit integer holds any signed 32-bit word: docs/frames/64bit.md)
	if ((value & 0xc0000000) != 0 && (value & 0xc0000000) != 0xc0000000)
		Throw(exFrames, (void*) kNSErrLongOutOfRange, nil);
#endif
	return MAKEINT((long) (int) value);
}


// ROM 0x00315bd0 FStuffLong
// (the ROM checks the bounds but not that the object is writable)
Ref
FStuffLong(RefArg /*rcvr*/, RefArg obj, RefArg offset, RefArg value)
{
	Long index = RINT(offset);
	BoundsCheck(obj, index, 4);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	Long v = RINT(value);
	unsigned char* data = (unsigned char*) BinaryData(obj) + index;
	data[0] = (unsigned char) (v >> 24);
	data[1] = (unsigned char) (v >> 16);
	data[2] = (unsigned char) (v >> 8);
	data[3] = (unsigned char) v;
	return NILREF;
}


// ROM 0x00315c70 FExtractXLong
// The 32-bit word at offset without its low three bits (a value that
// always fits).
Ref
FExtractXLong(RefArg /*rcvr*/, RefArg obj, RefArg offset)
{
	Long index = RINT(offset);
	BoundsCheck(obj, index, 4);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	return MAKEINT(LongAt(obj, index) >> 3);
}


// ROM 0x00315cf8 FExtractCString
// The NUL-terminated C string at offset, as a string.
Ref
FExtractCString(RefArg /*rcvr*/, RefArg obj, RefArg offset)
{
	Long index = RINT(offset);
	BoundsCheck(obj, index, 1);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	LockRef(obj);
	char* data = BinaryData(obj);
	long length = Length(obj);
	char* p = data + index;
	while (*p != 0 && p < data + length)
		p++;
	if (p >= data + length)
	{
		UnlockRef(obj);
		Throw(exFrames, (void*) kNSErrBadArgs, nil);
	}
	RefVar str(MakeString(data + index));
	UnlockRef(obj);
	return str;
}


// ROM 0x00315de4 FStuffCString
Ref
FStuffCString(RefArg /*rcvr*/, RefArg obj, RefArg offset, RefArg str)
{
	Long index = RINT(offset);
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	long length = (Length(str) - 2) / 2;
	BoundsWriteCheck(obj, index, length + 1);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	ConvertFromUnicode((UniChar*) BinaryData(str), BinaryData(obj) + index, kMacRomanEncoding, length);
	return NILREF;
}


// ROM 0x00315e8c FExtractPString
// The Pascal string (a length byte then its characters) at offset.
Ref
FExtractPString(RefArg /*rcvr*/, RefArg obj, RefArg offset)
{
	Long index = RINT(offset);
	unsigned char length = (unsigned char) BinaryData(obj)[index];
	BoundsCheck(obj, index, length + 1);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	RefVar str(AllocateBinary(RSSYMstring, (length + 1) * sizeof(UniChar)));
	ConvertToUnicode(BinaryData(obj) + index + 1, (UniChar*) BinaryData(str), kMacRomanEncoding, length);
	return str;
}


// ROM 0x00315f3c FStuffPString
Ref
FStuffPString(RefArg /*rcvr*/, RefArg obj, RefArg offset, RefArg str)
{
	Long index = RINT(offset);
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	ULong length = (Length(str) - 2) >> 1;
	if (length > 0xff)
		Throw(exFrames, (void*) kNSErrStringTooBig, nil);
	BoundsWriteCheck(obj, index, length + 1);
	TBinaryBytesAsROM bytes(obj);		// (a string's bytes as the ROM's: BinaryBytes.h)
	char text[256];
	ConvertFromUnicode((UniChar*) BinaryData(str), text, kMacRomanEncoding, 0xff);
	memcpy(BinaryData(obj) + index + 1, text, length);
	BinaryData(obj)[index] = (char) length;
	return NILREF;
}


// ROM 0x00316028 FExtractBytes
// A new binary of class theClass holding count bytes (nil: to the end)
// from offset.
Ref
FExtractBytes(RefArg /*rcvr*/, RefArg obj, RefArg offset, RefArg count, RefArg theClass)
{
	Long index = RINT(offset);
	long length;
	if ((Ref) count == NILREF)
		length = Length(obj) - index;
	else
		length = RINT(count);
	Ref ref = obj;
	if (!(ISPTR(ref) && (ObjectFlags(ref) & kObjSlotted) == 0 && index >= 0 && length >= 0
		&& index + length <= Length(ref) && IsSymbol(theClass)))
		Throw(exFrames, (void*) kNSErrBadArgs, nil);
	RefVar result(AllocateBinary(theClass, length));
	{
		TBinaryBytesAsROM bytes(obj);		// (the bytes as the ROM's: BinaryBytes.h)
		memcpy(BinaryData(result), BinaryData(obj) + index, length);
	}
	BinaryBytesFromROM(result);			// (a string made of them in the host's order)
	return result;
}


/* -------------------------------------------------------------------------------
	Registration
------------------------------------------------------------------------------- */

#define NATIVE(symbol, fn, n)	RegisterNativeFunction(symbol, (void*) (NativeFn##n) fn, n)

void
RegisterArrayNatives(void)
{
	NATIVE("FQuickSort", FQuickSort, 3);
	NATIVE("FStableSort", FStableSort, 3);
	NATIVE("FShellSort", FShellSort, 3);
	NATIVE("FInsertionSort", FInsertionSort, 3);
	NATIVE("FLSearch", FLSearch, 5);
	NATIVE("FLFetch", FLFetch, 5);
	NATIVE("FBSearchLeft", FBSearchLeft, 4);
	NATIVE("FBSearchRight", FBSearchRight, 4);
	NATIVE("FBInsert", FBInsert, 5);
	NATIVE("FBInsertRight", FBInsertRight, 5);
	NATIVE("FBDelete", FBDelete, 5);
	NATIVE("FBFind", FBFind, 4);
	NATIVE("FBFindRight", FBFindRight, 4);
	NATIVE("FBFetch", FBFetch, 4);
	NATIVE("FBFetchRight", FBFetchRight, 4);
	NATIVE("FBMerge", FBMerge, 5);
	NATIVE("FBIntersect", FBIntersect, 5);
	NATIVE("FBDifference", FBDifference, 4);
	NATIVE("FSetOverlaps", FSetOverlaps, 2);
	NATIVE("FSetUnion", FSetUnion, 3);
	NATIVE("FSetDifference", FSetDifference, 2);
	NATIVE("FBinEqual", FBinEqual, 2);
	NATIVE("FExtractChar", FExtractChar, 2);
	NATIVE("FStuffChar", FStuffChar, 3);
	NATIVE("FExtractUniChar", FExtractUniChar, 2);
	NATIVE("FStuffUniChar", FStuffUniChar, 3);
	NATIVE("FExtractByte", FExtractByte, 2);
	NATIVE("FStuffByte", FStuffByte, 3);
	NATIVE("FExtractWord", FExtractWord, 2);
	NATIVE("FStuffWord", FStuffWord, 3);
	NATIVE("FExtractLong", FExtractLong, 2);
	NATIVE("FStuffLong", FStuffLong, 3);
	NATIVE("FExtractXLong", FExtractXLong, 2);
	NATIVE("FExtractCString", FExtractCString, 2);
	NATIVE("FStuffCString", FStuffCString, 3);
	NATIVE("FExtractPString", FExtractPString, 2);
	NATIVE("FStuffPString", FStuffPString, 3);
	NATIVE("FExtractBytes", FExtractBytes, 4);
}
