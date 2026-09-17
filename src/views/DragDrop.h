/*
	File:		views/DragDrop.h

	Contains:	TDragInfo, the data of a drag-and-drop.  A drag carries one or
				more items; each is a frame (the ROM's canonicalDragItem)
				with the item's `types` (the drag types it offers, symbols),
				its `dragRef` (the data), a `label` and the source `view`.
				A view starting a drag fills a TDragInfo (its `AddDragInfo`
				adds the items); the drop target, found under the pen, is
				asked which of the types it accepts (`GetSupportedDropTypes`,
				`CheckTypes`) and given the data (`GetDropData` from the
				source, then the target's `Drop` runs its viewDropScript).

				The ROM's object is a single RefHandle wrapping the items
				array; the reconstruction keeps the array in a RefStruct.

	Reconstructed from the MP2100 D ROM (0x000a1d78-0x000a2700); each
	function cites its origin.
*/

#ifndef __DRAGDROP_H
#define __DRAGDROP_H

#ifndef __FRAMES_H
#include "Frames.h"
#endif

class TView;
class TDragInfo;

Ref		PointToFrame(const Point& pt);		// a `{x, y}` frame (ROM 0x000a1798)
TView*	FindDropViewDeep(TView* under, const TDragInfo& dragInfo, const Point& pt);	// ROM 0x0009e744 FindDropViewDeep__FP5TViewRC9TDragInfoRC6TPoint - the first view from under up that accepts the drag

class TDragInfo
{
public:
				TDragInfo(long numItems);							// ROM 0x000a1d78 __ct__9TDragInfoFl - numItems empty item slots
				TDragInfo(RefArg items);							// ROM 0x000a1e08 __ct__9TDragInfoFRC6RefVar - wrap an existing items array
				TDragInfo(RefArg types, RefArg dragRef, RefArg label);	// ROM 0x000a1e5c __ct__9TDragInfoFRC6RefVarN21 - one item

	Ref			GetItems(void) const		{ return fItems; }
	long		Count(void) const			{ return NOTNIL((Ref) fItems) ? Length(RefVar(fItems)) : 0; }

	Boolean		CheckTypes(RefArg acceptedTypes) const;				// ROM 0x000a1ecc CheckTypes__9TDragInfoCFRC6RefVar - every item's types overlap the accepted set
	Ref			GetItemTypes(long i) const;							// ROM 0x000a1f6c GetItemTypes__9TDragInfoCFl
	Ref			GetItemIndType(long i, long j) const;				// ROM 0x000a1fb8 GetItemIndType__9TDragInfoCFlT1
	Ref			GetItemDragRef(long i) const;						// ROM 0x000a1ff8 GetItemDragRef__9TDragInfoCFl
	Ref			GetItemDragLabel(long i) const;						// ROM 0x000a2044 GetItemDragLabel__9TDragInfoCFl
	TView*		GetItemView(long i) const;							// ROM 0x000a2090 GetItemView__9TDragInfoCFl
	void		SetItemView(long i, TView* view);					// ROM 0x000a211c SetItemView__9TDragInfoFlP5TView
	Ref			FindType(long i, RefArg types) const;				// ROM 0x000a215c FindType__9TDragInfoCFlRC6RefVar - the item's first type in the given set
	Ref			CreateItemFrame(long i);							// ROM 0x000a2280 CreateItemFrame__9TDragInfoFl
	void		SetItemDragRef(long i, RefArg dragRef);				// ROM 0x000a22ec SetItemDragRef__9TDragInfoFlRC6RefVar
	void		SetItemDragLabel(long i, RefArg label);				// ROM 0x000a232c SetItemDragLabel__9TDragInfoFlRC6RefVar
	void		SetItemDragTypes(long i, RefArg types);				// ROM 0x000a236c SetItemDragTypes__9TDragInfoFlRC6RefVar
	void		AddItemDragType(long i, RefArg type);				// ROM 0x000a23ac AddItemDragType__9TDragInfoFlRC6RefVar
	long		AddDragItem(void);									// ROM 0x000a2530 AddDragItem__9TDragInfoFv - an empty item; ==> its index
	long		AddDragItem(RefArg types, RefArg dragRef, RefArg label);	// ROM 0x000a2578 AddDragItem__9TDragInfoFRC6RefVarN21

private:
	RefStruct	fItems;			// the items array (the ROM keeps it in a RefHandle)
};

#endif	/* __DRAGDROP_H */
