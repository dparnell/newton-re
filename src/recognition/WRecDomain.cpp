/*
	File:		recognition/WRecDomain.cpp

	Contains:	The word domain and the engine behind it - WRecDomain.h.

				Every one of these is the same shape: make the engine's
				heap current, ask it, put the heap back.  Anything thrown
				on the way out is taken as the engine having run out of
				memory: the heap goes back, the failure is counted, and
				the recogniser is put to sleep rather than asked anything
				else.

				DEVIATION: the host has one heap.  NewVMHeap, SetHeap and
				DestroyVMHeap are NOT YET, so the engine runs in the
				ordinary heap and only the exception handler is left.
				Where the ROM gives the recogniser back by destroying the
				heap it lived in, this deletes it.
*/

#include "WRecDomain.h"
#include "Controller.h"
#include "NewtonExceptions.h"

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
