// The word domain and the units it works in, over the standalone heap.
//
// The engine behind the domain is a protocol, so the test registers one
// of its own: it answers "ink" for anything, which is what the ROM's
// own engine comes to when it cannot read the writing, and counts the
// calls so that the domain's side of each one can be checked.  What is
// checked is the domain being a shim - every question handed straight
// on - and the word unit keeping its readings: an interpretation with a
// word in it, set from Unicode and from characters, and given back with
// the interpretation when it goes.

#include "WRecDomain.h"
#include "WordUnit.h"
#include "Controller.h"
#include "Arbiter.h"
#include "Domain.h"
#include "Unit.h"
#include "Stroke.h"
#include "Unicode.h"
#include "Ports.h"			// ToFixed
#include "NewtonExceptions.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


/*------------------------------------------------------------------------------
	A n   e n g i n e   t h a t   a n s w e r s   i n k
------------------------------------------------------------------------------*/

struct EngineCalls
{
	long	fInitialize;
	long	fGroup;
	long	fClassify;
	long	fReclassify;
	long	fUnitInfoFree;
	long	fSleep;
	long	fWakeUp;
	long	fVerify;
	long	fConfidence;
};
static EngineCalls	gCalls;
static Boolean		gEngineThrows = false;
static const char*	gLastFreed = nil;


PROTOCOL TTestWRecognizer : public TWRecognizer
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TTestWRecognizer);

	TTestWRecognizer*	New(void);
	void				Delete(void);

	void	Initialize(void);
	void	Group(TStrokeUnit* stroke);
	long	Classify(TWRecUnit* unit);
	long	Reclassify(TWRecUnit* unit);
	long	FindBaseline(TStroke** strokes, Point* out);
	void	GroupInkStroke(TStrokeUnit* stroke, ULong a, ULong b, Boolean flag);
	long	AreaInfoGetSize(void);
	void	AreaInfoFillDefaults(Handle info);
	void	AreaInfoConfigure(Handle info, RefArg config);
	void	AreaInfoFreeDependents(Handle info);
	void	AreaInfoSetParameters(Handle info);
	void	UnitInfoFreePtr(char* info);
	Boolean	VerifyWordSymbols(UniChar* word);
	long	UnitConfidence(TWRecUnit* unit);
	void	Sleep(void);
	void	WakeUp(void);
};

PROTOCOL_IMPL_SOURCE_MACRO(TTestWRecognizer)
PROTOCOL_CLASSINFO(TTestWRecognizer, "TWRecognizer", "", 0, 0, nil)

TTestWRecognizer*	TTestWRecognizer::New(void)			{ return this; }
void	TTestWRecognizer::Delete(void)					{ }
void	TTestWRecognizer::Initialize(void)				{ gCalls.fInitialize++; }
// the ROM's own engines all group this way: the word still being built
// takes the stroke, or a new one is started from it
void
TTestWRecognizer::Group(TStrokeUnit* stroke)
{
	gCalls.fGroup++;
	if (gEngineThrows)
		Throw(exAbort, nil, nil);
	UChar found = 0;
	TWRecUnit* group = (TWRecUnit*) GetPartialGroup(&found);
	if (found == 0)
		MakeNewGroupFromStroke(stroke);
	else
		AddSub(group, stroke);
}
long	TTestWRecognizer::Classify(TWRecUnit*)			{ gCalls.fClassify++; if (gEngineThrows) Throw(exAbort, nil, nil); return 0; }
long	TTestWRecognizer::Reclassify(TWRecUnit*)		{ gCalls.fReclassify++; return 0; }
long	TTestWRecognizer::FindBaseline(TStroke**, Point*)	{ return 1; }
void	TTestWRecognizer::GroupInkStroke(TStrokeUnit*, ULong, ULong, Boolean)	{ }
long	TTestWRecognizer::AreaInfoGetSize(void)			{ return 0; }
void	TTestWRecognizer::AreaInfoFillDefaults(Handle)	{ }
void	TTestWRecognizer::AreaInfoConfigure(Handle, RefArg)	{ }
void	TTestWRecognizer::AreaInfoFreeDependents(Handle)	{ }
void	TTestWRecognizer::AreaInfoSetParameters(Handle)	{ }
void	TTestWRecognizer::UnitInfoFreePtr(char* info)	{ gCalls.fUnitInfoFree++; gLastFreed = info; }
Boolean	TTestWRecognizer::VerifyWordSymbols(UniChar*)	{ gCalls.fVerify++; return true; }
long	TTestWRecognizer::UnitConfidence(TWRecUnit*)	{ gCalls.fConfidence++; return kWRecInk; }
void	TTestWRecognizer::Sleep(void)					{ gCalls.fSleep++; }
void	TTestWRecognizer::WakeUp(void)					{ gCalls.fWakeUp++; }


/*------------------------------------------------------------------------------
	T h e   t e s t s
------------------------------------------------------------------------------*/

static void
TestNoEngine(TController* controller)
{
	// with nothing registered there is no domain to be had, and the
	// half-made one is given back rather than left about
	EXPECT(TWRecDomain::Make(controller) == nil);
}


static TWRecDomain*
TestDomain(TController* controller)
{
	TTestWRecognizer::ClassInfo()->Register();
	memset(&gCalls, 0, sizeof(gCalls));
	TWRecDomain* domain = (TWRecDomain*) TWRecDomain::Make(controller);
	EXPECT(domain != nil);
	if (domain == nil)
		return nil;
	EXPECT(gCalls.fInitialize == 1);
	EXPECT(domain->fType == kWRecDomainType);
	EXPECT(domain->fDelay == 0x78);
	EXPECT(domain->fController == controller);
	// it takes strokes and nothing else
	EXPECT(domain->fPieceTypes->FindType(kStrokeUnitType) == 0);
	EXPECT(domain->fPieceTypes->FindType(kClickUnit) == (ULong) -1);
	EXPECT(controller->GetTypedDomain(kWRecDomainType) == domain);

	// every question goes straight to the engine
	EXPECT(domain->UnitConfidence(nil) == kWRecInk && gCalls.fConfidence == 1);
	UniChar word[2] = { 'a', 0 };
	EXPECT(domain->VerifyWordSymbols(word) && gCalls.fVerify == 1);
	domain->Sleep();
	domain->WakeUp();
	EXPECT(gCalls.fSleep == 1 && gCalls.fWakeUp == 1);
	return domain;
}


static void
TestUnit(TWRecDomain* domain)
{
	TWRecUnit* unit = TWRecUnit::Make(domain, 1, nil);
	EXPECT(unit != nil);
	if (unit == nil)
		return;
	// it takes its type from the domain that made it
	EXPECT(unit->fType == kWRecDomainType && unit->fDomain == domain);
	EXPECT(unit->InterpretationCount() == 0);

	// a reading: an interpretation with a word in it
	long first = unit->AddWordInterpretation();
	EXPECT(first == 0 && unit->InterpretationCount() == 1);
	Handle string = unit->GetString(0);
	EXPECT(string != nil && *(UniChar*) *string == 0);		// empty to begin with
	UniChar hello[6] = { 'h', 'e', 'l', 'l', 'o', 0 };
	EXPECT(unit->SetWordString(0, hello) != nil);
	EXPECT(Ustrcmp((UniChar*) *unit->GetString(0), hello) == 0);
	unit->SetLabel(0, 3);
	unit->SetScore(0, 17);
	EXPECT(unit->GetLabel(0) == 3 && unit->GetScore(0) == 17);

	// a second one, set from characters
	long second = unit->AddWordInterpretation();
	EXPECT(second == 1 && unit->InterpretationCount() == 2);
	unit->SetCharWordString(1, "Hi");
	const UniChar* read = (const UniChar*) *unit->GetString(1);
	EXPECT(Ustrlen(read) == 2 && read[0] == 'H' && read[1] == 'i');
	// the two are separate strings
	EXPECT(unit->GetString(0) != unit->GetString(1));
	EXPECT(Ustrcmp((UniChar*) *unit->GetString(0), hello) == 0);

	// the parameter is the word, not a recogniser object
	EXPECT(unit->GetParam(0) == nil);
	// and its size counts the words in
	EXPECT(unit->SizeInBytes() > 0);

	// deleting a reading gives its word back with it
	EXPECT(unit->DeleteInterpretation(1) == 1);
	EXPECT(unit->InterpretationCount() == 1);
	EXPECT(Ustrcmp((UniChar*) *unit->GetString(0), hello) == 0);

	// with nothing measured, the word stands on the bottom of its box
	FRect box;
	box.left = ToFixed(10);
	box.top = ToFixed(20);
	box.right = ToFixed(50);
	box.bottom = ToFixed(40);
	unit->SetBBox(&box);
	FPoint left, right;
	unit->GetWordBase(&left, &right, 0);
	EXPECT(left.x == box.left && left.y == box.bottom);
	EXPECT(right.x == box.right && right.y == box.bottom);
	EXPECT(unit->GetWordSlant(0) == 0 && unit->GetWordSize(0) == 0);

	// the engine's working store goes back through the domain when the
	// unit does
	char store[4];
	unit->fUnitInfo = store;
	gLastFreed = nil;
	long before = gCalls.fUnitInfoFree;
	unit->Dispose();
	EXPECT(gCalls.fUnitInfoFree == before + 1 && gLastFreed == store);
}


static void
TestClassify(TWRecDomain* domain, TController* controller)
{
	// the engine reads nothing, so the unit is marked invalid and closed
	// - but it still goes back to the controller as a piece, because only
	// an *invalidated* unit is held back
	TWRecUnit* unit = TWRecUnit::Make(domain, 1, nil);
	EXPECT(unit != nil);
	if (unit == nil)
		return;
	long pieces = controller->fPieces->Count();
	domain->Classify(unit);
	EXPECT(gCalls.fClassify == 1);
	EXPECT(unit->TestFlags(kInvalidUnit) != 0);
	EXPECT(controller->fPieces->Count() == pieces + 1);

	// one that has been invalidated is not offered
	TWRecUnit* gone = TWRecUnit::Make(domain, 1, nil);
	gone->SetFlags(kInvalidatedUnit);
	pieces = controller->fPieces->Count();
	domain->Classify(gone);
	EXPECT(controller->fPieces->Count() == pieces);
	gone->Dispose();

	// and an engine that throws is taken as having run out of memory:
	// the failure is counted and the unit marked invalid just the same
	TWRecUnit* other = TWRecUnit::Make(domain, 1, nil);
	gEngineThrows = true;
	long errors = gRecMemErrCount;
	domain->Classify(other);
	gEngineThrows = false;
	EXPECT(gRecMemErrCount == errors + 1);
	EXPECT(other->TestFlags(kInvalidUnit) != 0);
	other->Dispose();
	unit->Dispose();
}


static TStroke*
MakeStroke(long x0, long y0, long x1, long y1, ULong down, ULong up)
{
	TStroke* stroke = TStroke::Make(0);
	TabPt pt;
	pt.z = 3;
	pt.p = 0;
	pt.x = ToFixed(x0);
	pt.y = ToFixed(y0);
	stroke->AddPoint(&pt);
	pt.x = ToFixed(x1);
	pt.y = ToFixed(y1);
	stroke->AddPoint(&pt);
	stroke->fDownTime = down;
	stroke->fUpTime = up;
	stroke->EndStroke();
	return stroke;
}


// Two strokes offered to the domain end up in one word, because the
// engine asks for the word still being built and adds to it.
static void
TestGrouping(TWRecDomain* domain, TController* controller)
{
	TStroke* first = MakeStroke(10, 20, 30, 40, 100, 110);
	TStroke* second = MakeStroke(32, 20, 50, 40, 120, 130);
	TStrokeUnit* a = TStrokeUnit::Make(gRootDomain, 1, first, nil);
	TStrokeUnit* b = TStrokeUnit::Make(gRootDomain, 1, second, nil);
	EXPECT(a != nil && b != nil);
	if (a == nil || b == nil)
		return;

	long groups = gCalls.fGroup;
	domain->Group(a, nil);
	EXPECT(gCalls.fGroup == groups + 1);
	// the word the engine started is being held back, so the next
	// stroke finds it
	UChar found = 0;
	TWRecUnit* word = (TWRecUnit*) domain->fRecognizer->GetPartialGroup(&found);
	EXPECT(found != 0 && word != nil);
	if (word == nil)
		return;
	EXPECT(word->fType == kWRecDomainType);
	EXPECT(word->SubCount() == 1 && word->GetSub(0) == a);

	domain->Group(b, nil);
	EXPECT(word->SubCount() == 2 && word->GetSub(1) == b);
	// the word covers both strokes, in space and in time
	EXPECT(domain->fRecognizer->GetStartTime(word) == 100);
	EXPECT(domain->fRecognizer->GetEndTime(word) >= 130);
	EXPECT(domain->fRecognizer->StrokeSize(a) == first->Count());
	EXPECT(domain->fRecognizer->StrokeUnitStroke(a) == first);
	EXPECT(domain->fRecognizer->GetStartTime(first) == 100);
	EXPECT(domain->fRecognizer->GetEndTime(first) == 110);

	// a reading put on it through the protocol's own calls
	long interp = domain->fRecognizer->AddWordInterpretation(word);
	EXPECT(interp == 0);
	UniChar hi[3] = { 'h', 'i', 0 };
	EXPECT(domain->fRecognizer->SetWordString(word, 0, hi) != nil);
	domain->fRecognizer->SetLabel(word, 0, 2);
	domain->fRecognizer->SetScore(word, 0, 9);
	EXPECT(domain->fRecognizer->GetLabel(word, 0) == 2);
	EXPECT(domain->fRecognizer->GetScore(word, 0) == 9);
	EXPECT(domain->fRecognizer->InterpretationCount(word) == 1);
	EXPECT(Ustrcmp((UniChar*) *domain->fRecognizer->GetWordString(word, 0), hi) == 0);

	// the flags the engine works in
	EXPECT(domain->fRecognizer->TestRejectedUnit(word) == 0);
	domain->fRecognizer->RejectUnit(word);
	EXPECT(domain->fRecognizer->TestRejectedUnit(word) != 0);
	EXPECT(domain->fRecognizer->TestInvalidUnit(word) == 0);
	domain->fRecognizer->InvalidateUnit(word);
	EXPECT(domain->fRecognizer->TestInvalidUnit(word) != 0);

	// its own working store, kept on the unit
	char store[8];
	domain->fRecognizer->UnitInfoSetPtr(word, store);
	EXPECT(domain->fRecognizer->UnitInfoGetPtr(word) == store);
	domain->fRecognizer->UnitInfoSetPtr(word, nil);

	// closed: it stops waiting
	EXPECT(word->fDelay != 0);
	domain->fRecognizer->EndSubs(word);
	EXPECT(word->fDelay == 0);
	controller->CleanUp();
}


// (the protocol registry is a monitor, so the test runs as the kernel
//  services task rather than over the standalone heap)
static void
WRecScenario(void)
{
	TController* controller = TController::Make();
	gController = controller;
	TArbiter* arbiter = TArbiter::Make(controller);
	EXPECT(arbiter != nil);
	gRootDomain = TDomain::Make(controller, kRootDomainType, (char*) "Root");

	TestNoEngine(controller);
	TWRecDomain* domain = TestDomain(controller);
	if (domain != nil)
	{
		TestUnit(domain);
		TestGrouping(domain, controller);
		TestClassify(domain, controller);
	}

	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = WRecScenario;
	OsBoot();
	if (failures == 0)
		printf("test_WRecDomain: all passed\n");
	else
		printf("test_WRecDomain: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
