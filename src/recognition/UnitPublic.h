/*
	File:		recognition/UnitPublic.h

	Contains:	TUnitPublic, the face of a unit the view system and
				NewtonScript see (the unit natives take one as an address
				ref): the unit's type, its bounds in pixels (the stroke's for
				the click and stroke recognisers' units, else the unit's
				box, a pixel wider and taller), its times, whether it is a
				tap, its stroke as a TStrokePublic (made once), the view
				under it (found once per flags mask and remembered), the
				view's recognition mask, and the invalidation of what its
				strokes inked; the gesture units' caret type and angle from
				their interpretations; the word units' word list, word info
				frame and base line; the shape units' polygons.  The ROM's
				TUnitPublic is 0x3c bytes.

				NOT YET RECONSTRUCTED: the word side (MakeWordList,
				ExtractWords, Word, Words, WordScore, WordInfo, SetWordBase,
				Strokes, TrainingData: TWordList, the dictionaries), the
				shape side (RoughShape, CleanShape, ShapeType: AsPolygon,
				TGeneralShapeUnit), GesturePoint (the gesture unit's
				points), the arbiter's whole-screen mode that FindView looks
				at (gArbiter), and EndTime's controller stroke (the unit's
				own end time is used).

	Reconstructed from the MP2100 D ROM (0x0022a688-0x0022b600); each
	function cites its origin.
*/

#ifndef __UNITPUBLIC_H
#define __UNITPUBLIC_H

#include "Unit.h"
#include "objects.h"

class TView;
class TWordList;

class TUnitPublic
{
public:
						TUnitPublic(TUnit* unit, ULong unused);	// ROM 0x0022a688 __ct__11TUnitPublicFP5TUnitUl
						~TUnitPublic();							// ROM 0x0022a718 __dt__11TUnitPublicFv (the stroke face, the polygons, the word list and the word info gone)

	ULong				GetType(void);							// ROM 0x0022af18 GetType__11TUnitPublicFv
	ULong				StartTime(void);						// ROM 0x0022b1d4 StartTime__11TUnitPublicFv
	ULong				EndTime(void);							// ROM 0x0022b308 EndTime__11TUnitPublicFv - the end of the last stroke it covers
	ULong				ContextID(void);						// ROM 0x0022a944 ContextID__11TUnitPublicFv
	void				Bounds(Rect* rect);						// ROM 0x0022b4c8 Bounds__11TUnitPublicFP5TRect
	Boolean				IsTap(void);							// ROM 0x0022b424 IsTap__11TUnitPublicFv - the bounds under 6 pixels each way
	TStrokePublic*		Stroke(void);							// ROM 0x0022b47c Stroke__11TUnitPublicFv - the first stroke's face, made once; nil for no stroke
	TView*				FindView(ULong flags);					// ROM 0x0022b1e0 FindView__11TUnitPublicFUl - the view under the bounds' centre with the flags (then within 10 pixels), remembered
	void				SetViewHit(TView* view, ULong flags);	// ROM 0x0022b340 SetViewHit__11TUnitPublicFP5TViewUl
	ULong				InputMask(void);						// ROM 0x0022b34c InputMask__11TUnitPublicFv - the recognition bits of the view found with the required mask
	ULong				RequiredMask(void);						// ROM 0x0022b3c8 RequiredMask__11TUnitPublicFv - the recogniser's enabled services (strokes take the shape and word bits along, gestures the clicks)
	void				Invalidate(void);						// ROM 0x0022b59c Invalidate__11TUnitPublicFv - what the strokes inked given to the root view to redraw
	void				Cleanup(void);							// ROM 0x0022b388 Cleanup__11TUnitPublicFv - a click's ink taken off
	long				CaretType(void);						// ROM 0x0022a780 CaretType__11TUnitPublicFv - the first interpretation's label when it is a caret kind (2, 3, 5, 6), else 0
	long				GestureAngle(void);						// ROM 0x0022a870 GestureAngle__11TUnitPublicFv - the first interpretation's angle, snapped to 0, 90, -90, 180 or 135 within 20 degrees (30 for label 5)

	// NOT YET RECONSTRUCTED
	Point				GesturePoint(void);						// ROM 0x0022a7d4 GesturePoint__11TUnitPublicFl
	Handle				RoughShape(void);						// ROM 0x0022a950 RoughShape__11TUnitPublicFv
	Handle				CleanShape(void);						// ROM 0x0022a990 CleanShape__11TUnitPublicFv
	ULong				ShapeType(void);						// ROM 0x0022a9ec ShapeType__11TUnitPublicFv
	TWordList*			MakeWordList(Boolean, Boolean);			// ROM 0x0022aa20 MakeWordList__11TUnitPublicFUcT1
	Ref					WordInfo(void);							// ROM 0x0022af24 WordInfo__11TUnitPublicFv
	void				ExtractWords(void);						// ROM 0x0022af64 ExtractWords__11TUnitPublicFv
	Handle				Word(void);								// ROM 0x0022af98 Word__11TUnitPublicFv
	ULong				WordScore(void);						// ROM 0x0022afbc WordScore__11TUnitPublicFv
	TWordList*			Words(void);							// ROM 0x0022afe0 Words__11TUnitPublicFv
	void				SetWordBase(void);						// ROM 0x0022b004 SetWordBase__11TUnitPublicFv
	Ref					Strokes(void);							// ROM 0x0022b110 Strokes__11TUnitPublicFv
	Ref					TrainingData(void);						// ROM 0x0022b154 TrainingData__11TUnitPublicFv

	TUnit*				fUnit;			// +0x00
	TWordList*			fWordList;		// +0x04
	ULong				fUnused08;		// +0x08
	ULong				fUnused0c;		// +0x0c
	ULong				fUnused10;		// +0x10
	Handle				fCleanShape;	// +0x14  a polygon
	Handle				fRoughShape;	// +0x18  a polygon
	TStrokePublic*		fStroke;		// +0x1c
	ULong				fUnused20;		// +0x20
	ULong				fUnused24;		// +0x24
	Rect				fWordBase;		// +0x28  the word's base line box (top -0x8000: not known yet)
	RefHandle*			fWordInfo;		// +0x30  the word info frame (a heap RefHandle; nil until made)
	TView*				fViewHit;		// +0x34
	ULong				fViewHitFlags;	// +0x38  the flags it was found with
};

// the NewtonScript side (UnitNatives.cpp)
TUnitPublic*	UnitFromRef(RefArg unit);					// ROM 0x001ec718 UnitFromRef__FRC6RefVar - the unit a script argument stands for (a throw for nil)
TStrokePublic*	StrokeFromRef(RefArg unit);					// ROM 0x001ec750 StrokeFromRef__FRC6RefVar - its stroke's face
void	RegisterUnitNatives(void);							// the unit functions bound (GetPoint, GetPointsArray, StrokeDone, StrokeBounds, InkOff, ...)

#endif	/* __UNITPUBLIC_H */
