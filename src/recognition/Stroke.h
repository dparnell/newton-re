/*
	File:		recognition/Stroke.h

	Contains:	TStroke, a pen stroke: the tablet's points (TabPt: x, y in
				Fixed, a pressure, flags) packed as SamplePts - 14 bits each
				of x and y in eighths of a pixel, two bits of pressure, a
				flag - in a TDArray, with the bounding box (an FRect), the
				pen-down and pen-up times, and a decimation that keeps every
				n-th point once a stroke grows past 800 points (Bifurcate
				doubles it and halves the points).  The flags (TRecObject's):
				0x40000000 the stroke is done (the pen is up), 0x20000000 no
				ink is drawn for it, 0x04000000 it was drawn, 0x08000000
				drawn when done, bits 8-15 the pen tip it was made with.
				The ROM's TStroke is 0x4c bytes over TDArray.

				TStrokePublic is the face the views and NewtonScript see
				(StrokeFromRef): the points as QuickDraw Points (rounded),
				the bounds, the times, done, and the inking - InkOff takes
				the stroke's ink off the screen (the inker, NOT YET: the
				inked rectangle is invalidated) - 0x14 bytes.

				Rotate and Scale go through toolbox/Matrix.h.

				NOT YET RECONSTRUCTED: the inker (Draw, InkOff's InkerOff),
				the stroke semaphore between the inker task and the
				recogniser (AcquireStroke: the host has one task).

	Reconstructed from the MP2x00 US ROM (0x0022212c-0x00223038,
	0x00145728-0x00145b5c, 0x001a3658-0x001a3fa0); each function cites its
	origin.
*/

#ifndef __STROKE_H
#define __STROKE_H

#include "RecObject.h"

// what the tablet reports: a point in Fixed, a pressure, flags
struct TabPt
{
	Fixed		x;			// +0x00
	Fixed		y;			// +0x04
	UShort		z;			// +0x08  the pressure (0-7 kept)
	UShort		p;			// +0x0a  flags (bit 1 kept)
};

// a packed point: x and y in eighths of a pixel (14 bits), the pressure
// (bits 15-14 of x's word and 14 of y's), a flag (bit 15 of y's word)
struct SamplePt
{
	UShort		fX;
	UShort		fY;
};
Fixed	SampleX(SamplePt* pt);								// ROM 0x00220e80 SampleX__FP8SamplePt
Fixed	SampleY(SamplePt* pt);								// ROM 0x0022212c SampleY__FP8SamplePt
ULong	SampleP(SamplePt* pt);								// ROM 0x002223c4 SampleP__FP8SamplePt - the pressure
void	SetSampleX(SamplePt* pt, Fixed x);					// ROM 0x00222e8c SetSampleX__FP8SamplePtl
void	SetSampleY(SamplePt* pt, Fixed y);					// ROM 0x00223014 SetSampleY__FP8SamplePtl
void	GetPoint(SamplePt* pt, FPoint* fpt);				// ROM 0x00222140 GetPoint__FP8SamplePtP6FPoint
void	SetPoint(SamplePt* pt, FPoint* fpt);				// ROM 0x0022216c SetPoint__FP8SamplePtP6FPoint
ULong	TestFlag(SamplePt* pt, ULong flag);					// ROM 0x002221d8 TestFlag__FP8SamplePtUl
void	SetFlag(SamplePt* pt, ULong flag);					// ROM 0x002221f0 SetFlag__FP8SamplePtUl
void	UnsetFlag(SamplePt* pt, ULong flag);				// ROM 0x00222220 UnsetFlag__FP8SamplePtUl

// the stroke flags
enum
{
	kStrokeDone			= 0x40000000,
	kStrokeNoInk		= 0x20000000,
	kStrokeDrawnWhenDone = 0x08000000,
	kStrokeDrawn		= 0x04000000
};

void	AddPtToRect(const FPoint* pt, FRect* rect, Boolean first);	// ROM 0x001a4174 AddPtToRect - the rect grown to the point (set to it when first)
void	SetRectangleEmpty(FRect* rect);						// ROM 0x001a43b4 SetRectangleEmpty
void	SetRectanglePoint(FRect* rect, const FPoint* pt);	// ROM 0x001a43cc SetRectanglePoint
void	RectangleCenter(const FRect* rect, FPoint* center);	// ROM 0x001a4128 RectangleCenter
void	UnfixRect(const FRect* src, Rect* dst);				// ROM 0x001a3f44 UnfixRect - rounded to pixels
void	GetMapper(const FRect* src, const FRect* dst);		// ROM 0x001a4224 GetMapper - the dst rect kept in the src rect's proportions
void	MapPoint(FPoint* pt, const FRect* src, const FRect* dst);	// ROM 0x001a42e4 MapPoint - the point moved from the src rect to the dst rect
Boolean	EmptyRectangle(const FRect* rect);					// ROM 0x001a3fec EmptyRectangle
void	InsetRectangle(FRect* rect, Fixed dx, Fixed dy);	// ROM 0x001a4020 InsetRectangle
Boolean	PointInRectangle(const FPoint* pt, const FRect* rect);	// ROM 0x001a4054 PointInRectangle
Boolean	SectRectangle(FRect* result, const FRect* a, const FRect* b);	// ROM 0x001a4098 SectRectangle - ==> whether they meet (the result empty when not)
void	SetRectangleEdges(FRect* rect, Fixed left, Fixed top, Fixed right, Fixed bottom);	// ROM 0x001a43f0 SetRectangleEdges

extern Boolean	gDefaultInk;								// ROM 0x0c101890 gDefaultInk - strokes are inked unless told otherwise
extern ULong	gLastPenTip;								// ROM 0x0c1008bc gLastPenTip - the pen tip new strokes are flagged with
extern long		gInkerCalibrated;							// ROM 0x0c101654 gInkerCalibrated - set once the tablet has been calibrated

class TStroke : public TDArray
{
public:
	static TStroke*	Make(ULong count);						// ROM 0x00222254 Make__7TStrokeSFUl
	long			IStroke(ULong count);					// ROM 0x002222c4 IStroke__7TStrokeFUl
	virtual void	IDispose(void);							// ROM 0x00222370 IDispose__7TStrokeFv
	virtual long	SizeInBytes(void);						// ROM 0x002223c0 SizeInBytes__7TStrokeFv

	void			Bifurcate(void);						// ROM 0x002223f0 Bifurcate__7TStrokeFv - every other point dropped, the decimation doubled
	SamplePt*		TryToAddPoint(void);					// ROM 0x00222484 TryToAddPoint__7TStrokeFv - a point added (bifurcating when memory is short)
	virtual long	AddPoint(TabPt* pt);					// ROM 0x002224c0 AddPoint__7TStrokeFP5TabPt - ==> 0, or 1 for no memory
	void			EndStroke(void);						// ROM 0x0022277c EndStroke__7TStrokeFv - done, compacted
	SamplePt*		GetPoint(long index);					// ROM 0x002227c0 GetPoint__7TStrokeFl
	void			GetTabPt(long index, TabPt* pt);		// ROM 0x002227c8 GetTabPt__7TStrokeFlP5TabPt
	void			GetFPoint(long index, FPoint* pt);		// ROM 0x00222858 GetFPoint__7TStrokeFlP6FPoint
	void			Rotate(long angle);						// ROM 0x002228a0 Rotate__7TStrokeFl - turned by the degrees (16.16) about the box's centre
	void			Scale(long sx, long sy);				// ROM 0x002229b4 Scale__7TStrokeFlT1 - scaled (16.16), a negative scale flipping within the box
	void			Draw(void);								// ROM 0x00222af8 Draw__7TStrokeFv - the stroke inked straight into the screen (InkerLine)
	void			Map(FRect* dst);						// ROM 0x00222c6c Map__7TStrokeFP5FRect - the points moved from the box to the rect
	void			Offset(long dx, long dy);				// ROM 0x00222d98 Offset__7TStrokeFlT1
	void			UpdateBBox(void);						// ROM 0x00222ec4 UpdateBBox__7TStrokeFv
	Boolean			Done(void);								// ROM 0x00222f64 Done__7TStrokeFv

	FRect			fBBox;			// +0x20  the points' box (its right and bottom a fixed unit past them)
	long			fSampleRate;	// +0x30  halved by Bifurcate
	ULong			fDownTime;		// +0x34
	ULong			fUpTime;		// +0x38
	ULong			fPrevDownTime;	// +0x3c  the stroke before's times (StrokeCentral::StartNewStroke)
	ULong			fPrevUpTime;	// +0x40
	UShort			fDecimation;	// +0x44  every n-th point kept (1: all)
	UShort			fDecimationCount;	// +0x46  points since the last kept
	long			fClickEvent;	// +0x48  the click event noted in it (Unit.h: kTapClick...; kProcessedClick once handled)
};

// the inker's lock on the stroke being drawn (the host has no inker task)
Boolean	AcquireStroke(TStroke* stroke);						// ROM 0x001ff5a0 AcquireStroke__FP7TStroke - ==> whether it was taken (to release)
void	ReleaseStroke(void);								// ROM 0x001ff5d8 ReleaseStroke__Fv
// A list of strokes ended by a nil, which is how the ink and the
// recogniser pass a handful of them about.  Dispose releases rather than
// frees, so the same stroke may be in more than one list at once.
long	CountTStrokes(TStroke** strokes);					// ROM 0x001a3420 CountTStrokes__FPP7TStroke
void	DisposeTStrokes(TStroke** strokes);					// ROM 0x001a3448 DisposeTStrokes__FPP7TStroke

void	GetStrokeRect(TStroke* stroke, Rect* rect);			// ROM 0x001a3658 GetStrokeRect__FP7TStrokeP5TRect - the box in pixels, at least a pixel each way
// The stroke as a QuickDraw polygon in a handle of its own.
Handle	AsPolygon(TStroke* stroke);							// ROM 0x00145e38 AsPolygon__FP7TStroke
void	AdjustForInk(Rect* rect);							// ROM 0x0022de2c AdjustForInk__FP5TRect - let out for the pen size

class TStrokePublic
{
public:
	static TStrokePublic*	Make(TStroke* stroke, Boolean owns);	// ROM 0x00145728 Make__13TStrokePublicSFP7TStrokeUc
					TStrokePublic(TStroke* stroke, Boolean owns);	// ROM 0x00145738 __ct__13TStrokePublicFP7TStrokeUc
					~TStrokePublic();						// ROM 0x00145794 __dt__13TStrokePublicFv (the stroke disposed when owned)
	Boolean			Done(void);								// ROM 0x001457d0 Done__13TStrokePublicFv
	long			Size(void);								// ROM 0x001457d8 Size__13TStrokePublicFv - the points
	ULong			DownTime(void);							// ROM 0x001457e4 DownTime__13TStrokePublicFv
	ULong			UpTime(void);							// ROM 0x001457f0 UpTime__13TStrokePublicFv
	void			Bounds(Rect* rect);						// ROM 0x001458b8 Bounds__13TStrokePublicFP5TRect - the box in pixels, a pixel wider and taller
	Point			GetPoint(long index);					// ROM 0x00145908 GetPoint__13TStrokePublicFl - rounded (the last for an index past the end)
	Point			FirstPoint(void);						// ROM 0x001459b0 FirstPoint__13TStrokePublicFv
	Point			FinalPoint(void);						// ROM 0x001459b8 FinalPoint__13TStrokePublicFv
	void			InkOn(void);							// ROM 0x001459e8 InkOn__13TStrokePublicFv (nothing)
	void			InkOff(Boolean invalidate, Boolean hobbled);	// ROM 0x001459ec InkOff__13TStrokePublicFUcT1
	void			InkOff(Boolean invalidate);				// ROM 0x00145acc InkOff__13TStrokePublicFUc
	void			GetInkedRect(Rect* rect);				// ROM 0x00145ad8 GetInkedRect__13TStrokePublicFP5TRect
	void			Invalidate(void);						// ROM 0x00145b20 Invalidate__13TStrokePublicFv

	TStroke*		Stroke(void)		{ return fStroke; }

	long			fUnused;		// +0x00  0
	TStroke*		fStroke;		// +0x04
	Boolean			fOwnsStroke;	// +0x08
	Rect			fInkedRect;		// +0x0c  where the ink was (top -0x8000: not known yet)
};

#endif	/* __STROKE_H */
