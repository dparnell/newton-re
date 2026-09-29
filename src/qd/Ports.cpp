/*
	File:		qd/Ports.cpp

	Contains:	QuickDraw's ports, pens, patterns and pixel maps.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM's standard patterns are PixelMaps in ROM reached through "fake
	handles" (a master pointer in ROM); the host makes real handles over
	the same data when InitGraf runs.  The current port is one host global
	standing in for the task's NewtGlobals (NOT YET RECONSTRUCTED).
*/

#include "Ports.h"
#include "ByteOrder.h"
#include "OSErrors.h"
#include "Frames.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "StoreCompander.h"		// InitQDCompression
#include "Draw.h"
#include "Shapes.h"
#include "Curves.h"
#include "Paths.h"
#include "Polygons.h"
#include "PicPlay.h"
#include "PicRecord.h"
#include "TextObject.h"
#include <string.h>
#include <stdint.h>

// ROM 0x0c107d88 qdGlobals
QDGlobals		qdGlobals;
// ROM 0x0c107d74 stdPatterns
PatternHandle	stdPatterns[5];
// ROM 0x0c1056f0 wideHandle (a fake handle to the ROM's region at 0x00377a50)
RgnHandle		wideHandle;
// ROM 0x0c1067cc gGrafPort
GrafPort		gGrafPort;
// ROM 0x0c105410 gQDRunning
Boolean			gQDRunning = false;

static GrafPort*	gCurrentPort = nil;		// the task's NewtGlobals + 0x0c in the ROM

// ROM 0x00380b04 whitePatternData .. 0x00376f50 blackPatternData: the
// eight rows of each standard pattern
static const unsigned char kStdPatternData[5][8] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },		// white
	{ 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22 },		// light gray
	{ 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 },		// gray
	{ 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd },		// dark gray
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }		// black
};

const long	kPixelMapSize = 0x1c;			// the ROM's PixelMap (GetPixelMapSize)
// DEVIATION: a pattern's handle holds a PixelMap and its eight rows right
// after it - 0x1c + 8 on the Newton, but a host PixelMap is bigger (its
// baseAddr and grayTable are pointers), so the handle and the offset to
// the rows are sized from the host's struct.  (Sized the ROM's way, every
// pattern made from a binary wrote past its handle and broke the heap.)
extern const long	kPatternPixelsOffset = (long) sizeof(PixelMap);
const long	kPatternHandleSize = kPatternPixelsOffset + 8;


/*------------------------------------------------------------------------------
	P i x e l   m a p s
------------------------------------------------------------------------------*/

// ROM 0x002af0e0 GetPixelMapBits__FP8PixelMap
// The pixels: baseAddr as a handle, a pointer or an offset from the map.
Ptr
GetPixelMapBits(const PixelMap* pm)
{
	ULong storage = pm->pixMapFlags & kPixMapStorage;
	if (storage == kPixMapOffset)
		return (Ptr) pm + (intptr_t) pm->baseAddr;
	if (storage == kPixMapHandle)
		return *(Handle) pm->baseAddr;
	if (storage == kPixMapPtr)
		return pm->baseAddr;
	return nil;
}


// ROM 0x002af11c GetPixelMapSize__FP8PixelMap
// The map's size: without the gray table before version 1.
long
GetPixelMapSize(const PixelMap* pm)
{
	return (pm->pixMapFlags & kPixMapVersionMask) == 0 ? 0x18 : kPixelMapSize;
}


// ROM 0x002af130 PtInPixelMap__FP8PixelMaplT2
// Whether the pixel x across and y down from the map's origin is set (not
// white); false outside the map.
Boolean
PtInPixelMap(const PixelMap* pm, long x, long y)
{
	if (x < 0 || x >= pm->bounds.right - pm->bounds.left || y < 0 || y >= pm->bounds.bottom - pm->bounds.top)
		return false;
	return GetPixel(pm, pm->bounds.left + x, pm->bounds.top + y) != 0;
}


// ROM 0x002af1fc PtInCPixelMap__FP8PixelMaplT2
// The value of the pixel x across and y down from the map's origin, and
// -1 outside the map.  (The ROM reaches the pixel through tables of
// shifts and masks by depth; GetPixel below works it out.)
long
PtInCPixelMap(const PixelMap* pm, long x, long y)
{
	if (x < 0 || x >= pm->bounds.right - pm->bounds.left || y < 0 || y >= pm->bounds.bottom - pm->bounds.top)
		return -1;
	return GetPixel(pm, pm->bounds.left + x, pm->bounds.top + y);
}


// ROM 0x002af2c0 PtInMask__FP8PixelMaplT2
// Zero where the mask is set, -1 where it is clear or outside the map:
// the answer to look for is the -1, which is how a caller asks whether a
// point falls outside the picture.
long
PtInMask(const PixelMap* pm, long x, long y)
{
	if (x < 0 || x >= pm->bounds.right - pm->bounds.left || y < 0 || y >= pm->bounds.bottom - pm->bounds.top)
		return -1;
	return GetPixel(pm, pm->bounds.left + x, pm->bounds.top + y) != 0 ? 0 : -1;
}


// Host: a pixel read and written in the map's big-endian rows (the ROM's
// blitter works on words; Draw.cpp works a pixel at a time).
long
GetPixel(const PixelMap* pm, long x, long y)
{
	long depth = PixelMapDepth(pm);
	const unsigned char* row = (const unsigned char*) GetPixelMapBits(pm) + (y - pm->bounds.top) * pm->rowBytes;
	long bit = (x - pm->bounds.left) * depth;
	if (depth == 8)
		return row[bit >> 3];
	long shift = 8 - depth - (bit & 7);
	return (row[bit >> 3] >> shift) & ((1 << depth) - 1);
}


void
SetPixel(PixelMap* pm, long x, long y, long value)
{
	long depth = PixelMapDepth(pm);
	unsigned char* row = (unsigned char*) GetPixelMapBits(pm) + (y - pm->bounds.top) * pm->rowBytes;
	long bit = (x - pm->bounds.left) * depth;
	if (depth == 8)
	{
		row[bit >> 3] = (unsigned char) value;
		return;
	}
	long shift = 8 - depth - (bit & 7);
	long mask = ((1 << depth) - 1) << shift;
	row[bit >> 3] = (unsigned char) ((row[bit >> 3] & ~mask) | ((value << shift) & mask));
}


/*------------------------------------------------------------------------------
	P a t t e r n s
------------------------------------------------------------------------------*/

// ROM 0x003280e0 MakeSimplePattern__FlN71
// A one-bit 8x8 pattern from its rows: a handle holding the PixelMap
// (its pixels an offset: right after it) and the eight bytes.
PatternHandle
MakeSimplePattern(long row0, long row1, long row2, long row3, long row4, long row5, long row6, long row7)
{
	char rows[8] = { (char) row0, (char) row1, (char) row2, (char) row3, (char) row4, (char) row5, (char) row6, (char) row7 };
	return MakeSimplePattern(rows);
}


// ROM 0x003281a4 MakeSimplePattern__FPc
PatternHandle
MakeSimplePattern(const char* rows)
{
	PatternHandle pattern = (PatternHandle) NewHandle(kPatternHandleSize);
	if (pattern != nil)
	{
		PixelMap* pm = *pattern;
		pm->baseAddr = (Ptr) kPatternPixelsOffset;
		pm->rowBytes = 1;
		SetRect(&pm->bounds, 0, 0, 8, 8);
		pm->pixMapFlags = kPixMapOffset | 1;
		pm->deviceRes.v = kDefaultDPI;
		pm->deviceRes.h = kDefaultDPI;
		pm->grayTable = nil;
		memcpy((char*) pm + kPatternPixelsOffset, rows, 8);
	}
	return pattern;
}


// the depth a gray pattern is made at: the port's, or the screen's for a
// printer's port
static long
GrayPatternDepth(void)
{
	ULong flags = GetCurrentPort()->portBits.pixMapFlags;
	if (flags & 0x300)
		flags = qdGlobals.fScreenBits.pixMapFlags;
	return flags & 0xff;
}


// the header of an 8x8 pattern of the depth, its rows after it
static void
InitGrayPattern(PixelMap* pm, long depth)
{
	pm->baseAddr = (Ptr) kPatternPixelsOffset;
	pm->rowBytes = (short) depth;
	SetRect(&pm->bounds, 0, 0, 8, 8);
	pm->pixMapFlags = depth + kPixMapOffset;
	pm->deviceRes.v = kDefaultDPI;
	pm->deviceRes.h = kDefaultDPI;
	pm->grayTable = nil;
}


// ROM 0x00328e90 GetStdGrayPattern__FUlN21
// A solid pattern of the gray the colour comes to at the port's depth
// (one bit: black for any gray but white).
//
// DEVIATION: the rows follow the host's PixelMap (kPatternPixelsOffset)
// where the ROM's follow its 0x1c-byte one.
PatternHandle
GetStdGrayPattern(ULong red, ULong green, ULong blue)
{
	long depth = GrayPatternDepth();
	long size = depth * 8;
	PatternHandle pattern = (PatternHandle) NewHandle(size + kPatternPixelsOffset);
	if (pattern != nil)
	{
		ULong gray = RGBtoGray(red, green, blue, 0x10, depth);
		PixelMap* pm = *pattern;
		InitGrayPattern(pm, depth);
		UChar byte;
		if (depth == 1)
			byte = gray != 0 ? 0xff : 0;
		else if (depth == 2)
			byte = (UChar) ((gray << 6) | (gray << 4) | (gray << 2) | gray);
		else if (depth == 4)
			byte = (UChar) (gray | (gray << 4));
		else
			byte = 0;
		UChar* p = (UChar*) pm + kPatternPixelsOffset;
		for (long i = 0; i < size; i++)
			p[i] = byte;
	}
	return pattern;
}


// ROM 0x0032840c MakeSimpleGrayPattern__FPlUlT2
// An old eight-row pattern drawn in two grays: a set bit the foreground,
// a clear one the background, at the port's depth - a plain one-bit
// pattern when that is simply black on white.  (The rows are read as two
// big-endian words, as the ARM loads them.)
//
// ROM QUIRK, kept: at a depth of eight the rows are left as the handle
// was allocated.
PatternHandle
MakeSimpleGrayPattern(const char* rows, ULong fg, ULong bg)
{
	long depth = GrayPatternDepth();
	if ((0xffffffffu >> (32 - depth)) == fg && bg == 0)
		return MakeSimplePattern(rows);
	PatternHandle pattern = (PatternHandle) NewHandle(depth * 8 + kPatternPixelsOffset);
	if (pattern == nil)
		return nil;
	PixelMap* pm = *pattern;
	InitGrayPattern(pm, depth);
	UChar* dst = (UChar*) pm + kPatternPixelsOffset;
	if (depth == 1)
	{
		BlockMove(rows, dst, 8);
		return pattern;
	}
	ULong words[2] = { GetBigEndianWord(rows), GetBigEndianWord(rows + 4) };
	ULong* word = words;
	UChar fg4 = (UChar) (fg << 4);
	UChar bg4 = (UChar) (bg << 4);
	ULong bit = 0x80000000;
	if (depth == 2)
	{
		for (long i = 0; i < 0x10; i++)
		{
			UChar b = (*word & bit) == 0 ? (UChar) (bg << 6) : (UChar) (fg << 6);
			b |= (*word & (bit >> 1)) != 0 ? fg4 : bg4;
			b |= (*word & (bit >> 2)) == 0 ? (UChar) (bg << 2) : (UChar) (fg << 2);
			b |= (UChar) ((*word & (bit >> 3)) != 0 ? fg : bg);
			*dst++ = b;
			bit >>= 4;
			if (bit == 0)
			{
				bit = 0x80000000;
				word++;
			}
		}
	}
	else if (depth == 4)
	{
		for (long i = 0; i < 0x20; i++)
		{
			UChar b = (*word & bit) != 0 ? fg4 : bg4;
			b |= (UChar) ((*word & (bit >> 1)) != 0 ? fg : bg);
			*dst++ = b;
			bit >>= 2;
			if (bit == 0)
			{
				bit = 0x80000000;
				word++;
			}
		}
	}
	return pattern;
}


// ROM 0x00328fc0 MakeGrayPattern__FRC6RefVar
// A pattern out of a 'grayPattern binary: six bytes a pixel (red, green
// and blue, big-endian sixteen bits each), eight pixels a row, the rows
// used over again until there are eight; fewer than eight pixels make one
// row, the pixels used over again along it.  Each pixel is the gray it
// comes to at the port's depth.  None at all is black.
//
// ROM QUIRK, kept: at a depth of eight the rows are left as the handle was
// allocated.
PatternHandle
MakeGrayPattern(RefArg spec)
{
	long depth = GrayPatternDepth();
	long count = Length(spec) / 6;
	if (count == 0)
		return GetStdPattern(blackPat);
	const UChar* src = (const UChar*) BinaryData(spec);
	long rows = count / 8;
	UChar row[48];
	if (rows == 0)
	{
		for (long i = 0; i < 8; i++)
			memmove(row + i * 6, src + (i % count) * 6, 6);
		src = row;
		rows = 1;
	}
	PatternHandle pattern = (PatternHandle) NewHandle(depth * 8 + kPatternPixelsOffset);
	if (pattern == nil)
		return GetStdPattern(blackPat);
	PixelMap* pm = *pattern;
	InitGrayPattern(pm, depth);
	UChar* dst = (UChar*) pm + kPatternPixelsOffset;
	const UChar* p = src;
	long left = rows;
	for (long r = 0; r < 8 && (depth == 1 || depth == 2 || depth == 4); r++)
	{
		long perByte = 8 / depth;
		for (long b = 0; b < depth; b++)
		{
			UChar byte = 0;
			for (long k = 0; k < perByte; k++, p += 6)
			{
				ULong gray = RGBtoGray(GetBigEndianHalf(p), GetBigEndianHalf(p + 2), GetBigEndianHalf(p + 4), 0x10, depth);
				if (depth == 1)
				{
					if (gray != 0)
						byte |= (UChar) (0x80 >> k);
				}
				else
					byte |= (UChar) (gray << (8 - depth * (k + 1)));
			}
			*dst++ = byte;
		}
		if (--left == 0)
		{
			left = rows;
			p = src;
		}
	}
	return pattern;
}


// ROM 0x002bf0ac GrayToRGB__FUcPUlN22l
// A gray at a depth as the colour it stands for: the same in all three,
// white at nought.  (Every depth but two is taken as four.)
void
GrayToRGB(UChar gray, ULong* red, ULong* green, ULong* blue, long depth)
{
	ULong step = depth == 2 ? 0x5555 : 0x1111;
	ULong value = (ULong) (ULong32) (0xffff - step * gray);
	*red = value;
	*green = value;
	*blue = value;
}


// ROM 0x00328238 MakeNSPattern__FP8PixelMapl
// The pattern as a script holds it: a one-bit one as an eight-byte
// 'pattern binary of its rows, any other as a 'grayPattern binary of six
// bytes a pixel (big-endian red, green and blue) for count bytes of its
// pixels, each through the map's gray table when it has one.
Ref
MakeNSPattern(PixelMap* pm, long count)
{
	RefVar result;
	UChar* src = (UChar*) GetPixelMapBits(pm);
	long depth = pm->pixMapFlags & 0xff;
	if (depth == 1)
	{
		result = AllocateBinary(RSSYMpattern, 8);
		memmove(BinaryData(result), src, 8);
		return result;
	}
	long perByte = depth == 4 ? 2 : 4;
	result = AllocateBinary(RSSYMgraypattern, perByte * count * 6);
	UChar* dst = (UChar*) BinaryData(result);
	UChar* grayTable = pm->grayTable;
	for (long i = 0; i < count; i++)
	{
		UChar byte = *src++;
		UChar pixels[4] = { 0, 0, 0, 0 };
		if (depth == 4)
		{
			pixels[0] = byte >> 4;
			pixels[1] = byte & 0xf;
		}
		else if (depth == 2)
		{
			pixels[0] = byte >> 6;
			pixels[1] = (byte & 0x30) >> 4;
			pixels[2] = (byte & 0xc) >> 2;
			pixels[3] = byte & 3;
		}
		// (ROM QUIRK, kept: at any other depth the pixels are nought)
		for (long k = 0; k < perByte; k++)
		{
			UChar gray = pixels[k];
			if (grayTable != nil)
				gray = grayTable[gray];
			ULong red, green, blue;
			GrayToRGB(gray, &red, &green, &blue, depth);
			PutBigEndianHalf(dst, (unsigned short) red);
			PutBigEndianHalf(dst + 2, (unsigned short) green);
			PutBigEndianHalf(dst + 4, (unsigned short) blue);
			dst += 6;
		}
	}
	return result;
}


// the pattern's pixel bytes, rowBytes times its height
static long
PatternBytes(const PixelMap* pm)
{
	return pm->rowBytes * (pm->bounds.bottom - pm->bounds.top);
}


// ROM 0x003286e8 BlackOrWhitePat__FPP8PixelMap
// 1 when every byte of the pattern is 0xff, 2 when every one is nought,
// else 0.
long
BlackOrWhitePat(PatternHandle pattern)
{
	PixelMap* pm = *pattern;
	const UChar* p = (const UChar*) GetPixelMapBits(pm);
	UChar first = *p++;
	if (first != 0 && first != 0xff)
		return 0;
	for (long n = PatternBytes(pm) - 1; n > 0; n--)
		if (*p++ != first)
			return 0;
	return first != 0 ? 1 : 2;
}


// ROM 0x00328768 MonochromePat__FPP8PixelMapPUl
// Whether every pixel of the pattern is the same gray, which is answered
// (the first pixel's, even when they differ).  Only depths 1, 2 and 4.
Boolean
MonochromePat(PatternHandle pattern, ULong* gray)
{
	PixelMap* pm = *pattern;
	const UChar* p = (const UChar*) GetPixelMapBits(pm);
	UChar first = *p++;
	Boolean same;
	switch ((*pattern)->pixMapFlags & 0xff)
	{
	case 1:
		same = first == 0 || first == 0xff;
		*gray = first & 1;
		break;
	case 2:
		same = first == 0 || first == 0x55 || first == 0xaa || first == 0xff;
		*gray = first & 3;
		break;
	case 4:
		same = (first & 0xf) == (first >> 4);
		*gray = first & 0xf;
		break;
	default:
		return false;
	}
	if (!same)
		return false;
	for (long n = PatternBytes(pm) - 1; n > 0; n--)
		if (*p++ != first)
			return false;
	return same;
}


// ROM 0x00328d64 CopyPattern__FPP8PixelMap
// A copy of any pattern with its pixels inside the handle.
PatternHandle
CopyPattern(PatternHandle pattern)
{
	PixelMap* src = *pattern;
	long size = (src->bounds.bottom - src->bounds.top) * src->rowBytes;
	PatternHandle copy = (PatternHandle) NewHandle(size + kPatternPixelsOffset);
	if (copy != nil)
	{
		src = *pattern;
		PixelMap* pm = *copy;
		*pm = *src;
		pm->baseAddr = (Ptr) kPatternPixelsOffset;
		pm->pixMapFlags = (pm->pixMapFlags & ~kPixMapStorage) | kPixMapOffset;
		BlockMove(GetPixelMapBits(src), (char*) pm + kPatternPixelsOffset, size);
	}
	return copy;
}


// ROM 0x00328dfc DisposePattern__FPP8PixelMap
// A pattern freed with its pixels, unless it is a standard one.
void
DisposePattern(PatternHandle pattern)
{
	if (pattern == nil)
		return;
	for (long i = 4; i >= 0; i--)
		if (stdPatterns[i] == pattern)
			return;
	ULong storage = (*pattern)->pixMapFlags & kPixMapStorage;
	if (storage != kPixMapOffset)
	{
		Ptr bits = (*pattern)->baseAddr;
		if (storage == kPixMapHandle)
			DisposHandle((Handle) bits);
		else
			DisposPtr(bits);
	}
	DisposHandle((Handle) pattern);
}


// ROM 0x00329330 GetStdPattern__FUc
PatternHandle
GetStdPattern(GetPatSelector which)
{
	if (which < 5)
		return stdPatterns[which];
	return stdPatterns[blackPat];
}


// ROM 0x003280c8 GetFgPattern__Fv
PatternHandle
GetFgPattern(void)
{
	return GetCurrentPort()->fgPat;
}


// ROM 0x003280b0 GetBgPattern__Fv
PatternHandle
GetBgPattern(void)
{
	return GetCurrentPort()->bgPat;
}


// ROM 0x00328d48 SetFgPattern__FPP8PixelMap
void
SetFgPattern(PatternHandle pattern)
{
	GetCurrentPort()->fgPat = pattern;
}


// ROM 0x00328d2c SetBgPattern__FPP8PixelMap
void
SetBgPattern(PatternHandle pattern)
{
	GetCurrentPort()->bgPat = pattern;
}


// Host: the pattern's pixel for the port pixel (x, y) at the given depth:
// the pattern tiles from the port's patAlign (as the ROM's PatExpand
// 0x0030356c aligns its expanded rows), a one-bit pattern's set bits
// being black at any depth.
long
PatternPixel(PatternHandle pattern, long x, long y, long depth)
{
	const PixelMap* pm = *pattern;
	Point align = GetCurrentPort()->patAlign;
	long width = pm->bounds.right - pm->bounds.left;
	long height = pm->bounds.bottom - pm->bounds.top;
	long px = pm->bounds.left + ((x + align.h) % width + width) % width;
	long py = pm->bounds.top + ((y + align.v) % height + height) % height;
	long value = GetPixel(pm, px, py);
	if (PixelMapDepth(pm) == depth)
		return value;
	return value ? (1 << depth) - 1 : 0;
}


/*------------------------------------------------------------------------------
	T h e   l i b r a r y
------------------------------------------------------------------------------*/

// ROM 0x002e45b8 SetStdProcs__FP7QDProcs
// Every proc the standard one (the ROM copies them from its table at
// 0x00380bcc).
void
SetStdProcs(QDProcs* procs)
{
	procs->arcProc = StdArc;
	procs->bitsProc = StdBits;
	procs->curveProc = StdCurve;
	procs->getPicProc = StdGetPic;
	procs->lineProc = StdLine;
	procs->ovalProc = StdOval;
	procs->pathsProc = StdPaths;
	procs->commentProc = StdComment;
	procs->polyProc = StdPoly;
	procs->putPicProc = (PutPicDataProc) StdPutPic;
	procs->rectProc = StdRect;
	procs->rgnProc = StdRgn;
	procs->rRectProc = StdRRect;
	procs->textProc = StdText;
}


// ROM 0x002e4388 InitGraf__Fv
// ROM 0x0033f528 GetRandSeed__Fv
long
GetRandSeed(void)
{
	return qdGlobals.fRandSeed;
}


// ROM 0x0033f538 SetRandSeed__Fl
void
SetRandSeed(long seed)
{
	qdGlobals.fRandSeed = seed;
}


static const char kGrafException[] = "evt.ex.graf";
const long kGrafErrBadParameters = -8809;		// the ROM's, for a component that will not fit


/*------------------------------------------------------------------------------
	C o l o u r s

	A colour a script hands the drawing verbs is one packed integer: eight
	bits each of red, green and blue with 0x10 above them, which says it
	is an RGB rather than one of the small numbered colours.  The
	components a script gives and gets are sixteen-bit, as QuickDraw's
	RGBColor holds them, so packing throws the low byte of each away and
	unpacking puts the high byte back in both halves (0xNN -> 0xNNNN),
	which is what keeps white white.
------------------------------------------------------------------------------*/

// ROM 0x002befdc PackRGBvalues__FUlN21
ULong
PackRGBvalues(ULong red, ULong green, ULong blue)
{
	return ((red & 0xff00) << 8) + (green & 0xff00) + ((blue >> 8) & 0xff) + 0x10000000;
}


// ROM 0x002beffc UnpackRGBvalues__FUlPUlN22
void
UnpackRGBvalues(ULong colour, ULong* red, ULong* green, ULong* blue)
{
	*red = ((colour >> 16) & 0xff) * 0x101;
	*green = ((colour >> 8) & 0xff) * 0x101;
	*blue = (colour & 0xff) * 0x101;
}


// ROM 0x000e387c FPackRGB
// PackRGB(red, green, blue): the three into one colour.  A component
// that does not fit in sixteen bits is an error - and the ROM tests the
// three together, taking whichever of them is out of range first, so
// only one test is made.
static Ref
FPackRGB(RefArg /*rcvr*/, RefArg red, RefArg green, RefArg blue)
{
	ULong r = (ULong) RINT(red);
	ULong g = (ULong) RINT(green);
	ULong b = (ULong) RINT(blue);
	ULong outOfRange = r;
	if (outOfRange < 0x10000)
		outOfRange = g;
	if (outOfRange < 0x10000)
		outOfRange = b;
	if (outOfRange > 0xffff)
		Throw((ExceptionName) kGrafException, (void*) kGrafErrBadParameters, nil);
	return MAKEINT(PackRGBvalues(r, g, b));
}


// ROM 0x000e3938 FGetRed
static Ref
FGetRed(RefArg /*rcvr*/, RefArg colour)
{
	ULong r, g, b;
	UnpackRGBvalues((ULong) RINT(colour), &r, &g, &b);
	return MAKEINT(r);
}


// ROM 0x000e397c FGetGreen
static Ref
FGetGreen(RefArg /*rcvr*/, RefArg colour)
{
	ULong r, g, b;
	UnpackRGBvalues((ULong) RINT(colour), &r, &g, &b);
	return MAKEINT(g);
}


// ROM 0x000e39c0 FGetBlue
static Ref
FGetBlue(RefArg /*rcvr*/, RefArg colour)
{
	ULong r, g, b;
	UnpackRGBvalues((ULong) RINT(colour), &r, &g, &b);
	return MAKEINT(b);
}


// ROM 0x002bf044 RGBtoGray__FUlN21lT4
// A colour as a gray of `depthOut` bits: the luminance of the three
// components weighted 19589, 38443 and 7497 (the usual 0.299, 0.587,
// 0.114 in sixteenths of a thousandth), *inverted* - the Newton's grays
// run from 0 white to all-ones black, the other way from a colour - and
// rounded by half a step of the incoming depth before it is shifted
// down.
//
// The subtraction of the half-step is guarded by an unsigned compare
// against what it came from, so a value too small to take it keeps the
// value it had rather than wrapping; that is the ROM's own arithmetic.
//
// DEVIATION: the whole of it is thirty-two bit arithmetic on the ARM -
// the products wrap and the shifts are by a register, where a count of
// thirty-two or more gives nought - so it is done in ULong32 through the
// two helpers rather than in whatever width the host's ULong is.
static ULong32
ArmLsl(ULong32 value, ULong count)	{ return count >= 32 ? 0 : (ULong32) (value << count); }
static ULong32
ArmLsr(ULong32 value, ULong count)	{ return count >= 32 ? 0 : (ULong32) (value >> count); }

ULong
RGBtoGray(ULong red, ULong green, ULong blue, long depthIn, long depthOut)
{
	ULong32 gray = (ULong32) ((ULong32) red * 0xffffb37bUL
							+ (ULong32) green * 0xffff69d5UL
							+ (ULong32) blue * 0xffffe2b7UL) - 1;
	ULong32 rounded = gray - ArmLsl(1, (ULong) (0x1f - depthIn) & 0xff);
	if (rounded < gray)
		gray = rounded;
	return ArmLsr(gray, (ULong) (0x20 - depthOut) & 0xff);
}


// ROM 0x000e3a04 FGetTone
// GetTone(colour): the gray the current port would draw that colour as,
// at the port's own depth.
static Ref
FGetTone(RefArg /*rcvr*/, RefArg colour)
{
	GrafPort* port;
	GetPort(&port);
	long depth = (long) (port->portBits.pixMapFlags & kPixMapDepth);
	ULong r, g, b;
	UnpackRGBvalues((ULong) RINT(colour), &r, &g, &b);
	return MAKEINT(RGBtoGray(r, g, b, depth, depth));
}


// ROM 0x000e3c38 FIsEqualTone
// IsEqualTone(a, b): whether two colours come out as the same gray in
// the current port - which is how a script asks whether a colour is
// worth using on this screen.
static Ref
FIsEqualTone(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	GrafPort* port;
	GetPort(&port);
	long depth = (long) (port->portBits.pixMapFlags & kPixMapDepth);
	ULong r, g, bl;
	UnpackRGBvalues((ULong) RINT(a), &r, &g, &bl);
	ULong first = RGBtoGray(r, g, bl, depth, depth);
	UnpackRGBvalues((ULong) RINT(b), &r, &g, &bl);
	ULong second = RGBtoGray(r, g, bl, depth, depth);
	return MAKEBOOLEAN(first == second);
}



void
RegisterPortNatives(void)
{
	RegisterNativeFunction("FPackRGB", (void*) FPackRGB, 3);
	RegisterNativeFunction("FGetRed", (void*) FGetRed, 1);
	RegisterNativeFunction("FGetGreen", (void*) FGetGreen, 1);
	RegisterNativeFunction("FGetBlue", (void*) FGetBlue, 1);
	RegisterNativeFunction("FGetTone", (void*) FGetTone, 1);
	RegisterNativeFunction("FIsEqualTone", (void*) FIsEqualTone, 2);
}

// ROM 0x0033f488 Random__Fv
// The Macintosh's generator: the seed multiplied by 16807 modulo 2^31 - 1
// (in two halves), the low halfword answered signed (0x8000 as 0).
long
Random(void)
{
	ULong seed = GetRandSeed();
	ULong lo = (seed & 0xffff) * 0x41a7;
	ULong hi = ((seed >> 16) & 0xffff) * 0x41a7 + (lo >> 16);
	seed = (lo & 0xffff) + 0x80000001 + (hi & 0x7fff) * 0x10000 + ((long) (hi * 2) >> 16);
	if ((long) seed < 0)
		seed += 0x7fffffff;
	SetRandSeed(seed);
	short result = (short) seed;
	if ((seed & 0xffff) == 0x8000)
		result = 0;
	return result;
}


// ROM 0x0025c5b4 Rand__Fl
// A random number from 0 to n - 1.
long
Rand(long n)
{
	long r = Random();
	if (r < 0)
		r = -r;
	return r % n;
}


// The library started: the globals cleared, the standard patterns and
// the wide-open region made, the default port opened on the screen.
// NOT YET RECONSTRUCTED: InitScreen (the display driver's PixelMap: the
// screen is empty here until one is set), InitQDCompression, the QD
// protocols (TPinPad, TGrayShrink, TQDLibraryDriver) registered.
void
InitGraf(void)
{
	memset(&qdGlobals, 0, sizeof(qdGlobals));
	qdGlobals.fRandSeed = 1;
	static const Region kWideOpen = { kRectRgnSize, 0, { -32767, -32767, 32767, 32767 } };
	wideHandle = (RgnHandle) NewHandle(kRectRgnSize);
	**wideHandle = kWideOpen;
	for (long i = 0; i < 5; i++)
		stdPatterns[i] = MakeSimplePattern((const char*) kStdPatternData[i]);
	qdGlobals.fScreenBits.baseAddr = nil;
	qdGlobals.fScreenBits.rowBytes = 0;
	SetEmptyRect(&qdGlobals.fScreenBits.bounds);
	qdGlobals.fScreenBits.pixMapFlags = kPixMapPtr | 1;
	InitQDCompression();			// (a store bitmap's compander: stores/StoreCompander.h)
	OpenPort(&gGrafPort);
	gQDRunning = true;
}


// ROM 0x002e45e8 GetCurrentPort__Fv
// The task's port, or the default before the task has globals.
GrafPort*
GetCurrentPort(void)
{
	return gCurrentPort != nil ? gCurrentPort : &gGrafPort;
}


// ROM 0x002e4794 SetPort__FP8GrafPort
void
SetPort(GrafPort* port)
{
	gCurrentPort = port;
}


// ROM 0x002e47c8 GetPort__FPP8GrafPort
void
GetPort(GrafPort** port)
{
	*port = GetCurrentPort();
}


// the fields InitPort and OpenPort share: the screen's bits and rect,
// the regions, the patterns and pen, and the port made current
static void
SetUpPort(GrafPort* port)
{
	RgnHandle clip = port->clipRgn;
	RgnHandle vis = port->visRgn;
	memset(port, 0, sizeof(GrafPort));
	port->clipRgn = clip;
	port->visRgn = vis;
	port->portBits = qdGlobals.fScreenBits;
	port->portRect = qdGlobals.fScreenBits.bounds;
	InitPortRgns(port);
	port->fgPat = stdPatterns[blackPat];
	port->bgPat = stdPatterns[whitePat];
	port->pnSize.v = 1;
	port->pnSize.h = 1;
	port->pnMode = patCopy;
	port->pnVis = 0;
	SetPort(port);
}


// ROM 0x002e44ac OpenPort__FP8GrafPort
void
OpenPort(GrafPort* port)
{
	port->visRgn = NewRgn();
	port->clipRgn = NewRgn();
	SetUpPort(port);
}


// ROM 0x002e460c InitPort__FP8GrafPort
void
InitPort(GrafPort* port)
{
	SetUpPort(port);
}


// ROM 0x002e46b4 InitPortRgns__FP8GrafPort
// The visible region the screen's bounds, the clip region wide open.
void
InitPortRgns(GrafPort* port)
{
	RectRgn(port->visRgn, &qdGlobals.fScreenBits.bounds);
	CopyRgn(wideHandle, port->clipRgn);
}


// ROM 0x002e47e4 ClosePort__FP8GrafPort
// The port's regions and patterns freed.
void
ClosePort(GrafPort* port)
{
	DisposeRgn(port->visRgn);
	DisposeRgn(port->clipRgn);
	DisposePattern(port->fgPat);
	DisposePattern(port->bgPat);
}


// ROM 0x002e4818 SetPortBits__FP8PixelMap
void
SetPortBits(const PixelMap* bits)
{
	GetCurrentPort()->portBits = *bits;
}


// ROM 0x002e44d8 SetOrigin__FlT1
// The port's coordinate system moved so that (h, v) is its top left:
// the bits' bounds, the port rect and the visible region shift.
void
SetOrigin(long h, long v)
{
	GrafPort* port = GetCurrentPort();
	if (port->portRect.top == v && port->portRect.left == h)
		return;
	long dh = h - port->portRect.left;
	long dv = v - port->portRect.top;
	OffsetRect(&port->portBits.bounds, dh, dv);
	OffsetRect(&port->portRect, dh, dv);
	OffsetRgn(port->visRgn, dh, dv);
}


// ROM 0x002e454c SetClip__FPP6Region
void
SetClip(RgnHandle rgn)
{
	CopyRgn(rgn, GetCurrentPort()->clipRgn);
}


// ROM 0x002e4570 GetClip__FPP6Region
void
GetClip(RgnHandle rgn)
{
	CopyRgn(GetCurrentPort()->clipRgn, rgn);
}


// ROM 0x002e4594 ClipRect__FP4Rect
void
ClipRect(const Rect* r)
{
	RectRgn(GetCurrentPort()->clipRgn, r);
}


/*------------------------------------------------------------------------------
	T h e   p e n
------------------------------------------------------------------------------*/

// ROM 0x00329660 HidePen__Fv
void
HidePen(void)
{
	GetCurrentPort()->pnVis--;
}


// ROM 0x0032968c ShowPen__Fv
void
ShowPen(void)
{
	GetCurrentPort()->pnVis++;
}


// ROM 0x003296b8 GetPen__FP5Point
void
GetPen(Point* pt)
{
	*pt = GetCurrentPort()->pnLoc;
}


// ROM 0x003296d8 GetPenState__FP8PenState
void
GetPenState(PenState* state)
{
	GrafPort* port = GetCurrentPort();
	state->pnLoc = port->pnLoc;
	state->pnSize = port->pnSize;
	state->pnMode = port->pnMode;
	state->fgPat = GetFgPattern();
}


// ROM 0x00329720 SetPenState__FP8PenState
void
SetPenState(const PenState* state)
{
	GrafPort* port = GetCurrentPort();
	port->pnLoc = state->pnLoc;
	port->pnSize = state->pnSize;
	port->pnMode = state->pnMode;
	port->fgPat = state->fgPat;
}


// ROM 0x00329768 PenSize__FlT1
void
PenSize(long width, long height)
{
	GrafPort* port = GetCurrentPort();
	port->pnSize.h = (short) width;
	port->pnSize.v = (short) height;
}


// ROM 0x0032979c PenMode__Fl
void
PenMode(long mode)
{
	GetCurrentPort()->pnMode = (short) mode;
}


// ROM 0x003297c0 PenNormal__Fv
// A one-pixel pen, copying, black.
void
PenNormal(void)
{
	PenSize(1, 1);
	PenMode(patCopy);
	GetCurrentPort()->fgPat = stdPatterns[blackPat];
}


// ROM 0x003297f4 MoveTo__FlT1
void
MoveTo(long h, long v)
{
	GrafPort* port = GetCurrentPort();
	port->pnLoc.h = (short) h;
	port->pnLoc.v = (short) v;
}


// ROM 0x00329828 Move__FlT1
void
Move(long dh, long dv)
{
	GrafPort* port = GetCurrentPort();
	port->pnLoc.h = (short) (port->pnLoc.h + dh);
	port->pnLoc.v = (short) (port->pnLoc.v + dv);
}


// ROM 0x0033f684 AllocNewTempBuf__Fv
void*
AllocNewTempBuf(void)
{
	return NewPtr(0x400);
}


// ROM 0x0033f68c DeleteNewTempBuf__FPc
// (InvalidateQDTempBuf 0x0033f658 marks a task's buffer as none with
// -0x400, which is not given back)
void
DeleteNewTempBuf(char* buffer)
{
	if (buffer != (char*) -0x400)
		DisposPtr(buffer);
}
