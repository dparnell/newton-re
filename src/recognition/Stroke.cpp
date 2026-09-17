/*
	File:		recognition/Stroke.cpp

	Contains:	TStroke, TStrokePublic, the sample points and the Fixed
				rectangle helpers.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Stroke.h"
#include "Rects.h"
#include "Screen.h"
#include "RootView.h"
#include "Locale.h"
#include "FixedMath.h"
#include <string.h>

// the pen tip and inking defaults the strokes are made with
static ULong	gLastPenTip = 0;			// ROM 0x0c1008bc gLastPenTip
static Boolean	gDefaultInk = true;			// ROM 0x0c10197c gDefaultInk


/*------------------------------------------------------------------------------
	S a m p l e   p o i n t s
------------------------------------------------------------------------------*/

// ROM 0x0021e638 SampleX__FP8SamplePt
// The x in Fixed: 14 bits of eighths of a pixel.
Fixed
SampleX(SamplePt* pt)
{
	return (Fixed) (pt->fX & 0x3fff) << 13;
}


// ROM 0x0021f8e4 SampleY__FP8SamplePt
Fixed
SampleY(SamplePt* pt)
{
	return (Fixed) (pt->fY & 0x3fff) << 13;
}


// ROM 0x0021fb7c SampleP__FP8SamplePt
// The pressure: two bits above x, one above y.
ULong
SampleP(SamplePt* pt)
{
	return ((pt->fX & 0xc000) >> 13) | ((pt->fY & 0x4000) >> 14);
}


// ROM 0x00220644 SetSampleX__FP8SamplePtl
// The x set (no less than 0), the pressure bits kept.
void
SetSampleX(SamplePt* pt, Fixed x)
{
	if (x < 1)
		x = 0;
	pt->fX = (UShort) (((x >> 13) & 0x3fff) | (pt->fX & 0xc000));
}


// ROM 0x002207cc SetSampleY__FP8SamplePtl
void
SetSampleY(SamplePt* pt, Fixed y)
{
	if (y < 1)
		y = 0;
	pt->fY = (UShort) (((y >> 13) & 0x3fff) | (pt->fY & 0xc000));
}


// ROM 0x0021f8f8 GetPoint__FP8SamplePtP6FPoint
void
GetPoint(SamplePt* pt, FPoint* fpt)
{
	fpt->x = SampleX(pt);
	fpt->y = SampleY(pt);
}


// ROM 0x0021f924 SetPoint__FP8SamplePtP6FPoint
void
SetPoint(SamplePt* pt, FPoint* fpt)
{
	SetSampleX(pt, fpt->x);
	SetSampleY(pt, fpt->y);
}


// ROM 0x0021f990 TestFlag__FP8SamplePtUl
// The flag bit (bit 15 of y's word, as bit 1 of the flags).
ULong
TestFlag(SamplePt* pt, ULong flag)
{
	return flag & ((pt->fY & 0x8000) >> 14);
}


// ROM 0x0021f9a8 SetFlag__FP8SamplePtUl
void
SetFlag(SamplePt* pt, ULong flag)
{
	ULong flags = ((pt->fY & 0x8000) >> 14) | (flag & 2);
	pt->fY = (UShort) ((pt->fY & 0x7fff) | (flags << 14));
}


// ROM 0x0021f9d8 UnsetFlag__FP8SamplePtUl
void
UnsetFlag(SamplePt* pt, ULong flag)
{
	ULong flags = ((pt->fY & 0x8000) >> 14) & ~flag;
	pt->fY = (UShort) ((pt->fY & 0x7fff) | (flags << 14));
}


/*------------------------------------------------------------------------------
	F i x e d   r e c t a n g l e s
------------------------------------------------------------------------------*/

// ROM 0x001a66f4 AddPtToRect
void
AddPtToRect(const FPoint* pt, FRect* rect, Boolean first)
{
	if (first)
	{
		rect->left = rect->right = pt->x;
		rect->top = rect->bottom = pt->y;
		return;
	}
	if (pt->y < rect->top)
		rect->top = pt->y;
	else if (pt->y > rect->bottom)
		rect->bottom = pt->y;
	if (pt->x < rect->left)
		rect->left = pt->x;
	else if (pt->x > rect->right)
		rect->right = pt->x;
}


// ROM 0x001a6934 SetRectangleEmpty
void
SetRectangleEmpty(FRect* rect)
{
	rect->left = rect->top = rect->right = rect->bottom = 0;
}


// ROM 0x001a694c SetRectanglePoint
void
SetRectanglePoint(FRect* rect, const FPoint* pt)
{
	rect->left = rect->right = pt->x;
	rect->top = rect->bottom = pt->y;
}


// ROM 0x001a66a8 RectangleCenter
void
RectangleCenter(const FRect* rect, FPoint* center)
{
	center->x = (rect->left + rect->right) >> 1;
	center->y = (rect->top + rect->bottom) >> 1;
}


// ROM 0x001a64c4 UnfixRect
// The Fixed rectangle rounded to pixels.
void
UnfixRect(const FRect* src, Rect* dst)
{
	dst->top = (short) ((src->top + 0x8000) >> 16);
	dst->left = (short) ((src->left + 0x8000) >> 16);
	dst->bottom = (short) ((src->bottom + 0x8000) >> 16);
	dst->right = (short) ((src->right + 0x8000) >> 16);
}


// ROM 0x001a67a4 GetMapper
// The dst rect narrowed (or shortened) about its centre to the src
// rect's proportions, so that a mapping between them keeps shapes.
void
GetMapper(const FRect* src, const FRect* dst)
{
	FRect* d = (FRect*) dst;
	Fixed srcRatio = FixedDivide(src->bottom - src->top, src->right - src->left);
	Fixed dstHeight = d->bottom - d->top;
	Fixed dstWidth = d->right - d->left;
	Fixed dstRatio = FixedDivide(dstHeight, dstWidth);
	if (srcRatio < dstRatio)
	{
		Fixed height = FixedMultiply(dstWidth, srcRatio);
		Fixed inset = (dstHeight - height) >> 1;
		d->top += inset;
		d->bottom -= inset;
	}
	else
	{
		Fixed width = FixedDivide(dstHeight, srcRatio);
		Fixed inset = (dstWidth - width) >> 1;
		d->left += inset;
		d->right -= inset;
	}
}


// ROM 0x001a6864 MapPoint
// The point moved from where it lies in src to the same place in dst.
void
MapPoint(FPoint* pt, const FRect* src, const FRect* dst)
{
	Fixed srcHeight = src->bottom - src->top;
	Fixed dstHeight = dst->bottom - dst->top;
	Fixed dy = pt->y - src->top;
	if (srcHeight != dstHeight)
		dy = FixedMultiply(dstHeight, FixedDivide(dy, srcHeight));
	pt->y = dst->top + dy;
	Fixed srcWidth = src->right - src->left;
	Fixed dstWidth = dst->right - dst->left;
	Fixed dx = pt->x - src->left;
	if (srcWidth != dstWidth)
		dx = FixedMultiply(dstWidth, FixedDivide(dx, srcWidth));
	pt->x = dst->left + dx;
}


/*------------------------------------------------------------------------------
	T S t r o k e
------------------------------------------------------------------------------*/

// ROM 0x0021fa0c Make__7TStrokeSFUl
// A stroke with room for count points; nil for no memory.
TStroke*
TStroke::Make(ULong count)
{
	TStroke* stroke = new TStroke;
	if (stroke != nil)
	{
		stroke->fData = nil;
		if (stroke->IStroke(count) != 0)
		{
			stroke->Dispose();
			stroke = nil;
		}
	}
	return stroke;
}


// ROM 0x0021fa7c IStroke__7TStrokeFUl
// The array of SamplePts made; the times, the box and the decimation (1:
// every point kept) cleared; the pen tip in the flags (bits 8-15), and
// no ink when that is the default.
long
TStroke::IStroke(ULong count)
{
	long err = IArray(sizeof(SamplePt), count);
	if (err == 0)
	{
		fUpTime = 0;
		fDownTime = 0;
		fUnused40 = 0;
		fUnused3c = 0;
		fDecimation = 1;
		fDecimationCount = 0;
		SetRectangleEmpty(&fBBox);
		fFlags = 0;
		SetFlags(gLastPenTip << 8);
		if (!gDefaultInk)
			SetFlags(kStrokeNoInk);
		fSampleRate = 0;
		fUnused48 = 0;
	}
	return err;
}


// ROM 0x0021fb28 IDispose__7TStrokeFv
// The points freed (the ROM: in the stroker's heap when the stroke was
// made there).
void
TStroke::IDispose(void)
{
	TArray::IDispose();
}


// ROM 0x0021fb78 SizeInBytes__7TStrokeFv
long
TStroke::SizeInBytes(void)
{
	return TArray::SizeInBytes() + (sizeof(TStroke) - sizeof(TDArray));
}


// ROM 0x0021fba8 Bifurcate__7TStrokeFv
// The decimation doubled (when it does not overflow) and every other
// point dropped; the sample rate halved.
void
TStroke::Bifurcate(void)
{
	fDecimation = (UShort) (fDecimation << 1);
	if (fDecimation == 0)
		return;
	SamplePt* dst = GetPoint(0);
	SamplePt* src = dst;
	long kept = 0;
	for (ULong i = 0; i < (ULong) fCount; i += 2)
	{
		*dst++ = *src;
		src += 2;
		kept++;
	}
	CutToIndex(kept);
	fSampleRate >>= 1;
}


// ROM 0x0021fc3c TryToAddPoint__7TStrokeFv
// A point added; when there is no memory the stroke is thinned
// (Bifurcate) and the point tried again.
SamplePt*
TStroke::TryToAddPoint(void)
{
	SamplePt* pt = (SamplePt*) AddEntry();
	if (pt != nil)
		return pt;
	Bifurcate();
	return (SamplePt*) AddEntry();
}


// ROM 0x0021fc78 AddPoint__7TStrokeFP5TabPt
// A tablet point added: only every fDecimation'th (a stroke over 800
// points is thinned first); the x and y clamped at 0, the pressure
// clamped at 7, the flag's bit 1 kept; the box grown to it (set to it
// for the first point, whose right and bottom are then a unit past it).
// ==> 0, or 1 when there was no memory for it.
long
TStroke::AddPoint(TabPt* pt)
{
	if (fDecimation == 0)
		return 0;
	fDecimationCount++;
	if (fDecimationCount < fDecimation)
		return 0;
	fDecimationCount = 0;
	if (fCount > 800)
		Bifurcate();
	Boolean first = fCount == 0;
	SamplePt* sample = TryToAddPoint();
	if (sample == nil)
		return 1;
	sample->fX = 0;
	sample->fY = 0;
	SetSampleX(sample, pt->x);
	SetSampleY(sample, pt->y);
	ULong pressure = pt->z;
	if (pressure > 6)
		pressure = 7;
	sample->fX = (UShort) ((sample->fX & 0x3fff) | ((pressure & 6) << 13));
	sample->fY = (UShort) ((sample->fY & 0x3fff) | ((pressure & 1) << 14));
	sample->fY = (UShort) ((sample->fY & 0x7fff) | ((pt->p & 2) << 14));
	FPoint fpt;
	fpt.x = pt->x;
	fpt.y = pt->y;
	AddPtToRect(&fpt, &fBBox, first);
	if (first)
	{
		fBBox.right++;
		fBBox.bottom++;
	}
	return 0;
}


// ROM 0x0021ff34 EndStroke__7TStrokeFv
// The stroke done: compacted, its box a unit past its points.
void
TStroke::EndStroke(void)
{
	SetFlags(kStrokeDone);
	Compact();
	fBBox.right++;
	fBBox.bottom++;
}


// ROM 0x0021ff78 GetPoint__7TStrokeFl
SamplePt*
TStroke::GetPoint(long index)
{
	return (SamplePt*) GetEntry(index);
}


// ROM 0x0021ff80 GetTabPt__7TStrokeFlP5TabPt
void
TStroke::GetTabPt(long index, TabPt* pt)
{
	SamplePt* sample = GetPoint(index);
	pt->x = SampleX(sample);
	pt->y = SampleY(sample);
	pt->z = (UShort) SampleP(sample);
	pt->p = (UShort) ((sample->fY & 0x8000) >> 14);
}


// ROM 0x00220010 GetFPoint__7TStrokeFlP6FPoint
void
TStroke::GetFPoint(long index, FPoint* pt)
{
	SamplePt* sample = GetPoint(index);
	pt->x = SampleX(sample);
	pt->y = SampleY(sample);
}


// ROM 0x00220058 Rotate__7TStrokeFl
// NOT YET RECONSTRUCTED: the points turned about the box's centre (the
// matrix utilities SetIdentityMatrix/RotateMatrix/TransformPoints).
void
TStroke::Rotate(long /*angle*/)
{
	UpdateBBox();
}


// ROM 0x0022016c Scale__7TStrokeFlT1
// NOT YET RECONSTRUCTED: the points scaled (MxScale/MxMove).
void
TStroke::Scale(long /*sx*/, long /*sy*/)
{
	UpdateBBox();
}


// ROM 0x002202b0 Draw__7TStrokeFv
// The stroke marked drawn (and drawn-when-done when it is done); its
// segments go to the inker unless the stroke is inkless.  NOT YET
// RECONSTRUCTED: the inker (InkerLine).
void
TStroke::Draw(void)
{
	SetFlags(kStrokeDrawn);
	if (Done())
		SetFlags(kStrokeDrawnWhenDone);
}


// ROM 0x00220424 Map__7TStrokeFP5FRect
// The points moved from the box into the rect (GetMapper keeps their
// proportions), which becomes the box.
void
TStroke::Map(FRect* dst)
{
	FRect src = fBBox;
	FRect to = *dst;
	GetMapper(&src, &to);
	for (long i = 0; i < fCount; i++)
	{
		SamplePt* sample = GetPoint(i);
		FPoint pt;
		::GetPoint(sample, &pt);
		MapPoint(&pt, &src, &to);
		SetPoint(sample, &pt);
	}
	fBBox = to;
}


// ROM 0x00220550 Offset__7TStrokeFlT1
void
TStroke::Offset(long dx, long dy)
{
	for (long i = 0; i < fCount; i++)
	{
		SamplePt* sample = GetPoint(i);
		SetSampleX(sample, SampleX(sample) + dx);
		SetSampleY(sample, SampleY(sample) + dy);
	}
	fBBox.top += dy;
	fBBox.bottom += dy;
	fBBox.left += dx;
	fBBox.right += dx;
}


// ROM 0x0022067c UpdateBBox__7TStrokeFv
// The box found again from the points, a unit past them.
void
TStroke::UpdateBBox(void)
{
	for (long i = 0; i < fCount; i++)
	{
		FPoint pt;
		::GetPoint(GetPoint(i), &pt);
		AddPtToRect(&pt, &fBBox, i == 0);
	}
	fBBox.right++;
	fBBox.bottom++;
}


// ROM 0x0022071c Done__7TStrokeFv
Boolean
TStroke::Done(void)
{
	return (fFlags & kStrokeDone) != 0;
}


// ROM 0x001fcdf0 AcquireStroke__FP7TStroke
// The inker's lock taken while the stroke is still being drawn (the
// host has no inker task: nothing to take); ==> whether it was.
Boolean
AcquireStroke(TStroke* /*stroke*/)
{
	return false;
}


// ROM 0x001fce28 ReleaseStroke__Fv
void
ReleaseStroke(void)
{ }


// ROM 0x001a5bd8 GetStrokeRect__FP7TStrokeP5TRect
// The box rounded to pixels, made at least a pixel each way.
void
GetStrokeRect(TStroke* stroke, Rect* rect)
{
	UnfixRect(&stroke->fBBox, rect);
	if (rect->bottom == rect->top)
		rect->bottom++;
	if (rect->right == rect->left)
		rect->right++;
}


// ROM 0x0022b6cc AdjustForInk__FP5TRect
// The rectangle let out a pixel, and the userPenSize preference more at
// the right and bottom.
void
AdjustForInk(Rect* rect)
{
	long penSize = RINT(GetPreference(RSSYMuserpensize));
	InsetRect(rect, -1, -1);
	rect->right = (short) (rect->right + penSize);
	rect->bottom = (short) (rect->bottom + penSize);
}


/*------------------------------------------------------------------------------
	T S t r o k e P u b l i c
------------------------------------------------------------------------------*/

// ROM 0x0014727c Make__13TStrokePublicSFP7TStrokeUc
TStrokePublic*
TStrokePublic::Make(TStroke* stroke, Boolean owns)
{
	return new TStrokePublic(stroke, owns);
}


// ROM 0x0014728c __ct__13TStrokePublicFP7TStrokeUc
TStrokePublic::TStrokePublic(TStroke* stroke, Boolean owns)
{
	fUnused = 0;
	fStroke = stroke;
	fOwnsStroke = owns;
	fInkedRect.top = -0x8000;
	fInkedRect.left = 0;
	fInkedRect.bottom = -0x8000;
	fInkedRect.right = 0;
}


// ROM 0x001472e8 __dt__13TStrokePublicFv
TStrokePublic::~TStrokePublic()
{
	if (fOwnsStroke && fStroke != nil)
		fStroke->Dispose();
}


// ROM 0x00147324 Done__13TStrokePublicFv
Boolean
TStrokePublic::Done(void)
{
	return (fStroke->fFlags & kStrokeDone) != 0;
}


// ROM 0x0014732c Size__13TStrokePublicFv
long
TStrokePublic::Size(void)
{
	return fStroke->fCount;
}


// ROM 0x00147338 DownTime__13TStrokePublicFv
ULong
TStrokePublic::DownTime(void)
{
	return fStroke->fDownTime;
}


// ROM 0x00147344 UpTime__13TStrokePublicFv
ULong
TStrokePublic::UpTime(void)
{
	return fStroke->fUpTime;
}


// ROM 0x0014740c Bounds__13TStrokePublicFP5TRect
// The box in pixels, a pixel wider and taller.
void
TStrokePublic::Bounds(Rect* rect)
{
	UnfixRect(&fStroke->fBBox, rect);
	rect->bottom++;
	rect->right++;
}


// a sample as a QuickDraw point, rounded
static Point
SamplePoint(SamplePt* sample)
{
	Point pt;
	pt.h = (short) ((SampleX(sample) + 0x8000) >> 16);
	pt.v = (short) ((SampleY(sample) + 0x8000) >> 16);
	return pt;
}


// ROM 0x0014745c GetPoint__13TStrokePublicFl
// The point at the index (the last for an index past the end).
Point
TStrokePublic::GetPoint(long index)
{
	Boolean locked = AcquireStroke(fStroke);
	if (index >= fStroke->fCount)
		index = fStroke->fCount - 1;
	SamplePt* sample = fStroke->GetPoint(index);
	if (locked)
		ReleaseStroke();
	return SamplePoint(sample);
}


// ROM 0x00147504 FirstPoint__13TStrokePublicFv
Point
TStrokePublic::FirstPoint(void)
{
	Boolean locked = AcquireStroke(fStroke);
	SamplePt* sample = fStroke->GetPoint(fStroke->fCount == 0 ? -1 : 0);
	if (locked)
		ReleaseStroke();
	if (sample == nil)
	{
		Point none = { 0, 0 };
		return none;
	}
	return SamplePoint(sample);
}


// ROM 0x0014750c FinalPoint__13TStrokePublicFv
Point
TStrokePublic::FinalPoint(void)
{
	return GetPoint(Size() - 1);
}


// ROM 0x0014753c InkOn__13TStrokePublicFv
void
TStrokePublic::InkOn(void)
{ }


// ROM 0x00147540 InkOff__13TStrokePublicFUcT1
// The stroke's ink taken off the screen (once): the stroke marked
// inkless, the inker told to stop (hobbled or not; NOT YET: the inker)
// when the stroke is still going, and the inked rectangle either put
// down as dirty screen (the ink was drawn straight on it) or, when
// invalidate is asked, redrawn - the views' way when the stroke was
// drawn, the screen's when not.
void
TStrokePublic::InkOff(Boolean invalidate, Boolean hobbled)
{
	if (fStroke->TestFlags(kStrokeNoInk))
		return;
	Boolean locked = AcquireStroke(fStroke);
	fStroke->SetFlags(kStrokeNoInk);
	if (locked)
		ReleaseStroke();
	if (!fStroke->Done())
		(void) hobbled;		// NOT YET RECONSTRUCTED: InkerOff / InkerOffUnHobbled(&fInkedRect)
	Rect inked;
	GetInkedRect(&inked);
	if (!invalidate)
		gRootView->SmartScreenDirty(inked);
	else if (fStroke->TestFlags(kStrokeDrawn))
		gRootView->SmartInvalidate(inked);
	else
	{
		StartDrawing(nil, &inked);
		StopDrawing(nil, &inked);
	}
}


// ROM 0x00147620 InkOff__13TStrokePublicFUc
void
TStrokePublic::InkOff(Boolean invalidate)
{
	InkOff(invalidate, true);
}


// ROM 0x0014762c GetInkedRect__13TStrokePublicFP5TRect
// Where the ink lies: the stroke's box let out for the pen, found once.
void
TStrokePublic::GetInkedRect(Rect* rect)
{
	if (fInkedRect.top == -0x8000)
	{
		GetStrokeRect(fStroke, &fInkedRect);
		AdjustForInk(&fInkedRect);
	}
	*rect = fInkedRect;
}


// ROM 0x00147674 Invalidate__13TStrokePublicFv
// The inked rectangle redrawn (for an inked stroke).
void
TStrokePublic::Invalidate(void)
{
	if (fStroke->TestFlags(kStrokeNoInk))
		return;
	Rect inked;
	GetInkedRect(&inked);
	gRootView->SmartInvalidate(inked);
}
