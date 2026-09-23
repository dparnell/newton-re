/*
	File:		recognition/CorrectInfo.cpp

	Contains:	The correction information - CorrectInfo.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CorrectInfo.h"
#include "WordInfo.h"
#include "UnitPublic.h"
#include "StrokeBundle.h"
#include "ParagraphView.h"
#include "InkShapes.h"
#include "View.h"
#include "ViewFlags.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Unicode.h"


/*------------------------------------------------------------------------------
	T h e   l i s t
------------------------------------------------------------------------------*/

// ROM 0x000761f0 CorrectInfo__Fv
// The machine's own correction list: `vars.correctInfo`.
Ref
CorrectInfo(void)
{
	return GetFrameSlotRef(gVarFrame, RSSYMcorrectinfo);
}


// ROM 0x0007627c InitCorrection__FRC6RefVar
// A correction list emptied: a fresh `info` array.
void
InitCorrection(RefArg list)
{
	SetFrameSlot(list, RSSYMinfo, RefVar(MakeArray(0)));
}


// ROM 0x00076bf0 InitCorrection__Fv
// The machine's own list emptied, and told how many words it keeps.
void
InitCorrection(void)
{
	RefVar list(CorrectInfo());
	InitCorrection(list);
	SetFrameSlot(list, RSSYMmax, RefVar(MAKEINT(0x28)));
}


/*------------------------------------------------------------------------------
	M a k i n g   o n e
------------------------------------------------------------------------------*/

// ROM 0x00078548 MakeWordInterp__FRC6RefVar
// One reading: protoWordInterp with the word in it.
Ref
MakeWordInterp(RefArg word)
{
	RefVar interp(Clone(RefVar(Rprotowordinterp)));
	SetFrameSlot(interp, RSSYMword, word);
	return interp;
}


// ROM 0x00078598 MakeWordInterp__FRC6RefVarlN22
// The same with the recogniser's three numbers: how sure it is, where
// the reading came in its list, and what kind of thing it took the
// writing for.
Ref
MakeWordInterp(RefArg word, long score, long index, long label)
{
	RefVar interp(Clone(RefVar(Rprotowordinterp)));
	SetFrameSlot(interp, RSSYMword, word);
	SetFrameSlot(interp, RSSYMscore, RefVar(MAKEINT(score)));
	SetFrameSlot(interp, RSSYMindex, RefVar(MAKEINT(index)));
	SetFrameSlot(interp, RSSYMlabel, RefVar(MAKEINT(label)));
	return interp;
}


// ROM 0x0007817c MakeWordInfo__FRC6RefVar
// A word info made out of one thing: a string becomes its single
// reading, and anything else - a stroke bundle - becomes the writing
// with no reading at all.
Ref
MakeWordInfo(RefArg word)
{
	RefVar words(MakeArray(0));
	RefVar info(Clone(RefVar(Rprotowordinfo)));
	if (IsString(word))
	{
		AddArraySlot(words, RefVar(MakeWordInterp(word)));
		SetFrameSlot(info, RSSYMwords, words);
	}
	else
	{
		SetFrameSlot(info, RSSYMwords, words);
		SetFrameSlot(info, RSSYMstrokes, word);
	}
	return info;
}


// ROM 0x00078960 GetStrokesAt__FP5TViewl
// The writing at a character offset of a paragraph, opened back out into
// a stroke bundle with its bounds worked out.  Nothing for a view that
// is not a paragraph, or an offset whose character is not ink.
Ref
GetStrokesAt(TView* view, long offset)
{
	RefVar strokes;
	if (view->DerivedFrom(clParagraphView))
	{
		RefVar ink(GetInkAt((TParagraphView*) view, offset));
		if (NOTNIL(ink))
		{
			strokes = ExpandInk(ink, 3);
			CalcBundleBounds(strokes);
		}
	}
	return strokes;
}


// ROM 0x0007824c MakeWordInfo__FP5TViewlT2
// A word info for a stretch of a view's text that the recogniser never
// saw - typed, or pasted, or already there.  When there is writing at
// the offset the frame is made of that; otherwise the characters
// themselves are its one reading.
Ref
MakeWordInfo(TView* view, long offset, long length)
{
	RefVar strokes(GetStrokesAt(view, offset));
	if (NOTNIL(strokes))
		return MakeWordInfo(strokes);
	RefVar textRef(((TParagraphView*) view)->Text());
	const UniChar* text = GetCString(textRef);
	RefVar word(MakeString(text + offset, length));
	return MakeWordInfo(word);
}


// ROM 0x00078340 SetWordList__FRC6RefVarT1
// The frame's readings replaced by a plain list of words, each wrapped
// in a protoWordInterp of its own.  A nil list empties them.
void
SetWordList(RefArg info, RefArg words)
{
	RefVar interps;
	if (NOTNIL(words))
	{
		long count = Length(words);
		interps = MakeArray(count);
		for (long i = 0; i < count; i++)
		{
			RefVar interp(Clone(RefVar(Rprotowordinterp)));
			SetFrameSlot(interp, RSSYMword, RefVar(GetArraySlotRef(words, i)));
			SetArraySlot(interps, i, interp);
		}
	}
	SetFrameSlot(info, RSSYMwords, interps);
}


// ROM 0x00078438 SetOffsetInfo__FRC6RefVarP5TViewlN23
// Where the word went: which view (by its id, so the frame survives the
// view), the character offsets it covers, and what is known about it.
void
SetOffsetInfo(RefArg info, TView* view, long start, long stop, long flags)
{
	SetFrameSlot(info, RSSYMid, RefVar(MAKEINT(view->fId)));
	SetFrameSlot(info, RSSYMstart, RefVar(MAKEINT(start)));
	SetFrameSlot(info, RSSYMstop, RefVar(MAKEINT(stop)));
	SetFrameSlot(info, RSSYMflags, RefVar(MAKEINT(flags)));
}


/*------------------------------------------------------------------------------
	R e a d i n g   o n e
------------------------------------------------------------------------------*/

// ROM 0x00077c64 GetNthEntry__FRC6RefVarl
// The index'th reading, or nil when there are not that many.
Ref
GetNthEntry(RefArg info, long index)
{
	RefVar words(GetFrameSlotRef(info, RSSYMwords));
	if (NOTNIL(words) && index < Length(words))
		return GetArraySlotRef(words, index);
	return NILREF;
}


// ROM 0x00077ce0 GetNthWord__FRC6RefVarl
// Its word.
Ref
GetNthWord(RefArg info, long index)
{
	RefVar entry(GetNthEntry(info, index));
	if (ISNIL(entry))
		return NILREF;
	return GetFrameSlotRef(entry, RSSYMword);
}


// ROM 0x00077be4 UnitID__FRC6RefVar
// The four characters of the unit's type, as the recogniser wrote them
// into the frame - read back out of the string's bytes rather than
// decoded, because that is all anyone does with them.
ULong
UnitID(RefArg info)
{
	RefVar id(GetFrameSlotRef(info, RSSYMunitid));
	if (ISNIL(id))
		return 0;
	return *(const ULong*) BinaryData(id);
}


// ROM 0x00077d34 TestWordInfoFlags__FRC6RefVarl
// Whether *all* of them are set.
Boolean
TestWordInfoFlags(RefArg info, long flags)
{
	long was = RINT(RefVar(GetFrameSlotRef(info, RSSYMflags)));
	return (was & flags) == flags;
}


// ROM 0x00077e30 ClearWordInfoFlags__FRC6RefVarl
void
ClearWordInfoFlags(RefArg info, long flags)
{
	long was = RINT(RefVar(GetFrameSlotRef(info, RSSYMflags)));
	SetFrameSlot(info, RSSYMflags, RefVar(MAKEINT(was & ~flags)));
}


/*------------------------------------------------------------------------------
	F i n d i n g   o n e
------------------------------------------------------------------------------*/

// ROM 0x000768fc FindWordInfoIndex__FRC6RefVarP5TViewl
// Where in the list the word covering a character offset of a view is.
// A frame matches when its `id` is the view's and the offset falls in
// [start, stop).  ==> -1 for none.
long
FindWordInfoIndex(RefArg list, TView* view, long offset)
{
	long id = view->fId;
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	long count = Length(info);
	for (long i = 0; i < count; i++)
	{
		RefVar word(GetArraySlotRef(info, i));
		if (RINT(RefVar(GetFrameSlotRef(word, RSSYMid))) != id)
			continue;
		if (RINT(RefVar(GetFrameSlotRef(word, RSSYMstart))) > offset)
			continue;
		if (offset < RINT(RefVar(GetFrameSlotRef(word, RSSYMstop))))
			return i;
	}
	return -1;
}


// ROM 0x00076a30 FindWordInfo__FRC6RefVarP5TViewl
// The same, answering the frame itself.  (The ROM writes the search out
// twice rather than calling the one above; the two walk the list the
// same way.)
Ref
FindWordInfo(RefArg list, TView* view, long offset)
{
	long index = FindWordInfoIndex(list, view, offset);
	if (index < 0)
		return NILREF;
	return GetArraySlotRef(RefVar(GetFrameSlotRef(list, RSSYMinfo)), index);
}


// ROM 0x00078500 FindWordInfo__FP5TViewl
// Out of the machine's own list.
Ref
FindWordInfo(TView* view, long offset)
{
	RefVar list(CorrectInfo());
	return FindWordInfo(list, view, offset);
}


/*------------------------------------------------------------------------------
	A d d i n g   o n e
------------------------------------------------------------------------------*/

// ROM 0x000770cc AddWordInfo__FRC6RefVarT1
// A word info put on the list - but only when its first reading is a
// single word.  A reading with a space in it (or anything else that ends
// a word before the string does) is not something the corrector can
// offer alternatives for, so it is dropped rather than kept.
//
// ==> the frame, whether or not it went on.
Ref
AddWordInfo(RefArg list, RefArg info)
{
	RefVar word(GetNthWord(info, 0));
	if (NOTNIL(word))
	{
		const UniChar* text = GetCString(word);
		long length = Ustrlen(text);
		if (Ustrlen(text) == ScanWordEnd(text, 0, length))
			AddArraySlot(RefVar(GetFrameSlotRef(list, RSSYMinfo)), info);
	}
	return info;
}


// ROM 0x00076b64 AddWordInfo__FRC6RefVarP5TViewlT3P11TUnitPublic
// The unit's own word info put on the list, told where it landed and
// marked as a word the corrector knows about (flags 3: known, and the
// entry is in use).
Ref
AddWordInfo(RefArg list, TView* view, long start, long stop, TUnitPublic* unit)
{
	RefVar info(unit->WordInfo());
	SetOffsetInfo(info, view, start, stop, 3);
	return AddWordInfo(list, info);
}


// ROM 0x0007959c AutoRemove__FRC6RefVar
// A word the machine put into the dictionary on this entry's account
// taken out again, because the entry is going.
//
// NOT YET RECONSTRUCTED: RemoveAutoAdd 0x0013e4b4, which is the
// dictionary's side of it - the whole dictionary area is NOT YET.  The
// flag is cleared either way, as the ROM clears it.
void
AutoRemove(RefArg info)
{
	if (!TestWordInfoFlags(info, kWordInfoAutoAdded))
		return;
	RefVar word(GetNthWord(info, 0));
	if (NOTNIL(word) && (Length(word) - 2) / (long) sizeof(UniChar) != 0)
		;	// RemoveAutoAdd(GetCString(word));
	ClearWordInfoFlags(info, kWordInfoAutoAdded);
}
