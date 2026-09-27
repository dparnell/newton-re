/*
	File:		assist/ParseUtter.h

	Contains:	The Assistant's parse: what happens when the "Please ..."
				slip is asked to do something.

				ParseUtter(sentence) opens the slip with the sentence in it,
				checks the sentence (IAInputErrors: not blank, fewer than
				sixteen words) and has the slip's progress box
				(startIAProgress) run IaAtWork, which hands every run of
				words the phrase generator makes to MatchString and keeps
				what was known: [the meanings of each run found, the runs
				as written, the class each run stands for (GetClasses), the
				people and places found in the Names file].

				Then the task: the first action among the classes whose
				task template makes it the primary act (or, failing that,
				the template whose signature the classes fit best -
				CheezySubsumption, scored by how much of the signature they
				cover); a copy of the template gets the parse and the
				sentence, FillPreconditions sorts the words into the slots
				the template wants, and DriveTaskSlip opens the task's slip.

	Reconstructed from the MP2x00 US ROM (0x000e7048-0x000e82c0 and
	InitDarkStar 0x00089a8c); each function cites its origin.
*/

#ifndef __PARSEUTTER_H
#define __PARSEUTTER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "objects.h"

Ref		InitDarkStar(RefArg rcvr, RefArg arg);					// ROM 0x00089a8c InitDarkStar__FRC6RefVarT1 - the Assistant started (TNotebook::InitToolbox)
Ref		parseUtter(RefArg rcvr, RefArg sentence);				// ROM 0x000e7048 parseUtter__FRC6RefVarT1
Ref		FIaAtWork(RefArg rcvr, RefArg progress);				// ROM 0x000e7a0c FIaAtWork
Ref		IAInputErrors(RefArg rcvr, RefArg assistant);			// ROM 0x000e7f84 IAInputErrors__FRC6RefVarT1 - TRUE when the sentence can be parsed
Ref		RemoveTrailingPunct(RefArg rcvr, RefArg str);			// ROM 0x000e8128 RemoveTrailingPunct__FRC6RefVarT1

void	RegisterParseUtterNatives(void);
// every Assistant native (RegisterAssistantNatives and the lexicon's,
// the heuristics', the phrase generator's and the parse's)
void	RegisterAllAssistantNatives(void);

#endif	/* __PARSEUTTER_H */
