/*
	File:		views/ClipboardView.cpp

	Contains:	TClipboard and the clipboard's NewtonScript functions - see
				ClipboardView.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ClipboardView.h"
#include "RootView.h"
#include "DragDrop.h"
#include "Application.h"
#include "Commands.h"
#include "ViewFlags.h"
#include "Bits.h"
#include "Draw.h"
#include "Fonts.h"
#include "Ports.h"
#include "Rects.h"
#include "Text.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "RichString.h"
#include "SoundSettings.h"
#include "StyleRuns.h"
#include "Unicode.h"
#include "NativeFunctions.h"

// frames/Munger.cpp, frames/Locale.h and the object system
Ref		FSetContains(RefArg rcvr, RefArg array, RefArg target);
Ref		GetPreference(RefArg slot);
void	GetAppAreaBounds(Rect* bounds);							// views/PickView.cpp

// ROM 0x0c100cc0 (unnamed)
// When the clipping was last picked up off the clipboard: a second
// stroke within 80 ticks of it is a copy rather than a move.
static ULong	gLastClipboardDragTicks = 0;


/*------------------------------------------------------------------------------
	W h e r e   a   c l i p p i n g   m a y   b e   d r o p p e d
------------------------------------------------------------------------------*/

// ROM 0x0009e2b0 PointOnClipboard__FRC6TPointR5TRectRC6RefVar
// Whether the point lies outside the rectangle - which is what says a
// drag was let go on the background rather than in the application area.
// The edge the button bar is on does not count: a clipping dropped over
// the button bar still belongs to the application area, because the bar
// is drawn on top of it.
Boolean
PointOnClipboard(const Point& pt, const Rect& appArea, RefArg buttonBarPosition)
{
	if ((appArea.left < pt.h || EQ(buttonBarPosition, RSSYMleft))
		&& (appArea.top < pt.v || EQ(buttonBarPosition, RSSYMtop))
		&& (pt.h < appArea.right || EQ(buttonBarPosition, RSSYMright))
		&& (pt.v < appArea.bottom || EQ(buttonBarPosition, RSSYMbottom)))
		return false;
	return true;
}


// ROM 0x0009ef64 PointOnClipboard__FRC6TPoint
// The same question against the application area of vars.displayParams,
// inset by five pixels on every side - a drag that ends within five
// pixels of the edge counts as let go on the background.
Boolean
PointOnClipboard(const Point& pt)
{
	RefVar params(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdisplayparams));
	Rect box;
	box.left = (short) (RINT(GetProtoVariable(params, RSSYMappareagloballeft, nil)) + 5);
	box.top = (short) (RINT(GetProtoVariable(params, RSSYMappareaglobaltop, nil)) + 5);
	box.right = (short) (box.left + RINT(GetProtoVariable(params, RSSYMappareawidth, nil)) - 10);
	box.bottom = (short) (box.top + RINT(GetProtoVariable(params, RSSYMappareaheight, nil)) - 10);
	RefVar where(GetProtoVariable(params, RSSYMbuttonbarposition, nil));
	return PointOnClipboard(pt, box, where);
}


// ROM 0x0009ca58 OffsetBoundsRef__FRC6RefVarRC6TPoint
// The `viewBounds` slot of a dragged item's data frame moved by the point -
// which is how the data comes out of the source view's coordinates and
// into the clipping's.
void
OffsetBoundsRef(RefArg frame, const Point& by)
{
	if (!IsFrame(frame))
		return;
	RefVar bounds(GetFrameSlotRef(frame, RSSYMviewbounds));
	if (NOTNIL(bounds))
	{
		Rect r;
		FromObject(bounds, r);
		OffsetRect(&r, by.h, by.v);
		SetBoundsRect(bounds, r);
	}
}


// ROM 0x0009cb00 CheckViewBounds__FRC6RefVarT1
// A 'text item's data frame must have a viewBounds for the clipping to
// lay it out; one that has none gets an empty rectangle.
void
CheckViewBounds(RefArg dragType, RefArg form)
{
	if (!EQ(dragType, RSSYMtext))
		return;
	RefVar bounds(GetFrameSlotRef(form, RSSYMviewbounds));
	if (ISNIL(bounds))
	{
		Rect empty;
		SetRect(&empty, 0, 0, 0, 0);
		SetFrameSlot(form, RSSYMviewbounds, RefVar(ToObject(empty)));
	}
}


/*------------------------------------------------------------------------------
	T C l i p b o a r d
------------------------------------------------------------------------------*/

// ROM 0x0009edfc ClassID__10TClipboardCFv
long
TClipboard::ClassID(void) const
{
	return clClipboard;
}


// ROM 0x0009ee04 DerivedFrom__10TClipboardCFl
Boolean
TClipboard::DerivedFrom(long id) const
{
	return id == clClipboard || TView::DerivedFrom(id);
}


// ROM 0x0009ee38 Constructor__10TClipboardFRC6RefVarP5TView
// The clipping's four slots read out of the context: the drag types and
// data of each item, the rectangle the items came from (moved into the
// application area's coordinates) and the picture taken of them.
void
TClipboard::Constructor(RefArg context, TView* parent)
{
	TView::Constructor(context, parent);
	fTypes = GetProto(RSSYMtypes);
	fData = GetProto(RSSYMdata);
	FromObject(RefVar(GetProto(RSSYMbounds)), fBounds);
	Rect appArea;
	GetAppAreaBounds(&appArea);
	OffsetRect(&fBounds, appArea.left, appArea.top);
	fBits = GetProto(RSSYMbits);
}


// ROM 0x0009f0e0 AllocateClipboardBits__10TClipboardSFRC5TRectP8PixelMap
// A 'bits binary big enough for the rectangle at the screen's depth, and
// the pixel map that describes it (its baseAddr is the caller's to fill
// in, so the map is marked as holding a pointer).  A heap too full for
// the picture is not an error: the clipping is made without one and
// DrawDragData frames its outline instead.
Ref
TClipboard::AllocateClipboardBits(const Rect& bounds, PixelMap* map)
{
	long size = TBits::InitBitMap(bounds, map);
	map->pixMapFlags |= kPixMapPtr;
	RefVar bits;
	newton_try
	{
		bits = AllocateBinary(RSSYMbits, size);
	}
	newton_catch(exOutOfMemory)
	{ }
	end_try;
	return bits;
}


// ROM 0x0009f188 NewClipboard__10TClipboardSFRC9TDragInfoP5TViewRC5TRectPC6TPoint
// A clipping made of a drag that was let go on the background.  Each of
// the drag's items is asked of the source view - or, when it answers
// nothing, of the item's own view - for its data of each of its types;
// the data is made internal (it may be pointing into a store), given a
// viewBounds when it is text without one, and moved out of the source
// view's coordinates.  The first 'text item's data becomes the label of
// the icon.  The two views - the clipping and its icon - are then handed
// to the root view together.
void
TClipboard::NewClipboard(const TDragInfo& dragInfo, TView* view, const Rect& bounds, const Point* where)
{
	// with no point given the clipping goes where the last icon was, or
	// 72 pixels down the left edge when there is none
	Point place;
	const Point* pt = where;
	if (where == nil)
	{
		TView* icon = gRootView->GetClipboardIcon();
		if (icon != nil)
			place = *(Point*) &icon->viewBounds;
		else
		{
			place.v = 72;
			place.h = 0;
		}
		pt = &place;
	}

	Rect box = bounds;
	RefVar bits(view->GetClipboardDataBits(&box));
	long count = dragInfo.Count();
	RefVar types(MakeArray(count));
	RefVar data(MakeArray(count));
	RefVar dragRef, itemData, type, dropData, labelInfo;
	for (long i = 0; i < count; i++)
	{
		SetArraySlot(types, i, RefVar(EnsureInternal(RefVar(dragInfo.GetItemTypes(i)))));
		dragRef = dragInfo.GetItemDragRef(i);
		long typeCount = Length(RefVar(dragInfo.GetItemTypes(i)));
		itemData = MakeArray(typeCount);
		TView* itemView = dragInfo.GetItemView(i);
		for (long j = 0; j < typeCount; j++)
		{
			type = dragInfo.GetItemIndType(i, j);
			TView* from = view;
			dropData = view->GetDropData(type, dragRef);
			if (ISNIL(dropData) && itemView != nil)
			{
				dropData = itemView->GetDropData(type, dragRef);
				from = itemView;
			}
			dropData = EnsureInternal(dropData);
			CheckViewBounds(type, dropData);
			OffsetBoundsRef(dropData, *(Point*) &from->viewBounds);
			SetArraySlot(itemData, j, dropData);
			if (ISNIL(labelInfo) && EQ(type, RSSYMtext))
				labelInfo = dropData;
		}
		SetArraySlot(data, i, itemData);
	}

	RefVar form(Clone(RefVar(Rstarterclipboard)));
	SetFrameSlot(form, RSSYMtypes, types);
	SetFrameSlot(form, RSSYMdata, data);
	SetFrameSlot(form, RSSYMbounds, RefVar(ToObject(box)));
	SetFrameSlot(form, RSSYMbits, bits);
	RefVar label(CreateLabelForm(dragInfo, labelInfo, *pt));
	gRootView->AddClipboard(form, label);
}


// ROM 0x0009f568 MoveIcon__10TClipboardFRC6TPoint
// The clipping's icon moved so that the point is where CalcIconBounds
// puts it; the icon's `pin` records which edges of the application area
// it has come to rest against, so that a turn of the screen keeps it
// there (FReOrientLabelForm).
void
TClipboard::MoveIcon(const Point& pt)
{
	TView* icon = gRootView->GetClipboardIcon(this);
	Rect box;
	CalcIconBounds((short) (icon->viewBounds.right - icon->viewBounds.left),
				   (short) (icon->viewBounds.bottom - icon->viewBounds.top), pt, &box);
	Rect appArea;
	GetAppAreaBounds(&appArea);
	long pin = 0;
	if (box.left <= 0)
		pin |= 1;
	if (box.top <= 0)
		pin |= 2;
	if (appArea.right - appArea.left <= box.right)
		pin |= 4;
	if (appArea.bottom - appArea.top <= box.bottom)
		pin |= 8;
	SetFrameSlot(icon->fContext, RSSYMpin, RefVar(MAKEINT(pin)));
	Point by;
	by.h = (short) (box.left - icon->viewBounds.left);
	by.v = (short) (box.top - icon->viewBounds.top);
	icon->DoMoveCommand(by);
}


// ROM 0x0009f6dc CalcDataBitsBounds__10TClipboardFP5TRect
// Where the clipping's picture is drawn when it is dragged off the
// clipboard: the size of the rectangle the items came from, laid at the
// icon's top left and pushed back inside the application area when it
// hangs off the right or the bottom.
void
TClipboard::CalcDataBitsBounds(Rect* bounds)
{
	TView* icon = gRootView->GetClipboardIcon(this);
	*bounds = icon->viewBounds;
	Rect appArea;
	GetAppAreaBounds(&appArea);

	short width = (short) (fBounds.right - fBounds.left);
	bounds->right = (short) (bounds->left + width);
	if (bounds->right > appArea.right)
	{
		bounds->left = (short) (appArea.right - width);
		if (bounds->left < appArea.left)
			bounds->left = appArea.left;
		bounds->right = appArea.right;
	}
	short height = (short) (fBounds.bottom - fBounds.top);
	bounds->bottom = (short) (bounds->top + height);
	if (bounds->bottom > appArea.bottom)
	{
		bounds->top = (short) (appArea.bottom - height);
		if (bounds->top < appArea.top)
			bounds->top = appArea.top;
		bounds->bottom = appArea.bottom;
	}
}


// ROM 0x0009f84c TruncateLabel__10TClipboardSFP11TRichStringRC6RefVar
// The label cut down to 50 pixels in the icon's style: the characters
// are measured one at a time until the fiftieth pixel, and the rest are
// replaced by an ellipsis.
void
TClipboard::TruncateLabel(TRichString* label, RefArg style)
{
	StyleRecord record;
	CreateTextStyleRecord(style, &record);
	Fixed width = 0;
	ULong i = 0;
	long length = label->fLength;
	FPoint origin;
	origin.x = 0;
	origin.y = 0;
	while (width < 50 * 0x10000 && (long) i < length)
	{
		TextBoundsInfo info;
		MeasureRichString(*label, i, 1, &record, origin, nil, &info);
		width += info.fWidth;
		i++;
	}
	if ((long) i < length)
	{
		UniChar ellipsis[1];
		ellipsis[0] = U_CONST_CHAR(0xc9);
		TRichString rest(ellipsis, 1);
		label->MungeRange(i, length - i, &rest, 0, 1);
	}
	if (record.fPattern != nil)
		DisposePattern(record.fPattern);
}


// ROM 0x000a08a4 CalcIconDimensions__10TClipboardSFP11TRichStringPsT2RC6RefVar
// How wide and how tall the icon's paragraph has to be: the label's
// advance rounded to a pixel, and the tallest ascent plus the deepest
// descent of the style and of every ink word in it.
//
// (The ROM makes a style record per ink word into the same variable and
// disposes only the last one's pattern, so the patterns of the earlier
// ones are lost - ported as the ROM has it.)
void
TClipboard::CalcIconDimensions(TRichString* label, short* width, short* height, RefArg style)
{
	StyleRecord record;
	CreateTextStyleRecord(style, &record);
	FontInfo font;
	font.ascent = 0;
	font.descent = 0;
	font.widMax = 0;
	font.leading = 0;
	TextBoundsInfo info;
	FPoint origin;
	origin.x = 0;
	origin.y = 0;
	MeasureRichString(*label, 0, label->fLength, &record, origin, nil, &info);
	*width = (short) ((info.fWidth + 0x8000) >> 16);
	GetStyleFontInfo(&record, &font);
	short ascent = (short) font.ascent;
	short descent = (short) font.descent;
	long words = label->NumInkWords();
	for (long i = 0; i < words; i++)
	{
		RefVar inkWord(label->CloneInkWordNo(i));
		CreateTextStyleRecord(inkWord, &record);
		GetStyleFontInfo(&record, &font);
		if ((short) font.ascent > ascent)
			ascent = (short) font.ascent;
		if ((short) font.descent > descent)
			descent = (short) font.descent;
	}
	*height = (short) (ascent + descent);
	if (record.fPattern != nil)
		DisposePattern(record.fPattern);
}


// ROM 0x000a0a50 CalcIconBounds__10TClipboardSFsT1RC6TPointP5TRect
// The icon's bounds in the application area's coordinates: a rectangle
// of that size with the point at the middle of its top edge, clamped to
// the application area.  A point at or left of the middle of the icon's
// own width goes against the left edge; one too far down goes against
// the bottom.
void
TClipboard::CalcIconBounds(short width, short height, const Point& where, Rect* bounds)
{
	SetRect(bounds, 0, 0, width, height);
	Point pt = where;
	Rect appArea;
	GetAppAreaBounds(&appArea);
	if (appArea.left + width / 2 >= pt.h)
		pt.h = 0;
	else if (appArea.right - width > pt.h)
		pt.h = (short) (pt.h - (appArea.left + width / 2));
	else
		pt.h = (short) ((appArea.right - appArea.left) - width);
	if (pt.v <= appArea.top)
		pt.v = 0;
	else if ((appArea.bottom - appArea.top) - height <= pt.v)
		pt.v = (short) ((appArea.bottom - appArea.top) - height);
	OffsetRect(bounds, pt.h, pt.v);
}


// ROM 0x0009fcf0 CreateLabelForm__10TClipboardSFRC9TDragInfoRC6RefVarRC6TPoint
// The template of the clipping's icon: a protoParagraph of the drag's
// label in the user's font, laid at the point (a v of -0x8000 means "no
// point given": 72 pixels down the left edge).  The label is the first
// item of the drag that has one, or the text of the frame the caller
// passes (the first 'text item's data), or the string "data"; tabs and
// returns in it become spaces so that it stays on one line.
Ref
TClipboard::CreateLabelForm(const TDragInfo& dragInfo, RefArg label, const Point& where)
{
	Point pt;
	if (where.v == -0x8000)
	{
		pt.v = 72;
		pt.h = 0;
	}
	else
		pt = where;

	RefVar text, styles;
	long count = dragInfo.Count();
	for (long i = 0; i < count; i++)
	{
		text = dragInfo.GetItemDragLabel(i);
		if (NOTNIL(text))
			break;
	}
	if (ISNIL(text))
	{
		if (ISNIL(label))
			text = MAKEMAGICPTR(63);				// the ROM's string "data"
		else
		{
			text = GetFrameSlotRef(label, RSSYMtext);
			styles = GetFrameSlotRef(label, RSSYMstyles);
		}
	}
	text = ExtractRichStringFromParaSlots(text, styles, 0, 16);

	RefVar fontParms(Clone(RefVar(Rcanonicalfontparms)));
	RefVar userFont(GetPreference(RSSYMuserfont));
	SetFrameSlot(fontParms, RSSYMfamily, RefVar(GetFontFamilySym(userFont)));
	SetFrameSlot(fontParms, RSSYMsize, RefVar(MAKEINT(12)));
	SetFrameSlot(fontParms, RSSYMface, RefVar(MAKEINT(0)));
	SetFrameSlot(fontParms, RSSYMpensize, RefVar(MAKEINT(2)));

	TRichString rich(text);
	TruncateLabel(&rich, fontParms);
	UniChar tab = U_CONST_CHAR(9);
	UniChar cr = U_CONST_CHAR(13);
	UniChar space = U_CONST_CHAR(32);
	for (long i = 0; i < rich.fLength; i++)
	{
		UniChar c = rich.GetChar(i);
		if (c == tab || c == cr)
			rich.SetChar(i, space);
	}

	short width, height;
	CalcIconDimensions(&rich, &width, &height, fontParms);
	Rect bounds;
	CalcIconBounds(width, height, pt, &bounds);

	RefVar form(Clone(RefVar(Rstarterparagraph)));
	SetFrameSlot(form, RSSYMviewbounds, RefVar(ToObject(bounds)));
	SetFrameSlot(form, RSSYMtext, RefVar(rich.MakeParagraphTextSlot()));
	RefVar styleSlots(rich.MakeParagraphStylesSlot(fontParms));
	long slots = Length(styleSlots);
	for (long i = 1; i < slots; i += 2)
		SetArraySlot(styleSlots, i, RefVar(SetFontParms(RefVar(GetArraySlotRef(styleSlots, i)), fontParms)));
	SetFrameSlot(form, RSSYMstyles, styleSlots);
	// vClipboard | vClickable | vFloating | vReadOnly | vVisible
	SetFrameSlot(form, RSSYMviewflags, RefVar(MAKEINT(0x04000243)));
	SetFrameSlot(form, RSSYMtextflags, RefVar(MAKEINT(0)));
	SetFrameSlot(form, RSSYMviewformat, RefVar(MAKEINT(vfFillCustom)));
	SetFrameSlot(form, RSSYMviewfillpattern, RefVar(MAKEINT(0x10000000)));
	SetFrameSlot(form, RSSYMviewtransfermode, RefVar(MAKEINT(srcBic)));
	SetFrameSlot(form, RSSYMreorienttoscreen, RefVar(MakeCFunction((void*) FReOrientLabelForm, 0, "")));

	Rect appArea;
	GetAppAreaBounds(&appArea);
	long pin = bounds.left < 1 ? 1 : 0;
	if (bounds.top < 1)
		pin |= 2;
	if (appArea.right - appArea.left <= bounds.right)
		pin |= 4;
	if (appArea.bottom - appArea.top <= bounds.bottom)
		pin |= 8;
	SetFrameSlot(form, RSSYMpin, RefVar(MAKEINT(pin)));
	return form;
}


// ROM 0x0009f978 FReOrientLabelForm__FRC6RefVar
// The icon's ReOrientToScreen: the edges of the application area its
// `pin` says it was against are found again on the screen as it is now,
// and the icon is put back against them.  The button bar's own edge is
// the exception - an icon pinned to the edge the bar is on is moved to
// the far side instead, so that it does not end up under the bar.
Ref
FReOrientLabelForm(RefArg form)
{
	TView* view = GetView(form);
	long pin = RINT(GetProtoVariable(form, RSSYMpin, nil));
	Rect bounds = view->viewBounds;
	short height = (short) (bounds.bottom - bounds.top);
	short width = (short) (bounds.right - bounds.left);
	RefVar params(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdisplayparams));
	RefVar where(GetProtoVariable(params, RSSYMbuttonbarposition, nil));
	Rect appArea;
	GetAppAreaBounds(&appArea);

	if (pin & 1)
	{
		if (EQ(where, RSSYMleft))
		{
			bounds.right = (short) (appArea.right - appArea.left);
			bounds.left = (short) (bounds.right - width);
		}
		else
		{
			bounds.left = 0;
			bounds.right = (short) (bounds.left + width);
		}
	}
	if (pin & 2)
	{
		if (EQ(where, RSSYMtop))
		{
			bounds.bottom = (short) (appArea.bottom - appArea.top);
			bounds.top = (short) (bounds.bottom - height);
		}
		else
		{
			bounds.top = 0;
			bounds.bottom = (short) (bounds.top + height);
		}
	}
	if (pin & 4)
	{
		if (EQ(where, RSSYMright))
		{
			bounds.left = appArea.left;
			bounds.right = (short) (bounds.left + width);
		}
		else
		{
			bounds.right = (short) (appArea.right - appArea.left);
			bounds.left = (short) (bounds.right - width);
		}
	}
	if (pin & 8)
	{
		if (EQ(where, RSSYMbottom))
		{
			bounds.top = appArea.top;
			bounds.bottom = (short) (bounds.top + height);
		}
		else
		{
			bounds.bottom = (short) (appArea.bottom - appArea.top);
			bounds.top = (short) (bounds.bottom - height);
		}
	}

	long newPin = bounds.left <= 0 ? 1 : 0;
	if (bounds.top <= 0)
		newPin |= 2;
	if (appArea.right - appArea.left <= bounds.right)
		newPin |= 4;
	if (appArea.bottom - appArea.top <= bounds.bottom)
		newPin |= 8;
	SetFrameSlot(form, RSSYMpin, RefVar(MAKEINT(newPin)));
	view->WriteBounds(bounds);
	return TRUEREF;
}


// ROM 0x000a02d8 GetClipboardDataInfo__10TClipboardFP9TDragInfo
// The drag that takes the clipping off the clipboard: one item per item
// the clipping holds, whose dragRef is that item's index (GetDropData
// looks it up again) and whose types are the ones it was made with.
void
TClipboard::GetClipboardDataInfo(TDragInfo* dragInfo)
{
	long count = Length(fTypes);
	for (long i = 0; i < count; i++)
	{
		dragInfo->AddDragItem();
		dragInfo->SetItemDragRef(i, RefVar(MAKEINT(i)));
		dragInfo->SetItemDragTypes(i, RefVar(GetArraySlotRef(fTypes, i)));
	}
}


// ROM 0x000a0380 DragFromClipboard__10TClipboardFP13TStrokePublic
// The pen picking the clipping up: the clipping's own items are made
// into a drag and dragged from where the picture is drawn.  A second
// stroke within 80 ticks of the last one is a copy rather than a move,
// which is how the clipping is left behind by tapping it twice.
Boolean
TClipboard::DragFromClipboard(TStrokePublic* stroke)
{
	FPlaySound(RefVar(), RefVar(Rremovesound));
	TDragInfo dragInfo(0);
	GetClipboardDataInfo(&dragInfo);
	Rect box;
	CalcDataBitsBounds(&box);
	ULong now = Ticks();
	ULong since = now - gLastClipboardDragTicks;
	gLastClipboardDragTicks = now;
	return DragAndDrop(stroke, box, &box, nil, since < 80, dragInfo, nil);
}


// ROM 0x000a0454 DrawDragData__10TClipboardFRC5TRect
// The clipping drawn where it is being dragged: the picture taken of
// what was dragged, exclusive-ored into the rectangle.  A clipping made
// when the heap was too full to hold a picture has no `bits`, and its
// outline is framed in gray instead.
void
TClipboard::DrawDragData(const Rect& bounds)
{
	if (ISNIL(GetProtoVariable(fContext, RSSYMbits, nil)))
	{
		PenState pen;
		GetPenState(&pen);
		PenNormal();
		SetFgPattern(GetStdPattern(grayPat));
		PenSize(2, 2);
		FrameRect(&bounds);
		SetPenState(&pen);
		return;
	}
	PixelMap map;
	TBits::InitBitMap(fBounds, &map);
	LockRefArg(fBits);
	map.baseAddr = (Ptr) BinaryData(fBits);
	map.pixMapFlags |= kPixMapPtr;
	TBits bits;
	newton_try
	{
		bits.Constructor(map);
		bits.Draw(fBounds, bounds, srcXor, nil);
	}
	newton_catch_all
	{
		UnlockRefArg(fBits);
		rethrow;
	}
	end_try;
	UnlockRefArg(fBits);
}


// ROM 0x000a0634 GetDropData__10TClipboardFRC6RefVarT1
// The data of the clipping's item that the drag asks for: the dragRef is
// the item's index, and the type picks which of that item's data to give
// back.  It is deep-cloned, so the clipping keeps its own copy however
// the taker treats it.
Ref
TClipboard::GetDropData(RefArg dragType, RefArg dragRef)
{
	long item = RINT(dragRef);
	RefVar types(GetArraySlotRef(fTypes, item));
	RefVar which(FSetContains(RefVar(), types, dragType));
	RefVar data;
	if (NOTNIL(which))
	{
		RefVar itemData(GetArraySlotRef(fData, item));
		data = GetArraySlotRef(itemData, RINT(which));
		data = DeepClone(data);
	}
	return data;
}


// ROM 0x000a0744 EndDrag__10TClipboardFRC9TDragInfoP5TViewRC6TPointN23Uc
// The drag off the clipboard finished: the start point is moved from the
// picture's place on the screen into the clipping's own coordinates, the
// target's hilites are dropped, and the drag is handed on to TView.
// Unless it was a copy, the clipping and its icon are then taken off the
// root view - the clipping has moved into whatever took it.
void
TClipboard::EndDrag(const TDragInfo& dragInfo, TView* target, const Point& startPt, const Point& dropPt, const Point& dragPt, Boolean copy)
{
	Rect box;
	CalcDataBitsBounds(&box);
	Point start;
	start.h = (short) (fBounds.left - box.left + startPt.h);
	start.v = (short) (fBounds.top - box.top + startPt.v);
	target->RemoveAllHilites();
	TView::EndDrag(dragInfo, target, start, dropPt, dragPt, copy);
	if (!copy)
	{
		TView* icon = gRootView->GetClipboardIcon(this);
		gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, gRootView, fId)));
		gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, gRootView, icon->fId)));
	}
}


/*------------------------------------------------------------------------------
	T h e   c l i p b o a r d   f r o m   a   s c r i p t
------------------------------------------------------------------------------*/

// ROM 0x001ee538 FGetClipboard
// GetClipboard(): the front clipping as a frame of its label (the icon's
// text), its types, its data, the bounds it came from and its picture -
// or nil when there is none.
static Ref
FGetClipboard(RefArg /*rcvr*/)
{
	TView* clipboard = gRootView->GetClipboard();
	TView* icon = gRootView->GetClipboardIcon();
	RefVar result;
	if (clipboard != nil && icon != nil)
	{
		RefVar context(clipboard->fContext);
		RefVar iconContext(icon->fContext);
		result = AllocateFrame();
		SetFrameSlot(result, RSSYMlabel, RefVar(GetProtoVariable(iconContext, RSSYMtext, nil)));
		SetFrameSlot(result, RSSYMtypes, RefVar(GetProtoVariable(context, RSSYMtypes, nil)));
		SetFrameSlot(result, RSSYMdata, RefVar(GetProtoVariable(context, RSSYMdata, nil)));
		SetFrameSlot(result, RSSYMbounds, RefVar(GetProtoVariable(context, RSSYMbounds, nil)));
		SetFrameSlot(result, RSSYMbits, RefVar(GetProtoVariable(context, RSSYMbits, nil)));
	}
	return result;
}


// ROM 0x001ee2bc FSetClipboard
// SetClipboard(clipping): the frame GetClipboard answers put back as the
// front clipping, or, for nil, the front clipping thrown away.  The
// clipping's `xy` frame says where its icon goes.  ==> what it was given.
static Ref
FSetClipboard(RefArg /*rcvr*/, RefArg clipping)
{
	if (ISNIL(clipping))
	{
		gRootView->RemoveClipboard();
		return clipping;
	}
	RefVar xy(GetProtoVariable(clipping, RSSYMxy, nil));
	Point pt;
	if (ISNIL(xy))
	{
		pt.v = -0x8000;
		pt.h = 0;
	}
	else
	{
		pt.h = (short) RINT(GetFrameSlotRef(xy, RSSYMx));
		pt.v = (short) RINT(GetFrameSlotRef(xy, RSSYMy));
	}
	TDragInfo dragInfo(0);
	RefVar label(AllocateFrame());
	SetFrameSlot(label, RSSYMtext, RefVar(GetProtoVariable(clipping, RSSYMlabel, nil)));
	RefVar labelForm(TClipboard::CreateLabelForm(dragInfo, label, pt));
	RefVar form(Clone(RefVar(Rstarterclipboard)));
	SetFrameSlot(form, RSSYMtypes, RefVar(GetProtoVariable(clipping, RSSYMtypes, nil)));
	SetFrameSlot(form, RSSYMdata, RefVar(GetProtoVariable(clipping, RSSYMdata, nil)));
	SetFrameSlot(form, RSSYMbounds, RefVar(GetProtoVariable(clipping, RSSYMbounds, nil)));
	SetFrameSlot(form, RSSYMbits, RefVar(GetProtoVariable(clipping, RSSYMbits, nil)));
	gRootView->AddClipboard(form, labelForm);
	return clipping;
}


// ROM 0x001ee268 FClipboardCommand
// view:ClipboardCommand(command) - one of the editing commands (cut,
// copy, paste, ...) sent to the view; ==> true when it took it.
static Ref
FClipboardCommand(RefArg rcvr, RefArg command)
{
	TView* view = FailGetView(rcvr);
	return MAKEBOOLEAN(view->DoEditCommand(RINT(command)));
}


// ROM 0x001b583c FGetClipboardIcon
// GetClipboardIcon(): the contexts of the views that draw the clippings'
// icons, front first (the ROM's function is TRootView::GetClipboardIcons
// under another name), or nil when there are none.
static Ref
FGetClipboardIcon(RefArg /*rcvr*/)
{
	return gRootView->GetClipboardIcons();
}


void
RegisterClipboardNatives(void)
{
	RegisterNativeFunction("FGetClipboard", (void*) FGetClipboard, 0);
	RegisterNativeFunction("FSetClipboard", (void*) FSetClipboard, 1);
	RegisterNativeFunction("FClipboardCommand", (void*) FClipboardCommand, 1);
	RegisterNativeFunction("FGetClipboardIcon", (void*) FGetClipboardIcon, 0);
}
