/*
	File:		recognition/WRecDomain.h

	Contains:	`TWRecognizer`, the protocol a handwriting engine plugs
				into, and `TWRecDomain` ('WREC'), the domain that drives
				one.

				The domain itself does almost nothing.  It takes strokes
				as its pieces ('STRK'), makes them into word units, and
				hands every question straight to the protocol; what it
				adds is a heap and an exception handler.  The ROM gives
				the engine a 222 KB virtual-memory heap of its own, makes
				that heap current around every call into it, and treats
				any exception coming out as the engine having run out of
				memory - `SignalMemoryError` then puts the recogniser to
				sleep rather than let it go on in an unknown state.

				The engine calls back the other way through the
				protocol's own (non-virtual) methods, which is where the
				grouping is kept: `GetPartialGroup` answers the word
				being built, `MakeNewGroupFromStroke` starts one,
				`AddSub` adds a stroke to it and `EndSubs` closes it, and
				`AddWordInterpretation`/`SetWordString`/`SetScore` are
				how a reading is put on a unit.

	DEVIATION: the engine runs in the ordinary heap and the domain only
	keeps the exception handler.  The ROM gives the recogniser a VM heap
	of its own (`NewVMHeap`, `SetHeap`, `DestroyVMHeap` -
	memory/MemoryManager.cpp has those, not `CreateVMHeap`) so that its
	memory is kept apart and can be thrown away whole when it runs out;
	nothing the host does depends on that, so the engine stays in the
	ordinary heap (decided 2026-09-29, and revisited only if a heap of
	its own becomes observable).

	`EndInkStrokeGroup` hands a run of ink to the stroke world's grouping
	(`WRecEndInkStrokeGroup`); the ROM's heap switch around it is the
	DEVIATION above.

	Reconstructed from the MP2x00 US ROM (0x0026d84c-0x0026e808); the
	protocol's interface follows the ROM's dispatch table
	(analysis/classinfo.py --name TRosRecognizer).  Each function cites
	its origin.
*/

#ifndef __WRECDOMAIN_H
#define __WRECDOMAIN_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

#include "Domain.h"
#include "WordUnit.h"

class TStroke;
class TStrokeUnit;
class TController;
class TWRecDomain;

// the domain's type, and the piece type it takes
const ULong kWRecDomainType = 'WREC';
const ULong kStrokeUnitType = 'STRK';

// What the engine makes of a unit, which is what the recogniser above
// it asks for: 0 it is not sure, 1 it read the writing, 2 it could not
// and the unit is ink.
const long kWRecUnsure = 0;
const long kWRecWord = 1;
const long kWRecInk = 2;


PROTOCOL TWRecognizer : public TProtocol
{
public:
	static TWRecognizer*	New(const char* implementation);	// ROM 0x00388bd0 New__12TWRecognizerSFPc
	void				Delete(void);							// ROM 0x00388bfc Delete__12TWRecognizerFv

	VIRTUAL void		Initialize(void) ENDVIRTUAL;										// ROM 0x00388c18
	// A stroke offered to the engine: it decides whether the stroke
	// joins the word being built or starts a new one.
	VIRTUAL void		Group(TStrokeUnit* stroke) ENDVIRTUAL;								// ROM 0x00388c24
	VIRTUAL long		Classify(TWRecUnit* unit) ENDVIRTUAL;								// ROM 0x00388c30
	VIRTUAL long		Reclassify(TWRecUnit* unit) ENDVIRTUAL;								// ROM 0x00388c3c
	VIRTUAL long		FindBaseline(TStroke** strokes, Point* out) ENDVIRTUAL;				// ROM 0x00388c48
	VIRTUAL void		GroupInkStroke(TStrokeUnit* stroke, ULong a, ULong b, Boolean flag) ENDVIRTUAL;	// ROM 0x00388c54
	VIRTUAL long		AreaInfoGetSize(void) ENDVIRTUAL;									// ROM 0x00388c60
	VIRTUAL void		AreaInfoFillDefaults(Handle info) ENDVIRTUAL;						// ROM 0x00388c6c
	VIRTUAL void		AreaInfoConfigure(Handle info, RefArg config) ENDVIRTUAL;			// ROM 0x00388c78
	VIRTUAL void		AreaInfoFreeDependents(Handle info) ENDVIRTUAL;						// ROM 0x00388c84
	VIRTUAL void		AreaInfoSetParameters(Handle info) ENDVIRTUAL;						// ROM 0x00388c90
	VIRTUAL void		UnitInfoFreePtr(char* info) ENDVIRTUAL;								// ROM 0x00388c9c
	VIRTUAL Boolean		VerifyWordSymbols(UniChar* word) ENDVIRTUAL;							// ROM 0x00388ca8
	VIRTUAL long		UnitConfidence(TWRecUnit* unit) ENDVIRTUAL;							// ROM 0x00388cb4
	VIRTUAL void		Sleep(void) ENDVIRTUAL;												// ROM 0x00388cc0
	VIRTUAL void		WakeUp(void) ENDVIRTUAL;											// ROM 0x00388ccc

	// What the engine calls back into, which is where the grouping is
	// kept.  These are the protocol's own, not dispatched: an engine
	// gets them by being one.  Each puts the task's heap back before
	// doing anything that allocates and makes its own current again
	// afterwards, so that the recogniser's objects stay out of the
	// engine's heap.
	TUnit*		MakeNewGroupFromStroke(TStrokeUnit* stroke);	// ROM 0x0026ddbc MakeNewGroupFromStroke__12TWRecognizerFP11TStrokeUnit
	TUnit*		GetPartialGroup(UChar* found);				// ROM 0x0026de70 GetPartialGroup__12TWRecognizerFPUc - the word still being built
	long		AddSub(TWRecUnit* group, TStrokeUnit* stroke);	// ROM 0x0026dff4 AddSub__12TWRecognizerFP9TWRecUnitP11TStrokeUnit
	long		EndSubs(TWRecUnit* group);					// ROM 0x0026e038 EndSubs__12TWRecognizerFP9TWRecUnit
	void		EndInkStrokeGroup(TStrokeUnit** strokes);	// ROM 0x0026e074 EndInkStrokeGroup__12TWRecognizerFPP11TStrokeUnit - WRecEndInkStrokeGroup (the heap switch: DEVIATION)
	void		NewClassification(TWRecUnit* unit);			// ROM 0x0026df88 NewClassification__12TWRecognizerFP9TWRecUnit

	void		InvalidateUnit(TWRecUnit* unit);			// ROM 0x0026df20 InvalidateUnit__12TWRecognizerFP9TWRecUnit
	ULong		TestInvalidUnit(TWRecUnit* unit);			// ROM 0x0026df64 TestInvalidUnit__12TWRecognizerFP9TWRecUnit
	void		RejectUnit(TWRecUnit* unit);				// ROM 0x0026df70 RejectUnit__12TWRecognizerFP9TWRecUnit
	ULong		TestRejectedUnit(TWRecUnit* unit);			// ROM 0x0026df7c TestRejectedUnit__12TWRecognizerFP9TWRecUnit
	ULong		TestClassifiedUnit(TWRecUnit* unit);		// ROM 0x0026dfc8 TestClassifiedUnit__12TWRecognizerFP9TWRecUnit

	long		SubCount(TWRecUnit* unit);					// ROM 0x0026dfd4 SubCount__12TWRecognizerFP9TWRecUnit
	TUnit*		GetSub(TWRecUnit* unit, ULong index);		// ROM 0x0026dfe0 GetSub__12TWRecognizerFP9TWRecUnitUl

	long		AddWordInterpretation(TWRecUnit* unit);		// ROM 0x0026e1a4 AddWordInterpretation__12TWRecognizerFP9TWRecUnit
	void		SetCharWordString(TWRecUnit* unit, ULong index, const char* str);	// ROM 0x0026e1e8 SetCharWordString__12TWRecognizerFP9TWRecUnitUlPc
	UniChar*	SetWordString(TWRecUnit* unit, ULong index, const UniChar* str);	// ROM 0x0026e234 SetWordString__12TWRecognizerFP9TWRecUnitUlPUs
	Handle		GetWordString(TWRecUnit* unit, ULong index);	// ROM 0x0026e280 GetWordString__12TWRecognizerFP9TWRecUnitUl
	void		SetLabel(TWRecUnit* unit, ULong index, ULong label);	// ROM 0x0026e294 SetLabel__12TWRecognizerFP9TWRecUnitUlT2
	long		GetLabel(TWRecUnit* unit, ULong index);		// ROM 0x0026e2b0 GetLabel__12TWRecognizerFP9TWRecUnitUl
	void		SetScore(TWRecUnit* unit, ULong index, ULong score);	// ROM 0x0026e2c4 SetScore__12TWRecognizerFP9TWRecUnitUlT2
	long		GetScore(TWRecUnit* unit, ULong index);		// ROM 0x0026e2e0 GetScore__12TWRecognizerFP9TWRecUnitUl
	long		InterpretationCount(TWRecUnit* unit);		// ROM 0x0026e2f4 InterpretationCount__12TWRecognizerFP9TWRecUnit

	// the strokes themselves, as the engine reads them
	TStroke*	StrokeUnitStroke(TStrokeUnit* unit);		// ROM 0x0026e300 StrokeUnitStroke__12TWRecognizerFP11TStrokeUnit
	long		StrokeSize(TStrokeUnit* unit);				// ROM 0x0026e41c StrokeSize__12TWRecognizerFP11TStrokeUnit
	long		StrokeSize(TStroke* stroke);				// ROM 0x0026e428 StrokeSize__12TWRecognizerFP7TStroke
	SamplePt*	GetSamplePtAddress(TStrokeUnit* unit, ULong index);	// ROM 0x0026e430 GetSamplePtAddress__12TWRecognizerFP11TStrokeUnitUl
	SamplePt*	GetSamplePtAddress(TStroke* stroke, ULong index);	// ROM 0x0026e43c GetSamplePtAddress__12TWRecognizerFP7TStrokeUl
	Fixed		StrokeSampleX(SamplePt* pt);				// ROM 0x0026e448 StrokeSampleX__12TWRecognizerFP12WrecSamplePt
	Fixed		StrokeSampleY(SamplePt* pt);				// ROM 0x0026e450 StrokeSampleY__12TWRecognizerFP12WrecSamplePt

	ULong		GetStartTime(TUnit* unit);					// ROM 0x0026e458 GetStartTime__12TWRecognizerFP5TUnit
	ULong		GetStartTime(TStroke* stroke);				// ROM 0x0026e460 GetStartTime__12TWRecognizerFP7TStroke
	ULong		GetEndTime(TUnit* unit);					// ROM 0x0026e524 GetEndTime__12TWRecognizerFP5TUnit
	ULong		GetEndTime(TStroke* stroke);				// ROM 0x0026e534 GetEndTime__12TWRecognizerFP7TStroke
	// the same for each kind of unit (the public jump table's entries)
	ULong		GetStartTime(TSIUnit* unit);				// ROM 0x0026e468 GetStartTime__12TWRecognizerFP7TSIUnit
	ULong		GetStartTime(TStrokeUnit* unit);			// ROM 0x0026e470 GetStartTime__12TWRecognizerFP11TStrokeUnit
	ULong		GetStartTime(TWRecUnit* unit);				// ROM 0x0026e51c GetStartTime__12TWRecognizerFP9TWRecUnit
	ULong		GetEndTime(TSIUnit* unit);					// ROM 0x0026e53c GetEndTime__12TWRecognizerFP7TSIUnit
	ULong		GetEndTime(TStrokeUnit* unit);				// ROM 0x0026e54c GetEndTime__12TWRecognizerFP11TStrokeUnit
	ULong		GetEndTime(TWRecUnit* unit);				// ROM 0x0026e55c GetEndTime__12TWRecognizerFP9TWRecUnit

	char*		UnitInfoGetPtr(TWRecUnit* unit);			// ROM 0x0026e56c UnitInfoGetPtr__12TWRecognizerFP9TWRecUnit
	void		UnitInfoSetPtr(TWRecUnit* unit, char* info);	// ROM 0x0026e574 UnitInfoSetPtr__12TWRecognizerFP9TWRecUnitPc

	TWRecDomain*	fDomain;	// +0x10  the domain that made it
};


class TWRecDomain : public TDomain
{
public:
	static TDomain*		Make(TController* controller);			// ROM 0x0026d84c Make__11TWRecDomainSFP11TController
	void				IWRecDomain(TController* controller);	// ROM 0x0026d91c IWRecDomain__11TWRecDomainFP11TController

	virtual void		Dispose(void);							// ROM 0x0026df2c Dispose__11TWRecDomainFv (+0x00)
	virtual void		Classify(TUnit* unit);					// ROM 0x0026e0a8 Classify__11TWRecDomainFP5TUnit (+0x10)
	virtual void		Reclassify(TUnit* unit);				// ROM 0x0026e308 Reclassify__11TWRecDomainFP5TUnit (+0x14)
	virtual long		Group(TUnit* unit, dInfoRec* info);		// ROM 0x0026e478 Group__11TWRecDomainFP5TUnitP8dInfoRec (+0x18)
	// the area information an engine keeps for each place that is
	// written in
	virtual long		DomainParameter(ULong selector, ULong result, ULong info);	// ROM 0x0026e57c DomainParameter__11TWRecDomainFUlN21 (+0x2c)
	virtual Boolean		SetParameters(Handle params);			// ROM 0x0026e768 SetParameters__11TWRecDomainFPPc (+0x30)
	virtual void		ConfigureArea(RefArg config, ULong info);	// ROM 0x0026e6ac ConfigureArea__11TWRecDomainFRC6RefVarUl (+0x40)

	// the engine asked, each with its heap made current and an
	// exception handler round it
	Boolean				VerifyWordSymbols(UniChar* word);		// ROM 0x0026da20 VerifyWordSymbols__11TWRecDomainFPUs
	void				UnitInfoFreePtr(char* info);			// ROM 0x0026dacc UnitInfoFreePtr__11TWRecDomainFPc
	long				UnitConfidence(TSIUnit* unit);			// ROM 0x0026db68 UnitConfidence__11TWRecDomainFP7TSIUnit
	void				Sleep(void);							// ROM 0x0026dc14 Sleep__11TWRecDomainFv
	void				WakeUp(void);							// ROM 0x0026dcc4 WakeUp__11TWRecDomainFv
	// The engine has failed: it is put to sleep rather than asked
	// anything else.
	void				SignalMemoryError(void);				// ROM 0x0026dd90 SignalMemoryError__11TWRecDomainFv

	TWRecognizer*		fRecognizer;	// +0x24
};									// 0x28 bytes

// ROM 0x0c104f78 gRecMemErrCount
// How many times the engine has run out of memory, which is what the
// word recogniser tells the user about once a day.
extern long		gRecMemErrCount;

#endif	/* __WRECDOMAIN_H */
