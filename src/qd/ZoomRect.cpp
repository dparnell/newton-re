/*
	File:		qd/ZoomRect.cpp

	Contains:	ZoomRect: the zooming rectangles drawn between two
				rectangles (the book reader's ZoomView, as a picture is
				zoomed open from where it is on the page), and FixStep.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Rects.h"
#include "Regions.h"
#include "Ports.h"
#include "Draw.h"
#include "Screen.h"
#include "FixedMath.h"
#include "UserTasks.h"

void	ZoomRect(Rect* from, Rect* to, long steps, Boolean zoomIn);


// ROM 0x00340d28 FixStep__FlN21
// The point a fraction t (16.16) of the way from a to b, rounded.
short
FixStep(long a, long b, long t)
{
	Fixed fa = FixedMultiply(a << 16, 0x10000 - t);
	Fixed fb = FixedMultiply(b << 16, t);
	return (short) ((ULong) (fb + fa + 0x8000) >> 16);
}


// ROM 0x003404e0 ZoomRect__FP4RectT1lUc
// Rectangles drawn in gray, in XOR, from one rectangle towards the other
// (at least five steps; each a sixtieth and a bit apart - Sleep(0x1e000),
// the three last ones left on the screen for a moment and then taken off
// again): zooming in, the fraction starts at 0.833^(steps-1) and grows by
// 1.2 a step, and the first rectangle framed is where it starts; zooming
// out it starts at 1 and shrinks by 0.833, from where it ends.  Each step
// frames the new rectangle and takes off the one three steps back.
void
ZoomRect(Rect* from, Rect* to, long steps, Boolean zoomIn)
{
	GrafPort* port;
	GetPort(&port);
	RgnHandle clip = NewRgn();
	GetClip(clip);
	SetClip(port->visRgn);		// (the ROM's [port,#0x24] at 0x00340518: the visRgn)
	SetFgPattern(stdPatterns[grayPat]);
	PenMode(notPatXor);
	if (steps < 5)
		steps = 5;
	Fixed factor = 0xd555;
	Fixed t;
	Rect r1, r2, r3, next;
	if (!zoomIn)
	{
		t = 0x10000;
		r1 = *to;
	}
	else
	{
		t = factor;
		for (long i = steps - 2; i >= 0; i--)
			t = FixedMultiply(t, 0xd555);
		factor = 0x13333;
		r1 = *from;
	}
	r3 = r1;
	r2 = r1;
	FrameRect(&r1);
	for ( ; steps >= 0; steps--)
	{
		next.top = FixStep(from->top, to->top, t);
		next.left = FixStep(from->left, to->left, t);
		next.bottom = FixStep(from->bottom, to->bottom, t);
		next.right = FixStep(from->right, to->right, t);
		Sleep(0x1e000);
		StartDrawing(nil, nil);
		FrameRect(&next);
		FrameRect(&r1);
		StopDrawing(nil, nil);
		r1 = r2;
		r2 = r3;
		r3 = next;
		t = FixedMultiply(t, factor);
	}
	Sleep(0x1e000);
	FrameRect(&r1);
	Sleep(0x1e000);
	FrameRect(&r2);
	Sleep(0x1e000);
	FrameRect(&r3);
	PenNormal();
	SetClip(clip);
	DisposeRgn(clip);
}
