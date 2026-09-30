/*
	File:		views/ContainerView.h

	Contains:	TContainerView, the view that holds others and lets them be
				selected - the frame a page of a notebook application is
				laid out in, and the base of TEditView.

				What it adds to TDataView is a selection made of whole
				children rather than a range of anything: a TContainerHilite
				either says "all of me" (fComplete, drawn as a gray wash over
				the container) or stands for the children that are hilited
				themselves, and the container then hands DrawHilites,
				DrawHilitedData, GlobalHiliteBounds and RemoveHilite on to
				them.  GetHiliteView answers which view a selection really
				belongs to: the container when it is complete, else the first
				hilited child.

	Not in the DDK; reconstructed from the MP2x00 US ROM (0x000731e4-
	0x00074a00), each function citing its origin.  What the recogniser
	drives is handed to the children: a scrub (HandleScrub - a child scrubbed
	away entirely is removed), a caret or a line gesture (to the first
	visible child that takes it), a word (HandleWord: the visible child
	near it that bids most gets it), ink words (HandleInkWord), a tap
	(HandleTap: to the first child the point is over text in -
	PointOverText) and the hilite stroke (HandleHilite).  The editing that
	goes with TEditView: a selection made a view of its own (AddHilited -
	the whole container copied, CopyForm, or the selected child's part),
	deleted (DeleteHilited), the children's data added and removed with
	undo (RealDoCommand's aeAddData/aeRemoveData), the selection's offsets
	(GetValue 'hilites 'offset), the children to the right moved along
	when one grows wider (ChildBoundsChanged) and the caret asked of each
	child in turn (PointToCaret).
*/

#ifndef __CONTAINERVIEW_H
#define __CONTAINERVIEW_H

#ifndef __DATAVIEW_H
#include "DataView.h"
#endif

#ifndef __HILITES_H
#include "Hilites.h"
#endif


// A container's selection: the whole of it, or its hilited children.
class TContainerHilite : public THilite
{
public:
					TContainerHilite();						// the ROM makes one inline (MakeHilite, Clone)
	virtual			~TContainerHilite();

	virtual THilite* Clone(void);							// ROM 0x0007401c Clone__16TContainerHiliteFv
	void			CopyFrom(THilite* other);				// ROM 0x000740a4 CopyFrom__16TContainerHiliteFP7THilite

	Boolean			fComplete;			// +0x0c  the whole container, not a set of children
	TView*			fView;				// +0x10  the container the hilite belongs to
};


class TContainerView : public TDataView
{
public:
	virtual long	ClassID(void) const;					// ROM 0x000731e4 ClassID__14TContainerViewCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x000731ec DerivedFrom__14TContainerViewCFl
	virtual			~TContainerView();						// ROM 0x00074064 __dt__14TContainerViewFv
	virtual void	Constructor(RefArg context, TView* parent);	// ROM 0x00073930 Constructor__14TContainerViewFRC6RefVarP5TView

	virtual void	DrawHilitedData(void);					// ROM 0x000738c0 DrawHilitedData__14TContainerViewFv
	virtual void	DrawHilites(Boolean scaled);			// ROM 0x000737c8 DrawHilites__14TContainerViewFUc
	virtual Boolean	IsCompletelyHilited(RefArg hilite);		// ROM 0x00073c58 IsCompletelyHilited__14TContainerViewFRC6RefVar
	virtual long	HandleHilite(TUnitPublic* unit, long kind, Boolean reallyDoIt);	// ROM 0x00074770 HandleHilite__14TContainerViewFP11TUnitPubliclUc
	virtual void	HiliteAll(void);						// ROM 0x00073220 HiliteAll__14TContainerViewFv
	virtual void	RemoveHilite(RefArg hilite);			// ROM 0x00073bc4 RemoveHilite__14TContainerViewFRC6RefVar
	virtual void	RemoveAllHilites(void);					// ROM 0x00073254 RemoveAllHilites__14TContainerViewFv - TView's, word for word
	virtual long	GlobalHiliteBounds(Rect* bounds);		// ROM 0x000736e8 GlobalHiliteBounds__14TContainerViewFP5TRect
	virtual long	ClickOptions(void);						// ROM 0x00073928 ClickOptions__14TContainerViewFv
	virtual long	HandleScrub(const Rect& bounds, long kind, TUnitPublic* unit, Boolean reallyDoIt);	// ROM 0x00073258 HandleScrub__14TContainerViewFRC5TRectlP11TUnitPublicUc
	virtual void	DeleteHilited(RefArg hilite);			// ROM 0x00073a9c DeleteHilited__14TContainerViewFRC6RefVar
	virtual Boolean	RealDoCommand(RefArg cmd);				// ROM 0x000740cc RealDoCommand__14TContainerViewFRC6RefVar
	virtual Ref		GetValue(RefArg slot, RefArg type);		// ROM 0x00074240 GetValue__14TContainerViewFRC6RefVarT1
	virtual void	ChildBoundsChanged(TView* child, Rect& bounds);	// ROM 0x000743c0 ChildBoundsChanged__14TContainerViewFP5TViewR5TRect
	virtual void	PointToCaret(Point& pt, Rect* caret, Rect* bounds);	// ROM 0x000746ec PointToCaret__14TContainerViewFR6TPointP5TRectT2

	// TDataView's
	virtual void	HandleTap(Point& pt);					// ROM 0x00073ee8 HandleTap__14TContainerViewFR6TPoint
	virtual long	HandleCaret(ULong kind, long angle, Point& armA, Point& point,
								Point& armB, Point& tail);	// ROM 0x00073454 HandleCaret__14TContainerViewFUllR6TPointN33
	virtual long	HandleLineGesture(long angle, Point& from, Point& to);	// ROM 0x000734fc HandleLineGesture__14TContainerViewFlR6TPointT2
	virtual TView*	AddHilited(RefArg hilite, class TEditView* editor);	// ROM 0x00073970 AddHilited__14TContainerViewFRC6RefVarP9TEditView
	virtual Boolean	PointOverText(Point& pt, Point* onLine);	// ROM 0x00073e68 PointOverText__14TContainerViewFR6TPointP6TPoint
	virtual long	HandleWord(const UniChar* text, ULong length, const Rect& box,
							   const Point& pt, ULong a, ULong b, RefArg word,
							   Boolean flag, long* outOffset, TUnitPublic* unit);	// ROM 0x00073cf8 HandleWord__14TContainerViewFPCUsUlRC5TRectRC6TPointN22RC6RefVarUcPlP11TUnitPublic

	// the container's own
	virtual void	MakeHilite(long child, TView* view);	// ROM 0x000748d0 MakeHilite__14TContainerViewFlP5TView
	virtual Ref		CopyForm(void);							// ROM 0x00073fd4 CopyForm__14TContainerViewFv - a clone of the data frame (vtable +0x164)
	// An ink word written over the container: the visible children whose
	// boxes it touches (with five pixels of slack) are asked how well
	// they would take it, and the best one gets it.
	virtual long	HandleInkWord(RefArg cmd, Boolean reallyDoIt);		// ROM 0x00073584 HandleInkWord__14TContainerViewFRC6RefVarUc (vtable +0x12c)
	virtual TView*	GetHiliteView(void);					// ROM 0x00073c78 GetHiliteView__14TContainerViewFv

	long			fGap;				// +0x30  5 from the constructor: the room ChildBoundsChanged keeps between a child and the next to its right
	long			fUnknown34;			// +0x34  2
};

#endif	/* __CONTAINERVIEW_H */
