// The readings a word unit came to (recognition/WordList.h).
//
// What is checked is the packing - every word in one handle, separated
// by 0xFFFF and terminated by NUL, which is what Wstrlen and Wstrcpy
// read and what ScanTo walks - the scores and labels that come with
// them, the sixteen-reading ceiling, the lookup, the try string and
// the reordering of single-character guesses it drives, and the pool
// the lists are made out of.

#include "WordList.h"
#include "Unicode.h"
#include "NewtonMemory.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// a Unicode literal out of an eight-bit one, for the test's own use
static UniChar*
U(const char* str)
{
	static UniChar	buffers[4][32];
	static long		next = 0;
	UniChar* out = buffers[next++ & 3];
	long i = 0;
	for (; str[i] != 0 && i < 31; i++)
		out[i] = (UniChar) (UByte) str[i];
	out[i] = 0;
	return out;
}


// the first n characters of a packed word against a literal
static Boolean
SameFor(const UniChar* packed, const char* text, long n)
{
	for (long i = 0; i < n; i++)
		if (packed[i] != (UniChar) (UByte) text[i])
			return false;
	return true;
}

static void
Insert(TWordList* list, const char* word, long score, long label)
{
	UniChar* text = U(word);
	list->InsertLast(&text, score, label);
}


static void
TestEmpty(void)
{
	TWordList* list = new TWordList;
	EXPECT(list != nil);
	EXPECT(list->Count() == 0);
	EXPECT(list->fWords != nil);
	// nothing in it but the terminator
	EXPECT(*(UniChar*) *list->fWords == 0);
	EXPECT(GetHandleSize(list->fWords) == (Size) sizeof(UniChar));
	// and nothing to find
	UniChar* wanted = U("a");
	EXPECT(list->Find(&wanted) == -1);
	delete list;
}


static void
TestPacking(void)
{
	TWordList* list = new TWordList;
	Insert(list, "hello", 90, 1);
	EXPECT(list->Count() == 1);
	Insert(list, "hell", 60, 2);
	Insert(list, "he", 30, 3);
	EXPECT(list->Count() == 3);

	// one handle, the words separated by 0xFFFF and the last one
	// NUL-terminated
	const UniChar* packed = (const UniChar*) *list->fWords;
	EXPECT(SameFor(packed, "hello", 5));
	EXPECT(packed[5] == 0xffff);
	EXPECT(SameFor(packed + 6, "hell", 4));
	EXPECT(packed[10] == 0xffff);
	EXPECT(SameFor(packed + 11, "he", 2));
	EXPECT(packed[13] == 0);

	// a word ends at the separator as well as at NUL
	EXPECT(Wstrlen(packed) == 5);
	EXPECT(Wstrlen(packed + 6) == 4);
	EXPECT(Wstrlen(packed + 11) == 2);
	EXPECT(Ustrlen(packed) == 13);			// ... but a plain string does not
	UniChar copy[8];
	EXPECT(Wstrcpy(copy, packed + 6) == copy + 4);
	EXPECT(Ustrcmp(copy, U("hell")) == 0);

	// which is what ScanTo walks
	EXPECT(list->ScanTo(0) == packed);
	EXPECT(list->ScanTo(1) == packed + 6);
	EXPECT(list->ScanTo(2) == packed + 11);
	EXPECT(list->ScanTo(3) == nil);			// one past the end is safe

	// the words come out as handles of their own
	Handle word = list->Word(1);
	EXPECT(word != nil && Ustrcmp((UniChar*) *word, U("hell")) == 0);
	DisposHandle(word);

	// with what came with them
	EXPECT(list->Score(0) == 90 && list->Label(0) == 1);
	EXPECT(list->Score(2) == 30 && list->Label(2) == 3);
	long score = 0;
	long label = 0;
	word = list->Ith(2, &score, &label);
	EXPECT(Ustrcmp((UniChar*) *word, U("he")) == 0);
	EXPECT(score == 30 && label == 3);
	DisposHandle(word);
	// and neither has to be asked for
	word = list->Ith(0, nil, nil);
	EXPECT(Ustrcmp((UniChar*) *word, U("hello")) == 0);
	DisposHandle(word);

	// looking one up
	UniChar* wanted = U("hell");
	EXPECT(list->Find(&wanted) == 1);
	wanted = U("he");
	EXPECT(list->Find(&wanted) == 2);
	wanted = U("goodbye");
	EXPECT(list->Find(&wanted) == -1);
	// (BUG, kept: a word is compared only as far as the reading goes,
	//  so a longer one finds the reading it starts with)
	wanted = U("hellos");
	EXPECT(list->Find(&wanted) == 0);

	delete list;
}


static void
TestFull(void)
{
	// sixteen readings is all there is room for
	TWordList* list = new TWordList;
	char word[4];
	for (long i = 0; i < 20; i++)
	{
		word[0] = (char) ('a' + i);
		word[1] = 0;
		Insert(list, word, 100 - i, i);
	}
	EXPECT(list->Count() == kMaxWordListEntries);
	EXPECT(list->Score(15) == 100 - 15 && list->Label(15) == 15);
	Handle last = list->Word(15);
	EXPECT(*(UniChar*) *last == 'a' + 15);
	DisposHandle(last);
	delete list;
}


// the class the bubbling asks about: a circle, which is what tells '0'
// from 'O' and from '1'
static UChar
IsRound(UniChar c)
{
	return (UChar) (c == '0' || c == 'O' || c == 'o');
}


static void
TestBubble(void)
{
	// "1" is not round, "O" and "o" are: bubbling '0' towards the back
	// takes it past both of them and stops at the "1"
	TWordList* list = new TWordList;
	Insert(list, "0", 90, 0);
	Insert(list, "O", 80, 1);
	Insert(list, "o", 70, 2);
	Insert(list, "1", 60, 3);
	list->BubbleGuess('0', IsRound, 0);
	EXPECT(*list->ScanTo(0) == 'O');
	EXPECT(*list->ScanTo(1) == 'o');
	EXPECT(*list->ScanTo(2) == '0');
	EXPECT(*list->ScanTo(3) == '1');
	// only the characters move: the scores stay where they were, which
	// is what makes this a reordering of the guesses rather than of the
	// readings
	EXPECT(list->Score(0) == 90 && list->Score(2) == 70);

	// and back to the front again
	list->BubbleGuess('0', IsRound, 1);
	EXPECT(*list->ScanTo(0) == '0');
	EXPECT(*list->ScanTo(1) == 'O');
	EXPECT(*list->ScanTo(2) == 'o');

	// a guess that is not there does nothing
	list->BubbleGuess('z', IsRound, 0);
	EXPECT(*list->ScanTo(0) == '0');
	delete list;
}


static void
TestTryString(void)
{
	// the characters the writer last chose by hand
	ClearTryString();
	EXPECT(TryStringLength() == 0);
	EXPECT(InTryString('a') == 0);
	AddTryString('a');
	EXPECT(TryStringLength() == 1 && InTryString('a') != 0);
	AddTryString('b');
	EXPECT(TryStringLength() == 2 && InTryString('b') != 0);
	// (BUG, kept: the index wraps after the write rather than before,
	//  so the third character lands on the terminator and stays there
	//  for ever - the ring is of two but the string is three long)
	AddTryString('c');
	EXPECT(TryStringLength() == 3);
	EXPECT(gTryString[0] == 'a' && gTryString[1] == 'b' && gTryString[2] == 'c');
	AddTryString('d');
	EXPECT(gTryString[0] == 'd' && gTryString[2] == 'c');
	AddTryString('e');
	EXPECT(gTryString[1] == 'e' && gTryString[2] == 'c');
	// choosing one that is already there says the writer has settled on
	// it, so the string starts again
	AddTryString('e');
	EXPECT(TryStringLength() == 1 && gTryString[0] == 'e');
	EXPECT(InTryString('c') == 0);
}


static void
TestReorder(void)
{
	// having last written a letter, the round and straight guesses go to
	// the back of the list
	TWordList* list = new TWordList;
	Insert(list, "0", 90, 0);
	Insert(list, "O", 80, 1);
	Insert(list, "1", 70, 2);
	Insert(list, "l", 60, 3);
	ClearTryString();
	AddTryString('e');
	list->Reorder();
	EXPECT(*list->ScanTo(0) == 'O');
	EXPECT(*list->ScanTo(1) == '0');
	EXPECT(*list->ScanTo(2) == 'l');
	EXPECT(*list->ScanTo(3) == '1');

	// and having last written a digit, they come forward again
	ClearTryString();
	AddTryString('7');
	list->Reorder();
	EXPECT(*list->ScanTo(0) == '0');
	EXPECT(*list->ScanTo(1) == 'O');
	EXPECT(*list->ScanTo(2) == '1');
	EXPECT(*list->ScanTo(3) == 'l');

	// a first try character that is neither leaves the list alone
	ClearTryString();
	AddTryString('!');
	list->Reorder();
	EXPECT(*list->ScanTo(0) == '0' && *list->ScanTo(2) == '1');
	delete list;

	// the classes themselves
	EXPECT(IsCircular('0') && IsCircular('O') && IsCircular('o') && !IsCircular('1'));
	EXPECT(IsLinear('1') && IsLinear('I') && IsLinear('l') && IsLinear('i') && IsLinear('|'));
	EXPECT(!IsLinear('0') && IsSlash('/') && !IsSlash('7'));
}


static void
TestPool(void)
{
	// the first twelve lists come out of the pool, in order, and a slot
	// is free again as soon as its list goes
	TWordList* lists[kPreallocatedWordLists + 2];
	for (long i = 0; i < kPreallocatedWordLists + 2; i++)
		lists[i] = new TWordList;
	for (long i = 1; i < kPreallocatedWordLists; i++)
		EXPECT((char*) lists[i] - (char*) lists[i - 1] == (long) sizeof(TWordList));
	// the two past the pool came off the heap
	EXPECT(lists[kPreallocatedWordLists] < lists[0]
		|| lists[kPreallocatedWordLists] >= lists[0] + kPreallocatedWordLists);

	TWordList* third = lists[2];
	delete lists[2];
	TWordList* again = new TWordList;
	EXPECT(again == third);						// the slot came back
	delete again;
	for (long i = 0; i < kPreallocatedWordLists + 2; i++)
		if (i != 2)
			delete lists[i];
}


int
main()
{
	InitHostStandaloneHeap();

	TestEmpty();
	TestPacking();
	TestFull();
	TestBubble();
	TestTryString();
	TestReorder();
	TestPool();

	if (failures == 0)
		printf("test_WordList: all passed\n");
	else
		printf("test_WordList: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
