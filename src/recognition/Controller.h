/*
	File:		recognition/Controller.h

	Contains:	TController (gController), the engine in the middle of the
				recogniser.  Everything the pen writes arrives here as a
				*piece* - a click unit from the stroke world - and leaves
				as a *unit* some domain has made of it.  The controller
				keeps the two lists, runs the work in four passes, and
				times each pass so that nothing is decided before the pen
				has had its chance to add to it:

				  Group     each piece offered to the domains that take
				            its type (the group queue, built when the
				            piece arrives), each of which may make a unit
				            of it - TStrokeDomain turns a finished click
				            into a 'STRK' unit.
				  Classify  each unit handed to its own domain, which
				            (through NewClassification) makes it a piece
				            in its turn, so a stroke becomes a word's
				            piece and a word a sentence's.
				  Arbitrate the arbiter decides which of the units built
				            over the same strokes wins (Arbiter.h).
				  CleanUp   the lists compacted.

				`Idle` runs whichever passes are due and answers how long
				to wait before the next one (`NextIdleTime` asks without
				running anything); `TriggerRecognition` makes all four due
				at once.  A unit's `delay` holds its pass back while the
				pen might still be adding to it (`NoEventsWithinDelay`).

				An area (Areas.h) says which domains run over a piece and
				with which parameters: `NewClassification` looks the piece's
				type up in the area it was written in and queues one group
				entry per domain that takes it.  A type the area marks with
				arbitrate time 2 is *externally arbitrated* - handled as
				soon as it is made, which is how a click reaches the view
				while the pen is still down.

				NOT YET RECONSTRUCTED: the
				per-area recognition of `RecognizeInArea` (re-recognising
				the strokes of an existing area) and `BuildGTypes`.

	Reconstructed from the MP2x00 US ROM (0x00209e84-0x0020c7a0); each
	function cites its origin.
*/

#ifndef __CONTROLLER_H
#define __CONTROLLER_H

#include "RecObject.h"
#include "Unit.h"
#include "Areas.h"

class TDomain;
class TArbiter;
struct dInfoRec;


// One entry of the group queue: a piece, and one domain that takes its
// type with the information that domain is to be run with.  The piece is
// cleared (not removed) when the unit goes away, so the queue may hold
// nil entries until the next CleanUp.
struct GroupEntry
{
	TUnit*		fPiece;			// +0x00  nil: the piece is gone
	TDomain*	fDomain;		// +0x04
	dInfoRec*	fInfo;			// +0x08  the domain's own record for it
	Handle		fParams;		// +0x0c  the parameter block to run it with
};


// The flags the controller keeps in its own TRecObject::fFlags.
enum
{
	kControllerError	= 0x10000000,	// out of memory: everything is thrown away and started again
	kControllerBusy		= 0x40000000,	// a unit is being handled: no recognition
	kControllerInternal	= 0x08000000	// inside RecognizeInArea: every pass runs whether it is due or not
};

// A unit the controller will not work on any further: claimed by a
// recogniser, invalidated, or already handed on as a piece.  (0x20000000
// is the bit a *stroke* carries as kStrokeNoInk; nothing in the ROM's
// recogniser sets it on a unit, but the controller tests for it here.)
enum
{
	kUnitDone			= 0x68000000,
	kUnitDoneOrPassed	= 0x68200000,
	kUnitClassified		= 0x00200000,	// handed on to the piece list: off the unit list at the next classify
	kUnitLateStroke		= 0x00040000	// an expired stroke among more than fifty: let go without grouping
};


class TController : public TRecObject
{
public:
	static TController*	Make(void);								// ROM 0x00209e84 Make__11TControllerSFv
	void				IController(void);						// ROM 0x0020a7a8 IController__11TControllerFv
	void				Initialize(void);						// ROM 0x0020c554 Initialize__11TControllerFv - the domains ordered by how far they are from the strokes

	virtual void		Dispose(void);							// ROM 0x0020b1e8 Dispose__11TControllerFv

	void				RegisterDomain(TDomain* domain);		// ROM 0x00209f34 RegisterDomain__11TControllerFP7TDomain
	void				RegisterArbiter(TArbiter* arbiter);		// ROM 0x00209f70 RegisterArbiter__11TControllerFP8TArbiter
	TDomain*			GetTypedDomain(ULong type);				// ROM 0x0020c6fc GetTypedDomain__11TControllerFUl

	ULong				NewClassification(TUnit* piece);		// ROM 0x0020a38c NewClassification__11TControllerFP5TUnit - a new piece: its areas found and the group queue filled; ==> 1 for no memory
	ULong				QueuePiece(TUnit* piece, TRecArea* area);	// the group-queue and arbiter entries an area asks for a piece (the tail of NewClassification, shared with RegroupSub)
	void				NewGroup(TUnit* unit);					// ROM 0x0020a970 NewGroup__11TControllerFP5TUnit - a unit a domain has made
	Boolean				IsExternallyArbitrated(TUnit* unit);	// ROM 0x0020a724 IsExternallyArbitrated__11TControllerFP5TUnit - its area gives its type arbitrate time 2
	void				RegroupUnclaimedSubs(TUnit* unit);		// ROM 0x0020a804 RegroupUnclaimedSubs__11TControllerFP5TUnit
	void				RegroupSub(TUnit* unit, TUnit* sub);	// ROM 0x0020a880 RegroupSub__11TControllerFP5TUnitT1 - the sub put back on the group queue

	long				Idle(void);								// ROM 0x0020aa04 Idle__11TControllerFv - the passes that are due; ==> milliseconds to the next, -1 for never
	long				NextIdleTime(void);						// ROM 0x0020ab30 NextIdleTime__11TControllerFv - the same answer without running anything
	void				TriggerRecognition(void);				// ROM 0x0020ab08 TriggerRecognition__11TControllerFv - every pass due now
	void				DoGroup(void);							// ROM 0x0020ad54 DoGroup__11TControllerFv
	Boolean				DoClassify(void);						// ROM 0x0020af60 DoClassify__11TControllerFv
	void				DoArbitration(void);					// ROM 0x0020ac60 DoArbitration__11TControllerFv
	void				CleanUp(void);							// ROM 0x0020b688 CleanUp__11TControllerFv
	void				TimeOut(ULong type);					// ROM 0x0020b604 TimeOut__11TControllerFUl - the units of a type made ready at once
	Boolean				NoEventsWithinDelay(TUnit* unit, ULong arg);	// ROM 0x0020b2a0 NoEventsWithinDelay__11TControllerFP5TUnitUl - whether the unit's delay has passed with nothing else written in its area

	TUnitList*			GetUList(TDomain* domain, ULong type, ULong has, ULong hasNot);	// ROM 0x0020b6dc GetUList__11TControllerFP7TDomainUlN22
	TUnitList*			GetDelayList(TDomain* domain, ULong type);	// ROM 0x0020b7ec GetDelayList__11TControllerFP7TDomainUl - the delayed units of a type
	TUnit*				GetIndexedStroke(ULong index);			// ROM 0x0020b80c GetIndexedStroke__11TControllerFUl - the 'STRK' piece that is stroke number index
	void				DeletePiece(long index);				// ROM 0x0020b940 DeletePiece__11TControllerFl
	void				DeleteUnit(long index);					// ROM 0x0020b97c DeleteUnit__11TControllerFl
	void				MarkUnits(TUnit* unit, ULong flags);	// ROM 0x0020b9b8 MarkUnits__11TControllerFP5TUnitUl - the unit and everything built over it flagged
	void				CleanGroupQ(TUnit* unit);				// ROM 0x0020bd9c CleanGroupQ__11TControllerFP5TUnit - its group entries emptied
	Boolean				CheckBusy(void);						// ROM 0x0020bc68 CheckBusy__11TControllerFv
	void				UpdateInk(FRect* bounds);				// ROM 0x0020bc88 UpdateInk__11TControllerFP5FRect - the ink of the strokes it still holds drawn again
	TUnit*				GetClickInProgress(void);				// ROM 0x0020c090 GetClickInProgress__11TControllerFv
	Boolean				IsLastCompleteStroke(TUnit* unit);		// ROM 0x0020c0e8 IsLastCompleteStroke__11TControllerFP5TUnit
	void				ExpireAllStrokes(void);					// ROM 0x0020beb0 ExpireAllStrokes__11TControllerFv
	void				CleanUpUnits(Boolean all);        		// ROM 0x0020c140 CleanUpUnits__11TControllerFUc
	void				ClearArbiter(void);						// ROM 0x0020c354 ClearArbiter__11TControllerFv
	void				ClearController(void);					// ROM 0x0020c444 ClearController__11TControllerFv
	void				SignalMemoryError(void);				// ROM 0x0020bdf4 SignalMemoryError__11TControllerFv
	Boolean				ControllerError(void);					// ROM 0x0020be3c ControllerError__11TControllerFv
	void				CleanupAfterError(void);				// ROM 0x0020be44 CleanupAfterError__11TControllerFv

	void				SetExpireStrokeRoutine(void (*routine)(TUnit*));	// ROM 0x0020bd94 SetExpireStrokeRoutine__11TControllerFPFP5TUnit_v
	void				BuildGTypes(TRecArea* area);			// ROM 0x0021c7cc BuildGTypes__11TControllerFP8TRecArea - the domains an area must run for the types its recognisers take
	void				SetHitTestRoutine(ULong (*routine)(TUnit*, TArray*));	// ROM 0x0021c7c4 SetHitTestRoutine__11TControllerFPFP5TUnitP6TArray_Ul

	// Strokes recognised there and then, in an area of the caller's own
	// rather than the ones the pen is over - which is how writing already
	// on a page is read again.  The strokes become stroke units spread
	// over the last second, every area type with no handler of its own
	// answers through `handler` (called with each winning unit and `arg`),
	// and the controller is idled until every stroke is accounted for.
	void				RecognizeInArea(TArray* strokes, TRecArea* area, ULong (*handler)(TUnit*, ULong), ULong arg);	// ROM 0x0020a1d8 RecognizeInArea__11TControllerFP6TArrayP8TRecAreaPFP5TUnitUl_UlUl

	TUnitList*			fPieces;		// +0x08  what the domains group: the clicks and the units handed on
	TUnitList*			fUnits;			// +0x0c  what the domains have made and not yet classified
	TArray*				fDomains;		// +0x10
	TArray*				fGroupQ;		// +0x14  GroupEntry
	TArbiter*			fArbiter;		// +0x18
	UShort				fNextStroke;	// +0x1c  the number the next piece's stroke range starts at
	UShort				fUnused1e;		// +0x1e
	ULong				fClassifyTime;	// +0x20  when each pass is next due (-1: not due)
	ULong				fGroupTime;		// +0x24
	ULong				fArbitrateTime;	// +0x28
	ULong				fCleanUpTime;	// +0x2c
	ULong				fUnused30[2];	// +0x30
	ULong				(*fHitTest)(TUnit*, TArray*);	// +0x38  the areas a unit lies in (Areas.h's GetAreasHit)
	void				(*fExpireStroke)(TUnit*);		// +0x3c  a stroke nobody wanted
	Boolean				fInArea;		// +0x40  inside RecognizeInArea
	ULong				fAreaCount;		// +0x44  its state: the strokes to do, the ones done,
	ULong				fAreaDone;		// +0x48
	ULong				fAreaArg;		// +0x4c
	TRecArea*			fArea;			// +0x50  the area they are done in,
	ULong				(*fSavedHitTest)(TUnit*, TArray*);	// +0x54  and the routines put back afterwards
	void				(*fSavedExpire)(TUnit*);			// +0x58
	ULong				(*fAreaHandler)(TUnit*, ULong);		// +0x5c
};

Boolean	ClickInProgress(TUnit* unit);					// ROM 0x0020c4b4 ClickInProgress__FP5TUnit - the click of the stroke the pen is writing
Boolean	UnitsHitSameArea(TUnit* a, TUnit* b);			// ROM 0x0021c68c UnitsHitSameArea__FP5TUnitT1
void	TimeOutSubs(TSIUnit* unit);						// ROM 0x0020b58c TimeOutSubs__FP7TSIUnit
void	HandleAreaSwitched(TDomain* domain, Handle params);	// ROM 0x0020ac84 HandleAreaSwitched__FP7TDomainPPc

// What RecognizeInArea puts in place while it runs: every unit is in its
// area, the winners go to its handler, and a stroke nobody wanted is
// handed to it as well (and counted done).
ULong	SpecialGetAreasHit(TUnit* unit, TArray* areas);		// ROM 0x0020a0ac SpecialGetAreasHit__FP5TUnitP6TArray
long	SpecialHandler(TArray* units);						// ROM 0x0020a0e4 SpecialHandler__FP6TArray
void	SpecialExpireStroke(TUnit* unit);					// ROM 0x0020a188 SpecialExpireStroke__FP5TUnit
extern ULong	gLastWordEndTime;							// ROM 0x0c104c88 gLastWordEndTime - when the last strokes RecognizeInArea read were taken to end

void	SetDomainDelays(TController* controller, ULong delay);	// ROM 0x0020c4f4 SetDomainDelays__FP11TControllerUl - every domain that waits made to wait this long

extern TController*	gController;							// ROM 0x0c10187c gController

// The controller's state put aside (TRecognitionManager::
// SaveRecognitionState): its flags, lists and pass times, and the
// arbiter's; the controller meanwhile starts with empty lists.
struct ArbiterState;
struct ControllerState		// 0x28 bytes in the ROM
{
	ULong			fFlags;
	TUnitList*		fPieces;
	TUnitList*		fUnits;
	TArray*			fGroupQ;
	ULong			fClassifyTime;
	ULong			fGroupTime;
	ULong			fArbitrateTime;
	ULong			fCleanUpTime;
	ArbiterState*	fArbiter;
	UChar			fFailed;
};
ControllerState*	SaveRecognitionState(TController* controller, UChar* failed);	// ROM 0x0020b894 SaveRecognitionState__FP11TControllerPUc
void				RestoreRecognitionState(TController* controller, ControllerState* state);	// ROM 0x0020c294 RestoreRecognitionState__FP11TControllerUl

// Whether anything has been written since a unit's last stroke.
Boolean	AreStrokesAfterUnit(TUnit* unit);						// ROM 0x0020c018 AreStrokesAfterUnit__FP5TUnit

#endif	/* __CONTROLLER_H */
