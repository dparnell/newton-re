/*
	File:		recognition/InkRecognizer.cpp

	Contains:	An engine that reads nothing - InkRecognizer.h.
*/

#include "InkRecognizer.h"
#include "WordUnit.h"
#include "Unit.h"
#include "Stroke.h"
#include "Words.h"			// FindBaseline
#include "Protocols.h"


PROTOCOL TInkOnlyRecognizer : public TWRecognizer
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TInkOnlyRecognizer);

	TInkOnlyRecognizer*	New(void);
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

PROTOCOL_IMPL_SOURCE_MACRO(TInkOnlyRecognizer)
PROTOCOL_CLASSINFO(TInkOnlyRecognizer, "TWRecognizer", "", 0, 0, nil)


TInkOnlyRecognizer*
TInkOnlyRecognizer::New(void)
{
	return this;
}


void
TInkOnlyRecognizer::Delete(void)
{
}


void
TInkOnlyRecognizer::Initialize(void)
{
}


// A stroke offered.  It joins the word being built when the pen went
// down again soon enough after that word ended; otherwise that word is
// finished with and this stroke starts the next one.  "Soon enough" is
// the domain's own delay, which is the time the arbiter would wait for
// another stroke anyway.
void
TInkOnlyRecognizer::Group(TStrokeUnit* stroke)
{
	UChar found = 0;
	TWRecUnit* group = (TWRecUnit*) GetPartialGroup(&found);
	if (found != 0 && group != nil)
	{
		ULong gap = GetStartTime(stroke) - GetEndTime(group);
		if (GetStartTime(stroke) >= GetEndTime(group) && gap <= (ULong) fDomain->fDelay)
		{
			AddSub(group, stroke);
			return;
		}
		EndSubs(group);
	}
	MakeNewGroupFromStroke(stroke);
}


// The word read, which is to say not read.  One empty reading is put on
// it so that the unit is still worth something - a unit with no
// interpretations at all is marked invalid by the domain and never
// reaches a view - and `UnitConfidence` then says what it really is.
// The score is the same 1000 the word list gives a reading nobody made.
long
TInkOnlyRecognizer::Classify(TWRecUnit* unit)
{
	if (InterpretationCount(unit) == 0)
	{
		long at = AddWordInterpretation(unit);
		if (at >= 0)
		{
			UniChar empty[1];
			empty[0] = 0;
			SetWordString(unit, (ULong) at, empty);
			SetLabel(unit, (ULong) at, (ULong) -1);
			SetScore(unit, (ULong) at, 1000);
		}
	}
	return 0;
}


// Asked again, with more of the writing: the answer does not change.
long
TInkOnlyRecognizer::Reclassify(TWRecUnit* /*unit*/)
{
	return 0;
}


// Where the writing stands.  The word recogniser's own baseline finder
// answers this without reading anything, so it is used as it is.
long
TInkOnlyRecognizer::FindBaseline(TStroke** strokes, Point* out)
{
	return ::FindBaseline(strokes, out);
}


void	TInkOnlyRecognizer::GroupInkStroke(TStrokeUnit*, ULong, ULong, Boolean)	{ }
long	TInkOnlyRecognizer::AreaInfoGetSize(void)			{ return 0; }
void	TInkOnlyRecognizer::AreaInfoFillDefaults(Handle)	{ }
void	TInkOnlyRecognizer::AreaInfoConfigure(Handle, RefArg)	{ }
void	TInkOnlyRecognizer::AreaInfoFreeDependents(Handle)	{ }
void	TInkOnlyRecognizer::AreaInfoSetParameters(Handle)	{ }
// it never asks for any, so there is never any to give back
void	TInkOnlyRecognizer::UnitInfoFreePtr(char*)			{ }
// it writes no words, so there are none to check
Boolean	TInkOnlyRecognizer::VerifyWordSymbols(UniChar*)		{ return true; }
// every word is ink, which is the whole of this engine's opinion
long	TInkOnlyRecognizer::UnitConfidence(TWRecUnit*)		{ return kWRecInk; }
void	TInkOnlyRecognizer::Sleep(void)						{ }
void	TInkOnlyRecognizer::WakeUp(void)					{ }


void
RegisterInkOnlyRecognizer(void)
{
	if (gProtocolRegistry == nil)
		return;
	TInkOnlyRecognizer::ClassInfo()->Register();
}
