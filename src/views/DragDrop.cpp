/*
	File:		views/DragDrop.cpp

	Contains:	TDragInfo, the data carried by a drag-and-drop.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "DragDrop.h"
#include "View.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "RSSymbols.h"


// the index in `set` of the first element also in `other` (EQ), -1 for
// none (the ROM uses the SetOverlaps native)
static long
SetOverlapIndex(RefArg set, RefArg other)
{
	if (ISNIL(set) || ISNIL(other))
		return -1;
	long n = Length(set), m = Length(other);
	for (long i = 0; i < n; i++)
		for (long j = 0; j < m; j++)
			if (EQRef(GetArraySlotRef(set, i), GetArraySlotRef(other, j)))
				return i;
	return -1;
}


// a TPoint as a `{x, y}` frame (the ROM's canonicalPoint), the way the
// drop scripts get a point (ROM 0x000a1798)
Ref
PointToFrame(const Point& pt)
{
	RefVar frame(Clone(RefVar(Rcanonicalpoint)));
	SetFrameSlot(frame, RSSYMx, RefVar(MAKEINT(pt.h)));
	SetFrameSlot(frame, RSSYMy, RefVar(MAKEINT(pt.v)));
	return frame;
}


// ROM 0x000a0b78 __ct__9TDragInfoFl
TDragInfo::TDragInfo(long numItems)
{
	fItems = MakeArray(numItems);		// numItems nil slots
}


// ROM 0x000a0c08 __ct__9TDragInfoFRC6RefVar
TDragInfo::TDragInfo(RefArg items)
{
	fItems = items;
}


// ROM 0x000a0c5c __ct__9TDragInfoFRC6RefVarN21
TDragInfo::TDragInfo(RefArg types, RefArg dragRef, RefArg label)
{
	fItems = MakeArray(0);
	AddDragItem(types, dragRef, label);
}


// ROM 0x000a0d6c GetItemTypes__9TDragInfoCFl
Ref
TDragInfo::GetItemTypes(long i) const
{
	return GetFrameSlotRef(RefVar(GetArraySlotRef(RefVar(fItems), i)), RSSYMtypes);
}


// ROM 0x000a0db8 GetItemIndType__9TDragInfoCFlT1
Ref
TDragInfo::GetItemIndType(long i, long j) const
{
	return GetArraySlotRef(RefVar(GetItemTypes(i)), j);
}


// ROM 0x000a0df8 GetItemDragRef__9TDragInfoCFl
Ref
TDragInfo::GetItemDragRef(long i) const
{
	return GetFrameSlotRef(RefVar(GetArraySlotRef(RefVar(fItems), i)), RSSYMdragref);
}


// ROM 0x000a0e44 GetItemDragLabel__9TDragInfoCFl
Ref
TDragInfo::GetItemDragLabel(long i) const
{
	return GetFrameSlotRef(RefVar(GetArraySlotRef(RefVar(fItems), i)), RSSYMlabel);
}


// ROM 0x000a0e90 GetItemView__9TDragInfoCFl
TView*
TDragInfo::GetItemView(long i) const
{
	RefVar view(GetFrameSlotRef(RefVar(GetArraySlotRef(RefVar(fItems), i)), RSSYMview));
	return ISNIL(view) ? nil : FailGetView(view);
}


// ROM 0x000a0f1c SetItemView__9TDragInfoFlP5TView
void
TDragInfo::SetItemView(long i, TView* view)
{
	RefVar frame(CreateItemFrame(i));
	SetFrameSlot(frame, RSSYMview, RefVar(view->fContext));
}


// ROM 0x000a0ccc CheckTypes__9TDragInfoCFRC6RefVar
// True when every item offers at least one of the accepted types.
Boolean
TDragInfo::CheckTypes(RefArg acceptedTypes) const
{
	for (long i = Count() - 1; i >= 0; i--)
		if (SetOverlapIndex(acceptedTypes, RefVar(GetItemTypes(i))) < 0)
			return false;
	return true;
}


// ROM 0x000a0f5c FindType__9TDragInfoCFlRC6RefVar
// The item's first type that is in the given set, nil for none.
Ref
TDragInfo::FindType(long i, RefArg types) const
{
	long idx = SetOverlapIndex(types, RefVar(GetItemTypes(i)));
	return idx < 0 ? NILREF : GetArraySlotRef(types, idx);
}


// ROM 0x000a1080 CreateItemFrame__9TDragInfoFl
// The item's frame, made (a clone of canonicalDragItem) when the slot is
// still nil.
Ref
TDragInfo::CreateItemFrame(long i)
{
	RefVar item(GetArraySlotRef(RefVar(fItems), i));
	if (ISNIL(item))
	{
		item = Clone(RefVar(Rcanonicaldragitem));
		SetArraySlot(RefVar(fItems), i, item);
	}
	return item;
}


// ROM 0x000a10ec SetItemDragRef__9TDragInfoFlRC6RefVar
void
TDragInfo::SetItemDragRef(long i, RefArg dragRef)
{
	SetFrameSlot(RefVar(CreateItemFrame(i)), RSSYMdragref, dragRef);
}


// ROM 0x000a112c SetItemDragLabel__9TDragInfoFlRC6RefVar
void
TDragInfo::SetItemDragLabel(long i, RefArg label)
{
	SetFrameSlot(RefVar(CreateItemFrame(i)), RSSYMlabel, label);
}


// ROM 0x000a116c SetItemDragTypes__9TDragInfoFlRC6RefVar
void
TDragInfo::SetItemDragTypes(long i, RefArg types)
{
	SetFrameSlot(RefVar(CreateItemFrame(i)), RSSYMtypes, types);
}


// ROM 0x000a11ac AddItemDragType__9TDragInfoFlRC6RefVar
// A type (or an array of types) added to the item's types.
void
TDragInfo::AddItemDragType(long i, RefArg type)
{
	RefVar item(CreateItemFrame(i));
	RefVar types(GetFrameSlotRef(item, RSSYMtypes));
	if (!IsArray(type))
	{
		if (ISNIL(types))
		{
			types = MakeArray(0);
			SetFrameSlot(item, RSSYMtypes, types);
		}
		AddArraySlot(types, type);
	}
	else
	{
		// union: every type of the array, each once
		if (ISNIL(types))
		{
			types = MakeArray(0);
			SetFrameSlot(item, RSSYMtypes, types);
		}
		long n = Length(type);
		for (long k = 0; k < n; k++)
		{
			RefVar t(GetArraySlotRef(type, k));
			Boolean present = false;
			long m = Length(types);
			for (long j = 0; j < m && !present; j++)
				if (EQRef(GetArraySlotRef(types, j), t))
					present = true;
			if (!present)
				AddArraySlot(types, t);
		}
	}
}


// ROM 0x000a1330 AddDragItem__9TDragInfoFv
long
TDragInfo::AddDragItem(void)
{
	AddArraySlot(RefVar(fItems), RefVar(NILREF));
	return Count() - 1;
}


// ROM 0x000a1378 AddDragItem__9TDragInfoFRC6RefVarN21
long
TDragInfo::AddDragItem(RefArg types, RefArg dragRef, RefArg label)
{
	long i = AddDragItem();
	SetItemDragRef(i, dragRef);
	AddItemDragType(i, types);
	if (NOTNIL(label))
		SetItemDragLabel(i, label);
	return i;
}


// ROM 0x0009d544 FindDropViewDeep__FP5TViewRC9TDragInfoRC6TPoint
// From the view under the pen up to the enclosing window (or the root),
// the first that is not read-only and accepts the drag (AcceptDrop).
TView*
FindDropViewDeep(TView* under, const TDragInfo& dragInfo, const Point& pt)
{
	for (TView* v = under; v != nil; )
	{
		if ((v->fFlags & (vReadOnly | vWriteProtected)) == 0 && v->AcceptDrop(dragInfo, pt))
			return v;
		if ((v->fViewFormat & 0xf) != 0 || v->HasVisRgn() || v->fParent == (TView*) gRootView)
			break;
		v = v->fParent;
	}
	return nil;
}
