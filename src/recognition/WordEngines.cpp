/*
	File:		recognition/WordEngines.cpp

	Contains:	The host's own handwriting engines - WordEngines.h.
*/

#include "WordEngines.h"
#include "WRecDomain.h"
#include "Recognizer.h"
#include "Words.h"			// gWordID
#include "UnistrokeRecognizer.h"
#include "Protocols.h"
#include "Frames.h"
#include "Locale.h"			// GetPreference, SetPreference
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "Commands.h"		// aeWord
#include "UnitPublic.h"
#include "WordUnit.h"
#include "Keyboard.h"		// GetPostingView, PostKeyString
#include "Unicode.h"

#include <string.h>


// The engines, in the order of their buttons.  An engine whose
// implementation is not registered (or whose domain will not start) is
// not installed, and is then neither listed nor chosen.
// (The neural engine - docs/recognition/engines.md - goes in here as
//  'NNET', button 9, when it exists.)
static const SWordEngine kWordEngines[] =
{
	{ kUnistrokeType, kFirstHostWordEngine, "TUnistrokeRecognizer", "Unistroke (one letter per stroke)",
	  "Unistroke Word-rec", false, RegisterUnistrokeRecognizer },
};
static const long kWordEngineCount = sizeof(kWordEngines) / sizeof(kWordEngines[0]);


void
RegisterHostWordEngines(void)
{
	for (long i = 0; i < kWordEngineCount; i++)
		if (kWordEngines[i].fRegister != nil)
			kWordEngines[i].fRegister();
}


TWRecognizer*
NewHostWordEngine(const char* implementation)
{
	if (gProtocolRegistry == nil)
		return nil;
	TWRecognizer* p = (TWRecognizer*) AllocInstanceByName(kHostWordEngineInterface, implementation);
	return p != nil ? (TWRecognizer*) p->GlueNew() : nil;
}


Boolean gHostUnitTyped = false;


// The word recogniser over a host engine.  It is TWRecRecognizer but for
// what becomes of a unit whose reading is typing (kHostTypedLabel - the
// unistroke engine's characters): the reading goes to the caret as keys,
// as the keyboard sends them (PostKeyString), so that a character appears
// as soon as it is written, a space is a space and backspace and return
// do what those keys do; and nothing is sent to the view under the
// writing.  With no caret to type at, a character is put down as a word
// is - which makes the paragraph the next ones are typed into - and a
// space, backspace or return does nothing.
class THostWRecRecognizer : public TWRecRecognizer
{
public:
	virtual ULong		HandleUnit(TUnitPublic* unit);
};


ULong
THostWRecRecognizer::HandleUnit(TUnitPublic* pub)
{
	TStdWordUnit* unit = (TStdWordUnit*) pub->fUnit;
	if (!pub->IsTap() && unit->InterpretationCount() > 0
	 && (ULong) unit->GetLabel(0) == kHostTypedLabel
	 && UnitConfidence(pub) != kWRecInk)
	{
		Handle h = unit->GetString(0);
		UniChar ch = (h != nil && *h != nil) ? *(UniChar*) *h : 0;
		TView* view = GetPostingView(false);
		if (view != nil)
		{
			if (ch != 0)
			{
				UniChar text[2];
				text[0] = ch;
				text[1] = 0;
				PostKeyString(view, RefVar(MakeString(text)));
			}
			gHostUnitTyped = true;
			return 0;
		}
		// nowhere to type a space, backspace, return or shift: the stroke
		// is used up all the same
		if (ch == 0 || ch == ' ' || ch == 0x08 || ch == 0x0D)
		{
			gHostUnitTyped = true;
			return 0;
		}
	}
	return WordRecognizerHandleUnit(this, pub);
}


// Each one as InstallWRecRecognizer installs Rosetta's: the word command,
// the "reads writing" flag, an arbitration time of one tick, the word
// services possible and none of them offered, and asleep.
void
InstallHostWordEngines(TRecognitionManager* manager)
{
	for (long i = 0; i < kWordEngineCount; i++)
	{
		const SWordEngine* engine = &kWordEngines[i];
		if (ClassInfoByName(kHostWordEngineInterface, engine->fImplementation, 0) == nil)
			continue;
		if (manager->fRecognizers->FindRecognizer(engine->fType) != nil)
			continue;
		TDomain* domain = TWRecDomain::MakeHostEngine(manager->fController, engine->fType,
													  engine->fImplementation, engine->fDomainName);
		if (domain == nil)
			continue;
		TWRecRecognizer* recognizer = new THostWRecRecognizer;
		recognizer->Init(domain, domain->fType, aeWord, kRecognizerIsWriting, 1);
		recognizer->InitServices(kWRecServices, 0);
		manager->fRecognizers->AddRecognizer(recognizer);
		recognizer->Sleep();
	}
}


static Boolean
IsInstalled(const SWordEngine* engine)
{
	return gRecognition.fRecognizers != nil
		&& gRecognition.fRecognizers->FindRecognizer(engine->fType) != nil;
}


const SWordEngine*
HostWordEngineByType(ULong type)
{
	for (long i = 0; i < kWordEngineCount; i++)
		if (kWordEngines[i].fType == type)
			return IsInstalled(&kWordEngines[i]) ? &kWordEngines[i] : nil;
	return nil;
}


const SWordEngine*
HostWordEngineByChoice(long choice)
{
	for (long i = 0; i < kWordEngineCount; i++)
		if (kWordEngines[i].fChoice == choice)
			return IsInstalled(&kWordEngines[i]) ? &kWordEngines[i] : nil;
	return nil;
}


Boolean
IsHostWordEngineType(ULong type)
{
	for (long i = 0; i < kWordEngineCount; i++)
		if (kWordEngines[i].fType == type)
			return true;
	return false;
}


TDomain*
HostWordEngineDomainInUse(void)
{
	if (gWordID == 0 || HostWordEngineByType(gWordID) == nil)
		return nil;
	TRecognizer* recognizer = gRecognition.fRecognizers->FindRecognizer(gWordID);
	return recognizer != nil ? recognizer->Domain() : nil;
}


Ref
HostWordEngineSlot(void)
{
	return Intern((char*) "hostWordEngine");
}


// The writer's choice, read where ReadCursiveOptions reads the letter
// set: an engine this host has is put in use as UseWRec would put it,
// and the preferences the ROM's set-up functions keep say so - which
// recogniser is current, and whether writing is read a line at a time.
// A choice of an engine this host does not have leaves the letter set's
// recogniser in use.
Boolean
SetUpHostEngine(void)
{
	RefVar choice(GetPreference(RefVar(HostWordEngineSlot())));
	if (!ISINT(choice))
		return false;
	const SWordEngine* engine = HostWordEngineByChoice(RINT(choice));
	if (engine == nil)
		return false;
	if (gWordID != engine->fType && !SetWordRecognizer(engine->fType))
		return false;
	char name[5];
	for (int i = 0; i < 4; i++)
		name[i] = (char) (engine->fType >> (24 - 8 * i));
	name[4] = 0;
	SetPreference(RSSYMcurrentwordrecognizer, RefVar(MakeString(name)));
	SetPreference(RSSYMlineatatime, engine->fLineAtATime ? RefVar(TRUEREF) : RefVar());
	return true;
}


Ref
FHostWordEngines(RefArg /*rcvr*/)
{
	RefVar list(MakeArray(0));
	for (long i = 0; i < kWordEngineCount; i++)
	{
		const SWordEngine* engine = &kWordEngines[i];
		if (!IsInstalled(engine))
			continue;
		char type[5];
		for (int j = 0; j < 4; j++)
			type[j] = (char) (engine->fType >> (24 - 8 * j));
		type[4] = 0;
		RefVar item(AllocateFrame());
		SetFrameSlot(item, RefVar(Intern((char*) "text")), RefVar(MakeString(engine->fTitle)));
		SetFrameSlot(item, RefVar(Intern((char*) "value")), RefVar(MAKEINT(engine->fChoice)));
		SetFrameSlot(item, RefVar(Intern((char*) "type")), RefVar(MakeString(type)));
		AddArraySlot(list, item);
	}
	return list;
}
