/*
	File:		views/ContainerView.cpp

	Contains:	TContainerView and TContainerHilite (ContainerView.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ContainerView.h"
#include "RootView.h"
#include "Commands.h"
#include "Application.h"
#include "ViewFlags.h"
#include "Rects.h"
#include "Draw.h"
#include "Ports.h"
#include "Frames.h"
#include "RSSymbols.h"


/*------------------------------------------------------------------------------
	T C o n t a i n e r H i l i t e
------------------------------------------------------------------------------*/

// The ROM makes one inline wherever it needs it (MakeHilite, Clone): a
// THilite with the container's vtable put over it.
TContainerHilite::TContainerHilite()
{
	fComplete = false;
	fView = nil;
}


TContainerHilite::~TContainerHilite()
{ }


// ROM 0x0007401c Clone__16TContainerHiliteFv
THilite*
TContainerHilite::Clone(void)
{
	TContainerHilite* copy = new TContainerHilite;
	copy->CopyFrom(this);
	return copy;
}


// ROM 0x000740a4 CopyFrom__16TContainerHiliteFP7THilite
// The bounds, and the eight bytes that follow them.
void
TContainerHilite::CopyFrom(THilite* other)
{
	THilite::CopyFrom(other);
	TContainerHilite* from = (TContainerHilite*) other;
	fComplete = from->fComplete;
	fView = from->fView;
}


/*------------------------------------------------------------------------------
	T C o n t a i n e r V i e w
------------------------------------------------------------------------------*/

// ROM 0x000731e4 ClassID__14TContainerViewCFv
long	TContainerView::ClassID(void) const		{ return clContainerView; }


// ROM 0x000731ec DerivedFrom__14TContainerViewCFl
Boolean
TContainerView::DerivedFrom(long id) const
{
	return id == clContainerView || TDataView::DerivedFrom(id);
}


// ROM 0x00074064 __dt__14TContainerViewFv
TContainerView::~TContainerView()
{ }


// ROM 0x00073930 Constructor__14TContainerViewFRC6RefVarP5TView
// TView's, and two fields whose meaning is still to be found - the ROM's
// readers of them are the editing this class does not have yet.
void
TContainerView::Constructor(RefArg context, TView* parent)
{
	TView::Constructor(context, parent);
	fUnknown30 = 5;
	fUnknown34 = 2;
}


// ROM 0x00073928 ClickOptions__14TContainerViewFv
long	TContainerView::ClickOptions(void)		{ return 1; }


// host: the container's own hilite, or nil when nothing is selected.
static TContainerHilite*
ContainerHiliteOf(RefArg hilite)
{
	return ISNIL(hilite) ? nil : (TContainerHilite*) RefToAddress(hilite);
}


// ROM 0x00073c58 IsCompletelyHilited__14TContainerViewFRC6RefVar
// Whether the hilite stands for the whole container rather than a set of
// its children.
Boolean
TContainerView::IsCompletelyHilited(RefArg hilite)
{
	TContainerHilite* object = ContainerHiliteOf(hilite);
	return object != nil && object->fComplete;
}


// ROM 0x00073584 HandleInkWord__14TContainerViewFRC6RefVarUc
// An ink word written over the container.  The command carries a stroke
// bundle whose `bounds` say where it was written; if that misses the
// container altogether nobody is asked.  Otherwise each visible child
// whose box the writing touches - with five pixels of slack, because a
// word written just outside a field still belongs in it - is asked how
// well it would take the word, and the best answer wins.  `reallyDoIt`
// false only asks, which is what lets a container inside a container
// answer for its own children before anything is changed.
//
// DEVIATION: the ROM dispatches through the child's vtable slot for
// HandleInkWord whatever the child is, so a child that is not a data
// view at all is called through a slot it does not have.  The host
// cannot do that, so the class is asked first.
long
TContainerView::HandleInkWord(RefArg cmd, Boolean reallyDoIt)
{
	RefVar bundle(CommandFrameParameter(cmd));
	Rect box;
	FromObject(RefVar(GetFrameSlot(bundle, RSSYMbounds)), box);
	if (!Overlaps(&viewBounds, &box))
		return 0;
	long best = 0;
	TView* bestView = nil;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if ((child->fFlags & vVisible) == 0)
			continue;
		Rect room = child->viewBounds;
		InsetRect(&room, -5, -5);
		if (!Overlaps(&room, &box) || !child->DerivedFrom(clDataView))
			continue;
		long score = ((TDataView*) child)->HandleInkWord(cmd, false);
		if (score > best)
		{
			best = score;
			bestView = child;
		}
	}
	if (reallyDoIt && bestView != nil)
		((TDataView*) bestView)->HandleInkWord(cmd, true);
	return best;
}


// ROM 0x00073c78 GetHiliteView__14TContainerViewFv
// Which view the selection really belongs to: this one when the whole
// container is selected, else the first child that is hilited itself.
TView*
TContainerView::GetHiliteView(void)
{
	if (IsCompletelyHilited(RefVar(FirstHilite())))
		return this;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->Hilited())
			return child;
	}
	return nil;
}


// ROM 0x000748d0 MakeHilite__14TContainerViewFlP5TView
// A hilite of the whole container (child 0), or of one child - whose own
// hilite bounds it takes.  Either way the bounds end up in the container's
// own coordinates, and it goes in through the command so that it can be
// undone.
void
TContainerView::MakeHilite(long child, TView* view)
{
	TContainerHilite* hilite = new TContainerHilite;
	RefVar cmd(MakeCommand(aeAddHilite, this, 0x8000000));
	CommandSetFrameParameter(cmd, RefVar(AddressToRef(hilite)));
	if (child == 0)
	{
		hilite->fBounds = viewBounds;
		hilite->fComplete = true;
	}
	else
	{
		// the ROM marks "nothing yet" by setting top and bottom to -32768,
		// which makes the rectangle empty so that the first Union replaces it
		// (and GlobalHiliteResizeBounds reads the top as the marker); left and
		// right it leaves, which the host cannot, so all four are set
		Rect bounds;
		bounds.top = -32768;
		bounds.left = -32768;
		bounds.bottom = -32768;
		bounds.right = -32768;
		view->GlobalHiliteBounds(&bounds);
		hilite->fBounds = bounds;
		hilite->fComplete = false;
	}
	OffsetRect(&hilite->fBounds, -viewBounds.left, -viewBounds.top);
	hilite->fView = this;
	gApplication->DispatchCommand(cmd);
}


// ROM 0x00074770 HandleHilite__14TContainerViewFP11TUnitPubliclUc
// A hilite stroke over a container.  The container first asks whether it
// is itself covered (TView's test); if not, it asks its children, and a
// child that would take a whole-object hilite (1) makes the *container*
// answer 5 - so a lasso round something inside a container selects the
// container rather than the one child it went round.
//
// Carrying it out drops the container's own hilites first, then offers
// the kind to each child in turn and stops at the first that takes it
// (a 5 offered to a child becomes a 1, which is what a child understands);
// the child that took it becomes the container's hilite.
long
TContainerView::HandleHilite(TUnitPublic* unit, long kind, Boolean reallyDoIt)
{
	long mine = TView::HandleHilite(unit, kind, reallyDoIt);
	if (mine != 0)
		return mine;
	if (!reallyDoIt)
	{
		long best = 0;
		Boolean any = false;
		TViewLoop loop(fChildren);
		TView* child;
		while ((child = loop.Next()) != nil)
		{
			any = true;
			long claim = child->HandleHilite(unit, kind, false);
			if (claim > best)
				best = claim;
		}
		if (any && best == 1)
			best = 5;
		return best;
	}
	RemoveAllHilites();
	if (kind != 0)
	{
		Boolean whole = kind == 5;
		long offered = whole ? 1 : kind;
		TView* winner = nil;
		TViewLoop loop(fChildren);
		TView* child;
		while ((child = loop.Next()) != nil)
		{
			if (child->HandleHilite(unit, offered, true) == offered)
			{
				winner = child;
				break;
			}
		}
		if (winner != nil)
			MakeHilite(1, winner);
	}
	return kind;
}


// ROM 0x00073220 HiliteAll__14TContainerViewFv
void
TContainerView::HiliteAll(void)
{
	RemoveAllHilites();
	MakeHilite(0, nil);
}


// ROM 0x00073254 RemoveAllHilites__14TContainerViewFv
// The ROM has TView's again here, word for word, rather than inheriting it.
void
TContainerView::RemoveAllHilites(void)
{
	HiliteLoop loop(this);
	while (loop.Next())
	{
		RemoveHilite(loop.fHilite);
		loop.fIndex--;
		loop.fCount--;
	}
	if (gRootView->fHiliter == this)
		gRootView->fHiliter = nil;
}


// ROM 0x00073bc4 RemoveHilite__14TContainerViewFRC6RefVar
// A hilite that stood for the children takes their own selections with it.
void
TContainerView::RemoveHilite(RefArg hilite)
{
	if (!IsCompletelyHilited(hilite))
	{
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			if (child->Hilited())
				child->RemoveAllHilites();
		}
	}
	TView::RemoveHilite(hilite);
}


// ROM 0x000737c8 DrawHilites__14TContainerViewFUc
// The whole container is its bounds filled with the black pattern (in
// whatever mode the port is in); a selection of
// children is each of them drawing its own.  A view on its way out of
// existence draws neither.
void
TContainerView::DrawHilites(Boolean scaled)
{
	TContainerHilite* hilite = ContainerHiliteOf(RefVar(FirstHilite()));
	if (hilite == nil || !hilite->fComplete)
	{
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			if (child->Hilited() && (child->fFlags & vIsInSetup2) == 0)
				child->DrawHilites(scaled);
		}
		return;
	}
	if (scaled || (fFlags & vIsInSetup2) != 0)
		return;
	Rect bounds = hilite->fBounds;
	OffsetRect(&bounds, viewBounds.left, viewBounds.top);
	FillRect(&bounds, GetStdPattern(blackPat));
}


// ROM 0x000738c0 DrawHilitedData__14TContainerViewFv
void
TContainerView::DrawHilitedData(void)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->Hilited())
			child->DrawHilitedData();
	}
}


// ROM 0x000736e8 GlobalHiliteBounds__14TContainerViewFP5TRect
// The whole container's bounds, or the union of the hilited children's;
// the answer, as in TView's, is the click options that go with the
// selection.
long
TContainerView::GlobalHiliteBounds(Rect* bounds)
{
	if (Hilited())
	{
		TContainerHilite* hilite = ContainerHiliteOf(RefVar(FirstHilite()));
		if (hilite == nil || !hilite->fComplete)
		{
			TListLoop loop(fChildren);
			TView* child;
			while ((child = (TView*) loop.Next()) != nil)
				child->GlobalHiliteBounds(bounds);
		}
		else
		{
			Rect r = hilite->fBounds;
			OffsetRect(&r, viewBounds.left, viewBounds.top);
			UnionRect(bounds, &r, bounds);
		}
	}
	return ClickOptions();
}
