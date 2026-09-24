/*
	File:		recognition/Stroke.cpp

	Contains:	TStroke, TStrokePublic, the sample points and the Fixed
				rectangle helpers.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Stroke.h"
#include "Polygons.h"
#include "Ports.h"
#include "Draw.h"
#include "Rects.h"
#include "Screen.h"
#include "RootView.h"
#include "Locale.h"
#include "FixedMath.h"
#include "FixedMathExtra.h"	// WrapAdd/WrapSub: the ARM's wrapping add and subtract
#include <string.h>

// the pen tip and inking defaults the strokes are made with
ULong	gLastPenTip = 0;				// ROM 0x0c1008bc gLastPenTip
Boolean	gDefaultInk = true;				// ROM 0x0c101890 gDefaultInk


/*------------------------------------------------------------------------------
	S a m p l e   p o i n t s
------------------------------------------------------------------------------*/

// ROM 0x00220e80 SampleX__FP8SamplePt
// The x in Fixed: 14 bits of eighths of a pixel.
Fixed
SampleX(SamplePt* pt)
{
	return (Fixed) (pt->fX & 0x3fff) << 13;
}


// ROM 0x0022212c SampleY__FP8SamplePt
Fixed
SampleY(SamplePt* pt)
{
	return (Fixed) (pt->fY & 0x3fff) << 13;
}


// ROM 0x002223c4 SampleP__FP8SamplePt
// The pressure: two bits above x, one above y.
ULong
SampleP(SamplePt* pt)
{
	return ((pt->fX & 0xc000) >> 13) | ((pt->fY & 0x4000) >> 14);
}


// ROM 0x00222e8c SetSampleX__FP8SamplePtl
// The x set (no less than 0), the pressure bits kept.
void
SetSampleX(SamplePt* pt, Fixed x)
{
	if (x < 1)
		x = 0;
	pt->fX = (UShort) (((x >> 13) & 0x3fff) | (pt->fX & 0xc000));
}


// ROM 0x00223014 SetSampleY__FP8SamplePtl
void
SetSampleY(SamplePt* pt, Fixed y)
{
	if (y < 1)
		y = 0;
	pt->fY = (UShort) (((y >> 13) & 0x3fff) | (pt->fY & 0xc000));
}


// ROM 0x00222140 GetPoint__FP8SamplePtP6FPoint
void
GetPoint(SamplePt* pt, FPoint* fpt)
{
	fpt->x = SampleX(pt);
	fpt->y = SampleY(pt);
}


// ROM 0x0022216c SetPoint__FP8SamplePtP6FPoint
void
SetPoint(SamplePt* pt, FPoint* fpt)
{
	SetSampleX(pt, fpt->x);
	SetSampleY(pt, fpt->y);
}


// ROM 0x002221d8 TestFlag__FP8SamplePtUl
// The flag bit (bit 15 of y's word, as bit 1 of the flags).
ULong
TestFlag(SamplePt* pt, ULong flag)
{
	return flag & ((pt->fY & 0x8000) >> 14);
}


// ROM 0x002221f0 SetFlag__FP8SamplePtUl
void
SetFlag(SamplePt* pt, ULong flag)
{
	ULong flags = ((pt->fY & 0x8000) >> 14) | (flag & 2);
	pt->fY = (UShort) ((pt->fY & 0x7fff) | (flags << 14));
}


// ROM 0x00222220 UnsetFlag__FP8SamplePtUl
void
UnsetFlag(SamplePt* pt, ULong flag)
{
	ULong flags = ((pt->fY & 0x8000) >> 14) & ~flag;
	pt->fY = (UShort) ((pt->fY & 0x7fff) | (flags << 14));
}


/*------------------------------------------------------------------------------
	F i x e d   r e c t a n g l e s
------------------------------------------------------------------------------*/

// ROM 0x001a4174 AddPtToRect
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


// ROM 0x001a43b4 SetRectangleEmpty
void
SetRectangleEmpty(FRect* rect)
{
	rect->left = rect->top = rect->right = rect->bottom = 0;
}


// ROM 0x001a43cc SetRectanglePoint
void
SetRectanglePoint(FRect* rect, const FPoint* pt)
{
	rect->left = rect->right = pt->x;
	rect->top = rect->bottom = pt->y;
}


// ROM 0x001a4128 RectangleCenter
void
RectangleCenter(const FRect* rect, FPoint* center)
{
	center->x = (rect->left + rect->right) >> 1;
	center->y = (rect->top + rect->bottom) >> 1;
}


// ROM 0x001a3f44 UnfixRect
// The Fixed rectangle rounded to pixels.
void
UnfixRect(const FRect* src, Rect* dst)
{
	dst->top = (short) ((src->top + 0x8000) >> 16);
	dst->left = (short) ((src->left + 0x8000) >> 16);
	dst->bottom = (short) ((src->bottom + 0x8000) >> 16);
	dst->right = (short) ((src->right + 0x8000) >> 16);
}


// ROM 0x001a4224 GetMapper
// The dst rect narrowed (or shortened) about its centre to the src
// rect's proportions, so that a mapping between them keeps shapes.
//
// BUG (the ROM's): a stroke that is perfectly flat blows the dst rect up
// to about sixteen thousand pixels each way.  `UpdateBBox` makes a box a
// single Fixed unit - 1/65536 of a pixel - past its points, so a stroke
// drawn along one exact y has a height of 1 and `srcRatio` divides that
// by its width and comes out 0.  `FixedDivide(dstHeight, 0)` then
// saturates to the largest Fixed there is, and the inset that is worked
// out from it is about -2^30, which is added to the left edge and taken
// off the right.  Every point of the stroke is then mapped into that,
// which on the machine wraps and puts the ink somewhere meaningless.
// The reconstruction does the same (MapPoint below wraps as the ARM
// does), because a Newton with a flat stroke in a word large enough to
// need scaling really does make nonsense of it.
void
GetMapper(const FRect* src, const FRect* dst)
{
	FRect* d = (FRect*) dst;
	Fixed srcRatio = FixedDivide(WrapSub(src->bottom, src->top), WrapSub(src->right, src->left));
	Fixed dstHeight = WrapSub(d->bottom, d->top);
	Fixed dstWidth = WrapSub(d->right, d->left);
	Fixed dstRatio = FixedDivide(dstHeight, dstWidth);
	if (srcRatio < dstRatio)
	{
		Fixed height = FixedMultiply(dstWidth, srcRatio);
		Fixed inset = WrapSub(dstHeight, height) >> 1;
		d->top = WrapAdd(d->top, inset);
		d->bottom = WrapSub(d->bottom, inset);
	}
	else
	{
		Fixed width = FixedDivide(dstHeight, srcRatio);
		Fixed inset = WrapSub(dstWidth, width) >> 1;
		d->left = WrapAdd(d->left, inset);
		d->right = WrapSub(d->right, inset);
	}
}


// ROM 0x001a3fec EmptyRectangle
Boolean
EmptyRectangle(const FRect* rect)
{
	return !(rect->top < rect->bottom && rect->left < rect->right);
}


// ROM 0x001a4020 InsetRectangle
void
InsetRectangle(FRect* rect, Fixed dx, Fixed dy)
{
	rect->top += dy;
	rect->left += dx;
	rect->bottom -= dy;
	rect->right -= dx;
}


// ROM 0x001a4054 PointInRectangle
Boolean
PointInRectangle(const FPoint* pt, const FRect* rect)
{
	return rect->top <= pt->y && pt->y < rect->bottom && rect->left <= pt->x && pt->x < rect->right;
}


// ROM 0x001a43f0 SetRectangleEdges
void
SetRectangleEdges(FRect* rect, Fixed left, Fixed top, Fixed right, Fixed bottom)
{
	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;
}


// ROM 0x001a4098 SectRectangle
// The rectangles' intersection; empty (and false) when they do not meet
// or a is empty.
Boolean
SectRectangle(FRect* result, const FRect* a, const FRect* b)
{
	Fixed left = a->left, top = a->top, right = a->right, bottom = a->bottom;
	if (top < bottom && left < right)
	{
		if (top < b->top)
			top = b->top;
		if (left < b->left)
			left = b->left;
		if (bottom > b->bottom)
			bottom = b->bottom;
		if (right > b->right)
			right = b->right;
		if (top < bottom && left < right)
		{
			SetRectangleEdges(result, left, top, right, bottom);
			return true;
		}
	}
	SetRectangleEmpty(result);
	return false;
}


// ROM 0x001a42e4 MapPoint
// The point moved from where it lies in src to the same place in dst.
//
// DEVIATION: the widths and heights are taken through ULong so that they
// wrap as the ARM's own `sub` does.  A rect GetMapper has blown up (see
// the bug there) is wider than a Fixed can hold, and the machine simply
// wraps where the host would trap.
void
MapPoint(FPoint* pt, const FRect* src, const FRect* dst)
{
	Fixed srcHeight = WrapSub(src->bottom, src->top);
	Fixed dstHeight = WrapSub(dst->bottom, dst->top);
	Fixed dy = WrapSub(pt->y, src->top);
	if (srcHeight != dstHeight)
		dy = FixedMultiply(dstHeight, FixedDivide(dy, srcHeight));
	pt->y = WrapAdd(dst->top, dy);
	Fixed srcWidth = WrapSub(src->right, src->left);
	Fixed dstWidth = WrapSub(dst->right, dst->left);
	Fixed dx = WrapSub(pt->x, src->left);
	if (srcWidth != dstWidth)
		dx = FixedMultiply(dstWidth, FixedDivide(dx, srcWidth));
	pt->x = WrapAdd(dst->left, dx);
}


/*------------------------------------------------------------------------------
	T S t r o k e
------------------------------------------------------------------------------*/

// ROM 0x00222254 Make__7TStrokeSFUl
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


// ROM 0x002222c4 IStroke__7TStrokeFUl
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
		fPrevUpTime = 0;
		fPrevDownTime = 0;
		fDecimation = 1;
		fDecimationCount = 0;
		SetRectangleEmpty(&fBBox);
		fFlags = 0;
		SetFlags(gLastPenTip << 8);
		if (!gDefaultInk)
			SetFlags(kStrokeNoInk);
		fSampleRate = 0;
		fClickEvent = 0;
	}
	return err;
}


// ROM 0x00222370 IDispose__7TStrokeFv
// The points freed (the ROM: in the stroker's heap when the stroke was
// made there).
void
TStroke::IDispose(void)
{
	TArray::IDispose();
}


// ROM 0x002223c0 SizeInBytes__7TStrokeFv
long
TStroke::SizeInBytes(void)
{
	return TArray::SizeInBytes() + (sizeof(TStroke) - sizeof(TDArray));
}


// ROM 0x002223f0 Bifurcate__7TStrokeFv
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


// ROM 0x00222484 TryToAddPoint__7TStrokeFv
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


// ROM 0x002224c0 AddPoint__7TStrokeFP5TabPt
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


// ROM 0x0022277c EndStroke__7TStrokeFv
// The stroke done: compacted, its box a unit past its points.
void
TStroke::EndStroke(void)
{
	SetFlags(kStrokeDone);
	Compact();
	fBBox.right++;
	fBBox.bottom++;
}


// ROM 0x002227c0 GetPoint__7TStrokeFl
SamplePt*
TStroke::GetPoint(long index)
{
	return (SamplePt*) GetEntry(index);
}


// ROM 0x002227c8 GetTabPt__7TStrokeFlP5TabPt
void
TStroke::GetTabPt(long index, TabPt* pt)
{
	SamplePt* sample = GetPoint(index);
	pt->x = SampleX(sample);
	pt->y = SampleY(sample);
	pt->z = (UShort) SampleP(sample);
	pt->p = (UShort) ((sample->fY & 0x8000) >> 14);
}


// ROM 0x00222858 GetFPoint__7TStrokeFlP6FPoint
void
TStroke::GetFPoint(long index, FPoint* pt)
{
	SamplePt* sample = GetPoint(index);
	pt->x = SampleX(sample);
	pt->y = SampleY(sample);
}


// ROM 0x002228a0 Rotate__7TStrokeFl
// NOT YET RECONSTRUCTED: the points turned about the box's centre (the
// matrix utilities SetIdentityMatrix/RotateMatrix/TransformPoints).
void
TStroke::Rotate(long /*angle*/)
{
	UpdateBBox();
}


// ROM 0x002229b4 Scale__7TStrokeFlT1
// NOT YET RECONSTRUCTED: the points scaled (MxScale/MxMove).
void
TStroke::Scale(long /*sx*/, long /*sy*/)
{
	UpdateBBox();
}


// ROM 0x00222af8 Draw__7TStrokeFv
// The stroke inked: the segment between each pair of samples drawn
// straight into the screen's pixel map (InkerLine), so that the ink keeps
// up with the pen whatever the view system is doing.  The stroke is
// marked drawn - and drawn-when-done when the pen has been lifted - so
// that StrokeUpdate knows which of the queued strokes still have to be
// put back when something draws over them.
//
// A stroke that is inkless (kStrokeNoInk - a tap, or a view that does not
// want ink) is marked and left; segments whose two samples round to the
// same pixel are skipped, which is most of them while the pen is still.
// The nib's size is the second byte of the stroke's flags, square.
void
TStroke::Draw(void)
{
	SetFlags(kStrokeDrawn);
	if (Done())
		SetFlags(kStrokeDrawnWhenDone);
	if (TestFlags(kStrokeNoInk))
		return;
	Boolean acquired = AcquireStroke(this);
	long count = Count();
	if (count != 0)
	{
		Lock();
		SamplePt* sample = GetPoint(0);
		Point at;
		at.h = (short) ((SampleX(sample) + 0x8000) >> 16);
		at.v = (short) ((SampleY(sample) + 0x8000) >> 16);
		Point pen;
		pen.h = pen.v = (short) ((fFlags & 0xff00) >> 8);
		sample++;
		for (long i = 1; i < count; i++, sample++)
		{
			Point was = at;
			at.h = (short) ((SampleX(sample) + 0x8000) >> 16);
			at.v = (short) ((SampleY(sample) + 0x8000) >> 16);
			SampleP(sample);		// (the ROM reads the pressure and drops it)
			if (was.h != at.h || was.v != at.v)
			{
				Rect damaged;
				InkerLine(was, at, &damaged, pen);
			}
		}
		Unlock();
	}
	if (acquired)
		ReleaseStroke();
}


// ROM 0x00222c6c Map__7TStrokeFP5FRect
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


// ROM 0x00222d98 Offset__7TStrokeFlT1
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


// ROM 0x00222ec4 UpdateBBox__7TStrokeFv
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


// ROM 0x00222f64 Done__7TStrokeFv
Boolean
TStroke::Done(void)
{
	return (fFlags & kStrokeDone) != 0;
}


// ROM 0x001ff5a0 AcquireStroke__FP7TStroke
// The inker's lock taken while the stroke is still being drawn (the
// host has no inker task: the semaphore is nothing); ==> whether it was.
Boolean
AcquireStroke(TStroke* stroke)
{
	return !stroke->Done();
}


// ROM 0x001ff5d8 ReleaseStroke__Fv
void
ReleaseStroke(void)
{ }


// ROM 0x001a3658 GetStrokeRect__FP7TStrokeP5TRect
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


// ROM 0x0022de2c AdjustForInk__FP5TRect
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

// ROM 0x00145728 Make__13TStrokePublicSFP7TStrokeUc
TStrokePublic*
TStrokePublic::Make(TStroke* stroke, Boolean owns)
{
	return new TStrokePublic(stroke, owns);
}


// ROM 0x00145738 __ct__13TStrokePublicFP7TStrokeUc
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


// ROM 0x00145794 __dt__13TStrokePublicFv
TStrokePublic::~TStrokePublic()
{
	if (fOwnsStroke && fStroke != nil)
		fStroke->Dispose();
}


// ROM 0x001457d0 Done__13TStrokePublicFv
Boolean
TStrokePublic::Done(void)
{
	return (fStroke->fFlags & kStrokeDone) != 0;
}


// ROM 0x001457d8 Size__13TStrokePublicFv
long
TStrokePublic::Size(void)
{
	return fStroke->fCount;
}


// ROM 0x001457e4 DownTime__13TStrokePublicFv
ULong
TStrokePublic::DownTime(void)
{
	return fStroke->fDownTime;
}


// ROM 0x001457f0 UpTime__13TStrokePublicFv
ULong
TStrokePublic::UpTime(void)
{
	return fStroke->fUpTime;
}


// ROM 0x001458b8 Bounds__13TStrokePublicFP5TRect
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


// ROM 0x00145908 GetPoint__13TStrokePublicFl
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


// ROM 0x001459b0 FirstPoint__13TStrokePublicFv
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


// ROM 0x001459b8 FinalPoint__13TStrokePublicFv
Point
TStrokePublic::FinalPoint(void)
{
	return GetPoint(Size() - 1);
}


// ROM 0x001459e8 InkOn__13TStrokePublicFv
void
TStrokePublic::InkOn(void)
{ }


// ROM 0x001459ec InkOff__13TStrokePublicFUcT1
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


// ROM 0x00145acc InkOff__13TStrokePublicFUc
void
TStrokePublic::InkOff(Boolean invalidate)
{
	InkOff(invalidate, true);
}


// ROM 0x00145ad8 GetInkedRect__13TStrokePublicFP5TRect
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


// ROM 0x00145b20 Invalidate__13TStrokePublicFv
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


// ROM 0x00145e38 AsPolygon__FP7TStroke
// The stroke as a QuickDraw polygon in a handle of its own: its points
// rounded to pixels and moved to the top left of its box, which is what
// the polygon's own bounds hold.  ==> nil when there is no memory.
Handle
AsPolygon(TStroke* stroke)
{
	long count = stroke->Count();
	long size = count * 4 + 12;
	Handle h = NewHandle(size);
	if (h == nil)
		return nil;
	SetHandleName(h, 'AsPl');
	Polygon* poly = (Polygon*) *h;
	poly->polySize = (short) size;
	GetStrokeRect(stroke, &poly->polyBBox);
	SamplePt* sample = stroke->GetPoint(0);
	for (long i = 0; i < count; i++)
	{
		poly->polyPoints[i].h = (short) (RoundFixed(SampleX(sample)) - poly->polyBBox.left);
		poly->polyPoints[i].v = (short) (RoundFixed(SampleY(sample)) - poly->polyBBox.top);
		sample++;
	}
	return h;
}
