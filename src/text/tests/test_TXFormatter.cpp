// TXFormatter test (src/text/TXFormatter.h): a paragraph of styled text
// broken into lines in the one frame of a view, checked word by word
// against the run's own measurements; then reflowed after an insertion
// and after a deletion, the lines compared with formatting the edited
// text from scratch; the empty last line after a line break; tabs and a
// line without wrapping; and the rulers brought within a narrow width.
// The ROM image is imported for its fonts and its U.S. locale bundle.
#include "TXFormatter.h"
#include "TXStyledText.h"
#include "TXNewtTextRun.h"
#include "TXRuler.h"
#include "TXRulerRange.h"
#include "TXChars.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "Fonts.h"
#include "Text.h"
#include "Ports.h"
#include "Regions.h"
#include "Locale.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const ULong kUSABundle = 0x004a4d09;		// the ROM's locale bundle 'USA (as test_Dates)
const long kLineWidth = 80;

static GrafPort gPort;
static unsigned char gBits[16 * 16];
static PixelMap gMap;


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
	void	Put(long at, long count, const char* text)
			{
				UniChar buffer[512];
				long n = (long) strlen(text);
				for (long i = 0; i < n; i++)
					buffer[i] = (UniChar) text[i];
				TXTextDescriptor source;
				source.Set(buffer, n);
				Replace(at, count, &source);
			}
	UniChar*	fBlocks[256];
	long		fBlockCount;
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


// A document in the one frame of a view: one run, one ruler.
struct Document
{
	Document(const char* s, long width = kLineWidth)
	{
		chars = new TestChars;
		chars->Put(0, 0, s);
		long length = chars->Count();
		text = new TXStyledText;
		text->IStyledText(&gPort, chars, 2);
		run = new TXNewtTextRun;
		text->fRuns->InsertObjectRange(0, length, run, false);
		ruler = new TXAdvancedRuler;
		rulers = new TXRulerRange(chars, new TXAdvancedRuler);
		rulers->InsertObjectRange(0, length, ruler, false);
		frames = new TXMonoFrame;
		TXLongPoint size = { 0, width };
		frames->SetTextBoundsSize(size, nil, 0);
		formatter = new TXFormatter;
		formatter->SetHandlers(text, frames, rulers, 2);
	}
	~Document()
	{
		delete formatter;
		delete frames;
		delete rulers;
		delete text;
	}
	// the characters changed: the one run and the one ruler cover them all
	void	Edit(long at, long count, const char* s)
	{
		chars->Put(at, count, s);
		text->fRuns->SetRangeEnd(0, chars->Count());
		rulers->SetRangeEnd(0, chars->Count());
	}
	long	Lines(void)				{ return formatter->fLastLine + 1; }
	TXOffset End(long line)			{ return formatter->fLineEnds->GetRangeEnd(line); }
	TXOffset Start(long line)		{ return formatter->fLineEnds->GetRangeStart(line); }

	TestChars*		chars;
	TXStyledText*	text;
	TXNewtTextRun*	run;
	TXAdvancedRuler* ruler;
	TXRulerRange*	rulers;
	TXMonoFrame*	frames;
	TXFormatter*	formatter;
};


// the run's width of chars [from, to), in whole pixels rounded up
static long
Width(Document& doc, long from, long to)
{
	UniChar buffer[512];
	for (long i = from; i < to; i++)
		buffer[i - from] = doc.chars->GetChar(i);
	TXLineRunDisplayInfo info = { buffer, to - from, 0, 0 };
	Fixed w = doc.run->MeasureWidth(info);
	return (w + 0xffff) >> 16;
}


static Boolean
IsSpace(Document& doc, long at)		{ return doc.chars->GetChar(at) == ' '; }


// Every line fits (its trailing spaces aside), the next word would not
// have, and the lines follow one another to the end of the text.
static void
CheckLines(Document& doc, const char* what)
{
	long count = doc.chars->Count();
	TXOffset start = 0;
	for (long line = 0; line < doc.Lines(); line++)
	{
		TXOffset end = doc.End(line);
		EXPECT(doc.Start(line) == start);
		long visible = end;
		while (visible > start && (IsSpace(doc, visible - 1) || doc.chars->GetChar(visible - 1) == '\r'))
			visible--;
		if (Width(doc, start, visible) > kLineWidth)
		{
			failures++;
			fprintf(stderr, "FAIL %s: line %ld [%ld,%ld) too wide\n", what, line, start, end);
		}
		if (end < count && doc.chars->GetChar(end - 1) != '\r')
		{
			long next = end;
			while (next < count && !IsSpace(doc, next) && doc.chars->GetChar(next) != '\r')
				next++;
			// (and the space after it: a fitted length ending exactly on a
			// word's end lands FindWordBreaks on the space, which the host's
			// FindWordBreaks - qd/Text.cpp's DEVIATION, no break table yet -
			// takes as the end of the word before it, so the line breaks in
			// front of that word)
			if (next < count)
				next++;
			if (Width(doc, start, next) <= kLineWidth)
			{
				failures++;
				fprintf(stderr, "FAIL %s: line %ld [%ld,%ld) could have taken the next word (%ld wide to %ld)\n", what, line, start, end, Width(doc, start, next), next);
			}
		}
		start = end;
	}
	EXPECT(start == count);
}


static void
SameLines(Document& a, Document& b)
{
	EXPECT(a.Lines() == b.Lines());
	for (long i = 0; i < a.Lines() && i < b.Lines(); i++)
		EXPECT(a.End(i) == b.End(i));
	EXPECT(a.formatter->fFrameFormatter->fTotalHeight == b.formatter->fFrameFormatter->fTotalHeight);
}


static const char kText[] = "The quick brown fox jumps over the lazy dog while the cat watches from the old stone wall\rSecond paragraph here";


static void
TestParagraph()
{
	Document doc(kText);
	EXPECT(doc.Lines() == 1 && doc.End(0) == 0);				// SetHandlers's empty line
	long first = -2, last = -2;
	EXPECT(doc.formatter->Format(0, -1, &first, &last) == noErr);
	EXPECT(first == 0 && last == doc.formatter->fLastLine);
	EXPECT(doc.Lines() >= 5);
	CheckLines(doc, "formatted");
	// the paragraph break ends a line
	long para = strchr(kText, '\r') - kText + 1;
	Boolean found = false;
	for (long i = 0; i < doc.Lines(); i++)
		found |= doc.End(i) == para;
	EXPECT(found);
	// every line one height: the run's
	int ascent, descent, leading;
	doc.run->GetHeightInfo(&ascent, &descent, &leading);
	long height = ascent + descent + leading;
	EXPECT(doc.formatter->fFrameFormatter->fTotalHeight == doc.Lines() * height);
	TXLineHeightInfo info;
	doc.formatter->fFrameFormatter->GetLineHeightInfo(2, &info);
	EXPECT(info.fHeight == height && info.fAscent == ascent);
	TXOffsetRange range;
	doc.formatter->GetLineRange(1, &range);
	EXPECT(range.fStart.fOffset == doc.End(0) && range.fEnd.fOffset == doc.End(1) && range.fEnd.fAtStart);

	// an insertion, reflowed: the same lines as formatting it afresh
	long at = strstr(kText, "lazy") - kText;
	doc.Edit(at, 0, "very very ");
	EXPECT(doc.formatter->fLineEnds->GetLastRangeEnd() == (long) strlen(kText));
	doc.formatter->ReplaceRange(at, 0, 10, 0, &first, &last);
	CheckLines(doc, "inserted");
	char edited[256];
	strcpy(edited, kText);
	memmove(edited + at + 10, edited + at, strlen(edited + at) + 1);
	memcpy(edited + at, "very very ", 10);
	Document fresh(edited);
	fresh.formatter->Format(0, -1, nil, nil);
	SameLines(doc, fresh);
	EXPECT(first >= 0 && doc.Start(first) <= at);				// the first line that changed

	// a deletion across lines
	long from = strstr(edited, "quick") - edited;
	long to = strstr(edited, "over") - edited;
	doc.Edit(from, to - from, "");
	doc.formatter->ReplaceRange(from, to - from, 0, 0, &first, &last);
	CheckLines(doc, "deleted");
	memmove(edited + from, edited + to, strlen(edited + to) + 1);
	Document fresh2(edited);
	fresh2.formatter->Format(0, -1, nil, nil);
	SameLines(doc, fresh2);

	// typing at the end, a character at a time
	for (int i = 0; i < 12; i++)
	{
		long end = doc.chars->Count();
		doc.Edit(end, 0, (i % 4 == 3) ? " " : "x");
		doc.formatter->ReplaceRange(end, 0, 1, 0, &first, &last);
	}
	CheckLines(doc, "typed");
}


static void
TestEmptyLastLine()
{
	long first, last;
	Document doc("one\rtwo\r");
	doc.formatter->Format(0, -1, nil, nil);
	EXPECT(doc.Lines() == 3);
	EXPECT(doc.End(0) == 4 && doc.End(1) == 8 && doc.End(2) == 8);	// the empty line after the break
	int ascent, descent, leading;
	doc.run->GetHeightInfo(&ascent, &descent, &leading);
	TXLineHeightInfo info;
	doc.formatter->fFrameFormatter->GetLineHeightInfo(2, &info);
	EXPECT(info.fHeight == ascent + descent + leading && info.fAscent == ascent);
	// the break deleted: the empty line goes
	doc.Edit(7, 1, "");
	doc.formatter->ReplaceRange(7, 1, 0, 0, &first, &last);
	EXPECT(doc.Lines() == 2 && doc.End(1) == 7);

	// an empty text is one empty line, 12 tall with an ascent of 9... when
	// there is no text run at its end
	Document empty("");
	EXPECT(empty.Lines() == 1);
	empty.formatter->fFrameFormatter->GetLineHeightInfo(0, &info);
	EXPECT(info.fHeight == 12 && info.fAscent == 9);
}


static void
TestTabsAndNoWrap()
{
	// a tab stop at 40: "ab<tab>cd" is one line, the tab's width taken off
	Document tabbed("ab\tcd ef gh ij kl");
	TXTabsArray tabs;
	TXTab tab;
	tab.Set(40, kTXTabLeft, 0);
	tabs.InsertTab(tab);
	tabbed.ruler->SetTabs(&tabs);
	tabbed.formatter->Format(0, -1, nil, nil);
	TXOffset end0 = tabbed.End(0);
	EXPECT(end0 > 5);											// past "cd "
	long width = 40 + Width(tabbed, 3, end0);					// the stop, then the rest
	long visible = end0;
	while (IsSpace(tabbed, visible - 1))
		visible--;
	EXPECT(40 + Width(tabbed, 3, visible) <= kLineWidth);
	(void) width;

	// no wrapping: a paragraph is a line
	Document plain(kText);
	plain.formatter->fNoWrap = true;
	plain.formatter->Format(0, -1, nil, nil);
	EXPECT(plain.Lines() == 2);
	EXPECT(plain.End(0) == (long) (strchr(kText, '\r') - kText + 1));
}


static void
TestRulerSettings()
{
	Document doc("words");
	long margin = 50;
	doc.ruler->SetAttributeValue(kTXAttrRightMargin, &margin);	// 80 - 0 - 50 < 50: goes
	long indent = 10;
	doc.ruler->SetAttributeValue(kTXAttrIndent, &indent);
	TXTabsArray tabs;
	TXTab near, far;
	near.Set(30, kTXTabLeft, 0);
	far.Set(200, kTXTabLeft, 0);
	tabs.InsertTab(near);
	tabs.InsertTab(far);
	doc.ruler->SetTabs(&tabs);
	EXPECT(doc.formatter->CheckRulerSettings() == noErr);
	long value = -1;
	doc.ruler->GetAttributeValue(kTXAttrRightMargin, &value);
	EXPECT(value == 0);
	doc.ruler->GetAttributeValue(kTXAttrIndent, &value);
	EXPECT(value == 10);										// room enough: kept
	EXPECT(doc.ruler->fTabs != nil && doc.ruler->fTabs->GetCount() == 1);	// the tab past the edge gone
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_TXFormatter: cannot import %s\n", NEWTON_ROM_BIN);
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
	gTXDefaultTabVal = 30;

	gMap.baseAddr = (Ptr) gBits;
	gMap.rowBytes = 16;
	SetRect(&gMap.bounds, 0, 0, 128, 16);
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
		TestParagraph();
		TestEmptyLastLine();
		TestTabsAndNoWrap();
		TestRulerSettings();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	printf("test_TXFormatter: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
