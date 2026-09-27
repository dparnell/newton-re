/*
	File:		views/ListView.cpp

	Contains:	TListView, the outline list.  See ListView.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The ROM builds its rectangles on the stack a halfword at a time through
	unaligned loads; each one here is written as the fields it comes to,
	read out of the assembly.
*/

#include "ListView.h"
#include "ParagraphView.h"
#include "RootView.h"
#include "Commands.h"
#include "Application.h"
#include "DragDrop.h"
#include "ClipboardView.h"
#include "Animate.h"
#include "UnitPublic.h"
#include "Stroke.h"
#include "Rects.h"
#include "Draw.h"
#include "Ports.h"
#include "Regions.h"
#include "Screen.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "NSErrors.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "SoundSettings.h"


// the viewFlags bit that makes a view the key view's candidate (a topic's
// paragraph is added with it set, and then without)
const ULong	kTopicAddFlag = 0x8000000;

// ROM 0x00128cec ArrayAppend__FRC6RefVarN21
// The value added to the end of the frame's array slot - the slot made an
// array of the one value when it is nil.
void
ArrayAppend(RefArg frame, RefArg slot, RefArg value)
{
	RefVar array(GetFrameSlotRef(frame, slot));
	if (ISNIL(array))
	{
		array = MakeArray(1);
		SetArraySlotRef(array, 0, value);
		SetFrameSlot(frame, slot, array);
	}
	else
		AddArraySlot(array, value);
}


// an integer slot (nil as the value given)
static long
IntOr(Ref value, long ifNil)
{
	return ISNIL(value) ? ifNil : RINT(value);
}


/*------------------------------------------------------------------------------
	T h e   t o p i c s   a s   f r a m e s
------------------------------------------------------------------------------*/

// ROM 0x00112a28 TopicLevel__FRC6RefVar
long
TopicLevel(RefArg topic)
{
	return IntOr(GetFrameSlotRef(topic, RSSYMlevel), 1);
}


// ROM 0x00112a8c TopicTop__FRC6RefVar
long
TopicTop(RefArg topic)
{
	RefVar bounds(GetProtoVariable(topic, RSSYMviewbounds, nil));
	return (short) RINT(GetFrameSlotRef(bounds, RSSYMtop));
}


// ROM 0x00112af0 TopicVisible__FRC6RefVar
Boolean
TopicVisible(RefArg topic)
{
	RefVar hidden(GetFrameSlotRef(topic, RSSYMhidecount));
	return ISNIL(hidden) || RINT(hidden) == 0;
}


// ROM 0x001129cc TopicHeight__FRC6RefVar
long
TopicHeight(RefArg topic)
{
	long bottom = RINT(FTopicBottom(RefVar(), topic));
	return bottom - TopicTop(topic);
}


// ROM 0x00112b64 VisibleTopicIndex__FRC6RefVarl
// The index among the list's children of the topic's paragraph: the
// visible topics up to it, less `firstTopic`.
long
VisibleTopicIndex(RefArg context, long index)
{
	RefVar topics(GetVariable(context, RSSYMtopics, nil, 0));
	long visible = -1;
	for (long i = 0; i <= index; i++)
		if (TopicVisible(RefVar(GetArraySlotRef(topics, i))))
			visible++;
	return visible - RINT(GetVariable(context, RSSYMfirsttopic, nil, 0));
}


// ROM 0x00111b20 FixTopic__FRC6RefVars
// The topic's box moved to the top given, its height kept.
void
FixTopic(RefArg topic, short top)
{
	Rect bounds;
	FromObject(RefVar(GetVariable(topic, RSSYMviewbounds, nil, 0)), bounds);
	short delta = (short) (top - bounds.top);
	bounds.top = top;
	bounds.bottom = (short) (bounds.bottom + delta);
	SetFrameSlot(topic, RSSYMviewbounds, RefVar(ToObject(bounds)));
}


// ROM 0x00111bc4 MakeDragRef__FP9TListViewl
// What a dragged topic is carried as: a frame of the `index`es of it and
// the topics under it, its `level`, the `topics` themselves (copies, the
// first moved to the top and the others below it) and their `height`.
Ref
MakeDragRef(TListView* list, long index)
{
	RefVar topic;
	RefVar ref(AllocateFrame());
	ArrayAppend(ref, RSSYMindex, RefVar(MAKEINT(index)));
	topic = list->Topic(index);
	long level = TopicLevel(topic);
	SetFrameSlot(ref, RSSYMlevel, RefVar(MAKEINT(level)));
	topic = Clone(topic);
	FixTopic(topic, 0);
	ArrayAppend(ref, RSSYMtopics, topic);
	long height = TopicHeight(topic);
	long count = list->NTopics();
	while (++index < count)
	{
		topic = list->Topic(index);
		if (TopicLevel(topic) <= level)
			break;
		ArrayAppend(ref, RSSYMindex, RefVar(MAKEINT(index)));
		topic = Clone(topic);
		FixTopic(topic, (short) height);
		ArrayAppend(ref, RSSYMtopics, topic);
		if (TopicVisible(topic))
			height += TopicHeight(topic);
	}
	SetFrameSlot(ref, RSSYMheight, RefVar(MAKEINT(height)));
	return ref;
}


// ROM 0x00111db4 MarkerBounds__FRC6RefVarT1R5TRect
// The topic's marker: 16 high from its top, 20 wide to the left of its
// box by the rightMarkGap.
void
MarkerBounds(RefArg context, RefArg topic, Rect* bounds)
{
	RefVar box(GetFrameSlotRef(topic, RSSYMviewbounds));
	bounds->top = (short) RINT(GetFrameSlotRef(box, RSSYMtop));
	bounds->bottom = (short) (bounds->top + 16);
	short left = (short) RINT(GetFrameSlotRef(box, RSSYMleft));
	bounds->right = (short) (left - RINT(GetVariable(context, RSSYMrightmarkgap, nil, 0)));
	bounds->left = (short) (bounds->right - 20);
}


// ROM 0x00111208 AdjustParagraph__FRC6RefVarP14TParagraphViewT2lT4
// A topic's paragraph placed below the one before it (its first baseline
// the next one after that one's last, at least sixteen lower, and at
// least sixteen pixels below that one's top), or at the list's top margin
// when it is the first; then the left and right edges given (-1: as it
// is), and the whole made local to the list.
//
// ROM QUIRK, kept: the right edge is added to the list's *top* rather
// than its left - the ROM loads the other half of the list's origin.
void
AdjustParagraph(RefArg context, TParagraphView* prev, TParagraphView* para, long left, long right)
{
	Point origin = { 0, 0 };
	TView* list = GetView(context);
	origin.v = list->viewBounds.top;
	origin.h = list->viewBounds.left;
	long baseline;
	if (prev == nil)
	{
		long first = para->GetFirstBaseline() - para->viewBounds.top;
		baseline = RINT(GetProtoVariable(context, RSSYMtopmargin, nil)) + first + origin.v;
	}
	else
	{
		long last = prev->GetLastBaseline();
		baseline = prev->GetNextBaseline(para);
		if (baseline <= last + 16)
			baseline = last + 16;
	}
	para->AdjustBoundsForFirstBaseline(baseline);
	Rect bounds = para->viewBounds;
	long gap = prev != nil ? bounds.top - prev->viewBounds.top : 16;
	if (left >= 0 || right >= 0)
	{
		if (left >= 0)
			bounds.left = (short) (origin.h + left);
		if (right >= 0)
			bounds.right = (short) (right + origin.v);
	}
	if (gap < 16)
		OffsetRect(&bounds, 0, 16 - gap);
	OffsetRect(&bounds, -origin.h, -origin.v);
	para->SetBounds(bounds);
}


// ROM 0x001116f0 DrawCheck__FRC6RefVarT1lT3
// The topic's check box (the context's checkBitmaps, 0 clear and 1
// checked; -1 the topic's own mtgDone says which), 13 square below the
// marker's top and 13 to its left - 20 further right when the list has a
// gutter.
void
DrawCheck(RefArg context, RefArg topic, long flags, long checked)
{
	RefVar bitmaps;
	if (checked == -1)
	{
		bitmaps = GetVariable(topic, RSSYMmtgdone, nil, 0);
		checked = NOTNIL(bitmaps) ? 1 : 0;
	}
	bitmaps = GetVariable(context, RSSYMcheckbitmaps, nil, 0);
	Rect box;
	MarkerBounds(context, topic, &box);
	box.top = (short) (box.top + 2);
	box.bottom = (short) (box.top + 13);
	box.right = (short) (box.left + 13);
	OffsetRect(&box, -13, 0);
	if (flags & 1)
		OffsetRect(&box, 20, 0);
	FDrawXBitmap(context, RefVar(ToObject(box)), RefVar(GetArraySlotRef(bitmaps, checked)), RefVar(MAKEINT(0)), RefVar(MAKEINT(0)));
}


// ROM 0x00111870 DrawPriority__FRC6RefVarT1ls
// A To Do topic's priority (mtgPriority, 3 when it has none) out of the
// context's priorityItems - for a first-level topic, or any when the
// list's flags ask for priorities (8).
void
DrawPriority(RefArg context, RefArg topic, long flags, short mode)
{
	if ((flags & 8) == 0 && TopicLevel(topic) >= 2)
		return;
	RefVar items(GetVariable(topic, RSSYMmtgpriority, nil, 0));
	long priority = IntOr(items, 3);
	items = GetVariable(context, RSSYMpriorityitems, nil, 0);
	Rect box;
	MarkerBounds(context, topic, &box);
	box.top = (short) (box.top + 2);
	box.left = (short) (box.left + 1);
	box.bottom = (short) (box.top + 14);
	if (flags & 1)
		OffsetRect(&box, 20, 0);
	FDrawXBitmap(context, RefVar(ToObject(box)), RefVar(GetArraySlotRef(items, priority)), RefVar(MAKEINT(0)), RefVar(MAKEINT(mode)));
}


// ROM 0x00111a1c DrawTopicMarker__FRC6RefVarT1ls
// The topic's triangle (the context's topicMarkers: 0 collapsed, 1
// expanded, 2 hilited), 14 by 11 inside the marker.
void
DrawTopicMarker(RefArg context, RefArg topic, long which, short mode)
{
	Rect box;
	MarkerBounds(context, topic, &box);
	box.left = (short) (box.left + 1);
	box.top = (short) (box.top + 2);
	box.right = (short) (box.left + 14);
	box.bottom = (short) (box.top + 11);
	FDrawXBitmap(context, RefVar(ToObject(box)), RefVar(GetProtoVariable(context, RSSYMtopicmarkers, nil)),
				 RefVar(MAKEINT(which)), RefVar(MAKEINT(mode)));
}


/*------------------------------------------------------------------------------
	T h e   n a t i v e s
------------------------------------------------------------------------------*/

// ROM 0x001113c8 FAdjustParagraph
// :AdjustParagraph(prevContext, context, left, right)
Ref
FAdjustParagraph(RefArg rcvr, RefArg prev, RefArg para, RefArg left, RefArg right)
{
	TParagraphView* prevView = ISNIL(prev) ? nil : (TParagraphView*) GetView(prev);
	TParagraphView* view = (TParagraphView*) GetView(para);
	AdjustParagraph(rcvr, prevView, view, IntOr(left, -1), IntOr(right, -1));
	return TRUEREF;
}


// ROM 0x00111488 FChildTemplateFromTopic
// :ChildTemplateFromTopic(topic): the paragraph a topic is shown as - a
// copy of canonicalParaTopic with its box, text and styles (and the font
// of its first run, when that is a font; the list's topicFont when it
// has one), read-only when the topic has a `source` that is nil.
Ref
FChildTemplateFromTopic(RefArg rcvr, RefArg topic)
{
	RefVar templ(Clone(RefVar(GetProtoVariable(rcvr, RSSYMcanonicalparatopic, nil))));
	RefVar styles(GetFrameSlotRef(topic, RSSYMstyles));
	SetFrameSlot(templ, RSSYMviewbounds, RefVar(GetFrameSlotRef(topic, RSSYMviewbounds)));
	SetFrameSlot(templ, RSSYMtext, RefVar(GetFrameSlotRef(topic, RSSYMtext)));
	SetFrameSlot(templ, RSSYMstyles, styles);
	if (NOTNIL(styles) && Length(styles) > 0 && ISINT(GetArraySlotRef(styles, 1)))
		SetFrameSlot(templ, RSSYMviewfont, RefVar(GetArraySlotRef(styles, 1)));
	RefVar font(GetProtoVariable(rcvr, RSSYMtopicfont, nil));
	if (NOTNIL(font))
		SetFrameSlot(templ, RSSYMviewfont, font);
	if (FrameHasSlot(topic, RSSYMsource) && ISNIL(GetFrameSlotRef(topic, RSSYMsource)))
	{
		long flags = RINT(GetFrameSlotRef(templ, RSSYMviewflags));
		SetFrameSlot(templ, RSSYMviewflags, RefVar(MAKEINT(flags | 2)));
	}
	return templ;
}


// ROM 0x00111ec8 FCollapseTopic
// :CollapseTopic(index, redo): the topics under it hidden a level more
// (a hideCount of nil counts as one), the children rebuilt when asked.
// ==> nil when the topic is itself hidden.  `topics` is read from the
// context itself, not inherited (as in ExpandTopic).
//
// ROM QUIRK, kept: nil counting as one already hidden, a topic with no
// hideCount comes out of a collapse at 2 and of the expand after it at
// 1 - still hidden.  The ROM's stationery gives every topic a 0.
Ref
FCollapseTopic(RefArg rcvr, RefArg index, RefArg redo)
{
	long i = RINT(index);
	RefVar topic;
	RefVar topics(GetFrameSlotRef(rcvr, RSSYMtopics));
	long count = Length(topics);
	if (i < count - 1)
	{
		topic = GetArraySlotRef(topics, i);
		if (!TopicVisible(topic))
			return NILREF;
		if (ISNIL(FIsCollapsed(rcvr, index)))
		{
			long level = TopicLevel(topic);
			i++;
			topic = GetArraySlotRef(topics, i);
			while (i < count && TopicLevel(topic) > level)
			{
				long hidden = IntOr(GetFrameSlotRef(topic, RSSYMhidecount), 1);
				SetFrameSlot(topic, RSSYMhidecount, RefVar(MAKEINT(hidden + 1)));
				i++;
				if (i < count)
					topic = GetArraySlotRef(topics, i);
			}
			if (NOTNIL(redo))
				FRedoChildrenX(rcvr);
		}
	}
	return TRUEREF;
}


// ROM 0x001120ac DrawHilitedData__9TListViewFv
// A topic being dragged (and those under it) drawn with its marker (or
// priority) and box: the edit view's own for anything else.
void
TListView::DrawHilitedData(void)
{
	if (fDragTopic == -2)
	{
		TEditView::DrawHilitedData();
		return;
	}
	long flags = RINT(GetProto(RSSYMlistviewflags));
	long i = fDragTopic;
	RefVar topic(Topic(i));
	long level = ::TopicLevel(topic);
	if (i < NTopics())
	{
		RefVar context(fContext);
		do
		{
			topic = Topic(i);
			if (fDragTopic != i && ::TopicLevel(topic) <= level)
				break;
			if (::TopicVisible(topic))
			{
				if ((flags & 0xc) == 0)
					::DrawTopicMarker(context, topic, NOTNIL(FIsCollapsed(context, RefVar(MAKEINT(i)))) ? 1 : 0, 0);
				else
					DrawPriority(context, topic, flags, 0);
				if (flags & 2)
					DrawCheck(context, topic, flags, -1);
				TView* child = (TView*) fChildren->At(::VisibleTopicIndex(context, i));
				Rect bounds = child->viewBounds;
				child->Draw(bounds, false);
			}
			i++;
		}
		while (i < NTopics());
	}
}


// ROM 0x00112260 FExpandTopic
// :ExpandTopic(index, redo): the topics under it shown a level more (a
// hideCount of nil counts as one; nought at the least).
Ref
FExpandTopic(RefArg rcvr, RefArg index, RefArg redo)
{
	long i = RINT(index);
	RefVar topic;
	RefVar topics(GetFrameSlotRef(rcvr, RSSYMtopics));
	long count = Length(topics);
	if (i < count - 1)
	{
		topic = GetArraySlotRef(topics, i);
		if (!TopicVisible(topic))
			return NILREF;
		if (NOTNIL(FIsCollapsed(rcvr, index)))
		{
			long level = TopicLevel(topic);
			i++;
			topic = GetArraySlotRef(topics, i);
			while (i < count && TopicLevel(topic) > level)
			{
				long hidden = IntOr(GetFrameSlotRef(topic, RSSYMhidecount), 1) - 1;
				if (hidden < 0)
					hidden = 0;
				SetFrameSlot(topic, RSSYMhidecount, RefVar(MAKEINT(hidden)));
				i++;
				if (i < count)
					topic = GetArraySlotRef(topics, i);
			}
			if (NOTNIL(redo))
				FRedoChildrenX(rcvr);
		}
	}
	return TRUEREF;
}


// ROM 0x00112450 FFamilyBottom
// :FamilyBottom(index): the bottom of the last visible topic under it.
Ref
FFamilyBottom(RefArg rcvr, RefArg index)
{
	RefVar topics(GetVariable(rcvr, RSSYMtopics, nil, 0));
	long count = Length(topics);
	long i = RINT(index);
	RefVar topic(GetArraySlotRef(topics, i));
	long level = TopicLevel(topic);
	long last = i;
	while (++i < count)
	{
		topic = GetArraySlotRef(topics, i);
		if (TopicLevel(topic) <= level)
			break;
		if (TopicVisible(topic))
			last = i;
	}
	topic = GetArraySlotRef(topics, last);
	return FTopicBottom(rcvr, topic);
}


// ROM 0x00112558 FIsCollapsed
// :IsCollapsed(index): the next topic is under it and hidden.
Ref
FIsCollapsed(RefArg rcvr, RefArg index)
{
	RefVar topics(GetVariable(rcvr, RSSYMtopics, nil, 0));
	long count = Length(topics);
	long i = RINT(index);
	if (i < count - 1)
	{
		RefVar next(GetArraySlotRef(topics, i + 1));
		long nextLevel = TopicLevel(next);
		long level = TopicLevel(RefVar(GetArraySlotRef(topics, i)));
		if (level < nextLevel && !TopicVisible(next))
			return TRUEREF;
	}
	return NILREF;
}


// ROM 0x00112658 FListBottom
// :ListBottom(): the bottom of the last topic shown - lastTopic's when
// the list shows only the children it needs, else the last visible one;
// 0 for no topics.
Ref
FListBottom(RefArg rcvr)
{
	RefVar topic;
	RefVar topics(GetVariable(rcvr, RSSYMtopics, nil, 0));
	long slot = Length(topics);
	if (slot == 0)
		return MAKEINT(0);
	if (NOTNIL(GetVariable(rcvr, RSSYMminimalchildren, nil, 0)))
	{
		long last = RINT(GetVariable(rcvr, RSSYMlasttopic, nil, 0));
		if (last >= 0)
		{
			topic = GetArraySlotRef(topics, last);
			return FTopicBottom(rcvr, topic);
		}
	}
	do
	{
		slot--;
		topic = GetArraySlotRef(topics, slot);
		if (TopicVisible(topic))
			break;
	}
	while (slot > 0);
	return FTopicBottom(rcvr, topic);
}


// ROM 0x00112784 FMakeDragRef
Ref
FMakeDragRef(RefArg rcvr, RefArg index)
{
	return MakeDragRef((TListView*) GetView(rcvr), RINT(index));
}


// ROM 0x001127c4 FMarkerBounds
Ref
FMarkerBounds(RefArg rcvr, RefArg topic)
{
	Rect bounds;
	MarkerBounds(rcvr, topic, &bounds);
	return ToObject(bounds);
}


// ROM 0x001127f4 FSetupVisibleChildren
// :SetupVisibleChildren(first, all, x) (the ROM has the method inline)
Ref
FSetupVisibleChildren(RefArg rcvr, RefArg first, RefArg all, RefArg x)
{
	TListView* view = (TListView*) GetView(rcvr);
	return view->SetupVisibleChildren(RINT(first), NOTNIL(all), NOTNIL(x));
}


// ROM 0x00112864 FTopicBottom
// :TopicBottom(topic): its box's bottom, sixteen below its top at least.
Ref
FTopicBottom(RefArg /*rcvr*/, RefArg topic)
{
	RefVar bounds(GetVariable(topic, RSSYMviewbounds, nil, 0));
	long bottom = RINT(GetFrameSlotRef(bounds, RSSYMbottom));
	long top = RINT(GetFrameSlotRef(bounds, RSSYMtop));
	long least = top + 16;
	if (least <= bottom)
		least = bottom;
	return MAKEINT(least);
}


// ROM 0x00112904 FTopicIndexToView
// :TopicIndexToView(index): the context of the topic's paragraph; nil
// when it has none.
Ref
FTopicIndexToView(RefArg rcvr, RefArg index)
{
	TView* list = GetView(rcvr);
	TView* child = (TView*) list->fChildren->At(VisibleTopicIndex(RefVar(list->fContext), RINT(index)));
	if (child == nil)
		return NILREF;
	return child->fContext;
}


// ROM 0x00112990 FVisibleTopicIndex
Ref
FVisibleTopicIndex(RefArg rcvr, RefArg index)
{
	return MAKEINT(VisibleTopicIndex(rcvr, RINT(index)));
}


/*------------------------------------------------------------------------------
	T L i s t V i e w
------------------------------------------------------------------------------*/

// ROM 0x0010eec4 ClassID__9TListViewCFv
long
TListView::ClassID(void) const
{
	return clListView;
}


// ROM 0x0010eecc DerivedFrom__9TListViewCFl
Boolean
TListView::DerivedFrom(long id) const
{
	return id == clListView || TEditView::DerivedFrom(id);
}


// ROM 0x0010ef00 GadgetWidth__9TListViewFv
// The width of the gutter the markers are in (a sum that is not an
// integer is a bad-type error).
long
TListView::GadgetWidth(void)
{
	Ref left = GetProto(RSSYMleftmarkgap);
	Ref right = GetProto(RSSYMrightmarkgap);
	Ref sum = left + right;
	if ((sum & 3) != 0)
	{
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, RefVar(sum));
		return 0;
	}
	return RVALUE(sum);
}


// ROM 0x0010ef48 GetDropData__9TListViewFRC6RefVarT1
// A dragged topic (or task), or text that was not a view, is the plain
// view's to answer; the rest the edit view's.
Ref
TListView::GetDropData(RefArg dragType, RefArg dragRef)
{
	if (EQRef(dragType, RSSYMtopic) || EQRef(dragType, RSSYMtask))
		return TView::GetDropData(dragType, dragRef);
	if (EQRef(dragType, RSSYMtext) && (!IsFrame(dragRef) || GetView(dragRef) == nil))
		return TView::GetDropData(dragType, dragRef);
	return TEditView::GetDropData(dragType, dragRef);
}


// ROM 0x0010f014 HandlePenDown__9TListViewFRC6RefVar
// The pen down on the list: below the last topic nothing; above the top
// margin the first topic gets the caret; else, from firstTopic, the
// first visible topic whose marker (or check box) the pen is in - a
// marker is tracked as a tap or a drag (when the list takes topic drags),
// a box as a check - stopping at the first marker below the pen.  ==> 1
// (the command's result as well) when something took it.
long
TListView::HandlePenDown(RefArg cmd)
{
	long result = 0;
	long listBottom = RINT(FListBottom(RefVar(fContext)));
	Rect bounds = viewBounds;
	TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
	Point pt = unit->Stroke()->FirstPoint();
	RefVar topics;
	RefVar topic;
	long flags = RINT(GetProto(RSSYMlistviewflags));
	pt.v = (short) (pt.v - bounds.top);
	if (listBottom < pt.v)
		return 0;
	topics = Topics();
	long count = Length(topics);
	short topMargin = (short) RINT(GetProto(RSSYMtopmargin));
	if (topMargin > pt.v && count > 0)
		gRootView->SetKeyView((TView*) fChildren->At(0), 99999, 0, false);
	pt.h = (short) (pt.h - bounds.left);
	for (long i = RINT(GetProto(RSSYMfirsttopic)); i < count; i++)
	{
		topic = GetArraySlotRef(topics, i);
		if (!::TopicVisible(topic))
			continue;
		MarkerBounds(i, &bounds);
		if (flags & 1)
			OffsetRect(&bounds, 20, 0);
		InsetRect(&bounds, 1, 2);
		bounds.right = (short) (bounds.right - 2);
		if ((flags & 1) == 0 && PtInRect(pt, &bounds))
		{
			if (NOTNIL(GetProto(RSSYMtopicdraginfo)))
				result = TrackTopic(cmd, i);
			break;
		}
		Rect box = bounds;
		if (flags & 2)
		{
			OffsetRect(&box, -13, 0);
			if (PtInRect(pt, &box))
			{
				TrackCheck(cmd, i);
				result = 1;
				break;
			}
		}
		if (bounds.bottom > pt.v)
			break;
	}
	if (result != 0)
		CommandSetResult(cmd, result);
	return result;
}


// ROM 0x0010f338 HandleTap__9TListViewFR6TPoint
// A tap: the selection let go; in a topic's text the tap is the
// paragraph's; below the last topic the caret goes there.
void
TListView::HandleTap(Point& pt)
{
	RemoveAllHilites();
	TView* text = TextContainingPoint(pt, nil, nil);
	if (text != nil && (text->fFlags & 0x82) == 0)
	{
		((TDataView*) text)->HandleTap(pt);
		return;
	}
	long listBottom = RINT(FListBottom(RefVar(fContext)));
	if (pt.v - viewBounds.top <= listBottom)
		return;
	PositionCaret(pt, true);
}


// ROM 0x0010f3e0 IndexFromY__9TListViewFl
// The topic above which a y (local to the list) falls: the one before
// the first visible topic whose middle it is above; the last when it is
// below them all.
long
TListView::IndexFromY(long y)
{
	RefVar topic;
	RefVar topics(Topics());
	long count = NTopics();
	for (long i = 0; i < count; i++)
	{
		if (!TopicVisible(i))
			continue;
		Rect bounds;
		MarkerBounds(i, &bounds);
		topic = GetArraySlotRef(topics, i);
		bounds.bottom = (short) RINT(FTopicBottom(RefVar(), topic));
		if (y < MidPoint(bounds).v)
			return i - 1;
	}
	return count - 1;
}


// ROM 0x0010f510 LevelFromX__9TListViewFlT1
// The level an x (local) stands for - one per 20 pixels past the gutter,
// no deeper than maxLevel or one below the topic before; 1 at the far
// left.
long
TListView::LevelFromX(long x, long index)
{
	if (x < 10)
		return 1;
	long past = x - GadgetWidth();
	if (past < 0)
		past = 0;
	long level = past / 20;
	long maxLevel = RINT(GetVar(RSSYMmaxlevel));
	if (level + 1 < maxLevel)
		maxLevel = level + 1;
	level = maxLevel;
	if (index != -2)
	{
		RefVar topic(Topic(index));
		long below = ::TopicLevel(topic) + 1;
		level = below;
		if (maxLevel < below)
			level = maxLevel;
	}
	return level;
}


// ROM 0x0010f5e8 MarkerBounds__9TListViewFlR5TRect
// (the free MarkerBounds, with the list's own rightMarkGap)
void
TListView::MarkerBounds(long index, Rect* bounds)
{
	RefVar topic(Topic(index));
	RefVar box(GetFrameSlotRef(topic, RSSYMviewbounds));
	bounds->top = (short) RINT(GetFrameSlotRef(box, RSSYMtop));
	bounds->bottom = (short) (bounds->top + 16);
	short left = (short) RINT(GetFrameSlotRef(box, RSSYMleft));
	bounds->right = (short) (left - RINT(GetVar(RSSYMrightmarkgap)));
	bounds->left = (short) (bounds->right - 20);
}


// ROM 0x0010f700 NTopics__9TListViewFv
long
TListView::NTopics(void)
{
	return Length(Topics());
}


// ROM 0x0010f718 PointToCaret__9TListViewFR6TPointP5TRectT2
// Where a dragged topic would go if dropped at the point: fDropIndex (the
// topic it would follow; -2 nowhere - inside itself, or where the level
// would leave the next topic stranded) and fDropLevel, both put in the
// context's `dragTo`, and the caret - a line 16 wide and 2 high at the
// drop's level, below the topic it would follow (or at the top of the
// dragged one, when it would stay where it is).  No caret: top and
// bottom -32768.
void
TListView::PointToCaret(Point& pt, Rect* caret, Rect* /*bounds*/)
{
	RefVar dragTo(AllocateFrame());
	Rect list = viewBounds;
	caret->top = -32768;
	caret->bottom = -32768;
	long index = IndexFromY(pt.v - list.top);
	fDropIndex = index;
	RefVar context(fContext);
	if (index == -2)
	{
		SetFrameSlot(dragTo, RSSYMindex, RefVar(MAKEINT(-2)));
		SetFrameSlot(dragTo, RSSYMlevel, RefVar(MAKEINT(1)));
		SetFrameSlot(context, RSSYMdragto, dragTo);
		return;
	}
	if (fDragTopic == index)
	{
		index--;
		fDropIndex = index;
	}
	fDropLevel = index == -1 ? 1 : LevelFromX(pt.h, index);
	Boolean stays = fDragTopic != -2 && fDragTopic - 1 == index;
	if ((!stays && index != -1 && index < NTopics() - 1 && fDropLevel < TopicLevel(index + 1))
	 || (fDragTopic != -2 && !stays && fDragTopic < index && index <= FamilySize(fDragTopic) + fDragTopic - 1))
	{
		fDropIndex = -2;
		SetFrameSlot(dragTo, RSSYMindex, RefVar(MAKEINT(-2)));
		SetFrameSlot(dragTo, RSSYMlevel, RefVar(MAKEINT(fDropLevel)));
		SetFrameSlot(context, RSSYMdragto, dragTo);
		return;
	}
	caret->left = (short) (GadgetWidth() + (fDropLevel - 1) * 20);
	if (caret->left == list.right)
		caret->left = (short) (caret->left - 17);
	RefVar topics(Topics());
	if (index == -1)
		caret->top = 0;
	else if (index < Length(topics))
	{
		if (stays)
			caret->top = (short) TopicTop(RefVar(GetArraySlotRef(topics, fDragTopic)));
		else
		{
			RefVar topic(GetArraySlotRef(topics, index));
			while (!::TopicVisible(topic) && index > 0)
			{
				index--;
				topic = GetArraySlotRef(topics, index);
			}
			caret->top = (short) RINT(FTopicBottom(RefVar(), topic));
		}
	}
	else
		caret->top = (short) RINT(FListBottom(context));
	caret->left = (short) (caret->left + list.left);
	caret->top = (short) (caret->top + list.top);
	caret->right = (short) (caret->left + 16);
	caret->bottom = (short) (caret->top + 2);
	SetFrameSlot(dragTo, RSSYMindex, RefVar(MAKEINT(fDropIndex)));
	SetFrameSlot(dragTo, RSSYMlevel, RefVar(MAKEINT(fDropLevel)));
	SetFrameSlot(context, RSSYMdragto, dragTo);
}


// ROM 0x0010fbe8 RealDoCommand__9TListViewFRC6RefVar
// A click on a clickable list is the pen down (and nothing more when
// that does not take it and the view is not clickable).
Boolean
TListView::RealDoCommand(RefArg cmd)
{
	if (CommandID(cmd) == aeClick)
	{
		if ((fFlags & vClickable) == 0)
			return false;
		long result = HandlePenDown(cmd);
		if (result != 0)
			return result;
	}
	return TEditView::RealDoCommand(cmd);
}


// ROM 0x0010fc50 Constructor__9TListViewFRC6RefVarP5TView
void
TListView::Constructor(RefArg context, TView* parent)
{
	TEditView::Constructor(context, parent);
	fDragTopic = -2;
}


// ROM 0x0010fc70 RealDraw__9TListViewFR5TRect
// The markers (or priorities) and check boxes of the topics from
// firstTopic to lastTopic that are in the area being drawn - every one
// while the list is reflowing, except the first-level markers' collapsed
// state then not being asked unless `reflow` is nil.
void
TListView::RealDraw(Rect& bounds)
{
	RefVar topic;
	RefVar topics(Topics());
	long flags = RINT(GetProto(RSSYMlistviewflags));
	long top = (short) (bounds.top - viewBounds.top);
	long bottom = (short) (bounds.bottom - viewBounds.top);
	Boolean reflowing = false;
	RefVar first(GetProto(RSSYMfirsttopic));
	RefVar last(GetProto(RSSYMlasttopic));
	RefVar reflow(GetVar(RSSYMreflow));
	if (NOTNIL(reflow))
		reflowing = !EQRef(GetVar(RSSYMreflowoptions), RSSYMvisible);
	RefVar context(fContext);
	for (TObjectIterator iter(topics); !iter.Done(); iter.Next())
	{
		Ref tag = iter.Tag();
		if ((Ref) first > tag)
			continue;
		if (NOTNIL(last) && (Ref) last < tag)
			break;
		topic = iter.Value();
		if (!reflowing)
		{
			if (!::TopicVisible(topic))
				continue;
			long topicTop = TopicTop(topic);
			if (bottom < topicTop)
				return;
			if (top > topicTop + 16)
				continue;
		}
		long which = 1;
		if (ISNIL(reflow) && ISNIL(FIsCollapsed(context, RefVar(tag))))
			which = 0;
		if ((flags & 0xc) == 0)
		{
			if ((flags & 1) == 0)
				::DrawTopicMarker(context, topic, which, 0);
		}
		else
			DrawPriority(context, topic, flags, 0);
		if (flags & 2)
			DrawCheck(context, topic, flags, -1);
	}
}


// ROM 0x0010ff6c Scrub__9TListViewFP11TUnitPublic
// A scrub: over the selection it deletes it (with a poof); else the
// context's handleScrub is asked, with the scrub's box made local - TRUE
// it did it (poof), anything else not nil it did something - and when
// it answers nil the edit view's own scrub runs (with `doingScrub` set).
// The list's parent is made dirty over the box afterwards.
long
TListView::Scrub(TUnitPublic* unit)
{
	TAnimate animate;
	Rect box;
	unit->Bounds(&box);
	animate.SetupPoofEffect(this, box);
	long result;
	Boolean poof;
	if (ScrubHilite(box))
	{
		result = 1;
		poof = true;
	}
	else
	{
		OffsetRect(&box, -viewBounds.left, -viewBounds.top);
		RefVar args(MakeArray(2));
		SetArraySlotRef(args, 0, ToObject(box));
		SetArraySlotRef(args, 1, AddressToRef(unit));
		RefVar answer(RunScript(RSSYMhandlescrub, args, true));
		result = NOTNIL(answer) ? 1 : 0;
		poof = EQRef(answer, TRUEREF);
		if (result == 0)
		{
			RefVar context(fContext);
			SetFrameSlot(context, RSSYMdoingscrub, RefVar(TRUEREF));
			result = TEditView::Scrub(unit);
			SetFrameSlot(context, RSSYMdoingscrub, RefVar(NILREF));
			fParent->Dirty(&box);
			return result;
		}
	}
	gRootView->fDirtyFlag = true;
	unit->Stroke()->InkOff(false);
	if (poof)
		animate.DoEffect(RefVar(Rpoof));
	fParent->Dirty(&box);
	return result;
}


// ROM 0x001101f0 SetupVisibleChildren__9TListViewFlUcT2
// A paragraph child for each topic from `first` on that is visible (or
// every one, when asked), made from ChildTemplateFromTopic and laid out
// below the one before at the topic's level (and then removed again:
// the list's viewChildren are what is answered); with minimalChildren set
// only those in the part of the list that shows - lastTopic set to the
// one before the first that is below it.  Each topic's box is set to
// where its paragraph came out.  ==> the children's templates.
Ref
TListView::SetupVisibleChildren(long first, Boolean all, Boolean /*x*/)
{
	RefVar topic;
	RefVar topics(Topics());
	long slot = 0;
	RefVar templ;
	RefVar children;
	Boolean everything = ISNIL(GetVar(RSSYMminimalchildren));
	long flags = RINT(GetProto(RSSYMlistviewflags));
	GrafPort* port;
	GetPort(&port);
	Rect visible = (*port->visRgn)->rgnBBox;
	SectRect(&viewBounds, &visible, &visible);
	OffsetRect(&visible, -viewBounds.left, -viewBounds.top);
	long right = (short) (RINT(GetProto(RSSYMrightindent)) + viewBounds.right);
	RefVar context(fContext);
	if (!everything)
		SetVariable(context, RSSYMlasttopic, RefVar(MAKEINT(NTopics() - 1)));
	StartDrawing(nil, nil);
	children = MakeArray(NTopics());
	long leftGap = RINT(GetProto(RSSYMleftmarkgap));
	long rightGap = RINT(GetProto(RSSYMrightmarkgap));
	TParagraphView* prev = nil;
	for (TObjectIterator iter(topics); !iter.Done(); iter.Next())
	{
		TParagraphView* para = prev;
		long index = RINT(iter.Tag());
		if (first <= index)
		{
			topic = iter.Value();
			if (all || ::TopicVisible(topic))
			{
				templ = FChildTemplateFromTopic(context, topic);
				long left = (short) (leftGap + ::TopicLevel(topic) * 20 + rightGap - 20);
				if ((flags & 1) == 0 || (flags & 0xc) != 0)
					left = (short) (left + 20);
				long viewFlags = RINT(GetFrameSlotRef(templ, RSSYMviewflags));
				SetFrameSlot(templ, RSSYMviewflags, RefVar(MAKEINT(viewFlags | kTopicAddFlag)));
				SetFrameSlot(templ, RSSYMindex, RefVar(MAKEINT(-1)));
				para = (TParagraphView*) AddView(templ);
				SetFrameSlot(templ, RSSYMviewflags, RefVar(MAKEINT(viewFlags)));
				AdjustParagraph(context, prev, para, left, right);
				Rect box;
				FromObject(RefVar(para->GetProto(RSSYMviewbounds)), box);
				if (!everything)
				{
					if (!(box.top <= visible.bottom && visible.top <= box.bottom))
					{
						if (box.top <= visible.bottom)
						{
							prev = para;
							continue;
						}
						SetVariable(context, RSSYMlasttopic, RefVar(MAKEINT(index - 1)));
						break;
					}
				}
				SetFrameSlot(templ, RSSYMindex, RefVar(iter.Tag()));
				SetArraySlotRef(children, slot, templ);
				SetFrameSlot(topic, RSSYMviewbounds, RefVar(ToObject(box)));
				slot++;
			}
		}
		prev = para;
	}
	RemoveAllViews();			// the children were only made to be measured
	StopDrawing(nil, nil);
	SetLength(children, slot);
	return children;
}


// ROM 0x001107a0 Topic__9TListViewFl
Ref
TListView::Topic(long index)
{
	return GetArraySlotRef(RefVar(Topics()), index);
}


// ROM 0x001107dc TopicIndexToView__9TListViewFl
Ref
TListView::TopicIndexToView(long index)
{
	TView* child = (TView*) fChildren->At(VisibleTopicIndex(RefVar(fContext), index));
	if (child == nil)
		return NILREF;
	return child->fContext;
}


// ROM 0x00110814 TopicLevel__9TListViewFl
long
TListView::TopicLevel(long index)
{
	return ::TopicLevel(RefVar(Topic(index)));
}


// ROM 0x0011084c Topics__9TListViewFv
// The context's `topics` (inherited through its protos); a list with no
// context is an error.
Ref
TListView::Topics(void)
{
	if (ISNIL(fContext))
		ThrowExInterpreterWithSymbol(kNSErrUndefinedVariable, RSSYMtopics);
	return GetProto(RSSYMtopics);
}


// ROM 0x00110858 TopicVisible__9TListViewFl
Boolean
TListView::TopicVisible(long index)
{
	return ::TopicVisible(RefVar(Topic(index)));
}


// ROM 0x00110890 TrackCheck__9TListViewFRC6RefVarl
// The pen in a topic's check box: the box redrawn checked or not as the
// pen goes in and out of it until it comes up, and when it comes up
// inside, the context's handleCheck told.  ==> whether it came up inside.
long
TListView::TrackCheck(RefArg cmd, long index)
{
	TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
	TStrokePublic* stroke = unit->Stroke();
	stroke->InkOff(true);
	FClicker(RefVar());
	RefVar topic(Topic(index));
	long inside = 0;
	long wasInside = 0;
	long flags = RINT(GetProto(RSSYMlistviewflags));
	RefVar done(GetVariable(topic, RSSYMmtgdone, nil, 0));
	Boolean checked = NOTNIL(done);
	RefVar context(fContext);
	Rect box;
	::MarkerBounds(context, topic, &box);
	box.top = (short) (box.top + 4);
	box.bottom = (short) (box.top + 11);
	box.right = (short) (box.left + 13);
	OffsetRect(&box, -13, 0);
	if (flags & 1)
		OffsetRect(&box, 20, 0);
	OffsetRect(&box, viewBounds.left, viewBounds.top);
	do
	{
		Point pt = stroke->FinalPoint();
		inside = PtInRect(pt, &box);
		if (inside == wasInside)
			Wait(1);
		else
		{
			checked = !checked;
			DrawCheck(context, topic, flags, checked);
			wasInside = inside;
		}
	}
	while (!stroke->Done());
	if (inside)
	{
		RefVar args(MakeArray(2));
		SetArraySlotRef(args, 0, MAKEINT(index));
		SetArraySlotRef(args, 1, topic);
		RunScript(RSSYMhandlecheck, args, true);
	}
	OffsetRect(&box, -viewBounds.left, -viewBounds.top);
	Dirty(&box);
	return inside;
}


// ROM 0x00110b8c TrackTopic__9TListViewFRC6RefVarl
// The pen on a topic's marker, hilited: moved more than three pixels it
// drags the topic and those under it (a 'topic drag of MakeDragRef,
// labelled topicsLabel when there are several, its bounds from the
// marker's column to the list's right and its pin the marker's column);
// lifted where it went down it toggles the topic (the context's
// toggleTopic, and curTopic set).  ==> 1.
long
TListView::TrackTopic(RefArg cmd, long index)
{
	TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
	TStrokePublic* stroke = unit->Stroke();
	stroke->InkOff(true);
	Rect marker;
	MarkerBounds(index, &marker);
	Boolean dragged = false;
	Point first = stroke->FirstPoint();
	Point pt = first;
	Ticks();
	RefVar topic(Topic(index));
	long flags = RINT(GetProto(RSSYMlistviewflags));
	RefVar context(fContext);
	if (flags & 0xc)
		DrawPriority(context, topic, flags, 2);
	::DrawTopicMarker(context, topic, 2, 0);
	FClicker(RefVar());
	if (!stroke->Done())
	{
		do
		{
			pt = stroke->FinalPoint();
			if (CheapDistance(pt, first) < 4)
				Wait(1);
			else
			{
				dragged = true;
				((TUnitPublic*) CommandParameter(cmd))->Stroke()->FirstPoint();
				Rect bounds = viewBounds;
				OffsetRect(&bounds, -viewBounds.left, -viewBounds.top);
				bounds.bottom = (short) (RINT(FFamilyBottom(context, RefVar(MAKEINT(index)))) + bounds.top);
				bounds.top = (short) (TopicTop(topic) + bounds.top);
				bounds.left = (short) ((short) GadgetWidth() + (::TopicLevel(topic) - 1) * 20 + bounds.left);
				::DrawTopicMarker(context, topic, 2, 2);
				RefVar dragRef(MakeDragRef(this, index));
				OffsetRect(&bounds, viewBounds.left, viewBounds.top);
				Rect pin = bounds;
				pin.right = (short) (bounds.left + 20);
				fDragTopic = index;
				RefVar info(Clone(RefVar(GetProto(RSSYMtopicdraginfo))));
				if (Length(RefVar(GetFrameSlotRef(dragRef, RSSYMindex))) > 1)
					SetFrameSlot(info, RSSYMlabel, RefVar(GetProto(RSSYMtopicslabel)));
				SetFrameSlot(info, RSSYMdragref, dragRef);
				RefVar items(MakeArray(1));
				SetArraySlotRef(items, 0, info);
				TDragInfo dragInfo(items);
				DragAndDrop(stroke, bounds, &pin, &bounds, false, dragInfo, nil);
				fDragTopic = -2;
			}
		}
		while (!stroke->Done());
		if (dragged)
			goto done;
	}
	{
		Point local;
		local.h = (short) (first.h - viewBounds.left);
		local.v = (short) (first.v - viewBounds.top);
		if (PtInRect(local, &marker))
		{
			DrawTopicMarker(index, 2, 0);
			RefVar args(MakeArray(1));
			SetArraySlotRef(args, 0, MAKEINT(index));
			RunScript(RSSYMtoggletopic, args, false);
			SetVariable(context, RSSYMcurtopic, RefVar(MAKEINT(index)));
		}
	}
done:
	OffsetRect(&marker, viewBounds.left, viewBounds.top);
	Dirty(&marker);
	return 1;
}


// ROM 0x00111120 DragFeedback__9TListViewFRC9TDragInfoRC6TPointUc
// A topic or task dragged over the list: the drop caret inverted where
// PointToCaret puts it; anything else the plain view's feedback.
Boolean
TListView::DragFeedback(const TDragInfo& dragInfo, const Point& pt, Boolean show)
{
	RefVar type(dragInfo.GetItemIndType(0, 0));
	if (!EQRef(type, RSSYMtopic) && !EQRef(type, RSSYMtask))
		return TView::DragFeedback(dragInfo, pt, show);
	Point where = pt;
	Rect caret;
	PointToCaret(where, &caret, nil);
	if (caret.top != -32768)
		InvertRect(&caret);
	return show;
}


// ROM 0x00112944 DrawTopicMarker__9TListViewFlT1s
void
TListView::DrawTopicMarker(long index, long which, short mode)
{
	::DrawTopicMarker(RefVar(fContext), RefVar(Topic(index)), which, mode);
}


// ROM 0x00112c28 DropMove__9TListViewFRC6RefVarRC6TPointT2Uc
// A view dropped on the list from the list itself: its data taken as
// the list's drop data (moved by the delta) and dropped at the point,
// the original removed - or, for a copy, left and unhilited.  Anything
// else the plain view's.
Boolean
TListView::DropMove(RefArg dragRef, const Point& delta, const Point& dropPt, Boolean copy)
{
	TView* view;
	if (IsFrame(dragRef) && (view = GetView(dragRef)) != nil)
	{
		TDragInfo info(0L);
		view->AddDragInfo(&info);
		RefVar type(info.GetItemIndType(0, 0));
		RefVar data(GetDropData(type, dragRef));
		OffsetBoundsRef(data, delta);
		Point pt = dropPt;
		Drop(type, data, &pt);
		if (!copy)
			view->DropRemove(dragRef);
		else
			view->RemoveAllHilites();
		return true;
	}
	return TView::DropMove(dragRef, delta, dropPt, copy);
}


// ROM 0x00112d74 DropRemove__9TListViewFRC6RefVar
// A dragged topic (a frame with no view) taken away by the context's
// viewDropRemoveScript; a view taken away by the plain view, else by
// itself, else by a remove command to it.
Boolean
TListView::DropRemove(RefArg dragRef)
{
	if (IsFrame(dragRef) && GetView(dragRef) == nil)
	{
		RefVar args(MakeArray(1));
		SetArraySlotRef(args, 0, dragRef);
		return NOTNIL(RunScript(RSSYMviewdropremovescript, args, true));
	}
	if (!TView::DropRemove(dragRef))
	{
		TView* view = FailGetView(dragRef);
		if (!view->DropRemove(dragRef))
			gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, view->fId)));
	}
	return true;
}


// ROM 0x00112dc4 FamilySize__9TListViewFl
long
TListView::FamilySize(long index)
{
	RefVar topics(Topics());
	RefVar topic(GetArraySlotRef(topics, index));
	long level = ::TopicLevel(topic);
	long count = NTopics();
	long i = index;
	do
	{
		i++;
		if (count <= i)
			break;
	}
	while (level < TopicLevel(i));
	return i - index;
}


// ROM 0x00112e5c FindDropView__9TListViewFRC9TDragInfoRC6TPoint
// A topic or task is dropped on the plain view's terms; anything else on
// the edit view's, except above the last topic (nil: the list takes it
// nowhere).
TView*
TListView::FindDropView(const TDragInfo& dragInfo, const Point& pt)
{
	RefVar type(dragInfo.GetItemIndType(0, 0));
	if (!EQRef(type, RSSYMtopic) && !EQRef(type, RSSYMtask))
	{
		long listBottom = RINT(FListBottom(RefVar(fContext)));
		long top = viewBounds.top;
		TView* view = TEditView::FindDropView(dragInfo, pt);
		if (view == this && pt.v - top < listBottom)
			return nil;
		return view;
	}
	return TView::FindDropView(dragInfo, pt);
}


void
RegisterListViewNatives(void)
{
	RegisterNativeFunction("FAdjustParagraph", (void*) FAdjustParagraph, 4);
	RegisterNativeFunction("FChildTemplateFromTopic", (void*) FChildTemplateFromTopic, 1);
	RegisterNativeFunction("FCollapseTopic", (void*) FCollapseTopic, 2);
	RegisterNativeFunction("FExpandTopic", (void*) FExpandTopic, 2);
	RegisterNativeFunction("FFamilyBottom", (void*) FFamilyBottom, 1);
	RegisterNativeFunction("FIsCollapsed", (void*) FIsCollapsed, 1);
	RegisterNativeFunction("FListBottom", (void*) FListBottom, 0);
	RegisterNativeFunction("FMakeDragRef", (void*) FMakeDragRef, 1);
	RegisterNativeFunction("FMarkerBounds", (void*) FMarkerBounds, 1);
	RegisterNativeFunction("FSetupVisibleChildren", (void*) FSetupVisibleChildren, 3);
	RegisterNativeFunction("FTopicBottom", (void*) FTopicBottom, 1);
	RegisterNativeFunction("FTopicIndexToView", (void*) FTopicIndexToView, 1);
	RegisterNativeFunction("FVisibleTopicIndex", (void*) FVisibleTopicIndex, 1);
}
