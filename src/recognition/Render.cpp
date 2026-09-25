/*
	File:		recognition/Render.cpp

	Contains:	The handwriting engine's renderer - see Render.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "Render.h"
#include "RosStrokes.h"			// the engine's own allocation
#include "NewtonMemory.h"

#include <stdio.h>


// ROM 0x001a6d40 RenderRecNew
RenderRec*
RenderRecNew(void)
{
	RenderRec* rec = (RenderRec*) RosAllocate((long) sizeof(RenderRec));
	rec->fBits = nil;
	rec->fDotSize = 0;
	rec->fDotOffset = 0;
	rec->fStencils = nil;
	return rec;
}


// ROM 0x001a70d8 RenderClear
void
RenderClear(RenderRec* rec)
{
	UByte* bits = rec->fBits;
	for (long i = rec->fSize; i > 0; i--)
		*bits++ = 0;
}


// ROM 0x001a70b4 RenderSetDotSize
// The pen: how wide it is, what a point is moved by so that it is
// drawn about its middle, and the eight pre-shifted stencils.
void
RenderSetDotSize(RenderRec* rec, long size)
{
	rec->fDotSize = size;
	rec->fDotOffset = size >> 1;
	rec->fStencils = kDotStencils[size];
}


// ROM 0x001a6dd8 RenderRecCreate
// A one-bit bitmap, cleared, with a pen one pixel across.
RenderRec*
RenderRecCreate(long width, long height)
{
	RenderRec* rec = RenderRecNew();
	newton_try
	{
		rec->fWidth = width;
		rec->fHeight = height;
		rec->fRowBytes = (width + 7) >> 3;
		rec->fSize = rec->fRowBytes * rec->fHeight;
		rec->fBits = (UByte*) RosAllocate(rec->fSize);
		RenderClear(rec);
		RenderSetDotSize(rec, 1);
	}
	cleanup
	{
		RenderRecDestroy(rec);
	}
	end_try;
	return rec;
}


// ROM 0x001a6da8 RenderRecDestroy
void
RenderRecDestroy(RenderRec* rec)
{
	if (rec == nil)
		return;
	if (rec->fBits != nil)
		DisposPtr((Ptr) rec->fBits);
	DisposPtr((Ptr) rec);
}


// ROM 0x001a6edc RenderLine
// Bresenham, with the pen put down at every step.  The pen is drawn
// about its middle, so both ends are moved back by half its width
// first; if either end then falls outside the bitmap **nothing at all
// is drawn** - there is no clipping, only a refusal.
//
// The loop is written over a major and a minor coordinate with a
// pointer to each of x and y, so that the steep and the shallow case
// are the same code; putting the pen down is then an OR of the
// stencil for the x position's phase, which is why there are eight of
// them.
void
RenderLine(RenderRec* rec, long x0, long y0, long x1, long y1)
{
	long inset = rec->fDotOffset;
	x0 -= inset;	y0 -= inset;
	x1 -= inset;	y1 -= inset;
	if (x0 < 0 || y0 < 0)
		return;
	long limitX = rec->fWidth - rec->fDotSize;
	long limitY = rec->fHeight - rec->fDotSize;
	if (x0 > limitX || y0 > limitY)
		return;
	if (x1 < 0 || y1 < 0)
		return;
	if (x1 > limitX || y1 > limitY)
		return;

	long dx = x1 - x0;
	long dy = y1 - y0;

	// the coordinates, and which of them the loop runs over
	long at[2];
	long* x;
	long* y;
	long major, minor;
	if ((dx - dy) * (dx + dy) < 0)
	{
		// taller than it is wide: the loop runs over y
		at[0] = y0;	at[1] = x0;
		y = &at[0];	x = &at[1];
		major = dy;	minor = dx;
	}
	else
	{
		at[0] = x0;	at[1] = y0;
		x = &at[0];	y = &at[1];
		major = dx;	minor = dy;
	}

	long step = 1;
	if (major < 0)
	{
		// backwards: start at the other end and go forwards - both
		// coordinates moved to it.  (A decompile shows only the first
		// move; the second is there at 0x001a6fb8, and without it a
		// line drawn backwards comes out shifted by its own width and
		// can run off the bitmap.)
		at[0] += major;
		at[1] += minor;
		major = -major;
		minor = -minor;
	}
	if (minor < 0)
	{
		minor = -minor;
		step = -1;
	}

	long end = at[0] + major;
	long error = major >> 1;
	const RenderStencil* stencils = rec->fStencils;
	for (; at[0] <= end; at[0]++)
	{
		const RenderStencil* dot = &stencils[*x & 7];
		const UByte* src = dot->fBits;
		UByte* row = rec->fBits + *y * rec->fRowBytes + (*x >> 3);
		for (long r = dot->fRows; r > 0; r--)
		{
			UByte* p = row;
			for (long c = dot->fByteWidth; c > 0; c--)
				*p++ |= *src++;
			row += rec->fRowBytes;
		}
		error -= minor;
		if (error < 0)
		{
			error += major;
			at[1] += step;
		}
	}
}


// ROM 0x001a6b18 RenderAANew
RenderAA*
RenderAANew(void)
{
	RenderAA* aa = (RenderAA*) RosAllocate((long) sizeof(RenderAA));
	aa->fPixels = nil;
	aa->fRec = nil;
	return aa;
}


// ROM 0x001a6bb0 RenderAACreate
// A grey grid of that size, with a one-bit bitmap `1 << shift` times
// as wide and tall behind it and a pen that size across - so the pen
// is one output cell wide however big the scale is.
RenderAA*
RenderAACreate(long width, long height, long shift)
{
	if (shift < 0 || shift > kRenderMaxShift)
	{
		// (the ROM writes the complaint into a buffer on its stack and
		//  does nothing with it)
		char message[404];
		sprintf(message, "RenderAA(%ld, %ld, logscale=%ld): Invalid scale.\r", width, height, shift);
		return nil;
	}

	// (`volatile` because the handler below reads it after a longjmp)
	RenderAA* volatile aa = nil;
	newton_try
	{
		aa = RenderAANew();
		aa->fPixels = (UByte*) RosAllocate(width * height);
		aa->fWidth = width;
		aa->fHeight = height;
		aa->fShift = shift;
		aa->fScale = 1 << shift;
		aa->fRec = RenderRecCreate(width * aa->fScale, height * aa->fScale);
		RenderSetDotSize(aa->fRec, (long) ((0x00010000u << shift) >> 16));
	}
	cleanup
	{
		RenderAADestroy(aa);
	}
	end_try;
	return aa;
}


// ROM 0x001a6b78 RenderAADestroy
void
RenderAADestroy(RenderAA* aa)
{
	if (aa == nil)
		return;
	if (aa->fPixels != nil)
		DisposPtr((Ptr) aa->fPixels);
	RenderRecDestroy(aa->fRec);
	DisposPtr((Ptr) aa);
}


// ROM 0x001a69b0 RenderAAFlush
// The sub-pixels counted into the grid.  One pass over the one-bit
// bitmap: each byte of a source row says, through the table, how much
// each of the `8 >> shift` output cells it covers gains, and every
// `1 << shift` source rows the output moves on a row.
void
RenderAAFlush(RenderAA* aa)
{
	RenderRec* rec = aa->fRec;
	long shift = aa->fShift;
	UByte* out = aa->fPixels;
	if (shift < 0 || shift > kRenderMaxShift)
	{
		char message[520];
		sprintf(message, "RenderAA(logscale=%ld): Invalid scale.\r", shift);
		return;
	}

	long cells = 8 >> shift;
	const UByte* table = kAATables[shift];
	long outWidth = rec->fWidth >> shift;
	long inBlock = 0;
	long row = 0;
	const UByte* src = rec->fBits;

	for (;;)
	{
		// a fresh output row
		for (long i = 0; i < outWidth; i++)
			out[i] = 0;
		do
		{
			UByte* p = out;
			for (long b = 0; b < rec->fRowBytes; b++)
			{
				const UByte* counts = table + src[b] * cells;
				for (long c = 0; c < cells; c++)
					*p++ = (UByte) (*p + counts[c]);
			}
			if (++inBlock >= aa->fScale)
			{
				inBlock = 0;
				out += outWidth;
			}
			row++;
			src += rec->fRowBytes;
			if (row >= rec->fHeight)
				return;
		}
		while (inBlock != 0);
	}
}
