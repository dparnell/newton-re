/*
	File:		views/RootView.h

	Contains:	TRootView, the view at the top: the screen.  It keeps the
				update regions - up to three dirty regions, each with the
				view that will paint its background (the "filler": a filled
				view, or the common parent of the views that dirtied it) -
				and redraws them (Update); the key view and its caret (the
				view SetKeyView names, with a character offset and a
				selection length; the caret - the ROM's caret bitmaps - is
				drawn at the offset (the view's OffsetToCaret) over the
				screen with the bits under it saved in a TBits, and taken
				away and put back by Update, HideCaret/ShowCaret and
				RestoreBitsUnderCaret; the selection stack of the earlier
				key views), the popup, default button and modal view; the
				idlers; the clipboards (NOT YET: none).  gRootView is the one
				instance;
				its context is a clone of Rrootcontext with the root template
				as its _proto.

				The layout follows the ROM's (the fields the Constructor
				0x001b1868 and the methods use, at their offsets; what lies
				between is unknown); the update regions are TUpdateRegion[3]
				at +0x34.

	Reconstructed from the MP2x00 US ROM (0x001b182c-0x001b5c28); each
	function cites its origin.
*/

#ifndef __ROOTVIEW_H
#define __ROOTVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif
#ifndef __NEWTONTIME_H
#include "NewtonTime.h"
#endif

class CDynamicArray;
class TBits;

// a dirty region and the view that paints its background
struct TUpdateRegion
{
	TView*			fFiller;		// +0x00  nil: the slot is free
	TRegionStruct	fRegion;		// +0x04
};

// an idler: a view told to Idle(arg) when its time comes (16 bytes in
// the root's fIdlers array)
struct IdlerRecord
{
	TView*		fView;			// +0x00
	long		fArg;			// +0x04
	TTime		fTime;			// +0x08  when it is due
};

// the views whose Idle is running, linked through the stack (so a view
// that removes its idler, or goes, while idling is noticed)
struct IdlingView
{
	IdlingView*	fNext;
	TView*		fView;
};

class TClipboard;

class TRootView : public TView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x001b182c ClassID__9TRootViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x001b1834 DerivedFrom__9TRootViewCFl
	virtual			~TRootView();										// ROM 0x001b19dc __dt__9TRootViewFv
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x001b31f0 RealDoCommand__9TRootViewFRC6RefVar
	virtual void	Dirty(const Rect* rect);							// ROM 0x001b2e0c Dirty__9TRootViewFPC5TRect
	virtual void	RemoveAllViews(void);								// ROM 0x001b5b6c RemoveAllViews__9TRootViewFv
	virtual void	PostDraw(Rect& bounds);								// ROM 0x001b2360 PostDraw__9TRootViewFR5TRect
	virtual void	RealDraw(Rect& bounds);								// ROM 0x001b236c RealDraw__9TRootViewFR5TRect

	using TView::Constructor;
	void		Constructor(RefArg templ);								// ROM 0x001b1868 Constructor__9TRootViewFRC6RefVar
	void		Invalidate(RgnHandle rgn, TView* filler);				// ROM 0x001b204c Invalidate__9TRootViewFC11TBaseRegionP5TView
	void		Validate(RgnHandle rgn);								// ROM 0x001b2320 Validate__9TRootViewFC11TBaseRegion
	void		SmartInvalidate(const Rect& rect);						// ROM 0x001b1f00 SmartInvalidate__9TRootViewFRC5TRect
	void		SmartScreenDirty(const Rect& rect);						// ROM 0x001b2390 SmartScreenDirty__9TRootViewFRC5TRect
	Boolean		NeedsUpdate(void);										// ROM 0x001b2398 NeedsUpdate__9TRootViewFv
	void		Update(Rect* rect);										// ROM 0x001b243c Update__9TRootViewFP5TRect
	TView*		GetCommonParent(TView* a, TView* b);					// ROM 0x001b1ffc GetCommonParent__9TRootViewFP5TViewT1
	void		ForgetAboutView(TView* view);							// ROM 0x001b1e0c ForgetAboutView__9TRootViewFP5TView
	void		CaretViewGone(void);									// ROM 0x001b1d34 CaretViewGone__9TRootViewFv
	Boolean		ViewContainsCaretView(TView* view);						// ROM 0x00265508 ViewContainsCaretView__FP5TView
	void		SetPopup(TView* view, Boolean set);						// ROM 0x001b56d8 SetPopup__9TRootViewFP5TViewUc
	// the clipboards and their icons: two parallel arrays of contexts,
	// front first (the icon at index i belongs to the clipboard at i)
	void		AddClipboard(RefArg clipboard, RefArg icon);				// ROM 0x001b37fc AddClipboard__9TRootViewFRC6RefVarT1 - the two views added as children (undoable)
	void		RemoveClipboard(void);									// ROM 0x001b3870 RemoveClipboard__9TRootViewFv - the front clipping and its icon taken off
	TView*		GetClipboard(void);										// ROM 0x001b58bc GetClipboard__9TRootViewFv - the front clipboard
	TView*		GetClipboardIcon(void);									// ROM 0x001b5928 GetClipboardIcon__9TRootViewFv - the front icon
	TView*		GetClipboard(TView* icon);								// ROM 0x001b5994 GetClipboard__9TRootViewFP5TView - the clipboard the icon belongs to
	TView*		GetClipboardIcon(TClipboard* clipboard);				// ROM 0x001b5a64 GetClipboardIcon__9TRootViewFP10TClipboard - the icon the clipboard belongs to
	Ref			GetClipboardIcons(void);								// ROM 0x001b5b54 GetClipboardIcons__9TRootViewFv - the icons' contexts, front first
	// the key view and the caret
	void		SetKeyView(TView* view, long offset, long length, Boolean noSelection);	// ROM 0x001b3bb4 SetKeyView__9TRootViewFP5TViewlT2Uc
	void		SetKeyViewSelection(TView* view, RefArg selection, Boolean check);	// ROM 0x001b3ae4 SetKeyViewSelection__9TRootViewFP5TViewRC6RefVarUc
	void		CommonSetKeyView(TView* view, long offset, long length);	// ROM 0x001b3c9c CommonSetKeyView__9TRootViewFP5TViewlT2
	void		HoldPendingKeyView(RefArg view, RefArg info);			// ROM 0x001b1cc0 HoldPendingKeyView__9TRootViewFRC6RefVarT1
	void		ActivatePendingKeyView(void);							// ROM 0x001b1ce4 ActivatePendingKeyView__9TRootViewFv
	void		PushSelection(TView* view, RefArg info);				// ROM 0x001b44d0 PushSelection__9TRootViewFP5TViewRC6RefVar
	Ref			PopSelection(void);										// ROM 0x001b4390 PopSelection__9TRootViewFv
	void		CleanSelectionStack(TView* view, Boolean trim);			// ROM 0x001b40b0 CleanSelectionStack__9TRootViewFP5TViewUc
	Ref			GetSelectionStack(void);								// ROM 0x001b41e0 GetSelectionStack__9TRootViewFv
	TView*		FindRestorableKeyView(TView* view, ULong* index);		// ROM 0x001b41fc FindRestorableKeyView__9TRootViewFP5TViewPUl
	Boolean		RestoreKeyView(TView* view);							// ROM 0x001b42b4 RestoreKeyView__9TRootViewFP5TView
	Boolean		GetPreserveHilites(void);								// ROM 0x001b455c GetPreserveHilites__9TRootViewFv
	Boolean		SetPreserveHilites(Boolean preserve);					// ROM 0x001b4548 SetPreserveHilites__9TRootViewFUc - ==> what it was
	Boolean		GetRemoteWriting(void);									// ROM 0x001b4a6c GetRemoteWriting__9TRootViewFv
	void		SetRemoteWriting(Boolean on);							// ROM 0x001b4a94 SetRemoteWriting__9TRootViewFUc
	Boolean		CaretEnabled(void);										// ROM 0x001b4ba4 CaretEnabled__9TRootViewFv
	Boolean		CaretValid(Point* pt);									// ROM 0x001b4bf4 CaretValid__9TRootViewFP6TPoint
	void		GetCaretPoint(Point* pt);								// ROM 0x001b4dc8 GetCaretPoint__9TRootViewFP6TPoint
	void		GetCaretRect(Rect* rect);								// ROM 0x001b4e3c GetCaretRect__9TRootViewFP5TRect
	Boolean		DoCaretClick(TUnitPublic* unit);						// ROM 0x001b529c DoCaretClick__9TRootViewFP11TUnitPublic - a click on the caret tracked (the caret inverted while the pen is on it); ==> whether the caret popup came up
	void		DrawCaret(Point pt);									// ROM 0x001b4f84 DrawCaret__9TRootViewF6TPoint
	void		RestoreBitsUnderCaret(void);							// ROM 0x001b51c0 RestoreBitsUnderCaret__9TRootViewFv
	void		HideCaret(void);										// ROM 0x001b5604 HideCaret__9TRootViewFv
	void		ShowCaret(void);										// ROM 0x001b5634 ShowCaret__9TRootViewFv
	void		DirtyCaret(void);										// ROM 0x001b5694 DirtyCaret__9TRootViewFv
	void		FindDefaultButtonAndCaretSlip(TView* view, TView** button, TView** slip);	// ROM 0x001b6bac
	void		UpdateDefaultButtonAndCaretSlip(void);					// ROM 0x001b4788 UpdateDefaultButtonAndCaretSlip__9TRootViewFv
	long		GetKeyboardIndex(RefArg context);						// ROM 0x001b4884 GetKeyboardIndex__9TRootViewFRC6RefVar
	void		RegisterKeyboard(RefArg context, ULong flags);			// ROM 0x001b4564 RegisterKeyboard__9TRootViewFRC6RefVarUl
	Boolean		UnregisterKeyboard(RefArg context);						// ROM 0x001b466c UnregisterKeyboard__9TRootViewFRC6RefVar
	Boolean		KeyboardConnected(void);								// ROM 0x001b4ad4 KeyboardConnected__9TRootViewFv
	Boolean		CommandKeyboardConnected(void);							// ROM 0x001b4afc CommandKeyboardConnected__9TRootViewFv
	Boolean		KeyboardActive(void);									// ROM 0x001b4b0c KeyboardActive__9TRootViewFv
	void		ConnectPassthruKeyboard(Boolean connected);				// ROM 0x001b491c ConnectPassthruKeyboard__9TRootViewFUc
	void		HandleKeyIn(ULong keyCode, Boolean isDown, TView* keyboard);	// ROM 0x001b492c HandleKeyIn__9TRootViewFUlUcP5TView
	void		CheckForCaretRemoval(void);								// ROM 0x001b45f8 CheckForCaretRemoval__9TRootViewFv
	void		SetModalView(TView* view);								// ROM 0x0030de2c SetModalView__FP5TView
	TTime		IdleViews(void);										// ROM 0x001b271c IdleViews__9TRootViewFv - the due idlers run; ==> the next idle time (zero: none)
	ULong		AddIdler(TView* view, ULong delay, long arg);			// ROM 0x001b2ab4 AddIdler__9TRootViewFP5TViewUll - delay 0 removes; ==> the time left
	ULong		RemoveIdler(TView* view, long arg);						// ROM 0x001b2c4c RemoveIdler__9TRootViewFP5TViewl
	void		RemoveAllIdlers(TView* view);							// ROM 0x001b2d60 RemoveAllIdlers__9TRootViewFP5TView
	IdlingView*	GetIdlingView(TView* view);								// ROM 0x001b2bf8 GetIdlingView__9TRootViewFP5TView
	void		UnlinkIdleView(TView* view);							// ROM 0x001b2c28 UnlinkIdleView__9TRootViewFP5TView
	long		ScreenWidth(void) const;								// host: the port's width (the ROM's screenWidth global)
	long		ScreenHeight(void) const;

	TView*			fHiliter;			// +0x30  the view owning the hilites (NOT YET)
	TUpdateRegion*	fUpdateRegions;		// +0x34  three of them
	Rect			fDirtyScreen;		// +0x38  what the screen must show again (SmartScreenDirty; NOT YET: the screen)
	CDynamicArray*	fIdlers;			// +0x40  the IdlerRecords
	long			fChildrenHighWater;	// +0x44  the children list packed (MoveLow) when it shrank below this
	long			fIdlersHighWater;	// +0x48
	IdlingView*		fIdlingViews;		// +0x4c  the views whose Idle is running
	TView*			fPopup;				// +0x50  the popup view
	RefStruct		fClipboardIcons;	// +0x54  the clipping icons' contexts, front first (nil when there are none)
	RefStruct		fClipboards;		// +0x58  their clipboards' contexts, one for one
	Boolean			fDirtyFlag;			// +0x5c  a gesture or a command to the children changed something (the ROM's event loop looks)
	RefStruct		fKeyboards;			// +0x60  the registered on-screen keyboards: [context, flags] pairs (flags: 1 shows the modifiers, 2 hears viewCaretChangedScript, 4 active) - the registry NOT YET
	Boolean			fPassthruKeyboard;	// +0x64  a keyboard connected through a soft keyboard (ConnectPassthruKeyboard)
	TView*			fCaretView;			// +0x68  the key view
	long			fCaretOffset;		// +0x6c  the caret's character offset in it
	long			fCaretLength;		// +0x70  the selection's length (the caret shows only for 0)
	TView*			fDefaultButton;		// +0x74  drawn with its marks; three pixels of outer bounds
	TView*			fCaretSlip;			// +0x78  the view whose hilite frame is thick
	RefStruct		fSelectionStack;	// +0x7c  the earlier key views' [context, caret info] pairs
	Boolean			fPreserveHilites;	// +0x80  a key view change keeps the old view's hilites
	TBits*			fCaretBits;			// +0x84  the screen under the caret
	Boolean			fCaretShowing;		// +0x88  the caret is on the screen
	Point			fCaretPoint;		// +0x8c  where (h = -0x8000: nowhere)
	TView*			fCaretDrawnView;	// +0x90  the view it was drawn for
	long			fCaretHidden;		// +0x94  HideCarets outstanding
	RefStruct		fPendingKeyView;	// +0x98  a key view to activate later (HoldPendingKeyView)
	RefStruct		fPendingKeyInfo;	// +0x9c  ... and its caret info
	TView*			fModalView;			// host: SetModalView's view (the ROM keeps it in the modal dialog code)
};

// A message sent to the root view's context, which is how the system's
// own code reaches a script that lives at the top of the view hierarchy
// (the power, backlight and alarm code all send this way).
Ref		NSSendRootMessage(RefArg message);					// ROM 0x001b1fe4 NSSendRootMessage__FRC6RefVar
Ref		NSSendRootMessage(RefArg message, RefArg a1);		// ROM 0x001b2a94 NSSendRootMessage__FRC6RefVarT1

extern Boolean	gNewtIsAliveAndWell;		// 0x0c102604  the boot is over: the root view draws no splash

#endif	/* __ROOTVIEW_H */
