// The cursive reader's answers (recognition/XrAnswers.cpp): a word graph
// made by hand turned into readings - scored, sorted and cut - and the
// split information of a reading of two words traced back to its
// strokes; and the training data's entries (XrDomains.h's LHAddEntry).
#include "XrReader.h"
#include "LowLevel.h"
#include "CursiveReader.h"
#include "XrDomains.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(c) do { if (!(c)) { failures++; fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)


static RWS_type
Sym(UByte sym, UByte type, UByte weight = 0, UByte letWeight = 0, UByte attr = 0)
{
	RWS_type s;
	memset(&s, 0, sizeof(s));
	s.sym = sym;
	s.realSym = sym;
	s.type = type;
	s.weight = weight;
	s.letWeight = letWeight;
	s.attr = attr;
	s.xrLen = 1;
	return s;
}


// A graph of two answers, "Fo" and "to": the readings come out best first,
// the graph is put into the same order, and one too far below the best
// is dropped.
static void
TestReadings(void)
{
	RWS_type rws[] = {
		Sym(0, 2),
		Sym('F', 1, 55, 10), Sym('o', 1, 0, 10),
		Sym(0, 4),
		Sym('t', 1, 71, 20), Sym('o', 1, 0, 20),
		Sym(0, 3)
	};
	RWG_PPD_type ppd[7];
	memset(ppd, 0, sizeof(ppd));
	RWG_type rwg;
	memset(&rwg, 0, sizeof(rwg));
	rwg.type = 1;
	rwg.size = 7;
	rwg.rws = (RWS_type*) HWRMemoryAlloc(sizeof(rws));
	rwg.ppd = (RWG_PPD_type*) HWRMemoryAlloc(sizeof(ppd));
	memcpy(rwg.rws, rws, sizeof(rws));
	memcpy(rwg.ppd, ppd, sizeof(ppd));
	rc_type rc;
	memset(&rc, 0, sizeof(rc));
	RCSetH(&rc, 0x1a, 100);
	RCSetH(&rc, 0x16, 0);
	xrdata_type xr = { 0, 0, nil };			// no xrs: no scale, the graph's scores stand
	rec_w_type readings[10];
	memset(readings, 0, sizeof(readings));
	MakeAndCombRecWordsFromWordGraph(&rwg, &rc, &xr, readings);
	EXPECT(strcmp((char*) readings[0].fWord, "to") == 0);
	EXPECT(readings[0].fWeight == 71);
	EXPECT(strcmp((char*) readings[1].fWord, "Fo") == 0);
	EXPECT(readings[1].fWeight == 55);
	EXPECT(readings[2].fWord[0] == 0);
	// the graph sorted the same way: "to" first
	EXPECT(rwg.rws[1].sym == 't');
	// one more than ten below the best is dropped
	memset(readings, 0, sizeof(readings));
	RCSetH(&rc, 0x1a, 10);
	MakeRecWordsFromWordGraph(&rwg, readings, 0);
	EXPECT(strcmp((char*) readings[0].fWord, "to") == 0);
	memset(readings, 0, sizeof(readings));
	MakeAndCombRecWordsFromWordGraph(&rwg, &rc, &xr, readings);
	EXPECT(strcmp((char*) readings[0].fWord, "to") == 0);
	EXPECT(readings[1].fWord[0] == 0);
	// with xrs the score is worked out again from the letters: two letters
	// adding 20 each over five xrs is 40 * 1000 / 40, a tenth of which is
	// 100 - and two adding 10 is half that
	xrd_el_type els[5];
	memset(els, 0, sizeof(els));
	xr.fLength = 5;
	xr.fSize = 5;
	xr.fElements = els;
	RCSetH(&rc, 0x1a, 100);
	memset(readings, 0, sizeof(readings));
	MakeAndCombRecWordsFromWordGraph(&rwg, &rc, &xr, readings);
	EXPECT(strcmp((char*) readings[0].fWord, "to") == 0);
	EXPECT(readings[0].fWeight == 100);
	EXPECT(strcmp((char*) readings[1].fWord, "Fo") == 0);
	EXPECT(readings[1].fWeight == 50);
	HWRMemoryFree((Ptr) rwg.rws);
	HWRMemoryFree((Ptr) rwg.ppd);
}


static void
SetXr(xrd_el_type* e, UByte type, short beg, short end)
{
	memset(e, 0, sizeof(*e));
	e->type = type;
	XrSetH(e->begpoint, beg);
	XrSetH(e->endpoint, end);
}


// "a b" written as two strokes: the split information says two words, a
// stroke each, where each ends, and each one's dictionary attribute.
static void
TestSplit(void)
{
	PS_point_type trace[] = {
		{ 0, -1 }, { 10, 10 }, { 12, 12 }, { 14, 10 },
		{ 0, -1 }, { 30, 10 }, { 32, 12 }, { 34, 10 },
		{ 0, -1 }, { 0, -1 }
	};
	rc_type rc;
	memset(&rc, 0, sizeof(rc));
	rc.fTrace = trace;
	RCSetH(&rc, 0x96, 9);
	xrd_el_type els[6];
	SetXr(&els[0], 1, 0, 0);
	SetXr(&els[1], 0x0b, 1, 3);
	SetXr(&els[2], 1, 4, 4);
	SetXr(&els[3], 0x0b, 5, 7);
	SetXr(&els[4], 1, 8, 8);
	memset(&els[5], 0, sizeof(els[5]));
	xrdata_type xr = { 5, 6, els };
	RWS_type rws[] = { Sym(0, 2), Sym('a', 1, 50, 0, 5), Sym(' ', 1), Sym('b', 1, 0, 0, 6), Sym(0, 3) };
	RWG_type rwg;
	memset(&rwg, 0, sizeof(rwg));
	rwg.type = 4;
	rwg.size = 5;
	rwg.rws = rws;
	rec_w_type readings[10];
	memset(readings, 0, sizeof(readings));
	strcpy((char*) readings[0].fWord, "a b");
	readings[0].fX30[0] = 1;
	readings[0].fX30[1] = 1;
	readings[0].fX30[2] = 2;
	// one word: just that
	rec_w_type one[10];
	memset(one, 0, sizeof(one));
	strcpy((char*) one[0].fWord, "ab");
	UByte* split = FillRecwordSplitInfo(&xr, &rc, &rwg, one, nil);
	EXPECT(split != nil && split[0xf] == 1);
	if (split != nil)
		HWRMemoryFree((Ptr) split);
	// two words
	split = FillRecwordSplitInfo(&xr, &rc, &rwg, readings, nil);
	EXPECT(split != nil);
	if (split != nil)
	{
		EXPECT(split[0xf] == 2);
		EXPECT(split[0] == 0x05);			// the words end at letters 0 and 2
		EXPECT(split[0x4c] == 1 && split[0x4d] == 1);
		EXPECT(split[0x58] == 1 && split[0x59] == 2);
		EXPECT(split[0x10] == 5 && split[0x11] == 6);
		EXPECT((els[1].attrib & 4) != 0 && (els[3].attrib & 4) != 0);
		HWRMemoryFree((Ptr) split);
	}
	// a stroke's number and extent
	EXPECT(GetStrokeNumber(2, &rc) == 1);
	EXPECT(GetStrokeNumber(6, &rc) == 2);
	EXPECT(GetStrokeNumber(4, &rc) == 0);
	long beg, end;
	GetBegEndOfStroke(2, &rc, &beg, &end);
	EXPECT(beg == 5 && end == 7);
	// a letter's stretches: a crossing mark is one of its own
	short count = 0;
	Part_of_letter* parts = (Part_of_letter*) HWRMemoryAlloc(0x60 * 4);
	SetXr(&els[2], 0x34, 2, 3);
	EXPECT(connect_trajectory_and_letter(els, 1, 3, &count, parts) == 0);
	EXPECT(count == 2);
	HWRMemoryFree((Ptr) parts);
}


// Training data: entries added one after another and found again; a
// second one with the same ids is refused.
static void
TestTrainingData(void)
{
	Handle h = nil;
	long a = 0x11223344;
	char b[] = "hello";
	EXPECT(LHAddEntry(&h, 'AAAA', '0001', 0, &a, sizeof(a)) == 0);
	EXPECT(LHAddEntry(&h, 'BBBB', '0001', 0, b, sizeof(b)) == 0);
	EXPECT(LHAddEntry(&h, 'AAAA', '0001', 0, &a, sizeof(a)) == -4);
	EXPECT(LHAddEntry(&h, 'CCCC', '0001', 0, nil, 4) == -2);
	void* data = LHLock(h);
	void* entry;
	ULong size;
	EXPECT(LHFindEntry(data, 'AAAA', '0001', 0, &entry, &size) == 0 && size == sizeof(a) && *(long*) entry == a);
	EXPECT(LHFindEntry(data, 'BBBB', '****', 0, &entry, &size) == 0 && size == sizeof(b) && strcmp((char*) entry, "hello") == 0);
	EXPECT(LHFindEntry(data, 'CCCC', '0001', 0, &entry, &size) == -3);
	LHUnLock(h);
	HWRMemoryFreeHandle(h);
}


// A graph of alternatives: "c", then o or a, then "t".  The group is put
// best first in the graph, the first reading takes the best of each ("cot"
// 240 / 3 = 80), the next changes the letter that loses least ("cat" 230 /
// 3 = 76), and there are no more.
static void
TestGraphOfAlternatives(void)
{
	RWS_type rws[8] = {
		Sym('c', 1, 80), Sym(0, 2), Sym('a', 1, 60), Sym(0, 4), Sym('o', 1, 70), Sym(0, 3), Sym('t', 1, 90), Sym(0, 0),
	};
	rec_w_type readings[10];
	UByte paths[10][24];
	memset(readings, 0, sizeof(readings));
	MakeRecWordsFromGraph(rws, 7, readings, &paths[0][0], nil);
	EXPECT(rws[2].sym == 'o' && rws[4].sym == 'a');		// sorted in the graph
	EXPECT(strcmp((const char*) readings[0].fWord, "cot") == 0 && readings[0].fWeight == 80);
	EXPECT(strcmp((const char*) readings[1].fWord, "cat") == 0 && readings[1].fWeight == 76);
	EXPECT(readings[2].fWord[0] == 0);
	EXPECT(paths[0][0] == 0 && paths[0][1] == 2 && paths[0][2] == 6);
	EXPECT(paths[1][1] == 4);

	// an alternative of two symbols ("rn" for an m): both given the mean of
	// their scores, and the pair moved as one
	RWS_type two[7] = {
		Sym(0, 2), Sym('m', 1, 50), Sym(0, 4), Sym('r', 1, 60), Sym('n', 1, 80), Sym(0, 3), Sym(0, 0),
	};
	memset(readings, 0, sizeof(readings));
	MakeRecWordsFromGraph(two, 6, readings, &paths[0][0], nil);
	EXPECT(two[1].sym == 'r' && two[2].sym == 'n' && two[3].type == 4 && two[4].sym == 'm');
	EXPECT(two[1].weight == 70 && two[2].weight == 70);
	EXPECT(strcmp((const char*) readings[0].fWord, "rn") == 0 && readings[0].fWeight == 70);
	EXPECT(strcmp((const char*) readings[1].fWord, "m") == 0 && readings[1].fWeight == 50);
	EXPECT(readings[2].fWord[0] == 0);

	// a letter read in another case has its variant's top bit set; one
	// read as another letter clears the reading's first variant and span
	// (ROM BUG: not its own)
	RWS_type sym[2] = { Sym('A', 1, 50), Sym('x', 1, 50) };
	sym[0].realSym = 'a';
	sym[0].var = 3;
	sym[1].realSym = 'y';
	memset(readings, 0, sizeof(readings));
	readings[0].fVariants[0] = 9;
	readings[0].fVariants[1] = 9;
	FillRecWordsElement(readings, sym, 0, 1, 0);
	EXPECT(readings[0].fWord[1] == 'A' && readings[0].fVariants[1] == 0x83);
	FillRecWordsElement(readings, sym, 0, 2, 1);
	EXPECT(readings[0].fWord[2] == 'x' && readings[0].fVariants[0] == 0 && readings[0].fVariants[2] == 0);
}


// Two sets of readings merged, best first, the same word only once.
static void
TestMergeReadings(void)
{
	rec_w_type a[10], b[10];
	memset(a, 0, sizeof(a));
	memset(b, 0, sizeof(b));
	strcpy((char*) a[0].fWord, "cot");	a[0].fWeight = 80;
	strcpy((char*) a[1].fWord, "cat");	a[1].fWeight = 76;
	strcpy((char*) b[0].fWord, "cat");	b[0].fWeight = 90;
	strcpy((char*) b[1].fWord, "c0t");	b[1].fWeight = 50;
	MergeTwoRecWordsSets(a, b);
	EXPECT(strcmp((const char*) a[0].fWord, "cat") == 0 && a[0].fWeight == 90);
	EXPECT(strcmp((const char*) a[1].fWord, "cot") == 0 && a[1].fWeight == 80);
	EXPECT(strcmp((const char*) a[2].fWord, "c0t") == 0 && a[2].fWeight == 50);
	EXPECT(a[3].fWord[0] == 0);
	// a tie goes to the first set
	memset(a, 0, sizeof(a));
	memset(b, 0, sizeof(b));
	strcpy((char*) a[0].fWord, "one");	a[0].fWeight = 60;
	strcpy((char*) b[0].fWord, "two");	b[0].fWeight = 60;
	MergeTwoRecWordsSets(a, b);
	EXPECT(strcmp((const char*) a[0].fWord, "one") == 0 && strcmp((const char*) a[1].fWord, "two") == 0);
}


int
main()
{
	InitHostStandaloneHeap();
	TestReadings();
	TestSplit();
	TestTrainingData();
	TestGraphOfAlternatives();
	TestMergeReadings();
	if (failures == 0)
		printf("test_XrAnswers: all passed\n");
	return failures == 0 ? 0 : 1;
}
