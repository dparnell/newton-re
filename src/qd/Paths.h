/*
	File:		qd/Paths.h

	Contains:	The Newton's paths: a handle of contours, each a closed
				outline of points in 16.16 with a bit per point saying
				whether it is off the curve (NewtQD.h's paths, path) - the
				TrueType way: two points on the curve are a line, an
				off-curve point between two on-curve ones a quadratic
				curve through it, and between two off-curve points there is
				an on-curve one at their middle.  A contour is its count,
				(count + 31) / 32 words of bits (the first point's the top
				bit of the first word) and the points; the path walker
				(InitPathWalker, NextPathSegment) turns one into its lines
				and curves, closing it back to its start.

				The verbs go through the port's pathsProc or StdPaths, which
				records the paths into an open picture (0x8190 + the verb,
				the handle's size and its bytes) and draws them: framed as
				each contour's lines and curves from its start (FrCurve for
				a curve), otherwise the inside of the outlines as a region
				(the regions' inversion points make it even-odd).

	Reconstructed from the MP2x00 US ROM (0x00327800-0x003280b0); each
	function cites its origin.
*/

#ifndef __PATHS_H
#define __PATHS_H

#include "Ports.h"
#include "FixedGeometry.h"

void		FramePaths(pathsHandle p);										// ROM 0x00327800 FramePaths__FPP5paths
void		PaintPaths(pathsHandle p);										// ROM 0x0032780c PaintPaths__FPP5paths
void		ErasePaths(pathsHandle p);										// ROM 0x00327c74 ErasePaths__FPP5paths
void		InvertPaths(pathsHandle p);										// ROM 0x00327dc4 InvertPaths__FPP5paths
void		FillPaths(pathsHandle p, PatternHandle pattern);				// ROM 0x00327dd0 FillPaths__FPP5pathsPP8PixelMap
void		CallPaths(GrafVerb verb, pathsHandle p);						// ROM 0x00327950 CallPaths__FUcPP5paths
extern "C" void	StdPaths(GrafVerb verb, pathsHandle p);					// ROM 0x00327e08 StdPaths

void		MapPaths(pathsHandle p, const Rect* src, const Rect* dst);		// ROM 0x00327818 MapPaths__FPP5pathsP4RectT2
void		OffsetPaths(pathsHandle p, Fixed dh, Fixed dv);					// ROM 0x00327fbc OffsetPaths__FPP5pathslT2
void		ScalePaths(pathsHandle p, Fixed hScale, Fixed vScale);			// ROM 0x00328028 ScalePaths__FPP5pathslT2
void		GetPathsBounds(pathsHandle p, Rect* bounds);					// ROM 0x00327ed0 GetPathsBounds__FPP5pathsP4Rect
long		SizeOfPaths(pathsHandle p);										// ROM 0x003278dc SizeOfPaths__FPP5paths
void		CopyPaths(pathsHandle src, pathsHandle dst);					// ROM 0x003278e0 CopyPaths__FPP5pathsT1
void		DisposePaths(pathsHandle p);									// ROM 0x00327ecc DisposePaths__FPP5paths

// the path walker
path*		NextPath(path* contour);										// ROM 0x00327990 NextPath__FP4path - the contour after this one
Boolean		OnCurve(const long* bits, long index);							// ROM 0x00327c50 OnCurve__FPll
void		InitPathWalker(pathWalker* walker, path* contour);				// ROM 0x003279ac InitPathWalker__FP10pathWalkerP4path
Boolean		NextPathSegment(pathWalker* walker);							// ROM 0x00327a74 NextPathSegment__FP10pathWalker - the next line (isLine) or curve in c; false at the end
path*		FramePath(path* contour);										// ROM 0x00327c80 FramePath__FP4path - drawn from its start as lines; ==> the next contour
void		FrPaths(pathsHandle p);											// ROM 0x00327bfc FrPaths__FPP5paths
void		DrawPaths(pathsHandle p, long mode, PatternHandle pattern);		// ROM 0x00327b9c DrawPaths__FPP5pathslPP8PixelMap

#endif	/* __PATHS_H */
