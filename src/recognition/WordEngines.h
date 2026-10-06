/*
	File:		recognition/WordEngines.h

	Contains:	The host's own handwriting engines, beside the ROM's two
				word recognisers, and how the writer chooses one.

				The MP2x00 has two word recognisers, Rosetta for printing
				and ParaGraph's for cursive, and the letter set the writer
				picks in the Handwriting Recognition slip says which is in
				use (`ReadCursiveOptions`, Recognizer.h).  The machinery
				that switches between them, `SetWordRecognizer`, works by
				unit type and for any number of recognisers, so a host
				engine is simply a third (or fourth) of them: a
				`TWRecognizer` implementation driven by a `TWRecDomain` of
				its own type, put on the recogniser list asleep by
				`InstallHostWordEngines` the way `InstallWRecRecognizer`
				puts Rosetta there, and put in use when the writer picks it.

				Each engine is registered under the interface name
				`THostWordEngine`, not `TWRecognizer`.  The ROM's word
				domain asks the registry for "any TWRecognizer"
				(`TWRecognizer::New(nil)`), and another implementation of
				that interface would be as likely an answer as Rosetta;
				under a name of their own the host's engines are never
				found by it.

				The choice is the `hostWordEngine` slot of the user
				configuration - the number of the engine's button in the
				slip (8 and up), or no slot for the ROM's own choice.  The
				letter set itself always stays one of the ROM's (0..4):
				ParaGraph's code reads it, sleeping or not, and refuses
				anything above 4 (`AllocLearnInfo`).  An engine is chosen
				with the letter set left at printing, 2, so that the slip's
				example and spacing are drawn as for printing.

				The slip's buttons are the host's (host/HostWordEngines.ns,
				docs/recognition/engines.md): `HostWordEngines()` lists the
				engines this host has installed.

	DEVIATION: all of it.  The ROM has no such engines;
	`ReadCursiveOptions` calls `SetUpHostEngine` after its own two, and
	`TRecognitionManager::InitRecognizers` calls `InstallHostWordEngines`
	after the ROM's word recognisers are installed.
*/

#ifndef __WORDENGINES_H
#define __WORDENGINES_H

#include "Newton.h"
#include "objects.h"

class TRecognitionManager;
class TDomain;
class TWRecognizer;

// the interface name the host's engines are registered under
#define kHostWordEngineInterface	"THostWordEngine"

// the first number the slip's buttons give the host's engines (the ROM's
// letter sets are 0..4; 5..7 are left alone)
const long	kFirstHostWordEngine = 8;

// Set by a host engine's recogniser that has typed the unit it was asked
// to handle (posted its reading at the caret) instead of answering a
// command: HandleUnitList then counts the unit as handled and claims its
// strokes, which an unanswered command would leave to become ink.
extern Boolean	gHostUnitTyped;

// The label an engine puts on a reading that is typing rather than a
// word: the recogniser posts it at the caret as keys (PostKeyString),
// as the keyboard does, instead of sending the view the word.
const ULong	kHostTypedLabel = 'TYPE';

struct SWordEngine
{
	ULong			fType;				// its unit type, the recogniser's id ('UNIS')
	long			fChoice;			// its button's value in the slip
	const char*		fImplementation;	// its protocol implementation's name
	const char*		fTitle;				// its button's text
	const char*		fDomainName;		// its domain's name (what the debugger shows)
	Boolean			fLineAtATime;		// whether it reads a line at a time, as ParaGraph's does
	void			(*fRegister)(void);	// registers the implementation; nil for none
};

// The engines this host was built with: their implementations put in
// the protocol registry, beside RegisterRosettaWRec.
void	RegisterHostWordEngines(void);

// Each registered engine's recogniser put on the list, asleep and
// offering nothing, as InstallWRecRecognizer puts Rosetta's.
void	InstallHostWordEngines(TRecognitionManager* manager);

// The writer's choice put into force, after ReadCursiveOptions has put
// in the letter set's recogniser: the chosen engine is put in use in its
// place.  ==> whether an engine is in use.
Boolean	SetUpHostEngine(void);

// The engine of that unit type, or of that button, if this host has one
// installed.  nil for none.
const SWordEngine*	HostWordEngineByType(ULong type);
const SWordEngine*	HostWordEngineByChoice(long choice);
// whether a unit type is one of the host's engines' (installed or not)
Boolean				IsHostWordEngineType(ULong type);
// the domain of the host engine in use, nil when none is
TDomain*			HostWordEngineDomainInUse(void);

// The host's engine `implementation`, made out of the registry by name
// under kHostWordEngineInterface.  nil when there is none.
TWRecognizer*		NewHostWordEngine(const char* implementation);

// HostWordEngines(): the installed engines, each {text, value, type},
// in the order of their buttons.
Ref		FHostWordEngines(RefArg rcvr);

// the user configuration's slot holding the choice
Ref		HostWordEngineSlot(void);

#endif	/* __WORDENGINES_H */
