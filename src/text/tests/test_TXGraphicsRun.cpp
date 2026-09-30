// TXGraphicsRun test (src/text/TXGraphicsRun.h): a picture standing in the
// text as one character - its box and height, how it breaks a line and
// is squeezed onto an empty one, where a tap on it lands, its XOR frame
// when selected, and the Newton's own, a NewtonScript shape drawn into
// an offscreen port.  The ROM image is imported for the shapes' styles.
#include "TXGraphicsRun.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "Ports.h"
#include "Rects.h"
#include "Regions.h"
#include "Draw.h"
#include "DrawShape.h"
#include "Fonts.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kWidth = 128;
const long kHeight = 64;
static unsigned char gBits[kWidth * kHeight / 8];
static PixelMap gMap;
static GrafPort gPort;


static Boolean
Pixel(long x, long y)
{
	return (gBits[y * (kWidth / 8) + x / 8] & (0x80 >> (x % 8))) != 0;
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


// a graphics run 30 wide and 20 high that records where it was drawn
class TTestRun : public TXGraphicsRun
{
public:
	virtual TXAttrObject* CreateNew(void) const		{ return new TTestRun; }
	virtual long	GetClassId(void) const			{ return 'test'; }
	virtual Ref		GetNSObject(void) const			{ return NILREF; }
	virtual void	SetNSObject(RefArg)				{ }
	virtual void	GetDimensions(int* height, int* width)	{ *height = 20; *width = 30; }
	virtual void	DrawContent(const Rect& box)	{ fDrawn = box; }
	Rect			fDrawn;
};


static void
TestBase()
{
	TTestRun* run = new TTestRun;
	EXPECT(!run->IsTextRun());
	EXPECT(run->GetObjFlags() == 7);							// a thing in the text, with a margin
	EXPECT(run->GetHiliteInset() == 2);

	// the size, the margins included, all of it above the baseline
	int height, width;
	run->GetTotalDimensions(&height, &width);
	EXPECT(height == 24 && width == 34);
	int ascent, descent, leading;
	run->GetHeightInfo(&ascent, &descent, &leading);
	EXPECT(ascent == 24 && descent == 0 && leading == 0);
	UniChar placeholder[2] = { 0x01, 0 };
	TXLineRunDisplayInfo info = { placeholder, 1, 34 << 16, 0 };
	EXPECT(run->MeasureWidth(info) == (34 << 16));

	// a line with room, exactly enough, and too little
	Fixed room = 100 << 16;
	long length = -1;
	EXPECT(run->LineBreak(placeholder, 1, 0, &room, false, &length) == 2);
	EXPECT(room == (66 << 16) && length == 1);
	room = 34 << 16;
	EXPECT(run->LineBreak(placeholder, 1, 0, &room, false, &length) == 0);
	EXPECT(room == 0 && length == 1);
	room = 20 << 16;
	EXPECT(run->LineBreak(placeholder, 1, 0, &room, false, &length) == 0);
	EXPECT(length == 0 && run->fExtraWidth == 0);
	room = 20 << 16;
	EXPECT(run->LineBreak(placeholder, 1, 0, &room, true, &length) == 1);	// an empty line: squeezed on
	EXPECT(length == 1 && run->fExtraWidth == -14);
	run->GetTotalDimensions(&height, &width);
	EXPECT(width == 20);
	room = 100 << 16;
	run->LineBreak(placeholder, 1, 0, &room, true, &length);	// the squeeze undone
	EXPECT(run->fExtraWidth == 0);

	// a tap: in front of it, on it, after it (the margins a quarter of it)
	TXOffsetRange range;
	run->PixelToChar(info, 5 << 16, &range);
	EXPECT(range.fStart.fOffset == 0 && range.fEnd.fOffset == 0 && !range.fStart.fAtStart);
	run->PixelToChar(info, 10 << 16, &range);
	EXPECT(range.fStart.fOffset == 0 && range.fEnd.fOffset == 1 && !range.fStart.fAtStart && range.fEnd.fAtStart);
	run->PixelToChar(info, 30 << 16, &range);
	EXPECT(range.fStart.fOffset == 1 && range.fEnd.fOffset == 1 && range.fStart.fAtStart);
	UniChar letter[2] = { 'A', 0 };
	TXLineRunDisplayInfo lettered = { letter, 1, 34 << 16, 0 };
	run->PixelToChar(lettered, 1 << 16, &range);				// no margins for a printing character
	EXPECT(range.fStart.fOffset == 0 && range.fEnd.fOffset == 1);
	EXPECT(run->CharToPixel(info, 0) == 0);
	EXPECT(run->CharToPixel(info, 1) == (34 << 16));

	// its box on a line: standing on the line's bottom, less the margin
	TXRunPositionInfo where = { 10, 30, 5 << 16, 34 << 16 };
	Rect box;
	run->GetRunRect(where, &box);
	EXPECT(box.top == 18 && box.left == 7 && box.bottom == 38 && box.right == 37);
	Rect line;
	SetRect(&line, 0, 10, 100, 40);
	run->Draw(info, 5 << 16, line, 0);
	EXPECT(run->fDrawn.top == 18 && run->fDrawn.left == 7 && run->fDrawn.bottom == 38 && run->fDrawn.right == 37);

	// selected: a frame in XOR round the whole box, taken away by drawing it again
	memset(gBits, 0, sizeof(gBits));
	run->SetHilite(2, where, true);
	EXPECT(run->fHilite == 2);
	EXPECT(CountBits() > 0);
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
			if (Pixel(x, y))
				EXPECT(y >= 16 && y < 40 && x >= 5 && x < 39 && (y == 16 || y == 39 || x == 5 || x == 38));
	run->SetHilite(0, where, true);
	EXPECT(run->fHilite == 0 && CountBits() == 0);
	run->SetHilite(1, where, false);								// not drawn
	EXPECT(run->fHilite == 1 && CountBits() == 0);

	// never shared: a reference is a copy
	TXAttrObject* ref = run->Reference();
	EXPECT(ref != run && ref->GetClassId() == 'test');
	ref->Free();
	run->Free();
}


static void
TestNewt()
{
	TXNewtGraphicsRun* run = new TXNewtGraphicsRun;
	EXPECT(run->GetClassId() == 'graf' && run->GetPublicType() == 'shap');
	EXPECT(run->GetObjFlags() == 6 && run->GetHiliteInset() == 0);
	EXPECT(run->GetAttributeFlags('graf') & 3);
	int height, width;
	run->GetDimensions(&height, &width);
	EXPECT(height == 16 && width == 16);						// nothing to show

	// a rectangle 20 wide and 10 high: two pixels round it
	RefVar object(AllocateFrame());
	RefVar shape(MakeRectShape(RSSYMrectangle, RefVar(MAKEINT(40)), RefVar(MAKEINT(30)), RefVar(MAKEINT(60)), RefVar(MAKEINT(40))));
	SetFrameSlot(object, RSSYMshape, shape);
	run->SetNSObject(object);
	EXPECT(EQRef(run->GetNSObject(), object));
	run->GetDimensions(&height, &width);
	EXPECT(height == 14 && width == 24);

	// drawn two pixels into its box, whatever the shape's own position
	memset(gBits, 0, sizeof(gBits));
	UniChar placeholder[2] = { 0x01, 0 };
	TXLineRunDisplayInfo info = { placeholder, 1, 24 << 16, 0 };
	Rect line;
	SetRect(&line, 0, 20, 100, 50);
	run->Draw(info, 10 << 16, line, 0);							// box {36, 10, 50, 34}
	EXPECT(Pixel(12, 38) && Pixel(31, 38) && Pixel(12, 47) && Pixel(31, 47));	// the frame's corners
	EXPECT(!Pixel(11, 38) && !Pixel(12, 37) && !Pixel(32, 38) && !Pixel(12, 48));
	EXPECT(!Pixel(20, 42));										// framed, not filled

	// a copy shares the object
	TXNewtGraphicsRun* copy = (TXNewtGraphicsRun*) run->Reference();
	EXPECT(copy != run && EQRef(copy->GetNSObject(), object));
	copy->Free();
	run->Free();
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_TXGraphicsRun: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	InitFonts();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
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
		TestBase();
		TestNewt();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	printf("test_TXGraphicsRun: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
