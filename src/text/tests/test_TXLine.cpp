// TXLine test (src/text/TXLine.h): a line of styled text with a tab,
// laid out against a ruler, measured, drawn into an offscreen port and
// hit-tested - the text engine's first line of text.  The pieces, the
// three kinds of tab (left, right, decimal), the four justifications,
// the hilite of a selection and a caret.  The ROM image is imported for
// its fonts and its U.S. locale bundle.
#include "TXLine.h"
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
#include "FixedMath.h"
#include "Text.h"
#include "Draw.h"
#include "Ports.h"
#include "Regions.h"
#include "Locale.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const ULong kUSABundle = 0x004a4d09;		// the ROM's locale bundle 'USA (as test_Dates)

const long kWidth = 160;
const long kHeight = 32;
static unsigned char gBits[kWidth * kHeight / 8];
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
	void	SetText(const char* text)
			{
				UniChar buffer[256];
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


static Ref
FontSpec(const char* family, long size, long face)
{
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMfamily, RefVar(MakeSymbol((char*) family)));
	SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(size)));
	SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(face)));
	return spec;
}


// QuickDraw's width of a string in a run's style
static Fixed
WidthOf(TXNewtTextRun* run, const char* s)
{
	UniChar text[64];
	long n = (long) strlen(s);
	for (long i = 0; i < n; i++)
		text[i] = (UniChar) s[i];
	StyleRecord style;
	run->GetNewtStyleRecord(&style);
	StyleRecord* styles[1] = { &style };
	TextBoundsInfo bounds;
	FPoint zero = { 0, 0 };
	MeasureTextOnce(text, n, styles, nil, zero, nil, &bounds);
	return bounds.fWidth;
}


static void
DrawString(TXNewtTextRun* run, const char* s, Fixed x, long y)
{
	UniChar text[64];
	long n = (long) strlen(s);
	for (long i = 0; i < n; i++)
		text[i] = (UniChar) s[i];
	StyleRecord style;
	run->GetNewtStyleRecord(&style);
	StyleRecord* styles[1] = { &style };
	FPoint where = { x, ToFixed(y) };		// (qd/Ports.h: never `y << 16` by hand - Fixed is the ARM's word)
	DrawTextOnce(text, n, styles, nil, where, nil, nil);
}


// A document: its characters, its runs (two styles) and one ruler.
struct Document
{
	Document(const char* s, long split, char justification)
	{
		chars = new TestChars;
		chars->SetText(s);
		long length = chars->Count();
		text = new TXStyledText;
		text->IStyledText(&gPort, chars, 2);
		first = new TXNewtTextRun;
		second = new TXNewtTextRun;
		second->SetNSObject(RefVar(FontSpec("espy", 12, 1)));	// 12 point bold
		if (split > 0)
			text->fRuns->InsertObjectRange(0, split, first, false);
		text->fRuns->InsertObjectRange(split > 0 ? 1 : 0, length, second, false);
		ruler = new TXAdvancedRuler;
		ruler->fJustification = justification;
		rulers = new TXRulerRange(chars, new TXAdvancedRuler);
		rulers->InsertObjectRange(0, length, ruler, false);
		line = new TXLine(text, rulers);
	}
	~Document()
	{
		delete line;
		delete rulers;
		delete text;
	}
	void	AddTab(int position, unsigned char kind)
	{
		TXTabsArray tabs;
		if (ruler->fTabs != nil)
			for (long i = 0; i < ruler->fTabs->GetCount(); i++)
				tabs.InsertTab(ruler->fTabs->GetIndTab(i));
		TXTab tab;
		tab.Set(position, kind, 0);
		tabs.InsertTab(tab);
		ruler->SetTabs(&tabs);
	}

	TestChars*		chars;
	TXStyledText*	text;
	TXNewtTextRun*	first;
	TXNewtTextRun*	second;
	TXAdvancedRuler* ruler;
	TXRulerRange*	rulers;
	TXLine*			line;
};


static void
TestTabbedLine()
{
	// "Name" in 10 point, a tab and "Value" in 12 point bold; a tab stop at 60
	Document doc("Name\tValue", 4, kTXJustifyLeft);
	doc.AddTab(60, kTXTabLeft);
	TXLine* line = doc.line;
	line->DoLineLayout(0, 10, kWidth);

	// three pieces: the text, the tab, the text
	EXPECT(line->fLast == 2);
	EXPECT(line->fStart == 0 && line->fLength == 10 && line->fVisibleLength == 10 && !line->fInvisible);
	EXPECT(line->fLeft == 0 && line->fWidth == (kWidth << 16));
	TXLineRunInfo* name = line->GetRunInfo(0);
	TXLineRunInfo* tab = line->GetRunInfo(1);
	TXLineRunInfo* value = line->GetRunInfo(2);
	Fixed nameWidth = WidthOf(doc.first, "Name");
	Fixed valueWidth = WidthOf(doc.second, "Value");
	EXPECT(name->fRun == doc.first && name->fOffset == 0 && name->fLength == 4 && name->fKind == 0);
	EXPECT(name->fWidth == nameWidth);
	EXPECT(tab->fRun == doc.second && tab->fOffset == 4 && tab->fLength == 1 && tab->fKind == 9);
	EXPECT(tab->fWidth == (60 << 16) - nameWidth);				// to the stop
	EXPECT(value->fOffset == 5 && value->fLength == 5 && value->fKind == 0 && value->fWidth == valueWidth);

	// where the characters are
	EXPECT(line->CharacterToPixel(0, false) == 0);
	EXPECT(line->CharacterToPixel(5, false) == 60);				// after the tab: the stop
	EXPECT(line->CharacterToPixel(7, false) == (short) ((60 << 16) + WidthOf(doc.second, "Va") + 0x8000 >> 16));
	EXPECT(line->CharacterToPixel(2, false) == (short) (WidthOf(doc.first, "Na") + 0x8000 >> 16));
	EXPECT(line->CharacterToPixel(5, true) == 60);				// the end of the tab, seen from before it
	EXPECT(line->CharacterToPixel(4, true) == (short) (nameWidth + 0x8000 >> 16));	// the end of "Name"

	// a tap
	TXOffsetRange range;
	TXRun* run = line->PixelToCharacter((60 << 16) + WidthOf(doc.second, "V") + 0x10000, &range);
	EXPECT(run == doc.second);
	EXPECT(range.fStart.fOffset == 6 || range.fStart.fOffset == 7);
	Fixed middleOfV = (60 << 16) + WidthOf(doc.second, "V") / 2;
	line->PixelToCharacter(middleOfV - 0x8000, &range);
	EXPECT(range.fStart.fOffset == 5 && range.fEnd.fOffset == 5);
	line->PixelToCharacter(middleOfV + 0x8000, &range);
	EXPECT(range.fStart.fOffset == 6);
	run = line->PixelToCharacter(WidthOf(doc.first, "N") / 2 - 0x8000, &range);
	EXPECT(run == doc.first && range.fStart.fOffset == 0);
	run = line->PixelToCharacter(nameWidth + ((60 << 16) - nameWidth) / 2, &range);	// the middle of the tab: all of it
	EXPECT(run == doc.second && range.fStart.fOffset == 4 && range.fEnd.fOffset == 5);
	run = line->PixelToCharacter(200 << 16, &range);			// past the end
	EXPECT(run == doc.second && range.fStart.fOffset == 10);

	// the hilite of a selection and of a caret
	TXLineHilite hilite;
	TXOffsetRange valueRange(5, 10, false, true);
	line->GetLineHilite(valueRange, &hilite, false);
	EXPECT(hilite.fLeft == (60 << 16) && hilite.fWidth == valueWidth);
	TXOffsetRange across(2, 7, false, true);
	line->GetLineHilite(across, &hilite, false);
	EXPECT(hilite.fLeft == WidthOf(doc.first, "Na"));
	EXPECT(hilite.fLeft + hilite.fWidth == (60 << 16) + WidthOf(doc.second, "Va"));
	line->GetLineHilite(valueRange, &hilite, true);				// to the line's right edge
	EXPECT(hilite.fLeft == (60 << 16) && hilite.fWidth == ((kWidth - 60) << 16));
	TXOffsetRange caret(5, 5, false, false);
	line->GetLineHilite(caret, &hilite, false);
	EXPECT(hilite.fLeft == (60 << 16) && hilite.fWidth == 0x10000);

	// drawn: the two strings where QuickDraw would put them, and nothing else
	int ascent, descent, leading;
	doc.second->GetHeightInfo(&ascent, &descent, &leading);
	Rect box;
	SetRect(&box, 0, 2, kWidth, 2 + ascent + descent);
	memset(gBits, 0, sizeof(gBits));
	line->Draw(box, ascent);
	unsigned char drawn[sizeof(gBits)];
	memcpy(drawn, gBits, sizeof(gBits));
	memset(gBits, 0, sizeof(gBits));
	DrawString(doc.first, "Name", 0, 2 + ascent);
	DrawString(doc.second, "Value", 60 << 16, 2 + ascent);
	EXPECT(memcmp(drawn, gBits, sizeof(gBits)) == 0);
	long on = 0;
	for (unsigned long i = 0; i < sizeof(drawn); i++)
		on += drawn[i] != 0;
	EXPECT(on > 10);

	// the same line again: nothing to do
	name->fWidth = 0;
	line->DoLineLayout(0, 10, kWidth);
	EXPECT(line->GetRunInfo(0)->fWidth == 0);
}


static void
TestTabKinds()
{
	// a right tab: the text after it ends at the stop
	Document right("a\tbcd", 0, kTXJustifyLeft);
	right.AddTab(100, kTXTabRight);
	right.line->DoLineLayout(0, 5, kWidth);
	EXPECT(right.line->fLast == 2);
	EXPECT(right.line->CharacterToPixel(5, true) == 100);
	EXPECT(right.line->GetRunInfo(1)->fWidth == (100 << 16) - WidthOf(right.second, "a") - WidthOf(right.second, "bcd"));

	// a decimal tab: the full stop at the stop
	Document decimal("x\t12.5", 0, kTXJustifyLeft);
	decimal.AddTab(80, kTXTabDecimalPoint);
	decimal.line->DoLineLayout(0, 6, kWidth);
	EXPECT(decimal.line->CharacterToPixel(4, false) == 80);	// "12|.5"

	// a centre tab: the text after it centred on the stop
	Document centre("x\tmiddle", 0, kTXJustifyLeft);
	centre.AddTab(80, kTXTabCenter);
	centre.line->DoLineLayout(0, 8, kWidth);
	short start = centre.line->CharacterToPixel(2, false);
	short end = centre.line->CharacterToPixel(8, true);
	EXPECT(start + end >= 159 && start + end <= 161);

	// no stop left: the next default one
	Document plain("abc\tdef", 0, kTXJustifyLeft);
	plain.line->DoLineLayout(0, 7, kWidth);
	EXPECT(plain.line->CharacterToPixel(4, false) == 30);		// gTXDefaultTabVal
}


static void
TestJustification()
{
	// right: the spaces at the end left out, the line moved right
	Document right("Hi  ", 0, kTXJustifyRight);
	right.line->DoLineLayout(0, 4, 100);
	EXPECT(right.line->fVisibleLength == 2);
	EXPECT(right.line->GetRunInfo(0)->fLength == 2);
	EXPECT(right.line->fLeft == (100 << 16) - WidthOf(right.second, "Hi"));
	EXPECT(right.line->CharacterToPixel(2, true) == 100);

	// centred: the spaces count
	Document centre("Hi", 0, kTXJustifyCenter);
	centre.line->DoLineLayout(0, 2, 100);
	EXPECT(centre.line->fLeft == FixedDivide((100 << 16) - WidthOf(centre.second, "Hi"), 0x20000));

	// full, on a line that does not end its paragraph: stretched to the edge
	Document full("aa bb cc dd", 0, kTXJustifyFull);
	full.line->DoLineLayout(0, 6, 100);							// "aa bb "
	EXPECT(full.line->fVisibleLength == 5);
	EXPECT(full.line->GetRunInfo(0)->fExtra == (100 << 16) - WidthOf(full.second, "aa bb"));
	EXPECT(full.line->CharacterToPixel(5, true) == 100);
	EXPECT(full.line->CharacterToPixel(3, false) > (short) (WidthOf(full.second, "aa ") >> 16));	// the space stretched

	// ... but not on its last line
	full.line->DoLineLayout(6, 5, 100);							// "cc dd"
	EXPECT(full.line->GetRunInfo(0)->fExtra == 0 && full.line->fLeft == 0);
	EXPECT(full.line->fVisibleLength == 5);

	// full with the last line too
	Document all("cc dd", 0, kTXJustifyFullAll);
	all.line->DoLineLayout(0, 5, 100);
	EXPECT(all.line->GetRunInfo(0)->fExtra == (100 << 16) - WidthOf(all.second, "cc dd"));

	// a line of nothing but spaces shows nothing and is not drawn
	Document blank("   ", 0, kTXJustifyLeft);
	blank.line->DoLineLayout(0, 3, 100);
	EXPECT(blank.line->fInvisible);
	memset(gBits, 0, sizeof(gBits));
	Rect box;
	SetRect(&box, 0, 0, 100, 20);
	blank.line->Draw(box, 12);
	for (unsigned long i = 0; i < sizeof(gBits); i++)
		EXPECT(gBits[i] == 0);

	// a line end is a piece of its own that shows nothing
	Document ended("ab\rcd", 0, kTXJustifyLeft);
	ended.line->DoLineLayout(0, 3, 100);
	EXPECT(ended.line->fLast == 1 && ended.line->GetRunInfo(1)->fKind == 13);
	EXPECT(ended.line->GetRunInfo(1)->fWidth == 0);
	TXOffsetRange range;
	ended.line->PixelToCharacter(90 << 16, &range);
	EXPECT(range.fStart.fOffset == 2);							// the line end's start
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_TXLine: cannot import %s\n", NEWTON_ROM_BIN);
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
	// what Textension::TextensionStart does
	gTXDefaultTabVal = 30;

	gMap.baseAddr = (Ptr) gBits;
	gMap.rowBytes = kWidth / 8;
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
		TestTabbedLine();
		TestTabKinds();
		TestJustification();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	printf("test_TXLine: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
