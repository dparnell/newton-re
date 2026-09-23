/*
	File:		recognition/WordInfo.cpp

	Contains:	The word info frame - WordInfo.h.
*/

#include "WordInfo.h"
#include "WordList.h"
#include "UnitPublic.h"
#include "Unit.h"
#include "Recognizer.h"
#include "StrokeBundle.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Unicode.h"
#include "WRecDomain.h"		// kWRecInk


// ROM 0x00077bb0 EncodeUnitID__FP11TUnitPublic
// A unit's type as a string - and the string is the four characters of
// the type itself, read as two Unicode characters rather than four
// ASCII ones.  'STRK' comes out as the two characters 0x5354 0x524B,
// which is meaningless as text but unique per type and costs nothing to
// make.  The script side only ever compares it with another one.
Ref
EncodeUnitID(TUnitPublic* unit)
{
	ULong type = unit->GetType();
	UniChar id[3];
	// (the ROM writes the type as one word over the two characters,
	//  which on a big-endian machine puts the first two bytes of the
	//  type in the first character; written out a character at a time
	//  so that it comes to the same thing on any host)
	id[0] = (UniChar) (type >> 16);
	id[1] = (UniChar) type;
	id[2] = 0;
	return MakeString(id, 2);
}


// ROM 0x00077dc0 SetWordInfoFlags__FRC6RefVarl
// Flags or'd into what is already there.
void
SetWordInfoFlags(RefArg info, long flags)
{
	long was = RINT(RefVar(GetFrameSlot(info, RSSYMflags)));
	SetFrameSlot(info, RSSYMflags, MAKEINT(was | flags));
}


// ROM 0x000786b4 MakeWordList__FP11TUnitPublic
// The unit's readings as an array of protoWordInterp frames, one each,
// carrying the word, what the recogniser scored it, where it came in
// the list and what kind of thing the recogniser thinks it is.  The
// unit hands its word list over rather than lending it, so it is
// deleted here.
Ref
MakeWordList(TUnitPublic* unit)
{
	RefVar result(NILREF);
	TWordList* words = unit->Words();
	if (words != nil)
	{
		long count = words->Count();
		result = AllocateArray(RSSYMarray, count);
		for (long i = 0; i < count; i++)
		{
			RefVar interp(Clone(RefVar(Rprotowordinterp)));
			Handle word = words->Word(i);
			SetFrameSlot(interp, RSSYMword, RefVar(MakeString((UniChar*) *word)));
			DisposHandle(word);
			SetFrameSlot(interp, RSSYMscore, MAKEINT(words->Score(i)));
			SetFrameSlot(interp, RSSYMindex, MAKEINT(i));
			SetFrameSlot(interp, RSSYMlabel, MAKEINT(words->Label(i)));
			SetArraySlot(result, i, interp);
		}
		delete words;
	}
	return result;
}


// ROM 0x00077fd8 MakeWordInfo__FP11TUnitPublic
// Everything a script is told about a piece of writing, in one frame.
// A unit that was never read - no interpretations at all - or that the
// recogniser answers "ink" for is marked as ink and has its words taken
// away, so that a frame never offers a reading and ink at the same
// time.
Ref
MakeWordInfo(TUnitPublic* unit)
{
	RefVar info(Clone(RefVar(Rprotowordinfo)));
	if (unit == nil)
		return info;

	SetFrameSlot(info, RSSYMunitid, RefVar(EncodeUnitID(unit)));
	SetFrameSlot(info, RSSYMstrokes, RefVar(ExpandUnit(unit)));
	SetFrameSlot(info, RSSYMwords, RefVar(MakeWordList(unit)));
	SetFrameSlot(info, RSSYMunitdata, RefVar(unit->TrainingData()));

	TRecognizer* recognizer =
		gRecognition.fRecognizers->FindRecognizer(unit->GetType());
	Boolean isInk = (unit->fUnit->InterpretationCount() == 0);
	if (!isInk && recognizer != nil)
		isInk = (recognizer->UnitConfidence(unit) == kWRecInk);
	if (isInk)
	{
		SetWordInfoFlags(info, kWordInfoIsInk);
		RefVar words(GetFrameSlot(info, RSSYMwords));
		SetLength(words, 0);
	}
	return info;
}
