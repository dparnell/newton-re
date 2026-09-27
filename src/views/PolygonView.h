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

				NOT YET RECONSTRUCTED: the rest of TPolygonView - the
				hilites and their dragging (CalcHiliteBounds, MakeHilite,
				HiliteTraced, DrawHilites, ...), scrubbing, scaling, the
				drag and drop and RealDoCommand - and the printing path of
				the ink verb (InkMakePaths, FramePaths).

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

class TPolygonView : public TDataView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x0018de14 ClassID__12TPolygonViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x0018e700 DerivedFrom__12TPolygonViewCFl
	virtual void	RealDraw(Rect& bounds);								// ROM 0x0018c684 RealDraw__12TPolygonViewFR5TRect
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x0018ffbc RealDoCommand__12TPolygonViewFRC6RefVar (partial: see the definition)

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

// A point and a rectangle moved onto a grid of the given spacing.
void	AlignPtToGrid(Point* pt, Point& spacing);			// ROM 0x002628f8 AlignPtToGrid__FP6TPointR6TPoint
void	AlignRectToGrid(Rect* rect, Point& spacing);		// ROM 0x00262988 AlignRectToGrid__FP5TRectR6TPoint

// What an edit view's children are drawn through while the selection is
// being resized; not set up (so it changes nothing) the rest of the time.
extern TTransform	gEditViewTransform;		// ROM 0x0c100cc4 gEditViewTransform

#endif	/* __POLYGONVIEW_H */
