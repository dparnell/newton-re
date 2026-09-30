/*
	File:		views/ContainerView.cpp

	Contains:	TContainerView and TContainerHilite (ContainerView.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ContainerView.h"
#include "EditView.h"
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
	fGap = 5;
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


/*------------------------------------------------------------------------------
	W h a t   t h e   r e c o g n i s e r   d r i v e s
------------------------------------------------------------------------------*/

// ROM 0x00073258 HandleScrub__14TContainerViewFRC5TRectlP11TUnitPublicUc
// A scrub over the container: TView's first (a scrub over its selection).
// Asked (not reallyDoIt), the best any child would make of it - all of
// the container (5) only as much as all of a child (4).  Done, each child
// scrubs its part, one scrubbed away entirely (5) removed from the
// container (aeRemoveData); a whole scrub (kind 4 or 5) is handed to the
// children as a question only - ROM QUIRK: their reallyDoIt is `kind !=
// 5`.  ==> 5 when no child is left, the kind (4 for a whole one) when
// any child took it, else 0.
long
TContainerView::HandleScrub(const Rect& bounds, long kind, TUnitPublic* unit, Boolean reallyDoIt)
{
	if (!Overlaps(&viewBounds, &bounds))
		return 0;
	long result = TView::HandleScrub(bounds, kind, unit, reallyDoIt);
	if (result != 0)
		return result;
	if (!reallyDoIt)
	{
		long best = 0;
		TListLoop loop(fChildren);
		TView* child = (TView*) loop.Next();
		if (child == nil)
			return 0;
		for ( ; child != nil; child = (TView*) loop.Next())
		{
			long taken = child->HandleScrub(bounds, kind, unit, false);
			if (taken > best)
				best = taken;
		}
		if (best == 5)
			best = 4;
		return best;
	}
	CList* children = fChildren;
	if (kind == 4)
		kind = 5;
	Boolean childDoIt = kind != 5;
	Boolean took = false;
	if ((long) children->GetArraySize() <= 0)
		return 0;
	for (long i = 0; i < (long) children->GetArraySize(); i++)
	{
		TView* child = (TView*) children->At((short) i);
		long taken = child->HandleScrub(bounds, kind, unit, childDoIt);
		if (taken != 0)
		{
			took = true;
			if (taken == 5)
			{
				gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, child->fId)));
				i--;
			}
		}
	}
	if (!took)
		return 0;
	if (children->GetArraySize() == 0)
		return 5;
	return kind == 5 ? 4 : kind;
}


// ROM 0x00073454 HandleCaret__14TContainerViewFUllR6TPointN33
// To the first visible child that takes it.
long
TContainerView::HandleCaret(ULong kind, long angle, Point& armA, Point& point, Point& armB, Point& tail)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
		if ((child->fFlags & vVisible) != 0
			&& ((TDataView*) child)->HandleCaret(kind, angle, armA, point, armB, tail) != 0)
			return 1;
	return 0;
}


// ROM 0x000734fc HandleLineGesture__14TContainerViewFlR6TPointT2
// To the first visible child that takes it.
long
TContainerView::HandleLineGesture(long angle, Point& from, Point& to)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
		if ((child->fFlags & vVisible) != 0
			&& ((TDataView*) child)->HandleLineGesture(angle, from, to) != 0)
			return 1;
	return 0;
}


// ROM 0x00073cf8 HandleWord__14TContainerViewFPCUsUlRC5TRectRC6TPointN22RC6RefVarUcPlP11TUnitPublic
// A word written over the container: each visible child whose bounds (five
// pixels to spare) the word's box touches is asked how well it would take
// it, and - when this is not only a question - the one that bid most (the
// first of equals) is given it.  ==> the best bid.
long
TContainerView::HandleWord(const UniChar* text, ULong length, const Rect& box, const Point& pt, ULong a, ULong b,
						   RefArg word, Boolean flag, long* outOffset, TUnitPublic* unit)
{
	long best = 0;
	if (!Overlaps(&viewBounds, &box))
		return 0;
	TView* bestChild = nil;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if ((child->fFlags & vVisible) == 0)
			continue;
		Rect near = child->viewBounds;
		InsetRect(&near, -5, -5);
		if (!Overlaps(&near, &box))
			continue;
		long bid = ((TDataView*) child)->HandleWord(text, length, box, pt, a, b, word, false, nil, unit);
		if (bid > best)
		{
			bestChild = child;
			best = bid;
		}
	}
	if (flag && bestChild != nil)
		((TDataView*) bestChild)->HandleWord(text, length, box, pt, a, b, word, true, outOffset, unit);
	return best;
}


// ROM 0x00073e68 PointOverText__14TContainerViewFR6TPointP6TPoint
// Whether any visible child has text under the point.
Boolean
TContainerView::PointOverText(Point& pt, Point* onLine)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
		if ((child->fFlags & vVisible) != 0 && ((TDataView*) child)->PointOverText(pt, onLine))
			return true;
	return false;
}


// ROM 0x00073ee8 HandleTap__14TContainerViewFR6TPoint
// A tap handed to the first visible child the point is over text in - or
// that moved the line point it was given (which starts a pixel above and
// left of the container), which is a child that has lines there.
void
TContainerView::HandleTap(Point& pt)
{
	Point start;
	start.v = (short) (viewBounds.top - 1);
	start.h = (short) (viewBounds.left - 1);
	Point onLine = start;
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if ((child->fFlags & vVisible) == 0)
			continue;
		if (((TDataView*) child)->PointOverText(pt, &onLine)
			|| onLine.v != start.v || onLine.h != start.h)
		{
			((TDataView*) child)->HandleTap(pt);
			return;
		}
	}
}


/*------------------------------------------------------------------------------
	E d i t i n g
------------------------------------------------------------------------------*/

// ROM 0x00073fd4 CopyForm__14TContainerViewFv
// A copy of the container's data.
Ref
TContainerView::CopyForm(void)
{
	RefVar data(DataFrame());
	return Clone(data);
}


// ROM 0x00073970 AddHilited__14TContainerViewFRC6RefVarP9TEditView
// The selection made a view of its own on the page: the whole container
// copied (CopyForm, given its copy protection) and added to the editor,
// all of it selected; else the first hilited child's selection
// (AddHilited), moved to where the container is.  ==> the new view.
TView*
TContainerView::AddHilited(RefArg hilite, TEditView* editor)
{
	if (IsCompletelyHilited(hilite))
	{
		RefVar form(CopyForm());
		TransferCopyProtection(form);
		TView* view = editor->AddForm(form);
		view->HiliteAll();
		return view;
	}
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->Hilited())
		{
			RefVar first(child->FirstHilite());
			TView* view = ((TDataView*) child)->AddHilited(first, editor);
			view->DoMoveCommand(LocalOrigin());
			return view;
		}
	}
	return nil;
}


// ROM 0x00073a9c DeleteHilited__14TContainerViewFRC6RefVar
// The selection deleted: only unselected when the container is read-only
// (viewFlags 0x82); the whole container removed from its parent
// (aeRemoveData) when all of it is selected; else the first hilited
// child's selection deleted and the hilite taken off.
void
TContainerView::DeleteHilited(RefArg hilite)
{
	if ((fFlags & 0x82) != 0)
	{
		RemoveHilite(hilite);
		return;
	}
	if (IsCompletelyHilited(hilite))
	{
		gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, fParent, fId)));
		return;
	}
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->Hilited())
		{
			child->DeleteHilited(RefVar(child->FirstHilite()));
			break;
		}
	}
	RemoveHilite(hilite);
}


// ROM 0x000740cc RealDoCommand__14TContainerViewFRC6RefVar
// The children's data added and removed: aeAddData makes the frame
// parameter a child (its id the parameter's, unless that is none - the
// parameter left the new view) and posts aeRemoveData as its undo;
// aeRemoveData takes the child of the id out, unselected, and posts
// aeAddData with its data.  Either way the container is redrawn.  The
// rest is TView's.
Boolean
TContainerView::RealDoCommand(RefArg cmd)
{
	long id = CommandID(cmd);
	if (id == aeAddData)
	{
		TView* child = AddToSoup(RefVar(CommandFrameParameter(cmd)));
		TimeStampTextChange(child);
		long wantedId = CommandParameter(cmd);
		if (wantedId != kNoParameter)
			child->fId = wantedId;
		CommandSetParameter(cmd, (Long) child);
		gApplication->PostUndoCommand(aeRemoveData, this, child->fId);
		Dirty(nil);
		return true;
	}
	if (id != aeRemoveData)
		return TView::RealDoCommand(cmd);
	TView* child = FindID(CommandParameter(cmd));
	if (child != nil)
	{
		RefVar data(child->DataFrame());
		long childId = child->fId;
		child->RemoveAllHilites();
		RemoveFromSoup(child);
		RefVar undo(MakeCommand(aeAddData, this, childId));
		CommandSetFrameParameter(undo, data);
		gApplication->PostUndoCommand(undo);
	}
	Dirty(nil);
	return true;
}


// ROM 0x00074240 GetValue__14TContainerViewFRC6RefVarT1
// hilites as offset: when the container is hilited, the first offset
// each child answers, in an array (nil for none).  The rest is TView's.
Ref
TContainerView::GetValue(RefArg slot, RefArg type)
{
	if (!EQRef(slot, RSSYMhilites) || !EQRef(type, RSSYMoffset))
		return TView::GetValue(slot, type);
	RefVar result(NILREF);
	if (Hilited())
	{
		result = MakeArray(0);
		RefVar answer;
		TListLoop loop(fChildren);
		TView* child;
		while ((child = (TView*) loop.Next()) != nil)
		{
			answer = child->GetValue(slot, type);
			if (NOTNIL(answer))
				AddArraySlot(result, RefVar(GetArraySlotRef(answer, 0)));
		}
		if (Length(result) == 0)
			result = NILREF;
	}
	return result;
}


// ROM 0x000743c0 ChildBoundsChanged__14TContainerViewFP5TViewR5TRect
// A child whose right edge has moved (its left where it was): grown
// wider, every child to the right of where it ended that shares any of
// its rows and has come within fGap of it is pushed along - all by the
// most any of them needs - and the container widened by as much; grown
// narrower, the container is only redrawn.  `bounds` is the child's old
// bounds, its viewBounds the new.
void
TContainerView::ChildBoundsChanged(TView* child, Rect& bounds)
{
	Rect grown = child->viewBounds;
	// the rows it covers, as a strip a pixel wide (the other children's
	// the same), so only the vertical overlap counts
	Rect strip = grown;
	strip.left = 0;
	strip.right = 1;
	if (grown.left != bounds.left)
		return;
	long wider = grown.right - bounds.right;
	if (wider <= 0)
	{
		if (wider < 0)
			Dirty(nil);
		return;
	}
	long shift = 0;
	{
		TListLoop loop(fChildren);
		TView* other;
		while ((other = (TView*) loop.Next()) != nil)
		{
			if (other->viewBounds.left < bounds.right)
				continue;
			if (other->viewBounds.left >= fGap + grown.right)
				continue;
			Rect otherStrip = other->viewBounds;
			otherStrip.left = 0;
			otherStrip.right = 1;
			if (!Overlaps(&strip, &otherStrip))
				continue;
			long need = grown.right - other->viewBounds.left + fGap;
			if (need > shift)
				shift = need;
		}
	}
	if (shift <= 0)
		return;
	{
		TListLoop loop(fChildren);
		TView* other;
		while ((other = (TView*) loop.Next()) != nil)
		{
			if (other->viewBounds.left < bounds.right)
				continue;
			Rect otherStrip = other->viewBounds;
			otherStrip.left = 0;
			otherStrip.right = 1;
			if (!Overlaps(&strip, &otherStrip))
				continue;
			Rect moved = other->viewBounds;
			OffsetRect(&moved, -viewBounds.left, -viewBounds.top);
			OffsetRect(&moved, (short) shift, 0);
			other->WriteBounds(moved);
		}
	}
	Rect mine = viewBounds;
	mine.right = (short) (mine.right + shift);
	Point origin = fParent->ContentsOrigin();
	OffsetRect(&mine, (short) -origin.h, (short) -origin.v);
	WriteBounds(mine);
}


// ROM 0x000746ec PointToCaret__14TContainerViewFR6TPointP5TRectT2
// Each visible child asked in turn, until one puts the caret somewhere
// (its top not -32768).
void
TContainerView::PointToCaret(Point& pt, Rect* caret, Rect* bounds)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if ((child->fFlags & vVisible) == 0)
			continue;
		child->PointToCaret(pt, caret, bounds);
		if (caret->top != -32768)
			break;
	}
}
