/*
	File:		ink/CICCodeBook.cpp

	Contains:	The two code books the ink codec decodes and encodes
				against.  See CICCodec.h.
*/

#include "CICCodec.h"
#include "objects.h"
#include "ROMConstants.h"

#include <string.h>


// ROM 0x0c104fc8 globalCodeBookPtr
// ROM 0x0c104fcc globalCodeBookPtrInk
// The two books, once somebody has said where they are.
void*	gCodeBook = nil;
void*	gInkCodeBook = nil;


// One book as the codec holds it: how many callers have it open, the
// handle it was read into (when it was read from a file, which on a
// Newton never happens), and where it is.
struct BookEntry
{
	ULong	fCount;			// +0x00  (the ROM keeps it in the top halfword)
	void*	fHandle;		// +0x04
	void*	fData;			// +0x08
};

// ROM 0x0c104fd0 BookList
// (the second entry is the four words after it, at 0x0c104fdc)
static BookEntry	gBookList[2];


// ROM 0x001543a4 InitializeParagraphCompression__Fv
// Where the two code books are: the ROM's own binaries.  The CIC
// library would read them out of files called ParaGraphCodebook1.bin
// and ParaGraphCodebook2.bin, and LockBook still tries to; on a Newton
// HWRFileOpen answers nothing, so this is what puts them in place
// before anything asks for one.
void
InitializeParagraphCompression(void)
{
	gCodeBook = BinaryData(RefVar(Rparagraphcodebook1));
	gInkCodeBook = BinaryData(RefVar(Rparagraphcodebook2));
}


// ROM 0x002806cc LockBook__FPcP10_BOOKENTRY
// A book opened: already open, and it is just counted again; not open,
// and it comes from whichever global holds it - the name's fifth
// character says which, '1' for the writing book and anything else for
// the ink one.
//
// (The ROM would otherwise read "ParaGraphCode" + name + ".bin" through
// the CIC library's file calls.  HWRFileOpen 0x001543f4 answers 0 on a
// Newton - the whole file layer is stubbed out - so that path only ever
// prints "Cannot load the code book !!!" and gives up, and the host has
// no files either.  It is left here because it is what the code says.)
//
// ROM bug kept: the entry's handle is set from a register the "already
// there" path never loaded, so a book that came from a global gets
// whatever was lying in it.  Nothing ever frees through that handle -
// UnlockBook only counts down - so it does no harm.
void*
LockBook(const char* name, BookEntry* entry)
{
	if (entry->fData != nil)
	{
		entry->fCount++;
		return entry->fData;
	}
	Boolean isWritingBook = name[4] == '1';
	void* book = isWritingBook ? gCodeBook : gInkCodeBook;
	if (book == nil)
		return nil;					// (the ROM tries the file here, and cannot)
	entry->fCount = 1;
	entry->fHandle = nil;			// (the ROM leaves a stale register here)
	entry->fData = book;
	return book;
}


// ROM 0x002808ac UnlockBook__FP10_BOOKENTRY
// One caller fewer.  The book itself stays where it is.
Boolean
UnlockBook(BookEntry* entry)
{
	if (entry->fCount != 0)
		entry->fCount--;
	return true;
}


// ROM 0x002808d4 LockCodeBook__FUs
// Book 1 is the writing one and book 2 the ink one; anything else is
// answered with the number itself, which is the CIC library's way of
// saying it does not know.
void*
LockCodeBook(ULong which)
{
	if (which == 1)
		return LockBook("book1", &gBookList[0]);
	if (which == 2)
		return LockBook("book2", &gBookList[1]);
	return (void*) which;
}


// ROM 0x00280914 UnlockCodeBook__FUs
Boolean
UnlockCodeBook(ULong which)
{
	if (which == 1)
		UnlockBook(&gBookList[0]);
	else if (which == 2)
		UnlockBook(&gBookList[1]);
	return true;
}


// (host: the tests want to know, and nothing else does.)
ULong
CodeBookUseCount(ULong which)
{
	if (which == 1 || which == 2)
		return gBookList[which - 1].fCount;
	return 0;
}
