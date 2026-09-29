/*
	File:		books/Pages.cpp

	Contains:	The book reader's pages: PageTurnTo and the block views.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Pages.h"
#include "Librarian.h"
#include "ParagraphView.h"
#include "Animate.h"
#include "Rects.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include <string.h>

Ref		FCloseX(RefArg rcvr);			// views/ViewNatives.cpp: view:Close()
Ref		BookTitle(RefArg rcvr);			// Librarian.cpp
void	ZoomRect(Rect* from, Rect* to, long steps, Boolean zoomIn);	// qd/ZoomRect.cpp (ROM 0x003404e0)

const ULong kTextBlock = 'TEXT';
const ULong kPictBlock = 'PICT';
const ULong kFormBlock = 'form';
const ULong kOtherBlock = '****';


// ROM 0x00164d18 CuPage__FRC6RefVar
// The reader's page view (cuPage).
Ref
CuPage(RefArg reader)
{
	return GetVariable(reader, RSSYMcupage, nil, 0);
}


// ROM 0x00163a04 ContentView__FRC6RefVar
// The view a reader's page goes into: its page view's contentArea.
TView*
ContentView(RefArg reader)
{
	RefVar cuPage(CuPage(reader));
	GetView(cuPage);		// (asked, and not used)
	RefVar content(GetVariable(cuPage, RSSYMcontentarea, nil, 0));
	return GetView(content);
}


// ROM 0x001651e4 IsSpread__FRC6RefVar
// Whether the reader shows two pages side by side (pagesShowing not 1,
// and not printing).
Boolean
IsSpread(RefArg reader)
{
	return GetVariable(reader, RSSYMpagesshowing, nil, 0) != MAKEINT(1)
		&& ISNIL(GetProtoVariable(reader, RSSYMprinting, nil));
}


// ROM 0x00166ce8 PageTurnAway__FRC6RefVarUcP5TView
// The page the content view shows let go: its pageTurnAwayScript run, its
// views taken out, the page view's storyCard cleared, any markup layer
// open over it saved and closed, and - but for a help book - the content
// area's own scripts (the page template's) taken off again.  Nothing when a
// book (not a help book) is not open at a page yet.  With no content view,
// the reader printed on the REP.
void
PageTurnAway(RefArg reader, Boolean helpBook, TView* content)
{
	if (!helpBook && TLibrarian::gLibrarian->CurrentPage(reader) == 0)
		return;
	if (content == nil)
	{
		gREPout->Print("context: ");
		PrintObject(reader, 0);
		gREPout->Print("\r");
		return;
	}
	content->RunScript(RSSYMpageturnawayscript, RefVar(), true);
	content->RemoveAllViews();
	RefVar cuPage(CuPage(reader));
	SetFrameSlot(cuPage, RSSYMstorycard, RefVar());
	RefVar markup(content->GetVar(RSSYMmarkuplayer));
	if (NOTNIL(markup))
	{
		if ((RINT(GetProtoVariable(markup, RSSYMviewflags, nil)) & 1) != 0)
		{
			DoMessage(markup, RSSYMsavemarkup, RefVar());
			FCloseX(markup);
		}
	}
	if (!helpBook)
	{
		RefVar context(content->fContext);
		SetFrameSlot(context, RSSYM_proto, RefVar(GetFrameSlotRef(context, RSSYMbookscripts)));
	}
}


// Copy a bounds array's [left, top, right, bottom] into a Rect (inline in
// the ROM).
static void
BlockBounds(RefArg bounds, Rect* r)
{
	r->left = (short) RINT(GetArraySlotRef(bounds, 0));
	r->top = (short) RINT(GetArraySlotRef(bounds, 1));
	r->right = (short) RINT(GetArraySlotRef(bounds, 2));
	r->bottom = (short) RINT(GetArraySlotRef(bounds, 3));
}


// ROM 0x00165238 MakeBlockView__FRC6RefVarT1UlUc
// The view template for a block of a page: the content item it shows (put
// through the content area's mungeContentScript first, when it has one),
// with the item's look - a frame round it (look 0xf00: viewFormat's frame
// at the edge width), or edges drawn by edgeDrawer (the top edge left off
// a block continuing its text, the bottom one off a block its text
// continues past), a shadow (0x10), a transfer mode (0x20) - and bounds
// from the block (brought in by the edges); then by the item's type: text
// (canonicalTextBlock: the part of the text the block holds, its styles
// moved to where the part starts, its tabs), a picture (the item's data as
// its icon, in a scroller with a compass when it is bigger than the block
// and the item does not ask to be clipped - flags bit 3 - and the page is
// not being printed), or a form (the data is its _proto).  The item's
// scripts and related slot are the view's.
Ref
MakeBlockView(RefArg blocks, RefArg mungeScript, ULong index, Boolean printing)
{
	RefVar block;
	RefVar item;
	RefVar view;
	RefVar bounds;
	RefVar boundsArray;
	RefVar data;
	RefVar value;
	RefVar scripts;
	ULong viewFormat = 0;
	ULong look = 0;
	long edge = 1;
	ULong layout = 0;
	ULong flags = 0;

	block = GetArraySlotRef(blocks, index);
	item = GetFrameSlotRef(block, RSSYMitem);
	if (NOTNIL(mungeScript))
	{
		value = MakeArray(1);
		SetArraySlotRef(value, 0, item);
		item = DoBlock(mungeScript, value);
	}
	data = GetFrameSlotRef(item, RSSYMdata);
	ULong textLength = (ULong) (Length(data) - 2) >> 1;
	value = GetFrameSlotRef(block, RSSYMdatalen);
	ULong dataLength = textLength;
	if (NOTNIL(value))
		dataLength = RINT(value);
	value = GetFrameSlotRef(block, RSSYMdataoffset);
	ULong offset = ISNIL(value) ? 0 : RINT(value);

	view = AllocateFrame();
	SetFrameSlot(view, RSSYMitem, item);
	bounds = AllocateFrame();
	if (FrameHasSlot(item, RSSYMedgewidth))
		edge = RINT(GetFrameSlotRef(item, RSSYMedgewidth));
	if (FrameHasSlot(item, RSSYMlook))
	{
		look = RINT(GetFrameSlotRef(item, RSSYMlook));
		if ((look & 0xf00) == 0xf00)
			viewFormat = 0x10050 + (edge << 8);
		else if ((look & 0xf00) != 0)
		{
			SetFrameSlot(view, RSSYMviewdrawscript, RefVar(Redgedrawer));
			SetFrameSlot(view, RSSYMedgewidth, RefVar(MAKEINT(edge)));
			SetFrameSlot(view, RSSYMdrawpensizex, RefVar(MAKEINT(edge)));
			SetFrameSlot(view, RSSYMdrawpensizey, RefVar(MAKEINT(edge)));
			if (dataLength < textLength)
			{
				Boolean last = false;
				if (offset != 0)
				{
					last = dataLength + offset + 1 == textLength;
					look &= ~0x100;
				}
				if (!last)
					look &= ~0x400;
			}
			SetFrameSlot(view, RSSYMlook, RefVar(MAKEINT(look)));
		}
		if ((look & 0x10) != 0)
			viewFormat |= 0x6000000;
		if ((look & 0x20) != 0)
		{
			viewFormat |= 5;
			SetFrameSlot(view, RSSYMviewtransfermode, RefVar(MAKEINT(3)));
		}
	}
	if (viewFormat != 0)
		SetFrameSlot(view, RSSYMviewformat, RefVar(MAKEINT(viewFormat)));

	boundsArray = GetFrameSlotRef(block, RSSYMbounds);
	if ((look & 0xf00) == 0)
	{
		SetFrameSlot(bounds, RSSYMleft, RefVar(GetArraySlotRef(boundsArray, 0)));
		SetFrameSlot(bounds, RSSYMtop, RefVar(GetArraySlotRef(boundsArray, 1)));
		SetFrameSlot(bounds, RSSYMright, RefVar(GetArraySlotRef(boundsArray, 2)));
		SetFrameSlot(bounds, RSSYMbottom, RefVar(GetArraySlotRef(boundsArray, 3)));
	}
	else
	{
		long left = RINT(GetArraySlotRef(boundsArray, 0));
		long top = RINT(GetArraySlotRef(boundsArray, 1));
		long right = RINT(GetArraySlotRef(boundsArray, 2));
		long bottom = RINT(GetArraySlotRef(boundsArray, 3));
		if ((look & 0x200) != 0)
			left += edge + 1;
		if ((look & 0x100) != 0)
			top += edge + 1;
		if ((look & 0x400) != 0)
			bottom -= edge + 1;
		if ((look & 0x800) != 0)
			right -= edge + 2;
		SetFrameSlot(bounds, RSSYMleft, RefVar(MAKEINT(left)));
		SetFrameSlot(bounds, RSSYMtop, RefVar(MAKEINT(top)));
		SetFrameSlot(bounds, RSSYMright, RefVar(MAKEINT(right)));
		SetFrameSlot(bounds, RSSYMbottom, RefVar(MAKEINT(bottom)));
	}
	SetFrameSlot(view, RSSYMviewbounds, bounds);

	ULong type;
	if (!FrameHasSlot(item, RSSYMtype))
		type = EQRef(ClassOf(data), RSSYMstring) ? kTextBlock : kPictBlock;
	else
		type = EQRef(GetFrameSlotRef(item, RSSYMtype), RSSYMform) ? kFormBlock : kOtherBlock;
	if (FrameHasSlot(item, RSSYMflags))
		flags = RINT(GetFrameSlotRef(item, RSSYMflags));
	if (FrameHasSlot(item, RSSYMlayout))
		layout = RINT(GetFrameSlotRef(item, RSSYMlayout));
	if (type == kTextBlock || type == kPictBlock)
	{
		value = GetFrameSlotRef(item, RSSYMviewjustify);
		if (ISNIL(value))
			SetFrameSlot(view, RSSYMviewjustify, RefVar(MAKEINT((layout & 4) == 0 ? 0 : 2)));
		else
			SetFrameSlot(view, RSSYMviewjustify, value);
	}

	if (type == kTextBlock)
	{
		SetFrameSlot(view, RSSYM_proto, RefVar(Rcanonicaltextblock));
		if (dataLength == textLength && offset == 0)
			SetFrameSlot(view, RSSYMtext, data);
		else
		{
			const UniChar* chars = (const UniChar*) BinaryData(data);
			SetFrameSlot(view, RSSYMtext, RefVar(MakeString(chars + offset, dataLength)));
		}
		value = GetFrameSlotRef(item, RSSYMstyles);
		if (ISNIL(value))
		{
			if (FrameHasSlot(item, RSSYMviewfont))
			{
				value = GetFrameSlotRef(item, RSSYMviewfont);
				SetFrameSlot(view, RSSYMviewfont, value);
			}
		}
		else if (offset == 0)
			SetFrameSlot(view, RSSYMstyles, value);
		else
		{
			// the runs from the part's start on: the run it starts in cut
			// down to what is left of it
			ULong total = 0;
			long count = Length(value);
			RefVar styles(MakeArray(0));
			Boolean started = false;
			for (long i = 0; i < count; i += 2)
			{
				long run = RINT(GetArraySlotRef(value, i));
				total += run;
				if (started)
				{
					AddArraySlot(styles, RefVar(MAKEINT(run)));
					AddArraySlot(styles, RefVar(GetArraySlotRef(value, i + 1)));
				}
				else if (offset < total)
				{
					AddArraySlot(styles, RefVar(MAKEINT(total - offset)));
					AddArraySlot(styles, RefVar(GetArraySlotRef(value, i + 1)));
					started = true;
				}
			}
			SetFrameSlot(view, RSSYMstyles, styles);
		}
		value = GetFrameSlotRef(item, RSSYMtabs);
		if (NOTNIL(value))
			SetFrameSlot(view, RSSYMtabs, value);
	}
	else if (type == kFormBlock)
		SetFrameSlot(view, RSSYM_proto, data);
	else if (type == kPictBlock)
	{
		Rect r;
		Rect pict = { 0, 0, 0, 0 };		// (the ROM's is stack rubbish when the data is neither)
		BlockBounds(RefVar(GetFrameSlotRef(block, RSSYMbounds)), &r);
		if (EQRef(ClassOf(data), RSSYMpicture))
		{
			// the picture's frame: big-endian words at +2
			const UByte* p = (const UByte*) BinaryData(data) + 2;
			pict.top = (short) ((p[0] << 8) | p[1]);
			pict.left = (short) ((p[2] << 8) | p[3]);
			pict.bottom = (short) ((p[4] << 8) | p[5]);
			pict.right = (short) ((p[6] << 8) | p[7]);
		}
		else if (FrameHasSlot(data, RSSYMbits))
		{
			RefVar bits(GetFrameSlotRef(data, RSSYMbits));		// (not used)
			FromObject(RefVar(GetFrameSlotRef(data, RSSYMbounds)), pict);
		}
		if ((flags & 8) != 0 || printing
		||  ((short) (pict.right - pict.left) <= (short) (r.right - r.left)
		  && (short) (pict.bottom - pict.top) <= (short) (r.bottom - r.top)))
		{
			SetFrameSlot(view, RSSYMviewclass, RefVar(MAKEINT(76)));
			SetFrameSlot(view, RSSYMviewflags, RefVar(MAKEINT(0x201)));
			SetFrameSlot(view, RSSYMicon, data);
		}
		else
		{
			SetFrameSlot(view, RSSYM_proto, RefVar(Rcanonicalscroller));
			RefVar scrollee(AllocateFrame());
			RefVar scrolleeBounds(AllocateFrame());
			SetFrameSlot(scrollee, RSSYM_proto, RefVar(Rcanonicalscrollee));
			SetFrameSlot(scrollee, RSSYMicon, data);
			SetFrameSlot(scrolleeBounds, RSSYMtop, RefVar(TRUEREF));		// (the ROM: Ref 1, not nil)
			SetFrameSlot(scrolleeBounds, RSSYMleft, RefVar(TRUEREF));		// (the ROM: Ref 1, not nil)
			SetFrameSlot(scrolleeBounds, RSSYMbottom, RefVar(MAKEINT((short) (r.bottom - r.top))));
			SetFrameSlot(scrolleeBounds, RSSYMright, RefVar(MAKEINT((short) (r.right - r.left))));
			SetFrameSlot(scrollee, RSSYMviewbounds, scrolleeBounds);
			SetFrameSlot(scrollee, RSSYMviewformat, RefVar(MAKEINT(viewFormat)));
			RefVar children(AllocateArray(RSSYMviewchildren, 0));
			AddArraySlot(children, scrollee);
			RefVar dataBounds(AllocateFrame());
			SetFrameSlot(dataBounds, RSSYMtop, RefVar(TRUEREF));		// (the ROM: Ref 1, not nil)
			SetFrameSlot(dataBounds, RSSYMleft, RefVar(TRUEREF));		// (the ROM: Ref 1, not nil)
			SetFrameSlot(dataBounds, RSSYMbottom, RefVar(MAKEINT((short) (pict.bottom - pict.top))));
			SetFrameSlot(dataBounds, RSSYMright, RefVar(MAKEINT((short) (pict.right - pict.left))));
			SetFrameSlot(scrollee, RSSYMdatabounds, dataBounds);
			RefVar compass(AllocateFrame());
			SetFrameSlot(compass, RSSYM_proto, RefVar(Rcanonicalcompass));
			RefVar delta(GetFrameSlotRef(item, RSSYMscrolldelta));
			if (NOTNIL(delta))
				SetFrameSlot(compass, RSSYMscrolldelta, delta);
			AddArraySlot(children, compass);
			SetFrameSlot(view, RSSYMviewchildren, children);
		}
	}

	if (FrameHasSlot(item, RSSYMscripts))
	{
		scripts = GetFrameSlotRef(item, RSSYMscripts);
		for (long i = 0; i < Length(scripts); i += 2)
			SetFrameSlot(view, RefVar(GetArraySlotRef(scripts, i)), RefVar(GetArraySlotRef(scripts, i + 1)));
	}
	if (FrameHasSlot(item, RSSYMrelated))
		SetFrameSlot(view, RSSYMrelated, RefVar(GetFrameSlotRef(item, RSSYMrelated)));
	return view;
}


// ROM 0x00163a8c PageTurnTo__FRC6RefVarUlUc
// A page of the reader's book (from 1) made into views in its content
// area (the old page turned away first, when asked and not printing): the
// page template's scripts made the content area's (printing: put straight
// into it; else in a frame of their own between it and its bookScripts);
// on a spread, a subContentArea of its own for the page, on the left for
// an even page and the right for an odd one; a title, unless the reader is
// a help book's (it has a bookRef) or the template's flags have bit 3 -
// the template's header (a string or a picture) or the book's title; then
// a view for each block; the content area's viewSetupDoneScript (not
// printing) and pageTurnToScript run.
void
PageTurnTo(RefArg reader, ULong page, Boolean turnAway)
{
	RefVar viewBounds;
	RefVar pageFrame;
	RefVar blocks;
	RefVar frame;
	RefVar scriptsFrame;
	RefVar cuPage(CuPage(reader));
	RefVar book;
	RefVar templ;
	Boolean withTitle = true;
	Boolean spread = IsSpread(reader);
	book = GetProtoVariable(reader, RSSYMbookref, nil);
	GetView(cuPage);		// (asked, and not used)
	TView* content = ContentView(reader);
	Boolean printing = NOTNIL(GetProtoVariable(reader, RSSYMprinting, nil));
	if (turnAway && !printing)
		PageTurnAway(reader, NOTNIL(book), content);
	pageFrame = TLibrarian::gLibrarian->GetPageN(page, reader);
	templ = GetFrameSlotRef(pageFrame, RSSYMtemplate);
	if (!spread)
	{
		if (FrameHasSlot(templ, RSSYMscripts))
		{
			RefVar scripts;
			RefVar context(content->fContext);
			if (printing)
			{
				scripts = GetFrameSlotRef(templ, RSSYMscripts);
				long count = Length(scripts);
				for (long i = 0; i < count; i += 2)
					SetFrameSlot(context, RefVar(GetArraySlotRef(scripts, i)), RefVar(GetArraySlotRef(scripts, i + 1)));
			}
			else
			{
				scriptsFrame = AllocateFrame();
				scripts = GetFrameSlotRef(templ, RSSYMscripts);
				long count = Length(scripts);
				for (long i = 0; i < count; i += 2)
					SetFrameSlot(scriptsFrame, RefVar(GetArraySlotRef(scripts, i)), RefVar(GetArraySlotRef(scripts, i + 1)));
				SetFrameSlot(scriptsFrame, RSSYM_proto, RefVar(GetFrameSlotRef(context, RSSYMbookscripts)));
				SetFrameSlot(context, RSSYM_proto, scriptsFrame);
			}
		}
	}
	else
	{
		frame = AllocateFrame();
		SetFrameSlot(frame, RSSYM_proto, RefVar(Rprotosubcontentarea));
		SetFrameSlot(frame, RSSYMpagenumber, RefVar(MAKEINT(page)));
		RefVar size(TLibrarian::gLibrarian->PageSize(reader));
		viewBounds = AllocateFrame();
		SetFrameSlot(viewBounds, RSSYMtop, RefVar(TRUEREF));		// (the ROM: Ref 1, not nil)
		SetFrameSlot(viewBounds, RSSYMbottom, RefVar(GetFrameSlotRef(size, RSSYMbottom)));
		if ((page & 1) == 0)
		{
			SetFrameSlot(viewBounds, RSSYMleft, RefVar(TRUEREF));		// (the ROM: Ref 1, not nil)
			SetFrameSlot(viewBounds, RSSYMright, RefVar(GetFrameSlotRef(size, RSSYMright)));
		}
		else
		{
			long width = RINT(GetFrameSlotRef(size, RSSYMright));
			SetFrameSlot(viewBounds, RSSYMleft, RefVar(MAKEINT(width + 15)));
			long right = RINT(GetFrameSlotRef(size, RSSYMright));
			SetFrameSlot(viewBounds, RSSYMright, RefVar(MAKEINT(right + width + 15)));
		}
		SetFrameSlot(frame, RSSYMviewbounds, viewBounds);
		if (FrameHasSlot(templ, RSSYMscripts))
		{
			RefVar scripts(GetFrameSlotRef(templ, RSSYMscripts));
			long count = Length(scripts);
			for (long i = 0; i < count; i += 2)
				SetFrameSlot(frame, RefVar(GetArraySlotRef(scripts, i)), RefVar(GetArraySlotRef(scripts, i + 1)));
		}
		content = content->AddView(frame);
	}
	if (FrameHasSlot(templ, RSSYMflags))
	{
		if ((RINT(GetFrameSlotRef(templ, RSSYMflags)) & 8) != 0)
			withTitle = false;
	}
	if (!((NOTNIL(book) && !printing) || !withTitle))
	{
		frame = AllocateFrame();
		if (!FrameHasSlot(templ, RSSYMheader))
		{
			SetFrameSlot(frame, RSSYM_proto, RefVar(Rcanonicaltitle));
			if (printing)
				SetFrameSlot(frame, RSSYMtext, RefVar(GetFrameSlotRef(book, RSSYMtitle)));
			else
				SetFrameSlot(frame, RSSYMtext, RefVar(BookTitle(reader)));
		}
		else
		{
			RefVar header(GetFrameSlotRef(templ, RSSYMheader));
			RefVar data(GetFrameSlotRef(header, RSSYMdata));
			if (EQRef(ClassOf(data), RSSYMstring))
			{
				SetFrameSlot(frame, RSSYM_proto, RefVar(Rcanonicaltitle));
				SetFrameSlot(frame, RSSYMtext, data);
				if (FrameHasSlot(header, RSSYMviewfont))
					SetFrameSlot(frame, RSSYMviewfont, RefVar(GetFrameSlotRef(header, RSSYMviewfont)));
				else if (FrameHasSlot(header, RSSYMstyles))
					SetFrameSlot(frame, RSSYMstyles, RefVar(GetFrameSlotRef(header, RSSYMstyles)));
			}
			else if (EQRef(ClassOf(data), RSSYMpicture))
			{
				SetFrameSlot(frame, RSSYMviewclass, RefVar(MAKEINT(76)));
				SetFrameSlot(frame, RSSYMviewflags, RefVar(MAKEINT(1)));
				SetFrameSlot(frame, RSSYMicon, data);
				// the picture's frame: big-endian words at +2
				const UByte* p = (const UByte*) BinaryData(data) + 2;
				short top = (short) ((p[0] << 8) | p[1]);
				short left = (short) ((p[2] << 8) | p[3]);
				short bottom = (short) ((p[4] << 8) | p[5]);
				short right = (short) ((p[6] << 8) | p[7]);
				viewBounds = AllocateFrame();
				SetFrameSlot(viewBounds, RSSYMtop, RefVar(TRUEREF));		// (the ROM: Ref 1, not nil)
				long x = (240 - (short) (right - left)) / 2;
				SetFrameSlot(viewBounds, RSSYMleft, RefVar(MAKEINT(x)));
				SetFrameSlot(viewBounds, RSSYMbottom, RefVar(MAKEINT((short) (bottom - top))));
				SetFrameSlot(viewBounds, RSSYMright, RefVar(MAKEINT(x + (short) (right - left))));
				SetFrameSlot(frame, RSSYMviewbounds, viewBounds);
			}
		}
		content->AddView(frame);
	}
	blocks = GetFrameSlotRef(pageFrame, RSSYMblocks);
	long count = Length(blocks);
	RefVar munge(GetVariable(content->fContext, RSSYMmungecontentscript, nil, 0));
	for (ULong i = 0; i < (ULong) count; i++)
	{
		frame = MakeBlockView(blocks, munge, i, printing);
		content->AddView(frame);
	}
	if (!printing && NOTNIL(GetVariable(content->fContext, RSSYMviewsetupdonescript, nil, 0)))
		content->RunScript(RSSYMviewsetupdonescript, RefVar(), false);
	content->RunScript(RSSYMpageturntoscript, RefVar(), true);
}


// ROM 0x0016469c PageTurnToSpread__FRC6RefVarUl
// The reader turned to a page, the page before it too on a spread (an odd
// page but the first is shown with the even one before it); not printing:
// the page recorded as the current one, the page view told the browser's
// position, the page numbers and the bookmark icon, and the turn animated
// forwards or backwards with the page sound (the gutter between a spread's
// pages redrawn after); the markup put back - shown, or when printing the
// ink marked on the page made the markup layer's children; the control
// panel redrawn.
void
PageTurnToSpread(RefArg reader, ULong page)
{
	ULong pageCount = TLibrarian::gLibrarian->CountPages(reader);
	Boolean notPrinting = ISNIL(GetProtoVariable(reader, RSSYMprinting, nil));
	Boolean spread = IsSpread(reader);
	RefVar cuPage(CuPage(reader));
	TView* pageView = GetView(cuPage);
	RefVar markup(pageView->GetProto(RSSYMmarkuplayer));
	Boolean markupShown = false;
	if (NOTNIL(markup))
		markupShown = (RINT(GetProtoVariable(markup, RSSYMviewflags, nil)) & 1) != 0;
	ULong current = TLibrarian::gLibrarian->CurrentPage(reader);
	if (spread && page != 1 && (page & 1) == 1)
		page = page - 1;
	if (notPrinting)
		TLibrarian::gLibrarian->SetCurrentPage(reader, page);
	PageTurnTo(reader, page, true);
	if (notPrinting)
	{
		RefVar numbers(MakeArray(2));
		SetArraySlotRef(numbers, 0, MAKEINT(page));
		DoMessage(cuPage, RSSYMsetbrowserposition, RefVar());
		Ref second;
		if (!spread || page == 1 || pageCount < page + 1)
			second = NILREF;
		else
		{
			PageTurnTo(reader, page + 1, false);
			second = MAKEINT(page + 1);
		}
		SetArraySlotRef(numbers, 1, second);
		DoMessage(cuPage, RSSYMsetpagenumber, numbers);
		DoMessage(cuPage, RSSYMsetmarkicon, RefVar());
		if (current != 0 && page != current)
		{
			TView* content = ContentView(reader);
			TAnimate effect;
			effect.SetupPlainEffect(content, true, page < current ? 0x30000 : 0x30c00);
			effect.DoEffect(RSSYMpagesound);
			if (spread)
			{
				Rect gutter = content->viewBounds;
				OffsetRect(&gutter, -content->viewBounds.left, -content->viewBounds.top);
				long middle = gutter.right / 2;
				gutter.left = (short) (middle - 15);
				gutter.right = (short) (middle + 15);
				OffsetRect(&gutter, content->viewBounds.left, content->viewBounds.top);
				content->Dirty(&gutter);
			}
		}
	}
	if (markupShown)
	{
		if (notPrinting)
			DoMessage(cuPage, RSSYMshowmarkup, RefVar());
		else
		{
			RefVar marks(GetVariable(reader, RSSYMinkmarks, nil, 0));
			if (NOTNIL(marks))
			{
				long count = Length(marks);
				for (long i = 0; i < count; i += 2)
					if ((ULong) RINT(GetArraySlotRef(marks, i)) == page)
					{
						SetFrameSlot(markup, RSSYMviewchildren, RefVar(GetArraySlotRef(marks, i + 1)));
						break;
					}
			}
		}
	}
	if (notPrinting)
	{
		RefVar panel(GetVariable(reader, RSSYMcntrlpanel, nil, 0));
		GetView(panel)->Dirty(nil);
	}
}


/*------------------------------------------------------------------------------
	T h e   p a g e   f u n c t i o n s
------------------------------------------------------------------------------*/

// ROM 0x00162860 AddToContentArea
// reader:AddToContentArea(template) - a view added to the page, shown.
Ref
AddToContentArea(RefArg rcvr, RefArg templ)
{
	TView* view = ContentView(rcvr)->AddView(templ);
	view->ClearFlags(1);
	return view->fContext;
}


// ROM 0x00166544 PageContents
// reader:PageContents(page) - the content items a page shows, each once.
Ref
PageContents(RefArg rcvr, RefArg pageArg)
{
	RefVar block;
	RefVar page(TLibrarian::gLibrarian->GetPageN(RINT(pageArg), rcvr));
	RefVar blocks(GetFrameSlotRef(page, RSSYMblocks));
	long count = Length(blocks);
	RefVar items(MakeArray(0));
	for (long i = 0; i < count; i++)
	{
		block = GetArraySlotRef(blocks, i);
		FSetAdd(RefVar(), items, RefVar(GetFrameSlotRef(block, RSSYMitem)), RefVar(TRUEREF));		// (the ROM: Ref 1, not nil)
	}
	return items;
}


// ROM 0x001666a4 PageScroll
// reader:ScrollPage(delta) - the reader turned delta pages on (0: the
// current page again); nothing past either end.
Ref
PageScroll(RefArg rcvr, RefArg delta)
{
	ULong count = TLibrarian::gLibrarian->CountPages(rcvr);
	ULong page = TLibrarian::gLibrarian->CurrentPage(rcvr);
	if (RINT(delta) != 0)
	{
		page = RINT(delta) + page;
		if (count < page)
			return TRUEREF;
		if (page == 0)
			return TRUEREF;
	}
	PageTurnToSpread(rcvr, page);
	return TRUEREF;
}


// ROM 0x00164c88 ZoomView
// ZoomView(fromView, toView, steps, zoomIn) - the zooming rectangles
// drawn from one view's bounds to the other's (ZoomRect).
Ref
ZoomView(RefArg rcvr, RefArg fromView, RefArg toView, RefArg steps, RefArg zoomIn)
{
	Rect from = GetView(rcvr, fromView)->viewBounds;
	Rect to = GetView(rcvr, toView)->viewBounds;
	ZoomRect(&from, &to, RINT(steps), NOTNIL(zoomIn));
	return TRUEREF;
}


// ROM 0x00164c4c TurnToPage
// reader:TurnToPage(page)
Ref
TurnToPage(RefArg rcvr, RefArg page)
{
	PageTurnToSpread(rcvr, RINT(page));
	return TRUEREF;
}


// ROM 0x00164d64 HiliteBlock
// reader:HiliteBlock(item, offset, length) - on the current page, the
// block showing the item hilited: characters offset to offset + length of
// a text item (in the paragraph the block's middle is in), or a form's
// formHiliteScript([offset, length]).  ==> true, or nil for an item of any
// other kind.
Ref
HiliteBlock(RefArg rcvr, RefArg item, RefArg offsetArg, RefArg length)
{
	RefVar pageFrame;
	RefVar blocks;
	RefVar block;
	long offset = RINT(offsetArg);
	Boolean isForm = false;
	TView* content = ContentView(rcvr);
	long page = TLibrarian::gLibrarian->CurrentPage(rcvr);
	pageFrame = TLibrarian::gLibrarian->GetPageN(page, rcvr);
	blocks = GetFrameSlotRef(pageFrame, RSSYMblocks);
	long count = Length(blocks);
	for (long i = 0; i < count; i++)
	{
		block = GetArraySlotRef(blocks, i);
		if (EQRef(GetFrameSlotRef(block, RSSYMitem), item))
			break;
	}
	RefVar data(GetFrameSlotRef(item, RSSYMdata));
	if (!FrameHasSlot(item, RSSYMtype))
	{
		if (!EQRef(ClassOf(data), RSSYMstring))
			return NILREF;
	}
	else
	{
		if (!EQRef(GetFrameSlotRef(item, RSSYMtype), RSSYMform))
			return NILREF;
		isForm = true;
	}
	Rect r;
	BlockBounds(RefVar(GetFrameSlotRef(block, RSSYMbounds)), &r);
	OffsetRect(&r, content->viewBounds.left, content->viewBounds.top);
	Point middle = MidPoint(r);
	if (FrameHasSlot(block, RSSYMdataoffset))
		offset -= RINT(GetFrameSlotRef(block, RSSYMdataoffset));
	TView* view = content->FindView(middle, 0, nil);
	if (view != nil)
	{
		if (!isForm)
			((TParagraphView*) view)->MakeHilite(offset, RINT(length) + offset, true);
		else
		{
			RefVar args(MakeArray(2));
			SetArraySlotRef(args, 0, offsetArg);
			SetArraySlotRef(args, 1, length);
			DoMessage(view->fContext, RSSYMformhilitescript, args);
		}
	}
	return TRUEREF;
}


void
RegisterPageNatives(void)
{
	RegisterNativeFunction("AddToContentArea", (void*) AddToContentArea, 1);
	RegisterNativeFunction("TurnToPage", (void*) TurnToPage, 1);
	RegisterNativeFunction("HiliteBlock", (void*) HiliteBlock, 3);
	RegisterNativeFunction("PageContents", (void*) PageContents, 1);
	RegisterNativeFunction("PageScroll", (void*) PageScroll, 1);
	RegisterNativeFunction("ZoomView", (void*) ZoomView, 4);
}
