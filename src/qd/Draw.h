/*
	File:		qd/Draw.h

	Contains:	Drawing rectangles and regions into the current port, and
				the blitter beneath everything.  A drawing call (PaintRect,
				FrameRgn, ...) goes through the port's QDProcs when it has
				them, else the standard procs (StdRect, StdRgn), which record
				into an open picture or region (NOT YET RECONSTRUCTED) and
				draw with the verb's mode and pattern (PushVerb: frame and
				paint use the pen's, erase the background pattern copied,
				invert black xor-ed, fill the port's pattern copied - FillRect
				installs its argument for the call).  Everything ends in
				RgnBlt: a source rectangle (or the pattern) transferred into
				the destination rectangle under the transfer mode, clipped by
				up to three regions (the port's visRgn and clipRgn and the
				shape being painted), or BitBlt when the clips are rectangles.

				Transfer modes (NewtQD.h): srcCopy 0, srcOr 1, srcXor 2,
				srcBic 3 and their notSrc forms 4-7 with the source
				inverted; patCopy 8 .. notPatBic 15 take the pattern for the
				source.  "Or" on a gray map is the ROM's: every non-white
				source pixel replaces the destination pixel.

	The ROM's blitter (RgnBlt 0x003172e0, BitBlt 0x00287e20 and the BB*
	routines) works a word at a time in the map's depth; the host works a
	pixel at a time with the same results (DEVIATION: the code, not the
	pixels).  NOT YET RECONSTRUCTED: lines, ovals, round rectangles, arcs,
	polygons, pictures, text, StretchBits (CopyBits between rectangles of
	different sizes), the screen locking around a blit (QDStartDrawing).

	Reconstructed from the MP2100 D ROM (0x00314114-0x00314170,
	0x00314844, 0x00314a80-0x00314de0, 0x003154cc-0x003155bc, 0x0031567c,
	0x0031582c, 0x00315918, 0x00315ee0, 0x00287e20, 0x00288a5c-0x00288abc,
	0x00289898, 0x003172e0); each function cites its origin.
*/

#ifndef __DRAW_H
#define __DRAW_H

#ifndef __PORTS_H
#include "Ports.h"
#endif

// rectangles
void	FrameRect(const Rect* r);
void	PaintRect(const Rect* r);
void	EraseRect(const Rect* r);
void	InvertRect(const Rect* r);
void	FillRect(const Rect* r, PatternHandle pattern);
void	CallRect(GrafVerb verb, const Rect* r);
void	StdRect(GrafVerb verb, Rect* r);
void	FrRect(const Rect* r);									// the frame, pen wide, in the pen's mode and pattern
void	DrawRect(const Rect* r, long mode, PatternHandle pattern);

// regions
void	FrameRgn(RgnHandle rgn);
void	PaintRgn(RgnHandle rgn);
void	EraseRgn(RgnHandle rgn);
void	InvertRgn(RgnHandle rgn);
void	FillRgn(RgnHandle rgn, PatternHandle pattern);
void	CallRgn(GrafVerb verb, RgnHandle rgn);
void	StdRgn(GrafVerb verb, RgnHandle rgn);
void	FrRgn(RgnHandle rgn, long mode, PatternHandle pattern);
void	DrawRgn(RgnHandle rgn, long mode, PatternHandle pattern);

void	PushVerb(GrafVerb verb, long* mode, PatternHandle* pattern);

// bits
void	CopyBits(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, RgnHandle mask);
void	CallBits(PixelMap* src, const Rect* srcRect, const Rect* dstRect, long mode, RgnHandle mask);
void	StdBits(PixelMap* src, Rect* srcRect, Rect* dstRect, long mode, RgnHandle mask);
void	StretchBits(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, RgnHandle clip1, RgnHandle clip2, RgnHandle mask);
void	RgnBlt(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, PatternHandle pattern, RgnHandle clip1, RgnHandle clip2, RgnHandle clip3);
void	BitBlt(PixelMap* src, PixelMap* dst, const Rect* srcRect, const Rect* dstRect, long mode, PatternHandle pattern);

#endif	/* __DRAW_H */
