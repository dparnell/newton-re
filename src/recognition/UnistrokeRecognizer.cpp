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
};

PROTOCOL_IMPL_SOURCE_MACRO(TUnistrokeRecognizer)
PROTOCOL_CLASSINFO(TUnistrokeRecognizer, kHostWordEngineInterface, "", 0, 0, nil)


TUnistrokeRecognizer*
TUnistrokeRecognizer::New(void)
{
	return this;
}


void
TUnistrokeRecognizer::Delete(void)
{
}


void
TUnistrokeRecognizer::Initialize(void)
{
}


// A stroke offered: one letter.  It joins the word being built when the
// pen went down again within the domain's delay of that word's end, and
// otherwise ends that word and starts the next.
void
TUnistrokeRecognizer::Group(TStrokeUnit* stroke)
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


// NOT YET: each stroke read as a letter.  Until then the word gets one
// empty reading, so that the unit is still worth something and reaches
// the view, and UnitConfidence says it is ink.
long
TUnistrokeRecognizer::Classify(TWRecUnit* unit)
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


long
TUnistrokeRecognizer::Reclassify(TWRecUnit* /*unit*/)
{
	return 0;
}


// where the writing stands: the word recogniser's own baseline finder
long
TUnistrokeRecognizer::FindBaseline(TStroke** strokes, Point* out)
{
	return ::FindBaseline(strokes, out);
}


void	TUnistrokeRecognizer::GroupInkStroke(TStrokeUnit*, ULong, ULong, Boolean)	{ }
long	TUnistrokeRecognizer::AreaInfoGetSize(void)			{ return 0; }
void	TUnistrokeRecognizer::AreaInfoFillDefaults(Handle)	{ }
void	TUnistrokeRecognizer::AreaInfoConfigure(Handle, RefArg)	{ }
void	TUnistrokeRecognizer::AreaInfoFreeDependents(Handle)	{ }
void	TUnistrokeRecognizer::AreaInfoSetParameters(Handle)	{ }
void	TUnistrokeRecognizer::UnitInfoFreePtr(char*)			{ }
Boolean	TUnistrokeRecognizer::VerifyWordSymbols(UniChar*)		{ return true; }
// NOT YET: kWRecWord for a word it has read
long	TUnistrokeRecognizer::UnitConfidence(TWRecUnit*)		{ return kWRecInk; }
void	TUnistrokeRecognizer::Sleep(void)						{ }
void	TUnistrokeRecognizer::WakeUp(void)					{ }


void
RegisterUnistrokeRecognizer(void)
{
	if (gProtocolRegistry == nil)
		return;
	TUnistrokeRecognizer::ClassInfo()->Register();
}
