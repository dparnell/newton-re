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
#include "OSErrors.h"
#include "Frames.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
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

const long	kPatternHandleSize = 0x24;		// a PixelMap and its eight rows
const long	kPixelMapSize = 0x1c;


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
		pm->baseAddr = (Ptr) kPixelMapSize;
		pm->rowBytes = 1;
		SetRect(&pm->bounds, 0, 0, 8, 8);
		pm->pixMapFlags = kPixMapOffset | 1;
		pm->deviceRes.v = kDefaultDPI;
		pm->deviceRes.h = kDefaultDPI;
		pm->grayTable = nil;
		memcpy((char*) pm + kPixelMapSize, rows, 8);
	}
	return pattern;
}


// ROM 0x00328d64 CopyPattern__FPP8PixelMap
// A copy of any pattern with its pixels inside the handle.
static PatternHandle
CopyPattern(PatternHandle pattern)
{
	PixelMap* src = *pattern;
	long size = (src->bounds.bottom - src->bounds.top) * src->rowBytes;
	PatternHandle copy = (PatternHandle) NewHandle(size + kPixelMapSize);
	if (copy != nil)
	{
		src = *pattern;
		PixelMap* pm = *copy;
		*pm = *src;
		pm->baseAddr = (Ptr) kPixelMapSize;
		pm->pixMapFlags = (pm->pixMapFlags & ~kPixMapStorage) | kPixMapOffset;
		BlockMove(GetPixelMapBits(src), (char*) pm + kPixelMapSize, size);
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
// NOT YET RECONSTRUCTED: the arc, bits, curve, line, oval, paths, picture,
// polygon, round-rect and text procs; the rect and region ones are Draw.cpp's.
void	StdRect(GrafVerb verb, Rect* r);
void	StdRgn(GrafVerb verb, RgnHandle rgn);

void
SetStdProcs(QDProcs* procs)
{
	memset(procs, 0, sizeof(QDProcs));
	procs->rectProc = StdRect;
	procs->rgnProc = StdRgn;
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
