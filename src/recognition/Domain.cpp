/*
	File:		recognition/Domain.cpp

	Contains:	TDomain, the base of the recognisers' domains.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Domain.h"
#include "Areas.h"

TDomain*	gRootDomain = nil;			// ROM 0x0c101970 gRootDomain


// ROM 0x0020a858 Make__7TDomainSFP11TControllerUlPc
TDomain*
TDomain::Make(TController* controller, ULong type, char* name)
{
	TDomain* domain = new TDomain;
	if (domain != nil)
		domain->IDomain(controller, type, name);
	return domain;
}


// ROM 0x0020a8d8 IDomain__7TDomainFP11TControllerUlPc
// A domain of a type and name in a controller: no piece types, no delay,
// no parameters yet.
void
TDomain::IDomain(TController* controller, ULong type, char* name)
{
	fFlags = 0;
	fController = controller;
	fType = type;
	fName = name;
	fDelay = 0;
	fUnused1c = 0;
	fPieceTypes = TTypeList::Make();
	fParameters = nil;
	NamePtr((char*) this, 0xd4446f6d);		// the block named 'Dom' with the top bit set
}


// ROM 0x0020a894 VUnitInClass__7TDomainSFUlT1
// Whether a unit type belongs to a class of types: the word class ('WORD')
// takes in the three word recognisers' types.
ULong
TDomain::VUnitInClass(ULong type, ULong classType)
{
	if (classType == kWordUnit)
		return type == 'WRXR' || type == 'JANK' || type == 'WREC';
	return 0;
}


// ROM 0x0020a924 Dispose__7TDomainFv
void
TDomain::Dispose(void)
{
	fPieceTypes->Dispose();
	delete this;
}


// ROM 0x0020a694 Dump__7TDomainFP4TMsg
// NOT YET RECONSTRUCTED: TMsg.
void
TDomain::Dump(TMsg* /*msg*/)
{ }


// ROM 0x0020a788 DumpName__7TDomainFP4TMsg
void
TDomain::DumpName(TMsg* /*msg*/)
{ }


// ROM 0x0020a954 SizeInBytes__7TDomainFv
long
TDomain::SizeInBytes(void)
{
	return TRecObject::SizeInBytes() + fPieceTypes->SizeInBytes();
}


// ROM 0x0020a9b0 Classify__7TDomainFP5TUnit
void
TDomain::Classify(TUnit* /*unit*/)
{ }


// ROM 0x0020a9b4 Reclassify__7TDomainFP5TUnit
void
TDomain::Reclassify(TUnit* /*unit*/)
{ }


// ROM 0x0020a9b8 Group__7TDomainFP5TUnitP8dInfoRec
long
TDomain::Group(TUnit* /*unit*/, dInfoRec* /*info*/)
{
	return 0;
}


// ROM 0x0020a674 PreGroup__7TDomainFP5TUnit
long
TDomain::PreGroup(TUnit* /*unit*/)
{
	return 0;
}


// ROM 0x0020a67c PruneDictionary__7TDomainFP5TUnit
long
TDomain::PruneDictionary(TUnit* /*unit*/)
{
	return 0;
}


// ROM 0x0020a684 PruneConstraints__7TDomainFP5TUnit
long
TDomain::PruneConstraints(TUnit* /*unit*/)
{
	return 0;
}


// ROM 0x0020a808 DomainParameter__7TDomainFUlN21
// The base has no parameters: selector 0 (the size of the block) answers 0.
void
TDomain::DomainParameter(ULong selector, ULong result, ULong /*arg*/)
{
	if (selector == 0)
		*(ULong*) result = 0;
}


// ROM 0x0020a7e4 SetParameters__7TDomainFPPc
// ==> whether the parameters changed.
Boolean
TDomain::SetParameters(Handle params)
{
	Boolean changed = fParameters != params;
	if (changed)
		fParameters = params;
	return changed;
}


// ROM 0x0020a7fc InvalParameters__7TDomainFv
void
TDomain::InvalParameters(void)
{
	fParameters = (Handle) -1;
}


// ROM 0x0020a854 ConfigureSubDomain__7TDomainFP8TRecArea
void
TDomain::ConfigureSubDomain(TRecArea* /*area*/)
{ }


// ROM 0x0020a68c CompleteUnit__7TDomainFv
long
TDomain::CompleteUnit(void)
{
	return 0;
}


// ROM 0x0020a988 AddPieceType__7TDomainFUl
void
TDomain::AddPieceType(ULong type)
{
	fPieceTypes->AddUnique(type);
	fPieceTypes->Compact();
}
