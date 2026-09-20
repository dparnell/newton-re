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

				NOT YET RECONSTRUCTED: the deciding itself - `DoArbitration`,
				`GatherUnits`, `ArbitrateUnits`, `ArbitrateGraphicsWords`,
				`WaitingForOtherUnits` and `AllUnitsPresent`.  Until they
				are here the arbiter gathers nothing and `CleanUp` only
				clears away what the controller has already claimed.

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
struct ArbiterEntry
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

	void				DoArbitration(void);				// ROM 0x002085ec DoArbitration__8TArbiterFv (NOT YET)
	void				CleanUp(void);						// ROM 0x00207de0 CleanUp__8TArbiterFv

	TDArray*			Pending(void)	{ return (TDArray*) fLists[kArbiterPending]; }
	TDArray*			Active(void)	{ return (TDArray*) fLists[kArbiterActive]; }

	TController*		fController;	// +0x00
	TArray*				fLists[kArbiterListCount];	// +0x04
	Boolean				fArbitrateNow;	// +0x20  decide now, whatever is still coming
	Boolean				fWaiting;		// +0x21  an entry is waiting on units not yet made
	ULong				fUnused24;		// +0x24
};

extern TArbiter*	gArbiter;							// ROM 0x0c101880 gArbiter

#endif	/* __ARBITER_H */
