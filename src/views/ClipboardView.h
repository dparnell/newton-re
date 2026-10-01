/*
	File:		views/ClipboardView.h

	Contains:	TClipboard (clClipboard, 101): the clipping the Newton keeps
				when something is dragged out of a view and let go on the
				background.  A clipping is two views, put on the root
				together: the *clipboard* itself, which holds the dragged
				items and draws the picture taken of them, and its *icon*, a
				small paragraph of the clipping's label that sits at the
				edge of the application area and is what the pen picks the
				clipping up by.  The root view keeps them as two parallel
				arrays (`TRootView::AddClipboard`), so the icon at index i
				belongs to the clipboard at index i; `clipboardDepth` says
				how many clippings are kept, the oldest falling off the end.

				`NewClipboard` is how one is made: every item of the drag is
				asked of the source view for its data of each of its types
				(`GetDropData`), the bounds of anything that carries them
				are moved into the clipping's own coordinates, and a
				picture of the dragged view (`GetClipboardDataBits`) is put
				in the clipping's `bits` slot.  `CreateLabelForm` makes the
				icon: the drag's label, cut at 50 pixels with an ellipsis
				(`TruncateLabel`), laid out in the user font as a
				protoParagraph pinned to whichever edges of the application
				area it touches - `FReOrientLabelForm` is the C function the
				form carries as its ReOrientToScreen, which moves it to the
				same edges when the screen is turned round.

				`DragFromClipboard` is the other direction: the pen taking
				the clipping off the clipboard, which is an ordinary drag of
				a TDragInfo whose items are the ones the clipping holds, and
				whose `EndDrag` takes both views off the root again unless
				the drag was a copy.

				A script sees the front clipping through GetClipboard and
				SetClipboard, and a view answers ClipboardCommand (cut,
				copy, paste) through TView::DoEditCommand.

				The ROM's object is 0x44 bytes.

				`PointOnClipboard` is asked by the drag's pen tracking
				(TView::Drag, DragDrop.h) whether the pen was let go at
				the screen's edge.

	Reconstructed from the MP2x00 US ROM (0x0009e2b0-0x0009e5f0,
	0x0009ca58-0x0009cb80, 0x0009edfc-0x000a0b78, 0x001b37fc-0x001b38e0,
	0x001b58bc-0x001b5b70, 0x001ee268-0x001ee640); each function cites its
	origin.
*/

#ifndef __CLIPBOARDVIEW_H
#define __CLIPBOARDVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif

class TDragInfo;
class TRichString;
class TStrokePublic;

// The point is outside the part of the application area a clipping may be
// dropped on - true says the drag ended on the background rather than in
// a view.  The button bar's edge is not counted, so a clipping may be let
// go over it.
Boolean	PointOnClipboard(const Point& pt, const Rect& appArea, RefArg buttonBarPosition);	// ROM 0x0009e2b0 PointOnClipboard__FRC6TPointR5TRectRC6RefVar
Boolean	PointOnClipboard(const Point& pt);									// ROM 0x0009ef64 PointOnClipboard__FRC6TPoint - over the application area inset by five pixels

// the two helpers the clipping's items go through
void	OffsetBoundsRef(RefArg frame, const Point& by);						// ROM 0x0009ca58 OffsetBoundsRef__FRC6RefVarRC6TPoint - the frame's `viewBounds` moved
void	CheckViewBounds(RefArg dragType, RefArg form);						// ROM 0x0009cb00 CheckViewBounds__FRC6RefVarT1 - a 'text item without viewBounds gets an empty one

Ref		FReOrientLabelForm(RefArg form);									// ROM 0x0009f978 FReOrientLabelForm__FRC6RefVar - the icon moved to the same edges of a turned screen

class TClipboard : public TView
{
public:
	virtual			~TClipboard();										// ROM 0x0009ef0c __dt__10TClipboardFv
	virtual long	ClassID(void) const;								// ROM 0x0009edfc ClassID__10TClipboardCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x0009ee04 DerivedFrom__10TClipboardCFl
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x0009ee38 Constructor__10TClipboardFRC6RefVarP5TView
	virtual void	DrawDragData(const Rect& bounds);					// ROM 0x000a0454 DrawDragData__10TClipboardFRC5TRect
	virtual Ref		GetDropData(RefArg dragType, RefArg dragRef);		// ROM 0x000a0634 GetDropData__10TClipboardFRC6RefVarT1
	virtual void	EndDrag(const TDragInfo& dragInfo, TView* target, const Point& startPt, const Point& dropPt, const Point& dragPt, Boolean copy);	// ROM 0x000a0744 EndDrag__10TClipboardFRC9TDragInfoP5TViewRC6TPointN23Uc

	static Ref		AllocateClipboardBits(const Rect& bounds, PixelMap* map);	// ROM 0x0009f0e0 AllocateClipboardBits__10TClipboardSFRC5TRectP8PixelMap - a 'bits binary big enough, or nil when the heap is full
	static void		NewClipboard(const TDragInfo& dragInfo, TView* view, const Rect& bounds, const Point* where);	// ROM 0x0009f188 NewClipboard__10TClipboardSFRC9TDragInfoP5TViewRC5TRectPC6TPoint
	void			MoveIcon(const Point& pt);							// ROM 0x0009f568 MoveIcon__10TClipboardFRC6TPoint
	void			CalcDataBitsBounds(Rect* bounds);					// ROM 0x0009f6dc CalcDataBitsBounds__10TClipboardFP5TRect
	static void		TruncateLabel(TRichString* label, RefArg style);		// ROM 0x0009f84c TruncateLabel__10TClipboardSFP11TRichStringRC6RefVar
	static Ref		CreateLabelForm(const TDragInfo& dragInfo, RefArg label, const Point& where);	// ROM 0x0009fcf0 CreateLabelForm__10TClipboardSFRC9TDragInfoRC6RefVarRC6TPoint
	void			GetClipboardDataInfo(TDragInfo* dragInfo);			// ROM 0x000a02d8 GetClipboardDataInfo__10TClipboardFP9TDragInfo
	Boolean			DragFromClipboard(TStrokePublic* stroke);			// ROM 0x000a0380 DragFromClipboard__10TClipboardFP13TStrokePublic
	static void		CalcIconDimensions(TRichString* label, short* width, short* height, RefArg style);	// ROM 0x000a08a4 CalcIconDimensions__10TClipboardSFP11TRichStringPsT2RC6RefVar
	static void		CalcIconBounds(short width, short height, const Point& where, Rect* bounds);	// ROM 0x000a0a50 CalcIconBounds__10TClipboardSFsT1RC6TPointP5TRect

	RefStruct		fTypes;			// +0x30  the context's `types`: one array of drag types per item
	RefStruct		fData;			// +0x34  the context's `data`: one array of data per item, parallel to the types
	Rect			fBounds;		// +0x38  the context's `bounds`, in the application area's coordinates
	RefStruct		fBits;			// +0x40  the context's `bits`: the picture taken of what was dragged
};

void	RegisterClipboardNatives(void);

#endif	/* __CLIPBOARDVIEW_H */
