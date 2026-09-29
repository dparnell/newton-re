/*
	File:		books/Outline.h

	Contains:	TOutline (view classes 102-105) and THelpOutline (106, 107):
				the book reader's outline, a table of contents drawn as a
				list of topics that open and close.

				The topics are a browser's list (Copperfield's browsers, a
				help book's browsers[0]): frames with a level (1 at the
				top), a name or an item (the content item the topic is,
				whose data is its text) and perhaps a pageNumber.  The
				outline keeps a Topic of eight bytes for each - its
				parent's index, its top within the list, and a word of
				flags - and draws the visible ones, one line each in the
				view's font, a topic with topics under it in bold and the
				one the book is at marked by a bar.  A tap on a topic with
				topics under it opens it (closing any other open topic
				that is not on its way) or closes it; a tap on any other
				turns the book to the topic's page (ClickCommand: Tiny
				Tim's through the library, Copperfield's through the page
				number and the browser).  The scroll arrows move the list
				a screenful less a line at a time.

				The Topic flags word, as the ROM keeps it:
				bits 28-31	the level
				bits 24-27	how many of its ancestors are closed (0: shown)
				bit 23		it has topics under it
				bit 22		it is shown
				bit 21		it is open
				bit 20		it is on the way to the topic AutoCollapse keeps open
				bits 16-17	its kind (1: the left half of a line, 2: the right
							half, sharing the line before it); nothing in the
							ROM sets them

	Reconstructed from the MP2x00 US ROM (0x0014c49c-0x0014eefc); each
	function cites its origin.
*/

#ifndef __OUTLINE_H
#define __OUTLINE_H

#ifndef __VIEW_H
#include "View.h"
#endif

const long clOutlineView = 105;			// (ViewFlags.h's clOutline; 102-104 are TOutlines too)
const long clHelpOutlineView = 107;		// (106 too)

// the Topic flags
const ULong kTopicLevelShift	= 28;
const ULong kTopicHiddenShift	= 24;
const ULong kTopicHiddenMask	= 0x0f000000;
const ULong kTopicHasChildren	= 0x00800000;
const ULong kTopicVisible		= 0x00400000;
const ULong kTopicExpanded		= 0x00200000;
const ULong kTopicOnPath		= 0x00100000;

// 8 bytes
struct Topic
{
	short		fParent;			// +0x00  the parent topic's index, -1 at the top level
	short		fTop;				// +0x02  its top in the list (0: the first line)
	ULong		fFlags;				// +0x04
};

inline ULong	TopicLevel(ULong flags)		{ return flags >> kTopicLevelShift; }
inline ULong	TopicKind(ULong flags)		{ return (flags & 0x3ffff) >> 16; }

class TOutline : public TView
{
public:
					TOutline();
	virtual			~TOutline();											// ROM 0x0014d3a0 __dt__8TOutlineFv
	virtual long	ClassID(void) const;									// ROM 0x0014c49c ClassID__8TOutlineCFv
	virtual Boolean	DerivedFrom(long id) const;								// ROM 0x0014c4a4 DerivedFrom__8TOutlineCFl
	virtual void	Constructor(RefArg context, TView* parent);				// ROM 0x0014ed64 Constructor__8TOutlineFRC6RefVarP5TView
	virtual Boolean	RealDoCommand(RefArg cmd);								// ROM 0x0014d400 RealDoCommand__8TOutlineFRC6RefVar
	virtual void	RealDraw(Rect& bounds);									// ROM 0x0014d5bc RealDraw__8TOutlineFR5TRect

	// the outline's own virtuals, in the ROM's vtable order (+0x11c on)
	virtual Ref		Browser(void);											// ROM 0x0014e7dc Browser__8TOutlineFv
	virtual void	AddTopic(long index, Topic* topic);						// ROM 0x0014dc50 AddTopic__8TOutlineFlP5Topic
	virtual void	AutoCollapse(long index);								// ROM 0x0014e330 AutoCollapse__8TOutlineFl
	virtual void	ClickCommand(long index);								// ROM 0x0014e854 ClickCommand__8TOutlineFl
	virtual long	CountTopics(void);										// ROM 0x0014eebc CountTopics__8TOutlineFv
	virtual void	Collapse(long index, Boolean draw);						// ROM 0x0014ec3c Collapse__8TOutlineFlUc
	virtual void	DoClick(Rect& bounds);									// ROM 0x0014c4d8 DoClick__8TOutlineFR5TRect
	virtual void	DrawTopic(long index, Rect& bounds);					// ROM 0x0014c6ec DrawTopic__8TOutlineFlR5TRect
	virtual long	DrawTopicRefs(long index, Rect& bounds);				// ROM 0x0014c980 DrawTopicRefs__8TOutlineFlR5TRect
	virtual void	Expand(long index, Boolean draw);						// ROM 0x0014c988 Expand__8TOutlineFlUc
	virtual long	FindPageInList(void);									// ROM 0x0014cb0c FindPageInList__8TOutlineFv
	virtual long	FindTopic(Point pt);									// ROM 0x0014cdf8 FindTopic__8TOutlineF6TPoint
	virtual void	InitTopics(void);										// ROM 0x0014cf20 InitTopics__8TOutlineFv
	virtual long	PageNumber(long index, RefArg item, long offset);		// ROM 0x0014d15c PageNumber__8TOutlineFlRC6RefVarT1
	virtual void	RefreshTopics(long pane);								// ROM 0x0014d6c0 RefreshTopics__8TOutlineFl
	virtual void	RevealTopic(long index);								// ROM 0x0014d798 RevealTopic__8TOutlineFl
	virtual long	ScrollPos(void);
	virtual void	SetScrollers(void);										// ROM 0x0014d964 SetScrollers__8TOutlineFv
	virtual void	ScrollToCurrent(void);									// ROM 0x0014d7f8 ScrollToCurrent__8TOutlineFv
	virtual void	ScrollToSelection(void);								// ROM 0x0014d880 ScrollToSelection__8TOutlineFv
	virtual Ref		TopicFrame(long index);									// ROM 0x0014dac8 TopicFrame__8TOutlineFl
	virtual void	TopicInit(long index, Topic* topic);					// ROM 0x0014db28 TopicInit__8TOutlineFlP5Topic
	virtual Topic*	TopicPtr(long index);									// ROM 0x0014dc44 TopicPtr__8TOutlineFCl
	virtual void	TopicRect(Topic* topic, Rect* rect, Rect& bounds);		// ROM 0x0014dd14 TopicRect__8TOutlineFP5TopicR5TRectT2
	virtual void	TopicText(long index, UniChar* text, long* length);	// ROM 0x0014de30 TopicText__8TOutlineFlPUsPl
	virtual long	ViewableTopics(void);
	virtual long	VisibleTopic(long index);								// ROM 0x0014e0f4 VisibleTopic__8TOutlineFl
	virtual long	WhereAreWe(void);										// ROM 0x0014e13c WhereAreWe__8TOutlineFv
	virtual Ref		TopicByName(RefArg name);

	Ref				List(void);												// ROM 0x0014d0b8 List__8TOutlineFv
	long			PaneIndex(void);										// ROM 0x0014d340 PaneIndex__8TOutlineFv

	RefStruct*		fFont;				// +0x30  viewFont
	long			fCount;				// +0x34  topics
	Topic*			fTopics;			// +0x38
	long			fSelection;			// +0x3c  the topic tapped last (-1: none)
	long			fField40;			// +0x40  (set to 0, never read)
	short			fScroll;			// +0x44  the list's offset in pixels
	long			fCurrent;			// +0x48  the topic the book is at, marked with a bar (-1: none)
	Boolean			fDynamic;			// +0x4c  the browser is dynamic (no page numbers in the rendering)
	Boolean			fHeader;			// +0x4d  the browser has a header (its list is one per pane)
	short			fLineHeight;		// +0x4e  a line of the font
	short			fAscent;			// +0x50
	short			fHeight;			// +0x52  the shown topics' height, when made
	long			fPane;				// +0x54  the pane of a browser with a header
	long			fViewable;			// +0x58  viewableTopics (9 when it has none)
	long			fPaneIndex;			// +0x5c  paneIndex (-1: none)
	RefStruct*		fBrowser;			// +0x60  the browser, cached
	RefStruct*		fList;				// +0x64  its list, cached
};

class THelpOutline : public TOutline
{
public:
	virtual long	ClassID(void) const;									// ROM 0x0014e2f4 ClassID__12THelpOutlineCFv
	virtual Boolean	DerivedFrom(long id) const;								// ROM 0x0014e2fc DerivedFrom__12THelpOutlineCFl
	virtual Ref		Browser(void);											// ROM 0x0014e41c Browser__12THelpOutlineFv
	virtual void	ClickCommand(long index);								// ROM 0x0014e494 ClickCommand__12THelpOutlineFl
	virtual long	ViewableTopics(void);									// ROM 0x0014e7cc ViewableTopics__12THelpOutlineFv
	virtual long	WhereAreWe(void);										// ROM 0x0014e7d4 WhereAreWe__12THelpOutlineFv
	virtual Ref		TopicByName(RefArg name);								// ROM 0x0014e604 TopicByName__12THelpOutlineFRC6RefVar
};

// the outline's NewtonScript functions
void			RegisterOutlineNatives(void);

#endif	/* __OUTLINE_H */
