/*
	File:		recognition/Arbiter.h

	Contains:	TArbiter, the half of the recogniser that decides.  The
				controller (Controller.h) makes every unit the domains can
				see in what the pen wrote; the arbiter picks the ones that
				win, hands them to their recognisers and throws the rest
				away.

				A piece whose area arbitrates its type is entered on the
				arbiter's pending list when it is classified (one entry per
				type the area takes it as, carrying the area's association
				for it).  `DoArbitration` gathers the entries whose units
				are ready, works out which of the units built over the same
				strokes to keep, and `CleanUp` then clears the losers out
				of the controller's lists - regrouping the subs of a unit
				that lost, so that its strokes get another chance in
				another shape.

				`fWaiting` says an entry is waiting on units that have not
				been made yet; `fArbitrateNow` is what the controller sets
				when it will not wait any longer (it is out of memory, or
				nothing else is going to happen).

				The deciding goes: an entry whose type is arbitrated at
				once (or whose area has exactly one such type) simply
				wins; anything else is held until every stroke under it is
				covered by units at its own level (`GatherUnits`,
				`AllUnitsPresent`, `WaitingForOtherUnits`), and then
				`ArbitrateUnits` picks between what was gathered - by
				score (`GetBestInterpretation`) unless the area's mixture
				of scrubs, shapes and words calls for another rule
				(`GetRecognitionCase`).  The winners go to the area's
				handler and are marked claimed; everything else gathered
				is marked claimed and invalid, which is what `CleanUp`
				then clears out.

				NOT YET RECONSTRUCTED: `ArbitrateGraphicsWords` (a word
				drawn as a shape), the shape half of `ArbitrateEarly`, and
				`SetCaseAndTime` (the journal's replay).

	Reconstructed from the MP2x00 US ROM (0x00206bf0-0x00208ea0); each
	function cites its origin.
*/

#ifndef __ARBITER_H
#define __ARBITER_H

#include "RecObject.h"
#include "Areas.h"
#include "Unit.h"

class TController;


// One unit offered for arbitration as one of the types its area takes,
// with the association that says how (the recogniser, its parameters and
// the time it is arbitrated at).
struct BestMatch
{
	TUnit*		fUnit;			// +0x00
	ULong		fState;			// +0x04  0 until the arbitration has looked at it
	ULong		fArbitrateTime;	// +0x08  a copy of fAssoc.fArbitrateTime
	Assoc		fAssoc;			// +0x0c
};

// The arbiter's seven lists, in the order it holds them.
enum
{
	kArbiterPending = 0,		// entries waiting for their units (0x28 bytes each)
	kArbiterActive,				// the ones this arbitration is deciding between
	kArbiterGathered,			// the units it gathered to decide between
	kArbiterWinners,			// what it settled on, and hands to the area's handler
	kArbiterUnitsA,				// three working lists of unit pointers
	kArbiterUnitsB,
	kArbiterUnitsC,
	kArbiterListCount
};


class TArbiter
{
public:
	static TArbiter*	Make(TController* controller);		// ROM 0x00206bf0 Make__8TArbiterSFP11TController
	long				IArbiter(TController* controller);	// ROM 0x00207d2c IArbiter__8TArbiterFP11TController

	void				DoArbitration(void);				// ROM 0x002085ec DoArbitration__8TArbiterFv
	void				CleanUp(void);						// ROM 0x00207de0 CleanUp__8TArbiterFv
	Boolean				GatherUnits(ULong level, Boolean restore, TArray* out);	// ROM 0x00206cc4 GatherUnits__8TArbiterFUlUcP6TArray - the units that cover the same strokes; ==> whether they cover them all
	Boolean				ArbitrateUnits(TRecArea* area);		// ROM 0x00207118 ArbitrateUnits__8TArbiterFP8TRecArea - ==> whether anything won
	void				ArbitrateGraphicsWords(TArray* gathered);	// ROM 0x0020770c ArbitrateGraphicsWords__8TArbiterFP6TArray (NOT YET)
	Boolean				WaitingForOtherUnits(TRecArea* area, BestMatch* match);	// ROM 0x00208ca8 WaitingForOtherUnits__8TArbiterFP8TRecAreaP9BestMatch
	Boolean				AllUnitsPresent(TRecArea* area, BestMatch* match);	// ROM 0x00208dbc AllUnitsPresent__8TArbiterFP8TRecAreaP9BestMatch

	TDArray*			Pending(void)	{ return (TDArray*) fLists[kArbiterPending]; }
	TDArray*			Active(void)	{ return (TDArray*) fLists[kArbiterActive]; }
	TArray*				Gathered(void)	{ return fLists[kArbiterGathered]; }
	TArray*				Winners(void)	{ return fLists[kArbiterWinners]; }

	TController*		fController;	// +0x00
	TArray*				fLists[kArbiterListCount];	// +0x04
	Boolean				fArbitrateNow;	// +0x20  decide now, whatever is still coming
	Boolean				fWaiting;		// +0x21  an entry is waiting on units not yet made
	ULong				fCase;			// +0x24  the recognition case the journal's replay was recorded under
};

// The helpers the deciding is written in.
long		UnionStrokes(TDArray* strokes, TDArray* levels, ULong level, TDArray* add, ULong* wanted, ULong* last);	// ROM 0x00206f68 UnionStrokes__FP7TDArrayT1UlT1PUlT5
long		ArbiterGetUnitStrokes(TSIUnit* unit, TDArray* strokes);	// ROM 0x00208104 ArbiterGetUnitStrokes__FP7TSIUnitP7TDArray - the stroke numbers under a unit, in order
long		GetRecognitionCase(TRecArea* area);			// ROM 0x00208218 GetRecognitionCase__FP8TRecArea - the scrubs it takes, +2 for shapes, +4 for words
long		GetBestInterpretation(TArray* gathered, TArray* winners);	// ROM 0x00207608 GetBestInterpretation__FP6TArrayT1 - the lowest-scoring one wins
long		ArbitrateWithScrubs(TArray* gathered, TArray* winners);	// ROM 0x002074ac ArbitrateWithScrubs__FP6TArrayT1 - everything that is not a scrub wins
Boolean		ArbitrateEarly(BestMatch* match);			// ROM 0x00208adc ArbitrateEarly__FP9BestMatch - whether it can be decided without waiting
ULong		UnitInClass(ULong type, ULong classType);	// ROM 0x0038abd8 (unnamed) - the type itself, or one of the class's

// what gLastType is set to
enum
{
	kLastTypeShape		= 1,
	kLastTypeWord		= 2,
	kLastTypeNumber		= 3
};

void		SetCaseAndTime(TArray* winners, ULong time);	// ROM 0x0020830c SetCaseAndTime__FP6TArrayUl (NOT YET: the journal's replay)

extern long	gLastType;									// ROM 0x0c104c64 gLastType - what was recognised last: 1 a shape, 2 a word, 3 a number

extern TArbiter*	gArbiter;							// ROM 0x0c101880 gArbiter

// The arbiter's state put aside while something else recognises (a modal
// dialog's fork: TRecognitionManager::SaveRecognitionState) and put back
// afterwards; the arbiter meanwhile starts with lists of its own.
struct ArbiterState			// 0x24 bytes in the ROM
{
	TArray*		fLists[kArbiterListCount];
	Boolean		fArbitrateNow;
	Boolean		fWaiting;
	ULong		fCase;
};
long			InitArbiterState(TArbiter* arbiter);		// ROM 0x00206c28 InitArbiterState__FP8TArbiter - new, empty lists
ArbiterState*	SaveArbiterState(TArbiter* arbiter, UChar* failed);	// ROM 0x00208428 SaveArbiterState__FP8TArbiterPUc
long			RestoreArbiterState(TArbiter* arbiter, ArbiterState* state);	// ROM 0x00208500 RestoreArbiterState__FP8TArbiterUl

#endif	/* __ARBITER_H */
