/*
	File:		assist/Heuristics.h

	Contains:	What the Assistant does with a phrase no lexicon knows, and
				the helpers its task scripts call: finding the person,
				place or phone number a sentence is about, and remembering
				the things it was last about.

				A phrase is looked up in the Names file (StringToFrameMapper,
				through the Assistant's `dsQuery` query frame, at most
				fifteen cards) and every card that matches is looked over
				slot by slot (DSTagString): a name, a company, a custom
				field, a group or a title that contains every word of the
				phrase makes the phrase a person, and the card is recorded
				in the parse (AddEntry) under what it matched - `person`,
				`company`, `affiliate`, `custom`, `group` or `title`.

				The histories keep the last three things of each of four
				kinds - who, what, when, where (the Assistant's `who_obj`
				and so on) - newest first.

	Reconstructed from the MP2x00 US ROM (0x0007ebb4-0x000831d8); each
	function cites its origin.
*/

#ifndef __HEURISTICS_H
#define __HEURISTICS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "objects.h"

extern Ref	gWhoHistory;		// ROM 0x0c100b98 gWhoHistory
extern Ref	gWhatHistory;		// ROM 0x0c100b9c gWhatHistory
extern Ref	gWhenHistory;		// ROM 0x0c100ba0 gWhenHistory
extern Ref	gWhereHistory;		// ROM 0x0c100ba4 gWhereHistory
extern Ref	gWhoObj;			// ROM 0x0c100ba8 gWhoObj - @8.who_obj
extern Ref	gWhatObj;			// ROM 0x0c100bac gWhatObj
extern Ref	gWhenObj;			// ROM 0x0c100bb0 gWhenObj
extern Ref	gWhereObj;			// ROM 0x0c100bb4 gWhereObj

// the histories
Ref		InitDSHeuristics(RefArg rcvr, RefArg arg);				// ROM 0x0007fd28 InitDSHeuristics__FRC6RefVarT1
Ref		ShuffleHistory(RefArg history, RefArg item);			// ROM 0x0007ebb4 ShuffleHistory__FRC6RefVarT1 - the item put in front, the oldest dropped
Ref		SelectHistList(RefArg thing);							// ROM 0x0007ec48 SelectHistList__FRC6RefVar - the history of its kind, or nil
Ref		RecordHistory(RefArg thing);							// ROM 0x0007fca8 RecordHistory__FRC6RefVar
Ref		History(RefArg rcvr, RefArg thing);						// ROM 0x0007fd18 History__FRC6RefVarT1
Ref		AddHistory(RefArg rcvr, RefArg thing);					// ROM 0x0007fd20 AddHistory__FRC6RefVarT1

// the task scripts' helpers
Ref		DSConstructSubjectLine(RefArg rcvr, RefArg phrases);	// ROM 0x0007edc0 DSConstructSubjectLine__FRC6RefVarT1 - the meal or meeting word
Ref		DSFindPossibleName(RefArg rcvr, RefArg words, RefArg all);	// ROM 0x0007f15c DSFindPossibleName__FRC6RefVarN21
Ref		DSFindPossibleLocation(RefArg rcvr, RefArg exclude, RefArg words);	// ROM 0x0007f3b0 DSFindPossibleLocation__FRC6RefVarN21
Ref		DSFilterStringsAux(RefArg rcvr, RefArg word, RefArg words);	// ROM 0x0007f678 DSFilterStringsAux__FRC6RefVarN21
Ref		DSFilterStrings(RefArg rcvr, RefArg str, RefArg words);	// ROM 0x0007f818 DSFilterStrings__FRC6RefVarN21
Ref		DSScanForWackyName(RefArg rcvr, RefArg phrases, RefArg strings);	// ROM 0x0007f918 DSScanForWackyName__FRC6RefVarN21
Ref		DSGetMatchedEntries(RefArg rcvr, RefArg kind, RefArg entries);	// ROM 0x0007fae0 DSGetMatchedEntries__FRC6RefVarN21
Ref		SuffixP(RefArg rcvr, RefArg word);						// ROM 0x0007fe74 SuffixP__FRC6RefVarT1
Ref		PrefixP(RefArg rcvr, RefArg word);						// ROM 0x00080060 PrefixP__FRC6RefVarT1
Ref		GuessAddressee(RefArg rcvr, RefArg str);				// ROM 0x00080174 GuessAddressee__FRC6RefVarT1
Ref		DSFindPossiblePhone(RefArg rcvr, RefArg phrases);		// ROM 0x00080710 DSFindPossiblePhone__FRC6RefVarT1
Ref		GetCardFileSoup(void);									// ROM 0x0008105c GetCardFileSoup__Fv

// the Names file
Ref		IASmartCFLookup(RefArg rcvr, RefArg str);				// ROM 0x00081068 IASmartCFLookup__FRC6RefVarT1
Ref		StringToFrameMapper(RefArg rcvr, RefArg str);			// ROM 0x000810bc StringToFrameMapper__FRC6RefVarT1
Ref		DSTagString(RefArg rcvr, RefArg entries, RefArg str, RefArg info);	// ROM 0x000813ec DSTagString__FRC6RefVarN31
Ref		TagStringHelper(RefArg name, RefVar words);				// ROM 0x00081ddc TagStringHelper__FRC6RefVar6RefVar
Ref		DSResolveString(RefArg rcvr, RefArg str, RefArg info);	// ROM 0x000820ec DSResolveString__FRC6RefVarN21
Ref		PhraseFilter(RefArg rcvr, RefArg words);				// ROM 0x000822f4 PhraseFilter__FRC6RefVarT1 - the stop words taken out
Ref		IASmarterCFLookup(RefArg rcvr, RefArg words);			// ROM 0x000824bc IASmarterCFLookup__FRC6RefVarT1

// phone numbers
Ref		GenPhoneTypeList(RefArg rcvr, RefArg phones);			// ROM 0x000828b0 GenPhoneTypeList__FRC6RefVarT1
Ref		PhoneSymToString(RefArg rcvr, RefArg sym);				// ROM 0x00082ad8 PhoneSymToString__FRC6RefVarT1
Ref		PhoneSymToIndex(RefArg rcvr, RefArg str, RefArg phones);	// ROM 0x00082b94 PhoneSymToIndex__FRC6RefVarN21
Ref		PhoneStringToSym(RefArg str);							// ROM 0x00082dd4 PhoneStringToSym__FRC6RefVar
Ref		PhoneStringToValue(RefArg rcvr, RefArg str, RefArg phones);	// ROM 0x00082f20 PhoneStringToValue__FRC6RefVarN21
Ref		PhoneIndexToValue(RefArg rcvr, RefArg index, RefArg phones);	// ROM 0x0008302c PhoneIndexToValue__FRC6RefVarN21

void	RegisterHeuristicsNatives(void);

#endif	/* __HEURISTICS_H */
