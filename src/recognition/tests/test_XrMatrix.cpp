// The cursive reader's xr matching matrix (recognition/XrMatrix.h): the
// inner loop's arithmetic on a prototype made by hand, and letters of
// the ROM's own letter table (the DTE) counted against xrs made to be
// the best each of a letter's prototypes can read - the letter they were
// made from must read them best.
#include "XrMatrix.h"
#include "XrReader.h"
#include "XrPost.h"
#include "WordSegment.h"
#include "LowLevel.h"
#include "CursiveReader.h"
#include "XrDomains.h"
#include "ROMImport.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtErrors.h"
#include "memory/host/KernelHeap.h"
#include "toolbox/ByteOrder.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(c) do { if (!(c)) { failures++; fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)


static xrd_el_type		gXrs[kXrMaxElements];
static xrdata_type		gXr;
static rc_type			gRC;

static void
StartXrs(void)
{
	memset(gXrs, 0, sizeof(gXrs));
	gXr.fLength = 0;
	gXr.fSize = kXrMaxElements;
	gXr.fElements = gXrs;
}

static void
AddXr(UByte type, UByte height, UByte shift, UByte orient, UByte link, UByte penalty, UByte attrib)
{
	xrd_el_type* e = &gXrs[gXr.fLength++];
	e->type = type;
	e->height = height;
	e->shift = shift;
	e->orient = orient;
	e->link = link;
	e->penalty = penalty;
	e->attrib = attrib;
}


// A prototype's nibble tables set a nibble at a time.
static void
SetNibble(UByte* table, long index, long value)
{
	UByte* b = &table[index >> 1];
	if ((index & 1) == 0)
		*b = (UByte) ((*b & 0x0f) | (value << 4));
	else
		*b = (UByte) ((*b & 0xf0) | value);
}

static long
GetNibble(const UByte* table, long index)
{
	UByte b = table[index >> 1];
	return (index & 1) == 0 ? b >> 4 : b & 0xf;
}


/*--------------------------------------------------------------------
	The inner loop, on a prototype made by hand.
--------------------------------------------------------------------*/

static void
TestInnerLoop(void)
{
	StartXrs();
	AddXr(1, 0, 0, 0, 0, 5, 0x80);		// a break
	AddXr(0x10, 3, 1, 2, 4, 6, 0);
	AddXr(0x11, 3, 1, 2, 4, 7, 0);
	AddXr(0x10, 5, 1, 2, 4, 8, 0);
	AddXr(1, 0, 0, 0, 0, 9, 0x80);
	memset(&gRC, 0, sizeof(gRC));
	xrcm_type x;
	memset(&x, 0, sizeof(x));
	xrinp_type in[8];
	short inp[8] = { 100, 90, 80, 70, 60, 0, 0, 0 };
	short out[8];
	memset(in, 0, sizeof(in));
	for (long i = 0; i < gXr.fLength; i++)
		memcpy(&in[i], &gXrs[i], sizeof(xrinp_type));
	UByte xrp[kXrpSize];
	memset(xrp, 0, sizeof(xrp));
	xrp[3] = 12;						// what skipping the prototype costs
	SetNibble(xrp + 0x04, 0x10, 9);		// a type 0x10 is worth 9
	SetNibble(xrp + 0x24, 3, 2);		// height 3: 2
	SetNibble(xrp + 0x2c, 1, 1);		// shift 1: 1
	SetNibble(xrp + 0x3c, 2, 3);		// orient 2: 3
	SetNibble(xrp + 0x34, 4, 1);		// link 4: 1
	x.inp = inp;
	x.out = out;
	x.xrinp = in;
	x.xrp = xrp;
	x.st = 0;
	x.end = 5;
	CountXrAsm(&x);
	// position 0: skip the xr 0-5=-5, skip the prototype 100-12=88,
	// diagonal 0-50 (no type 1 in the table: no match) = -50 -> 88
	EXPECT(out[0] == 88);
	// position 1: 88-6=82; 90-12=78; 100-50+(9+2+1+3+1)=66 -> 82
	EXPECT(out[1] == 82);
	// position 2: 82-7=75; 80-12=68; type 0x11 no match: 90-50=40 -> 75
	EXPECT(out[2] == 75);
	// position 3: 75-8=67; 70-12=58; 80-50+9+0+1+3+1=44 -> 67
	EXPECT(out[3] == 67);
	EXPECT(out[5] == 0);				// nought after the last

	// a prototype that only reads xrs next to a break does not read one
	// that is not
	xrp[2] = 0x80;
	x.st = 1;
	x.end = 2;
	short inp2[8] = { 200, 100, 0, 0, 0, 0, 0, 0 };
	x.inp = inp2;
	CountXrAsm(&x);
	// position 1: prevOut 0 - 6; 100-12=88; diagonal with no previous
	// input (the loop starts at nought): -50 -> 88
	EXPECT(out[1] == 88);

	// the traced loop: the same numbers, and which way each was reached
	xrp[2] = 0;
	UByte txr[4 + 8];
	memset(txr, 0xee, sizeof(txr));
	x.txr = txr;
	x.inp = inp;
	x.st = 0;
	x.end = 5;
	TCountXrAsm(&x);
	EXPECT(out[0] == 88 && out[1] == 82 && out[2] == 75 && out[3] == 67);
	EXPECT(txr[4] == 2 && txr[5] == 1 && txr[6] == 1 && txr[7] == 1);
	// a tie between the diagonal and a skip: CountXrAsm takes the
	// diagonal, TCountXrAsm the skip
	// (the loop starts with nought as the input before its first
	// position, so the diagonal there is always -50 plus the match)
	short inp3[8] = { 100, 100, 0, 0, 0, 0, 0, 0 };
	x.inp = inp3;
	x.st = 0;
	x.end = 2;
	xrp[3] = 34;						// skipping the prototype at 1: 100-34 = 66, the diagonal's 100-50+16
	TCountXrAsm(&x);
	EXPECT(out[1] == 66 && txr[5] == 2);
	xrp[3] = 35;						// one worse: the diagonal wins
	TCountXrAsm(&x);
	EXPECT(out[1] == 66 && txr[5] == 3);
}


/*--------------------------------------------------------------------
	Letters of the ROM's letter table.
--------------------------------------------------------------------*/

static DTIHeader*	gDTI;

static UByte*
Descriptor(UByte sym)
{
	ULong offset = GetBigEndianWord(gDTI->fDTEMain + sym * 4);
	return offset != 0 ? gDTI->fDTEMain + offset : nil;
}

// The xr each prototype of a variant reads best: the type, height,
// shift, direction and link it gives most for.
static long
IdealXrs(UByte sym, long var)
{
	UByte* d = Descriptor(sym);
	if (d == nil || var >= d[0])
		return 0;
	UByte* xrp = d + 0x54;
	for (long v = 0; v < var; v++)
		xrp += d[4 + v] * kXrpSize;
	long len = d[4 + var];
	for (long k = 0; k < len; k++, xrp += kXrpSize)
	{
		long best[5] = { 0, 0, 0, 0, 0 };
		static const long offsets[5] = { 0x04, 0x24, 0x2c, 0x3c, 0x34 };
		static const long counts[5] = { 64, 16, 16, 32, 16 };
		for (long t = 0; t < 5; t++)
			for (long i = 1; i < counts[t]; i++)
				if (GetNibble(xrp + offsets[t], i) > GetNibble(xrp + offsets[t], best[t]))
					best[t] = i;
		AddXr((UByte) best[0], (UByte) best[1], (UByte) best[2], (UByte) best[3], (UByte) best[4], 8, 0x80);
	}
	return len;
}

// How well a word reads the xrs from the first to the last: the value
// the matrix's out line has at the last xr.
static long
ReadsAs(const char* word, long* score)
{
	xrcm_type* x = nil;
	if (xrmatr_alloc(&gRC, &gXr, &x) != 0)
		return -9999;
	SetInitialLine(1, x);
	long value = -9999;
	if (CountWord((const UByte*) word, 0x1f, 0, x) == 0)
	{
		value = (x->outSt <= x->nXrs - 1 && x->nXrs - 1 < x->outEnd) ? x->outLine[x->nXrs - 1] : -9999;
		if (score != nil)
			*score = x->bestScore;
	}
	xrmatr_dealloc(&x);
	return value;
}

static void
TestLetters(void)
{
	Handle dtiHandle = nil, trigrams = nil;
	EXPECT(ReadDteResource("avp.dte", 1, &dtiHandle, &trigrams, 0) == 0);
	if (dtiHandle == nil)
		return;
	gDTI = (DTIHeader*) HWRMemoryLockHandle(dtiHandle);
	EXPECT(dti_lock(gDTI) == 0);
	memset(&gRC, 0, sizeof(gRC));
	RCSetH(&gRC, 0x04, 0x0f);		// every letter set's variants
	RCSetH(&gRC, 0x1e, 0x1f);
	gRC.fDTI = gDTI;

	const char* letters = "abcdefghijklmnopqrstuvwxyz";
	long checked = 0, won = 0;
	for (const char* l = "oltesm"; *l != 0; l++)
	{
		StartXrs();
		if (IdealXrs(*l, 0) == 0)
			continue;
		char own[2] = { *l, 0 };
		long ownValue = ReadsAs(own, nil);
		long bestOther = -9999;
		char bestLetter = 0;
		for (const char* o = letters; *o != 0; o++)
		{
			if (*o == *l)
				continue;
			char w[2] = { *o, 0 };
			long v = ReadsAs(w, nil);
			if (v > bestOther)
			{
				bestOther = v;
				bestLetter = *o;
			}
		}
		fprintf(stderr, "  '%c' (%ld xrs): reads %ld; the best other '%c' %ld\n", *l, gXr.fLength, ownValue, bestLetter, bestOther);
		checked++;
		if (ownValue > bestOther)
			won++;
	}
	EXPECT(checked == 6);
	EXPECT(won == checked);

	// a word: "lo" written as the ideal l then the ideal o reads as lo
	// better than as ol
	StartXrs();
	IdealXrs('l', 0);
	IdealXrs('o', 0);
	long lo = ReadsAs("lo", nil);
	long ol = ReadsAs("ol", nil);
	fprintf(stderr, "  'l' then 'o': lo %ld, ol %ld\n", lo, ol);
	EXPECT(lo > ol);

	// traced: the layout says which xrs each letter read
	{
		xrcm_type* x = nil;
		EXPECT(xrmatr_alloc(&gRC, &gXr, &x) == 0);
		SetInitialLine(1, x);
		EXPECT(CountWord((const UByte*) "lo", 0x1f, 4, x) == 0);
		EXPECT(x->layout != nil);
		if (x->layout != nil)
		{
			XrLayoutLetter* l = x->layout->letters[0];
			XrLayoutLetter* o = x->layout->letters[1];
			fprintf(stderr, "  layout: '%c' var %d xrs %d..%d (%d steps), '%c' var %d xrs %d..%d (%d steps); weights %d %d\n",
					l->sym, l->var, l->start, l->end, l->count, o->sym, o->var, o->start, o->end, o->count,
					x->weights[0], x->weights[1]);
			EXPECT(l->sym == 'l' && o->sym == 'o');
			EXPECT(l->end < o->end && o->end == gXr.fLength - 1);
			EXPECT(o->start <= l->end + 1);
		}
		FreeLayout(x);
		xrmatr_dealloc(&x);
	}
}


/*--------------------------------------------------------------------
	The Viterbi: a word of ideal xrs read with the character set alone.
--------------------------------------------------------------------*/

static void
MarkLocation(void)
{
	gXrs[gXr.fLength - 1].attrib |= 1;
}

static void
TestXrlv(void)
{
	// a break, then the ideal xrs of l and of o, each letter's last xr a
	// location
	StartXrs();
	AddXr(1, 0, 0, 0, 0, 8, 0x81);
	IdealXrs('l', 0);
	MarkLocation();
	IdealXrs('o', 0);
	MarkLocation();
	static PS_point_type points[3] = { { 10, 10 }, { 20, 20 }, { 30, 10 } };
	RCSetH(&gRC, 0x00, 1);			// words
	RCSetH(&gRC, 0x02, 1);			// letters only
	RCSetH(&gRC, 0x08, 2);			// the character set, no dictionary
	RCSetH(&gRC, 0x10, 16);			// sixteen readings a location
	RCSetH(&gRC, 0x14, 0x190);		// a beam of 100
	RCSetH(&gRC, 0x16, 0);
	RCSetH(&gRC, 0x1a, 0x64);
	RCSetH(&gRC, 0x1e, 5);
	RCSetH(&gRC, 0x96, 3);
	gRC.fTrace = points;
	gRC.fAlphaCharset = "abcdefghijklmnopqrstuvwxyz";
	RWG_type rwg;
	rec_w_type readings[10];
	memset(readings, 0, sizeof(readings));
	EXPECT(xrw_algs(&gXr, readings, &rwg, &gRC) == 0);
	EXPECT(rwg.type == 1 && rwg.size >= 2 && rwg.rws != nil && rwg.ppd != nil);
	if (rwg.rws != nil)
	{
		// the answers, best first: symbols, 4 between answers
		char text[128];
		long t = 0;
		for (long i = 0; i < rwg.size && t < 120; i++)
		{
			RWS_type* e = &rwg.rws[i];
			if (e->type == 1)
				text[t++] = (char) e->sym;
			else if (e->type == 4)
				text[t++] = ' ';
		}
		text[t] = 0;
		fprintf(stderr, "  xrlv read l, o as: %s\n", text);
		long first = rwg.rws[0].type == 2 ? 1 : 0;
		EXPECT(rwg.rws[first].sym == 'l' && rwg.rws[first + 1].sym == 'o');
		// each letter's xrs: l from the first xr, o after l's last
		RWS_type* l = &rwg.rws[first];
		RWS_type* o = &rwg.rws[first + 1];
		fprintf(stderr, "  l: xrs %d+%d var %d weight %d; o: xrs %d+%d var %d weight %d\n",
				l->xrStart, l->xrLen, l->var, l->weight, o->xrStart, o->xrLen, o->var, o->weight);
		EXPECT(l->xrStart == 1 && o->xrStart == l->xrStart + l->xrLen);
		EXPECT(o->xrStart + o->xrLen == gXr.fLength);
		EXPECT(rwg.ppd[first].el[0][1] != 0);		// the xrs l was read from
	}
	FreeRWGMem(&rwg);

	// a field with one fixed answer compares against it alone
	strcpy((char*) RCByte(&gRC, 0xc0), "lo");
	EXPECT(xrw_algs(&gXr, readings, &rwg, &gRC) == 0);
	EXPECT(rwg.size == 2 && rwg.rws[0].sym == 'l' && rwg.rws[1].sym == 'o');
	if (rwg.rws != nil && rwg.ppd != nil)
		fprintf(stderr, "  fixed answer: l xrs %d+%d, o xrs %d+%d\n",
				rwg.rws[0].xrStart, rwg.rws[0].xrLen, rwg.rws[1].xrStart, rwg.rws[1].xrLen);
	FreeRWGMem(&rwg);
	*RCByte(&gRC, 0xc0) = 0;
}


// The post-processing's rule interpreter (XrPostCalc.cpp) on queues made
// by hand, then the answers of "lo" scored again by the ROM's own rules
// (XrPostEval.cpp): the controls set so that they are scored at all (the
// ROM leaves an answer alone that is under the field's first control, or
// ahead of the next by more than its second).
static void
TestPost(void)
{
	POST_PARAMS pp;
	memset(&pp, 0, sizeof(pp));
	pp.pdf = (PDFHeader*) gDTI->fPDFPtr;
	pp.rc = &gRC;
	pp.xr = &gXr;
	intptr_t stack[15];
	pp.stack = stack;
	pp.stackBytes = 0x3c;
	UByte err[2];
	// push 5, push 3, add
	static const UByte add[] = { 0x0b, 5, 0x0b, 3, 0x0c, 0, 0 };
	EXPECT(CalculateQueueResult(&pp, add, err) == 8 && err[1] == 0);
	// push -7 (four bytes), the table's function 0 (CalculateAbs)
	static const UByte abs7[] = { 0x09, 0xff, 0xff, 0xff, 0xf9, 0x0e, 0, 0 };
	EXPECT(CalculateQueueResult(&pp, abs7, err) == 7 && err[1] == 0);
	// the fuzzy comparisons answer 0..20: 10 less than 20 is 10 + 10/5
	static const UByte less[] = { 0x0b, 10, 0x0b, 20, 0x0c, 6, 0 };
	EXPECT(CalculateQueueResult(&pp, less, err) == 12);
	static const UByte equal[] = { 0x0b, 10, 0x0b, 10, 0x0c, 8, 0 };
	EXPECT(CalculateQueueResult(&pp, equal, err) == 20);
	// a division by nought answers 10000
	static const UByte div0[] = { 0x0b, 9, 0x0b, 0, 0x0c, 3, 0 };
	EXPECT(CalculateQueueResult(&pp, div0, err) == 10000);
	// min of 4 and 9 (function 30, two arguments)
	static const UByte min49[] = { 0x0b, 4, 0x0b, 9, 0x0e, 30, 0 };
	EXPECT(CalculateQueueResult(&pp, min49, err) == 4);
	// variable 3 set to 42 (57) and read back (58): 42 + 42
	static const UByte vars[] = { 0x0b, 3, 0x0b, 42, 0x0e, 57, 0x0b, 3, 0x0e, 58, 0x0c, 0, 0 };
	EXPECT(CalculateQueueResult(&pp, vars, err) == 84 && pp.vars[3] == 42);
	// an unknown bytecode and a full stack both end the queue with nought
	static const UByte bad[] = { 0x01, 0 };
	EXPECT(CalculateQueueResult(&pp, bad, err) == 0 && err[1] == 0xf);
	UByte full[32];
	for (long i = 0; i < 15; i++)
	{
		full[i * 2] = 0x0b;
		full[i * 2 + 1] = 1;
	}
	full[30] = 0;
	EXPECT(CalculateQueueResult(&pp, full, err) == 0 && err[1] == 0x10);

	// "lo" read, a point for each xr, and every answer scored
	StartXrs();
	AddXr(1, 0, 0, 0, 0, 8, 0x81);
	IdealXrs('l', 0);
	MarkLocation();
	IdealXrs('o', 0);
	MarkLocation();
	static PS_point_type points[kXrMaxElements];
	for (long i = 0; i < gXr.fLength; i++)
	{
		XrSetH(gXrs[i].begpoint, i);
		XrSetH(gXrs[i].endpoint, i);
		XrSetH(gXrs[i].box + kXrLeft, i * 10);
		XrSetH(gXrs[i].box + kXrRight, i * 10 + 5);
		XrSetH(gXrs[i].box + kXrTop, 10);
		XrSetH(gXrs[i].box + kXrBottom, 30);
		points[i].x = (short) (i * 10);
		points[i].y = (short) ((i & 1) ? 30 : 10);
	}
	RCSetH(&gRC, 0x96, (UShort) gXr.fLength);
	gRC.fTrace = points;
	RWG_type rwg;
	rec_w_type readings[10];
	memset(readings, 0, sizeof(readings));
	EXPECT(xrw_algs(&gXr, readings, &rwg, &gRC) == 0);
	XrSetH(RCByte(&gRC, 0x100), 0);				// any answer is sure enough to be scored
	XrSetH(RCByte(&gRC, 0x102), 0x7fff);		// however far ahead
	*RCByte(&gRC, 0xaf) = 1;
	if (rwg.rws != nil)
	{
		// the letters' rules run by hand first: l and o have rules of
		// their own, so queues are run
		long first = rwg.rws[0].type == 2 ? 1 : 0;
		memset(&pp, 0, sizeof(pp));
		pp.pdf = (PDFHeader*) gDTI->fPDFPtr;
		pp.rc = &gRC;
		pp.xr = &gXr;
		pp.rwg = &rwg;
		pp.rws = rwg.rws;
		pp.ppd = rwg.ppd;
		pp.stack = stack;
		pp.stackBytes = 0x3c;
		pp.flags = 0xf;
		pp.competeOn = 1;
		pp.nPoints = (short) gXr.fLength;
		short xy[2 * kXrMaxElements];
		pp.x = xy;
		pp.y = xy + pp.nPoints;
		trace_to_xy(pp.x, pp.y, pp.nPoints, points);
		long queues = 0;
		for (long i = first; i < first + 2; i++)
		{
			pp.cur = (short) i;
			long q = EvaluateCharQuality(&pp);
			fprintf(stderr, "  post: '%c' scores %ld over %d queues\n", rwg.rws[i].sym, q, pp.queues);
			queues += pp.queues;
		}
		EXPECT(queues > 0);
		// then the whole of it: the graph is left as type 4
		EvaluateAndSortAnswers(readings, &gRC, &gXr, &rwg);
		EXPECT(rwg.type == 4);
		fprintf(stderr, "  post: first answer %s weight %d; l f0b %d, o f0b %d\n", readings[0].fWord, readings[0].fWeight,
				(SByte) rwg.rws[first].f0b, (SByte) rwg.rws[first + 1].f0b);
	}
	FreeRWGMem(&rwg);
}


// The prototype data's rules (XrRules.cpp): the bit-set arithmetic by
// hand, then the ROM's own rules walked - every character the main header
// names has a header, every variant its character's header names has one,
// and the rules found are inside the table.
static void
TestRules(void)
{
	UByte bits[2] = { 0xa0, 0x81 };		// bits 0, 2, 8 and 15
	EXPECT(PDFReturnNumberOfBits(bits, 2) == 4);
	EXPECT(PDFReturnIndex(bits, 0) == 0);
	EXPECT(PDFReturnIndex(bits, 2) == 1);
	EXPECT(PDFReturnIndex(bits, 8) == 2);
	EXPECT(PDFReturnIndex(bits, 15) == 3);
	EXPECT(PDFReturnBitNumber(bits, 15) == 3);
	EXPECT(PDFReturnBitNumber(bits, 1) == -1);
	if (gDTI == nil || gDTI->fPDFPtr == nil)
	{
		EXPECT(gDTI != nil && gDTI->fPDFPtr != nil);
		return;
	}
	const UByte* main = ((PDFHeader*) gDTI->fPDFPtr)->fSection0;
	long chars = 0, vars = 0, own = 0, connected = 0;
	for (short c = 0; c < 256; c++)
	{
		const UByte* ch = PDFGetCharAddress(main, c);
		if (PDFReturnBitNumber(main + 0x10, c) == -1)
		{
			EXPECT(ch == nil);
			continue;
		}
		chars++;
		EXPECT(ch != nil);
		for (short v = 0; v < 32 && ch != nil; v++)
		{
			if (PDFReturnBitNumber(ch + 4, v) == -1)
				continue;
			vars++;
			const UByte* var = PDFGetVarAddress(ch, v);
			EXPECT(var != nil);
			const UByte* rule;
			if (PDFGetRule(main, c, v, -1, -1, &rule))
			{
				own++;
				EXPECT(rule > var);
			}
			for (short n = 0; n < 256 && var != nil; n++)
				if (PDFGetConnectionAddress(var, n) != nil)
					connected++;
		}
	}
	fprintf(stderr, "  rules: %ld characters, %ld variants (%ld with a rule of their own), %ld connections\n",
			chars, vars, own, connected);
	EXPECT(0 < chars && chars <= vars);
}


int
main()
{
	InitHostStandaloneHeap();
	TestInnerLoop();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_XrMatrix: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();
	TestLetters();
	TestRules();
	TestXrlv();
	TestPost();
	if (failures == 0)
		printf("test_XrMatrix: all passed\n");
	return failures == 0 ? 0 : 1;
}
