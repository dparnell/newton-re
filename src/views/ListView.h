/*
	File:		views/ListView.h

	Contains:	TListView (class 99), the outline list the To Do list and
				the Notepad's checklist and outline stationery are built on.

				A list view is an edit view whose context holds `topics`, an
				array of topic frames - each its `text` and `styles`, its
				`viewBounds` in the list, its `level` (1 at the left) and a
				`hideCount` (not nil and not nought: it is inside a collapsed
				topic).  SetupVisibleChildren makes a paragraph child for
				each visible topic from `firstTopic` on (out of the
				context's `canonicalParaTopic`), laid out one below the
				other by AdjustParagraph; RealDraw draws each topic's marker
				in the gutter to the left - a triangle that says whether it
				is collapsed, or a To Do item's priority - and, when the
				list has check boxes, the box.

				The pen: a tap in a marker collapses or expands the topic
				(the context's `toggleTopic`), a drag from it moves the
				topic and everything under it (a 'topic drag whose drop
				caret PointToCaret puts between two topics at the level the
				pen's x says), a tap in the box checks it (`handleCheck`),
				and a scrub over topics asks `handleScrub`.

				`listViewFlags`: 1 there is a gutter to the left of the
				markers, 2 the topics have check boxes, 4|8 the markers are
				priorities rather than triangles.

	Reconstructed from the MP2x00 US ROM (0x0010eec4-0x00112f74); each
	function cites its origin.
*/

#ifndef __LISTVIEW_H
#define __LISTVIEW_H

#include "EditView.h"

class TParagraphView;

class TListView : public TEditView
{
public:
	virtual long	ClassID(void) const;					// ROM 0x0010eec4 ClassID__9TListViewCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x0010eecc DerivedFrom__9TListViewCFl
	virtual void	Constructor(RefArg context, TView* parent);	// ROM 0x0010fc50 Constructor__9TListViewFRC6RefVarP5TView
	virtual Boolean	RealDoCommand(RefArg cmd);				// ROM 0x0010fbe8 RealDoCommand__9TListViewFRC6RefVar
	virtual void	RealDraw(Rect& bounds);					// ROM 0x0010fc70 RealDraw__9TListViewFR5TRect
	virtual void	PointToCaret(Point& pt, Rect* caret, Rect* bounds);	// ROM 0x0010f718 PointToCaret__9TListViewFR6TPointP5TRectT2 - the drop caret between two topics
	virtual void	DrawHilitedData(void);					// ROM 0x001120ac DrawHilitedData__9TListViewFv
	virtual Ref		GetDropData(RefArg dragType, RefArg dragRef);	// ROM 0x0010ef48 GetDropData__9TListViewFRC6RefVarT1
	virtual Boolean	DropMove(RefArg dragRef, const Point& delta, const Point& dropPt, Boolean copy);	// ROM 0x00112c28 DropMove__9TListViewFRC6RefVarRC6TPointT2Uc
	virtual Boolean	DropRemove(RefArg dragRef);				// ROM 0x00112d74 DropRemove__9TListViewFRC6RefVar
	virtual Boolean	DragFeedback(const TDragInfo& dragInfo, const Point& pt, Boolean show);	// ROM 0x00111120 DragFeedback__9TListViewFRC9TDragInfoRC6TPointUc
	virtual TView*	FindDropView(const TDragInfo& dragInfo, const Point& pt);	// ROM 0x00112e5c FindDropView__9TListViewFRC9TDragInfoRC6TPoint
	virtual void	HandleTap(Point& pt);					// ROM 0x0010f338 HandleTap__9TListViewFR6TPoint
	virtual long	Scrub(class TUnitPublic* unit);			// ROM 0x0010ff6c Scrub__9TListViewFP11TUnitPublic
	// the list view's own virtual (vtable +0x12c)
	virtual long	HandlePenDown(RefArg cmd);				// ROM 0x0010f014 HandlePenDown__9TListViewFRC6RefVar - a marker or a check box under the pen

	long		GadgetWidth(void);							// ROM 0x0010ef00 GadgetWidth__9TListViewFv - leftMarkGap + rightMarkGap
	long		IndexFromY(long y);							// ROM 0x0010f3e0 IndexFromY__9TListViewFl - the topic above which the y falls (-1 before the first)
	long		LevelFromX(long x, long index);				// ROM 0x0010f510 LevelFromX__9TListViewFlT1
	void		MarkerBounds(long index, Rect* bounds);		// ROM 0x0010f5e8 MarkerBounds__9TListViewFlR5TRect
	long		NTopics(void);								// ROM 0x0010f700 NTopics__9TListViewFv
	Ref			SetupVisibleChildren(long first, Boolean all, Boolean x);	// ROM 0x001101f0 SetupVisibleChildren__9TListViewFlUcT2
	Ref			Topic(long index);							// ROM 0x001107a0 Topic__9TListViewFl
	Ref			TopicIndexToView(long index);				// ROM 0x001107dc TopicIndexToView__9TListViewFl
	long		TopicLevel(long index);						// ROM 0x00110814 TopicLevel__9TListViewFl
	Ref			Topics(void);								// ROM 0x0011084c Topics__9TListViewFv
	Boolean		TopicVisible(long index);					// ROM 0x00110858 TopicVisible__9TListViewFl
	long		TrackCheck(RefArg cmd, long index);			// ROM 0x00110890 TrackCheck__9TListViewFRC6RefVarl
	long		TrackTopic(RefArg cmd, long index);			// ROM 0x00110b8c TrackTopic__9TListViewFRC6RefVarl
	void		DrawTopicMarker(long index, long which, short mode);	// ROM 0x00112944 DrawTopicMarker__9TListViewFlT1s
	long		FamilySize(long index);						// ROM 0x00112dc4 FamilySize__9TListViewFl - the topic and those under it

	long		fDragTopic;			// +0x50  the topic being dragged (-2: none)
	long		fDropIndex;			// +0x54  where PointToCaret would drop it (-2: nowhere)
	long		fDropLevel;			// +0x58  ... and at what level
};

// the topics as frames (the free functions the natives are made of)
long	TopicLevel(RefArg topic);									// ROM 0x00112a28 TopicLevel__FRC6RefVar - 1 when it has none
long	TopicTop(RefArg topic);										// ROM 0x00112a8c TopicTop__FRC6RefVar
Boolean	TopicVisible(RefArg topic);									// ROM 0x00112af0 TopicVisible__FRC6RefVar - its hideCount nil or 0
long	TopicHeight(RefArg topic);									// ROM 0x001129cc TopicHeight__FRC6RefVar
long	VisibleTopicIndex(RefArg context, long index);				// ROM 0x00112b64 VisibleTopicIndex__FRC6RefVarl - its child's index
void	FixTopic(RefArg topic, short top);							// ROM 0x00111b20 FixTopic__FRC6RefVars - its box moved to the top given
Ref		MakeDragRef(TListView* list, long index);					// ROM 0x00111bc4 MakeDragRef__FP9TListViewl
void	MarkerBounds(RefArg context, RefArg topic, Rect* bounds);	// ROM 0x00111db4 MarkerBounds__FRC6RefVarT1R5TRect
void	AdjustParagraph(RefArg context, TParagraphView* prev, TParagraphView* para, long left, long right);	// ROM 0x00111208 AdjustParagraph__FRC6RefVarP14TParagraphViewT2lT4
void	DrawCheck(RefArg context, RefArg topic, long flags, long checked);	// ROM 0x001116f0 DrawCheck__FRC6RefVarT1lT3
void	DrawPriority(RefArg context, RefArg topic, long flags, short mode);	// ROM 0x00111870 DrawPriority__FRC6RefVarT1ls
void	DrawTopicMarker(RefArg context, RefArg topic, long which, short mode);	// ROM 0x00111a1c DrawTopicMarker__FRC6RefVarT1ls

// the natives (the list view methods a script sees)
Ref		FAdjustParagraph(RefArg rcvr, RefArg prev, RefArg para, RefArg left, RefArg right);	// ROM 0x001113c8 FAdjustParagraph
Ref		FChildTemplateFromTopic(RefArg rcvr, RefArg topic);		// ROM 0x00111488 FChildTemplateFromTopic
Ref		FCollapseTopic(RefArg rcvr, RefArg index, RefArg redo);	// ROM 0x00111ec8 FCollapseTopic
Ref		FExpandTopic(RefArg rcvr, RefArg index, RefArg redo);	// ROM 0x00112260 FExpandTopic
Ref		FFamilyBottom(RefArg rcvr, RefArg index);				// ROM 0x00112450 FFamilyBottom
Ref		FIsCollapsed(RefArg rcvr, RefArg index);				// ROM 0x00112558 FIsCollapsed
Ref		FListBottom(RefArg rcvr);								// ROM 0x00112658 FListBottom
Ref		FMakeDragRef(RefArg rcvr, RefArg index);				// ROM 0x00112784 FMakeDragRef
Ref		FMarkerBounds(RefArg rcvr, RefArg topic);				// ROM 0x001127c4 FMarkerBounds
Ref		FSetupVisibleChildren(RefArg rcvr, RefArg first, RefArg all, RefArg x);	// ROM 0x001127f4 FSetupVisibleChildren
Ref		FTopicBottom(RefArg rcvr, RefArg topic);				// ROM 0x00112864 FTopicBottom
Ref		FTopicIndexToView(RefArg rcvr, RefArg index);			// ROM 0x00112904 FTopicIndexToView
Ref		FVisibleTopicIndex(RefArg rcvr, RefArg index);			// ROM 0x00112990 FVisibleTopicIndex

void	RegisterListViewNatives(void);

#endif	/* __LISTVIEW_H */
