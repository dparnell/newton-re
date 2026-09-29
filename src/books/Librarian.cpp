/*
	File:		books/Librarian.cpp

	Contains:	TLibrarian: the library of installed books, and the
				book reader's NewtonScript functions over it.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Librarian.h"
#include "RootView.h"
#include "Soups.h"
#include "Entries.h"
#include "Assistant.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "NewtonExceptions.h"
#include <string.h>

Ref		FCloseX(RefArg rcvr);			// views/ViewNatives.cpp: view:Close()
Ref		CurrentBook(RefArg rcvr);

TLibrarian*	TLibrarian::gLibrarian = nil;

static const NewtonErr	kError_BookNotInLibrary = -10008;	// (0xffffd8e8)


/*------------------------------------------------------------------------------
	T L i b r a r i a n
------------------------------------------------------------------------------*/

// ROM 0x001082f8 ClassID__10TLibrarianCFv
long
TLibrarian::ClassID(void) const
{
	return clLibrarian;
}


// ROM 0x00108300 DerivedFrom__10TLibrarianCFl
Boolean
TLibrarian::DerivedFrom(long id) const
{
	return id == clLibrarian || TResponder::DerivedFrom(id);
}


// ROM 0x0010b428 InitLibrarian__Fv
// The librarian made, the Library soup made if it is not there yet
// (copperfield:CreateGetSoup(), the answer not kept), and a new library
// frame, which Copperfield sees as its library slot.
void
InitLibrarian(void)
{
	TLibrarian* librarian = new TLibrarian;
	if (librarian != nil)
	{
		librarian->fLibrary = new RefStruct(NILREF);
		librarian->fBookCount = 0;
	}
	TLibrarian::gLibrarian = librarian;
	RefVar soup(DoMessage(RefVar(gRootView->GetVar(RSSYMcopperfield)), RSSYMcreategetsoup, RefVar()));
	RefVar copperfield(gRootView->GetVar(RSSYMcopperfield));
	RefVar library(AllocateFrame());
	*TLibrarian::gLibrarian->fLibrary = library;
	SetFrameSlot(copperfield, RSSYMlibrary, library);
	TLibrarian::gLibrarian->fBookCount = 0;
}


// ROM 0x0010b6d4 Librarian__Fv
TLibrarian*
Librarian(void)
{
	return TLibrarian::gLibrarian;
}


// ROM 0x0010bc70 StrRefToSymbol__10TLibrarianFRC6RefVar
// A string (an ISBN) as the symbol the library is indexed by: its
// characters in the Mac encoding, interned.
Ref
TLibrarian::StrRefToSymbol(RefArg str)
{
	RefVar sym;
	UniChar* chars = GetCString(str);
	long length = Ustrlen(chars);
	char* name = NewPtr(length * 2 + 2);
	ConvertFromUnicode(chars, name, kMacRomanEncoding, 0x7fffffff);
	sym = Intern(name);
	DisposPtr(name);
	return sym;
}


// ROM 0x0010b34c GetLibraryEntry__10TLibrarianFRC6RefVar
// The book's entry in the Library soup: copperfield:GetLibraryEntry(isbn).
Ref
TLibrarian::GetLibraryEntry(RefArg isbn)
{
	RefVar args(MakeArray(1));
	SetArraySlotRef(args, 0, isbn);
	RefVar copperfield(gRootView->GetVar(RSSYMcopperfield));
	return DoMessage(copperfield, RSSYMgetlibraryentry, args);
}


// ROM 0x0010b2ac GetBookFrame__10TLibrarianFRC6RefVar
// The book (the part frame's book slot) of the ISBN, if it is in.
Ref
TLibrarian::GetBookFrame(RefArg isbn)
{
	RefVar sym;
	RefVar part;
	sym = StrRefToSymbol(isbn);
	part = GetFrameSlotRef(*fLibrary, sym);
	if (NOTNIL(part))
		return GetFrameSlotRef(part, RSSYMbook);
	return NILREF;
}


// ROM 0x0010ba4c Rendering__10TLibrarianFRC6RefVar
// The rendering the reader shows: the book of its isbn (or, with none, its
// bookRef) - rendering[curRendering].
Ref
TLibrarian::Rendering(RefArg reader)
{
	RefVar current;
	RefVar isbn;
	RefVar sym;
	RefVar book;
	isbn = GetVariable(reader, RSSYMisbn, nil, 0);
	current = GetVariable(reader, RSSYMcurrendering, nil, 0);
	if (ISNIL(isbn))
		book = GetVariable(reader, RSSYMbookref, nil, 0);
	else
	{
		sym = StrRefToSymbol(isbn);		// (unused: GetBookFrame makes it again)
		book = GetBookFrame(isbn);
	}
	RefVar renderings(GetFrameSlotRef(book, RSSYMrendering));
	return GetArraySlotRef(renderings, RINT(current));
}


// ROM 0x0010b778 Pages__10TLibrarianFRC6RefVar
Ref
TLibrarian::Pages(RefArg reader)
{
	RefVar rendering(Rendering(reader));
	return GetFrameSlotRef(rendering, RSSYMpages);
}


// ROM 0x0010b3d8 GetPageN__10TLibrarianFlRC6RefVar
// Page n (from 1).
Ref
TLibrarian::GetPageN(long page, RefArg reader)
{
	RefVar pages(Pages(reader));
	return GetArraySlotRef(pages, page - 1);
}


// ROM 0x00108af4 CountPages__10TLibrarianFRC6RefVar
long
TLibrarian::CountPages(RefArg reader)
{
	RefVar pages(Pages(reader));
	return Length(pages);
}


// ROM 0x0010b7d0 PageSize__10TLibrarianFRC6RefVar
// The rendering's pageSize - its bottom made 318 for a book of version 1.
Ref
TLibrarian::PageSize(RefArg reader)
{
	RefVar rendering(Rendering(reader));
	RefVar size(GetFrameSlotRef(rendering, RSSYMpagesize));
	RefVar book(CurrentBook(reader));
	if (NOTNIL(book))
	{
		if (RINT(GetFrameSlotRef(book, RSSYMversion)) == 1)
		{
			size = Clone(size);
			SetFrameSlot(size, RSSYMbottom, RefVar(MAKEINT(318)));
		}
	}
	return size;
}


// ROM 0x0010909c CurrentPage__10TLibrarianFRC6RefVar
// The page the reader's book is at (its library entry's curPage).
// ROM BUG: a page beyond the book's end sets the entry back to page 1, but
// the page beyond the end is still what is answered.
long
TLibrarian::CurrentPage(RefArg reader)
{
	RefVar entry;
	RefVar isbn;
	isbn = GetVariable(reader, RSSYMisbn, nil, 0);
	entry = GetLibraryEntry(isbn);
	long page = RINT(GetFrameSlotRef(entry, RSSYMcurpage));
	if (page > CountPages(reader))
	{
		SetFrameSlot(entry, RSSYMprevpage, RefVar(MAKEINT(0)));
		SetFrameSlot(entry, RSSYMcurpage, RefVar(MAKEINT(1)));
		EntryChange(entry);
	}
	return page;
}


// ROM 0x0010b944 PreviousPage__10TLibrarianFRC6RefVar
long
TLibrarian::PreviousPage(RefArg reader)
{
	RefVar entry;
	RefVar isbn;
	isbn = GetVariable(reader, RSSYMisbn, nil, 0);
	entry = gLibrarian->GetLibraryEntry(isbn);
	return RINT(GetFrameSlotRef(entry, RSSYMprevpage));
}


// ROM 0x0010bb90 SetCurrentPage__10TLibrarianFRC6RefVarl
// The page the reader's book is at moved on: the old one kept as prevPage.
void
TLibrarian::SetCurrentPage(RefArg reader, long page)
{
	RefVar isbn;
	RefVar entry;
	isbn = GetVariable(reader, RSSYMisbn, nil, 0);
	entry = GetLibraryEntry(isbn);
	SetFrameSlot(entry, RSSYMprevpage, RefVar(GetFrameSlotRef(entry, RSSYMcurpage)));
	SetFrameSlot(entry, RSSYMcurpage, RefVar(MAKEINT(page)));
	EntryChange(entry);
}


// Copy a string's characters into a buffer of room + 1 UniChars, at most
// room of them, and terminate them: what BookAvailable does (inline) with
// the ISBN, the title and the short title.
static void
CopyChars(RefArg str, UniChar* buffer, ULong room)
{
	ULong count = (ULong) (Length(str) - 2) >> 1;
	if (count > room - 1)
		count = room;
	memmove(buffer, BinaryData(str), count * sizeof(UniChar));
	buffer[count] = 0;
}


// ROM 0x0010c61c BookAvailable__10TLibrarianFRC6RefVarT1
// BookAvailable with no source (the script's BookAvailable).
Ref
TLibrarian::BookAvailable(RefArg partFrame, RefArg packageId)
{
	return BookAvailable(partFrame, packageId, nil);
}


// ROM 0x0010c624 BookAvailable__10TLibrarianFRC6RefVarT1P10SourceType
// A part frame's book entered in the library.  ==> nil when there is no
// book (or it has no ISBN), true when a book of that ISBN is in already,
// else the frame the part handler keeps to remove it by: the ISBN and the
// task templates it registered with the Assistant (assist).
// A book that is not a help book (a part frame with a help slot, which
// goes to the Tiny Tim reader) gets an entry in the Library soup - made,
// or marked present again - and makes Copperfield one of the
// applications Find searches.  A book in memory on no device (one of the
// ROM's), on a removable store or with no source says itself in the
// Extras drawer: the ROM's help book through SetupROMHelpBook, any other
// by an AddIcon of a protoExtrasFormEntry-like frame (a book on a card
// store has its icon from the extras soup already).
// ROM BUG: with no source (FBookAvailable), the source is read from
// address 0 - the reset vector, a branch whose first two bytes are 0xea
// and 0x00 - and the book is given an icon by AddIcon.
Ref
TLibrarian::BookAvailable(RefArg partFrame, RefArg packageId, SourceType* source)
{
	static const SourceType kAddressZero = { 0xea, 0x00, 0x61a0, 0xea680c7a };	// (the ROM's first eight bytes)
	RefVar newEntry;
	RefVar isbnSym;
	RefVar entry;
	RefVar book;
	RefVar result;
	RefVar iconEntry;
	RefVar templates;
	UniChar isbnChars[24];
	UniChar titleChars[64];

	book = GetFrameSlotRef(partFrame, RSSYMbook);
	if (ISNIL(book))
		return NILREF;
	Boolean isHelp = NOTNIL(GetFrameSlotRef(partFrame, RSSYMhelp));
	RefVar isbn(GetFrameSlotRef(book, RSSYMisbn));
	if (ISNIL(isbn))
		return NILREF;
	CopyChars(isbn, isbnChars, 23);
	isbnSym = StrRefToSymbol(isbn);
	RefVar title(GetFrameSlotRef(book, RSSYMtitle));
	CopyChars(title, titleChars, 63);
	RefVar library(*fLibrary);
	if (NOTNIL(GetFrameSlotRef(library, isbnSym)))
		return TRUEREF;

	if (!isHelp)
	{
		entry = GetLibraryEntry(isbn);
		if (ISNIL(entry))
		{
			newEntry = AllocateFrame();
			SetFrameSlot(newEntry, RSSYMbookpresent, RefVar(MAKEINT(1)));
			SetFrameSlot(newEntry, RSSYMtitle, RefVar(MakeString(titleChars)));
			SetFrameSlot(newEntry, RSSYMisbn, RefVar(MakeString(isbnChars)));
			SetFrameSlot(newEntry, RSSYMcurpage, RefVar(MAKEINT(0)));
			SetFrameSlot(newEntry, RSSYMprevpage, RefVar(MAKEINT(0)));
			long renderings = Length(GetFrameSlotRef(book, RSSYMrendering));
			RefVar marks;
			RefVar mark;
			RefVar inkMarks;
			RefVar inkMark;
			marks = MakeArray(renderings);
			inkMarks = MakeArray(renderings);
			for (long i = 0; i < renderings; i++)
			{
				mark = MakeArray(0);
				SetArraySlotRef(marks, i, mark);
				inkMark = MakeArray(0);
				SetArraySlotRef(inkMarks, i, inkMark);
			}
			SetFrameSlot(newEntry, RSSYMmarks, marks);
			SetFrameSlot(newEntry, RSSYMinkmarks, inkMarks);
			SetFrameSlot(newEntry, RSSYMdata, RefVar(AllocateFrame()));
			SetFrameSlot(newEntry, RSSYMpackageid, packageId);
			SetFrameSlot(newEntry, RSSYMcurrendering, RefVar(MAKEINT(0)));
			SetFrameSlot(newEntry, RSSYMflags, RefVar(MAKEINT(0)));
			RefVar soup(DoMessage(RefVar(gRootView->GetVar(RSSYMcopperfield)), RSSYMcreategetsoup, RefVar()));
			SoupAdd(soup, newEntry);
		}
		else
		{
			SetFrameSlot(entry, RSSYMbookpresent, RefVar(MAKEINT(1)));
			SetFrameSlot(entry, RSSYMpackageid, packageId);
			EntryChange(entry);
		}
	}
	SetFrameSlot(library, isbnSym, partFrame);

	const SourceType* type = source != nil ? source : &kAddressZero;
	if (source == nil
	||  (source->deviceKind == kStoreDevice && (source->format & kRemovableMask) != 0)
	||  ((source->format & kFormatMask) != 0 && source->deviceKind == 0))
	{
		iconEntry = AllocateFrame();
		SetFrameSlot(iconEntry, RSSYMapp, isHelp ? RSSYMtinytim : RSSYMcopperfield);
		title = GetFrameSlotRef(book, RSSYMshorttitle);
		if (NOTNIL(title))
			CopyChars(title, titleChars, 63);
		SetFrameSlot(iconEntry, RSSYMtext, RefVar(MakeString(titleChars)));
		SetFrameSlot(iconEntry, RSSYMisbn, RefVar(MakeString(isbnChars)));
		SetFrameSlot(iconEntry, RSSYMpackageid, packageId);
		SetFrameSlot(iconEntry, RSSYMtype, RSSYMbook);
		if (FrameHasSlot(book, RSSYMicon))
			SetFrameSlot(iconEntry, RSSYMicon, RefVar(TotalClone(RefVar(GetFrameSlotRef(book, RSSYMicon)))));
		RefVar extras(GetVariable(gRootView->fContext, RSSYMextrasdrawer, nil, 0));
		result = MakeArray(1);
		SetArraySlotRef(result, 0, iconEntry);
		RefVar message;
		if ((type->format & kFormatMask) == 0 || type->deviceKind != 0)
		{
			SetFrameSlot(iconEntry, RSSYM_proto, RefVar(Rproto1_2Exformentry));
			message = RSSYMaddicon;
		}
		else
			message = RSSYMsetupromhelpbook;
		DoMessage(extras, message, result);
	}

	if (!isHelp)
	{
		if (fBookCount == 0)
		{
			result = GetFrameSlotRef(RefVar(gVarFrame), RSSYMfindapps);
			AddArraySlot(result, RSSYMcopperfield);
		}
		fBookCount++;
	}

	result = AllocateFrame();
	SetFrameSlot(result, RSSYMisbn, RefVar(MakeString(isbnChars)));
	if (FrameHasSlot(book, RSSYMassist))
	{
		RefVar registered(MakeArray(0));
		SetFrameSlot(result, RSSYMassist, registered);
		templates = GetFrameSlotRef(book, RSSYMassist);
		long count = Length(templates);
		for (long i = 0; i < count; i++)
		{
			RefVar templ(GetArraySlotRef(templates, i));
			RefVar id(RegTaskTemplate(RefVar(), templ));
			AddArraySlot(registered, id);
		}
	}
	return result;
}


// ROM 0x00108358 BookRemoved__10TLibrarianFRC6RefVar
// A book taken out, by the frame BookAvailable answered (and the part
// handler added to): its remove script run, the reader closed if it is
// showing it, the book taken out of the library and marked not present in
// its Library soup entry, its task templates unregistered, its icon
// dropped from the Extras drawer (unless it is a store package's, which
// the extras soup looks after), Copperfield dropped from vars.findApps with
// the last book, and the reader told to let go of the oldest book it has
// cached.  ==> -10008 when the book has no Library entry (a help book).
// ROM BUG: the count is decremented for every book with an entry, whether
// or not it was one BookAvailable counted.
long
TLibrarian::BookRemoved(RefArg bookFrame)
{
	RefVar entry;
	RefVar part;
	RefVar list;
	RefVar isbn;
	isbn = GetFrameSlotRef(bookFrame, RSSYMisbn);
	entry = GetLibraryEntry(isbn);
	if (ISNIL(entry))
		return kError_BookNotInLibrary;

	RefVar removeScript(GetFrameSlotRef(bookFrame, RSSYMbookremovescript));
	if (NOTNIL(removeScript))
	{
		RefVar args(MakeArray(1));
		SetArraySlotRef(args, 0, bookFrame);
		DoBlock(removeScript, args);
	}
	RefVar copperfield(GetVariable(gRootView->fContext, RSSYMcopperfield, nil, 0));
	RefVar shown(GetFrameSlotRef(copperfield, RSSYMisbn));
	if (NOTNIL(shown))
	{
		shown = GetFrameSlotRef(copperfield, RSSYMisbn);
		if (NOTNIL(FStrEqual(RefVar(), shown, isbn)))
			FCloseX(copperfield);
	}
	RefVar sym(StrRefToSymbol(isbn));
	RefVar library(*fLibrary);
	RemoveSlot(library, sym);
	SetFrameSlot(entry, RSSYMbookpresent, RefVar(MAKEINT(0)));
	SetFrameSlot(entry, RSSYMpackageid, RefVar());
	EntryChange(entry);
	if (FrameHasSlot(bookFrame, RSSYMassist))
	{
		RefVar templates(GetFrameSlotRef(bookFrame, RSSYMassist));
		long count = Length(templates);
		for (long i = 0; i < count; i++)
			UnRegTaskTemplate(RefVar(), RefVar(GetArraySlotRef(templates, i)));
	}
	if (ISNIL(GetFrameSlotRef(bookFrame, RSSYMtype)))
	{
		RefVar extras(GetVariable(gRootView->fContext, RSSYMextrasdrawer, nil, 0));
		list = MakeArray(1);
		RefVar icon(AllocateFrame());
		SetFrameSlot(icon, RSSYMapp, RSSYMcopperfield);
		SetFrameSlot(icon, RSSYMisbn, isbn);
		SetArraySlotRef(list, 0, icon);
		DoMessage(extras, RSSYMdropicon, list);
	}
	if (--fBookCount == 0)
	{
		list = GetFrameSlotRef(RefVar(gVarFrame), RSSYMfindapps);
		ArrayRemove(list, RSSYMcopperfield);
	}
	DoMessage(RefVar(gRootView->GetVar(RSSYMcopperfield)), RSSYMremoveoldestbook, RefVar());
	return noErr;
}


/*------------------------------------------------------------------------------
	T h e   b o o k   f u n c t i o n s
	Mostly methods of Copperfield: the reader is the receiver, and its isbn
	and curRendering say which book and which rendering of it.
------------------------------------------------------------------------------*/

// ROM 0x0010c608 FBookAvailable
// BookAvailable(partFrame, packageId)
Ref
FBookAvailable(RefArg rcvr, RefArg partFrame, RefArg packageId)
{
	return TLibrarian::gLibrarian->BookAvailable(partFrame, packageId);
}


// ROM 0x00108334 FBookRemoved
// BookRemoved(bookFrame)
Ref
FBookRemoved(RefArg rcvr, RefArg bookFrame)
{
	return MAKEINT(TLibrarian::gLibrarian->BookRemoved(bookFrame));
}


// ROM 0x00108858 BookTitle
// reader:BookTitle() - the title in the book's Library entry.
Ref
BookTitle(RefArg rcvr)
{
	RefVar entry;
	RefVar isbn;
	isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	return GetFrameSlotRef(entry, RSSYMtitle);
}


// ROM 0x00108acc CountPages
Ref
CountPages(RefArg rcvr)
{
	return MAKEINT(TLibrarian::gLibrarian->CountPages(rcvr));
}


// ROM 0x00108bec CurrentBook
// reader:CurrentBook() - the book frame, nil when the reader has no isbn.
Ref
CurrentBook(RefArg rcvr)
{
	RefVar isbn(GetVariable(rcvr, RSSYMisbn, nil, 0));
	if (ISNIL(isbn))
		return NILREF;
	return TLibrarian::gLibrarian->GetBookFrame(isbn);
}


// ROM 0x00108c5c CurrentKiosk
// reader:CurrentKiosk() - the nearest page before the current one whose
// template's flags have bit 0 set (a kiosk: a page the book goes back to),
// as {page, name: its first block's item's name}; nil at page 1.
// ROM BUG: at page 0 (a book not opened yet) the search starts below 1 and
// stops at once, and the page that is not there is asked for its blocks.
Ref
CurrentKiosk(RefArg rcvr)
{
	RefVar isbn;
	RefVar entry;
	RefVar page;
	RefVar templ;
	isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	long index = RINT(GetFrameSlotRef(entry, RSSYMcurpage));
	long flags;
	do
	{
		if (--index < 1)
			break;
		page = TLibrarian::gLibrarian->GetPageN(index, rcvr);
		templ = GetFrameSlotRef(page, RSSYMtemplate);
		if (FrameHasSlot(templ, RSSYMflags))
			flags = RINT(GetFrameSlotRef(templ, RSSYMflags));
		else
			flags = 0;
	} while ((flags & 1) == 0);
	if (index != 0)
	{
		RefVar kiosk(AllocateFrame());
		SetFrameSlot(kiosk, RSSYMpage, RefVar(MAKEINT(index)));
		RefVar blocks(GetFrameSlotRef(page, RSSYMblocks));
		blocks = GetArraySlotRef(blocks, 0);
		RefVar item(GetFrameSlotRef(blocks, RSSYMitem));
		SetFrameSlot(kiosk, RSSYMname, RefVar(GetFrameSlotRef(item, RSSYMname)));
		return kiosk;
	}
	return NILREF;
}


// ROM 0x00108ecc AddBookmark
// reader:AddBookmark(page) - the page added to the rendering's bookmarks
// (six at most: the oldest goes).  ==> the bookmarks, nil when the page is
// marked already.
Ref
AddBookmark(RefArg rcvr, RefArg page)
{
	RefVar isbn;
	RefVar entry;
	RefVar marks;
	isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	long rendering = RINT(GetVariable(rcvr, RSSYMcurrendering, nil, 0));
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	marks = GetFrameSlotRef(entry, RSSYMmarks);
	marks = GetArraySlotRef(marks, rendering);
	long count = Length(marks);
	if (count != 0)
	{
		for (long i = 0; i < count; i++)
			if (RINT(GetArraySlotRef(marks, i)) == RINT(page))
				return NILREF;
		if (count == 6)
			ArrayRemoveCount(marks, 0, 1);
	}
	AddArraySlot(marks, page);
	EntryChange(entry);
	return marks;
}


// ROM 0x00109074 CurrentPage
Ref
CurrentPage(RefArg rcvr)
{
	return MAKEINT(TLibrarian::gLibrarian->CurrentPage(rcvr));
}


// ROM 0x0010b55c InkMarks
// reader:InkMarks(page) - the ink marked on a page of the rendering (the
// entry's inkMarks are [page, marks, page, marks, ...]); an empty array
// when there is none; with page nil, the whole list.
Ref
InkMarks(RefArg rcvr, RefArg page)
{
	RefVar entry;
	RefVar isbn;
	RefVar marks;
	isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	long rendering = RINT(GetVariable(rcvr, RSSYMcurrendering, nil, 0));
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	marks = GetFrameSlotRef(entry, RSSYMinkmarks);
	marks = GetArraySlotRef(marks, rendering);
	if (ISNIL(page))
		return marks;
	long count = Length(marks);
	if (count != 0)
	{
		long pageNo = RINT(page);
		for (long i = 0; i < count; i += 2)
			if (RINT(GetArraySlotRef(marks, i)) == pageNo)
				return GetArraySlotRef(marks, i + 1);
	}
	return MakeArray(0);
}


// ROM 0x0010b6e4 AuthorData
// reader:AuthorData() - the frame a book may keep its own things in.
Ref
AuthorData(RefArg rcvr)
{
	RefVar isbn;
	RefVar entry;
	isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	return GetFrameSlotRef(entry, RSSYMdata);
}


// ROM 0x0010b8c4 PrepBook
// copperfield:PrepBookX(isbn) - what is left of it is a debugging line.
Ref
PrepBook(RefArg rcvr, RefArg isbn)
{
	gREPout->Print("PrepBook-isbn=");
	PrintObject(isbn, 0);
	gREPout->Print("\r");
	return NILREF;
}


// ROM 0x0010b91c PreviousPage
Ref
PreviousPage(RefArg rcvr)
{
	return MAKEINT(TLibrarian::gLibrarian->PreviousPage(rcvr));
}


// ROM 0x0010b9e8 RegisterBookRef
// RegisterBookRef(isbn, partFrame) - a book put in the library by hand.
Ref
RegisterBookRef(RefArg rcvr, RefArg isbn, RefArg partFrame)
{
	RefVar sym(TLibrarian::gLibrarian->StrRefToSymbol(isbn));
	RefVar library(*TLibrarian::gLibrarian->fLibrary);
	SetFrameSlot(library, sym, partFrame);
	return NILREF;
}


// ROM 0x0010bf80 UnregisterBookRef
Ref
UnregisterBookRef(RefArg rcvr, RefArg isbn)
{
	RefVar sym(TLibrarian::gLibrarian->StrRefToSymbol(isbn));
	RefVar library(*TLibrarian::gLibrarian->fLibrary);
	RemoveSlot(library, sym);
	return NILREF;
}


// ROM 0x0010bda8 Bookmarks
// reader:Bookmarks() - the rendering's bookmarks.
Ref
Bookmarks(RefArg rcvr)
{
	RefVar isbn;
	RefVar entry;
	RefVar marks;
	isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	long rendering = RINT(GetVariable(rcvr, RSSYMcurrendering, nil, 0));
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	marks = GetFrameSlotRef(entry, RSSYMmarks);
	return GetArraySlotRef(marks, rendering);
}


// ROM 0x0010be88 UpdateBookmarks
// reader:UpdateBookmarks(marks) - the rendering's bookmarks replaced.
// ==> every rendering's.
Ref
UpdateBookmarks(RefArg rcvr, RefArg newMarks)
{
	RefVar isbn;
	RefVar entry;
	RefVar marks;
	isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	long rendering = RINT(GetVariable(rcvr, RSSYMcurrendering, nil, 0));
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	marks = GetFrameSlotRef(entry, RSSYMmarks);
	SetArraySlotRef(marks, rendering, newMarks);
	EntryChange(entry);
	return marks;
}


// ROM 0x0010bfdc WhereIsBook
// reader:WhereIsBook(isbn) - {library: its Library entry, bookSoup: its
// part frame}; nil when the book is not in.  isbn nil: the reader's own.
Ref
WhereIsBook(RefArg rcvr, RefArg isbnArg)
{
	RefVar entry;
	RefVar sym;
	RefVar isbn(isbnArg);
	if (ISNIL(isbnArg))
		isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	RefVar where(AllocateFrame());
	SetFrameSlot(where, RSSYMlibrary, entry);
	sym = TLibrarian::gLibrarian->StrRefToSymbol(isbn);
	RefVar part(GetFrameSlotRef(*TLibrarian::gLibrarian->fLibrary, sym));
	if (ISNIL(part))
		return NILREF;
	SetFrameSlot(where, RSSYMbooksoup, part);
	return where;
}


// ROM 0x0010d108 BookClosed
// reader:BookClosed() - the book's Library entry written back.
Ref
BookClosed(RefArg rcvr)
{
	RefVar isbn;
	RefVar entry;
	isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	EntryChange(entry);
	return TRUEREF;
}


void
RegisterBookNatives(void)
{
	RegisterNativeFunction("FBookAvailable", (void*) FBookAvailable, 2);
	RegisterNativeFunction("FBookRemoved", (void*) FBookRemoved, 1);
	RegisterNativeFunction("BookTitle", (void*) BookTitle, 0);
	RegisterNativeFunction("CountPages", (void*) CountPages, 0);
	RegisterNativeFunction("CurrentBook", (void*) CurrentBook, 0);
	RegisterNativeFunction("CurrentKiosk", (void*) CurrentKiosk, 0);
	RegisterNativeFunction("AddBookmark", (void*) AddBookmark, 1);
	RegisterNativeFunction("CurrentPage", (void*) CurrentPage, 0);
	RegisterNativeFunction("InkMarks", (void*) InkMarks, 1);
	RegisterNativeFunction("AuthorData", (void*) AuthorData, 0);
	RegisterNativeFunction("PrepBook", (void*) PrepBook, 1);
	RegisterNativeFunction("PreviousPage", (void*) PreviousPage, 0);
	RegisterNativeFunction("RegisterBookRef", (void*) RegisterBookRef, 2);
	RegisterNativeFunction("UnregisterBookRef", (void*) UnregisterBookRef, 1);
	RegisterNativeFunction("Bookmarks", (void*) Bookmarks, 0);
	RegisterNativeFunction("UpdateBookmarks", (void*) UpdateBookmarks, 1);
	RegisterNativeFunction("WhereIsBook", (void*) WhereIsBook, 1);
	RegisterNativeFunction("BookClosed", (void*) BookClosed, 0);
}
