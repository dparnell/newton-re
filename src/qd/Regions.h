/*
	File:		qd/Regions.h

	Contains:	QuickDraw's regions: an arbitrary set of pixels as a handle
				to a Region (NewtQD.h): rgnSize, the bounding box, and -
				unless the region is that rectangle (rgnSize 12) - rows of
				shorts: a y, then the x positions at which membership flips
				from that y down (a change list against the rows above it),
				0x7fff ending the row and another 0x7fff ending the region.
				This is the Macintosh's region format; the ROM's operations
				(from Apple's QuickDraw, rewritten in C) turn two regions
				into the points where the result's rows change (RgnOp),
				sort them and pack them back into the format (PackRgn).

				Membership tests (PtInRgn, RectInRgn) and drawing scan-convert
				a region a row at a time into a bit mask (RgnState, SeekRgn)
				whose layout is the current port's pixel depth (NOT YET
				RECONSTRUCTED: ports; the host uses one bit per pixel).
				Drawing (FrameRgn, PaintRgn, ...) is NOT YET RECONSTRUCTED.

	Reconstructed from the MP2100 D ROM (0x00314884-0x00314988,
	0x003150b0-0x00316dc4); each function cites its origin.
*/

#ifndef __REGIONS_H
#define __REGIONS_H

#ifndef __RECTS_H
#include "Rects.h"
#endif
#ifndef __NEWTONMEMORY_H
#include "NewtonMemory.h"
#endif

const short	kRgnEnd = 0x7fff;				// ends a row's x list and the region's rows
const long	kRectRgnSize = 12;				// a rectangular region: the header alone
const long	kRgnRowsOffset = 12;			// where the rows start

// the region operations of RgnOp/DoRgnOp
enum
{
	kRgnOpSect = 0,
	kRgnOpDiff = 2,
	kRgnOpUnion = 4,
	kRgnOpXor = 6,
	kRgnOpInset = 8
};

RgnHandle	NewRgn(void);
void		DisposeRgn(RgnHandle rgn);
void		SetEmptyRgn(RgnHandle rgn);
void		SetRectRgn(RgnHandle rgn, long left, long top, long right, long bottom);
void		RectRgn(RgnHandle rgn, const Rect* r);
Boolean		CopyRgn(RgnHandle src, RgnHandle dst);				// ==> false when the copy could not be sized
void		OffsetRgn(RgnHandle rgn, long dh, long dv);
void		InsetRgn(RgnHandle rgn, long dh, long dv);
void		SectRgn(RgnHandle a, RgnHandle b, RgnHandle dst);
void		UnionRgn(RgnHandle a, RgnHandle b, RgnHandle dst);
void		DiffRgn(RgnHandle a, RgnHandle b, RgnHandle dst);		// a minus b
void		XorRgn(RgnHandle a, RgnHandle b, RgnHandle dst);
void		DoRgnOp(long op, RgnHandle a, RgnHandle b, RgnHandle dst);
Boolean		EmptyRgn(RgnHandle rgn);
Boolean		EqualRgn(RgnHandle a, RgnHandle b);
Boolean		IsWideOpenRgn(RgnHandle rgn);							// nil, or the rectangle of every coordinate
Boolean		PtInRgn(Point pt, RgnHandle rgn);
Boolean		RectInRgn(const Rect* r, RgnHandle rgn);				// any pixel of the rectangle is in the region
void		MapRgn(RgnHandle rgn, const Rect* src, const Rect* dst);
long		TrimRect(RgnHandle rgn, Rect* r);						// r cut to the region: 0 when the intersection is a rectangle (r), <0 empty, >0 more complex

// the internals other QuickDraw code shares
struct TrueRegion;				// a Region with its rows (the ROM's name for the non-rectangular form)
long		RgnOp(RgnHandle a, RgnHandle b, Handle points, long pointsSize, long op, long inset, Boolean canGrow);	// ==> how many points, -1 for no memory
long		PackRgn(Handle points, long count, RgnHandle rgn);
void		SortPoints(Point* points, long count);
void		CullPoints(Point* points, long* count);
Boolean		PutRgn(RgnHandle rgn, Handle points, long* offset, long* limit);
void		PutRect(const Rect* r, Handle points, long* offset, long* limit);

// scan conversion: the rows of a region applied to a bit mask of one
// pixel row at a time
struct RgnState
{
	Region*		fRegion;		// +0x00
	const short* fRow;			// +0x04  the next row to apply
	ULong32*	fScan;			// +0x08  the mask of the current pixel row
	long		fScanWords;		// +0x0c  its words, less one
	long		fTop;			// +0x10  the row the mask applies from (-0x7fff before the first)
	long		fBottom;		// +0x14  the row where the next change comes
	long		fLeft;			// +0x18  the mask's range of x
	long		fRight;			// +0x1c
	long		fOrigin;		// +0x20  the x of the mask's first pixel
	long		fShift;			// +0x24  log2 of the pixels per word
	long		fDepth;			// +0x28  bits per pixel
};
void		InitRgnRec(Region* rgn, RgnState* state, long left, long right, long origin);
void		InitRgn(Region* rgn, RgnState* state, long left, long right, long origin, char* scan);
Boolean		SeekRgn(RgnState* state, long y);						// the mask made that of pixel row y; ==> whether it changed

void*		QDNewTempPtr(long size);
void		QDDisposeTempPtr(void* p);

#endif	/* __REGIONS_H */
