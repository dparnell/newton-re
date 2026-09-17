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
				AddSub and EndSubs lower (TController is NOT YET), the
				inker's stroke queue (UnbufferStroke).

	Reconstructed from the MP2100 D ROM (0x0022a24c-0x0022a688,
	0x0022b744-0x0022bea0, 0x0021a340-0x0021b4a0, 0x0021e6e0-0x0021e760,
	0x0021f680-0x0021f8e4, 0x0021cb5c-0x0021cf4c); each function cites its
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
	kAreaListUnit		= 0x00020000			// fAreas is a TAreaList, not a TRecArea
};

// the click events a stroke can carry (TStroke::fClickEvent)
enum
{
	kNoClick			= 0,
	kProcessedClick		= 1,
	kTapClick			= 2,
	kDoubleTapClick		= 3,
	kHiliteClick		= 4
};

void	FixRect(FRect* dst, const Rect* src);				// ROM 0x001a6528 FixRect - pixels to Fixed
void	AddRect(const FRect* src, FRect* dst, Boolean first);	// ROM 0x001a6750 AddRect - dst grown to hold src (set to it when first)

class TUnit : public TRecObject
{
public:
						TUnit();								// ROM 0x0022a468 __ct__5TUnitFv
	virtual				~TUnit();								// ROM 0x0022b744 __dt__5TUnitFv
	long				IUnit(TDomain* domain, ULong type, ULong kind, TArray* areas);	// ROM 0x0022bb5c IUnit__5TUnitFP7TDomainUlT2P6TArray

	virtual void		Dispose(void);							// ROM 0x0022bdcc Dispose__5TUnitFv (vtable +0x00: released; gone when no user is left)
	virtual void		Dump(TMsg* msg);						// ROM 0x0022b978 Dump__5TUnitFP4TMsg
	virtual long		SizeInBytes(void);						// ROM 0x0022be34 SizeInBytes__5TUnitFv
	virtual void		IDispose(void);							// ROM 0x0022be00 IDispose__5TUnitFv (+0x10: the areas let go, the object deleted)
	virtual void		Clone(void);							// ROM 0x0022be84 Clone__5TUnitFv (+0x14: one more user)
	virtual Boolean		Release(void);							// ROM 0x0022be94 Release__5TUnitFv (+0x18: one fewer; ==> whether none is left)
	virtual long		SubCount(void);							// ROM 0x0022b784 SubCount__5TUnitFv (+0x1c)
	virtual long		InterpretationCount(void);				// ROM 0x0022b78c InterpretationCount__5TUnitFv (+0x20)
	virtual long		GetBestInterpretation(void);			// ROM 0x0022b794 GetBestInterpretation__5TUnitFv (+0x24: -1)
	virtual void		DumpName(TMsg* msg);					// ROM 0x0022b910 DumpName__5TUnitFP4TMsg (+0x28)
	virtual void		ClaimUnit(TUnitList* list);				// ROM 0x0022bae8 ClaimUnit__5TUnitFP9TUnitList (+0x2c: marked claimed)
	virtual long		MarkUnit(TUnitList* list, ULong flags);	// ROM 0x0022bab0 MarkUnit__5TUnitFP9TUnitListUl (+0x30: added to the list, flagged; ==> 0, or 1 for no memory)
	virtual void		Invalidate(void);						// ROM 0x0022baf4 Invalidate__5TUnitFv (+0x34)
	virtual void		DoneUsingUnit(void);					// ROM 0x0022be2c DoneUsingUnit__5TUnitFv (+0x38: the areas let go)
	virtual long		CountStrokes(void);						// ROM 0x0022bafc CountStrokes__5TUnitFv (+0x3c)
	virtual TStroke*	GetStroke(ULong index);					// ROM 0x0022bb04 GetStroke__5TUnitFUl (+0x40)
	virtual TUnitList*	GetAllStrokes(void);					// ROM 0x0022bb0c GetAllStrokes__5TUnitFv (+0x44: the stroke units under this one)
	virtual Boolean		OwnsStroke(void);						// ROM 0x0022bb14 OwnsStroke__5TUnitFv (+0x48)
	virtual ULong		ContextID(void);						// ROM 0x0022bb1c ContextID__5TUnitFv (+0x4c)
	virtual void		SetContextID(ULong id);					// ROM 0x0022bb24 SetContextID__5TUnitFUl (+0x50)

	TAreaList*			GetAreas(void);							// ROM 0x0022b79c GetAreas__5TUnitFv - a list (cloned) of the areas, nil for none
	void				SetAreas(TAreaList* areas);				// ROM 0x0022b818 SetAreas__5TUnitFP9TAreaList - the old ones let go; one area kept alone, several as the list
	TRecArea*			GetArea(void);							// ROM 0x0022b8b4 GetArea__5TUnitFv - the (last) area
	void				SetDelay(ULong delay);					// ROM 0x0022b8f4 SetDelay__5TUnitFUl - in ticks, at most 255; flagged delayed when not 0
	void				SetBBox(FRect* box);					// ROM 0x0022bb28 SetBBox__5TUnitFP5FRect - rounded to pixels
	FRect*				GetBBox(FRect* box);					// ROM 0x0022bb38 GetBBox__5TUnitFP5FRect

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

long	CountStrokes(TUnit* unit);								// ROM 0x0022bc14 CountStrokes__FP5TUnit - the distinct strokes under a unit
void	MarkStrokes(TUnit* unit, char* marks, long base);		// ROM 0x0022bcf4 MarkStrokes__FP5TUnitPcl - a count per stroke index from base

// a list of units (their pointers)
class TUnitList : public TDArray
{
public:
	static TUnitList*	Make(void);								// ROM 0x0022a4a8 Make__9TUnitListSFv
	long				IUnitList(void);						// ROM 0x0022a510 IUnitList__9TUnitListFv
	virtual void		Dump(TMsg* msg);						// ROM 0x0022a62c Dump__9TUnitListFP4TMsg

	void				Purge(void);							// ROM 0x0022a51c Purge__9TUnitListFv - every unit disposed (the list kept)
	Boolean				AddUnit(TUnit* unit);					// ROM 0x0022a564 AddUnit__9TUnitListFP5TUnit - ==> true for no memory
	Boolean				AddUnique(TUnit* unit);					// ROM 0x0022a594 AddUnique__9TUnitListFP5TUnit
	TUnit*				GetUnit(ULong index);					// ROM 0x0022a604 GetUnit__9TUnitListFUl - nil past the end
};

// a list of unit types
class TTypeList : public TDArray
{
public:
	static TTypeList*	Make(void);								// ROM 0x0022a24c Make__9TTypeListSFv
	long				ITypeList(void);						// ROM 0x0022a2b4 ITypeList__9TTypeListFv
	virtual void		Dump(TMsg* msg);						// ROM 0x0022a404 Dump__9TTypeListFP4TMsg

	Boolean				AddType(ULong type);					// ROM 0x0022a2ec AddType__9TTypeListFUl - ==> true for no memory
	Boolean				AddUnique(ULong type);					// ROM 0x0022a31c AddUnique__9TTypeListFUl
	ULong				FindType(ULong type);					// ROM 0x0022a390 FindType__9TTypeListFUl - its index, -1 for not there
	ULong				GetType(ULong index);					// ROM 0x0022a3e4 GetType__9TTypeListFUl
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
long	InitInterpretation(UnitInterpretation* interp, ULong elementSize, ULong count);	// ROM 0x0021a940 InitInterpretation__FP18UnitInterpretationUlT2 - param a TArray of count entries when count > 0; ==> 1, or 0 for no memory

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
						TSIUnit();								// ROM 0x0021a340 __ct__7TSIUnitFv
	long				ISIUnit(TDomain* domain, ULong type, ULong kind, TArray* areas, ULong interpSize);	// ROM 0x0021adf8 ISIUnit__7TSIUnitFP7TDomainUlT2P6TArrayT2

	virtual void		Dump(TMsg* msg);						// ROM 0x0021ad10 Dump__7TSIUnitFP4TMsg
	virtual long		SizeInBytes(void);						// ROM 0x0021b2f4 SizeInBytes__7TSIUnitFv
	virtual void		IDispose(void);							// ROM 0x0021aff8 IDispose__7TSIUnitFv
	virtual long		SubCount(void);							// ROM 0x0021b488 SubCount__7TSIUnitFv
	virtual long		InterpretationCount(void);				// ROM 0x0021a998 InterpretationCount__7TSIUnitFv
	virtual long		GetBestInterpretation(void);			// ROM 0x0021ac94 GetBestInterpretation__7TSIUnitFv - the labelled one with the lowest score, -1 for none
	virtual void		ClaimUnit(TUnitList* list);				// ROM 0x0021a84c ClaimUnit__7TSIUnitFP9TUnitList
	virtual long		MarkUnit(TUnitList* list, ULong flags);	// ROM 0x0021a7ac MarkUnit__7TSIUnitFP9TUnitListUl - the subs marked (the unit itself when it has none)
	virtual void		DoneUsingUnit(void);					// ROM 0x0021b290 DoneUsingUnit__7TSIUnitFv - the interpretations and areas let go
	virtual long		CountStrokes(void);						// ROM 0x0021af90 CountStrokes__7TSIUnitFv - the subs' strokes
	virtual TStroke*	GetStroke(ULong index);					// ROM 0x0021b074 GetStroke__7TSIUnitFUl - the index-th stroke across the subs
	virtual TUnitList*	GetAllStrokes(void);					// ROM 0x0021b140 GetAllStrokes__7TSIUnitFv - the sub tree walked down to the units that own strokes

	virtual long		AddSub(TUnit* sub);						// ROM 0x0021a380 AddSub__7TSIUnitFP5TUnit (+0x54: box, times and stroke range merged; ==> the sub's index, -1 for no memory)
	virtual TUnit*		GetSub(ULong index);					// ROM 0x0021a688 GetSub__7TSIUnitFUl (+0x58)
	virtual void		DeleteSub(ULong index);					// ROM 0x0021a6e4 DeleteSub__7TSIUnitFUl (+0x5c: the sub disposed)
	virtual long		EndSubs(void);							// ROM 0x0021b3ac EndSubs__7TSIUnitFv (+0x60: no more subs - the delay dropped, the list compacted)
	virtual long		AddInterpretation(char* interp);		// ROM 0x0021ab10 AddInterpretation__7TSIUnitFPc (+0x64: ==> its index, -1 for no memory)
	virtual UnitInterpretation*	GetInterpretation(ULong index);	// ROM 0x0021ab88 GetInterpretation__7TSIUnitFUl (+0x68)
	virtual Boolean		CheckInterpretationIndex(ULong index);	// ROM 0x0021af50 CheckInterpretationIndex__7TSIUnitFUl (+0x6c)
	virtual long		DeleteInterpretation(ULong index);		// ROM 0x0021aba8 DeleteInterpretation__7TSIUnitFUl (+0x70: its param disposed)
	virtual long		InsertInterpretation(ULong index);		// ROM 0x0021ac08 InsertInterpretation__7TSIUnitFUl (+0x74: a slot opened; ==> its index, -1 for no memory)
	virtual char*		LockInterpretations(void);				// ROM 0x0021ac54 LockInterpretations__7TSIUnitFv (+0x78)
	virtual void		UnlockInterpretations(void);			// ROM 0x0021ac68 UnlockInterpretations__7TSIUnitFv (+0x7c)
	virtual void		CompactInterpretations(void);			// ROM 0x0021ac7c CompactInterpretations__7TSIUnitFv (+0x80)
	virtual long		InterpretationReuse(ULong count, ULong paramSize, ULong paramCount);	// ROM 0x0021a9c4 InterpretationReuse__7TSIUnitFUlN21 (+0x84: count interpretations kept, the rest deleted or made anew)
	virtual TDArray*	GetSubsCopy(void);						// ROM 0x0021a858 GetSubsCopy__7TSIUnitFv (+0x88: a list of the subs - the list itself, cloned, when there is one)
	virtual long		GetLabel(ULong index);					// ROM 0x0021ae38 GetLabel__7TSIUnitFUl (+0x8c)
	virtual long		GetScore(ULong index);					// ROM 0x0021ae60 GetScore__7TSIUnitFUl (+0x90)
	virtual long		GetAngle(ULong index);					// ROM 0x0021ae88 GetAngle__7TSIUnitFUl (+0x94)
	virtual TRecObject*	GetParam(ULong index);					// ROM 0x0021aeb0 GetParam__7TSIUnitFUl (+0x98)
	virtual void		SetLabel(ULong index, ULong label);		// ROM 0x0021aed8 SetLabel__7TSIUnitFUlT1 (+0x9c)
	virtual void		SetScore(ULong index, ULong score);		// ROM 0x0021af00 SetScore__7TSIUnitFUlT1 (+0xa0)
	virtual void		SetAngle(ULong index, long angle);		// ROM 0x0021af28 SetAngle__7TSIUnitFUll (+0xa4)
	virtual void		EndUnit(void);							// ROM 0x0021b40c EndUnit__7TSIUnitFv (+0xa8: the subs ended, the params compacted)

	void				CloseInterpList(void);					// ROM 0x0021a8d0 CloseInterpList__7TSIUnitFv - the list disposed, its element size kept
	long				OpenInterpList(void);					// ROM 0x0021a900 OpenInterpList__7TSIUnitFv - ==> 0, or -1 for no memory

	UChar				fSubKind;		// +0x30  kNoSubs, kOneSub, kSubList
	UChar				fHasInterps;	// +0x31  fInterps is a list (else its element size)
	TRecObject*			fSubs;			// +0x34  a TUnit, or a TDArray of TUnit*
	union {
		TDArray*		fInterps;		// +0x38  the interpretations
		ULong			fInterpSize;	// +0x38  their element size while there are none
	};
};

class TStrokeUnit : public TSIUnit
{
public:
	static TStrokeUnit*	Make(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas);	// ROM 0x0021f680 Make__11TStrokeUnitSFP7TDomainUlP7TStrokeP6TArray
	long				IStrokeUnit(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas);	// ROM 0x0021f708 IStrokeUnit__11TStrokeUnitFP7TDomainUlP7TStrokeP6TArray

	virtual void		Dump(TMsg* msg);						// ROM 0x0021e74c Dump__11TStrokeUnitFP4TMsg
	virtual void		IDispose(void);							// ROM 0x0021f788 IDispose__11TStrokeUnitFv (the stroke disposed when the type is still 'STRK')
	virtual long		CountStrokes(void);						// ROM 0x0021e6e0 CountStrokes__11TStrokeUnitFv (1)
	virtual TStroke*	GetStroke(ULong index);					// ROM 0x0021e6e8 GetStroke__11TStrokeUnitFUl
	virtual TUnitList*	GetAllStrokes(void);					// ROM 0x0021e708 GetAllStrokes__11TStrokeUnitFv (itself)
	virtual Boolean		OwnsStroke(void);						// ROM 0x0021e6f0 OwnsStroke__11TStrokeUnitFv (true)
	virtual ULong		ContextID(void);						// ROM 0x0021e6f8 ContextID__11TStrokeUnitFv
	virtual void		SetContextID(ULong id);					// ROM 0x0021e700 SetContextID__11TStrokeUnitFUl

	ULong				fContextID;		// +0x3c
	TStroke*			fStroke;		// +0x40
};

class TClickUnit : public TUnit
{
public:
	static TClickUnit*	Make(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas);	// ROM 0x0021cde8 Make__10TClickUnitSFP7TDomainUlP7TStrokeP6TArray
	long				IClickUnit(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas);	// ROM 0x0021ce70 IClickUnit__10TClickUnitFP7TDomainUlP7TStrokeP6TArray

	virtual void		Dump(TMsg* msg);						// ROM 0x0021cb5c Dump__10TClickUnitFP4TMsg
	virtual void		IDispose(void);							// ROM 0x0021cf10 IDispose__10TClickUnitFv (the stroke unbuffered and disposed)
	virtual long		MarkUnit(TUnitList* list, ULong flags);	// ROM 0x0021ced8 MarkUnit__10TClickUnitFP9TUnitListUl (the stroke let out of the inker's buffer)
	virtual long		CountStrokes(void);						// ROM 0x0021cbb4 CountStrokes__10TClickUnitFv (1)
	virtual TStroke*	GetStroke(ULong index);					// ROM 0x0021cbbc GetStroke__10TClickUnitFUl
	virtual Boolean		OwnsStroke(void);						// ROM 0x0021cbc4 OwnsStroke__10TClickUnitFv (true)

	TStroke*			fStroke;		// +0x30
};

class TClickEventUnit : public TSIUnit
{
public:
	static TClickEventUnit*	Make(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0021cbcc Make__15TClickEventUnitSFP7TDomainUlP6TArray
	long				IClickEventUnit(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0021cc44 IClickEventUnit__15TClickEventUnitFP7TDomainUlP6TArray

	virtual void		Dump(TMsg* msg);						// ROM 0x0021cd04 Dump__15TClickEventUnitFP4TMsg

	long				Event(void);							// ROM 0x0021cc84 Event__15TClickEventUnitFv - the click event of the sub's stroke, read once
	void				ClearEvent(void);						// ROM 0x0021ccc8 ClearEvent__15TClickEventUnitFv - the stroke's event marked processed (the unit keeps it)

	long				fEvent;			// +0x3c  -1 until read
};

// the inker's queue of strokes still being drawn (NOT YET: nothing on the host)
void	UnbufferStroke(TStroke* stroke);						// ROM 0x001fd474 UnbufferStroke__FP7TStroke
enum { kBufferedStroke = 0x10000000 };						// the stroke flag the inker holds it by

#endif	/* __UNIT_H */
