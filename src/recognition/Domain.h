/*
	File:		recognition/Domain.h

	Contains:	TDomain, the base of the recognisers' domains - the piece of
				the recogniser that groups units of the piece types it
				accepts into units of its own type and classifies them.  The
				base holds the controller it belongs to, the list of piece
				types, its type and name, the delay its units wait before
				arbitration, and a parameter block; the grouping and
				classifying are the subclasses' (TStrokeDomain,
				TEdgeListDomain, TGeneralShapeDomain, the word domains: NOT
				YET RECONSTRUCTED).  The root domain ('ROOT', a plain
				TDomain) is what the stroke world's click units are made
				in.  The ROM's TDomain is 0x24 bytes.

	Reconstructed from the MP2x00 US ROM (0x0020cda4-0x0020d0f0); each
	function cites its origin.
*/

#ifndef __DOMAIN_H
#define __DOMAIN_H

#include "Unit.h"

class TController;
class TRecArea;
struct dInfoRec;

class TDomain : public TRecObject
{
public:
						TDomain();								// ROM 0x0020cd24 __ct__7TDomainFv
	virtual				~TDomain();								// ROM 0x0020cd64 __dt__7TDomainFv
	static TDomain*		Make(TController* controller, ULong type, char* name);	// ROM 0x0020cf88 Make__7TDomainSFP11TControllerUlPc
	void				IDomain(TController* controller, ULong type, char* name);	// ROM 0x0020d008 IDomain__7TDomainFP11TControllerUlPc
	static ULong		VUnitInClass(ULong type, ULong classType);	// ROM 0x0020cfc4 VUnitInClass__7TDomainSFUlT1 - whether type is a word type ('WRXR', 'JANK', 'WREC') when classType is 'WORD'

	virtual void		Dispose(void);							// ROM 0x0020d054 Dispose__7TDomainFv
	virtual void		Dump(TMsg* msg);						// ROM 0x0020cdc4 Dump__7TDomainFP4TMsg
	virtual long		SizeInBytes(void);						// ROM 0x0020d084 SizeInBytes__7TDomainFv
	virtual void		Classify(TUnit* unit);					// ROM 0x0020d0e0 Classify__7TDomainFP5TUnit (+0x10: nothing)
	virtual void		Reclassify(TUnit* unit);				// ROM 0x0020d0e4 Reclassify__7TDomainFP5TUnit (+0x14: nothing)
	virtual long		Group(TUnit* unit, dInfoRec* info);		// ROM 0x0020d0e8 Group__7TDomainFP5TUnitP8dInfoRec (+0x18: 0)
	virtual long		PreGroup(TUnit* unit);					// ROM 0x0020cda4 PreGroup__7TDomainFP5TUnit (+0x1c: 0)
	virtual void		DumpName(TMsg* msg);					// ROM 0x0020ceb8 DumpName__7TDomainFP4TMsg (+0x20)
	virtual long		PruneDictionary(TUnit* unit);			// ROM 0x0020cdac PruneDictionary__7TDomainFP5TUnit (+0x24: 0)
	virtual long		PruneConstraints(TUnit* unit);			// ROM 0x0020cdb4 PruneConstraints__7TDomainFP5TUnit (+0x28: 0)
	virtual long		DomainParameter(ULong selector, ULong result, ULong arg);	// ROM 0x0020cf38 DomainParameter__7TDomainFUlN21 (+0x2c: 0)
	virtual Boolean		SetParameters(Handle params);			// ROM 0x0020cf14 SetParameters__7TDomainFPPc (+0x30: ==> whether they changed)
	virtual void		InvalParameters(void);					// ROM 0x0020cf2c InvalParameters__7TDomainFv (+0x34)
	virtual void		ConfigureSubDomain(TRecArea* area);		// ROM 0x0020cf84 ConfigureSubDomain__7TDomainFP8TRecArea (+0x38: nothing)
	virtual long		CompleteUnit(void);						// ROM 0x0020cdbc CompleteUnit__7TDomainFv (+0x3c: 0)

	void				AddPieceType(ULong type);				// ROM 0x0020d0b8 AddPieceType__7TDomainFUl

	ULong				Type(void) const		{ return fType; }
	ULong				Delay(void) const		{ return fDelay; }

	TController*		fController;	// +0x08
	TTypeList*			fPieceTypes;	// +0x0c
	ULong				fType;			// +0x10
	char*				fName;			// +0x14
	ULong				fDelay;			// +0x18  ticks its units wait before arbitration
	long				fLevel;			// +0x1c  how far its type is from the strokes (TController::Initialize)
	Handle				fParameters;	// +0x20  -1 when invalid
};

// The stroke domain ('STRK'), the first domain above the stroke world:
// it takes the clicks the pen makes and, as each one's stroke finishes,
// turns it into a stroke unit - the piece everything else is built from.
class TStrokeDomain : public TDomain
{
public:
	static TStrokeDomain*	Make(TController* controller);		// ROM 0x00220e94 Make__13TStrokeDomainSFP11TController
	void				IStrokeDomain(TController* controller);	// ROM 0x00220edc IStrokeDomain__13TStrokeDomainFP11TController

	virtual void		Dispose(void);							// ROM 0x0022105c Dispose__13TStrokeDomainFv
	virtual void		Classify(TUnit* unit);					// ROM 0x00221ec0 Classify__13TStrokeDomainFP5TUnit - the stroke unit offered as a piece
	virtual long		Group(TUnit* unit, dInfoRec* info);		// ROM 0x00221dc0 Group__13TStrokeDomainFP5TUnitP8dInfoRec - a finished click made into a stroke unit
};

extern TStrokeDomain*	gStrokeDomain;						// ROM 0x0c101680 gStrokeDomain

extern TDomain*	gRootDomain;								// ROM 0x0c101884 gRootDomain - the 'ROOT' domain the clicks are made in

#endif	/* __DOMAIN_H */
