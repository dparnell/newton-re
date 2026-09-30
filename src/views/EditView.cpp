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
#include "Rerecognize.h"		// aeRecognizeInk, aeRecognizeRange
#include "Inker.h"			// BusyBoxSend
#include "SoundSettings.h"	// FClicker
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
#include "DragDrop.h"
#include "ClipboardView.h"	// OffsetBoundsRef
#include "Screen.h"			// StartDrawing
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
#include "PolygonView.h"
#include "StrokeCentral.h"
#include "Polygons.h"
#include "ShapeDomain.h"
#include "Stroke.h"


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
// selection differently when scaled uses; then the resize border round
// a resizable selection, where a resize in progress (gEditViewTransform)
// puts it.
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
			// the gray border a resizable selection is resized by, where
			// a resize in progress puts it
			Rect border;
			border.top = border.bottom = kNoBounds;
			GlobalHiliteResizeBounds(&border);
			::Scale(&border, gEditViewTransform);
			DrawResizeBorder(border, &viewBounds);
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
// ROM 0x0c100ce4 gHiliteClickMakeCopy - a drag of the selection is a copy
Boolean	gHiliteClickMakeCopy = false;
// ROM 0x0c100cf4 gScalingFeeedback - the selection is being drawn scaled
Boolean	gScalingFeeedback = false;


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
// `click` asks for the click the machine makes when the caret moves
// (FClicker, when there is a caret to show).
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
				FClicker(RefVar(NILREF));
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
		FClicker(RefVar(NILREF));
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


// ROM 0x000a4360 RealDoCommand__9TEditViewFRC6RefVar (the double tap on a
// selection of ink, 0x000a48e0-0x000a4e78)
// The selected ink read again, if there is any: ==> whether it was.  The
// children that are ink shapes (IsOldInk) become kids for the sort, the
// paragraphs whose selection has an ink word are asked to read it where
// it is (command 0x1a over the selected range, the hilites kept while
// they do), any other selection is taken off; then the kids, sorted into
// reading order, each read again (command 0x19) with the remote writing
// preference off meanwhile - a stroke the sort set aside is removed
// (aeRemoveData) - and the root marked as changed.
long
TEditView::RereadSelectedInk(void)
{
	long handled = 0;
	TView* firstInk = nil;
	{
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
			if (IsOldInk(child) || ContainsHilitedInkWord(child))
			{
				firstInk = child;
				break;
			}
	}
	RefVar kids(MakeArray(0));
	if (firstInk == nil)
		return 0;
	Boolean inkWords = false;
	InvalAllHilites();
	{
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			if (ISNIL(child->FirstHilite()))
				continue;
			if (IsOldInk(child))
				AddArraySlot(kids, RefVar(MakeKidForSort(child, loop.Index())));
			else if (ContainsHilitedInkWord(child))
				inkWords = true;
			else
				child->RemoveAllHilites();
		}
	}
	gRootView->Update(nil);
	if (inkWords)
	{
		Boolean preserved = gRootView->SetPreserveHilites(true);
		InvalAllHilites();
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			if (!ContainsHilitedInkWord(child))
				continue;
			TParagraphHilite* hilite = (TParagraphHilite*) RefToAddress(RefVar(child->FirstHilite()));
			RefVar reread(MakeCommand(aeRecognizeRange, child, child->fId));
			SetFrameSlot(reread, RSSYMstart, RefVar(MAKEINT(hilite->fStart)));
			SetFrameSlot(reread, RSSYMstop, RefVar(MAKEINT(hilite->fEnd)));
			SetFrameSlot(reread, RSSYMdohilite, RefVar(TRUEREF));
			SetFrameSlot(reread, RSSYMrecconfig, RefVar(NILREF));
			gApplication->DispatchCommand(reread);
			child->RemoveAllHilites();
			handled = 1;
		}
		gRootView->SetPreserveHilites(preserved);
	}
	if (Length(kids) <= 0)
		return handled;
	RefVar order(SortTextInk(kids));
	long count = Length(order);
	CList* views = CList::Make(count);
	for (long i = 0; i < count; i++)
	{
		long index = RINT(GetArraySlotRef(order, i));
		if (index < 0)
			index = MapIndex(index);
		views->InsertAt(views->GetArraySize(), fChildren->At(index));
	}
	InvalAllHilites();
	gRootView->SetKeyView(nil, 0, 0, false);
	RefVar remote(GetPreference(RSSYMremotewriting));
	SetPreference(RSSYMremotewriting, RefVar(NILREF));
	for (long i = 0; i < count; i++)
	{
		TView* view = (TView*) views->At(i);
		view->RemoveAllHilites();
		if (RINT(GetArraySlotRef(order, i)) < 0)
			gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, view->fId)));
		else
		{
			RefVar reread(MakeCommand(aeRecognizeInk, view, view->fId));
			SetFrameSlot(reread, RSSYMdohilite, RefVar(TRUEREF));
			SetFrameSlot(reread, RSSYMrecconfig, RefVar(NILREF));
			gApplication->DispatchCommand(reread);
		}
		handled = 1;
	}
	SetPreference(RSSYMremotewriting, remote);
	delete views;
	gRootView->fDirtyFlag = true;
	return handled;
}


// ROM 0x000a4360 RealDoCommand__9TEditViewFRC6RefVar
// The editor's commands - the largest function in the view system: the
// guard a read-only page puts on the commands it will take at all, the
// pen's gestures (scrub, caret, line, the hilite stroke, the tap and the
// double tap), the words, shapes and ink the recognisers send, the keys,
// and the page's own side of adding and removing data.
//
// The ROM's shape for nearly every case: when the page takes gestures
// itself (text flag 0x2000) the view's scripts are asked first; then the
// editor's own handling; and a command it did not handle goes to
// TView::RealDoCommand - the scripts - unless they were asked already.
//
// A tap is not acted on where it arrives.  The editor notes where it was
// and asks the root view to idle it after the double-tap interval
// (Idle, reason 2); a second tap in that time turns it into something
// else, and if none comes the caret goes where the tap was.
//
// The click and the tap-drag on a selection are HiliteClick's (it drags
// or resizes the selection).  NOT YET RECONSTRUCTED: the double tap on a
// selection of text, which sends its ink to be recognised again.
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
		// and answers them all as done (aeGetContextUnits alone as not:
		// no shapes to snap to)
		CommandSetResult(cmd, id != aeGetContextUnits ? 1 : 0);
		return 1;
	}
	if (id == aeCaret)
	{
		Boolean asked = false;
		if ((TextFlags() & 0x2000) != 0)
		{
			asked = true;
			if (TView::RealDoCommand(cmd))
				return true;
		}
		if (HandleCaret((TUnitPublic*) CommandParameter(cmd)))
		{
			CommandSetResult(cmd, 1);
			return true;
		}
		// (the script only when it has not been asked already)
		return asked ? false : TView::RealDoCommand(cmd);
	}
	if (id == aeLine)
	{
		Boolean asked = false;
		if ((TextFlags() & 0x2000) != 0)
		{
			asked = true;
			if (TView::RealDoCommand(cmd))
				return true;
		}
		if (HandleLineGesture((TUnitPublic*) CommandParameter(cmd)))
		{
			CommandSetResult(cmd, 1);
			return true;
		}
		// (the script only when it has not been asked already)
		return asked ? false : TView::RealDoCommand(cmd);
	}

	if (id == aeClick)
	{
		// the pen pressed on the page: on a selection it may drag it,
		// resize it or reshape it; a click that is the second half of a
		// tap-drag is left for that
		Boolean asked = false;
		if ((TextFlags() & 0x2000) != 0)
		{
			asked = true;
			if (TView::RealDoCommand(cmd))
				return true;
		}
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		if (!PartOfTapDrag(unit))
		{
			gHiliteClickMakeCopy = false;
			if (HiliteClick(unit->Stroke()))
			{
				CommandSetResult(cmd, 1);
				return true;
			}
		}
		return asked ? false : TView::RealDoCommand(cmd);
	}
	if (id == aeTapDrag)
	{
		// a tap and then a press-and-drag on a selection: dragged as a
		// copy, the pending tap forgotten
		fTapPending = false;
		gHiliteClickMakeCopy = true;
		if (HiliteClick(((TUnitPublic*) CommandParameter(cmd))->Stroke()))
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
		Boolean asked = false;
		if ((TextFlags() & 0x2000) != 0)
		{
			asked = true;
			if (TView::RealDoCommand(cmd))
				return true;
		}
		if (AddHiliter((TUnitPublic*) CommandParameter(cmd)))
		{
			CommandSetResult(cmd, 1);
			return true;
		}
		return asked ? false : TView::RealDoCommand(cmd);
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

	if (id == aeShape)
	{
		// (0x000a56d0) a shape: any ink left for its strokes removed, the
		// selection taken away, the shapes on the page it was joined to
		// removed (its context id holds two view ids, one in each half),
		// and the clean shape put down
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		RemoveInk(this, unit->fUnit);
		RemoveAllHilites();
		ULong joined = unit->ContextID();
		if (joined != 0)
		{
			gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, (long) joined)));
			gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, (long) joined >> 16)));
		}
		Handle polygon = unit->CleanShape();
		if (polygon == nil)
			Throw(exOutOfMemory, (void*) -10007, nil);
		Boolean done = HandleShape(polygon, (long) unit->ShapeType());
		CommandSetResult(cmd, done);
		// (the ROM's shared exit: one not placed goes to the scripts)
		if (!done)
			return TView::RealDoCommand(cmd);
		return done;
	}

	if (id == aeGetContextUnits)
	{
		// (0x000a58e4) the page's polygons as shape units, for a shape
		// being drawn to snap to - only for a unit the shape recogniser
		// (0x11) or the stroke recogniser (0xc) answers, and only the
		// polygons near it unless the whole page is asked for (the
		// unit's box let out by the gravity distance, within the screen
		// and the page).  A unit the word recogniser answers (0x16) is
		// answered with no list.
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		ULong command = GetCommand(unit->GetType());
		if (command == 0x11 || command == 0x0c || command == 0x16)
		{
			TUnitList* list = nil;
			Rect near;
			unit->Bounds(&near);
			long whole = CommandIndexParameter(cmd, 0);
			if (whole == 0)
			{
				Rect screen;
				UnfixRect(&gGSScreenRect, &screen);
				InsetRect(&near, -gPixMaxContextGravity, -gPixMaxContextGravity);
				SectRect(&screen, &near, &near);
				SectRect(&viewBounds, &near, &near);
			}
			TListLoop loop(fChildren);
			TView* child;
			while ((child = (TView*) loop.Next()) != nil)
			{
				if (child == gSkipView)
					continue;
				if (whole == 0 && !Overlaps(&near, &child->viewBounds))
					continue;
				RefVar points(child->GetProto(RSSYMpoints));
				if (ISNIL(points))
					continue;
				if (command != 0x11 && command != 0x0c)
					continue;
				if (list == nil)
					list = TUnitList::Make();
				if (list == nil)
					break;
				TGeneralShapeUnit* shape = MakeGeneralShape(unit, (PolygonShape*) BinaryData(points),
															 child->viewBounds, child->fId);
				if (shape == nil)
					break;
				list->AddUnit(shape);
			}
			CommandSetResult(cmd, (Long) list);
			return true;
		}
		return TView::RealDoCommand(cmd);
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
		Boolean asked = false;
		if ((TextFlags() & 0x2000) != 0)
		{
			asked = true;
			if (TView::RealDoCommand(cmd))
				return true;
		}
		// A scrub that took something out says so in the command's
		// result, which is what tells HandleUnitList the scrub was taken -
		// it claims the scrub's stroke, so the word recogniser is not left
		// to read it as writing.  One that took nothing goes to the
		// gesture script, unless that has been asked already.
		if (Scrub((TUnitPublic*) CommandParameter(cmd)) != 0)
		{
			CommandSetResult(cmd, 1);
			return true;
		}
		if (asked)
			return false;
		return TView::RealDoCommand(cmd);
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
		// up.  On the selection of a page that takes text, and when the
		// selection has ink in it, the ink is read again instead: the
		// paragraphs' selected ink words where they are (command 0x1a),
		// and the shapes of old ink sorted into reading order
		// (SortTextInk) and each read again (command 0x19) - a stroke the
		// sort set aside (a dot, a mark over a word) is removed.
		Boolean asked = false;
		if ((TextFlags() & 0x2000) != 0)
		{
			asked = true;
			if (TView::RealDoCommand(cmd))
				return 1;
		}
		fTapPending = false;
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		Point pt = unit->Stroke()->FirstPoint();
		Boolean onSelection = PointInHilite(pt);
		Boolean takesText = ViewAllowsText(this);
		long handled = 0;
		if (takesText && onSelection)
			handled = RereadSelectedInk();
		if (handled == 0)
		{
			TBackwardViewListLoop loop(fChildren);
			for (TView* child = loop.Next(); child != nil; child = loop.Next())
				if (PtInRect(pt, &child->viewBounds) && (handled = child->RealDoCommand(cmd)) != 0)
					break;
		}
		if (handled == 0)
		{
			// nothing took it: on a page that takes text, the caret goes
			// where it was and the keyboard comes up for it
			if (!takesText)
				return asked ? false : TView::RealDoCommand(cmd);
			gAboutToOpenSoftKeyboard = true;
			HandleTap(pt);
			gAboutToOpenSoftKeyboard = false;
			OpenKeypadFor(this);
			handled = 1;
		}
		CommandSetResult(cmd, 1);
		return handled;
	}
	if (id == aeRemoveAllHilites)
	{
		// the page's own, and then the base's (which asks it again)
		RemoveAllHilites();
		return TView::RealDoCommand(cmd);
	}
	if (id == aeReplaceText)
	{
		// a paragraph's text replaced, sent to the page with the
		// paragraph's id as the parameter: passed on to that child
		long child = CommandParameter(cmd);
		if (child != kNoParameter)
		{
			TView* view = FindID(child);
			if (view != nil && view->RealDoCommand(cmd))
				return true;
		}
		return TView::RealDoCommand(cmd);
	}
	if (id == aeWord17)
	{
		// a word the page is to put down as it is: no script and no
		// selection taken away first
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		RemoveInk(this, unit->fUnit);
		Boolean done = HandleWordUnit(unit);
		CommandSetResult(cmd, done);
		if (!done)
			return TView::RealDoCommand(cmd);
		return done;
	}
	if (id == aeAddData)
		return AddDataCommand(cmd);
	if (id == aeRemoveData)
		return RemoveDataCommand(cmd);
	if (id == aePlaybackInk)
	{
		PlaybackInk(RefVar(MAKEINT(CommandParameter(cmd))));
		return true;
	}
	if (id == kInsertItemsCommand && (fFlags & (vReadOnly | vWriteProtected)) == 0)
	{
		// items put in at the caret when the caret is on the page itself
		if (HandleInsertItems(RefVar(CommandFrameParameter(cmd))))
			return true;
		return TView::RealDoCommand(cmd);
	}
	if (id != aeTap)
		return TView::RealDoCommand(cmd);
	// 0x2000 of the textFlags slot - not the viewFlags, and what it is
	// called is not yet known; the ROM tests it before letting the view's
	// own scripts see the tap
	Boolean asked = false;
	if ((TextFlags() & 0x2000) != 0)
	{
		asked = true;
		if (TView::RealDoCommand(cmd) != 0)
			return 1;						// the view's gesture script took it
	}
	fTapPending = true;
	TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
	fTapPoint = unit->Stroke()->FirstPoint();
	gRootView->AddIdler(this, 0x50 + (gDoubleTapInterval << 4), 2);
	// the tap is still offered to the view's scripts, unless they have
	// been asked already
	return asked ? false : TView::RealDoCommand(cmd);
}


// ROM 0x000a4360 RealDoCommand__9TEditViewFRC6RefVar +0x5ae8 (aeAddData)
// Data added to the page's soup (the base does it), and the paragraph it
// made - an undo putting back a paragraph that was removed - given back
// what was saved when it went: its words' correction information, and
// the caret, if the caret was in it.  An undo of the removal takes the
// selection away first.
Boolean
TEditView::AddDataCommand(RefArg cmd)
{
	if (IsUndoCommand(cmd))
		RemoveAllHilites();
	Boolean handled = TView::RealDoCommand(cmd);
	TView* child = (TView*) CommandParameter(cmd);
	if (child == nil)
		return handled;
	TimeStampTextChange(child);
	RefVar data(child->DataFrame());
	RefVar saved(GetFrameSlotRef(data, RSSYMcorrectinfo));
	if (NOTNIL(saved))
	{
		InsertRange(RefVar(CorrectInfo()), saved, child);
		RemoveSlot(data, RSSYMcorrectinfo);
	}
	RefVar caret(GetFrameSlotRef(data, RSSYMinsertoffset));
	if (NOTNIL(caret))
	{
		gRootView->SetKeyView(child, RINT(caret), 0, false);
		RemoveSlot(data, RSSYMinsertoffset);
	}
	RemoveSlot(data, RSSYMoffset);
	return handled;
}


// ROM 0x000a4360 RealDoCommand__9TEditViewFRC6RefVar +0x26c (aeRemoveData)
// A child taken out of the page's soup, the base doing it, and what an
// undo will want to put back with it saved in its data: its words'
// correction information (taken out of the list), and the caret offset
// when the caret was in it - the caret moves to the page meanwhile.
Boolean
TEditView::RemoveDataCommand(RefArg cmd)
{
	if (IsUndoCommand(cmd))
		RemoveAllHilites();
	TView* child = FindID(CommandParameter(cmd));
	if (child == nil)
		return true;
	child->RemoveAllHilites();
	RefVar data(child->DataFrame());
	RefVar saved(ExtractRange(RefVar(CorrectInfo()), child, 0, -1));
	DeletedCorrectionInfo(RefVar(CorrectInfo()), child);
	if (NOTNIL(saved))
		SetFrameSlot(data, RSSYMcorrectinfo, saved);
	Boolean hadCaret = false;
	if (gRootView->CaretEnabled() && gRootView->fCaretView == child)
	{
		SetFrameSlot(data, RSSYMinsertoffset, RefVar(MAKEINT(gRootView->fCaretOffset)));
		hadCaret = true;
		Rect caret;
		child->OffsetToCaret(0, &caret);
		SetCaretRectGlobal(caret);
	}
	Boolean handled = TView::RealDoCommand(cmd);
	if (hadCaret)
		gRootView->SetKeyView(this, 0, 0, false);
	return handled;
}


// ROM 0x000a6a04 HandleInsertItems__9TEditViewFRC6RefVar
// Items put in at the caret when the caret is on the page itself rather
// than in a paragraph: an empty paragraph made where the caret is, the
// caret moved into it, and the items given to that.  ==> whether they
// were taken; nothing when the caret is not this page's.
Boolean
TEditView::HandleInsertItems(RefArg spec)
{
	ValidateCaret(true);
	if (gRootView->fCaretView != this)
		return false;
	Rect box = fCaretRect;
	Point origin = ContentsOrigin();
	OffsetRect(&box, origin.h, origin.v);
	UniChar none = 0;
	long offset = 0;
	TView* paragraph = AddNewParagraph(&none, 0, box, box, nil, RefVar(NILREF), &offset, RefVar(NILREF));
	fCaretRect.top = -32768;
	fCaretRect.bottom = -32768;
	gRootView->SetKeyView(paragraph, 0, 0, false);
	return ((TParagraphView*) paragraph)->HandleInsertItems(spec);
}


// ROM 0x000a6b30 PlaybackInk__9TEditViewFRC6RefVar
// The page's ink recognised again: the recognition preferences set for
// the kind asked for (0 text, 1 shapes, 2 both; the formulas off), each
// child that has ink handed back to the stroke world as deferred strokes
// at its place on the page, and the preferences put back.
void
TEditView::PlaybackInk(RefArg kind)
{
	RefVar text(GetPreference(RSSYMdotextrecognition));
	RefVar shapes(GetPreference(RSSYMdoshaperecognition));
	RefVar formulas(GetPreference(RSSYMdoformularecognition));
	long which = RINT(kind);
	SetPreference(RSSYMdotextrecognition, (which == 0 || which == 2) ? RefVar(TRUEREF) : RefVar(NILREF));
	SetPreference(RSSYMdoshaperecognition, (which == 1 || which == 2) ? RefVar(TRUEREF) : RefVar(NILREF));
	SetPreference(RSSYMdoformularecognition, RefVar(NILREF));
	ReadDomainOptions();
	RefVar ink;
	TListLoop loop(fChildren);
	for (TView* child = (TView*) loop.Next(); child != nil; child = (TView*) loop.Next())
	{
		ink = child->GetProto(RSSYMink);
		if (NOTNIL(ink))
			gStrokeWorld.AddDeferredStroke(ink, child->viewBounds.left, child->viewBounds.top);
	}
	SetPreference(RSSYMdotextrecognition, text);
	SetPreference(RSSYMdoshaperecognition, shapes);
	SetPreference(RSSYMdoformularecognition, formulas);
	ReadDomainOptions();
}


// ROM 0x000a74d8 PartOfTapDrag__FP11TUnitPublic
// Whether a click came within 60 ticks of the last one asked about - the
// second half of a tap-drag, which the tap-drag command handles.  The
// click is remembered as the last either way.
ULong	gLastTapDragClick = 0;			// ROM 0x0c100ce8 gLastTapDragClick

Boolean
PartOfTapDrag(TUnitPublic* unit)
{
	ULong last = gLastTapDragClick;
	gLastTapDragClick = unit->StartTime();
	return (uint32_t) (unit->StartTime() - last) < 0x3c;
}


// ROM 0x001a2a74 ViewAllowsText__FP5TView
// Whether the view writing lands on takes any kind of text (its
// recognition flags: words, letters, numbers, punctuation and the rest).
Boolean
ViewAllowsText(TView* view)
{
	return (GetRecognitionView(view)->fFlags & 0x17ef000) != 0;
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
		RgnHandle clip = port->visRgn;		// (the ROM's [port,#0x24] at 0x000aaa5c: the visRgn, which the decompiler calls clipRgn)
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
		CopyRgn(visible, port->visRgn);		// (0x000aab4c)
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
	// The root view's context always has a `correct` view frame on the
	// machine - the corrector is one of the root's own children - so the
	// ROM asks it for its viewCObject straight away; a host test's root
	// built without the ROM's root template has none, and the slot is
	// nil, which GetFrameSlotRef would throw on (host guard).
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
// With remote writing on (or the corrector's selection in a child), a
// written word goes to the caret wherever it was written
// (0x000abe58-0x000ac0ac): into the caret's paragraph through
// InsertItemsAtCaret, onto the end of the text under a caret on the page
// itself, or into a new paragraph.
//
// ==> the view the word ended up in - on the two caret paths, the
// caller's own leftover (the ROM bug below).
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

	// ROM bug: the best child is kept in r8, which is set only when a
	// child answers better than the one before - never cleared first.
	// Two paths below put the word in without choosing a child (at the
	// caret, and at the end of the text under the caret), so the view
	// answered there is whatever r8 held in the caller.  Only
	// HandleWordUnit can reach them (CleanupData and JamText pass no
	// unit), and it has the word's text pointer in r8 at the call - so
	// the ROM answers the text as the view, which HandleWordUnit only
	// tests for nil: the word counts as taken, and the recogniser claims
	// its strokes.  Kept by starting from the text pointer; answering nil
	// instead makes the command's result 0, and the arbiter then turns
	// the strokes of the word just inserted into ink as well.
	TView* best = (TView*) text;
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

// A view the shape domain is not to snap to (NOT YET: who sets it).
TView* gSkipView = nil;


// ROM 0x000a654c HandleShape__9TEditViewFPP7Polygonl
Boolean
TEditView::HandleShape(Handle polygon, long type)
{
	long pen = RINT(GetPreference(RSSYMuserpensize));
	HLock(polygon);
	Polygon* poly = (Polygon*) *polygon;
	Rect box = poly->polyBBox;
	Point origin = ContentsOrigin();
	OffsetRect(&box, -origin.h, -origin.v);
	Point spacing;
	if (IsGridded(RefVar(RSSYMsquaregrid), &spacing))
	{
		if ((ULong) type < 3 || type == 10 || type == 11)
			AlignRectToGrid(&box, spacing);
		if (type == 4 || type == 5 || type == 8 || type == 9)
		{
			long count = PolyPointCount(poly);
			for (long i = 0; i < count; i++)
				AlignPtToGrid(&poly->polyPoints[i], spacing);
			AlignRectToGrid(&box, spacing);
		}
	}
	RefVar form(MakePolygonForm(poly->polyPoints, PolyPointCount(poly), type, box, pen));
	if (NOTNIL(GetProtoVariable(fContext, RSSYMeditaddshapescript, nil)))
	{
		RefVar args(MakeArray(1));
		SetArraySlot(args, 0, form);
		form = RunScript(RSSYMeditaddshapescript, args, true, nil);
	}
	AddForm(form);
	HUnlock(polygon);
	return true;
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
// The geometry (0x000a1eb4-0x000a22bc) is the path taken when the word
// came from the recogniser or has an ink font - it measures the text with
// TextBounds, lines the result up with the page's other children
// (AlignBounds) and with its ruled lines (AlignToLineSpacing), and is what
// makes handwriting tidy itself into columns.  Typed text does not go that
// way: the keyboard has already measured its own box, so the ROM jumps
// straight over the whole section (the test at 0x000a1e94).  NOT YET
// RECONSTRUCTED: the remote-writing caret's point (0x000a1b98) and the
// word offered to the dictionary (AddWordInfo).
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


/*------------------------------------------------------------------------------
	D r a g   a n d   d r o p

	What a page does as the source and as the target of a drag.  As the
	source it offers the drag items of each selected child in reading
	order; the data of an item is the child's own (its GetDropData, else
	a copy of its data frame) moved into the page's coordinates.  As the
	target it takes text, polygons, ink and pictures, each added as a
	child through an undoable aeAddData with the stationery that shows it,
	and a drag let go on the page it came from moves the child rather than
	copying it.  A paragraph the pen is over that is not wholly selected
	takes a drop of its own (FindDropView), which is how text is dragged
	into the middle of other text.
------------------------------------------------------------------------------*/

// ROM 0x000a8c78 GetDragInfo__9TEditViewFP9TDragInfoUc
// The drag items of the selected children, in reading order; a copy
// leaves out a child whose data may not be copied.
void
TEditView::GetDragInfo(TDragInfo* dragInfo, Boolean copy)
{
	TView** sorted = GetHilitedViewsSorted();
	if (sorted == nil)
		return;
	TView** each = sorted;
	for (long count = CountHilites(); --count >= 0; )
	{
		TView* child = *each++;
		if (!copy || (child->CopyProtection() & 1) == 0)
			child->AddDragInfo(dragInfo);
	}
	delete[] sorted;
}


// ROM 0x000a8cf4 AddDragInfo__9TEditViewFP9TDragInfo
// The page's script first; failing that the items of every selected
// child whose data may be copied.  ==> whether there was a selection.
Boolean
TEditView::AddDragInfo(TDragInfo* dragInfo)
{
	if (TView::AddDragInfo(dragInfo))
		return true;
	TView** sorted = GetHilitedViewsSorted();
	if (sorted == nil)
		return false;
	TView** each = sorted;
	for (long count = CountHilites(); --count >= 0; )
	{
		TView* child = *each++;
		if ((child->CopyProtection() & 1) == 0)
			child->AddDragInfo(dragInfo);
	}
	delete[] sorted;
	return true;
}


// ROM 0x000a8a94 GetSupportedDropTypes__9TEditViewFRC6TPoint
// The page's script first; failing that what a page can show.
Ref
TEditView::GetSupportedDropTypes(const Point& pt)
{
	RefVar types(TView::GetSupportedDropTypes(pt));
	if (ISNIL(types))
	{
		types = MakeArray(4);
		SetArraySlot(types, 0, RefVar(RSSYMtext));
		SetArraySlot(types, 1, RefVar(RSSYMpolygon));
		SetArraySlot(types, 2, RefVar(RSSYMink));
		SetArraySlot(types, 3, RefVar(RSSYMpicture));
	}
	return types;
}


// ROM 0x000a8b48 GetDropData__9TEditViewFRC6RefVarT1
// The data of a dragged child (the drag ref is its context): the child's
// own answer with its viewBounds moved by where the child is on the
// page, or, when it has none, a copy of its data frame as it stands.
// Any other drag ref is the page's script's business.
Ref
TEditView::GetDropData(RefArg dragType, RefArg dragRef)
{
	TView* child;
	if (!IsFrame(dragRef) || (child = GetView(dragRef)) == nil)
		return TView::GetDropData(dragType, dragRef);
	RefVar data(child->GetDropData(dragType, dragRef));
	if (ISNIL(data))
		data = DeepClone(RefVar(child->DataFrame()));
	else
	{
		Point by;
		by.h = (short) (child->viewBounds.left - viewBounds.left);
		by.v = (short) (child->viewBounds.top - viewBounds.top);
		OffsetBoundsRef(data, by);
	}
	return data;
}


// ROM 0x000a8d7c FindDropView__9TEditViewFRC9TDragInfoRC6TPoint
// Where on the page a drag at the point goes: to a paragraph whose text
// the point is in - one the drag suits and which is not wholly selected
// (dropping a selection on itself is a move of the whole) - and otherwise
// to the page.
TView*
TEditView::FindDropView(const TDragInfo& dragInfo, const Point& pt)
{
	TView* target = TView::FindDropView(dragInfo, pt);
	if (target == this)
	{
		Point at = pt;
		TView* child = TextContainingPoint(at, nil, nil);
		if (child != nil)
		{
			Rect caret;
			child->PointToCaret(at, &caret, nil);
			if (caret.top == -32768)
				child = nil;
		}
		if (child != nil && child->AcceptDrop(dragInfo, pt))
		{
			RefVar hilite(child->FirstHilite());
			if (ISNIL(hilite) || !child->IsCompletelyHilited(hilite))
				target = child;
		}
	}
	return target;
}


// ROM 0x000a8f34 Drop__9TEditViewFRC6RefVarT1P6TPoint
// Data dropped on the page, when its script does not take it: added as
// a child with an undoable aeAddData.  Text becomes a paragraph (its
// textFlags dropped, and a paragraph with no width given the page's
// right edge as its own), a picture a picture view and a polygon or ink
// a polygon view - unless the data names a stationery of its own.  The
// new child is selected and made the page's hiliter, and one that would
// start above the page is moved down onto it.  The drop point is not
// looked at: the data's viewBounds say where it goes.
Boolean
TEditView::Drop(RefArg dropType, RefArg dropData, Point* dropPt)
{
	if (TView::Drop(dropType, dropData, dropPt))
		return true;
	RefVar cmd(MakeCommand(aeAddData, this, 0x08000000));
	RefVar stationery;
	if (EQRef(dropType, RSSYMtext))
	{
		stationery = RSSYMpara;
		RefVar bounds(GetFrameSlotRef(dropData, RSSYMviewbounds));
		RemoveSlot(dropData, RSSYMtextflags);
		if (NOTNIL(bounds))
		{
			Rect r;
			FromObject(bounds, r);
			if (r.left == r.right)
			{
				r.right = viewBounds.right;
				SetBoundsRect(bounds, r);
			}
		}
	}
	else if (EQRef(dropType, RSSYMpicture))
		stationery = RSSYMpict;
	else if (EQRef(dropType, RSSYMpolygon) || EQRef(dropType, RSSYMink))
		stationery = RSSYMpoly;
	if (NOTNIL(stationery) && ISNIL(GetProtoVariable(dropData, RSSYMviewstationery, nil)))
		SetFrameSlot(dropData, RSSYMviewstationery, stationery);
	CommandSetFrameParameter(cmd, dropData);
	gApplication->DispatchCommand(cmd);
	TView* child = (TView*) CommandParameter(cmd);
	if (child != nil)
	{
		child->HiliteAll();
		gRootView->fHiliter = this;
		if (child->viewBounds.top < viewBounds.top)
		{
			Point by;
			by.v = (short) (viewBounds.top - child->viewBounds.top);
			by.h = 0;
			child->DoMoveCommand(by);
		}
	}
	return true;
}


// ROM 0x000a91ec DropMove__9TEditViewFRC6RefVarRC6TPointT2Uc
// A child dragged about on its own page, when the page's script does not
// take it.  A child that is wholly selected, and is simply being moved,
// is moved by the distance (no higher than the page's top) with an
// undoable aeMoveData.  Otherwise - a copy, or part of a paragraph - the
// data is fetched as for a drop elsewhere, moved into the page's
// coordinates (and down onto the page if it would start above it) and
// dropped, the original then losing its selection (a copy) or the part
// that was dragged (a move).
Boolean
TEditView::DropMove(RefArg dragRef, const Point& delta, const Point& dropPt, Boolean copy)
{
	if (TView::DropMove(dragRef, delta, dropPt, copy))
		return true;
	TView* child = FailGetView(dragRef);
	RefVar hilite(child->FirstHilite());
	Boolean whole = child->IsCompletelyHilited(hilite);
	if (whole && !copy && child->fParent == this)
	{
		Point by = delta;
		if (child->viewBounds.top + delta.v < viewBounds.top)
			by.v = (short) (viewBounds.top - child->viewBounds.top);
		child->DoMoveCommand(by);
		return true;
	}
	TDragInfo dragInfo(0L);
	child->AddDragInfo(&dragInfo);
	RefVar type(dragInfo.GetItemIndType(0, 0));
	RefVar data(GetDropData(type, dragRef));
	Point origin;
	GetChildOrigin(&origin);
	Point by;
	by.h = (short) (delta.h + origin.h);
	by.v = (short) (delta.v + origin.v);
	OffsetBoundsRef(data, by);
	Rect bounds;
	FromObject(RefVar(GetFrameSlotRef(data, RSSYMviewbounds)), bounds);
	if (bounds.top < 0)
	{
		by.v = (short) -bounds.top;
		by.h = 0;
		OffsetBoundsRef(data, by);
	}
	Point pt = dropPt;
	Drop(type, data, &pt);
	if (copy)
		child->RemoveAllHilites();
	else
		child->DropRemove(dragRef);
	return true;
}


// ROM 0x000a94b4 DropRemove__9TEditViewFRC6RefVar
// A child's dragged data taken off the page after a move: the child's
// own DropRemove (part of a paragraph), and failing that the whole child
// removed with an undoable aeRemoveData.
Boolean
TEditView::DropRemove(RefArg dragRef)
{
	if (TView::DropRemove(dragRef))
		return true;
	TView* child = FailGetView(dragRef);
	if (!child->DropRemove(dragRef))
		gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, child->fId)));
	return true;
}


// ROM 0x000a9540 DropDone__9TEditViewFv
// The drop over: the page's script, then the key view worked out again
// from what is selected now.
Boolean
TEditView::DropDone(void)
{
	TView::DropDone();
	DetermineKeyView();
	return true;
}


/*------------------------------------------------------------------------------
	C l i c k s   o n   t h e   s e l e c t i o n

	The pen pressed on a page's selection (HiliteClick).  On the gray
	border of a selection that can be resized it resizes the selected
	children (TrackScale, and CleanupData when the pen did not move); on a
	corner of a selected shape of straight sides (ClickOptions bit 4) it
	drags the corner (TrackDistort); anywhere else on the selection it
	drags it.
------------------------------------------------------------------------------*/

// ROM 0x000a370c ClipBoxToBox__FP5TRectPC5TRect
// A rectangle cut back to within another.
void
ClipBoxToBox(Rect* box, const Rect* limit)
{
	if (box->left < limit->left)
		box->left = limit->left;
	if (limit->right < box->right)
		box->right = limit->right;
	if (box->top < limit->top)
		box->top = limit->top;
	if (limit->bottom < box->bottom)
		box->bottom = limit->bottom;
}


// ROM 0x000a3780 DrawResizeBorder__FRC5TRectPC5TRect
// The gray border a resizable selection is resized by: four pixels of
// gray, exclusive-ored, eight outside the selection, kept within the
// limit when there is one.
void
DrawResizeBorder(const Rect& bounds, const Rect* limit)
{
	Rect r = bounds;
	InsetRect(&r, -8, -8);
	if (limit != nil)
		ClipBoxToBox(&r, limit);
	SetFgPattern(GetStdPattern(grayPat));
	PenMode(patXor);
	PenSize(4, 4);
	FrameRect(&r);
	PenNormal();
}


// ROM 0x000aabb0 HiliteClick__9TEditViewFP13TStrokePublic
// The pen pressed on the page's selection.  ==> whether it did anything
// with it.
Boolean
TEditView::HiliteClick(TStrokePublic* stroke)
{
	Point pt = stroke->FirstPoint();
	Rect bounds;
	StartGathering(&bounds);
	long options = GlobalHiliteBounds(&bounds);
	if (GatheredNothing(&bounds))
		return false;
	Boolean resizable = (options & 2) != 0;
	Rect grab;
	if (!resizable)
		grab = bounds;
	else
	{
		StartGathering(&grab);
		GlobalHiliteResizeBounds(&grab);
		ToOutsideGrayBorder(&grab, &viewBounds);
	}
	if (!PtInRect(pt, &grab))
		return false;
	if (resizable && (fFlags & vWriteProtected) == 0)
	{
		// on the gray border: a resize
		Rect inside = grab;
		InsetRect(&inside, 8, 8);
		if (!PtInRect(pt, &inside))
		{
			Rect selected;
			GlobalSelectedBounds(&selected);
			if ((TextFlags() & 0x40) == 0 && !TrackScale(pt, stroke, selected))
				CleanupData();
			return true;
		}
	}
	if ((options & 4) != 0 && (fFlags & vWriteProtected) == 0
		&& TrackDistort(pt, stroke, bounds))
		return true;
	if ((options & 1) == 0)
		return false;
	if (!resizable && !PointInHilite(pt))
		return false;
	Rect pinned;
	StartGathering(&pinned);
	GlobalHilitePinnedBounds(&pinned);
	gLassoedDrag = (fClickOptions & 2) != 0;
	TDragInfo dragInfo(0L);
	GetDragInfo(&dragInfo, gHiliteClickMakeCopy);
	Boolean dragged = false;
	if (Length(RefVar(dragInfo.GetItems())) != 0 && !stroke->Done() && !fTapPending)
	{
		DragAndDrop(stroke, grab, &pinned, &pinned, gHiliteClickMakeCopy, dragInfo, nil);
		dragged = true;
	}
	gLassoedDrag = false;
	return dragged;
}


// ROM 0x000a6384 DrawScaledViews__9TEditViewFRC5TRectT1
// The selected children drawn as a resize from `src` to `dst` would leave
// them, and the gray border round them all.
void
TEditView::DrawScaledViews(const Rect& src, const Rect& dst)
{
	PenNormal();
	Rect bounds;
	StartGathering(&bounds);
	GlobalHiliteBounds(&bounds);
	if (GatheredNothing(&bounds))
		return;
	Rect all;
	StartGathering(&all);
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->Hilited())
		{
			Rect drawn;
			child->DrawScaledData(src, dst, &drawn);
			Union(&all, &drawn);
		}
	}
	DrawResizeBorder(all, &viewBounds);
	PenNormal();
}


// ROM 0x000a9560 DiceHilited__9TEditViewFv
// Every child only part of which is selected cut in two, so that what is
// selected is a whole view of its own: the next such child each time
// round, until there is none.
void
TEditView::DiceHilited(void)
{
	RefVar hilite;
	for ( ; ; )
	{
		TDataView* partial = nil;
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			hilite = child->FirstHilite();
			if (NOTNIL(hilite) && !child->IsCompletelyHilited(hilite))
			{
				partial = (TDataView*) child;
				break;
			}
		}
		if (partial == nil)
			break;
		Point none;
		none.h = none.v = 0;
		partial->DiceHilited(hilite, this, none, false);
	}
}


// ROM 0x000a7b18 TrackScale__9TEditViewF6TPointP13TStrokePublicRC5TRect
// The selection resized by its gray border.  The side of the selected
// children's bounds the pen went down nearer to follows the pen, the
// other stays put (vertically and horizontally each), no closer than 16
// pixels to it and not beyond the page; on a square grid the corner is
// snapped to it.  As the pen moves, the page's picture is drawn with the
// selected children scaled into the new rectangle (DrawScaledViews,
// through gEditViewTransform) over the screen as it was without them.
// When the pen is lifted, children only part of which is selected are cut
// in two first, and each selected child is sent an undoable aeScaleData
// from the old bounds to the new ones (a polygon's arcerBounds with it).
// ==> whether the pen moved at all (more than four pixels).
//
// (the ROM first tells the busy box a resize is being tracked -
//  BusyBoxSend 0x37 - which the views layer cannot reach from here)
Boolean
TEditView::TrackScale(Point pt, TStrokePublic* stroke, const Rect& selected)
{
	Point size;
	size.v = (short) (selected.bottom - selected.top);
	size.h = (short) (selected.right - selected.left);
	BusyBoxSend(0x37);
	stroke->InkOff(true);
	Rect page = viewBounds;
	Point anchor;
	Rect limit;
	if (selected.top + (short) (selected.bottom - selected.top) / 2 < pt.v)
	{
		// the lower half: the bottom follows the pen
		anchor.v = selected.top;
		limit.top = (short) (selected.top + 16);
		limit.bottom = page.bottom;
	}
	else
	{
		anchor.v = selected.bottom;
		size.v = (short) -size.v;
		limit.bottom = (short) (selected.bottom - 16);
		limit.top = page.top;
	}
	if (selected.left + (short) (selected.right - selected.left) / 2 < pt.h)
	{
		anchor.h = selected.left;
		limit.left = (short) (selected.left + 16);
		limit.right = page.right;
	}
	else
	{
		anchor.h = selected.right;
		size.h = (short) -size.h;
		limit.right = (short) (selected.right - 16);
		limit.left = page.left;
	}
	Point lastPt;
	lastPt.v = -32768;
	lastPt.h = 0;
	Rect newBounds = selected;
	Boolean moved = false;
	DragBits bits(this, nil, false);
	Point spacing;
	Boolean gridded = IsGridded(RefVar(RSSYMsquaregrid), &spacing);
	TRegion visRgn(SetupVisRgn());
	TRegionVar vis(visRgn);
	unwind_protect
	{
		while (!stroke->Done())
		{
			Point pen = stroke->FinalPoint();
			if (!moved)
				moved = CheapDistance(pen, pt) > 4;
			if (moved && (pen.h != lastPt.h || pen.v != lastPt.v))
			{
				Point corner;
				corner.h = (short) (anchor.h + size.h);
				corner.v = (short) (anchor.v + size.v);
				corner.h = (short) (corner.h + pen.h - pt.h);
				corner.v = (short) (corner.v + pen.v - pt.v);
				if (gridded)
					AlignPtToGrid(&corner, spacing);
				PinTo(&corner, &limit);
				newBounds.top = anchor.v;
				newBounds.left = anchor.h;
				newBounds.bottom = corner.v;
				newBounds.right = corner.h;
				Flip(&newBounds);
				gEditViewTransform.Setup(&selected, &newBounds, false);
				Rect r = viewBounds;
				bits.fDataBits.SetPort();
				EraseRect(&r);
				gScalingFeeedback = true;
				DrawScaledViews(selected, newBounds);
				gScalingFeeedback = false;
				bits.fDataBits.RestorePort();
				StartDrawing(nil, nil);
				bits.fBackground.Draw(r, r, srcCopy, nil);
				bits.fDataBits.Draw(r, r, srcXor, nil);
				StopDrawing(nil, nil);
				lastPt = pen;
			}
			else
				Wait(1);
		}
	}
	on_unwind
	{
		// the port's visible region put back, a Throw or not
		GrafPort* port;
		GetPort(&port);
		CopyRgn(vis, port->visRgn);
	}
	end_unwind;
	gEditViewTransform.Setup(nil, nil, false);
	PenNormal();
	Dirty(nil);
	if (moved)
	{
		DiceHilited();
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			if (!child->Hilited())
				continue;
			RefVar cmd(MakeCommand(aeScaleData, child, kNoParameter));
			CommandSetIndexRect(cmd, 0, selected);
			CommandSetIndexRect(cmd, 2, newBounds);
			RefVar arc(GetFrameSlotRef(RefVar(child->DataFrame()), RSSYMarcerbounds));
			if (NOTNIL(arc))
				CommandSetIndexFrame(cmd, 4, arc);
			gApplication->DispatchCommand(cmd);
		}
	}
	return moved;
}


// ROM 0x000a9634 TrackDistort__9TEditViewF6TPointP13TStrokePublicRC5TRect
// A corner of a selected shape dragged.  The pen has to have gone down
// within eight pixels (CheapDistance) of a corner of a selection that
// takes it (a child answering ClickOptions bit 4 - a TPolygonView of
// straight sides), and up to four corners go together, where selected
// shapes share one.  Each such shape is first diced (TDataView::
// DiceHilited: the selection copied into a new view and the old one
// removed, which is a whole shape here), and the corners are then found
// again on the new views.  As the pen moves - onto the square grid when
// the page has one, and never off the page - the corners follow it and
// the hilites are drawn, over the screen as it was without them, into the
// box the moved hilites cover.  When the pen is lifted each corner goes to
// its shape as command 0x43 (the point's index and where it went on the
// page).  ==> whether a corner was under the pen.  (`bounds`, the
// selection's, is not looked at.)
//
// (the ROM first tells the busy box a drag is being tracked -
//  BusyBoxSend 0x37 - which the views layer cannot reach from here)
Boolean
TEditView::TrackDistort(Point pt, TStrokePublic* stroke, const Rect& /*bounds*/)
{
	struct Corner
	{
		TPolygonHilite*	hilite;		// +0x00
		TView*			view;		// +0x04
		long			index;		// +0x08  of the point in the hilite's shape
		Point			topLeft;	// +0x0c  the view's
		Point			orig;		// +0x10  the point where it was
		Point			cur;		// +0x14  and where it is now
		Point*			at;			// +0x18  the point in the hilite's shape
	};
	Corner corners[4];
	long n = 0;

	// the shapes with a corner under the pen: the first corner of each
	// notes the view, the rest of its corners nil
	{
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			long found = 0;
			HiliteLoop hilites(child);
			while (hilites.Next())
			{
				if ((child->ClickOptions() & 4) == 0)
					continue;
				TPolygonHilite* hilite = (TPolygonHilite*) hilites.fCurrent;
				Point topLeft;
				topLeft.v = child->viewBounds.top;
				topLeft.h = child->viewBounds.left;
				Rect r = hilite->fBounds;
				OffsetRect(&r, topLeft.h, topLeft.v);
				InsetRect(&r, -8, -8);
				if (!PtInRect(pt, &r))
					continue;
				Point* p = hilite->fShape->fPoints;
				for (long i = 0; i < hilite->fShape->fCount; i++, p++)
				{
					Point corner;
					corner.v = (short) (p->v + topLeft.v);
					corner.h = (short) (p->h + topLeft.h);
					if (n < 4 && CheapDistance(corner, pt) < 8)
					{
						corners[n++].view = (found++ == 0) ? child : nil;
					}
				}
			}
		}
	}
	if (n == 0)
		return false;

	BusyBoxSend(0x37);
	stroke->InkOff(true);
	for (long i = 0; i < n; i++)
	{
		TView* view = corners[i].view;
		if (view != nil)
		{
			Point none;
			none.h = none.v = 0;
			((TDataView*) view)->DiceHilited(RefVar(view->FirstHilite()), this, none, false);
		}
	}

	// the corners again, on the diced shapes
	n = 0;
	{
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			HiliteLoop hilites(child);
			while (hilites.Next())
			{
				if ((child->ClickOptions() & 4) == 0
				 || !child->IsCompletelyHilited(hilites.fHilite))
					continue;
				TPolygonHilite* hilite = (TPolygonHilite*) hilites.fCurrent;
				Point topLeft;
				topLeft.v = child->viewBounds.top;
				topLeft.h = child->viewBounds.left;
				Rect r = hilite->fBounds;
				OffsetRect(&r, topLeft.h, topLeft.v);
				InsetRect(&r, -8, -8);
				if (!PtInRect(pt, &r))
					continue;
				Point* p = hilite->fShape->fPoints;
				for (long i = 0; i < hilite->fShape->fCount; i++, p++)
				{
					Point corner;
					corner.v = (short) (p->v + topLeft.v);
					corner.h = (short) (p->h + topLeft.h);
					if (n < 4 && CheapDistance(corner, pt) < 8)
					{
						Corner& c = corners[n++];
						c.topLeft = topLeft;
						c.orig = *p;
						c.cur = *p;
						c.hilite = hilite;
						c.view = child;
						c.index = i;
						c.at = p;
					}
				}
			}
		}
	}

	Rect page = viewBounds;
	Point lastPt;
	lastPt.v = -32768;
	lastPt.h = 0;
	DragBits bits(this, nil, false);
	Point spacing;
	Boolean gridded = IsGridded(RefVar(RSSYMsquaregrid), &spacing);
	TRegion visRgn(SetupVisRgn());
	TRegionVar vis(visRgn);
	unwind_protect
	{
		while (!stroke->Done())
		{
			Point pen = stroke->FinalPoint();
			if (gridded)
				AlignPtToGrid(&pen, spacing);
			PinTo(&pen, &page);
			if (pen.h != lastPt.h || pen.v != lastPt.v)
			{
				Rect r;
				StartGathering(&r);
				GlobalHiliteBounds(&r);
				for (long i = 0; i < n; i++)
				{
					Corner& c = corners[i];
					c.cur.h = (short) (c.orig.h + pen.h - pt.h);
					c.cur.v = (short) (c.orig.v + pen.v - pt.v);
					if (gridded)
						AlignPtToGrid(&c.cur, spacing);
					*c.at = c.cur;
					c.hilite->UpdateBounds();
					Rect moved = c.hilite->fBounds;
					OffsetRect(&moved, c.topLeft.h, c.topLeft.v);
					Union(&r, &moved);
				}
				InsetRect(&r, -8, -8);
				bits.fDataBits.SetPort();
				EraseRect(&r);
				DrawHiliting();
				bits.fDataBits.RestorePort();
				StartDrawing(nil, nil);
				bits.fBackground.Draw(r, r, srcCopy, nil);
				bits.fDataBits.Draw(r, r, srcXor, nil);
				StopDrawing(nil, nil);
				lastPt = pen;
			}
			else
				Wait(1);
		}
	}
	on_unwind
	{
		// the port's visible region put back, a Throw or not
		GrafPort* port;
		GetPort(&port);
		CopyRgn(vis, port->visRgn);
	}
	end_unwind;
	PenNormal();

	for (long i = 0; i < n; i++)
	{
		Corner& c = corners[i];
		Point where;
		where.h = (short) (c.cur.h + c.topLeft.h);
		where.v = (short) (c.cur.v + c.topLeft.v);
		RefVar cmd(MakeCommand(0x43, c.view, 0x8000000));
		CommandSetIndexParameter(cmd, 1, (Long) (((ULong) (unsigned short) where.v << 16) | (unsigned short) where.h));
		CommandSetIndexParameter(cmd, 0, c.index);
		gApplication->DispatchCommand(cmd);
	}
	gRootView->fDirtyFlag = true;
	return true;
}


// ROM 0x000aafcc CleanupData__9TEditViewFv
// A click on the resize border that did not move: the selected
// paragraphs joined into the first of them - each one's text put in
// under it as a word written there would be (HandleWord, with the
// paragraph's own data), and the paragraph itself removed with an
// undoable aeRemoveData - and the first then tidied (its CleanupData),
// the selection taken away and the caret put at its end.
void
TEditView::CleanupData(void)
{
	TView** sorted = GetHilitedViewsSorted();
	if (sorted == nil)
		return;
	long count = CountHilites();
	TView* first = nil;
	for (long i = 0; i < count; i++)
	{
		TView* child = sorted[i];
		if (!child->DerivedFrom(clParagraphView))
			continue;
		if (first == nil)
		{
			first = child;
			continue;
		}
		RefVar text(child->GetProto(RSSYMtext));
		UniChar* chars = (UniChar*) GetCString(text);
		ULong length = Ustrlen(chars);
		if ((long) length > 0)
		{
			// a line's height under the first paragraph's bottom
			Rect box = first->viewBounds;
			box.top = box.bottom;
			box.bottom = (short) (box.top + 1);
			HandleWord(chars, length, box, box, nil, RefVar(child->DataFrame()), nil);
		}
		gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, child->fId)));
	}
	if (first != nil)
	{
		((TDataView*) first)->CleanupData();
		RemoveAllHilites();
		RefVar text(((TParagraphView*) first)->Text());
		gRootView->SetKeyView(first, Length(text) / 2 - 1, 0, false);
	}
	delete[] sorted;
}
