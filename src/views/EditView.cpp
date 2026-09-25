/*
	File:		views/EditView.cpp

	Contains:	TEditView (EditView.h) - the editor's frame and the hilites
				of its children.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The editor keeps no hilite of its own: every question about the
	selection is put to the children that are hilited themselves, and the
	rectangle they are gathered into starts with its top and bottom at
	-32768, which makes it empty so that the first one replaces it and
	leaves a marker the callers test for.
*/

#include "EditView.h"
#include "CorrectInfo.h"
#include "Ink.h"
#include "RecConfig.h"		// GetRecognitionView
#include "InkShapes.h"
#include "StrokeBundle.h"
#include "DrawShape.h"
#include "Locale.h"			// GetPreference
#include "Hilites.h"
#include "DataView.h"
#include "ParagraphView.h"
#include "RootView.h"
#include "Application.h"
#include "RichString.h"
#include "Text.h"
#include "Fonts.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NewtonTime.h"
#include "Keyboard.h"
#include "Commands.h"
#include "UnitPublic.h"
#include "Unit.h"
#include "StrokeQueue.h"
#include "RecConfig.h"
#include "Bits.h"
#include "ViewFlags.h"
#include "Rects.h"
#include "Animate.h"
#include "ROMConstants.h"
#include "Regions.h"
#include "RegionVars.h"
#include "Locale.h"
#include "Draw.h"
#include "Ports.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"


// what a gathering rectangle starts as, and how the callers know nothing
// went into it
enum { kNoBounds = -32768 };

static void
StartGathering(Rect* bounds)
{
	bounds->top = kNoBounds;
	bounds->bottom = kNoBounds;
}

static inline Boolean
GatheredNothing(const Rect* bounds)
{
	return bounds->top == kNoBounds;
}


// ROM 0x000a3498 ToOutsideGrayBorder__FP5TRectPC5TRect
void
ToOutsideGrayBorder(Rect* r, const Rect* limit)
{
	InsetRect(r, -12, -12);
	if (limit == nil)
		return;
	if (r->left < limit->left)
		r->left = limit->left;
	if (limit->right < r->right)
		r->right = limit->right;
	if (r->top < limit->top)
		r->top = limit->top;
	if (limit->bottom < r->bottom)
		r->bottom = limit->bottom;
}


/*------------------------------------------------------------------------------
	T E d i t V i e w
------------------------------------------------------------------------------*/

// ROM 0x000a1a68 ClassID__9TEditViewCFv
long	TEditView::ClassID(void) const		{ return clEditView; }


// ROM 0x000a1a70 DerivedFrom__9TEditViewCFl
// TView's, not TContainerView's: an edit view answers the same questions as
// a container but is not one.
Boolean
TEditView::DerivedFrom(long id) const
{
	return id == clEditView || TView::DerivedFrom(id);
}


// ROM 0x000a41f4 Constructor__9TEditViewFRC6RefVarP5TView
void
TEditView::Constructor(RefArg context, TView* parent)
{
	fTextFlags = 0;
	fHilitingChildren = false;
	TView::Constructor(context, parent);
}


// ROM 0x000a648c SetupDone__9TEditViewFv
// The line spacing the new paragraphs are aligned to, the text flags a
// child paragraph inherits, no caret yet, and a selection that may not be
// resized until something says otherwise.
void
TEditView::SetupDone(void)
{
	RefVar spacing(GetVar(RSSYMviewlinespacing));
	fLineSpacing = (short) (ISNIL(spacing) ? 0 : RINT(spacing));
	fTextFlags = (long) GetInputViewTextFlags((ULong) TextFlags(), fFlags);
	fClickOptions = ~2;
	fTapPending = false;
	StartGathering(&fCaretRect);
	TView::SetupDone();
}


// ROM 0x000a4170 HasHilitedChildren__9TEditViewFlPP5TView
// Whether at least `atLeast` children are hilited; `first` comes back as the
// last one counted, when it is wanted.
Boolean
TEditView::HasHilitedChildren(long atLeast, TView** first)
{
	long count = 0;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->Hilited())
		{
			count++;
			if (first != nil)
				*first = child;
		}
		if (count >= atLeast)
			break;
	}
	return count >= atLeast;
}


// ROM 0x000a7abc CountHilites__9TEditViewFv
long
TEditView::CountHilites(void)
{
	long count = 0;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->Hilited())
			count++;
	}
	return count;
}


// ROM 0x000a7a54 PointInHilite__9TEditViewFR6TPoint
Boolean
TEditView::PointInHilite(Point& pt)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->PointInHilite(pt))
			return true;
	}
	return false;
}


// ROM 0x000a751c HiliteAll__9TEditViewFv
// Every child selects the whole of itself, and the caret goes wherever the
// selection leaves it.
void
TEditView::HiliteAll(void)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
		child->HiliteAll();
	DetermineKeyView();
}


// ROM 0x000a7574 RemoveAllHilites__9TEditViewFv
void
TEditView::RemoveAllHilites(void)
{
	InvalAllHilites();
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
		child->RemoveAllHilites();
	if (gRootView->fHiliter == this)
	{
		fClickOptions = ~2;					// nothing to resize any more
		gRootView->fHiliter = nil;
	}
}


// ROM 0x000a6fb0 AddHiliter__9TEditViewFP11TUnitPublic +0xf0
// How many children carry a selection.  (The ROM has this inline twice
// in AddHiliter, and it is not TEditView::CountHilites: it looks the
// `hilites` slot up in the child's own frame rather than asking the
// child, and does not care whether the array is empty.)
static long
CountHilitedChildren(TViewList* children)
{
	long count = 0;
	TViewLoop loop(children);
	TView* child;
	while ((child = loop.Next()) != nil)
		if (NOTNIL(GetFrameSlotRef(child->fContext, RSSYMhilites)))
			count++;
	return count;
}


// ROM 0x000a6fb0 AddHiliter__9TEditViewFP11TUnitPublic
// A hilite stroke over a page.  The stroke is judged first - a lasso
// goes round something rather than through it - and a lasso drawn on the
// Calendar is not a selection at all: that application answers it by
// redrawing itself.
//
// Otherwise the children are asked what the stroke would select and the
// strongest claim is carried out, exactly as TView::AddHiliter does it;
// the page then works out where its caret belongs again.  What is left
// is the *click options* the selection offers: a lasso, or a selection
// that has come to cover more than one child, may be resized (all the
// options, -1) unless the children's own bounds say otherwise; anything
// else may not (~2).  The selection's frame is then dirtied so that it
// is drawn - with nothing to draw and nothing to rub out, a page whose
// children were not selected before and are not selected now stops here.
Boolean
TEditView::AddHiliter(TUnitPublic* unit)
{
	Boolean lasso = IsLassoStroke(unit);
	if (lasso && EQ(RefVar(GetVariable(fContext, RSSYMappsymbol, nil, 0)), RSSYMcalendar))
	{
		Dirty(nil);
		return true;
	}
	long before = CountHilitedChildren(fChildren);
	long kind = 0;
	{
		TViewLoop loop(fChildren);
		TView* child;
		while ((child = loop.Next()) != nil)
		{
			long claim = child->HandleHilite(unit, lasso ? 1 : -1, false);
			if (claim > kind)
				kind = claim;
		}
	}
	fHilitingChildren = true;
	if (kind != 0)
	{
		TViewLoop loop(fChildren);
		TView* child;
		while ((child = loop.Next()) != nil)
			child->HandleHilite(unit, kind, true);
	}
	fHilitingChildren = false;
	DetermineKeyView();
	long after = CountHilitedChildren(fChildren);
	Rect box;
	if (lasso || after > 1)
	{
		fClickOptions = -1;
		if ((GlobalHiliteBounds(&box) & 2) == 0)
			fClickOptions = ~2;
	}
	else
	{
		fClickOptions = ~2;
		if (before == 0)
			return true;
	}
	GlobalHiliteResizeBounds(&box);
	ToOutsideGrayBorder(&box, &viewBounds);
	Dirty(&box);
	return true;
}


// ROM 0x000a7388 DetermineKeyView__9TEditViewFv
// Where the caret goes after a selection changed: into the one selected
// paragraph when that is all there is, else onto the editor itself with the
// number of selected children as the length.
void
TEditView::DetermineKeyView(void)
{
	TView* keyView = nil;
	long count = 0;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (ISNIL(GetFrameSlotRef(child->fContext, RSSYMhilites)))
			continue;
		count++;
		if (!child->DerivedFrom(clDataView))
			continue;
		TView* hiliteView = ((TDataView*) child)->GetHiliteView();
		if (keyView == nil && hiliteView != nil && hiliteView->DerivedFrom(clParagraphView))
			keyView = hiliteView;
	}
	if (count == 1 && keyView != nil)
	{
		RefVar first(keyView->FirstHilite());
		if (NOTNIL(first))
		{
			TParagraphHilite* hilite = (TParagraphHilite*) RefToAddress(first);
			gRootView->SetKeyView(keyView, hilite->fStart, hilite->fEnd - hilite->fStart, false);
		}
		return;
	}
	gRootView->SetKeyView(this, 0, count, false);
}


// ROM 0x000a77cc GlobalHiliteBounds__9TEditViewFP5TRect
// The hilited children's bounds, gathered; the answer is the click options
// they have in common - every bit AND-ed except bit 2, which is OR-ed -
// through the editor's own mask.  Nothing selected: the bounds keep their
// marker and the answer is 0.
long
TEditView::GlobalHiliteBounds(Rect* bounds)
{
	StartGathering(bounds);
	long options = ~4;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (!child->Hilited())
			continue;
		long theirs = child->GlobalHiliteBounds(bounds);
		options = (theirs & 4) | ((theirs | 4) & options);
	}
	options &= fClickOptions;
	if (GatheredNothing(bounds))
		options = 0;
	return options;
}


// ROM 0x000a788c GlobalSelectedBounds__9TEditViewFP5TRect
// Not the selections but the hilited children themselves: the union of
// their view bounds.
void
TEditView::GlobalSelectedBounds(Rect* bounds)
{
	StartGathering(bounds);
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (!child->Hilited())
			continue;
		if (GatheredNothing(bounds))
			*bounds = child->viewBounds;
		else
		{
			if (child->viewBounds.left < bounds->left)
				bounds->left = child->viewBounds.left;
			if (child->viewBounds.top < bounds->top)
				bounds->top = child->viewBounds.top;
			if (bounds->right < child->viewBounds.right)
				bounds->right = child->viewBounds.right;
			if (bounds->bottom < child->viewBounds.bottom)
				bounds->bottom = child->viewBounds.bottom;
		}
	}
}


// ROM 0x000a7990 GlobalHiliteResizeBounds__9TEditViewFP5TRect
// Every child, hilited or not - a resize is bounded by what is around it.
void
TEditView::GlobalHiliteResizeBounds(Rect* bounds)
{
	StartGathering(bounds);
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
		child->GlobalHiliteResizeBounds(bounds);
}


// ROM 0x000a7a00 GlobalHilitePinnedBounds__9TEditViewFP5TRect
// The caller's rectangle is added to, not started again.
void
TEditView::GlobalHilitePinnedBounds(Rect* bounds)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
		child->GlobalHilitePinnedBounds(bounds);
}


// ROM 0x000a6270 InvalAllHilites__9TEditViewFv
// A resizable selection is drawn with a border outside the children, so
// what has to be redrawn is more than the children themselves.
void
TEditView::InvalAllHilites(void)
{
	Rect bounds;
	long options = GlobalHiliteBounds(&bounds);
	if (GatheredNothing(&bounds) || (options & 2) == 0)
		return;
	GlobalHiliteResizeBounds(&bounds);
	ToOutsideGrayBorder(&bounds, &viewBounds);
	Dirty(&bounds);
}


// ROM 0x000a6010 DirtyBoxHilites__9TEditViewFv
void
TEditView::DirtyBoxHilites(void)
{
	if (gRootView->fHiliter != this)
		return;
	Rect bounds;
	long options = GlobalHiliteBounds(&bounds);
	if (GatheredNothing(&bounds) || (options & 2) == 0)
		return;
	Dirty(nil);
}


// ROM 0x000a6310 DrawHilitedData__9TEditViewFv
// The hilited children draw themselves again, and the pen is left as the
// caller found it.
void
TEditView::DrawHilitedData(void)
{
	PenNormal();
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->Hilited())
			child->DrawHilitedData();
	}
	PenSize(1, 1);
	PenMode(srcXor);
	SetFgPattern(GetStdPattern(blackPat));	// the ROM writes stdPatterns[4] into the port
}


// ROM 0x000a609c DrawHiliting__9TEditViewFv
// Each hilited child draws its hilites twice, scaled false and then true:
// the ROM's two passes, the second being what a view that draws its
// selection differently when scaled uses.  NOT YET: the resize border round
// a resizable selection (DrawResizeBorder over gEditViewTransform).
void
TEditView::DrawHiliting(void)
{
	if ((fFlags & vClipping) != 0)
		ClipRect(&viewBounds);
	PenNormal();
	Rect bounds;
	long options = GlobalHiliteBounds(&bounds);
	if (!GatheredNothing(&bounds))
	{
		TListLoop first(fChildren);
		TView* child;
		while ((child = (TView*) first.Next()) != nil)
		{
			if (child->Hilited())
				child->DrawHilites(false);
		}
		TListLoop second(fChildren);
		while ((child = (TView*) second.Next()) != nil)
		{
			if (child->Hilited())
				child->DrawHilites(true);
		}
		if ((options & 2) != 0)
		{
			// NOT YET: GlobalHiliteResizeBounds, scaled through
			// gEditViewTransform, drawn by DrawResizeBorder
		}
		if ((fFlags & vClipping) != 0)
		{
			Rect wide;
			SetRect(&wide, -32767, -32767, 32766, 32766);
			ClipRect(&wide);
		}
		PenNormal();
	}
}


// ROM 0x000a5eb8 PostDraw__9TEditViewFR5TRect
// The hiliting is drawn into an offscreen map and blitted over the view, so
// that inverting it twice does not leave the children drawn twice.
void
TEditView::PostDraw(Rect& drawBounds)
{
	TView::PostDraw(drawBounds);
	if (gDontDrawHilites || gRootView->fHiliter != this || Printing())
		return;
	Rect bounds;
	long options = GlobalHiliteBounds(&bounds);
	if (GatheredNothing(&bounds))
		return;
	if ((options & 2) != 0)
	{
		GlobalHiliteResizeBounds(&bounds);
		ToOutsideGrayBorder(&bounds, &viewBounds);
	}
	GrafPort* port = GetCurrentPort();
	TBits bits;
	if (bits.Constructor(port->portRect))
	{
		Point origin;
		origin.h = 0;
		origin.v = 0;
		bits.BeginDrawing(origin);
		DrawHiliting();
		bits.EndDrawing();
		bits.Draw(bounds, bounds, srcXor, nil);
	}
}

// ROM 0x000aa860 ActivateSelection__9TEditViewFUc
// Losing the caret loses the selection with it.
void
TEditView::ActivateSelection(Boolean on)
{
	TView::ActivateSelection(on);
	if (!on)
		RemoveAllHilites();
}


// ROM 0x000ab9b4 BuildKeyChildList__9TEditViewFP9TViewListlT2
// The editor itself takes the caret as well as its children do - tab
// stops on the page, not only on what is written on it - unless it is
// read-only.
void
TEditView::BuildKeyChildList(TViewList* list, long a, long b)
{
	TView::BuildKeyChildList(list, a, b);
	if (a == 0 && (fFlags & vReadOnly) == 0)
	{
		TView* self = this;
		list->InsertElementsBefore(list->GetArraySize(), &self, 1);
	}
}


// ROM 0x000aa894 SetCaretRectLocal__9TEditViewFRC5TRect
void
TEditView::SetCaretRectLocal(const Rect& r)
{
	fCaretRect = r;
}


// ROM 0x000aa8a4 SetCaretRectGlobal__9TEditViewFRC5TRect
// The same, given in the coordinates the view is scrolled to.
void
TEditView::SetCaretRectGlobal(const Rect& r)
{
	fCaretRect = r;
	Point origin = ContentsOrigin();
	OffsetRect(&fCaretRect, -origin.h, -origin.v);
}


// ROM 0x000aa92c GetCaretLocalTopLeft__9TEditViewFv
Point
TEditView::GetCaretLocalTopLeft(void)
{
	Point pt;
	pt.v = fCaretRect.top;
	pt.h = fCaretRect.left;
	return pt;
}


// ROM 0x000aa938 GetCaretGlobalTopLeft__9TEditViewFv
Point
TEditView::GetCaretGlobalTopLeft(void)
{
	Point origin = ContentsOrigin();
	Point pt;
	pt.v = (short) (fCaretRect.top + origin.v);
	pt.h = (short) (fCaretRect.left + origin.h);
	return pt;
}

// ROM 0x000a2ee4 OffsetToCaret__9TEditViewFlP5TRect
// Where the caret is, in the coordinates the view is scrolled to.  The
// offset is the paragraph's way of asking and means nothing here: the
// editor has one caret rectangle, wherever it was last put.
void
TEditView::OffsetToCaret(long /*offset*/, Rect* caret)
{
	if (fCaretRect.top == kNoBounds)
	{
		StartGathering(caret);
		return;
	}
	*caret = fCaretRect;
	Point origin = ContentsOrigin();
	OffsetRect(caret, origin.h, origin.v);
}


// ROM 0x000aae90 GetHilitedViewsSorted__9TEditViewFv
// The selected children in reading order: down the page, and within
// twelve pixels of the same top, left to right.  The array is the
// caller's to delete[]; nil when nothing is selected.
TView**
TEditView::GetHilitedViewsSorted(void)
{
	long count = CountHilites();
	if (count == 0)
		return nil;
	TView** sorted = new TView*[count];
	if (sorted == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	long found = 0;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (!child->Hilited())
			continue;
		long at = 0;
		while (at < found)
		{
			long down = sorted[at]->viewBounds.top - child->viewBounds.top;
			Boolean before = down > 12;
			if (!before)
			{
				long apart = down < 0 ? -down : down;
				before = apart < 13 && child->viewBounds.left < sorted[at]->viewBounds.left;
			}
			if (before)
				break;
			at++;
		}
		for (long i = found; i > at; i--)
			sorted[i] = sorted[i - 1];
		sorted[at] = child;
		found++;
	}
	return sorted;
}


// ROM 0x000ab60c MoveBetweenParagraphs__9TEditViewFlT1
// The paragraph nearest above (direction -1) or below (+1) the line v,
// which is how the up and down arrows leave one paragraph for the next.
TView*
TEditView::MoveBetweenParagraphs(long v, long direction)
{
	TView* best = nil;
	for (ArrayIndex i = 0; i < fChildren->GetArraySize(); i++)
	{
		TView* child = fChildren->At((short) i);
		if (!child->DerivedFrom(clParagraphView))
			continue;
		long top = child->viewBounds.top;
		if (direction == -1)
		{
			if (top < v && (best == nil || best->viewBounds.top < top))
				best = child;
		}
		else if (direction == 1)
		{
			if (v < top && (best == nil || top < best->viewBounds.top))
				best = child;
		}
	}
	return best;
}

// ROM 0x000a2bc4 AlignToLineSpacing__9TEditViewFP5TRectlT2
// A new paragraph's rectangle moved onto the ruled lines: its baseline
// (top + ascent) onto the nearest line, and its left onto the square grid
// when the view has one.  A line is chosen by rounding two thirds of the
// way down - so a baseline a little below a line still belongs to it -
// and the text then sits three pixels above the line, four on a wide
// spacing.  A clipboard is left alone.
void
TEditView::AlignToLineSpacing(Rect* r, long top, long ascent)
{
	if ((fFlags & vClipboard) != 0)
		return;
	long left = r->left;
	RefVar lineSpacing(GetVar(RSSYMviewlinespacing));
	long spacing = ISNIL(lineSpacing) ? 0 : RINT(lineSpacing);
	if (spacing > 0)
	{
		Point grid;
		if (IsGridded(RSSYMsquaregrid, &grid) && grid.h != 0)
			left = (left + grid.h / 2) / grid.h * grid.h;
		long line = (top + (spacing * 2) / 3) / spacing;
		top = spacing * line - (spacing <= 20 ? 3 : 4);
	}
	long down = top - (ascent + r->top);
	long across = left - r->left;
	if (down != 0 || across != 0)
		OffsetRect(r, across, down);
}

/* -------------------------------------------------------------------------------
	T h e   c a r e t
------------------------------------------------------------------------------- */

// ROM 0x0c100cf0 gAboutToOpenSoftKeyboard
Boolean	gAboutToOpenSoftKeyboard = false;

// ROM 0x0c100ce0 gLassoedDrag
Boolean	gLassoedDrag = false;


// ROM 0x002628c8 AlignToGrid__FlT1
// The value moved to the nearest multiple of the grid, rounding to the
// nearer (half a grid is added before the division); a grid of nothing
// leaves the value as it was.  The answer is cut to a short, as every
// coordinate in the view system is.
long
AlignToGrid(long v, long grid)
{
	long result = v;
	if (grid != 0)
		result = grid * ((v + (grid >> 1)) / grid);
	return (short) result;
}


// ROM 0x001a2aa4 TextOrInkWordsEnabled__FP5TView
// Whether the view takes words from the recogniser: bit 0 when ink words
// are wanted, bit 1 when text is.  The view its recognition settings
// belong to (GetRecognitionView - a paragraph's are the page's) is asked
// for a configuration built out of its flags (BuildRecConfig), and
// doInkWordRecognition and doTextRecognition are read out of it.
//
// Text is decided twice over: a view that allows everything is asked its
// configuration, because the answer is a preference the user can turn
// off; a view that allows only some things is not asked at all, because
// a date field takes text whatever the preference says.
//
// This is what lets a tap on the empty part of a page open a caret to
// type or write at: PositionCaret does nothing at all on a view that
// takes neither.
long
TextOrInkWordsEnabled(TView* view)
{
	long enabled = 0;
	TView* recView = GetRecognitionView(view);
	ULong flags = recView->fFlags & vRecognitionAllowed;
	Boolean anything = (flags & vAnythingAllowed) == vAnythingAllowed;
	RefVar config(BuildRecConfig(recView, flags));
	if (NOTNIL(GetVariable(config, RSSYMdoinkwordrecognition, nil, 0)))
		enabled = 1;
	if (anything)
	{
		if (NOTNIL(GetVariable(config, RSSYMdotextrecognition, nil, 0)))
			enabled |= 2;
	}
	else if ((flags & (vCharsAllowed | vNumbersAllowed | vLettersAllowed
					   | vPunctuationAllowed | vMathAllowed | vPhoneField
					   | vDateField | vTimeField | vAddressField | vNameField
					   | vCustomDictionaries)) != 0)
		enabled |= 2;
	return enabled;
}


// ROM 0x000a8844 TextContainingPoint__9TEditViewFR6TPointP5TRectPl
// The child whose text the point falls in.  Each child that is visible
// and holds data is asked how well it would take a single letter written
// at the point - HandleWord with an 'A' and no unit, which is the
// question the recogniser would ask - and the best answer wins.
//
// ==> that child, with `score` left holding its answer: 0 nobody wants
// the point, 1 it is in a view's text, 2 it is in one only just.  A score
// of 0 answers no view, and so does a score of 2 while a lasso drag is
// going on, because the point belongs to the drag.
//
// `box` is how big the thing written at the point is; with none it is a
// single pixel.
TView*
TEditView::TextContainingPoint(Point& pt, Rect* box, long* score)
{
	long best = 0;
	TView* bestView = nil;		// (the ROM leaves the register as it found it,
								//  which is safe: a best of 0 answers nothing)
	UniChar letter[2];
	letter[0] = 'A';
	letter[1] = 0;
	Rect at;
	at.top = pt.v;
	at.left = pt.h;
	at.bottom = pt.v + 1;
	at.right = box == nil ? pt.h + 1 : pt.h + (box->right - box->left);
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if ((child->fFlags & vVisible) != 0 && child->DerivedFrom(clDataView))
		{
			RefVar word;
			long wants = ((TDataView*) child)->HandleWord(letter, 1, at, pt, 0, 0,
														  word, false, nil, nil);
			if (best < wants)
			{
				best = wants;
				bestView = child;
			}
		}
	}
	if (score != nil)
		*score = best;
	if (best == 0 || (best == 2 && gLassoedDrag))
		bestView = nil;
	return bestView;
}

// ROM 0x000a9fb0 PositionCaret__9TEditViewFR6TPointUc
// The caret put where the point says, in the coordinates the page is
// scrolled to.  Three things can happen:
//
//   The point is in a child's text (score 1).  That child becomes the key
//   view with the caret at the character the point falls on - before the
//   first when the point is above the child, after the last when it is
//   below - and the editor itself is left alone.
//
//   The point is in a child's text but only just (score 2), or there is
//   no text there at all and the child says so.  The caret goes just
//   under that child, where the next line would start, and the editor
//   becomes the key view.
//
//   There is no text anywhere near.  The caret is made from the view's
//   own text style - as tall as the font's ascent and two pixels wide -
//   at the point, lined up with the page's ruled grid, and the editor
//   becomes the key view.  A page that takes neither text nor ink words,
//   and has no keyboard up, does nothing at all here.
//
// `click` asks for the click the machine makes when the caret moves.
//
// NOT YET RECONSTRUCTED: FClicker 0x001e6578, the click itself.
void
TEditView::PositionCaret(Point& pt, Boolean click)
{
	RemoveAllHilites();
	if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
		return;
	long score = 0;
	TView* container = TextContainingPoint(pt, nil, &score);
	TParagraphView* para = container != nil
						 ? (TParagraphView*) ((TDataView*) container)->GetTextView() : nil;
	Boolean underTheChild = false;
	if (para != nil && (para->fFlags & (vReadOnly | vWriteProtected)) == 0 && score > 0)
	{
		if (score != 2)
		{
			// the point is on a character of the child's text
			para->RemoveAllHilites();
			long offset = para->PointToOffset(pt);
			if (offset < 0)
				offset = para->viewBounds.top > pt.v
					   ? 0 : (Length(RefVar(para->Text())) - 2) / 2;
			gRootView->SetKeyView(para, offset, 0, false);
			if (click && gRootView->CaretEnabled())
				;	// NOT YET RECONSTRUCTED: FClicker(nil)
			return;
		}
		underTheChild = true;
	}
	else if (score == 2)
		underTheChild = true;

	if (underTheChild)
	{
		// just under the child, where the line after it would start
		Rect caret = para->viewBounds;
		pt.v = caret.bottom + 5;
		para->PointToCaret(pt, &caret, nil);
		SetCaretRectGlobal(caret);
	}
	else
	{
		if (!gAboutToOpenSoftKeyboard && TextOrInkWordsEnabled(this) == 0
			&& !gRootView->KeyboardActive())
			return;		// the page takes no writing and nothing is typing at it
		if (fLineSpacing != 0)
		{
			// lined up with the page's ruling, in the view's own coordinates
			short height = (short) (viewBounds.bottom - viewBounds.top);
			Point origin = ContentsOrigin();
			pt.v -= origin.v;
			pt.h -= origin.h;
			short was = pt.v;
			pt.v = (short) AlignToGrid(pt.v, fLineSpacing);
			// the first line when the point rounded above the page, and the
			// point itself when even the first line would be off the bottom
			if (pt.v == 0)
			{
				pt.v = fLineSpacing <= height ? fLineSpacing : was;
			}
			else if (pt.v > height && fLineSpacing > height)
				pt.v = was;
			pt.v -= 4;
			origin = ContentsOrigin();
			pt.v += origin.v;
			pt.h += origin.h;
		}
		// as tall as the font's ascent and two pixels wide
		StyleRecord style;
		FontInfo font;
		style.fPattern = nil;		// (the ROM clears it before asking, and
									//  disposes whatever came back)
		GetTextStyleRecord(&style);
		GetStyleFontInfo(&style, &font);
		Rect caret;
		caret.top = (short) -font.ascent;
		caret.left = 0;
		caret.bottom = 0;
		caret.right = 2;
		OffsetRect(&caret, pt.h, pt.v);
		SetCaretRectGlobal(caret);
		if (style.fPattern != nil)
			DisposePattern(style.fPattern);
	}
	gRootView->SetKeyView(this, 0, 0, true);
	if (click && gRootView->CaretEnabled())
		;	// NOT YET RECONSTRUCTED: FClicker(nil)
}

// ROM 0x000aaba4 HandleTap__9TEditViewFR6TPoint
// A tap on the page puts the caret where it was.
void
TEditView::HandleTap(Point& pt)
{
	PositionCaret(pt, true);
}


// ROM 0x000a9f64 Idle__9TEditViewFl
// Reason 2 is the tap waiting to become a caret: a tap arrives, the
// editor remembers where it was and asks the root view to come back after
// the double-tap interval, and if nothing has taken the tap away by then
// it is a single tap and the caret goes there.  Any other reason is the
// view's own viewIdleScript, whose answer is when to idle next.
long
TEditView::Idle(long reason)
{
	if (reason != 2)
	{
		RefVar result(RunCacheScript(kIndexViewIdleScript, RefVar(NILREF), false));
		return (NOTNIL(result) && ISINT(result)) ? RINT(result) : 0;
	}
	if (fTapPending)
		HandleTap(fTapPoint);
	fTapPending = false;
	return 0;
}


// ROM 0x000ab490 ValidTextEditCaret__FP11TUnitPublic
// Which of the caret family a text editor will take, and pointing which
// way: the plain caret (2) up, right or down; the one with a tail (3) up
// or right; the open one (5) up; and the flat one (6) leaning at 135.
Boolean
ValidTextEditCaret(TUnitPublic* unit)
{
	long kind = unit->CaretType();
	long angle = unit->GestureAngle();
	switch (kind)
	{
	case 2:		return angle == 0 || angle == 90 || angle == 180;
	case 3:		return angle == 0 || angle == 90;
	case 5:		return angle == 0;
	case 6:		return angle == 135;
	default:	return false;
	}
}


// ROM 0x000ab6dc ValidLineGesture__FP11TUnitPublic
// A line an editor takes is one of the four square ones.
Boolean
ValidLineGesture(TUnitPublic* unit)
{
	long angle = unit->GestureAngle();
	return angle == 0 || angle == 180 || angle == 90 || angle == -90;
}


// ROM 0x000ab334 HandleCaret__9TEditViewFP11TUnitPublic
// A caret gesture on the page offered to each visible child that holds
// data, with the gesture's kind, its angle and the corners of its
// polyline: the point, the two arms, and - for the kinds that have one -
// the tail.  The first child that takes it wins, and the page's hilites
// go.
long
TEditView::HandleCaret(TUnitPublic* unit)
{
	if (!ValidTextEditCaret(unit))
		return 0;
	ULong kind = (ULong) unit->CaretType();
	long angle = unit->GestureAngle();
	Point point = unit->GesturePoint(0);
	Point armA = unit->GesturePoint(1);
	Point armB = unit->GesturePoint(2);
	Point tail;
	if (kind == 3 || kind == 5)
		tail = unit->GesturePoint(3);
	else
	{
		tail.v = (short) 0x8000;		// no tail
		tail.h = 0;
	}
	long done = 0;
	TListLoop loop(fChildren);
	for (TView* child = (TView*) loop.Next(); child != nil; child = (TView*) loop.Next())
		if ((child->fFlags & vVisible) != 0 && child->DerivedFrom(clDataView)
			&& ((TDataView*) child)->HandleCaret(kind, angle, point, armA, armB, tail))
		{
			done = 1;
			break;
		}
	if (done != 0)
		RemoveAllHilites();
	return done;
}


// ROM 0x000ab528 HandleLineGesture__9TEditViewFP11TUnitPublic
// A line gesture offered the same way, with its angle and its two ends.
long
TEditView::HandleLineGesture(TUnitPublic* unit)
{
	long angle = unit->GestureAngle();
	Point from = unit->GesturePoint(0);
	Point to = unit->GesturePoint(1);
	if (!ValidLineGesture(unit))
		return 0;
	TListLoop loop(fChildren);
	for (TView* child = (TView*) loop.Next(); child != nil; child = (TView*) loop.Next())
		if ((child->fFlags & vVisible) != 0 && child->DerivedFrom(clDataView)
			&& ((TDataView*) child)->HandleLineGesture(angle, from, to))
			return 1;
	return 0;
}


// ROM 0x000a8750 DeleteHilitedViews__9TEditViewFv
// Every hilited child of the page deleted, one at a time: each round
// looks for the first child that still has a hilite and tells it to
// delete it, until none is left.  The children are looked for again each
// round because deleting one can take others with it.  The caret goes to
// the top of the page afterwards.
//
// NOT YET RECONSTRUCTED: SetCorrectorBusy/RestoreCorrectorBusy, which
// keep the corrector from following the text that is going away.
void
TEditView::DeleteHilitedViews(void)
{
	InvalAllHilites();
	for (;;)
	{
		TView* hilited = nil;
		RefVar hilite;
		TListLoop loop(fChildren);
		for (TView* child = (TView*) loop.Next(); child != nil; child = (TView*) loop.Next())
		{
			hilite = child->FirstHilite();
			if (NOTNIL(hilite))
			{
				hilited = child;
				break;
			}
		}
		if (hilited == nil)
			break;
		hilited->DeleteHilited(hilite);
	}
	gRootView->SetKeyView(this, 0, 0, false);
	gRootView->fDirtyFlag = true;
}


// ROM 0x000a75f4 ScrubHilite__9TEditViewFRC5TRect
// Whether the scrub went over the selection - in which case the selection
// is what it takes out, whatever else it covers.  A page whose selection
// may be resized is asked for the selection's bounds as a whole (the grey
// border round it counts); otherwise every hilite of every child is asked
// whether the scrub overlaps it, in that child's own coordinates.
Boolean
TEditView::ScrubHilite(const Rect& bounds)
{
	if ((fClickOptions & 2) != 0)
	{
		Rect selection;
		SetRect(&selection, -0x8000, -0x8000, -0x8000, -0x8000);
		GlobalHiliteResizeBounds(&selection);
		ToOutsideGrayBorder(&selection, &viewBounds);
		Boolean hit = Overlaps(&selection, &bounds);
		if (hit)
			DeleteHilitedViews();
		return hit;
	}
	TListLoop loop(fChildren);
	for (TView* child = (TView*) loop.Next(); child != nil; child = (TView*) loop.Next())
	{
		Rect r = bounds;
		OffsetRect(&r, -child->viewBounds.left, -child->viewBounds.top);
		HiliteLoop hilites(child);
		while (hilites.Next())
			if (hilites.fCurrent->Overlaps(r))
			{
				DeleteHilitedViews();
				return true;
			}
	}
	return false;
}


// ROM 0x000a6d38 Scrub__9TEditViewFP11TUnitPublic
// A scrub on the page.  The selection goes first if the scrub touched it;
// otherwise every child is asked what it would take out (HandleScrub with
// -1 and nothing done), the biggest answer wins, and the children that
// gave an answer at all are asked again - this time for real - for that
// one kind.  A child that answers 5 has nothing left in it: the page
// removes it from the soup, unless its own text flags say it keeps its
// children, in which case the child is told to empty itself instead.
//
// The hilites are preserved across all of it (SetPreserveHilites), and a
// scrub that did anything takes its own ink off and puffs the hole away.
// ==> whether anything was done.
long
TEditView::Scrub(TUnitPublic* unit)
{
	Boolean savedPreserve = gRootView->SetPreserveHilites(true);
	Rect bounds;
	unit->Bounds(&bounds);
	long done = ScrubHilite(bounds);
	if (done == 0)
	{
		long best = 0;
		CList* takers = CList::Make();
		{
			TListLoop loop(fChildren);
			for (TView* child = (TView*) loop.Next(); child != nil; child = (TView*) loop.Next())
			{
				long kind = child->HandleScrub(bounds, -1, unit, false);
				if (best <= kind)
					best = kind;
				if (kind != 0)
					takers->InsertLast(child);
			}
		}
		if (best != 0)
		{
			TListLoop loop(takers);
			for (TView* child = (TView*) loop.Next(); child != nil; child = (TView*) loop.Next())
			{
				// a child the last round took away is not asked again
				if (fChildren->GetIdentityIndex(child) == kEmptyIndex)
					continue;
				if (child->HandleScrub(bounds, best, unit, best != 5) == 5)
				{
					if ((TextFlags() & 0x80) == 0)
						gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, child->fId)));
					else
						child->HandleScrub(bounds, 5, unit, true);
				}
			}
			done = 1;
		}
		delete takers;
	}
	if (done != 0)
	{
		gRootView->fDirtyFlag = true;
		unit->Stroke()->InkOff(false);
		TAnimate effect;
		AdjustForInk(&bounds);
		Dirty(&bounds);
		effect.SetupPoofEffect(this, bounds);
		effect.DoEffect(RefVar(Rpoof));
	}
	gRootView->SetPreserveHilites(savedPreserve);
	return done;
}


// ROM 0x000a4360 RealDoCommand__9TEditViewFRC6RefVar
// The editor's commands.  The ROM's is the largest function in the view
// system - the whole of scrubbing, the caret, the line and shape
// gestures, the ink, the drag and the undo - and what is here is the
// beginning of it: the guard a read-only page puts on the commands it
// will take at all, and the tap.
//
// A tap is not acted on where it arrives.  The editor notes where it was
// and asks the root view to idle it after the double-tap interval
// (Idle, reason 2); a second tap in that time turns it into something
// else, and if none comes the caret goes where the tap was.  The view's
// own viewGestureScript gets first refusal when the view takes gestures
// (the vGesturesAllowed bit of its text flags).
//
// NOT YET RECONSTRUCTED: every other command the editor answers.  They
// go to TView::RealDoCommand, which runs the view's scripts for them, as
// they did before this function existed.
Boolean
TEditView::RealDoCommand(RefArg cmd)
{
	long id = CommandID(cmd);
	if ((fFlags & (vReadOnly | vWriteProtected)) != 0
		&& id != aeClick && id != aeTapDrag && id != aeShow && id != aeHide
		&& id != aeTap && id != aeLine && id != aeRemoveHilite
		&& id != aeHiliteClick && id != aeGesture2f)
	{
		// a page that may not be written on answers everything but these,
		// and answers them all as done (aeWord alone as not)
		CommandSetResult(cmd, id != aeWord ? 1 : 0);
		return 1;
	}
	if (id == aeCaret)
	{
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		if (HandleCaret((TUnitPublic*) CommandParameter(cmd)))
		{
			CommandSetResult(cmd, 1);
			return true;
		}
		return TView::RealDoCommand(cmd);
	}
	if (id == aeLine)
	{
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		if (HandleLineGesture((TUnitPublic*) CommandParameter(cmd)))
		{
			CommandSetResult(cmd, 1);
			return true;
		}
		return TView::RealDoCommand(cmd);
	}

	if (id == aeGesture2f)
	{
		// the hilite stroke, after the root view has finished drawing it:
		// the page asks its children what it selects (AddHiliter)
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		if (AddHiliter((TUnitPublic*) CommandParameter(cmd)))
		{
			CommandSetResult(cmd, 1);
			return true;
		}
		return TView::RealDoCommand(cmd);
	}

	if (id == aeWord)
	{
		// a word the recogniser read.  A page that takes words through
		// its script (text flag 0x2000) offers it there first; then the
		// hilites are cleared for it as they are for a word of ink (or,
		// with the corrector up, taken away altogether), any ink left
		// on the page for its strokes removed, and the word handed to
		// HandleWord, to go into the paragraph under it or become one.
		Boolean triedScript = false;
		if ((TextFlags() & 0x2000) != 0)
		{
			triedScript = true;
			if (TView::RealDoCommand(cmd))
				return true;
		}
		ULong remote = SetRemoteForCorrector();
		if (CorrectorUp())
			RemoveAllHilites();
		else
			ResetHilitesForNewWord();
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		RemoveInk(this, unit->fUnit);
		Boolean done = HandleWordUnit(unit);
		RestoreRemoteForCorrector(remote);
		CommandSetResult(cmd, done);
		// the ROM's shared exit: the scripts, unless they have had
		// their turn already
		if (!done && !triedScript)
			return TView::RealDoCommand(cmd);
		return done;
	}

	if (id == aeRawInk)
	{
		// ink nobody is to read, written over the page.  The view's own
		// viewRawInkScript gets it first; then the visible data-view
		// children the writing touches are asked how well they would take
		// it and the best one gets it, and if none will it becomes a
		// sketch of the page's own.
		if (TView::RealDoCommand(cmd))
			return true;
		if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
			return TView::RealDoCommand(cmd);	// (the ROM's shared exit runs the scripts again)
		RefVar bundle(CommandFrameParameter(cmd));
		long best = 0;
		TView* bestView = nil;
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			if ((child->fFlags & vVisible) == 0 || !child->DerivedFrom(clDataView))
				continue;
			long score = ((TDataView*) child)->HandleInk(bundle, false);
			if (score > best)
			{
				best = score;
				bestView = child;
			}
		}
		if (best != 0)
			((TDataView*) bestView)->HandleInk(bundle, true);
		else
			::HandleInk(this, bundle);
		return true;
	}

	if (id == aeInkWord)
	{
		// A word of writing nobody read.  The view's own
		// viewInkWordScript gets it first; then the command's parameter
		// - the unit the recogniser sends - is exchanged for the unit's
		// strokes, because everything below here works in stroke
		// bundles.  The visible data-view children are asked how well
		// they would take the word and the best one gets it.
		//
		// What happens when none of them will depends on where the caret
		// is.  A page whose caret is in one of its own paragraphs takes
		// the word into that paragraph, at the caret, rather than
		// starting a new one somewhere else - which is what lets a word
		// written anywhere on the page carry on the line being written.
		// That only happens when the writer has asked for it: the
		// `remoteWriting` preference says writing may come from
		// somewhere other than the caret, and without it the caret has
		// to be asking for the word itself (it is hilited, and the
		// corrector is not up).  Otherwise the word starts a paragraph
		// of its own.
		//
		// While the corrector is up the word has to go where it was
		// written, so remote writing is turned off for the duration
		// (SetRemoteForCorrector) and put back at the end.
		if (TView::RealDoCommand(cmd))
			return true;
		if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
			return TView::RealDoCommand(cmd);	// (the ROM's shared exit runs the scripts again)
		ULong remote = SetRemoteForCorrector();
		if (CorrectorUp())
			RemoveAllHilites();
		else
			ResetHilitesForNewWord();
		ValidateCaret(true);
		RefVar param(GetFrameSlot(cmd, RSSYMparameter));
		if (NOTNIL(param))
		{
			TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
			if (unit != nil)
				CommandSetFrameParameter(cmd, RefVar(unit->Strokes()));
		}
		long best = 0;
		TView* bestView = nil;
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			if ((child->fFlags & vVisible) == 0 || !child->DerivedFrom(clDataView))
				continue;
			long score = ((TDataView*) child)->HandleInkWord(cmd, false);
			if (score > best)
			{
				best = score;
				bestView = child;
			}
		}

		// the view the caret is in, when it is one of this page's (a
		// child or a grandchild of it)
		TView* caretView = gRootView->fCaretView;
		Boolean caretIsOurs = caretView != nil
							  && (caretView->fParent == this
								  || (caretView->fParent != nil
									  && caretView->fParent->fParent == this));
		Boolean caretAsksForIt = caretIsOurs && caretView->Hilited() && !CorrectorUp();

		if (ISNIL(RefVar(GetPreference(RSSYMremotewriting))) && !caretAsksForIt)
		{
			// the word goes where it was written
			if (best != 0)
				((TDataView*) bestView)->HandleInkWord(cmd, true);
			else
				HandleInkWord(cmd);
		}
		else
		{
			Boolean atTheCaret = caretIsOurs;
			if (!atTheCaret && caretView == this)
			{
				// the caret is on the page itself rather than in a
				// paragraph: the word goes to the caret only when there
				// is text right under it that would only just take it
				// (score 2 - the caret sits at the end of that view's
				// last line, where the next line would start)
				Point where = GetCaretGlobalTopLeft();
				long score = 0;
				TView* under = TextContainingPoint(where, nil, &score);
				if (under != nil && score == 2)
				{
					// a carriage return written in a one-pixel box at
					// that view's bottom right corner: it starts the new
					// line and moves the caret into it, and the word
					// then goes in at the caret like any other item
					UniChar cr[2];
					cr[0] = 0x0d;
					cr[1] = 0;
					Point corner;
					corner.v = (short) (under->viewBounds.bottom - 1);
					corner.h = (short) (under->viewBounds.right - 1);
					Rect box;
					SetRect(&box, 0, 0, 1, 1);
					OffsetRect(&box, corner.h, corner.v);
					RefVar none;
					((TDataView*) under)->HandleWord(cr, 1, box, corner, 0, 0,
													 none, true, nil, nil);
					// (the ROM asks the *page* what x-height the word wants
					//  below, not the paragraph the caret has just moved
					//  into; kept as it is)
					atTheCaret = true;
				}
			}

			if (atTheCaret)
			{
				// the word put in at the caret, as an item the paragraph
				// inserts: the strokes made into an ink word and brought
				// to the x-height the caret's view writes in
				RefVar spec(AllocateFrame());
				RefVar ink(StrokeBundleToInkWord(RefVar(CommandFrameParameter(cmd))));
				AdjustInkWordXHeight(ink, ViewExpectsNumbers(caretView));
				SetFrameSlot(spec, RSSYMinsertitems, ink);
				InsertItemsAtCaret(spec);
			}
			else if (caretView == this || best == 0)
				HandleInkWord(cmd);
			else
				((TDataView*) bestView)->HandleInkWord(cmd, true);
		}
		RestoreRemoteForCorrector(remote);
		CommandSetResult(cmd, 1);
		return true;
	}

	if (id == aeScrub)
	{
		// (textFlags bit 0x2000: the page answers the pen itself first)
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		return Scrub((TUnitPublic*) CommandParameter(cmd)) != 0;
	}

	if (id == aeKeyDown || id == aeKeyRepeat)
	{
		// a key typed at the page.  The view's own key scripts get it
		// first (TView::HandleKeyEvent, which says whether anything took
		// it); what is left and can be printed is put on the page as a
		// word of one character.  The Newton's enter key comes through as
		// 3 and goes on the page as a carriage return.
		Boolean taken = false;
		if (HandleKeyEvent(cmd, id, &taken))
			return 1;
		if ((fFlags & (vReadOnly | vWriteProtected)) != 0 || taken)
			return 1;
		UniChar ch = (UniChar) CommandParameter(cmd);
		if (ch == 8)				// backspace
			DeleteHilitedViews();
		else
		{
			if (ch == 3)
				ch = 0x0d;			// the Newton's enter key writes a carriage return
			if (KeyIsPrintable(ch, this))
				JamText(&ch, 1);
		}
		return 1;
	}
	if (id == aeKeyString)
	{
		// a whole string typed at once (the keyboard tool's)
		Boolean taken = false;
		if (HandleKeyEvent(cmd, id, &taken))
			return 1;
		RefVar typed(CommandFrameParameter(cmd));
		UniChar* chars = (UniChar*) BinaryData(typed);
		JamText(chars, Ustrlen(chars));
		return 1;
	}
	if (id == aeDoubleTap)
	{
		// The second tap goes to whatever is under it: the page itself
		// has nothing to correct, so the topmost child whose bounds hold
		// the point is offered the command, and the first that takes it
		// ends it.  That is how a double tap on a word of a paragraph
		// reaches `TParagraphView::RealDoCommand` and puts the corrector
		// up.
		//
		// NOT YET RECONSTRUCTED: the other arm, for a page that takes
		// text and was tapped on ink - the writing is gathered up and
		// offered to the recogniser again rather than corrected, which
		// wants the re-recognition path (`RecognizeInArea`).
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return 1;
		fTapPending = false;
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		Point pt = unit->Stroke()->FirstPoint();
		TBackwardViewListLoop loop(fChildren);
		for (TView* child = loop.Next(); child != nil; child = loop.Next())
			if (PtInRect(pt, &child->viewBounds) && child->DoCommand(cmd))
				break;
		return 1;
	}
	if (id != aeTap)
		return TView::RealDoCommand(cmd);	// NOT YET: the rest of the editor's own
	// 0x2000 of the textFlags slot - not the viewFlags, and what it is
	// called is not yet known; the ROM tests it before letting the view's
	// own scripts see the tap
	if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd) != 0)
		return 1;							// the view's gesture script took it
	fTapPending = true;
	TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
	fTapPoint = unit->Stroke()->FirstPoint();
	gRootView->AddIdler(this, 0x50 + (gDoubleTapInterval << 4), 2);
	return 1;
}

// ROM 0x000a4204 ResetHilitesForNewWord__9TEditViewFv
// The selection made ready for a word about to be written or typed.
// More than one child selected is no place to put a word, so the whole
// selection goes and the caret with it; one selected paragraph becomes
// the key view with its selected characters, so the word replaces them;
// one selected polygon is no place either.  Nothing selected is left
// alone, which is the empty page's case.
void
TEditView::ResetHilitesForNewWord(void)
{
	TView* first = nil;
	if (HasHilitedChildren(2, &first))
	{
		gRootView->SetPreserveHilites(true);
		gRootView->SetKeyView(nil, 0, 0, false);
		gRootView->SetPreserveHilites(false);
		RemoveAllHilites();
		fCaretRect.top = kNoBounds;
		fCaretRect.bottom = kNoBounds;
		return;
	}
	if (first == nil)
		return;
	if (first->DerivedFrom(clParagraphView))
	{
		RefVar hilite(first->FirstHilite());
		TParagraphHilite* selection = (TParagraphHilite*) RefToAddress(hilite);
		gRootView->SetKeyView(first, selection->fStart,
							  selection->fEnd - selection->fStart, false);
	}
	else if (first->DerivedFrom(clPolygonView))
	{
		gRootView->SetPreserveHilites(true);
		gRootView->SetKeyView(nil, 0, 0, false);
		gRootView->SetPreserveHilites(false);
		RemoveAllHilites();
	}
}


// ROM 0x000aa9b0 ValidateCaret__9TEditViewFUc
// The caret thrown away when it has gone out of sight, so that what is
// written next does not go somewhere nobody can see.  It is out of sight
// when nothing of it is left in the port's clipping region, and -
// `scrolled` asking for it - when it has come within fifty pixels of the
// right edge or ten of the top or bottom, which is the ROM's margin for
// a page that is about to scroll.  ==> whether the caret is still this
// view's afterwards.
//
// The whole of it is behind the remoteWriting preference: with none set
// the caret is never taken away, which is the machine as it boots.
Boolean
TEditView::ValidateCaret(Boolean scrolled)
{
	if (gRootView->fCaretView == this && NOTNIL(RefVar(GetPreference(RSSYMremotewriting))))
	{
		Rect caret = fCaretRect;
		Point origin = ContentsOrigin();
		OffsetRect(&caret, origin.h, origin.v);
		TRegion saved(SetupVisRgn());
		TRegionVar visible(saved);
		GrafPort* port;
		GetPort(&port);
		RgnHandle clip = port->clipRgn;
		Rect clipBox = (*clip)->rgnBBox;
		TRegionVar showing;
		RectRgn(showing, &caret);
		SectRgn(showing, clip, showing);
		if (EmptyRgn(showing)
			|| (scrolled && (clipBox.right - 50 < caret.right
							 || clipBox.top + 10 > caret.bottom
							 || clipBox.bottom - 10 < caret.bottom)))
		{
			fCaretRect.top = kNoBounds;
			fCaretRect.bottom = kNoBounds;
			gRootView->SetKeyView(nil, 0, 0, false);
		}
		GetPort(&port);
		CopyRgn(visible, port->clipRgn);
	}
	return gRootView->fCaretView == this;
}

/*------------------------------------------------------------------------------
	I n k   o n   t h e   p a g e
------------------------------------------------------------------------------*/

// (the id of the ROM's numbers dictionary, which the ROM writes in
// here as a bare number)
const long kNumbersDictionaryId = 117;


// ROM 0x0017fb04 ViewExpectsNumbers__FP5TView
// Whether the view is after numbers rather than words.  Three things say
// so: a bit of its text flags; its recognition flags leaving numbers, a
// time or a phone number as the only thing it will take; or it naming a
// single custom dictionary, that dictionary being the numbers one.
//
// (The text-flag bit has no name in the ROM's own headers.)
Boolean
ViewExpectsNumbers(TView* view)
{
	TView* rec = GetRecognitionView(view);
	if ((rec->TextFlags() & 0x200) != 0)
		return true;
	ULong flags = rec->fFlags & (vCharsAllowed | vNumbersAllowed | vLettersAllowed
								 | vPunctuationAllowed | vShapesAllowed | vMathAllowed
								 | vPhoneField | vDateField | vTimeField | vAddressField
								 | vNameField | vCapsRequired | vCustomDictionaries);
	if ((flags & ~(ULong) (vCustomDictionaries | vTimeField | vPhoneField | vNumbersAllowed)) != 0)
		return false;
	if (flags == vNumbersAllowed || flags == vTimeField || flags == vPhoneField)
		return true;
	if (flags == vCustomDictionaries)
	{
		RefVar dictionaries(rec->GetProto(RSSYMdictionaries));
		if (IsArray(dictionaries) && Length(dictionaries) == 1)
			dictionaries = GetArraySlot(dictionaries, 0);
		if (EQRef(dictionaries, MAKEINT(kNumbersDictionaryId)))
			return true;
	}
	return false;
}


// ROM 0x00140834 HandleInk__FP9TEditViewPP7TStroke
// Strokes put on the page as ink of their own: they are packed up as a
// sketch, the box they came from is brought back into the page's own
// coordinates, and the shape is added as a child.
void
HandleInk(TEditView* view, TStroke** strokes)
{
	Rect box;
	RefVar ink(TStrokesToInk(strokes, &box));
	Point origin = view->ContentsOrigin();
	OffsetRect(&box, (short) -origin.h, (short) -origin.v);
	RefVar form(MakePolygonForm(nil, 0, kInkVerb, box,
								RINT(GetPreference(RSSYMuserpensize))));
	SetFrameSlot(form, RSSYMink, ink);
	view->AddForm(form);
}


// ROM 0x00140754 HandleInk__FP9TEditViewRC6RefVar
// The same from a bundle of strokes.
long
HandleInk(TEditView* view, RefArg bundle)
{
	TStroke** strokes = StrokeBundleToTStrokes(bundle);
	HandleInk(view, strokes);
	DisposeTStrokes(strokes);
	return 1;
}


// ROM 0x000a6798 HandleInk__9TEditViewFP11TUnitPublic
// One unit's stroke put on the page.
long
TEditView::HandleInk(TUnitPublic* unit)
{
	TStroke* list[2];
	list[0] = GetTStroke(unit->fUnit);
	list[1] = nil;
	::HandleInk(this, list);
	return 1;
}


// ROM 0x000a6854 HandleInk__9TEditViewFRC6RefVar
// ... and a whole bundle of them.
long
TEditView::HandleInk(RefArg bundle)
{
	TStroke** strokes = StrokeBundleToTStrokes(bundle);
	::HandleInk(this, strokes);
	DisposeTStrokes(strokes);
	return 1;
}


// ROM 0x000a6888 HandleInkWord__9TEditViewFRC6RefVar
// A word the recogniser could not read, or was not asked to, put on the
// page as a paragraph of one character: the ink word itself, standing as
// 0xf701 with the ink as its style.  The word's x-height is adjusted
// first for a view that is after numbers, which sit differently on the
// line from letters.
void
TEditView::HandleInkWord(RefArg cmd)
{
	UniChar text[2];
	text[0] = kInkWordChar;
	text[1] = 0;
	RefVar bundle(CommandFrameParameter(cmd));
	RefVar ink(StrokeBundleToInkWord(bundle));
	AdjustInkWordXHeight(ink, ViewExpectsNumbers(this));
	Rect box;
	FromObject(RefVar(GetFrameSlot(bundle, RSSYMbounds)), box);
	RefVar info(AllocateFrame());
	RefVar styles(AllocateArray(RSSYMstyles, 2));
	SetArraySlot(styles, 0, RefVar(MAKEINT(1)));
	SetArraySlot(styles, 1, ink);
	SetFrameSlot(info, RSSYMstyles, styles);
	long offset = 0;
	AddNewParagraph(text, 1, box, box, nil, info, &offset, ink);
}


// ROM 0x000ab28c AddForm__9TEditViewFRC6RefVar
// The context frame made into a child of the editor - a new paragraph,
// usually.  It does not add the view itself: it sends itself an
// aeAddData command through the application, which is what puts the
// child in the soup and posts the aeRemoveData that undoes it, and then
// answers the view that command made.  A throw on the way is let past
// once the view has been read out of the command.
TView*
TEditView::AddForm(RefArg form)
{
	RefVar cmd(MakeCommand(aeAddData, this, kNoParameter));
	CommandSetFrameParameter(cmd, form);
	newton_try
	{
		gApplication->DispatchCommand(cmd);
	}
	newton_catch_all
	{
		// (the ROM reads the command's parameter here too, on the way out,
		//  because the two paths share a tail; nothing can see it)
		rethrow;
	}
	end_try;
	return (TView*) CommandParameter(cmd);
}

// The text with a nul after it, when it has not got one: TextBounds
// measures a C string and the caller's run may not be terminated.
static UniChar*
NullTerminated(const UniChar* text, ULong length)
{
	UniChar* copy = new UniChar[length + 1];
	if (copy == nil)
		return nil;
	memcpy(copy, text, length * sizeof(UniChar));
	copy[length] = 0;
	return copy;
}


// ROM 0x0c101710 gAddWordInfo
Boolean	gAddWordInfo = false;


// ROM 0x001767b8 CorrectorUp__Fv
// Whether the corrector is up: the root view's `correct` variable is the
// corrector's context, and a context that has been built has a
// viewCObject.
Boolean
CorrectorUp(void)
{
	RefVar corrector(gRootView->GetVar(RSSYMcorrect));
	// NOT YET RECONSTRUCTED: the corrector itself.  On the machine the
	// root view's context always has a `correct` view frame - the
	// corrector is one of the root's own children - so the ROM asks it
	// for its viewCObject straight away; here there is no corrector at
	// all and the slot is nil, which GetFrameSlotRef would throw on.
	if (ISNIL(corrector))
		return false;
	return NOTNIL(GetFrameSlotRef(corrector, RSSYMviewcobject));
}


// ROM 0x00177470 SetRemoteForCorrector__Fv
// Remote writing turned off while the corrector is up, and what was
// there before remembered so it can be put back.
//
// The corrector is the slip that offers a word's other readings.  While
// it is up, writing that arrives has to go where it was written - if
// remote writing were left on, every word would be posted to whatever
// the corrector's own caret happens to be in.
//
// ==> two bits: 1 the corrector was up, 2 remote writing was on.
ULong
SetRemoteForCorrector(void)
{
	Boolean up = CorrectorUp();
	ULong state = up ? 1 : 0;
	Boolean remote = NOTNIL(RefVar(GetPreference(RSSYMremotewriting)));
	if (remote)
		state |= 2;
	if (up && remote)
		SetPreference(RSSYMremotewriting, RefVar(NILREF));
	return state;
}


// ROM 0x001774e0 RestoreRemoteForCorrector__Fl
// Remote writing put back after the corrector has had its turn.
//
// ROM BUG, kept: the test is "either bit", not "both bits".  The
// preference is only ever taken away when the corrector was up *and*
// remote writing was on, so only that case should put it back - but a
// session where the corrector was up with remote writing off ends with
// remote writing switched on, and it stays on.  Writing a word anywhere
// on a page with the corrector up is enough to change a preference the
// writer never touched.
void
RestoreRemoteForCorrector(ULong state)
{
	if ((state & 3) != 0)
		SetPreference(RSSYMremotewriting, RefVar(TRUEREF));
}


// ROM 0x000a39f4 TimeStampTextChange__FP5TView
// The view whose text has just changed put in the globals as
// `lastTextChanged`, which is how a script - the assistant's, for one -
// finds out where the last word went.
void
TimeStampTextChange(TView* view)
{
	RefVar globals(FGetGlobals(RefVar()));
	SetFrameSlot(globals, RSSYMlasttextchanged, view->fContext);
}


// ROM 0x000abaa4 HandleWord__9TEditViewFPUsUlR5TRectT3P11TUnitPublicRC6RefVarPl
// A word - written and recognised, or typed and made to look like one -
// put on the page.  It begins by making sure the caret belongs here: the
// key view has to be this editor or one of its children, and anything
// else is dropped so that the word does not go into somebody else's
// text.
//
// Then every visible child that holds data is asked how well it would
// take the word, exactly as TextContainingPoint asks them where a point
// is (the child whose data frame *is* this word's is skipped, so a word
// is not offered back to itself).  The best answer wins; 6 is as good as
// it gets and stops the search.  The winner is asked again, this time
// for real - the second call's `insert` is true, and that is what makes
// it take the word rather than measure it - and if nobody wanted the
// word at all it becomes a paragraph of its own (AddNewParagraph).
//
// `box` is where the word was written; an empty one means nowhere in
// particular, and goes straight to a new paragraph.
//
// ==> the view the word ended up in.
//
// NOT YET RECONSTRUCTED: the remote-writing and corrector path
// (0x000abe58-0x000ac0ac), which is where a recognised word goes when
// the assistant is taking dictation into another view or the corrector
// is up - it inserts the word at the caret through DoInsertItems rather
// than handing it to a child.  Nothing typed goes that way.
TView*
TEditView::HandleWord(UniChar* text, ULong length, Rect& box, Rect& room,
					  TUnitPublic* unit, RefArg info, long* outOffset)
{
	TView* key = gRootView->fCaretView;
	if (key != this
		&& (key == nil || (key->fParent != this
						   && (key->fParent == nil || key->fParent->fParent != this))))
	{
		gRootView->SetKeyView(nil, 0, 0, false);
		key = this;
	}
	ValidateCaret(true);
	Boolean emptyBox = box.left == 0 && box.right == 0 && box.top == 0 && box.bottom == 0;
	Boolean handled = false;
	gAddWordInfo = true;

	// where on the page the word is, which is what a child measures
	// against: the middle of the unit's base line for a written word, the
	// bottom left of the box for a typed one.  A written word's middle is
	// pulled back into the box when it falls outside it.
	ULong startTime, endTime;
	Point pt;
	if (unit != nil)
	{
		startTime = unit->StartTime();
		endTime = unit->EndTime();
		const Rect& base = unit->fWordBase;
		pt.v = (short) ((short) (base.top + base.bottom) >> 1);
		pt.h = (short) ((short) (base.left + base.right) >> 1);
		if (box.left > pt.h || pt.h > box.right)
			pt.h = box.left;
		if (pt.v < box.top || box.bottom < pt.v)
			pt.v = box.bottom;
	}
	else
	{
		startTime = 0;
		endTime = 0;
		pt.v = box.bottom;
		pt.h = box.left;
	}

	// whether a child of this editor holds the caret and has a selection
	// in it with the corrector down, which is the one case the plain path
	// below is not taken
	Boolean corrector = key != nil
						&& (key->fParent == this
							|| (key->fParent != nil && key->fParent->fParent == this))
						&& key->Hilited() && !CorrectorUp();

	TView* best = nil;
	long bestScore = 0;
	if (!emptyBox)
	{
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			if ((child->fFlags & vVisible) == 0)
				continue;
			if (EQRef(child->DataFrame(), info))		// not back into itself
				continue;
			if (!child->DerivedFrom(clDataView))
				continue;
			long wants = ((TDataView*) child)->HandleWord(text, length, box, pt,
														  startTime, endTime, info,
														  false, nil, unit);
			if (wants > bestScore)
			{
				best = child;
				bestScore = wants;
			}
			if (wants == 6)				// as good as it gets
				break;
		}
		if (unit != nil && (NOTNIL(GetPreference(RSSYMremotewriting)) || corrector))
		{
			// ROM 0x000abe58: remote writing - a written word goes to the
			// caret, wherever on the page it was written.  (A child that
			// answered 6 has taken it already.)
			if (bestScore != 6)
			{
				TView* under = nil;
				long where = 0;
				if (key != nil && key->DerivedFrom(clParagraphView)
					&& ((TDataView*) key)->GetEnclosingEditView() == this)
				{
					// the caret is in one of this page's paragraphs: the
					// word's info inserted there, with a space before it
					// unless it is a letter written into the middle of
					// a word
					RefVar spec(Clone(RefVar(Rstarterinsertspec)));
					RefVar wordInfo(unit->WordInfo());
					Boolean addSpace = !IsMidWordLetterInsertion((TParagraphView*) key, unit);
					SetFrameSlot(spec, RSSYMinsertitems, wordInfo);
					SetFrameSlot(spec, RSSYMaddspace, RefVar(addSpace ? TRUEREF : NILREF));
					InsertItemsAtCaret(spec);
				}
				else
				{
					Boolean placed = false;
					if (key == this)
					{
						// the caret is on the page itself: the word goes
						// to the end of the text under it when that text
						// would only just take it (score 2 - the caret
						// sits where its next line would start), on a
						// line of its own; otherwise it becomes a
						// paragraph at the caret
						Point caret = GetCaretGlobalTopLeft();
						under = TextContainingPoint(caret, nil, &where);
						if (under == nil || where != 2)
						{
							RefVar noInkFont;
							best = AddNewParagraph(text, length, box, room, unit, info,
												   outOffset, noInkFont);
							placed = true;
						}
					}
					if (placed)
						;
					else if (under != nil && where == 2)
					{
						RefVar items(MakeArray(2));
						SetArraySlot(items, 0, RefVar(MakeString("\r")));
						SetArraySlot(items, 1, RefVar(unit->WordInfo()));
						RefVar textRef(((TParagraphView*) under)->Text());
						long end = (long) ((ULong) (Length(textRef) - sizeof(UniChar)) / sizeof(UniChar));
						RefVar noFont;
						DoInsertItems(under, items, false, true, end, 0, true, noFont);
					}
					else if (bestScore != 0)
						((TDataView*) best)->HandleWord(text, length, box, pt,
														startTime, endTime, info,
														true, outOffset, unit);
					else
					{
						RefVar noInkFont;
						best = AddNewParagraph(text, length, box, room, unit, info,
											   outOffset, noInkFont);
					}
				}
			}
			handled = true;
		}
		else if (bestScore != 0)
		{
			if (bestScore != 6)
				((TDataView*) best)->HandleWord(text, length, box, pt,
												startTime, endTime, info,
												true, outOffset, unit);
			handled = true;
		}
	}

	if (!handled)
	{
		RefVar noInkFont;
		best = AddNewParagraph(text, length, box, room, unit, info, outOffset, noInkFont);
	}
	else if (ISNIL(GetPreference(RSSYMremotewriting)) && !corrector)
	{
		TView* textView = ((TDataView*) best)->GetTextView();
		TimeStampTextChange(textView);
		// a word the recogniser read is registered with the machine, so
		// that the corrector can still be asked about it afterwards
		// (gAddWordInfo is cleared by the path that puts the word in at
		//  the caret instead, which registers it for itself)
		if (unit != nil && gAddWordInfo && outOffset != nil)
			AddWordInfo(((TDataView*) best)->GetTextView(), *outOffset,
						*outOffset + (long) length, unit);
	}
	return best;
}


// ROM 0x000ab9f8 HandleWordUnit__9TEditViewFP11TUnitPublic
// A word the recogniser read, put on the page: its best reading (a
// handle the unit's face makes and the caller throws away) handed to
// HandleWord in the box it was written in, which is also the room it
// is given.
Boolean
TEditView::HandleWordUnit(TUnitPublic* unit)
{
	Rect box;
	unit->Bounds(&box);
	Handle word = unit->Word();
	HLock(word);
	UniChar* text = (UniChar*) *word;
	ULong length = Ustrlen(text);
	long offset;
	RefVar info;
	TView* view = HandleWord(text, length, box, box, unit, info, &offset);
	HUnlock(word);
	DisposHandle(word);
	return view != nil;
}

// ROM 0x0019dfa4 RemoveInk__FP9TEditViewP5TUnit
void
RemoveInk(TEditView* view, TUnit* unit)
{
	long count = unit->SubCount();
	for (long i = 0; i < count; i++)
	{
		// (a unit with subs is a TSIUnit; GetSub is its vtable +0x58)
		TUnit* sub = ((TSIUnit*) unit)->GetSub(i);
		if (sub->fType == 'STRK')
		{
			if (sub->ContextID() != 0)
			{
				TView* ink = view->FindID((long) sub->ContextID());
				if (ink != nil)
					gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, view, ink->fId)));
			}
		}
		else
			RemoveInk(view, sub);
	}
}

// ROM 0x000ab70c JamText__9TEditViewFPUsUl
// Typed text put on the page.  The Newton has no text cursor of its own
// on a page like this: what is typed is made to look like a word that
// has just been written and handed to the same HandleWord the recogniser
// uses, so that typing and writing end in the same place.
//
// With no caret - nothing has been tapped, or it has been taken away -
// one is put on the line after the last thing on the page: a line's
// spacing below the lowest child (or the top of the page when there are
// none), twelve pixels in from its left edge.
void
TEditView::JamText(UniChar* text, ULong length)
{
	ResetHilitesForNewWord();
	ValidateCaret(true);
	if (fCaretRect.top == kNoBounds)
	{
		RefVar spacing(GetVar(RSSYMviewlinespacing));
		Point pt;
		pt.v = ISNIL(spacing) ? 20 : (short) RINT(spacing);
		pt.h = 12;
		short lowest = viewBounds.top;
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
			if (child->viewBounds.bottom > lowest)
				lowest = child->viewBounds.bottom;
		pt.v += lowest;
		pt.h += viewBounds.left;
		PositionCaret(pt, false);
	}
	// the box the text will fill, at the caret
	RefVar font(GetProto(RSSYMviewfont));
	if (ISNIL(font))
		font = GetPreference(RSSYMuserfont);
	Rect box;
	box.top = box.left = box.bottom = box.right = 0;
	UniChar* measured = text;
	if (text[length] != 0)
		measured = NullTerminated(text, length);
	{
		TRichString rich(measured, length * sizeof(UniChar) + sizeof(UniChar));
		TextBounds(rich, font, &box, 0);
	}
	Point caret = GetCaretGlobalTopLeft();
	OffsetRect(&box, caret.h, caret.v);
	if (measured != text)
		delete[] measured;
	RefVar word;
	HandleWord(text, length, box, box, nil, word, nil);
	fCaretRect.top = kNoBounds;
	fCaretRect.bottom = kNoBounds;
}

// ROM 0x000a3e70 MakeNullTerminatedString__FPUsUl
// A copy of the text with a NUL after it, for the things that measure a
// string rather than a counted range.  The caller frees it.
UniChar*
MakeNullTerminatedString(UniChar* text, ULong length)
{
	UniChar* copy = new UniChar[length + 1];
	BlockMove(text, copy, (long) (length * sizeof(UniChar)));
	copy[length] = 0;
	return copy;
}


// ROM 0x000a1b2c AddNewParagraph__9TEditViewFPUsUlR5TRectT3P11TUnitPublicRC6RefVarPlT6
// A word nobody would take made into a paragraph of its own.
//
// The style it goes in is `vars.nextStyle` when a script has set one -
// and setting one is a single use, so it is taken back out of the
// globals here - and the user's font otherwise.  An ink font, when there
// is one, wins over both.
//
// Where it goes is `room` - the box the caller says the word may grow
// into - in the editor's own coordinates, moved down by the word info's
// `offset` when it has one.  MakeParagraphForm turns the text and that
// box into a paragraph's context frame, the page's `editAddWordScript`
// gets a chance to replace the frame with one of its own, and AddForm
// makes it a child through the undoable aeAddData command.  The new
// paragraph's text view then becomes the key view with the caret after
// the word, which is what lets the next character typed go straight into
// the paragraph rather than back through here.
//
// ==> the view that was made, or nil.
//
// NOT YET RECONSTRUCTED: the geometry (0x000a1eb4-0x000a22bc), which is
// the path taken when the word came from the recogniser or has an ink
// font - it measures the text with TextBounds, lines the result up with
// the page's other children (AlignBounds) and with its ruled lines
// (AlignToLineSpacing), and is what makes handwriting tidy itself into
// columns.  Typed text does not go that way: the keyboard has already
// measured its own box, so the ROM jumps straight over the whole section
// (the test at 0x000a1e94).  The remote-writing caret (0x000a1b98) and
// the text view's own notification (vtable +0x150) are NOT YET for the
// same reason.
TView*
TEditView::AddNewParagraph(UniChar* text, ULong length, Rect& box, Rect& room,
						   TUnitPublic* unit, RefArg info, long* outOffset, RefArg inkFont)
{
	Boolean remoteCaret = false;
	if (NOTNIL(GetPreference(RSSYMremotewriting)) && gRootView->fCaretView == this)
	{
		remoteCaret = true;
		// NOT YET RECONSTRUCTED: the box the caret is in worked out from
		// the editor's own caret rectangle (0x000a1b98-0x000a1ca4), which
		// only the geometry section below reads.
	}
	Boolean hasInkFont = NOTNIL(inkFont);
	RefVar theInfo(info);
	RefVar style;

	// the style: a script's nextStyle, used once and taken back out of the
	// globals, or the user's font; an ink font beats both
	RefVar nextStyle(GetFrameSlotRef(gVarFrame, RSSYMnextstyle));
	if (NOTNIL(nextStyle) && !hasInkFont)
	{
		RefVar globals(gVarFrame);
		SetFrameSlot(globals, RSSYMnextstyle, RefVar());
		style = nextStyle;
	}
	else
	{
		nextStyle = GetPreference(RSSYMuserfont);
		style = hasInkFont ? (Ref) inkFont : (Ref) nextStyle;
	}
	if (unit != nil)
	{
		// a written word carries its style to the paragraph as a style run
		// covering the whole of it
		if (ISNIL(theInfo))
			theInfo = AllocateFrame();
		RefVar styles(AllocateArray(RSSYMstyles, 2));
		SetFrameSlot(theInfo, RSSYMstyles, styles);
		SetArraySlot(styles, 0, MAKEINT(length));
		SetArraySlot(styles, 1, nextStyle);
	}

	StyleRecord styleRecord;
	CreateTextStyleRecord(style, &styleRecord);
	FontInfo fontInfo;
	GetStyleFontInfo(&styleRecord, &fontInfo);

	TView* view = nil;
	Rect area;
	if (unit != nil || hasInkFont)
	{
		// A word that came from the pen goes where it was written rather
		// than where a caret is.  Both kinds are placed around a point:
		// the middle of the base line the writing stands on, for a word
		// the recogniser read, and the middle of the top of the box it
		// was measured in, for a word that was not read.
		Point pt;
		if (unit != nil)
		{
			pt.v = (short) ((unit->fWordBase.top + unit->fWordBase.bottom) / 2);
			pt.h = (short) ((unit->fWordBase.left + unit->fWordBase.right) / 2);
		}
		else
		{
			pt.v = (short) (box.top + fontInfo.ascent);
			pt.h = (short) ((box.left + box.right) / 2);
		}

		if (hasInkFont)
		{
			// An ink word, which has already been brought down to a size
			// a line of text can hold: it is as wide as the word
			// measures at its own scale and one line of the paragraph's
			// font tall, from the point rightwards.
			InkWordInfo wordInfo;
			GetInkWordInfo(inkFont, &wordInfo);
			area.top = (short) (pt.v - fontInfo.ascent);
			area.left = pt.h;
			area.right = (short) (area.left + wordInfo.fScaledWidth);
			area.bottom = (short) (pt.v + fontInfo.descent + fontInfo.leading);
		}
		else
		{
			// A word the recogniser read.  It is measured first: a box
			// one line of the font tall, standing on the line the
			// writing stood on and with no width at all, which
			// TextBounds fills in.
			Rect measured;
			measured.top = (short) (pt.v - fontInfo.ascent);
			measured.left = room.left;
			measured.bottom = (short) (pt.v + fontInfo.descent + fontInfo.leading);
			measured.right = room.left;
			{
				UniChar* measuredText = text;
				if (text[length] != 0)
					measuredText = MakeNullTerminatedString(text, length);
				TRichString rich(measuredText, (ULong) (length * 2 + 2));
				TextBounds(rich, style, &measured, 0);
				if (measuredText != text)
					delete[] measuredText;
			}

			// then lined up with whatever the page already has on it -
			// the other children's edges - within the room it was given,
			// which is what makes handwriting tidy itself into columns
			Rect want = room;
			want.bottom = pt.v;
			AlignBounds(want, measured, &area);
			// and then, only if that left the line alone, with the
			// page's ruled lines
			Boolean moved = (area.top != measured.top || area.bottom != measured.bottom);
			Point origin = ContentsOrigin();
			OffsetRect(&area, -origin.h, -origin.v);
			if (!moved)
				AlignToLineSpacing(&area, pt.v - origin.v, fontInfo.ascent);
		}
	}
	else
	{
		// Typed text: the keyboard has measured its own box already, so
		// the word simply fills the room it was given, moved down by the
		// word info's offset when it has one.
		area = room;
		Point origin = ContentsOrigin();
		OffsetRect(&area, -origin.h, -origin.v);
		if (NOTNIL(theInfo))
		{
			RefVar offset(GetFrameSlotRef(theInfo, RSSYMoffset));
			if (NOTNIL(offset))
				area.top += RINT(offset);
		}
	}

	RefVar form(MakeParagraphForm(text, length, area, theInfo, false));
	long hasScript = 0;
	GetProtoVariable(fContext, RSSYMeditaddwordscript, &hasScript);
	if (hasScript != 0)
	{
		// the page may make the paragraph itself: it is handed the frame
		// and the room, and whatever it answers is what goes down
		Rect where = room;
		Point at = ContentsOrigin();
		OffsetRect(&where, -at.h, -at.v);
		RefVar args(MakeArray(2));
		SetArraySlot(args, 0, form);
		SetArraySlot(args, 1, ToObject(where));
		form = RunScript(RSSYMeditaddwordscript, args, true, nil);
	}

	view = AddForm(form);
	if (view != nil)
	{
		TView* textView = ((TDataView*) view)->GetTextView();
		gRootView->SetKeyView(textView, length, 0, false);
		if (outOffset != nil)
			*outOffset = 0;
		ULong stamp = unit != nil ? unit->EndTime() : Ticks();
		(void) stamp;		// (the recogniser's path tells the text view about it)
		TimeStampTextChange(((TDataView*) view)->GetTextView());
		// (the ROM offers the word to the dictionary here - AddWordInfo
		//  0x00079790, NOT YET - when it came from the recogniser)
	}
	DisposeStyleRecord(&styleRecord);
	(void) remoteCaret;
	return view;
}


// ROM 0x000a2670 RangeDistance__FlN31
// How far apart two ranges are.  One range wholly inside the other is 0
// - they are as together as they can be - and ranges that merely overlap
// are 1; anything else is the gap between them.  The editor asks this of
// two views' tops and bottoms to decide whether they are on the same
// line, and of their lefts and rights for the same column.
long
RangeDistance(long aLow, long aHigh, long bLow, long bHigh)
{
	if (aLow <= bLow && aHigh >= bHigh)
		return 0;					// b inside a
	if (bLow <= aLow && bHigh >= aHigh)
		return 0;					// a inside b
	if (aLow <= bLow && bLow <= aHigh)
		return 1;					// b starts inside a
	if (bLow <= aLow && aLow <= bHigh)
		return 1;					// a starts inside b
	long gap = bLow - aHigh;		// b is after a
	if (gap <= 0)
		gap = aLow - bHigh;			// no: a is after b
	return gap;
}

// How far out an edge may be and still be worth lining up with.
enum { kAlignTolerance = 10 };

// One alignment tried.  `apart` is how far the child's edge is from where
// the new paragraph was put; when that is closer than the best so far, the
// paragraph will move by `shift` - which is the same edge measured against
// the box the text actually needs, because that is what will be offset.
static inline void
TryAlignment(long apart, long shift, long& bestShift, long& best, long& bestRange, long range)
{
	long near = apart < 0 ? -apart : apart;
	long sofar = best < 0 ? -best : best;
	if (near < sofar)
	{
		bestShift = shift;
		best = apart;
		bestRange = range;
	}
}


// ROM 0x000a26c4 AlignBounds__9TEditViewFR5TRectT1P5TRect
// Why a page of Newton handwriting looks tidier than what was actually
// written.  A paragraph about to go down is lined up with whatever of the
// editor's children it is nearly aligned with already: five alignments on
// each axis - edge to the same edge, edge to the opposite edge, and centre
// to centre - and the one that is out by least wins, so long as it is out
// by less than ten pixels.
//
// The two axes are crossed, which is the point of it: children that
// overlap the new paragraph *vertically* have their *horizontal* edges
// lined up, so two words on a line share a left edge, and children that
// overlap horizontally have their vertical edges lined up, so two lines in
// a column share a top.  A child that overlaps a good deal more than the
// best one so far opens the ten pixels up again, so a near neighbour is
// not held to a distant one's alignment.
//
// `want` is where the paragraph was put, `measured` is the box its text
// needs, and `result` comes back as `measured` moved - never off the top
// or left of the editor, never past its right or bottom.
long
TEditView::AlignBounds(Rect& want, Rect& measured, Rect* result)
{
	long hBest = kAlignTolerance, hShift = 0, hRange = 0x7fffffff;
	long vBest = kAlignTolerance, vShift = 0, vRange = 0x7fffffff;
	long wantCentreX = (want.left + want.right) / 2;
	long wantCentreY = (want.bottom + want.top) / 2;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		const Rect& at = child->viewBounds;
		long across = RangeDistance(at.left, at.right, want.left, want.right);
		long down = RangeDistance(at.top, at.bottom, want.top, want.bottom);
		if (down <= vRange)
		{
			// it is on the same line: line the sides up
			long centre = (at.left + at.right) / 2;
			if (down < vRange / 2)
				hBest = kAlignTolerance;
			TryAlignment(at.left - want.left, at.left - measured.left, hShift, hBest, vRange, down);
			TryAlignment(at.right - want.right, at.right - measured.right, hShift, hBest, vRange, down);
			TryAlignment(at.left - want.right, at.left - measured.right, hShift, hBest, vRange, down);
			TryAlignment(at.right - want.left, at.right - measured.left, hShift, hBest, vRange, down);
			TryAlignment(centre - wantCentreX,
						 centre - (measured.right + measured.left) / 2, hShift, hBest, vRange, down);
		}
		if (across <= hRange)
		{
			// it is in the same column: line the tops and bottoms up
			long centre = (at.top + at.bottom) / 2;
			if (across < hRange / 2)
				vBest = kAlignTolerance;
			TryAlignment(at.top - want.top, at.top - measured.top, vShift, vBest, hRange, across);
			TryAlignment(at.bottom - want.bottom, at.bottom - measured.bottom, vShift, vBest, hRange, across);
			TryAlignment(at.top - want.bottom, at.top - measured.bottom, vShift, vBest, hRange, across);
			TryAlignment(at.bottom - want.top, at.bottom - measured.top, vShift, vBest, hRange, across);
			TryAlignment(centre - wantCentreY,
						 centre - (measured.top + measured.bottom) / 2, vShift, vBest, hRange, across);
		}
	}
	*result = measured;
	// nothing came within the ten pixels on that axis: leave it where it is
	if ((hBest < 0 ? -hBest : hBest) >= kAlignTolerance)
		hShift = 0;
	if ((vBest < 0 ? -vBest : vBest) >= kAlignTolerance)
		vShift = 0;
	// and never off the top or left of the page
	if (result->left + hShift < viewBounds.left)
		hShift = viewBounds.left - result->left;
	if (result->top + vShift < viewBounds.top)
		vShift = viewBounds.top - result->top;
	OffsetRect(result, hShift, vShift);
	if (viewBounds.right < result->right)
		result->right = viewBounds.right;
	if (viewBounds.bottom < result->bottom)
		result->bottom = viewBounds.bottom;
	return 0;
}
