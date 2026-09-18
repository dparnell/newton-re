/*
	File:		views/EditView.cpp

	Contains:	TEditView (EditView.h) - the editor's frame and the hilites
				of its children.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
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
#include "Bits.h"
#include "ViewFlags.h"
#include "Rects.h"
#include "Regions.h"
#include "Draw.h"
#include "Ports.h"
#include "Frames.h"
#include "RSSymbols.h"


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


// ROM 0x000a4698 ToOutsideGrayBorder__FP5TRectPC5TRect
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

// ROM 0x000a2c68 ClassID__9TEditViewCFv
long	TEditView::ClassID(void) const		{ return clEditView; }


// ROM 0x000a2c70 DerivedFrom__9TEditViewCFl
// TView's, not TContainerView's: an edit view answers the same questions as
// a container but is not one.
Boolean
TEditView::DerivedFrom(long id) const
{
	return id == clEditView || TView::DerivedFrom(id);
}


// ROM 0x000a53f4 Constructor__9TEditViewFRC6RefVarP5TView
void
TEditView::Constructor(RefArg context, TView* parent)
{
	fTextFlags = 0;
	fUnknown4C = false;
	TView::Constructor(context, parent);
}


// ROM 0x000a768c SetupDone__9TEditViewFv
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
	fUnknown40 = false;
	StartGathering(&fCaretRect);
	TView::SetupDone();
}


// ROM 0x000a5370 HasHilitedChildren__9TEditViewFlPP5TView
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


// ROM 0x000a8cbc CountHilites__9TEditViewFv
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


// ROM 0x000a8c54 PointInHilite__9TEditViewFR6TPoint
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


// ROM 0x000a871c HiliteAll__9TEditViewFv
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


// ROM 0x000a8774 RemoveAllHilites__9TEditViewFv
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


// ROM 0x000a8588 DetermineKeyView__9TEditViewFv
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


// ROM 0x000a89cc GlobalHiliteBounds__9TEditViewFP5TRect
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


// ROM 0x000a8a8c GlobalSelectedBounds__9TEditViewFP5TRect
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


// ROM 0x000a8b90 GlobalHiliteResizeBounds__9TEditViewFP5TRect
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


// ROM 0x000a8c00 GlobalHilitePinnedBounds__9TEditViewFP5TRect
// The caller's rectangle is added to, not started again.
void
TEditView::GlobalHilitePinnedBounds(Rect* bounds)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
		child->GlobalHilitePinnedBounds(bounds);
}


// ROM 0x000a7470 InvalAllHilites__9TEditViewFv
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


// ROM 0x000a7210 DirtyBoxHilites__9TEditViewFv
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


// ROM 0x000a7510 DrawHilitedData__9TEditViewFv
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


// ROM 0x000a729c DrawHiliting__9TEditViewFv
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


// ROM 0x000a70b8 PostDraw__9TEditViewFR5TRect
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
