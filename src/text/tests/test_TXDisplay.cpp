// TXDisplay test (src/text/TXDisplay.h, TXHilite.h, Textension.h): a
// document put together from its handlers and filled by an edit, drawn
// through the display into an offscreen port, a caret placed by a tap, a
// selection hilited (and grown by dragging and by the arrows), a scroll,
// and typing; and a paginated document (TXPageFrames, TXPageFormatter)
// with a page break.  The ROM image is imported for its fonts and its
// U.S. locale bundle.
#include "Textension.h"
#include "TXNewtTextRun.h"
#include "TXRuler.h"
#include "TXRulerRange.h"
#include "TXChars.h"
#include "TXFrames.h"
#include "TXStream.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "Fonts.h"
#include "Text.h"
#include "Draw.h"
#include "Ports.h"
#include "Rects.h"
#include "Regions.h"
#include "Locale.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const ULong kUSABundle = 0x004a4d09;		// the ROM's locale bundle 'USA (as test_Dates)

const long kWidth = 128;
const long kHeight = 48;
const long kRowBytes = kWidth / 8;
static unsigned char gBits[kRowBytes * kHeight];
static PixelMap gMap;
static GrafPort gPort;


// A chunked storage over heap blocks (as test_TXLinesHeights's).
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
	UniChar*	fBlocks[256];
	long		fBlockCount;
};


// A pen that went down at one place and is dragged along a list of others.
class TestPen : public TXPointingDevice
{
public:
			TestPen(Point first) : fFirst(first), fCount(0), fAt(0)	{ }
	void	MoveTo(Point pt)					{ fPath[fCount++] = pt; }
	virtual Point	FirstLocation(void)			{ return fFirst; }
	virtual Point	CurrentLocation(void)		{ return fPath[fAt++]; }
	virtual Boolean	IsStillDown(void)			{ return fAt < fCount; }
	virtual long	GetDoubleClickTime(void)	{ return 30; }
	Point	fFirst;
	Point	fPath[8];
	long	fCount;
	long	fAt;
};


static Ref
FontSpec(const char* family, long size, long face)
{
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMfamily, RefVar(MakeSymbol((char*) family)));
	SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(size)));
	SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(face)));
	return spec;
}


static Point
Pt(short h, short v)
{
	Point pt;
	pt.h = h;
	pt.v = v;
	return pt;
}


static long
Ink(void)
{
	long n = 0;
	for (unsigned long i = 0; i < sizeof(gBits); i++)
		for (unsigned char b = gBits[i]; b != 0; b >>= 1)
			n += b & 1;
	return n;
}


static Boolean
Pixel(long x, long y)
{
	return (gBits[y * kRowBytes + x / 8] & (0x80 >> (x % 8))) != 0;
}


// The document, filled the way an editor fills one: an edit at nought.
static Textension*
MakeDocument(const char* s)
{
	TXHandlers handlers;
	handlers.fChars = new TestChars;
	Textension* doc = new Textension;
	EXPECT(doc->ITextension(&gPort, handlers, 2) == noErr);
	TXLongPoint size = { 0, kWidth };
	doc->fDisplay->fFrames->SetTextBoundsSize(size, nil, 0);
	RgnHandle view = NewRgn();
	RectRgn(view, &gMap.bounds);
	doc->fDisplay->SetViewRgn(view);
	DisposeRgn(view);
	UniChar buffer[512];
	long n = (long) strlen(s);
	for (long i = 0; i < n; i++)
		buffer[i] = (UniChar) s[i];
	TXTextDescriptor text;
	text.Set(buffer, n);
	TXReplaceParams params(text);
	EXPECT(doc->ReplaceRange(0, 0, &params) == noErr);
	return doc;
}


// What the document shows, drawn from scratch.
static void
DrawAll(Textension* doc, unsigned char* into)
{
	memset(gBits, 0, sizeof(gBits));
	doc->fDisplay->Draw(gMap.bounds);
	memcpy(into, gBits, sizeof(gBits));
}


static const char kText[] = "The quick brown fox jumps over the lazy dog\rA second paragraph follows it here";


static void
TestDocument()
{
	Textension* doc = MakeDocument(kText);
	long length = (long) strlen(kText);
	EXPECT(doc->fChars->Count() == length);
	EXPECT(doc->fRuns->GetLastRangeEnd() == length);
	TXFormatter* formatter = doc->fFormatter;
	EXPECT(formatter->fLastLine >= 3);							// wrapped into 128 pixels
	EXPECT(formatter->fLineEnds->GetRangeEnd(formatter->fLastLine) == length);
	int ascent, descent, leading;
	((TXNewtTextRun*) doc->fPendingRun)->GetHeightInfo(&ascent, &descent, &leading);
	long lineHeight = ascent + descent + leading;
	EXPECT(doc->fFrameFormatter->fTotalHeight == (formatter->fLastLine + 1) * lineHeight);

	// active, with the caret after what was put in
	doc->Activate(true, true);
	TXOffsetRange caret;
	doc->fHilite->GetHiliteRange(&caret);
	EXPECT(caret.fStart.fOffset == length && caret.fEnd.fOffset == length);

	// drawn: each line where the line itself would draw it
	unsigned char drawn[sizeof(gBits)];
	DrawAll(doc, drawn);
	EXPECT(Ink() > 100);
	unsigned char byLines[sizeof(gBits)];
	memset(gBits, 0, sizeof(gBits));
	for (long line = 0; line <= formatter->fLastLine && line * lineHeight < kHeight; line++)
	{
		doc->fDisplay->DoLineLayout(line);
		Rect r;
		SetRect(&r, 0, line * lineHeight, kWidth, (line + 1) * lineHeight);
		doc->fDisplay->fLine->Draw(r, ascent);
	}
	memcpy(byLines, gBits, sizeof(gBits));
	EXPECT(memcmp(drawn, byLines, sizeof(gBits)) == 0);

	// a tap on the second line: the caret where the line says
	Point tap = Pt(30, (short) (lineHeight + lineHeight / 2));
	TXOffsetRange expected;
	unsigned char outside, past;
	doc->fDisplay->PointToChar(tap, &expected, &outside, &past);
	EXPECT(expected.fStart.fOffset > formatter->fLineEnds->GetRangeEnd(0));
	TestPen pen(tap);
	doc->fHilite->fLastClickTime = 0;						// not a double tap
	doc->Click(&pen, 0, nil, nil, nil);
	doc->fHilite->GetHiliteRange(&caret);
	EXPECT(caret.fStart.fOffset == expected.fStart.fOffset && caret.fEnd.fOffset == expected.fStart.fOffset);
	EXPECT(doc->fHilite->fClickCount == 1);
	memset(gBits, 0, sizeof(gBits));
	doc->fDisplay->Draw(gMap.bounds);
	EXPECT(memcmp(gBits, drawn, sizeof(gBits)) == 0);		// a caret is not drawn by the hilite

	// a selection on the first line: exactly its rectangle inverted
	TXOffsetRange quick(4, 9, false, true);						// "quick"
	doc->SetHiliteRange(quick, true, true);
	memset(gBits, 0, sizeof(gBits));
	doc->fDisplay->Draw(gMap.bounds);
	Rect bounds;
	doc->GetRangeBounds(quick, &bounds);
	EXPECT(bounds.top == 0 && bounds.bottom == lineHeight && bounds.right > bounds.left);
	long wrong = 0;
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
		{
			Boolean before = (drawn[y * kRowBytes + x / 8] & (0x80 >> (x % 8))) != 0;
			Boolean inside = x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom;
			if (Pixel(x, y) != (before != inside))
				wrong++;
		}
	EXPECT(wrong == 0);
	// taken away again by a caret: the plain text back
	TXOffsetRange at9(9, 9, false, false);
	doc->SetHiliteRange(at9, true, true);
	EXPECT(memcmp(gBits, drawn, sizeof(gBits)) == 0);

	// dragged from "quick" to "fox": the selection follows the pen
	Rect q, f;
	TXOffsetRange quickStart(4, 4, false, false);
	TXOffsetRange foxEnd(19, 19, true, true);
	doc->GetRangeBounds(quickStart, &q);
	doc->GetRangeBounds(foxEnd, &f);
	TestPen drag(Pt(q.left + 1, lineHeight / 2));
	drag.MoveTo(Pt(q.left + 20, lineHeight / 2));
	drag.MoveTo(Pt(f.left - 1, lineHeight / 2));
	doc->fHilite->fLastClickTime = 0;
	doc->Click(&drag, 0, nil, nil, nil);
	TXOffsetRange dragged;
	doc->fHilite->GetHiliteRange(&dragged);
	EXPECT(dragged.fStart.fOffset == 4 && dragged.fEnd.fOffset == 19);

	// the arrows: right collapses to the end, then moves a character
	doc->fHilite->ArrowKey(0x1d, 0);
	doc->fHilite->GetHiliteRange(&caret);
	EXPECT(caret.fStart.fOffset == 19 && caret.fEnd.fOffset == 19);
	doc->fHilite->ArrowKey(0x1d, 0);
	doc->fHilite->GetHiliteRange(&caret);
	EXPECT(caret.fStart.fOffset == 20);
	doc->fHilite->ArrowKey(0x1c, kTXClickExtend);				// shift-left: a character selected
	doc->fHilite->GetHiliteRange(&caret);
	EXPECT(caret.fStart.fOffset == 19 && caret.fEnd.fOffset == 20);
	doc->fHilite->ArrowKey(0x1f, 0);							// down a line, at the same column
	doc->fHilite->GetHiliteRange(&caret);
	long line1 = doc->CharToLine(caret.fStart.fOffset, caret.fStart.fAtStart, nil);
	EXPECT(line1 == 1);

	// a scroll down a line: no further than the text reaches (it is
	// 4 lines, 56 pixels, in a 48-pixel view), and what the view shows then
	// the same as drawing it afresh
	TXLongPoint down = { -lineHeight, 0 };
	long below = doc->fDisplay->fFrames->GetTotalHeight() - kHeight;
	memcpy(gBits, drawn, sizeof(gBits));
	doc->fHilite->SetHiliteRange(at9, false, true);
	doc->fHilite->SetHiliteState(kTXHiliteOff);
	memcpy(gBits, drawn, sizeof(gBits));
	doc->fDisplay->Scroll(&down);
	EXPECT(below > 0 && below < lineHeight);
	EXPECT(down.v == -below);
	EXPECT(doc->fDisplay->fFrames->fScrollV == below);
	unsigned char scrolled[sizeof(gBits)];
	memcpy(scrolled, gBits, sizeof(gBits));
	DrawAll(doc, gBits);
	EXPECT(memcmp(scrolled, gBits, sizeof(gBits)) == 0);
	EXPECT(memcmp(scrolled, drawn + below * kRowBytes, sizeof(gBits) - below * kRowBytes) == 0);
	TXLongPoint tooFar = { 1000, 0 };							// back up, no further than the top
	doc->fDisplay->Scroll(&tooFar);
	EXPECT(tooFar.v == below && doc->fDisplay->fFrames->fScrollV == 0);

	// typing at the caret: the text grows, and the display matches afresh
	TXOffsetRange at4(4, 4, false, false);
	doc->SetHiliteRange(at4, true, true);
	UniChar typed[] = { 'v', 'e', 'r', 'y', ' ' };
	for (int i = 0; i < 5; i++)
		doc->KeyDown(&typed[i], 1, 0, doc->GetKeyDownFlags(typed[i]));
	EXPECT(doc->fChars->Count() == length + 5);
	EXPECT(doc->fChars->GetChar(4) == 'v' && doc->fChars->GetChar(9) == 'q');
	doc->fHilite->GetHiliteRange(&caret);
	EXPECT(caret.fStart.fOffset == 9);
	unsigned char afterTyping[sizeof(gBits)];
	memcpy(afterTyping, gBits, sizeof(gBits));
	doc->fHilite->SetHiliteState(kTXHiliteOff);
	DrawAll(doc, gBits);
	EXPECT(memcmp(afterTyping, gBits, sizeof(gBits)) == 0);	// the edit's redraw is the whole drawing
	// backspace takes one back
	UniChar bs = 8;
	doc->fHilite->SetHiliteState(kTXHiliteOn);
	EXPECT(doc->GetKeyDownFlags(bs) == 0x17);
	doc->KeyDown(&bs, 1, 0, doc->GetKeyDownFlags(bs));
	EXPECT(doc->fChars->Count() == length + 4 && doc->fChars->GetChar(8) == 'q');

	delete doc;
}


// A paginated document: pages of two lines each, the lines poured from
// one page into the next as the text grows and shrinks, and a page break
// (a character 10) ending its page early.
static void
TestPages()
{
	TXHandlers handlers;
	handlers.fChars = new TestChars;
	TXPageFrames* pages = new TXPageFrames;
	handlers.fFrames = pages;
	Textension* doc = new Textension;
	EXPECT(doc->ITextension(&gPort, handlers, 2) == noErr);
	EXPECT(pages->GetCountPages() == 1);					// the empty line's page
	int ascent, descent, leading;
	((TXNewtTextRun*) doc->fPendingRun)->GetHeightInfo(&ascent, &descent, &leading);
	long lineHeight = ascent + descent + leading;
	TXLongPoint size = { 2 * lineHeight + 3, kWidth };
	pages->SetTextBoundsSize(size, nil, 0);
	EXPECT(((TXPageFormatter*) pages->fFormatter)->fPageHeight == size.v);

	UniChar buffer[512];
	long n = (long) strlen(kText);
	for (long i = 0; i < n; i++)
		buffer[i] = (UniChar) kText[i];
	TXTextDescriptor text;
	text.Set(buffer, n);
	TXReplaceParams params(text);
	EXPECT(doc->ReplaceRange(0, 0, &params) == noErr);

	TXFormatter* formatter = doc->fFormatter;
	TXMultiFrameFormatter* frames = (TXMultiFrameFormatter*) pages->fFormatter;
	long lines = formatter->fLastLine + 1;
	EXPECT(lines >= 4);
	long count = pages->GetCountPages();
	EXPECT(count == (lines + 1) / 2);
	for (long page = 0; page < count; page++)
	{
		TXOffsetPair range;
		EXPECT(frames->GetFrameLineRange(page, &range));
		long last = 2 * page + 1 < lines ? 2 * page + 1 : lines - 1;
		EXPECT(range.fStart == 2 * page && range.fEnd == last);
		EXPECT(frames->GetFrameTextHeight(page) == (last - 2 * page + 1) * lineHeight);
		EXPECT(frames->LineToFrame(2 * page, false) == page);
	}
	TXOffsetPair none;
	EXPECT(!frames->GetFrameLineRange(count, &none));

	// the pages one under another, a 5-pixel gutter between
	Rect margins;
	pages->GetFramesMargins(&margins);
	long pageHeight = size.v + margins.top + margins.bottom;
	EXPECT(pages->GetPageHeight() == pageHeight);
	EXPECT(pages->GetTotalHeight() == count * (pageHeight + 5) - 5);
	EXPECT(pages->GetTotalWidth() == kWidth + margins.left + margins.right);
	TXLongRect bounds;
	pages->GetAbsTextBounds(1, &bounds);
	EXPECT(bounds.top == pageHeight + 5 + margins.top && bounds.bottom == bounds.top + size.v);
	TXLongPoint pt = { pageHeight + 6, 3 };
	EXPECT(pages->PointToNearestFrame(pt) == 1);
	pt.v = 0x7fff;
	EXPECT(pages->PointToNearestFrame(pt) == count - 1);
	TXPageCell cell;
	pages->PageNoToCell(3, &cell);
	EXPECT(cell.fRow == 3 && cell.fColumn == 0);

	// a line's worth taken out of the first page: the others come back
	TXOffset firstLineEnd = formatter->fLineEnds->GetRangeEnd(0);
	TXTextDescriptor nothing;
	nothing.Set(buffer, 0);
	TXReplaceParams remove(nothing);
	EXPECT(doc->ReplaceRange(0, firstLineEnd, &remove) == noErr);
	long fewer = formatter->fLastLine + 1;
	EXPECT(fewer == lines - 1);
	EXPECT(pages->GetCountPages() == (fewer + 1) / 2);
	for (long page = 0; page < pages->GetCountPages(); page++)
	{
		TXOffsetPair range;
		EXPECT(frames->GetFrameLineRange(page, &range));
		EXPECT(range.fStart == 2 * page);
	}

	// a page break after the first line's first word: the first page
	// ends with the line the break ends
	long before = pages->GetCountPages();
	TXOffset at = 0;
	while (doc->fChars->GetChar(at) != ' ')
		at++;
	UniChar lf = 10;
	TXTextDescriptor brk;
	brk.Set(&lf, 1);
	TXReplaceParams insert(brk);
	EXPECT(doc->ReplaceRange(at, at + 1, &insert) == noErr);
	EXPECT(frames->fPageBreaks != nil && frames->fPageBreaks->fCount == 1);
	EXPECT(*(long*) frames->fPageBreaks->GetElementPtr(0) == at);
	TXOffsetPair range;
	EXPECT(frames->GetFrameLineRange(0, &range));
	EXPECT(range.fStart == 0 && range.fEnd == 0);
	EXPECT(formatter->fLineEnds->GetRangeEnd(0) == at + 1);
	EXPECT(frames->GetFrameTextHeight(0) == lineHeight);
	EXPECT(pages->GetCountPages() >= before);
	long total = 0;
	for (long page = 0; page < pages->GetCountPages(); page++)
		total += frames->GetFrameTextHeight(page);
	EXPECT(total == frames->fTotalHeight);

	// laid out again from scratch, the pages come out the same
	long again = pages->GetCountPages();
	EXPECT(frames->Format() == noErr);
	EXPECT(pages->GetCountPages() == again);
	EXPECT(frames->GetFrameLineRange(0, &range) && range.fEnd == 0);

	// the page breaks go through a stream and come back
	TXHandleStream stream;
	EXPECT(frames->WriteToStream(&stream) == noErr);
	stream.SetPosition(0);
	delete frames->fPageBreaks;
	frames->fPageBreaks = nil;
	EXPECT(frames->ReadFromStream(&stream) == noErr);
	EXPECT(frames->fPageBreaks != nil && frames->fPageBreaks->fCount == 1 && *(long*) frames->fPageBreaks->GetElementPtr(0) == at);

	// the break taken out again: no table, and every change reflows
	TXTextDescriptor none2;
	none2.Set(buffer, 0);
	TXReplaceParams remove2(none2);
	EXPECT(doc->ReplaceRange(at, at + 1, &remove2) == noErr);
	EXPECT(doc->fChars->GetChar(at) != 10);
	EXPECT(frames->fPageBreaks == nil);
	EXPECT(frames->GetFrameLineRange(0, &range) && range.fEnd == 1);

	delete doc;
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_TXDisplay: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	InitFonts();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), RefVar(GetArraySlotRef(list, i)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfonts, fonts);
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(TranslateROMRef(kUSABundle)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	RefVar config(AllocateFrame());
	SetFrameSlot(config, RSSYMuserfont, RefVar(FontSpec("espy", 10, 0)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, config);
	InitInternationalUtils();

	gMap.baseAddr = (Ptr) gBits;
	gMap.rowBytes = kRowBytes;
	SetRect(&gMap.bounds, 0, 0, kWidth, kHeight);
	gMap.pixMapFlags = kPixMapPtr | 1;
	gMap.deviceRes.v = kDefaultDPI;
	gMap.deviceRes.h = kDefaultDPI;
	gMap.grayTable = nil;
	OpenPort(&gPort);
	SetPortBits(&gMap);
	gPort.portRect = gMap.bounds;
	RectRgn(gPort.visRgn, &gMap.bounds);

	newton_try
	{
		// what the Newton's start-up does: the engine, its default run and ruler
		EXPECT(Textension::TextensionStart() == noErr);
		Textension::RegisterRun(new TXNewtTextRun);
		Textension::RegisterRuler(new TXAdvancedRuler);
		TestDocument();
		TestPages();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	printf("test_TXDisplay: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
