// Controller test: TController over the standalone heap, driven by hand
// with no views and no arbiter deciding anything.
//
// What is checked is the loop the recogniser runs on: a click unit offered
// as a piece is queued for the domains its area runs over clicks, the
// group pass hands it to the stroke domain, which makes a 'STRK' unit of
// the finished stroke and gives it back as a group; the classify pass
// hands that to its own domain, which offers it as a piece in its turn,
// so it ends up on the piece list and off the unit list.  Along the way:
// the domain levels Initialize works out, the delay a unit waits, the
// lists GetUList answers, the marking that follows a unit up its tree,
// and the clean-up that throws everything away and keeps the strokes.

#include "Controller.h"
#include "Arbiter.h"
#include "Domain.h"
#include "Areas.h"
#include "Unit.h"
#include "Stroke.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static TStroke*
MakeStroke(long x0, long y0, long x1, long y1, ULong down, ULong up)
{
	TStroke* stroke = TStroke::Make(0);
	TabPt pt;
	pt.z = 3;
	pt.p = 0;
	pt.x = (Fixed) x0 * 0x10000;
	pt.y = (Fixed) y0 * 0x10000;
	stroke->AddPoint(&pt);
	pt.x = (Fixed) x1 * 0x10000;
	pt.y = (Fixed) y1 * 0x10000;
	stroke->AddPoint(&pt);
	stroke->fDownTime = down;
	stroke->fUpTime = up;
	stroke->EndStroke();
	return stroke;
}


// A domain that stands in for the ones above the strokes: it counts the
// pieces it is offered and never takes any, so the group queue keeps
// them and the test can look at what was queued.
class TCountingDomain : public TDomain
{
public:
	virtual long	Group(TUnit* unit, dInfoRec* info);

	long			fGrouped;
	TUnit*			fLast;
	dInfoRec*		fLastInfo;
};

long
TCountingDomain::Group(TUnit* unit, dInfoRec* info)
{
	fGrouped++;
	fLast = unit;
	fLastInfo = info;
	return 0;
}


// The areas the test hands out: one area, given to every piece.
static TRecArea* gTestArea = nil;

static ULong
TestHitTest(TUnit* /*unit*/, TArray* areas)
{
	return ((TAreaList*) areas)->AddArea(gTestArea);
}

static long gExpired = 0;

static void
TestExpireStroke(TUnit* /*unit*/)
{
	gExpired++;
}


static Assoc
MakeAssoc(ULong type, TDomain* domain, ULong arbitrate)
{
	Assoc assoc;
	memset(&assoc, 0, sizeof(assoc));
	assoc.fType = type;
	assoc.fDomain = domain;
	assoc.fArbitrateTime = arbitrate;
	assoc.fSharedParams = true;			// nothing of ours to free
	return assoc;
}


int
main()
{
	InitHostStandaloneHeap();

	TController* controller = TController::Make();
	EXPECT(controller != nil && controller->fPieces != nil && controller->fUnits != nil);
	EXPECT(controller->fGroupQ != nil && controller->fGroupQ->ElementSize() == (long) sizeof(GroupEntry));
	gController = controller;
	TArbiter* arbiter = TArbiter::Make(controller);
	EXPECT(arbiter != nil && controller->fArbiter == arbiter);
	EXPECT(arbiter->Pending()->ElementSize() == (long) sizeof(ArbiterEntry));

	// nothing is due yet and there is nothing to wait for
	EXPECT(controller->Idle() == -1);

	gRootDomain = TDomain::Make(controller, kRootDomainType, (char*) "Root");
	gStrokeDomain = TStrokeDomain::Make(controller);
	EXPECT(gStrokeDomain->fType == kStrokeUnit);
	EXPECT(gStrokeDomain->fPieceTypes->FindType(kClickUnit) == 0);
	EXPECT(controller->GetTypedDomain(kStrokeUnit) == gStrokeDomain);
	EXPECT(controller->GetTypedDomain('XXXX') == nil);

	// a domain over the strokes, and one over that
	TCountingDomain* words = new TCountingDomain;
	words->IDomain(controller, kWordUnit, (char*) "Words");
	words->AddPieceType(kStrokeUnit);
	words->fGrouped = 0;
	words->fDelay = 4;
	controller->RegisterDomain(words);
	TCountingDomain* lines = new TCountingDomain;
	lines->IDomain(controller, 'LINE', (char*) "Lines");
	lines->AddPieceType(kWordUnit);
	lines->fGrouped = 0;
	controller->RegisterDomain(lines);

	// the strokes are two away, what takes strokes is three, and so on
	controller->Initialize();
	EXPECT(gStrokeDomain->fLevel == 2 && words->fLevel == 3 && lines->fLevel == 4);

	// an area that runs the stroke domain over clicks and the word domain
	// over strokes, and arbitrates both
	gTestArea = TRecArea::Make(0, 0);
	gTestArea->fViewId = 77;
	gTestArea->fTypes = TTypeAssoc::Make();
	gTestArea->fDomains = TTypeAssoc::Make();
	Assoc a;
	a = MakeAssoc(kClickUnit, gStrokeDomain, 0);
	gTestArea->fDomains->AddAssoc(&a);
	a = MakeAssoc(kStrokeUnit, words, 0);
	gTestArea->fDomains->AddAssoc(&a);
	a = MakeAssoc(kStrokeUnit, gStrokeDomain, 0);
	gTestArea->fTypes->AddAssoc(&a);
	a = MakeAssoc(kClickUnit, gStrokeDomain, kArbitrateExternally);
	gTestArea->fTypes->AddAssoc(&a);
	controller->SetHitTestRoutine(TestHitTest);
	controller->SetExpireStrokeRoutine(TestExpireStroke);

	// a click, offered as a piece
	TStroke* stroke = MakeStroke(10, 20, 30, 40, 1000, 1010);
	TClickUnit* click = TClickUnit::Make(gRootDomain, 1, stroke, nil);
	EXPECT(controller->NewClassification(click) == 0);
	EXPECT(controller->fPieces->Count() == 1 && controller->fPieces->GetUnit(0) == click);
	EXPECT(click->fMinStroke == 0 && click->fMaxStroke == 0 && controller->fNextStroke == 1);
	// one group entry - the stroke domain takes clicks - and no arbiter
	// entry, because the area arbitrates clicks externally
	EXPECT(controller->fGroupQ->Count() == 1);
	EXPECT(((GroupEntry*) controller->fGroupQ->GetEntry(0))->fDomain == gStrokeDomain);
	EXPECT(arbiter->Pending()->Count() == 0);
	EXPECT(controller->IsExternallyArbitrated(click));
	// and the group pass has been asked for
	EXPECT(controller->fGroupTime != 0xffffffff);

	// the group pass: the stroke is finished, so it becomes a 'STRK' unit
	controller->DoGroup();
	EXPECT(controller->fGroupQ->Count() == 0);
	EXPECT(controller->fUnits->Count() == 1);
	TUnit* strokeUnit = controller->fUnits->GetUnit(0);
	EXPECT(strokeUnit->fType == kStrokeUnit && strokeUnit->fDomain == gStrokeDomain);
	EXPECT(strokeUnit->SubCount() == 1 && ((TSIUnit*) strokeUnit)->GetSub(0) == click);
	EXPECT(click->fDuration == 10);						// the pen-up time less the pen-down
	EXPECT(controller->GetIndexedStroke(0) == nil);		// not a piece yet
	EXPECT(controller->IsLastCompleteStroke(strokeUnit));

	// it is not delayed (the stroke domain has no delay), so the classify
	// pass hands it straight to its domain, which offers it as a piece
	EXPECT(strokeUnit->fDelay == 0);
	EXPECT(!controller->DoClassify());
	EXPECT(controller->fUnits->Count() == 0);			// off the unit list
	EXPECT(controller->fPieces->Count() == 2);
	EXPECT(controller->GetIndexedStroke(0) == strokeUnit);
	// the word domain takes strokes, so there is a group entry for it, and
	// the area arbitrates strokes, so the arbiter has one too
	EXPECT(controller->fGroupQ->Count() == 1);
	EXPECT(((GroupEntry*) controller->fGroupQ->GetEntry(0))->fDomain == words);
	EXPECT(arbiter->Pending()->Count() == 1);
	EXPECT(((ArbiterEntry*) arbiter->Pending()->GetEntry(0))->fUnit == strokeUnit);

	// the word domain is offered it and does not take it, so the entry stays
	controller->DoGroup();
	EXPECT(words->fGrouped == 1 && words->fLast == strokeUnit);
	EXPECT(controller->fGroupQ->Count() == 1);
	controller->DoGroup();
	EXPECT(words->fGrouped == 2 && controller->fGroupQ->Count() == 1);

	// a unit its domain does delay waits: NewGroup gives it the domain's
	// delay and the classify pass leaves it alone and asks to be run again
	TStroke* stroke2 = MakeStroke(50, 50, 60, 60, GetTicks(), GetTicks());
	TStrokeUnit* word = TStrokeUnit::Make(words, 2, stroke2, nil);
	word->fStartTime = GetTicks();
	controller->NewGroup(word);
	EXPECT(word->fDelay == 4 && controller->fUnits->Count() == 1);
	EXPECT(controller->fClassifyTime != 0xffffffff);
	controller->DoClassify();
	EXPECT(controller->fUnits->Count() == 1);			// still waiting

	// the lists GetUList answers.  Note that the flags a unit must have
	// are tested with TestFlags, so asking for none at all matches
	// nothing: GetDelayList asks for kDelayedUnit, which is what a unit
	// that is still waiting carries.
	TUnitList* delayed = controller->GetDelayList(nil, kStrokeUnit);
	EXPECT(delayed != nil && delayed->Count() == 1 && delayed->GetUnit(0) == word);
	delayed->Dispose();
	TUnitList* none = controller->GetDelayList(gStrokeDomain, kStrokeUnit);
	EXPECT(none != nil && none->Count() == 0);		// not the stroke domain's
	none->Dispose();

	// TimeOut takes the wait off every unit of a type
	controller->TimeOut(kStrokeUnit);
	EXPECT(word->fDelay == 0);
	delayed = controller->GetDelayList(nil, kStrokeUnit);
	EXPECT(delayed != nil && delayed->Count() == 0);
	delayed->Dispose();

	// marking a unit marks everything built over it: the click is under
	// the stroke unit, so marking the click marks both
	controller->MarkUnits(click, kClaimedUnit);
	EXPECT(click->TestFlags(kClaimedUnit) && strokeUnit->TestFlags(kClaimedUnit));
	EXPECT(!word->TestFlags(kClaimedUnit));

	// a group entry whose piece is gone is emptied, not removed
	long queued = controller->fGroupQ->Count();
	controller->CleanGroupQ(strokeUnit);
	EXPECT(controller->fGroupQ->Count() == queued);
	EXPECT(((GroupEntry*) controller->fGroupQ->GetEntry(0))->fPiece == nil);
	// and a nil entry is dropped by the next group pass
	controller->DoGroup();
	EXPECT(controller->fGroupQ->Count() == 0);

	// the arbiter's clean-up takes the claimed units and pieces out
	arbiter->CleanUp();
	EXPECT(controller->fPieces->Count() == 0);

	// everything thrown away: the strokes are kept by the first call and
	// go in the second
	TStroke* stroke3 = MakeStroke(1, 1, 2, 2, 1, 2);
	TClickUnit* click3 = TClickUnit::Make(gRootDomain, 1, stroke3, nil);
	controller->NewClassification(click3);
	controller->DoGroup();
	EXPECT(controller->fUnits->Count() >= 1);
	controller->CleanUpUnits(false);
	EXPECT(controller->fPieces->Count() == 0);			// the click went, the strokes stayed
	controller->ExpireAllStrokes();
	EXPECT(gExpired == 0);								// none of them is a stroke piece yet
	controller->CleanUpUnits(true);
	EXPECT(controller->fUnits->Count() == 0);
	controller->ClearController();
	EXPECT(controller->fPieces->Count() == 0 && controller->fGroupQ->Count() == 0);

	// an error throws the state away and starts again.  The times are not
	// put back - CleanupAfterError clears the flags and the lists but
	// nothing resets the four - so the idle after it is still due at once
	// and answers no wait rather than "nothing to wait for".
	controller->SignalMemoryError();
	EXPECT(controller->ControllerError());
	EXPECT(controller->Idle() == 0);
	EXPECT(!controller->ControllerError());
	EXPECT(controller->fPieces->Count() == 0 && controller->fUnits->Count() == 0);

	printf(failures == 0 ? "test_Controller: all passed\n" : "test_Controller: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
