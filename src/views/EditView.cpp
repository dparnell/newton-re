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
#include "Commands.h"
#include "UnitPublic.h"
#include "StrokeQueue.h"
#include "Bits.h"
#include "ViewFlags.h"
#include "Rects.h"
#include "Regions.h"
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
