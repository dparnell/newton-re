// TXNewtTextRun test (src/text/TXNewtTextRun.h): the text run - its three
// attributes and the lists they travel in, its height, and the measuring,
// hit-testing, line breaking and drawing it does through QuickDraw's text
// objects, checked against QuickDraw's own answers in the ROM's fonts.
// The ROM image is imported for its fonts and its U.S. locale bundle
// (the line break table).
#include "TXNewtTextRun.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "FixedMath.h"
#include "Fonts.h"
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

const long kWidth = 128;
const long kHeight = 32;
static unsigned char gBits[kWidth * kHeight / 8];
static PixelMap gMap;
static GrafPort gPort;


static Ref
FontSpec(const char* family, long size, long face)
{
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMfamily, RefVar(MakeSymbol((char*) family)));
	SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(size)));
	SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(face)));
	return spec;
}


static const UniChar*
Chars(const char* s, UniChar* buffer)
{
	long i = 0;
	for (; s[i] != 0; i++)
		buffer[i] = (UniChar) (unsigned char) s[i];
	buffer[i] = 0;
	return buffer;
}


// QuickDraw's width of the first n characters in the run's style
static Fixed
WidthOf(TXNewtTextRun* run, const UniChar* text, long n)
{
	StyleRecord style;
	run->GetNewtStyleRecord(&style);
	StyleRecord* styles[1] = { &style };
	TextBoundsInfo bounds;
	FPoint zero = { 0, 0 };
	MeasureTextOnce(text, n, styles, nil, zero, nil, &bounds);
	return bounds.fWidth;
}


static void
TestAttributes()
{
	// a new run takes the user's font
	TXNewtTextRun* run = new TXNewtTextRun;
	EXPECT(EQRef(run->fFamily, SYM(espy)));
	EXPECT(run->fSize == 10 && run->fFace == 0);
	EXPECT(run->fAscent == -1);
	EXPECT(run->GetClassId() == 'text');
	EXPECT(run->IsTextRun());

	// the three attributes
	TXAttrValues values;
	run->GetAttributesValues(&values);
	EXPECT(values.GetCount() == 3);
	TXNewtFontFamilyInfo* info = new TXNewtFontFamilyInfo(RefVar(NILREF));
	EXPECT(run->GetAttributeValue(kTXAttrFont, &info) && EQRef(info->fFamily, SYM(espy)));
	long size = 0;
	EXPECT(run->GetAttributeValue(kTXAttrSize, &size) && size == 10);
	EXPECT(!run->GetAttributeValue(kTXAttrJustification, &size));
	EXPECT(run->GetAttributeFlags(kTXAttrFont) & 3);
	EXPECT((run->GetAttributeFlags(kTXAttrTabs) & 3) == 0);

	// the height is worked out once and thrown away by any change
	int ascent, descent, leading;
	run->GetHeightInfo(&ascent, &descent, &leading);
	EXPECT(ascent > 0 && descent > 0);
	EXPECT(run->fAscent == ascent);
	StyleRecord style;
	CreateTextStyleRecord(RefVar(FontSpec("espy", 10, 0)), &style);
	FontInfo fontInfo;
	GetStyleFontInfo(&style, &fontInfo);
	EXPECT(ascent == fontInfo.ascent && descent == fontInfo.descent && leading == fontInfo.leading);
	size = 18;
	run->SetAttributeValue(kTXAttrSize, &size);
	EXPECT(run->fSize == 18 && run->fAscent == -1);
	int bigAscent;
	run->GetHeightInfo(&bigAscent, &descent, &leading);
	EXPECT(bigAscent > ascent);

	// faces added and taken away, and what two faces agree about
	long bold = 1, italic = 2;
	run->UpdateAttribute(kTXAttrFace, &bold, 4);
	run->UpdateAttribute(kTXAttrFace, &italic, 4);
	EXPECT(run->fFace == 3);
	run->UpdateAttribute(kTXAttrFace, &bold, 8);
	EXPECT(run->fFace == 2);
	run->UpdateAttribute(kTXAttrFace, &bold, 0);				// neither: set
	EXPECT(run->fFace == 1);
	ULong faces = 3;
	EXPECT(run->GetCommonAttrValue(kTXAttrFace, &faces) && faces == 1);
	faces = 2;
	EXPECT(!run->GetCommonAttrValue(kTXAttrFace, &faces) && faces == 2);
	run->fFace = 0;
	faces = 0;
	EXPECT(run->GetCommonAttrValue(kTXAttrFace, &faces));
	size = 18;
	EXPECT(run->GetCommonAttrValue(kTXAttrSize, &size));
	EXPECT(run->GetCommonAttrValue(kTXAttrFont, &info));

	// equality, copies, and the attributes applied through the list
	TXNewtTextRun* other = (TXNewtTextRun*) run->CreateNew();
	EXPECT(!run->IsEqual(other));								// 18 point against 10
	other->Assign(run);
	EXPECT(run->IsEqual(other) && other->fSize == 18);
	TXAttrValues change;
	long twelve = 12;
	change.Add(kTXAttrSize, &twelve, sizeof(twelve), false);
	EXPECT(other->Update(&change, 0) & 3);
	EXPECT(other->fSize == 12 && !run->IsEqual(other));

	// the font spec a script sees, and back
	RefVar spec(run->GetNSObject());
	other->SetNSObject(RefVar(FontSpec("newYork", 9, 1)));
	EXPECT(EQRef(other->fFamily, SYM(newYork)) && other->fSize == 9 && other->fFace == 1);
	other->SetNSObject(spec);
	EXPECT(run->IsEqual(other));

	// a font spec as an attribute list
	TXAttrValues* list = TXGetRunAttrValues(RefVar(FontSpec("geneva", 9, 1)));
	EXPECT(list->GetCount() == 3);
	TXNewtFontFamilyInfo* got = nil;
	EXPECT(list->GetValue(kTXAttrFont, &got) && got != nil && EQRef(got->fFamily, SYM(geneva)));
	other->Update(list, 0);
	EXPECT(EQRef(other->fFamily, SYM(geneva)) && other->fSize == 9 && other->fFace == 1);
	delete list;
	RefVar noFace(FontSpec("geneva", 9, 0));
	SetFrameSlot(noFace, RSSYMface, RefVar(NILREF));
	list = TXGetRunAttrValues(noFace);
	EXPECT(list->GetCount() == 2);
	delete list;

	delete info;
	other->Free();
	run->Free();
	DisposeStyleRecord(&style);
}


static void
TestMeasuring()
{
	TXNewtTextRun* run = new TXNewtTextRun;
	UniChar buffer[64];
	const UniChar* text = Chars("hello world again", buffer);
	const long length = 17;
	TXLineRunDisplayInfo info = { text, length, 0, 0 };

	Fixed whole = WidthOf(run, text, length);
	EXPECT(whole > 0);
	EXPECT(run->MeasureWidth(info) == whole);

	// where each character starts, and which boundary a pixel is nearest
	for (long i = 0; i <= length; i++)
		EXPECT(run->CharToPixel(info, i) == WidthOf(run, text, i));
	for (long i = 0; i < length; i++)
	{
		Fixed left = WidthOf(run, text, i);
		Fixed right = WidthOf(run, text, i + 1);
		Fixed middle = left + (right - left) / 2;
		TXOffsetRange range;
		run->PixelToChar(info, middle - 0x8000, &range);
		EXPECT(range.fStart.fOffset == i && range.fEnd.fOffset == i);
		EXPECT(range.fStart.fAtStart == (i != 0));
		run->PixelToChar(info, middle + 0x8000, &range);
		EXPECT(range.fStart.fOffset == i + 1 && range.fStart.fAtStart);
	}

	// fully justified: the run stretched over its width and the extra, the
	// extra spread over the two spaces
	info.fWidth = whole;
	info.fJustifyExtra = 10 << 16;
	EXPECT(run->CharToPixel(info, 0) == 0);
	Fixed stretched = run->CharToPixel(info, length);
	EXPECT(stretched > whole + (9 << 16) && stretched <= whole + (10 << 16));
	EXPECT(run->CharToPixel(info, 6) > WidthOf(run, text, 6));	// past the first space: moved on
	Fixed before = run->CharToPixel(info, 5);						// before it: only the letters' share
	EXPECT(before > WidthOf(run, text, 5) && before < WidthOf(run, text, 5) + (2 << 16));
	info.fJustifyExtra = 0;

	// what a fully justified line may stretch it by: a thirty-second of
	// the size a space
	EXPECT(run->FullJustifPortion(info) == FixedMultiply(10 << 16, 2 * 0x800));
	EXPECT(run->FullJustifPortion(info) == 0x0a000);

	// the trailing spaces and line ends do not show
	EXPECT(run->VisibleLen(Chars("ab  \r", buffer), 5) == 2);
	EXPECT(run->VisibleLen(Chars("ab\n c", buffer), 5) == 5);
	EXPECT(run->VisibleLen(Chars("  \n", buffer), 3) == 0);
	EXPECT(run->VisibleLen(buffer, 0) == 0);
	run->Free();
}


static void
TestLineBreak()
{
	TXNewtTextRun* run = new TXNewtTextRun;
	UniChar buffer[64];
	const UniChar* text = Chars("hello world again", buffer);
	const long length = 17;
	Fixed whole = WidthOf(run, text, length);

	// all of it fits: the room it took comes off
	Fixed width = whole + (20 << 16);
	long fitted = -1;
	EXPECT(run->LineBreak(text, length, 0, &width, false, &fitted) == 2);
	EXPECT(fitted == length && width == (20 << 16));

	// the fitted length ends inside "world": cut in front of it, the
	// space staying on the line
	width = WidthOf(run, text, 9) + 0x100;
	EXPECT(run->LineBreak(text, length, 0, &width, false, &fitted) == 0);
	EXPECT(fitted == 6 && width == 0);

	// it ends on the space: the spaces are a word of their own (the
	// locale's break table), so the line breaks after them
	width = WidthOf(run, text, 5) + 0x100;
	EXPECT(run->LineBreak(text, length, 0, &width, false, &fitted) == 0);
	EXPECT(fitted == 6);

	// from part way along: the lengths are from `start`
	width = WidthOf(run, text + 6, 8) + 0x100;					// "world ag"
	EXPECT(run->LineBreak(text, length, 6, &width, false, &fitted) == 0);
	EXPECT(fitted == 6);										// "world "

	// the first word does not fit: cut inside it when the line may
	width = WidthOf(run, text, 3) + 0x100;
	EXPECT(run->LineBreak(text, length, 0, &width, true, &fitted) == 1);
	EXPECT(fitted == 3);
	width = 0x100;												// not even a character
	EXPECT(run->LineBreak(text, length, 0, &width, true, &fitted) == 1);
	EXPECT(fitted == 1);										// but at least one
	width = WidthOf(run, text, 3) + 0x100;
	EXPECT(run->LineBreak(text, length, 0, &width, false, &fitted) == 0);
	EXPECT(fitted == 0);										// (-start: nought here)
	run->Free();
}


static long
CountBits(void)
{
	long n = 0;
	for (unsigned long i = 0; i < sizeof(gBits); i++)
		for (unsigned char b = gBits[i]; b != 0; b >>= 1)
			n += b & 1;
	return n;
}


static void
TestDraw()
{
	TXNewtTextRun* run = new TXNewtTextRun;
	UniChar buffer[64];
	const UniChar* text = Chars("Tab", buffer);
	TXLineRunDisplayInfo info = { text, 3, WidthOf(run, text, 3), 0 };
	Rect line;
	SetRect(&line, 0, 4, kWidth, 24);
	int ascent, descent, leading;
	run->GetHeightInfo(&ascent, &descent, &leading);

	// the run's drawing is QuickDraw's drawing of the same text on the
	// line's baseline
	memset(gBits, 0, sizeof(gBits));
	run->Draw(info, 5 << 16, line, ascent);
	long drawn = CountBits();
	EXPECT(drawn > 0);
	unsigned char copy[sizeof(gBits)];
	memcpy(copy, gBits, sizeof(gBits));
	memset(gBits, 0, sizeof(gBits));
	StyleRecord style;
	run->GetNewtStyleRecord(&style);
	StyleRecord* styles[1] = { &style };
	FPoint where = { 5 << 16, (4 + ascent) << 16 };
	DrawTextOnce(text, 3, styles, nil, where, nil, nil);
	EXPECT(memcmp(copy, gBits, sizeof(gBits)) == 0);

	// nothing left of x or above the line's top
	memcpy(gBits, copy, sizeof(gBits));
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
			if (gBits[y * (kWidth / 8) + x / 8] & (0x80 >> (x % 8)))
				EXPECT(x >= 5 && y >= 4 && y < 4 + ascent + descent);

	// drawn in or mode: what was there stays
	memset(gBits, 0, sizeof(gBits));
	gBits[0] = 0x80;
	run->Draw(info, 5 << 16, line, ascent);
	EXPECT(gBits[0] & 0x80);
	EXPECT(CountBits() == drawn + 1);
	run->Free();
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_TXNewtTextRun: cannot import %s\n", NEWTON_OBJECTS);
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
	// what the boot script makes: the locale, and the user's font
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(TranslateROMRef(kUSABundle)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	RefVar config(AllocateFrame());
	SetFrameSlot(config, RSSYMuserfont, RefVar(FontSpec("espy", 10, 0)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, config);
	InitInternationalUtils();

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
		TestAttributes();
		TestMeasuring();
		TestLineBreak();
		TestDraw();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	printf("test_TXNewtTextRun: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
