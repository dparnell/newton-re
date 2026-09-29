/*
	File:		books/Librarian.h

	Contains:	The book reader's C++ side: TLibrarian, the list of the
				books that are installed, and TBookPartHandler, which
				installs a 'book part.

				The book reader itself is the ROM's NewtonScript
				application Copperfield (the root view's copperfield);
				TLibrarian is what it asks about books.  gLibrarian keeps
				the library - a frame from each book's ISBN (as a symbol)
				to the part frame that holds the book - and a count of the
				books that are not help books, while the Library soup on
				the internal store keeps what a reader has done to each
				book (the page it is at, its bookmarks and ink marks) as
				an entry per ISBN, whether the book is in or not.

				A book arriving (BookAvailable) is entered in the library
				and the soup, an icon is put in the Extras drawer for it
				(the ROM's help book a special entry of its own,
				SetupROMHelpBook), Copperfield is added to the applications
				Find searches, and the task templates the book offers the
				Assistant are registered.  A book going (BookRemoved) takes
				all of that back.  The book's pages are its renderings
				(TLibrarian::Rendering: one per screen shape a book was
				laid out for, picked by the reader's curRendering), each an
				array of page frames (Pages, GetPageN).

				The book format and the reader's layers: docs/books/README.md.

	Reconstructed from the MP2x00 US ROM (0x001082f8-0x0010d180); each
	function cites its origin.
*/

#ifndef __LIBRARIAN_H
#define __LIBRARIAN_H

#ifndef __PARTHANDLER_H
#include "PartHandler.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __VIEW_H
#include "View.h"
#endif

const long clLibrarian = 0x68;

// 0xc bytes on the MessagePad: the vtable, the library frame (a RefHandle
// the class keeps alive) and the count of books that are not help books -
// Copperfield is in vars.findApps while there is one.
class TLibrarian : public TResponder
{
public:
	virtual long	ClassID(void) const;
	virtual Boolean	DerivedFrom(long id) const;

	Ref			BookAvailable(RefArg partFrame, RefArg packageId);
	Ref			BookAvailable(RefArg partFrame, RefArg packageId, SourceType* source);
	long		BookRemoved(RefArg bookFrame);

	Ref			GetBookFrame(RefArg isbn);
	Ref			GetLibraryEntry(RefArg isbn);
	Ref			StrRefToSymbol(RefArg str);

	Ref			Rendering(RefArg reader);
	Ref			Pages(RefArg reader);
	Ref			GetPageN(long page, RefArg reader);
	Ref			PageSize(RefArg reader);
	long		CountPages(RefArg reader);
	long		CurrentPage(RefArg reader);
	long		PreviousPage(RefArg reader);
	void		SetCurrentPage(RefArg reader, long page);

	Boolean		CompareValues(RefArg item, RefArg slot, RefArg value);
	Ref			FindContentByValue(RefArg reader, RefArg slot, RefArg value, RefArg book);
	long		FindPageByContent(RefArg reader, RefArg item, long offset, long* blockIndex, RefArg book);

	// the search (Search.cpp)
	Boolean		Encode(const UChar* chars, UShort* code);
	Boolean		CheckHints(const UShort* codes, const char* hints, long count);
	Boolean		TextSearch(const UniChar* word, long length, const UniChar* text, long* pos, long end, long* found);
	Ref			Find(UniChar* word, RefArg owner, RefArg results, RefArg arg4, RefArg status, RefArg books);
	Ref			FindContentBySlot(RefArg reader, RefArg slot, RefArg book);
	Ref			FindPageByValue(RefArg reader, RefArg slot, RefArg value, RefArg book);

	static TLibrarian*	gLibrarian;		// (0x0c1010d0)

	RefStruct*	fLibrary;				// +0x04  { isbnSymbol: partFrame, ... }
	long		fBookCount;				// +0x08  the books that are not help books
};

// the librarian made and the library started; TNotebook::Constructor
// calls it once the root view is up
void			InitLibrarian(void);
TLibrarian*		Librarian(void);

// 'book: a part whose frame's book slot is a book
class TBookPartHandler : public TPartHandler
{
public:
	virtual	NewtonErr	Install(const PartId& partId, SourceType sourceType, PartInfo* partInfo);
	virtual	NewtonErr	Remove(const PartId& partId, PartType partType, RemoveObjPtr removePtr);
	virtual	NewtonErr	Expand(void* data, CPipe* pipe, PartInfo* info);

private:
	NewtonErr			InstallBook(RefArg partFrame, const PartId& partId, SourceType sourceType, class TImportedObjectArea** area);
};

void			ExtractWords(const UniChar* text, long* start, long* end);		// ROM 0x00109220 ExtractWords__FPUsPlT2

// the books' NewtonScript functions
void			RegisterBookNatives(void);
void			RegisterSearchNatives(void);		// Search.cpp

#endif	/* __LIBRARIAN_H */
