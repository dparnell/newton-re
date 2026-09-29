/*
	File:		text/TXView.h

	Contains:	TXView (clTXView, 108): the view a protoTXView is - a
				document of the text engine (Textension) in a view, with
				the engine's handlers made for the Newton: the display
				draws through the view's visible region (TXNewtDisplay),
				the hilite keeps the root view's key view and caret in step
				(TXNewtHilite), and the pen is a stroke (TXNewtPen).

				The view makes its document when it is set up
				(`SetupDone` -> `CreateNewTextension`: the characters in a
				string, TXBinaryChars, or on the store SetStore gave it,
				TXVBOChars),
				with the page's size and margins from SetGeometry and the
				view's font as the default style.  The first TXView the
				machine makes starts the engine (TextensionStart, the
				Newton's default text run, graphics run and ruler, and the
				stream factory).

				Editing is undoable: typing, a replacement, a restyle and a
				move are edit commands (TXCommand.h) kept in a C object
				frame and posted to the application's undo stack as an
				0xd3 command to this view (`PostUndo`), which Undo sends
				back (`RealDoCommand` -> `ExecuteCommand`, which undoes it
				the second time and redoes it the third).  Consecutive keys
				go into one command (`NewKey`, `GetCurrentKeyCommand`).

				A script sees the text as frames (TXNewtContainer.h):
				`Replace` puts one in over a range, `GetRangeData` answers
				one, `Externalize`/`Internalize` save and restore the whole
				document - the characters, the styles and rulers, and the
				line breaks already worked out (TXFormatter's stream).

				The 39 protoTXView methods are TXViewNatives.cpp.

				Drag and drop and the clipboard: the selection leaves as drag
				items ('text runs and pictures), comes in as a frame of text
				and styles; a scrub deletes what it covers and a caret gesture
				puts a space or a return in, or takes a character out.

				With a store (SetStore, before the view is set up) the text
				is kept in a large binary on it (TXVBOChars.h).

				ShowRuler puts the ruler bar (TXRulerUI.h) above the text.

				NOT YET: the pages (TXPageFrames), marked where they would
				be.

	Reconstructed from the MP2x00 US ROM (0x0024659c-0x0024dff4); each
	function cites its origin.
*/

#ifndef __TXVIEW_H
#define __TXVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif
#ifndef __TEXTENSION_H
#include "Textension.h"
#endif
#ifndef __REGIONVARS_H
#include "RegionVars.h"
#endif
#ifndef __TXCOMMAND_H
#include "TXCommand.h"
#endif

class TStrokePublic;
class TUnitPublic;
class TDragInfo;
class TXRulerUI;
class TXNewtRulerUI;
class TXNewtPen;
class TXKeyCommand;
class TXStream;

// What a click in a run asks for.  The ROM's is 0x18 bytes; only the
// first word is known: nought, nothing.
struct TXClickCommandInfo
{
	long			fCommand;		// +0x00
	long			fData[5];		// +0x04
};

// TXView::fTXFlags
enum
{
	kTXViewHasScrollers		= 1,	// the view has a viewUpdateScrollersScript
	kTXViewModified			= 2,
	kTXViewTapPending		= 4,	// a tap on the hilite waits to see whether a second follows
	kTXViewPaginated		= 8		// SetGeometry asked for pages
};


// The ROM's object is 0x60 bytes.
class TXView : public TView
{
public:
	virtual long	ClassID(void) const;							// ROM 0x0024659c ClassID__6TXViewCFv
	virtual Boolean	DerivedFrom(long id) const;						// ROM 0x0024af18 DerivedFrom__6TXViewCFl
	virtual			~TXView();										// ROM 0x0024c400 __dt__6TXViewFv
	virtual void	Constructor(RefArg context, TView* parent);		// ROM 0x0024b930 Constructor__6TXViewFRC6RefVarP5TView
	virtual Boolean	RealDoCommand(RefArg cmd);						// ROM 0x0024b19c RealDoCommand__6TXViewFRC6RefVar
	virtual long	TextFlags(void) const;							// ROM 0x0024c3e0 TextFlags__6TXViewCFv
	virtual void	SetBounds(const Rect& bounds);					// ROM 0x0024af4c SetBounds__6TXViewFRC5TRect
	virtual void	SetupDone(void);								// ROM 0x0024be38 SetupDone__6TXViewFv
	virtual Ref		GetRangeText(long start, long length);			// ROM 0x0024c48c GetRangeText__6TXViewFlT1
	virtual Ref		GetValue(RefArg slot, RefArg type);				// ROM 0x0024c4d4 GetValue__6TXViewFRC6RefVarT1
	virtual void	SetCaretOffset(long* offset, long* length);		// ROM 0x0024c1cc SetCaretOffset__6TXViewFPlT1
	virtual void	SetSelection(RefArg selection, long* start, long* end);	// ROM 0x0024c20c SetSelection__6TXViewFRC6RefVarPlT2
	virtual Ref		GetSelection(void);								// ROM 0x0024c304 GetSelection__6TXViewFv
	virtual void	ActivateSelection(Boolean on);					// ROM 0x0024c3b0 ActivateSelection__6TXViewFUc
	virtual Boolean	DoEditCommand(long command);					// ROM 0x00247c6c DoEditCommand__6TXViewFl
	virtual void	OffsetToCaret(long offset, Rect* caret);		// ROM 0x0024c080 OffsetToCaret__6TXViewFlP5TRect
	virtual void	NarrowVisByIntersectingObscuringSiblingsAndUncles(TView* upTo, Rect* bounds);	// ROM 0x0024c158 NarrowVisByIntersectingObscuringSiblingsAndUncles__6TXViewFP5TViewP5TRect
	virtual long	Idle(long arg);									// ROM 0x0024b5e4 Idle__6TXViewFl
	virtual void	RealDraw(Rect& bounds);							// ROM 0x0024b57c RealDraw__6TXViewFR5TRect
	virtual Ref		GetDropData(RefArg dragType, RefArg dragRef);	// ROM 0x002489d4 GetDropData__6TXViewFRC6RefVarT1
	virtual Boolean	DrawDragBackground(const Rect& bounds, Boolean copy);	// ROM 0x002489cc DrawDragBackground__6TXViewFRC5TRectUc
	virtual void	DrawDragData(const Rect& bounds);				// ROM 0x00248948 DrawDragData__6TXViewFRC5TRect
	virtual Ref		GetClipboardDataBits(Rect* bounds);				// ROM 0x002486e8 GetClipboardDataBits__6TXViewFP5TRect
	virtual Boolean	AcceptDrop(const TDragInfo& info, const Point& pt);	// ROM 0x00248f40 AcceptDrop__6TXViewFRC9TDragInfoRC6TPoint
	virtual Boolean	Drop(RefArg dropType, RefArg dropData, Point* dropPt);	// ROM 0x00249184 Drop__6TXViewFRC6RefVarT1P6TPoint
	virtual Boolean	DropMove(RefArg dragRef, const Point& oldPt, const Point& newPt, Boolean copy);	// ROM 0x00248f58 DropMove__6TXViewFRC6RefVarRC6TPointT2Uc
	virtual Boolean	DropRemove(RefArg dragRef);						// ROM 0x00249028 DropRemove__6TXViewFRC6RefVar
	virtual Boolean	DragFeedback(const TDragInfo& info, const Point& pt, Boolean show);	// ROM 0x00248e80 DragFeedback__6TXViewFRC9TDragInfoRC6TPointUc
	virtual Ref		GetSupportedDropTypes(const Point& pt);			// ROM 0x00248e08 GetSupportedDropTypes__6TXViewFRC6TPoint

	// the document
	void			CreateNewTextension(void);						// ROM 0x0024d1bc CreateNewTextension__6TXViewFv
	void			SyncViewRgn(void);								// ROM 0x0024af78 SyncViewRgn__6TXViewFv
	void			GeometryChanged(Boolean redisplay);				// ROM 0x0024b058 GeometryChanged__6TXViewFUc
	void			SetGeometry(Boolean paginate, int width, int height, const Rect& margins);	// ROM 0x0024dea4 SetGeometry__6TXViewFUciT2RC5TRect
	void			SetDrawOrigin(const TXLongPoint& origin);		// ROM 0x0024dfd8 SetDrawOrigin__6TXViewFRC11TXLongPoint
	void			SetStore(RefArg store);							// ROM 0x0024d78c SetStore__6TXViewFRC6RefVar
	void			SetReadOnly(Boolean readOnly);					// ROM 0x0024dba8 SetReadOnly__6TXViewFUc
	Boolean			IsModified(void);								// ROM 0x0024b568 IsModified__6TXViewFv
	long			CountChars(void);								// ROM 0x0024be00 CountChars__6TXViewFv
	long			GetTotalHeight(void);							// ROM 0x0024be10 GetTotalHeight__6TXViewFv
	long			GetTotalWidth(void);							// ROM 0x0024be24 GetTotalWidth__6TXViewFv
	long			GetCountPages(void);							// ROM 0x0024d358 GetCountPages__6TXViewFv
	RgnHandle		GetTextViewRgn(void);							// ROM 0x0024bfe4 GetTextViewRgn__6TXViewFv

	// the selection and the pen
	void			SetHiliteRange(const TXOffsetRange& range, Boolean scroll, Boolean show);	// ROM 0x0024bd80 SetHiliteRange__6TXViewFRC13TXOffsetRangeUcT2
	void			GetHiliteRange(TXOffsetRange* range);			// ROM 0x0024bdf4 GetHiliteRange__6TXViewFP13TXOffsetRange
	void			GetHiliteBounds(Rect* bounds);					// ROM 0x0024d4ec GetHiliteBounds__6TXViewFP4Rect
	Boolean			PointToChar(Point pt, TXOffsetRange* range);	// ROM 0x0024bff4 PointToChar__6TXViewF5PointP13TXOffsetRange
	Point			CharToPoint(TXOffsetPos offset, int* height);	// ROM 0x0024c044 CharToPoint__6TXViewF8TXOffsetPi
	Boolean			GetWordRange(TXOffsetPos offset, TXOffsetRange* range);	// ROM 0x0024d37c GetWordRange__6TXViewF8TXOffsetP13TXOffsetRange
	Boolean			GetLineRange(TXOffsetPos offset, TXOffsetRange* range);	// ROM 0x0024d448 GetLineRange__6TXViewF8TXOffsetP13TXOffsetRange
	Boolean			GetParagraphRange(TXOffsetPos offset, TXOffsetRange* range);	// ROM 0x0024d48c GetParagraphRange__6TXViewF8TXOffsetP13TXOffsetRange
	long			Click(TXNewtPen* pen, unsigned long command);	// ROM 0x0024b6ac Click__6TXViewFP9TXNewtPenUl
	void			ClickLoop(Boolean inLoop, void* scroll);		// ROM 0x0024b650 ClickLoop__6TXViewFUcPv
	Boolean			RulerClick(TXNewtPen* pen);						// ROM 0x0024ba60 RulerClick__6TXViewFP9TXNewtPen
	Boolean			CheckDrag(TXNewtPen* pen);						// ROM 0x002483ec CheckDrag__6TXViewFP9TXNewtPen
	long			HandleCaretGesture(TUnitPublic* unit);			// ROM 0x00246a34 HandleCaretGesture__6TXViewFP11TUnitPublic
	long			Scrub(TUnitPublic* unit);						// ROM 0x002496c8 Scrub__6TXViewFP11TUnitPublic
	Boolean			GetIntersectedLines(const Rect& r, long* first, long* last);	// ROM 0x00249884 GetIntersectedLines__6TXViewFRC5TRectPlT2
	long			GetBestCoveredLine(Rect* r, long first, long last, long* coverage);	// ROM 0x00246644 GetBestCoveredLine__6TXViewFP5TRectlT2Pl
	long			GetBestCoveredLine(Rect* r, long* coverage);	// ROM 0x002466fc GetBestCoveredLine__6TXViewFP5TRectPl
	Boolean			IsLinesScrub(const Rect& r, long first, long last, TXOffsetRange* range);	// ROM 0x00246754 IsLinesScrub__6TXViewFRC5TRectlT2P13TXOffsetRange
	Boolean			IsCharOrWordsScrub(const Rect& r, long first, long last, TXOffsetRange* range);	// ROM 0x0024683c IsCharOrWordsScrub__6TXViewFRC5TRectlT2P13TXOffsetRange
	void			Scroll(TXLongPoint* d);							// ROM 0x0024bd0c Scroll__6TXViewFP11TXLongPoint
	void			GetScrollValues(TXLongPoint* scrolled);			// ROM 0x0024bd74 GetScrollValues__6TXViewFP11TXLongPoint

	// typing and editing
	void			KeyDown(UniChar key, Boolean repeat);			// ROM 0x002465ac KeyDown__6TXViewFUsUc
	void			KeyString(UniChar* chars, long count);			// ROM 0x002470d4 KeyString__6TXViewFPUsl
	void			NewKey(const UniChar* chars, long count, int keyFlags, long modifiers, Boolean showEnd);	// ROM 0x00247448 NewKey__6TXViewFPCUsliT2Uc
	TXKeyCommand*	GetCurrentKeyCommand(void);						// ROM 0x00247388 GetCurrentKeyCommand__6TXViewFv
	Ref				PostUndo(RefArg command);						// ROM 0x00247654 PostUndo__6TXViewFRC6RefVar
	void			ExecuteCommand(RefArg command);					// ROM 0x002476c0 ExecuteCommand__6TXViewFRC6RefVar
	void			NewAttrCommand(int kind, const TXOffsetRange& range, TXAttrValues* values, long how);	// ROM 0x00246fdc NewAttrCommand__6TXViewFiRC13TXOffsetRangeP12TXAttrValuesl
	void			NewReplaceTextCommand(const TXOffsetRange& range, TXReplaceParams* params);	// ROM 0x00247108 NewReplaceTextCommand__6TXViewFRC13TXOffsetRangeP15TXReplaceParams
	void			NewMoveTextCommand(const TXOffsetRange& range, const TXOffsetPos& to, Boolean copy);	// ROM 0x002472b8 NewMoveTextCommand__6TXViewFRC13TXOffsetRangeRC8TXOffsetUc
	void			ChangeRangeRuns(const TXOffsetRange& range, RefArg style, Boolean toggle, Boolean undoable);	// ROM 0x00246c60 ChangeRangeRuns__6TXViewFRC13TXOffsetRangeRC6RefVarUcT3
	void			ChangeRangeRulers(const TXOffsetRange& range, RefArg ruler, Boolean undoable);	// ROM 0x00246db8 ChangeRangeRulers__6TXViewFRC13TXOffsetRangeRC6RefVarUc
	void			Replace(const TXOffsetRange& range, RefArg data, Boolean undoable, Boolean all);	// ROM 0x002477e0 Replace__6TXViewFRC13TXOffsetRangeRC6RefVarUcT3
	void			CheckReplaceData(RefArg data, long length);		// ROM 0x00247910 CheckReplaceData__6TXViewFRC6RefVarl
	long			ReplaceAll(UniChar* find, long start, RefArg data);	// ROM 0x00247ac0 ReplaceAll__6TXViewFPUslRC6RefVar
	void			InsertPageBreak(const TXOffsetRange& range);	// ROM 0x00247cd0 InsertPageBreak__6TXViewFRC13TXOffsetRange
	void			Cut(void);										// ROM 0x00248d20 Cut__6TXViewFv
	void			Copy(void);										// ROM 0x00249550 Copy__6TXViewFv
	Boolean			Paste(void);									// ROM 0x002495d4 Paste__6TXViewFv
	void			Clear(void);									// ROM 0x00249678 Clear__6TXViewFv
	long			FindString(UniChar* find, long start);			// ROM 0x0024d6ac FindString__6TXViewFPUsl

	// drag and drop, and the clipboard
	void			AddTextDragItem(TDragInfo* info, long start, long length, int* count);	// ROM 0x00247d34 AddTextDragItem__6TXViewFP9TDragInfolT2Pi
	long			GetDragInfo(TDragInfo* info);					// ROM 0x0024803c GetDragInfo__6TXViewFP9TDragInfo
	Textension*		GetClipboardDataText(int height);				// ROM 0x002484d4 GetClipboardDataText__6TXViewFi
	long			GetDropOffset(const Point& pt);					// ROM 0x00248cd4 GetDropOffset__6TXViewFRC6TPoint
	Ref				GetSupportedDropTypes(void);					// ROM 0x00248d4c GetSupportedDropTypes__6TXViewFv
	void			NewPasteCommand(void);							// ROM 0x002471d8 NewPasteCommand__6TXViewFv
	// After an edit: the hilite shown (its end, or its start), the
	// scrollers told when the text's height changed.
	void			Edited(Boolean show, Boolean end, Boolean scrollers);	// ROM 0x00246e78 Edited__6TXViewFUcN21
	void			UpdateScrollers(Boolean h, Boolean v);			// ROM 0x00246f1c UpdateScrollers__6TXViewFUcT1
	Ref				GetContinuousRun(void);							// ROM 0x0024bb9c GetContinuousRun__6TXViewFv

	// the ruler bar
	void			ShowRuler(RefArg info);							// ROM 0x0024c6d4 ShowRuler__6TXViewFRC6RefVar
	void			HideRuler(void);								// ROM 0x0024c794 HideRuler__6TXViewFv
	void			UpdateRulerInfo(RefArg info);					// ROM 0x0024c7ec UpdateRulerInfo__6TXViewFRC6RefVar
	void			UpdateRuler(Boolean redraw);					// ROM 0x0024bb88 UpdateRuler__6TXViewFUc

	// the document as frames
	Ref				GetRangeData(TXOffsetRange* range, RefArg what);	// ROM 0x0024d07c GetRangeData__6TXViewFP13TXOffsetRangeRC6RefVar
	Ref				Externalize(void);								// ROM 0x0024c7fc Externalize__6TXViewFv
	void			Internalize(RefArg data);						// ROM 0x0024cd18 Internalize__6TXViewFRC6RefVar
	TXChars*		InternalizeChars(RefArg data);					// ROM 0x0024cad0 InternalizeChars__6TXViewFRC6RefVar
	Boolean			InternalizeFormattingData(TXStream* stream, char flags);	// ROM 0x0024cc6c InternalizeFormattingData__6TXViewFP8TXStreamc

	Textension*		fText;			// +0x30
	TXNewtRulerUI*	fRulerUI;		// +0x34  nil: no ruler shown
	long			fPageWidth;		// +0x38  0 or less: the view's
	long			fPageHeight;	// +0x3c
	Rect			fMargins;		// +0x40
	Point			fTapPt;			// +0x48  kTXViewTapPending's
	unsigned long	fTXFlags;		// +0x4c
	long			fTotalHeight;	// +0x50  as the scrollers were last told
	RefStruct		fKeyCommand;	// +0x54  the undo command typing goes on adding to
	RefStruct		fStore;			// +0x58  nil: the text in a string
	Boolean			fReadOnly;		// +0x5c
};


// The display: drawing clipped to the view's visible region, and every
// edit bracketed by StartDrawing/StopDrawing with the caret hidden.
// The ROM's object is 0x2c bytes.
class TXNewtDisplay : public TXDisplay
{
public:
					TXNewtDisplay(TView* view);						// ROM 0x0024d6b8 __ct__13TXNewtDisplayFP5TView

	virtual void	Focus(RgnHandle* savedClip, Point* savedOrigin);	// ROM 0x0024d824 Focus__13TXNewtDisplayFPPP6RegionP5Point
	virtual void	UnFocus(RgnHandle savedClip, Point savedOrigin);	// ROM 0x0024d898 UnFocus__13TXNewtDisplayFPP6Region5Point
	virtual void	BeginEdit(TXEditInfo* info);					// ROM 0x0024d708 BeginEdit__13TXNewtDisplayFP10TXEditInfo
	virtual void	EndEdit(const TXEditInfo& info, long firstLine, long lastLine, TXOffsetPos* caret);	// ROM 0x0024d748 EndEdit__13TXNewtDisplayFRC10TXEditInfolT2P8TXOffset

	TView*			fView;			// +0x24
	TRegionStruct	fSavedVis;		// +0x28  the port's visible region while focused
};


// The hilite: the key view follows a selection made in the text.
// The ROM's object is 0x50 bytes.
class TXNewtHilite : public TXHilite
{
public:
					TXNewtHilite(TView* view);						// ROM 0x0024d9f4 __ct__12TXNewtHiliteFP5TView

	virtual Boolean	SetHiliteRange(const TXOffsetRange& range, Boolean show, Boolean scroll);	// ROM 0x0024da44 SetHiliteRange__12TXNewtHiliteFRC13TXOffsetRangeUcT2
	// The recogniser already counted the taps: one, or two for a
	// double tap.
	virtual long	CalcCountClicks(Point pt, long now, long doubleClickTime);	// ROM 0x0024dae8 CalcCountClicks__12TXNewtHiliteF5PointlT2

	TView*			fView;			// +0x48
	Boolean			fDoubleTap;		// +0x4c
};


// The pen: a stroke still being drawn, or one point.
// The ROM's object is 0xc bytes.
class TXNewtPen : public TXPointingDevice
{
public:
					TXNewtPen(TStrokePublic* stroke);				// ROM 0x0024dbb0 __ct__9TXNewtPenFP13TStrokePublic
					TXNewtPen(Point pt);							// ROM 0x0024dbf4 __ct__9TXNewtPenF5Point

	virtual Point	FirstLocation(void);							// ROM 0x0024dc7c FirstLocation__9TXNewtPenFv
	virtual Point	CurrentLocation(void);							// ROM 0x0024dcbc CurrentLocation__9TXNewtPenFv
	virtual Boolean	IsStillDown(void);								// ROM 0x0024dc48 IsStillDown__9TXNewtPenFv
	virtual long	GetDoubleClickTime(void);						// ROM 0x0024dcfc GetDoubleClickTime__9TXNewtPenFv - 0

	// The stroke's ink taken off the screen.
	void			InkOff(void);									// ROM 0x0024dd04 InkOff__9TXNewtPenFv

	TStrokePublic*	fStroke;		// +0x04
	Point			fPoint;			// +0x08
};


// The paste: a replacement whose data is the front clipping's.  The ROM's
// object is 0x7c bytes.
class TXNewtPasteCommand : public TXReplaceTextCommand
{
public:
	virtual NewtonErr DoMainAction(void);							// ROM 0x002492a8 DoMainAction__18TXNewtPasteCommandFv
};


Ref		FixupDropData(RefArg type, RefArg data);				// ROM 0x00249068 FixupDropData__FRC6RefVarT1
void	ClickLoop(unsigned char inLoop, void* scroll, void* view);	// ROM 0x0024b694 ClickLoop__FUcPvT2
void	GCDeleteTXCommand(void* command);						// ROM 0x002465a4 GCDeleteTXCommand__FPv
void	GCDeleteTXChars(void* chars);							// ROM 0x00249d38 GCDeleteTXChars__FPv
Ref		ToObject(const TXOffsetRange& range);					// ROM 0x00249968 ToObject__FRC13TXOffsetRange - a {first, last} frame
// A {first, last, trailingFirst, trailingLast} frame as a range; with a
// view, one outside its text throws -8703.
void	FromObject(RefArg obj, TXOffsetRange* range, TXView* view);	// ROM 0x002499fc FromObject__FRC6RefVarP13TXOffsetRangeP6TXView
TXView*	FailGetTXView(RefArg context);							// ROM 0x0024a35c FailGetTXView__FRC6RefVar
long	TXFindString(TXChars* chars, UniChar* find, long start);	// ROM 0x0024d54c TXFindString__FP7TXCharsPUsl - -1: not there

void	RegisterTXViewNatives(void);	// the protoTXView methods (TXViewNatives.cpp)

#endif	/* __TXVIEW_H */
