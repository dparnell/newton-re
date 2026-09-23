/*
	File:		recognition/WordUnit.h

	Contains:	The units the word recognisers work in.

				`TStdWordUnit` is a TSIUnit whose interpretations carry a
				word: each one's parameter is a handle holding the string
				the recogniser read, and the unit answers the word's
				baseline, slant and size for whoever lays it out.  The
				base's own answers are the plain ones - the box's bottom
				edge for the baseline, nothing for the slant and the size
				- and a recogniser that measures them says so by
				overriding.

				`TRecUnit` is that with a block of the engine's own
				working store hung off it, which the domain frees when
				the unit goes; `TWRecUnit` is the unit the protocol-based
				word domain makes ('WREC'), and adds nothing further.

				An interpretation's `param` is a plain handle here rather
				than the TRecObject a TSIUnit's is, which is why deleting
				one is overridden: the handle has to be given back as a
				handle.

	NOT YET RECONSTRUCTED: the training data (GetTrainingData answers
	nothing, as the base's does), and the Dump methods.

	Reconstructed from the MP2x00 US ROM (0x0021f67c-0x0021fc78,
	0x0026e810-0x0026e8e8); each function cites its origin.
*/

#ifndef __WORDUNIT_H
#define __WORDUNIT_H

#include "Unit.h"

// A word unit's interpretation holds its string in a handle named 'istr.
const ULong kWordStringHandleName = 0x69737472;		// 'istr'

class TStdWordUnit : public TSIUnit
{
public:
	long				IStdWordUnit(TDomain* domain, ULong kind, TArray* areas,
									 ULong interpSize);	// ROM 0x0021f67c IStdWordUnit__12TStdWordUnitFP7TDomainUlP6TArrayT2

	virtual void		Dump(TMsg* msg);					// ROM 0x0021fb40 Dump__12TStdWordUnitFP4TMsg (nothing)
	virtual long		SizeInBytes(void);					// ROM 0x0021f9c8 SizeInBytes__12TStdWordUnitFv - the strings counted too
	virtual long		DeleteInterpretation(ULong index);	// ROM 0x0021fc28 DeleteInterpretation__12TStdWordUnitFUl (+0x70: the string handle given back first)
	virtual TRecObject*	GetParam(ULong index);				// ROM 0x0021f93c GetParam__12TStdWordUnitFUl (+0x98: none - the parameter is the string)
	virtual void		EndUnit(void);						// ROM 0x0021f998 EndUnit__12TStdWordUnitFv (+0xa8: the subs ended and the interpretations compacted)

	// the word unit's own, in the ROM's vtable order from +0xac
	virtual long		AddWordInterpretation(void);		// ROM 0x0021fb44 AddWordInterpretation__12TStdWordUnitFv (+0xac: one added at the end)
	virtual long		InsertWordInterpretation(ULong index);	// ROM 0x0021fb54 InsertWordInterpretation__12TStdWordUnitFUl (+0xb0: ==> its index, -1 for no memory)
	virtual void		SetCharWordString(ULong index, const char* str);	// ROM 0x0021fad0 SetCharWordString__12TStdWordUnitFUlPc (+0xb4)
	virtual UniChar*	SetWordString(ULong index, const UniChar* str);	// ROM 0x0021fa6c SetWordString__12TStdWordUnitFUlPUs (+0xb8: ==> the string, nil for no memory)
	virtual Handle		GetString(ULong index);				// ROM 0x0021fa44 GetString__12TStdWordUnitFUl (+0xbc)
	virtual void		SetParam(UnitInterpretation* interp, ULong elementSize, char* data);	// ROM 0x0021f944 SetParam__12TStdWordUnitFP18UnitInterpretationUlPc (+0xc0: nothing)
	// Where the word sits: the two ends of the line it stands on.  The
	// base answers the bottom of its own box, which is the best that can
	// be said without reading it.
	virtual void		GetWordBase(FPoint* left, FPoint* right, ULong index);	// ROM 0x0021f948 GetWordBase__12TStdWordUnitFP6FPointT1Ul (+0xc4)
	virtual long		GetWordSlant(ULong index);			// ROM 0x0021f988 GetWordSlant__12TStdWordUnitFUl (+0xc8: 0)
	virtual long		GetWordSize(ULong index);			// ROM 0x0021f990 GetWordSize__12TStdWordUnitFUl (+0xcc: 0)
	virtual void		ReinforceWordChoice(long index);	// ROM 0x0021f9c4 ReinforceWordChoice__12TStdWordUnitFl (+0xd0: nothing)
	virtual Handle		GetTrainingData(long index);		// ROM 0x0021fa38 GetTrainingData__12TStdWordUnitFl (+0xd4: none)
	virtual void		DisposeTrainingData(Handle data);	// ROM 0x0021fa40 DisposeTrainingData__12TStdWordUnitFPPc (+0xd8: nothing)
};									// 0x3c bytes, as TSIUnit


class TRecUnit : public TStdWordUnit
{
public:
	long				IRecUnit(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0026e810 IRecUnit__8TRecUnitFP7TDomainUlP6TArray

	virtual void		Dump(TMsg* msg);					// ROM 0x0026e8e4 Dump__8TRecUnitFP4TMsg (nothing)
	virtual void		IDispose(void);						// ROM 0x0026e8b8 IDispose__8TRecUnitFv - the engine's working store given back first

	char*				fUnitInfo;		// +0x3c  the engine's own, freed through the domain
};									// 0x40 bytes


class TWRecUnit : public TRecUnit
{
public:
	static TWRecUnit*	Make(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0026e83c Make__9TWRecUnitSFP7TDomainUlP6TArray
	long				IWRecUnit(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0026e8b4 IWRecUnit__9TWRecUnitFP7TDomainUlP6TArray
};									// 0x40 bytes

#endif	/* __WORDUNIT_H */
