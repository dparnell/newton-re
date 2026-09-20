/*
	File:		recognition/Domain.cpp

	Contains:	TDomain, the base of the recognisers' domains.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Domain.h"
#include "Areas.h"
#include "Controller.h"
#include "Stroke.h"

TDomain*	gRootDomain = nil;			// ROM 0x0c101884 gRootDomain


// ROM 0x0020cf88 Make__7TDomainSFP11TControllerUlPc
TDomain*
TDomain::Make(TController* controller, ULong type, char* name)
{
	TDomain* domain = new TDomain;
	if (domain != nil)
		domain->IDomain(controller, type, name);
	return domain;
}


// ROM 0x0020d008 IDomain__7TDomainFP11TControllerUlPc
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
	fLevel = 0;
	fPieceTypes = TTypeList::Make();
	fParameters = nil;
	NamePtr((char*) this, 0xd4446f6d);		// the block named 'Dom' with the top bit set
}


// ROM 0x0020cfc4 VUnitInClass__7TDomainSFUlT1
// Whether a unit type belongs to a class of types: the word class ('WORD')
// takes in the three word recognisers' types.
ULong
TDomain::VUnitInClass(ULong type, ULong classType)
{
	if (classType == kWordUnit)
		return type == 'WRXR' || type == 'JANK' || type == 'WREC';
	return 0;
}


// ROM 0x0020d054 Dispose__7TDomainFv
void
TDomain::Dispose(void)
{
	fPieceTypes->Dispose();
	delete this;
}


// ROM 0x0020cdc4 Dump__7TDomainFP4TMsg
// NOT YET RECONSTRUCTED: TMsg.
void
TDomain::Dump(TMsg* /*msg*/)
{ }


// ROM 0x0020ceb8 DumpName__7TDomainFP4TMsg
void
TDomain::DumpName(TMsg* /*msg*/)
{ }


// ROM 0x0020d084 SizeInBytes__7TDomainFv
long
TDomain::SizeInBytes(void)
{
	return TRecObject::SizeInBytes() + fPieceTypes->SizeInBytes();
}


// ROM 0x0020d0e0 Classify__7TDomainFP5TUnit
void
TDomain::Classify(TUnit* /*unit*/)
{ }


// ROM 0x0020d0e4 Reclassify__7TDomainFP5TUnit
void
TDomain::Reclassify(TUnit* /*unit*/)
{ }


// ROM 0x0020d0e8 Group__7TDomainFP5TUnitP8dInfoRec
long
TDomain::Group(TUnit* /*unit*/, dInfoRec* /*info*/)
{
	return 0;
}


// ROM 0x0020cda4 PreGroup__7TDomainFP5TUnit
long
TDomain::PreGroup(TUnit* /*unit*/)
{
	return 0;
}


// ROM 0x0020cdac PruneDictionary__7TDomainFP5TUnit
long
TDomain::PruneDictionary(TUnit* /*unit*/)
{
	return 0;
}


// ROM 0x0020cdb4 PruneConstraints__7TDomainFP5TUnit
long
TDomain::PruneConstraints(TUnit* /*unit*/)
{
	return 0;
}


// ROM 0x0020cf38 DomainParameter__7TDomainFUlN21
// The base has no parameters: selector 0 (the size of the block) answers 0.
void
TDomain::DomainParameter(ULong selector, ULong result, ULong /*arg*/)
{
	if (selector == 0)
		*(ULong*) result = 0;
}


// ROM 0x0020cf14 SetParameters__7TDomainFPPc
// ==> whether the parameters changed.
Boolean
TDomain::SetParameters(Handle params)
{
	Boolean changed = fParameters != params;
	if (changed)
		fParameters = params;
	return changed;
}


// ROM 0x0020cf2c InvalParameters__7TDomainFv
void
TDomain::InvalParameters(void)
{
	fParameters = (Handle) -1;
}


// ROM 0x0020cf84 ConfigureSubDomain__7TDomainFP8TRecArea
void
TDomain::ConfigureSubDomain(TRecArea* /*area*/)
{ }


// ROM 0x0020cdbc CompleteUnit__7TDomainFv
long
TDomain::CompleteUnit(void)
{
	return 0;
}


// ROM 0x0020d0b8 AddPieceType__7TDomainFUl
void
TDomain::AddPieceType(ULong type)
{
	fPieceTypes->AddUnique(type);
	fPieceTypes->Compact();
}


#pragma mark - TStrokeDomain

TStrokeDomain*	gStrokeDomain = nil;		// ROM 0x0c101680 gStrokeDomain


// ROM 0x00220e94 Make__13TStrokeDomainSFP11TController
TStrokeDomain*
TStrokeDomain::Make(TController* controller)
{
	TStrokeDomain* domain = new TStrokeDomain;
	if (domain != nil)
		domain->IStrokeDomain(controller);
	return domain;
}


// ROM 0x00220edc IStrokeDomain__13TStrokeDomainFP11TController
// 'STRK', made out of 'CLIK' pieces, and registered with the controller
// (the ROM puts it on the controller's list here rather than through
// RegisterDomain, having just set fController itself).
void
TStrokeDomain::IStrokeDomain(TController* controller)
{
	IDomain(controller, kStrokeUnit, (char*) "Stroke");
	AddPieceType(kClickUnit);
	fController = controller;
	*(TDomain**) controller->fDomains->AddEntry() = this;
	controller->fDomains->Compact();
}


// ROM 0x0022105c Dispose__13TStrokeDomainFv
void
TStrokeDomain::Dispose(void)
{
	fPieceTypes->Dispose();
	delete this;
}


// ROM 0x00221dc0 Group__13TStrokeDomainFP5TUnitP8dInfoRec
// A click offered to the stroke domain.  Its box and duration are brought
// up to the stroke's as it is being written, and answered 0 - the entry
// stays on the group queue and is offered again next time round.  Once
// the stroke is finished the click's duration is taken from the pen-up
// time, the inker's hold on the stroke is let go, and a 'STRK' unit is
// made with the click as its only sub: that is the piece the rest of the
// recogniser works on.
long
TStrokeDomain::Group(TUnit* unit, dInfoRec* /*info*/)
{
	TStroke* stroke = ((TClickUnit*) unit)->fStroke;
	unit->SetBBox(&stroke->fBBox);
	unit->fDuration = GetTicks() - unit->fStartTime;
	if (!stroke->Done())
		return 0;

	unit->fDuration = stroke->fUpTime - unit->fStartTime;
	stroke->UnsetFlags(kBufferedStroke);
	TAreaList* areas = unit->GetAreas();
	TStrokeUnit* strokeUnit = TStrokeUnit::Make(this, 2, stroke, areas);
	if (areas != nil)
		areas->Dispose();
	if (strokeUnit == nil)
		return 0;

	stroke->Clone();				// the stroke unit holds it too
	strokeUnit->AddSub(unit);
	strokeUnit->EndSubs();
	fController->NewGroup(strokeUnit);
	UnbufferStroke(stroke);
	return 1;
}


// ROM 0x00221ec0 Classify__13TStrokeDomainFP5TUnit
// A stroke unit is a piece for everything above it.
void
TStrokeDomain::Classify(TUnit* unit)
{
	fController->NewClassification(unit);
}
