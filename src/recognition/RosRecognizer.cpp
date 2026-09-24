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

#include <string.h>


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
PROTOCOL_CLASSINFO(TRosRecognizer, "TWRecognizer", "", 0, 0, nil)


// ROM 0x001b6cfc RosRecCheckWords
// What the engine calls back with; declared here because Initialize
// hands its address to the engine before it is defined.
void	RosRecCheckWords(char** words, ULong strokes, ULong count, UniChar* scores);


// ROM 0x0c10194c gRosRecognizer
// There is only ever one, and the engine's callback has no argument to
// find it by.
TRosRecognizer*	gRosRecognizer = nil;


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
	if (RosettaInitialize(x, y, (RosettaCheckWordsProc) RosRecCheckWords) != noErr)
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
		area.fStrokesExpected = (UByte) (9 - index);
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
// `scores` is read a word at a time and shifted down, because the score
// is the top half of each of them.
void
TRosRecognizer::AddRosettaWordsToInterpretation(TWRecUnit* unit, ULong count, char** words, UniChar* scores)
{
	for (ULong i = 0; i < count; i++)
	{
		long index = AddWordInterpretation(unit);
		if (index == -1)
			return;						// no room for another
		ULong score = ((const unsigned int*) scores)[i] >> 16;
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
RosRecCheckWords(char** words, ULong strokes, ULong count, UniChar* scores)
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
	area->fField04 = 0;
	area->fField61 = 0;
	for (long i = 0; i < 6; i++)
		area->fField2c[i] = 0;
	area->fField62 = 0;
	area->fField63 = 0;
	area->fField38 = 0;
	area->fField3c = 0;
	area->fField40 = 0;
	area->fField44 = 0;
	area->fField48 = 0;
	for (long i = 0; i < 8; i++)
		area->fLexicons[i] = -1;
	area->fStrokesExpected = 5;
	for (long i = 0; i < 5; i++)
	{
		area->fShapes[i][1] = 0xff;
		area->fShapes[i][0] = 0xff;
	}
}


// ROM 0x001b62a8 AreaInfoConfigure__14TRosRecognizerFPPcRC6RefVar
// NOT YET RECONSTRUCTED.  This is where a recognition configuration -
// the input mask of the field being written in and the dictionaries it
// names by hand - becomes the engine's own lexicon flags: a long switch
// over the dictionary ids (`recognition/Dictionaries.h`) setting a bit
// each in the area block, the locale's `rosIgnoreDicts` list of the ones
// to leave out, the letter set, the line height and the rest of it.
// Until it is here every area is the default one, which is every word
// list at once.
void
TRosRecognizer::AreaInfoConfigure(Handle info, RefArg /*config*/)
{
	AreaInfoFillDefaults(info);
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
