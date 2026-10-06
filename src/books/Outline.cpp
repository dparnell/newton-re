/*
	File:		books/Outline.cpp

	Contains:	TOutline and THelpOutline: the book reader's outline.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Outline.h"
#include "Librarian.h"
#include "Pages.h"
#include "Commands.h"
#include "Application.h"
#include "UnitPublic.h"
#include "Stroke.h"
#include "Rects.h"
#include "Draw.h"
#include "Shapes.h"
#include "Fonts.h"
#include "Text.h"
#include "Ports.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "NewtonExceptions.h"
#include "host/RomBugs.h"
#include "NewtonMemory.h"

extern const ExceptionName exOutOfMemory;


/*------------------------------------------------------------------------------
	T O u t l i n e
------------------------------------------------------------------------------*/

// (BuildView makes the object; the ROM's has its three RefHandles made
// there, at +0x30, +0x60 and +0x64)
TOutline::TOutline()
	: fFont(new RefStruct(NILREF)), fCount(0), fTopics(nil), fSelection(-1), fField40(0),
	  fScroll(0), fCurrent(-1), fDynamic(false), fHeader(false), fLineHeight(0), fAscent(0),
	  fHeight(0), fPane(0), fViewable(0), fPaneIndex(-1),
	  fBrowser(new RefStruct(NILREF)), fList(new RefStruct(NILREF))
{ }


// ROM 0x0014d3a0 __dt__8TOutlineFv
TOutline::~TOutline()
{
	DisposPtr((Ptr) fTopics);
	delete fList;
	delete fBrowser;
	delete fFont;
}


// ROM 0x0014c49c ClassID__8TOutlineCFv
long
TOutline::ClassID(void) const
{
	return clOutlineView;
}


// ROM 0x0014c4a4 DerivedFrom__8TOutlineCFl
Boolean
TOutline::DerivedFrom(long id) const
{
	return id == clOutlineView || TView::DerivedFrom(id);
}


// ROM 0x0014ed64 Constructor__8TOutlineFRC6RefVarP5TView
// The view made; its font measured for the line height and the ascent;
// viewableTopics (9 when it has none); the topics made (RefreshTopics(0)).
void
TOutline::Constructor(RefArg context, TView* parent)
{
	StyleRecord style;
	FontInfo info;
	TView::Constructor(context, parent);
	*fFont = GetVariable(fContext, RSSYMviewfont, nil, 0);
	CreateTextStyleRecord(RefVar(*fFont), &style);
	GetStyleFontInfo(&style, &info);
	fLineHeight = info.descent + info.ascent + info.leading;
	fAscent = info.ascent;
	RefVar viewable(GetVariable(fContext, RSSYMviewabletopics, nil, 0));
	fViewable = ISNIL(viewable) ? 9 : RINT(viewable);
	RefreshTopics(0);
	fCurrent = -1;
	if (style.fPattern != nil)
		DisposePattern(style.fPattern);
}


// ROM 0x0038abac (unnamed) - the vtable's +0x15c
// How far the list is scrolled, in pixels.
long
TOutline::ScrollPos(void)
{
	return fScroll;
}


// ROM 0x0038abf4 (unnamed) - the vtable's +0x180
// How many lines the view shows.
long
TOutline::ViewableTopics(void)
{
	return fViewable;
}


// (Not in TOutline's vtable: the ROM's TopicByName on a TOutline calls
// whatever follows it, the next class's destructor.)
Ref
TOutline::TopicByName(RefArg name)
{
	return NILREF;
}


// ROM 0x0014e7dc Browser__8TOutlineFv
// The browser the outline shows: browsers[paneIndex] (browsers[0] with no
// pane index), kept once found.
Ref
TOutline::Browser(void)
{
	if (NOTNIL(*fBrowser))
		return *fBrowser;
	RefVar browsers(GetVar(RSSYMbrowsers));
	long index = fPaneIndex;
	if (index == -1)
		index = 0;
	*fBrowser = GetArraySlotRef(browsers, index);
	return *fBrowser;
}


// ROM 0x0014d0b8 List__8TOutlineFv
// The browser's list of topics (of a browser with a header, the pane's
// list), kept once found.
Ref
TOutline::List(void)
{
	if (NOTNIL(*fList))
		return *fList;
	RefVar browser(Browser());
	*fList = GetFrameSlotRef(browser, RSSYMlist);
	if (fHeader)
		*fList = GetArraySlotRef(*fList, fPane);
	return *fList;
}


// ROM 0x0014dac8 TopicFrame__8TOutlineFl
Ref
TOutline::TopicFrame(long index)
{
	RefVar list(List());
	return GetArraySlotRef(list, index);
}


// ROM 0x0014eebc CountTopics__8TOutlineFv
long
TOutline::CountTopics(void)
{
	RefVar list(List());
	return Length(list);
}


// ROM 0x0014dc44 TopicPtr__8TOutlineFCl
Topic*
TOutline::TopicPtr(long index)
{
	return &fTopics[index];
}


// ROM 0x0014d340 PaneIndex__8TOutlineFv
long
TOutline::PaneIndex(void)
{
	RefVar index(GetVar(RSSYMpaneindex));
	if (ISNIL(index))
		return -1;
	return RINT(index);
}


// ROM 0x0014db28 TopicInit__8TOutlineFlP5Topic
// A topic's level (its frame's level, 1 without one) and its top (the
// height of the shown topics before it); its ancestors all counted closed.
// The parent and the other flags are AddTopic's.
void
TOutline::TopicInit(long index, Topic* topic)
{
	RefVar list(List());
	RefVar frame(GetArraySlotRef(list, index));
	if (NOTNIL(frame) && FrameHasSlot(frame, RSSYMlevel))
	{
		ULong level = RINT(GetFrameSlotRef(frame, RSSYMlevel)) & 0xff;
		topic->fFlags = (topic->fFlags & 0x0fffffff) | (level << 28);
	}
	else
		topic->fFlags = (topic->fFlags & 0x0fffffff) | 0x10000000;
	ULong flags = topic->fFlags;
	flags = (flags & ~kTopicHiddenMask) | (((TopicLevel(flags) - 1) & 0xf) << kTopicHiddenShift);
	topic->fTop = fHeight;
	topic->fFlags = flags & ~0x830000;
}


// ROM 0x0014dc50 AddTopic__8TOutlineFlP5Topic
// A topic put in its place: the topic before it marked as having topics
// under it when this one is deeper; a topic at the top level shown, any
// other hidden, with the nearest shallower topic before it as its parent;
// closed.
// ROM BUG (fixed): a first topic deeper than level 1 marks the word before
// the array (the heap block's header) as having topics under it.  The host
// leaves it alone, which is also the fix, so the two machines agree here.
// ROM BUG (fixed): a topic below the top level with no shallower topic
// before it keeps whatever parent the Topic it came in held - stack
// rubbish on the MessagePad (TopicInit does not set it); nought on the
// host.  The fix takes such a topic, having no parent to be opened from,
// as one at the top level: shown, with no parent (-1) - so RevealTopic
// and AutoCollapse stop at it rather than at a stranger.
void
TOutline::AddTopic(long index, Topic* topic)
{
	ULong before = index == 0 ? 1 : TopicLevel(fTopics[index - 1].fFlags);
	Topic* t = &fTopics[index];
	*t = *topic;
	if (before < TopicLevel(t->fFlags) && index > 0)
		fTopics[index - 1].fFlags |= kTopicHasChildren;
	ULong flags = t->fFlags;
	if (TopicLevel(flags) == 1)
	{
		t->fFlags = flags | kTopicVisible;
		t->fParent = -1;
	}
	else
	{
		t->fFlags = flags & ~kTopicVisible;
		Boolean parented = false;
		for (long i = index; i >= 0; i--)
			if (TopicLevel(fTopics[i].fFlags) < TopicLevel(flags))
			{
				t->fParent = (short) i;
				parented = true;
				break;
			}
		if (!parented && RomBugFixed())
		{
			t->fFlags = flags | kTopicVisible;
			t->fParent = -1;
		}
	}
	t->fFlags &= ~kTopicExpanded;
}


// ROM 0x0014cf20 InitTopics__8TOutlineFv
// The pane index (paneIndex, when the context has one), whether the
// browser is dynamic or has a header, and a Topic for each topic of the
// list - the height of the shown ones added up as they go.
void
TOutline::InitTopics(void)
{
	RefVar index(GetVariable(fContext, RSSYMpaneindex, nil, 0));
	if (NOTNIL(index))
		fPaneIndex = RINT(index);
	RefVar browser(Browser());
	fDynamic = FrameHasSlot(browser, RSSYMdynamic);
	fHeader = FrameHasSlot(browser, RSSYMheader);
	fCount = CountTopics();
	fTopics = (Topic*) NewPtr(fCount * sizeof(Topic));
	if (fTopics == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	fHeight = 0;
	for (long i = 0; i < fCount; i++)
	{
		Topic topic = { 0, 0, 0 };		// (the ROM's is stack rubbish: see AddTopic)
		TopicInit(i, &topic);
		AddTopic(i, &topic);
		ULong flags = fTopics[i].fFlags;
		if ((flags & kTopicVisible) != 0 && TopicKind(flags) != 2)
			fHeight = fHeight + fLineHeight;
	}
}


// ROM 0x0014d6c0 RefreshTopics__8TOutlineFl
// The topics made again from the browser (of a browser with a header, the
// pane's: any pane above 1 is 1), the list scrolled to its top; with the
// first pane index, the topic the book is at shown and selected.
void
TOutline::RefreshTopics(long pane)
{
	DisposPtr((Ptr) fTopics);
	fPaneIndex = -1;
	*fBrowser = NILREF;
	*fList = NILREF;
	fPane = pane;
	if (pane > 0)
		pane = 1;
	fPane = pane;
	InitTopics();
	fSelection = -1;
	fScroll = 0;
	fField40 = 0;
	long topic;
	if (fPaneIndex == 0 && (topic = FindPageInList()) != -1)
	{
		RevealTopic(topic);
		fSelection = topic;
		ScrollToSelection();
	}
	Dirty(nil);
}


// ROM 0x0014c988 Expand__8TOutlineFlUc
// A topic opened: each topic under it one closed ancestor fewer, the ones
// that come to none shown - a line apart from the topic's own, a
// right-hand half sharing the line before it - and the shown topics after
// them moved down by the room they take.
void
TOutline::Expand(long index, Boolean draw)
{
	ScrollPos();		// (asked, and not used)
	Topic* t = &fTopics[index];
	ULong flags = t->fFlags;
	if ((flags & kTopicExpanded) != 0)
		return;
	if ((flags & kTopicHasChildren) == 0)
		return;
	short base = (short) (t->fTop + fLineHeight);
	short y = base;
	long i = index;
	Topic* q;
	for ( ; ; )
	{
		i++;
		q = t + 1;
		if (i >= fCount || TopicLevel(q->fFlags) <= TopicLevel(flags))
			break;
		ULong f = q->fFlags;
		ULong hidden = (f - 0x1000000) & kTopicHiddenMask;
		q->fFlags = (f & 0xf0bfffff) | hidden | ((hidden == 0) ? kTopicVisible : 0);
		q->fTop = y;
		if (TopicKind(f) == 2)
			q->fTop = (short) (q->fTop - fLineHeight);
		t = q;
		if (hidden == 0 && TopicKind(f) != 2)
			y = (short) (fLineHeight + y);
	}
	for ( ; i < fCount; i++, q++)
		if ((q->fFlags & kTopicVisible) != 0)
			q->fTop = (short) (q->fTop - (short) (base - y));
	fTopics[index].fFlags |= kTopicExpanded;
}


// ROM 0x0014ec3c Collapse__8TOutlineFlUc
// A topic closed: each topic under it one closed ancestor more and hidden,
// and the shown topics after them moved up by the room they took.
void
TOutline::Collapse(long index, Boolean draw)
{
	ScrollPos();		// (asked, and not used)
	Topic* t = &fTopics[index];
	ULong flags = t->fFlags;
	if ((flags & kTopicExpanded) != kTopicExpanded)
		return;
	short base = (short) (t->fTop + fLineHeight);
	short y = base;
	long i = index;
	Topic* q;
	for ( ; ; )
	{
		i++;
		q = t + 1;
		if (i >= fCount || TopicLevel(q->fFlags) <= TopicLevel(flags))
			break;
		ULong f = q->fFlags;
		if ((f & kTopicVisible) != 0)
			y = (short) (q->fTop + fLineHeight);
		q->fFlags = (f & 0xf0bfffff) | ((f + 0x1000000) & kTopicHiddenMask);
		t = q;
	}
	for ( ; i < fCount; i++, q++)
		if ((q->fFlags & kTopicVisible) != 0)
			q->fTop = (short) (q->fTop - (short) (y - base));
	fTopics[index].fFlags &= ~kTopicExpanded;
}


// ROM 0x0014e330 AutoCollapse__8TOutlineFl
// Before a topic is opened: its ancestors marked as on its way, and the
// first open, shown topic that is not closed.
void
TOutline::AutoCollapse(long index)
{
	for (long i = 0; i < fCount; i++)
		fTopics[i].fFlags &= ~kTopicOnPath;
	while (fTopics[index].fParent != -1)
	{
		index = fTopics[index].fParent;
		fTopics[index].fFlags |= kTopicOnPath;
	}
	long open = -1;
	for (long i = 0; i < fCount; i++)
	{
		ULong flags = fTopics[i].fFlags;
		if ((flags & kTopicExpanded) != 0 && (flags & kTopicOnPath) == 0 && (flags & kTopicVisible) != 0)
		{
			open = i;
			break;
		}
	}
	if (open != -1)
		Collapse(open, false);
}


// ROM 0x0014d798 RevealTopic__8TOutlineFl
// A topic shown: its parent shown and opened, all the way up.
void
TOutline::RevealTopic(long index)
{
	if ((fTopics[index].fFlags & kTopicVisible) != 0)
		return;
	RevealTopic(fTopics[index].fParent);
	Expand(fTopics[index].fParent, false);
}


// ROM 0x0014e0f4 VisibleTopic__8TOutlineFl
// How many lines the shown topics up to this one take.
long
TOutline::VisibleTopic(long index)
{
	long count = 0;
	for (long i = 0; i <= index; i++)
	{
		ULong flags = fTopics[i].fFlags;
		if ((flags & kTopicVisible) != 0 && TopicKind(flags) != 2)
			count++;
	}
	return count;
}


// ROM 0x0014dd14 TopicRect__8TOutlineFP5TopicR5TRectT2
// Where a topic is drawn: its line, indented twelve pixels a level; the
// left or right half of it for a topic of kind 1 or 2.
void
TOutline::TopicRect(Topic* topic, Rect* rect, Rect& bounds)
{
	rect->top = (short) (topic->fTop + bounds.top - ScrollPos());
	rect->left = (short) (bounds.left + TopicLevel(topic->fFlags) * 12 - 12);
	rect->bottom = (short) (fLineHeight + rect->top);
	rect->right = bounds.right;
	if (TopicKind(topic->fFlags) == 1)
		rect->right = (short) (rect->right - ((rect->right - rect->left) >> 1));
	else if (TopicKind(topic->fFlags) == 2)
		rect->left = (short) (rect->left + ((rect->right - rect->left) >> 1));
}


// ROM 0x0014e19c (unnamed)
// The line-th line (lines end at a return or a nul) of the characters
// from *start to *length: *start moved to it and *length made its length.
static void
TopicLine(const UniChar* text, long* length, long* start, long line)
{
	long i = *start;
	long n = 0;
	for ( ; i < *length; i++)
	{
		UniChar c = text[i];
		if (c == U_CONST_CHAR(0x0d) || c == 0)
		{
			if (n++ == line)
				break;
			*start = i + 1;
		}
	}
	*length = i - *start;
}


// ROM 0x0014de30 TopicText__8TOutlineFlPUsPl
// A topic's text, at most 63 characters: the topic frame's name, else its
// item's data - or, for a browser with a header, the line of the pane's
// text.
// DEVIATION: a text that is not a string leaves the ROM copying from
// wherever its registers pointed; the host copies nothing.
void
TOutline::TopicText(long index, UniChar* text, long* length)
{
	RefVar frame;
	RefVar str;
	const UniChar* chars = nil;
	long count = 0;
	long start = 0;
	if (!fHeader)
	{
		frame = TopicFrame(index);
		if (ISNIL(frame))
		{
			text[0] = 0;
			return;
		}
		if (!fHeader)
		{
			if (FrameHasSlot(frame, RSSYMname))
				str = GetFrameSlotRef(frame, RSSYMname);
			else
			{
				frame = GetFrameSlotRef(frame, RSSYMitem);
				str = GetFrameSlotRef(frame, RSSYMdata);
			}
		}
		if (EQRef(ClassOf(str), RSSYMstring))
		{
			chars = (const UniChar*) BinaryData(str);
			count = (ULong) (Length(str) - 2) >> 1;
		}
	}
	else
	{
		RefVar browser(Browser());
		RefVar texts(GetFrameSlotRef(browser, RSSYMtext));
		str = GetArraySlotRef(texts, fPane);
		if (EQRef(ClassOf(str), RSSYMstring))
		{
			chars = (const UniChar*) BinaryData(str);
			count = (ULong) (Length(str) - 2) >> 1;
		}
		TopicLine(chars, &count, &start, index);
	}
	long n = count;
	if (n > 63)
		n = 63;
	*length = n;
	if (chars != nil)
		memmove(text, chars + start, n * sizeof(UniChar));
	text[*length] = 0;
}


// ROM 0x0014c980 DrawTopicRefs__8TOutlineFlR5TRect
// The width a topic's references take at the right of its line: none.
long
TOutline::DrawTopicRefs(long index, Rect& bounds)
{
	return 0;
}


// ROM 0x0014c6ec DrawTopic__8TOutlineFlR5TRect
// A topic drawn in its line: its text in the view's font (bold when it has
// topics under it), leading blanks skipped, cut to the line's width with
// an ellipsis; the topic the book is at marked by a bar two pixels wide
// down the line's left edge.
// ROM BUG (fixed): the topic is looked at when its index is the count, one
// past the last (no caller asks for it).  The fix draws only topics there
// are.
void
TOutline::DrawTopic(long index, Rect& bounds)
{
	StyleRecord style;
	if (RomBugFixed() ? index < fCount : index <= fCount)
	{
		UniChar text[64];
		long length;
		TopicText(index, text, &length);
		text[length] = 0;
		if (length != 0)
		{
			Topic* topic = &fTopics[index];
			long width = (short) (bounds.right - bounds.left);
			if (fHeader)
				width -= DrawTopicRefs(index, bounds);
			FPoint where;
			where.x = (bounds.left + 3) << 16;
			where.y = (fAscent + bounds.top) << 16;
			CreateTextStyleRecord(RefVar(*fFont), &style);
			if ((topic->fFlags & kTopicHasChildren) != 0)
				style.fFontFace = 1;
			StyleRecord* styles = &style;
			long i = 0;
			for ( ; i < length; i++)
			{
				UniChar c = text[i];
				if (c != U_CONST_CHAR(' ') && c != U_CONST_CHAR('\t') && c != U_CONST_CHAR(0x0d))
					break;
			}
			UniChar* shown = text + i;
			length = TruncateText(shown, length - i, width - 2, &style);
			DrawTextOnce(shown, length, &styles, nil, where, nil, nil);
		}
		if (fCurrent == index)
		{
			MoveTo(bounds.left, bounds.top + 2);
			LineTo(bounds.left, bounds.bottom - 2);
			MoveTo(bounds.left + 1, bounds.top + 2);
			LineTo(bounds.left + 1, bounds.bottom - 2);
		}
	}
	if (style.fPattern != nil)
		DisposePattern(style.fPattern);
}


// ROM 0x0014d5bc RealDraw__8TOutlineFR5TRect
// The shown topics from the scroll position on, as many as the view shows;
// the topic the book is at found first (not for a browser with a header,
// nor any pane but the first); the scrollers set.
void
TOutline::RealDraw(Rect& area)
{
	Rect bounds = viewBounds;
	if (!fHeader && fPaneIndex == 0)
		fCurrent = WhereAreWe();
	else
		fCurrent = -1;
	long rows = ViewableTopics();
	for (long i = 0; i < fCount; i++)
	{
		Topic* topic = &fTopics[i];
		if (ScrollPos() <= topic->fTop && (topic->fFlags & kTopicVisible) != 0)
		{
			Rect r;
			TopicRect(topic, &r, bounds);
			DrawTopic(i, r);
			if (--rows < 1)
				break;
		}
	}
	SetScrollers();
}


// ROM 0x0014cdf8 FindTopic__8TOutlineF6TPoint
// The shown topic whose line a point (on the screen) is in, or above:
// the first whose bottom is below it; a left-hand half only when the point
// is left of the middle.  ==> -1 above the list, or below it.
// ROM BUG (fixed): the middle is half the view's right edge on the screen,
// while the point has been made relative to the view's left.  The fix
// takes the middle of the topic's line relative to the view's left as the
// point is: its indent and half the width beyond it (what the ROM's
// ((right - indent) + indent) >> 1, an indent added and taken away again,
// evidently meant).
long
TOutline::FindTopic(Point pt)
{
	pt.v = (short) (pt.v + ScrollPos());
	Rect bounds = viewBounds;
	pt.v = (short) (pt.v - bounds.top);
	pt.h = (short) (pt.h - bounds.left);
	if (pt.v < 0)
		return -1;
	for (long i = 0; i < fCount; i++)
	{
		ULong flags = fTopics[i].fFlags;
		if ((flags & kTopicVisible) != 0 && fTopics[i].fTop + fLineHeight > pt.v)
		{
			if (TopicKind(flags) != 1)
				return i;
			short indent = (short) (TopicLevel(flags) * 12 - 12);
			if (RomBugFixed())
			{
				if (pt.h <= indent + (((bounds.right - bounds.left) - indent) >> 1))
					return i;
			}
			else if (pt.h <= (((bounds.right - indent) + indent) >> 1))
				return i;
		}
	}
	return -1;
}


// ROM 0x0014c4d8 DoClick__8TOutlineFR5TRect
// A tap on the list: the topic under it inverted while the click sound
// plays and it is acted on (ClickCommand), and then, when it has topics
// under it, closed - or opened (any other open topic not on its way
// closed first), the list scrolled so that as much of what it opened is
// shown as will fit.
void
TOutline::DoClick(Rect& bounds)
{
	long index = FindTopic(MidPoint(bounds));
	if (index == -1)
		return;
	fSelection = index;
	Rect viewRect = viewBounds;
	Rect r;
	TopicRect(&fTopics[index], &r, viewRect);
	SoundEffect(RSSYMclicksound);
	InvertRect(&r);
	ULong flags = fTopics[index].fFlags;
	ClickCommand(-1);
	if ((flags & kTopicHasChildren) != 0)
	{
		if ((flags & kTopicExpanded) == kTopicExpanded)
			Collapse(index, false);
		else
		{
			AutoCollapse(index);
			ScrollToSelection();
			Expand(index, false);
			short top = fTopics[index].fTop;
			long lineHeight = fLineHeight;
			long span = (short) (lineHeight * ViewableTopics());
			if (span + fScroll <= lineHeight + top)
			{
				long last = fCount - 1;
				while ((fTopics[last].fFlags & kTopicVisible) == 0)
					last--;
				long most = (ViewableTopics() / 2) * lineHeight + fScroll;
				long least = (fTopics[last].fTop - span) + fLineHeight;
				if (least <= most)
					most = least;
				fScroll = (short) most;
			}
		}
	}
	InvertRect(&r);
	SetScrollers();
	Dirty(nil);
}


// ROM 0x0014d400 RealDoCommand__8TOutlineFRC6RefVar
// A tap (its ink taken off, DoClick on its bounds) and the scroll arrows:
// up and down a screenful less a line, not above the top nor so far down
// that the last topic leaves the bottom line.
Boolean
TOutline::RealDoCommand(RefArg cmd)
{
	long id = CommandID(cmd);
	if (id == aeClick)
	{
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		unit->Stroke()->InkOff(true);
		Rect bounds;
		unit->Bounds(&bounds);
		DoClick(bounds);
		CommandSetResult(cmd, 1);
		return true;
	}
	long scroll;
	if (id == aeScrollUp)
	{
		if (fScroll < 1)
			return true;
		long lineHeight = fLineHeight;
		scroll = fScroll - (ViewableTopics() - 1) * lineHeight;
		if (scroll < 0)
			scroll = 0;
	}
	else if (id == aeScrollDown)
	{
		long last = fCount - 1;
		while ((fTopics[last].fFlags & kTopicVisible) == 0)
			last--;
		long rows = ViewableTopics();
		long lineHeight = fLineHeight;
		long span = (short) (lineHeight * rows);
		if (fTopics[last].fTop < span + fScroll)
			return true;
		scroll = (ViewableTopics() - 1) * lineHeight + fScroll;
		long least = (fTopics[last].fTop - span) + fLineHeight;
		if (least <= scroll)
			scroll = least;
	}
	else
		return TView::RealDoCommand(cmd);
	fScroll = (short) scroll;
	Dirty(nil);
	return true;
}


// ROM 0x0014d880 ScrollToSelection__8TOutlineFv
// The list scrolled, when the topic tapped is out of sight, to put it in
// the middle.
// ROM BUG (fixed): scrolled down, the position is not kept from going
// below nought, as it is scrolled up.  The fix keeps it from going below
// nought both ways.
void
TOutline::ScrollToSelection(void)
{
	short top = fTopics[fSelection].fTop;
	long rows = ViewableTopics();
	long scroll;
	if (top < fScroll)
	{
		fScroll = (short) (top - fLineHeight * (ViewableTopics() / 2));
		scroll = fScroll;
		if (scroll < 0)
			scroll = 0;
	}
	else
	{
		if (top < fScroll + (short) (fLineHeight * rows))
			return;
		scroll = top - fLineHeight * (ViewableTopics() / 2);
		if (RomBugFixed() && scroll < 0)
			scroll = 0;
	}
	fScroll = (short) scroll;
}


// ROM 0x0014d964 SetScrollers__8TOutlineFv
// The view's SetScroller told [shown, shown, viewable, first line shown],
// when it has one.
void
TOutline::SetScrollers(void)
{
	long shown = 0;
	for (long i = 0; i < fCount; i++)
		if ((fTopics[i].fFlags & 0x7fffff) >> 22 == 1)
			shown++;
	if (ISNIL(GetProto(RSSYMsetscroller)))
		return;
	RefVar args(MakeArray(4));
	SetArraySlotRef(args, 0, MAKEINT(shown));
	SetArraySlotRef(args, 1, MAKEINT(shown));
	SetArraySlotRef(args, 2, MAKEINT(ViewableTopics()));
	SetArraySlotRef(args, 3, MAKEINT(fScroll / fLineHeight));
	DoMessage(fContext, RSSYMsetscroller, args);
}


// ROM 0x0014d7f8 ScrollToCurrent__8TOutlineFv
// With the first pane: the topic the book is at shown, selected and
// scrolled to (any other open topic closed).
void
TOutline::ScrollToCurrent(void)
{
	if (fPaneIndex != 0)
		return;
	long topic = FindPageInList();
	if (topic != -1)
	{
		AutoCollapse(topic);
		RevealTopic(topic);
		fSelection = topic;
		ScrollToSelection();
	}
	Dirty(nil);
}


// ROM 0x0014e13c WhereAreWe__8TOutlineFv
// The shown topic the book is at, or the shown topic above it; -1 for
// any pane but the first.
long
TOutline::WhereAreWe(void)
{
	if (fPaneIndex != 0)
		return -1;
	long topic = FindPageInList();
	if (topic == -1)
		return -1;
	for ( ; ; )
	{
		if ((fTopics[topic].fFlags & kTopicVisible) != 0)
			return topic;
		if (topic == 0)
			break;
		topic--;
	}
	return 0;
}


// ROM 0x0014cb0c FindPageInList__8TOutlineFv
// The topic the reader's page is under: the last topic, in list order,
// whose page is not after the current page - a page number from the
// rendering's contents for the pane, else the topic's pageNumber, else the
// page its item is on.  ==> -1 for a dynamic browser, a rendering with no
// contents or none for the pane.
long
TOutline::FindPageInList(void)
{
	RefVar copperfield(GetVar(RSSYMcopperfield));
	if (fDynamic)
		return -1;
	TLibrarian* librarian = TLibrarian::gLibrarian;
	long current = librarian->CurrentPage(copperfield);
	RefVar contents;
	{
		RefVar rendering(librarian->Rendering(copperfield));
		contents = GetFrameSlotRef(rendering, RSSYMcontents);
	}
	if (ISNIL(contents))
		return -1;
	RefVar paneContents(GetArraySlotRef(contents, fPaneIndex));
	if (ISNIL(paneContents))
		return -1;
	RefVar list(GetFrameSlotRef(*fBrowser, RSSYMlist));
	RefVar entry;
	RefVar number;
	RefVar item;
	long found = -1;
	long lastPage = 0;
	ULong lastLevel = 0;
	for (long slot = 0; slot < fCount; )
	{
		long page;
		if (fPaneIndex < Length(contents))
			page = RINT(GetArraySlotRef(paneContents, slot));
		else
		{
			entry = GetArraySlotRef(list, slot);
			number = GetFrameSlotRef(entry, RSSYMpagenumber);
			if (NOTNIL(number))
				page = RINT(number);
			else
			{
				item = GetFrameSlotRef(entry, RSSYMitem);
				page = librarian->FindPageByContent(copperfield, item, 0, nil, RefVar());
			}
		}
		ULong level = TopicLevel(fTopics[slot].fFlags);
		if (current == page && (long) level <= (long) lastLevel && page == lastPage)
			break;
		if (page > current)
			break;
		found = slot;
		lastPage = page;
		lastLevel = level;
		slot++;
	}
	return found;
}


// ROM 0x0014d15c PageNumber__8TOutlineFlRC6RefVarT1
// The page a topic turns to: from the rendering's contents for the pane
// (not for a dynamic browser), else the topic's pageNumber, else the page
// its item is on.
long
TOutline::PageNumber(long index, RefArg item, long offset)
{
	RefVar copperfield(GetVar(RSSYMcopperfield));
	if (!fDynamic)
	{
		RefVar rendering(TLibrarian::gLibrarian->Rendering(copperfield));
		RefVar contents(GetFrameSlotRef(rendering, RSSYMcontents));
		if (NOTNIL(contents) && fPaneIndex < Length(contents))
		{
			RefVar paneContents(GetArraySlotRef(contents, fPaneIndex));
			if (NOTNIL(paneContents))
				return RINT(GetArraySlotRef(paneContents, index));
		}
	}
	RefVar frame(TopicFrame(index));
	if (FrameHasSlot(frame, RSSYMpagenumber))
		return RINT(GetFrameSlotRef(frame, RSSYMpagenumber));
	return TLibrarian::gLibrarian->FindPageByContent(copperfield, item, 0, nil, RefVar());
}


// ROM 0x0014e854 ClickCommand__8TOutlineFl
// Copperfield's outline acting on a tap: the view's outlineClickScript
// first ([selection, index]; an answer other than nil is the end of it);
// then, unless the topic is open, the book turned to its page - the page
// its entry (or item, or the index-th of its items) is on, else the next
// topic's - with the item hilited when the browser asks for it, and the
// browser closed when the view has autoClose.
void
TOutline::ClickCommand(long index)
{
	RefVar frame;
	RefVar item;
	RefVar result;
	RefVar copperfield(GetVar(RSSYMcopperfield));
	if (NOTNIL(GetProto(RSSYMoutlineclickscript)))
	{
		RefVar args(MakeArray(2));
		SetArraySlotRef(args, 0, MAKEINT(fSelection));
		SetArraySlotRef(args, 1, MAKEINT(index));
		Boolean ran;
		result = RunScript(RSSYMoutlineclickscript, args, false, &ran);
		if (NOTNIL(result))
			return;
	}
	if ((fTopics[fSelection].fFlags & kTopicExpanded) != 0)
		return;
	frame = TopicFrame(fSelection);
	if (ISNIL(frame))
		return;
	if (index == -1)
		item = GetFrameSlotRef(frame, FrameHasSlot(frame, RSSYMentry) ? RSSYMentry : RSSYMitem);
	else
		item = GetArraySlotRef(RefVar(GetFrameSlotRef(frame, RSSYMitem)), index);
	long page = PageNumber(fSelection, item, -1);
	if (page == 0)
	{
		if (fSelection + 1 >= fCount)
			return;
		frame = TopicFrame(fSelection + 1);
		if (index == -1)
			item = GetFrameSlotRef(frame, RSSYMitem);
		else
			item = GetArraySlotRef(RefVar(GetFrameSlotRef(frame, RSSYMitem)), index);
		page = PageNumber(fSelection + 1, item, -1);
	}
	if (page == 0)
		return;
	RefVar cuPage(GetFrameSlotRef(copperfield, RSSYMcupage));
	PageTurnToSpread(cuPage, page);
	if (FrameHasSlot(*fBrowser, RSSYMhilite))
		HiliteBlock(cuPage, item, RefVar(MAKEINT(0)), RefVar(MAKEINT(15000)));
	RefVar autoClose(GetVariable(fContext, RSSYMautoclose, nil, 0));
	if (NOTNIL(autoClose))
		DoMessage(fContext, RSSYMbrowserclose, RefVar());
}


/*------------------------------------------------------------------------------
	T H e l p O u t l i n e
------------------------------------------------------------------------------*/

// ROM 0x0014e2f4 ClassID__12THelpOutlineCFv
long
THelpOutline::ClassID(void) const
{
	return clHelpOutlineView;
}


// ROM 0x0014e2fc DerivedFrom__12THelpOutlineCFl
// ROM BUG (fixed): a help outline does not say it derives from TOutline
// (it asks TView).  The fix asks TOutline.
Boolean
THelpOutline::DerivedFrom(long id) const
{
	if (RomBugFixed())
		return id == clHelpOutlineView || TOutline::DerivedFrom(id);
	return id == clHelpOutlineView || TView::DerivedFrom(id);
}


// ROM 0x0014e41c Browser__12THelpOutlineFv
// The help book's first browser (bookRef.browsers[0]), not kept.
Ref
THelpOutline::Browser(void)
{
	RefVar book(GetVar(RSSYMbookref));
	RefVar browsers(GetFrameSlotRef(book, RSSYMbrowsers));
	return GetArraySlotRef(browsers, 0);
}


// ROM 0x0014e7cc ViewableTopics__12THelpOutlineFv
long
THelpOutline::ViewableTopics(void)
{
	return 15;
}


// ROM 0x0014e7d4 WhereAreWe__12THelpOutlineFv
long
THelpOutline::WhereAreWe(void)
{
	return -1;
}


// ROM 0x0014e494 ClickCommand__12THelpOutlineFl
// A tap on a topic with nothing under it: the help book's content area
// (cuPage) turned to the page its item is on, and the outline hidden
// (aeHide sent to itself).
void
THelpOutline::ClickCommand(long index)
{
	RefVar cuPage;
	RefVar frame;
	RefVar item;
	RefVar book;
	Topic* topic = TopicPtr(fSelection);
	if ((topic->fFlags & kTopicHasChildren) != 0)
		return;
	book = GetVar(RSSYMbookref);
	frame = TopicFrame(fSelection);
	item = GetFrameSlotRef(frame, RSSYMitem);
	long page = TLibrarian::gLibrarian->FindPageByContent(fContext, item, 0, nil, book);
	if (page != 0)
	{
		cuPage = GetVar(RSSYMcupage);
		PageTurnTo(cuPage, page, true);
		gApplication->DispatchCommand(RefVar(MakeCommand(aeHide, this, 0x08000000)));
	}
}


// ROM 0x0014e604 TopicByName__12THelpOutlineFRC6RefVar
// The topic whose item is the help book's content item of that name:
// selected, shown (any other open topic closed) and opened.  ==> true, or
// nil when there is none.
Ref
THelpOutline::TopicByName(RefArg name)
{
	RefVar found;
	RefVar book;
	RefVar frame;
	book = GetVar(RSSYMbookref);
	found = TLibrarian::gLibrarian->FindContentByValue(fContext, RSSYMname, name, book);
	if (Length(found) != 0)
	{
		for (long i = 0; i < fCount; i++)
		{
			frame = TopicFrame(i);
			RefVar item(GetFrameSlotRef(frame, RSSYMitem));
			if (EQRef(GetArraySlotRef(found, 0), item))
			{
				AutoCollapse(i);
				fSelection = i;
				RevealTopic(i);
				Expand(i, false);
				Dirty(nil);
				return TRUEREF;
			}
		}
	}
	return NILREF;
}


/*------------------------------------------------------------------------------
	T h e   o u t l i n e ' s   f u n c t i o n s
------------------------------------------------------------------------------*/

// ROM 0x0014e220 FTopicByName
// outline:TopicByName(name)
Ref
FTopicByName(RefArg rcvr, RefArg name)
{
	TOutline* view = (TOutline*) GetView(rcvr, RefVar());
	if (view != nil)
		return view->TopicByName(name);
	return NILREF;
}


// ROM 0x0014e284 RefreshTopics
// RefreshTopics(outline, pane): the outline's topics made again.
Ref
RefreshTopics(RefArg rcvr, RefArg outline, RefArg pane)
{
	TOutline* view = (TOutline*) GetView(rcvr, outline);
	view->RefreshTopics(RINT(pane));
	return TRUEREF;
}


// ROM 0x0014e2d0 ScrollToCurrent
// ScrollToCurrent(outline): the topic the book is at shown.
Ref
ScrollToCurrent(RefArg rcvr, RefArg outline)
{
	TOutline* view = (TOutline*) GetView(rcvr, outline);
	view->ScrollToCurrent();
	return TRUEREF;
}


void
RegisterOutlineNatives(void)
{
	RegisterNativeFunction("FTopicByName", (void*) FTopicByName, 1);
	RegisterNativeFunction("RefreshTopics", (void*) RefreshTopics, 2);
	RegisterNativeFunction("ScrollToCurrent", (void*) ScrollToCurrent, 1);
}
