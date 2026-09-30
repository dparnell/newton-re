/*
	File:		views/PolygonView.h

	Contains:	TPolygonView (clPolygonView, 82): a shape or a sketch on a
				page - what the shape recogniser's cleaned-up shapes and
				the ink nobody reads become.  Its `points` slot is a
				'polygonShape binary (PolygonShape): a verb, a count and
				the points, relative to the view's top left; the verb says
				whether the points are joined by lines (a polygon, a
				polyline, a curve already flattened into points), stand for
				an oval or an arc drawn in the view's `arcerBounds`, or -
				verb 14 - nothing at all, the view drawing its `ink` slot
				instead.  The pen is the viewFormat's pen-size nibble.

				The ROM's object is 0x30 bytes, a TDataView with nothing of
				its own.

				The selection (TPolygonHilite) is a copy of the points it
				covers - from point fFirst, fFirstPart of the way along
				the segment after it, to point fLast, fLastPart along the
				one before it (16.16; nought and 0x10000 are the points
				themselves) - with its own verb and pen.  A stroke over the
				shape selects the whole of it (TView::HandleHilite ->
				HiliteAll -> MakeHilite, or MakeInkHilite for ink), and a
				wholly selected shape of straight sides answers
				ClickOptions bit 2, so a page lets its corners be dragged
				(TEditView::TrackDistort, which moves each through the
				undoable command 0x43).  The selection is drawn as a thick
				black line over the shape (DrawHilites off) with white dots
				at the corners (on).

				A stroke traced along part of the shape selects that part
				(HiliteTraced, PolygonTraced.cpp), and deleting a part
				cuts it out (RemovePoints: what follows made a shape of its
				own, what precedes kept through the undoable points
				command 0x44).

				A scrub over most of the shape removes it; one over a run of
				its sides takes those out (HandleScrub, ScrubSegment,
				HitSegment, over RemovePoints).

				A shape is dragged as a 'polygon item (an 'ink one for ink),
				the dropped data a shape view's form of the selection
				(AddDragInfo, GetDropData, DropRemove).

				A resized selection maps the points (or the ink) from one
				rectangle onto the other (Scale, ScaleInk, DrawScaledData).

				A double tap on ink reads it again (RealDoCommand's
				aeDoubleTap, 0x32).

				On a printer's port the ink verb's strokes are made into
				outlined paths and framed (InkMakePaths, FramePaths).

	Reconstructed from the MP2x00 US ROM (0x0018b54c-0x00191900); each
	function cites its origin.
*/

#ifndef __POLYGONVIEW_H
#define __POLYGONVIEW_H

#ifndef __DATAVIEW_H
#include "DataView.h"
#endif

#ifndef __TRANSFORM_H
#include "Transform.h"
#endif

#ifndef __HILITES_H
#include "Hilites.h"
#endif

// the verbs of a PolygonShape (the shape recogniser's shape types, as
// TEditView::HandleShape hands them on)
enum
{
	kPolyOval			= 0,		// an oval: drawn in the view's arcerBounds
	kPolyArc			= 13,		// an arc of one, from the first point to the last
	kPolyInk			= 14		// no points: the view's ink slot (DrawShape.h: kInkVerb)
};

// A 'polygonShape binary's data: the verb and the count, then the points.
// (Host: the shorts and the points are in the host's order - see
// MakePolygonForm.)
struct PolygonShape
{
	short		fVerb;				// +0x00
	short		fCount;				// +0x02
	Point		fPoints[1];			// +0x04  fCount of them

	void		Scale(const Rect& from, const Rect& to);	// ROM 0x001911b4 Scale__12PolygonShapeFRC5TRectT1
	void		Offset(long dh, long dv);					// ROM 0x00191210 Offset__12PolygonShapeFlT1
	void		CalcBounds(Rect* bounds);					// ROM 0x0019127c CalcBounds__12PolygonShapeFP5TRect
	Boolean		IsCurvy(void);								// ROM 0x001912e0 IsCurvy__12PolygonShapeFv
	Boolean		IsOval(void);								// ROM 0x00191348 IsOval__12PolygonShapeFv
};

// A polygon's selection: its own copy of the points it covers, NewPtr'd
// behind it (ROM 0x24 bytes).
class TPolygonHilite : public THilite
{
public:
					TPolygonHilite(long count);				// ROM 0x00191364 __ct__14TPolygonHiliteFl - room for so many points
	virtual			~TPolygonHilite();						// ROM 0x001914ac __dt__14TPolygonHiliteFv

	virtual THilite* Clone(void);							// ROM 0x001913f0 Clone__14TPolygonHiliteFv
	virtual void	UpdateBounds(void);						// ROM 0x001914f4 UpdateBounds__14TPolygonHiliteFv - the points' box, grown by four (and the pen's four) - none for ink
	virtual Boolean	Overlaps(const Rect& r);				// ROM 0x00191568 Overlaps__14TPolygonHiliteFRC5TRect (the base's)
	virtual Boolean	Encloses(const Point& pt);				// ROM 0x0019156c Encloses__14TPolygonHiliteFRC6TPoint - on a side, within sixteen; a curved shape by its box
	void			CopyFrom(THilite* other);				// ROM 0x0019144c CopyFrom__14TPolygonHiliteFP7THilite

	long			fFirst;			// +0x0c  the first point
	long			fLast;			// +0x10  the last
	long			fFirstPart;		// +0x14  how far along the segment after fFirst it starts (16.16)
	long			fLastPart;		// +0x18  how far along the one before fLast it ends
	long			fPenSize;		// +0x1c
	PolygonShape*	fShape;			// +0x20  the points it covers (a verb and a count, then the points)
};

class TPolygonView : public TDataView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x0018de14 ClassID__12TPolygonViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x0018e700 DerivedFrom__12TPolygonViewCFl
	virtual void	RealDraw(Rect& bounds);								// ROM 0x0018c684 RealDraw__12TPolygonViewFR5TRect
	virtual Boolean	RealDoCommand(RefArg cmd);
	virtual void	OuterBounds(Rect* bounds);							// ROM 0x0018b67c OuterBounds__12TPolygonViewFP5TRect - grown by the pen at the bottom right, and by four all round while selected							// ROM 0x0018ffbc RealDoCommand__12TPolygonViewFRC6RefVar (partial: see the definition)

	// the selection
	virtual void	DrawHilitedData(void);								// ROM 0x0018c6ec DrawHilitedData__12TPolygonViewFv
	virtual long	HandleHilite(TUnitPublic* unit, long kind, Boolean reallyDoIt);	// ROM 0x001912ec HandleHilite__12TPolygonViewFP11TUnitPubliclUc
	virtual long	HandleScrub(const Rect& bounds, long kind, TUnitPublic* unit, Boolean reallyDoIt);	// ROM 0x00191798 HandleScrub__12TPolygonViewFRC5TRectlP11TUnitPublicUc
	virtual void	DrawHilites(Boolean on);							// ROM 0x0018c7d4 DrawHilites__12TPolygonViewFUc
	virtual Boolean	IsCompletelyHilited(RefArg hilite);					// ROM 0x0018b5c8 IsCompletelyHilited__12TPolygonViewFRC6RefVar
	virtual void	HiliteAll(void);									// ROM 0x0019184c HiliteAll__12TPolygonViewFv
	virtual void	DeleteHilited(RefArg hilite);						// ROM 0x0018be20 DeleteHilited__12TPolygonViewFRC6RefVar
	virtual void	RemoveHilite(RefArg hilite);						// ROM 0x0018de1c RemoveHilite__12TPolygonViewFRC6RefVar
	virtual void	GlobalHiliteResizeBounds(Rect* bounds);				// ROM 0x0018b54c GlobalHiliteResizeBounds__12TPolygonViewFP5TRect
	virtual long	ClickOptions(void);									// ROM 0x0018b554 ClickOptions__12TPolygonViewFv - 1, 2 when the whole shape is selected, 4 for straight sides
	virtual TView*	AddHilited(RefArg hilite, class TEditView* editor);
	// drag and drop: a shape is dragged as a 'polygon (or an 'ink) item
	// whose data is a shape view's form of what is selected
	virtual Boolean	AddDragInfo(TDragInfo* dragInfo);					// ROM 0x0018b8dc AddDragInfo__12TPolygonViewFP9TDragInfo
	virtual Ref		GetDropData(RefArg dragType, RefArg dragRef);		// ROM 0x0018b96c GetDropData__12TPolygonViewFRC6RefVarT1
	virtual Boolean	DropRemove(RefArg dragRef);							// ROM 0x0018bdc8 DropRemove__12TPolygonViewFRC6RefVar
	// the page's selection resized: the points (or the ink) mapped from
	// one rectangle onto the other, and the box they reach drawn into
	virtual void	Scale(const Rect& src, const Rect& dst);			// ROM 0x0018d8cc Scale__12TPolygonViewFRC5TRectT1
	virtual void	DrawScaledData(const Rect& src, const Rect& dst, Rect* bounds);	// ROM 0x0018cc70 DrawScaledData__12TPolygonViewFRC5TRectT1P5TRect	// ROM 0x0018b700 AddHilited__12TPolygonViewFRC6RefVarP9TEditView

	void			CalcHiliteBounds(PolygonShape* shape, Rect* bounds);	// ROM 0x0018bee0 CalcHiliteBounds__12TPolygonViewFP12PolygonShapeP5TRect
	void			MakeHilite(long first, long startPart, long last, long endPart);	// ROM 0x001909a4 MakeHilite__12TPolygonViewFlN31
	void			MakeInkHilite(void);								// ROM 0x00190d70 MakeInkHilite__12TPolygonViewFv
	void			AddHilite(TPolygonHilite* hilite);					// ROM 0x0018dcc8 AddHilite__12TPolygonViewFP14TPolygonHilite
	void			PolygonHiliteChanged(void);							// ROM 0x0018ddc8 PolygonHiliteChanged__12TPolygonViewFv
	void			UpdateBounds(PolygonShape* shape);					// ROM 0x00190ea0 UpdateBounds__12TPolygonViewFP12PolygonShape - the view fitted round its points again
	void			SetArcBounds(const Rect& bounds);					// ROM 0x0018c070 SetArcBounds__12TPolygonViewFRC5TRect
	void			SetPenSize(long pen);
	// A command to the view carrying `count` points (its 'points slot, a
	// 'polygonShape binary of the points alone).
	Ref				MakePointsCommand(ULong id, long count);			// ROM 0x0018d0d4 MakePointsCommand__12TPolygonViewFUll
	// A scrub over part of the shape: the stretch it covers (HitSegment)
	// taken out, and the view removed when that was all of it.  ==> 3 when
	// the scrub is taken (a curve's stretch must be over thirty pixels).
	long			ScrubSegment(const Rect& bounds, Boolean reallyDoIt);	// ROM 0x0018cd38 ScrubSegment__12TPolygonViewFRC5TRectUc
	// The run of segments a scrub's box covers more than half of: ==> false
	// unless it is one run (on a closed shape, one that may go round the
	// join), with its first and last points.
	Boolean			HitSegment(const Rect& bounds, long* first, long* last);	// ROM 0x0018cec4 HitSegment__12TPolygonViewFRC5TRectPlT2
	// Whether the hilite stroke was traced along the shape; with
	// `reallyDoIt` the part traced is selected (PolygonTraced.cpp).
	Boolean			HiliteTraced(TUnitPublic* unit, Boolean reallyDoIt);	// ROM 0x0018fa3c HiliteTraced__12TPolygonViewFP11TUnitPublicUc								// ROM 0x001910c4 SetPenSize__12TPolygonViewFl
	// The points from `first` (startPart along) to `last` (endPart
	// along) taken out.  ==> whether that was the whole shape, which the
	// caller then removes (partial: see the definition).
	Boolean			RemovePoints(Point* from, Point* to, long first, long last, long startPart, long endPart);	// ROM 0x0018d144 RemovePoints__12TPolygonViewFP6TPointT1lN33

	// The shape drawn.  `from` and `to`, when given, stand in for the
	// first and the last point - which is how a shape being dragged by
	// one end is drawn - and the segments they touch are drawn clipped
	// to the box the old segment filled.
	void			DrawData(PolygonShape* shape, Point* from, Point* to);	// ROM 0x0018c1ac DrawData__12TPolygonViewFP12PolygonShapeP6TPointT2
	// The box an oval or an arc is drawn in: the arcerBounds slot moved
	// to the view's place, or the view's bounds when there is none.
	void			GetArcBounds(Rect& bounds);							// ROM 0x0018bfc4 GetArcBounds__12TPolygonViewFR5TRect
	Ref				Points(void);										// ROM 0x00191088 Points__12TPolygonViewFv
	long			GetPenSize(void);									// ROM 0x001910b4 GetPenSize__12TPolygonViewFv
};

// The start of an arc and how far it goes, anticlockwise being negative,
// from the points it runs between.
void	CalcArcAngles(const Rect& box, Point from, Point to, long* start, long* arc);	// ROM 0x0018c148 CalcArcAngles__FRC5TRect6TPointT2PlT4
// The angle of a point about the middle of a rectangle, in whole degrees
// clockwise from twelve o'clock, as QuickDraw measures an arc: the
// rectangle's aspect is taken out first, so a corner is at 45 degrees
// however long the rectangle is.
void	PtToAngle(const Rect* box, Point pt, long* angle);	// ROM 0x002aa5b8 PtToAngle__FP4Rect5PointPl

// Whether the pen at `pt` is within `slop` of the segment from `a` to `b`
// (its ends widened by the slop along the longer axis): ==> how far along
// it the pen is, 16.16 (0 at `a`, 0x10000 at `b`), or 0x80000000 for not
// near it.
Fixed	LineHitRatio(const Point& pt, const Point& a, const Point& b, long slop);	// ROM 0x00198f74 LineHitRatio__6TPointCFRC6TPointT1l
// Ink stretched by the two 16.16 factors: each stroke's samples scaled
// about nought.
Ref		ScaleInk(RefArg ink, Fixed sx, Fixed sy);			// ROM 0x0018d7a8 ScaleInk__FRC6RefVarlT2
// Whether a shape of the verb closes on itself (PolygonTraced.cpp).
Boolean	IsClosed(long verb);								// ROM 0x0018e644 IsClosed__Fl
// A shape's verb made to agree with its points: none is nothing (15),
// one a dot (3), two a line (8, unless an arc), four closing on
// themselves a rectangle (9); a closed verb whose points do not close is
// the open one.  The rectangle is checked against the points' box, and
// nothing comes of it (see the definition).
void	ValidatePoly(RefArg points, const Rect& bounds);	// ROM 0x0018b2c8 ValidatePoly__F7DataPtrRC5TRect
// The 'points slot of a points command.
void	CommandSetPoints(RefArg cmd, RefArg points);		// ROM 0x0018bfb4 CommandSetPoints__FRC6RefVarT1
Ref		CommandPoints(RefArg cmd);							// ROM 0x0018cd1c CommandPoints__FRC6RefVar
// Whether the first point of one range comes before the last of another.
Boolean	LessOrEq(long first, long startPart, long last, long endPart);	// ROM 0x0018ccf0 LessOrEq__FlN31
// The pen-size palette (the root's stylePalette), when it is showing,
// told the selection changed.
void	UpdatePenSizePalette(void);						// ROM 0x0018dd44 UpdatePenSizePalette__Fv

// A point and a rectangle moved onto a grid of the given spacing.
void	AlignPtToGrid(Point* pt, Point& spacing);			// ROM 0x002628f8 AlignPtToGrid__FP6TPointR6TPoint
void	AlignRectToGrid(Rect* rect, Point& spacing);		// ROM 0x00262988 AlignRectToGrid__FP5TRectR6TPoint

// What an edit view's children are drawn through while the selection is
// being resized; not set up (so it changes nothing) the rest of the time.
extern TTransform	gEditViewTransform;		// ROM 0x0c100cc4 gEditViewTransform

#endif	/* __POLYGONVIEW_H */
