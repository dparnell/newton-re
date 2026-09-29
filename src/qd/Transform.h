/*
	File:		qd/Transform.h

	Contains:	TTransform, the mapping from one rectangle onto another that
				`view:DrawShape(shape, {transform: ...})` draws through, and
				the scaler that holds the ones in force.

				A transform is two rectangles and the scale between them.
				The style frame gives them either as `[srcRect, dstRect]` or
				as `[dx, dy]` - a pair of numbers, which stands for two
				ten-by-ten rectangles offset by them, so it scales by one
				and only moves.  That is how a list draws the hilite of any
				of its rows out of one shape: the shape is the first row's,
				and the transform moves it down by the row's height
				(`protoOverview`'s `hiliter`).

				The transforms in force are `gScale`, a TQDScaler: a stack
				of them (StartScaling pushes one, StopScaling pops it) and
				the one they come to together (RecalcTransform: the first's
				source rectangle mapped to where they all take it).  While
				there is one, the port's procs are the scaler's
				(ScaledRect, ScaledText, ...), which put every coordinate
				QuickDraw is given through that transform and hand it to
				the procs the port had: rectangles, points and polygons,
				regions, curves and paths mapped; a frame's pen scaled; the
				port's clip region - which the drawing sets in its own
				coordinates - mapped too and cut by the clip the port had
				when the scaling began; text drawn at the scales times the
				transform's.  Nothing is scaled while a picture, region or
				polygon is being recorded (SkipScaling), or while scaling is
				forced off (ForceScaling).

	Reconstructed from the MP2x00 US ROM (0x00196018-0x001973c8,
	0x001973e8-0x00197c8c); each function cites its origin.
*/

#ifndef __TRANSFORM_H
#define __TRANSFORM_H

#include "Rects.h"
#include "Ports.h"

// the transform flags
enum
{
	kTransformSetUp		= 0x80000000,	// the two rectangles and the scale are there
	kTransformNoScale	= 0x40000000	// both scales are one: it only moves things
};

struct TTransform
{
	Fixed	fScaleH;		// +0x00  the destination's width over the source's
	Fixed	fScaleV;		// +0x04  and its height over the source's
	Rect	fSrc;			// +0x08
	Rect	fDst;			// +0x10
	ULong	fFlags;			// +0x18

	void	Setup(const Rect* src, const Rect* dst, Boolean square);	// ROM 0x001973e8 Setup__10TTransformFPC5TRectT1Uc - square: the smaller scale used for both and the destination cut back to it
};

void	Scale(Rect* rect, const TTransform& transform);		// ROM 0x001979bc Scale__5TRectFRC10TTransform
void	Scale(Point* pt, const TTransform& transform);		// ROM 0x00197bcc Scale__6TPointFRC10TTransform


// The transforms in force, innermost last, and the port's procs replaced
// while they are.  The ROM's is 200 bytes, the offsets its (host: pointers
// are wider).
class CDynamicArray;

class TQDScaler
{
public:
					TQDScaler();											// ROM 0x00196018 __ct__9TQDScalerFv
					~TQDScaler();											// ROM 0x00196068 __dt__9TQDScalerFv

	// the scaling as the view system asks for it
	static void		StartScaling(const TTransform* transform, Boolean mapVis = false, long features = 0);	// ROM 0x00196540 StartScaling__9TQDScalerSFP10TTransformUcl
	static void		StartScaling(const TTransform& transform)		{ StartScaling(&transform); }
	static void		ReplaceScaling(const TTransform* transform);		// ROM 0x00196de8 ReplaceScaling__9TQDScalerSFP10TTransform
	static void		ReplaceScaling(const TTransform& transform)		{ ReplaceScaling(&transform); }
	static void		StopScaling(void);									// ROM 0x0019730c StopScaling__9TQDScalerSFv
	static long		GetTransformLevel(void);							// ROM 0x001973c8 GetTransformLevel__9TQDScalerSFv
	static long		ForceScaling(long how);								// ROM 0x00196124 ForceScaling__9TQDScalerSFl - 2: none, 1: even while recording; 0: as it was.  ==> the old setting
	static RgnHandle	GetActualClip(void);								// ROM 0x001960a8 GetActualClip__9TQDScalerSFv - the clip really drawn through
	static RgnHandle	GetActualVis(void);									// ROM 0x001960e4 GetActualVis__9TQDScalerSFv
	static Boolean	ReplaceClip(RgnHandle base, RgnHandle clip, long level);			// ROM 0x00196244 ReplaceClip__9TQDScalerSFPP6RegionT1l
	static Boolean	LowLevelReplaceClip(RgnHandle base, RgnHandle clip, long level);	// ROM 0x00196148 LowLevelReplaceClip__9TQDScalerSFPP6RegionT1l


	void			Setup(void);											// ROM 0x00196f8c Setup__9TQDScalerFv
	void			Cleanup(void);											// ROM 0x0019736c Cleanup__9TQDScalerFv
	void			UseTransform(const TTransform* transform, Boolean replace);	// ROM 0x001972a8 UseTransform__9TQDScalerFP10TTransformUc
	void			SetupScalingPen(GrafVerb verb);						// ROM 0x001962b0 SetupScalingPen__9TQDScalerFUc
	void			RestoreScalingPen(void);								// ROM 0x00196338 RestoreScalingPen__9TQDScalerFv
	void			SetupScalingRegions(void);								// ROM 0x00196348 SetupScalingRegions__9TQDScalerFv
	void			RestoreScalingRegions(void);							// ROM 0x0019650c RestoreScalingRegions__9TQDScalerFv
	ULong			TestFeature(ULong feature);								// ROM 0x00196534 TestFeature__9TQDScalerFl

	TTransform		fTransform;			// +0x00  the transforms in force, together
	QDProcs			fProcs;				// +0x1c  what the port drew with (or the standard procs)
	QDProcs			fScaledProcs;		// +0x54  what it draws with now
	QDProcs*		fSavedProcs;		// +0x8c  the port's grafProcs, put back at the end
	GrafPort*		fPort;				// +0x90
	Point			fPnSize;			// +0x94  the pen size while a frame's is scaled
	RgnHandle		fClip;				// +0x98  the port's clip and visible regions while drawing
	RgnHandle		fVis;				// +0x9c
	RgnHandle		fLastClip;			// +0xa0  the clip fMappedClip was made from
	RgnHandle		fLastVis;			// +0xa4  and the visible region fMappedVis was
	RgnHandle		fMappedClip;		// +0xa8  the clip in the destination's coordinates
	RgnHandle		fMappedVis;			// +0xac
	RgnHandle		fOuterClip;			// +0xb0  the clip the port had when it first changed under the scaling
	RgnHandle		fOuterMappedClip;	// +0xb4  and what it was mapped to
	long			fOuterLevel;		// +0xb8  at which level
	Boolean			fMapVis;			// +0xbc  map the visible region too
	ULong			fFeatures;			// +0xc0  1: text a sixteenth smaller; 0x80000000: the regions are set up
	CDynamicArray*	fTransforms;		// +0xc4  the stack

	static TQDScaler*	gScale;			// ROM 0x0c1017a8
	static long			gForcedScaling;
};

void	RecalcTransform(TTransform* transform, CDynamicArray* transforms, long level);	// ROM 0x001970dc RecalcTransform__FP10TTransformP13CDynamicArrayl
extern "C" void	ScaledText(Long text, Fixed hScale, Fixed vScale);				// ROM 0x00196634 ScaledText
extern "C" void	ScaledLine(Point to);											// ROM 0x001967b0 ScaledLine
extern "C" void	ScaledRect(GrafVerb verb, Rect* r);								// ROM 0x00196864 ScaledRect
extern "C" void	ScaledRRect(GrafVerb verb, Rect* r, long ovalWidth, long ovalHeight);	// ROM 0x00196908 ScaledRRect
extern "C" void	ScaledOval(GrafVerb verb, Rect* r);								// ROM 0x00196a04 ScaledOval
extern "C" void	ScaledArc(GrafVerb verb, Rect* r, long startAngle, long arcAngle);	// ROM 0x00196aa8 ScaledArc
extern "C" void	ScaledPoly(GrafVerb verb, PolyHandle poly);						// ROM 0x00196b68 ScaledPoly
extern "C" void	ScaledRgn(GrafVerb verb, RgnHandle rgn);						// ROM 0x00196c34 ScaledRgn
extern "C" void	ScaledBits(PixelMap* src, Rect* srcRect, Rect* dstRect, long mode, RgnHandle mask);	// ROM 0x00196cf0 ScaledBits
extern "C" void	ScaledCurve(GrafVerb verb, curve* c);							// ROM 0x00196e00 ScaledCurve
extern "C" void	ScaledPaths(GrafVerb verb, pathsHandle p);						// ROM 0x00196eb0 ScaledPaths
Boolean	SkipScaling(void);												// ROM 0x001965d0 SkipScaling__Fv - a picture, region or polygon being recorded (unless forced)

#endif	/* __TRANSFORM_H */
