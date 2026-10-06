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
		TWRecRecognizer* recognizer = new TWRecRecognizer;
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
