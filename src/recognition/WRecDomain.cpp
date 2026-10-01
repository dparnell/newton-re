/*
	File:		recognition/WRecDomain.cpp

	Contains:	The word domain and the engine behind it - WRecDomain.h.

				Every one of these is the same shape: make the engine's
				heap current, ask it, put the heap back.  Anything thrown
				on the way out is taken as the engine having run out of
				memory: the heap goes back, the failure is counted, and
				the recogniser is put to sleep rather than asked anything
				else.

				DEVIATION: the engine runs in the ordinary heap and only
				the exception handler is left (the VM heaps are in
				memory/MemoryManager.cpp; the engine stays
				out of them - WRecDomain.h).
				Where the ROM gives the recogniser back by destroying the
				heap it lived in, this deletes it.
*/

#include "WRecDomain.h"
#include "Controller.h"
#include "NewtonMemory.h"
#include "Stroke.h"
#include "NewtonExceptions.h"
#include "InkGroups.h"		// WRecEndInkStrokeGroup

#include <string.h>


// ROM 0x0c104f78 gRecMemErrCount
// How many times the engine has run out of memory, which is what the
// word recogniser tells the user about once a day.
long	gRecMemErrCount = 0;


// The engine asked, with an exception handler round it.  The ROM makes
// its own heap current first and puts the task's back afterwards; here
// there is only the one heap.
#define WREC_TRY									\
	newton_try										\
	{

#define WREC_CATCH									\
	}												\
	newton_catch_all								\
	{												\
		SignalMemoryError();

#define WREC_END									\
	}												\
	end_try


/*------------------------------------------------------------------------------
	T h e   p r o t o c o l
------------------------------------------------------------------------------*/

// ROM 0x00388bd0 New__12TWRecognizerSFPc
// The engine, made by name out of the protocol registry: nil is the
// implementation the machine has, which is the ROM's TRosRecognizer.
TWRecognizer*
TWRecognizer::New(const char* implementation)
{
	TWRecognizer* p = (TWRecognizer*) AllocInstanceByName("TWRecognizer", implementation);
	return p != nil ? (TWRecognizer*) p->GlueNew() : nil;
}


// ROM 0x00388bfc Delete__12TWRecognizerFv
void
TWRecognizer::Delete(void)
{
	GlueDelete();
}


/*------------------------------------------------------------------------------
	W h a t   t h e   e n g i n e   c a l l s   b a c k
------------------------------------------------------------------------------*/

// These are the protocol's own rather than dispatched: an engine has
// them by being a TWRecognizer.  Each puts the task's heap back before
// doing anything that allocates - the recogniser's units belong to the
// system, not to the engine's heap - and makes the engine's current
// again on the way out.  (DEVIATION: with one heap here there is
// nothing to switch.)

// ROM 0x0026ddbc MakeNewGroupFromStroke__12TWRecognizerFP11TStrokeUnit
// A word started from a stroke: a unit of the domain's type over the
// same areas, the stroke as its first sub, and the controller told.  A
// word that cannot be made is thrown rather than answered, because the
// engine has nowhere to put the stroke.
TUnit*
TWRecognizer::MakeNewGroupFromStroke(TStrokeUnit* stroke)
{
	TArray* areas = (TArray*) stroke->GetAreas();
	TWRecUnit* group = TWRecUnit::Make(fDomain, (ULong) (stroke->fKind + 1), areas);
	if (areas != nil)
		areas->Dispose();
	if (group == nil)
		Throw(exAbort, nil, nil);
	group->AddSub(stroke);
	fDomain->fController->NewGroup(group);
	return group;
}


// ROM 0x0026de70 GetPartialGroup__12TWRecognizerFPUc
// The word still being built, which is the last of the domain's units
// the controller is holding back.  `found` says whether there was one.
TUnit*
TWRecognizer::GetPartialGroup(UChar* found)
{
	TUnit* group = nil;
	TUnitList* delayed = fDomain->fController->GetDelayList(fDomain, fDomain->fType);
	if (delayed == nil)
		Throw(exAbort, nil, nil);
	if (delayed->Count() == 0)
		*found = 0;
	else
	{
		group = delayed->GetUnit(delayed->Count() - 1);
		*found = 1;
	}
	delayed->Dispose();
	return group;
}


// ROM 0x0026dff4 AddSub__12TWRecognizerFP9TWRecUnitP11TStrokeUnit
// Another stroke of the same word.
long
TWRecognizer::AddSub(TWRecUnit* group, TStrokeUnit* stroke)
{
	return group->AddSub(stroke);
}


// ROM 0x0026e038 EndSubs__12TWRecognizerFP9TWRecUnit
// No more: the word stops waiting and can be arbitrated.
long
TWRecognizer::EndSubs(TWRecUnit* group)
{
	return group->EndSubs();
}


// ROM 0x0026e074 EndInkStrokeGroup__12TWRecognizerFPP11TStrokeUnit
// A run of strokes the engine has decided are ink rather than writing,
// closed: handed to the stroke world's ink grouping
// (WRecEndInkStrokeGroup).  The ROM switches to the recogniser task's
// default heap for it and back to the engine's own afterwards
// (gWRecTaskDefaultHeap, gWRecHeap); DEVIATION: the host's engine runs in
// the ordinary heap (above), so there is nothing to switch.
void
TWRecognizer::EndInkStrokeGroup(TStrokeUnit** strokes)
{
	WRecEndInkStrokeGroup(strokes);
}


// ROM 0x0026df88 NewClassification__12TWRecognizerFP9TWRecUnit
// A word the engine has read, offered to the controller as a piece for
// the domains above.
void
TWRecognizer::NewClassification(TWRecUnit* unit)
{
	fDomain->fController->NewClassification(unit);
}


// ROM 0x0026df20 InvalidateUnit__12TWRecognizerFP9TWRecUnit
void		TWRecognizer::InvalidateUnit(TWRecUnit* unit)	{ unit->Invalidate(); }
// ROM 0x0026df64 TestInvalidUnit__12TWRecognizerFP9TWRecUnit
ULong		TWRecognizer::TestInvalidUnit(TWRecUnit* unit)	{ return unit->TestFlags(kInvalidatedUnit); }
// ROM 0x0026df70 RejectUnit__12TWRecognizerFP9TWRecUnit
void		TWRecognizer::RejectUnit(TWRecUnit* unit)		{ unit->SetFlags(kInvalidUnit); }
// ROM 0x0026df7c TestRejectedUnit__12TWRecognizerFP9TWRecUnit
ULong		TWRecognizer::TestRejectedUnit(TWRecUnit* unit)	{ return unit->TestFlags(kInvalidUnit); }
// ROM 0x0026dfc8 TestClassifiedUnit__12TWRecognizerFP9TWRecUnit
ULong		TWRecognizer::TestClassifiedUnit(TWRecUnit* unit)	{ return unit->TestFlags(kClassifiedUnit); }

// ROM 0x0026dfd4 SubCount__12TWRecognizerFP9TWRecUnit
long		TWRecognizer::SubCount(TWRecUnit* unit)			{ return unit->SubCount(); }
// ROM 0x0026dfe0 GetSub__12TWRecognizerFP9TWRecUnitUl
TUnit*		TWRecognizer::GetSub(TWRecUnit* unit, ULong index)	{ return unit->GetSub(index); }

// ROM 0x0026e1a4 AddWordInterpretation__12TWRecognizerFP9TWRecUnit
long		TWRecognizer::AddWordInterpretation(TWRecUnit* unit)	{ return unit->AddWordInterpretation(); }
// ROM 0x0026e1e8 SetCharWordString__12TWRecognizerFP9TWRecUnitUlPc
void		TWRecognizer::SetCharWordString(TWRecUnit* unit, ULong index, const char* str)	{ unit->SetCharWordString(index, str); }
// ROM 0x0026e234 SetWordString__12TWRecognizerFP9TWRecUnitUlPUs
UniChar*	TWRecognizer::SetWordString(TWRecUnit* unit, ULong index, const UniChar* str)	{ return unit->SetWordString(index, str); }
// ROM 0x0026e280 GetWordString__12TWRecognizerFP9TWRecUnitUl
Handle		TWRecognizer::GetWordString(TWRecUnit* unit, ULong index)	{ return unit->GetString(index); }
// ROM 0x0026e294 SetLabel__12TWRecognizerFP9TWRecUnitUlT2
void		TWRecognizer::SetLabel(TWRecUnit* unit, ULong index, ULong label)	{ unit->SetLabel(index, label); }
// ROM 0x0026e2b0 GetLabel__12TWRecognizerFP9TWRecUnitUl
long		TWRecognizer::GetLabel(TWRecUnit* unit, ULong index)	{ return unit->GetLabel(index); }
// ROM 0x0026e2c4 SetScore__12TWRecognizerFP9TWRecUnitUlT2
void		TWRecognizer::SetScore(TWRecUnit* unit, ULong index, ULong score)	{ unit->SetScore(index, score); }
// ROM 0x0026e2e0 GetScore__12TWRecognizerFP9TWRecUnitUl
long		TWRecognizer::GetScore(TWRecUnit* unit, ULong index)	{ return unit->GetScore(index); }
// ROM 0x0026e2f4 InterpretationCount__12TWRecognizerFP9TWRecUnit
long		TWRecognizer::InterpretationCount(TWRecUnit* unit)	{ return unit->InterpretationCount(); }

// ROM 0x0026e300 StrokeUnitStroke__12TWRecognizerFP11TStrokeUnit
TStroke*	TWRecognizer::StrokeUnitStroke(TStrokeUnit* unit)	{ return unit->fStroke; }
// ROM 0x0026e41c StrokeSize__12TWRecognizerFP11TStrokeUnit
long		TWRecognizer::StrokeSize(TStrokeUnit* unit)		{ return unit->fStroke->Count(); }
// ROM 0x0026e428 StrokeSize__12TWRecognizerFP7TStroke
long		TWRecognizer::StrokeSize(TStroke* stroke)		{ return stroke->Count(); }
// ROM 0x0026e430 GetSamplePtAddress__12TWRecognizerFP11TStrokeUnitUl
SamplePt*	TWRecognizer::GetSamplePtAddress(TStrokeUnit* unit, ULong index)	{ return unit->fStroke->GetPoint((long) index); }
// ROM 0x0026e43c GetSamplePtAddress__12TWRecognizerFP7TStrokeUl
SamplePt*	TWRecognizer::GetSamplePtAddress(TStroke* stroke, ULong index)	{ return stroke->GetPoint((long) index); }
// ROM 0x0026e448 StrokeSampleX__12TWRecognizerFP12WrecSamplePt
Fixed		TWRecognizer::StrokeSampleX(SamplePt* pt)		{ return SampleX(pt); }
// ROM 0x0026e450 StrokeSampleY__12TWRecognizerFP12WrecSamplePt
Fixed		TWRecognizer::StrokeSampleY(SamplePt* pt)		{ return SampleY(pt); }

// ROM 0x0026e458 GetStartTime__12TWRecognizerFP5TUnit
ULong		TWRecognizer::GetStartTime(TUnit* unit)			{ return unit->fStartTime; }
// ROM 0x0026e460 GetStartTime__12TWRecognizerFP7TStroke
ULong		TWRecognizer::GetStartTime(TStroke* stroke)		{ return stroke->fDownTime; }
// ROM 0x0026e524 GetEndTime__12TWRecognizerFP5TUnit
ULong		TWRecognizer::GetEndTime(TUnit* unit)			{ return unit->EndTime(); }
// ROM 0x0026e534 GetEndTime__12TWRecognizerFP7TStroke
ULong		TWRecognizer::GetEndTime(TStroke* stroke)		{ return stroke->fUpTime; }
// ROM 0x0026e468 GetStartTime__12TWRecognizerFP7TSIUnit
ULong		TWRecognizer::GetStartTime(TSIUnit* unit)		{ return unit->fStartTime; }
// ROM 0x0026e470 GetStartTime__12TWRecognizerFP11TStrokeUnit
ULong		TWRecognizer::GetStartTime(TStrokeUnit* unit)	{ return unit->fStartTime; }
// ROM 0x0026e51c GetStartTime__12TWRecognizerFP9TWRecUnit
ULong		TWRecognizer::GetStartTime(TWRecUnit* unit)		{ return unit->fStartTime; }
// ROM 0x0026e53c GetEndTime__12TWRecognizerFP7TSIUnit
ULong		TWRecognizer::GetEndTime(TSIUnit* unit)			{ return unit->fStartTime + unit->fDuration; }
// ROM 0x0026e54c GetEndTime__12TWRecognizerFP11TStrokeUnit
ULong		TWRecognizer::GetEndTime(TStrokeUnit* unit)		{ return unit->fStartTime + unit->fDuration; }
// ROM 0x0026e55c GetEndTime__12TWRecognizerFP9TWRecUnit
ULong		TWRecognizer::GetEndTime(TWRecUnit* unit)		{ return unit->fStartTime + unit->fDuration; }

// ROM 0x0026e56c UnitInfoGetPtr__12TWRecognizerFP9TWRecUnit
char*		TWRecognizer::UnitInfoGetPtr(TWRecUnit* unit)	{ return unit->fUnitInfo; }
// ROM 0x0026e574 UnitInfoSetPtr__12TWRecognizerFP9TWRecUnitPc
void		TWRecognizer::UnitInfoSetPtr(TWRecUnit* unit, char* info)	{ unit->fUnitInfo = info; }


/*------------------------------------------------------------------------------
	T h e   d o m a i n
------------------------------------------------------------------------------*/

// ROM 0x0026d84c Make__11TWRecDomainSFP11TController
// The domain, or nothing when the engine will not start: IWRecDomain
// throws if no TWRecognizer is registered, and the half-made domain and
// whatever it did make are given back.
TDomain*
TWRecDomain::Make(TController* controller)
{
	TWRecDomain* domain = new TWRecDomain;
	if (domain == nil)
		return nil;
	newton_try
	{
		domain->IWRecDomain(controller);
	}
	newton_catch_all
	{
		if (domain->fRecognizer != nil)
			domain->fRecognizer->Delete();
		domain->Dispose();
		domain = nil;
	}
	end_try;
	return domain;
}


// ROM 0x0026d91c IWRecDomain__11TWRecDomainFP11TController
// The domain takes strokes and makes words of them.  The engine is
// found by name in the protocol registry and started; there is nothing
// to be done without one, so its absence is thrown rather than
// answered.
//
// The delay is a hundred and twenty ticks: a word unit waits that long
// before the arbiter will look at it, which is the room the writer has
// to add another stroke to the same word.
void
TWRecDomain::IWRecDomain(TController* controller)
{
	IDomain(controller, kWRecDomainType, (char*) "Protocol-based Word-rec");
	fRecognizer = nil;
	fRecognizer = TWRecognizer::New(nil);
	if (fRecognizer == nil)
		Throw(exAbort, nil, nil);
	// the engine is given the domain it hangs off, which is how its own
	// calls back reach the controller
	fRecognizer->fDomain = this;
	fRecognizer->Initialize();
	SetFlags(0x80000000);
	AddPieceType(kStrokeUnitType);
	fDelay = 0x78;
	fController = controller;
	// (the ROM writes RegisterDomain out here rather than calling it)
	controller->RegisterDomain(this);
}


// ROM 0x0026df2c Dispose__11TWRecDomainFv
// The ROM gives the engine back by destroying the heap it was made in;
// with one heap here it is deleted instead.
void
TWRecDomain::Dispose(void)
{
	if (fRecognizer != nil)
	{
		fRecognizer->Delete();
		fRecognizer = nil;
	}
	fPieceTypes->Dispose();
	delete this;
}


// ROM 0x0026dd90 SignalMemoryError__11TWRecDomainFv
// The controller is told, and the count the word recogniser warns about
// goes up.
void
TWRecDomain::SignalMemoryError(void)
{
	fController->SignalMemoryError();
	gRecMemErrCount++;
}


/*------------------------------------------------------------------------------
	T h e   a r e a   i n f o r m a t i o n
------------------------------------------------------------------------------*/

// Every place that is written in - a field, a page - is a recognition
// area, and an engine may want its own block of parameters for each
// one: which dictionaries to read against, whether the field takes
// letters or numbers, how much of the writing to keep.  The block is
// the area's (it is made and freed by TRecArea), and everything that
// happens to it is asked of the engine through these four.
//
// The handle is locked around each of them, because the engine is given
// a pointer into it and the heap compacts handles.

// ROM 0x0026e57c DomainParameter__11TWRecDomainFUlN21
// What the area wants done with an engine's parameter block:
//
//   0   how big one is (written through `result`)
//   1   a new one filled in with the engine's defaults
//   2   whether `result` is a selector this domain answers at all
//   3   whatever the engine hangs off one let go, before the block is
//
// ==> 0; -1 for a selector it does not know (and for a question about
// one) and when the engine throws.
long
TWRecDomain::DomainParameter(ULong selector, ULong result, ULong info)
{
	volatile long err = 0;
	if (info != 0)
		HLock((Handle) info);
	WREC_TRY
		switch (selector)
		{
		case 0:
			*(long*) result = fRecognizer->AreaInfoGetSize();
			break;
		case 1:
			fRecognizer->AreaInfoFillDefaults((Handle) info);
			break;
		case 2:
			if (result > 3)
				err = -1;
			break;
		case 3:
			fRecognizer->AreaInfoFreeDependents((Handle) info);
			break;
		default:
			err = -1;
			break;
		}
	WREC_CATCH
		err = -1;
	WREC_END;
	if (info != 0)
		HUnlock((Handle) info);
	return err;
}


// ROM 0x0026e6ac ConfigureArea__11TWRecDomainFRC6RefVarUl
// The engine's block filled in from the area's recognition
// configuration - the frame a view's recognition flags and its
// `recConfig` slot come to.
void
TWRecDomain::ConfigureArea(RefArg config, ULong info)
{
	if (info != 0)
		HLock((Handle) info);
	WREC_TRY
		fRecognizer->AreaInfoConfigure((Handle) info, config);
	WREC_CATCH
	WREC_END;
	if (info != 0)
		HUnlock((Handle) info);
}


// ROM 0x0026e768 SetParameters__11TWRecDomainFPPc
// The block the domain is to work with from now on: the controller
// hands it over before classifying a unit written in a different area
// from the last one.
//
// (BUG, kept: the override does not call the base, so `fParameters` is
//  never written down - the controller therefore hands the block over
//  again before every unit rather than only when it changes.  And the
//  answer is the wrong way round: "the parameters changed" is what it
//  says when the engine has just run out of memory, and "unchanged"
//  every other time.)
Boolean
TWRecDomain::SetParameters(Handle params)
{
	Boolean failed = false;
	WREC_TRY
		fRecognizer->AreaInfoSetParameters(params);
	WREC_CATCH
		failed = true;
	WREC_END;
	return failed;
}


/*------------------------------------------------------------------------------
	W h a t   t h e   c o n t r o l l e r   a s k s
------------------------------------------------------------------------------*/

// ROM 0x0026e478 Group__11TWRecDomainFP5TUnitP8dInfoRec
// A stroke offered: the engine decides whether it belongs to the word
// it is building or starts another.  ==> 1 always - the domain takes
// every stroke it is given.
long
TWRecDomain::Group(TUnit* unit, dInfoRec* /*info*/)
{
	WREC_TRY
		fRecognizer->Group((TStrokeUnit*) unit);
	WREC_CATCH
	WREC_END;
	return 1;
}


// ROM 0x0026e0a8 Classify__11TWRecDomainFP5TUnit
// The word read.  A unit the engine made nothing of is marked invalid
// and closed; one that is still worth something goes back to the
// controller as a piece for the domains above.
//
// (0x00200000, which the flag test here pairs with kInvalidatedUnit,
// has no name in the ROM's own headers.)
void
TWRecDomain::Classify(TUnit* unit)
{
	WREC_TRY
		fRecognizer->Classify((TWRecUnit*) unit);
		if (unit->InterpretationCount() == 0)
		{
			unit->SetFlags(kInvalidUnit);
			((TSIUnit*) unit)->EndSubs();
		}
		if (!unit->TestFlags(kInvalidatedUnit | 0x00200000))
			fController->NewClassification(unit);
	WREC_CATCH
		unit->SetFlags(kInvalidUnit);
	WREC_END;
}


// ROM 0x0026e308 Reclassify__11TWRecDomainFP5TUnit
// The word read again - after the writing has changed, or the
// dictionaries have.  What was read before is thrown away first, last
// interpretation first, so that the engine starts from nothing.
void
TWRecDomain::Reclassify(TUnit* unit)
{
	for (long i = unit->InterpretationCount() - 1; i >= 0; i--)
		((TSIUnit*) unit)->DeleteInterpretation((ULong) i);
	WREC_TRY
		fRecognizer->Reclassify((TWRecUnit*) unit);
	WREC_CATCH
		unit->SetFlags(kInvalidUnit);
	WREC_END;
	if (unit->InterpretationCount() == 0)
	{
		unit->SetFlags(kInvalidUnit);
		((TSIUnit*) unit)->EndSubs();
	}
}


// ROM 0x0026db68 UnitConfidence__11TWRecDomainFP7TSIUnit
// What the engine makes of the unit: a word, ink, or not sure.  An
// engine that fails is taken to have said ink, which is what the
// recogniser above does with a word it cannot read.
long
TWRecDomain::UnitConfidence(TSIUnit* unit)
{
	long confidence = kWRecInk;
	WREC_TRY
		confidence = fRecognizer->UnitConfidence((TWRecUnit*) unit);
	WREC_CATCH
	WREC_END;
	return confidence;
}


// ROM 0x0026da20 VerifyWordSymbols__11TWRecDomainFPUs
// Whether the engine will admit the word - what a dictionary check asks.
Boolean
TWRecDomain::VerifyWordSymbols(UniChar* word)
{
	Boolean ok = false;
	WREC_TRY
		ok = fRecognizer->VerifyWordSymbols(word);
	WREC_CATCH
	WREC_END;
	return ok;
}


// ROM 0x0026dacc UnitInfoFreePtr__11TWRecDomainFPc
// A unit's working store given back to the engine that made it, which
// is the only thing that knows where it came from.
void
TWRecDomain::UnitInfoFreePtr(char* info)
{
	WREC_TRY
		fRecognizer->UnitInfoFreePtr(info);
	WREC_CATCH
	WREC_END;
}


// ROM 0x0026dc14 Sleep__11TWRecDomainFv
// The machine is going to sleep: the engine lets go of everything, and
// the ROM then destroys its heap outright - the whole point of giving
// it one.
void
TWRecDomain::Sleep(void)
{
	WREC_TRY
		fRecognizer->Sleep();
	WREC_CATCH
	WREC_END;
}


// ROM 0x0026dcc4 WakeUp__11TWRecDomainFv
// ... and the heap is made again before the engine is woken.
void
TWRecDomain::WakeUp(void)
{
	WREC_TRY
		fRecognizer->WakeUp();
	WREC_CATCH
	WREC_END;
}
