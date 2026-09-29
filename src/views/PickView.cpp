/*
	File:		views/PickView.cpp

	Contains:	TPickView: the picker's items laid out, drawn and picked.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PickView.h"
#include "RootView.h"
#include "Recognizer.h"	// gInhibitPopup
#include "Commands.h"
#include "UnitPublic.h"
#include "NewtonTime.h"
#include "NativeFunctions.h"
#include "Keyboard.h"
#include "Application.h"
#include "Rects.h"
#include "Ports.h"
#include "Draw.h"
#include "Shapes.h"
#include "Text.h"
#include "Pictures.h"
#include "RichString.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "Locale.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include <string.h>

const long kSeparatorHeight = 6;			// a 'pickSeparator row
const long kMaxInkHeight = 28;				// an ink item is scaled to this
const long kScrollerWidth = 19;				// pickRightMargin grows by this for the scrollers
const long kFlipEffect = 0x182000;			// viewEffect for a picker opening upward
const UniChar kEllipsisChar = 0x2026;
const UniChar kSpace = 0x20;

// ROM 0x00197564 Encloses__5TRectCFRC5TRect - the rectangle holds the other
static Boolean
RectEncloses(const Rect& outer, const Rect& inner)
{
	return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right && inner.bottom <= outer.bottom;
}


// ROM 0x001838c4 GetAppAreaBounds__FP5TRect
// The application area of vars.displayParams (appAreaGlobalLeft/Top,
// appAreaWidth/Height).
void
GetAppAreaBounds(Rect* bounds)
{
	RefVar params(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdisplayparams));
	bounds->left = (short) RINT(GetProtoVariable(params, RSSYMappareagloballeft, nil));
	bounds->top = (short) RINT(GetProtoVariable(params, RSSYMappareaglobaltop, nil));
	bounds->right = (short) (bounds->left + RINT(GetProtoVariable(params, RSSYMappareawidth, nil)));
	bounds->bottom = (short) (bounds->top + RINT(GetProtoVariable(params, RSSYMappareaheight, nil)));
}


// ROM 0x00184e54 AdjustPopupInRect__FR5TRectlT2RC5TRects
// A popup of the width and height placed against its bounds within the
// rectangle: to the right of the bounds, or - when that does not fit -
// to the left, or when neither does over them from the top (or the
// bottom, when it would run off); below the bounds' top, or above the
// bounds when that runs off the bottom and the bounds lie in the lower
// half.  frame is the view's frame width, kept clear.  ==> whether it
// went above.
Boolean
AdjustPopupInRect(Rect& bounds, long width, long height, const Rect& within, short frame)
{
	if (bounds.right + width > within.right)
	{
		if (bounds.left - width < within.left)
		{
			long room = (within.bottom - within.top) - (bounds.bottom - bounds.top);
			if (room < height)
				height = room;
			if (bounds.bottom + height > within.bottom)
			{
				bounds.bottom = bounds.top;
				bounds.top = (short) (bounds.top - height);
			}
			else
			{
				bounds.top = bounds.bottom;
				bounds.bottom = (short) (bounds.bottom + height);
			}
			bounds.left = (short) (bounds.left + frame);
			bounds.right = (short) (bounds.left + width);
			return false;
		}
		bounds.right = bounds.left;
		bounds.left = (short) (bounds.left - width);
	}
	else
	{
		bounds.left = bounds.right;
		bounds.right = (short) (bounds.left + width);
	}
	Boolean above = bounds.top + height > within.bottom && bounds.top - within.top > (within.bottom - within.top) / 2;
	if (!above)
	{
		bounds.top = (short) (bounds.top + frame);
		bounds.bottom = (short) (bounds.top + height);
	}
	else
	{
		bounds.bottom = (short) (bounds.bottom - frame);
		bounds.top = (short) (bounds.bottom - height);
	}
	return above;
}


/*------------------------------------------------------------------------------
	T P i c k V i e w
------------------------------------------------------------------------------*/

// ROM 0x00183824 ClassID__9TPickViewCFv
long
TPickView::ClassID(void) const
{
	return clPickView;
}


// ROM 0x00184c40 DerivedFrom__9TPickViewCFl
Boolean
TPickView::DerivedFrom(long id) const
{
	return id == clPickView || TView::DerivedFrom(id);
}


// ROM 0x00186590 Constructor__9TPickViewFRC6RefVarP5TView
// No caches yet, nothing picked; then as TView.
void
TPickView::Constructor(RefArg context, TView* parent)
{
	fItemBottoms = nil;
	fItemFlags = nil;
	fGrids = nil;
	fPicked.fItem = -1;
	fPicking = false;
	fTypeSelect = NILREF;
	fLastKeyTime = 0;
	TView::Constructor(context, parent);
}


// the item caches disposed
static void
DisposeCaches(TPickView* view)
{
	if (view->fItemBottoms != nil)
	{
		DisposPtr((Ptr) view->fItemBottoms);
		view->fItemBottoms = nil;
	}
	if (view->fItemFlags != nil)
	{
		DisposPtr((Ptr) view->fItemFlags);
		view->fItemFlags = nil;
	}
	if (view->fGrids != nil)
	{
		for (long i = 0; i < view->fItemCount; i++)
			if (view->fGrids[i] != nil)
				delete view->fGrids[i];
		DisposPtr((Ptr) view->fGrids);
		view->fGrids = nil;
	}
}


// ROM 0x00187848 __dt__9TPickViewFv
TPickView::~TPickView()
{
	DisposeCaches(this);
	DisposeStyleRecord(&fStyle);
}


// ROM 0x00183660 GetGridInfo__9TPickViewFRC6RefVarP5TRect
// A picture item with a width slot is a grid of width x height cells,
// cellFrame (1) apart, outerFrame (2) in from its bounds: the cells'
// size from the picture's.  Nil for any other item.
PickGridInfo*
TPickView::GetGridInfo(RefArg item, const Rect& bounds)
{
	if (!IsFrame(item))
		return nil;
	RefVar width(GetFrameSlotRef(item, RSSYMwidth));
	if (ISNIL(width))
		return nil;
	PickGridInfo* grid = new PickGridInfo;
	if (grid == nil)
		OutOfMemory();
	long columns = RINT(width);
	long rows = RINT(GetFrameSlotRef(item, RSSYMheight));
	RefVar value(GetFrameSlotRef(item, RSSYMcellframe));
	grid->fCellFrame = ISNIL(value) ? 1 : RINT(value);
	value = GetFrameSlotRef(item, RSSYMouterframe);
	grid->fOuterFrame = ISNIL(value) ? 2 : RINT(value);
	grid->fRows = rows;
	grid->fColumns = columns;
	grid->fCellWidth = ((bounds.right - bounds.left) - 2 * grid->fOuterFrame + grid->fCellFrame) / columns - grid->fCellFrame;
	grid->fCellHeight = ((bounds.bottom - bounds.top) - 2 * grid->fOuterFrame + grid->fCellFrame) / rows - grid->fCellFrame;
	return grid;
}


// ROM 0x00184d08 GetDisplayIcon__9TPickViewFRC6RefVar
// A frame item's icon slot.
Ref
TPickView::GetDisplayIcon(RefArg item)
{
	if (!IsFrame(item))
		return NILREF;
	return GetFrameSlotRef(item, RSSYMicon);
}


// ROM 0x00184d4c GetDisplayIndent__9TPickViewFRC6RefVar
// A frame item's indent, -1 for none: the text's offset after the
// margin (and the icon's width otherwise).
long
TPickView::GetDisplayIndent(RefArg item)
{
	if (IsFrame(item))
	{
		RefVar indent(GetFrameSlotRef(item, RSSYMindent));
		if (NOTNIL(indent))
			return RINT(indent);
	}
	return -1;
}


// ROM 0x00184dd0 GetDisplayFixedHeight__9TPickViewFRC6RefVar
// A frame item's fixedHeight, -1 for none: the height of it and the
// items after it (a separator keeps its own).
long
TPickView::GetDisplayFixedHeight(RefArg item)
{
	if (IsFrame(item))
	{
		RefVar height(GetFrameSlotRef(item, RSSYMfixedheight));
		if (NOTNIL(height))
			return RINT(height);
	}
	return -1;
}


// ROM 0x00187ea8 GetDisplayItem__9TPickViewFlPUcPUs
// What an item shows: a string or symbol itself; a frame's item slot
// (else its key command's name, else the empty string); with whether it
// can be picked (a separator cannot; a frame's pickable slot) and its
// mark character (a frame's mark).
Ref
TPickView::GetDisplayItem(long index, Boolean* pickable, UniChar* mark)
{
	RefVar item(GetArraySlotRef(fPickItems, index));
	RefVar display(item);
	if (pickable != nil)
		*pickable = true;
	if (mark != nil)
		*mark = 0;
	if (!IsFrame(item))
	{
		if (pickable != nil && IsSymbol(item) && (EQRef(item, RSSYMpickseparator) || EQRef(item, RSSYMpicksolidseparator)))
			*pickable = false;
		return display;
	}
	RefVar text(GetFrameSlotRef(item, RSSYMitem));
	if (NOTNIL(text))
		display = text;
	else if (NOTNIL(fKeyCommands))
	{
		RefVar command(GetArraySlotRef(fKeyCommands, index));
		if (NOTNIL(command))
			display = GetFrameSlotRef(command, RSSYMname);
		if (ISNIL(display))
			display = MakeString((char*) "");
	}
	if (mark != nil)
	{
		RefVar markChar(GetFrameSlotRef(item, RSSYMmark));
		if (NOTNIL(markChar))
			*mark = RCHAR(markChar);
	}
	if (pickable != nil && FrameHasSlotRef(item, RSSYMpickable))
		*pickable = NOTNIL(GetFrameSlotRef(item, RSSYMpickable));
	return display;
}


// ROM 0x001880e8 GetItemNoText__9TPickViewFl
// A pickable item's text (nil for a picture, a separator).
Ref
TPickView::GetItemNoText(long index)
{
	Boolean pickable;
	RefVar display(GetDisplayItem(index, &pickable, nil));
	if (!pickable || !IsString(display))
		return NILREF;
	return display;
}


// ROM 0x00187e08 GetItemFlags__9TPickViewFP9PickStuffPUcPUs
void
TPickView::GetItemFlags(PickStuff* item, Boolean* pickable, UniChar* mark)
{
	ULong flags = fItemFlags[item->fItem];
	*mark = (UniChar) (flags & 0xffff);
	*pickable = ((flags >> 16) & 1) != 0;
}


// ROM 0x00187e34 IsItemNoPickable__9TPickViewFl
Boolean
TPickView::IsItemNoPickable(long index)
{
	return ((fItemFlags[index] >> 16) & 1) != 0;
}


// ROM 0x00187e4c SetItemFlags__9TPickViewFP9PickStuffUcUs
void
TPickView::SetItemFlags(PickStuff* item, Boolean pickable, UniChar mark)
{
	fItemFlags[item->fItem] = (ULong) mark + ((pickable & 1) << 16);
}


// ROM 0x00187e70 GetItemLength__9TPickViewFl
long
TPickView::GetItemLength(long index)
{
	return (long) (SLong) fItemFlags[index] >> 20;
}


// ROM 0x00187e84 SetItemLength__9TPickViewFP9PickStuffl
void
TPickView::SetItemLength(PickStuff* item, long length)
{
	fItemFlags[item->fItem] = (fItemFlags[item->fItem] & 0xfffff) | ((ULong) length << 20);
}


// ROM 0x00185320 SetupForm__9TPickViewFv
// The items laid out and the view placed.  Each item's height and width
// are found: a string's the text height and its measured width (cut
// with an ellipsis to pickMaxWidth: the length kept negative to say so),
// plus its icon's width (or the indent) and height; a separator 6 high;
// a bitmap or picture its bounds plus the margins, with its grid info;
// ink its bounds scaled to 28 high (NOT YET); a fixedHeight applying to
// what follows.  The bottoms accumulate; the widest item, the margins
// (the marks' column when any item has a mark) and the right margin make
// the width.  The view is then placed from the template's bounds - by
// AdjustPopupInRect within the view named in the bounds' info, or below
// (above when it would run off the bottom from the lower half) and to
// the right of the bounds within the application area, its frame kept
// clear - shifted into the area, cut to it (a list taller than the area
// gets its scrollers shown and the right margin widened), and its
// viewBounds written, relative to the application area's origin; a
// picker opening upward gets the viewEffect for it.
void
TPickView::SetupForm(void)
{
	RefVar context(fContext);
	DisposeCaches(this);
	TView::SetupForm();
	RefVar timeout(GetPreference(RSSYMtypeselecttimeout));
	fTypeSelectTimeout = ISNIL(timeout) ? 0 : RINT(timeout);		// (a host without the preference: 0; the ROM's userConfiguration has it)
	fPickItems = GetProtoVariable(context, RSSYMpickitems, nil);
	fItemCount = Length(fPickItems);
	fTextItemHeight = RINT(GetProtoVariable(context, RSSYMpicktextitemheight, nil));
	RefVar font(GetVariable(context, RSSYMviewfont, nil, 0));
	CreateTextStyleRecord(font, &fStyle);
	GetStyleFontInfo(&fStyle, &fFontInfo);
	fAutoClose = NOTNIL(GetProtoVariable(context, RSSYMpickautoclose, nil));
	Boolean markable = NOTNIL(GetProtoVariable(context, RSSYMpickitemsmarkable, nil));
	long leftMargin = RINT(GetProtoVariable(context, RSSYMpickleftmargin, nil));
	long markWidth = RINT(GetProtoVariable(context, RSSYMpickmarkwidth, nil));
	fRightMargin = RINT(GetProtoVariable(context, RSSYMpickrightmargin, nil));
	fTopMargin = RINT(GetProtoVariable(context, RSSYMpicktopmargin, nil));
	fBottomMargin = RINT(GetProtoVariable(context, RSSYMpickbottommargin, nil));
	fItemBottoms = (short*) NewPtrClear(fItemCount * sizeof(short) + sizeof(short));
	fItemFlags = (ULong*) NewPtrClear(fItemCount * sizeof(ULong) + sizeof(ULong));
	fGrids = (PickGridInfo**) NewPtrClear(fItemCount * sizeof(PickGridInfo*) + sizeof(PickGridInfo*));
	if (fItemBottoms == nil || fItemFlags == nil || fGrids == nil)
	{
		DisposeCaches(this);
		OutOfMemory();
	}
	fKeyCommands = NILREF;					// NOT YET RECONSTRUCTED: GetKeyCommandInfo (no command keyboard)
	fKeyCommandWidth = 0;
	long maxWidth = RINT(GetProto(RSSYMpickmaxwidth)) - (fKeyCommandWidth >> 16);
	fPicked.fItem = -1;
	long indent = -1;
	long fixedHeight = -1;
	long widest = 0;
	long bottom = 0;
	for (long i = 0; i < fItemCount; i++)
	{
		PickStuff stuff;
		stuff.fItem = i;
		RefVar item(GetArraySlotRef(fPickItems, i));
		long itemIndent = GetDisplayIndent(item);
		if (itemIndent >= 0)
			indent = itemIndent;
		long itemFixedHeight = GetDisplayFixedHeight(item);
		if (itemFixedHeight >= 0)
			fixedHeight = itemFixedHeight;
		Boolean pickable;
		UniChar mark;
		RefVar display(GetDisplayItem(i, &pickable, &mark));
		if (mark != 0)
			fHasMarks = true;
		SetItemFlags(&stuff, pickable, mark);
		long width = 0;
		long height = 0;
		PickGridInfo* grid = nil;
		if (IsString(display))
		{
			TRichString rich(display);
			long length = rich.Length();
			TextBoundsInfo bounds;
			FPoint origin;
			origin.x = 0;
			origin.y = 0;
			MeasureRichString(rich, 0, length, &fStyle, origin, nil, &bounds);
			width = (short) ((bounds.fWidth + 0x8000) >> 16);
			long room = maxWidth;
			if (width > room)
			{
				RefVar cut(Clone(display));
				StyledStrTruncate(cut, room, RefVar(GetVar(RSSYMviewfont)));
				TRichString cutRich(cut);
				length = -cutRich.Length();
				width = room;
			}
			SetItemLength(&stuff, length);
			height = fTextItemHeight;
			RefVar icon(GetDisplayIcon(item));
			if (ISNIL(icon))
			{
				if (indent >= 0)
					width += indent;
			}
			else
			{
				Rect iconBounds;
				if (!FromObject(RefVar(GetFrameSlotRef(icon, RSSYMbounds)), iconBounds))
					ThrowMsg((char*) "bad pictBounds frame for icon");
				width += indent >= 0 ? indent : (iconBounds.right - iconBounds.left) + 2;
				long iconHeight = (iconBounds.bottom - iconBounds.top) + fTopMargin + fBottomMargin;
				if (iconHeight > height)
					height = iconHeight;
			}
		}
		else if (IsSymbol(display))
		{
			if (!EQRef(display, RSSYMpickseparator) && !EQRef(display, RSSYMpicksolidseparator))
				ThrowMsg((char*) "unsupported symbol");
			width = 0;
			height = kSeparatorHeight;
		}
		else if (IsFrame(display))
		{
			if (FrameHasSlotRef(display, RSSYMbits) || FrameHasSlotRef(display, RSSYMcolordata) || FrameHasSlotRef(display, RSSYMpicture))
			{
				Rect pictBounds;
				if (!FromObject(RefVar(GetFrameSlotRef(display, RSSYMbounds)), pictBounds))
					ThrowMsg((char*) "bad pictBounds frame");
				width = pictBounds.right - pictBounds.left;
				height = (pictBounds.bottom - pictBounds.top) + fTopMargin + fBottomMargin;
				grid = GetGridInfo(display, pictBounds);
			}
			else if (FrameHasSlotRef(display, RSSYMstrokelist))
			{
				Rect inkBounds;
				if (!FromObject(RefVar(GetFrameSlotRef(display, RSSYMbounds)), inkBounds))
					ThrowMsg((char*) "bad strokeBounds frame");
				height = inkBounds.bottom - inkBounds.top;
				width = inkBounds.right - inkBounds.left;
				if (height > kMaxInkHeight)
				{
					width = (width * kMaxInkHeight) / height;
					height = kMaxInkHeight;
				}
				height += fTopMargin + fBottomMargin;
			}
		}
		if (fixedHeight > 0 && !IsSymbol(display))
			height = fixedHeight;
		bottom += height;
		fItemBottoms[i] = (short) bottom;
		fGrids[i] = grid;
		if (width > widest)
			widest = width;
	}
	if (fHasMarks)
	{
		if (!markable)
			SetFrameSlot(context, RSSYMpickitemsmarkable, RefVar(TRUEREF));
		fMarkLeft = leftMargin;
		fTextLeft = leftMargin + markWidth;
	}
	else
	{
		fTextLeft = leftMargin;
		fMarkLeft = leftMargin;
	}
	// the frame the viewFormat gives: its pen and inset
	long frame = 0;
	RefVar format(GetProtoVariable(context, RSSYMviewformat, nil));
	if (NOTNIL(format))
	{
		long value = RINT(format);
		frame = ((value & vfFrameMask) != 0 ? (value & vfPenMask) >> vfPenShift : 0) + ((value & vfInsetMask) >> vfInsetShift);
	}
	RefVar boundsFrame(GetProtoVariable(context, RSSYMbounds, nil));
	Rect bounds;
	if (!FromObject(boundsFrame, bounds))
		ThrowMsg((char*) "bad bounds frame");
	Rect screen;
	SetRect(&screen, 0, 0, (short) gRootView->ScreenWidth(), (short) gRootView->ScreenHeight());
	Rect appArea;
	GetAppAreaBounds(&appArea);
	Point origin = MakePoint(bounds.left, bounds.top);
	Rect within = PtInRect(origin, &appArea) ? appArea : screen;
	Rect inner = within;
	InsetRect(&inner, frame, frame);
	RefVar info(GetFrameSlotRef(boundsFrame, RSSYMinfo));
	long height = bottom;
	if (height > inner.bottom - inner.top)
	{
		height = inner.bottom - inner.top;
		RefVar scrollers(GetProtoVariable(context, RSSYMscrollers, nil));
		if (NOTNIL(scrollers))
		{
			SetFrameSlot(scrollers, RSSYMviewflags, RefVar(MAKEINT(vVisible | vFloating | vClickable)));
			fRightMargin += kScrollerWidth;
		}
	}
	long width = fTextLeft + widest + fRightMargin;
	if (width > inner.right - inner.left)
		width = inner.right - inner.left;
	Boolean above = false;
	if (ISNIL(info))
	{
		above = inner.bottom < bounds.top + height && bounds.top - within.top > (inner.bottom - inner.top) / 2;
		if (inner.right < bounds.left + width)
			bounds.left = (short) (bounds.right - width);
		else
			bounds.right = (short) (bounds.left + width);
		if (above)
		{
			bounds.bottom = bounds.top;
			bounds.top = (short) (bounds.top - height);
		}
		else
		{
			bounds.top = bounds.bottom;
			bounds.bottom = (short) (bounds.bottom + height);
		}
	}
	else
	{
		TView* owner = (TView*) RefToAddress(info);
		Rect ownerBounds = within;
		Boolean placed = false;
		if (owner != nil)
		{
			while (owner->fParent != gRootView)
				owner = owner->fParent;
			if ((owner->fViewFormat & vfFrameMask) != 0)
			{
				ownerBounds = owner->viewBounds;
				SectRect(&within, &ownerBounds, &ownerBounds);
				Rect tryBounds = bounds;
				above = AdjustPopupInRect(tryBounds, width, height, ownerBounds, (short) frame);
				if (RectEncloses(ownerBounds, tryBounds))
				{
					bounds = tryBounds;
					placed = true;
				}
			}
		}
		if (!placed)
			above = AdjustPopupInRect(bounds, width, height, within, (short) frame);
	}
	// shifted into the area, cut to it
	long dh = 0;
	long dv = 0;
	if (bounds.right - bounds.left > inner.right - inner.left)
	{
		bounds.left = inner.left;
		bounds.right = inner.right;
	}
	else if (bounds.left < inner.left)
		dh = inner.left - bounds.left;
	else if (bounds.right > inner.right)
		dh = inner.right - bounds.right;
	if (bounds.bottom - bounds.top > inner.bottom - inner.top)
	{
		bounds.top = inner.top;
		bounds.bottom = inner.bottom;
	}
	else if (bounds.top < inner.top)
		dv = inner.top - bounds.top;
	else if (bounds.bottom > inner.bottom)
		dv = inner.bottom - bounds.bottom;
	OffsetRect(&bounds, dh, dv);
	OffsetRect(&bounds, -appArea.left, -appArea.top);
	if (above)
		SetFrameSlot(context, RSSYMvieweffect, RefVar(MAKEINT(kFlipEffect)));
	SetFrameSlot(context, RSSYMviewbounds, RefVar(ToObject(bounds)));
	if (fPicked.fItem != -1)
	{
		Rect pickedRect;
		GetGridItemRect(&fPicked, &pickedRect);
		while (pickedRect.top > viewBounds.bottom)
		{
			Scroll(RSSYMdown, false);
			GetGridItemRect(&fPicked, &pickedRect);
		}
	}
}


// ROM 0x00185068 GetOverflows__9TPickViewFv
// [what is scrolled off the top, what is off the bottom].
Ref
TPickView::GetOverflows(void)
{
	Point origin;
	GetChildOrigin(&origin);
	RefVar result(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(result, 0, MAKEINT(origin.v));
	long bottom = fItemCount > 0 ? fItemBottoms[fItemCount - 1] : 0;
	SetArraySlotRef(result, 1, MAKEINT(bottom + viewBounds.top + origin.v - viewBounds.bottom));
	return result;
}


// ROM 0x0018515c Scroll__9TPickViewFRC6RefVarUc
// The list scrolled a view's height 'up or 'down, to an item's top (not
// past the first, not past what lets the last show); unpick forgets the
// picked item.
void
TPickView::Scroll(RefArg direction, Boolean unpick)
{
	Point origin;
	GetChildOrigin(&origin);
	long lastBottom = fItemCount > 0 ? fItemBottoms[fItemCount - 1] : 0;
	long viewHeight = viewBounds.bottom - viewBounds.top;
	if (EQRef(direction, RSSYMup))
	{
		long newOrigin = origin.v + viewHeight;
		if (newOrigin > 0)
			newOrigin = 0;
		long i = fItemCount;
		while (--i >= 0 && ItemTop(i) + newOrigin > 0)
			;
		if (i < 0)
			i = 0;
		long top = ItemTop(i);
		if (viewHeight <= fItemBottoms[i] - top)
			newOrigin = -top;
		origin.v = (short) newOrigin;
	}
	else if (EQRef(direction, RSSYMdown))
	{
		long newOrigin = origin.v - viewHeight;
		if (newOrigin < viewHeight - lastBottom)
		{
			long i = 0;
			while (i < fItemCount && ItemTop(i) + newOrigin < 0)
				i++;
			if (i != 0)
				i--;
			long top = ItemTop(i);
			if (viewHeight <= fItemBottoms[i] - top)
				newOrigin = -top;
		}
		origin.v = (short) newOrigin;
	}
	if (unpick)
		fPicked.fItem = -1;
	SetOrigin(origin);
}


// ROM 0x00187384 GetItemRect__9TPickViewFP9PickStuffP5TRect
// An item's row: the view's width, from its top to its bottom (the
// child origin applied).
void
TPickView::GetItemRect(PickStuff* item, Rect* r)
{
	Point origin;
	GetChildOrigin(&origin);
	r->left = viewBounds.left;
	r->right = viewBounds.right;
	r->top = (short) (viewBounds.top + ItemTop(item->fItem) + origin.v);
	r->bottom = (short) (viewBounds.top + ItemBottom(item->fItem) + origin.v);
}


// ROM 0x001872ac GetGridItemRect__9TPickViewFP9PickStuffP5TRect
// A grid item's cell (the row for an item that is no grid).
void
TPickView::GetGridItemRect(PickStuff* item, Rect* r)
{
	GetItemRect(item, r);
	PickGridInfo* grid = fGrids[item->fItem];
	if (grid == nil)
		return;
	r->left = (short) (r->left + item->fX * (grid->fCellWidth + grid->fCellFrame) + fTextLeft + grid->fOuterFrame);
	r->top = (short) (r->top + item->fY * (grid->fCellHeight + grid->fCellFrame) + fTopMargin + grid->fOuterFrame);
	r->right = (short) (r->left + grid->fCellWidth);
	r->bottom = (short) (r->top + grid->fCellHeight);
}


// ROM 0x00187448 InvertItem__9TPickViewFP9PickStuff
void
TPickView::InvertItem(PickStuff* item)
{
	Rect r;
	GetGridItemRect(item, &r);
	InvertRect(&r);
}


// ROM 0x0018746c FlashItem__9TPickViewFP9PickStuff
// The item inverted three times, 5 ticks apart (it is left inverted:
// the tracking inverted it under the pen, so it ends un-inverted).
void
TPickView::FlashItem(PickStuff* item)
{
	if (item->fItem == -1)
		return;
	for (long i = 0; i < 3; i++)
	{
		Wait(5);
		InvertItem(item);
	}
}


// ROM 0x00187918 TrackStroke__9TPickViewFP13TStrokePublicP9PickStuff
// The pen tracked over the items, its ink off, the screen brought up to
// date first: the pickable item under the stroke's first point inverted,
// then each turn the one under its last point - the old one un-inverted
// and the new inverted when it changed (a grid item's cell counts), a
// tick waited when it did not - until the stroke is done; ==> item the
// one the pen ended on (-1 for none).  NOT YET RECONSTRUCTED: BusyBoxSend.
void
TPickView::TrackStroke(TStrokePublic* stroke, PickStuff* item)
{
	stroke->InkOff(true);
	gRootView->Update(nil);
	Point pt = stroke->FirstPoint();
	PickableItem(pt, item);
	if (item->fItem != -1)
		InvertItem(item);
	while (!stroke->Done())
	{
		PickStuff under;
		pt = stroke->FinalPoint();
		PickableItem(pt, &under);
		if (under.fItem == item->fItem && under.fX == item->fX && under.fY == item->fY)
			Wait(1);
		else
		{
			if (item->fItem != -1)
				InvertItem(item);
			if (under.fItem != -1)
				InvertItem(&under);
			*item = under;
		}
	}
}


// ROM 0x001874b4 SubItem__9TPickViewFR6TPointP9PickStuff
// The cell of a grid item under the point (none: the item -1).
void
TPickView::SubItem(Point& pt, PickStuff* item)
{
	PickGridInfo* grid = fGrids[item->fItem];
	if (grid == nil)
		return;
	item->fIsGrid = true;
	for (long y = 0; y < grid->fRows; y++)
	{
		item->fY = y;
		for (long x = 0; x < grid->fColumns; x++)
		{
			item->fX = x;
			Rect cell;
			GetGridItemRect(item, &cell);
			if (PtInRect(pt, &cell))
				return;
		}
	}
	item->fItem = -1;
}


// ROM 0x00187588 Item__9TPickViewFR6TPointP9PickStuff
// The item under the point (-1 for none), and its cell in a grid.
void
TPickView::Item(Point& pt, PickStuff* item)
{
	item->fItem = -1;
	item->fIsGrid = false;
	if (!PtInRect(pt, &viewBounds))
		return;
	for (item->fItem = 0; item->fItem < fItemCount; item->fItem++)
	{
		Rect r;
		GetItemRect(item, &r);
		if (PtInRect(pt, &r))
		{
			SubItem(pt, item);
			return;
		}
	}
	item->fItem = -1;
}


// ROM 0x0018763c PickableItem__9TPickViewFR6TPointP9PickStuff
// The pickable item under the point: an unpickable one (a separator)
// sends the search to the row above (from its middle); a grid cell over
// a masked picture's blank is not pickable (FPtInPicture, NOT YET).
void
TPickView::PickableItem(Point& pt, PickStuff* item)
{
	Item(pt, item);
	if (item->fItem == -1)
		return;
	Boolean pickable = false;
	UniChar mark;
	GetItemFlags(item, &pickable, &mark);
	if (pickable)
		return;
	Rect r;
	GetItemRect(item, &r);
	Point again = pt;
	again.v = (short) (r.top - 1);
	PickableItem(again, item);
}


/*------------------------------------------------------------------------------
	K e y s
------------------------------------------------------------------------------*/

// the pick command for the picked item: the PickStuff as a binary frame
// parameter (the ROM: ToObject('string, &fPicked, 16)), keys allowed
// through the picker while it is picked
static void
PostPick(TPickView* view)
{
	RefVar cmd(MakeCommand(aePickItem, view, view->fPicked.fItem));
	RefVar stuff(AllocateBinary(RSSYMstring, sizeof(PickStuff)));
	memmove(BinaryData(stuff), &view->fPicked, sizeof(PickStuff));
	CommandSetFrameParameter(cmd, stuff);
	SetFrameSlot(view->fContext, RSSYMallowkeysthrough, RefVar(TRUEREF));
	gApplication->DispatchCommand(cmd);
}


// ROM 0x0018814c KeyToNextItem__9TPickViewFl
// The pick moved down from the item given: the next pickable item (a
// grid's cells row by row), the first that is shown (its top within the
// view) when nothing was picked yet.
void
TPickView::KeyToNextItem(long from)
{
	long item = from;
	while (item != fItemCount && !IsItemNoPickable(item))
		item++;
	Boolean isGrid = false;
	PickGridInfo* grid = nil;
	long x = 0, y = 0;
	if (item != fItemCount)
	{
		grid = fGrids[item];
		isGrid = grid != nil;
	}
	for ( ; ; )
	{
		Rect r;
		SetEmptyRect(&r);
		if (item != fItemCount)
		{
			PickStuff stuff = { item, isGrid, x, y };
			GetGridItemRect(&stuff, &r);
		}
		if (fPicked.fItem != -1 || item == fItemCount || r.top >= viewBounds.top)
		{
			if (item != fItemCount)
			{
				fPicked.fItem = item;
				fPicked.fIsGrid = isGrid;
				if (isGrid)
				{
					fPicked.fX = x;
					fPicked.fY = y;
				}
			}
			return;
		}
		if (isGrid && y != grid->fRows - 1)
			y++;
		else
		{
			do
				item++;
			while (item != fItemCount && !IsItemNoPickable(item));
			if (item != fItemCount)
			{
				grid = fGrids[item];
				isGrid = grid != nil;
				x = y = 0;
			}
		}
	}
}


// ROM 0x001882f0 KeyToPrevItem__9TPickViewFl
// The pick moved up from the item given: the previous pickable item (a
// grid's last row), the last that is shown (its bottom within the view)
// when nothing was picked yet.
void
TPickView::KeyToPrevItem(long from)
{
	long item = from;
	while (item != -1 && !IsItemNoPickable(item))
		item--;
	Boolean isGrid = false;
	PickGridInfo* grid = nil;
	long x = 0, y = 0;
	if (item != -1)
	{
		grid = fGrids[item];
		isGrid = grid != nil;
		if (isGrid)
			y = grid->fRows - 1;
	}
	for ( ; ; )
	{
		Rect r;
		SetEmptyRect(&r);
		if (item != -1)
		{
			PickStuff stuff = { item, isGrid, x, y };
			GetGridItemRect(&stuff, &r);
		}
		if (fPicked.fItem != -1 || item == -1 || r.bottom <= viewBounds.bottom)
		{
			if (item != -1)
			{
				fPicked.fItem = item;
				fPicked.fIsGrid = isGrid;
				if (isGrid)
				{
					fPicked.fX = x;
					fPicked.fY = y;
				}
			}
			return;
		}
		if (isGrid && y != 0)
			y--;
		else
		{
			do
				item--;
			while (item != -1 && !IsItemNoPickable(item));
			if (item != -1)
			{
				grid = fGrids[item];
				isGrid = grid != nil;
				x = 0;
				y = isGrid ? grid->fRows - 1 : 0;
			}
		}
	}
}


// the first pickable item, as a key to a picker with nothing picked finds it
static long
FirstPickable(TPickView* view)
{
	long item = 0;
	while (item != view->fItemCount && !view->IsItemNoPickable(item))
		item++;
	return item;
}


// ROM 0x00188480 HandleKeyDown__9TPickViewFUsUl
// A key to the picker: the arrows move the pick (left/right within a
// grid's row - or to the first pickable item's last/first cell when
// nothing is picked; up/down to the previous/next item, tab down too);
// return (or enter) picks the picked item (the pick command with the
// PickStuff, keys allowed through); a key command of the items
// (keyCommands, FindKeyCommandInArray) picks that item outright;
// command-. / command-w / escape close the picker (aeDropChild to the
// parent); any other character type-selects: it joins the characters
// typed within the timeout (up to 21) and the first item from the pick
// on (else from the start) whose text begins with them is picked.  The
// picked item is scrolled into view, the picker redrawn.  ==> true
// (handled).  NOT YET RECONSTRUCTED: FClicker.
Boolean
TPickView::HandleKeyDown(UniChar ch, ULong parameter)
{
	ULong modifiers = parameter & 0x3e000000;
	UniChar key = (UniChar) (parameter & 0xffff);
	long item = fPicked.fItem;
	PickGridInfo* grid = item != -1 ? fGrids[item] : nil;
	Boolean moved = false;
	Boolean picked = false;
	if (key == 0x1c)
	{
		moved = true;
		if (item == -1)
		{
			long first = FirstPickable(this);
			if (first != fItemCount && fGrids[first] != nil)
			{
				fPicked.fItem = first;
				fPicked.fIsGrid = true;
				fPicked.fY = 0;
				fPicked.fX = fGrids[first]->fColumns - 1;
			}
		}
		else if (grid != nil && fPicked.fX != 0)
			fPicked.fX--;
	}
	else if (key == 0x1d)
	{
		moved = true;
		if (item == -1)
		{
			long first = FirstPickable(this);
			if (first != fItemCount && fGrids[first] != nil)
			{
				fPicked.fItem = first;
				fPicked.fIsGrid = true;
				fPicked.fY = 0;
				fPicked.fX = 0;
			}
		}
		else if (grid != nil && fPicked.fX != grid->fColumns - 1)
			fPicked.fX++;
	}
	else if (key == 0x1e)
	{
		moved = true;
		if (grid != nil && fPicked.fY != 0)
			fPicked.fY--;
		else
			KeyToPrevItem((item == -1 ? fItemCount : item) - 1);
	}
	else if (key == 0x1f || key == 9)
	{
		moved = true;
		if (grid != nil && fPicked.fY != grid->fRows - 1)
			fPicked.fY++;
		else
			KeyToNextItem(item == -1 ? 0 : item + 1);
	}
	else if (key == 0x0d || key == 3)
	{
		moved = true;
		if (item != -1)
		{
			// NOT YET RECONSTRUCTED: FClicker
			PostPick(this);
			picked = true;
		}
	}
	else
	{
		if (IsArray(fKeyCommands))
		{
			long found = FindKeyCommandInArray(fKeyCommands, key, modifiers, nil, nil);
			if (found != -1)
			{
				PickStuff old = fPicked;
				fPicked.fItem = found;
				if (fGrids[found] == nil)
					fPicked.fIsGrid = false;
				else
				{
					fPicked.fIsGrid = true;
					fPicked.fX = 0;
					fPicked.fY = 0;
				}
				if (found != old.fItem || (fPicked.fIsGrid && (fPicked.fX != old.fX || fPicked.fY != old.fY)))
				{
					Dirty(nil);
					gRootView->Update(nil);
				}
				PostPick(this);
				moved = true;
				picked = true;
			}
		}
		if (!picked && (((modifiers & (kCommandModifier << 25)) && (key == '.' || key == 'w')) || key == 0x1b))
		{
			SetFrameSlot(fContext, RSSYMallowkeysthrough, RefVar(TRUEREF));
			fAutoClose = true;
			RefVar cmd(MakeCommand(aeDropChild, fParent, (Long) this));
			gApplication->DispatchCommand(cmd);
			moved = true;
			picked = true;
		}
	}
	if (ch != 0 && !moved)
	{
		ULong now = Ticks();
		if (ISNIL(fTypeSelect) || (ULong) fTypeSelectTimeout < now - fLastKeyTime)
			fTypeSelect = Clone(RefVar(Remptystring));
		fLastKeyTime = now;
		long typed = Length(fTypeSelect) / 2 - 1;
		if (typed < 21)
		{
			SetLength(fTypeSelect, typed * 2 + 4);
			UniChar* text = GetCString(fTypeSelect);
			text[typed] = ch;
			text[typed + 1] = 0;
			typed++;
		}
		TRichString wanted(fTypeSelect);
		long count = Length(fPickItems);
		long found = -1;
		long start = fPicked.fItem == -1 ? 0 : fPicked.fItem;
		for (long pass = 0; pass < 2 && found == -1; pass++)
		{
			long first = pass == 0 ? start : 0;
			long last = pass == 0 ? start + 1 : count;
			if (pass == 0 && fPicked.fItem == -1)
				continue;
			for (long i = first; i < last; i++)
			{
				RefVar text(GetItemNoText(i));
				if (ISNIL(text))
					continue;
				TRichString itemText(text);
				long n = typed;
				if (itemText.Length() < n)
					n = itemText.Length();
				if (itemText.CompareSubStringCommon(wanted, 0, n, false) == 0)
				{
					found = i;
					break;
				}
			}
		}
		if (found != -1)
		{
			fPicked.fItem = found;
			fPicked.fIsGrid = false;
		}
	}
	if (picked)
		return true;
	if (fPicked.fItem == -1)
		return true;
	Rect r;
	GetGridItemRect(&fPicked, &r);
	if (r.bottom > viewBounds.bottom)
	{
		while (r.bottom > viewBounds.bottom)
		{
			Scroll(RSSYMdown, false);
			GetGridItemRect(&fPicked, &r);
		}
		NSSend(fContext, RSSYMsetscrollers);
	}
	else if (r.top < viewBounds.top)
	{
		while (r.top < viewBounds.top)
		{
			Scroll(RSSYMup, false);
			GetGridItemRect(&fPicked, &r);
		}
		NSSend(fContext, RSSYMsetscrollers);
	}
	Dirty(nil);
	gRootView->Update(nil);
	return true;
}


// ROM 0x0018382c FPickViewKeyDown
// :PickViewKeyDown(char, key) (protoPicker's viewKeyDownScript): the key
// handled by the picker; ==> whether it was.
static Ref
FPickViewKeyDown(RefArg rcvr, RefArg ch, RefArg key)
{
	TPickView* view = (TPickView*) GetView(rcvr);
	if (view == nil)
		return TRUEREF;
	return MAKEBOOLEAN(view->HandleKeyDown((UniChar) RCHAR(ch), (ULong) RINT(key)));
}


// ROM 0x001f09f4 FGetPopup
// GetPopup(): the context of the popup that is up, or nil.
static Ref
FGetPopup(RefArg /*rcvr*/)
{
	TView* popup = gRootView->fPopup;
	return popup != nil ? (Ref) popup->fContext : NILREF;
}


// ROM 0x001f0a18 FClearPopup
// ClearPopup(): the popup taken down, without closing it.
static Ref
FClearPopup(RefArg /*rcvr*/)
{
	if (gRootView->fPopup != nil)
		gRootView->SetPopup(gRootView->fPopup, false);
	return NILREF;
}


// ROM 0x001f0a48 FDismissPopup
// DismissPopup(): every popup closed, one after another, and the
// machine allowed to put one up again.
static Ref
FDismissPopup(RefArg /*rcvr*/)
{
	while (gRootView->fPopup != nil)
		gRootView->SetPopup(nil, true);
	gInhibitPopup = false;
	return NILREF;
}


// ROM 0x00185044 FPickViewGetScollerValues
// picker:GetScrollerValues() - where a popup list that is too tall for the
// screen is scrolled to, for its scroll arrows (SetScrollers): the
// view's GetOverflows.  (The ROM's name has the typo.)  nil for no view.
static Ref
FPickViewGetScollerValues(RefArg rcvr)
{
	TPickView* view = (TPickView*) GetView(rcvr);
	if (view == nil)
		return NILREF;
	return view->GetOverflows();		// (a tail call in the ROM)
}


// ROM 0x00185130 FPickViewScroll
// picker:Scroll(direction) - the list scrolled a page up or down, the
// picked item let go (TPickView::Scroll).
static Ref
FPickViewScroll(RefArg rcvr, RefArg direction)
{
	TPickView* view = (TPickView*) GetView(rcvr);
	if (view != nil)
		view->Scroll(direction, true);
	return NILREF;
}


void
RegisterPickNatives(void)
{
	RegisterNativeFunction("FPickViewGetScollerValues", (void*) FPickViewGetScollerValues, 0);
	RegisterNativeFunction("FPickViewScroll", (void*) FPickViewScroll, 1);
	RegisterNativeFunction("FPickViewKeyDown", (void*) FPickViewKeyDown, 2);
	RegisterNativeFunction("FGetPopup", (void*) FGetPopup, 0);
	RegisterNativeFunction("FClearPopup", (void*) FClearPopup, 0);
	RegisterNativeFunction("FDismissPopup", (void*) FDismissPopup, 0);
}


// ROM 0x00187a4c PickItem__9TPickViewFP9PickStuff
// An item picked: remembered, flashed, the picker hidden when it
// autocloses, and pickActionScript run with the item's index (plus
// topItem) - a grid cell as a protoGridItem frame {index, x, y} - on the
// callbackContext, else the view.  NOT YET RECONSTRUCTED: an item's key
// command sent to the key view instead (SendKeyMessage).  Then the
// picker forgets the pick.
void
TPickView::PickItem(PickStuff* item)
{
	fPicked = *item;
	if (item->fItem == -1)
		fPicking = false;
	RefVar context(fContext);
	RefVar allowKeys(GetProtoVariable(context, RSSYMallowkeysthrough, nil));
	SetFrameSlot(context, RSSYMallowkeysthrough, RefVar(TRUEREF));
	FlashItem(item);
	if (fAutoClose)
		Hide();
	RefVar callback(GetProto(RSSYMcallbackcontext));
	RefVar args(MakeArray(1));
	RefVar picked;
	if (item->fItem != -1)
	{
		long index = RINT(GetProto(RSSYMtopitem)) + item->fItem;
		picked = MAKEINT(index);
		if (item->fIsGrid)
		{
			picked = Clone(RefVar(Rprotogriditem));
			SetFrameSlot(picked, RSSYMindex, RefVar(MAKEINT(index)));
			SetFrameSlot(picked, RSSYMx, RefVar(MAKEINT(item->fX)));
			SetFrameSlot(picked, RSSYMy, RefVar(MAKEINT(item->fY)));
		}
	}
	SetArraySlotRef(args, 0, picked);
	if (ISNIL(callback))
		RunScript(RSSYMpickactionscript, args, true);
	else
		DoMessage(callback, RSSYMpickactionscript, args);
	TView* view = GetView(context);
	if (view != nil)
	{
		((TPickView*) view)->fPicked.fItem = -1;
		SetFrameSlot(context, RSSYMallowkeysthrough, allowKeys);
	}
}


// ROM 0x00186f74 Hide__9TPickViewFv
// Hidden (keys allowed through again); an autoclose picker hidden without
// a pick runs pickCancelledScript on the callbackContext (when it has
// one), else on the view.
void
TPickView::Hide(void)
{
	Boolean cancelled = (fFlags & vVisible) != 0 && !fPicking && fAutoClose;
	SetFrameSlot(fContext, RSSYMallowkeysthrough, RefVar(TRUEREF));
	TView::Hide();
	if (cancelled)
	{
		RefVar callback(GetProto(RSSYMcallbackcontext));
		if (ISNIL(callback))
			RunScript(RSSYMpickcancelledscript, RefVar(NILREF), true);
		else
		{
			long exists;
			GetVariable(callback, RSSYMpickcancelledscript, &exists, 0);
			if (exists)
				DoMessage(callback, RSSYMpickcancelledscript, RefVar(NILREF));
		}
	}
	fPicked.fItem = -1;
}


// ROM 0x001870c4 RealDoCommand__9TPickViewFRC6RefVar
// aeClick clicks (FClicker, NOT YET RECONSTRUCTED), tracks the pen over
// the items (TrackStroke) and dispatches the pick (0x36) with the item
// the pen ended on as the parameter and the PickStuff as a binary frame
// parameter; the pick command picks the item (PickItem) and, when the
// picker autocloses, drops it from its parent.  Other commands as TView.
// ==> handled.
Boolean
TPickView::RealDoCommand(RefArg cmd)
{
	long id = CommandID(cmd);
	if (id == aeClick)
	{
		PickStuff stuff;
		TrackStroke(((TUnitPublic*) CommandParameter(cmd))->Stroke(), &stuff);
		RefVar pick(MakeCommand(aePickItem, this, stuff.fItem));
		RefVar param(AllocateBinary(RSSYMstring, sizeof(PickStuff)));
		memmove(BinaryData(param), &stuff, sizeof(PickStuff));
		CommandSetFrameParameter(pick, param);
		gApplication->DispatchCommand(pick);
		CommandSetResult(cmd, 1);
		return true;
	}
	if (id != aePickItem)
		return TView::RealDoCommand(cmd);
	fPicking = true;
	SetFrameSlot(fContext, RSSYMallowkeysthrough, RefVar(TRUEREF));
	PickStuff stuff;
	RefVar param(CommandFrameParameter(cmd));
	if (ISNIL(param))
	{
		stuff.fItem = CommandParameter(cmd);
		stuff.fIsGrid = false;
		stuff.fX = 0;
		stuff.fY = 0;
	}
	else
		memmove(&stuff, BinaryData(param), sizeof(PickStuff));
	PickItem(&stuff);
	if (fAutoClose)
	{
		RefVar drop(MakeCommand(aeDropChild, fParent, (Long) this));
		gApplication->DispatchCommand(drop);
	}
	CommandSetResult(cmd, 1);
	return true;
}


// ROM 0x001865b0 RealDraw__9TPickViewFR5TRect
// The rows drawn from the child origin: a string item from the text
// column (its icon before it, and the indent), its baseline centred in
// the row, |length| characters and the ellipsis after them when the
// length is negative; a separator a gray (or a solid black, two pixels
// thick) line three down; a picture item its picture at the row's top
// left; the mark in the marks' column; the key command at the right (a
// command keyboard: NOT YET).  The picked item is inverted.
void
TPickView::RealDraw(Rect& /*bounds*/)
{
	Point origin;
	GetChildOrigin(&origin);
	long indent = -1;
	for (long i = 0; i < fItemCount; i++)
	{
		RefVar item(GetArraySlotRef(fPickItems, i));
		long itemIndent = GetDisplayIndent(item);
		if (itemIndent >= 0)
			indent = itemIndent;
		Boolean pickable;
		UniChar mark;
		RefVar display(GetDisplayItem(i, &pickable, &mark));
		long top = viewBounds.top + origin.v + ItemTop(i);
		long bottom = viewBounds.top + origin.v + ItemBottom(i);
		long baseline = fTopMargin + ((bottom + top) - (fFontInfo.ascent + fFontInfo.descent)) / 2 + fFontInfo.ascent - 1;
		long markBaseline = 0;
		if (IsString(display))
		{
			long x = viewBounds.left + fTextLeft;
			RefVar icon(GetDisplayIcon(item));
			if (ISNIL(icon))
			{
				if (indent >= 0)
					x += indent;
			}
			else
			{
				Rect iconBounds;
				FromObject(RefVar(GetFrameSlotRef(icon, RSSYMbounds)), iconBounds);
				long iconWidth = indent < 0 ? (iconBounds.right - iconBounds.left) + 2 : indent;
				Rect iconBox;
				SetRect(&iconBox, (short) x, (short) top, (short) (x + (iconBounds.right - iconBounds.left)), (short) bottom);
				DrawPicture(icon, iconBox, vjCenterV | vjCenterH, srcOr);
				x += iconWidth;
			}
			TRichString rich(display);
			long length = GetItemLength(i);
			long count = length < 0 ? -length : length;
			FPoint where;
			where.x = ToFixed(x);
			where.y = ToFixed(baseline);
			TextBoundsInfo textBounds;
			DrawRichString(rich, 0, count, &fStyle, where, nil, &textBounds);
			if (length < 0)
			{
				UniChar dots = kEllipsisChar;
				StyleRecord* style = &fStyle;
				FPoint at;
				at.x = where.x + textBounds.fWidth;
				at.y = where.y;
				DrawTextOnce(&dots, 1, &style, nil, at, nil, nil);
			}
			markBaseline = baseline;
		}
		else if (IsSymbol(display))
		{
			if (EQRef(display, RSSYMpickseparator))
			{
				PatternHandle saved = GetFgPattern();
				SetFgPattern(GetStdPattern(grayPat));
				MoveTo(viewBounds.left + 1, top + 3);
				LineTo(viewBounds.right - 1, top + 3);
				SetFgPattern(saved);
			}
			else if (EQRef(display, RSSYMpicksolidseparator))
			{
				PenState pen;
				GetPenState(&pen);
				SetFgPattern(GetStdPattern(blackPat));
				PenSize(1, 2);
				MoveTo(viewBounds.left, top + 3);
				LineTo(viewBounds.right - 1, top + 3);
				SetPenState(&pen);
			}
		}
		else if (IsFrame(display))
		{
			Rect box;
			SetRect(&box, (short) (viewBounds.left + fTextLeft), (short) (top + fTopMargin), (short) (viewBounds.left + fTextLeft), (short) (top + fTopMargin));
			if (FrameHasSlotRef(display, RSSYMbits) || FrameHasSlotRef(display, RSSYMcolordata))
				DrawPicture(display, box, 0, srcOr);
			else if (FrameHasSlotRef(display, RSSYMpicture))
				DrawPicture(RefVar(GetFrameSlotRef(display, RSSYMpicture)), box, 0, srcOr);
			// NOT YET RECONSTRUCTED: a strokeList item (DrawStrokeBundle)
			markBaseline = top + (bottom - top) / 2 + fFontInfo.ascent / 2;
		}
		if (fHasMarks && mark != 0 && mark != kSpace)
		{
			StyleRecord* style = &fStyle;
			FPoint at;
			at.x = ToFixed(viewBounds.left + fMarkLeft);
			at.y = ToFixed(markBaseline);
			DrawTextOnce(&mark, 1, &style, nil, at, nil, nil);
		}
	}
	if (fPicked.fItem != -1)
		InvertItem(&fPicked);
}
