/*
	File:		qd/Curves.h

	Contains:	The Newton's curves: a quadratic from a first point through
				a control point to a last one, in 16.16 (NewtQD.h's curve).
				The verbs go through the port's curveProc or StdCurve, which
				records the curve into an open picture (0x0c80 + the verb,
				or 0x0c88 + the verb alone for the curve the picture has
				already) and draws it: framed as 32 lines from the pen (FrCurve
				halves it five times), otherwise the inside of the curve
				closed by a line back to its start, as a region.

	Reconstructed from the MP2x00 US ROM (0x002d1c7c-0x002d2298); each
	function cites its origin.
*/

#ifndef __CURVES_H
#define __CURVES_H

#include "Ports.h"
#include "FixedGeometry.h"

void		SetCurve(curve* c, FPoint first, FPoint control, FPoint last);	// ROM 0x002d221c SetCurve__FP5curve6FPointN22
Boolean		EqualCurve(const curve* a, const curve* b);						// ROM 0x002d1d58 EqualCurve__FP5curveT1 - the same handle or the same six values
void		OffsetCurve(curve* c, Fixed dh, Fixed dv);						// ROM 0x002d21d0 OffsetCurve__FP5curvelT2
void		ScaleCurve(curve* c, Fixed hScale, Fixed vScale);				// ROM 0x002d2254 ScaleCurve__FP5curvelT2
void		MapCurve(curve* c, const Rect* src, const Rect* dst);			// ROM 0x002d1c94 MapCurve__FP5curveP4RectT2
void		GetCurveBounds(const curve* c, Rect* bounds);					// ROM 0x002d211c GetCurveBounds__FP5curveP4Rect

void		FrameCurve(curve* c);											// ROM 0x002d1c7c FrameCurve__FP5curve
void		PaintCurve(curve* c);											// ROM 0x002d1c88 PaintCurve__FP5curve
void		EraseCurve(curve* c);											// ROM 0x002d1fdc EraseCurve__FP5curve
void		InvertCurve(curve* c);											// ROM 0x002d1fe8 InvertCurve__FP5curve
void		FillCurve(curve* c, PatternHandle pattern);					// ROM 0x002d1ff4 FillCurve__FP5curvePP8PixelMap
void		CallCurve(GrafVerb verb, curve* c);							// ROM 0x002d1d98 CallCurve__FUcP5curve
extern "C" void	StdCurve(GrafVerb verb, curve* c);						// ROM 0x002d202c StdCurve
void		FrCurve(const curve* c, long depth);							// ROM 0x002d1dd8 FrCurve__FP5curvel - as lines from the pen, halved depth times (-1: five)
void		DrawCurve(const curve* c, long mode, PatternHandle pattern);	// ROM 0x002d1f10 DrawCurve__FP5curvelPP8PixelMap

#endif	/* __CURVES_H */
