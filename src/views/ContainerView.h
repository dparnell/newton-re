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

	Not in the DDK; reconstructed from the MP2100 D ROM (0x00073b84-
	0x000753a0), each function citing its origin.  NOT YET: everything the
	recogniser drives (HandleWord, HandleInkWord, HandleCaret,
	HandleLineGesture, HandleScrub, HandleHilite, HandleTap, PointOverText),
	the editing that goes with TEditView (AddHilited, DeleteHilited,
	CopyForm, RealDoCommand, GetValue, ChildBoundsChanged, PointToCaret),
	and TEditView itself.
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

	virtual THilite* Clone(void);							// ROM 0x000749bc Clone__16TContainerHiliteFv
	void			CopyFrom(THilite* other);				// ROM 0x00074a44 CopyFrom__16TContainerHiliteFP7THilite

	Boolean			fComplete;			// +0x0c  the whole container, not a set of children
	TView*			fView;				// +0x10  the container the hilite belongs to
};


class TContainerView : public TDataView
{
public:
	virtual long	ClassID(void) const;					// ROM 0x00073b84 ClassID__14TContainerViewCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x00073b8c DerivedFrom__14TContainerViewCFl
	virtual			~TContainerView();						// ROM 0x00074a04 __dt__14TContainerViewFv
	virtual void	Constructor(RefArg context, TView* parent);	// ROM 0x000742d0 Constructor__14TContainerViewFRC6RefVarP5TView

	virtual void	DrawHilitedData(void);					// ROM 0x00074260 DrawHilitedData__14TContainerViewFv
	virtual void	DrawHilites(Boolean scaled);			// ROM 0x00074168 DrawHilites__14TContainerViewFUc
	virtual Boolean	IsCompletelyHilited(RefArg hilite);		// ROM 0x000745f8 IsCompletelyHilited__14TContainerViewFRC6RefVar
	virtual void	HiliteAll(void);						// ROM 0x00073bc0 HiliteAll__14TContainerViewFv
	virtual void	RemoveHilite(RefArg hilite);			// ROM 0x00074564 RemoveHilite__14TContainerViewFRC6RefVar
	virtual void	RemoveAllHilites(void);					// ROM 0x00073bf4 RemoveAllHilites__14TContainerViewFv - TView's, word for word
	virtual long	GlobalHiliteBounds(Rect* bounds);		// ROM 0x00074088 GlobalHiliteBounds__14TContainerViewFP5TRect
	virtual long	ClickOptions(void);						// ROM 0x000742c8 ClickOptions__14TContainerViewFv

	// the container's own
	virtual void	MakeHilite(long child, TView* view);	// ROM 0x00075270 MakeHilite__14TContainerViewFlP5TView
	TView*			GetHiliteView(void);					// ROM 0x00074618 GetHiliteView__14TContainerViewFv

	long			fUnknown30;			// +0x30  5 from the constructor; nothing reconstructed reads them
	long			fUnknown34;			// +0x34  2
};

#endif	/* __CONTAINERVIEW_H */
