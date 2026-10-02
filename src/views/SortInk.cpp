/*
	File:		views/SortInk.cpp

	Contains:	The sort a page puts its selected ink through before it is
				read again: TEditView's double tap on a selection of ink
				(TEditView::RealDoCommand's aeDoubleTap) gathers the
				shapes of old ink as *kids* - frames made by
				MakeKidForSort, each protoed to its view's data and
				numbered by its textFlags - and SortTextInk orders them
				in reading order: into lines by where each one's middle
				falls (FindLine, TestLineOverlap), along a line from left
				to right (FindInsertPosition), a stroke that runs on at
				the end of the last line kept on it (AtEndOfLine).  A dot,
				and a short stroke over a word or an apostrophe beside one
				(TestWordOverlap), is set aside - HandleIgnoreStroke
				numbers it negatively (InvalKidIndex, MapIndex) - and the
				page removes it rather than read it.

				The sort's state (the ROM's SortStuff) is four RefVars:
				the kids in reading order, the index into them each line
				starts at (one more than there are lines), each line's
				rectangle, and the kid the last stroke found was a mark
				over.

	Reconstructed from the MP2x00 US ROM (0x000a1aa4-0x000ab218, among
	TEditView's own); each function cites its origin.
*/

#include "EditView.h"
#include "ParagraphView.h"
#include "Hilites.h"
#include "Rects.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "Ink.h"

struct SortStuff
{
	RefVar	fKids;			// +0x00  the kids in reading order
	RefVar	fLineStarts;	// +0x04  where each line starts in fKids, and its end
	RefVar	fLineRects;		// +0x08  each line's rectangle (a bounds frame)
	RefVar	fMarkHost;		// +0x0c  the kid the stroke just found is a mark over
};


static Rect
KidRect(RefArg bounds)
{
	Rect r;
	FromObject(bounds, r);
	return r;
}


// ROM 0x000a1aa4 AtEndOfLine__FP5TRectT1
// Whether a stroke whose middle is past the line's right edge runs on at
// the end of it: the line made taller by half the stroke's height, and
// then asked whether the stroke's middle falls within it.
static Boolean
AtEndOfLine(Rect* line, Rect* kid)
{
	if (line->right <= ((kid->left + kid->right) >> 1))
	{
		InsetRect(line, 0, -((short) (kid->bottom - kid->top) >> 1));
		if (TestLineOverlap(line, kid) == 1)
			return true;
	}
	return false;
}


// ROM 0x000a2ffc TestLineOverlap__FP5TRectT1
// Where the kid's middle is against the line: 0 above it (a new line goes
// before this one), 1 within it, 2 below it (the lines after are asked).
long
TestLineOverlap(Rect* line, Rect* kid)
{
	long middle = (kid->top + kid->bottom) >> 1;
	if (line->top <= middle)
		return line->bottom < middle ? 2 : 1;
	return 0;
}


// ROM 0x000a3fb0 GetKid__FRC6RefVar
// The data a kid stands for (its _proto).
static Ref
GetKid(RefArg kid)
{
	return GetVariable(kid, RSSYM_proto, nil, 0);
}


// ROM 0x000a3fc4 GetKidBounds__FRC6RefVar
static Ref
GetKidBounds(RefArg kid)
{
	RefVar data(GetKid(kid));
	return GetVariable(data, RSSYMviewbounds, nil, 0);
}


// ROM 0x000a400c GetKidIndex__FRC6RefVar
// The child index a kid was made with (its textFlags; negative once it is
// set aside).
static Ref
GetKidIndex(RefArg kid)
{
	return GetVariable(kid, RSSYMtextflags, nil, 0);
}


// ROM 0x000a4078 MapIndex__Fl
// An index made negative and back: -(i + 1).
long
MapIndex(long index)
{
	return -(index + 1);
}


// ROM 0x000a4020 InvalKidIndex__FRC6RefVar
// The kid set aside: its index made negative.
static void
InvalKidIndex(RefArg kid)
{
	SetVariable(kid, RSSYMtextflags, RefVar(MAKEINT(MapIndex(RINT(GetKidIndex(kid))))));
}


// ROM 0x000a3ef4 MakeKidForSort__FP5TViewl
// A kid for a child view: a clone of canonicalGroupee protoed to the
// view's data, its textFlags the child's index.
Ref
MakeKidForSort(TView* view, long index)
{
	RefVar kid(Clone(RefVar(Rcanonicalgroupee)));
	RefVar data(GetProtoVariable(view->fContext, RSSYMrealdata, nil));
	SetFrameSlot(kid, RSSYM_proto, data);
	SetFrameSlot(kid, RSSYMtextflags, RefVar(MAKEINT(index)));
	return kid;
}


// ROM 0x000a4084 IsOldInk__FP5TView
// Whether a child is ink drawn as a shape (a polygon view with ink).
Boolean
IsOldInk(TView* view)
{
	return view->DerivedFrom(clPolygonView) && NOTNIL(view->GetProto(RSSYMink));
}


// ROM 0x00171290 ContainsHilitedInkWord__FP5TView
// Whether a paragraph's selection has an ink word in it (the text the
// hilite copied).
Boolean
ContainsHilitedInkWord(TView* view)
{
	if (!view->DerivedFrom(clParagraphView))
		return false;
	RefVar first(view->FirstHilite());
	if (ISNIL(first))
		return false;
	const UniChar* text = ((TParagraphHilite*) RefToAddress(first))->fText;
	long length = Ustrlen(text);
	for (long i = 0; i < length && text[i] != 0; i++)
		if (text[i] == kInkWordChar)
			return true;
	return false;
}


// ROM 0x000a3cb0 UpdateLineRect__FP5TRectT1
// A stroke added to a line: its top and bottom moved halfway towards the
// stroke's, its sides out to take it in.
static void
UpdateLineRect(Rect* line, Rect* kid)
{
	line->top = (short) ((line->top + kid->top) >> 1);
	line->bottom = (short) ((line->bottom + kid->bottom) >> 1);
	if (kid->left < line->left)
		line->left = kid->left;
	if (line->right < kid->right)
		line->right = kid->right;
}


// ROM 0x000a3d2c IsDot__FP5TRect
// Less than seven pixels each way.
static Boolean
IsDot(Rect* r)
{
	return (short) (r->right - r->left) < 7 && (short) (r->bottom - r->top) < 7;
}


// ROM 0x000a3d7c IsLine__FP5TRect
// Less than ten pixels tall.
static Boolean
IsLine(Rect* r)
{
	return (short) (r->bottom - r->top) < 10;
}


// ROM 0x000a3da8 IsApostrophe__FP5TRectT1
// A mark smaller each way than half the word's height, right of the word's
// middle, its middle above the word's, reaching below the word's top.
static Boolean
IsApostrophe(Rect* mark, Rect* word)
{
	Point wordMiddle = MidPoint(*word);
	Point markMiddle = MidPoint(*mark);
	long half = (short) (word->bottom - word->top) >> 1;
	return (short) (mark->right - mark->left) < half
		&& (short) (mark->bottom - mark->top) < half
		&& mark->left > wordMiddle.h
		&& markMiddle.v < wordMiddle.v
		&& mark->bottom > word->top;
}


// ROM 0x000a3ebc LeftOf__FP5TRectT1
// Whether a's middle is left of b's.
static Boolean
LeftOf(Rect* a, Rect* b)
{
	return ((a->left + a->right) >> 1) < ((b->left + b->right) >> 1);
}


// ROM 0x000a34d0 TestWordOverlap__FRC6RefVarlP9SortStuff
// A stroke whose middle is within a line, against the line's strokes it
// overlaps side to side: a short one mostly over a stroke, or an
// apostrophe, is a mark over it (3, the stroke remembered); one mostly
// within the stroke's height (grown by both widths) is on the line (0: a
// new line before it, when the stroke is higher - 2 when lower, which
// asks the lines after); otherwise it is in the line (1).
static long
TestWordOverlap(RefArg kid, long line, SortStuff* stuff)
{
	RefVar other(NILREF);
	Long slot = RINT(GetArraySlotRef(stuff->fLineStarts, line));
	Long end = RINT(GetArraySlotRef(stuff->fLineStarts, line + 1));
	Rect mine = KidRect(RefVar(GetKidBounds(kid)));
	for ( ; slot < end; slot++)
	{
		other = GetArraySlotRef(stuff->fKids, slot);
		Rect theirs = KidRect(RefVar(GetKidBounds(other)));
		if (mine.left > theirs.right)
			continue;
		if (mine.right < theirs.left)
			break;
		long covered = CoveredBy(&mine, &theirs);
		if ((IsLine(&mine) && covered > 50) || IsApostrophe(&mine, &theirs))
		{
			stuff->fMarkHost = other;
			return 3;
		}
		Rect taller = theirs;
		InsetRect(&taller, 0, -((short) (mine.right - mine.left) + (short) (theirs.right - theirs.left)));
		if (CoveredBy(&mine, &taller) > 30)
		{
			Point myMiddle = MidPoint(mine);
			Point theirMiddle = MidPoint(theirs);
			return myMiddle.v > theirMiddle.v ? 2 : 0;
		}
	}
	return 1;
}


// ROM 0x000a3330 FindLine__FRC6RefVarPlP9SortStuff
// Which line a stroke belongs to: 0 in line *line, 1 a new line before
// *line (after the last when none is below it), 2 set aside (a dot, or a
// mark over a stroke).
static long
FindLine(RefArg kid, long* line, SortStuff* stuff)
{
	Rect mine = KidRect(RefVar(GetKidBounds(kid)));
	*line = -1;
	if (IsDot(&mine))
		return 2;
	long count = Length(stuff->fLineRects);
	for (long i = 0; i < count; i++)
	{
		Rect lineRect = KidRect(RefVar(GetArraySlotRef(stuff->fLineRects, i)));
		long where = TestLineOverlap(&lineRect, &mine);
		if (where == 1)
			where = TestWordOverlap(kid, i, stuff);
		if (where == 2)
			continue;
		if (where == 0)
		{
			*line = i;
			return 1;
		}
		if (where == 1)
		{
			*line = i;
			return 0;
		}
		if (where == 3)
			return 2;
	}
	*line = count;
	return 1;
}


// ROM 0x000a3b98 FindInsertPosition__FRC6RefVarlP9SortStuff
// Where along the line the stroke goes: before the first whose middle is
// right of its own.
static long
FindInsertPosition(RefArg kid, long line, SortStuff* stuff)
{
	RefVar other;
	Long slot = RINT(GetArraySlotRef(stuff->fLineStarts, line));
	Long end = RINT(GetArraySlotRef(stuff->fLineStarts, line + 1));
	Rect mine = KidRect(RefVar(GetKidBounds(kid)));
	for ( ; slot < end; slot++)
	{
		other = GetArraySlotRef(stuff->fKids, slot);
		Rect theirs = KidRect(RefVar(GetKidBounds(other)));
		if (LeftOf(&mine, &theirs))
			break;
	}
	return slot;
}


// ROM 0x000a3a4c AddToLine__FRC6RefVarlP9SortStuff
// The stroke put into a line: its place along it, the line's rectangle
// grown, the lines after starting one later.
static void
AddToLine(RefArg kid, long line, SortStuff* stuff)
{
	long position = FindInsertPosition(kid, line, stuff);
	Rect lineRect = KidRect(RefVar(GetArraySlotRef(stuff->fLineRects, line)));
	Rect mine = KidRect(RefVar(GetKidBounds(kid)));
	UpdateLineRect(&lineRect, &mine);
	SetArraySlotRef(stuff->fLineRects, line, ToObject(lineRect));
	long count = Length(stuff->fLineStarts);
	for (long i = line + 1; i < count; i++)
		SetArraySlotRef(stuff->fLineStarts, i, MAKEINT(RINT(GetArraySlotRef(stuff->fLineStarts, i)) + 1));
	ArrayInsertAt(stuff->fKids, position, kid);
}


// ROM 0x000aa738 NewLine__FRC6RefVarlP9SortStuff
// A line of its own for the stroke, before line `line`: its start the
// start that line had (the starts from there on one later), its rectangle
// the stroke's own bounds.
static void
NewLine(RefArg kid, long line, SortStuff* stuff)
{
	long count = Length(stuff->fLineStarts);
	RefVar start(GetArraySlotRef(stuff->fLineStarts, line));
	for (long i = line; i < count; i++)
		SetArraySlotRef(stuff->fLineStarts, i, MAKEINT(RINT(GetArraySlotRef(stuff->fLineStarts, i)) + 1));
	ArrayInsertAt(stuff->fLineStarts, line, start);
	ArrayInsertAt(stuff->fLineRects, line, RefVar(GetKidBounds(kid)));
	ArrayInsertAt(stuff->fKids, RINT(start), kid);
}


// ROM 0x000ab218 AtEndOfLine__FRC6RefVarlP9SortStuff
static Boolean
AtEndOfLine(RefArg kid, long line, SortStuff* stuff)
{
	Rect mine = KidRect(RefVar(GetKidBounds(kid)));
	Rect lineRect = KidRect(RefVar(GetArraySlotRef(stuff->fLineRects, line)));
	return AtEndOfLine(&lineRect, &mine);
}


// ROM 0x000a8e80 HandleIgnoreStroke__FRC6RefVarP9SortStuff
// A stroke set aside: noted in the viewBounds slot of the kid it is a mark
// over (an array of indexes), and numbered negatively.
static void
HandleIgnoreStroke(RefArg kid, SortStuff* stuff)
{
	if (NOTNIL(stuff->fMarkHost))
	{
		RefVar marks(GetFrameSlotRef(stuff->fMarkHost, RSSYMviewbounds));
		if (!IsArray(marks))
		{
			marks = MakeArray(0);
			SetFrameSlot(stuff->fMarkHost, RSSYMviewbounds, marks);
		}
		AddArraySlot(marks, RefVar(GetKidIndex(kid)));
	}
	InvalKidIndex(kid);
}


// ROM 0x000a8220 SortTextInk__FRC6RefVar
// The kids' child indexes in reading order, then the negative ones of the
// strokes set aside.
Ref
SortTextInk(RefArg kids)
{
	RefVar kid;
	SortStuff stuff;
	stuff.fKids = MakeArray(0);
	stuff.fLineStarts = MakeArray(0);
	AddArraySlot(stuff.fLineStarts, RefVar(MAKEINT(0)));
	stuff.fLineRects = MakeArray(0);
	stuff.fMarkHost = NILREF;
	kid = GetArraySlotRef(kids, 0);
	NewLine(kid, 0, &stuff);
	long count = Length(kids);
	for (long i = 1; i < count; i++)
	{
		kid = GetArraySlotRef(kids, i);
		stuff.fMarkHost = NILREF;
		long line;
		long where = FindLine(kid, &line, &stuff);
		long last = Length(stuff.fLineRects) - 1;
		if (AtEndOfLine(kid, last, &stuff) && (where != 0 || line == last))
		{
			where = 0;
			line = last;
		}
		if (where == 0)
			AddToLine(kid, line, &stuff);
		else if (where == 1)
			NewLine(kid, line, &stuff);
		else if (where == 2)
			HandleIgnoreStroke(kid, &stuff);
	}
	RefVar order(MakeArray(0));
	RefVar index;
	long sorted = Length(stuff.fKids);
	for (long i = 0; i < sorted; i++)
	{
		kid = GetArraySlotRef(stuff.fKids, i);
		AddArraySlot(order, RefVar(GetKidIndex(kid)));
	}
	for (long i = 0; i < count; i++)
	{
		kid = GetArraySlotRef(kids, i);
		index = GetKidIndex(kid);
		if (RINT(index) < 0)
			AddArraySlot(order, index);
	}
	return order;
}
