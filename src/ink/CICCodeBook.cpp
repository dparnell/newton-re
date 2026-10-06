/*
	File:		ink/CICCodeBook.cpp

	Contains:	The two code books the ink codec decodes and encodes
				against.  See CICCodec.h.
*/

#include "CICCodec.h"
#include "ParaGraph.h"			// HWRStrCpy, HWRStrCat, HWRMemory*
#include "objects.h"
#include "ROMConstants.h"

#include <string.h>
#include <stdio.h>


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


/*------------------------------------------------------------------------------
	T h e   C I C   l i b r a r y ' s   f i l e s

	The library reads its code books from files where it has a file
	system; on a Newton every one of these answers nought - no file opens,
	nothing is read - so the books must already be where the two globals
	say (InitializeParagraphCompression).
------------------------------------------------------------------------------*/

// ROM 0x001543f4 HWRFileOpen__FPcUiT2
void*	HWRFileOpen(const char* /*name*/, ULong /*mode*/, ULong /*flags*/)	{ return nil; }
// ROM 0x00154400 HWRFileSeek__FPvlUi
long	HWRFileSeek(void* /*file*/, long /*offset*/, ULong /*from*/)		{ return 0; }
// ROM 0x00154408 HWRFileTell__FPv
long	HWRFileTell(void* /*file*/)											{ return 0; }
// ROM 0x00154410 HWRFileRead__FPvT1Ui
long	HWRFileRead(void* /*file*/, void* /*buffer*/, ULong /*size*/)		{ return 0; }
// ROM 0x00154418 HWRFileClose__FPv
long	HWRFileClose(void* /*file*/)										{ return 0; }


// ROM 0x002806cc LockBook__FPcP10_BOOKENTRY
// A book opened: already open, and it is just counted again; not open,
// and it comes from whichever global holds it - the name's fifth
// character says which, '1' for the writing book and anything else for
// the ink one.
//
// A book not yet in its global is read from "ParaGraphCode" + name +
// ".bin" through the CIC library's file calls - which on a Newton open
// nothing (HWRFileOpen answers nought), so that path only ever prints
// "Cannot load the code book !!!" and gives up.
//
// ROM BUG (fixed): the entry's handle is set from a register the "already
// there" path never loaded, so a book that came from a global gets
// whatever was lying in it.  Nothing ever frees through that handle -
// UnlockBook only counts down - so it does no harm.  The host cannot have
// the register's garbage and gives such a book no handle (nil) whichever
// way RomBugFixed() is set - which is the fix: a book it did not allocate
// has no handle of its own.
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
	Handle handle = nil;
	if (book == nil)
	{
		char path[40];
		HWRStrCpy(path, "ParaGraphCode");
		HWRStrCat(path, name);
		HWRStrCat(path, ".bin");
		void* file = HWRFileOpen(path, 1, 0x21);
		if (file == nil)
		{
			printf("\rCannot load the code book !!!\r");
			return nil;
		}
		HWRFileSeek(file, 0, 2);
		ULong size = (UShort) HWRFileTell(file);
		HWRFileSeek(file, 0, 0);
		handle = HWRMemoryAllocHandle(size);
		if (handle == nil)
			return nil;
		book = HWRMemoryLockHandle(handle);
		if (book == nil || (ULong) HWRFileRead(file, book, size) != size)
		{
			HWRFileClose(file);
			if (book != nil)
				HWRMemoryUnlockHandle(handle);
			HWRMemoryFreeHandle(handle);
			return nil;
		}
		HWRFileClose(file);
		if (isWritingBook)
			gCodeBook = book;
		else
			gInkCodeBook = book;
	}
	entry->fCount = 1;
	entry->fHandle = handle;		// (the ROM's register is stale when the book came from a global)
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
