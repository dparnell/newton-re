/*
	File:		views/MeetingView.cpp

	Contains:	TMeetingView, one meeting in the Dates day view, and
				LayoutMeeting.  See MeetingView.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM builds its rectangles on the stack a halfword at a time
	through unaligned loads; each is written here as the fields it comes
	to, read out of the assembly.
*/

#include "MeetingView.h"
#include "ParagraphView.h"
#include "RootView.h"
#include "Commands.h"
#include "Application.h"
#include "DragDrop.h"
#include "DrawShape.h"
#include "Hilites.h"
#include "UnitPublic.h"
#include "Stroke.h"
#include "Rects.h"
#include "Draw.h"
#include "Regions.h"
#include "Screen.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RichString.h"
#include "REPTranslators.h"		// IsRichString
#include "Unicode.h"
#include "Entries.h"
#include "Soups.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "SoundSettings.h"

// views/ViewNatives.cpp
long	TimeToPosition(long time, long extent, long base, long span);	// ROM 0x001ca128 TimeToPosition__FlN31
// views/ListView.cpp
void	ArrayAppend(RefArg frame, RefArg slot, RefArg value);			// ROM 0x00128cec ArrayAppend__FRC6RefVarN21


// ROM 0x001ca39c GetMeetingSlot__FRC6RefVarT1
// The meeting's own slot; failing that (for anything but its viewBounds)
// its repeat template's - a repeating meeting's instances keep only what
// differs from the template.
Ref
GetMeetingSlot(RefArg meeting, RefArg slot)
{
	RefVar value(GetFrameSlotRef(meeting, slot));
	if (ISNIL(value) && !EQRef(slot, RSSYMviewbounds))
	{
		value = GetFrameSlotRef(meeting, RSSYMrepeattemplate);
		if (NOTNIL(value))
			value = GetFrameSlotRef(value, slot);
	}
	return value;
}


// ROM 0x001ca1a8 LayoutMeeting__FRC6RefVarN41
// The meeting's box in a day view `extent` pixels tall and `width` wide:
// its top and bottom are where its start and end (the minutes into the
// day) come down the extent; across, a meeting with no viewBounds takes
// the whole width, one whose viewBounds is TRUE the right half, and one
// with a box keeps its left unless the width is narrower than its right,
// when it takes the right half.  (`x` is not looked at.)
Ref
LayoutMeeting(RefArg /*rcvr*/, RefArg meeting, RefArg extentRef, RefArg widthRef, RefArg /*x*/)
{
	Long extent = RINT(extentRef);
	Long width = RINT(widthRef);
	RefVar bounds(GetMeetingSlot(meeting, RSSYMviewbounds));
	Long start = RINT(GetMeetingSlot(meeting, RSSYMmtgstartdate));
	Long duration = RINT(GetMeetingSlot(meeting, RSSYMmtgduration));
	long minutes = start % 1440;
	short top = (short) TimeToPosition(minutes, extent, 0, 1440);
	short bottom = (short) TimeToPosition(minutes + duration, extent, 0, 1440);
	Rect box;
	box.top = top;
	box.bottom = bottom;
	if (EQRef(bounds, TRUEREF))
	{
		short w = (short) width;
		box.left = (short) (w / 2);
		box.right = w;
	}
	else if (ISNIL(bounds))
	{
		box.left = 0;
		box.right = (short) width;
	}
	else
	{
		FromObject(bounds, box);
		box.top = top;
		box.bottom = bottom;
		if (width < box.right)
			box.left = (short) (width / 2);
		else
			box.right = (short) width;
	}
	return ToObject(box);
}


// ROM 0x001cae5c GetMeetingText__FRC6RefVar
Ref
GetMeetingText(RefArg meeting)
{
	RefVar repeat(GetFrameSlotRef(meeting, RSSYMrepeattemplate));
	if (ISNIL(repeat))
		return GetFrameSlotRef(meeting, RSSYMmtgtext);
	return GetFrameSlotRef(repeat, RSSYMmtgtext);
}


// ROM 0x001cb64c DeleteMeetingHilite__FRC6RefVar
// The context's hilites taken away, and the first of them (the one a
// click put there) freed.
void
DeleteMeetingHilite(RefArg context)
{
	RefVar hilites(GetFrameSlotRef(context, RSSYMhilites));
	if (NOTNIL(hilites))
	{
		SetFrameSlot(context, RSSYMhilites, RefVar(NILREF));
		THilite* hilite = (THilite*) RefToAddress(GetArraySlotRef(hilites, 0));
		if (hilite != nil)
			delete hilite;
	}
}


// HandleClick's drag hilite taken off the context and the hilites it had
// put back - freed as well when the drag took the view away.
static void
RestoreMeetingHilites(RefArg context, RefArg oldHilites)
{
	DeleteMeetingHilite(context);
	if (NOTNIL(oldHilites))
	{
		SetFrameSlot(context, RSSYMhilites, oldHilites);
		if (ISNIL(GetFrameSlotRef(context, RSSYMviewcobject)))
			DeleteMeetingHilite(context);
	}
}


/*------------------------------------------------------------------------------
	T M e e t i n g V i e w
------------------------------------------------------------------------------*/

// ROM 0x001ca394 ClassID__12TMeetingViewCFv
long
TMeetingView::ClassID(void) const
{
	return clMeetingView;
}


// ROM 0x001ca448 DerivedFrom__12TMeetingViewCFl
Boolean
TMeetingView::DerivedFrom(long id) const
{
	return id == clMeetingView || TContainerView::DerivedFrom(id);
}


// ROM 0x001ca47c Constructor__12TMeetingViewFRC6RefVarP5TView
void
TMeetingView::Constructor(RefArg context, TView* parent)
{
	TContainerView::Constructor(context, parent);
	fLastWordTime = Ticks();
}


// ROM 0x001ca49c RealDoCommand__12TMeetingViewFRC6RefVar
// A click is the icon's (HandleClick); a double tap is the text's; the
// rest, or what they did not take, the container's.  (Host: TContainerView
// has no RealDoCommand of its own yet, so that is TView's.)
Boolean
TMeetingView::RealDoCommand(RefArg cmd)
{
	long result;
	long id = CommandID(cmd);
	if (id == aeClick)
	{
		result = HandleClick(cmd);
		CommandSetResult(cmd, result);
	}
	else if (id == aeDoubleTap)
		result = GetTextView()->RealDoCommand(cmd);
	else
		return TContainerView::RealDoCommand(cmd);
	if (result != 0)
		return result;
	return TContainerView::RealDoCommand(cmd);
}


// ROM 0x001ca52c HandleClick__12TMeetingViewFRC6RefVar
// The pen down on the meeting's icon - the context's iconShape, beside
// the slider, no wider than the parent - inverts it and clicks; moved, it
// drags the meeting away as a 'meeting (the context) or as 'text (a copy
// of its text, the meeting name when that is empty, nothing for a rich
// string), the whole meeting view the drag's bounds and a corner of it
// (60 by 32 at the most) its pin, with a whole-container hilite put on
// the context for the drag's length; lifted without moving, the icon is
// put back and the viewClickScript runs.  While the meeting's notes are
// open a click only clicks.  ==> 1 taken, 2 not on the icon.
long
TMeetingView::HandleClick(RefArg cmd)
{
	TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
	Point pt = unit->Stroke()->FirstPoint();
	long dragged = 0;
	TView* slider = GetSliderView();
	RefVar shape(GetProto(RSSYMiconshape));
	Rect icon;
	ShapeBounds(shape, &icon);
	Rect box = viewBounds;
	box.left = (short) (box.left + (slider->viewBounds.right - slider->viewBounds.left));
	long right = box.left + icon.right + 2;
	if (fParent->viewBounds.right < right)
		right = fParent->viewBounds.right;
	box.right = (short) right;
	box.bottom = (short) (box.top + icon.bottom + 2);
	if (GetView(RefVar(GetVar(RSSYMmeetingnotes))) != nil)
	{
		FClicker(RefVar());
		return 2;
	}
	if (!PtInRect(pt, &box))
		return 2;

	InvertRect(&box);
	FClicker(RefVar());
	TStrokePublic* stroke = ((TUnitPublic*) CommandParameter(cmd))->Stroke();
	RefVar types(MakeArray(2));
	SetArraySlotRef(types, 0, RSSYMmeeting);
	SetArraySlotRef(types, 1, RSSYMtext);
	RefVar text(GetMeetingText(RefVar(DataFrame())));
	if (ISNIL(text))
		text = Rmeetingname;
	else if (IsRichString(text))
		text = NILREF;
	else if (Ustrlen((const UniChar*) BinaryData(text)) == 0)
		text = Rmeetingname;
	TDragInfo dragInfo(types, fContext, RefVar(Clone(text)));
	Rect bounds = viewBounds;
	long height = (short) (bounds.bottom - bounds.top);
	if (screenHeight < height)
		height = screenHeight;
	bounds.bottom = (short) (bounds.top + height);
	if (fParent->viewBounds.right < bounds.right)
		bounds.right = fParent->viewBounds.right;
	Rect pin = bounds;
	long pinHeight = (short) (pin.bottom - pin.top);
	if (pinHeight > 32)
		pinHeight = 32;
	long pinWidth = (short) (pin.right - pin.left);
	if (pinWidth > 60)
		pinWidth = 60;
	pin.bottom = (short) (pin.top + pinHeight);
	pin.right = (short) (pin.left + pinWidth);

	// the container hilited, for the drag's length (the ROM leaves its
	// fView unset)
	TContainerHilite* hilite = new TContainerHilite;
	SetRect(&hilite->fBounds, 0, 0, 0, 0);
	hilite->fComplete = true;
	RefVar context(fContext);
	RefVar oldHilites(GetFrameSlotRef(context, RSSYMhilites));
	SetFrameSlot(context, RSSYMhilites, RefVar(NILREF));
	ArrayAppend(context, RSSYMhilites, RefVar(AddressToRef(hilite)));
	// the hilite comes off (and the old ones go back) whether or not the
	// drag threw
	newton_try
	{
		dragged = fParent->DragAndDrop(stroke, bounds, &pin, &pin, false, dragInfo, nil);
	}
	newton_catch_all
	{
		RestoreMeetingHilites(context, oldHilites);
		rethrow;
	}
	end_try;
	RestoreMeetingHilites(context, oldHilites);
	if (dragged == 0)
	{
		InvertRect(&box);
		RefVar args(MakeArray(1));
		SetArraySlotRef(args, 0, AddressToRef((void*) CommandParameter(cmd)));
		RunScript(RSSYMviewclickscript, args, false);
	}
	return 1;
}


// ROM 0x001cab9c HandleScrub__12TMeetingViewFRC5TRectlP11TUnitPublicUc
// A scrub over the meeting: asked, one that covers half its icon (the
// 24 by 16 beside the slider) takes the whole meeting (5), anything else
// is the text's (a whole-object answer from it becoming 4); done, it is
// the text's (with 4 made 5 on the way down and back).
long
TMeetingView::HandleScrub(const Rect& bounds, long kind, TUnitPublic* unit, Boolean on)
{
	if (!Overlaps(&viewBounds, &bounds))
		return 0;
	if (on)
	{
		if (kind == 4)
			kind = 5;
		GetTextView()->HandleScrub(bounds, kind, unit, true);
		if (kind == 5)
			kind = 4;
		return kind;
	}
	Rect icon = viewBounds;
	TView* slider = GetSliderView();
	icon.left = (short) (icon.left + (slider->viewBounds.right - slider->viewBounds.left));
	icon.right = (short) (icon.left + 24);
	icon.bottom = (short) (icon.top + 16);
	if (CoveredBy(&icon, &bounds) >= 50)
		return 5;
	long result = GetTextView()->HandleScrub(bounds, kind, unit, false);
	if (result == 5)
		result = 4;
	return result;
}


// ROM 0x001cacf0 GetTextView__12TMeetingViewFv
TView*
TMeetingView::GetTextView(void)
{
	TView** child = fChildren != nil ? (TView**) fChildren->SafeElementPtrAt(1) : nil;
	return child != nil ? *child : nil;
}


// ROM 0x001cacfc GetSliderView__12TMeetingViewFv
TView*
TMeetingView::GetSliderView(void)
{
	TView** child = fChildren != nil ? (TView**) fChildren->SafeElementPtrAt(0) : nil;
	return child != nil ? *child : nil;
}


// ROM 0x001cad0c DrawHilites__12TMeetingViewFUc
// The whole meeting's hilite is the container's to draw; otherwise the
// text's, if it has one and is not being deleted.
void
TMeetingView::DrawHilites(Boolean on)
{
	RefVar hilite(FirstHilite());
	if (ISNIL(hilite) || !IsCompletelyHilited(hilite))
	{
		TView* text = GetTextView();
		if (text->Hilited() && (text->fFlags & vIsInSetup2) == 0)
			text->DrawHilites(on);
	}
}


// ROM 0x001cadac DrawHilitedData__12TMeetingViewFv
void
TMeetingView::DrawHilitedData(void)
{
	if (!Hilited())
		return;
	RefVar hilite(FirstHilite());
	if (!IsCompletelyHilited(hilite))
		GetTextView()->DrawHilitedData();
	else
	{
		Rect bounds = viewBounds;
		Draw(bounds, false);
	}
}


// ROM 0x001caee0 Hilited__12TMeetingViewFv
Boolean
TMeetingView::Hilited(void)
{
	return TView::Hilited() || GetTextView()->Hilited();
}


// ROM 0x001caf34 GlobalHiliteBounds__12TMeetingViewFP5TRect
long
TMeetingView::GlobalHiliteBounds(Rect* bounds)
{
	RefVar hilite(FirstHilite());
	if (ISNIL(hilite))
	{
		GetTextView()->GlobalHiliteBounds(bounds);
		return ClickOptions();
	}
	return TContainerView::GlobalHiliteBounds(bounds);
}


// ROM 0x001cafb4 HandleWord__12TMeetingViewFPCUsUlRC5TRectRC6TPointN22RC6RefVarUcPlP11TUnitPublic
// A word written on the meeting.  Asked, it is offered to the text
// (6, a letter over a letter, is the text's alone); otherwise one mostly
// on the meeting is taken (5), one mostly on it with 120 pixels to spare
// to the right is added (3), and so is one near it written within a
// second of the last (the reach is ten above, twenty below and 120 to
// the right).  Done, it goes to the text - at the end of the meeting's
// text unless it was written mostly over it - and the time is noted.
long
TMeetingView::HandleWord(const UniChar* text, ULong length, const Rect& box,
						 const Point& pt, ULong a, ULong b, RefArg word,
						 Boolean reallyDoIt, long* outOffset, TUnitPublic* unit)
{
	if (reallyDoIt)
	{
		Rect at = viewBounds;
		if (CoveredBy(&box, &at) >= 25)
			at = box;
		else
			at.left = (short) (at.right - 2);
		fLastWordTime = b;
		return ((TDataView*) GetTextView())->HandleWord(text, length, at, pt, a, b, word, reallyDoIt, outOffset, unit);
	}
	long result = 0;
	Rect mine = viewBounds;
	Rect wider = viewBounds;
	wider.right = (short) (wider.right + 120);
	Rect reach = viewBounds;
	reach.bottom = (short) (reach.bottom + 20);
	reach.top = (short) (reach.top - 10);
	reach.right = (short) (reach.right + 120);
	if (Intersects(&box, &reach))
	{
		if (((TDataView*) GetTextView())->HandleWord(text, length, box, pt, a, b, word, false, outOffset, unit) == 6)
			result = 6;
		else if (CoveredBy(&box, &mine) > 25)
			result = 5;
		else if (CoveredBy(&box, &wider) > 25
			  || (a != 0 && fLastWordTime + 60 > a))
			result = 3;
	}
	return result;
}


// ROM 0x001cb1bc HandleHilite__12TMeetingViewFP11TUnitPubliclUc
// A hilite stroke: a whole-object one (1) is not the meeting's; asked,
// the text's answer with a whole-object one made the meeting's own (5);
// done, the hilites dropped and the kind (5 as 1) given to the text,
// which becomes the meeting's hilite when it takes it.
long
TMeetingView::HandleHilite(TUnitPublic* unit, long kind, Boolean reallyDoIt)
{
	if (kind == 1)
		return 0;
	TView* text = GetTextView();
	if (reallyDoIt)
	{
		RemoveAllHilites();
		if (kind != 0)
		{
			Boolean whole = kind == 5;
			if (whole)
				kind = 1;
			long taken = text->HandleHilite(unit, kind, true);
			if (whole)
				kind = 5;
			if (taken != 0)
				MakeHilite(1, text);
		}
		return kind;
	}
	long result = text->HandleHilite(unit, kind, false);
	if (result == 1)
		result = 5;
	return result;
}


// ROM 0x001cb2a0 MakeHilite__12TMeetingViewFlP5TView
// The whole meeting is the container's hilite; a child's is a clone of
// the child's own first hilite, moved into the meeting's coordinates, put
// on with an undoable aeAddHilite.
void
TMeetingView::MakeHilite(long child, TView* view)
{
	if (child == 0)
	{
		TContainerView::MakeHilite(0, nil);
		return;
	}
	RefVar cmd(MakeCommand(aeAddHilite, this, 0x8000000));
	RefVar first(view->FirstHilite());
	THilite* hilite = ((THilite*) RefToAddress(first))->Clone();
	short dh = (short) (view->viewBounds.left - viewBounds.left);
	short dv = (short) (view->viewBounds.top - viewBounds.top);
	// the ROM moves the region at +0x18 whatever the hilite is: the text's
	// is always a paragraph's.  (Host: a paragraph hilite's region is made
	// when it is first worked out, so it may not be there yet.)
	RgnHandle area = ((TParagraphHilite*) hilite)->fArea;
	if (area != nil)
		OffsetRgn(area, dh, dv);
	OffsetRect(&hilite->fBounds, dh, dv);
	CommandSetFrameParameter(cmd, RefVar(AddressToRef(hilite)));
	gApplication->DispatchCommand(cmd);
}


// ROM 0x001cb3f4 HiliteText__12TMeetingViewFlT1Uc
// A range of the text selected: the meeting's hilites dropped, the text's
// range hilited, the text made the meeting's hilite, and (to show it) the
// meeting's parent made the view owning the hilites, the one that owned
// them before told to drop its own.
void
TMeetingView::HiliteText(long offset, long length, Boolean on)
{
	RemoveAllHilites();
	TView* text = GetTextView();
	((TDataView*) text)->HiliteText(offset, length, false);
	MakeHilite(1, text);
	if (!on)
		return;
	TView* hiliter = gRootView->fHiliter;
	if (hiliter == fParent)
		return;
	if (hiliter != nil)
		gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveAllHilites, hiliter, 0x8000000)));
	gRootView->fHiliter = fParent;
}


// ROM 0x001cb480 RemoveHilite__12TMeetingViewFRC6RefVar
// A hilite of the text's is the text's to remove, and the meeting's own
// goes with it once the text has none; the meeting's own the container's.
void
TMeetingView::RemoveHilite(RefArg hilite)
{
	RefVar first(FirstHilite());
	if (hilite != (Ref) first)
	{
		GetTextView()->RemoveHilite(hilite);
		if (GetTextView()->Hilited())
			return;
		TContainerView::RemoveHilite(first);
		return;
	}
	TContainerView::RemoveHilite(hilite);
}


// ROM 0x001cb51c RemoveAllHilites__12TMeetingViewFv
void
TMeetingView::RemoveAllHilites(void)
{
	if (ISNIL(FirstHilite()))
	{
		// host: a meeting view whose construction threw is deleted before
		// it has its children (the ROM would call through nil)
		TView* text = GetTextView();
		if (text != nil)
			text->RemoveAllHilites();
	}
	else
		TContainerView::RemoveAllHilites();
	if (gRootView->fHiliter == this)
		gRootView->fHiliter = nil;
}


// ROM 0x001cb57c DeleteHilited__12TMeetingViewFRC6RefVar
void
TMeetingView::DeleteHilited(RefArg hilite)
{
	RefVar first(FirstHilite());
	if (hilite == (Ref) first)
		TContainerView::DeleteHilited(hilite);
	else
	{
		GetTextView()->DeleteHilited(hilite);
		if (!GetTextView()->Hilited())
			TContainerView::RemoveHilite(first);
	}
}


// ROM 0x001cb620 AddDragInfo__12TMeetingViewFP9TDragInfo
Boolean
TMeetingView::AddDragInfo(TDragInfo* dragInfo)
{
	return GetTextView()->AddDragInfo(dragInfo);
}


// ROM 0x001cb6ec GetSupportedDropTypes__12TMeetingViewFRC6TPoint
Ref
TMeetingView::GetSupportedDropTypes(const Point& pt)
{
	RefVar types(TView::GetSupportedDropTypes(pt));
	if (ISNIL(types))
	{
		types = MakeArray(1);
		SetArraySlotRef(types, 0, RSSYMtext);
	}
	return types;
}


// ROM 0x001cb74c DragFeedback__12TMeetingViewFRC9TDragInfoRC6TPointUc
Boolean
TMeetingView::DragFeedback(const TDragInfo& dragInfo, const Point& pt, Boolean show)
{
	if (TView::DragFeedback(dragInfo, pt, show))
		return true;
	return GetTextView()->DragFeedback(dragInfo, pt, show);
}


// ROM 0x001cb7a4 GetDropData__12TMeetingViewFRC6RefVarT1
// What a meeting dragged away is (when the script does not say): as
// 'text, the text's selection - or, with nothing selected, the whole
// text as a canonicalMeetingDropText paragraph; as a 'meeting, a copy of
// the meeting - a repeating one with its template (without its
// exceptions and instance notes) and an alias to it and its notes as
// GetNotesData answers them, the whole deep-cloned - with its box made
// local and the signature of the store it is on.
Ref
TMeetingView::GetDropData(RefArg dragType, RefArg dragRef)
{
	RefVar data(TView::GetDropData(dragType, dragRef));
	if (NOTNIL(data))
		return data;
	if (EQRef(dragType, RSSYMtext))
	{
		TView* text = GetTextView();
		if (!text->Hilited())
		{
			data = Clone(RefVar(Rcanonicalmeetingdroptext));
			RefVar value(text->GetProto(RSSYMtext));
			SetFrameSlot(data, RSSYMtext, value);
			value = text->GetProto(RSSYMviewfont);
			if (NOTNIL(value))
				SetFrameSlot(data, RSSYMviewfont, value);
			value = text->GetProto(RSSYMstyles);
			if (NOTNIL(value) && Length(value) > 0)
				SetFrameSlot(data, RSSYMstyles, RefVar(Clone(value)));
			Rect none;
			SetRect(&none, 0, 0, 0, 0);
			SetFrameSlot(data, RSSYMviewbounds, RefVar(ToObject(none)));
		}
		else
			data = text->GetDropData(dragType, RefVar(text->fContext));
	}
	else if (EQRef(dragType, RSSYMmeeting))
	{
		RefVar entry(DataFrame());
		data = Clone(RefVar(DataFrame()));
		RefVar repeat(GetFrameSlotRef(RefVar(DataFrame()), RSSYMrepeattemplate));
		if (NOTNIL(repeat))
		{
			entry = repeat;
			RefVar alias(MakeEntryAlias(repeat));
			repeat = Clone(repeat);
			RefVar args(MakeArray(1));
			SetArraySlotRef(args, 0, DataFrame());
			RefVar notes(RunScript(RSSYMgetnotesdata, args, true));
			args = GetArraySlotRef(notes, 1);
			SetFrameSlot(data, RSSYMnotesdata, args);
			SetFrameSlot(repeat, RSSYMexceptions, RefVar(NILREF));
			SetFrameSlot(repeat, RSSYMinstancenotesdata, RefVar(NILREF));
			SetFrameSlot(data, RSSYMrepeattemplate, repeat);
			SetFrameSlot(data, RSSYMrepeattemplatealias, alias);
			data = DeepClone(data);
		}
		Rect bounds = viewBounds;
		OffsetRect(&bounds, -viewBounds.left, -viewBounds.top);
		SetFrameSlot(data, RSSYMviewbounds, RefVar(ToObject(bounds)));
		RefVar store(EntryStore(entry));
		SetFrameSlot(data, RSSYMstoresig, RefVar(StoreGetSignature(store)));
	}
	return data;
}


// ROM 0x001cbc80 Drop__12TMeetingViewFRC6RefVarT1P6TPoint
Boolean
TMeetingView::Drop(RefArg dropType, RefArg dropData, Point* dropPt)
{
	if (TView::Drop(dropType, dropData, dropPt))
		return true;
	return GetTextView()->Drop(dropType, dropData, dropPt);
}


// ROM 0x001cbcd4 DropRemove__12TMeetingViewFRC6RefVar
Boolean
TMeetingView::DropRemove(RefArg dragRef)
{
	if (TView::DropRemove(dragRef))
		return true;
	return GetTextView()->DropRemove(dragRef);
}


// ROM 0x001cbd18 DropDone__12TMeetingViewFv
Boolean
TMeetingView::DropDone(void)
{
	if (TView::DropDone())
		return true;
	return GetTextView()->DropDone();
}


/*------------------------------------------------------------------------------
	T h e   k i n d s   o f   m e e t i n g
------------------------------------------------------------------------------*/

// ROM 0x001cbed0 FGetMeetingTypeInfo
// GetMeetingTypeInfo(meeting): what the Dates application's
// meetingTypeRegistry says about the kind of meeting it is - by its
// meetingType when the registry knows it and gives it an icon and a
// shape, else by its mtgIconType likewise, else by its stationery (a
// cribNote as itself, none a to do item, anything else a meeting).
Ref
FGetMeetingTypeInfo(RefArg /*rcvr*/, RefArg meeting)
{
	RefVar calendar(gRootView->GetVar(RSSYMcalendar));
	RefVar registry(GetProtoVariable(calendar, RSSYMmeetingtyperegistry, nil));
	RefVar info(GetMeetingSlot(meeting, RSSYMmeetingtype));
	if (NOTNIL(info))
	{
		info = GetProtoVariable(registry, info, nil);
		if (NOTNIL(info))
		{
			if (ISNIL(GetProtoVariable(info, RSSYMicon, nil)) || ISNIL(GetProtoVariable(info, RSSYMshape, nil)))
				info = NILREF;
			else
				return info;
		}
	}
	info = GetMeetingSlot(meeting, RSSYMmtgicontype);
	if (NOTNIL(info))
	{
		info = GetProtoVariable(registry, info, nil);
		if (ISNIL(GetFrameSlotRef(info, RSSYMicon)) || ISNIL(GetFrameSlotRef(info, RSSYMshape)))
			info = NILREF;
		else if (NOTNIL(info))
			return info;
	}
	info = GetMeetingSlot(meeting, RSSYMviewstationery);
	if (ISNIL(info))
		info = RSSYMtodo;
	else if (!EQRef(info, RSSYMcribnote))
		info = RSSYMmeeting;
	return GetProtoVariable(registry, info, nil);
}


// ROM 0x001cc0e0 FGetMeetingIcon
// GetMeetingIcon(meeting): the icon of its kind.
Ref
FGetMeetingIcon(RefArg rcvr, RefArg meeting)
{
	RefVar info(FGetMeetingTypeInfo(RefVar(NILREF), meeting));
	return GetProtoVariable(info, RSSYMicon, nil);
}


void
RegisterMeetingViewNatives(void)
{
	RegisterNativeFunction("LayoutMeeting__FRC6RefVarN41", (void*) LayoutMeeting, 4);
	RegisterNativeFunction("FGetMeetingTypeInfo", (void*) FGetMeetingTypeInfo, 1);
	RegisterNativeFunction("FGetMeetingIcon", (void*) FGetMeetingIcon, 1);
}
