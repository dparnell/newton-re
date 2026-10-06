// TXFrames test (src/text/TXFrames.h, TXFrameFormatter.h): the one frame
// of a view - its text rectangle inside the margins, the absolute and draw
// coordinates and the origins between them, which line a point is on, a
// line's rectangle, the bands of equal lines a rectangle crosses, the total
// height; the frames a rectangle crosses, and the note an edit leaves of
// how a frame's text changed height.
#include "TXFrames.h"
#include "Rects.h"
#include "memory/host/KernelHeap.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
AddLines(TXFrameFormatter* formatter, long count, long height, long ascent)
{
	for (long i = 0; i < count; i++)
	{
		TXLineHeightInfo info = { height, ascent };
		TXFormatReflowLines reflow;
		reflow.Reset();
		EXPECT(formatter->InsertLine(info, &reflow, -1) == noErr);
	}
}


static void
TestMonoFrame()
{
	TXMonoFrame* frames = new TXMonoFrame;
	TXFrameFormatter* formatter = frames->fFormatter;
	EXPECT(formatter != nil && formatter->GetCountFrames() == 1);
	Rect margins;
	SetRect(&margins, 6, 4, 10, 8);								// left 6, top 4, right 10, bottom 8
	TXDisplayChanges changes;
	EXPECT(changes.fFlags == 0 && changes.fFormatStart == 0x7fffffff);
	frames->SetFramesMargins(margins, &changes);
	EXPECT(changes.fFlags == 0x18);
	changes.fFlags = 0;
	frames->SetFramesMargins(margins, &changes);				// the same: nothing
	EXPECT(changes.fFlags == 0);

	// a width and no height: unbounded
	TXLongPoint size = { 0, 100 };
	frames->SetTextBoundsSize(size, &changes, 0);
	EXPECT(changes.fFlags == 1);								// the width changed
	EXPECT(frames->fSize.v == 0x40000000 && frames->fSize.h == 100);
	EXPECT(frames->GetLineFormatWidth(0) == 100 && frames->GetLineMaxWidth(3) == 100);
	TXLongRect text;
	frames->GetAbsTextBounds(0, &text);
	EXPECT(text.top == 4 && text.left == 6 && text.bottom == 4 + 0x40000000 && text.right == 106);
	TXLongRect whole;
	frames->GetAbsFrameBounds(0, &whole);
	EXPECT(whole.top == 0 && whole.left == 0 && whole.right == 116);

	// three lines of 12 and two of 20
	AddLines(formatter, 3, 12, 9);
	AddLines(formatter, 2, 20, 15);
	EXPECT(formatter->fTotalHeight == 76 && formatter->fLastLine == 4);
	EXPECT(formatter->GetFrameTextHeight(0) == 76);
	TXOffsetPair range;
	EXPECT(formatter->GetFrameLineRange(0, &range) && range.fStart == 0 && range.fEnd == 4);
	EXPECT(frames->GetTotalHeight() == 76 + 4 + 8);				// the lines and the margins
	EXPECT(frames->GetTotalWidth() == 116);

	// which line a point is on
	unsigned char outside, past;
	Point pt = { 4 + 30, 20 };									// v, h
	EXPECT(frames->PointToLine(pt, &outside, &past) == 2 && !outside && !past);
	pt.v = 4 + 36;
	EXPECT(frames->PointToLine(pt, &outside, &past) == 3);
	pt.v = 4 + 200;												// past the last line
	EXPECT(frames->PointToLine(pt, &outside, &past) == 4 && past);
	pt.v = 1;													// above the text, in the margin
	EXPECT(frames->PointToLine(pt, &outside, &past) == 0 && outside);

	// a line's rectangle
	TXLongRect line;
	EXPECT(frames->GetLineBounds(3, &line));
	EXPECT(line.top == 4 + 36 && line.bottom == 4 + 56 && line.left == 6 && line.right == 106);
	Rect drawn;
	EXPECT(frames->GetLineBounds(0, &drawn));
	EXPECT(drawn.top == 4 && drawn.bottom == 16);

	// the origins: scrolled down 10, drawn from (2, 3) in the port
	frames->FramesScrolled(0, -10);								// the scroll position is decreased by the amount scrolled
	EXPECT(frames->fScrollV == 10);
	frames->SetDrawOrigin(2, 3);
	EXPECT(frames->VAbsToDraw(40) == 40 - 10 - 3 && frames->HAbsToDraw(6) == 6 - 2);
	EXPECT(frames->VDrawToAbs(27) == 40 && frames->HDrawToAbs(4) == 6);
	EXPECT(frames->VAbsToDraw(0x40000000) == 0x7fff);			// clipped to a QuickDraw coordinate
	TXLongPoint abs = { 40, 6 };
	Point back = frames->AbsToDraw(abs);
	EXPECT(back.v == 27 && back.h == 4);
	frames->FramesScrolled(0, 10);
	frames->SetDrawOrigin(0, 0);

	// the lines a rectangle crosses, as bands of equal lines
	TXArray bands(sizeof(TXSectLine), 4);
	Rect r;
	SetRect(&r, 0, 0, 200, 70);
	long first = -1, count = -1;
	EXPECT(frames->SectLines(&r, 0, &first, &count, &bands));
	EXPECT(r.top == 4 && r.left == 6 && r.bottom == 70 && r.right == 106);	// clipped to the text
	EXPECT(first == 0 && count == 5);
	EXPECT(bands.GetCount() == 2);
	TXSectLine* a = (TXSectLine*) bands.GetElementPtr(0);
	EXPECT(a->fLine == 0 && a->fCount == 3 && a->fAscent == 9);
	EXPECT(a->fRect.top == 4 && a->fRect.bottom == 16 && a->fRect.left == 6 && a->fRect.right == 106);
	TXSectLine* b = (TXSectLine*) bands.GetElementPtr(1);
	EXPECT(b->fLine == 3 && b->fCount == 2 && b->fAscent == 15);
	EXPECT(b->fRect.top == 40 && b->fRect.bottom == 60);
	// starting part way down, past the last line
	SetRect(&r, 0, 30, 200, 120);
	EXPECT(frames->SectLines(&r, 0, &first, &count, &bands));
	EXPECT(first == 2 && count == 3 && bands.GetCount() == 3);
	TXSectLine* c = (TXSectLine*) bands.GetElementPtr(2);
	EXPECT(c->fCount == 0 && c->fRect.top == 80 && c->fRect.bottom == 120);	// the rest, below the text
	SetRect(&r, 200, 0, 300, 50);								// beside the text: nothing
	EXPECT(!frames->SectLines(&r, 0, &first, &count, &bands));

	// a height of its own: the frame is as tall as that
	TXLongPoint fixed = { 200, 100 };
	changes.fFlags = 0;
	frames->SetTextBoundsSize(fixed, &changes, 0);
	EXPECT(changes.fFlags == 2);								// only the height
	EXPECT(frames->GetTotalHeight() == 200 + 4 + 8);

	// an edit's note: the frame's text grew by a line
	formatter->BeginEdit();
	TXFrameEditInfo* info = formatter->CatchFrame(0);
	EXPECT(info->fFrame == 0 && info->fHeightChange == 76);
	AddLines(formatter, 1, 12, 9);
	gFramesEditInfo.SetEditFlag(4, 0, 1);
	info = formatter->GetNextFrameEditInfo();
	EXPECT(info != nil && info->fHeightChange == 12 && info->fFlags == 4);
	EXPECT(formatter->GetNextFrameEditInfo() == nil);
	TXFrameEditInfo* found;
	EXPECT(!gFramesEditInfo.GetEditInfoPtr(0, &found, 4) && found == info);
	EXPECT(gFramesEditInfo.GetEditInfoPtr(0, &found, 1));
	formatter->EndEdit();
	EXPECT(gFramesEditInfo.fCount == 0);

	// four frames caught (a view showing four pages): the ROM's object has
	// room for two and the rest overwrite it and what follows it (the ROM
	// bug); fixed, every one is kept, found and flagged, and the paragraph
	// control characters after it are left alone
	{
		SetRomBugFixed(true);
		unsigned char ctrl[sizeof(gTXParagCtrlChars)];
		memcpy(ctrl, &gTXParagCtrlChars, sizeof(ctrl));
		formatter->BeginEdit();
		for (long f = 0; f < 4; f++)
			EXPECT(formatter->CatchFrame(f) != nil);
		EXPECT(gFramesEditInfo.fCount == 4 && gFramesEditInfo.fFirst == 0 && gFramesEditInfo.fLast == 3);
		gFramesEditInfo.SetEditFlag(4, 2, 0x7fffffff);
		for (long f = 0; f < 4; f++)
		{
			TXFrameEditInfo* e;
			EXPECT(gFramesEditInfo.GetEditInfoPtr(f, &e, 0) && e != nil && e->fFrame == f);
			EXPECT(e != nil && (e->fFlags == 4) == (f >= 2));
		}
		long seen = 0;
		for (TXFrameEditInfo* e = formatter->GetNextFrameEditInfo(); e != nil; e = formatter->GetNextFrameEditInfo())
			EXPECT(e->fFrame == seen++);
		EXPECT(seen == 4);
		formatter->EndEdit();
		EXPECT(memcmp(ctrl, &gTXParagCtrlChars, sizeof(ctrl)) == 0);
	}

	// one frame, 0x7fff tall
	EXPECT(formatter->GetFrameHeight(0) == 0x7fff);
	EXPECT(!formatter->TestFrameOverflow(0, 10));
	EXPECT(formatter->TestFrameOverflow(0, 0x8000));

	delete frames;
}


static void
TestSectFrames()
{
	TXSectFrames one;
	one.SetUniform(0, 1, 0, 1);
	EXPECT(one.GetNextFrame() == 0 && one.GetNextFrame() == -1);

	// two to a row, rows four apart, frames 1 to 7
	TXSectFrames grid;
	grid.SetUniform(1, 2, 7, 4);
	EXPECT(grid.GetNextFrame() == 1);
	EXPECT(grid.GetNextFrame() == 2);
	EXPECT(grid.GetNextFrame() == 5);
	EXPECT(grid.GetNextFrame() == 6);
	EXPECT(grid.GetNextFrame() == -1);
}


int
main()
{
	InitHostStandaloneHeap();
	TestMonoFrame();
	TestSectFrames();
	printf("test_TXFrames: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
