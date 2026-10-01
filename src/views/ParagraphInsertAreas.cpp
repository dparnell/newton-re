/*
	File:		views/ParagraphInsertAreas.cpp

	Contains:	A paragraph's insert areas: the stretches of white space a
				caret gesture opens for the writer to write into
				(InsertHorizontalSpace, InsertVerticalSpace, AddSpaceToEnd
				save them), followed through every later edit of the text
				(HandleReplaceText), and - once something has been written
				into one and the pen has been still for a while - the white
				space left over taken out again (Idle reason 1).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ParagraphView.h"
#include "RootView.h"
#include "Application.h"
#include "Commands.h"
#include "View.h"
#include "List.h"
#include "Frames.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "NewtonTime.h"
#include "Rects.h"

static const UniChar kSpaceChar = 0x20;
static const UniChar kReturnChar = 0x0d;
static const UniChar kTabChar = 0x09;

// What a block of white space is brought down to: the ROM's SpaceString
// (0x0c101750) and NewLineString (0x0c101748)
static const UniChar kOneSpace[] = { kSpaceChar, 0 };
static const UniChar kOneNewLine[] = { kReturnChar, 0 };


// ROM 0x00176818 SaveInsertArea__FP13InsertRunListUlT2
// A stretch just opened remembered, in order of its start - unless it
// starts inside one already there, which then stands for it.
void
SaveInsertArea(CList* list, ULong offset, ULong length)
{
	InsertRun* item = new InsertRun;
	if (item == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	item->fStart = offset;
	item->fLength = length;
	item->fChanged = false;
	TListLoop loop(list);
	InsertRun* run = (InsertRun*) loop.Next();
	while (run != nil && run->fStart <= offset)
	{
		if (offset < run->fLength + run->fStart)
		{
			delete item;
			return;
		}
		run = (InsertRun*) loop.Next();
	}
	list->InsertAt(loop.Index(), item);
}


// ROM 0x0017a2d0 ContainsOnlyInsertedWhiteSpace__FPUsUl
// Whether text going in is nothing but spaces and returns - more room,
// not writing.
Boolean
ContainsOnlyInsertedWhiteSpace(const UniChar* text, ULong length)
{
	for (ULong i = 0; i < length; i++)
		if (text[i] != kSpaceChar && text[i] != kReturnChar)
			return false;
	return true;
}


// ROM 0x0017af5c FindPreviousWhiteSpaceBlock__FPUsT1PPUsPUl
// Back from `from` (no further than `limit`) to the next block of spaces
// and returns worth taking out: two or more of them, or a single return
// that is not followed by a tab.  ==> whether there is one, its first
// character and its length through `block` and `length`.
Boolean
FindPreviousWhiteSpaceBlock(const UniChar* from, const UniChar* limit,
							const UniChar** block, ULong* length)
{
	while (from >= limit)
	{
		if (*from == kSpaceChar || *from == kReturnChar)
		{
			const UniChar* p = from;
			while (p >= limit && (*p == kSpaceChar || *p == kReturnChar))
				p--;
			*block = p + 1;
			ULong n = (ULong) (from - (p + 1)) + 1;
			*length = n;
			if (n >= 2)
				return true;
			if (n == 1 && **block == kReturnChar && (*block)[1] != kTabChar)
				return true;
			from = p;
		}
		from--;
	}
	return false;
}


// ROM 0x001768d8 AddSpaceToEnd__14TParagraphViewFl
// Returns, and a space after them, put at the end of the text the way a
// word is added (AddWord: a new line), and remembered as an insert area.
// (Nothing in the ROM calls it; it is in the jump table for packages.)
//
// DEVIATION: the ROM builds the characters in a fixed buffer on its stack
// without checking the count, so a count of more than about a hundred
// and fifteen runs over its frame; the host's buffer is made as long as
// it needs to be.
void
TParagraphView::AddSpaceToEnd(long returns)
{
	long count = returns > 0 ? returns : 0;
	UniChar* chars = new UniChar[count + 2];
	if (chars == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	for (long i = 0; i < count; i++)
		chars[i] = kReturnChar;
	chars[count] = kSpaceChar;
	ULong length = (ULong) (returns + 1);
	RefVar text(Text());
	ULong end = (ULong) (Length(text) - 2) >> 1;
	Finder finder;
	SetRect(&finder.fBox, 0, 0, 0, 0);			// (left as the ROM's stack had them)
	finder.fBase.h = finder.fBase.v = 0;
	finder.fText = chars;
	finder.fLength = length;
	finder.fView = this;
	finder.fOffset = (long) end;
	finder.fReplaceLength = 0;
	finder.fExact = false;
	finder.fNewLine = true;
	finder.fReallyDoIt = true;
	finder.fTab = 0;
	finder.fUnit = nil;
	AddWord(&finder, chars, length, RefVar(NILREF), nil);
	SaveInsertArea(fInsertRunList, end, length);
	delete[] chars;
}


// The idler that takes the left-over white space out: from the first
// time something is written into an insert area, the paragraph idles
// every second and a half (reason 1), and Ticks remembers when.
static void
StartInsertAreaIdler(TParagraphView* view, Boolean changed)
{
	if (!view->fInsertAreasChanged)
	{
		if (!changed)
			return;
		view->fInsertAreasChanged = true;
	}
	gRootView->AddIdler(view, 1500, 1);
	view->fInsertAreasTime = Ticks();
}


// ROM 0x001769d4 AdjustInsertAreasAfterDeletion__14TParagraphViewFP13InsertRunListUlT2
// The insert areas after `length` characters went from `offset`: one
// after them moves back, one they reach into shrinks (and counts as
// written into).
//
// ROM BUG: a deletion that swallows the whole of an area leaves its
// length negative, and the check meant to empty it compares the length
// as unsigned, so it never does; the area stays with a huge length.
void
TParagraphView::AdjustInsertAreasAfterDeletion(CList* list, ULong offset, ULong length)
{
	Boolean changed = false;
	TListLoop loop(list);
	InsertRun* run;
	while ((run = (InsertRun*) loop.Next()) != nil)
	{
		ULong start = run->fStart;
		ULong runLength = run->fLength;
		ULong end = runLength + start;
		if (offset < start)
		{
			if (offset + length <= start)
				run->fStart = start - length;
			else
			{
				run->fLength = runLength - (offset + length - start);
				run->fStart = offset;
				run->fChanged = true;
				changed = true;
				// (if (run->fLength < 0) - unsigned, never)
			}
		}
		else if (offset < end)
		{
			long cut = (long) length;
			if (cut >= (long) (end - offset))
				cut = (long) (end - offset);
			run->fLength = runLength - cut;
			run->fChanged = true;
			changed = true;
		}
	}
	if (loop.Count() > 0)
		StartInsertAreaIdler(this, changed);
}


// ROM 0x00176af0 AdjustInsertAreasAfterInsertion__14TParagraphViewFP13InsertRunListUlT2Uc
// The insert areas after `length` characters went in at `offset`: one
// after them moves on, one they go into grows - and counts as written
// into, unless all that went in was more white space.
void
TParagraphView::AdjustInsertAreasAfterInsertion(CList* list, ULong offset, ULong length, Boolean onlyWhiteSpace)
{
	Boolean changed = false;
	TListLoop loop(list);
	InsertRun* run;
	while ((run = (InsertRun*) loop.Next()) != nil)
	{
		ULong start = run->fStart;
		if (offset < start)
			run->fStart = start + length;
		else if (offset < run->fLength + start)
		{
			run->fLength = run->fLength + length;
			if (!onlyWhiteSpace)
			{
				run->fChanged = true;
				changed = true;
			}
		}
	}
	if (loop.Count() > 0)
		StartInsertAreaIdler(this, changed);
}


// ROM 0x0017e5c4 RemoveExcessWhiteSpace__14TParagraphViewFP9InsertRun
// What is left of an insert area once it has been written into: every
// block of white space in it (FindPreviousWhiteSpaceBlock, from the
// character after it back to the one before it) brought down to one
// space - or to one return where the block ends on the return after the
// area, or where it is the return that begins the area's line - each by
// its own replace-text command, so each is undoable.
//
// (The ROM keeps the text locked and holds pointers into it across the
// commands, which only change the text from the block on; the host keeps
// offsets and asks for the characters again after each one, the text
// object being free to move.)
void
TParagraphView::RemoveExcessWhiteSpace(InsertRun* run)
{
	RefVar textRef(Text());
	const UniChar* text = (const UniChar*) BinaryData(textRef);
	long limit = (long) run->fStart - 1;
	long end = (long) (run->fStart + run->fLength);
	Boolean afterReturn = false;
	if (run->fStart == 0)
		limit = 0;
	else
		afterReturn = text[limit] == kReturnChar;
	const UniChar* block;
	ULong length;
	Boolean found = FindPreviousWhiteSpaceBlock(text + end, text + limit, &block, &length);
	while (found)
	{
		long blockStart = block - text;
		const UniChar* replacement = kOneSpace;
		if ((blockStart + (long) length - 1 == end && text[end] == kReturnChar)
		 || (afterReturn && text[blockStart] == kReturnChar && blockStart == limit))
			replacement = kOneNewLine;
		RefVar cmd(MakeCommand(aeReplaceText, this, fId));
		CommandSetIndexParameter(cmd, 0, blockStart);
		CommandSetIndexParameter(cmd, 1, (long) length);
		CommandSetIndexParameter(cmd, 2, 1);
		CommandSetIndexParameter(cmd, 3, 0);
		CommandSetIndexParameter(cmd, 4, 1);
		CommandSetIndexParameter(cmd, 5, 0);
		CommandSetIndexParameter(cmd, 6, 0);
		CommandSetFrameParameter(cmd, RefVar(NILREF));
		CommandSetText(cmd, RefVar(MakeString(replacement, 1)));
		gApplication->DispatchCommand(cmd);
		textRef = Text();
		text = (const UniChar*) BinaryData(textRef);
		found = FindPreviousWhiteSpaceBlock(text + blockStart - 1, text + limit, &block, &length);
	}
}
