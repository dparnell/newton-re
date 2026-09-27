/*
	File:		views/MeetingView.h

	Contains:	TMeetingView (class 95), one meeting in the Dates day view.

				A meeting view is a container of two children: a slider (the
				bar down its left that says how long the meeting is, child 0)
				and the meeting's text (a paragraph, child 1).  Its icon - the
				context's `iconShape`, drawn to the right of the slider - is
				what the pen picks the meeting up by: a tap on it runs the
				viewClickScript, a drag carries the meeting away as a
				'meeting (or its text as 'text).  Everything else - writing,
				scrubbing, hiliting, dropping - it hands to the text.

				LayoutMeeting is the day view's arithmetic for where a meeting
				goes: its start and end as a top and bottom down the day, and
				its left and right from the space the day view has.

	Reconstructed from the MP2x00 US ROM (0x001ca1a8-0x001cbd54); each
	function cites its origin.
*/

#ifndef __MEETINGVIEW_H
#define __MEETINGVIEW_H

#include "ContainerView.h"

class TMeetingView : public TContainerView
{
public:
	virtual long	ClassID(void) const;					// ROM 0x001ca394 ClassID__12TMeetingViewCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x001ca448 DerivedFrom__12TMeetingViewCFl
	virtual void	Constructor(RefArg context, TView* parent);	// ROM 0x001ca47c Constructor__12TMeetingViewFRC6RefVarP5TView
	virtual Boolean	RealDoCommand(RefArg cmd);				// ROM 0x001ca49c RealDoCommand__12TMeetingViewFRC6RefVar
	virtual void	DrawHilitedData(void);					// ROM 0x001cadac DrawHilitedData__12TMeetingViewFv
	virtual long	HandleHilite(TUnitPublic* unit, long kind, Boolean reallyDoIt);	// ROM 0x001cb1bc HandleHilite__12TMeetingViewFP11TUnitPubliclUc
	virtual long	HandleScrub(const Rect& bounds, long kind, TUnitPublic* unit, Boolean on);	// ROM 0x001cab9c HandleScrub__12TMeetingViewFRC5TRectlP11TUnitPublicUc
	virtual Boolean	Hilited(void);							// ROM 0x001caee0 Hilited__12TMeetingViewFv
	virtual void	DrawHilites(Boolean on);				// ROM 0x001cad0c DrawHilites__12TMeetingViewFUc
	virtual void	DeleteHilited(RefArg hilite);			// ROM 0x001cb57c DeleteHilited__12TMeetingViewFRC6RefVar
	virtual void	RemoveHilite(RefArg hilite);			// ROM 0x001cb480 RemoveHilite__12TMeetingViewFRC6RefVar
	virtual void	RemoveAllHilites(void);					// ROM 0x001cb51c RemoveAllHilites__12TMeetingViewFv
	virtual long	GlobalHiliteBounds(Rect* bounds);		// ROM 0x001caf34 GlobalHiliteBounds__12TMeetingViewFP5TRect
	virtual Boolean	AddDragInfo(TDragInfo* dragInfo);		// ROM 0x001cb620 AddDragInfo__12TMeetingViewFP9TDragInfo
	virtual Ref		GetDropData(RefArg dragType, RefArg dragRef);	// ROM 0x001cb7a4 GetDropData__12TMeetingViewFRC6RefVarT1
	virtual Boolean	Drop(RefArg dropType, RefArg dropData, Point* dropPt);	// ROM 0x001cbc80 Drop__12TMeetingViewFRC6RefVarT1P6TPoint
	virtual Boolean	DropRemove(RefArg dragRef);				// ROM 0x001cbcd4 DropRemove__12TMeetingViewFRC6RefVar
	virtual Boolean	DropDone(void);							// ROM 0x001cbd18 DropDone__12TMeetingViewFv
	virtual Boolean	DragFeedback(const TDragInfo& dragInfo, const Point& pt, Boolean show);	// ROM 0x001cb74c DragFeedback__12TMeetingViewFRC9TDragInfoRC6TPointUc
	virtual Ref		GetSupportedDropTypes(const Point& pt);	// ROM 0x001cb6ec GetSupportedDropTypes__12TMeetingViewFRC6TPoint
	// (AddHilited, ROM 0x001cad08, is a branch to TContainerView's.)
	virtual TView*	GetTextView(void);						// ROM 0x001cacf0 GetTextView__12TMeetingViewFv - child 1
	virtual long	HandleWord(const UniChar* text, ULong length, const Rect& box,
							   const Point& pt, ULong a, ULong b, RefArg word,
							   Boolean reallyDoIt, long* outOffset, TUnitPublic* unit);	// ROM 0x001cafb4 HandleWord__12TMeetingViewFPCUsUlRC5TRectRC6TPointN22RC6RefVarUcPlP11TUnitPublic
	virtual void	HiliteText(long offset, long length, Boolean on);	// ROM 0x001cb3f4 HiliteText__12TMeetingViewFlT1Uc
	virtual void	MakeHilite(long child, TView* view);	// ROM 0x001cb2a0 MakeHilite__12TMeetingViewFlP5TView
	// the meeting view's own virtual (vtable +0x168)
	virtual TView*	GetSliderView(void);					// ROM 0x001cacfc GetSliderView__12TMeetingViewFv - child 0

	long			HandleClick(RefArg cmd);				// ROM 0x001ca52c HandleClick__12TMeetingViewFRC6RefVar - the pen on the icon

	ULong			fLastWordTime;		// +0x38  when the last word written on it was (Ticks at first)
};

// A meeting frame's slot, or (not viewBounds) its repeat template's.
Ref		GetMeetingSlot(RefArg meeting, RefArg slot);				// ROM 0x001ca39c GetMeetingSlot__FRC6RefVarT1
// A meeting's text: its repeat template's mtgText, else its own.
Ref		GetMeetingText(RefArg meeting);								// ROM 0x001cae5c GetMeetingText__FRC6RefVar
// The hilite a click put on a meeting view's context taken off and freed.
void	DeleteMeetingHilite(RefArg context);						// ROM 0x001cb64c DeleteMeetingHilite__FRC6RefVar

// :LayoutMeeting(meeting, extent, width, x) - the meeting's box in a day
// view `extent` pixels tall and `width` wide.
Ref		LayoutMeeting(RefArg rcvr, RefArg meeting, RefArg extent, RefArg width, RefArg x);	// ROM 0x001ca1a8 LayoutMeeting__FRC6RefVarN41

// GetMeetingTypeInfo(meeting), GetMeetingIcon(meeting): the kind of
// meeting it is out of the Dates application's meetingTypeRegistry.
Ref		FGetMeetingTypeInfo(RefArg rcvr, RefArg meeting);			// ROM 0x001cbed0 FGetMeetingTypeInfo
Ref		FGetMeetingIcon(RefArg rcvr, RefArg meeting);				// ROM 0x001cc0e0 FGetMeetingIcon

void	RegisterMeetingViewNatives(void);

#endif	/* __MEETINGVIEW_H */
