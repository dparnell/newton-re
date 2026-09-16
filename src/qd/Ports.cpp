/*
	File:		qd/Ports.cpp

	Contains:	QuickDraw's ports, pens, patterns and pixel maps.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The ROM's standard patterns are PixelMaps in ROM reached through "fake
	handles" (a master pointer in ROM); the host makes real handles over
	the same data when InitGraf runs.  The current port is one host global
	standing in for the task's NewtGlobals (NOT YET RECONSTRUCTED).
*/

#include "Ports.h"
#include "OSErrors.h"
#include <string.h>
#include <stdint.h>

// ROM 0x0c104e50 qdGlobals
QDGlobals		qdGlobals;
// ROM 0x0c104e3c stdPatterns
PatternHandle	stdPatterns[5];
// ROM 0x0c1027b4 wideHandle (a fake handle to the ROM's region at 0x00377a50)
RgnHandle		wideHandle;
// ROM 0x0c103a98 gGrafPort
GrafPort		gGrafPort;
// ROM 0x0c102504 gQDRunning
Boolean			gQDRunning = false;

static GrafPort*	gCurrentPort = nil;		// the task's NewtGlobals + 0x0c in the ROM

// ROM 0x00376eb0 whitePatternData .. 0x00376f50 blackPatternData: the
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

// ROM 0x0028a538 GetPixelMapBits__FP8PixelMap
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


// ROM 0x0028a574 GetPixelMapSize__FP8PixelMap
// The map's size: without the gray table before version 1.
long
GetPixelMapSize(const PixelMap* pm)
{
	return (pm->pixMapFlags & kPixMapVersionMask) == 0 ? 0x18 : kPixelMapSize;
}


// ROM 0x0028a588 PtInPixelMap__FP8PixelMaplT2
// Whether the pixel x across and y down from the map's origin is set (not
// white); false outside the map.
Boolean
PtInPixelMap(const PixelMap* pm, long x, long y)
{
	if (x < 0 || x >= pm->bounds.right - pm->bounds.left || y < 0 || y >= pm->bounds.bottom - pm->bounds.top)
		return false;
	return GetPixel(pm, pm->bounds.left + x, pm->bounds.top + y) != 0;
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

// ROM 0x00302de4 MakeSimplePattern__FlN71
// A one-bit 8x8 pattern from its rows: a handle holding the PixelMap
// (its pixels an offset: right after it) and the eight bytes.
PatternHandle
MakeSimplePattern(long row0, long row1, long row2, long row3, long row4, long row5, long row6, long row7)
{
	char rows[8] = { (char) row0, (char) row1, (char) row2, (char) row3, (char) row4, (char) row5, (char) row6, (char) row7 };
	return MakeSimplePattern(rows);
}


// ROM 0x00302ea8 MakeSimplePattern__FPc
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


// ROM 0x00303a68 CopyPattern__FPP8PixelMap
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


// ROM 0x00303b00 DisposePattern__FPP8PixelMap
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


// ROM 0x00304034 GetStdPattern__FUc
PatternHandle
GetStdPattern(GetPatSelector which)
{
	if (which < 5)
		return stdPatterns[which];
	return stdPatterns[blackPat];
}


// ROM 0x00302dcc GetFgPattern__Fv
PatternHandle
GetFgPattern(void)
{
	return GetCurrentPort()->fgPat;
}


// ROM 0x00302db4 GetBgPattern__Fv
PatternHandle
GetBgPattern(void)
{
	return GetCurrentPort()->bgPat;
}


// ROM 0x00303a4c SetFgPattern__FPP8PixelMap
void
SetFgPattern(PatternHandle pattern)
{
	GetCurrentPort()->fgPat = pattern;
}


// ROM 0x00303a30 SetBgPattern__FPP8PixelMap
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

// ROM 0x002be838 SetStdProcs__FP7QDProcs
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


// ROM 0x002be600 InitGraf__Fv
// The library started: the globals cleared, the standard patterns and
// the wide-open region made, the default port opened on the screen.
// NOT YET RECONSTRUCTED: InitScreen (the display driver's PixelMap: the
// screen is empty here until one is set), InitQDCompression, the QD
// protocols (TPinPad, TGrayShrink, TQDLibraryDriver) registered.
void
InitGraf(void)
{
	memset(&qdGlobals, 0, sizeof(qdGlobals));
	qdGlobals.fVersion = 1;
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


// ROM 0x002be868 GetCurrentPort__Fv
// The task's port, or the default before the task has globals.
GrafPort*
GetCurrentPort(void)
{
	return gCurrentPort != nil ? gCurrentPort : &gGrafPort;
}


// ROM 0x002bea14 SetPort__FP8GrafPort
void
SetPort(GrafPort* port)
{
	gCurrentPort = port;
}


// ROM 0x002bea48 GetPort__FPP8GrafPort
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


// ROM 0x002be72c OpenPort__FP8GrafPort
void
OpenPort(GrafPort* port)
{
	port->visRgn = NewRgn();
	port->clipRgn = NewRgn();
	SetUpPort(port);
}


// ROM 0x002be88c InitPort__FP8GrafPort
void
InitPort(GrafPort* port)
{
	SetUpPort(port);
}


// ROM 0x002be934 InitPortRgns__FP8GrafPort
// The visible region the screen's bounds, the clip region wide open.
void
InitPortRgns(GrafPort* port)
{
	RectRgn(port->visRgn, &qdGlobals.fScreenBits.bounds);
	CopyRgn(wideHandle, port->clipRgn);
}


// ROM 0x002bea64 ClosePort__FP8GrafPort
// The port's regions and patterns freed.
void
ClosePort(GrafPort* port)
{
	DisposeRgn(port->visRgn);
	DisposeRgn(port->clipRgn);
	DisposePattern(port->fgPat);
	DisposePattern(port->bgPat);
}


// ROM 0x002bea98 SetPortBits__FP8PixelMap
void
SetPortBits(const PixelMap* bits)
{
	GetCurrentPort()->portBits = *bits;
}


// ROM 0x002be758 SetOrigin__FlT1
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


// ROM 0x002be7cc SetClip__FPP6Region
void
SetClip(RgnHandle rgn)
{
	CopyRgn(rgn, GetCurrentPort()->clipRgn);
}


// ROM 0x002be7f0 GetClip__FPP6Region
void
GetClip(RgnHandle rgn)
{
	CopyRgn(GetCurrentPort()->clipRgn, rgn);
}


// ROM 0x002be814 ClipRect__FP4Rect
void
ClipRect(const Rect* r)
{
	RectRgn(GetCurrentPort()->clipRgn, r);
}


/*------------------------------------------------------------------------------
	T h e   p e n
------------------------------------------------------------------------------*/

// ROM 0x00304050 HidePen__Fv
void
HidePen(void)
{
	GetCurrentPort()->pnVis--;
}


// ROM 0x0030407c ShowPen__Fv
void
ShowPen(void)
{
	GetCurrentPort()->pnVis++;
}


// ROM 0x003040a8 GetPen__FP5Point
void
GetPen(Point* pt)
{
	*pt = GetCurrentPort()->pnLoc;
}


// ROM 0x003040c8 GetPenState__FP8PenState
void
GetPenState(PenState* state)
{
	GrafPort* port = GetCurrentPort();
	state->pnLoc = port->pnLoc;
	state->pnSize = port->pnSize;
	state->pnMode = port->pnMode;
	state->fgPat = GetFgPattern();
}


// ROM 0x00304110 SetPenState__FP8PenState
void
SetPenState(const PenState* state)
{
	GrafPort* port = GetCurrentPort();
	port->pnLoc = state->pnLoc;
	port->pnSize = state->pnSize;
	port->pnMode = state->pnMode;
	port->fgPat = state->fgPat;
}


// ROM 0x00304158 PenSize__FlT1
void
PenSize(long width, long height)
{
	GrafPort* port = GetCurrentPort();
	port->pnSize.h = (short) width;
	port->pnSize.v = (short) height;
}


// ROM 0x0030418c PenMode__Fl
void
PenMode(long mode)
{
	GetCurrentPort()->pnMode = (short) mode;
}


// ROM 0x003041b0 PenNormal__Fv
// A one-pixel pen, copying, black.
void
PenNormal(void)
{
	PenSize(1, 1);
	PenMode(patCopy);
	GetCurrentPort()->fgPat = stdPatterns[blackPat];
}


// ROM 0x003041e4 MoveTo__FlT1
void
MoveTo(long h, long v)
{
	GrafPort* port = GetCurrentPort();
	port->pnLoc.h = (short) h;
	port->pnLoc.v = (short) v;
}


// ROM 0x00304218 Move__FlT1
void
Move(long dh, long dv)
{
	GrafPort* port = GetCurrentPort();
	port->pnLoc.h = (short) (port->pnLoc.h + dh);
	port->pnLoc.v = (short) (port->pnLoc.v + dv);
}
