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
				Constructor 0x00266368 and Dump 0x00260274 use them) and its
				methods are declared in the order of its vtable (0x0001f75c).
				TxObject and TResponder are the bases: TxObject the class-id
				root (ClassID, DerivedFrom, Key), TResponder the command
				receiver (DoCommand).  TViewList is a CList of TView*, walked
				by TListLoop (forwards) and TBackwardLoop.

				Select/Hilite invert a view (a button pressed) through the
				viewHiliteScript or InvertRect; the data hilites - what is
				selected inside a view - are kept as THilite objects in the
				`hilites` array and walked with HiliteLoop (Hilites.h), the
				base class keeping them and a data view drawing them.  NOT
				YET RECONSTRUCTED: the pen-driven HandleHilite/HandleScrub,
				the key view chain (BuildKeyChildList, NextKeyView;
				HandleKeyEvent runs the key scripts and the key commands -
				Keyboard.h; RealDoCommand answers the other commands -
				Commands.h), the animation effects
				(TAnimate: Show and Hide draw at once), the stroke world, the
				sound effects, gSlowMotion, SyncScroll, and
				the subclasses (BuildView makes a TView for every class).

	Reconstructed from the MP2x00 US ROM (0x0025e4d0-0x0026b138,
	0x00066258-0x000663cc); each function cites its origin.
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
	static void*	operator new(size_t size);				// ROM 0x00143738 __nw__8TxObjectSFUi  (the memory cleared)
	static void		operator delete(void* p);				// ROM 0x00143774 __dl__8TxObjectSFPv
	virtual long	ClassID(void) const;					// ROM 0x00143790 ClassID__8TxObjectCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x00143798 DerivedFrom__8TxObjectCFl
	virtual			~TxObject();							// ROM 0x00143778 __dt__8TxObjectFv
	virtual ULong	Key(void) const;						// ROM 0x001437ac Key__8TxObjectCFv
};

class TResponder : public TxObject
{
public:
	virtual long	ClassID(void) const;					// ROM 0x001a9354 ClassID__10TResponderCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x001a935c DerivedFrom__10TResponderCFl
	virtual Boolean	DoCommand(RefArg cmd);					// ROM 0x001a9390 DoCommand__10TResponderFRC6RefVar
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
				TClipper();										// ROM 0x00066258 __ct__8TClipperFv
	void		UpdateRegions(TView* view);						// ROM 0x0006629c UpdateRegions__8TClipperFP5TView
	void		Offset(Point delta);							// ROM 0x0006635c Offset__8TClipperF6TPoint
	void		RecalcVisible(RgnHandle inFront);				// ROM 0x00066388 RecalcVisible__8TClipperF11TBaseRegion

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
	// the vtable's order (analysis/vtable.py build/MP2x00US 0x1f750)
	virtual long	ClassID(void) const;								// ROM 0x0025f290 ClassID__5TViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x002635c0 DerivedFrom__5TViewCFl
	virtual			~TView();											// ROM 0x00268af4 __dt__5TViewFv
	virtual Boolean	DoCommand(RefArg cmd);								// ROM 0x00268bcc DoCommand__5TViewFRC6RefVar
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x00266368 Constructor__5TViewFRC6RefVarP5TView
	virtual void	Delete(void);										// ROM 0x00267584 Delete__5TViewFv
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x00268d38 RealDoCommand__5TViewFRC6RefVar
	virtual long	TextFlags(void) const;								// ROM 0x0025fbdc TextFlags__5TViewCFv
	virtual void	OuterBounds(Rect* bounds);							// ROM 0x002640d0 OuterBounds__5TViewFP5TRect
	virtual Boolean	InsideView(Point& pt);								// ROM 0x0025fc34 InsideView__5TViewFR6TPoint
	virtual void	SetBounds(const Rect& bounds);						// ROM 0x0026555c SetBounds__5TViewFRC5TRect
	virtual void	ChildBoundsChanged(TView* child, Rect& bounds);		// ROM 0x002657cc ChildBoundsChanged__5TViewFP5TViewR5TRect
	virtual void	SetupForm(void);									// ROM 0x002657d0 SetupForm__5TViewFv
	virtual void	SetupDone(void);									// ROM 0x002658c0 SetupDone__5TViewFv
	virtual void	Hide(void);											// ROM 0x00265f84 Hide__5TViewFv
	virtual Ref		GetRangeText(long start, long end);					// ROM 0x0026a800 GetRangeText__5TViewFlT1
	virtual Ref		GetValue(RefArg slot, RefArg type);					// ROM 0x0026a808 GetValue__5TViewFRC6RefVarT1
	virtual void	SetValue(RefArg slot, RefArg value);				// ROM 0x0026a9ec SetValue__5TViewFRC6RefVarT1
	virtual void	Changed(RefArg slot);								// ROM 0x0026ac34 Changed__5TViewFRC6RefVar
	virtual void	Changed(RefArg slot, RefArg context);				// ROM 0x0026ac40 Changed__5TViewFRC6RefVarT1
	virtual void	Dirty(const Rect* rect);							// ROM 0x0026ae20 Dirty__5TViewFPC5TRect
	virtual void	Hilite(Boolean on);									// ROM 0x002660c4 Hilite__5TViewFUc
	virtual void	SetCaretOffset(long* offset, long* length);			// ROM 0x0026a6a0 SetCaretOffset__5TViewFPlT1
	virtual void	SetSelection(RefArg selection, long* start, long* end);	// ROM 0x0026a69c SetSelection__5TViewFRC6RefVarPlT2
	virtual Ref		GetSelection(void);									// ROM 0x0026a688 GetSelection__5TViewFv
	virtual void	ActivateSelection(Boolean on);						// ROM 0x0026a6a4 ActivateSelection__5TViewFUc
	virtual Boolean	DoEditCommand(long command);						// ROM 0x0009e7ec DoEditCommand__5TViewFl
	virtual void	OffsetToCaret(long offset, Rect* caret);			// ROM 0x0026a748 OffsetToCaret__5TViewFlP5TRect
	virtual void	PointToCaret(Point& pt, Rect* caret, Rect* bounds);	// ROM 0x0026a728 PointToCaret__5TViewFR6TPointP5TRectT2
	virtual void	NarrowVisByIntersectingObscuringSiblingsAndUncles(TView* upTo, Rect* bounds);	// ROM 0x002639d4
	virtual void	RemoveAllViews(void);								// ROM 0x0025fa5c RemoveAllViews__5TViewFv
	virtual long	Idle(long arg);										// ROM 0x00268b3c Idle__5TViewFl
	virtual void	DrawHiliting(void);									// ROM 0x00267188 DrawHiliting__5TViewFv
	virtual void	DrawHilitedData(void);								// ROM 0x0026715c DrawHilitedData__5TViewFv
	virtual Boolean	HandleHilite(TUnitPublic* unit, long arg, Boolean on);	// ROM 0x00262150 HandleHilite__5TViewFP11TUnitPubliclUc
	virtual Boolean	HandleScrub(const Rect& bounds, long arg, TUnitPublic* unit, Boolean on);	// ROM 0x00262528 HandleScrub__5TViewFRC5TRectlP11TUnitPublicUc
	virtual Boolean	Hilited(void);										// ROM 0x00261de4 Hilited__5TViewFv
	virtual void	DrawHilites(Boolean on);							// ROM 0x00261e94 DrawHilites__5TViewFUc
	virtual Boolean	IsCompletelyHilited(RefArg hilite);					// ROM 0x00262000 IsCompletelyHilited__5TViewFRC6RefVar
	virtual void	HiliteAll(void);									// ROM 0x00262008 HiliteAll__5TViewFv
	virtual void	DeleteHilited(RefArg hilite);						// ROM 0x00262104 DeleteHilited__5TViewFRC6RefVar
	virtual void	RemoveHilite(RefArg hilite);						// ROM 0x00261e98 RemoveHilite__5TViewFRC6RefVar
	virtual void	RemoveAllHilites(void);								// ROM 0x00261f64 RemoveAllHilites__5TViewFv
	virtual long	GlobalHiliteBounds(Rect* bounds);					// ROM 0x002622d8 GlobalHiliteBounds__5TViewFP5TRect
	virtual void	GlobalHiliteResizeBounds(Rect* bounds);				// ROM 0x00262414 GlobalHiliteResizeBounds__5TViewFP5TRect
	virtual void	GlobalHilitePinnedBounds(Rect* bounds);				// ROM 0x0026244c GlobalHilitePinnedBounds__5TViewFP5TRect
	virtual Boolean	PointInHilite(Point& pt);							// ROM 0x00262454 PointInHilite__5TViewFR6TPoint
	virtual long	ClickOptions(void);									// ROM 0x00262568 ClickOptions__5TViewFv
	virtual void	DrawScaledData(const Rect& src, const Rect& dst, Rect* bounds);	// ROM 0x00262570 DrawScaledData__5TViewFRC5TRectT1P5TRect
	virtual Boolean	AddDragInfo(TDragInfo* dragInfo);					// ROM 0x0009e648 AddDragInfo__5TViewFP9TDragInfo (viewAddDragInfoScript)
	virtual Ref		GetDropData(RefArg dragType, RefArg dragRef);		// ROM 0x000a15c0 GetDropData__5TViewFRC6RefVarT1 (viewGetDropDataScript, else nil)
	virtual Boolean	DragAndDrop(TStrokePublic* stroke, const Rect& bounds, const Rect* limit, const Rect* slop, Boolean copy, const TDragInfo& dragInfo, const Rect* dragBounds);	// ROM 0x0009d194 DragAndDrop__5TViewFP13TStrokePublicRC5TRectPC5TRectT3UcRC9TDragInfoT3
	virtual void	DrawDragBackground(const Rect& bounds, Boolean copy);
	virtual void	DrawDragData(const Rect& bounds);
	virtual Boolean	GetClipboardDataBits(Rect* bounds);
	virtual Boolean	AcceptDrop(const TDragInfo& dragInfo, const Point& pt);	// ROM 0x000a12c0 AcceptDrop__5TViewFRC9TDragInfoRC6TPoint
	virtual Boolean	Drop(RefArg dropTypes, RefArg dropData, Point* dropPt);	// ROM 0x0009cbc4 Drop__5TViewFRC6RefVarT1P6TPoint (viewDropScript)
	virtual Boolean	DropMove(RefArg dragRef, const Point& oldPt, const Point& newPt, Boolean copy);	// ROM 0x000a13e4 DropMove__5TViewFRC6RefVarRC6TPointT2Uc (viewDropMoveScript)
	virtual Boolean	DropRemove(RefArg dragRef);							// ROM 0x0009cc90 DropRemove__5TViewFRC6RefVar (viewDropRemoveScript)
	virtual Boolean	DropDone(void);										// ROM 0x0009d134 DropDone__5TViewFv
	virtual Boolean	DropApprove(TView* target);							// ROM 0x0009cd10 DropApprove__5TViewFP5TView (viewDropApproveScript)
	virtual TView*	TargetDrop(const TDragInfo& dragInfo, const Point& pt);	// ROM 0x0009d5c8 TargetDrop__5TViewFRC9TDragInfoRC6TPoint
	virtual void	BuildKeyChildList(TViewList* list, long arg1, long arg2);	// ROM 0x0026a1c8 BuildKeyChildList__5TViewFP9TViewListlT2
	virtual void	SimpleOffset(Point delta, Boolean inChildren);		// ROM 0x0025ff9c SimpleOffset__5TViewF6TPointl
	virtual void	PreDraw(Rect& bounds);								// ROM 0x002682a8 PreDraw__5TViewFR5TRect
	virtual void	PostDraw(Rect& bounds);								// ROM 0x002685f8 PostDraw__5TViewFR5TRect
	virtual void	RealDraw(Rect& bounds);								// ROM 0x002685f4 RealDraw__5TViewFR5TRect
	virtual void	Scale(const Rect& src, const Rect& dst);			// ROM 0x002625f4 Scale__5TViewFRC5TRectT1
	virtual void	EndDrag(const TDragInfo& dragInfo, TView* target, const Point& startPt, const Point& dropPt, const Point& dragPt, Boolean copy);	// ROM 0x0009cdb4 EndDrag__5TViewFRC9TDragInfoP5TViewRC6TPointN23Uc
	virtual void	DragFeedback(const TDragInfo& dragInfo, const Point& pt, Boolean copy);
	virtual Ref		GetSupportedDropTypes(const Point& pt);
	virtual TView*	FindDropView(const TDragInfo& dragInfo, const Point& pt);

	// making and structure
	static Ref		BuildContext(RefArg templ, Boolean forceVisible);		// ROM 0x0025e56c BuildContext__5TViewFRC6RefVarUc  (the view is unused: a static here)
	TView*		AddView(RefArg templ);									// ROM 0x0025f1ac AddView__5TViewFRC6RefVar
	void		AddView(TView* child);									// ROM 0x0025f8cc AddView__5TViewFP5TView
	TView*		AddChild(RefArg templ);									// ROM 0x00265e4c AddChild__5TViewFRC6RefVar
	void		AddViews(Boolean sync);									// ROM 0x00262c0c AddViews__5TViewFUc
	void		RemoveView(void);										// ROM 0x0025f960 RemoveView__5TViewFv
	void		RemoveChildView(TView* child);							// ROM 0x0025f96c RemoveChildView__5TViewFP5TView
	void		RemoveUnmarked(void);									// ROM 0x002623a0 RemoveUnmarked__5TViewFv
	void		ReorderView(TView* child, long index);					// ROM 0x00260cd8 ReorderView__5TViewFP5TViewl
	void		BringToFront(void);										// ROM 0x002611e8 BringToFront__5TViewFv
	void		MoveChildBehind(TView* child, TView* behind);			// ROM 0x002611fc MoveChildBehind__5TViewFP5TViewT1
	TView*		AddToSoup(RefArg templ);								// ROM 0x0025f43c AddToSoup__5TViewFRC6RefVar
	void		RemoveFromSoup(TView* child);							// ROM 0x0025f5a4 RemoveFromSoup__5TViewFP5TView
	TView*		FindView(RefArg data);									// ROM 0x0025fb34 FindView__5TViewFRC6RefVar
	TView*		FindView(Point pt, ULong flags, Point* distance);		// ROM 0x0025fe94 FindView__5TViewF6TPointUlP6TPoint
	TView*		FindClosestView(Point pt, ULong flags, long* distance, Point* delta, Boolean* clipped);	// ROM 0x0025de40
	long		Distance(Point pt, Point* delta);						// ROM 0x0025fc70 Distance__5TViewF6TPointP6TPoint
	void		Select(Boolean on, Boolean unique);						// ROM 0x00266b6c Select__5TViewFUcT1
	Boolean		HandleKeyEvent(RefArg cmd, ULong id, Boolean* isCommandKey);	// ROM 0x00269c38 HandleKeyEvent__5TViewFRC6RefVarUlPUc
	void		SelectNone(void);										// ROM 0x002662fc SelectNone__5TViewFv
	TView*		FindID(long id);										// ROM 0x00267398 FindID__5TViewFl
	TView*		FrontMost(void);										// ROM 0x0026130c FrontMost__5TViewFv
	TView*		FrontMostApp(void);										// ROM 0x00261380 FrontMostApp__5TViewFv
	Ref			ChildViewFrames(void);									// ROM 0x00261260 ChildViewFrames__5TViewFv
	Ref			Children(void);											// ROM 0x0026a690 Children__5TViewFv
	TView*		GetWindowView(void);									// ROM 0x00265c48 GetWindowView__5TViewFv
	TView*		NextKeyView(TView* focus, long direction, long kind);	// ROM 0x0026a2d8 NextKeyView__5TViewFP5TViewlT2 - the next (direction 1) or previous (-1) key view in the tab order
	Boolean		ProtoedFrom(RefArg proto);								// ROM 0x0026a138 ProtoedFrom__5TViewFRC6RefVar
	TClipper*	Clipper(void) const;									// ROM 0x0026a768 Clipper__5TViewCFv
	Boolean		HasVisRgn(void) const;									// ROM 0x0026a7d0 HasVisRgn__5TViewCFv
	Boolean		VisibleDeep(void) const;								// ROM 0x00262950 VisibleDeep__5TViewCFv

	// the context and its slots
	Ref			DataFrame(void);										// ROM 0x0026af70 DataFrame__5TViewFv
	Ref			Hilites(void);											// ROM 0x00261d74 Hilites__5TViewFv - the hilites slot (the view's selections)
	Ref			FirstHilite(void);										// ROM 0x00261e30 FirstHilite__5TViewFv - the first, or nil
	Ref			GetProto(RefArg slot) const;							// ROM 0x0026afac GetProto__5TViewCFRC6RefVar
	Ref			GetVar(RefArg slot) const;								// ROM 0x0026afb8 GetVar__5TViewCFRC6RefVar
	Ref			GetWriteableProtoVariable(RefArg slot);					// ROM 0x0026afc8 GetWriteableProtoVariable__5TViewFRC6RefVar
	Ref			GetWriteableVariable(RefArg slot);						// ROM 0x0026b06c GetWriteableVariable__5TViewFRC6RefVar
	void		SetContextSlot(RefArg slot, RefArg value);				// ROM 0x0026b0ec SetContextSlot__5TViewFRC6RefVarT1
	void		SetDataSlot(RefArg slot, RefArg value);					// ROM 0x0026b0f4 SetDataSlot__5TViewFRC6RefVarT1
	Ref			GetCacheProto(long index);								// ROM 0x0025f3ac GetCacheProto__5TViewFl
	Ref			GetCacheVariable(long index);							// ROM 0x0025f31c GetCacheVariable__5TViewFl
	void		InvalidateSlotCache(long index);						// ROM 0x00263ccc InvalidateSlotCache__5TViewFl
	Ref			RunScript(RefArg tag, RefArg args, Boolean lookupVars = false, Boolean* ran = nil);		// ROM 0x00261dc8
	Ref			RunCacheScript(long index, RefArg args, Boolean lookupVars = false, Boolean* ran = nil);	// ROM 0x00261c84
	void		Sync(void);												// ROM 0x0025f668 Sync__5TViewFv
	void		SetFlags(ULong flags);									// ROM 0x0025f298 SetFlags__5TViewFUl
	void		ClearFlags(ULong flags);								// ROM 0x0026abb0 ClearFlags__5TViewFUl
	Ref			GetTextStyle(void);										// ROM 0x0026180c GetTextStyle__5TViewFv
	void		GetTextStyleRecord(StyleRecord* style);					// ROM 0x00261880 GetTextStyleRecord__5TViewFP11StyleRecord
	Boolean		Printing(void);											// ROM 0x00261d44 Printing__5TViewFv

	// bounds
	void		JustifyBounds(Rect* bounds);							// ROM 0x0026415c JustifyBounds__5TViewFP5TRect
	void		DejustifyBounds(Rect* bounds);							// ROM 0x00264a54 DejustifyBounds__5TViewFP5TRect
	void		RecalcBounds(void);										// ROM 0x002652a4 RecalcBounds__5TViewFv
	void		WriteBounds(const Rect& bounds);						// ROM 0x00263f28 WriteBounds__5TViewFRC5TRect
	void		Move(const Point& delta);								// ROM 0x00263e1c Move__5TViewFRC6TPoint
	void		Offset(Point delta);									// ROM 0x0025fec4 Offset__5TViewF6TPoint
	Boolean		Drag(TStrokePublic* stroke, const Rect& limit);			// ROM 0x00266bf4 Drag__5TViewFP13TStrokePublicRC5TRect (the view dragged with the pen within the limit; ==> whether it moved)
	void		ChildViewMoved(TView* child, Point delta);				// ROM 0x00260028 ChildViewMoved__5TViewFP5TView6TPoint
	void		GetChildOrigin(Point* origin);							// ROM 0x00267458 GetChildOrigin__5TViewFP6TPoint
	Point		ContentsOrigin(void);									// ROM 0x00267504 ContentsOrigin__5TViewFv
	Boolean		IsGridded(RefArg gridKind, Point* spacing);	// ROM 0x00262a20 IsGridded__5TViewFRC6RefVarP6TPoint - the viewGrid is this kind, and how far apart
	Point		LocalOrigin(void) const;								// ROM 0x00263da4 LocalOrigin__5TViewCFv
	void		SetOrigin(Point& origin);								// ROM 0x00265334 SetOrigin__5TViewFR6TPoint
	long		ChildrenHeight(long* count);							// ROM 0x0026561c ChildrenHeight__5TViewFPl
	long		SetChildrenVertical(long top, long spacing);			// ROM 0x00265698 SetChildrenVertical__5TViewFlT1

	// showing and drawing
	void		Show(void);												// ROM 0x00265e80 Show__5TViewFv
	void		ViewVisibleChanged(TView* child, Boolean invalidate);		// ROM 0x00265c80 ViewVisibleChanged__5TViewFP5TViewUc
	TRegion		SetupVisRgn(void) const;								// ROM 0x00267974 SetupVisRgn__5TViewCFv
	TRegion		GetFrontMask(void) const;								// ROM 0x00265a84 GetFrontMask__5TViewCFv
	void		Draw(const Rect& bounds, Boolean force);				// ROM 0x00267a8c Draw__5TViewFRC5TRectUc
	void		Draw(RgnHandle rgn, Boolean force);						// ROM 0x00267ac8 Draw__5TViewF11TBaseRegionUc
	void		Update(RgnHandle rgn, TView* filler);					// ROM 0x00267f88 Update__5TViewF11TBaseRegionP5TView
	void		DrawChildren(const Rect& bounds, TView* after);			// ROM 0x00268138 DrawChildren__5TViewFRC5TRectP5TView
	void		DrawChildren(RgnHandle rgn, TView* after);				// ROM 0x00268174 DrawChildren__5TViewF11TBaseRegionP5TView
	Boolean		SetCustomPattern(RefArg slot);							// ROM 0x00268240 SetCustomPattern__5TViewFRC6RefVar
	void		Dump(long depth);										// ROM 0x00260274 Dump__5TViewFl

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

extern TRootView*	gRootView;
extern long			gModalCount;		// ROM 0x0c105524 gModalCount - how many modal dialogs are up (NOT YET: the modal dialogs)				// 0x0c101a20
extern RefStruct*	gSlotCacheTable;		// 0x0c10204c slotCacheRefs: the ROM keeps a pointer to the 34 slot symbols of Rslotcachetable; the host the array (SlotCacheRef)
Ref			SlotCacheRef(long index);		// the slot symbol of a cache index
extern Boolean		gSkipVisRegions;		// 0x0c102054  Draw does not clip to the visible regions
extern Boolean		gDontDrawHilites;		// 0x0c100cb8  the selection hilites are not drawn (an effect in progress)
extern Boolean		gOutlineViews;			// 0x0c101a28  Draw frames every view in light gray
extern long			gSlowMotion;			// 0x0c101a2c  drawing shown step by step (NOT YET: unused)

// views from contexts
TView*		GetView(RefArg context);								// ROM 0x002613fc GetView__FRC6RefVar
TView*		GetView(RefArg context, RefArg name);					// ROM 0x002614e0 GetView__FRC6RefVarT1
TView*		FailGetView(RefArg context);							// ROM 0x001eda04 FailGetView__FRC6RefVar
TView*		FailGetView(RefArg context, RefArg name);				// ROM 0x001ede40 FailGetView__FRC6RefVarT1
TView*		BuildView(TView* parent, RefArg context);				// ROM 0x0025e950 BuildView__FP5TViewRC6RefVar
Ref			DoPopupMenu(RefArg rcvr, RefArg pickItems, RefArg x, RefArg y, RefArg callbackContext);	// ROM 0x001f0624 FDoPopup__FRC6RefVarN41 - a popup menu opened over the items
TView*		Exists(TViewList* list, RefArg templ);					// ROM 0x00261d80 Exists__FP9TViewListRC6RefVar
TView*		DataExists(TViewList* list, RefArg data);				// ROM 0x00261784 DataExists__FP9TViewListRC6RefVar
Boolean		SoupEQ(RefArg a, RefArg b);								// ROM 0x0026581c SoupEQ__FRC6RefVarT1
Boolean		ProtoEQ(RefArg a, RefArg b);							// ROM 0x002649d0 ProtoEQ__FRC6RefVarT1
Ref			GetCacheContext(RefArg templ);							// ROM 0x0025e4d0 GetCacheContext__FRC6RefVar
void		OuterBounds1(Rect* bounds, ULong viewFormat);			// ROM 0x0026404c OuterBounds1__FP5TRectUl
void		BadWickedNaughtyNoot(long which);						// ROM 0x001ef4c4 BadWickedNaughtyNoot__Fl
Boolean		GetPattern(RefArg spec, Boolean* owned, PatternHandle* pattern, Boolean wasOwned);	// ROM 0x00197d2c GetPattern__FRC6RefVarPUcPPP8PixelMapUc - a pattern from its NewtonScript form
Boolean		SetPattern(long index);									// ROM 0x000e37e8 SetPattern__Fl - the pen pattern from a viewFormat pattern index
void		DisposeFgPattern(void);									// ROM 0x00328e6c DisposeFgPattern__Fv

void		InitViewPrototypes(void);		// host: the canonical context, data context and rect frames when no ROM is imported
void		InitViewSystem(void);			// host: the slot cache table, the prototypes, the root view (with the current port) - after InitObjects and InitGraf
void		InitViewSystem(RefArg rootTemplate);	// ... built from this template instead of the host's (the ROM's Rviewroot: TNotebook::Constructor)
Ref			MakeRootTemplate(void);			// the ROM's Rviewroot with the view methods under it, or the host's stand-in when there are no ROM objects
void		RegisterViewNatives(void);		// the NewtonScript view functions (ViewNatives.cpp)
Ref			MakeViewMethods(void);			// the methods a view inherits from the root template (Rviewroot's), as a frame

#endif	/* __VIEW_H */
