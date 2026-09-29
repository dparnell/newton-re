// Text test: the ROM's own fonts (espy - the system font - and Geneva, read
// out of the ROM image's font family frames) opened through the 'sfnt'
// engine, their metrics, "Hello Wg!" drawn in espy 12 with every pixel
// pinned, a bold strike and an underline, bold synthesised by smearing
// where the family has no bold data, measuring, and the NewtonScript
// StrFontWidth/FontAscent/FontDescent/FontHeight.  Runs over a standalone
// kernel heap with the ROM's objects imported.
#include "Text.h"
#include "Draw.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "RichString.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kWidth = 128;
const long kHeight = 32;
static unsigned char gBits[kWidth * kHeight / 8];
static PixelMap gMap;
static GrafPort gPort;

const long kEspy = 0;			// the ROM font list
const long kGeneva = 2;


static Ref
Eval(const char* source)
{
	if (getenv("EVAL_TRACE")) fprintf(stderr, "eval: %s\n", source);
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static void
Clear()
{
	memset(gBits, 0, sizeof(gBits));
}


// the map from (left, top) as rows of '#' and '.', against the picture
static Boolean
PictureIs(long left, long top, long width, long height, const char* picture, const char* what)
{
	Boolean same = true;
	for (long y = 0; y < height; y++)
		for (long x = 0; x < width; x++)
			if ((GetPixel(&gMap, left + x, top + y) != 0) != (picture[y * (width + 1) + x] == '#'))
				same = false;
	if (!same)
	{
		fprintf(stderr, "  %s:\n", what);
		for (long y = 0; y < height; y++)
		{
			fprintf(stderr, "    ");
			for (long x = 0; x < width; x++)
				fputc(GetPixel(&gMap, left + x, top + y) ? '#' : '.', stderr);
			fprintf(stderr, "   %.*s\n", (int) width, picture + y * (width + 1));
		}
	}
	return same;
}


static void
Draw(const char* what, long family, long size, long face, TextBoundsInfo* bounds)
{
	UniChar text[32];
	ConvertToUnicode(what, text, kMacRomanEncoding, 31);
	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(family, size, face))), &style);
	StyleRecord* styles[1] = { &style };
	FPoint where = { 2 << 16, 14 << 16 };
	DrawTextOnce(text, Ustrlen(text), styles, nil, where, nil, bounds);
	DisposeStyleRecord(&style);
}


static void
TestFonts()
{
	// the system font: espy 12, its metrics
	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &style);
	EXPECT(IsFrame(style.fFontFamily) && style.fFontSize == (12 << 16) && style.fFontFace == 0);
	EXPECT(EQRef(GetFrameSlotRef(style.fFontFamily, Intern((char*) "screenSym")), Intern((char*) "espyFont")));
	FontInfo info;
	GetStyleFontInfo(&style, &info);
	EXPECT(info.ascent == 12 && info.descent == 4 && info.leading == 0 && info.widMax == 15);
	// the font engine: the 12 point strike, 'H' and its metrics
	FontEngineInfo engine;
	EXPECT(OpenFont(&gMap, &style, 0x10000, 0x10000, &engine) == 0);
	EXPECT(engine.fScaleX == 0x10000 && engine.fAscent == 12 && engine.fDescent == 4 && engine.fWidthAdjust == 0);
	engine.fGetGlyph('H', 0, &engine);
	EXPECT(engine.fGlyphWidth == 7 && engine.fGlyphHeight == 9 && engine.fGlyphBearingX == 0 && engine.fGlyphBearingY == 9 && engine.fGlyphAdvance == (8 << 16));
	EXPECT(engine.fGlyphBits != nil && engine.fGlyphBits[0] == 0x82 && engine.fGlyphBits[4] == 0xfe);		// #.....# / #######
	engine.fGetGlyph(' ', 0, &engine);
	EXPECT(engine.fGlyphWidth == 0 && engine.fGlyphAdvance == (3 << 16));
	CloseFont(&engine);
	// a size without a strike takes the nearest and reports the scale
	style.fFontSize = 30 << 16;
	EXPECT(OpenFont(&gMap, &style, 0x10000, 0x10000, &engine) == 2 && engine.fScaleX != 0x10000);
	CloseFont(&engine);
	// a frame spec, and the packed spec's size and face
	EXPECT(PackedFontFamily(PackFont(2, 10, 5)) == 2 && PackedFontSize(PackFont(2, 10, 5)) == 10 && PackedFontFace(PackFont(2, 10, 5)) == 5);
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMfamily, RefVar(Intern((char*) "geneva")));
	SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(10)));
	SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(kBoldFace)));
	CreateTextStyleRecord(spec, &style);
	EXPECT(EQRef(GetFrameSlotRef(style.fFontFamily, RSSYMname), GetFrameSlotRef(RefVar(GetArraySlotRef(RefVar(Rromfontlist), kGeneva)), RSSYMname)));
	EXPECT(style.fFontSize == (10 << 16) && style.fFontFace == kBoldFace);
	// the font list and lookups
	EXPECT(Length(RefVar(GetROMFontList())) == 4);
	EXPECT(EQRef(SearchFont(3, nil), GetArraySlotRef(RefVar(Rromfontlist), kGeneva)));		// Geneva's Mac id
	EXPECT(EQRef(SearchFont(999, nil), GetFontFamily(RefVar(Rsystemfont))));
	EXPECT(FontRefToCharSize(RefVar(GetArraySlotRef(RefVar(Rromfontlist), kEspy))) == 2);
}


static void
TestDrawing()
{
	TextBoundsInfo bounds;
	Clear();
	Draw("Hello Wg!", kEspy, 12, 0, &bounds);
	EXPECT(bounds.fWidth == (51 << 16) && bounds.fAdvanceY == 0 && bounds.fLeft == (2 << 16) && bounds.fRight == (53 << 16));
	EXPECT(bounds.fTop == (2 << 16) && bounds.fBottom == (18 << 16));
	EXPECT(bounds.fLeading == 0);
	EXPECT(PictureIs(2, 5, 50, 12,
		"#.....#........#.#............#...#...#.........#.\n"
		"#.....#........#.#............#...#...#.........#.\n"
		"#.....#...###..#.#...###......#...#...#...####..#.\n"
		"#.....#..#...#.#.#..#...#......#.#.#.#...#...#..#.\n"
		"#######.#....#.#.#.#.....#.....#.#.#.#..#....#..#.\n"
		"#.....#.######.#.#.#.....#.....#.#.#.#..#....#..#.\n"
		"#.....#.#......#.#.#.....#......#...#...#....#....\n"
		"#.....#..#.....#.#..#...#.......#...#...#...##..#.\n"
		"#.....#...####.#.#...###........#...#....###.#..#.\n"
		".............................................#....\n"
		"............................................#.....\n"
		".........................................###......\n", "Hello Wg! in espy 12"));
	// the bold strike, underlined: the underline a pixel below the baseline
	Clear();
	Draw("Hello", kEspy, 12, kBoldFace | kUnderlineFace, &bounds);
	EXPECT(bounds.fWidth == (37 << 16));
	EXPECT(PictureIs(2, 5, 38, 12,
		"###...###..........###.###............\n"
		"###...###..........###.###............\n"
		"###...###...#####..###.###...#####....\n"
		"###...###..##..###.###.###..###.###...\n"
		"#########.###..###.###.###.###...###..\n"
		"###...###.########.###.###.###...###..\n"
		"###...###.###......###.###.###...###..\n"
		"###...###.####.....###.###..###.###...\n"
		"###...###...######.###.###...#####....\n"
		"......................................\n"
		"#####################################.\n"
		"......................................\n", "Hello in espy 12 bold underlined"));
	// Geneva has no bold data: bold is the plain strike smeared a pixel right
	Clear();
	Draw("Hello", kGeneva, 10, kBoldFace, &bounds);
	EXPECT(PictureIs(2, 6, 32, 8,
		".##..##.......###..###..........\n"
		".##..##........##...##..........\n"
		".##..##..###...##...##...###....\n"
		".######.##.##..##...##..##.##...\n"
		".##..##.#####..##...##..##.##...\n"
		".##..##.##.....##...##..##.##...\n"
		".##..##.##.##..##...##..##.##...\n"
		".##..##..###...##...##...###....\n", "Hello in Geneva 10 bold (smeared)"));
	Clear();
	Draw("Hello", kGeneva, 10, 0, &bounds);
	long plainWidth = bounds.fWidth >> 16;
	Draw("Hello", kGeneva, 10, kBoldFace, &bounds);
	EXPECT((bounds.fWidth >> 16) == plainWidth + 5);			// a pixel wider per glyph
	// the pen's mode does not apply (DrText takes the options' mode, srcOr
	// with none); the options' srcBic clears the glyph out of black
	Clear();
	PaintRect(&gMap.bounds);
	PenMode(patBic);
	Draw("H", kEspy, 12, 0, &bounds);
	PenNormal();
	EXPECT(GetPixel(&gMap, 2, 5) != 0 && GetPixel(&gMap, 3, 5) != 0 && GetPixel(&gMap, 2, 4) != 0);
	{
		UniChar h[2] = { 'H', 0 };
		StyleRecord style;
		CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &style);
		StyleRecord* styles[1] = { &style };
		FPoint where = { 2 << 16, 14 << 16 };
		TextOptions options;
		memset(&options, 0, sizeof(options));
		options.fTransferMode = srcBic;
		DrawTextOnce(h, 1, styles, nil, where, &options, nil);
		DisposeStyleRecord(&style);
	}
	EXPECT(GetPixel(&gMap, 2, 5) == 0 && GetPixel(&gMap, 3, 5) != 0 && GetPixel(&gMap, 2, 4) != 0);
	// srcCopy composes the run in a slab and copies it whole, the blank
	// round the glyphs included
	Clear();
	PaintRect(&gMap.bounds);
	{
		UniChar h[8] = { 'H', 'H', 'H', 'H', 'H', 'H', 'H', 0 };
		StyleRecord style;
		CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &style);
		StyleRecord* styles[1] = { &style };
		FPoint where = { 19 << 16, 14 << 16 };
		TextOptions options;
		memset(&options, 0, sizeof(options));
		options.fTransferMode = srcCopy;
		DrawTextOnce(h, 7, styles, nil, where, &options, nil);
		DisposeStyleRecord(&style);
	}
	EXPECT(GetPixel(&gMap, 17, 2) != 0 && GetPixel(&gMap, 18, 2) == 0 && GetPixel(&gMap, 75, 19) == 0 && GetPixel(&gMap, 76, 19) != 0);
	EXPECT(GetPixel(&gMap, 19, 5) != 0 && GetPixel(&gMap, 20, 5) == 0 && GetPixel(&gMap, 17, 1) != 0);
	Clear();
	HidePen();
	Draw("H", kEspy, 12, 0, &bounds);
	ShowPen();
	EXPECT(GetPixel(&gMap, 2, 5) == 0 && bounds.fWidth == (8 << 16));
	// measuring
	UniChar hello[8];
	ConvertToUnicode("Hello", hello, kMacRomanEncoding, 7);
	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &style);
	EXPECT(MeasureOnce(hello, 5, &style) == 27);
	EXPECT(MeasureOnceFont(hello, 5, RefVar(MAKEINT(PackFont(kEspy, 12, 0)))) == 27);
	EXPECT(MeasureOnce(hello, 0, &style) == 0);
	// two runs with two styles: the widths add up
	StyleRecord bold;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, kBoldFace))), &bold);
	StyleRecord* styles[2] = { &style, &bold };
	short runs[2] = { 2, 3 };
	FPoint origin = { 0, 0 };
	MeasureTextOnce(hello, 5, styles, runs, origin, nil, &bounds);
	EXPECT(bounds.fWidth == (MeasureOnce(hello, 2, &style) + MeasureOnce(hello + 2, 3, &bold)) << 16);
}


// Italic synthesised: each row of the run's slab moved right by another
// eight sixteenths of a pixel going up from the slab's bottom (which is
// below the descent by the descent again: espy 12's minAfterBL is -3, so
// its slab ends 6 below the baseline), so the baseline row is already three
// pixels over and the 'H''s top row seven.
static void
TestItalic()
{
	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, kItalicFace))), &style);
	FontEngineInfo engine;
	EXPECT(OpenFont(&gMap, &style, 0x10000, 0x10000, &engine) == 0);
	EXPECT(engine.fStyleAdjust[1] == 8 && engine.fMinAfterBL == -3);
	CloseFont(&engine);
	DisposeStyleRecord(&style);
	TextBoundsInfo bounds;
	Clear();
	Draw("Hl", kEspy, 12, kItalicFace, &bounds);
	EXPECT(PictureIs(2, 5, 24, 9,
		".......#.....#.#........\n"
		"......#.....#.#.........\n"
		"......#.....#.#.........\n"
		".....#.....#.#..........\n"
		".....#######.#..........\n"
		"....#.....#.#...........\n"
		"....#.....#.#...........\n"
		"...#.....#.#............\n"
		"...#.....#.#............\n", "Hl in espy 12 italic"));
}


// Word breaks by the U.S. locale's lineBreakTable (the ROM's 'Intl binary
// 0x4a4209): the word a line may break around, found by the table's two
// state machines.
static void
TestWordBreaks()
{
	RefVar table(TranslateROMRef(0x004a4209));
	EXPECT(IsBinary(table));
	UniChar text[32];
	ConvertToUnicode("Hello World again", text, kMacRomanEncoding, 31);
	ULong length = Ustrlen(text);
	// forward: the word the offset is in - a run of letters, or the space
	// between two (a word of its own); not forward: the character before
	// it (at 0 the offset wraps round and answers the end)
	// forward: the word the offset is in - a run of letters, or the space
	// between two (a word of its own); not forward: the character before
	// it (at 0 the offset wraps round and answers the end)
	const ULong offsets[] = { 0, 3, 5, 6, 8, 11, 12, 16 };
	const ULong forwards[][2] = { { 0, 5 }, { 0, 5 }, { 5, 6 }, { 6, 11 }, { 6, 11 }, { 11, 12 }, { 12, 17 }, { 12, 17 } };
	const ULong backwards[][2] = { { 17, 17 }, { 0, 5 }, { 0, 5 }, { 5, 6 }, { 6, 11 }, { 6, 11 }, { 11, 12 }, { 12, 17 } };
	for (unsigned long i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
	{
		ULong start, end;
		FindWordBreaks(text, length, offsets[i], true, table, &start, &end);
		EXPECT(start == forwards[i][0] && end == forwards[i][1]);
		FindWordBreaks(text, length, offsets[i], false, table, &start, &end);
		EXPECT(start == backwards[i][0] && end == backwards[i][1]);
	}
	// several spaces are one word of their own: "hello   world" at 6 is the
	// spaces, 5..8; looking back from "again" the word is the space before it
	ConvertToUnicode("hello   world again", text, kMacRomanEncoding, 31);
	ULong a, b;
	FindWordBreaks(text, 19, 6, true, table, &a, &b);
	EXPECT(a == 5 && b == 8);
	FindWordBreaks(text, 19, 14, false, table, &a, &b);
	EXPECT(a == 13 && b == 14);
}


static void
TestNatives()
{
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "espy12")), RefVar(MAKEINT(PackFont(kEspy, 12, 0))));
	EXPECT(RINT(Eval("FontAscent(espy12)")) == 12);
	EXPECT(RINT(Eval("FontDescent(espy12)")) == 4);
	EXPECT(RINT(Eval("FontLeading(espy12)")) == 0);
	EXPECT(RINT(Eval("FontHeight(espy12)")) == 16);
	EXPECT(RINT(Eval("StrFontWidth(\"Hello\", espy12)")) == 27);
	EXPECT(RINT(Eval("StrFontWidth(\"\", espy12)")) == 0);
	EXPECT(RINT(Eval("StrFontWidth(\"Hello\", {family: 'geneva, size: 10, face: 0})")) == 25);
}


// the extent of the set pixels in a row band of the map
static void
InkExtent(long top, long bottom, long* left, long* right)
{
	*left = kWidth;
	*right = 0;
	for (long y = top; y < bottom; y++)
		for (long x = 0; x < kWidth; x++)
			if (GetPixel(&gMap, x, y))
			{
				if (x < *left) *left = x;
				if (x + 1 > *right) *right = x + 1;
			}
}


static void
TestLayout()
{
	UniChar text[32];
	ConvertToUnicode("Hello World", text, kMacRomanEncoding, 31);
	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &style);
	StyleRecord* styles[1] = { &style };
	long width = MeasureOnce(text, 11, &style);
	// the options: a width to fit, the text aligned in it
	TextOptions options;
	memset(&options, 0, sizeof(options));
	options.fWidth = 100 << 16;
	options.fAlignment = 0x8000;			// centred
	TextBoundsInfo bounds;
	FPoint where = { 10 << 16, 14 << 16 };
	MeasureTextOnce(text, 11, styles, nil, where, &options, &bounds);
	EXPECT(((bounds.fLeft + 0x8000) >> 16) == 10 + (100 - width) / 2 && ((bounds.fWidth + 0x8000) >> 16) == width);
	EXPECT(((options.fFittedWidth + 0x8000) >> 16) == width);
	options.fAlignment = 0x10000;			// flush right
	MeasureTextOnce(text, 11, styles, nil, where, &options, &bounds);
	EXPECT(((bounds.fRight + 0x8000) >> 16) == 110);
	// drawn centred: the ink sits where the bounds say
	Clear();
	options.fAlignment = 0x8000;
	DrawTextOnce(text, 11, styles, nil, where, &options, &bounds);
	long inkLeft, inkRight;
	InkExtent(0, kHeight, &inkLeft, &inkRight);
	EXPECT(inkLeft == ((bounds.fLeft + 0x8000) >> 16) && inkRight <= ((bounds.fRight + 0x8000) >> 16));
	// full justification spreads the slack: the ink reaches the right edge
	Clear();
	options.fAlignment = 0;
	options.fJustification = 0x10000;
	DrawTextOnce(text, 11, styles, nil, where, &options, &bounds);
	InkExtent(0, kHeight, &inkLeft, &inkRight);
	EXPECT(inkLeft == 10 && inkRight >= 108 && inkRight <= 110);
	// a width too narrow: only the characters that fit are drawn
	options.fJustification = 0;
	options.fWidth = 30 << 16;
	long drawn = DoTextOnce(text, 11, styles, nil, where, &options, &bounds, false);
	EXPECT(drawn == 6 && ((bounds.fWidth + 0x8000) >> 16) == 30);		// "Hello " is 30 wide, the W would cross
	DisposeStyleRecord(&style);

	// a paragraph wrapped into a box: "Hello World" in 40 pixels is two lines
	RefVar str(MakeString((char*) "Hello World"));
	TRichString rich(str);
	Rect box;
	SetRect(&box, 0, 0, 40, 0);
	TextBounds(rich, RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &box, 0);
	EXPECT(box.bottom == 32 && box.right == 40);
	SetRect(&box, 0, 0, 0, 0);
	TextBounds(rich, RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &box, 0);
	EXPECT(box.bottom == 16 && box.right == width);
	// a carriage return breaks a line; a line too long for the box is cut at a word
	RefVar three(MakeString((char*) "one\rtwo three four"));
	TRichString rich3(three);
	SetRect(&box, 0, 0, 60, 0);
	TextBounds(rich3, RefVar(MAKEINT(PackFont(kEspy, 12, 0))), &box, 0);
	EXPECT(box.bottom == 48);
	// drawn centred in a 40 wide box: "Hello " (its space counted) then "World"
	Clear();
	SetRect(&box, 0, 0, 40, 32);
	TextBox(rich, RefVar(MAKEINT(PackFont(kEspy, 12, 0))), box, 2, 0, 1);
	InkExtent(0, 16, &inkLeft, &inkRight);
	EXPECT(inkLeft == (40 - 30) / 2 && inkRight <= 40);
	InkExtent(16, 32, &inkLeft, &inkRight);
	EXPECT(inkLeft == (40 - (width - 30)) / 2 && inkRight <= 40);
	// vertically centred in a box 48 high (the map is 32): the first line
	// starts 8 lower; at the bottom of a 40 high box, 4 lower
	Clear();
	SetRect(&box, 0, 0, 40, 48);
	TextBox(rich, RefVar(MAKEINT(PackFont(kEspy, 12, 0))), box, 0, 4, 1);
	InkExtent(0, 8, &inkLeft, &inkRight);
	EXPECT(inkRight == 0);
	InkExtent(8, 24, &inkLeft, &inkRight);
	EXPECT(inkLeft == 0 && inkRight > 0);
	Clear();
	SetRect(&box, 0, 0, 40, 40);
	TextBox(rich, RefVar(MAKEINT(PackFont(kEspy, 12, 0))), box, 0, 8, 1);
	InkExtent(0, 4, &inkLeft, &inkRight);
	EXPECT(inkRight == 0);
	InkExtent(4, 20, &inkLeft, &inkRight);
	EXPECT(inkLeft == 0 && inkRight > 0);
	// the NewtonScript TextBox
	Clear();
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "viewBounds")), RefVar(ToObject(box)));
	Eval("TextBox(\"Hi\", {font: espy12, justification: 'right}, {left: 0, top: 0, right: 60, bottom: 16})");
	InkExtent(0, 16, &inkLeft, &inkRight);
	EXPECT(inkRight == 60 || inkRight == 59);
}


// The style table the synthesised faces are made with, scaled to the
// size a font is drawn at.
static void
TestStyleTable()
{
	// drawn as it is, the table itself is answered
	EXPECT(UpdateStyleTable(ToFixed(1), ToFixed(1)) == kStyleTable);

	// drawn at twice the size, the measurements double and the rest is
	// left alone
	const unsigned char* twice = UpdateStyleTable(ToFixed(2), ToFixed(2));
	EXPECT(twice != kStyleTable);
	EXPECT(twice[3] == kStyleTable[3] * 2);			// the bold smear
	EXPECT(twice[0x17] == kStyleTable[0x17] * 2);	// the underline's offset
	EXPECT(twice[0x18] == kStyleTable[0x18] * 2);	// and its thickness
	// which adjustment each face touches is not a measurement
	EXPECT(twice[2] == kStyleTable[2]);
	EXPECT(twice[5] == kStyleTable[5]);
	EXPECT(twice[8] == kStyleTable[8]);

	// the same scale answers the same table without making it again
	EXPECT(UpdateStyleTable(ToFixed(2), ToFixed(2)) == twice);

	// the two axes are taken separately: the bold smear is horizontal,
	// the underline's offset vertical, and its thickness neither - it
	// goes by the mean of the two, which for three and one is two
	const unsigned char* wide = UpdateStyleTable(ToFixed(3), ToFixed(1));
	EXPECT(wide[3] == kStyleTable[3] * 3);
	EXPECT(wide[0x17] == kStyleTable[0x17]);
	EXPECT(wide[0x19] == kStyleTable[0x19]);
	EXPECT(wide[0x18] == kStyleTable[0x18] * 2);

	// (BUG, kept: the one negative entry is read unsigned, so scaling it
	//  gives 255 times the scale rather than minus the scale.  It is
	//  0xff at 0x13 - what an italic adds to the width, which is
	//  nothing - and halved it becomes 127 rather than staying -1.)
	EXPECT(kStyleTable[0x13] == 0xff);
	const unsigned char* half = UpdateStyleTable(ToFixed(1) / 2, ToFixed(1) / 2);
	EXPECT(half[0x13] == 0x80);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Text: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	InitFonts();
	RegisterTextNatives();
	InstallHostNatives();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	// what the boot makes: vars.fonts, the ROM's font families by their
	// family symbols ('espy, 'newYork, 'geneva, 'handwriting: the ROM's
	// globals template has fonts: {_proto: {espy: @80, ...}})
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
	{
		RefVar family(GetArraySlotRef(list, i));
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), family);
	}
	SetFrameSlot(RefVar(gVarFrame), RSSYMfonts, fonts);
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, RefVar(AllocateFrame()));
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
		TestFonts();
		TestDrawing();
		TestItalic();
		TestWordBreaks();
		TestNatives();
		TestLayout();
		TestStyleTable();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	if (failures == 0)
		printf("test_Text: all passed\n");
	else
		printf("test_Text: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
