/*
	File:		recognition/Arbiter.cpp

	Contains:	TArbiter (Arbiter.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Arbiter.h"
#include "Controller.h"
#include "Domain.h"
#include "Recognizer.h"
#include "ShapeDomain.h"		// TGeneralShapeUnit, GetAvgLength, gCurveFlag
#include "WordUnit.h"		// TStdWordUnit
#include "Unicode.h"			// Ustrlen
#include <string.h>

#include <stdio.h>
#include <stdlib.h>

long	gLastType = 0;						// ROM 0x0c104c64 gLastType

TArbiter*	gArbiter = nil;					// ROM 0x0c101880 gArbiter


// ROM 0x00207d2c IArbiter__8TArbiterFP11TController
// The seven lists: four of arbitration entries and three of bare unit
// pointers.  The first two and the last three are TDArrays - entries are
// taken out of the middle of them - and the two in between are plain
// arrays, cut back rather than unpicked.
long
TArbiter::IArbiter(TController* controller)
{
	fController = controller;
	fLists[kArbiterPending] = TDArray::Make(sizeof(BestMatch), 0);
	fLists[kArbiterActive] = TDArray::Make(sizeof(BestMatch), 0);
	fLists[kArbiterGathered] = TArray::Make(sizeof(BestMatch), 0);
	fLists[kArbiterWinners] = TArray::Make(sizeof(BestMatch), 0);
	fLists[kArbiterUnitsA] = TDArray::Make(sizeof(ULong), 0);
	fLists[kArbiterUnitsB] = TDArray::Make(sizeof(ULong), 0);
	fLists[kArbiterUnitsC] = TDArray::Make(sizeof(ULong), 0);
	fArbitrateNow = false;
	fWaiting = false;
	fCase = 0;
	return 0;
}


// ROM 0x00206bf0 Make__8TArbiterSFP11TController
TArbiter*
TArbiter::Make(TController* controller)
{
	TArbiter* arbiter = new TArbiter;
	if (arbiter != nil)
	{
		arbiter->IArbiter(controller);
		controller->RegisterArbiter(arbiter);
	}
	return arbiter;
}


// ROM 0x0038abd8 (unnamed)
// Whether a unit type is, or belongs to, a class of types.  (The ROM has
// no symbol for this one: it is the teq and tail call that stands in
// front of TDomain::VUnitInClass.)
ULong
UnitInClass(ULong type, ULong classType)
{
	if (type == classType)
		return 1;
	return TDomain::VUnitInClass(type, classType);
}


// ROM 0x0020830c SetCaseAndTime__FP6TArrayUl
// NOT YET RECONSTRUCTED: the journal's replay of *units* (as opposed to
// strokes, which testing/Journal.h replays), which marks each replayed
// winner with the recognition case the session was recorded under and
// the time it happened at.  Nothing replays units on the host.
void
SetCaseAndTime(TArray* /*winners*/, ULong /*time*/)
{ }


// ROM 0x00208104 ArbiterGetUnitStrokes__FP7TSIUnitP7TDArray
// The numbers of the strokes under a unit, added to a sorted list.  A
// stroke unit is one stroke and knows its number; anything else is the
// strokes of its subs.  ==> 1 for no memory.
long
ArbiterGetUnitStrokes(TSIUnit* unit, TDArray* strokes)
{
	if (unit == nil || strokes == nil)
		return 1;
	if (unit->fType == kStrokeUnit)
	{
		ULong number = unit->fMinStroke;
		long count = strokes->Count();
		long at = 0;
		ULong there = number + 1;
		while (at < count)
		{
			there = *(ULong*) strokes->GetEntry(at);
			if (there >= number)
				break;
			at++;
		}
		if (at == count)
			there = number + 1;			// past the end: it is not there
		if (there != number && strokes->InsertEntry(at, (char*) &number) == (ULong) -1)
			return 1;
		return 0;
	}
	long count = unit->SubCount();
	for (long i = 0; i < count; i++)
	{
		if (ArbiterGetUnitStrokes((TSIUnit*) unit->GetSub(i), strokes) != 0)
			return 1;
	}
	return 0;
}


// ROM 0x00206f68 UnionStrokes__FP7TDArrayT1UlT1PUlT5
// One unit's strokes folded into the set the arbitration is gathering
// over.  `strokes` is the sorted set and `levels` a count beside each of
// them - how many more units must still cover that stroke.  A stroke
// already in the set has its count taken down, and when it reaches zero
// one fewer stroke is wanted; a stroke that is new goes in with the
// starting count, and `last` follows the end of the set.
// ==> 1 for no memory.
long
UnionStrokes(TDArray* strokes, TDArray* levels, ULong level, TDArray* add, ULong* wanted, ULong* last)
{
	long at = 0;
	long count = strokes->Count();
	for (long i = 0; i < add->Count(); i++)
	{
		ULong stroke = *(ULong*) add->GetEntry(i);
		while (at < count && *(ULong*) strokes->GetEntry(at) < stroke)
			at++;
		if (at < count && *(ULong*) strokes->GetEntry(at) == stroke)
		{
			long* left = (long*) levels->GetEntry(at);
			if (--(*left) == 0)
				(*wanted)--;
		}
		else
		{
			(*wanted)++;
			if (strokes->InsertEntry(at, (char*) &stroke) == (ULong) -1
				|| levels->InsertEntry(at, (char*) &level) == (ULong) -1)
				return 1;
			if (at >= count)
				*last = stroke;
			at++;
			count++;
		}
	}
	return 0;
}


// ROM 0x00206cc4 GatherUnits__8TArbiterFUlUcP6TArray
// Everything that was built over the strokes this arbitration is about.
//
// The stroke set starts as the strokes already in the working list, each
// wanting `level` units over it - one for each ring of domains between it
// and the top.  Every entry on the active list whose stroke range meets
// the set is taken in, its own strokes folded into the set (which may
// widen it, and does when a word covers strokes the first unit did not),
// and the rounds go on until nothing new is taken in or every stroke has
// had its count run down to zero.  ==> whether every stroke is covered.
Boolean
TArbiter::GatherUnits(ULong level, Boolean restore, TArray* out)
{
	TDArray* strokes = (TDArray*) fLists[kArbiterUnitsA];
	TDArray* levels = (TDArray*) fLists[kArbiterUnitsB];
	TDArray* unitStrokes = (TDArray*) fLists[kArbiterUnitsC];
	Boolean failed = false;

	for (long i = 0; i < strokes->Count(); i++)
	{
		ULong* slot = (ULong*) levels->AddEntry();
		if (slot == nil)
		{
			failed = true;
			break;
		}
		*slot = level;
	}

	ULong wanted = 0;
	ULong last = 0;
	if (!failed)
	{
		ULong first = *(ULong*) strokes->GetEntry(0);
		wanted = strokes->Count();
		last = *(ULong*) strokes->GetEntry(wanted - 1);

		Boolean took;
		do
		{
			took = false;
			TArrayIterator iter;
			BestMatch* entry = (BestMatch*) Active()->GetIterator(&iter);
			for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (BestMatch*) iter.GetNext())
			{
				BestMatch match = *entry;
				TUnit* unit = match.fUnit;
				if (unit->TestFlags(kClaimedUnit | kGatheredUnit))
					continue;
				if ((ULong) unit->fMaxStroke < first || (ULong) unit->fMinStroke > last)
					continue;
				took = true;
				unit->SetFlags(kGatheredUnit);
				if (ArbiterGetUnitStrokes((TSIUnit*) unit, unitStrokes) != 0
					|| UnionStrokes(strokes, levels, level, unitStrokes, &wanted, &last) != 0)
				{
					failed = true;
					break;
				}
				BestMatch* gathered = (BestMatch*) out->AddEntry();
				if (gathered == nil)
				{
					failed = true;
					break;
				}
				*gathered = match;
				if (wanted == 0)
					break;
				unitStrokes->CutToIndex(0);
			}
		}
		while (!failed && took && wanted != 0);
	}

	if (!failed && restore)
	{
		TArrayIterator iter;
		BestMatch* entry = (BestMatch*) Active()->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (BestMatch*) iter.GetNext())
			entry->fUnit->UnsetFlags(kGatheredUnit);
	}
	unitStrokes->CutToIndex(0);
	levels->CutToIndex(0);
	if (failed)
	{
		fController->SignalMemoryError();
		return false;
	}
	return wanted == 0;
}


// ROM 0x00208dbc AllUnitsPresent__8TArbiterFP8TRecAreaP9BestMatch
// Whether everything the arbitration needs has been made: the unit in
// hand is gathered with the rest and the gather asked to cover every
// stroke at one level less than the area's own - the level the units
// under the top stand at.
Boolean
TArbiter::AllUnitsPresent(TRecArea* area, BestMatch* match)
{
	if (Active()->Count() < 2)
		return false;
	TSIUnit* unit = (TSIUnit*) match->fUnit;
	unit->SetFlags(kGatheredUnit);
	BestMatch* gathered = (BestMatch*) Gathered()->AddEntry();
	if (gathered != nil)
	{
		*gathered = *match;
		if (ArbiterGetUnitStrokes(unit, (TDArray*) fLists[kArbiterUnitsA]) == 0)
		{
			Boolean all = GatherUnits(area->fArbitrateNow - 1, true, Gathered());
			if (!fController->ControllerError())
			{
				((TDArray*) fLists[kArbiterUnitsA])->CutToIndex(0);
				unit->UnsetFlags(kGatheredUnit);
				return all;
			}
		}
	}
	fController->SignalMemoryError();
	return true;
}


// ROM 0x00208ca8 WaitingForOtherUnits__8TArbiterFP8TRecAreaP9BestMatch
// Whether the arbitration must wait.  The entry in hand goes on the
// active list, and the first active unit that has reached the area's own
// level is the one the gather is run from: if that gather does not cover
// every stroke, something is still to come and the arbitration waits.
// With no unit at the area's level at all, there is nothing to decide
// with and it waits too.
Boolean
TArbiter::WaitingForOtherUnits(TRecArea* area, BestMatch* match)
{
	BestMatch* entry = (BestMatch*) Active()->AddEntry();
	if (entry == nil)
	{
		fController->SignalMemoryError();
		return false;
	}
	*entry = *match;

	TArrayIterator iter;
	BestMatch* active = (BestMatch*) Active()->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, active = (BestMatch*) iter.GetNext())
	{
		TUnit* unit = active->fUnit;
		if (unit->TestFlags(kClaimedUnit) || (ULong) unit->fKind != area->fMaxLevel)
			continue;
		Active()->Lock();
		Boolean all = AllUnitsPresent(area, active);
		Active()->Unlock();
		if (fController->ControllerError())
		{
			fController->SignalMemoryError();
			return false;
		}
		return !all;
	}
	return true;
}


// ROM 0x00207608 GetBestInterpretation__FP6TArrayT1
// The lowest-scoring of what was gathered wins.  (The ROM starts from
// 10000, the worst score there is, with no match chosen: a gather that
// came out empty would copy from whatever the register held, but the
// arbitration never asks with an empty one.)
long
GetBestInterpretation(TArray* gathered, TArray* winners)
{
	gathered->Lock();
	long best = 10000;
	BestMatch* bestMatch = nil;
	TArrayIterator iter;
	BestMatch* entry = (BestMatch*) gathered->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (BestMatch*) iter.GetNext())
	{
		TSIUnit* unit = (TSIUnit*) entry->fUnit;
		long index = unit->GetBestInterpretation();
		unit->GetInterpretation(index);
		long score = unit->GetScore(index);
		if (score < best)
		{
			best = score;
			bestMatch = entry;
		}
	}
	BestMatch* winner = (BestMatch*) winners->AddEntry();
	if (winner != nil && bestMatch != nil)
		*winner = *bestMatch;
	gathered->Unlock();
	return winner == nil;
}


// ROM 0x002074ac ArbitrateWithScrubs__FP6TArrayT1
// With a scrub among them nothing is chosen between: everything that is
// not a scrub, not a replayed unit and not already invalid wins, and
// whichever kind came last is remembered (a shape, a word, or a number).
long
ArbitrateWithScrubs(TArray* gathered, TArray* winners)
{
	long failed = 0;
	gathered->Lock();
	TArrayIterator iter;
	BestMatch* entry = (BestMatch*) gathered->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (BestMatch*) iter.GetNext())
	{
		TUnit* unit = entry->fUnit;
		ULong type = unit->fType;
		if (UnitInClass(type, kScrubUnit) || UnitInClass(type, kReplayUnit)
			|| unit->TestFlags(kInvalidUnit))
			continue;
		BestMatch* winner = (BestMatch*) winners->AddEntry();
		if (winner == nil)
		{
			failed = 1;
			break;
		}
		*winner = *entry;
		if (UnitInClass(type, kWordUnit))
			gLastType = kLastTypeWord;
		else if (UnitInClass(type, kShapeUnit))
			gLastType = kLastTypeShape;
		else if (UnitInClass(type, 'CALC') || UnitInClass(type, 'CLMN'))
			gLastType = kLastTypeNumber;
	}
	gathered->Unlock();
	return failed;
}


// ROM 0x00208218 GetRecognitionCase__FP8TRecArea
// What kind of arbitration an area calls for: the number of scrub types
// it takes, plus 2 if it takes shapes and 4 if it takes words.
long
GetRecognitionCase(TRecArea* area)
{
	long scrubs = 0;
	Boolean shapes = false;
	Boolean words = false;
	TArrayIterator iter;
	Assoc* assoc = (Assoc*) area->fTypes->GetIterator(&iter);
	if (iter.fCount == 0)
		return 0;
	for (ULong i = 0; i < (ULong) iter.fCount; i++, assoc = (Assoc*) iter.GetNext())
	{
		ULong type = assoc->fType;
		if (UnitInClass(type, kScrubUnit))
			scrubs++;
		else if (UnitInClass(type, kShapeUnit))
			shapes = true;
		else if (UnitInClass(type, kWordUnit))
			words = true;
	}
	if (shapes)
		scrubs += 2;
	if (words)
		scrubs += 4;
	return scrubs;
}


// ROM 0x00208adc ArbitrateEarly__FP9BestMatch
// Whether a unit can be settled without waiting for the rest: a scrub
// over a single stroke that nothing else was written with, or a general
// shape meant as one.
//
// A shape settles early when it lies over one stroke nothing else was
// written with and snapped within five pixels of another (fSnapped 1,
// fSnapDist under 5.0), or when what it was read as is sure enough to be
// meant: a line or its like (labels 0 and 1; fewer than four elements of
// the shape wanted), a rectangle or its like (4 and 6; at most five), a
// curve (9 to 12; any), or a context unit (flag 0x100000; at most
// twenty) - and then only if its box is at least 25 pixels each way.
Boolean
ArbitrateEarly(BestMatch* match)
{
	TUnit* unit = match->fUnit;
	ULong type = unit->fType;
	if (UnitInClass(type, kScrubUnit))
	{
		if (OnlyStrokeWritten((TStrokeUnit*) ((TSIUnit*) unit)->GetSub(0)))
			return true;
	}
	if (type != kShapeUnit)
		return false;
	TGeneralShapeUnit* shape = (TGeneralShapeUnit*) unit;
	if (shape->fSnapped == 1 && shape->fSnapDist < 0x50000)
	{
		if (OnlyStrokeWritten((TStrokeUnit*) shape->GetSub(0)))
		{
			gLastType = kLastTypeShape;
			return true;
		}
	}
	TDArray* general = shape->GetGeneralShape();
	ULong elements = 0;
	Boolean context = shape->TestFlags(0x100000);		// a context unit (ShapeDomain.cpp)
	Boolean sure = true;
	ULong most = 0;
	if (shape->InterpretationCount() == 0)
		return false;
	switch (shape->GetLabel(0))
	{
	case 0: case 1:
		elements = 4;
		break;
	case 4: case 6:
		most = 5;
		break;
	case 9: case 10: case 11: case 12:
		break;
	default:
		sure = false;
		break;
	}
	if (context)
	{
		most = 0x14;
		sure = true;
	}
	if (general != nil && elements == 0)
		elements = (ULong) general->fCount;
	if (sure && (most == 0 || elements <= most))
	{
		FRect box;
		unit->GetBBox(&box);
		// (rHeight, rWidth: bottom - top, right - left)
		if (0x18ffff < box.bottom - box.top && 0x18ffff < box.right - box.left)
		{
			gLastType = kLastTypeShape;
			return true;
		}
	}
	return false;
}


// ROM 0x00207ccc GetFirstWordIndex__FP12TStdWordUnit
// The first of a word unit's interpretations that is not labelled 0x28;
// -1 when all of them are.
long
GetFirstWordIndex(TStdWordUnit* unit)
{
	ULong count = (ULong) unit->InterpretationCount();
	for (ULong i = 0; i < count; i++)
		if (unit->GetLabel(i) != 0x28)
			return (long) i;
	return -1;
}


// ROM 0x00207d34 GetGraphicBiasedScore__FP7TSIUnit
// A shape's score as it is weighed against a word's: a curve's (labels 9
// to 12) divided by three tenths of the shape's mean side (that capped at
// 30 pixels, the factor at least 1), which favours a big curve over a
// word; any other shape's as it is.
ULong
GetGraphicBiasedScore(TSIUnit* unit)
{
	ULong length = (ULong) GetAvgLength((TGeneralShapeUnit*) unit);
	if (length >= 0x1e)
		length = 0x1e;
	ULong factor = (length * 3) / 10;
	if (factor <= 1)
		factor = 1;
	long label = unit->GetLabel(0);
	if (label >= 9 && label <= 12)
		return (ULong) (uint32_t) unit->GetScore(0) / factor;
	return (ULong) unit->GetScore(0);
}


// ROM 0x002079f0 ArbitrateByRules__FP6TArrayT1UlN33
// One shape against one word, settled by rules before the scores are
// compared: the shape wins when it snapped onto another shape, when it
// is a curve and curves are looked for, when the word is a single
// letter and the shape is big (more than 30 pixels high or wide - 35 for
// an I or an a read beside a rectangle-like shape, labels 4 to 7), or
// when the word has no reading of one letter and the shape is a
// triangle-like one (label 8).  ==> 1 when the shape was put on the
// winners, 0 when the rules do not decide (or either unit is invalid, or
// there is not exactly one of each), -1 for no memory.
long
ArbitrateByRules(TArray* gathered, TArray* winners, ULong shapeScore, ULong shapes, ULong wordScore, ULong words)
{
	BestMatch shapeMatch;
	BestMatch wordMatch;
	memset(&shapeMatch, 0, sizeof(shapeMatch));
	memset(&wordMatch, 0, sizeof(wordMatch));
	if (!(shapes == 1 && words == 1))
		return 0;
	TArrayIterator iter;
	BestMatch* match = (BestMatch*) gathered->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++)
	{
		BestMatch entry = *match;
		ULong type = entry.fUnit->fType;
		if (UnitInClass(type, kShapeUnit))
			shapeMatch = entry;
		else if (UnitInClass(type, kWordUnit))
			wordMatch = entry;
		match = (BestMatch*) iter.GetNext();
	}
	TStdWordUnit* word = (TStdWordUnit*) wordMatch.fUnit;
	TGeneralShapeUnit* shape = (TGeneralShapeUnit*) shapeMatch.fUnit;
	if (word->TestFlags(kInvalidUnit) || shape->TestFlags(kInvalidUnit))
		return 0;
	Boolean shapeWins = false;
	long label;
	if (shape->fSnapped == 1)
		shapeWins = true;
	else if (((label = shape->GetLabel(0)) == 10 || label == 11 || label == 12) && gCurveFlag)
		shapeWins = true;
	else
	{
		if (word->InterpretationCount() == 0)
			return 0;
		long first = GetFirstWordIndex(word);
		Handle firstWord = first < 0 ? nil : word->GetString((ULong) first);
		Handle best = word->GetString(0);
		if (best != nil && Ustrlen(*(UniChar**) best) == 1)
		{
			Fixed limit = 0x1e0000;
			if (firstWord != nil)
			{
				UniChar c = **(UniChar**) firstWord;
				label = shape->GetLabel(0);
				if ((c == 'I' || c == 'a' || c == 'A') && (label == 7 || label == 6 || label == 5 || label == 4))
					limit = 0x230000;
			}
			FRect box;
			word->GetBBox(&box);
			if (box.bottom - box.top > limit || box.right - box.left > limit)
				shapeWins = true;
			else
				return 0;
		}
		else if (shape->GetLabel(0) == 8)
			shapeWins = true;
		else
			return 0;
	}
	BestMatch* won = (BestMatch*) winners->AddEntry();
	if (won == nil)
		return -1;
	*won = shapeMatch;
	gLastType = kLastTypeShape;
	return 1;
}


// ROM 0x0020770c ArbitrateGraphicsWords__8TArbiterFP6TArray
// A word that may have been drawn rather than written: the shapes and the
// words gathered are weighed against each other.  Each side's mean score
// is taken (an invalid unit counting 10000; a shape's biased towards big
// curves, GetGraphicBiasedScore); when neither side has a valid unit
// nothing is decided.  The rules for one of each go first
// (ArbitrateByRules); failing them, the side with the lower mean - the
// scores are costs - wins, every unit of it going on the winners.
void
TArbiter::ArbitrateGraphicsWords(TArray* gathered)
{
	ULong shapes = 0, words = 0;
	ULong shapeSum = 0, wordSum = 0;
	Boolean noWord = true, noShape = true;
	TArrayIterator iter;
	BestMatch* match = (BestMatch*) gathered->GetIterator(&iter);
	if (iter.fCount == 0)
		return;
	for (ULong i = 0; i < (ULong) iter.fCount; i++)
	{
		TSIUnit* unit = (TSIUnit*) match->fUnit;
		Boolean invalid = unit->TestFlags(kInvalidUnit);
		ULong type = unit->fType;
		if (UnitInClass(type, kWordUnit))
		{
			ULong score;
			if (!invalid)
			{
				score = (ULong) (uint32_t) unit->GetInterpretation(0)->score;
				noWord = false;
			}
			else
				score = 10000;
			wordSum += score;
			words++;
		}
		else if (UnitInClass(type, kShapeUnit))
		{
			ULong score;
			if (!invalid)
			{
				score = GetGraphicBiasedScore(unit);
				noShape = false;
			}
			else
				score = 10000;
			shapeSum += score;
			shapes++;
		}
		match = (BestMatch*) iter.GetNext();
	}
	if (words != 0)
		wordSum = (ULong) (uint32_t) wordSum / words;
	if (shapes != 0)
		shapeSum = (ULong) (uint32_t) shapeSum / shapes;
	if (noWord && noShape)
		return;
	long ruled = ArbitrateByRules(gathered, Winners(), shapeSum, shapes, wordSum, words);
	if (ruled < 0)
	{
		fController->SignalMemoryError();
		return;
	}
	if (ruled > 0)
		return;
	gathered->Lock();
	ULong winning = wordSum < shapeSum ? kWordUnit : kShapeUnit;
	match = (BestMatch*) gathered->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++)
	{
		if (UnitInClass(match->fUnit->fType, winning))
		{
			BestMatch* won = (BestMatch*) Winners()->AddEntry();
			if (won == nil)
			{
				fController->SignalMemoryError();
				break;
			}
			*won = *match;
			gLastType = winning == kWordUnit ? kLastTypeWord : kLastTypeShape;
		}
		match = (BestMatch*) iter.GetNext();
	}
	gathered->Unlock();
}


// HOST ONLY: with NEWTON_TRACE_ARBITER set in the environment, each
// arbitration prints the area's case (GetRecognitionCase), the types of
// the units gathered over the same strokes (each with its best
// interpretation's score, nought best) and those that won - which is
// how to tell why writing that was read still went down as ink (a word
// unit that never won leaves its strokes unclaimed, and they expire as
// ink).
static void
TraceArbitration(long recCase, TArray* gathered, TArray* winners)
{
	static int tracing = -1;
	if (tracing < 0)
		tracing = getenv("NEWTON_TRACE_ARBITER") != nil ? 1 : 0;
	if (!tracing)
		return;
	TArray* lists[2] = { gathered, winners };
	fprintf(stderr, "[arbiter] case %ld:", recCase);
	for (long l = 0; l < 2; l++)
	{
		fprintf(stderr, l == 0 ? " gathered" : "; won");
		TArrayIterator iter;
		BestMatch* entry = (BestMatch*) lists[l]->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (BestMatch*) iter.GetNext())
		{
			ULong t = entry->fUnit->fType;
			TSIUnit* unit = (TSIUnit*) entry->fUnit;
			fprintf(stderr, " '%c%c%c%c'/%ld", (char) (t >> 24), (char) (t >> 16), (char) (t >> 8), (char) t,
					unit->GetScore(unit->GetBestInterpretation()));
		}
	}
	fprintf(stderr, "\n");
}


// ROM 0x00207118 ArbitrateUnits__8TArbiterFP8TRecArea
// Which of the units gathered over the same strokes wins.  The rule
// depends on what the area takes (GetRecognitionCase): with a scrub among
// them nothing is chosen between, with shapes and words together the two
// are weighed against each other, and otherwise the lowest score wins.
//
// When the journal is replaying, the replayed units win as they are and
// whatever else was gathered is added afterwards.  A gather that ends
// with no winner at all has everything in it marked claimed and invalid,
// which is what throws it away.  ==> whether anything won.
Boolean
TArbiter::ArbitrateUnits(TRecArea* area)
{
	if (gController->fArbiter->fWaiting)
	{
		TArrayIterator iter;
		BestMatch* entry = (BestMatch*) Gathered()->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (BestMatch*) iter.GetNext())
		{
			if (UnitInClass(entry->fUnit->fType, kReplayUnit))
			{
				BestMatch* winner = (BestMatch*) Winners()->AddEntry();
				if (winner == nil)
				{
					fController->SignalMemoryError();
					return false;
				}
				*winner = *entry;
			}
		}
	}

	long failed = 0;
	switch (GetRecognitionCase(area))
	{
	case 3: case 5: case 9: case 0x11:
		failed = ArbitrateWithScrubs(Gathered(), Winners());
		break;
	case 6: case 7:
		ArbitrateGraphicsWords(Gathered());
		failed = fController->ControllerError();
		break;
	case 0: case 1: case 2: case 4: case 8: case 0x10:
		failed = GetBestInterpretation(Gathered(), Winners());
		break;
	default:
		// (the ROM's switch has no arm for the rest - an area with
		// several scrub types as well as shapes or words - so they fall
		// through to the clean-up below)
		break;
	}
	if (failed || fController->ControllerError())
	{
		fController->SignalMemoryError();
		return false;
	}

	if (fWaiting)
	{
		// replaying: whatever was gathered and not chosen wins too
		TArrayIterator iter;
		BestMatch* winner = (BestMatch*) Winners()->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, winner = (BestMatch*) iter.GetNext())
			winner->fUnit->SetFlags(kGatheredUnit);
		BestMatch* entry = (BestMatch*) Gathered()->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (BestMatch*) iter.GetNext())
		{
			BestMatch match = *entry;
			TUnit* unit = match.fUnit;
			if (unit->TestFlags(kGatheredUnit))
				continue;
			ULong type = unit->fType;
			if (UnitInClass(type, kScrubUnit) || UnitInClass(type, kReplayUnit))
				continue;
			BestMatch* extra = (BestMatch*) Winners()->AddEntry();
			if (extra == nil)
			{
				fController->SignalMemoryError();
				return false;
			}
			*extra = match;
		}
	}

	TraceArbitration(GetRecognitionCase(area), Gathered(), Winners());
	if (Winners()->Count() != 0)
		return true;

	TArrayIterator iter;
	BestMatch* entry = (BestMatch*) Gathered()->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (BestMatch*) iter.GetNext())
		fController->MarkUnits(entry->fUnit, kClaimedUnit | kInvalidUnit);
	return false;
}


// ROM 0x002085ec DoArbitration__8TArbiterFv
// Every entry waiting to be arbitrated, in turn.
//
// A type the area arbitrates at once - or the only such type it has -
// simply wins and goes straight to the area's handler; an invalid unit
// is thrown away instead.  Anything else is held: it may still decide
// early (a scrub over one stroke), and otherwise it goes on the active
// list and waits until every stroke under it is covered
// (WaitingForOtherUnits), at which point ArbitrateUnits picks between
// what was gathered.
//
// The winners go to the handler with the controller marked busy, so that
// nothing the view does in answer is taken for writing; then those that
// were arbitrated at once, and are not scrubs, are marked claimed and
// invalid for the clean-up to take away.
void
TArbiter::DoArbitration(void)
{
	Boolean did = false;
	if (Pending()->Count() != 0)
	{
		TArrayIterator iter;
		BestMatch* pending = (BestMatch*) Pending()->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, pending = (BestMatch*) iter.GetNext())
		{
			BestMatch match = *pending;
			TUnit* unit = match.fUnit;
			if (unit->TestFlags(kClaimedUnit))
				continue;
			TRecArea* area = unit->GetArea();
			if (area == nil)
			{
				fController->SignalMemoryError();
				return;
			}
			long arbitrateNow = area->fArbitrateNow;
			Boolean decided = false;

			if (match.fArbitrateTime == 0 || arbitrateNow == 1)
			{
				did = true;
				if (unit->TestFlags(kInvalidUnit))
					fController->MarkUnits(unit, kClaimedUnit | kInvalidUnit);
				else
				{
					BestMatch* winner = (BestMatch*) Winners()->AddEntry();
					if (winner == nil)
					{
						fController->SignalMemoryError();
						return;
					}
					*winner = match;
					decided = true;
				}
			}
			else
			{
				if (!unit->TestFlags(kInvalidUnit) && ArbitrateEarly(&match))
				{
					BestMatch* winner = (BestMatch*) Winners()->AddEntry();
					if (winner == nil)
					{
						fController->SignalMemoryError();
						return;
					}
					*winner = match;
					fController->SetFlags(kControllerBusy);
					did = did || (match.fAssoc.fHandler(Winners()) & 0xff) != 0;
					Winners()->CutToIndex(0);
					fController->UnsetFlags(kControllerBusy);
					if (unit->TestFlags(kClaimedUnit))
					{
						did = true;
						Gathered()->CutToIndex(0);
						Winners()->CutToIndex(0);
						continue;
					}
				}
				Boolean waiting = WaitingForOtherUnits(area, &match);
				if (fController->ControllerError())
				{
					fController->SignalMemoryError();
					return;
				}
				if (!waiting)
				{
					decided = ArbitrateUnits(area);
					if (fController->ControllerError())
					{
						fController->SignalMemoryError();
						return;
					}
					if (!decided)
						did = true;
				}
			}

			if (decided)
			{
				did = true;
				BestMatch first = *(BestMatch*) Winners()->GetEntry(0);
				fController->SetFlags(kControllerBusy);
				if (gController->fArbiter->fWaiting)
					SetCaseAndTime(Winners(), 0);
				first.fAssoc.fHandler(Winners());
				fController->UnsetFlags(kControllerBusy);

				TArrayIterator winIter;
				BestMatch* winner = (BestMatch*) Winners()->GetIterator(&winIter);
				for (ULong w = 0; w < (ULong) winIter.fCount; w++, winner = (BestMatch*) winIter.GetNext())
				{
					TUnit* won = winner->fUnit;
					if (winner->fArbitrateTime == kArbitrateAtOnce
						&& (!UnitInClass(won->fType, kScrubUnit) || arbitrateNow == 1)
						&& !won->TestFlags(kClaimedUnit))
						fController->MarkUnits(won, kClaimedUnit | kInvalidUnit);
				}
				if (arbitrateNow == 0)
				{
					BestMatch top = *(BestMatch*) Winners()->GetEntry(0);
					unit = top.fUnit;
					if (!unit->TestFlags(kClaimedUnit) && UnitInClass(unit->fType, kClickUnit))
						fController->MarkUnits(unit, kClaimedUnit | kInvalidUnit);
				}
				// ROM bug: the gather is walked and each entry's flags
				// are tested, but the unit that is marked is the one in
				// hand rather than the entry's own - the register holding
				// it is never reloaded inside the loop.  Kept as it is:
				// marking the same unit twice does no harm, and the
				// entries that should have been marked are left for the
				// round after.
				BestMatch* left = (BestMatch*) Gathered()->GetIterator(&winIter);
				for (ULong g = 0; g < (ULong) winIter.fCount; g++, left = (BestMatch*) winIter.GetNext())
				{
					if (!left->fUnit->TestFlags(kClaimedUnit))
						fController->MarkUnits(unit, kClaimedUnit | kInvalidUnit);
				}
			}
			Gathered()->CutToIndex(0);
			Winners()->CutToIndex(0);
		}
		if (did)
			fController->fCleanUpTime = GetTicks();
		Gathered()->CutToIndex(0);
		Winners()->CutToIndex(0);
		Pending()->CutToIndex(0);
	}
	if (fWaiting && did)
		CleanUp();
}


// ROM 0x00207de0 CleanUp__8TArbiterFv
// What the arbitration left behind.  A claimed unit's own entries are
// dropped; the subs of a claimed unit that was not invalidated are
// offered to the domains again (the strokes of a word that lost may still
// make something else); and the claimed units and pieces are taken out of
// the controller's two lists - a claimed stroke piece that was marked
// invalid first goes to the expire routine, which is what leaves it on
// the screen as ink.  A click the pen is still writing is never touched.
void
TArbiter::CleanUp(void)
{
	TArrayIterator iter;

	BestMatch* entry = (BestMatch*) fLists[kArbiterActive]->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (BestMatch*) iter.GetNext())
	{
		if (entry->fUnit->TestFlags(kClaimedUnit))
		{
			((TDArray*) fLists[kArbiterActive])->Delete(i);
			iter.RemoveCurrent();
			i--;
		}
	}

	TUnit** slot = (TUnit**) fController->fUnits->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kClaimedUnit) && !unit->TestFlags(kInvalidatedUnit))
			fController->RegroupUnclaimedSubs(unit);
	}

	slot = (TUnit**) fController->fPieces->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kClaimedUnit) && !ClickInProgress(unit) && !unit->TestFlags(kInvalidatedUnit))
			fController->RegroupUnclaimedSubs(unit);
	}

	slot = (TUnit**) fController->fUnits->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kClaimedUnit))
		{
			fController->CleanGroupQ(unit);
			fController->DeleteUnit(i);
			iter.RemoveCurrent();
			i--;
		}
	}

	slot = (TUnit**) fController->fPieces->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kClaimedUnit) && !ClickInProgress(unit))
		{
			fController->CleanGroupQ(unit);
			if (unit->fType == kStrokeUnit && unit->TestFlags(kInvalidUnit)
				&& fController->fExpireStroke != nil)
				fController->fExpireStroke(unit);
			fController->DeletePiece(i);
			iter.RemoveCurrent();
			i--;
		}
	}

	for (long i = 0; i < kArbiterListCount; i++)
		fLists[i]->Compact();
}


#pragma mark - saving the state

// ROM 0x00206c28 InitArbiterState__FP8TArbiter
// (IArbiter's lists, made again)
long
InitArbiterState(TArbiter* arbiter)
{
	arbiter->fLists[kArbiterPending] = TDArray::Make(sizeof(BestMatch), 0);
	arbiter->fLists[kArbiterActive] = TDArray::Make(sizeof(BestMatch), 0);
	arbiter->fLists[kArbiterGathered] = TArray::Make(sizeof(BestMatch), 0);
	arbiter->fLists[kArbiterWinners] = TArray::Make(sizeof(BestMatch), 0);
	arbiter->fLists[kArbiterUnitsA] = TDArray::Make(sizeof(ULong), 0);
	arbiter->fLists[kArbiterUnitsB] = TDArray::Make(sizeof(ULong), 0);
	arbiter->fLists[kArbiterUnitsC] = TDArray::Make(sizeof(ULong), 0);
	arbiter->fArbitrateNow = false;
	arbiter->fWaiting = false;
	arbiter->fCase = 0;
	return 0;
}


// ROM 0x00208428 SaveArbiterState__FP8TArbiterPUc
// The lists and flags put aside and new ones made; failed says whether
// there was no room for either.
ArbiterState*
SaveArbiterState(TArbiter* arbiter, UChar* failed)
{
	*failed = false;
	ArbiterState* state = new ArbiterState;
	if (state != nil)
	{
		for (long i = 0; i < kArbiterListCount; i++)
			state->fLists[i] = arbiter->fLists[i];
		state->fArbitrateNow = arbiter->fArbitrateNow;
		state->fWaiting = arbiter->fWaiting;
		state->fCase = arbiter->fCase;
		InitArbiterState(arbiter);
		Boolean all = true;
		for (long i = 0; i < kArbiterListCount && all; i++)
			all = arbiter->fLists[i] != nil;
		if (all)
		{
			*failed = false;
			return state;
		}
	}
	*failed = true;
	return state;
}


// ROM 0x00208500 RestoreArbiterState__FP8TArbiterUl
// The lists made meanwhile disposed of and the saved ones put back.
long
RestoreArbiterState(TArbiter* arbiter, ArbiterState* state)
{
	if (state != nil)
	{
		for (long i = 0; i < kArbiterListCount; i++)
			if (arbiter->fLists[i] != nil)
				arbiter->fLists[i]->Dispose();
		for (long i = 0; i < kArbiterListCount; i++)
			arbiter->fLists[i] = state->fLists[i];
		arbiter->fArbitrateNow = state->fArbitrateNow;
		arbiter->fWaiting = state->fWaiting;
		arbiter->fCase = state->fCase;
		delete state;
	}
	return 0;
}
