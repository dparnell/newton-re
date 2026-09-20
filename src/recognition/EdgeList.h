/*
	File:		recognition/EdgeList.h

	Contains:	The gesture domain - 'SCRB', the one every level of
				recognition has - and the units it makes.

				It takes one stroke at a time and reduces it to the corners
				of a polyline: FindCorners walks the stroke splitting it
				wherever a point is far enough off the chord between the
				two ends (the ROM's own recursive simplifier), Collapse2
				then drops the corners that are too close together or that
				hardly turn at all, and what is left is a handful of
				points.  Three tests are tried on them in turn -
				TestLine (two corners: a straight line), TestCarets (three
				or four corners meeting at a sharp angle) and TestScrub
				(a zig-zag of several alternating turns) - and the first
				that recognises something labels the unit.

				TScrubRecognizer turns those labels into the commands the
				views answer: a scrub is aeScrub, the carets aeCaret, a
				line aeLine, and a stroke that turned out to be a tap
				aeTap.  Each of them runs the view's viewGestureScript
				with the unit and the command's id, which is how
				scrubbing text out and the caret that opens a space are
				reached.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __EDGELIST_H
#define __EDGELIST_H

#ifndef __UNIT_H
#include "Unit.h"
#endif
#ifndef __DOMAIN_H
#include "Domain.h"
#endif

class TController;
class TDArray;


// What TestLine, TestCarets and TestScrub label a unit with; the
// recogniser (TScrubRecognizer::HandleUnit) turns these into commands.
enum
{
	kGestureNone		= 0,
	kGestureScrub		= 1,		// a zig-zag: three or more alternating turns
	kGestureCaret		= 2,		// three corners meeting sharply, pointing along the mid angle
	kGestureCaret4		= 3,		// four corners, the last arm square to the caret's axis
	kGestureLine		= 4,		// two corners: a straight line
	kGestureCaret4Open	= 5,		// four corners, the last arm anywhere else
	kGestureCaretFlat	= 6			// three corners at a shallower angle, leaning over
};

// the gesture domain's type and the pieces it takes
enum
{
	kEdgeListDomainType	= 'SCRB'
};


// The unit a gesture domain makes: a TSIUnit with one interpretation of
// its own (no list) and the corners of the stroke beside it.
class TEdgeListUnit : public TSIUnit
{
public:
	static TSIUnit*		Make(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0020ed90 Make__13TEdgeListUnitSFP7TDomainUlP6TArray
	long				IEdgeListUnit(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0020ee08 IEdgeListUnit__13TEdgeListUnitFP7TDomainUlP6TArray

	virtual void		Dump(TMsg* msg);						// ROM 0x0020eed0 Dump__13TEdgeListUnitFP4TMsg
	virtual long		SizeInBytes(void);						// ROM 0x0020efb4 SizeInBytes__13TEdgeListUnitFv
	virtual void		IDispose(void);							// ROM 0x0020ef54 IDispose__13TEdgeListUnitFv
	virtual long		InterpretationCount(void);				// ROM 0x0020f004 InterpretationCount__13TEdgeListUnitFv
	virtual void		DoneUsingUnit(void);					// ROM 0x0020ef80 DoneUsingUnit__13TEdgeListUnitFv
	virtual long		AddInterpretation(char* interp);		// ROM 0x0020f00c AddInterpretation__13TEdgeListUnitFPc - the one interpretation overwritten
	virtual UnitInterpretation*	GetInterpretation(ULong index);	// ROM 0x0020f03c GetInterpretation__13TEdgeListUnitFUl - always the one
	virtual void		EndUnit(void);							// ROM 0x0020eea0 EndUnit__13TEdgeListUnitFv

	void				SetInterpretation(TDArray* corners);	// ROM 0x0020ee70 SetInterpretation__13TEdgeListUnitFP7TDArray - the corners kept (cloned)
	TDArray*			GetCorners(void);						// ROM 0x0020effc GetCorners__13TEdgeListUnitFv

	TDArray*			fCorners;		// +0x3c  the polyline the stroke came down to, as FPoints
	long				fInterpCount;	// +0x40  0 or 1
	UnitInterpretation	fInterp;		// +0x44
};


// The gesture domain.  It groups each stroke on its own and classifies
// it by shape.
class TEdgeListDomain : public TDomain
{
public:
	static TDomain*		Make(TController* controller);			// ROM 0x0020e83c Make__15TEdgeListDomainSFP11TController
	void				IEdgeListDomain(TController* controller);	// ROM 0x0020e884 IEdgeListDomain__15TEdgeListDomainFP11TController

	virtual void		Dispose(void);							// ROM 0x0020e8d4 Dispose__15TEdgeListDomainFv (nothing: the domain lives as long as the system)
	virtual void		Classify(TUnit* unit);					// ROM 0x0020e8d8 Classify__15TEdgeListDomainFP5TUnit
	virtual long		Group(TUnit* piece, dInfoRec* info);	// ROM 0x0020ea1c Group__15TEdgeListDomainFP5TUnitP8dInfoRec

	void				FindCorners(TUnit* unit);				// ROM 0x0020eaa8 FindCorners__15TEdgeListDomainFP5TUnit
															// (it adds no fields of its own: TDomain's fController is the one it uses)
};

extern TDomain*	gEdgeListDomain;

// the shape tests (the gesture area of the ROM, beside the unit code)
void	Collapse2(TDArray* corners);							// ROM 0x0020ebe4 Collapse2__FP7TDArray - the corners too close or too straight dropped
long	Signum(long value);										// ROM 0x001f95ac Signum__Fl
void	Interpolate(const FPoint* a, const FPoint* b, long distance, FPoint* result);	// ROM 0x0021edf0 Interpolate__FP6FPointT1lT1
Boolean	TestLine(TDArray* corners, UnitInterpretation* interp);	// ROM 0x0021f0fc TestLine__FP7TDArrayP18UnitInterpretation
Boolean	TestCarets(TDArray* corners, UnitInterpretation* interp);	// ROM 0x0021ee64 TestCarets__FP7TDArrayP18UnitInterpretation
Boolean	TestScrub(TDArray* corners, FRect* bbox, UnitInterpretation* interp);	// ROM 0x0021ebb8 TestScrub__FP7TDArrayP5FRectP18UnitInterpretation

// The turns a candidate scrub is made of: their slopes, and the one being
// built.  ValidTurnSequence says whether they all lie within half a turn
// of each other, which is what tells a scrub from a scribble.
struct TurnData
{
	long		fCount;			// +0x00
	long		fSlopes[40];	// +0x04
	FPoint		fStart;			// +0xa4  the turn being built
	FPoint		fEnd;			// +0xac
};
void	InitTurnData(TurnData* turns);							// ROM 0x0021f170 InitTurnData__FP8TurnData
void	NewTurn(TurnData* turns, FPoint start, FPoint end);		// ROM 0x0021f17c NewTurn__FP8TurnData6FPointT2
void	ExtendTurn(TurnData* turns, FPoint end);				// ROM 0x0021f1a8 ExtendTurn__FP8TurnData6FPoint
void	EndTurn(TurnData* turns, FPoint end);					// ROM 0x0021f1b4 EndTurn__FP8TurnData6FPoint - kept when it is more than six units long
Boolean	ValidTurnSequence(TurnData* turns);						// ROM 0x0021f218 ValidTurnSequence__FP8TurnData

#endif	/* __EDGELIST_H */
