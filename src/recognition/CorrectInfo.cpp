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
	if (ISNIL(list))
	{
		// (host: the machine's starter globals come with a
		//  `correctInfo` frame, so the ROM never has to make one; a
		//  host that has not run the ROM's boot block has none)
		list = AllocateFrame();
		SetFrameSlot(RefVar(gVarFrame), RSSYMcorrectinfo, list);
	}
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


/*------------------------------------------------------------------------------
	K e e p i n g   u p   w i t h   t h e   t e x t
------------------------------------------------------------------------------*/

// The offsets a word info carries are into a paragraph's text, so every
// edit of that text has to be answered here or the corrector would offer
// a word's alternatives for whatever now happens to sit at those
// offsets.

// ROM 0x00076c48 MergeWords__FRC6RefVarT1
// The two frames' first readings run together, as the one reading of a
// new word list.  Nothing comes back when either has no reading at all.
Ref
MergeWords(RefArg first, RefArg second)
{
	RefVar words(GetFrameSlotRef(first, RSSYMwords));
	RefVar other(GetFrameSlotRef(second, RSSYMwords));
	if (ISNIL(words) || Length(words) == 0)
		return words;
	if (ISNIL(other) || Length(other) == 0)
		return words;
	words = GetFrameSlotRef(RefVar(GetArraySlotRef(words, 0)), RSSYMword);
	other = GetFrameSlotRef(RefVar(GetArraySlotRef(other, 0)), RSSYMword);
	long count = (Length(other) - 2) / (long) sizeof(UniChar);
	long at = (Length(words) - 2) / (long) sizeof(UniChar);
	StrMunger(words, at, 0, other, 0, count);
	return MakeWordList(RefVar(MakeWordInterp(words)));
}


// ROM 0x0007866c MakeWordList__FRC6RefVar
// One reading made into a word list of its own.
Ref
MakeWordList(RefArg interp)
{
	RefVar words(MakeArray(1));
	SetArraySlot(words, 0, interp);
	return words;
}


// ROM 0x00078840 MergeStrokes__FRC6RefVarT1
// The second bundle's strokes added to the first's, and the first's
// bounds worked out again over the lot.
void
MergeStrokes(RefArg bundle, RefArg other)
{
	RefVar strokes(GetFrameSlotRef(bundle, RSSYMstrokes));
	RefVar more(GetFrameSlotRef(other, RSSYMstrokes));
	long was = Length(strokes);
	long count = Length(more);
	SetLength(strokes, was + count);
	for (long i = was; i < was + count; i++)
		SetArraySlot(strokes, i, RefVar(GetArraySlotRef(more, i - was)));
	Rect bounds;
	GetBundleBounds(bundle, &bounds);
	SetFrameSlot(bundle, RSSYMbounds, RefVar(ToObject(bounds)));
}


// ROM 0x00076dc0 MergeWordInfo__FRC6RefVarlT2
// Two entries of a list made into one: the first takes the second's
// `stop`, their words are run together and their writing is joined, and
// the second goes.  The merged entry keeps none of its old flags but is
// marked as one the corrector knows about, because what it now stands
// for is not what either of them stood for.
//
// The words and the writing are only merged when *both* carry writing;
// otherwise the first simply swallows the second's range.
void
MergeWordInfo(RefArg list, long first, long second)
{
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	RefVar one(GetArraySlotRef(info, first));
	RefVar two(GetArraySlotRef(info, second));
	RefVar strokes(GetFrameSlotRef(one, RSSYMstrokes));
	RefVar more(GetFrameSlotRef(two, RSSYMstrokes));
	if (NOTNIL(strokes) && NOTNIL(more))
	{
		SetFrameSlot(one, RSSYMstop, RefVar(GetFrameSlotRef(two, RSSYMstop)));
		ClearWordInfoFlags(one, 0xffff);
		SetWordInfoFlags(one, kWordInfoKnown);
		SetFrameSlot(one, RSSYMwords, RefVar(MergeWords(one, two)));
		MergeStrokes(strokes, more);
	}
	ArrayRemoveCount(info, second, 1);
}


// ROM 0x000762c0 OffsetCorrectionInfo__FRC6RefVarP5TViewlN23
// The text of a view has changed: `removed` characters at `at` have been
// replaced by `inserted`.  Every entry of the list that belongs to that
// view (or, with no view, every entry at all) is answered:
//
//   one that straddles the change    goes - its word no longer means
//                                    anything
//   one entirely after it            has both its offsets moved along
//   one before it                    is left alone
//
// An entry the change covers exactly goes too.
//
// The one case that gains rather than loses is a backspace - one
// character out and nothing in - which may have closed the gap between
// two words: the entry that ends where the character was and the one
// that began just after it are merged back into one.
void
OffsetCorrectionInfo(RefArg list, TView* view, long at, long removed, long inserted)
{
	if (ISNIL(list))
		return;
	Boolean joining = removed == 1 && inserted == 0;
	long before = -1;
	long after = -1;
	long end = at + removed;
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	long count = Length(info);
	for (long i = 0; i < count; i++)
	{
		RefVar word(GetArraySlotRef(info, i));
		if (view != nil && RINT(RefVar(GetFrameSlotRef(word, RSSYMid))) != view->fId)
			continue;
		long start = RINT(RefVar(GetFrameSlotRef(word, RSSYMstart)));
		long stop = RINT(RefVar(GetFrameSlotRef(word, RSSYMstop)));

		if (joining)
		{
			// the two a backspace might join
			if (at == stop)
				before = i;
			else if (start - 1 == at)
				after = i;
		}

		Boolean drop = at == start && end == stop;
		if (!drop)
		{
			Boolean straddles = start < at ? at < stop : start < end;
			if (straddles)
				drop = true;
			else if (start >= end)
			{
				SetFrameSlot(word, RSSYMstart,
							 RefVar(MAKEINT(start + inserted - removed)));
				SetFrameSlot(word, RSSYMstop,
							 RefVar(MAKEINT(stop + inserted - removed)));
			}
		}
		if (!drop)
			continue;

		if (EQRef(list, RefVar(CorrectInfo())))
			AutoRemove(word);
		ArrayRemoveCount(info, i, 1);
		if (before == i)
			before = -1;
		else if (i <= before)
			before--;
		if (after == i)
			after = -1;
		else if (i <= after)
			after--;
		count--;
		i--;
	}
	if (joining && before != -1 && after != -1)
		MergeWordInfo(list, before, after);
}


// ROM 0x0007779c OffsetCorrectionInfo__FP5TViewlN22
// The same over the machine's own list.
void
OffsetCorrectionInfo(TView* view, long at, long removed, long inserted)
{
	RefVar list(CorrectInfo());
	OffsetCorrectionInfo(list, view, at, removed, inserted);
}


// ROM 0x000765bc ClearCorrectionRange__FRC6RefVarP5TViewlT3
// Every entry overlapping a range of a view's text taken off the list,
// with no moving of what is left: what the range held is about to be
// something else.
void
ClearCorrectionRange(RefArg list, TView* view, long at, long length)
{
	if (ISNIL(list))
		return;
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	long count = Length(info);
	for (long i = 0; i < count; i++)
	{
		RefVar word(GetArraySlotRef(info, i));
		if (view != nil && RINT(RefVar(GetFrameSlotRef(word, RSSYMid))) != view->fId)
			continue;
		long start = RINT(RefVar(GetFrameSlotRef(word, RSSYMstart)));
		long stop = RINT(RefVar(GetFrameSlotRef(word, RSSYMstop)));
		Boolean overlaps = start < at ? at < stop : start < at + length;
		if (!overlaps)
			continue;
		if (EQRef(list, RefVar(CorrectInfo())))
			AutoRemove(word);
		ArrayRemoveCount(info, i, 1);
		count--;
		i--;
	}
}


// ROM 0x00076758 DeletedCorrectionInfo__FRC6RefVarP5TView
// The view is going: every word the machine added to the dictionary on
// one of its entries' account is taken back out.  The entries themselves
// are left - it is `RemoveCorrectionInfo` that takes those.
void
DeletedCorrectionInfo(RefArg list, TView* view)
{
	long id = view->fId;
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	long count = Length(info);
	for (long i = 0; i < count; i++)
	{
		RefVar word(GetArraySlotRef(info, i));
		if (RINT(RefVar(GetFrameSlotRef(word, RSSYMid))) == id
			&& EQRef(list, RefVar(CorrectInfo())))
			AutoRemove(word);
	}
}


// ROM 0x00076830 RemoveCorrectionInfo__FRC6RefVarP5TView
// Every entry belonging to a view taken off the list.
void
RemoveCorrectionInfo(RefArg list, TView* view)
{
	long id = view->fId;
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	long count = Length(info);
	for (long i = 0; i < count; i++)
	{
		RefVar word(GetArraySlotRef(info, i));
		if (RINT(RefVar(GetFrameSlotRef(word, RSSYMid))) != id)
			continue;
		ArrayRemoveCount(info, i, 1);
		count--;
		i--;
	}
}


// ROM 0x00077d88 RemoveCorrectionInfo__FP5TView
void
RemoveCorrectionInfo(TView* view)
{
	RefVar list(CorrectInfo());
	RemoveCorrectionInfo(list, view);
}


// ROM 0x00078d04 ClearEmptyEntries__FRC6RefVar
// Entries that hold nothing the recogniser actually proposed taken off
// the list, working from the end.  An entry is empty when it has fewer
// than three readings and every one of them is one the machine made up
// rather than read: index -1 (the word as it was written) or -2 (the
// same word capitalised).
void
ClearEmptyEntries(RefArg list)
{
	if (ISNIL(list))
		return;
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	for (long i = Length(info) - 1; i >= 0; i--)
	{
		RefVar word(GetArraySlotRef(info, i));
		RefVar words(GetFrameSlotRef(word, RSSYMwords));
		if (ISNIL(words))
			continue;
		long count = Length(words);
		if (count >= 3)
			continue;
		Boolean capitalised = false;
		Boolean asWritten = false;
		for (long j = 0; j < count; j++)
		{
			long index = RINT(RefVar(GetFrameSlotRef(
									 RefVar(GetArraySlotRef(words, j)), RSSYMindex)));
			if (index == -2)
				capitalised = true;
			else if (index == -1)
				asWritten = true;
		}
		Boolean empty = false;
		if (count == 1)
			empty = capitalised || asWritten;
		else if (count == 2)
			empty = capitalised && asWritten;
		if (empty)
			ArrayRemoveCount(info, i, 1);
	}
}


// ROM 0x00076f58 GetWordInfo__FRC6RefVarP5TViewlT3
// The entry for a range of a view's text, made when there is none.  An
// entry that covers the offset but not the whole range is no use: the
// range is cleared and a fresh entry made for it out of whatever is
// there (`MakeWordInfo(view, offset, length)`).  An empty range gets its
// frame but does not go on the list.
Ref
GetWordInfo(RefArg list, TView* view, long at, long length)
{
	long index = FindWordInfoIndex(list, view, at);
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	RefVar word;
	if (index >= 0)
	{
		word = GetArraySlotRef(info, index);
		if (RINT(RefVar(GetFrameSlotRef(word, RSSYMstart))) != at
			|| RINT(RefVar(GetFrameSlotRef(word, RSSYMstop))) != at + length)
			word = NILREF;
	}
	if (ISNIL(word))
	{
		ClearCorrectionRange(list, view, at, length);
		word = MakeWordInfo(view, at, length);
		SetOffsetInfo(word, view, at, at + length, 0);
		if (length > 0)
		{
			ClearEmptyEntries(list);
			AddWordInfo(list, word);
		}
	}
	return word;
}


// ROM 0x00077190 ExtractRange__FRC6RefVarP5TViewlT3
// A correction info of its own holding copies of every entry of a view
// that overlaps a range of its text.  That is what goes into an undo:
// the words about to be deleted, kept so that undoing the deletion puts
// their alternatives back too.  ==> nil when the range holds none.
Ref
ExtractRange(RefArg list, TView* view, long from, long to)
{
	RefVar range;
	if (ISNIL(list))
		return range;
	long id = view->fId;
	RefVar taken(NewCorrectInfo());
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	long count = Length(info);
	for (long i = 0; i < count; i++)
	{
		RefVar word(GetArraySlotRef(info, i));
		long start = RINT(RefVar(GetFrameSlotRef(word, RSSYMstart)));
		long stop = RINT(RefVar(GetFrameSlotRef(word, RSSYMstop)));
		if (RINT(RefVar(GetFrameSlotRef(word, RSSYMid))) != id)
			continue;
		if (start < to && from <= stop)
			AddWordInfo(taken, RefVar(Clone(word)));
	}
	if (Length(RefVar(GetFrameSlotRef(taken, RSSYMinfo))) > 0)
		range = taken;
	return range;
}


// ROM 0x00077378 InsertRange__FRC6RefVarT1P5TView
// The other way: the entries of a range put onto a list and told which
// view they now belong to.  An undo puts its saved words back this way.
//
// NOT YET RECONSTRUCTED: the dictionary side - DoOverflowLearning
// 0x00079704, which learns from an entry about to fall off the end of
// the machine's list, and AutoAdd 0x000793d8, which puts a word the
// writer has corrected into the dictionary (unless the view's
// `_noAutoAdd` says not to).  Both belong to the dictionaries, which are
// NOT YET.
void
InsertRange(RefArg list, RefArg range, TView* view)
{
	if (ISNIL(range) || ISNIL(list))
		return;
	RefVar own(CorrectInfo());
	long id = view->fId;
	RefVar info(GetFrameSlotRef(range, RSSYMinfo));
	long count = Length(info);
	for (long i = 0; i < count; i++)
	{
		RefVar word(GetArraySlotRef(info, i));
		SetFrameSlot(word, RSSYMid, RefVar(MAKEINT(id)));
		if (EQRef(list, own))
		{
			// DoOverflowLearning(list);
			AddWordInfo(list, word);
			// if (ISNIL(view->GetProto(RSSYM_noautoadd))) AutoAdd(word);
		}
		else
			AddWordInfo(list, word);
	}
}


/*------------------------------------------------------------------------------
	T h e   r e a d i n g s ,   o n e   b y   o n e
------------------------------------------------------------------------------*/

// The corrector rewrites a word info's readings as the writer chooses
// between them, so the list has to be editable a reading at a time.

// ROM 0x000774c8 FindMatchingWord__FRC6RefVarT1
// Which reading has this word, comparing the characters rather than the
// objects.  ==> its index, -1 for none.
long
FindMatchingWord(RefArg info, RefArg word)
{
	RefVar words(GetFrameSlotRef(info, RSSYMwords));
	if (ISNIL(words))
		return -1;
	const UniChar* wanted = GetCString(word);
	long count = Length(words);
	for (long i = 0; i < count; i++)
	{
		RefVar entry(GetFrameSlotRef(RefVar(GetArraySlotRef(words, i)), RSSYMword));
		if (Ustrcmp(wanted, GetCString(entry)) == 0)
			return i;
	}
	return -1;
}


// ROM 0x00077644 InsertWordInterp__FRC6RefVarT1l
// A reading put in at an index - at the end when the index is past it,
// which is also what a negative index means.  A frame with no readings
// at all gets an array first.
void
InsertWordInterp(RefArg info, RefArg interp, long index)
{
	RefVar words(GetFrameSlotRef(info, RSSYMwords));
	if (ISNIL(words))
		words = MakeArray(0);
	if (index < 0 || Length(words) <= index)
		AddArraySlot(words, interp);
	else
	{
		RefVar one(MakeArray(1));
		SetArraySlot(one, 0, interp);
		ArrayMunger(words, index, 0, one, 0, 1);
	}
}


// ROM 0x000776e4 RemoveWordInterp__FRC6RefVarl
void
RemoveWordInterp(RefArg info, long index)
{
	RefVar words(GetFrameSlotRef(info, RSSYMwords));
	if (NOTNIL(words))
		ArrayRemoveCount(words, index, 1);
}


// ROM 0x0017b18c DeleteMatchingWord__FRC6RefVarT1
// The reading with this word taken out, if there is one.
void
DeleteMatchingWord(RefArg info, RefArg word)
{
	long index = FindMatchingWord(info, word);
	if (index < 0)
		return;
	RefVar words(GetFrameSlotRef(info, RSSYMwords));
	if (NOTNIL(words))
		ArrayRemoveCount(words, index, 1);
}


/*------------------------------------------------------------------------------
	L e a r n i n g   f r o m   t h e   l i s t
------------------------------------------------------------------------------*/

// The list is not only a record.  A word falling off the end of it is the
// machine's last chance to learn from what the writer did with it, and a
// word the writer accepted is a candidate for the dictionary.

// ROM 0x00077ea0 DoEntryLearning__FRC6RefVarl
// What the writer settled on, given to the engine so that it reads the
// same writing better next time.  Only an entry that carries training
// data (flag 1) and has kept it (`unitData`) has anything to teach, and
// only a reading the recogniser actually proposed (index >= 0) counts -
// a word the machine made up out of the letters tells it nothing.
// Afterwards the training data goes, because it has been used.
//
// NOT YET RECONSTRUCTED: DoIndexedLearning 0x000797ec, which is the
// engine's side of it.
Ref
DoEntryLearning(RefArg info, long which)
{
	if (!TestWordInfoFlags(info, kWordInfoHasTrainingData))
		return info;
	RefVar words(GetFrameSlotRef(info, RSSYMwords));
	if (ISNIL(words))
		return info;
	RefVar entry(GetArraySlotRef(words, which));
	long index = RINT(RefVar(GetFrameSlotRef(entry, RSSYMindex)));
	RefVar data(GetFrameSlotRef(info, RSSYMunitdata));
	if (index >= 0 && NOTNIL(data))
	{
		// DoIndexedLearning(UnitID(info), info, index);
		ClearWordInfoFlags(info, kWordInfoHasTrainingData);
		SetFrameSlot(info, RSSYMunitdata, RefVar(NILREF));
	}
	return info;
}


// ROM 0x000793d0 DoOverflowLearning__FRC6RefVar
// Room made for one more word: while the list is at its `max`, the
// oldest entry is learned from (its first reading, which is the one that
// went onto the page) and dropped.  The empty entries go first, so the
// machine does not spend its one lesson on a word it invented.
void
DoOverflowLearning(RefArg list)
{
	RefVar max(GetFrameSlotRef(list, RSSYMmax));
	if (ISNIL(max))
		return;
	ClearEmptyEntries(list);
	RefVar info(GetFrameSlotRef(list, RSSYMinfo));
	while (RINT(max) <= Length(info))
	{
		RefVar oldest(GetArraySlotRef(info, 0));
		DoEntryLearning(oldest, 0);
		ArrayRemoveCount(info, 0, 1);
	}
}


// ROM 0x000794ec AutoAdd__FRC6RefVar
// A word the writer has just put on the page offered to the dictionary,
// unless the view says not to (`_noAutoAdd`).  The entry is marked as
// having been added so that `AutoRemove` can take it back out again if
// the word goes.
//
// NOT YET RECONSTRUCTED: AddAutoAdd 0x0013e42c, the dictionary's side -
// so nothing is ever added and the flag is never set.
void
AutoAdd(RefArg info)
{
	if (ISNIL(RefVar(GetFrameSlotRef(info, RSSYMwords))))
		return;
	RefVar word(GetNthWord(info, 0));
	if (ISNIL(word) || (Length(word) - 2) / (long) sizeof(UniChar) == 0)
		return;
	if (NOTNIL(RefVar(GetProtoVariable(info, RSSYM_noautoadd, nil))))
		return;
	// if (AddAutoAdd(GetCString(word)))
	//     SetWordInfoFlags(info, kWordInfoAutoAdded);
}


// ROM 0x00079790 AddWordInfo__FP5TViewlT2P11TUnitPublic
// The word the recogniser has just put onto a page registered with the
// machine: room made for it, the unit's own frame put on the list saying
// where it landed, and the word offered to the dictionary.
Ref
AddWordInfo(TView* view, long start, long stop, TUnitPublic* unit)
{
	RefVar list(CorrectInfo());
	DoOverflowLearning(list);
	RefVar info(AddWordInfo(list, view, start, stop, unit));
	if (ISNIL(RefVar(view->GetProto(RSSYM_noautoadd))))
		AutoAdd(info);
	return info;
}
