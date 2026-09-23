/*
	File:		recognition/StrokeBundle.h

	Contains:	Stroke bundles: the NewtonScript form of a handful of
				strokes.  A bundle is a frame of class 'strokeBundle
				(cloned from the ROM's `Rstrokebundle`) with a `strokes`
				array, a `bounds`, and the `startTime` and `endTime` of
				the writing it came from.  Each member of the array is a
				binary of class 'stroke: one four-byte Point per sample,
				v then h, both in eighths of a pixel, big-endian as they
				are written to a soup.

				This is what a recogniser hands a view when the writing
				is to be kept rather than read - the aeAddInk command
				carries one - and what the ink of a paragraph is made
				from (StrokeBundleToInkWord, ink/InkShapes.h).

				The eighths are the ROM's own resolution rather than the
				tablet's: a point is rounded to whole pixels with
				(value + 4) >> 3 when it is asked for in pixels, which is
				what every function here that takes a `format` of 0 or 1
				does; 2 or more asks for the eighths themselves.

	Reconstructed from the MP2x00 US ROM (0x00144e54, 0x0019fd24-
	0x0019ff40, 0x001a17ec-0x001a2544, 0x001a3494, 0x001a3b48-0x001a3d60);
	each function cites its origin.
*/

#ifndef __STROKEBUNDLE_H
#define __STROKEBUNDLE_H

#include "Objects.h"
#include "NewtonTypes.h"

class TStroke;
class TUnitPublic;

// One stroke as a 'stroke binary.
Ref		MakeStrokeRef(TStroke* stroke);						// ROM 0x001a2474 MakeStrokeRef__FP7TStroke
// ... and from an array of numbers, v and h alternating: a format under
// two means they are pixels and are to be multiplied up to eighths.
Ref		MakeStrokeRef(RefArg points, long format);			// ROM 0x001a1f2c MakeStrokeRef__FRC6RefVarl

long	CountStrokes(RefArg bundle);						// ROM 0x001a17ec CountStrokes__FRC6RefVar
Ref		GetStroke(RefArg bundle, long index);				// ROM 0x001a1838 GetStroke__FRC6RefVarl
long	CountPoints(RefArg stroke);							// ROM 0x001a19d0 CountPoints__FRC6RefVar

// The box a stroke, or a whole bundle, covers, in pixels.
void	GetStrokeBounds(RefArg stroke, Rect* bounds);		// ROM 0x001a19f0 GetStrokeBounds__FRC6RefVarP5TRect
void	GetBundleBounds(RefArg bundle, Rect* bounds);		// ROM 0x001a188c GetBundleBounds__FRC6RefVarP5TRect
void	CalcBundleBounds(RefArg bundle);					// ROM 0x001a1924 CalcBundleBounds__FRC6RefVar - and written back to the frame

// One point of a stroke.
void	GetStrokePoint(RefArg stroke, long index, Point* pt, long format);	// ROM 0x001a1acc GetStrokePoint__FRC6RefVarlP6TPointT2
// All of them, as an array of alternating numbers.  The format's low
// byte is as above; bit 0 set keeps every point, clear drops those
// nearer the one before than the distance in the next byte up, and bit
// 16 asks for h before v rather than the Point order.
Ref		GetStrokePointsArray(RefArg stroke, long format);	// ROM 0x001a1b88 GetStrokePointsArray__FRC6RefVarl

// A bundle from an array of point arrays, or from the units a
// recogniser has finished with (which also answers their box, let out
// for the pen).
Ref		MakeStrokeBundle(RefArg strokes, long format);		// ROM 0x001a1db4 MakeStrokeBundle__FRC6RefVarl
Ref		StrokeBundle(TUnitPublic** units, Rect* bounds);	// ROM 0x00144e54 StrokeBundle__FPP11TUnitPublicP5TRect
Ref		ExpandUnit(TUnitPublic* unit);					// ROM 0x001a2554 ExpandUnit__FP11TUnitPublic - one unit's strokes as a bundle

// A bundle back into strokes, and drawn from the box it was written in
// into another.
TStroke**	StrokeBundleToTStrokes(RefArg bundle);			// ROM 0x001a3494 StrokeBundleToTStrokes__FRC6RefVar
void	DrawStrokeBundle(RefArg bundle, Rect* from, Rect* to);	// ROM 0x001a2170 DrawStrokeBundle__FRC6RefVarP5TRectT2

void	RegisterStrokeBundleNatives(void);

#endif	/* __STROKEBUNDLE_H */
