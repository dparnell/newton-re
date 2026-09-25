/*
	File:		recognition/ShapeDomain.h

	Contains:	The shape domain ('GSHP', "GeneralShape Domain"): what makes
				a drawn line, box, triangle, circle or curve into a clean
				shape.  It sits beside the word domain above the strokes,
				and its units (TGeneralShapeUnit) collect the strokes of
				one drawing - a box drawn as four lines is one unit of four
				subs - until the drawing is closed or nothing more joins
				it.

				Grouping.  A new stroke joins the shape being drawn when
				one of its ends lands near a free end of that shape
				(CheckConnect); a closed stroke (its ends within a fifth of
				its size, CheckClosed) is a shape on its own.  The shape
				keeps which way round each of its strokes runs and in what
				order they join (ShapeGroupInfo), and it also looks at the
				shapes already on the page - the *context units*, which the
				page makes out of its polygon views when asked
				(HandleGetContextUnits, command 0x14) - so that a line
				drawn to the corner of a box, or to a point on a circle,
				ends exactly there ("gravity": CheckConnect,
				CheckPtOnShape, CheckPtOnCircle).

				Classifying (TGeneralShapeDomain::Classify) finds the
				shape's key points and fits it (FindKeyPoints), decides
				whether it is really an ellipse (FindEllipses), and
				otherwise tidies it by writing what it looks like it
				should be - these two sides parallel, those angles equal -
				as a system of equations and solving it (FindEquations,
				SolveEquations, PlugNewVals); then it snaps the ends that
				touched the page's other shapes onto them (SnapPtToLC,
				GlobalTrends).  The label it leaves is the shape's type
				(ShapeType below), which TEditView::HandleShape hands on as
				the polygon view's verb.

	Reconstructed from the MP2x00 US ROM (0x0020fae8-0x002265d0 and
	0x0022bbb8-0x0022c780, the view side at 0x00144530-0x00144790); each
	function cites its origin.
*/

#ifndef __SHAPEDOMAIN_H
#define __SHAPEDOMAIN_H

#ifndef __UNIT_H
#include "Unit.h"
#endif

#ifndef __DOMAIN_H
#include "Domain.h"
#endif

#ifndef __RECOGNIZER_H
#include "Recognizer.h"
#endif

#ifndef __FIXEDGEOMETRY_H
#include "FixedGeometry.h"
#endif

class TUnitPublic;
struct PolygonShape;

// A shape's type, which is its unit's label (the ROM's GSType).  The
// names are what the types are used as; where that is not yet settled
// the number stands alone.
enum
{
	kShapeCircle		= 0,		// the interpretation's params: centre x, y, diameter
	kShapeEllipse		= 1,		// centre x, y, the two radii, the angle
	kShapeCurve			= 2,
	kShapeNone			= 3,		// nothing a shape can be made of
	kShapeClosed		= 4,		// a closed polygon (a stroke whose ends meet)
	kShapeOpen			= 5,		// an open polyline
	kShapeClosedCurve	= 6,		// closed, and not yet drawn as more than one stroke
	kShapeGrouping		= 7,		// what a unit is while strokes are still joining it
	kShapeOpenCurve		= 7,		// (and what FindEquations calls an open shape with curves in it)
	kShapeLine			= 8,		// straightened; the angle is its direction from the vertical
	kShapeTriangle		= 9,
	kShapeSquare		= 10,		// (or a rhombus: four sides of one length)
	kShapeRectangle		= 11,		// (or a parallelogram)
	kShapeQuadrilateral	= 12,		// four sides with nothing to solve
	kShapeArc			= 13,
	kShapeNothing		= 15		// the classifier gave up
};

// One point of a shape's outline (the ROM's GeneralPt, 12 bytes): a point
// of the fitted shape, and whether it is the control point of a curve
// rather than a corner (GetGSAsStroke draws a quadratic through it).
struct GeneralPt
{
	FPoint		fPt;			// +0x00
	UByte		fControl;		// +0x08  a curve's control point
	UByte		f09;			// +0x09
	UByte		f0a;			// +0x0a
	UByte		fPad;
};

// Where one of a shape's two loose ends meets something else - another
// shape on the page, or (while grouping) the shape it is being joined to.
struct ShapeEnd
{
	TUnit*		fUnit;			// +0x00  what it meets (nil: nothing)
	long		fPoint;			// +0x04  the point of that unit's outline it meets at
	long		fKind;			// +0x08  -1 nothing, 0 on a line, 1 on a corner,
								//		  2 on an open end, 3 on a closed shape's vertex,
								//		  4 on a circle, -2 let go (the join has moved)
	long		fDist;			// +0x0c  how far the end was from it
};

// What a shape unit keeps while it is being put together (the ROM's
// 0x7c-byte block at +0x40, the "FD"; freed when the unit is ended).
struct ShapeGroupInfo
{
	UByte		fOrder[8];		// +0x00  the subs in the order they join end to end
								//		  (fOrder[0] becomes 0xff once Classify has snapped the shape)
	UByte		fReversed[8];	// +0x08  whether each sub runs backwards along that order
	UByte		f10[0x40];		// +0x10
	long		f50[2];			// +0x50  (-1)
	long		fConnections;	// +0x58  how many of the two ends meet something
	ShapeEnd	fEnds[2];		// +0x5c  the first end and the last
};

// A shape unit's one interpretation (0x2c bytes).
struct ShapeInterpretation
{
	UnitInterpretation	fBase;	// +0x00  label (the shape type), score, angle, param
	TDArray*	fShape;			// +0x10  the fitted outline, GeneralPts
	long		fParams[6];		// +0x14  a circle's or an ellipse's numbers (see the types)
};

class TGeneralShapeUnit : public TSIUnit
{
public:
	static TGeneralShapeUnit*	Make(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x00216668 Make__17TGeneralShapeUnitSFP7TDomainUlP6TArray
	long				IGeneralShapeUnit(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x002166e0 IGeneralShapeUnit__17TGeneralShapeUnitFP7TDomainUlP6TArray

	virtual void		Dump(TMsg* msg);						// ROM 0x00216f8c Dump__17TGeneralShapeUnitFP4TMsg (nothing)
	virtual long		SizeInBytes(void);						// ROM 0x00216fa0 SizeInBytes__17TGeneralShapeUnitFv
	virtual void		IDispose(void);							// ROM 0x00216e90 IDispose__17TGeneralShapeUnitFv
	virtual long		InterpretationCount(void);				// ROM 0x002167d4 InterpretationCount__17TGeneralShapeUnitFv
	virtual void		DoneUsingUnit(void);					// ROM 0x00217214 DoneUsingUnit__17TGeneralShapeUnitFv
	virtual ULong		ContextID(void);						// ROM 0x00216f90 ContextID__17TGeneralShapeUnitFv
	virtual void		SetContextID(ULong id);					// ROM 0x00216f98 SetContextID__17TGeneralShapeUnitFUl
	virtual long		AddInterpretation(char* interp);		// ROM 0x002167dc AddInterpretation__17TGeneralShapeUnitFPc (the one interpretation overwritten)
	virtual UnitInterpretation*	GetInterpretation(ULong index);	// ROM 0x002167c0 GetInterpretation__17TGeneralShapeUnitFUl
	virtual void		EndUnit(void);							// ROM 0x00216f6c EndUnit__17TGeneralShapeUnitFv (the grouping state let go)

	ShapeInterpretation*	Interpretation(void)	{ return (ShapeInterpretation*) GetInterpretation(0); }
	TDArray*			GetGeneralShape(void);					// ROM 0x0021730c GetGeneralShape__17TGeneralShapeUnitFv
	void				SetGeneralShape(TDArray* shape);		// ROM 0x00217354 SetGeneralShape__17TGeneralShapeUnitFP7TDArray
	void				NewInterpretation(TDArray* shape);		// ROM 0x00217398 NewInterpretation__17TGeneralShapeUnitFP7TDArray - the one interpretation cleared and given the shape
	// The shape as a stroke, which is what a view is handed (CleanShape):
	// an ellipse's 25 points, or the outline with its curves drawn out.
	TStroke*			GetGSAsStroke(void);					// ROM 0x0021682c GetGSAsStroke__17TGeneralShapeUnitFv
	TStroke*			GetEllipseAsStroke(void);				// ROM 0x00216abc GetEllipseAsStroke__17TGeneralShapeUnitFv

	ULong				fContextID;		// +0x3c
	ShapeGroupInfo*		fGroupInfo;		// +0x40
	long				fInterpCount;	// +0x44  0 or 1
	ShapeInterpretation	fInterp;		// +0x48
	long				fSnapped;		// +0x74  an end of it meets another shape (Group)
	long				fSnapDist;		// +0x78  the nearest of those ends
};

// The mean length of a shape's sides, to the pixel: of the outline for a
// fitted shape, of the box for a round one.
long	GetAvgLength(TGeneralShapeUnit* unit);					// ROM 0x0021708c GetAvgLength__FP17TGeneralShapeUnit
void	DisposeFD(TGeneralShapeUnit* unit);						// ROM 0x002171ac DisposeFD__FP17TGeneralShapeUnit - the grouping state and the context units it held let go
// A unit and everything under it disposed, whoever else might hold it
// (the flag that marks a context unit's fake subs is cleared first).
void	PurgeDeep(TSIUnit* unit);								// ROM 0x00213508 PurgeDeep__FP7TSIUnit
void	PurgeDeep(TUnitList* list);								// ROM 0x002142a0 PurgeDeep__FP9TUnitList - every unit in it (the list kept)
void	GDisposeShape(TDArray* shape);							// ROM 0x000dc840 GDisposeShape__FP7TDArray

// Whether a stroke closes on itself: its ends within CloseDelta and it
// has enough points to be a shape at all.
Boolean	CheckClosed(TStrokeUnit* stroke);						// ROM 0x00210860 CheckClosed__FP11TStrokeUnit
// How near two ends must be to count as meeting: a fifth of the larger
// side of the box, held between the minimum and maximum connect distances.
long	CloseDelta(TStrokeUnit* stroke);						// ROM 0x00211388 CloseDelta__FP11TStrokeUnit
// The two loose ends of a stroke, or of a shape (its first sub's start
// and its last sub's end, each taken the way round the sub runs; for a
// shape of several subs only the end that is still free).  They are
// written into two static points, which `ends` is pointed at.
void	ExtractEnds(TStrokeUnit* stroke, TGeneralShapeUnit* shape, FPoint** ends);	// ROM 0x00210e60 ExtractEnds__FP11TStrokeUnitP17TGeneralShapeUnitPP6FPoint
// Whether the ends meet the shape `target`'s free ends.  Mode 0 only
// asks; mode 1 is a stroke about to become `target`'s next sub, and
// records where it goes in the order; mode 2 is `shape` meeting a shape
// on the page, and records the join in `shape`'s ends.  ==> how many of
// the two ends meet (0, 1 or 2).
long	CheckConnect(long mode, FPoint** ends, TGeneralShapeUnit* shape, TGeneralShapeUnit* target);	// ROM 0x00210f48 CheckConnect__FlPP6FPointP17TGeneralShapeUnitT3
// The ends of `shape` against the outline of a polygon on the page, and
// against a circle on the page.
void	CheckPtOnShape(FPoint** ends, TGeneralShapeUnit* shape, TGeneralShapeUnit* context);	// ROM 0x00210a10 CheckPtOnShape__FPP6FPointP17TGeneralShapeUnitT2
void	CheckPtOnCircle(FPoint** ends, TGeneralShapeUnit* shape, TGeneralShapeUnit* context);	// ROM 0x002108d8 CheckPtOnCircle__FPP6FPointP17TGeneralShapeUnitT2
// Whether `pt` is within `slop` pixels of the segment from `a` to `b`:
// ==> 0 no, 1 near `a`, 2 near `b`, 3 on the line between; `dist` gets
// how far from it (the end, or the line).
long	PtOnLine2(FPoint* a, FPoint* b, FPoint* pt, long slop, long* dist);	// ROM 0x00210cbc PtOnLine2__FP6FPointN21lPl
// Whether `pt` is on the circle, nearer than *dist: *dist becomes how near.
Boolean	PtOnCircle(FPoint* pt, TGeneralShapeUnit* circle, long* dist);	// ROM 0x00215c08 PtOnCircle__FP6FPointP17TGeneralShapeUnitPl
// A circle's centre and diameter, out of its box.
void	CircleParams(TGeneralShapeUnit* circle, FPoint* centre, long* diameter);	// ROM 0x00211c44 CircleParams__FP17TGeneralShapeUnitP6FPointPl

// The shapes on the page near a unit, as units (nil for none): the
// routine the recognition manager installs (HandleGetContextUnits).
TUnitList*	GetContextUnits(TUnit* unit, long whole);			// ROM 0x00211c94 GetContextUnits__FP5TUnitl
void	DisposeContextUnits(TUnitList* list);					// ROM 0x002156b0 DisposeContextUnits__FP9TUnitList
typedef TUnitList* (*ContextUnitProc)(TUnit* unit, long whole);
void	SetContextUnitRoutine(ContextUnitProc proc);			// ROM 0x002105c0 SetContextUnitRoutine__FPFP5TUnitl_P9TUnitList

// The screen the domain works in and the distances it works with, in
// pixels scaled by the tablet's resolution against 72 dots to the inch,
// worked out again whenever the screen or the sampling rate changes.
void	CheckScreenGlobals(void);								// ROM 0x002105d0 CheckScreenGlobals__Fv

// the domain's switches (the ROM's initialised data: all on)
extern Boolean	gCurveFlag;										// ROM 0x0c1018b8 gCurveFlag - curves and ellipses are looked for
extern Boolean	gSymmetryFlag;									// ROM 0x0c1018bc gSymmetryFlag - shapes are tidied by their symmetries
extern Boolean	gGravityFlag;									// ROM 0x0c1018c0 gGravityFlag - shapes snap to the shapes on the page

// the domain's working globals (the ROM's 0x0c104c94 block)
extern FRect	gGSScreenRect;									// ROM 0x0c104c94 gGSScreenRect - the screen, let out by gPixScreenRectInset
extern long		gPixScreenRectInset;							// ROM 0x0c104ca4 gPixScreenRectInset
extern long		gPixMaxContextGravity;							// ROM 0x0c104ca8 gPixMaxContextGravity
extern Boolean	gGSOffScreen;									// ROM 0x0c104cb0 (unnamed) - SetGeneralPt was given a point off the screen
extern long		gGSInkLength;									// ROM 0x0c104cac (unnamed) - how long the last stroke measured was (RSmallDists)
extern long		gGSClosed;										// ROM 0x0c104cb4 (unnamed) - the shape was drawn closed (FindKeyPoints); a curve is looked at as an ellipse only then
extern long		gPixMaxCollapseSize;							// ROM 0x0c104cb8 gPixMaxCollapseSize
extern long		gPixMaxSmallDist;								// ROM 0x0c104cbc gPixMaxSmallDist
extern long		gPixMaxClosedDist;								// ROM 0x0c104cc0 gPixMaxClosedDist
extern long		gPixMaxConnectDist;								// ROM 0x0c104cc4 gPixMaxConnectDist
extern long		gPixMinConnectDist;								// ROM 0x0c104cc8 gPixMinConnectDist
extern long		gPixMinKinkDist;								// ROM 0x0c104ccc gPixMinKinkDist
extern long		gPixMinRLineOutTolerance;						// ROM 0x0c104cd0 gPixMinRLineOutTolerance
extern long		gPixMaxRLineOutTolerance;						// ROM 0x0c104cd4 gPixMaxRLineOutTolerance
extern long		gPixMaxAvgLenForSmallDists;						// ROM 0x0c104cd8 gPixMaxAvgLenForSmallDists
extern long		gPixMinAvgLenForSmallDists;						// ROM 0x0c104cdc gPixMinAvgLenForSmallDists
extern long		gPixSomeMagicThreshold;							// ROM 0x0c104ce0 gPixSomeMagicThreshold
extern long		gPixLargeInitialValue;							// ROM 0x0c104ce4 gPixLargeInitialValue
extern long		gPixLowBlobThreshold;							// ROM 0x0c104ce8 gPixLowBlobThreshold
extern long		gPixHighBlobThreshold;							// ROM 0x0c104cec gPixHighBlobThreshold
extern long		gPixMaxSizeTrendSlop;							// ROM 0x0c104cf0 gPixMaxSizeTrendSlop
extern long		gPixPtOnLineSlop;								// ROM 0x0c104cf4 gPixPtOnLineSlop
extern long		gSmpMinClosedShapePts;							// ROM 0x0c104cf8 gSmpMinClosedShapePts
extern long		gSmpMinSmallDistRun;							// ROM 0x0c104cfc gSmpMinSmallDistRun
extern ContextUnitProc	gContextUnitProc;						// ROM 0x0c104d08 gContextUnitProc

class TGeneralShapeDomain : public TDomain
{
public:
	static TGeneralShapeDomain*	Make(TController* controller);	// ROM 0x00215f20 Make__19TGeneralShapeDomainSFP11TController
	void				IGeneralShapeDomain(TController* controller);	// ROM 0x00215f68 IGeneralShapeDomain__19TGeneralShapeDomainFP11TController

	virtual void		Classify(TUnit* unit);					// ROM 0x002113f0 Classify__19TGeneralShapeDomainFP5TUnit
	virtual long		Group(TUnit* unit, dInfoRec* info);		// ROM 0x00215fd4 Group__19TGeneralShapeDomainFP5TUnitP8dInfoRec
	virtual long		PreGroup(TUnit* unit);					// ROM 0x00216538 PreGroup__19TGeneralShapeDomainFP5TUnit
};

// The shape recogniser: 'GSHP' units answered with aeShape, except the
// ones that came to nothing.
class TShapeRecognizer : public TRecognizer
{
public:
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x00144530 HandleUnit__16TShapeRecognizerFP11TUnitPublic
};
void	InstallShapeRecognizer(TRecognitionManager* manager);	// ROM 0x0014456c InstallShapeRecognizer__FP19TRecognitionManager

// A polygon on the page made into a shape unit, for the new drawing to
// be snapped to: a stroke through its points, and a shape unit over it
// with the polygon's verb as its type (the curvy verbs made the plain
// ones), a circle given its centre and diameter out of the view's box.
TGeneralShapeUnit*	MakeGeneralShape(TUnitPublic* unit, PolygonShape* shape, const Rect& box, long contextID);	// ROM 0x001445fc MakeGeneralShape__FP11TUnitPublicP12PolygonShapeRC5TRectl

// The fitting and tidying Classify is made of (ShapeGeometry.cpp).
struct EqSystem;
void	FindKeyPoints(TGeneralShapeUnit* unit, long* type, ULong* score);	// ROM 0x0021227c FindKeyPoints__FP17TGeneralShapeUnitP6GSTypePUl
Boolean	FindEllipses(TGeneralShapeUnit* unit, long* type, ULong* score, long* angle);	// ROM 0x00215904 FindEllipses__FP17TGeneralShapeUnitP6GSTypePUlPl
Boolean	FindEquations(TGeneralShapeUnit* unit, long* values, EqSystem* system, long* type, ULong* score, long* angle);	// ROM 0x002231e0 FindEquations__FP17TGeneralShapeUnitPlP8EqSystemP6GSTypePUlT2
Boolean	SolveEquations(EqSystem* system, long* values);			// ROM 0x0020fae8 SolveEquations__FP8EqSystemPl
void	PlugNewVals(TGeneralShapeUnit* unit, long* values, EqSystem* system);	// ROM 0x00225560 PlugNewVals__FP17TGeneralShapeUnitPlP8EqSystem
void	ReleaseEqs(EqSystem* system);							// ROM 0x002257b8 ReleaseEqs__FP8EqSystem
void	GlobalTrends(TGeneralShapeUnit* unit, long* snapped);	// ROM 0x00211684 GlobalTrends__FP17TGeneralShapeUnitPl
void	SnapPtToLC(TGeneralShapeUnit* unit);					// ROM 0x00211d00 SnapPtToLC__FP17TGeneralShapeUnit

#endif	/* __SHAPEDOMAIN_H */
