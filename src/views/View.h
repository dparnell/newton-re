/*
	File:		views/View.h

	Contains:	TView, the C++ object behind every NewtonScript view.  A view
				is made from a template (a frame, usually protoed) by
				building a *context* frame for it (BuildContext: _proto the
				template, _parent the parent view's context, viewCObject
				the C++ object) and constructing the class the template's
				viewClass names (BuildView); the view keeps its flags, its
				format, its bounds in global coordinates (viewBounds justified
				against the parent by viewJustify: JustifyBounds), its parent
				and children (a TViewList of TView*), and a cache of which of
				the often-looked-up slots (Rslotcachetable) it has.  The
				scripts (viewSetupFormScript, viewDrawScript, ...) are sent to
				the context (RunScript, RunCacheScript).  Drawing: Draw clips
				the port to the view's visible region (SetupVisRgn; the
				children of the root view keep a TClipper with their full and
				visible regions), PreDraw paints the viewFormat's fill and
				lines, RealDraw the class's content, the viewDrawScript runs,
				the children draw, PostDraw frames.  Dirty accumulates the
				update region in the root view (RootView.h), whose Update
				redraws it.

				TView's layout is the ROM's (0x30 bytes; the fields as the
				Constructor 0x00264430 and Dump 0x0025e33c use them) and its
				methods are declared in the order of its vtable (0x0001f75c).
				TxObject and TResponder are the bases: TxObject the class-id
				root (ClassID, DerivedFrom, Key), TResponder the command
				receiver (DoCommand).  TViewList is a CList of TView*, walked
				by TListLoop (forwards) and TBackwardLoop.

				Select/Hilite invert a view (a button pressed) through the
				viewHiliteScript or InvertRect.  NOT YET RECONSTRUCTED: the
				data hilites (THilite, HiliteLoop, the DrawHilit* methods
				draw nothing; SetCaretOffset/OffsetToCaret/PointToCaret/
				GetSelection/SetSelection are the paragraph's - RootView.h
				has the key view), drag and
				drop, the key view chain (BuildKeyChildList, NextKeyView;
				HandleKeyEvent runs the key scripts and the key commands -
				Keyboard.h; RealDoCommand answers the other commands -
				Commands.h), the animation effects
				(TAnimate: Show and Hide draw at once), the stroke world, the
				sound effects, gSlowMotion, SyncScroll, and
				the subclasses (BuildView makes a TView for every class).

	Reconstructed from the MP2100 D ROM (0x0025c598-0x00269200,
	0x00066bf8-0x00066d6c); each function cites its origin.
*/

#ifndef __VIEW_H
#define __VIEW_H

#ifndef __VIEWFLAGS_H
#include "ViewFlags.h"
#endif
#ifndef __REGIONVARS_H
#include "RegionVars.h"
#endif
#ifndef __FRAMES_H
#include "Frames.h"
#endif
#ifndef __LIST_H
#include "List.h"
#endif

class TView;
class TRootView;
class TViewList;
class TDragInfo;				// NOT YET RECONSTRUCTED: drag and drop
class TStrokePublic;			// recognition/Stroke.h
class TUnitPublic;				// NOT YET RECONSTRUCTED: the recognition units
struct StyleRecord;

/*------------------------------------------------------------------------------
	T x O b j e c t,  T R e s p o n d e r
	The bases: a class id and derivation test (TxObject 0x001452e4..), a
	command receiver (TResponder 0x001ab8f0..).
------------------------------------------------------------------------------*/

class TxObject
{
public:
	static void*	operator new(size_t size);				// ROM 0x0014528c __nw__8TxObjectSFUi  (the memory cleared)
	static void		operator delete(void* p);				// ROM 0x001452c8 __dl__8TxObjectSFPv
	virtual long	ClassID(void) const;					// ROM 0x001452e4 ClassID__8TxObjectCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x001452ec DerivedFrom__8TxObjectCFl
	virtual			~TxObject();							// ROM 0x001452cc __dt__8TxObjectFv
	virtual ULong	Key(void) const;						// ROM 0x00145300 Key__8TxObjectCFv
};

class TResponder : public TxObject
{
public:
	virtual long	ClassID(void) const;					// ROM 0x001ab8f0 ClassID__10TResponderCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x001ab8f8 DerivedFrom__10TResponderCFl
	virtual Boolean	DoCommand(RefArg cmd);					// ROM 0x001ab92c DoCommand__10TResponderFRC6RefVar
	virtual Boolean	RealDoCommand(RefArg cmd);
};

/*------------------------------------------------------------------------------
	T V i e w L i s t
	The children of a view: a CList of TView*.  TView::gEmptyViewList is
	the shared empty one a childless view refers to.
------------------------------------------------------------------------------*/

class TViewList : public CList
{
public:
	static TViewList*	Make(void)						{ return (TViewList*) CList::Make(); }
	static TViewList*	Make(ArrayIndex size)			{ return (TViewList*) CList::Make(size); }
	TView*		At(ArrayIndex index)					{ return (TView*) CList::At(index); }
	TView*		First(void)								{ return At(0); }
	TView*		Last(void)								{ return At(GetArraySize() - 1); }
	long		Count(void)								{ return (long) GetArraySize(); }
};

// the walks: forwards (TListLoop 0x00142ed4) and backwards (TBackwardLoop 0x00142f8c)
class TListLoop
{
public:
				TListLoop(CList* list);
	void		Reset(void);
	void*		Next(void);				// the next item, nil at the end
	void*		Current(void);
	void		RemoveCurrent(void);

private:
	CList*		fList;				// +0x00
	long		fIndex;				// +0x04
	long		fCount;				// +0x08
};

class TBackwardLoop
{
public:
				TBackwardLoop(CList* list);
	void*		Next(void);				// the previous item (from the last), nil at the beginning
	void*		Current(void);

private:
	CList*		fList;				// +0x00
	long		fIndex;				// +0x04
};

// the same walks giving TView* (the ROM's TBackwardViewListLoop 0x0025e2a0)
class TViewLoop : public TListLoop
{
public:
				TViewLoop(TViewList* list)		: TListLoop(list) { }
	TView*		Next(void)						{ return (TView*) TListLoop::Next(); }
};

class TBackwardViewListLoop : public TBackwardLoop
{
public:
				TBackwardViewListLoop(TViewList* list)	: TBackwardLoop(list) { }
	TView*		Next(void)						{ return (TView*) TBackwardLoop::Next(); }
};

/*------------------------------------------------------------------------------
	T C l i p p e r
	The regions of a child of the root view (a "window"): its full region
	(the outer bounds, rounded when the format says) and what of it is
	visible in front of the views behind... that is, less the views in
	front of it (RecalcVisible), with whether anything obscures it.  A
	view's context keeps its clipper's address in the viewclipper slot.
------------------------------------------------------------------------------*/

class TClipper
{
public:
				TClipper();										// ROM 0x00066bf8 __ct__8TClipperFv
	void		UpdateRegions(TView* view);						// ROM 0x00066c3c UpdateRegions__8TClipperFP5TView
	void		Offset(Point delta);							// ROM 0x00066cfc Offset__8TClipperF6TPoint
	void		RecalcVisible(RgnHandle inFront);				// ROM 0x00066d28 RecalcVisible__8TClipperF11TBaseRegion

	TRegionStruct	fFullRgn;			// +0x00  the view's region
	TRegionStruct	fVisRgn;			// +0x04  less what is in front of it
	Boolean			fIsObscured;		// +0x08  the two differ
};

/*------------------------------------------------------------------------------
	T V i e w
------------------------------------------------------------------------------*/

class TView : public TResponder
{
public:
	// the vtable's order (analysis/vtable.py build/MP2100D 0x1f75c)
	virtual long	ClassID(void) const;								// ROM 0x0025d358 ClassID__5TViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x00261688 DerivedFrom__5TViewCFl
	virtual			~TView();											// ROM 0x00266bbc __dt__5TViewFv
	virtual Boolean	DoCommand(RefArg cmd);								// ROM 0x00266c94 DoCommand__5TViewFRC6RefVar
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x00264430 Constructor__5TViewFRC6RefVarP5TView
	virtual void	Delete(void);										// ROM 0x0026564c Delete__5TViewFv
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x00266e00 RealDoCommand__5TViewFRC6RefVar
	virtual long	TextFlags(void) const;								// ROM 0x0025dca4 TextFlags__5TViewCFv
	virtual void	OuterBounds(Rect* bounds);							// ROM 0x00262198 OuterBounds__5TViewFP5TRect
	virtual Boolean	InsideView(Point& pt);								// ROM 0x0025dcfc InsideView__5TViewFR6TPoint
	virtual void	SetBounds(const Rect& bounds);						// ROM 0x00263624 SetBounds__5TViewFRC5TRect
	virtual void	ChildBoundsChanged(TView* child, Rect& bounds);		// ROM 0x00263894 ChildBoundsChanged__5TViewFP5TViewR5TRect
	virtual void	SetupForm(void);									// ROM 0x00263898 SetupForm__5TViewFv
	virtual void	SetupDone(void);									// ROM 0x00263988 SetupDone__5TViewFv
	virtual void	Hide(void);											// ROM 0x0026404c Hide__5TViewFv
	virtual Ref		GetRangeText(long start, long end);					// ROM 0x002688c8 GetRangeText__5TViewFlT1
	virtual Ref		GetValue(RefArg slot, RefArg type);					// ROM 0x002688d0 GetValue__5TViewFRC6RefVarT1
	virtual void	SetValue(RefArg slot, RefArg value);				// ROM 0x00268ab4 SetValue__5TViewFRC6RefVarT1
	virtual void	Changed(RefArg slot);								// ROM 0x00268cfc Changed__5TViewFRC6RefVar
	virtual void	Changed(RefArg slot, RefArg context);				// ROM 0x00268d08 Changed__5TViewFRC6RefVarT1
	virtual void	Dirty(const Rect* rect);							// ROM 0x00268ee8 Dirty__5TViewFPC5TRect
	virtual void	Hilite(Boolean on);									// ROM 0x0026418c Hilite__5TViewFUc
	virtual void	SetCaretOffset(long* offset, long* length);			// ROM 0x00268768 SetCaretOffset__5TViewFPlT1
	virtual void	SetSelection(RefArg selection, long* start, long* end);	// ROM 0x00268764 SetSelection__5TViewFRC6RefVarPlT2
	virtual Ref		GetSelection(void);									// ROM 0x00268750 GetSelection__5TViewFv
	virtual void	ActivateSelection(Boolean on);						// ROM 0x0026876c ActivateSelection__5TViewFUc
	virtual Boolean	DoEditCommand(long command);						// ROM 0x0009f9ec DoEditCommand__5TViewFl
	virtual void	OffsetToCaret(long offset, Rect* caret);			// ROM 0x00268810 OffsetToCaret__5TViewFlP5TRect
	virtual void	PointToCaret(Point& pt, Rect* caret, Rect* bounds);	// ROM 0x002687f0 PointToCaret__5TViewFR6TPointP5TRectT2
	virtual void	NarrowVisByIntersectingObscuringSiblingsAndUncles(TView* upTo, Rect* bounds);	// ROM 0x002639d4
	virtual void	RemoveAllViews(void);								// ROM 0x0025db24 RemoveAllViews__5TViewFv
	virtual long	Idle(long arg);										// ROM 0x00266c04 Idle__5TViewFl
	virtual void	DrawHiliting(void);									// ROM 0x00265250 DrawHiliting__5TViewFv
	virtual void	DrawHilitedData(void);								// ROM 0x00265224 DrawHilitedData__5TViewFv
	virtual Boolean	HandleHilite(TUnitPublic* unit, long arg, Boolean on);	// ROM 0x00260218 HandleHilite__5TViewFP11TUnitPubliclUc
	virtual Boolean	HandleScrub(const Rect& bounds, long arg, TUnitPublic* unit, Boolean on);	// ROM 0x002605f0 HandleScrub__5TViewFRC5TRectlP11TUnitPublicUc
	virtual Boolean	Hilited(void);										// ROM 0x0025feac Hilited__5TViewFv
	virtual void	DrawHilites(Boolean on);							// ROM 0x0025ff5c DrawHilites__5TViewFUc
	virtual Boolean	IsCompletelyHilited(RefArg hilite);					// ROM 0x002600c8 IsCompletelyHilited__5TViewFRC6RefVar
	virtual void	HiliteAll(void);									// ROM 0x002600d0 HiliteAll__5TViewFv
	virtual void	DeleteHilited(RefArg hilite);						// ROM 0x002601cc DeleteHilited__5TViewFRC6RefVar
	virtual void	RemoveHilite(RefArg hilite);						// ROM 0x0025ff60 RemoveHilite__5TViewFRC6RefVar
	virtual void	RemoveAllHilites(void);								// ROM 0x0026002c RemoveAllHilites__5TViewFv
	virtual void	GlobalHiliteBounds(Rect* bounds);					// ROM 0x002603a0 GlobalHiliteBounds__5TViewFP5TRect
	virtual void	GlobalHiliteResizeBounds(Rect* bounds);				// ROM 0x002604dc GlobalHiliteResizeBounds__5TViewFP5TRect
	virtual void	GlobalHilitePinnedBounds(Rect* bounds);				// ROM 0x00260514 GlobalHilitePinnedBounds__5TViewFP5TRect
	virtual Boolean	PointInHilite(Point& pt);							// ROM 0x0026051c PointInHilite__5TViewFR6TPoint
	virtual long	ClickOptions(void);									// ROM 0x00260630 ClickOptions__5TViewFv
	virtual void	DrawScaledData(const Rect& src, const Rect& dst, Rect* bounds);	// ROM 0x00260638 DrawScaledData__5TViewFRC5TRectT1P5TRect
	virtual Boolean	AddDragInfo(TDragInfo* dragInfo);					// ROM 0x0009f848 AddDragInfo__5TViewFP9TDragInfo (viewAddDragInfoScript)
	virtual Ref		GetDropData(RefArg dragType, RefArg dragRef);		// ROM 0x000a27c0 GetDropData__5TViewFRC6RefVarT1 (viewGetDropDataScript, else nil)
	virtual Boolean	DragAndDrop(TStrokePublic* stroke, const Rect& bounds, const Rect* limit, const Rect* slop, Boolean copy, const TDragInfo& dragInfo, const Rect* dragBounds);	// ROM 0x0009e394 DragAndDrop__5TViewFP13TStrokePublicRC5TRectPC5TRectT3UcRC9TDragInfoT3
	virtual void	DrawDragBackground(const Rect& bounds, Boolean copy);
	virtual void	DrawDragData(const Rect& bounds);
	virtual Boolean	GetClipboardDataBits(Rect* bounds);
	virtual Boolean	AcceptDrop(const TDragInfo& dragInfo, const Point& pt);	// ROM 0x000a24c0 AcceptDrop__5TViewFRC9TDragInfoRC6TPoint
	virtual Boolean	Drop(RefArg dropTypes, RefArg dropData, Point* dropPt);	// ROM 0x0009ddc4 Drop__5TViewFRC6RefVarT1P6TPoint (viewDropScript)
	virtual Boolean	DropMove(RefArg dragRef, const Point& oldPt, const Point& newPt, Boolean copy);	// ROM 0x000a25e4 DropMove__5TViewFRC6RefVarRC6TPointT2Uc (viewDropMoveScript)
	virtual Boolean	DropRemove(RefArg dragRef);							// ROM 0x0009de90 DropRemove__5TViewFRC6RefVar (viewDropRemoveScript)
	virtual Boolean	DropDone(void);										// ROM 0x0009e334 DropDone__5TViewFv
	virtual Boolean	DropApprove(TView* target);							// ROM 0x0009df10 DropApprove__5TViewFP5TView (viewDropApproveScript)
	virtual TView*	TargetDrop(const TDragInfo& dragInfo, const Point& pt);	// ROM 0x0009e7c8 TargetDrop__5TViewFRC9TDragInfoRC6TPoint
	virtual void	BuildKeyChildList(TViewList* list, long arg1, long arg2);	// ROM 0x00268290 BuildKeyChildList__5TViewFP9TViewListlT2
	virtual void	SimpleOffset(Point delta, Boolean inChildren);		// ROM 0x0025e064 SimpleOffset__5TViewF6TPointl
	virtual void	PreDraw(Rect& bounds);								// ROM 0x00266370 PreDraw__5TViewFR5TRect
	virtual void	PostDraw(Rect& bounds);								// ROM 0x002666c0 PostDraw__5TViewFR5TRect
	virtual void	RealDraw(Rect& bounds);								// ROM 0x002666bc RealDraw__5TViewFR5TRect
	virtual void	Scale(const Rect& src, const Rect& dst);			// ROM 0x002606bc Scale__5TViewFRC5TRectT1
	virtual void	EndDrag(const TDragInfo& dragInfo, TView* target, const Point& startPt, const Point& dropPt, const Point& dragPt, Boolean copy);	// ROM 0x0009dfb4 EndDrag__5TViewFRC9TDragInfoP5TViewRC6TPointN23Uc
	virtual void	DragFeedback(const TDragInfo& dragInfo, const Point& pt, Boolean copy);
	virtual Ref		GetSupportedDropTypes(const Point& pt);
	virtual TView*	FindDropView(const TDragInfo& dragInfo, const Point& pt);

	// making and structure
	static Ref		BuildContext(RefArg templ, Boolean forceVisible);		// ROM 0x0025c634 BuildContext__5TViewFRC6RefVarUc  (the view is unused: a static here)
	TView*		AddView(RefArg templ);									// ROM 0x0025d274 AddView__5TViewFRC6RefVar
	void		AddView(TView* child);									// ROM 0x0025d994 AddView__5TViewFP5TView
	TView*		AddChild(RefArg templ);									// ROM 0x00263f14 AddChild__5TViewFRC6RefVar
	void		AddViews(Boolean sync);									// ROM 0x00260cd4 AddViews__5TViewFUc
	void		RemoveView(void);										// ROM 0x0025da28 RemoveView__5TViewFv
	void		RemoveChildView(TView* child);							// ROM 0x0025da34 RemoveChildView__5TViewFP5TView
	void		RemoveUnmarked(void);									// ROM 0x00260468 RemoveUnmarked__5TViewFv
	void		ReorderView(TView* child, long index);					// ROM 0x0025eda0 ReorderView__5TViewFP5TViewl
	void		BringToFront(void);										// ROM 0x0025f2b0 BringToFront__5TViewFv
	void		MoveChildBehind(TView* child, TView* behind);			// ROM 0x0025f2c4 MoveChildBehind__5TViewFP5TViewT1
	TView*		AddToSoup(RefArg templ);								// ROM 0x0025d504 AddToSoup__5TViewFRC6RefVar
	void		RemoveFromSoup(TView* child);							// ROM 0x0025d66c RemoveFromSoup__5TViewFP5TView
	TView*		FindView(RefArg data);									// ROM 0x0025dbfc FindView__5TViewFRC6RefVar
	TView*		FindView(Point pt, ULong flags, Point* distance);		// ROM 0x0025df5c FindView__5TViewF6TPointUlP6TPoint
	TView*		FindClosestView(Point pt, ULong flags, long* distance, Point* delta, Boolean* clipped);	// ROM 0x0025de40
	long		Distance(Point pt, Point* delta);						// ROM 0x0025dd38 Distance__5TViewF6TPointP6TPoint
	void		Select(Boolean on, Boolean unique);						// ROM 0x00264c34 Select__5TViewFUcT1
	Boolean		HandleKeyEvent(RefArg cmd, ULong id, Boolean* isCommandKey);	// ROM 0x00267d00 HandleKeyEvent__5TViewFRC6RefVarUlPUc
	void		SelectNone(void);										// ROM 0x002643c4 SelectNone__5TViewFv
	TView*		FindID(long id);										// ROM 0x00265460 FindID__5TViewFl
	TView*		FrontMost(void);										// ROM 0x0025f3d4 FrontMost__5TViewFv
	TView*		FrontMostApp(void);										// ROM 0x0025f448 FrontMostApp__5TViewFv
	Ref			ChildViewFrames(void);									// ROM 0x0025f328 ChildViewFrames__5TViewFv
	Ref			Children(void);											// ROM 0x00268758 Children__5TViewFv
	TView*		GetWindowView(void);									// ROM 0x00263d10 GetWindowView__5TViewFv
	TView*		NextKeyView(TView* focus, long direction, long kind);	// ROM 0x002683a0 NextKeyView__5TViewFP5TViewlT2 - the next (direction 1) or previous (-1) key view in the tab order
	Boolean		ProtoedFrom(RefArg proto);								// ROM 0x00268200 ProtoedFrom__5TViewFRC6RefVar
	TClipper*	Clipper(void) const;									// ROM 0x00268830 Clipper__5TViewCFv
	Boolean		HasVisRgn(void) const;									// ROM 0x00268898 HasVisRgn__5TViewCFv
	Boolean		VisibleDeep(void) const;								// ROM 0x00260a18 VisibleDeep__5TViewCFv

	// the context and its slots
	Ref			DataFrame(void);										// ROM 0x00269038 DataFrame__5TViewFv
	Ref			Hilites(void);											// ROM 0x0025fe3c Hilites__5TViewFv - the hilites slot (the view's selections)
	Ref			FirstHilite(void);										// ROM 0x0025fef8 FirstHilite__5TViewFv - the first, or nil
	Ref			GetProto(RefArg slot) const;							// ROM 0x00269074 GetProto__5TViewCFRC6RefVar
	Ref			GetVar(RefArg slot) const;								// ROM 0x00269080 GetVar__5TViewCFRC6RefVar
	Ref			GetWriteableProtoVariable(RefArg slot);					// ROM 0x00269090 GetWriteableProtoVariable__5TViewFRC6RefVar
	Ref			GetWriteableVariable(RefArg slot);						// ROM 0x00269134 GetWriteableVariable__5TViewFRC6RefVar
	void		SetContextSlot(RefArg slot, RefArg value);				// ROM 0x002691b4 SetContextSlot__5TViewFRC6RefVarT1
	void		SetDataSlot(RefArg slot, RefArg value);					// ROM 0x002691bc SetDataSlot__5TViewFRC6RefVarT1
	Ref			GetCacheProto(long index);								// ROM 0x0025d474 GetCacheProto__5TViewFl
	Ref			GetCacheVariable(long index);							// ROM 0x0025d3e4 GetCacheVariable__5TViewFl
	void		InvalidateSlotCache(long index);						// ROM 0x00261d94 InvalidateSlotCache__5TViewFl
	Ref			RunScript(RefArg tag, RefArg args, Boolean lookupVars = false, Boolean* ran = nil);		// ROM 0x00261dc8
	Ref			RunCacheScript(long index, RefArg args, Boolean lookupVars = false, Boolean* ran = nil);	// ROM 0x00261c84
	void		Sync(void);												// ROM 0x0025d730 Sync__5TViewFv
	void		SetFlags(ULong flags);									// ROM 0x0025d360 SetFlags__5TViewFUl
	void		ClearFlags(ULong flags);								// ROM 0x00268c78 ClearFlags__5TViewFUl
	Ref			GetTextStyle(void);										// ROM 0x0025f8d4 GetTextStyle__5TViewFv
	void		GetTextStyleRecord(StyleRecord* style);					// ROM 0x0025f948 GetTextStyleRecord__5TViewFP11StyleRecord
	Boolean		Printing(void);											// ROM 0x0025fe0c Printing__5TViewFv

	// bounds
	void		JustifyBounds(Rect* bounds);							// ROM 0x00262224 JustifyBounds__5TViewFP5TRect
	void		DejustifyBounds(Rect* bounds);							// ROM 0x00262b1c DejustifyBounds__5TViewFP5TRect
	void		RecalcBounds(void);										// ROM 0x0026336c RecalcBounds__5TViewFv
	void		WriteBounds(const Rect& bounds);						// ROM 0x00261ff0 WriteBounds__5TViewFRC5TRect
	void		Move(const Point& delta);								// ROM 0x00261ee4 Move__5TViewFRC6TPoint
	void		Offset(Point delta);									// ROM 0x0025df8c Offset__5TViewF6TPoint
	Boolean		Drag(TStrokePublic* stroke, const Rect& limit);			// ROM 0x00264cbc Drag__5TViewFP13TStrokePublicRC5TRect (the view dragged with the pen within the limit; ==> whether it moved)
	void		ChildViewMoved(TView* child, Point delta);				// ROM 0x0025e0f0 ChildViewMoved__5TViewFP5TView6TPoint
	void		GetChildOrigin(Point* origin);							// ROM 0x00265520 GetChildOrigin__5TViewFP6TPoint
	Point		ContentsOrigin(void);									// ROM 0x002655cc ContentsOrigin__5TViewFv
	Point		LocalOrigin(void) const;								// ROM 0x00261e6c LocalOrigin__5TViewCFv
	void		SetOrigin(Point& origin);								// ROM 0x002633fc SetOrigin__5TViewFR6TPoint
	long		ChildrenHeight(long* count);							// ROM 0x002636e4 ChildrenHeight__5TViewFPl
	long		SetChildrenVertical(long top, long spacing);			// ROM 0x00263760 SetChildrenVertical__5TViewFlT1

	// showing and drawing
	void		Show(void);												// ROM 0x00263f48 Show__5TViewFv
	void		ViewVisibleChanged(TView* child, Boolean invalidate);		// ROM 0x00263d48 ViewVisibleChanged__5TViewFP5TViewUc
	TRegion		SetupVisRgn(void) const;								// ROM 0x00265a3c SetupVisRgn__5TViewCFv
	TRegion		GetFrontMask(void) const;								// ROM 0x00263b4c GetFrontMask__5TViewCFv
	void		Draw(const Rect& bounds, Boolean force);				// ROM 0x00265b54 Draw__5TViewFRC5TRectUc
	void		Draw(RgnHandle rgn, Boolean force);						// ROM 0x00265b90 Draw__5TViewF11TBaseRegionUc
	void		Update(RgnHandle rgn, TView* filler);					// ROM 0x00266050 Update__5TViewF11TBaseRegionP5TView
	void		DrawChildren(const Rect& bounds, TView* after);			// ROM 0x00266200 DrawChildren__5TViewFRC5TRectP5TView
	void		DrawChildren(RgnHandle rgn, TView* after);				// ROM 0x0026623c DrawChildren__5TViewF11TBaseRegionP5TView
	Boolean		SetCustomPattern(RefArg slot);							// ROM 0x00266308 SetCustomPattern__5TViewFRC6RefVar
	void		Dump(long depth);										// ROM 0x0025e33c Dump__5TViewFl

	// the fields (the ROM's offsets)
	long		fId;				// +0x04  a serial number
	ULong		fFlags;				// +0x08  viewFlags, with the private bits
	ULong		fViewFormat;		// +0x0c  viewFormat
	Rect		viewBounds;			// +0x10  in global coordinates
	ULong		fSlotCache;			// +0x18  slot cache indices 0-31: the slot may be there
	TView*		fParent;			// +0x1c
	TViewList*	fChildren;			// +0x20  gEmptyViewList when none
	RefStruct	fContext;			// +0x24  the context frame
	ULong		fViewJustify;		// +0x28  viewJustify (bits 0-29) and the private bits
	ULong		fSlotCache2;		// +0x2c  slot cache indices 32-33 (bits 0-1); bit 16 marks a deleted view

	static TViewList*	gEmptyViewList;		// 0x0c101a1c
	static long			gViewIdCounter;		// 0x0c102050
};

extern TRootView*	gRootView;				// 0x0c101a20
extern RefStruct*	gSlotCacheTable;		// 0x0c10204c slotCacheRefs: the ROM keeps a pointer to the 34 slot symbols of Rslotcachetable; the host the array (SlotCacheRef)
Ref			SlotCacheRef(long index);		// the slot symbol of a cache index
extern Boolean		gSkipVisRegions;		// 0x0c102054  Draw does not clip to the visible regions
extern Boolean		gDontDrawHilites;		// 0x0c100cb8  the selection hilites are not drawn (an effect in progress)
extern Boolean		gOutlineViews;			// 0x0c101a28  Draw frames every view in light gray
extern long			gSlowMotion;			// 0x0c101a2c  drawing shown step by step (NOT YET: unused)

// views from contexts
TView*		GetView(RefArg context);								// ROM 0x0025f4c4 GetView__FRC6RefVar
TView*		GetView(RefArg context, RefArg name);					// ROM 0x0025f5a8 GetView__FRC6RefVarT1
TView*		FailGetView(RefArg context);							// ROM 0x001efe1c FailGetView__FRC6RefVar
TView*		FailGetView(RefArg context, RefArg name);				// ROM 0x001f0258 FailGetView__FRC6RefVarT1
TView*		BuildView(TView* parent, RefArg context);				// ROM 0x0025ca18 BuildView__FP5TViewRC6RefVar
Ref			DoPopupMenu(RefArg rcvr, RefArg pickItems, RefArg x, RefArg y, RefArg callbackContext);	// ROM 0x001f2a3c FDoPopup__FRC6RefVarN41 - a popup menu opened over the items
TView*		Exists(TViewList* list, RefArg templ);					// ROM 0x0025fe48 Exists__FP9TViewListRC6RefVar
TView*		DataExists(TViewList* list, RefArg data);				// ROM 0x0025f84c DataExists__FP9TViewListRC6RefVar
Boolean		SoupEQ(RefArg a, RefArg b);								// ROM 0x002638e4 SoupEQ__FRC6RefVarT1
Boolean		ProtoEQ(RefArg a, RefArg b);							// ROM 0x00262a98 ProtoEQ__FRC6RefVarT1
Ref			GetCacheContext(RefArg templ);							// ROM 0x0025c598 GetCacheContext__FRC6RefVar
void		OuterBounds1(Rect* bounds, ULong viewFormat);			// ROM 0x00262114 OuterBounds1__FP5TRectUl
void		BadWickedNaughtyNoot(long which);						// ROM 0x001f18dc BadWickedNaughtyNoot__Fl
Boolean		GetPattern(RefArg spec, Boolean* owned, PatternHandle* pattern, Boolean wasOwned);	// ROM 0x0019a378 GetPattern__FRC6RefVarPUcPPP8PixelMapUc - a pattern from its NewtonScript form
Boolean		SetPattern(long index);									// ROM 0x000e4aa0 SetPattern__Fl - the pen pattern from a viewFormat pattern index
void		DisposeFgPattern(void);									// ROM 0x00303b70 DisposeFgPattern__Fv

void		InitViewPrototypes(void);		// host: the canonical context, data context and rect frames when no ROM is imported
void		InitViewSystem(void);			// host: the slot cache table, the prototypes, the root view (with the current port) - after InitObjects and InitGraf
void		RegisterViewNatives(void);		// the NewtonScript view functions (ViewNatives.cpp)
Ref			MakeViewMethods(void);			// the methods a view inherits from the root template (Rviewroot's), as a frame

#endif	/* __VIEW_H */
