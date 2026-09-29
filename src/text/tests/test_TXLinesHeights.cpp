// The lines' heights (TXLinesHeights.h): lines added, given heights and
// taken out, the groups of equal lines split and joined, the heights of
// stretches of lines, and the line a pixel falls on; the reflow lines;
// and a paragraph's control characters.
#include "TXLinesHeights.h"
#include "OSErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static TXLineHeightInfo
Info(long height)
{
	TXLineHeightInfo info = { height, height };
	return info;
}


// the groups as (count, height) pairs
static Boolean
GroupsAre(const TXLinesHeights& lh, const long* pairs, long n)
{
	if (lh.GetCount() != n)
		return false;
	for (long i = 0; i < n; i++)
	{
		const TXLineHeightGroup* g = (const TXLineHeightGroup*) lh.GetElementPtr(i);
		if (g->fCount != pairs[2 * i] || g->fHeight != pairs[2 * i + 1])
			return false;
	}
	return true;
}


static void
TestHeights()
{
	TXLinesHeights lh;
	EXPECT(lh.fLastLine == -1 && lh.GetLinesHeight(0, 0) == 0);

	// ten lines of 12: one group
	for (long i = 0; i < 10; i++)
		EXPECT(lh.InsertLineHeightInfo(Info(12), -1) == noErr);
	static const long one[] = { 10, 12 };
	EXPECT(GroupsAre(lh, one, 1));
	EXPECT(lh.fLastLine == 9 && lh.fTotalHeight == 120);
	EXPECT(lh.GetLinesHeight(0, 9) == 120);
	EXPECT(lh.GetLinesHeight(2, 4) == 36);

	// line 5 taller: split in three
	EXPECT(lh.SetLineHeightInfo(Info(20), 5, nil) == noErr);
	static const long three[] = { 5, 12, 1, 20, 4, 12 };
	EXPECT(GroupsAre(lh, three, 3));
	EXPECT(lh.fTotalHeight == 128);
	EXPECT(lh.GetLinesHeight(4, 6) == 12 + 20 + 12);
	EXPECT(lh.GetLinesHeight(0, 9) == 128);
	EXPECT(lh.GetLinesHeight(3, 8) == 12 * 2 + 20 + 12 * 3);	// across three groups
	TXLineHeightInfo got;
	lh.GetLineHeightInfo(5, &got);
	EXPECT(got.fHeight == 20);
	lh.GetLineHeightInfo(6, &got);
	EXPECT(got.fHeight == 12);

	// line 6 taller too: leaves the group after for the equal one before
	EXPECT(lh.SetLineHeightInfo(Info(20), 6, nil) == noErr);
	static const long moved[] = { 5, 12, 2, 20, 3, 12 };
	EXPECT(GroupsAre(lh, moved, 3));

	// back to 12, both: the groups run together again
	EXPECT(lh.SetLineHeightInfo(Info(12), 6, nil) == noErr);
	EXPECT(lh.SetLineHeightInfo(Info(12), 5, nil) == noErr);
	EXPECT(GroupsAre(lh, one, 1));
	EXPECT(lh.fTotalHeight == 120);

	// the same height again: nothing to do
	EXPECT(lh.SetLineHeightInfo(Info(12), 3, nil) == noErr);
	EXPECT(GroupsAre(lh, one, 1));

	// taking lines out: a group emptied goes, its neighbours are joined
	lh.SetLineHeightInfo(Info(20), 5, nil);
	lh.RemoveLines(1, 5, nil);
	static const long nine[] = { 9, 12 };
	EXPECT(GroupsAre(lh, nine, 1));
	EXPECT(lh.fLastLine == 8 && lh.fTotalHeight == 108);
	lh.RemoveLines(3, 2, nil);
	static const long six[] = { 6, 12 };
	EXPECT(GroupsAre(lh, six, 1));
	EXPECT(lh.fTotalHeight == 72);
	lh.RemoveLines(10, 0, nil);								// more than there are: all gone
	EXPECT(lh.GetCount() == 0 && lh.fTotalHeight == 0 && lh.fLastLine < 0);

	// a line inserted in the middle takes its neighbours' height first
	lh.FreeData();
	for (long i = 0; i < 4; i++)
		lh.InsertLineHeightInfo(Info(10), -1);
	EXPECT(lh.InsertLineHeightInfo(Info(30), 2) == noErr);
	static const long mid[] = { 2, 10, 1, 30, 2, 10 };
	EXPECT(GroupsAre(lh, mid, 3));
	EXPECT(lh.fTotalHeight == 70 && lh.fLastLine == 4);
}


static void
TestPixelToLine()
{
	TXLinesHeights lh;
	for (long i = 0; i < 3; i++)
		lh.InsertLineHeightInfo(Info(10), -1);				// lines 0-2: 10 each
	lh.InsertLineHeightInfo(Info(20), -1);					// line 3: 20
	lh.InsertLineHeightInfo(Info(10), -1);					// line 4: 10

	long px = 25;											// 25 down from the top: in line 2, whose top is 20
	TXLineHeightGroup* g;
	long place;
	long line = lh.PixelToLine(&px, 0, &g, &place);
	EXPECT(line == 2 && px == 20 && place == 2);
	px = 35;												// in line 3 (top 30)
	line = lh.PixelToLine(&px, 0, &g, &place);
	EXPECT(line == 3 && px == 30 && place == 0 && g->fHeight == 20);
	px = 30;												// exactly the top of line 3
	line = lh.PixelToLine(&px, 0, &g, &place);
	EXPECT(line == 3 && px == 30 && place == 0 && g->fHeight == 20);
	px = 0;
	EXPECT(lh.PixelToLine(&px, 4, nil, nil) == 4);
	px = 500;												// past the end: the line after the last
	line = lh.PixelToLine(&px, 0, &g, &place);
	EXPECT(line == 5 && px == 60);

	long pixels = 25;
	const TXLineHeightGroup* first = (const TXLineHeightGroup*) lh.GetElementPtr(0);
	EXPECT(lh.HeightToCountLines(*first, 0, &pixels) == 3 && pixels == -5);
	pixels = 40;
	EXPECT(lh.HeightToCountLines(*first, 1, &pixels) == 2 && pixels == 20);
}


static void
TestReflowLines()
{
	TXFormatReflowLines r;
	r.Reset();
	long line;
	EXPECT(!r.GetFirst(&line) && line == -1);
	EXPECT(!r.GetLast(&line));
	EXPECT(r.fFlag08 && !r.fFlag09);
	r.fFirst = 3;
	r.fLast = 7;
	EXPECT(r.GetFirst(&line) && line == 3);
	EXPECT(r.GetLast(&line) && line == 7);
}


// A chunked storage over heap blocks (as test_TXStream's).
class TestChars : public TXChunkedChars
{
public:
			TestChars() : TXChunkedChars(8), fBlockCount(0)	{ memset(fBlocks, 0, sizeof(fBlocks)); }
	virtual	~TestChars()
			{
				for (long i = 0; i < fBlockCount; i++)
					delete[] fBlocks[i];
			}
	virtual UniChar* GetChunkPtr(long chunk, Boolean, Boolean)	{ return fBlocks[chunk]; }
	virtual NewtonErr AllocateChunks(long at, long count)
			{
				for (long i = fBlockCount - 1; i >= at; i--)
					fBlocks[i + count] = fBlocks[i];
				for (long i = 0; i < count; i++)
					fBlocks[at + i] = new UniChar[fChunkSize];
				fBlockCount += count;
				return noErr;
			}
	virtual void RemoveChunks(long at, long count)
			{
				for (long i = 0; i < count; i++)
					delete[] fBlocks[at + i];
				for (long i = at; i + count < fBlockCount; i++)
					fBlocks[i] = fBlocks[i + count];
				fBlockCount -= count;
			}
	void	SetText(const char* text)
			{
				UniChar buffer[128];
				long n = (long) strlen(text);
				for (long i = 0; i < n; i++)
					buffer[i] = (UniChar) text[i];
				TXTextDescriptor source;
				source.Set(buffer, n);
				Replace(0, Count(), &source);
			}
	UniChar*	fBlocks[64];
	long		fBlockCount;
};


static void
TestCtrlChars()
{
	TestChars chars;
	chars.SetText("ab\tcd\tef\ngh\tij");					// tabs at 2 and 5, the break at 8, a tab at 11
	TXParagCtrlChars ctrl;
	ctrl.Define(&chars, 0, chars.Count());
	EXPECT(ctrl.fCount == 3);								// stops at the break
	EXPECT(ctrl.fOffsets[0] == 2 && ctrl.fChars[0] == '\t');
	EXPECT(ctrl.fOffsets[1] == 5 && ctrl.fChars[1] == '\t');
	EXPECT(ctrl.fOffsets[2] == 8 && ctrl.fChars[2] == '\n');
	EXPECT(ctrl.fEnd == 9);									// just past the break
	EXPECT(ctrl.GetCurrCtrlOffset() == 2 && ctrl.GetCurrCtrlChar() == '\t');
	ctrl.fCurrent = 3;
	EXPECT(ctrl.GetCurrCtrlOffset() == -1 && ctrl.GetCurrCtrlChar() == 0);

	// from the second paragraph: offsets from where it starts
	ctrl.Define(&chars, 9, chars.Count());
	EXPECT(ctrl.fCount == 1 && ctrl.fOffsets[0] == 2);
	EXPECT(ctrl.GetCurrCtrlOffset() == 11);
	EXPECT(ctrl.fEnd == chars.Count());						// no break: to the end

	ctrl.Invalid();
	EXPECT(ctrl.fEnd == 0 && ctrl.fCurrent == 0);
}


int
main()
{
	InitHostStandaloneHeap();
	TestHeights();
	TestPixelToLine();
	TestReflowLines();
	TestCtrlChars();
	if (failures == 0)
		printf("test_TXLinesHeights: all passed\n");
	return failures != 0;
}
