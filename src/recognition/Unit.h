/*
	File:		recognition/Unit.h

	Contains:	The recogniser's units - what the recognisers make of the
				strokes and hand on to the views.  TUnit is the base: a type
				(a four-character code: 'CLIK' a click, 'STRK' a stroke,
				'CEVT' a click event, 'WORD', 'GSHP' a shape...), a bounding
				box in pixels, the domain that made it, the areas it lies in
				(one TRecArea, or a TAreaList when there are several), its
				start time and duration in ticks, the range of the
				controller's strokes it covers, a delay before it is
				arbitrated, and a use count (Clone/Release).  The flags
				(TRecObject's): 0x40000000 claimed, 0x10000000 delayed,
				0x8000000 invalidated, 0x400000 invalid, 0x80000 passed on
				from the subs, 0x20000 the areas are a list.  TUnitList and
				TTypeList are TDArrays of units and of types.

				TSIUnit (a sub-unit and interpretation unit) adds the subs it
				was grouped from - none, one kept in place, or a TDArray of
				them - and a list of UnitInterpretations (a label, a score,
				an angle, a parameter object), and merges the subs' boxes,
				times and stroke ranges as they are added.  TStrokeUnit
				holds a TStroke (owned when its type is 'STRK') and a
				context id; TClickUnit holds the stroke of the pen-down
				(bounded by its box, started at its down time);
				TClickEventUnit is made over a click unit whose stroke has
				had a click event (tap, double tap, hilite) noted in it.
				The ROM's objects: TUnit 0x30, TSIUnit 0x3c, TStrokeUnit
				0x44, TClickUnit 0x34, TClickEventUnit 0x40.

				NOT YET RECONSTRUCTED: the Dump methods (TMsg, the debugging
				message buffer), the controller's next-event time that
				AddSub and EndSubs lower (TController is NOT YET).

	Reconstructed from the MP2x00 US ROM (0x0022ca94-0x0022ced0,
	0x0022dea4-0x0022e600, 0x0021ca70-0x0021dbd0, 0x00220f28-0x00220fa8,
	0x00221ec8-0x0022212c, 0x0021f28c-0x0021f67c); each function cites its
	origin.
*/

#ifndef __UNIT_H
#define __UNIT_H

#include "Stroke.h"

class TDomain;
class TRecArea;
class TAreaList;
class TUnitList;

// the unit types
enum
{
	kClickUnit			= 'CLIK',
	kStrokeUnit			= 'STRK',
	kClickEventUnit		= 'CEVT',
	kWordUnit			= 'WORD',
	kShapeUnit			= 'GSHP',
	kScrubUnit			= 'SCRB',		// the scrub-out gesture
	kReplayUnit			= 'WRPL',		// a unit the journal is replaying
	kRootDomainType		= 'ROOT'
};

// the unit flags
enum
{
	kClaimedUnit		= 0x40000000,
	kDelayedUnit		= 0x10000000,
	kInvalidatedUnit	= 0x08000000,
	kInvalidUnit		= 0x00400000,
	kPassedOnUnit		= 0x00080000,		// set on a TSIUnit when a sub has it
	kAreaListUnit		= 0x00020000,		// fAreas is a TAreaList, not a TRecArea
	kGatheredUnit		= 0x00800000,		// the arbitration has this one in hand
	kClassifiedUnit		= 0x00200000		// it has been classified already
};

// the click events a stroke can carry (TStroke::fClickEvent)
enum
{
	kNoClick			= 0,
	kProcessedClick		= 1,
	kTapClick			= 2,
	kDoubleTapClick		= 3,
	kHiliteClick		= 4,
	kTapDragClick		= 5			// a press within reach of the last tap
};

void	FixRect(FRect* dst, const Rect* src);				// ROM 0x001a3fa8 FixRect - pixels to Fixed
void	AddRect(const FRect* src, FRect* dst, Boolean first);	// ROM 0x001a41d0 AddRect - dst grown to hold src (set to it when first)

class TUnit : public TRecObject
{
public:
						TUnit();								// ROM 0x0022ccb0 __ct__5TUnitFv
	virtual				~TUnit();								// ROM 0x0022dea4 __dt__5TUnitFv
	long				IUnit(TDomain* domain, ULong type, ULong kind, TArray* areas);	// ROM 0x0022e2bc IUnit__5TUnitFP7TDomainUlT2P6TArray

	virtual void		Dispose(void);							// ROM 0x0022e52c Dispose__5TUnitFv (vtable +0x00: released; gone when no user is left)
	virtual void		Dump(TMsg* msg);						// ROM 0x0022e0d8 Dump__5TUnitFP4TMsg
	virtual long		SizeInBytes(void);						// ROM 0x0022e594 SizeInBytes__5TUnitFv
	virtual void		IDispose(void);							// ROM 0x0022e560 IDispose__5TUnitFv (+0x10: the areas let go, the object deleted)
	virtual void		Clone(void);							// ROM 0x0022e5e4 Clone__5TUnitFv (+0x14: one more user)
	virtual Boolean		Release(void);							// ROM 0x0022e5f4 Release__5TUnitFv (+0x18: one fewer; ==> whether none is left)
	virtual long		SubCount(void);							// ROM 0x0022dee4 SubCount__5TUnitFv (+0x1c)
	virtual long		InterpretationCount(void);				// ROM 0x0022deec InterpretationCount__5TUnitFv (+0x20)
	virtual long		GetBestInterpretation(void);			// ROM 0x0022def4 GetBestInterpretation__5TUnitFv (+0x24: -1)
	virtual void		DumpName(TMsg* msg);					// ROM 0x0022e070 DumpName__5TUnitFP4TMsg (+0x28)
	virtual void		ClaimUnit(TUnitList* list);				// ROM 0x0022e248 ClaimUnit__5TUnitFP9TUnitList (+0x2c: marked claimed)
	virtual long		MarkUnit(TUnitList* list, ULong flags);	// ROM 0x0022e210 MarkUnit__5TUnitFP9TUnitListUl (+0x30: added to the list, flagged; ==> 0, or 1 for no memory)
	virtual void		Invalidate(void);						// ROM 0x0022e254 Invalidate__5TUnitFv (+0x34)
	virtual void		DoneUsingUnit(void);					// ROM 0x0022e58c DoneUsingUnit__5TUnitFv (+0x38: the areas let go)
	virtual long		CountStrokes(void);						// ROM 0x0022e25c CountStrokes__5TUnitFv (+0x3c)
	virtual TStroke*	GetStroke(ULong index);					// ROM 0x0022e264 GetStroke__5TUnitFUl (+0x40)
	virtual TUnitList*	GetAllStrokes(void);					// ROM 0x0022e26c GetAllStrokes__5TUnitFv (+0x44: the stroke units under this one)
	virtual Boolean		OwnsStroke(void);						// ROM 0x0022e274 OwnsStroke__5TUnitFv (+0x48)
	virtual ULong		ContextID(void);						// ROM 0x0022e27c ContextID__5TUnitFv (+0x4c)
	virtual void		SetContextID(ULong id);					// ROM 0x0022e284 SetContextID__5TUnitFUl (+0x50)

	TAreaList*			GetAreas(void);							// ROM 0x0022defc GetAreas__5TUnitFv - a list (cloned) of the areas, nil for none
	void				SetAreas(TAreaList* areas);				// ROM 0x0022df78 SetAreas__5TUnitFP9TAreaList - the old ones let go; one area kept alone, several as the list
	TRecArea*			GetArea(void);							// ROM 0x0022e014 GetArea__5TUnitFv - the (last) area
	void				SetDelay(ULong delay);					// ROM 0x0022e054 SetDelay__5TUnitFUl - in ticks, at most 255; flagged delayed when not 0
	void				SetBBox(FRect* box);					// ROM 0x0022e288 SetBBox__5TUnitFP5FRect - rounded to pixels
	FRect*				GetBBox(FRect* box);					// ROM 0x0022e298 GetBBox__5TUnitFP5FRect

	ULong				Type(void) const		{ return fType; }
	ULong				StartTime(void) const	{ return fStartTime; }
	ULong				EndTime(void) const		{ return fStartTime + fDuration; }

	ULong				fType;			// +0x08
	Rect				fBBox;			// +0x0c  in pixels
	TDomain*			fDomain;		// +0x14
	TRecObject*			fAreas;			// +0x18  a TRecArea, or a TAreaList (kAreaListUnit)
	ULong				fStartTime;		// +0x1c  ticks
	UShort				fDuration;		// +0x20  ticks
	UShort				fElapsed;		// +0x22  ticks from the start to the last sub added
	UChar				fKind;			// +0x24  as made (1 for the clicks the stroke world makes)
	SChar				fUsers;			// +0x25  Clone/Release (0: one user)
	SChar				fPriority;		// +0x26
	UChar				fDelay;			// +0x27  ticks before arbitration
	UShort				fSubRange;		// +0x28  0xffff until the first sub is added
	UShort				fMinStroke;		// +0x2a  the first of the controller's strokes it covers
	UShort				fMaxStroke;		// +0x2c  the last
};

long	CountStrokes(TUnit* unit);								// ROM 0x0022e374 CountStrokes__FP5TUnit - the distinct strokes under a unit
void	MarkStrokes(TUnit* unit, char* marks, long base);		// ROM 0x0022e454 MarkStrokes__FP5TUnitPcl - a count per stroke index from base

// a list of units (their pointers)
class TUnitList : public TDArray
{
public:
	static TUnitList*	Make(void);								// ROM 0x0022ccf0 Make__9TUnitListSFv
	long				IUnitList(void);						// ROM 0x0022cd58 IUnitList__9TUnitListFv
	virtual void		Dump(TMsg* msg);						// ROM 0x0022ce74 Dump__9TUnitListFP4TMsg

	void				Purge(void);							// ROM 0x0022cd64 Purge__9TUnitListFv - every unit disposed (the list kept)
	Boolean				AddUnit(TUnit* unit);					// ROM 0x0022cdac AddUnit__9TUnitListFP5TUnit - ==> true for no memory
	Boolean				AddUnique(TUnit* unit);					// ROM 0x0022cddc AddUnique__9TUnitListFP5TUnit
	TUnit*				GetUnit(ULong index);					// ROM 0x0022ce4c GetUnit__9TUnitListFUl - nil past the end
};

// a list of unit types
class TTypeList : public TDArray
{
public:
	static TTypeList*	Make(void);								// ROM 0x0022ca94 Make__9TTypeListSFv
	long				ITypeList(void);						// ROM 0x0022cafc ITypeList__9TTypeListFv
	virtual void		Dump(TMsg* msg);						// ROM 0x0022cc4c Dump__9TTypeListFP4TMsg

	Boolean				AddType(ULong type);					// ROM 0x0022cb34 AddType__9TTypeListFUl - ==> true for no memory
	Boolean				AddUnique(ULong type);					// ROM 0x0022cb64 AddUnique__9TTypeListFUl
	ULong				FindType(ULong type);					// ROM 0x0022cbd8 FindType__9TTypeListFUl - its index, -1 for not there
	ULong				GetType(ULong index);					// ROM 0x0022cc2c GetType__9TTypeListFUl
};

// what a recogniser made of a unit: a label (-1: none), a score (lower is
// better; 10000 the worst), an angle, and a parameter object of its own
struct UnitInterpretation
{
	long				label;			// +0x00
	long				score;			// +0x04
	long				angle;			// +0x08
	TRecObject*			param;			// +0x0c
};
long	InitInterpretation(UnitInterpretation* interp, ULong elementSize, ULong count);	// ROM 0x0021d070 InitInterpretation__FP18UnitInterpretationUlT2 - param a TArray of count entries when count > 0; ==> 1, or 0 for no memory

// how a TSIUnit keeps its subs
enum
{
	kNoSubs				= 0,
	kOneSub				= 1,				// fSubs is the TUnit itself
	kSubList			= 2					// fSubs is a TDArray of TUnit*
};

class TSIUnit : public TUnit
{
public:
						TSIUnit();								// ROM 0x0021ca70 __ct__7TSIUnitFv
	long				ISIUnit(TDomain* domain, ULong type, ULong kind, TArray* areas, ULong interpSize);	// ROM 0x0021d528 ISIUnit__7TSIUnitFP7TDomainUlT2P6TArrayT2

	virtual void		Dump(TMsg* msg);						// ROM 0x0021d440 Dump__7TSIUnitFP4TMsg
	virtual long		SizeInBytes(void);						// ROM 0x0021da24 SizeInBytes__7TSIUnitFv
	virtual void		IDispose(void);							// ROM 0x0021d728 IDispose__7TSIUnitFv
	virtual long		SubCount(void);							// ROM 0x0021dbb8 SubCount__7TSIUnitFv
	virtual long		InterpretationCount(void);				// ROM 0x0021d0c8 InterpretationCount__7TSIUnitFv
	virtual long		GetBestInterpretation(void);			// ROM 0x0021d3c4 GetBestInterpretation__7TSIUnitFv - the labelled one with the lowest score, -1 for none
	virtual void		ClaimUnit(TUnitList* list);				// ROM 0x0021cf7c ClaimUnit__7TSIUnitFP9TUnitList
	virtual long		MarkUnit(TUnitList* list, ULong flags);	// ROM 0x0021cedc MarkUnit__7TSIUnitFP9TUnitListUl - the subs marked (the unit itself when it has none)
	virtual void		DoneUsingUnit(void);					// ROM 0x0021d9c0 DoneUsingUnit__7TSIUnitFv - the interpretations and areas let go
	virtual long		CountStrokes(void);						// ROM 0x0021d6c0 CountStrokes__7TSIUnitFv - the subs' strokes
	virtual TStroke*	GetStroke(ULong index);					// ROM 0x0021d7a4 GetStroke__7TSIUnitFUl - the index-th stroke across the subs
	virtual TUnitList*	GetAllStrokes(void);					// ROM 0x0021d870 GetAllStrokes__7TSIUnitFv - the sub tree walked down to the units that own strokes

	virtual long		AddSub(TUnit* sub);						// ROM 0x0021cab0 AddSub__7TSIUnitFP5TUnit (+0x54: box, times and stroke range merged; ==> the sub's index, -1 for no memory)
	virtual TUnit*		GetSub(ULong index);					// ROM 0x0021cdb8 GetSub__7TSIUnitFUl (+0x58)
	virtual void		DeleteSub(ULong index);					// ROM 0x0021ce14 DeleteSub__7TSIUnitFUl (+0x5c: the sub disposed)
	virtual long		EndSubs(void);							// ROM 0x0021dadc EndSubs__7TSIUnitFv (+0x60: no more subs - the delay dropped, the list compacted)
	virtual long		AddInterpretation(char* interp);		// ROM 0x0021d240 AddInterpretation__7TSIUnitFPc (+0x64: ==> its index, -1 for no memory)
	virtual UnitInterpretation*	GetInterpretation(ULong index);	// ROM 0x0021d2b8 GetInterpretation__7TSIUnitFUl (+0x68)
	virtual Boolean		CheckInterpretationIndex(ULong index);	// ROM 0x0021d680 CheckInterpretationIndex__7TSIUnitFUl (+0x6c)
	virtual long		DeleteInterpretation(ULong index);		// ROM 0x0021d2d8 DeleteInterpretation__7TSIUnitFUl (+0x70: its param disposed)
	virtual long		InsertInterpretation(ULong index);		// ROM 0x0021d338 InsertInterpretation__7TSIUnitFUl (+0x74: a slot opened; ==> its index, -1 for no memory)
	virtual char*		LockInterpretations(void);				// ROM 0x0021d384 LockInterpretations__7TSIUnitFv (+0x78)
	virtual void		UnlockInterpretations(void);			// ROM 0x0021d398 UnlockInterpretations__7TSIUnitFv (+0x7c)
	virtual void		CompactInterpretations(void);			// ROM 0x0021d3ac CompactInterpretations__7TSIUnitFv (+0x80)
	virtual long		InterpretationReuse(ULong count, ULong paramSize, ULong paramCount);	// ROM 0x0021d0f4 InterpretationReuse__7TSIUnitFUlN21 (+0x84: count interpretations kept, the rest deleted or made anew)
	virtual TDArray*	GetSubsCopy(void);						// ROM 0x0021cf88 GetSubsCopy__7TSIUnitFv (+0x88: a list of the subs - the list itself, cloned, when there is one)
	virtual long		GetLabel(ULong index);					// ROM 0x0021d568 GetLabel__7TSIUnitFUl (+0x8c)
	virtual long		GetScore(ULong index);					// ROM 0x0021d590 GetScore__7TSIUnitFUl (+0x90)
	virtual long		GetAngle(ULong index);					// ROM 0x0021d5b8 GetAngle__7TSIUnitFUl (+0x94)
	virtual TRecObject*	GetParam(ULong index);					// ROM 0x0021d5e0 GetParam__7TSIUnitFUl (+0x98)
	virtual void		SetLabel(ULong index, ULong label);		// ROM 0x0021d608 SetLabel__7TSIUnitFUlT1 (+0x9c)
	virtual void		SetScore(ULong index, ULong score);		// ROM 0x0021d630 SetScore__7TSIUnitFUlT1 (+0xa0)
	virtual void		SetAngle(ULong index, long angle);		// ROM 0x0021d658 SetAngle__7TSIUnitFUll (+0xa4)
	virtual void		EndUnit(void);							// ROM 0x0021db3c EndUnit__7TSIUnitFv (+0xa8: the subs ended, the params compacted)

	void				CloseInterpList(void);					// ROM 0x0021d000 CloseInterpList__7TSIUnitFv - the list disposed, its element size kept
	long				OpenInterpList(void);					// ROM 0x0021d030 OpenInterpList__7TSIUnitFv - ==> 0, or -1 for no memory

	UChar				fSubKind;		// +0x30  kNoSubs, kOneSub, kSubList
	UChar				fHasInterps;	// +0x31  fInterps is a list (else its element size)
	TRecObject*			fSubs;			// +0x34  a TUnit, or a TDArray of TUnit*
	union {
		TDArray*		fInterps;		// +0x38  the interpretations
		ULong			fInterpSize;	// +0x38  their element size while there are none
	};
};

// The stroke a unit was made from, which is the only thing a view that
// only wants the ink needs of it.
TStroke*	GetTStroke(TUnit* unit);					// ROM 0x00145dfc GetTStroke__FP5TUnit
long		CountTStrokes(TUnit* unit);					// ROM 0x00145e08 CountTStrokes__FP5TUnit - how many strokes are under it

class TStrokeUnit : public TSIUnit
{
public:
	static TStrokeUnit*	Make(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas);	// ROM 0x00221ec8 Make__11TStrokeUnitSFP7TDomainUlP7TStrokeP6TArray
	long				IStrokeUnit(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas);	// ROM 0x00221f50 IStrokeUnit__11TStrokeUnitFP7TDomainUlP7TStrokeP6TArray

	virtual void		Dump(TMsg* msg);						// ROM 0x00220f94 Dump__11TStrokeUnitFP4TMsg
	virtual void		IDispose(void);							// ROM 0x00221fd0 IDispose__11TStrokeUnitFv (the stroke disposed when the type is still 'STRK')
	virtual long		CountStrokes(void);						// ROM 0x00220f28 CountStrokes__11TStrokeUnitFv (1)
	virtual TStroke*	GetStroke(ULong index);					// ROM 0x00220f30 GetStroke__11TStrokeUnitFUl
	virtual TUnitList*	GetAllStrokes(void);					// ROM 0x00220f50 GetAllStrokes__11TStrokeUnitFv (itself)
	virtual Boolean		OwnsStroke(void);						// ROM 0x00220f38 OwnsStroke__11TStrokeUnitFv (true)
	virtual ULong		ContextID(void);						// ROM 0x00220f40 ContextID__11TStrokeUnitFv
	virtual void		SetContextID(ULong id);					// ROM 0x00220f48 SetContextID__11TStrokeUnitFUl

	// the shape domain's tests of the stroke (ShapeEllipses.cpp)
	TArray*				GetPts(void);							// ROM 0x002220a4 GetPts__11TStrokeUnitFv - the points as FPoints
	Boolean				IsCircle(FPoint* centre, long* radius, ULong* score);	// ROM 0x00220f98 IsCircle__11TStrokeUnitFP6FPointPlPUl
	Boolean				IsEllipse(FPoint* centre, long* radius1, long* radius2, long* angle, ULong* score);	// ROM 0x002212e4 IsEllipse__11TStrokeUnitFP6FPointPlN22PUl

	ULong				fContextID;		// +0x3c
	TStroke*			fStroke;		// +0x40
};

class TClickUnit : public TUnit
{
public:
	static TClickUnit*	Make(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas);	// ROM 0x0021f518 Make__10TClickUnitSFP7TDomainUlP7TStrokeP6TArray
	long				IClickUnit(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas);	// ROM 0x0021f5a0 IClickUnit__10TClickUnitFP7TDomainUlP7TStrokeP6TArray

	virtual void		Dump(TMsg* msg);						// ROM 0x0021f28c Dump__10TClickUnitFP4TMsg
	virtual void		IDispose(void);							// ROM 0x0021f640 IDispose__10TClickUnitFv (the stroke unbuffered and disposed)
	virtual long		MarkUnit(TUnitList* list, ULong flags);	// ROM 0x0021f608 MarkUnit__10TClickUnitFP9TUnitListUl (the stroke let out of the inker's buffer)
	virtual long		CountStrokes(void);						// ROM 0x0021f2e4 CountStrokes__10TClickUnitFv (1)
	virtual TStroke*	GetStroke(ULong index);					// ROM 0x0021f2ec GetStroke__10TClickUnitFUl
	virtual Boolean		OwnsStroke(void);						// ROM 0x0021f2f4 OwnsStroke__10TClickUnitFv (true)

	TStroke*			fStroke;		// +0x30
};

class TClickEventUnit : public TSIUnit
{
public:
	static TClickEventUnit*	Make(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0021f2fc Make__15TClickEventUnitSFP7TDomainUlP6TArray
	long				IClickEventUnit(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0021f374 IClickEventUnit__15TClickEventUnitFP7TDomainUlP6TArray

	virtual void		Dump(TMsg* msg);						// ROM 0x0021f434 Dump__15TClickEventUnitFP4TMsg

	long				Event(void);							// ROM 0x0021f3b4 Event__15TClickEventUnitFv - the click event of the sub's stroke, read once
	void				ClearEvent(void);						// ROM 0x0021f3f8 ClearEvent__15TClickEventUnitFv - the stroke's event marked processed (the unit keeps it)

	long				fEvent;			// +0x3c  -1 until read
};

// the stroke queue (StrokeQueue.h): a stroke taken out of it
void	UnbufferStroke(TStroke* stroke);						// ROM 0x001ffc24 UnbufferStroke__FP7TStroke
enum { kBufferedStroke = 0x10000000 };						// the stroke flag the queue holds it by (StrokeQueue.h: kStrokeInQueue)

#endif	/* __UNIT_H */
