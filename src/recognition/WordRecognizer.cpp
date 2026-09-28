/*
	File:		recognition/WordRecognizer.cpp

	Contains:	The cursive word recogniser and its letter sets and
				weights.  See WordRecognizer.h.
*/

#include "WordRecognizer.h"
#include "LetterShapes.h"		// LIBeginWeights, LIEndWeights, LIInit
#include "Controller.h"
#include "Areas.h"
#include "Dictionaries.h"		// BuildChains, TDictChain
#include "UnitPublic.h"
#include "WordUnit.h"			// TStdWordUnit
#include "StrokeBundle.h"		// StrokeBundleToTStrokes
#include "Stroke.h"				// DisposeTStrokes
#include "Words.h"				// gWordID, gEnabledLanguage, gSaveWordTrainingData
#include "Locale.h"				// GetPreference, SetPreference
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"		// NSCallGlobalFn
#include "RSSymbols.h"
#include "NativeFunctions.h"
#include "NewtonGestalt.h"
#include "KernelGlobals.h"		// gCurrentTask
#include "Unicode.h"
#include "InkGroups.h"			// GetTraceFromStrokes
#include <string.h>

// ROM 0x0c104d40 gUSE_GROUP_AND_CLASSIFY
Boolean	gUSE_GROUP_AND_CLASSIFY = true;

static TDomain*
XrWordDomain(void)
{
	return gController->GetTypedDomain(kXrWordDomainType);
}


/*------------------------------------------------------------------------------
	T h e   r e c o g n i s e r
------------------------------------------------------------------------------*/

// ROM 0x00166efc InstallWordRecognizer__FP19TRecognitionManager
// The two cursive domains made and the recogniser put on the list over
// the word one, asleep; the letter images initialised, and the Gestalt
// that says the machine has a cursive recogniser registered.
void
InstallWordRecognizer(TRecognitionManager* manager)
{
	TStrXrDomain* strXr = TStrXrDomain::Make(manager->fController);
	TXrWordDomain* xrWord = TXrWordDomain::Make(manager->fController);
	TWordRecognizer* recognizer = new TWordRecognizer;
	recognizer->Init(xrWord, xrWord->fType, 0x12, 1, 1);
	recognizer->InitServices(kWRecServices, 0);
	recognizer->fStrXrDomain = strXr;
	manager->fRecognizers->AddRecognizer(recognizer);
	LIInit();
	recognizer->WakeUp();
	recognizer->Sleep();
	// DEVIATION: a host program that runs without the kernel (newtonscript)
	// has no name server to register the Gestalt with, and skips it.
	if (gCurrentTask == nil)
		return;
	TUGestalt gestalt;
	UByte* present = new UByte[4];
	if (present != nil)
	{
		present[0] = 1;
		gestalt.RegisterGestalt(0x02000008, present, 4);
	}
}


// ROM 0x00168314 UnitConfidence__15TWordRecognizerFP11TUnitPublic
long
TWordRecognizer::UnitConfidence(TUnitPublic* /*unit*/)
{
	return 0;
}


// ROM 0x001683f8 Sleep__15TWordRecognizerFv
// The weights written to the System soup as letter set nought's -
// `saveLetterWeights` saves them unless the set is 2, the printed one -
// and the learning infos let go; the letter set put back as it was.
void
TWordRecognizer::Sleep(void)
{
	ULong letterSet = GetLetterSet();
	SetLetterSet(0);
	NSCallGlobalFn(RSSYMsaveletterweights);
	PGFreeAllLearningData();
	gLetterSetSelection = letterSet;
	SetPreference(RSSYMlettersetselection, RefVar(MAKEINT(letterSet)));
}


// ROM 0x00168430 WakeUp__15TWordRecognizerFv
// The weights read back from the System soup (`loadLetterWeights`).
void
TWordRecognizer::WakeUp(void)
{
	ULong letterSet = GetLetterSet();
	SetLetterSet(0);
	NSCallGlobalFn(RSSYMloadletterweights);
	gLetterSetSelection = letterSet;
	SetPreference(RSSYMlettersetselection, RefVar(MAKEINT(letterSet)));
}


// ROM 0x0016700c BuildConfig__15TWordRecognizerFRC6RefVarP5TViewUl
void
TWordRecognizer::BuildConfig(RefArg /*config*/, TView* /*view*/, ULong /*flags*/)
{ }


// ROM 0x001677b0 HandleUnit__15TWordRecognizerFP11TUnitPublic
ULong
TWordRecognizer::HandleUnit(TUnitPublic* unit)
{
	return WordRecognizerHandleUnit(this, unit);
}


// ROM 0x00167844 FieldType__15TWordRecognizerFRC6RefVar
ULong
TWordRecognizer::FieldType(RefArg config)
{
	ULong mask = RINT(RefVar(GetVariable(config, RSSYMinputmask, nil, 0)));
	ULong type = (mask & 0x1681000) != 0;
	if ((mask & 0x13c2000) != 0)
		type |= 2;
	if ((mask & 0x4000) != 0)
		type |= 4;
	if ((mask & 0x8000) != 0)
		type |= 8;
	if ((mask & 0x20000) != 0)
		type |= 0x10;
	fFieldType = type;
	return type;
}


// ROM 0x00168000 ConfigureArea__15TWordRecognizerFP8TRecAreaRC6RefVar
// The area's parameter blocks for both domains filled in from its
// configuration: the strokes-to-xrs one's field type, letter spacing and
// language and its own commands, then the word one's speed, switches,
// dictionary chains and commands.  (lineAtATime is read first, whether
// or not the area runs the domain.)
// NOT YET RECONSTRUCTED: a configuration's `rcBaseInfo` and `rcGridInfo`
// (the lines the writing stands on, and a grid of boxes) turned into the
// engine's WORD_GEOM and WORD_BASELINE (FromObject, GetWordGeom
// 0x001686ec, GetGridGeom 0x00167010) - they are not handed on.
long
TWordRecognizer::ConfigureArea(TRecArea* area, RefArg config)
{
	gUSE_GROUP_AND_CLASSIFY = NOTNIL(RefVar(GetPreference(RSSYMlineatatime)));
	if (!DomainOn(area, ID()))
		return 0;
	ULong mask = RINT(RefVar(GetVariable(config, RSSYMinputmask, nil, 0)));
	ULong type = FieldType(config);
	TDomain* strXr = fStrXrDomain;
	Handle strXrInfo = area->GetInfoFor(kStrXrDomainType, true);
	// (NOT YET: rcBaseInfo -> 0x2000b, rcGridInfo -> 0x2000e)
	if (NOTNIL(RefVar(GetProtoVariable(config, RSSYMrcsingleletters, nil))))
		type = (type & ~8) | 0x20;
	strXr->DomainParameter(0x20006, type, (ULong) strXrInfo);
	RefVar spacing(GetVariable(config, RSSYMletterspacecursiveoption, nil, 0));
	if (ISNIL(spacing) || (mask & 0x100) != 0)
		strXr->DomainParameter(0x20027, 0, (ULong) strXrInfo);
	else
	{
		strXr->DomainParameter(0x20028, 0, (ULong) strXrInfo);
		strXr->DomainParameter(0x20025, 9 - RINT(spacing), (ULong) strXrInfo);
	}
	strXr->DomainParameter(0x20046, gEnabledLanguage, (ULong) strXrInfo);
	GetVariable(config, RSSYMpar_separatelettersflag, nil, 0);
	DoParaCommands(strXr, strXrInfo, config, RSSYMstrxrcommands);
	area->ParamsAllSet(kStrXrDomainType);

	TDomain* xrWord = Domain();
	Handle info = area->GetInfoFor(kXrWordDomainType, true);
	ULong speed = RINT(RefVar(GetVariable(config, RSSYMspeedcursiveoption, nil, 0)));
	SetupXRD(xrWord, info, speed, type);
	TDictChain* chains[kAreaDictChains];
	BuildChains(chains, config);
	SetupChains(xrWord, info, chains);
	for (long i = 0; i < kAreaDictChains; i++)
		area->fDictionaries[i] = chains[i];
	DoParaCommands(xrWord, info, config, RSSYMxrwcommands);
	area->ParamsAllSet(kXrWordDomainType);
	return 0;
}


// ROM 0x00168464 GetLearningData__15TWordRecognizerFP11TUnitPublic
// The unit's training data as a 'learningData binary.
Ref
TWordRecognizer::GetLearningData(TUnitPublic* unit)
{
	RefVar data;
	TStdWordUnit* word = (TStdWordUnit*) unit->fUnit;
	Handle training = word->GetTrainingData(0);
	if (training != nil)
	{
		Size size = GetHandleSize(training);
		data = AllocateBinary(RSSYMlearningdata, size);
		BlockMove(*training, BinaryData(data), size);
		word->DisposeTrainingData(training);
	}
	return data;
}


// ROM 0x00168528 DoLearning__15TWordRecognizerFRC6RefVarl
// What the writer settled on - a word info frame's unit data and its
// strokes - learnt by the word domain (selector 0x20010), for the
// first five readings only - with the strokes as the reading engine's
// trace (GetTraceFromStrokes) and how many points it has.
void
TWordRecognizer::DoLearning(RefArg data, long which)
{
	if (ISNIL(data) || which >= 5)
		return;
	RefVar unitData(GetFrameSlotRef(data, RSSYMunitdata));
	RefVar strokes(GetFrameSlotRef(data, RSSYMstrokes));
	TStroke** tstrokes;
	if (NOTNIL(strokes) && NOTNIL(unitData) && (tstrokes = StrokeBundleToTStrokes(strokes)) != nil)
	{
		PS_point_type* trace = nil;
		short nStrokes = 0;
		short count = 0;
		GetTraceFromStrokes(tstrokes, &trace, &nStrokes, &count);
		if (trace != nil)
		{
			long size = Length(unitData);
			Handle h = NewHandle(size);
			if (h != nil)
			{
				BlockMove(BinaryData(unitData), *h, size);
				XrLearningRecord record = { h, trace, count, which };
				Handle info = LIBeginWeights();
				if (info != nil)
				{
					TDomain* domain = XrWordDomain();
					SetupXRD(domain, info, 0, 0);
					domain->DomainParameter(0x20010, (ULong) &record, (ULong) info);
					LIEndWeights(info);
				}
				DisposHandle(h);
			}
			HWRMemoryFree((Ptr) trace);
		}
		DisposeTStrokes(tstrokes);
	}
}


/*------------------------------------------------------------------------------
	S e t t i n g   u p   t h e   w o r d   d o m a i n
------------------------------------------------------------------------------*/

// ROM 0x001675d0 SetupXRD__FP7TDomainPPcUlT3
void
SetupXRD(TDomain* domain, Handle info, ULong speed, ULong type)
{
	domain->DomainParameter(0x20008, speed, (ULong) info);
	domain->DomainParameter(0x20006, type, (ULong) info);
	domain->DomainParameter(0x20019, 0, (ULong) info);
	domain->DomainParameter(gUseBigTrainingData ? 0x20035 : 0x20036, 0, (ULong) info);
	domain->DomainParameter((type & 1) ? 0x20044 : 0x20043, (type & 4) != 0, (ULong) info);
	Boolean noLearning = gLetterSetSelection == 4;
	domain->DomainParameter((gSaveWordTrainingData && !noLearning) ? 0x20033 : 0x20034, 0, (ULong) info);
	domain->DomainParameter((gSaveWordTrainingData && !noLearning && gUseBigTrainingData) ? 0x20037 : 0x20038, 0, (ULong) info);
	domain->DomainParameter(0x20046, gEnabledLanguage, (ULong) info);
}


// ROM 0x0016706c SetupChains__FP7TDomainPPcPP10TDictChain
// The first two chains (each only when it holds a dictionary) and the
// third, with nothing else, handed to the domain.
void
SetupChains(TDomain* domain, Handle info, TDictChain** chains)
{
	void* pair[2];
	pair[0] = (chains[0] != nil && chains[0]->GetEntry(0) != nil) ? chains[0] : nil;
	pair[1] = (chains[1] != nil && chains[1]->GetEntry(0) != nil) ? chains[1] : nil;
	domain->DomainParameter(0x20002, (ULong) pair, (ULong) info);
	XrChainInfo third;
	third.fChain = (chains[2] != nil && chains[2]->GetEntry(0) != nil) ? chains[2] : nil;
	third.f04 = 0;
	third.f08 = 0;
	domain->DomainParameter(0x20012, (ULong) &third, (ULong) info);
}


// ROM 0x00167508 DoParaCommands__FP7TDomainPPcRC6RefVarT3
void
DoParaCommands(TDomain* domain, Handle info, RefArg config, RefArg commands)
{
	RefVar command;
	RefVar list(GetVariable(config, commands, nil, 0));
	if (NOTNIL(list))
	{
		long count = Length(list);
		for (long i = 0; i < count; i++)
		{
			command = GetArraySlotRef(list, i);
			domain->DomainParameter(0x20032, RINT(command), (ULong) info);
		}
	}
}


/*------------------------------------------------------------------------------
	T h e   l e t t e r   s e t s   a n d   t h e i r   w e i g h t s
------------------------------------------------------------------------------*/

// ROM 0x001677b4 GetLetterSet__Fv
ULong
GetLetterSet(void)
{
	gLetterSetSelection = RINT(RefVar(GetPreference(RSSYMlettersetselection)));
	return gLetterSetSelection;
}


// ROM 0x001677ec SetLetterSet__FUl
void
SetLetterSet(ULong letterSet)
{
	gLetterSetSelection = letterSet;
	SetPreference(RSSYMlettersetselection, RefVar(MAKEINT(letterSet)));
}


// ROM 0x00167830 GetParaPtr__FPPc
Ptr
GetParaPtr(Handle h)
{
	return *h + kHWRHeader;
}


// ROM 0x0016783c AdjustParaSize__FUl
ULong
AdjustParaSize(ULong size)
{
	return size - kHWRHeader;
}


// ROM 0x001678cc LetterWeightDataPtr__Fv
// (a pointer into a handle the caller has not locked: the learning info
//  outlives the parameter block it was asked through)
Ptr
LetterWeightDataPtr(void)
{
	GetLetterSet();
	Handle info = LIBeginWeights();
	TDomain* domain = XrWordDomain();
	domain->DomainParameter(0x20041, gLetterSetSelection, (ULong) info);
	Handle weights;
	domain->DomainParameter(0x20022, (ULong) &weights, (ULong) info);
	LIEndWeights(info);
	return GetParaPtr(weights);
}


// ROM 0x00167964 SizeOfLetterWeightData__Fv
ULong
SizeOfLetterWeightData(void)
{
	GetLetterSet();
	Handle info = LIBeginWeights();
	TDomain* domain = XrWordDomain();
	domain->DomainParameter(0x20041, gLetterSetSelection, (ULong) info);
	ULong size;
	domain->DomainParameter(0x20021, (ULong) &size, (ULong) info);
	LIEndWeights(info);
	return AdjustParaSize(size);
}


// ROM 0x00167dc8 LearningDataPtr__Fv
Ptr
LearningDataPtr(void)
{
	Handle info = LIBeginWeights();
	TDomain* domain = XrWordDomain();
	SetupXRD(domain, info, 0, 0);
	Handle db;
	domain->DomainParameter(0x20040, (ULong) &db, (ULong) info);
	LIEndWeights(info);
	return GetParaPtr(db);
}


// ROM 0x00167e40 SizeOfLearningData__Fv
// ROM BUG: when there is no database the domain answers nought, and
// AdjustParaSize takes the handle word off that - a size just short of
// four gigabytes (on the host, of the address space), which
// FGetLearningData then tries to allocate.  Only a writer who turned big
// learning on has a database, and only then is this asked.
ULong
SizeOfLearningData(void)
{
	Handle info = LIBeginWeights();
	TDomain* domain = XrWordDomain();
	SetupXRD(domain, info, 0, 0);
	ULong size;
	domain->DomainParameter(0x20039, (ULong) &size, (ULong) info);
	LIEndWeights(info);
	return AdjustParaSize(size);
}


// ROM 0x001679fc UseTrainingDataForRecognition
// UseTrainingDataForRecognition(on): whether the word domain reads with
// the learning info (0x20024) or with the descriptors' defaults (0x20023)
// - which lasts only as long as the parameter block it was asked in.
Ref
UseTrainingDataForRecognition(RefArg /*rcvr*/, RefArg on)
{
	Handle info = LIBeginWeights();
	TDomain* domain = XrWordDomain();
	domain->DomainParameter(ISNIL(on) ? 0x20023 : 0x20024, 0, (ULong) info);
	LIEndWeights(info);
	return NILREF;
}


// ROM 0x00167a70 FSetLetterWeights__FRC6RefVarT1
// SetLetterWeights(weights): a single binary for the current letter
// set, or an array of five, one per set (nil leaving a set at its
// defaults).  The learning infos are all thrown away first, so a set
// the array leaves out goes back to its defaults.
// (An array must have five elements: the ROM reads all five.)
Ref
FSetLetterWeights(RefArg /*rcvr*/, RefArg weights)
{
	if (ISNIL(weights))
		return NILREF;
	ULong letterSet = GetLetterSet();
	PGFreeAllLearningData();
	Handle info = LIBeginWeights();
	TDomain* domain = XrWordDomain();
	if (!IsArray(weights))
	{
		SetLetterSet(letterSet);
		domain->DomainParameter(0x20041, letterSet, (ULong) info);
		Size size = SizeOfLetterWeightData();
		Ptr dest = LetterWeightDataPtr();
		BlockMove(BinaryData(weights), dest, size);
	}
	else
	{
		RefVar one;
		for (ULong slot = 0; slot < 5; slot++)
		{
			one = GetArraySlotRef(weights, slot);
			if (NOTNIL(one))
			{
				SetLetterSet(slot);
				domain->DomainParameter(0x20041, slot, (ULong) info);
				Size size = SizeOfLetterWeightData();
				Ptr dest = LetterWeightDataPtr();
				BlockMove(BinaryData(one), dest, size);
			}
		}
	}
	SetLetterSet(letterSet);
	domain->DomainParameter(0x20041, letterSet, (ULong) info);
	LIEndWeights(info);
	return NILREF;
}


// ROM 0x00167c28 FGetLetterWeights__FRC6RefVar
// GetLetterWeights(): an array of five, a 'letterWeights binary for each
// letter set the writer has changed and nil for the rest - or nil when
// none has been.
Ref
FGetLetterWeights(RefArg /*rcvr*/)
{
	RefVar weights;
	RefVar one;
	long size = SizeOfLetterWeightData();
	if (size != 0)
	{
		for (ULong slot = 0; slot < 5; slot++)
		{
			Handle info = PGGetLetterSetInfo(slot);
			if (info != nil)
			{
				one = AllocateBinary(RSSYMletterweights, size);
				BlockMove(GetParaPtr(info), BinaryData(one), size);
				if (ISNIL(weights))
					weights = AllocateArray(RSSYMarray, 5);
				SetArraySlot(weights, slot, one);
			}
		}
	}
	return weights;
}


// ROM 0x00167d2c FResetLearningDefaults
// ResetLetterDefaults(): every letter set back to its defaults.
Ref
FResetLearningDefaults(RefArg /*rcvr*/)
{
	ULong letterSet = GetLetterSet();
	PGFreeAllLearningData();
	Handle info = LIBeginWeights();
	TDomain* domain = XrWordDomain();
	SetupXRD(domain, info, 0, 0);
	domain->DomainParameter(0x20041, letterSet, (ULong) info);
	domain->DomainParameter(0x20015, 0, (ULong) info);
	LIEndWeights(info);
	return NILREF;
}


// ROM 0x00167eb8 FSetLearningData__FRC6RefVarT1
Ref
FSetLearningData(RefArg /*rcvr*/, RefArg data)
{
	if (NOTNIL(data))
	{
		Size size = SizeOfLearningData();
		Ptr dest = LearningDataPtr();
		BlockMove(BinaryData(data), dest, size);
	}
	return NILREF;
}


// ROM 0x00167f24 FGetLearningData__FRC6RefVar
// GetLearningData(): the orthographic database as a 'learningData
// binary on the internal store.
// NOT YET RECONSTRUCTED: large binaries on a store (FLBAlloc); the host
// makes an ordinary binary.
Ref
FGetLearningData(RefArg /*rcvr*/)
{
	RefVar data;
	long size = SizeOfLearningData();
	if (size != 0)
	{
		data = AllocateBinary(RSSYMlearningdata, size);
		BlockMove(LearningDataPtr(), BinaryData(data), size);
	}
	return data;
}


// ROM 0x0016810c ConvertLetterWeights__FRC6RefVarlT2
// `direction` 0 takes a MessagePad 100 binary (a word in front) into
// this machine's layout; 1 the other way, the binary made with the word
// in front.
// DEVIATION: that word is the four bytes in front of the data - on the
// ROM, the engine handle's own address (the header word, which is all it
// is); on the host, the low half of the host's larger one.
Ref
ConvertLetterWeights(RefArg weights, long how, long direction)
{
	RefVar result;
	if (ISNIL(weights))
		return NILREF;
	ULong size = SizeOfLetterWeightData();
	Handle from = HWRMemoryAllocHandle(size);
	Handle to = HWRMemoryAllocHandle(size);
	if (to != nil && from != nil)
	{
		Ptr fromData = HWRMemoryLockHandle(from);
		ZeroBytes(fromData, size);
		Ptr toData = HWRMemoryLockHandle(to);
		ZeroBytes(toData, size);
		Ptr src = BinaryData(weights);
		if (direction == 0)
			src += 4;
		BlockMove(src, fromData, size);
		if (direction == 0)
			ConvertLearningInfo(from, to, how | (direction << 16));
		else if (direction == 1)
			ConvertLearningInfo(to, from, how | (direction << 16));
		if (direction == 1)
		{
			size += 4;
			toData = *to + kHWRHeader - 4;
		}
		result = AllocateBinary(RSSYMletterweights, size);
		BlockMove(toData, BinaryData(result), size);
	}
	if (from != nil)
		DisposHandle(from);
	if (to != nil)
		DisposHandle(to);
	return result;
}


// ROM 0x0016804c FConvertFromMP__FRC6RefVarN21
Ref
FConvertFromMP(RefArg /*rcvr*/, RefArg weights, RefArg how)
{
	return ConvertLetterWeights(weights, RINT(how), 0);
}


// ROM 0x001680ac FConvertForMP__FRC6RefVarN21
Ref
FConvertForMP(RefArg /*rcvr*/, RefArg weights, RefArg how)
{
	return ConvertLetterWeights(weights, RINT(how), 1);
}


// ROM 0x0016836c VerifyWordSymbols__FPUs
Boolean
VerifyWordSymbols(UniChar* word)
{
	Boolean ok = false;
	if (Ustrlen(word) < 0x3f)
	{
		char buffer[64];
		ConvertFromUnicode(word, buffer, kMacRomanEncoding, 0x3f);
		Handle info = LIBeginWeights();
		ok = XrWordDomain()->DomainParameter(0x20018, (ULong) buffer, (ULong) info) == 0;
		LIEndWeights(info);
	}
	return ok;
}


// ROM 0x001a0d14 FDoCursiveTraining__FRC6RefVarN21
// DoCursiveTraining(data, which): what the writer settled on handed to
// whichever word recogniser is in use to learn from.
Ref
FDoCursiveTraining(RefArg /*rcvr*/, RefArg data, RefArg which)
{
	ULong index = ISINT(which) ? RINT(which) : 0;
	DoIndexedLearning(gWordID, data, index);
	return NILREF;
}


// ROM 0x001b6c3c FRosettaExtension
Ref
FRosettaExtension(RefArg /*rcvr*/, RefArg /*a*/, RefArg /*b*/)
{
	return NILREF;
}


void
RegisterWordRecognizerNatives(void)
{
	RegisterNativeFunction("UseTrainingDataForRecognition", (void*) UseTrainingDataForRecognition, 1);
	RegisterNativeFunction("FSetLetterWeights__FRC6RefVarT1", (void*) FSetLetterWeights, 1);
	RegisterNativeFunction("FGetLetterWeights__FRC6RefVar", (void*) FGetLetterWeights, 0);
	RegisterNativeFunction("FResetLearningDefaults", (void*) FResetLearningDefaults, 0);
	RegisterNativeFunction("FSetLearningData__FRC6RefVarT1", (void*) FSetLearningData, 1);
	RegisterNativeFunction("FGetLearningData__FRC6RefVar", (void*) FGetLearningData, 0);
	RegisterNativeFunction("FConvertFromMP__FRC6RefVarN21", (void*) FConvertFromMP, 2);
	RegisterNativeFunction("FConvertForMP__FRC6RefVarN21", (void*) FConvertForMP, 2);
	RegisterNativeFunction("FDoCursiveTraining__FRC6RefVarN21", (void*) FDoCursiveTraining, 2);
	RegisterNativeFunction("FRosettaExtension", (void*) FRosettaExtension, 2);
}
