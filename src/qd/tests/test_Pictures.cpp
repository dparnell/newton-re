// Bitmap frame test: asking a picture about a point.  PtInPicture answers
// on the bits when the bitmap has no mask and on the mask when it has one;
// GetBitmapPixel answers the pixel under the point, or -1 where the mask
// says there is no picture.  Also DrawBitmap into an offscreen map, so
// that what the point test says and what is drawn can be compared.
// Runs over a standalone kernel heap and object heap without ROM objects.
#include "Pictures.h"
#include "Draw.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ByteOrder.h"
#include "NewtonExceptions.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// a 'bits binary: an 8 x 4 one-bit picture with the given rows (a
// persistent format, so its halfwords are big-endian)
static Ref
MakeBits(const unsigned char* rows, long width, long height)
{
	long rowBytes = ((width + 31) / 32) * 4;
	RefVar bits(AllocateBinary(RefVar(RSSYMbits), kFramBitmapHeaderSize + rowBytes * height));
	unsigned char* data = (unsigned char*) BinaryData(bits);
	memset(data, 0, kFramBitmapHeaderSize + rowBytes * height);
	PutBigEndianHalf(data + 4, (unsigned short) rowBytes);
	PutBigEndianHalf(data + 8, 0);						// top
	PutBigEndianHalf(data + 10, 0);						// left
	PutBigEndianHalf(data + 12, (unsigned short) height);
	PutBigEndianHalf(data + 14, (unsigned short) width);
	for (long y = 0; y < height; y++)
		data[kFramBitmapHeaderSize + y * rowBytes] = rows[y];
	return bits;
}


static Ref
MakeBitmap(const unsigned char* rows, const unsigned char* mask, long width, long height)
{
	RefVar frame(AllocateFrame());
	RefVar bounds(AllocateFrame());			// (by hand: ToObject wants the ROM's rectangle prototype)
	SetFrameSlot(bounds, RSSYMtop, RefVar(MAKEINT(0)));
	SetFrameSlot(bounds, RSSYMleft, RefVar(MAKEINT(0)));
	SetFrameSlot(bounds, RSSYMbottom, RefVar(MAKEINT(height)));
	SetFrameSlot(bounds, RSSYMright, RefVar(MAKEINT(width)));
	SetFrameSlot(frame, RSSYMbounds, bounds);
	SetFrameSlot(frame, RSSYMbits, RefVar(MakeBits(rows, width, height)));
	if (mask != nil)
		SetFrameSlot(frame, RSSYMmask, RefVar(MakeBits(mask, width, height)));
	return frame;
}


static Boolean
In(RefArg bitmap, long x, long y)
{
	return NOTNIL(RefVar(PtInPicture(RefVar(MAKEINT(x)), RefVar(MAKEINT(y)), bitmap, false)));
}


static long
PixelAt(RefArg bitmap, long x, long y)
{
	return RINT(PtInPicture(RefVar(MAKEINT(x)), RefVar(MAKEINT(y)), bitmap, true));
}


// a hollow 4 x 4 box in the left of an 8 wide row
static const unsigned char kBox[4] = { 0xf0, 0x90, 0x90, 0xf0 };
// a solid 4 x 4 square, the box's outline filled in
static const unsigned char kSolid[4] = { 0xf0, 0xf0, 0xf0, 0xf0 };


// With no mask the bits are the picture: a point is in it where a pixel
// is set.
static void
TestWithoutMask()
{
	RefVar bitmap(MakeBitmap(kBox, nil, 8, 4));
	EXPECT(In(bitmap, 0, 0) && In(bitmap, 3, 0) && In(bitmap, 0, 3) && In(bitmap, 3, 3));
	EXPECT(!In(bitmap, 1, 1) && !In(bitmap, 2, 2));			// the hollow middle
	EXPECT(!In(bitmap, 4, 0) && !In(bitmap, 7, 3));			// the empty right half
	// outside the map altogether
	EXPECT(!In(bitmap, -1, 0) && !In(bitmap, 0, -1) && !In(bitmap, 8, 0) && !In(bitmap, 0, 4));

	// and the pixels themselves
	EXPECT(PixelAt(bitmap, 0, 0) == 1 && PixelAt(bitmap, 1, 1) == 0);
	EXPECT(PixelAt(bitmap, -1, 0) == -1 && PixelAt(bitmap, 8, 0) == -1);
}


// With a mask the mask is the picture's shape, whatever the bits say.
static void
TestWithMask()
{
	RefVar bitmap(MakeBitmap(kBox, kSolid, 8, 4));
	// the middle is hollow in the bits but filled in the mask, so it is in
	EXPECT(In(bitmap, 1, 1) && In(bitmap, 2, 2));
	EXPECT(!In(bitmap, 4, 0));								// outside the mask
	EXPECT(!In(bitmap, 8, 0) && !In(bitmap, 0, -1));

	// the pixel comes from the bits, and -1 where the mask is clear
	EXPECT(PixelAt(bitmap, 0, 0) == 1);
	EXPECT(PixelAt(bitmap, 1, 1) == 0);						// in the mask, clear in the bits
	EXPECT(PixelAt(bitmap, 4, 0) == -1);					// outside the mask
	EXPECT(PixelAt(bitmap, 9, 9) == -1);

	// the other way round: a mask narrower than the bits
	static const unsigned char kLeftHalf[4] = { 0xc0, 0xc0, 0xc0, 0xc0 };
	RefVar narrow(MakeBitmap(kSolid, kLeftHalf, 8, 4));
	EXPECT(In(narrow, 0, 0) && In(narrow, 1, 3));
	EXPECT(!In(narrow, 2, 0) && !In(narrow, 3, 3));
	EXPECT(PixelAt(narrow, 1, 0) == 1 && PixelAt(narrow, 2, 0) == -1);
}


// A picture that is not a bitmap throws, and the TPixelObj is given back
// through the exception handler rather than a destructor.
static void
TestNotABitmap()
{
	RefVar picture(AllocateFrame());
	SetFrameSlot(picture, RSSYMclass, RefVar(RSSYMpicture));
	Boolean threw = false;
	newton_try
	{
		PtInPicture(RefVar(MAKEINT(0)), RefVar(MAKEINT(0)), picture, false);
	}
	newton_catch_all
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);
}


// What the point test says and what DrawBitmap draws must agree.
static void
TestDrawnAgrees()
{
	static const long kSize = 16;
	unsigned char bits[kSize * kSize / 8];
	PixelMap pm;
	pm.baseAddr = (Ptr) bits;
	pm.rowBytes = kSize / 8;
	SetRect(&pm.bounds, 0, 0, kSize, kSize);
	pm.pixMapFlags = kPixMapPtr | 1;
	pm.deviceRes.v = kDefaultDPI;
	pm.deviceRes.h = kDefaultDPI;
	pm.grayTable = nil;
	memset(bits, 0, sizeof(bits));

	GrafPort port;
	OpenPort(&port);
	SetPortBits(&pm);
	port.portRect = pm.bounds;
	RectRgn(port.visRgn, &pm.bounds);
	RefVar bitmap(MakeBitmap(kBox, nil, 8, 4));
	Rect box;
	SetRect(&box, 0, 0, 0, 0);
	DrawBitmap(bitmap, &box, srcCopy);
	for (long y = 0; y < 4; y++)
		for (long x = 0; x < 8; x++)
			EXPECT((GetPixel(&pm, x, y) != 0) == In(bitmap, x, y));
	ClosePort(&port);
}


int
main()
{
	InitHostStandaloneHeap();
	InitGraf();
	gObjectHeapSize = 0x100000;
	InitObjects();

	TestWithoutMask();
	TestWithMask();
	TestNotABitmap();
	TestDrawnAgrees();

	printf("test_Pictures: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
