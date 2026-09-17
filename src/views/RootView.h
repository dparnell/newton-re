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
				0x001b3d40 and the methods use, at their offsets; what lies
				between is unknown); the update regions are TUpdateRegion[3]
				at +0x34.

	Reconstructed from the MP2100 D ROM (0x001b3d04-0x001b8100); each
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

class TRootView : public TView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x001b3d04 ClassID__9TRootViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x001b3d0c DerivedFrom__9TRootViewCFl
	virtual			~TRootView();										// ROM 0x001b3eb4 __dt__9TRootViewFv
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x001b56c8 RealDoCommand__9TRootViewFRC6RefVar
	virtual void	Dirty(const Rect* rect);							// ROM 0x001b52e4 Dirty__9TRootViewFPC5TRect
	virtual void	RemoveAllViews(void);								// ROM 0x001b8044 RemoveAllViews__9TRootViewFv
	virtual void	PostDraw(Rect& bounds);								// ROM 0x001b4838 PostDraw__9TRootViewFR5TRect
	virtual void	RealDraw(Rect& bounds);								// ROM 0x001b4844 RealDraw__9TRootViewFR5TRect

	using TView::Constructor;
	void		Constructor(RefArg templ);								// ROM 0x001b3d40 Constructor__9TRootViewFRC6RefVar
	void		Invalidate(RgnHandle rgn, TView* filler);				// ROM 0x001b4524 Invalidate__9TRootViewFC11TBaseRegionP5TView
	void		Validate(RgnHandle rgn);								// ROM 0x001b47f8 Validate__9TRootViewFC11TBaseRegion
	void		SmartInvalidate(const Rect& rect);						// ROM 0x001b43d8 SmartInvalidate__9TRootViewFRC5TRect
	void		SmartScreenDirty(const Rect& rect);						// ROM 0x001b4868 SmartScreenDirty__9TRootViewFRC5TRect
	Boolean		NeedsUpdate(void);										// ROM 0x001b4870 NeedsUpdate__9TRootViewFv
	void		Update(Rect* rect);										// ROM 0x001b4914 Update__9TRootViewFP5TRect
	TView*		GetCommonParent(TView* a, TView* b);					// ROM 0x001b44d4 GetCommonParent__9TRootViewFP5TViewT1
	void		ForgetAboutView(TView* view);							// ROM 0x001b42e4 ForgetAboutView__9TRootViewFP5TView
	void		CaretViewGone(void);									// ROM 0x001b420c CaretViewGone__9TRootViewFv
	Boolean		ViewContainsCaretView(TView* view);						// ROM 0x002635d0 ViewContainsCaretView__FP5TView
	void		SetPopup(TView* view, Boolean set);						// ROM 0x001b7bb0 SetPopup__9TRootViewFP5TViewUc
	TView*		GetClipboard(TView* view);								// ROM 0x001b7e6c GetClipboard__9TRootViewFP5TView
	// the key view and the caret
	void		SetKeyView(TView* view, long offset, long length, Boolean noSelection);	// ROM 0x001b608c SetKeyView__9TRootViewFP5TViewlT2Uc
	void		SetKeyViewSelection(TView* view, RefArg selection, Boolean check);	// ROM 0x001b5fbc SetKeyViewSelection__9TRootViewFP5TViewRC6RefVarUc
	void		CommonSetKeyView(TView* view, long offset, long length);	// ROM 0x001b6174 CommonSetKeyView__9TRootViewFP5TViewlT2
	void		HoldPendingKeyView(RefArg view, RefArg info);			// ROM 0x001b4198 HoldPendingKeyView__9TRootViewFRC6RefVarT1
	void		ActivatePendingKeyView(void);							// ROM 0x001b41bc ActivatePendingKeyView__9TRootViewFv
	void		PushSelection(TView* view, RefArg info);				// ROM 0x001b69a8 PushSelection__9TRootViewFP5TViewRC6RefVar
	Ref			PopSelection(void);										// ROM 0x001b6868 PopSelection__9TRootViewFv
	void		CleanSelectionStack(TView* view, Boolean trim);			// ROM 0x001b6588 CleanSelectionStack__9TRootViewFP5TViewUc
	Ref			GetSelectionStack(void);								// ROM 0x001b66b8 GetSelectionStack__9TRootViewFv
	TView*		FindRestorableKeyView(TView* view, ULong* index);		// ROM 0x001b66d4 FindRestorableKeyView__9TRootViewFP5TViewPUl
	Boolean		RestoreKeyView(TView* view);							// ROM 0x001b678c RestoreKeyView__9TRootViewFP5TView
	Boolean		GetPreserveHilites(void);								// ROM 0x001b6a34 GetPreserveHilites__9TRootViewFv
	void		SetPreserveHilites(Boolean preserve);					// ROM 0x001b6a20 SetPreserveHilites__9TRootViewFUc
	Boolean		GetRemoteWriting(void);									// ROM 0x001b6f44 GetRemoteWriting__9TRootViewFv
	void		SetRemoteWriting(Boolean on);							// ROM 0x001b6f6c SetRemoteWriting__9TRootViewFUc
	Boolean		CaretEnabled(void);										// ROM 0x001b707c CaretEnabled__9TRootViewFv
	Boolean		CaretValid(Point* pt);									// ROM 0x001b70cc CaretValid__9TRootViewFP6TPoint
	void		GetCaretPoint(Point* pt);								// ROM 0x001b72a0 GetCaretPoint__9TRootViewFP6TPoint
	void		GetCaretRect(Rect* rect);								// ROM 0x001b7314 GetCaretRect__9TRootViewFP5TRect
	void		DrawCaret(Point pt);									// ROM 0x001b745c DrawCaret__9TRootViewF6TPoint
	void		RestoreBitsUnderCaret(void);							// ROM 0x001b7698 RestoreBitsUnderCaret__9TRootViewFv
	void		HideCaret(void);										// ROM 0x001b7adc HideCaret__9TRootViewFv
	void		ShowCaret(void);										// ROM 0x001b7b0c ShowCaret__9TRootViewFv
	void		DirtyCaret(void);										// ROM 0x001b7b6c DirtyCaret__9TRootViewFv
	void		FindDefaultButtonAndCaretSlip(TView* view, TView** button, TView** slip);	// ROM 0x001b6bac
	void		UpdateDefaultButtonAndCaretSlip(void);					// ROM 0x001b6c60 UpdateDefaultButtonAndCaretSlip__9TRootViewFv
	long		GetKeyboardIndex(RefArg context);						// ROM 0x001b6d5c GetKeyboardIndex__9TRootViewFRC6RefVar
	void		RegisterKeyboard(RefArg context, ULong flags);			// ROM 0x001b6a3c RegisterKeyboard__9TRootViewFRC6RefVarUl
	Boolean		UnregisterKeyboard(RefArg context);						// ROM 0x001b6b44 UnregisterKeyboard__9TRootViewFRC6RefVar
	Boolean		KeyboardConnected(void);								// ROM 0x001b6fac KeyboardConnected__9TRootViewFv
	Boolean		CommandKeyboardConnected(void);							// ROM 0x001b6fd4 CommandKeyboardConnected__9TRootViewFv
	Boolean		KeyboardActive(void);									// ROM 0x001b6fe4 KeyboardActive__9TRootViewFv
	void		ConnectPassthruKeyboard(Boolean connected);				// ROM 0x001b6df4 ConnectPassthruKeyboard__9TRootViewFUc
	void		HandleKeyIn(ULong keyCode, Boolean isDown, TView* keyboard);	// ROM 0x001b6e04 HandleKeyIn__9TRootViewFUlUcP5TView
	void		CheckForCaretRemoval(void);								// ROM 0x001b6ad0 CheckForCaretRemoval__9TRootViewFv
	void		SetModalView(TView* view);								// ROM 0x002e8b18 SetModalView__FP5TView
	TTime		IdleViews(void);										// ROM 0x001b4bf4 IdleViews__9TRootViewFv - the due idlers run; ==> the next idle time (zero: none)
	ULong		AddIdler(TView* view, ULong delay, long arg);			// ROM 0x001b4f8c AddIdler__9TRootViewFP5TViewUll - delay 0 removes; ==> the time left
	ULong		RemoveIdler(TView* view, long arg);						// ROM 0x001b5124 RemoveIdler__9TRootViewFP5TViewl
	void		RemoveAllIdlers(TView* view);							// ROM 0x001b5238 RemoveAllIdlers__9TRootViewFP5TView
	IdlingView*	GetIdlingView(TView* view);								// ROM 0x001b50d0 GetIdlingView__9TRootViewFP5TView
	void		UnlinkIdleView(TView* view);							// ROM 0x001b5100 UnlinkIdleView__9TRootViewFP5TView
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
	RefStruct		fClipboardIcon;		// +0x54  (NOT YET)
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

extern Boolean	gNewtIsAliveAndWell;		// 0x0c102604  the boot is over: the root view draws no splash

#endif	/* __ROOTVIEW_H */
