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

	Reconstructed from the MP2100 D ROM (0x0020a674-0x0020a9c0); each
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
	static TDomain*		Make(TController* controller, ULong type, char* name);	// ROM 0x0020a858 Make__7TDomainSFP11TControllerUlPc
	void				IDomain(TController* controller, ULong type, char* name);	// ROM 0x0020a8d8 IDomain__7TDomainFP11TControllerUlPc
	static ULong		VUnitInClass(ULong type, ULong classType);	// ROM 0x0020a894 VUnitInClass__7TDomainSFUlT1 - whether type is a word type ('WRXR', 'JANK', 'WREC') when classType is 'WORD'

	virtual void		Dispose(void);							// ROM 0x0020a924 Dispose__7TDomainFv
	virtual void		Dump(TMsg* msg);						// ROM 0x0020a694 Dump__7TDomainFP4TMsg
	virtual long		SizeInBytes(void);						// ROM 0x0020a954 SizeInBytes__7TDomainFv
	virtual void		Classify(TUnit* unit);					// ROM 0x0020a9b0 Classify__7TDomainFP5TUnit (+0x10: nothing)
	virtual void		Reclassify(TUnit* unit);				// ROM 0x0020a9b4 Reclassify__7TDomainFP5TUnit (+0x14: nothing)
	virtual long		Group(TUnit* unit, dInfoRec* info);		// ROM 0x0020a9b8 Group__7TDomainFP5TUnitP8dInfoRec (+0x18: 0)
	virtual long		PreGroup(TUnit* unit);					// ROM 0x0020a674 PreGroup__7TDomainFP5TUnit (+0x1c: 0)
	virtual void		DumpName(TMsg* msg);					// ROM 0x0020a788 DumpName__7TDomainFP4TMsg (+0x20)
	virtual long		PruneDictionary(TUnit* unit);			// ROM 0x0020a67c PruneDictionary__7TDomainFP5TUnit (+0x24: 0)
	virtual long		PruneConstraints(TUnit* unit);			// ROM 0x0020a684 PruneConstraints__7TDomainFP5TUnit (+0x28: 0)
	virtual void		DomainParameter(ULong selector, ULong result, ULong arg);	// ROM 0x0020a808 DomainParameter__7TDomainFUlN21 (+0x2c)
	virtual Boolean		SetParameters(Handle params);			// ROM 0x0020a7e4 SetParameters__7TDomainFPPc (+0x30: ==> whether they changed)
	virtual void		InvalParameters(void);					// ROM 0x0020a7fc InvalParameters__7TDomainFv (+0x34)
	virtual void		ConfigureSubDomain(TRecArea* area);		// ROM 0x0020a854 ConfigureSubDomain__7TDomainFP8TRecArea (+0x38: nothing)
	virtual long		CompleteUnit(void);						// ROM 0x0020a68c CompleteUnit__7TDomainFv (+0x3c: 0)

	void				AddPieceType(ULong type);				// ROM 0x0020a988 AddPieceType__7TDomainFUl

	ULong				Type(void) const		{ return fType; }
	ULong				Delay(void) const		{ return fDelay; }

	TController*		fController;	// +0x08
	TTypeList*			fPieceTypes;	// +0x0c
	ULong				fType;			// +0x10
	char*				fName;			// +0x14
	ULong				fDelay;			// +0x18  ticks its units wait before arbitration
	ULong				fUnused1c;		// +0x1c
	Handle				fParameters;	// +0x20  -1 when invalid
};

extern TDomain*	gRootDomain;								// ROM 0x0c101970 gRootDomain - the 'ROOT' domain the clicks are made in

#endif	/* __DOMAIN_H */
