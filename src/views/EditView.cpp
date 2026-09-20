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
#include "Hilites.h"
#include "DataView.h"
#include "ParagraphView.h"
#include "RootView.h"
#include "Application.h"
#include "RichString.h"
#include "Text.h"
#include "Keyboard.h"
#include "Commands.h"
#include "UnitPublic.h"
#include "StrokeQueue.h"
#include "Bits.h"
#include "ViewFlags.h"
#include "Rects.h"
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
	fUnknown4C = false;
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
// are wanted, bit 1 when text is.  The ROM asks the view its recognition
// settings are configured from (GetRecognitionView) for a configuration
// frame built out of its flags (BuildRecConfig), and reads
// doInkWordRecognition and doTextRecognition out of it.
//
// NOT YET RECONSTRUCTED: GetRecognitionView 0x001a2a24 and BuildRecConfig
// 0x001a1e5c, which are the whole of it.  With no configuration to ask,
// nothing is enabled - so a tap on the empty part of a page does not open
// a paragraph to write in, where the machine would.
long
TextOrInkWordsEnabled(TView* /*view*/)
{
	return 0;
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
			;	// NOT YET RECONSTRUCTED: DeleteHilitedViews 0x000a8750
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


// ROM 0x000abaa4 HandleWord__9TEditViewFPUsUlR5TRectT3P11TUnitPublicRC6RefVarPl
// A word - written and recognised, or typed and made to look like one -
// put on the page.  It begins by making sure the caret belongs here: the
// key view has to be this editor or one of its children, and anything
// else is dropped so that the word does not go into somebody else's
// text.
//
// NOT YET RECONSTRUCTED: the rest of it, which is where the word goes -
// into the paragraph the box falls in when there is one, and into a new
// one (AddNewParagraph 0x000a1b2c) when there is not.  Until that is
// here a typed character reaches this point and stops, so nothing
// appears on the page.
long
TEditView::HandleWord(UniChar* /*text*/, ULong /*length*/, Rect& box, Rect& /*room*/,
					  TUnitPublic* /*unit*/, RefArg /*info*/, long* /*outOffset*/)
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
	(void) emptyBox;	// NOT YET: what the rest of the function does with it
	return 0;
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
