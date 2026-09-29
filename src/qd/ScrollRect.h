/*
	File:		qd/ScrollRect.h

	Contains:	ScrollRect: the bits of a rectangle of the current port
				moved by (dh, dv), and the part of it they left behind -
				the update region, which the caller redraws - filled with
				the port's background pattern.  Only what shows (the
				visRgn and the clipRgn) is moved; with the pen hidden, or
				nothing to move, nothing is done and the update region is
				empty.

				StartProtectSrcBits/StopProtectSrcBits bracket a blit whose
				source is the screen; on the MP2x00 both are empty.

	Reconstructed from the MP2x00 US ROM (0x00340378, 0x001ccf2c); each
	function cites its origin.
*/

#ifndef __SCROLLRECT_H
#define __SCROLLRECT_H

#include "Ports.h"

void	ScrollRect(Rect* r, long dh, long dv, RgnHandle updateRgn);	// ROM 0x00340378 ScrollRect__FP4RectlT2PP6Region
void	StartProtectSrcBits(PixelMap* map, Rect* r);				// ROM 0x001ccf2c StartProtectSrcBits__FP8PixelMapP4Rect - nothing
void	StopProtectSrcBits(PixelMap* map);							// ROM 0x001ccf30 StopProtectSrcBits__FP8PixelMap - nothing

#endif	/* __SCROLLRECT_H */
