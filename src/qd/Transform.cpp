/*
	File:		qd/Transform.cpp

	Contains:	TTransform and the scaler's transform stack (Transform.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Transform.h"
#include "Ports.h"
#include "FixedMath.h"
#include "Regions.h"
#include "Polygons.h"
#include "Curves.h"
#include "Paths.h"
#include "PicPlay.h"
#include "TextObject.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "DynamicArray.h"

#include <string.h>


// ROM 0x001973e8 Setup__10TTransformFPC5TRectT1Uc
// The mapping from one rectangle onto another: the scale each way, and
// the two rectangles kept to map from and to.  With `square` the smaller
// of the two scales is used for both and the destination is cut back to
// what that leaves, so that nothing is stretched out of shape.
void
TTransform::Setup(const Rect* src, const Rect* dst, Boolean square)
{
	if (src == nil)
	{
		fFlags &= ~kTransformSetUp;
		return;
	}
	fSrc = *src;
	fDst = *dst;
	fScaleH = FixedDivide(ToFixed(fDst.right - fDst.left), ToFixed(fSrc.right - fSrc.left));
	fScaleV = FixedDivide(ToFixed(fDst.bottom - fDst.top), ToFixed(fSrc.bottom - fSrc.top));
	if (square)
	{
		if (fScaleH < fScaleV)
		{
			fScaleV = fScaleH;
			fDst.bottom = fDst.top + RoundFixed(ScaleFixed(fScaleH, fSrc.bottom - fSrc.top));
		}
		else
		{
			fScaleH = fScaleV;
			fDst.right = fDst.left + RoundFixed(ScaleFixed(fScaleV, fSrc.right - fSrc.left));
		}
	}
	fFlags |= kTransformSetUp;
	if (fScaleH == ToFixed(1) && fScaleV == ToFixed(1))
		fFlags |= kTransformNoScale;
	else
		fFlags &= ~kTransformNoScale;
}


// ROM 0x00197bcc Scale__6TPointFRC10TTransform
// A point moved from the source rectangle into the destination.
void
Scale(Point* pt, const TTransform& transform)
{
	if ((transform.fFlags & kTransformSetUp) == 0)
		return;
	pt->h = (short) (transform.fDst.left + RoundFixed(ScaleFixed(transform.fScaleH, pt->h - transform.fSrc.left)));
	pt->v = (short) (transform.fDst.top + RoundFixed(ScaleFixed(transform.fScaleV, pt->v - transform.fSrc.top)));
}


// ROM 0x001979bc Scale__5TRectFRC10TTransform
// A rectangle's two corners, each moved as a point is.
void
Scale(Rect* rect, const TTransform& transform)
{
	Scale((Point*) rect, transform);
	if ((transform.fFlags & kTransformSetUp) == 0)
		return;
	rect->right = (short) (transform.fDst.left + RoundFixed(ScaleFixed(transform.fScaleH, rect->right - transform.fSrc.left)));
	rect->bottom = (short) (transform.fDst.top + RoundFixed(ScaleFixed(transform.fScaleV, rect->bottom - transform.fSrc.top)));
}


/*------------------------------------------------------------------------------
	T h e   t r a n s f o r m s   i n   f o r c e
------------------------------------------------------------------------------*/

TQDScaler*	TQDScaler::gScale = nil;
long		TQDScaler::gForcedScaling = 0;


// ROM 0x00196018 __ct__9TQDScalerFv
// Nothing yet but the stack (the ROM clears its 200 bytes first).
TQDScaler::TQDScaler()
{
	memset(&fTransform, 0, sizeof(fTransform));
	memset(&fProcs, 0, sizeof(fProcs));
	memset(&fScaledProcs, 0, sizeof(fScaledProcs));
	fSavedProcs = nil;
	fPort = nil;
	fPnSize.h = 0;
	fPnSize.v = 0;
	fClip = nil;
	fVis = nil;
	fLastClip = nil;
	fLastVis = nil;
	fMappedClip = nil;
	fMappedVis = nil;
	fOuterClip = nil;
	fOuterMappedClip = nil;
	fOuterLevel = 0;
	fMapVis = false;
	fFeatures = 0;
	fTransforms = new CDynamicArray(sizeof(TTransform), 1);
}


// ROM 0x00196068 __dt__9TQDScalerFv
TQDScaler::~TQDScaler()
{
	if (fTransforms != nil)
		delete fTransforms;
	Cleanup();
}


// ROM 0x001960a8 GetActualClip__9TQDScalerSFv
// The clip drawing really goes through: the scaler's mapped one while it
// is in force, else the port's.
RgnHandle
TQDScaler::GetActualClip(void)
{
	if (gScale == nil)
	{
		GrafPort* port;
		GetPort(&port);
		return port->clipRgn;
	}
	return gScale->fMappedClip;
}


// ROM 0x001960e4 GetActualVis__9TQDScalerSFv
RgnHandle
TQDScaler::GetActualVis(void)
{
	RgnHandle vis = gScale != nil ? gScale->fMappedVis : nil;
	if (gScale == nil || vis == nil)
	{
		GrafPort* port;
		GetPort(&port);
		return port->visRgn;
	}
	return vis;
}


// ROM 0x00196124 ForceScaling__9TQDScalerSFl
// 2 turns the scaling off, 1 keeps it on even while something is being
// recorded; nought goes back to how it was (the low two bits cleared).
// ==> the two bits it had.
long
TQDScaler::ForceScaling(long how)
{
	long had = gForcedScaling & 3;
	if (how == 0)
		how = gForcedScaling & ~3;
	gForcedScaling = how;
	return had;
}


// ROM 0x00196148 LowLevelReplaceClip__9TQDScalerSFPP6RegionT1l
// The clip drawn through made again: `clip` - in the coordinates of the
// transforms up to `level`, so mapped through them unless it is wide open
// - cut by `base`, or `base` alone when there is no clip.  The port's clip
// is then remembered as the one the mapped clip was made from.  ==> false
// when there is no scaler (or no memory).
Boolean
TQDScaler::LowLevelReplaceClip(RgnHandle base, RgnHandle clip, long level)
{
	RgnHandle actual = GetActualClip();
	if (clip != nil)
	{
		if (gScale != nil && !IsWideOpenRgn(clip))
		{
			CopyRgn(clip, actual);
			TTransform transform;
			if (GetTransformLevel() == level)
				transform = gScale->fTransform;
			else
				RecalcTransform(&transform, gScale->fTransforms, level);
			MapRgn(actual, &transform.fSrc, &transform.fDst);
			clip = actual;
		}
		if (clip != nil)
			SectRgn(base, clip, actual);
		else
			CopyRgn(base, actual);
	}
	else
		CopyRgn(base, actual);
	if (gScale == nil)
		return false;
	RgnHandle portClip = gScale->fPort->clipRgn;
	if (portClip != gScale->fLastClip)
		CopyRgn(portClip, gScale->fLastClip);
	return true;
}


// ROM 0x00196244 ReplaceClip__9TQDScalerSFPP6RegionT1l
// LowLevelReplaceClip, the clip saved from outside the scaling forgotten
// first.
Boolean
TQDScaler::ReplaceClip(RgnHandle base, RgnHandle clip, long level)
{
	if (gScale != nil && gScale->fOuterClip != nil)
	{
		DisposeRgn(gScale->fOuterClip);
		gScale->fOuterClip = nil;
		DisposeRgn(gScale->fOuterMappedClip);
		gScale->fOuterMappedClip = nil;
	}
	return LowLevelReplaceClip(base, clip, level);
}


// ROM 0x001962b0 SetupScalingPen__9TQDScalerFUc
// The pen size saved and, to frame, scaled (never below a pixel).
//
// ROM BUG, kept: the height is the pen's *width* times the vertical scale,
// so a pen taller or shorter than it is wide comes out square.
void
TQDScaler::SetupScalingPen(GrafVerb verb)
{
	fPnSize = fPort->pnSize;
	if (verb != frame)
		return;
	long width = fPnSize.h;
	short h = (short) RoundFixed((Fixed) ((ULong32) fTransform.fScaleH * (ULong32) width));
	if (h < 1)
		h = 1;
	fPort->pnSize.h = h;
	short v = (short) RoundFixed((Fixed) ((ULong32) width * (ULong32) fTransform.fScaleV));
	if (v < 1)
		v = 1;
	fPort->pnSize.v = v;
}


// ROM 0x00196338 RestoreScalingPen__9TQDScalerFv
void
TQDScaler::RestoreScalingPen(void)
{
	fPort->pnSize = fPnSize;
}


// ROM 0x00196348 SetupScalingRegions__9TQDScalerFv
// The port given the regions to draw through.  Its clip was set in the
// drawing's own coordinates: when it has changed since it was last mapped,
// it is mapped again and cut by the clip the port had outside - the first
// time, the mapped clip so far is kept as that outside clip (and the port's
// clip then as the clip that goes with it); when the clip goes back to
// that outside one, the outside pair is taken up again.  With mapVis the
// visible region is mapped through the first transform too.
void
TQDScaler::SetupScalingRegions(void)
{
	fFeatures |= 0x80000000;
	fClip = fPort->clipRgn;
	if (!EqualRgn(fClip, fLastClip) && (!IsWideOpenRgn(fClip) || !IsWideOpenRgn(fLastClip)))
	{
		RgnHandle base = fMappedClip;
		RgnHandle clip = fClip;
		long level = GetTransformLevel();
		if (fOuterClip == nil)
		{
			fOuterLevel = level;
			fOuterClip = fLastClip;
			fLastClip = NewRgn();
			fOuterMappedClip = fMappedClip;
			fMappedClip = NewRgn();
		}
		else
		{
			level = fOuterLevel;
			base = fOuterMappedClip;
			if (EqualRgn(fClip, fOuterClip) || (IsWideOpenRgn(fClip) && IsWideOpenRgn(fOuterClip)))
			{
				DisposeRgn(fLastClip);
				fLastClip = fOuterClip;
				fOuterClip = nil;
				DisposeRgn(fMappedClip);
				fMappedClip = fOuterMappedClip;
				fOuterMappedClip = nil;
				clip = nil;
			}
		}
		LowLevelReplaceClip(base, clip, level);
	}
	fPort->clipRgn = fMappedClip;
	fVis = fPort->visRgn;
	if (!fMapVis)
		return;
	if (!EqualRgn(fVis, fLastVis))
	{
		CopyRgn(fVis, fLastVis);
		if (fMappedVis != nil)
			CopyRgn(fVis, fMappedVis);
		if (!IsWideOpenRgn(fVis))
		{
			if (fMappedVis == nil)
			{
				fMappedVis = NewRgn();
				CopyRgn(fVis, fMappedVis);
			}
			const TTransform* first = (const TTransform*) fTransforms->SafeElementPtrAt(0);
			MapRgn(fMappedVis, &first->fSrc, &first->fDst);
		}
	}
	if (fMappedVis != nil)
		fPort->visRgn = fMappedVis;
}


// ROM 0x0019650c RestoreScalingRegions__9TQDScalerFv
void
TQDScaler::RestoreScalingRegions(void)
{
	fFeatures &= 0x7fffffff;
	fPort->clipRgn = fClip;
	fPort->visRgn = fVis;
}


// ROM 0x00196534 TestFeature__9TQDScalerFl
ULong
TQDScaler::TestFeature(ULong feature)
{
	return fFeatures & feature;
}


// ROM 0x00196540 StartScaling__9TQDScalerSFP10TTransformUcl
// A transform pushed; the first makes the scaler, which takes the current
// port's procs over.  mapVis and the features are the first one's.
void
TQDScaler::StartScaling(const TTransform* transform, Boolean mapVis, long features)
{
	if (gScale == nil)
	{
		gScale = new TQDScaler;
		if (gScale == nil || gScale->fTransforms == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		gScale->Setup();
		gScale->fMapVis = mapVis;
		gScale->fFeatures = (ULong) features;
	}
	gScale->UseTransform(transform, false);
}


// ROM 0x00196de8 ReplaceScaling__9TQDScalerSFP10TTransform
// The innermost transform changed for another - a style frame set again
// at the same level.
void
TQDScaler::ReplaceScaling(const TTransform* transform)
{
	gScale->UseTransform(transform, true);
}


// ROM 0x0019730c StopScaling__9TQDScalerSFv
// The innermost transform popped; the last one takes the scaler away and
// gives the port its procs back.
void
TQDScaler::StopScaling(void)
{
	long last = (long) gScale->fTransforms->GetArraySize() - 1;
	if (last < 1)
	{
		if (gScale != nil)
			delete gScale;
		gScale = nil;
		return;
	}
	gScale->fTransforms->RemoveElementsAt(last, 1);
	gScale->UseTransform(nil, false);
}


// ROM 0x001973c8 GetTransformLevel__9TQDScalerSFv
long
TQDScaler::GetTransformLevel(void)
{
	return gScale == nil ? 0 : (long) gScale->fTransforms->GetArraySize();
}


// (host) See Transform.h.
Point
TQDScaler::Offset(void)
{
	Point none;
	none.h = 0;
	none.v = 0;
	return none;
}


// ROM 0x001972a8 UseTransform__9TQDScalerFP10TTransformUc
// A transform pushed, or put in place of the innermost; the transform in
// force worked out again.
void
TQDScaler::UseTransform(const TTransform* transform, Boolean replace)
{
	if (transform != nil)
	{
		CDynamicArray* transforms = fTransforms;
		if (!replace)
			transforms->InsertElementsBefore(transforms->GetArraySize(), (void*) transform, 1);
		else
			transforms->ReplaceElementsAt(transforms->GetArraySize() - 1, (void*) transform, 1);
	}
	RecalcTransform(&fTransform, fTransforms, GetTransformLevel());
}


// ROM 0x001970dc RecalcTransform__FP10TTransformP13CDynamicArrayl
// The first `level` transforms as one: from the first's source rectangle
// to where they take its corner - the first's offset, then each one's
// offset scaled by the scales before it - at the product of their scales.
void
RecalcTransform(TTransform* transform, CDynamicArray* transforms, long level)
{
	Fixed scaleH = 0x10000;
	Fixed scaleV = 0x10000;
	long top = 0;
	long left = 0;
	Boolean first = true;
	Rect src;
	SetRect(&src, 0, 0, 0, 0);
	ArrayIndex count = transforms->GetArraySize();
	for (ArrayIndex i = 0; i < count && level-- > 0; i++)
	{
		const TTransform* t = (const TTransform*) transforms->SafeElementPtrAt(i);
		long dv = t->fDst.top - t->fSrc.top;
		long dh = t->fDst.left - t->fSrc.left;
		if (first)
		{
			src = t->fSrc;
			top = src.top;
			left = src.left;
			first = false;
			scaleV = t->fScaleV;
			scaleH = t->fScaleH;
		}
		else
		{
			dh = (short) RoundFixed((Fixed) ((ULong32) dh * (ULong32) scaleH));
			dv = (short) RoundFixed((Fixed) ((ULong32) dv * (ULong32) scaleV));
			scaleH = FixedMultiply(scaleH, t->fScaleH);
			scaleV = FixedMultiply(scaleV, t->fScaleV);
		}
		left += dh;
		top += dv;
	}
	Rect dst;
	dst.top = (short) top;
	dst.left = (short) left;
	dst.bottom = (short) (top + RoundFixed(FixedMultiply(scaleV, (Fixed) ((ULong32) (src.bottom - src.top) << 16))));
	dst.right = (short) (left + RoundFixed(FixedMultiply(scaleH, (Fixed) ((ULong32) (src.right - src.left) << 16))));
	transform->Setup(&src, &dst, false);
}


// ROM 0x00196f8c Setup__9TQDScalerFv
// The current port's procs (the standard ones if it has none) kept, and a
// copy of them with the scaler's in place of the ones that take
// coordinates installed instead; the clip regions to map it into made.
void
TQDScaler::Setup(void)
{
	GrafPort* port;
	GetPort(&port);
	fPort = port;
	fSavedProcs = port->grafProcs;
	if (fSavedProcs == nil)
		SetStdProcs(&fProcs);
	else
		fProcs = *fSavedProcs;
	fScaledProcs = fProcs;
	fScaledProcs.textProc = ScaledText;
	fScaledProcs.lineProc = ScaledLine;
	fScaledProcs.rectProc = ScaledRect;
	fScaledProcs.rRectProc = ScaledRRect;
	fScaledProcs.ovalProc = ScaledOval;
	fScaledProcs.arcProc = ScaledArc;
	fScaledProcs.polyProc = ScaledPoly;
	fScaledProcs.rgnProc = ScaledRgn;
	fScaledProcs.bitsProc = ScaledBits;
	fScaledProcs.curveProc = ScaledCurve;
	fScaledProcs.pathsProc = ScaledPaths;
	fPort->grafProcs = &fScaledProcs;
	fLastClip = NewRgn();
	GetClip(fLastClip);
	fLastVis = NewRgn();
	fMappedClip = NewRgn();
	GetClip(fMappedClip);
	fMappedVis = nil;
	fOuterClip = nil;
	fOuterMappedClip = nil;
}


// ROM 0x0019736c Cleanup__9TQDScalerFv
// The regions given back and the port its own procs.
void
TQDScaler::Cleanup(void)
{
	DisposeRgn(fLastClip);
	DisposeRgn(fLastVis);
	DisposeRgn(fMappedClip);
	if (fMappedVis != nil)
		DisposeRgn(fMappedVis);
	if (fOuterClip != nil)
		DisposeRgn(fOuterClip);
	if (fOuterMappedClip != nil)
		DisposeRgn(fOuterMappedClip);
	fPort->grafProcs = fSavedProcs;
}


// ROM 0x001965d0 SkipScaling__Fv
// Whether to draw as given: scaling forced off, or - unless forced on - a
// picture, region or polygon being recorded.
Boolean
SkipScaling(void)
{
	if (TQDScaler::gForcedScaling & 2)
		return true;
	if ((TQDScaler::gForcedScaling & 1) == 0)
	{
		GrafPort* port;
		GetPort(&port);
		if (port->picSave != nil || port->rgnSave != nil || port->polySave != nil)
			return true;
	}
	return false;
}


/*------------------------------------------------------------------------------
	T h e   s c a l e r ' s   p r o c s

	Each hands what it is given, mapped, to the proc the port had; while
	scaling is skipped, as it is.
------------------------------------------------------------------------------*/

// (host) whether the transform in force moves without scaling
static inline Boolean
OnlyMoves(void)
{
	return (TQDScaler::gScale->fTransform.fFlags & kTransformNoScale) != 0;
}


// ROM 0x00196634 ScaledText
// Text at the scales times the transform's (a sixteenth less with feature
// 1), its location mapped and - when the transform scales - its options'
// width scaled with it for the call.
extern "C" void
ScaledText(Long text, Fixed hScale, Fixed vScale)
{
	TextObjProc proc = TQDScaler::gScale->fProcs.textProc;
	if (SkipScaling())
	{
		proc(text, hScale, vScale);
		return;
	}
	TQDScaler::gScale->SetupScalingRegions();
	TQDScaler* scaler = TQDScaler::gScale;
	hScale = FixedMultiply(hScale, scaler->fTransform.fScaleH);
	vScale = FixedMultiply(vScale, scaler->fTransform.fScaleV);
	if (TQDScaler::gScale->TestFeature(1))
	{
		hScale -= hScale >> 4;
		vScale -= vScale >> 4;
	}
	Fixed width = 0;
	TextOptions* options = nil;
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
	{
		GetTextObjField(text, kTextObjOptions, &options);
		if (options != nil)
		{
			width = options->fWidth;
			options->fWidth = FixedMultiply(scaler->fTransform.fScaleH, width);
			SetTextObjField(text, kTextObjOptions, options);
		}
	}
	FPoint location, mapped;
	GetTextObjField(text, kTextObjLocation, &location);
	mapped = location;
	MapFPoint(&mapped, &scaler->fTransform.fSrc, &scaler->fTransform.fDst);
	SetTextObjField(text, kTextObjLocation, &mapped);
	proc(text, hScale, vScale);
	if (options != nil)
	{
		options->fWidth = width;
		SetTextObjField(text, kTextObjOptions, options);
	}
	SetTextObjField(text, kTextObjLocation, &location);
	TQDScaler::gScale->RestoreScalingRegions();
}


// ROM 0x001967b0 ScaledLine
// A line from the pen, mapped, the pen's location mapped with it and put
// at the unmapped end afterwards; the pen scaled as for a frame.
extern "C" void
ScaledLine(Point to)
{
	LineProcPtr proc = TQDScaler::gScale->fProcs.lineProc;
	if (SkipScaling())
	{
		proc(to);
		return;
	}
	GrafPort* port;
	GetPort(&port);
	TQDScaler::gScale->SetupScalingRegions();
	TQDScaler* scaler = TQDScaler::gScale;
	if (!OnlyMoves())
		TQDScaler::gScale->SetupScalingPen(frame);
	Point end = to;
	Scale(&port->pnLoc, scaler->fTransform);
	Scale(&to, scaler->fTransform);
	proc(to);
	port->pnLoc = end;
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
		TQDScaler::gScale->RestoreScalingPen();
	TQDScaler::gScale->RestoreScalingRegions();
}


// ROM 0x00196864 ScaledRect
extern "C" void
ScaledRect(GrafVerb verb, Rect* r)
{
	RectProcPtr proc = TQDScaler::gScale->fProcs.rectProc;
	if (SkipScaling())
	{
		proc(verb, r);
		return;
	}
	TQDScaler::gScale->SetupScalingRegions();
	TQDScaler* scaler = TQDScaler::gScale;
	if (!OnlyMoves())
		TQDScaler::gScale->SetupScalingPen(verb);
	Rect mapped = *r;
	Scale(&mapped, scaler->fTransform);
	proc(verb, &mapped);
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
		TQDScaler::gScale->RestoreScalingPen();
	TQDScaler::gScale->RestoreScalingRegions();
}


// ROM 0x00196908 ScaledRRect
// ... and the corners scaled too when the transform scales.
extern "C" void
ScaledRRect(GrafVerb verb, Rect* r, long ovalWidth, long ovalHeight)
{
	RRectProcPtr proc = TQDScaler::gScale->fProcs.rRectProc;
	if (SkipScaling())
	{
		proc(verb, r, ovalWidth, ovalHeight);
		return;
	}
	TQDScaler::gScale->SetupScalingRegions();
	TQDScaler* scaler = TQDScaler::gScale;
	if (!OnlyMoves())
		TQDScaler::gScale->SetupScalingPen(verb);
	Rect mapped = *r;
	Scale(&mapped, scaler->fTransform);
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
	{
		ovalWidth = (short) RoundFixed((Fixed) ((ULong32) scaler->fTransform.fScaleH * (ULong32) ovalWidth));
		ovalHeight = (short) RoundFixed((Fixed) ((ULong32) ovalHeight * (ULong32) scaler->fTransform.fScaleV));
	}
	proc(verb, &mapped, ovalWidth, ovalHeight);
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
		TQDScaler::gScale->RestoreScalingPen();
	TQDScaler::gScale->RestoreScalingRegions();
}


// ROM 0x00196a04 ScaledOval
extern "C" void
ScaledOval(GrafVerb verb, Rect* r)
{
	OvalProcPtr proc = TQDScaler::gScale->fProcs.ovalProc;
	if (SkipScaling())
	{
		proc(verb, r);
		return;
	}
	TQDScaler::gScale->SetupScalingRegions();
	TQDScaler* scaler = TQDScaler::gScale;
	if (!OnlyMoves())
		TQDScaler::gScale->SetupScalingPen(verb);
	Rect mapped = *r;
	Scale(&mapped, scaler->fTransform);
	proc(verb, &mapped);
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
		TQDScaler::gScale->RestoreScalingPen();
	TQDScaler::gScale->RestoreScalingRegions();
}


// ROM 0x00196aa8 ScaledArc
extern "C" void
ScaledArc(GrafVerb verb, Rect* r, long startAngle, long arcAngle)
{
	ArcProcPtr proc = TQDScaler::gScale->fProcs.arcProc;
	if (SkipScaling())
	{
		proc(verb, r, startAngle, arcAngle);
		return;
	}
	TQDScaler::gScale->SetupScalingRegions();
	TQDScaler* scaler = TQDScaler::gScale;
	if (!OnlyMoves())
		TQDScaler::gScale->SetupScalingPen(verb);
	Rect mapped = *r;
	Scale(&mapped, scaler->fTransform);
	proc(verb, &mapped, startAngle, arcAngle);
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
		TQDScaler::gScale->RestoreScalingPen();
	TQDScaler::gScale->RestoreScalingRegions();
}


// ROM 0x00196b68 ScaledPoly
// A copy of the polygon mapped.
extern "C" void
ScaledPoly(GrafVerb verb, PolyHandle poly)
{
	PolyProcPtr proc = TQDScaler::gScale->fProcs.polyProc;
	if (SkipScaling())
	{
		proc(verb, poly);
		return;
	}
	TQDScaler::gScale->SetupScalingRegions();
	TQDScaler* scaler = TQDScaler::gScale;
	if (!OnlyMoves())
		TQDScaler::gScale->SetupScalingPen(verb);
	long size = GetHandleSize((Handle) poly);
	PolyHandle mapped = (PolyHandle) NewHandle(size);
	if (mapped != nil)
	{
		BlockMove(*poly, *mapped, size);
		MapPoly(mapped, &scaler->fTransform.fSrc, &scaler->fTransform.fDst);
		proc(verb, mapped);
		DisposHandle((Handle) mapped);
	}
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
		TQDScaler::gScale->RestoreScalingPen();
	TQDScaler::gScale->RestoreScalingRegions();
}


// ROM 0x00196c34 ScaledRgn
// A copy of the region mapped.
extern "C" void
ScaledRgn(GrafVerb verb, RgnHandle rgn)
{
	RgnProcPtr proc = TQDScaler::gScale->fProcs.rgnProc;
	if (SkipScaling())
	{
		proc(verb, rgn);
		return;
	}
	TQDScaler::gScale->SetupScalingRegions();
	TQDScaler* scaler = TQDScaler::gScale;
	if (!OnlyMoves())
		TQDScaler::gScale->SetupScalingPen(verb);
	RgnHandle mapped = NewRgn();
	if (mapped != nil)
	{
		CopyRgn(rgn, mapped);
		MapRgn(mapped, &scaler->fTransform.fSrc, &scaler->fTransform.fDst);
		proc(verb, mapped);
		DisposeRgn(mapped);
	}
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
		TQDScaler::gScale->RestoreScalingPen();
	TQDScaler::gScale->RestoreScalingRegions();
}


// ROM 0x00196cf0 ScaledBits
// The destination rectangle mapped, and a copy of the mask region (a
// wide-open mask, nil among them, goes on as nil).
extern "C" void
ScaledBits(PixelMap* src, Rect* srcRect, Rect* dstRect, long mode, RgnHandle mask)
{
	BitsProcPtr proc = TQDScaler::gScale->fProcs.bitsProc;
	if (SkipScaling())
	{
		proc(src, srcRect, dstRect, mode, mask);
		return;
	}
	TQDScaler::gScale->SetupScalingRegions();
	RgnHandle mapped = nil;
	if (!IsWideOpenRgn(mask))
	{
		mapped = NewRgn();
		CopyRgn(mask, mapped);
		MapRgn(mapped, &TQDScaler::gScale->fTransform.fSrc, &TQDScaler::gScale->fTransform.fDst);
	}
	Rect dst = *dstRect;
	Scale(&dst, TQDScaler::gScale->fTransform);
	proc(src, srcRect, &dst, mode, mapped);
	TQDScaler::gScale->RestoreScalingRegions();
	if (mapped != nil)
		DisposeRgn(mapped);
}


// ROM 0x00196e00 ScaledCurve
extern "C" void
ScaledCurve(GrafVerb verb, curve* c)
{
	CurveProcPtr proc = TQDScaler::gScale->fProcs.curveProc;
	if (SkipScaling())
	{
		proc(verb, c);
		return;
	}
	TQDScaler::gScale->SetupScalingRegions();
	TQDScaler* scaler = TQDScaler::gScale;
	if (!OnlyMoves())
		TQDScaler::gScale->SetupScalingPen(verb);
	curve mapped = *c;
	MapCurve(&mapped, &scaler->fTransform.fSrc, &scaler->fTransform.fDst);
	proc(verb, &mapped);
	if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
		TQDScaler::gScale->RestoreScalingPen();
	TQDScaler::gScale->RestoreScalingRegions();
}


// ROM 0x00196eb0 ScaledPaths
// A copy of the paths mapped.  The regions and pen are set up only when
// they are not already (a path drawn from inside another scaled call).
extern "C" void
ScaledPaths(GrafVerb verb, pathsHandle p)
{
	PathsProcPtr proc = TQDScaler::gScale->fProcs.pathsProc;
	if (SkipScaling())
	{
		proc(verb, p);
		return;
	}
	TQDScaler* scaler = TQDScaler::gScale;
	ULong already = TQDScaler::gScale->TestFeature(0x80000000);
	if (already == 0)
	{
		TQDScaler::gScale->SetupScalingRegions();
		if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
			TQDScaler::gScale->SetupScalingPen(verb);
	}
	pathsHandle mapped = (pathsHandle) NewHandle(0x14);
	if (mapped != nil)
	{
		CopyPaths(p, mapped);
		MapPaths(mapped, &scaler->fTransform.fSrc, &scaler->fTransform.fDst);
		proc(verb, mapped);
		DisposHandle((Handle) mapped);
	}
	if (already == 0)
	{
		if ((scaler->fTransform.fFlags & kTransformNoScale) == 0)
			TQDScaler::gScale->RestoreScalingPen();
		TQDScaler::gScale->RestoreScalingRegions();
	}
}
