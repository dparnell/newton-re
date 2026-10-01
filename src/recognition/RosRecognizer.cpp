/*
	File:		recognition/RosRecognizer.cpp

	Contains:	The ROM's own handwriting engine as the recognition
				system sees it - see RosRecognizer.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RosRecognizer.h"
#include "Rosetta.h"
#include "WordUnit.h"
#include "Unit.h"
#include "Stroke.h"
#include "Protocols.h"
#include "NewtonGestalt.h"
#include "NewtonMemory.h"
#include "Unicode.h"
#include "Dictionaries.h"
#include "Airus.h"
#include "Areas.h"			// GetNonNilInt
#include "Locale.h"		// GetCurrentLocale
#include "Frames.h"
#include "Interpreter.h"	// GetVariable

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


PROTOCOL TRosRecognizer : public TWRecognizer
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TRosRecognizer);

	TRosRecognizer*	New(void);									// ROM 0x001b6c44 New__14TRosRecognizerFv
	void			Delete(void);								// ROM 0x001b6ff8 Delete__14TRosRecognizerFv

	void	Initialize(void);									// ROM 0x001b6f54 Initialize__14TRosRecognizerFv
	void	Group(TStrokeUnit* stroke);							// ROM 0x001b700c Group__14TRosRecognizerFP11TStrokeUnit
	long	Classify(TWRecUnit* unit);							// ROM 0x001b5c60 Classify__14TRosRecognizerFP9TWRecUnit
	long	Reclassify(TWRecUnit* unit);						// ROM 0x001b5d0c Reclassify__14TRosRecognizerFP9TWRecUnit
	long	FindBaseline(TStroke** strokes, Point* out);		// ROM 0x001b6024 FindBaseline__14TRosRecognizerFPP7TStrokeP5Point
	void	GroupInkStroke(TStrokeUnit* stroke, ULong index, ULong count, Boolean last);	// ROM 0x001b5e10 GroupInkStroke__14TRosRecognizerFP11TStrokeUnitUlT2Uc
	long	AreaInfoGetSize(void);								// ROM 0x001b6200 AreaInfoGetSize__14TRosRecognizerFv
	void	AreaInfoFillDefaults(Handle info);					// ROM 0x001b6208 AreaInfoFillDefaults__14TRosRecognizerFPPc
	void	AreaInfoConfigure(Handle info, RefArg config);		// ROM 0x001b62a8 AreaInfoConfigure__14TRosRecognizerFPPcRC6RefVar
	void	AreaInfoFreeDependents(Handle info);				// ROM 0x001b6adc AreaInfoFreeDependents__14TRosRecognizerFPPc
	void	AreaInfoSetParameters(Handle info);					// ROM 0x001b6ae0 AreaInfoSetParameters__14TRosRecognizerFPPc
	void	UnitInfoFreePtr(char* info);						// ROM 0x001b6b94 UnitInfoFreePtr__14TRosRecognizerFPc
	Boolean	VerifyWordSymbols(UniChar* word);					// ROM 0x001b6b98 VerifyWordSymbols__14TRosRecognizerFPUs
	long	UnitConfidence(TWRecUnit* unit);					// ROM 0x001b6be0 UnitConfidence__14TRosRecognizerFP9TWRecUnit
	void	Sleep(void);										// ROM 0x001b6c04 Sleep__14TRosRecognizerFv
	void	WakeUp(void);										// ROM 0x001b6c38 WakeUp__14TRosRecognizerFv

	// A stroke's samples as the engine wants them: an array of FPoints
	// the caller then frees.  ==> how many there are.
	ULong	AllocateAndConvertStrokeForRosetta(TStroke* stroke, FPoint** points);	// ROM 0x001b5bb8 AllocateAndConvertStrokeForRosetta__14TRosRecognizerFP7TStrokePP6FPoint
	ULong	AllocateAndConvertStrokeForRosetta(TStrokeUnit* unit, FPoint** points);	// ROM 0x001b70e4 AllocateAndConvertStrokeForRosetta__14TRosRecognizerFP11TStrokeUnitPP6FPoint
	// The engine's readings put on a unit as its interpretations.
	void	AddRosettaWordsToInterpretation(TWRecUnit* unit, ULong count, char** words, UniChar* scores);	// ROM 0x001b6c60 AddRosettaWordsToInterpretation__14TRosRecognizerFP9TWRecUnitUlPPcPUs

	TWRecUnit*		fUnit;			// +0x14  the unit being read again
	ULong			fLabel;			// +0x18  what the area's words are labelled with
	Boolean			fInkGrouping;	// +0x1c  the strokes coming in are a drawing
	long			fInkCount;		// +0x20  how many are in hand
	TStrokeUnit*	fInkStrokes[kRosMaxInkStrokes];	// +0x24
};									// 0x164 bytes

PROTOCOL_IMPL_SOURCE_MACRO(TRosRecognizer)
PROTOCOL_CLASSINFO(TRosRecognizer, "TWRecognizer", "", 0, 0, nil)	// ROM 0x00388d14 ClassInfo__14TRosRecognizerSFv


// ROM 0x001b6cfc RosRecCheckWords
// What the engine calls back with; declared here because Initialize
// hands its address to the engine before it is defined.
void	RosRecCheckWords(char** words, UniChar* scores, ULong strokes, ULong count);


// ROM 0x0c10194c gRosRecognizer
// There is only ever one, and the engine's callback has no argument to
// find it by.
TRosRecognizer*	gRosRecognizer = nil;


// HOST ONLY: with NEWTON_TRACE_ROSETTA set in the environment, what
// goes down to the engine and what comes back is written on the
// standard error - which is how to see why a piece of writing was
// read as it was, or was not read at all.
static Boolean
RosTracing(void)
{
	static int tracing = -1;
	if (tracing < 0)
		tracing = getenv("NEWTON_TRACE_ROSETTA") != nil ? 1 : 0;
	return tracing != 0;
}


#pragma mark -
/*--------------------------------------------------------------------
	Making it, and its life.
--------------------------------------------------------------------*/

// ROM 0x001b6c44 New__14TRosRecognizerFv
TRosRecognizer*
TRosRecognizer::New(void)
{
	gRosRecognizer = this;
	fInkGrouping = false;
	fInkCount = 0;
	return this;
}


// ROM 0x001b6ff8 Delete__14TRosRecognizerFv
void
TRosRecognizer::Delete(void)
{
	gRosRecognizer = nil;
}


// ROM 0x001b6f54 Initialize__14TRosRecognizerFv
// The engine started, with the tablet's resolution: the screen's own
// over 85, times 64, which is the scale the engine's arithmetic works
// in.  (The ROM reads the two halves of the resolution Point with one
// unaligned `ldr` and one aligned one, which on ARM rotates rather than
// faults - so the first of the two it works out is the *vertical*
// resolution, which is not the order the struct is in.)
void
TRosRecognizer::Initialize(void)
{
	TUGestalt gestalt;
	fUnit = nil;

	TGestaltSystemInfo info;
	memset(&info, 0, sizeof(info));		// DEVIATION: a host with no such Gestalt leaves it alone
	gestalt.Gestalt(kGestalt_SystemInfo, &info, sizeof(info));
	// (the shifts go through a 32-bit word because the ROM's wrap where
	// the host's would trap)
	long y = (long) (int) ((unsigned int) (((int) ((unsigned int) info.fScreenResolution.v << 16)) / 0x55) << 6);
	long x = (long) (int) ((unsigned int) (((int) ((unsigned int) info.fScreenResolution.h << 16)) / 0x55) << 6);
	if (RosettaInitialize(x, y, RosRecCheckWords) != noErr)
		Throw(exAbort, nil, nil);
}


// ROM 0x001b6c04 Sleep__14TRosRecognizerFv
void
TRosRecognizer::Sleep(void)
{
	if (RosettaSleep() != noErr)
		Throw(exAbort, nil, nil);
}


// ROM 0x001b6c38 WakeUp__14TRosRecognizerFv
// (a branch straight to the engine: whatever it answers is not looked at)
void
TRosRecognizer::WakeUp(void)
{
	RosettaAwaken();
}


#pragma mark -
/*--------------------------------------------------------------------
	The strokes handed down.
--------------------------------------------------------------------*/

// ROM 0x001b5bb8 AllocateAndConvertStrokeForRosetta__14TRosRecognizerFP7TStrokePP6FPoint
// The engine reads a stroke as an array of Fixed pairs, so every stroke
// is copied out of the tablet's packed samples before it goes down.
ULong
TRosRecognizer::AllocateAndConvertStrokeForRosetta(TStroke* stroke, FPoint** points)
{
	ULong count = StrokeSize(stroke);
	FPoint* out = (FPoint*) NewPtr(count * sizeof(FPoint));
	*points = out;
	if (out == nil)
		Throw(exAbort, nil, nil);

	SamplePt* sample = GetSamplePtAddress(stroke, 0);
	for (ULong i = 0; i < count; i++, out++, sample++)
	{
		out->x = StrokeSampleX(sample);
		out->y = StrokeSampleY(sample);
	}
	return count;
}


// ROM 0x001b70e4 AllocateAndConvertStrokeForRosetta__14TRosRecognizerFP11TStrokeUnitPP6FPoint
ULong
TRosRecognizer::AllocateAndConvertStrokeForRosetta(TStrokeUnit* unit, FPoint** points)
{
	return AllocateAndConvertStrokeForRosetta(StrokeUnitStroke(unit), points);
}


// ROM 0x001b700c Group__14TRosRecognizerFP11TStrokeUnit
// A stroke offered.  The grouping is the engine's: the stroke joins the
// word being built, or - when there is none - the engine's working
// values are put back and a new group started, and either way the
// stroke goes straight down, because the engine decides for itself
// where one word ends and the next begins.
void
TRosRecognizer::Group(TStrokeUnit* stroke)
{
	UChar found = 0;
	TWRecUnit* group = (TWRecUnit*) GetPartialGroup(&found);
	fInkGrouping = false;
	if (found == 0)
	{
		if (RosettaInitializeValues() != noErr)
			Throw(exAbort, nil, nil);
		MakeNewGroupFromStroke(stroke);
	}
	else
		AddSub(group, stroke);

	FPoint* points = nil;
	ULong count = AllocateAndConvertStrokeForRosetta(stroke, &points);
	ULong end = GetEndTime(stroke);
	ULong start = GetStartTime(stroke);
	if (RosTracing())
		fprintf(stderr, "rosetta: stroke of %lu points%s" "\n", (unsigned long) count,
				found == 0 ? ", a new word" : "");
	if (RosettaClassify(count, points, start, end) != noErr)
		Throw(exAbort, nil, nil);
}


// ROM 0x001b5e10 GroupInkStroke__14TRosRecognizerFP11TStrokeUnitUlT2Uc
// A stroke of a *drawing* rather than of writing.  The engine is told to
// group but to read nothing (`kRosettaNoClassify`), the strokes are kept
// here as they come, and when the drawing ends - or eighty of them have
// been gathered - the group is closed and handed back whole through
// `EndInkStrokeGroup`.
void
TRosRecognizer::GroupInkStroke(TStrokeUnit* stroke, ULong index, ULong count, Boolean last)
{
	newton_try
	{
		NewtonErr err = noErr;
		UChar found = 0;
		GetPartialGroup(&found);
		if (found != 0)
		{
			// a word is half-built: it is finished with first
			err = RosettaClassify(0, nil, 0, 0);
			fInkCount = 0;
		}
		if (fInkCount == 0 && err == noErr)
			err = RosettaInitializeValues();
		if (err != noErr)
			Throw(exAbort, nil, nil);

		// the engine groups the strokes but reads nothing of them
		RosettaDontClassify(kRosettaNoClassify);
		RosettaAreaInfo area;
		char* block = (char*) &area;
		AreaInfoFillDefaults(&block);
		area.fLetterSpace = (UByte) (9 - index);
		RosettaSetArea(&area);

		if (stroke != nil)
		{
			fInkGrouping = true;
			FPoint* points = nil;
			ULong n = AllocateAndConvertStrokeForRosetta(stroke, &points);
			ULong end = GetEndTime(stroke);
			ULong start = GetStartTime(stroke);
			if (RosettaClassify(n, points, start, end) != noErr)
				Throw(exAbort, nil, nil);
			fInkStrokes[fInkCount++] = stroke;
		}

		Boolean keep = false;
		if (fInkGrouping)
		{
			if (last || fInkCount >= kRosMaxInkStrokes || (ULong) fInkCount >= count)
			{
				if (RosettaClassify(0, nil, 0, 0) != noErr)
					Throw(exAbort, nil, nil);
				fInkGrouping = false;
			}
			else
				keep = true;			// more to come: the strokes stay in hand
		}
		if (!keep)
			fInkCount = 0;
		RosettaDontClassify(kRosettaClassifyNormally);
	}
	newton_catch_all
	{
		fInkCount = 0;
		RosettaDontClassify(kRosettaClassifyNormally);
		rethrow;
	}
	end_try;
}


#pragma mark -
/*--------------------------------------------------------------------
	Reading.
--------------------------------------------------------------------*/

// ROM 0x001b5c60 Classify__14TRosRecognizerFP9TWRecUnit
// The word finished with: the engine is given an empty stroke, which is
// how it is told to read what it has.  A unit that already carries
// readings is left alone - the engine gave them to it through
// `RosRecCheckWords` while the strokes were going down.
long
TRosRecognizer::Classify(TWRecUnit* unit)
{
	if (InterpretationCount(unit) > 0)
		return 0;

	NewtonErr err = noErr;
	newton_try
	{
		err = RosettaClassify(0, nil, 0, 0);
	}
	newton_catch_all
	{
		rethrow;
	}
	end_try;
	if (err != noErr)
		Throw(exAbort, nil, nil);
	if (RosettaInitializeValues() != noErr)
		Throw(exAbort, nil, nil);
	return 0;
}


// ROM 0x001b5d0c Reclassify__14TRosRecognizerFP9TWRecUnit
// A unit read again - because the field it was written in expects
// something else now.  Every stroke of it goes down again in order and
// then it is classified as before; `fUnit` says which unit the words
// coming back belong to, so that `RosRecCheckWords` does not start a
// group for them.
long
TRosRecognizer::Reclassify(TWRecUnit* unit)
{
	if (RosettaInitializeValues() != noErr)
		Throw(exAbort, nil, nil);

	fUnit = unit;
	ULong count = SubCount(unit);
	for (ULong i = 0; i < count; i++)
	{
		TStrokeUnit* stroke = (TStrokeUnit*) GetSub(unit, i);
		FPoint* points = nil;
		ULong n = AllocateAndConvertStrokeForRosetta(stroke, &points);
		ULong end = GetEndTime(stroke);
		ULong start = GetStartTime(stroke);
		if (RosettaClassify(n, points, start, end) != noErr)
		{
			fUnit = nil;
			Throw(exAbort, nil, nil);
		}
	}
	Classify(unit);
	fUnit = nil;
	return 0;
}


// ROM 0x001b6c60 AddRosettaWordsToInterpretation__14TRosRecognizerFP9TWRecUnitUlPPcPUs
// The readings the engine came back with put on the unit: the word
// itself, the score it was given and the label the area asked for.
void
TRosRecognizer::AddRosettaWordsToInterpretation(TWRecUnit* unit, ULong count, char** words, UniChar* scores)
{
	for (ULong i = 0; i < count; i++)
	{
		long index = AddWordInterpretation(unit);
		if (index == -1)
			return;						// no room for another
		// (the ROM loads a word at `scores + i * 2` and shifts it down -
		//  on the ARM an unaligned load, which answers the halfword)
		ULong score = scores[i];
		SetCharWordString(unit, index, words[i]);
		SetScore(unit, index, score);
		SetLabel(unit, index, fLabel);
	}
}


// ROM 0x001b6cfc RosRecCheckWords
// What the engine calls back with, and the one piece of real work this
// file does.  `strokes` is how many of the group's strokes the words
// cover.
//
// While a drawing is being gathered there are no words, and the call
// only says how much of the drawing has been used: the strokes the
// engine has finished with are handed back through `EndInkStrokeGroup`
// and the rest shuffled down to the front.
//
// Otherwise the words belong to the group being built.  If the engine
// covered fewer strokes than the group holds it has decided the writing
// is two words rather than one: a group is made of the first `strokes`
// of them, the words go on that, another is made of the rest, and the
// group that was there is thrown away.
void
RosRecCheckWords(char** words, UniChar* scores, ULong strokes, ULong count)
{
	TRosRecognizer* self = gRosRecognizer;
	if (self->fInkGrouping)
	{
		if (self->fInkCount == (long) strokes)
		{
			// the whole drawing is finished with
			self->fInkStrokes[self->fInkCount] = nil;
			self->fInkCount = 0;
			self->EndInkStrokeGroup(self->fInkStrokes);
			return;
		}
		// part of it: those strokes are handed over and the rest kept
		TStrokeUnit* done[kRosMaxInkStrokes + 1];
		for (ULong i = 0; i < strokes; i++)
			done[i] = self->fInkStrokes[i];
		done[strokes] = nil;
		self->EndInkStrokeGroup(done);
		long from = (long) strokes;
		long to = 0;
		while (from < self->fInkCount)
			self->fInkStrokes[to++] = self->fInkStrokes[from++];
		self->fInkCount -= (long) strokes;
		return;
	}

	UChar found = 0;
	TWRecUnit* group = (TWRecUnit*) self->GetPartialGroup(&found);
	if (found == 0)
		// no group is being built, so this is a unit being read again
		group = self->fUnit;
	if (RosTracing())
	{
		fprintf(stderr, "rosetta: read %lu strokes as", (unsigned long) strokes);
		for (ULong i = 0; i < count; i++)
			fprintf(stderr, " \"%s\"/%d", words[i], (int) scores[i]);
		fprintf(stderr, "\n");
	}

	ULong subs = self->SubCount(group);
	if (strokes > subs)
		strokes = subs;

	TWRecUnit* target = group;
	Boolean split = false;
	if (strokes != subs)
	{
		// the engine read fewer strokes than the group holds, so the
		// words belong to a group of their own
		target = (TWRecUnit*) self->MakeNewGroupFromStroke((TStrokeUnit*) self->GetSub(group, 0));
		for (ULong i = 1; i < strokes; i++)
			self->AddSub(target, (TStrokeUnit*) self->GetSub(group, i));
		split = true;
	}

	self->AddRosettaWordsToInterpretation(target, count, words, scores);

	if (subs > strokes)
	{
		// ... and what is left starts the next one
		TWRecUnit* rest = (TWRecUnit*) self->MakeNewGroupFromStroke((TStrokeUnit*) self->GetSub(group, strokes));
		for (ULong i = strokes + 1; i < subs; i++)
			self->AddSub(rest, (TStrokeUnit*) self->GetSub(group, i));
	}
	if (strokes != subs)
	{
		self->EndSubs(group);
		self->InvalidateUnit(group);
	}
	if (split)
	{
		self->EndSubs(target);
		self->NewClassification(target);
	}
}


// ROM 0x001b6024 FindBaseline__14TRosRecognizerFPP7TStrokeP5Point
// Where a handful of strokes sit on a line.  The engine is asked to
// group them and work the baseline out but to read nothing
// (`kRosettaBaselineOnly`), and the two Points it answers are opened
// out into the four the caller wants.
long
TRosRecognizer::FindBaseline(TStroke** strokes, Point* out)
{
	NewtonErr err = noErr;
	if (!fInkGrouping)
	{
		if (RosettaInitializeValues() != noErr)
			Throw(exAbort, nil, nil);
		RosettaDontClassify(kRosettaBaselineOnly);

		RosettaAreaInfo area;
		char* block = (char*) &area;
		AreaInfoFillDefaults(&block);
		// the area's flags say the strokes are only being measured
		area.fFlags |= 0x2000;

		for (long i = 0; strokes[i] != nil; i++)
		{
			FPoint* points = nil;
			ULong n = AllocateAndConvertStrokeForRosetta(strokes[i], &points);
			ULong end = GetEndTime(strokes[i]);
			ULong start = GetStartTime(strokes[i]);
			if (RosettaClassify(n, points, start, end) != noErr)
				Throw(exAbort, nil, nil);
		}
		err = RosettaClassify(0, nil, 0, 0);
	}

	if (err == noErr)
	{
		Point lines[2];
		err = RosettaGetBaseLine(lines);
		if (err == noErr)
		{
			// the two Points opened out into the four the caller wants
			short a = lines[0].v;
			short b = lines[0].h;
			short c = lines[1].v;
			out[0].v = a;	out[0].h = b;
			out[1].v = a;	out[1].h = c;
			out[2].v = b;	out[2].h = c;
			out[3].v = b;	out[3].h = a;
		}
	}

	if (!fInkGrouping)
	{
		if (RosettaInitializeValues() != noErr || err != noErr)
			Throw(exAbort, nil, nil);
	}
	return 0;
}


#pragma mark -
/*--------------------------------------------------------------------
	The area, and what the domain asks.
--------------------------------------------------------------------*/

// ROM 0x001b6200 AreaInfoGetSize__14TRosRecognizerFv
long
TRosRecognizer::AreaInfoGetSize(void)
{
	return kRosettaAreaInfoSize;
}


// ROM 0x001b6208 AreaInfoFillDefaults__14TRosRecognizerFPPc
// An area block as the engine expects to find it: everything clear,
// except the eight lexicon words and the five stroke shapes, which are
// all ones - every word list and every shape - and the count of strokes
// still to come, which is five.
void
TRosRecognizer::AreaInfoFillDefaults(Handle info)
{
	RosettaAreaInfo* area = (RosettaAreaInfo*) *info;
	area->fFlags = 0;
	area->fLabel = 0;
	area->fField01 = 0;
	area->fField00 = 0;
	area->fMainDict = nil;
	area->fDictCount = 0;
	area->fBase = 0;
	area->fBoxLeft = 0;
	area->fBoxRight = 0;
	area->fSmallHeight = 0;
	area->fXSpace = 0;
	area->fBoxTop = 0;
	area->fBoxBottom = 0;
	area->fYSpace = 0;
	for (long i = 0; i < 5; i++)
		area->fDicts[i] = nil;
	for (long i = 0; i < 8; i++)
		area->fSymbolSet[i] = 0xffffffff;		// every character
	area->fLetterSpace = 5;
	for (long i = 0; i < 5; i++)
		area->fMap[i][0] = -1;					// nothing read as anything else
}


// ROM 0x001b62a8 AreaInfoConfigure__14TRosRecognizerFPPcRC6RefVar
// A recognition configuration read into the engine's area block: what
// kinds of word the field expects, which dictionaries it names by hand,
// the grid it is written on and the characters it allows.
//
// The dictionaries are the interesting part.  Most of the ROM's
// lexicons stand for a *kind* of thing the engine knows how to read by
// itself - names, dates, numbers - so naming one of those only sets a
// flag; the rest are handed over as dictionaries, at most five of them,
// and only if the locale's `rosIgnoreDicts` does not list them.  A
// sixteen-bit dictionary is never handed over: this engine reads bytes.
void
TRosRecognizer::AreaInfoConfigure(Handle info, RefArg config)
{
	ULong mask = (ULong) RINT(RefVar(GetVariable(config, RSSYMinputmask, nil, false)));
	long customs = CountCustomDictionaries(config);

	HLock(info);
	RosettaAreaInfo* area = (RosettaAreaInfo*) *info;

	// the dictionaries this locale would rather the engine read by
	// itself than be handed
	TObjectIterator* ignore = nil;
	RefVar list(GetProtoVariable(RefVar(GetCurrentLocale()), RSSYMrosignoredicts, nil));
	if (IsArray(list))
		ignore = NewIterator(list);

	for (long i = 0; i < customs; i++)
	{
		ULong id = GetCustomDictionary(config, i);
		dictListEntry* entry = FindDictionaryEntry(id);
		if (i == 0)
			// the first one names what words read here are labelled with
			area->fLabel = (UByte) id;

		Boolean handOver = false;
		switch (id)
		{
		// the kinds the engine reads by itself
		case 0x01:	area->fFlags |= kRosAreaTime;			break;
		case 0x08: case 0x0b: case 0x0c: case 0x0d: case 0x0e:
		case 0x13: case 0x16: case 0x17: case 0x18: case 0x1a:
		case 0x29: case 0x2b: case 0x2c:
					area->fFlags |= kRosAreaNames;			break;
		case 0x22: case 0x65: case 0x6e:
					area->fFlags |= kRosAreaPhone;			break;
		case 0x64: case 0x6f:
					area->fFlags |= kRosAreaDate;			break;
		case 0x66: case 0x70: case 0x72: case 0x73:
					area->fFlags |= kRosAreaPunctuation;	break;
		case 0x67: case 0x71:
					area->fFlags |= kRosAreaNumbers;		break;
		case 0x74:	area->fFlags |= kRosAreaCustom1;		break;
		case 0x75:	area->fFlags |= kRosAreaCustom2;		break;
		case 0x76:	area->fFlags |= kRosAreaAddress;		break;
		case 0x00: case 0x03: case 0x06: case 0x07: case 0x09:
		case 0x0a: case 0x14: case 0x19: case 0x1b: case 0x1c:
		case 0x1d: case 0x2a: case 0x2d: case 0x30: case 0x31:
					area->fFlags |= kRosAreaLetters;		break;
		default:
					handOver = true;						break;
		}
		if (!handOver)
			continue;

		// one the engine does not know: hand it over, unless the locale
		// says to leave it out
		if (ignore != nil)
		{
			ignore->Reset();
			do
			{
				RefVar value(ignore->Value());
				if (ISINT(value) && (ULong) RINT(value) == id)
					break;
			}
			while (ignore->Next());
			if (!ignore->Done())
				continue;					// it is in the list
		}
		if (entry != nil && area->fDictCount < 5)
		{
			AirusAParmBlock* parms = (AirusAParmBlock*) *entry->fDictionary;
			long kind = (UByte) (*parms->fDataHandle)[1] & 7;
			if (kind != kAirusKindEnum16 && kind != kAirusKindAL16)
				area->fDicts[area->fDictCount++] = parms->fDataHandle;
		}
	}
	if (ignore != nil)
		delete ignore;

	// the characters this locale reads as other characters
	RefVar map(GetProtoVariable(RefVar(GetCurrentLocale()), RSSYMrosmapdicts, nil));
	if (IsArray(map))
	{
		TObjectIterator* iter = NewIterator(map);
		iter->Reset();
		long i = 0;
		do
		{
			area->fMap[i][0] = (short) RINT(RefVar(iter->Value()));
			iter->Next();
			area->fMap[i][1] = (short) RINT(RefVar(iter->Value()));
			i++;
		}
		while (iter->Next());
		delete iter;
	}

	// cursive writing, and how far apart its letters are
	RefVar spacing(GetVariable(config, RSSYMletterspacecursiveoption, nil, false));
	if (ISNIL(spacing) || (mask & 0x100) != 0)
		area->fFlags |= kRosAreaCursive;
	if (NOTNIL(spacing))
		area->fLetterSpace = (UByte) RINT(spacing);

	RefVar single(GetVariable(config, RSSYMrcsingleletters, nil, false));
	if (ISNIL(single))
	{
		// ordinary writing: the input mask says what the field expects
		if ((mask & 0x00002000) != 0)	area->fFlags |= kRosAreaNumbers;
		if ((mask & 0x00040000) != 0)	area->fFlags |= kRosAreaPunctuation;
		if ((mask & 0x00080000) != 0)	area->fFlags |= kRosAreaPhone;
		if ((mask & 0x00100000) != 0)	area->fFlags |= kRosAreaDate;
		if ((mask & 0x00800000) != 0)	area->fFlags |= kRosAreaCapitals;
		if ((mask & 0x00001000) != 0)	area->fFlags |= kRosAreaTime;
		if ((mask & 0x00004000) != 0)	area->fFlags |= kRosAreaMoney;
		if ((mask & 0x00008000) != 0)	area->fFlags |= 0x200;
		if ((mask & 0x00020000) != 0)	area->fFlags |= kRosAreaUpperCase;
		if ((mask & 0x00400000) != 0)	area->fFlags |= kRosAreaLetters;
		if ((mask & 0x00200000) != 0)	area->fFlags |= kRosAreaNames;
		dictListEntry* entry = FindDictionaryEntry(0x1f);
		area->fMainDict = ((AirusAParmBlock*) *entry->fDictionary)->fDataHandle;
	}
	else
	{
		// one letter at a time, in boxes
		if (area->fLabel == 0)
			area->fLabel = 0x28;
		area->fFlags |= kRosAreaSingleLetters;
	}

	// where the line it is written on is
	RefVar base(GetProtoVariable(config, RSSYMrcbaseinfo, nil));
	if (NOTNIL(base))
	{
		area->fFlags |= kRosAreaHasBaseInfo;
		area->fBase = (short) GetNonNilInt(base, RSSYMbase);
		area->fSmallHeight = (UByte) GetNonNilInt(base, RSSYMsmallheight);
	}

	// ... and the grid, when it is written in boxes
	RefVar grid(GetProtoVariable(config, RSSYMrcgridinfo, nil));
	if (NOTNIL(grid))
	{
		area->fFlags |= kRosAreaHasBaseInfo;
		area->fBoxLeft = (short) GetNonNilInt(grid, RSSYMboxleft);
		area->fBoxRight = (short) GetNonNilInt(grid, RSSYMboxright);
		area->fXSpace = (UByte) GetNonNilInt(grid, RSSYMxspace);
		area->fBoxTop = (short) GetNonNilInt(grid, RSSYMboxtop);
		area->fBoxBottom = (short) GetNonNilInt(grid, RSSYMboxbottom);
		area->fYSpace = (UByte) GetNonNilInt(grid, RSSYMyspace);
	}

	// the characters it allows, as a set of 256 bits
	RefVar symbols(GetProtoVariable(config, RSSYMsymbolset, nil));
	if (NOTNIL(symbols))
	{
		area->fFlags |= kRosAreaHasSymbolSet;
		UByte text[0x100];
		ConvertFromUnicode(GetCString(symbols), text, kMacRomanEncoding, 0x100);
		for (long i = 0; i < 8; i++)
			area->fSymbolSet[i] = 0;
		long length = (long) strlen((const char*) text);
		for (long i = 0; i < length; i++)
			area->fSymbolSet[text[i] >> 5] |= 1u << (text[i] & 0x1f);
	}

	// ... and the ones it does not, taken back out of that set
	RefVar remove(GetProtoVariable(config, RSSYMremovesymbol, nil));
	if (NOTNIL(remove))
	{
		area->fFlags |= kRosAreaHasSymbolSet;
		UByte text[0x100];
		ConvertFromUnicode(GetCString(remove), text, kMacRomanEncoding, 0x100);
		long length = (long) strlen((const char*) text);
		for (long i = 0; i < length; i++)
			area->fSymbolSet[text[i] >> 5] &= ~(1u << (text[i] & 0x1f));
	}

	HUnlock(info);
}


// ROM 0x001b6adc AreaInfoFreeDependents__14TRosRecognizerFPPc
void
TRosRecognizer::AreaInfoFreeDependents(Handle /*info*/)
{
}


// ROM 0x001b6ae0 AreaInfoSetParameters__14TRosRecognizerFPPc
// The area handed to the engine.  A word half-built is finished with
// first, because it was written against the area that is going away.
void
TRosRecognizer::AreaInfoSetParameters(Handle info)
{
	NewtonErr classified = noErr;
	UChar found = 0;
	GetPartialGroup(&found);
	if (found != 0)
		classified = RosettaClassify(0, nil, 0, 0);

	NewtonErr values = RosettaInitializeValues();
	HLock(info);
	RosettaAreaInfo* area = (RosettaAreaInfo*) *info;
	NewtonErr set = RosettaSetArea(area);
	fLabel = area->fLabel;
	HUnlock(info);
	if (set != noErr || values != noErr || classified != noErr)
		Throw(exAbort, nil, nil);
}


// ROM 0x001b6b94 UnitInfoFreePtr__14TRosRecognizerFPc
// The engine keeps nothing of its own per unit.
void
TRosRecognizer::UnitInfoFreePtr(char* /*info*/)
{
}


// ROM 0x001b6b98 VerifyWordSymbols__14TRosRecognizerFPUs
// Whether the engine could have written that word.  Anything over
// seventy characters it could not, and it is not asked about.
Boolean
TRosRecognizer::VerifyWordSymbols(UniChar* word)
{
	char bytes[0x54];
	if (Ustrlen(word) > 0x46)
		return false;
	ConvertFromUnicode(word, bytes, kMacRomanEncoding, 0x50);
	return RosettaVerifyWordSymbols(bytes);
}


// ROM 0x001b6be0 UnitConfidence__14TRosRecognizerFP9TWRecUnit
// How sure the engine is of its best reading, as the arbiter wants it:
// two for a score of 900 or better, nought for anything less.
long
TRosRecognizer::UnitConfidence(TWRecUnit* unit)
{
	return GetScore(unit, 0) < 900 ? 0 : 2;
}


// ROM 0x001b6b7c RegisterRosettaWRec__Fv
// The ROM's `RegisterWRec` (0x001b5bb4) is a branch to this.
void
RegisterRosettaWRec(void)
{
	if (gProtocolRegistry == nil)
		return;
	TRosRecognizer::ClassInfo()->Register();
}
