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

	DEVIATION: the host has one heap.  `NewVMHeap`, `SetHeap` and
	`DestroyVMHeap` are NOT YET, so the engine runs in the ordinary heap
	and the domain only keeps the exception handler.

	NOT YET RECONSTRUCTED: the protocol's own methods listed above (the
	engine's side of the grouping), `ConfigureArea` and the area
	information a recogniser keeps per writing area.

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
	// NOT YET: DomainParameter (+0x2c), SetParameters (+0x30) and
	// ConfigureArea (+0x40), which are the area information a
	// recogniser keeps for each place that is written in

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
