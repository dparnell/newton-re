// The questions a text object is asked (qd/TextObject.h): the length that
// fits a width, where a character starts (CharToPoint) and which
// character a point is nearest (PointToChar), answered by the standard
// text proc over the same layout the drawing uses - checked against
// MeasureTextOnce's widths of the same text in the ROM's own font.
#include "TextObject.h"
#include "Text.h"
#include "Fonts.h"
#include "Draw.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kWidth = 128;
const long kHeight = 32;
static unsigned char gBits[kWidth * kHeight / 8];
static PixelMap gMap;
static GrafPort gPort;


// the width of the first n characters, by MeasureTextOnce
static Fixed
WidthOf(const UniChar* text, long n, StyleRecord* style)
{
	StyleRecord* styles[1] = { style };
	TextBoundsInfo bounds;
	FPoint zero = { 0, 0 };
	MeasureTextOnce(text, n, styles, nil, zero, nil, &bounds);
	return bounds.fWidth;
}


static void
TestQuestions()
{
	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(0, 12, 0))), &style);	// the ROM's first font, 12
	static const UniChar kText[] = { 'W', 'i', 'd', 'e', ' ', 'm', 'o', 'n', 'k', 0 };
	const long kLength = 9;
	StyleRecord* styles[1] = { &style };
	FPoint at = { 10 << 16, 20 << 16 };

	TextObjectRef text = NewText(kText, kLength, styles, nil, at, nil);
	EXPECT(text != 0);

	// where each character starts: the location plus the advance before it
	for (long i = 0; i <= kLength; i++)
	{
		FPoint p;
		CharToPoint(text, i, &p);
		EXPECT(p.x == at.x + WidthOf(kText, i, &style));
		EXPECT(p.y == at.y);
	}

	// which boundary a point is nearest: a point just past the middle of a
	// character goes after it, just before the middle before it
	for (long i = 0; i < kLength; i++)
	{
		Fixed left = WidthOf(kText, i, &style);
		Fixed w = WidthOf(kText, i + 1, &style) - left;
		FPoint before = { at.x + left + (w >> 1) - 0x8000, at.y };
		FPoint after = { at.x + left + (w >> 1) + 0x8000, at.y };
		EXPECT(PointToChar(text, before) == i);
		EXPECT(PointToChar(text, after) == i + 1);
	}
	FPoint left = { at.x - (5 << 16), at.y };
	EXPECT(PointToChar(text, left) == 0);						// before the start
	FPoint right = { at.x + (200 << 16), at.y };
	EXPECT(PointToChar(text, right) == kLength);				// past the end

	// the other fields
	const void* chars = nil;
	GetTextObjField(text, kTextObjText, &chars);
	EXPECT(chars == kText);
	FPoint where;
	GetTextObjField(text, kTextObjLocation, &where);
	EXPECT(where.x == at.x && where.y == at.y);
	StyleRecord** gotStyles = nil;
	GetTextObjField(text, kTextObjStyles, &gotStyles);
	EXPECT(gotStyles == styles);
	long fitted = -1;
	GetTextObjField(text, kTextObjFittedLength, &fitted);
	EXPECT(fitted == kLength);									// no width to fit: all of it
	DisposeText(text);

	// with a width to fit: the length cut to what fits
	TextOptions options;
	memset(&options, 0, sizeof(options));
	Fixed four = WidthOf(kText, 4, &style);
	options.fWidth = four + 0x10000;							// "Wide" and a pixel: not the space as well
	text = NewText(kText, kLength, styles, nil, at, &options);
	fitted = -1;
	TQDLibraryDriver::GetTextObjField(text, kTextObjFittedLength, &fitted);
	Fixed five = WidthOf(kText, 5, &style);
	EXPECT(fitted == (five <= options.fWidth ? 5 : 4));
	FPoint p;
	TQDLibraryDriver::CharToPoint(text, 2, &p);
	EXPECT(p.x == at.x + WidthOf(kText, 2, &style));
	EXPECT(TQDLibraryDriver::PointToChar(text, at) == 0);
	DisposeText(text);

	// right-aligned in a wider box: the characters start further in
	memset(&options, 0, sizeof(options));
	options.fWidth = 100 << 16;
	options.fAlignment = 0x10000;
	text = NewText(kText, kLength, styles, nil, at, &options);
	Fixed whole = WidthOf(kText, kLength, &style);
	CharToPoint(text, 0, &p);
	EXPECT(p.x == at.x + (options.fWidth - whole));
	FPoint rightEdge = { at.x + options.fWidth - 0x8000, at.y };
	EXPECT(PointToChar(text, rightEdge) == kLength);
	DisposeText(text);
	DisposeStyleRecord(&style);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_TextObject: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	InitFonts();
	InstallHostNatives();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), RefVar(GetArraySlotRef(list, i)));
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
		TestQuestions();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	if (failures == 0)
		printf("test_TextObject: all passed\n");
	return failures != 0;
}
