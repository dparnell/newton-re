/*
	File:		recognition/UnistrokeRecognizer.cpp

	Contains:	The host's Graffiti-style engine - UnistrokeRecognizer.h.
*/

#include "UnistrokeRecognizer.h"
#include "WordEngines.h"
#include "WordUnit.h"
#include "Unit.h"
#include "Stroke.h"
#include "Words.h"			// FindBaseline
#include "Protocols.h"
#include "Unistroke.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "ViewFlags.h"
#include "Interpreter.h"		// GetVariable

#include <stdio.h>
#include <stdlib.h>


// NEWTON_TRACE_UNISTROKE=1: each stroke's readings (read once)
static Boolean
Tracing(void)
{
	static int tracing = -1;
	if (tracing < 0)
		tracing = getenv("NEWTON_TRACE_UNISTROKE") != nil;
	return tracing != 0;
}


static const char*
CharName(UniChar ch)
{
	static char one[2];
	switch (ch)
	{
	case kUnistrokeSpace:		return "space";
	case kUnistrokeBackspace:	return "backspace";
	case kUnistrokeReturn:		return "return";
	case kUnistrokeShift:		return "shift";
	}
	one[0] = (char) ch;
	one[1] = 0;
	return one;
}


// What the engine keeps for each place written in: whether it reads the
// shapes digits and letters share as digits.
struct UnistrokeAreaInfo
{
	ULong	fDigitsFirst;
};


PROTOCOL TUnistrokeRecognizer : public TWRecognizer
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TUnistrokeRecognizer);

	TUnistrokeRecognizer*	New(void);
	void					Delete(void);

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

private:
	void	Read(TWRecUnit* unit, Boolean advance);

	long	fShift;			// 0, 1 the next letter a capital, 2 caps lock
	Boolean	fDigitsFirst;	// the area in force reads 0/1/5 as digits
};

PROTOCOL_IMPL_SOURCE_MACRO(TUnistrokeRecognizer)
PROTOCOL_CLASSINFO(TUnistrokeRecognizer, kHostWordEngineInterface, "", 0, 0, nil)


TUnistrokeRecognizer*
TUnistrokeRecognizer::New(void)
{
	fShift = 0;
	fDigitsFirst = false;
	return this;
}


void
TUnistrokeRecognizer::Delete(void)
{
}


void
TUnistrokeRecognizer::Initialize(void)
{
	fShift = 0;
	fDigitsFirst = false;
}


// A stroke offered: a character of its own.  It is a unit by itself and
// is closed at once, so that it is read and typed without waiting for
// the pen to rest - one stroke, one character, as on a Palm.
void
TUnistrokeRecognizer::Group(TStrokeUnit* stroke)
{
	TWRecUnit* unit = (TWRecUnit*) MakeNewGroupFromStroke(stroke);
	EndSubs(unit);
}


// The stroke read: its readings put on the unit, nearest first, each
// labelled as typing (kHostTypedLabel - what the recogniser's HandleUnit
// posts at the caret).  The caps shift changes how the next letter is
// read and is itself an empty reading; the space, backspace and return
// strokes are those characters.  A stroke too small to be read gets no
// reading, and the domain then lets the unit go.
//
// `advance`: the caps shift is used up (or set) by this reading - not so
// when a unit is read again.
void
TUnistrokeRecognizer::Read(TWRecUnit* unit, Boolean advance)
{
	if (SubCount(unit) < 1)
		return;
	TStrokeUnit* stroke = (TStrokeUnit*) GetSub(unit, 0);
	long count = StrokeSize(stroke);
	if (count < 1)
		return;
	if (count > 1000)
		count = 1000;
	double xy[2 * 1000];
	for (long i = 0; i < count; i++)
	{
		SamplePt* pt = GetSamplePtAddress(stroke, (ULong) i);
		xy[2 * i] = StrokeSampleX(pt) / 65536.0;
		xy[2 * i + 1] = StrokeSampleY(pt) / 65536.0;
	}
	UnistrokeMatch matches[4];
	long found = UnistrokeClassify(xy, count, fDigitsFirst ? kUnistrokeDigits : kUnistrokeLetters, matches, 4);
	if (found == 0)
		return;

	UniChar first = matches[0].fChar;
	Boolean capital = fShift != 0;
	if (advance)
	{
		if (first == kUnistrokeShift)
			fShift = fShift == 0 ? 1 : fShift == 1 ? 2 : 0;
		else if (fShift == 1 && first >= 'a' && first <= 'z')
			fShift = 0;
	}
	for (long i = 0; i < found; i++)
	{
		UniChar ch = matches[i].fChar;
		// a stroke that does something is only ever its first reading
		Boolean command = ch == kUnistrokeShift || ch == kUnistrokeSpace
					   || ch == kUnistrokeBackspace || ch == kUnistrokeReturn;
		if (i > 0 && command)
			continue;
		if (capital && ch >= 'a' && ch <= 'z')
			ch = ch - 'a' + 'A';
		long at = AddWordInterpretation(unit);
		if (at < 0)
			break;
		UniChar text[2];
		text[0] = ch == kUnistrokeShift ? 0 : ch;
		text[1] = 0;
		SetWordString(unit, (ULong) at, text);
		SetLabel(unit, (ULong) at, kHostTypedLabel);
		SetScore(unit, (ULong) at, (ULong) matches[i].fScore);
		if (i == 0 && command)
			break;
	}
	if (Tracing())
	{
		fprintf(stderr, "unistroke: %ld points:", count);
		for (long i = 0; i < found; i++)
			fprintf(stderr, " %s/%ld", CharName(matches[i].fChar), matches[i].fScore);
		fprintf(stderr, "%s\n", fShift == 2 ? " (caps lock)" : fShift == 1 ? " (shift)" : "");
	}
}


long
TUnistrokeRecognizer::Classify(TWRecUnit* unit)
{
	if (InterpretationCount(unit) == 0)
		Read(unit, true);
	return 0;
}


// read again (the area expects something else now): the caps shift is
// not used up a second time
long
TUnistrokeRecognizer::Reclassify(TWRecUnit* unit)
{
	Read(unit, false);
	return 0;
}


// where the writing stands: the word recogniser's own baseline finder
long
TUnistrokeRecognizer::FindBaseline(TStroke** strokes, Point* out)
{
	return ::FindBaseline(strokes, out);
}


void	TUnistrokeRecognizer::GroupInkStroke(TStrokeUnit*, ULong, ULong, Boolean)	{ }


long
TUnistrokeRecognizer::AreaInfoGetSize(void)
{
	return sizeof(UnistrokeAreaInfo);
}


void
TUnistrokeRecognizer::AreaInfoFillDefaults(Handle info)
{
	((UnistrokeAreaInfo*) *info)->fDigitsFirst = 0;
}


// A field that takes numbers but no letters reads 0, 1 and 5 as digits
// (Graffiti's number area).
void
TUnistrokeRecognizer::AreaInfoConfigure(Handle info, RefArg config)
{
	RefVar maskRef(GetVariable(config, RSSYMinputmask, nil, false));
	ULong mask = ISINT((Ref) maskRef) ? (ULong) RINT(maskRef) : 0;
	((UnistrokeAreaInfo*) *info)->fDigitsFirst =
		(mask & vNumbersAllowed) != 0 && (mask & (vLettersAllowed | vCharsAllowed)) == 0;
}


void
TUnistrokeRecognizer::AreaInfoFreeDependents(Handle)
{
}


void
TUnistrokeRecognizer::AreaInfoSetParameters(Handle info)
{
	fDigitsFirst = info != nil && *info != nil && ((UnistrokeAreaInfo*) *info)->fDigitsFirst != 0;
}


void	TUnistrokeRecognizer::UnitInfoFreePtr(char*)			{ }
Boolean	TUnistrokeRecognizer::VerifyWordSymbols(UniChar*)		{ return true; }


// a character read well is a word; anything else is left as ink
long
TUnistrokeRecognizer::UnitConfidence(TWRecUnit* unit)
{
	if (InterpretationCount(unit) == 0)
		return kWRecInk;
	return GetScore(unit, 0) < kUnistrokeGoodScore ? kWRecWord : kWRecInk;
}


void	TUnistrokeRecognizer::Sleep(void)						{ fShift = 0; }
void	TUnistrokeRecognizer::WakeUp(void)					{ }


Ref
UnistrokeExample(void)
{
	static const char* const kWords[] = { "one", "two", "123" };
	const double kWidth = 12, kHeight = 19, kTop = 3;
	RefVar words(MakeArray(0));
	for (unsigned w = 0; w < sizeof(kWords) / sizeof(kWords[0]); w++)
	{
		RefVar letters(MakeArray(0));
		for (const char* p = kWords[w]; *p != 0; p++)
		{
			Boolean digit = *p >= '0' && *p <= '9';
			double xy[2 * 40];
			long n = UnistrokeTemplatePath((UniChar) *p, digit, 1.0, xy, 40);
			if (n == 0)
				continue;
			double left = xy[0], right = xy[0];
			for (long i = 1; i < n; i++)
			{
				if (xy[2 * i] < left) left = xy[2 * i];
				if (xy[2 * i] > right) right = xy[2 * i];
			}
			RefVar points(MakeArray(2 * n));
			for (long i = 0; i < n; i++)
			{
				SetArraySlot(points, 2 * i, MAKEINT((long) ((xy[2 * i] - left) * kWidth + 0.5)));
				SetArraySlot(points, 2 * i + 1, MAKEINT((long) (kTop + xy[2 * i + 1] * kHeight + 0.5)));
			}
			long width = (long) ((right - left) * kWidth + 0.5);
			RefVar letter(AllocateFrame());
			SetFrameSlot(letter, RefVar(Intern((char*) "points")), points);
			SetFrameSlot(letter, RefVar(Intern((char*) "width")), RefVar(MAKEINT(width < 2 ? 2 : width)));
			AddArraySlot(letters, letter);
		}
		AddArraySlot(words, letters);
	}
	return words;
}


void
RegisterUnistrokeRecognizer(void)
{
	if (gProtocolRegistry == nil)
		return;
	TUnistrokeRecognizer::ClassInfo()->Register();
}
