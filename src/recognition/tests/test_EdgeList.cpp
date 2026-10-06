// EdgeList test: the gesture domain's shape tests, over the standalone
// heap.  No controller and no units - the three tests take a TDArray of
// FPoints and an interpretation, so a gesture can be written here as the
// corners it comes down to, and Collapse2 given lists to tidy.
//
// The corner finder itself is driven through TEdgeListDomain::FindCorners
// at the end, over a stroke made by hand, which is the only part that
// needs a unit.

#include "EdgeList.h"
#include "Controller.h"
#include "Domain.h"
#include "Areas.h"
#include "Unit.h"
#include "Stroke.h"
#include "Angles.h"
#include "memory/host/KernelHeap.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// a corner list out of whole-pixel coordinates
static TDArray*
Corners(const long* xy, long count)
{
	TDArray* array = TDArray::Make(sizeof(FPoint), count);
	for (long i = 0; i < count; i++)
	{
		FPoint p;
		p.x = xy[2 * i] << 16;
		p.y = xy[2 * i + 1] << 16;
		array->SetEntry(i, (char*) &p);
	}
	return array;
}


static void
TestALine()
{
	static const long line[] = { 10, 10, 60, 10 };
	TDArray* corners = Corners(line, 2);
	UnitInterpretation interp;
	InitInterpretation(&interp, 0, 0);
	EXPECT(TestLine(corners, &interp));
	EXPECT(interp.label == kGestureLine && interp.score == 0);
	EXPECT(interp.angle == 0x5a0000);			// to the right: 90 degrees
	// the other tests want more corners than a line has
	EXPECT(!TestCarets(corners, &interp));
	FRect box;
	box.left = box.top = 0;
	box.right = box.bottom = 100 << 16;
	EXPECT(!TestScrub(corners, &box, &interp));
	corners->Dispose();

	// three corners are not a line
	static const long bent[] = { 10, 10, 60, 10, 60, 60 };
	corners = Corners(bent, 3);
	EXPECT(!TestLine(corners, &interp));
	corners->Dispose();
}


static void
TestACaret()
{
	// the classic caret: up and over, the arms the same length and the
	// point facing straight up the screen
	static const long caret[] = { 20, 60, 40, 20, 60, 60 };
	TDArray* corners = Corners(caret, 3);
	UnitInterpretation interp;
	InitInterpretation(&interp, 0, 0);
	EXPECT(TestCarets(corners, &interp));
	EXPECT(interp.label == kGestureCaret && interp.score == 0);
	corners->Dispose();

	// one arm more than twice the other, with four corners, is nothing
	static const long lopsided[] = { 38, 24, 40, 20, 60, 60, 20, 60 };
	corners = Corners(lopsided, 4);
	EXPECT(!TestCarets(corners, &interp));
	corners->Dispose();

	// the same arms turned on their side are a caret too: once the two
	// meet sharply enough the ROM stops caring which way they face, and
	// only the angle it answers says which way it lies
	static const long sideways[] = { 20, 20, 60, 60, 20, 100 };
	corners = Corners(sideways, 3);
	EXPECT(TestCarets(corners, &interp));
	EXPECT(interp.label == kGestureCaret);
	// the angle is half way between the two arms measured out from the
	// point, which is the way the caret opens: to the right
	corners->Dispose();
}


// A caret's long arm split to the short one's length (TestCarets, ROM BUG
// (fixed)): the ROM gives Interpolate the arms' ratio for a distance, so
// the new corner lands a third of a pixel from the point; the fix puts it
// the first arm's length along the second.
static void
TestTheSplitArm()
{
	static const long arms[] = { 0, 20, 0, 0, 60, 0 };
	UnitInterpretation interp;
	for (int fixed = 0; fixed < 2; fixed++)
	{
		SetRomBugFixed(fixed != 0);
		TDArray* corners = Corners(arms, 3);
		InitInterpretation(&interp, 0, 0);
		TestCarets(corners, &interp);
		EXPECT(corners->fCount == 4);
		FPoint split;
		memcpy(&split, corners->GetEntry(2), sizeof(FPoint));
		FPoint end;
		memcpy(&end, corners->GetEntry(3), sizeof(FPoint));
		EXPECT(end.x == (60 << 16) && end.y == 0);
		EXPECT(split.y == 0);
		if (fixed)
			EXPECT(split.x > (19 << 16) && split.x < (21 << 16));
		else
			EXPECT(split.x > 0 && split.x < (1 << 16));
		corners->Dispose();
	}
	SetRomBugFixed(true);
}


static void
TestAScrub()
{
	// a zig-zag back and forth across a word, stepping down each time:
	// five corners, four turns, every one of them sharp
	static const long scrub[] = { 20, 20, 80, 24, 20, 28, 80, 32, 20, 36 };
	TDArray* corners = Corners(scrub, 5);
	UnitInterpretation interp;
	InitInterpretation(&interp, 0, 0);
	FRect box;
	box.left = 20 << 16;
	box.top = 20 << 16;
	box.right = 80 << 16;
	box.bottom = 36 << 16;
	EXPECT(TestScrub(corners, &box, &interp));
	EXPECT(interp.label == kGestureScrub && interp.score == 0);
	corners->Dispose();

	// fewer than five corners is never a scrub
	static const long two[] = { 20, 20, 80, 24, 20, 28, 80, 32 };
	corners = Corners(two, 4);
	EXPECT(!TestScrub(corners, &box, &interp));
	corners->Dispose();

	// a gentle wave turns too little: the turns are not sharp enough
	static const long wave[] = { 20, 20, 40, 10, 60, 20, 80, 10, 100, 20 };
	corners = Corners(wave, 5);
	EXPECT(!TestScrub(corners, &box, &interp));
	corners->Dispose();
}


static void
TestCollapse()
{
	// two corners within seven units of each other: one of them goes
	static const long close[] = { 10, 10, 40, 10, 43, 11, 70, 40 };
	TDArray* corners = Corners(close, 4);
	Collapse2(corners);
	EXPECT(corners->fCount == 3);
	corners->Dispose();

	// three corners in a line, the middle one turning by nothing at all:
	// the second pass drops it
	static const long straight[] = { 10, 10, 40, 10, 70, 10 };
	corners = Corners(straight, 3);
	Collapse2(corners);
	EXPECT(corners->fCount == 2);
	corners->Dispose();

	// a real corner is kept
	static const long corner[] = { 10, 10, 40, 10, 40, 40 };
	corners = Corners(corner, 3);
	Collapse2(corners);
	EXPECT(corners->fCount == 3);
	corners->Dispose();

	// two corners are left alone whatever they are
	static const long pair[] = { 10, 10, 11, 10 };
	corners = Corners(pair, 2);
	Collapse2(corners);
	EXPECT(corners->fCount == 2);
	corners->Dispose();
}


static void
TestTurns()
{
	TurnData turns;
	InitTurnData(&turns);
	EXPECT(turns.fCount == 0);

	FPoint a, b;
	a.x = 10 << 16;	a.y = 10 << 16;
	b.x = 11 << 16;	b.y = 10 << 16;
	NewTurn(&turns, a, b);
	EndTurn(&turns, b);
	EXPECT(turns.fCount == 0);					// shorter than six units: not kept

	b.x = 40 << 16;
	NewTurn(&turns, a, b);
	EndTurn(&turns, b);
	EXPECT(turns.fCount == 1);
	EXPECT(turns.fSlopes[0] == -0x5a0000);		// from b back to a: -90 degrees

	// a second turn the other way is half a turn from the first, which is
	// as far apart as a scrub's turns are allowed to be
	turns.fSlopes[1] = turns.fSlopes[0] + 0x5a0000;
	turns.fCount = 2;
	EXPECT(ValidTurnSequence(&turns));
	turns.fSlopes[1] = turns.fSlopes[0] + 0xb40000;
	EXPECT(!ValidTurnSequence(&turns));
}


// FindCorners over a stroke of its own: an L, whose corner the splitter
// has to find.
static void
TestFindCorners()
{
	gController = TController::Make();
	TDomain* domain = TEdgeListDomain::Make(gController);
	EXPECT(domain != nil && domain->fType == kEdgeListDomainType);

	TStroke* stroke = TStroke::Make(0);
	TabPt pt;
	pt.z = 3;
	pt.p = 0;
	for (long i = 0; i <= 20; i++)
	{
		pt.x = (Fixed) (i <= 10 ? i * 4 : 40) << 16;
		pt.y = (Fixed) (i <= 10 ? 0 : (i - 10) * 4) << 16;
		stroke->AddPoint(&pt);
	}
	stroke->fDownTime = 1000;
	stroke->fUpTime = 1010;
	stroke->EndStroke();

	TStrokeUnit* strokeUnit = TStrokeUnit::Make(domain, 2, stroke, nil);
	TSIUnit* unit = TEdgeListUnit::Make(domain, 3, nil);
	unit->AddSub(strokeUnit);
	unit->EndSubs();

	((TEdgeListDomain*) domain)->FindCorners(unit);
	TDArray* corners = ((TEdgeListUnit*) unit)->GetCorners();
	EXPECT(corners != nil && corners->fCount >= 3);
	if (corners != nil)
	{
		// the two ends are the first and last points of the stroke, and
		// the bend is somewhere in between
		FPoint* p = (FPoint*) corners->GetEntry(0);
		EXPECT(p->x == 0 && p->y == 0);
		p = (FPoint*) corners->GetEntry(corners->fCount - 1);
		EXPECT(p->x == (40 << 16) && p->y == (40 << 16));
		Collapse2(corners);
		EXPECT(corners->fCount == 3);			// the L comes down to three
	}
	unit->Dispose();
}


int
main()
{
	InitHostStandaloneHeap();
	TestALine();
	TestACaret();
	TestTheSplitArm();
	TestAScrub();
	TestCollapse();
	TestTurns();
	TestFindCorners();
	if (failures != 0)
	{
		fprintf(stderr, "test_EdgeList: %d failures\n", failures);
		return 1;
	}
	printf("test_EdgeList: all tests passed\n");
	return 0;
}
