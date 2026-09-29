// Picture playback test: small QuickDraw pictures, written out by hand in
// their big-endian bytes, played into an offscreen one-bit map - a
// version 1 picture with a clip, a painted rectangle and an unpacked
// bitmap, the same scaled up, a packed bitmap, a line, the empty clip a
// picture starts with, and a version 2 picture with its word opcodes;
// then a picture recorded (OpenPicture, every standard proc, ClosePicture)
// and played back to the same pixels as the scene drawn directly, and
// PackBits against UnpackBits.  Runs over a standalone kernel heap with
// the ROM's objects imported (a recorded picture's text style begins in
// the system font).
#include "PicPlay.h"
#include "PicRecord.h"
#include "Shapes.h"
#include "Polygons.h"
#include "Ports.h"
#include "Draw.h"
#include "Rects.h"
#include "Regions.h"
#include "ByteOrder.h"
#include "NewtonMemory.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "Fonts.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const long kSize = 64;
static unsigned char gBits[kSize * kSize / 8];
static PixelMap gMap;
static GrafPort gPort;


// a picture being written: bytes and big-endian words
struct PictureWriter
{
	unsigned char	fData[512];
	long			fSize;

	PictureWriter()			{ fSize = 2; }
	void	Byte(long b)	{ fData[fSize++] = (unsigned char) b; }
	void	Word(long w)	{ PutBigEndianHalf(fData + fSize, (unsigned short) w); fSize += 2; }
	void	Rect4(long top, long left, long bottom, long right)	{ Word(top); Word(left); Word(bottom); Word(right); }
	void	Finish()		{ PutBigEndianHalf(fData, (unsigned short) fSize); }
};


static void
ClearMap()
{
	memset(gBits, 0, sizeof(gBits));
}


static void
Play(PictureWriter& w, long top, long left, long bottom, long right)
{
	w.Finish();
	Ptr data = (Ptr) w.fData;
	Rect dst;
	SetRect(&dst, left, top, right, bottom);
	DrawPicture((PicHandle) &data, &dst, false);
}


static long
Ink(long left, long top, long right, long bottom)
{
	long count = 0;
	for (long y = top; y < bottom; y++)
		for (long x = left; x < right; x++)
			count += GetPixel(&gMap, x, y) != 0;
	return count;
}


// a version 1 picture 16 square: the clip, a rectangle painted at 4..8,
// and a two-row bitmap (unpacked: its rows are under 8 bytes) at 12..14
static void
WriteVersion1(PictureWriter& w, Boolean withClip)
{
	w.Rect4(0, 0, 16, 16);							// the frame
	w.Byte(0x11);									// Version 1
	w.Byte(0x01);
	if (withClip)
	{
		w.Byte(0x01);								// ClipRgn: a rectangle
		w.Word(10);
		w.Rect4(0, 0, 16, 16);
	}
	w.Byte(0x31);									// paintRect
	w.Rect4(4, 4, 8, 8);
	w.Byte(0x90);									// BitsRect
	w.Word(2);										// rowBytes
	w.Rect4(0, 0, 2, 16);							// bounds
	w.Rect4(0, 0, 2, 16);							// srcRect
	w.Rect4(12, 0, 14, 16);							// dstRect
	w.Word(srcCopy);
	w.Byte(0xff);									// the rows
	w.Byte(0x00);
	w.Byte(0x00);
	w.Byte(0xff);
	w.Byte(0xff);									// OpEndPic
}


static void
TestVersion1()
{
	ClearMap();
	PictureWriter w;
	WriteVersion1(w, true);
	Play(w, 0, 0, 16, 16);
	EXPECT(Ink(4, 4, 8, 8) == 16 && GetPixel(&gMap, 3, 3) == 0 && GetPixel(&gMap, 8, 8) == 0);
	EXPECT(Ink(0, 12, 8, 13) == 8 && Ink(8, 12, 16, 13) == 0);
	EXPECT(Ink(0, 13, 8, 14) == 0 && Ink(8, 13, 16, 14) == 8);
	EXPECT(Ink(0, 0, kSize, kSize) == 16 + 16);
	// the port is given back as it was
	EXPECT(GetCurrentPort()->pnMode == patCopy && qdGlobals.fPicHandle == nil && qdGlobals.fPicOffset == 0);
}


// The frame mapped onto a rectangle twice the size doubles everything.
static void
TestScaled()
{
	ClearMap();
	PictureWriter w;
	WriteVersion1(w, true);
	Play(w, 0, 0, 32, 32);
	EXPECT(Ink(8, 8, 16, 16) == 64 && Ink(0, 0, 8, 8) == 0);
	EXPECT(Ink(0, 24, 16, 26) == 32 && Ink(16, 24, 32, 26) == 0);
	EXPECT(Ink(16, 26, 32, 28) == 32);
}


// A picture starts with an empty clip: without a ClipRgn nothing shows.
static void
TestNoClip()
{
	ClearMap();
	PictureWriter w;
	WriteVersion1(w, false);
	Play(w, 0, 0, 16, 16);
	EXPECT(Ink(0, 0, kSize, kSize) == 0);
}


// PackBitsRect: rows of eight bytes or more come packed, a count byte
// before each; a repeat run and a literal run, and only the rows the
// destination shows unpacked.
static void
TestPacked()
{
	ClearMap();
	PictureWriter w;
	w.Rect4(0, 0, 3, 64);
	w.Byte(0x11);
	w.Byte(0x01);
	w.Byte(0x01);
	w.Word(10);
	w.Rect4(0, 0, 3, 64);
	w.Byte(0x98);									// PackBitsRect
	w.Word(8);
	w.Rect4(0, 0, 3, 64);
	w.Rect4(0, 0, 3, 64);
	w.Rect4(0, 0, 3, 64);
	w.Word(srcCopy);
	w.Byte(2);										// row 0: 0xff eight times
	w.Byte(0xf9);
	w.Byte(0xff);
	w.Byte(9);										// row 1: eight bytes as they are
	w.Byte(7);
	for (long i = 0; i < 8; i++)
		w.Byte(i & 1 ? 0x0f : 0xf0);
	w.Byte(2);										// row 2: nothing
	w.Byte(0xf9);
	w.Byte(0x00);
	w.Byte(0xff);
	Play(w, 0, 0, 3, 64);
	EXPECT(Ink(0, 0, 64, 1) == 64);
	EXPECT(Ink(0, 1, 64, 2) == 32 && GetPixel(&gMap, 0, 1) != 0 && GetPixel(&gMap, 4, 1) == 0 && GetPixel(&gMap, 12, 1) != 0);
	EXPECT(Ink(0, 2, 64, 3) == 0);
}


// Line from a point to a point, then ShortLineFrom a byte each way from
// where it ended.
static void
TestLines()
{
	ClearMap();
	PictureWriter w;
	w.Rect4(0, 0, 16, 16);
	w.Byte(0x11);
	w.Byte(0x01);
	w.Byte(0x01);
	w.Word(10);
	w.Rect4(0, 0, 16, 16);
	w.Byte(0x20);									// Line (0, 2) to (0, 9): v then h
	w.Word(2);
	w.Word(0);
	w.Word(9);
	w.Word(0);
	w.Byte(0x23);									// ShortLineFrom: 5 across
	w.Byte(5);
	w.Byte(0);
	w.Byte(0xff);
	Play(w, 0, 0, 16, 16);
	EXPECT(Ink(0, 2, 1, 10) == 8 && GetPixel(&gMap, 0, 1) == 0);
	EXPECT(Ink(0, 9, 6, 10) == 6);
}


// Version 2: word opcodes, a header opcode passed over by its length.
static void
TestVersion2()
{
	ClearMap();
	PictureWriter w;
	w.Rect4(0, 0, 16, 16);
	w.Word(0x0011);									// VersionOp
	w.Word(0x02ff);
	w.Word(0x0c00);									// HeaderOp: 24 bytes
	for (long i = 0; i < 12; i++)
		w.Word(0);
	w.Word(0x0001);
	w.Word(10);
	w.Rect4(0, 0, 16, 16);
	w.Word(0x0031);
	w.Rect4(2, 2, 4, 6);
	w.Word(0x00a0);									// ShortComment
	w.Word(100);
	w.Word(0x00ff);
	Play(w, 0, 0, 16, 16);
	EXPECT(Ink(2, 2, 6, 4) == 8 && Ink(0, 0, kSize, kSize) == 8);
}


// PackBits itself: a repeat, a literal and the -128 that is nothing.
static void
TestUnpackBits()
{
	char packed[] = { (char) 0xfe, (char) 0xaa, (char) 0x80, 1, 1, 2 };
	char out[5];
	char* src = packed;
	char* dst = out;
	UnpackBits(&src, &dst, 5);
	EXPECT(dst == out + 5 && src == packed + 6);
	EXPECT((unsigned char) out[0] == 0xaa && (unsigned char) out[2] == 0xaa && out[3] == 1 && out[4] == 2);
}


// the scene the recording tests draw: every standard proc a picture
// records - rectangles in each verb, an oval, a round rectangle, an arc, a
// polygon, a region, lines long and short, a bitmap packed and one not,
// a comment - with the pen's size, mode and pattern changing between them
static unsigned char gSceneBits[4 * 8];		// a 32 x 4 bitmap (packed: 4 row bytes... see below)
static unsigned char gWideBits[16 * 3];		// a 128 x 3 bitmap: 16 row bytes, packed
static void
Scene()
{
	PenNormal();
	Rect r;
	SetRect(&r, 2, 2, 20, 12);
	FrameRect(&r);
	PenSize(2, 2);
	SetRect(&r, 24, 2, 40, 12);
	FrameRect(&r);
	PenSize(1, 1);
	SetRect(&r, 44, 2, 60, 12);
	PaintRect(&r);
	SetRect(&r, 46, 4, 58, 10);
	EraseRect(&r);
	SetRect(&r, 48, 5, 56, 9);
	InvertRect(&r);
	InvertRect(&r);							// the same rectangle again: recorded as "the same"
	InvertRect(&r);
	SetRect(&r, 2, 16, 20, 30);
	PaintOval(&r);
	SetRect(&r, 24, 16, 40, 30);
	FrameRoundRect(&r, 6, 6);
	SetRect(&r, 44, 16, 60, 30);
	FillRect(&r, GetStdPattern(grayPat));
	MoveTo(2, 34);
	LineTo(60, 34);							// long
	LineTo(60, 40);							// from where it ended, short
	MoveTo(2, 40);
	LineTo(20, 44);
	PolyHandle poly = OpenPoly();
	MoveTo(24, 36);
	LineTo(40, 36);
	LineTo(32, 44);
	LineTo(24, 36);
	ClosePoly();
	PaintPoly(poly);
	KillPoly(poly);
	RgnHandle rgn = NewRgn();
	SetRect(&r, 44, 36, 60, 44);
	RectRgn(rgn, &r);
	PaintRgn(rgn);
	DisposeRgn(rgn);
	PixelMap pm;
	pm.baseAddr = (Ptr) gSceneBits;
	pm.rowBytes = 4;
	SetRect(&pm.bounds, 0, 0, 32, 4);
	pm.pixMapFlags = kPixMapPtr | 1;
	pm.deviceRes.v = kDefaultDPI;
	pm.deviceRes.h = kDefaultDPI;
	pm.grayTable = nil;
	Rect src, dst;
	SetRect(&src, 0, 0, 32, 4);
	SetRect(&dst, 2, 48, 34, 52);
	CopyBits(&pm, &gMap, &src, &dst, srcCopy, nil);
	pm.baseAddr = (Ptr) gWideBits;
	pm.rowBytes = 16;
	SetRect(&pm.bounds, 0, 0, 128, 3);
	SetRect(&src, 0, 0, 64, 3);
	SetRect(&dst, 0, 56, 64, 59);
	CopyBits(&pm, &gMap, &src, &dst, srcCopy, nil);
	PicComment(100, 0, nil);
	PenNormal();
}


// A picture recorded: nothing drawn while it is open, and played back it
// draws what the scene drew; its opcodes are what the procs write.
static void
TestRecord()
{
	for (long i = 0; i < (long) sizeof(gSceneBits); i++)
		gSceneBits[i] = (unsigned char) (i * 37 + 11);
	for (long i = 0; i < (long) sizeof(gWideBits); i++)
		gWideBits[i] = (unsigned char) (i < 20 ? 0xff : i * 13);
	ClearMap();
	Scene();
	unsigned char direct[sizeof(gBits)];
	memcpy(direct, gBits, sizeof(gBits));
	EXPECT(Ink(0, 0, kSize, kSize) > 500);

	ClearMap();
	Rect frame;
	SetRect(&frame, 0, 0, kSize, kSize);
	PicHandle picture = OpenPicture(&frame, false);
	EXPECT(picture != nil && gPort.picSave != nil);
	EXPECT(OpenPicture(&frame, false) == nil);		// one at a time
	Scene();
	ClosePicture();
	EXPECT(gPort.picSave == nil && gPort.pnVis == 0);
	EXPECT(Ink(0, 0, kSize, kSize) == 0);			// the pen was hidden
	long size = GetHandleSize((Handle) picture);
	const unsigned char* p = (const unsigned char*) *picture;
	EXPECT(GetBigEndianHalf(p) == size && size > 100);
	EXPECT(GetBigEndianHalf(p + 2) == 0 && GetBigEndianHalf(p + 8) == kSize);
	EXPECT(GetBigEndianHalf(p + 10) == 0x0011 && GetBigEndianHalf(p + 12) == 0x02ff);
	EXPECT(GetBigEndianHalf(p + 14) == 0x0001);		// the clip, first
	EXPECT(p[size - 2] == 0x00 && p[size - 1] == 0xff);
	// the rectangle inverted three times: invertRect (0x33) once, then
	// invertSameRect (0x3b) twice with nothing after it
	long same = 0, packed = 0;
	for (long i = 10; i + 1 < size; i += 2)
	{
		if (GetBigEndianHalf(p + i) == 0x003b)
			same++;
		if (GetBigEndianHalf(p + i) == 0x0098)
			packed++;
	}
	EXPECT(same >= 2 && packed >= 1);

	Rect dst = frame;
	DrawPicture(picture, &dst, false);
	EXPECT(memcmp(direct, gBits, sizeof(gBits)) == 0);
	if (memcmp(direct, gBits, sizeof(gBits)) != 0)
		for (long y = 0; y < kSize; y++)
			for (long x = 0; x < kSize; x++)
				if ((GetPixel(&gMap, x, y) != 0) != ((direct[y * 8 + x / 8] & (0x80 >> (x & 7))) != 0))
				{
					fprintf(stderr, "  first difference at (%ld,%ld)\n", x, y);
					y = kSize;
					break;
				}
	KillPicture(picture);
}


// PackBits, the ROM's: runs and literals, and back out through UnpackBits
static void
TestPackBits()
{
	const unsigned char rows[][16] = {
		{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0xaa, 0xaa, 0xaa, 1, 2, 2, 3, 3, 3, 3, 4, 5, 5, 5, 6, 6 },
		{ 7, 7, 1, 7, 7, 7, 7, 7, 7, 7, 7, 2, 2, 2, 2, 9 },
	};
	for (unsigned long r = 0; r < sizeof(rows) / sizeof(rows[0]); r++)
	{
		char packed[64];
		char* from = (char*) rows[r];
		char* to = packed;
		PackBits(&from, &to, 16);
		EXPECT(from == (char*) rows[r] + 16);
		char out[16];
		char* in = packed;
		char* back = out;
		UnpackBits(&in, &back, 16);
		EXPECT(in == to && memcmp(out, rows[r], 16) == 0);
	}
	// a row of one byte sixteen times packs to two bytes: -15 and the byte
	char packed[8];
	char* from = (char*) rows[1];
	char* to = packed;
	PackBits(&from, &to, 16);
	EXPECT(to == packed + 2 && packed[0] == -15 && packed[1] == 0);
}


int
main()
{
	InitHostStandaloneHeap();
	// the ROM's objects: a picture being recorded begins with the system
	// font as its text style (OpenPicture's SearchFont), which is theirs
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_PicPlay: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	InitObjects();					// (DrawPicture keeps its styles and shapes in Refs, as the ROM's does)
	InitGraf();
	InitFonts();
	// vars.fonts as the boot makes it: the ROM's font families by their
	// family symbols
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), RefVar(GetArraySlotRef(list, i)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(RSSYMfonts), fonts);
	gMap.baseAddr = (Ptr) gBits;
	gMap.rowBytes = kSize / 8;
	SetRect(&gMap.bounds, 0, 0, kSize, kSize);
	gMap.pixMapFlags = kPixMapPtr | 1;
	gMap.deviceRes.v = kDefaultDPI;
	gMap.deviceRes.h = kDefaultDPI;
	gMap.grayTable = nil;
	OpenPort(&gPort);
	SetPortBits(&gMap);
	gPort.portRect = gMap.bounds;
	RectRgn(gPort.visRgn, &gMap.bounds);
	RectRgn(gPort.clipRgn, &gMap.bounds);

	TestVersion1();
	TestScaled();
	TestNoClip();
	TestPacked();
	TestLines();
	TestVersion2();
	TestUnpackBits();
	TestPackBits();
	TestRecord();

	ClosePort(&gPort);
	printf("test_PicPlay: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
