/*
	File:		books/Pages.h

	Contains:	The book reader's pages: a page of a book made into views.

				A reader (Copperfield, or a help book's Tiny Tim) has a
				page view - its cuPage - with a contentArea child the
				page's views go into.  PageTurnTo empties it and fills it
				with the page's views: its template's scripts (run as the
				content area's own), a title (unless the page's template
				has its flags' bit 3), then one view per block of the page
				(MakeBlockView), each placed by the block's bounds and
				made from the content item the block shows - a text block
				(canonicalTextBlock) of the part of the item's text the
				block holds, with its styles; a picture (in a scroller of
				its own when it is bigger than the block); a form (the item
				is the view's template).  An item's look adds edges
				(edgeDrawer) or a frame round it.  A reader showing two
				pages side by side (pagesShowing 2) puts the second in a
				subContentArea of its own (PageTurnToSpread).

	Reconstructed from the MP2x00 US ROM (0x00162860, 0x00163a04-0x00166efc);
	each function cites its origin.
*/

#ifndef __PAGES_H
#define __PAGES_H

#ifndef __VIEW_H
#include "View.h"
#endif

Ref			CuPage(RefArg reader);									// ROM 0x00164d18 CuPage__FRC6RefVar
TView*		ContentView(RefArg reader);								// ROM 0x00163a04 ContentView__FRC6RefVar
Boolean		IsSpread(RefArg reader);								// ROM 0x001651e4 IsSpread__FRC6RefVar
void		PageTurnTo(RefArg reader, ULong page, Boolean turnAway);	// ROM 0x00163a8c PageTurnTo__FRC6RefVarUlUc
void		PageTurnToSpread(RefArg reader, ULong page);			// ROM 0x0016469c PageTurnToSpread__FRC6RefVarUl
void		PageTurnAway(RefArg reader, Boolean helpBook, TView* content);	// ROM 0x00166ce8 PageTurnAway__FRC6RefVarUcP5TView
Ref			MakeBlockView(RefArg blocks, RefArg mungeScript, ULong index, Boolean printing);	// ROM 0x00165238 MakeBlockView__FRC6RefVarT1UlUc

// the page functions
Ref			HiliteBlock(RefArg rcvr, RefArg item, RefArg offset, RefArg length);	// ROM 0x00164d64 HiliteBlock
void		RegisterPageNatives(void);

#endif	/* __PAGES_H */
