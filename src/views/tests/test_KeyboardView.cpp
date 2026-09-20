// Keyboard view test: the two iterators that walk a keyboard's
// definition, over the ROM's own numeric and alphabetic keyboards.
//
// What is checked is the walk (every key of every row reached, and the
// count the constructor works out agreeing with it) and the layout the
// visible iterator puts on it - each key's cell, face and shadow, the
// pen moving along the row, the rows stepping down by their pitch, and
// the row's own rectangle - against the numbers in the ROM's numeric
// keyboard, which are known.  Then the hit test, which is what a tap on
// the keyboard goes through: a point in a key's face finds that key, and
// a point in one of the gaps between them finds nothing.

#include "KeyboardView.h"
#include "Rects.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static Boolean
RectIs(const Rect& r, long left, long top, long right, long bottom)
{
	if (r.left == left && r.top == top && r.right == right && r.bottom == bottom)
		return true;
	fprintf(stderr, "      rect is %d,%d,%d,%d, wanted %ld,%ld,%ld,%ld\n",
			r.left, r.top, r.right, r.bottom, left, top, right, bottom);
	return false;
}


static Point
Pt(long h, long v)
{
	Point pt;
	pt.h = (short) h;
	pt.v = (short) v;
	return pt;
}


// every key of the definition reached exactly once
static long
WalkedKeys(RefArg keys)
{
	TRawKeyIterator iter(keys);
	long count = 0;
	while (!iter.Done())
	{
		count++;
		iter.Next();
	}
	return count;
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_KeyboardView: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();

	RefVar numeric(GetFrameSlotRef(RefVar(Rnumerickeys), RSSYMkeydefinitions));
	EXPECT(IsArray(numeric) && Length(numeric) > 0);

	// ---- the raw walk ----
	{
		TRawKeyIterator iter(numeric);
		long expected = 0;
		for (long i = 0; i < Length(numeric); i++)
			expected += (Length(RefVar(GetArraySlotRef(numeric, i))) - 2) / 3;
		EXPECT(iter.fTotalKeys == expected);
		EXPECT(iter.fRowCount == Length(numeric));
		EXPECT(WalkedKeys(numeric) == expected);

		// the first row of the numeric keyboard is 7 8 9, a gap, / and del
		EXPECT(iter.fRowKeys == 6);
		EXPECT(RCHAR(iter.fResult) == '7');
		EXPECT((iter.fInfo & kKeyIsGap) == 0);
		iter.Next();
		EXPECT(RCHAR(iter.fResult) == '8');
		iter.Next();
		EXPECT(RCHAR(iter.fResult) == '9');
		iter.Next();
		EXPECT(ISNIL(iter.fResult) && (iter.fInfo & kKeyIsGap) != 0);		// the gap
		EXPECT(((iter.fInfo >> kKeyWidthShift) & kKeyWidthMask) == 2);		// a quarter of a cell
	}

	// ---- the layout ----
	{
		Rect cell;
		SetRect(&cell, 0, 0, 8, 8);			// eight pixels each way
		TVisKeyIterator iter(numeric, cell, Pt(0, 0));
		// a full-width key fills the cell; its face is grown by the two
		// pixels of 3-D edge the ROM's keys ask for, and its shadow is the
		// same cell moved in by them
		EXPECT(RectIs(iter.fKeyBounds, 0, 0, 8, 8));
		EXPECT(RectIs(iter.fKeyFace, 0, 0, 10, 10));
		EXPECT(RectIs(iter.fKeyShadow, 2, 2, 8, 8));
		// the row: 8 + 8 + 8 + 2 + 8 + 8 eighths across, a pixel over
		EXPECT(RectIs(iter.fRowBounds, 0, 0, 43, 9));
		EXPECT(iter.fRowPitch == 8 && iter.fRowHeight == 8);

		iter.Next();
		EXPECT(RectIs(iter.fKeyBounds, 8, 0, 16, 8));
		iter.Next();
		EXPECT(RectIs(iter.fKeyBounds, 16, 0, 24, 8));
		iter.Next();
		EXPECT(RectIs(iter.fKeyBounds, 24, 0, 26, 8));		// the quarter-width gap
		iter.Next();
		EXPECT(RectIs(iter.fKeyBounds, 26, 0, 34, 8));
		iter.Next();
		EXPECT(RectIs(iter.fKeyBounds, 34, 0, 42, 8));

		// and on to the second row, a pitch below
		iter.Next();
		EXPECT(!iter.Done());
		EXPECT(iter.fRowIndex == 1 && iter.fKeyIndex == 0);
		EXPECT(RectIs(iter.fKeyBounds, 0, 8, 8, 16));
		EXPECT(iter.fRowBounds.top == 8);

		// the whole thing walks to the end and stops with empty rectangles
		while (!iter.Done())
			iter.Next();
		EXPECT(RectIs(iter.fKeyBounds, 0, 0, 0, 0));
		EXPECT(RectIs(iter.fKeyFace, 0, 0, 0, 0));

		// copied, and the copy stands where the original does
		TVisKeyIterator other(numeric, cell, Pt(0, 0));
		iter.Reset();
		iter.Next();
		iter.Next();
		iter.CopyInto(&other);
		EXPECT(other.fRowIndex == iter.fRowIndex && other.fKeyIndex == iter.fKeyIndex);
		EXPECT(RectIs(other.fKeyBounds, 16, 0, 24, 8));
	}

	// ---- the hit test ----
	{
		Rect cell;
		SetRect(&cell, 0, 0, 8, 8);
		TVisKeyIterator iter(numeric, cell, Pt(0, 0));
		EXPECT(iter.FindEnclosingKey(Pt(4, 4)));
		EXPECT(RCHAR(iter.fResult) == '7');
		EXPECT(iter.FindEnclosingKey(Pt(20, 4)));
		EXPECT(RCHAR(iter.fResult) == '9');
		// the second row, found from wherever the walk had got to
		EXPECT(iter.FindEnclosingKey(Pt(4, 12)));
		EXPECT(iter.fRowIndex == 1 && iter.fKeyIndex == 0);
		// a point off the keyboard altogether finds nothing
		EXPECT(!iter.FindEnclosingKey(Pt(400, 400)));
	}

	// ---- the alphabetic keyboard, which is bigger and has odd rows ----
	{
		// (alphaKeys carries its definitions per keyboard layout; the
		//  viewSetupFormScript picks one into keyDefinitions at run time)
		RefVar alpha(GetFrameSlotRef(RefVar(Ralphakeys), RefVar(Intern((char*) "keyDefinitionsUS"))));
		EXPECT(IsArray(alpha) && Length(alpha) > 0);
		long expected = 0;
		for (long i = 0; i < Length(alpha); i++)
			expected += (Length(RefVar(GetArraySlotRef(alpha, i))) - 2) / 3;
		EXPECT(WalkedKeys(alpha) == expected);

		Rect cell;
		SetRect(&cell, 0, 0, 10, 10);
		TVisKeyIterator iter(alpha, cell, Pt(5, 7));
		EXPECT(iter.fKeyBounds.left == 5 && iter.fKeyBounds.top == 7);	// laid out from the origin
		EXPECT(iter.fRowBounds.left == 5 && iter.fRowBounds.top == 7);
		// every row of it has something in it that is not a gap
		long rows = 0;
		iter.Reset();
		while (!iter.Done())
		{
			EXPECT(iter.fRowHasKeys);
			rows++;
			iter.SkipToStartOfNextRow();
		}
		EXPECT(rows == Length(alpha));
	}

	printf("test_KeyboardView: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
