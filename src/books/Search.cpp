/*
	File:		books/Search.cpp

	Contains:	Finding things in books: the Find slip's search of the
				books in the library (TLibrarian::Find, CuFind), and the
				questions the reader asks of a book's contents and pages
				(FindContentBySlot, FindPageByValue, FindPageBySubject,
				TurnToContent), with the ink marked on its pages
				(AddInkMarks).

				A book may carry hints: for each content item, a binary
				bit set of the three-letter runs of its text, each run
				encoded in fifteen bits (TLibrarian::Encode, over the
				five-bit alphabet FiveBitASCII_Adobe, which folds cases
				and accents together).  Find skips an item whose hints
				lack a run of the word sought, and searches the text of
				the rest a character at a time (TextSearch: cases folded,
				and only at the start of a word).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Librarian.h"
#include "Pages.h"
#include "Entries.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "NewtonExceptions.h"
#include "host/RomBugs.h"
#include "NewtonMemory.h"

extern const unsigned char	FiveBitASCII_Adobe[256];		// BookTables.cpp (generated)
extern const ExceptionName exOutOfMemory;
Ref		FSetContains(RefArg rcvr, RefArg array, RefArg target);	// views/ClipboardView.cpp


// ROM 0x001091b4 Encode__10TLibrarianFPUcPUs
// Three characters (Mac encoding) as a fifteen-bit code: five bits each
// out of FiveBitASCII_Adobe, the first two shifted and exclusive-ored
// together.  ==> false, and a code of nought, when any of them is not in
// the alphabet.
Boolean
TLibrarian::Encode(const UChar* chars, UShort* code)
{
	*code = 0;
	ULong a = FiveBitASCII_Adobe[chars[0]];
	if (a == 0)
		return false;
	ULong b = FiveBitASCII_Adobe[chars[1]];
	if (b == 0)
		return false;
	ULong ab = (b ^ (a << 2)) & 0xffff;
	ULong c = FiveBitASCII_Adobe[chars[2]];
	if (c == 0)
		return false;
	*code = (UShort) (c ^ (ab << 3));
	return true;
}


// ROM 0x001088ec CheckHints__10TLibrarianFPUsPcl
// Whether every code of the word (but nought, a run not in the alphabet)
// is set in an item's hints.
Boolean
TLibrarian::CheckHints(const UShort* codes, const char* hints, long count)
{
	UShort i = 0;
	for ( ; count != 0; count--)
	{
		ULong code = codes[i];
		if (code != 0 && (((UByte) hints[code >> 3]) & (1 << (code & 7))) == 0)
			return false;
		i = (UShort) (i + 1);
	}
	return true;
}


// ROM 0x0010bce8 TextSearch__10TLibrarianFPUslT1PlT2T4
// The word (upper case, length characters) looked for in text from *pos
// to end, cases folded (a to z only), where a word starts.  ==> whether it
// was found: *found its start and *pos past it; else *pos the end.
// ROM BUG (fixed): a character that breaks a partial match is not tried as
// the start of a new one, so "aab" does not contain "ab".  The fix tries
// the word at every position in turn (a match that does not start a word
// passed over, as before).
Boolean
TLibrarian::TextSearch(const UniChar* word, long length, const UniChar* text, long* pos, long end, long* found)
{
	if (RomBugFixed())
	{
		for (long start = *pos; start + length <= end; start++)
		{
			long k = 0;
			for ( ; k < length; k++)
			{
				ULong c = text[start + k];
				if (c > 0x60 && c < 0x7b)
					c -= 0x20;
				if (word[k] != (UniChar) c)
					break;
			}
			if (k == length && (start == 0 || !IsAlphaNumeric(text[start - 1])))
			{
				*found = start;
				*pos = start + length;
				return true;
			}
		}
		*pos = end;
		return false;
	}
	long matched = 0;
	long i = *pos;
	for ( ; ; )
	{
		for ( ; ; )
		{
			if (i >= end)
			{
				*pos = i;
				return false;
			}
			ULong c = text[i];
			if (c > 0x60 && c < 0x7b)
				c -= 0x20;
			i++;
			if (word[matched] == (UniChar) c)
				break;
			matched = 0;
		}
		matched++;
		if (matched == length
		&&  (i - length == 0 || !IsAlphaNumeric(text[i - length - 1])))
			break;
	}
	*found = i - length;
	*pos = i;
	return true;
}


// ROM 0x00109220 ExtractWords__FPUsPlT2
// The stretch of text shown for something found at *start: back to the
// start of the word before it (or only its own word's, at the start of a
// line), and on to the end of the word after it (or only its own, at the
// start of a line); *start and *end (a length on the way out) made that
// stretch.
void
ExtractWords(const UniChar* text, long* start, long* end)
{
	Boolean twoWords = true;
	long from = *start;
	long back = from;
	for ( ; ; )
	{
		if (back <= 0)
			break;
		UniChar c = text[back];
		if (c == U_CONST_CHAR('\t') || c == U_CONST_CHAR(' '))
			break;
		if (c == U_CONST_CHAR(0x0d))
		{
			twoWords = false;
			goto forward;
		}
		back--;
	}
	for ( ; ; )
	{
		back--;
		if (back < 1)
			break;
		UniChar c = text[back];
		if (c == U_CONST_CHAR(0x0d) || c == U_CONST_CHAR('\t') || c == U_CONST_CHAR(' '))
			break;
	}
forward:
	long limit = *end;
	while (from < limit)
	{
		UniChar c = text[from];
		if (c == U_CONST_CHAR(0x0d) || c == U_CONST_CHAR('\t') || c == U_CONST_CHAR(' '))
			break;
		from++;
	}
	long last;
	for ( ; ; )
	{
		last = from;
		from = last + 1;
		if (from >= limit)
			break;
		UniChar c = text[from];
		if (c == U_CONST_CHAR(0x0d) || c == U_CONST_CHAR('\t') || c == U_CONST_CHAR(' '))
			break;
	}
	if (!twoWords)
	{
		from = last + 2;
		while (from < limit)
		{
			UniChar c = text[from];
			if (c == U_CONST_CHAR(0x0d) || c == U_CONST_CHAR('\t') || c == U_CONST_CHAR(' '))
				break;
			from++;
		}
	}
	if (back < 1)
		back = 0;
	*end = from - back;
	*start = back;
}


// One thing found (inline in the ROM, three times): the Find slip's entry
// {title, owner, isbn, found}.
static void
AddFound(RefArg items, RefArg title, RefArg owner, RefArg isbn, RefArg found)
{
	RefVar entry(AllocateFrame());
	SetFrameSlot(entry, RSSYMtitle, title);
	SetFrameSlot(entry, RSSYMowner, owner);
	SetFrameSlot(entry, RSSYMisbn, isbn);
	SetFrameSlot(entry, RSSYMfound, found);
	AddArraySlot(items, entry);
}


// ROM 0x00109388 Find__10TLibrarianFPUsRC6RefVarN42
// The Find slip's search of the books whose library entries are given:
// for each book, the status view told the book's title in quotes
// (SetStatus); then every content item with hints whose hints do not rule
// the word out - put through the book's mungeContentScript, then asked by
// the book's bookSearchScript (an answer other than nil or true is what
// was found), or by a form's formSearchScript when the form has no text,
// or else its text searched (TextSearch) - each thing found added to a
// {title, items, appSymbol: 'copperfield} entry of the results, its title
// the words round it between ellipses.  ==> the last book's entry, nil
// when nothing was found in any.
// ROM BUG (fixed): the book's scripts, once one book has them, are kept
// for the books after it that have none.  The fix forgets them at each
// book.
// ROM BUG (fixed): a book with no hints is not searched at all.  The fix
// searches every content item of such a book, as one whose hints do not
// rule the word out.
// ROM BUG (fixed): the entry answered is the last one made, which the
// next book found nothing in does not clear.  The fix is no change: the
// callers (Copperfield's find and FindTargeted) only ask whether the
// answer is nil, to learn whether anything was found in any book - which
// the last entry made answers rightly, and the last book's own would not.
Ref
TLibrarian::Find(UniChar* word, RefArg owner, RefArg results, RefArg arg4, RefArg status, RefArg books)
{
	RefVar book;
	RefVar isbn;
	RefVar sym;
	RefVar args;
	RefVar hints;
	RefVar contents;
	RefVar hint;
	RefVar item;
	RefVar text;
	RefVar found;
	RefVar entry;
	RefVar items;
	RefVar bookEntry;
	RefVar searchScript;
	RefVar mungeScript;
	UniChar ellipsis[2];
	UniChar title[40];

	ConvertToUnicode("\xC9", ellipsis, kMacRomanEncoding, 0x7fffffff);
	long length = Ustrlen(word);
	char* chars = NewPtr((length + 1) * 2);
	if (chars == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	ConvertFromUnicode(word, chars, kMacRomanEncoding, 0x7fffffff);
	long codeCount = length - 2;
	if (codeCount < 1)
		codeCount = 0;
	UShort* codes = (UShort*) NewPtr(codeCount * sizeof(UShort));
	if (codes == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	for (long i = 0; i < codeCount; i++)
		Encode((const UChar*) chars + i, codes + i);
	DisposPtr(chars);
	for (UniChar* p = word; *p != 0; p++)
		if (*p > 0x60 && *p < 0x7b)
			*p = (UniChar) (*p - 0x20);

	long bookCount = Length(books);
	for (long b = 0; b < bookCount; b++)
	{
		isbn = GetFrameSlotRef(RefVar(GetArraySlotRef(books, b)), RSSYMisbn);
		sym = StrRefToSymbol(isbn);
		book = GetFrameSlotRef(*fLibrary, sym);
		book = GetFrameSlotRef(book, RSSYMbook);
		if (RomBugFixed())
		{
			searchScript = NILREF;
			mungeScript = NILREF;
		}
		if (FrameHasSlot(book, RSSYMscripts))
		{
			RefVar scripts(GetFrameSlotRef(book, RSSYMscripts));
			if (NOTNIL(scripts))
			{
				RefVar index(FSetContains(RefVar(), scripts, RSSYMbooksearchscript));
				if (NOTNIL(index))
					searchScript = GetArraySlotRef(scripts, RINT(index) + 1);
				index = FSetContains(RefVar(), scripts, RSSYMmungecontentscript);
				if (NOTNIL(index))
					mungeScript = GetArraySlotRef(scripts, RINT(index) + 1);
			}
		}
		Boolean anyFound = false;
		RefVar quoted(MakeArray(3));
		SetArraySlotRef(quoted, 0, MAKEMAGICPTR(255));
		SetArraySlotRef(quoted, 1, GetFrameSlotRef(book, RSSYMtitle));
		SetArraySlotRef(quoted, 2, MAKEMAGICPTR(256));
		args = MakeArray(1);
		SetArraySlotRef(args, 0, FStringer(RefVar(), quoted));
		DoMessage(status, RSSYMsetstatus, args);
		items = MakeArray(0);
		hints = GetFrameSlotRef(book, RSSYMhints);
		Boolean noHints = false;
		if (ISNIL(hints))
		{
			if (!RomBugFixed())
				continue;
			noHints = true;
		}
		contents = GetFrameSlotRef(book, RSSYMcontents);
		if (noHints && ISNIL(contents))
			continue;
		long hintCount = noHints ? Length(contents) : Length(hints);
		for (long i = 0; i < hintCount; i++)
		{
			hint = noHints ? RefVar(TRUEREF) : RefVar(GetArraySlotRef(hints, i));
			if (ISNIL(hint))
				continue;
			if (EQRef(ClassOf(hint), RSSYMdata))
			{
				if (!CheckHints(codes, (const char*) BinaryData(hint), codeCount))
					continue;
			}
			item = GetArraySlotRef(contents, i);
			if (NOTNIL(mungeScript))
			{
				args = MakeArray(1);
				SetArraySlotRef(args, 0, item);
				item = DoBlock(mungeScript, args);
			}
			if (NOTNIL(searchScript))
			{
				args = MakeArray(5);
				SetArraySlotRef(args, 0, MakeString(word));
				SetArraySlotRef(args, 1, MAKEINT(length));
				SetArraySlotRef(args, 2, item);
				SetArraySlotRef(args, 3, GetFrameSlotRef(book, RSSYMdata));
				SetArraySlotRef(args, 4, book);
				found = DoBlock(searchScript, args);
				if (NOTNIL(found))
				{
					if (!EQRef(found, TRUEREF))
					{
						SetFrameSlot(found, RSSYMitem, item);
						SetFrameSlot(found, RSSYMbooksoup, book);
						AddFound(items, RefVar(GetFrameSlotRef(found, RSSYMtitle)), owner, isbn, found);
						anyFound = true;
					}
					continue;
				}
			}
			if (EQRef(GetFrameSlotRef(item, RSSYMtype), RSSYMform))
			{
				text = GetFrameSlotRef(item, RSSYMtext);
				if (ISNIL(text))
				{
					text = GetFrameSlotRef(item, RSSYMdata);
					RefVar formSearch(GetVariable(text, RSSYMformsearchscript, nil, 0));
					if (NOTNIL(formSearch))
					{
						args = MakeArray(2);
						SetArraySlotRef(args, 0, MakeString(word));
						SetArraySlotRef(args, 1, MAKEINT(length));
						found = DoBlock(formSearch, args);
						if (NOTNIL(found))
						{
							SetFrameSlot(found, RSSYMitem, item);
							SetFrameSlot(found, RSSYMbooksoup, book);
							AddFound(items, RefVar(GetFrameSlotRef(found, RSSYMtitle)), owner, isbn, found);
							anyFound = true;
						}
					}
					continue;
				}
			}
			else
				text = GetFrameSlotRef(item, RSSYMdata);
			long textLength = (ULong) (Length(text) - 2) >> 1;
			if (length <= textLength)
			{
				const UniChar* chars = (const UniChar*) BinaryData(text);
				long pos = 0;
				long start;
				do
				{
					if (TextSearch(word, length, chars, &pos, textLength, &start))
					{
						found = AllocateFrame();
						SetFrameSlot(found, RSSYMlen, RefVar(MAKEINT(length)));
						SetFrameSlot(found, RSSYMitem, item);
						SetFrameSlot(found, RSSYMbooksoup, book);
						SetFrameSlot(found, RSSYMchar, RefVar(MAKEINT(start)));
						title[0] = 0;
						if (start > 0)
							Ustrcpy(title, ellipsis);
						long end = textLength;
						ExtractWords(chars, &start, &end);
						if (end >= 0x22)
							end = 0x22;
						Ustrncat(title, chars + start, end);
						Ustrcat(title, ellipsis);
						AddFound(items, RefVar(MakeString(title)), owner, isbn, found);
						anyFound = true;
					}
				} while (length + pos <= textLength);
			}
		}
		if (anyFound)
		{
			bookEntry = AllocateFrame();
			SetFrameSlot(bookEntry, RSSYMtitle, RefVar(GetFrameSlotRef(book, RSSYMtitle)));
			SetFrameSlot(bookEntry, RSSYMitems, items);
			SetFrameSlot(bookEntry, RSSYMappsymbol, RSSYMcopperfield);
			AddArraySlot(results, bookEntry);
		}
	}
	DisposPtr((Ptr) codes);
	return bookEntry;
}


// ROM 0x0010a1a0 FindContentBySlot__10TLibrarianFRC6RefVarN21
// The content items of a book that have the slot (or all the slots of an
// array); the book is the reader's (book nil or true: true stops at the
// first) or the one given.
Ref
TLibrarian::FindContentBySlot(RefArg reader, RefArg slot, RefArg bookArg)
{
	RefVar isbn;
	RefVar book;
	RefVar contents;
	RefVar item;
	RefVar found;
	Boolean firstOnly = false;
	if (ISNIL(bookArg) || EQRef(bookArg, TRUEREF))
	{
		isbn = GetVariable(reader, RSSYMisbn, nil, 0);
		book = GetBookFrame(isbn);
		firstOnly = EQRef(bookArg, TRUEREF);
	}
	else
		book = bookArg;
	found = MakeArray(0);
	contents = GetFrameSlotRef(book, RSSYMcontents);
	long count = Length(contents);
	Boolean manySlots = IsArray(slot);
	long slotCount = manySlots ? Length(slot) : 0;
	for (long i = 0; i < count; i++)
	{
		item = GetArraySlotRef(contents, i);
		Boolean has = true;
		if (manySlots)
		{
			for (long j = 0; j < slotCount && has; j++)
				has = FrameHasSlot(item, RefVar(GetArraySlotRef(slot, j)));
		}
		else
			has = FrameHasSlot(item, slot);
		if (has)
		{
			AddArraySlot(found, item);
			if (firstOnly)
				break;
		}
	}
	return found;
}


// ROM 0x0010ae68 FindPageByValue__10TLibrarianFRC6RefVarN31
// The pages (from 1) of the reader's rendering (or the given book's
// rendering of the same number) with a block whose item's slot holds the
// value (or whose slots hold the values), a page once for each such block;
// items whose layout has bit 11 or 14 set are passed over.
// ROM BUG (fixed): book true (first only) stops at the first block found
// on each page, not at the first page.  The fix answers the first page
// alone.
Ref
TLibrarian::FindPageByValue(RefArg reader, RefArg slot, RefArg value, RefArg bookArg)
{
	RefVar pages;
	RefVar page;
	RefVar blocks;
	RefVar block;
	RefVar item;
	RefVar found;
	RefVar layout;
	Boolean firstOnly = false;
	Boolean manySlots = false;
	long slotCount = 0;
	if (ISNIL(bookArg) || EQRef(bookArg, TRUEREF))
	{
		if (EQRef(bookArg, TRUEREF))
			firstOnly = true;
		pages = Pages(reader);
	}
	else
	{
		pages = GetFrameSlotRef(bookArg, RSSYMrendering);
		pages = GetArraySlotRef(pages, RINT(GetVariable(reader, RSSYMcurrendering, nil, 0)));
		pages = GetFrameSlotRef(pages, RSSYMpages);
	}
	long pageCount = Length(pages);
	found = MakeArray(0);
	if (IsArray(slot))
	{
		slotCount = Length(slot);
		manySlots = true;
	}
	for (long p = 0; p < pageCount; p++)
	{
		page = GetArraySlotRef(pages, p);
		blocks = GetFrameSlotRef(page, RSSYMblocks);
		long blockCount = Length(blocks);
		for (long i = 0; i < blockCount; i++)
		{
			block = GetArraySlotRef(blocks, i);
			item = GetFrameSlotRef(block, RSSYMitem);
			layout = GetFrameSlotRef(item, RSSYMlayout);
			if (NOTNIL(layout) && (RINT(layout) & 0x4800) != 0)
				continue;
			Boolean matches = true;
			if (!manySlots)
				matches = CompareValues(item, slot, value);
			else
				for (long j = 0; j < slotCount && matches; j++)
					matches = CompareValues(item, RefVar(GetArraySlotRef(slot, j)), RefVar(GetArraySlotRef(value, j)));
			if (!matches)
				continue;
			AddArraySlot(found, RefVar(MAKEINT(p + 1)));
			if (firstOnly)
			{
				if (RomBugFixed())
					return found;
				break;
			}
		}
	}
	return found;
}


/*------------------------------------------------------------------------------
	T h e   f u n c t i o n s
------------------------------------------------------------------------------*/

// ROM 0x00108b3c CuFind
// CuFind(word, owner, results, arg, status, books): the books searched
// (TLibrarian::Find) with a copy of the word.
Ref
CuFind(RefArg rcvr, RefArg word, RefArg owner, RefArg results, RefArg arg4, RefArg status, RefArg books)
{
	RefVar copy(Clone(word));
	LockRef(copy);
	UniChar* chars = GetCString(copy);
	RefVar result(TLibrarian::gLibrarian->Find(chars, owner, results, arg4, status, books));
	UnlockRef(copy);
	return result;
}


// ROM 0x0010a184 FindContentBySlot
// reader:FindContentBySlot(slot, book)
Ref
FindContentBySlot(RefArg rcvr, RefArg slot, RefArg book)
{
	return TLibrarian::gLibrarian->FindContentBySlot(rcvr, slot, book);
}


// ROM 0x0010ae38 FindPageByValue
// reader:FindPageByValue(slot, value, book)
Ref
FindPageByValue(RefArg rcvr, RefArg slot, RefArg value, RefArg book)
{
	return TLibrarian::gLibrarian->FindPageByValue(rcvr, slot, value, book);
}


// ROM 0x0010ad84 FindPageBySubject
// reader:FindPageBySubject(n) - the page the rendering's first contents
// list gives for the n-th subject (from 1).
Ref
FindPageBySubject(RefArg rcvr, RefArg subject)
{
	RefVar rendering(TLibrarian::gLibrarian->Rendering(rcvr));
	RefVar contents(GetFrameSlotRef(rendering, RSSYMcontents));
	RefVar pages(GetArraySlotRef(contents, 0));
	return GetArraySlotRef(pages, RINT(subject) - 1);
}


// ROM 0x00164b88 TurnToContent
// reader:TurnToContent(slot, value) - the reader turned to the first page
// with a content item whose slot holds the value.  ==> nil when there is
// none.
Ref
TurnToContent(RefArg rcvr, RefArg slot, RefArg value)
{
	RefVar pages(TLibrarian::gLibrarian->FindPageByValue(rcvr, slot, value, RefVar(TRUEREF)));
	if (Length(pages) == 0)
		return NILREF;
	PageTurnToSpread(rcvr, RINT(GetArraySlotRef(pages, 0)));
	return TRUEREF;
}


// ROM 0x0010a78c AddInkMarks
// reader:AddInkMarks(page, marks) - the ink marked on a page of the
// rendering replaced (taken out when there is none), or added.
Ref
AddInkMarks(RefArg rcvr, RefArg page, RefArg marks)
{
	RefVar isbn;
	RefVar entry;
	RefVar list;
	isbn = GetVariable(rcvr, RSSYMisbn, nil, 0);
	Long rendering = RINT(GetVariable(rcvr, RSSYMcurrendering, nil, 0));
	entry = TLibrarian::gLibrarian->GetLibraryEntry(isbn);
	list = GetFrameSlotRef(entry, RSSYMinkmarks);
	list = GetArraySlotRef(list, rendering);
	long count = Length(list);
	if (count != 0)
	{
		Long pageNo = RINT(page);
		for (long i = 0; i < count; i += 2)
			if (RINT(GetArraySlotRef(list, i)) == pageNo)
			{
				if (Length(marks) == 0)
					ArrayRemoveCount(list, i, 2);
				else
					SetArraySlotRef(list, i + 1, marks);
				EntryChange(entry);
				return TRUEREF;
			}
	}
	if (Length(marks) != 0)
	{
		AddArraySlot(list, page);
		AddArraySlot(list, marks);
		EntryChange(entry);
	}
	return TRUEREF;
}


void
RegisterSearchNatives(void)
{
	RegisterNativeFunction("CuFind", (void*) CuFind, 6);
	RegisterNativeFunction("FindContentBySlot", (void*) FindContentBySlot, 2);
	RegisterNativeFunction("FindPageByValue", (void*) FindPageByValue, 3);
	RegisterNativeFunction("FindPageBySubject", (void*) FindPageBySubject, 1);
	RegisterNativeFunction("TurnToContent", (void*) TurnToContent, 2);
	RegisterNativeFunction("AddInkMarks", (void*) AddInkMarks, 2);
}
