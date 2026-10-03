/*
	File:		qd/Draw.h

	Contains:	Drawing rectangles and regions into the current port, and
				the blitter beneath everything.  A drawing call (PaintRect,
				FrameRgn, ...) goes through the port's QDProcs when it has
				them, else the standard procs (StdRect, StdRgn), which record
				into an open picture (PicRecord.h) or region and
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

	The ROM's blitter (RgnBlt 0x00343228, BitBlt 0x002ac9c8 and the BB*
	routines) works a word at a time in the map's depth; the host works a
	byte at a time with the same results (DEVIATION, for speed: the code,
	not the pixels - SetQDSlowBlitter below).  Lines, ovals and the rest
	are Shapes.h and Polygons.h, pictures PicPlay.h, text Text.h,
	StretchBits Stretch.cpp.  A blit onto the screen is bracketed by
	QDStartDrawing/QDStopDrawing (Screen.h).

	Reconstructed from the MP2x00 US ROM (0x0034005c-0x003400b8,
	0x0034078c, 0x003409c8-0x00340d28, 0x00341414-0x00341504, 0x003415c4,
	0x00341774, 0x00341860, 0x00341e28, 0x002ac9c8, 0x002ad604-0x002ad664,
	0x002ae440, 0x00343228); each function cites its origin.
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

// The host's blitter has two ways of drawing the same pixels: the fast one
// (the rows worked out in place, the pixel maps' bits addressed directly)
// and the slow one it was first written as, a pixel at a time through
// GetPixel/SetPixel - kept as the oracle the fast one is checked against
// (qd/tests/test_Blitter.cpp draws every case both ways and compares).
// NEWTON_QD_SLOW=1 in the environment makes the slow one the default.
void	SetQDSlowBlitter(Boolean slow);
Boolean	QDSlowBlitter(void);

// The inker's line: the segment from one pen sample to the next, drawn
// straight into a pixel map (the screen's, when none is named) without a
// port, a pen or a clipping region - which is what lets ink keep up with
// the pen while the view system is busy.  `pen` is the nib's size, and
// `damaged` comes back as the part of the map that was drawn on.
void	InkerLine(const Point from, const Point to, Rect* damaged, const Point pen);	// ROM 0x002f7c3c InkerLine__FC5PointT1P4RectT1
void	InkerLine(const Point from, const Point to, Rect* damaged, const Point pen,
				  const PixelMap* map);		// ROM 0x002f7c64 InkerLine__FC5PointT1P4RectT1PC8PixelMap

#endif	/* __DRAW_H */
