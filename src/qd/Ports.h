/*
	File:		qd/Ports.h

	Contains:	QuickDraw's graphics ports, pens, patterns and pixel maps.
				A GrafPort (NewtQD.h) is the drawing environment: its
				portBits (the PixelMap drawn into), portRect, visRgn and
				clipRgn, the foreground and background patterns, the pen
				(location, size, mode, visibility) and, optionally, its own
				QDProcs.  The current port is per task in the ROM (the
				NewtGlobals of the task's TNewtWorld; a default gGrafPort
				before those exist); the host keeps one current port.

				Patterns are handles to 8x8 PixelMaps (one bit deep for the
				five standard ones - white, light gray, gray, dark gray,
				black - whose data is the ROM's).  The pixel maps here are
				the ROM's: rows of big-endian pixels, 1, 2, 4 or 8 bits
				deep, 0 white, all ones black; baseAddr a pointer, a handle
				or an offset from the PixelMap as pixMapFlags says.

	The ROM's layout (APCS, QD_Gray): PixelMap 0x1c bytes, GrafPort 0x54
	(portRect +0x1c, visRgn +0x24, clipRgn +0x28, fgPat +0x2c, bgPat
	+0x30, pnLoc +0x34, pnSize +0x38, pnMode +0x3c, pnVis +0x3e,
	grafProcs +0x40, picSave +0x44, rgnSave +0x48, polySave +0x4c,
	patAlign +0x50).

	Reconstructed from the MP2x00 US ROM (0x002e4388-0x002e4818,
	0x003280b0-0x00328268, 0x00328dfc, 0x00329330-0x00329874,
	0x002af0e0-0x002af1a8); each function cites its origin.  NOT YET
	RECONSTRUCTED: the screen (InitScreen: the display driver's PixelMap),
	the per-task globals, pictures, polygons, OpenRgn/CloseRgn.
*/

#ifndef __PORTS_H
#define __PORTS_H

#ifndef __REGIONS_H
#include "Regions.h"
#endif

// Fixed-point arithmetic the way the ARM does it, which is to say
// wrapping.  QuickDraw works in 16.16 on numbers that go negative - a
// coordinate off the left of the screen, a descent below the baseline, a
// slope that has run away - and on values that FixedDivide has already
// saturated to 0x7fffffff.  Shifting a negative signed long left and
// overflowing a signed multiply or addition are both undefined in C++ and
// both perfectly ordinary on the ARM, so every such place in the
// reconstruction goes through one of these and computes what the machine
// computed.
inline Fixed	ToFixed(long n)					{ return (Fixed) ((ULong) n << 16); }
inline Fixed	AddFixed(Fixed a, Fixed b)		{ return (Fixed) ((ULong) a + (ULong) b); }
inline Fixed	ScaleFixed(Fixed f, long n)		{ return (Fixed) ((ULong) f * (ULong) n); }
inline long		RoundFixed(Fixed f)				{ return AddFixed(f, 0x8000) >> 16; }


// the QuickDraw globals (ROM 0x0c104e3c stdPatterns, 0x0c104e50 qdGlobals)
struct QDGlobals
{
	long		fRandSeed;			// +0x00  1 from InitGraf: the seed of Random (GetRandSeed/SetRandSeed)
	PixelMap	fScreenBits;		// +0x04  the screen's pixel map (a port starts with it)
	long		fReserved20;		// +0x20
	long		fReserved24;		// +0x24
	long		fPolySize;			// +0x28  OpenPoly's buffer
	Handle		fPolyHandle;		// +0x2c
	long		fRgnSize;			// +0x30  OpenRgn's point buffer
	long		fRgnOffset;			// +0x34
	Handle		fRgnHandle;			// +0x38
};
extern QDGlobals		qdGlobals;

// the random numbers (the Macintosh's generator over the seed in qdGlobals)
long		GetRandSeed(void);					// ROM 0x0033f528 GetRandSeed__Fv
void		SetRandSeed(long seed);				// ROM 0x0033f538 SetRandSeed__Fl
long		Random(void);						// ROM 0x0033f488 Random__Fv - -32767..32767
long		Rand(long n);						// ROM 0x0025c5b4 Rand__Fl - 0..n-1
extern PatternHandle	stdPatterns[5];		// white, light gray, gray, dark gray, black
extern RgnHandle		wideHandle;			// the rectangle of every coordinate
extern GrafPort			gGrafPort;			// 0x0c103a98  the port before a task has its own
extern Boolean			gQDRunning;

// pixel maps
inline long	PixelMapDepth(const PixelMap* pm)	{ return pm->pixMapFlags & kPixMapDepth; }
Ptr			GetPixelMapBits(const PixelMap* pm);
long		GetPixelMapSize(const PixelMap* pm);
Boolean		PtInPixelMap(const PixelMap* pm, long x, long y);	// the pixel (from the map's origin) is not white
long		GetPixel(const PixelMap* pm, long x, long y);		// host: a pixel's value, coordinates in the map's bounds
void		SetPixel(PixelMap* pm, long x, long y, long value);

// patterns
PatternHandle	GetStdPattern(GetPatSelector which);
PatternHandle	MakeSimplePattern(long row0, long row1, long row2, long row3, long row4, long row5, long row6, long row7);
PatternHandle	MakeSimplePattern(const char* rows);
void			DisposePattern(PatternHandle pattern);		// the standard ones stay
PatternHandle	GetFgPattern(void);
PatternHandle	GetBgPattern(void);
long			PatternPixel(PatternHandle pattern, long x, long y, long depth);	// host: the pattern's pixel for (x, y), in depth

// the graphics library and ports
void		InitGraf(void);
GrafPort*	GetCurrentPort(void);
void		SetPort(GrafPort* port);
void		GetPort(GrafPort** port);
void		OpenPort(GrafPort* port);				// regions made, the port initialised and made current
void		InitPort(GrafPort* port);				// re-initialised (its regions kept) and made current
void		InitPortRgns(GrafPort* port);			// visRgn the screen, clipRgn wide open
void		ClosePort(GrafPort* port);
void		SetPortBits(const PixelMap* bits);
void		SetOrigin(long h, long v);
void		SetClip(RgnHandle rgn);
void		GetClip(RgnHandle rgn);
void		ClipRect(const Rect* r);
void		SetStdProcs(QDProcs* procs);

// the pen
void		HidePen(void);
void		ShowPen(void);
void		GetPen(Point* pt);
void		GetPenState(PenState* state);
void		SetPenState(const PenState* state);
void		PenSize(long width, long height);
void		PenMode(long mode);
void		SetFgPattern(PatternHandle pattern);		// (PenPat)
void		SetBgPattern(PatternHandle pattern);		// (BackPat)
void		PenNormal(void);
void		MoveTo(long h, long v);
void		Move(long dh, long dv);

#endif	/* __PORTS_H */
