// Unit test: the recogniser's units over the standalone heap - a click
// unit over a stroke (its box and times), a click-event unit over it, a
// TSIUnit's subs (box, times and stroke range merged; the list folding
// back to one) and interpretations (added, best, reused, deleted), the
// stroke count across subs, unit and type lists, areas and area lists,
// the root domain, the recognisers of TRecognitionManager and the public
// face's bounds, tap and stroke; then the tablet buffer and the stroke
// queue: pen records in, strokes out, with the click events the queue
// notes (a tap, a hilite click, a double tap).  No views (FindView,
// Invalidate and the stroke world's clicks need the root view:
// test_Views).
#include "Unit.h"
#include "UnitPublic.h"
#include "Areas.h"
#include "Domain.h"
#include "Recognizer.h"
#include "TabletBuffer.h"
#include "StrokeQueue.h"
#include "StrokeCentral.h"
#include "Commands.h"
#include "Rects.h"
#include "memory/host/KernelHeap.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"

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


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x80000;
	InitObjects();
	// the preferences and the locale the recognition system reads when
	// it starts, which the boot has made long before anything is written
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, RefVar(AllocateFrame()));
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(AllocateFrame()));
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	// a machine at this level builds no dictionaries, and the ROM then
	// reads the dictionary preferences over a list that is not there (the
	// bug written down in ReadDictPrefs); an empty list stands in for it
	SetFrameSlot(RefVar(gVarFrame), RSSYMdictionaries, RefVar(MakeArray(0)));
	gRecognition.Init(1);
	EXPECT(gRootDomain != nil && gRootDomain->fType == kRootDomainType && gRootDomain->fDelay == 0 && gRootDomain->fPieceTypes->Count() == 0);
	EXPECT(gRecognition.fRecognizers->Count() == 4);	// the gesture, click-event, stroke and click recognisers
	TRecognizer* clicks = gRecognition.fRecognizers->FindRecognizer(kClickUnit);
	TRecognizer* events = gRecognition.fRecognizers->FindRecognizer(kClickEventUnit);
	EXPECT(clicks != nil && clicks->Command() == aeClick && clicks->TestFlags(kRecognizerStrokeBounds) && clicks->ArbitrateTime() == 2 && clicks->ServicesEnabled() == 0x200);
	EXPECT(events != nil && events->ID() == kClickEventUnit && events->Command() == aeTap && gRecognition.fRecognizers->FindRecognizer(kWordUnit) == nil);
	EXPECT(gRecognition.fRecognizers->GetRecognizer(3) == clicks);	// installed last
	TRecognizer* strokeRec = gRecognition.fRecognizers->FindRecognizer(kStrokeUnit);
	EXPECT(strokeRec != nil && strokeRec->Command() == aeStroke && strokeRec->Domain() == gStrokeDomain
		&& strokeRec->ArbitrateTime() == kArbitrateAtOnce && strokeRec->ServicesEnabled() == vStrokesAllowed);

	// a click unit over a stroke
	TStroke* stroke = MakeStroke(10, 20, 30, 40, 1000, 1010);
	TClickUnit* click = TClickUnit::Make(gRootDomain, 1, stroke, nil);
	EXPECT(click != nil && click->fType == kClickUnit && click->fKind == 1 && click->fDomain == gRootDomain);
	EXPECT(click->fBBox.left == 10 && click->fBBox.top == 20 && click->fBBox.right == 30 && click->fBBox.bottom == 40);
	EXPECT(click->fStartTime == 1000 && click->fDuration == 0 && click->fDelay == 0 && !click->TestFlags(kDelayedUnit));
	EXPECT(click->CountStrokes() == 1 && click->GetStroke(0) == stroke && click->OwnsStroke() && click->SubCount() == 0 && click->GetBestInterpretation() == -1);
	EXPECT(click->fAreas == nil && click->GetArea() == nil && click->GetAreas() == nil && click->fSubRange == 0xffff);
	FRect box;
	click->GetBBox(&box);
	EXPECT(box.left == (Fixed) 10 << 16 && box.bottom == (Fixed) 40 << 16);
	EXPECT(CountStrokes(click) == 1);

	// the public face: bounds from the stroke (the click recogniser's flag), a pixel over
	{
		TUnitPublic pub(click, 0);
		Rect r;
		pub.Bounds(&r);
		EXPECT(r.left == 10 && r.top == 20 && r.right == 31 && r.bottom == 41);
		EXPECT(!pub.IsTap() && pub.GetType() == kClickUnit && pub.StartTime() == 1000 && pub.EndTime() == 1000);
		TStrokePublic* face = pub.Stroke();
		EXPECT(face != nil && face->fStroke == stroke && !face->fOwnsStroke && pub.Stroke() == face && face->Size() == 2);
		EXPECT(pub.RequiredMask() == 0x200 && pub.ContextID() == 0);
	}
	// a tap
	TStroke* tapStroke = MakeStroke(50, 50, 52, 53, 2000, 2005);
	tapStroke->fClickEvent = kTapClick;
	TClickUnit* tap = TClickUnit::Make(gRootDomain, 1, tapStroke, nil);
	{
		TUnitPublic pub(tap, 0);
		EXPECT(pub.IsTap());
	}
	// a click event over the tap
	TClickEventUnit* event = TClickEventUnit::Make(gRootDomain, 1, nil);
	EXPECT(event != nil && event->fType == kClickEventUnit && event->fEvent == -1 && event->SubCount() == 0 && event->InterpretationCount() == 0);
	EXPECT(event->AddSub(tap) == 0 && event->SubCount() == 1 && event->GetSub(0) == tap && event->fSubKind == kOneSub);
	EXPECT(event->fBBox.left == 50 && event->fBBox.bottom == 53 && event->fStartTime == 2000 && event->fDuration == 0);
	EXPECT(event->Event() == kTapClick && tapStroke->fClickEvent == kTapClick);
	event->ClearEvent();
	EXPECT(event->Event() == kTapClick && tapStroke->fClickEvent == kProcessedClick);
	EXPECT(event->CountStrokes() == 1 && event->GetStroke(0) == tapStroke && event->GetStroke(1) == nil);
	{
		TUnitPublic pub(event, 0);
		Rect r;
		pub.Bounds(&r);
		EXPECT(r.left == 50 && r.right == 53 && pub.Stroke()->fStroke == tapStroke);
	}
	TUnitList* strokes = event->GetAllStrokes();
	EXPECT(strokes != nil && strokes->Count() == 1 && strokes->GetUnit(0) == tap);
	strokes->Dispose();

	// a sub/interpretation unit: subs merged, the list made and folded back
	TStroke* s1 = MakeStroke(0, 0, 10, 10, 100, 110);
	TStroke* s2 = MakeStroke(20, 5, 30, 25, 120, 130);
	TStroke* s3 = MakeStroke(-5, 30, 5, 35, 140, 160);
	TStrokeUnit* u1 = TStrokeUnit::Make(gRootDomain, 0, s1, nil);
	TStrokeUnit* u2 = TStrokeUnit::Make(gRootDomain, 0, s2, nil);
	TStrokeUnit* u3 = TStrokeUnit::Make(gRootDomain, 0, s3, nil);
	EXPECT(u1 != nil && u1->fType == kStrokeUnit && u1->fStartTime == 100 && u1->fDuration == 10 && u1->fStroke == s1 && u1->fContextID == 0);
	u1->fMinStroke = u1->fMaxStroke = 3;
	u2->fMinStroke = u2->fMaxStroke = 4;
	u3->fMinStroke = u3->fMaxStroke = 5;
	u3->SetContextID(77);
	EXPECT(u3->ContextID() == 77);
	gRootDomain->fDelay = 12;
	TSIUnit* group = new TSIUnit;
	group->ISIUnit(gRootDomain, kWordUnit, 0, nil, sizeof(UnitInterpretation));
	EXPECT(group->fDelay == 12 && group->TestFlags(kDelayedUnit) && group->fSubKind == kNoSubs && group->fInterpSize == sizeof(UnitInterpretation));
	EXPECT(group->AddSub(u1) == 0 && group->fSubKind == kOneSub && group->fSubs == u1);
	EXPECT(group->fBBox.left == 0 && group->fBBox.right == 10 && group->fStartTime == 100 && group->fDuration == 10 && group->fMinStroke == 3 && group->fMaxStroke == 3 && group->fSubRange == 0);
	EXPECT(group->AddSub(u2) == 1 && group->fSubKind == kSubList && group->SubCount() == 2 && group->GetSub(1) == u2 && group->GetSub(0) == u1);
	EXPECT(group->fBBox.left == 0 && group->fBBox.right == 30 && group->fBBox.bottom == 25 && group->fStartTime == 100 && group->fDuration == 30 && group->fMaxStroke == 4);
	EXPECT(group->AddSub(u3) == 2 && group->SubCount() == 3 && group->fBBox.left == -5 && group->fBBox.bottom == 35 && group->fDuration == 60 && group->fMaxStroke == 5);
	EXPECT(group->CountStrokes() == 3 && group->GetStroke(0) == s1 && group->GetStroke(2) == s3 && group->GetStroke(3) == nil);
	EXPECT(CountStrokes(group) == 3);
	TDArray* copy = group->GetSubsCopy();
	EXPECT(copy == group->fSubs && copy->fUsers == 1);
	copy->Release();
	TUnitList* all = group->GetAllStrokes();
	EXPECT(all != nil && all->Count() == 3 && all->GetUnit(2) == u3);
	all->Dispose();
	// marked: the subs go in the list, not the group
	TUnitList* marked = TUnitList::Make();
	group->ClaimUnit(marked);
	EXPECT(marked->Count() == 3 && group->TestFlags(kClaimedUnit) && u1->TestFlags(kClaimedUnit) && marked->GetUnit(1) == u2);
	EXPECT(!marked->AddUnique(u2) && marked->Count() == 3 && !marked->AddUnique(group) && marked->Count() == 4);
	marked->Dispose();
	// interpretations
	EXPECT(group->GetBestInterpretation() == -1 && group->GetInterpretation(0) == nil);
	UnitInterpretation interp;
	InitInterpretation(&interp, 0, 0);
	EXPECT(interp.label == -1 && interp.score == 10000 && interp.param == nil);
	interp.label = 'a';
	interp.score = 300;
	EXPECT(group->AddInterpretation((char*) &interp) == 0 && group->InterpretationCount() == 1 && group->fHasInterps == 1);
	interp.label = 'b';
	interp.score = 200;
	interp.angle = 45 << 16;
	EXPECT(group->AddInterpretation((char*) &interp) == 1 && group->InterpretationCount() == 2);
	interp.label = -1;
	interp.score = 1;
	EXPECT(group->AddInterpretation((char*) &interp) == 2);
	EXPECT(group->GetBestInterpretation() == 1 && group->GetLabel(1) == 'b' && group->GetScore(0) == 300 && group->GetAngle(1) == 45 << 16 && group->GetParam(1) == nil);
	EXPECT(group->CheckInterpretationIndex(2) && !group->CheckInterpretationIndex(3));
	group->SetScore(0, 100);
	group->SetLabel(2, 'c');
	group->SetAngle(2, 7);
	EXPECT(group->GetBestInterpretation() == 2 && group->GetAngle(2) == 7);
	EXPECT(group->DeleteInterpretation(2) == 1 && group->InterpretationCount() == 2 && group->GetBestInterpretation() == 0);
	EXPECT(group->InsertInterpretation(0) == 0 && group->InterpretationCount() == 3 && group->GetLabel(1) == 'a');
	group->InterpretationReuse(1, 0, 0);
	EXPECT(group->InterpretationCount() == 1);
	group->InterpretationReuse(3, sizeof(long), 2);
	EXPECT(group->InterpretationCount() == 3 && group->GetParam(2) != nil && ((TArray*) group->GetParam(2))->Count() == 2 && group->GetLabel(2) == -1);
	group->EndUnit();
	EXPECT(group->fDelay == 0 && !group->TestFlags(kDelayedUnit));
	group->InterpretationReuse(0, 0, 0);
	EXPECT(group->InterpretationCount() == 0 && group->fHasInterps == 0 && group->fInterpSize == sizeof(UnitInterpretation));
	// a sub deleted: the list folds back to one
	group->DeleteSub(1);
	EXPECT(group->SubCount() == 2 && group->GetSub(1) == u3);
	group->DeleteSub(0);
	EXPECT(group->SubCount() == 1 && group->fSubKind == kOneSub && group->GetSub(0) == u3);
	group->DeleteSub(0);
	EXPECT(group->SubCount() == 0 && group->fSubKind == kNoSubs);
	group->Dispose();
	gRootDomain->fDelay = 0;

	// unit and type lists
	TTypeList* types = TTypeList::Make();
	EXPECT(types != nil && !types->AddUnique(kClickUnit) && !types->AddUnique(kStrokeUnit) && !types->AddUnique(kClickUnit) && types->Count() == 2);
	EXPECT(types->FindType(kStrokeUnit) == 1 && types->FindType(kWordUnit) == (ULong) -1 && types->GetType(0) == kClickUnit);
	types->Dispose();
	gRootDomain->AddPieceType(kClickUnit);
	gRootDomain->AddPieceType(kClickUnit);
	EXPECT(gRootDomain->fPieceTypes->Count() == 1 && gRootDomain->fPieceTypes->fFree == 0);
	EXPECT(TDomain::VUnitInClass('WREC', kWordUnit) && !TDomain::VUnitInClass(kClickUnit, kWordUnit) && !TDomain::VUnitInClass('WREC', kClickUnit));

	// areas: one kept alone, two as a list, cloned by use
	TRecArea* a1 = TRecArea::Make(0x200, 0);
	TRecArea* a2 = TRecArea::Make(0x600, 0);
	a2->fViewId = 42;
	EXPECT(a1 != nil && a1->fUsers == 0 && a1->fViewFlags == 0x200);
	TAreaList* areas = TAreaList::Make();
	EXPECT(areas->AddArea(a1) == 0 && a1->fUsers == 1 && areas->Count() == 1);
	TClickUnit* c2 = TClickUnit::Make(gRootDomain, 1, MakeStroke(1, 1, 2, 2, 1, 2), areas);
	EXPECT(c2->fAreas == a1 && !c2->TestFlags(kAreaListUnit) && a1->fUsers == 2 && c2->GetArea() == a1);
	TAreaList* got = c2->GetAreas();
	EXPECT(got != nil && got != areas && got->Count() == 1 && got->GetArea(0) == a1 && a1->fUsers == 3);
	got->Dispose();
	EXPECT(a1->fUsers == 2);
	EXPECT(areas->AddArea(a2) == 0 && areas->Count() == 2 && areas->GetMergedArea() == a2 && areas->FindMatchingView(42) && !areas->FindMatchingView(7));
	c2->SetAreas(areas);
	EXPECT(c2->fAreas == areas && c2->TestFlags(kAreaListUnit) && areas->fUsers == 1 && a1->fUsers == 2 && a2->fUsers == 2 && c2->GetArea() == a2);		// (the list clones its areas)
	got = c2->GetAreas();
	EXPECT(got == areas && areas->fUsers == 2);
	got->Dispose();
	c2->DoneUsingUnit();
	EXPECT(c2->fAreas == nil && areas->fUsers == 0 && a1->fUsers == 1 && a2->fUsers == 1);
	areas->Dispose();
	EXPECT(a1->fUsers == 0 && a2->fUsers == 0);
	a1->Dispose();
	a2->Dispose();
	c2->Dispose();

	// the units go with their strokes (the click's, the stroke units' still 'STRK')
	click->Clone();
	click->Dispose();
	EXPECT(click->fUsers == 0);
	click->Dispose();
	tap->Dispose();
	event->Dispose();		// (the stroke units went with DeleteSub)

	// the recognition manager's clicks
	gRecognition.IgnoreClicks(100);
	EXPECT(gRecognition.fIgnoreClicksUntil != 0);
	gRecognition.SetNextClick(gRecognition.fIgnoreClicksUntil - 30);
	EXPECT(gRecognition.fIgnoreClicksUntil != 0);
	gRecognition.SetNextClick(gRecognition.fIgnoreClicksUntil + 1);
	EXPECT(gRecognition.fIgnoreClicksUntil == 0);
	gRecognition.SaveClickView((TView*) 1);
	gRecognition.SaveClickView((TView*) 2);
	EXPECT(gRecognition.fPrevClickView == (TView*) 1 && gRecognition.fClickView == (TView*) 2);
	gRecognition.RemoveClickView((TView*) 2);
	EXPECT(gRecognition.fClickView == nil && gRecognition.fPrevClickView == (TView*) 1);
	if (failures == 0)
		printf("test_Unit: all passed\n");
	else
		printf("test_Unit: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
