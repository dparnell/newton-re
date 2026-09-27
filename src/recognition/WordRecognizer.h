/*
	File:		recognition/WordRecognizer.h

	Contains:	The cursive word recogniser, and the letter sets and
				letter weights a script sees of it.

				`InstallWordRecognizer` makes the two cursive domains
				(`XrDomains.h`) and a `TWordRecognizer` over the word one,
				'XRWR', and puts it on the recogniser list - woken and put
				straight back to sleep, which runs the writer's saved
				letter weights through the machine's NewtonScript
				(`loadLetterWeights`, `saveLetterWeights`).  Which of the
				two word recognisers is then in use is the writer's letter
				set: `ReadCursiveOptions` calls `SetUpRosetta` and
				`SetUpParaGraph` (`Recognizer.h`), and set 2 - printed -
				puts Rosetta in use, any other ParaGraph.

				The letter sets (`userConfiguration.letterSetSelection`,
				`gLetterSetSelection`) are the Handwriting Style slip's
				choices, and each has its own learning info - the weights
				of every way of writing every letter (`ParaGraph.h`).  A
				script reaches them through `GetLetterWeights` (an array
				of five 'letterWeights binaries, nil for a set still at its
				defaults) and `SetLetterWeights`, `ResetLetterDefaults`
				and `UseTrainingDataForRecognition`; the MessagePad 100's
				layout is converted with `ConvertFromMP`/`ConvertForMP`.
				Every one of them builds a whole parameter block for the
				word domain to ask it (`LIBeginWeights`, in
				`LetterShapes.h`) and throws it away again.

				NOT YET RECONSTRUCTED: the reading engine, so a cursive
				letter set chosen on the host reads nothing; the parts of
				`ConfigureArea` that set the engine's base and grid lines
				(`FromObject` for a WordBaseInfo and a RecGridInfo,
				`GetWordGeom`, `GetGridGeom`); and `GetTraceFromStrokes`,
				without which `DoLearning` has nothing to learn from.

	Reconstructed from the MP2x00 US ROM (0x00166efc-0x001686ec); each
	function cites its origin.
*/

#ifndef __WORDRECOGNIZER_H
#define __WORDRECOGNIZER_H

#ifndef __RECOGNIZER_H
#include "Recognizer.h"
#endif
#ifndef __XRDOMAINS_H
#include "XrDomains.h"
#endif

class TDictChain;

// The cursive recogniser: TRecognizer over the 'XRWR' domain, with the
// 'STXR' one beside it.
class TWordRecognizer : public TRecognizer
{
public:
	virtual long		UnitConfidence(TUnitPublic* unit);		// ROM 0x00168314 UnitConfidence__15TWordRecognizerFP11TUnitPublic - 0
	virtual void		Sleep(void);							// ROM 0x001683f8 Sleep__15TWordRecognizerFv
	virtual void		WakeUp(void);							// ROM 0x00168430 WakeUp__15TWordRecognizerFv
	virtual void		BuildConfig(RefArg config, TView* view, ULong flags);	// ROM 0x0016700c BuildConfig__15TWordRecognizerFRC6RefVarP5TViewUl - nothing
	virtual long		ConfigureArea(TRecArea* area, RefArg config);	// ROM 0x00168000 ConfigureArea__15TWordRecognizerFP8TRecAreaRC6RefVar
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x001677b0 HandleUnit__15TWordRecognizerFP11TUnitPublic
	virtual Ref			GetLearningData(TUnitPublic* unit);		// ROM 0x00168464 GetLearningData__15TWordRecognizerFP11TUnitPublic
	virtual void		DoLearning(RefArg data, long which);	// ROM 0x00168528 DoLearning__15TWordRecognizerFRC6RefVarl

	// The recogniser's own field flags out of a configuration's input
	// mask: 1 words, 2 numbers, 4 capitals, 8 punctuation, 0x10 phone.
	ULong				FieldType(RefArg config);				// ROM 0x00167844 FieldType__15TWordRecognizerFRC6RefVar

	TDomain*			fStrXrDomain;	// +0x20
	ULong				fFieldType;		// +0x24  FieldType's last answer
};

void	InstallWordRecognizer(TRecognitionManager* manager);	// ROM 0x00166efc InstallWordRecognizer__FP19TRecognitionManager

// The word domain set up the way an area's configuration asks: its
// speed and field type, the single-letter/learning/orthographic switches
// the writer's preferences say, the letter set's language.
void	SetupXRD(TDomain* domain, Handle info, ULong speed, ULong type);	// ROM 0x001675d0 SetupXRD__FP7TDomainPPcUlT3
void	SetupChains(TDomain* domain, Handle info, TDictChain** chains);	// ROM 0x0016706c SetupChains__FP7TDomainPPcPP10TDictChain
// Each of the configuration's `commands` (an array of integers) handed
// to the domain as a SetXrWordRC word.
void	DoParaCommands(TDomain* domain, Handle info, RefArg config, RefArg commands);	// ROM 0x00167508 DoParaCommands__FP7TDomainPPcRC6RefVarT3

// The letter set: `userConfiguration.letterSetSelection` into
// gLetterSetSelection, and back.
ULong	GetLetterSet(void);										// ROM 0x001677b4 GetLetterSet__Fv
void	SetLetterSet(ULong letterSet);							// ROM 0x001677ec SetLetterSet__FUl
// An engine handle's data and size, past the handle word.
Ptr		GetParaPtr(Handle h);									// ROM 0x00167830 GetParaPtr__FPPc
ULong	AdjustParaSize(ULong size);								// ROM 0x0016783c AdjustParaSize__FUl
// The current letter set's learning info, and how big it is.
Ptr		LetterWeightDataPtr(void);								// ROM 0x001678cc LetterWeightDataPtr__Fv
ULong	SizeOfLetterWeightData(void);							// ROM 0x00167964 SizeOfLetterWeightData__Fv
// The orthographic learning's database, and how big it is (0 unless
// the writer turned big learning on).
Ptr		LearningDataPtr(void);									// ROM 0x00167dc8 LearningDataPtr__Fv
ULong	SizeOfLearningData(void);								// ROM 0x00167e40 SizeOfLearningData__Fv
// A letter-weights binary converted between the MessagePad 100's layout
// (a word in front) and this machine's.  ==> the new binary, or nil.
Ref		ConvertLetterWeights(RefArg weights, long how, long direction);	// ROM 0x0016810c ConvertLetterWeights__FRC6RefVarlT2
// Whether every character of the word has a way of being written.
Boolean	VerifyWordSymbols(UniChar* word);						// ROM 0x0016836c VerifyWordSymbols__FPUs

extern Boolean	gUSE_GROUP_AND_CLASSIFY;						// ROM 0x0c104d40 gUSE_GROUP_AND_CLASSIFY - the lineAtATime preference: read a line of writing at a time

Ref		UseTrainingDataForRecognition(RefArg rcvr, RefArg on);	// ROM 0x001679fc UseTrainingDataForRecognition
Ref		FSetLetterWeights(RefArg rcvr, RefArg weights);			// ROM 0x00167a70 FSetLetterWeights__FRC6RefVarT1
Ref		FGetLetterWeights(RefArg rcvr);							// ROM 0x00167c28 FGetLetterWeights__FRC6RefVar
Ref		FResetLearningDefaults(RefArg rcvr);					// ROM 0x00167d2c FResetLearningDefaults
Ref		FSetLearningData(RefArg rcvr, RefArg data);				// ROM 0x00167eb8 FSetLearningData__FRC6RefVarT1
Ref		FGetLearningData(RefArg rcvr);							// ROM 0x00167f24 FGetLearningData__FRC6RefVar
Ref		FConvertFromMP(RefArg rcvr, RefArg weights, RefArg how);	// ROM 0x0016804c FConvertFromMP__FRC6RefVarN21
Ref		FConvertForMP(RefArg rcvr, RefArg weights, RefArg how);	// ROM 0x001680ac FConvertForMP__FRC6RefVarN21
Ref		FDoCursiveTraining(RefArg rcvr, RefArg data, RefArg which);	// ROM 0x001a0d14 FDoCursiveTraining__FRC6RefVarN21
Ref		FRosettaExtension(RefArg rcvr, RefArg a, RefArg b);		// ROM 0x001b6c3c FRosettaExtension - nil: a hook left empty

void	RegisterWordRecognizerNatives(void);

#endif	/* __WORDRECOGNIZER_H */
