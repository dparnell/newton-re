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

				NOT YET RECONSTRUCTED: the inker (Draw, InkOff's InkerOff),
				Rotate and Scale (the matrix utilities), the stroke semaphore
				between the inker task and the recogniser (AcquireStroke:
				the host has one task).

	Reconstructed from the MP2100 D ROM (0x0021f8e4-0x002207f0,
	0x0014727c-0x001476b0, 0x001a5bd8-0x001a6520); each function cites its
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
Fixed	SampleX(SamplePt* pt);								// ROM 0x0021e638 SampleX__FP8SamplePt
Fixed	SampleY(SamplePt* pt);								// ROM 0x0021f8e4 SampleY__FP8SamplePt
ULong	SampleP(SamplePt* pt);								// ROM 0x0021fb7c SampleP__FP8SamplePt - the pressure
void	SetSampleX(SamplePt* pt, Fixed x);					// ROM 0x00220644 SetSampleX__FP8SamplePtl
void	SetSampleY(SamplePt* pt, Fixed y);					// ROM 0x002207cc SetSampleY__FP8SamplePtl
void	GetPoint(SamplePt* pt, FPoint* fpt);				// ROM 0x0021f8f8 GetPoint__FP8SamplePtP6FPoint
void	SetPoint(SamplePt* pt, FPoint* fpt);				// ROM 0x0021f924 SetPoint__FP8SamplePtP6FPoint
ULong	TestFlag(SamplePt* pt, ULong flag);					// ROM 0x0021f990 TestFlag__FP8SamplePtUl
void	SetFlag(SamplePt* pt, ULong flag);					// ROM 0x0021f9a8 SetFlag__FP8SamplePtUl
void	UnsetFlag(SamplePt* pt, ULong flag);				// ROM 0x0021f9d8 UnsetFlag__FP8SamplePtUl

// the stroke flags
enum
{
	kStrokeDone			= 0x40000000,
	kStrokeNoInk		= 0x20000000,
	kStrokeDrawnWhenDone = 0x08000000,
	kStrokeDrawn		= 0x04000000
};

void	AddPtToRect(const FPoint* pt, FRect* rect, Boolean first);	// ROM 0x001a66f4 AddPtToRect - the rect grown to the point (set to it when first)
void	SetRectangleEmpty(FRect* rect);						// ROM 0x001a6934 SetRectangleEmpty
void	SetRectanglePoint(FRect* rect, const FPoint* pt);	// ROM 0x001a694c SetRectanglePoint
void	RectangleCenter(const FRect* rect, FPoint* center);	// ROM 0x001a66a8 RectangleCenter
void	UnfixRect(const FRect* src, Rect* dst);				// ROM 0x001a64c4 UnfixRect - rounded to pixels
void	GetMapper(const FRect* src, const FRect* dst);		// ROM 0x001a67a4 GetMapper - the dst rect kept in the src rect's proportions
void	MapPoint(FPoint* pt, const FRect* src, const FRect* dst);	// ROM 0x001a6864 MapPoint - the point moved from the src rect to the dst rect

class TStroke : public TDArray
{
public:
	static TStroke*	Make(ULong count);						// ROM 0x0021fa0c Make__7TStrokeSFUl
	long			IStroke(ULong count);					// ROM 0x0021fa7c IStroke__7TStrokeFUl
	virtual void	IDispose(void);							// ROM 0x0021fb28 IDispose__7TStrokeFv
	virtual long	SizeInBytes(void);						// ROM 0x0021fb78 SizeInBytes__7TStrokeFv

	void			Bifurcate(void);						// ROM 0x0021fba8 Bifurcate__7TStrokeFv - every other point dropped, the decimation doubled
	SamplePt*		TryToAddPoint(void);					// ROM 0x0021fc3c TryToAddPoint__7TStrokeFv - a point added (bifurcating when memory is short)
	virtual long	AddPoint(TabPt* pt);					// ROM 0x0021fc78 AddPoint__7TStrokeFP5TabPt - ==> 0, or 1 for no memory
	void			EndStroke(void);						// ROM 0x0021ff34 EndStroke__7TStrokeFv - done, compacted
	SamplePt*		GetPoint(long index);					// ROM 0x0021ff78 GetPoint__7TStrokeFl
	void			GetTabPt(long index, TabPt* pt);		// ROM 0x0021ff80 GetTabPt__7TStrokeFlP5TabPt
	void			GetFPoint(long index, FPoint* pt);		// ROM 0x00220010 GetFPoint__7TStrokeFlP6FPoint
	void			Rotate(long angle);						// ROM 0x00220058 Rotate__7TStrokeFl (NOT YET)
	void			Scale(long sx, long sy);				// ROM 0x0022016c Scale__7TStrokeFlT1 (NOT YET)
	void			Draw(void);								// ROM 0x002202b0 Draw__7TStrokeFv (NOT YET: the inker; the flags are set)
	void			Map(FRect* dst);						// ROM 0x00220424 Map__7TStrokeFP5FRect - the points moved from the box to the rect
	void			Offset(long dx, long dy);				// ROM 0x00220550 Offset__7TStrokeFlT1
	void			UpdateBBox(void);						// ROM 0x0022067c UpdateBBox__7TStrokeFv
	Boolean			Done(void);								// ROM 0x0022071c Done__7TStrokeFv

	FRect			fBBox;			// +0x20  the points' box (its right and bottom a fixed unit past them)
	long			fSampleRate;	// +0x30  halved by Bifurcate
	ULong			fDownTime;		// +0x34
	ULong			fUpTime;		// +0x38
	long			fUnused3c;		// +0x3c
	long			fUnused40;		// +0x40
	UShort			fDecimation;	// +0x44  every n-th point kept (1: all)
	UShort			fDecimationCount;	// +0x46  points since the last kept
	long			fClickEvent;	// +0x48  the click event noted in it (Unit.h: kTapClick...; kProcessedClick once handled)
};

// the inker's lock on the stroke being drawn (the host has no inker task)
Boolean	AcquireStroke(TStroke* stroke);						// ROM 0x001fcdf0 AcquireStroke__FP7TStroke - ==> whether it was taken (to release)
void	ReleaseStroke(void);								// ROM 0x001fce28 ReleaseStroke__Fv
void	GetStrokeRect(TStroke* stroke, Rect* rect);			// ROM 0x001a5bd8 GetStrokeRect__FP7TStrokeP5TRect - the box in pixels, at least a pixel each way
void	AdjustForInk(Rect* rect);							// ROM 0x0022b6cc AdjustForInk__FP5TRect - let out for the pen size

class TStrokePublic
{
public:
	static TStrokePublic*	Make(TStroke* stroke, Boolean owns);	// ROM 0x0014727c Make__13TStrokePublicSFP7TStrokeUc
					TStrokePublic(TStroke* stroke, Boolean owns);	// ROM 0x0014728c __ct__13TStrokePublicFP7TStrokeUc
					~TStrokePublic();						// ROM 0x001472e8 __dt__13TStrokePublicFv (the stroke disposed when owned)
	Boolean			Done(void);								// ROM 0x00147324 Done__13TStrokePublicFv
	long			Size(void);								// ROM 0x0014732c Size__13TStrokePublicFv - the points
	ULong			DownTime(void);							// ROM 0x00147338 DownTime__13TStrokePublicFv
	ULong			UpTime(void);							// ROM 0x00147344 UpTime__13TStrokePublicFv
	void			Bounds(Rect* rect);						// ROM 0x0014740c Bounds__13TStrokePublicFP5TRect - the box in pixels, a pixel wider and taller
	Point			GetPoint(long index);					// ROM 0x0014745c GetPoint__13TStrokePublicFl - rounded (the last for an index past the end)
	Point			FirstPoint(void);						// ROM 0x00147504 FirstPoint__13TStrokePublicFv
	Point			FinalPoint(void);						// ROM 0x0014750c FinalPoint__13TStrokePublicFv
	void			InkOn(void);							// ROM 0x0014753c InkOn__13TStrokePublicFv (nothing)
	void			InkOff(Boolean invalidate, Boolean hobbled);	// ROM 0x00147540 InkOff__13TStrokePublicFUcT1
	void			InkOff(Boolean invalidate);				// ROM 0x00147620 InkOff__13TStrokePublicFUc
	void			GetInkedRect(Rect* rect);				// ROM 0x0014762c GetInkedRect__13TStrokePublicFP5TRect
	void			Invalidate(void);						// ROM 0x00147674 Invalidate__13TStrokePublicFv

	TStroke*		Stroke(void)		{ return fStroke; }

	long			fUnused;		// +0x00  0
	TStroke*		fStroke;		// +0x04
	Boolean			fOwnsStroke;	// +0x08
	Rect			fInkedRect;		// +0x0c  where the ink was (top -0x8000: not known yet)
};

#endif	/* __STROKE_H */
