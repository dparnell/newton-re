/*
	File:		views/RootView.h

	Contains:	TRootView, the view at the top: the screen.  It keeps the
				update regions - up to three dirty regions, each with the
				view that will paint its background (the "filler": a filled
				view, or the common parent of the views that dirtied it) -
				and redraws them (Update); the caret, key view, popup,
				default button and modal view (mostly NOT YET RECONSTRUCTED:
				the pointers are kept and cleared); the idlers (NOT YET); the
				clipboards (NOT YET: none).  gRootView is the one instance;
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

class CDynamicArray;

// a dirty region and the view that paints its background
struct TUpdateRegion
{
	TView*			fFiller;		// +0x00  nil: the slot is free
	TRegionStruct	fRegion;		// +0x04
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
	void		SetModalView(TView* view);								// ROM 0x002e8b18 SetModalView__FP5TView
	long		ScreenWidth(void) const;								// host: the port's width (the ROM's screenWidth global)
	long		ScreenHeight(void) const;

	TView*			fHiliter;			// +0x30  the view owning the hilites (NOT YET)
	TUpdateRegion*	fUpdateRegions;		// +0x34  three of them
	Rect			fDirtyScreen;		// +0x38  what the screen must show again (SmartScreenDirty; NOT YET: the screen)
	CDynamicArray*	fIdlers;			// +0x40  the idling views (NOT YET)
	TView*			fPopup;				// +0x50  the popup view
	RefStruct		fClipboardIcon;		// +0x54  (NOT YET)
	RefStruct		fSelectionStack;	// +0x60  the saved key view selections (NOT YET)
	Boolean			fDirtyFlag;			// +0x5c  a gesture or a command to the children changed something (the ROM's event loop looks)
	TView*			fCaretView;			// +0x68  the key view with the caret (NOT YET)
	TView*			fDefaultButton;		// +0x74  drawn with its marks; three pixels of outer bounds
	TView*			fCaretSlip;			// +0x78  the view whose hilite frame is thick
	RefStruct		fKeyboards;			// +0x7c  the registered keyboards (NOT YET)
	TView*			fModalView;			// host: SetModalView's view (the ROM keeps it in the modal dialog code)
};

extern Boolean	gNewtIsAliveAndWell;		// 0x0c101a24  the boot is over: the root view draws no splash

#endif	/* __ROOTVIEW_H */
