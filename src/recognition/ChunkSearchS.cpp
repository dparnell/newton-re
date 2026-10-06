/*
	File:		ChunkSearchS.cpp

	Contains:	The digit reader's fourth searcher (SearchDigit_S): the
				one that reads the signs and the small things - the bar
				coded 13 that FindPound builds on, the full stop, comma,
				colon, solidus, brackets, the "@" and the per cent sign -
				and the digits whose strokes are arcs (a 0, 6, 8 and 9 from
				an arc down and one up, a 3, a 5 with its bar, a 7 with
				its bar, a + from a bar and the stroke through it).  It
				puts marks it is not yet sure of in the list of low
				objects as class 1600 (value 1600 + the code: 1600 a
				small ring, 1613 a bar, 1614 a dot) and works through
				them afterwards, and what it is sure of as class 1300,
				value 1600 + the digit or the sign's code (as
				ChunkSecondLook.cpp's DigitChar); a chunk it has used is
				marked in its f6C (10, or 13 when an arc was read with the
				one before it) so the other passes leave it alone.  See
				Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x00290ed8-0x00296e04: SearchDigit_S and its forty-five
				unnamed statics); each function cites its origin.  All
				of it is from the disassembly: the decompiler loses the
				stack arguments and most of the statics' results.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRAbs
#include "host/RomBugs.h"


// SearchDigit_S's own copy of the writing's line (on its stack): the
// staff's fHeight, fTopLine and fBottomLine, until S_DigitsLine works it
// out again from the digits found.  The fourth word is never set or read.
struct SLine
{
	int32_t		fHeight;			// +0
	int32_t		fTop;				// +4
	int32_t		fBottom;			// +8
	int32_t		f0C;
};

static long	S_StackedMarks(tag_CHUNK_STAFF* staff, SLine* line);
static long	S_RightParen(tag_CHUNK_STAFF* staff, long i, SLine* line);
static long	S_Bar(tag_CHUNK_STAFF* staff, long i, SLine* line);
static long	S_CrossesBar(tag_wapx_type* nodes, tag_CHUNK* chunks, tag_STK* a, tag_STK* b);
static long	S_FiveWithBar(tag_CHUNK_STAFF* staff, SLine* line, tag_STK* a, tag_STK* b);
static long	S_FiveBody(tag_wapx_type* nodes, tag_CHUNK* chunks, tag_STK* a, tag_STK* b);
static long	S_SevenWithBar(tag_CHUNK_STAFF* staff, SLine* line, tag_STK* a, tag_STK* b);
static void	S_ArcsPass(tag_CHUNK_STAFF* staff, long unused);
static long	S_SpanLeft(tag_CHUNK_STAFF* staff, long ri);
static long	S_SpanRight(tag_CHUNK_STAFF* staff, long ri);
static long	S_SpanNoCorner(tag_CHUNK_STAFF* staff, long ri);
static long	S_StackedPair(tag_STK* a, tag_STK* b, long loose, SLine* line, tag_STK** upper, tag_STK** lower);
static long	S_ZeroShape(tag_CHUNK_STAFF* staff, long ri);
static long	S_NineTail(tag_CHUNK_STAFF* staff, long ri);
static long	S_NineShape(tag_CHUNK_STAFF* staff, long ri);
static long	S_SixOrEight(tag_CHUNK_STAFF* staff, long ri, long* digit);
static long	S_SixOrEightBack(tag_CHUNK_STAFF* staff, long ri, long* digit);
static long	S_EvenPair(tag_CHUNK_STAFF* staff, long ri);
static long	S_PercentAround(tag_CHUNK_STAFF* staff, tag_STK* a, tag_STK* b, int32_t* from, int32_t* to);
static long	S_PercentSlash(tag_CHUNK_STAFF* staff, long s, tag_STK* a, tag_STK* b);
static long	S_PercentLast3(tag_CHUNK_STAFF* staff, long ri, int32_t* from, int32_t* to);
static long	S_RingsMatch(tag_CHUNK_STAFF* staff, long s1, long s2);
static long	S_DotsPass(tag_CHUNK_STAFF* staff, SLine* line);
static long	S_Percent(tag_CHUNK_STAFF* staff, long ri, int32_t* from, int32_t* to);
static void	S_MarkChunks(tag_wapx_type* nodes, tag_CHUNK* chunks, long from, long to, long mark);
static long	S_ArcDown(tag_CHUNK_STAFF* staff, long ri, int32_t* done);
static long	S_ArcAfterArc(tag_CHUNK_STAFF* staff, long ri, int32_t* done);
static long	S_StrokeThrough(tag_CHUNK_STAFF* staff, long ri, int32_t* to);
static long	S_LoopTurnsBack(tag_CHUNK_STAFF* staff, long ri, long rj);
static long	S_EndsTurnBack(tag_CHUNK* chunks, tag_wapx_type* nodes, tag_CHUNK* a, tag_CHUNK* b);
static long	S_NearX(tag_CHUNK* unused, tag_wapx_type* nodes, const int32_t* pair, long idx, long alt, long base, long height);
static long	S_Overlaps(long a0, long a1, long b0, long b1);
static void	S_DigitsLine(tag_CHUNK_STAFF* staff, SLine* line);
static long	S_BarsPass(tag_CHUNK_STAFF* staff, SLine* line);
static long	S_Three(tag_CHUNK_STAFF* staff, long ri);
static long	S_Nine(tag_CHUNK_STAFF* staff, long ri);
static void	S_PutStroke(tag_CHUNK_STAFF* staff, long ri, long digit, const void* unused);
static long	S_PutChunks(tag_CHUNK_STAFF* staff, long first, long last, long digit, const void* unused);
static long	S_At(tag_CHUNK_STAFF* staff, long ri);
static long	S_GreyEight(tag_CHUNK_STAFF* staff, long ri);
static long	S_RingsPass(tag_CHUNK_STAFF* staff, SLine* line);
static long	S_PerChunk(tag_CHUNK_STAFF* staff, long i, SLine* line);
static long	S_SmallMarks(tag_CHUNK_STAFF* staff, long i, SLine* line);
static long	S_Slash(tag_CHUNK_STAFF* staff, long s, tag_CHUNK* c, SLine* line);
static long	S_LeftParen(tag_CHUNK_STAFF* staff, long i, SLine* line);


// the chunk a polyline node belongs to (its f18, one-based, negative at a
// chunk's last node when the next continues from it), as the ROM works it
// out in two places: the magnitude less one, nought left as nought
static inline long
NodeChunk(tag_wapx_type* nodes, long k)
{
	long c = HWRAbs(nodes[k].f18);
	if (c != 0)
		c = c - 1;
	return c;
}


#pragma mark - the top level

// ROM 0x00290ed8 SearchDigit_S__FP15tag_CHUNK_STAFF
long
SearchDigit_S(tag_CHUNK_STAFF* staff)
{
	SLine line;
	line.fHeight = staff->fHeight;
	line.fTop = staff->fTopLine;
	line.fBottom = staff->fBottomLine;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	long last = staff->fRealCount - 1;
	// every real chunk marked unused.  The ROM also works out the least left
	// and the greatest right of them, and loads the last one's twice more,
	// only to throw all of it away: S_ArcsPass is given half as much again
	// as the writing's height, and does not use that either.
	for (long i = 0; i <= last; i++)
		chunks[real[i]].f6C = 0;
	S_ArcsPass(staff, line.fHeight * 3 / 2);
	for (long i = 0; i <= last; i++)
		if (chunks[real[i]].f6C == 0)
			S_PerChunk(staff, i, &line);
	S_DigitsLine(staff, &line);
	for (long i = 0; i <= last; i++)
		if (chunks[real[i]].f6C == 0)
			S_SmallMarks(staff, i, &line);
	S_BarsPass(staff, &line);
	S_StackedMarks(staff, &line);
	S_RingsPass(staff, &line);
	S_DotsPass(staff, &line);
	return 0;		// (the ROM leaves the last pass's answer in r0; nothing reads it)
}


#pragma mark - small helpers

// ROM 0x00294450 (unnamed) - the chunks from the one polyline node from is
// in to the one node to is in marked (f6C) as used
static void
S_MarkChunks(tag_wapx_type* nodes, tag_CHUNK* chunks, long from, long to, long mark)
{
	// (no guard for a node in no chunk, as NodeChunk has: nought would mark
	// from chunk -1, but the nodes asked about are always a chunk's)
	long c1 = HWRAbs(nodes[from].f18) - 1;
	long c2 = HWRAbs(nodes[to].f18) - 1;
	for (long c = c1; c <= c2; c++)
		chunks[c].f6C = (int32_t) mark;
}


// ROM 0x00294f7c (unnamed) - whether two spans overlap: one inside the
// other, or either end of the first inside the second
static long
S_Overlaps(long a0, long a1, long b0, long b1)
{
	if (a0 >= b0 && a1 <= b1)
		return 1;
	if (b0 >= a0 && b1 <= a1)
		return 1;
	if (a1 >= b0 && a1 <= b1)
		return 1;
	if (a0 >= b0 && a0 <= b1)
		return 1;
	return 0;
}


// ROM 0x00294f24 (unnamed) - whether node idx (or alt, when idx is one of
// the pair's two nodes) is less than an eighth of height to the right of
// node base.  Its first argument is never read.
static long
S_NearX(tag_CHUNK* /*unused*/, tag_wapx_type* nodes, const int32_t* pair, long idx, long alt, long base, long height)
{
	long n = (pair[0] == idx || pair[1] == idx) ? alt : idx;
	return nodes[n].x - nodes[base].x < height / 8 ? 1 : 0;
}


// ROM 0x00295b38 (unnamed) - chunks first to last marked used and put in
// as class 1300, value 1600 + digit.  ROM QUIRK: the extra the object is
// given is the digit again; the fifth argument, which the callers load
// from a word beside their code holding the character ('@', '9', '3'), is
// never read.
static long
S_PutChunks(tag_CHUNK_STAFF* staff, long first, long last, long digit, const void* /*unused*/)
{
	tag_CHUNK* chunks = staff->fChunks;
	for (long c = first; c <= last; c++)
		chunks[c].f6C = 10;
	return LO_Add(staff->fLO, staff->fNodes, 1300, chunks[first].fFrom, chunks[last].fTo, 1600 + digit, digit);
}


// ROM 0x00295ae8 (unnamed) - the whole stroke real chunk ri is in put in
// as the digit
static void
S_PutStroke(tag_CHUNK_STAFF* staff, long ri, long digit, const void* unused)
{
	tag_STK* stk = &staff->fStrokes[staff->fChunks[staff->fRealChunks[ri]].fStroke];
	S_PutChunks(staff, stk->fFirstChunk, stk->fLastChunk, digit, unused);
}


#pragma mark - the arcs

// ROM 0x002926a0 (unnamed) - the arcs (class 400) that go down and are not
// used yet, each with what comes after it in its stroke: a 9 (an arc and
// a tail), a 6 or 8 (two arcs), a 0 or a per cent sign's ring; the pieces
// of a pair are read by S_ArcAfterArc and S_ArcDown.  Its second argument
// is never read.
static void
S_ArcsPass(tag_CHUNK_STAFF* staff, long /*unused*/)
{
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	void* lo = staff->fLO;
	tag_wapx_type* nodes = staff->fNodes;
	tag_STK* strokes = staff->fStrokes;
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	int32_t done = -1;			// the last chunk of the stroke last read (written, never read)
	LO_SetWorkClass(lo, 400);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		long ri = LO_GetRealChunkInd(lo, chunks, nodes, obj, 1);
		tag_CHUNK* c = &chunks[real[ri]];
		if (c->f6C != 0 || c->fKind != 2)
			continue;
		if (S_GreyEight(staff, ri))
			continue;
		long value = obj->fValue;
		if (c->fPrev != -1)
		{
			if (S_ArcAfterArc(staff, ri, &done))
				continue;
			tag_CHUNK* p = &chunks[real[ri - 1]];
			if (p->fPrev != -1 || p->fHeight > c->fHeight)
				continue;
			if (p->f78 != value && p->f74 != 300)
				continue;
		}
		if (c->fNext == -1)
			continue;
		if (value != 402)
		{
			S_ArcDown(staff, ri, &done);
			continue;
		}
		tag_CHUNK* n = &chunks[real[ri + 1]];
		// ROM QUIRK: the test is of the next chunk's class (f74) against 301,
		// a subclass, which no class is; only a subclass of 402 passes
		if (n->f78 != 402 && n->f74 != 301)
			continue;
		if (!S_SpanLeft(staff, ri))
			continue;
		tag_STK* stk = &strokes[c->fStroke];
		int32_t from = chunks[stk->fFirstChunk].fFrom;
		int32_t to = chunks[stk->fLastChunk].fTo;
		if (S_NineTail(staff, ri))
		{
			LO_Add(lo, nodes, 1300, from, to, 1609, 1);
			S_MarkChunks(nodes, chunks, from, to, 10);
		}
		else
		{
			if (chunks[real[ri + 1]].f78 != 402)
				continue;
			long digit;
			if (S_SixOrEight(staff, ri, &digit))
			{
				S_MarkChunks(nodes, chunks, from, to, 10);
				LO_Add(lo, nodes, 1300, from, to, 1600 + digit, 1);
			}
			else
			{
				if (!S_EvenPair(staff, ri))
					continue;
				if (S_Percent(staff, ri, &from, &to))
					LO_Add(lo, nodes, 1300, from, to, 1617, 1);
				else
					LO_Add(lo, nodes, 1300, from, to, 1600, 1);
				done = strokes[c->fStroke].fLastChunk;
				S_MarkChunks(nodes, chunks, from, to, 10);
				continue;
			}
		}
		done = strokes[c->fStroke].fLastChunk;
	}
	LO_SetWorkClass(lo, saved);
}


// How far apart two chunks' tops are, as S_SpanLeft and S_SpanRight ask:
// the ROM's nought (see S_SpanLeft), or with the ROM bug fixed the
// distance.
static long
S_TopsApart(const tag_CHUNK* c, const tag_CHUNK* n)
{
	if (RomBugFixed())
		return HWRAbs(c->fTop - n->fTop);
	return HWRAbs(0);
}


// ROM 0x00292ae8 (unnamed) - whether real chunk ri and the one after it
// span enough: the width across them at least twice how far the stroke
// reaches out either side of them (from the chunk before the first's
// right, or its start, to the chunk after the second's left, or its end).
// When both run from their tops, apart by more than three quarters of
// that width, the ends are looked at too - ROM BUG (fixed): it asks
// whether HWRAbs(0) is less than a quarter of the chunk's height, where
// some distance was meant, so only a chunk under four high skips the
// look.  The nought is a constant in the code (`mov r0,#0`), what a
// difference of a field with itself folds to; the fix takes it to be the
// two chunks' tops (both run from them) and asks that they be level
// within a quarter of the chunk's height (S_TopsApart).
static long
S_SpanLeft(tag_CHUNK_STAFF* staff, long ri)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	tag_CHUNK* n = &chunks[real[ri + 1]];
	long width = n->fRight - c->fLeft;
	long after = n->fX1;
	if (n->fNext != -1)
		after = chunks[n->fNext].fLeft;
	long before = c->fX0;
	if (c->fPrev != -1)
		before = chunks[c->fPrev].fRight;
	if (c->fTopNode == c->fFrom && n->fTo == n->fTopNode
		&& c->fX0 - n->fX1 > width * 3 / 4
		&& S_TopsApart(c, n) < c->fHeight / 4)
	{
		tag_wapx_type* s = &nodes[c->fFrom];
		long startDrop = s[1].y - s[0].y;
		tag_wapx_type* e = &nodes[n->fTo];
		long endRise = e[-1].y - e[0].y;
		long h6 = c->fHeight / 6;
		if (h6 < startDrop && h6 < endRise
			&& s[0].x - s[1].x < startDrop
			&& e[-1].x - nodes[c->fTo].x < endRise)
			return 0;
	}
	return width >= 2 * (after - before) ? 1 : 0;
}


// ROM 0x00292c60 (unnamed) - S_SpanLeft the other way round: the pair
// turning the other way, the first no more than twice the second's height.
// ROM BUG (fixed): the same HWRAbs(0) as S_SpanLeft's, and the same fix.
// ROM QUIRK: its last test
// is against the node before the first chunk's end, where S_SpanLeft's is
// against the end itself.
static long
S_SpanRight(tag_CHUNK_STAFF* staff, long ri)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	tag_CHUNK* n = &chunks[real[ri + 1]];
	if (c->fHeight > 2 * n->fHeight)
		return 0;
	long width = c->fRight - n->fLeft;
	long before = c->fX0;
	if (c->fPrev != -1)
		before = chunks[c->fPrev].fLeft;
	long after = n->fX1;
	if (n->fNext != -1)
		after = chunks[n->fNext].fRight;
	if (c->fFrom == c->fTopNode && n->fTo == n->fTopNode
		&& n->fX1 - c->fX0 > width * 3 / 4
		&& S_TopsApart(c, n) < c->fHeight / 4)
	{
		tag_wapx_type* s = &nodes[c->fFrom];
		long startDrop = s[1].y - s[0].y;
		tag_wapx_type* e = &nodes[n->fTo];
		long endRise = e[-1].y - e[0].y;
		long h6 = c->fHeight / 6;
		if (h6 < startDrop && h6 < endRise
			&& s[1].x - s[0].x < startDrop
			&& e[0].x - nodes[c->fTo - 1].x < endRise)
			return 0;
	}
	return width < 2 * (before - after) ? 0 : 1;
}


// ROM 0x00292df0 (unnamed) - whether real chunk ri and the next span at
// least a fifth of the first's height, its end is not a corner, and they
// are at least twice as wide as the stroke reaches out beyond them
static long
S_SpanNoCorner(tag_CHUNK_STAFF* staff, long ri)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	tag_CHUNK* n = &chunks[real[ri + 1]];
	long width = c->fRight - n->fLeft;
	if (c->fHeight / 5 > width)
		return 0;
	if ((nodes[c->fTo].fDir & 0xff00) != 0)
		return 0;
	long before = c->fX0;
	if (c->fPrev != -1)
		before = chunks[c->fPrev].fLeft;
	long after = n->fX1;
	if (n->fNext != -1)
		after = chunks[n->fNext].fRight;
	return width >= 2 * (before - after) ? 1 : 0;
}


// ROM 0x002930e4 (unnamed) - whether the arc down at real chunk ri and the
// one up after it close into a 0: the second much taller when they are
// close together, or where the first is a quarter up from the second's
// foot at least a quarter of their width to the right of the second
static long
S_ZeroShape(tag_CHUNK_STAFF* staff, long ri)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	tag_CHUNK* n = &chunks[real[ri + 1]];
	long width = c->fRight - n->fLeft;
	if (n->fX1 - c->fX0 < width / 4
		&& n->fHeight - c->fHeight < n->fHeight / 4 + n->fHeight / 8)
		return 1;
	if (n->fBottom - n->fHeight / 4 > c->fBottom)
		return 0;
	long xn = x_in_curve(nodes, n, n->fBottom - n->fHeight / 4);
	long xc = x_in_curve(nodes, c, n->fBottom - n->fHeight / 4);
	return xc - xn >= width / 4 ? 1 : 0;
}


// ROM 0x002931fc (unnamed) - whether the arc pair at real chunk ri has a 9's
// tail after it: a line or an arc down reaching below the loop, and at
// most one short stroke back left after that.  ROM QUIRK: the tail's foot
// must be below the loop's top by at least a third of the span and at
// most a sixth of it at once, which only a span under three (or one
// upside down) can be; the test almost never passes.
static long
S_NineTail(tag_CHUNK_STAFF* staff, long ri)
{
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	if (chunks[real[ri + 1]].fNext == -1)
		return 0;
	tag_CHUNK* tail = &chunks[real[ri + 2]];
	if (tail->f78 != 401 && tail->f74 != 300)
		return 0;
	long span = tail->fBottom - c->fTop;
	long below = tail->fBottom - c->fBottom;
	if (below < span / 3)
		return 0;
	if (below > span / 6)
		return 0;
	if (tail->fNext == -1)
		return 1;
	if (chunks[tail->fNext].fNext != -1)
		return 0;
	tag_CHUNK* hook = &chunks[real[ri + 3]];
	if (tail->fHeight < 2 * hook->fHeight || hook->fX0 < hook->fX1)
		return 0;
	if (hook->f74 == 300 || hook->f78 == 401)
		return 1;
	return 0;
}


// ROM 0x00293324 (unnamed) - whether the two arcs at real chunk ri are a
// 9 read backwards (the first the tall stroke): the first at least half as
// tall again as the second, and whatever follows short.  ROM QUIRK: the
// same impossible pair of bounds as S_NineTail's.
static long
S_NineShape(tag_CHUNK_STAFF* staff, long ri)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	tag_CHUNK* n = &chunks[real[ri + 1]];
	tag_CHUNK* nn = nil;
	long h = c->fHeight;
	if (h < n->fHeight * 3 / 2)
		return 0;
	if (n->fNext != -1)
	{
		nn = &chunks[n->fNext];
		if (nn->f78 != 401 && nn->f74 != 300)
			return 0;
		if (nn->fNext != -1)
			return 0;
		if (n->fHeight < nn->fHeight * 3 / 2)
			return 0;
	}
	long below = c->fBottom - n->fBottom;
	if (below < h / 3)
		return 0;
	if (below > h / 6)
		return 0;
	long end = n->fX1;
	if (nn != nil)
		end = nn->fRight;
	long x = x_in_curve(nodes, c, n->fBottom);
	long width = c->fRight - n->fLeft;
	if (2 * (x - end) - width > width / 8)
		return 0;
	return 1;
}


// ROM 0x00293458 (unnamed) - a 6 or an 8 from the arc down at real chunk ri
// and the smaller one up after it, with at most a short stroke on either
// side: *digit 6, or 8 when the second arc's top is its end or a corner
// close to its start.  ==> 1 found.
static long
S_SixOrEight(tag_CHUNK_STAFF* staff, long ri, long* digit)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	tag_CHUNK* n = &chunks[real[ri + 1]];
	tag_CHUNK* nn = nil;
	if (n->fNext != -1)
	{
		// (the chunk two on in the real list, not n's own continuation)
		nn = &chunks[real[ri + 2]];
		if (nn->fNext != -1)
			return 0;
		if (nn->f78 != 402 && nn->f74 != 300)
			return 0;
	}
	long h = c->fHeight;
	if (h - h / 8 < n->fHeight * 3 / 2)
		return 0;
	if (h - 4 * n->fHeight > h / 8)
		return 0;
	if (nn != nil)
	{
		if (nn->fHeight > 2 * n->fHeight)
			return 0;
		long dir = nodes[nn->fFrom].fDir;
		if ((dir & 0xff00) != 0)
			return 0;
		if (!direct_suits(dir, 4, 10))
			return 0;
	}
	long end = n->fX1;
	if (nn != nil)
		end = nn->fLeft;
	long width = n->fRight - c->fLeft;
	if (2 * (end - c->fLeft) - width > width / 8)
		return 0;
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[real[ri - 1]];
		if (p->fPrev != -1)
			return 0;
		if (h < 2 * p->fHeight)
			return 0;
		if (p->f78 != 401 && (p->f74 != 300 || !direct_suits(p->fDir, 15, 18)))
			goto six;
		long gap = n->fTop - p->fBottom;
		if (gap >= h / 4 && (gap >= h / 3 || p->fWidth <= h / 4))
			goto six;
		if (p->f74 == 300 && p->fRight > c->fX0 && p->fHeight > n->fHeight * 3 / 2)
			return 0;
		if (n->fTopNode != n->fTo)
		{
			tag_wapx_type* top = &nodes[n->fTopNode];
			if ((top->fDir & 0xff00) == 0)
				return 0;
			if (HWRAbs(top->y - n->fY0) > h / 8)
				return 0;
			if (HWRAbs(top->x - n->fX0) > h / 8)
				return 0;
		}
		*digit = 8;
		return 1;
	}
six:
	*digit = 6;
	return 1;
}


// ROM 0x00293710 (unnamed) - S_SixOrEight the other way round: a short
// stroke before, the smaller arc first and the tall one after it
static long
S_SixOrEightBack(tag_CHUNK_STAFF* staff, long ri, long* digit)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	tag_CHUNK* n = &chunks[real[ri + 1]];
	tag_CHUNK* p = nil;
	if (c->fPrev != -1)
	{
		p = &chunks[real[ri - 1]];
		if (p->f78 != 401 && p->f74 != 300)
			return 0;
		if (p->fPrev != -1)
			return 0;
	}
	long hn = n->fHeight;
	if (c->fHeight * 3 / 2 - hn > hn / 8)
		return 0;
	if (hn - 4 * c->fHeight > hn / 8)
		return 0;
	if (p != nil)
	{
		if (p->fHeight - c->fHeight > hn / 8)
			return 0;
		long dir = nodes[c->fFrom].fDir;
		if ((dir & 0xff00) != 0)
			return 0;
		if (!direct_suits(dir, 16, 20))
			return 0;
	}
	long start = c->fX0;
	if (p != nil)
		start = p->fLeft;
	long width = c->fRight - n->fLeft;
	if (2 * (start - n->fLeft) - width > width / 8)
		return 0;
	if (n->fNext != -1)
	{
		tag_CHUNK* nn = &chunks[real[ri + 2]];
		if (nn->fNext != -1)
			return 0;
		if (hn < 2 * nn->fHeight)
			return 0;
		if (nn->f78 != 402 && (nn->f74 != 300 || !direct_suits(nn->fDir, 6, 9)))
			goto six;
		long gap = c->fTop - nn->fBottom;
		if (gap >= hn / 4 && (gap >= hn / 3 || nn->fWidth <= hn / 4))
			goto six;
		if (nn->f74 == 300 && nn->fRight > n->fX1)
			return 0;
		if (nn->fHeight > c->fHeight * 3 / 2)
			return 0;
		if (c->fTopNode != c->fFrom)
		{
			tag_wapx_type* top = &nodes[c->fTopNode];
			if ((top->fDir & 0xff00) == 0)
				return 0;
			if (HWRAbs(top->y - c->fY0) > hn / 8)
				return 0;
			if (HWRAbs(top->x - c->fX0) > hn / 8)
				return 0;
		}
		*digit = 8;
		return 1;
	}
six:
	*digit = 6;
	return 1;
}


// ROM 0x002939d4 (unnamed) - whether the two arcs at real chunk ri are much
// the same height, and a stroke before or after them (if any) a short one
// of the same kind or a line, no taller than the arc it continues
static long
S_EvenPair(tag_CHUNK_STAFF* staff, long ri)
{
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	tag_CHUNK* n = &chunks[real[ri + 1]];
	long kind = c->f78;
	long d = HWRAbs(c->fHeight - n->fHeight);
	long tall = c->fHeight > n->fHeight ? c->fHeight : n->fHeight;
	if (3 * d - tall > tall / 8)
		return 0;
	if (n->fNext != -1)
	{
		tag_CHUNK* nn = &chunks[real[ri + 2]];
		if (nn->fNext != -1)
			return 0;
		if (nn->f78 != kind && nn->f74 != 300)
			return 0;
		if (nn->fHeight > n->fHeight)
			return 0;
	}
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[real[ri - 1]];
		if (p->fPrev != -1)
			return 0;
		if (p->f78 != kind && p->f74 != 300)
			return 0;
		if (p->fHeight > c->fHeight)
			return 0;
	}
	return 1;
}


// ROM 0x002944b0 (unnamed) - the arc down at real chunk ri (not a 402: an
// arc turning the other way) and the 401 after it: a 6 or 8, or a 0 or a
// per cent sign's ring.  *done the stroke's last chunk.  ==> 1 read.
static long
S_ArcDown(tag_CHUNK_STAFF* staff, long ri, int32_t* done)
{
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_wapx_type* nodes = staff->fNodes;
	tag_STK* strokes = staff->fStrokes;
	void* lo = staff->fLO;
	if (chunks[real[ri + 1]].f78 != 401)
		return 0;
	if (!S_SpanRight(staff, ri))
		return 0;
	tag_STK* stk = &strokes[chunks[real[ri]].fStroke];
	int32_t from = chunks[stk->fFirstChunk].fFrom;
	int32_t to = chunks[stk->fLastChunk].fTo;
	long digit;
	if (S_SixOrEightBack(staff, ri, &digit))
	{
		S_MarkChunks(nodes, chunks, from, to, 10);
		LO_Add(lo, nodes, 1300, from, to, 1600 + digit, 1);
		*done = strokes[chunks[real[ri]].fStroke].fLastChunk;
		return 1;
	}
	if (!S_EvenPair(staff, ri))
		return 0;
	if (S_Percent(staff, ri, &from, &to))
		LO_Add(lo, nodes, 1300, from, to, 1617, 1);
	else
		LO_Add(lo, nodes, 1300, from, to, 1600, 1);
	*done = strokes[chunks[real[ri]].fStroke].fLastChunk;
	S_MarkChunks(nodes, chunks, from, to, 10);
	return 1;
}


// ROM 0x002946d8 (unnamed) - two arcs 402 in a row, the one at real chunk ri
// and the one before it (with at most a short stroke either side): a 9
// read backwards, a per cent sign's ring or a 0.  Their chunks are marked
// 13.  *done the stroke's last chunk.  ==> 1 read.
static long
S_ArcAfterArc(tag_CHUNK_STAFF* staff, long ri, int32_t* done)
{
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_wapx_type* nodes = staff->fNodes;
	tag_STK* strokes = staff->fStrokes;
	void* lo = staff->fLO;
	tag_CHUNK* p = &chunks[real[ri - 1]];
	tag_CHUNK* c = &chunks[real[ri]];
	if (!(p->f78 == 402 && c->f78 == 402))
		return 0;
	if (p->fHeight - c->fHeight > p->fHeight / 4)
		return 0;
	if (p->fPrev != -1)
	{
		tag_CHUNK* pp = &chunks[p->fPrev];
		if (pp->fPrev != -1)
			return 0;
		if (p->fHeight < 2 * pp->fHeight)
			return 0;
		if (pp->f78 != 402 && pp->f74 != 300)
			return 0;
	}
	if (c->fNext != -1)
	{
		tag_CHUNK* nn = &chunks[c->fNext];
		if (nn->fHeight > c->fHeight)
			return 0;
		if (nn->f78 != 402 && nn->f74 != 300)
			return 0;
	}
	if (!S_SpanNoCorner(staff, ri - 1))
		return 0;
	int32_t from = chunks[strokes[p->fStroke].fFirstChunk].fFrom;
	int32_t to = chunks[strokes[c->fStroke].fLastChunk].fTo;
	if (S_NineShape(staff, ri - 1))
	{
		S_MarkChunks(nodes, chunks, from, to, 13);
		LO_Add(lo, nodes, 1300, from, to, 1609, 1);
		*done = strokes[chunks[real[ri]].fStroke].fLastChunk;
		return 1;
	}
	if (!S_EvenPair(staff, ri - 1))
		return 0;
	long value;
	// (ri here, where the tests before it were of ri - 1)
	if (S_Percent(staff, ri, &from, &to))
		value = 1617;
	else if (S_ZeroShape(staff, ri - 1))
		value = 1600;
	else
		return 0;
	LO_Add(lo, nodes, 1300, from, to, value, 1);
	*done = strokes[chunks[real[ri]].fStroke].fLastChunk;
	S_MarkChunks(nodes, chunks, from, to, 13);
	return 1;
}


// ROM 0x00295d60 (unnamed) - a grey 8: an arc 402 at real chunk ri with an
// arc 402 before and after it in its stroke, both shorter, the three
// meeting close in the middle - put in as class 2200, value 1608.
static long
S_GreyEight(tag_CHUNK_STAFF* staff, long ri)
{
	void* lo = staff->fLO;
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	tag_CHUNK* c = &chunks[staff->fRealChunks[ri]];
	if (c->f78 != 402)
		return 0;
	long h = c->fHeight;
	if (c->fNext == -1 || c->fPrev == -1)
		return 0;
	tag_CHUNK* p = &chunks[c->fPrev];
	tag_CHUNK* n = &chunks[c->fNext];
	if (p->f78 != 402 || n->f78 != 402)
		return 0;
	long hp = p->fHeight;
	if (hp > h - h / 8)
		return 0;
	long hn = n->fHeight;
	if (hn > h - h / 8)
		return 0;
	if (strokes[c->fStroke].fWidth > h + h / 8)
		return 0;
	long tenth = h / 10;
	if (n->fTop - p->fBottom > tenth + 1)
		return 0;
	if (HWRAbs(hn - hp) > h / 3 + tenth)
		return 0;
	if (hn * 3 / 2 > h + h / 8)
		return 0;
	if (hp * 3 / 2 > h + h / 8)
		return 0;
	tag_CHUNK* pp = p->fPrev == -1 ? nil : &chunks[p->fPrev];
	tag_CHUNK* nn = n->fNext == -1 ? nil : &chunks[n->fNext];
	long leftP = p->fLeftNode;
	if (p->fLeftNode > p->fBottomNode)
		leftP = p->fFrom;
	long leftN = n->fLeftNode;
	if (n->fLeftNode < n->fRightNode)
		leftN = n->fTo;
	long from = p->fFrom;
	long to = n->fTo;
	if (pp != nil)
	{
		if (pp->fPrev != -1)
			return 0;
		if (pp->f74 != 300 && pp->f78 != 402)
			return 0;
		if (pp->fHeight > hp)
			return 0;
		from = pp->fFrom;
		if (nodes[leftP].x > pp->fLeft)
			leftP = pp->fLeftNode;
	}
	if (nn != nil)
	{
		if (nn->fNext != -1)
			return 0;
		if (nn->f74 != 300 && nn->f78 != 402)
			return 0;
		if (nn->fHeight > hn)
			return 0;
		to = nn->fTo;
		if (nodes[leftN].x > nn->fLeft)
			leftN = nn->fLeftNode;
	}
	long width = p->fRight <= n->fRight ? n->fRight - c->fLeft : p->fRight - c->fLeft;
	long half = width / 2;
	if (nodes[leftP].x - c->fLeft > half + width / 8)
		return 0;
	if (nodes[leftN].x - c->fLeft > half + width / 8)
		return 0;
	S_MarkChunks(nodes, chunks, from, to, 10);
	LO_Add(lo, nodes, 2200, from, to, 1608, 0);
	return 1;
}


#pragma mark - the per cent sign

// ROM 0x00293c10 (unnamed) - whether stroke s is a per cent sign's slash
// between a ring a above left and one b below right: one or two chunks
// (one going down), a line or a flat arc, about as tall as wide, reaching
// from a's foot to b's top and across both
static long
S_PercentSlash(tag_CHUNK_STAFF* staff, long s, tag_STK* a, tag_STK* b)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* stk = &staff->fStrokes[s];
	long first = stk->fFirstChunk, last = stk->fLastChunk;
	long span = last - first;
	if (span > 2)
		return 0;
	long ci = first;
	if (chunks[first].fKind == 2)
	{
		if (span > 1)
			return 0;
	}
	else
	{
		ci = first + 1;
		if (last < ci)
			return 0;
	}
	tag_CHUNK* c = &chunks[ci];
	if (c->f74 != 300)
	{
		if (c->f74 != 400)
			return 0;
		if (c->fLength2 < 16 * c->fBulge)
			return 0;
	}
	if (c->fWidth - c->fHeight > c->fWidth / 8)
		return 0;
	long gap = b->fTop - a->fBottom;
	long h = staff->fHeight;
	if (h <= gap)
		h = gap;
	if (h > 2 * c->fHeight)
		return 0;
	long h6 = h / 6;
	if (b->fTop - c->fBottom > h6)
		return 0;
	if (c->fTop - a->fBottom > h6)
		return 0;
	if (c->fRight < a->fRight)
		return 0;
	return c->fLeft <= b->fLeft ? 1 : 0;
}


// ROM 0x00293e1c (unnamed) - whether stroke s2 (one to three chunks, all
// lines or of the kind of arc stroke s1's lowest point is on) is a ring
// that matches s1, the lower of the two starting no more than half s2's
// height below the other's foot
static long
S_RingsMatch(tag_CHUNK_STAFF* staff, long s1, long s2)
{
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	tag_STK* b = &strokes[s2];
	tag_STK* a = &strokes[s1];
	long first = b->fFirstChunk, last = b->fLastChunk;
	long span = last - first;
	long found = 0;
	long ci = NodeChunk(nodes, a->fBottomNode);
	long kind;
	if (chunks[ci].f74 != 300)
		kind = chunks[ci].f78;
	else if (ci + 1 <= a->fLastChunk && chunks[ci + 1].f74 != 300)
		kind = chunks[ci + 1].f78;
	else
	{
		if (ci - 1 < a->fFirstChunk)
			return 0;
		if (chunks[ci - 1].f74 == 300)
			return 0;
		kind = chunks[ci - 1].f78;
	}
	if (span < 1 || span > 3)
		return 0;
	for (long j = first; j <= last; j++)
	{
		if (chunks[j].f74 != 300 && chunks[j].f78 != kind)
			return 0;
		if (chunks[j].f78 == kind)
			found = 1;
	}
	if (!found)
		return 0;
	tag_STK* upper = s1 >= s2 ? b : a;
	tag_STK* lower = s1 >= s2 ? a : b;
	return upper->fBottom - lower->fTop > b->fHeight / 2 ? 0 : 1;
}


// ROM 0x00293d2c (unnamed) - a per cent sign as the last three strokes, the
// ring at real chunk ri the first or the last of them: *from and *to the
// span of the three.  ==> 1 found.
static long
S_PercentLast3(tag_CHUNK_STAFF* staff, long ri, int32_t* from, int32_t* to)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	long count = staff->fStrokeCount;
	long s = chunks[staff->fRealChunks[ri]].fStroke;
	long other, slash;
	tag_STK* first;
	tag_STK* third;
	if (s == count - 1)
	{
		other = s - 2;
		if (other < 0)
			return 0;
		third = &strokes[s];
		first = &strokes[s - 2];
		slash = s - 1;
	}
	else
	{
		other = s + 2;
		if (other != count - 1)
			return 0;
		first = &strokes[s];
		third = &strokes[s + 2];
		slash = s + 1;
	}
	if (!S_PercentSlash(staff, slash, first, third))
		return 0;
	if (!S_RingsMatch(staff, s, other))
		return 0;
	*from = chunks[first->fFirstChunk].fFrom;
	*to = chunks[third->fLastChunk].fTo;
	return 1;
}


// ROM 0x00294134 (unnamed) - whether the ring at real chunk ri is a per
// cent sign's: the last three strokes (S_PercentLast3), or the ring the
// last stroke and the stroke before it a tall slash ending in a curve
// down whose foot is the ring's box - *from moved back to take in the
// stroke before that too when it is a matching ring.  ==> 1 found.
static long
S_Percent(tag_CHUNK_STAFF* staff, long ri, int32_t* from, int32_t* to)
{
	tag_STK* strokes = staff->fStrokes;
	long count = staff->fStrokeCount;
	brack_type* brackets = staff->fBrackets;
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	long s = chunks[staff->fRealChunks[ri]].fStroke;
	if (s == 0)
		return 0;
	if (S_PercentLast3(staff, ri, from, to))
		return 1;
	if (count - 1 != s)
		return 0;
	tag_STK* cur = &strokes[s];
	tag_STK* prev = &strokes[s - 1];
	long hp = prev->fHeight;
	if (hp < cur->fHeight * 3 / 2)
		return 0;
	if (cur->fTop - prev->fTop < hp / 3)
		return 0;
	long k = prev->fLastChunk;
	if (chunks[k].fKind != 2)
	{
		k = k - 1;
		if (k < prev->fFirstChunk || chunks[k].fHeight < 4 * chunks[k + 1].fHeight)
			return 0;
	}
	tag_CHUNK* ck = &chunks[k];
	long hk = ck->fHeight;
	if (hp - hk > hp / 6)
		return 0;
	long node;
	if (k == prev->fFirstChunk)
	{
		if (ck->f78 != 501)
			return 0;
		node = brackets[ck->fLastBracket].fFrom;
		if (node + 1 > prev->fBottomNode)
			node = 1;
		if (prev->fBottomNode <= node)
			return 0;
	}
	else
	{
		if (k - prev->fFirstChunk > 5)
			return 0;
		if (ck->f74 == 300)
			node = ck->fFrom;
		else
		{
			if (ck->f78 != 501 && ck->f78 != 401)
				return 0;
			// ROM QUIRK: the walk only runs when the arc's top comes at or
			// after the node before its foot, and then walks away from it
			node = ck->fBottomNode - 1;
			if (ck->fTopNode >= node)
			{
				long top = ck->fTop;
				while (nodes[node].y - top >= hk / 4)
				{
					node = node - 1;
					if (!(ck->fTopNode >= node))
						break;
				}
			}
		}
	}
	if (nodes[node].y - ck->fTop > hp / 8)
		return 0;
	tag_BOX box;
	GetBox(nodes, chunks[prev->fFirstChunk].fFrom, node, &box);
	if (box.bottom - box.top > 2 * cur->fHeight)
		return 0;
	if (prev->fHeight - 2 * (prev->fBottom - box.bottom) > prev->fHeight / 8)
		return 0;
	long firstChunk = prev->fFirstChunk;
	if (s - 1 > 0)
	{
		tag_STK* before = &strokes[s - 2];
		if (2 * before->fHeight - hk < hk / 8
			&& before->fBottom - cur->fTop < cur->fHeight / 3
			&& HWRAbs(cur->fHeight - before->fHeight) < cur->fHeight / 4)
			firstChunk = before->fFirstChunk;
	}
	*from = chunks[firstChunk].fFrom;
	return 1;
}


// ROM 0x00293ae8 (unnamed) - a per cent sign from two small marks, a above
// left of b: the stroke before the earlier of them or after the later
// being the slash between them.  *from (or *to) moved to take it in.
static long
S_PercentAround(tag_CHUNK_STAFF* staff, tag_STK* a, tag_STK* b, int32_t* from, int32_t* to)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	long count = staff->fStrokeCount;
	if (a->fRight > b->fRight || a->fLeft > b->fLeft)
		return 0;
	long sa = chunks[a->fFirstChunk].fStroke;
	long sb = chunks[b->fFirstChunk].fStroke;
	long lo = sa >= sb ? sb : sa;
	long hi = sa <= sb ? sb : sa;
	long before = lo - 1;
	long after = hi + 1;
	if (before >= 0 && S_PercentSlash(staff, before, a, b))
	{
		*from = chunks[strokes[before].fFirstChunk].fFrom;
		return 1;
	}
	if (after >= count)
		return 0;
	if (!S_PercentSlash(staff, after, a, b))
		return 0;
	*to = chunks[strokes[after].fLastChunk].fTo;
	return 1;
}


#pragma mark - the marks

// ROM 0x00292eb4 (unnamed) - whether strokes a and b are two small marks
// one above the other (a colon's dots, the rings of an 8 or a per cent
// sign): much of a size (within a quarter of the line's height, half
// when loose), overlapping across or close, one or two chunks each (two to
// four when taller than a quarter of the line), the lower well below the
// upper.  *upper and *lower which is which.
static long
S_StackedPair(tag_STK* a, tag_STK* b, long loose, SLine* line, tag_STK** upper, tag_STK** lower)
{
	long limit = line->fHeight / 4;
	if (loose)
		limit = line->fHeight / 2;
	long tall = a->fHeight > b->fHeight ? a->fHeight : b->fHeight;
	long dh = HWRAbs(a->fHeight - b->fHeight);
	long dw = HWRAbs(a->fWidth - b->fWidth);
	if (dh > limit || dw > limit)
		return 0;
	long gap;
	if ((a->fRight >= b->fLeft && a->fRight <= b->fRight)
		|| (a->fLeft >= b->fLeft && a->fLeft <= b->fRight))
		gap = 0;
	else
	{
		long l = a->fLeft > b->fLeft ? a->fLeft : b->fLeft;
		long r = a->fRight < b->fRight ? a->fRight : b->fRight;
		gap = l - r;
	}
	if (gap > line->fHeight)
		return 0;
	long n = a->fLastChunk - a->fFirstChunk + 1;
	if (tall > line->fHeight / 4)
	{
		if (n < 2 || n > 4)
			return 0;
	}
	else
	{
		if (n > 2)
			return 0;
		if (a->fWidth > line->fHeight / 4)
			return 0;
	}
	if (a->fBottom <= b->fBottom)
	{
		*upper = a;
		*lower = b;
	}
	else
	{
		*upper = b;
		*lower = a;
	}
	tag_STK* u = *upper;
	tag_STK* d = *lower;
	if (d->fBottom - u->fBottom < tall / 3)
		return 0;
	if (d->fTop - u->fTop < tall / 3)
		return 0;
	long span = d->fBottom - u->fTop;
	if (gap > span / 2)
		return 0;
	if (gap > line->fHeight / 4 || gap > span / 4)
	{
		if (dh > line->fHeight / 8)
			return 0;
		if (dw > line->fHeight / 8)
			return 0;
	}
	if (d->fTop > u->fBottom)
		return 1;
	if (u->fBottom - d->fTop >= tall / 2)
		return 0;
	if (gap != 0)
		return 0;
	return 1;
}


// ROM 0x00291060 (unnamed) - the small marks (class 1600, a ring 1600 or a
// dot 1614) that are whole strokes, each tried with the stroke before and
// after it: two of them stacked an 8 (overlapping across, both small), a
// per cent sign (with a slash beside them) or a colon.  A mark whose
// chunks fall inside what the last one read took is used up.  ROM QUIRK:
// with the first stroke's mark and no second stroke it returns at once,
// leaving the list's work class unrestored.  ==> 0.
static long
S_StackedMarks(tag_CHUNK_STAFF* staff, SLine* line)
{
	void* lo = staff->fLO;
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_STK* strokes = staff->fStrokes;
	long strokeCount = staff->fStrokeCount;
	tag_LOWOBJ* obj = nil;
	long prevStroke = -1, prevValue = 0;
	long lastFrom = -1, lastTo = -1;		// the chunks the last one read spans
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1600);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue != 1600 && obj->fValue != 1614)
			continue;
		long n = LO_HowManyChunks(lo, obj);
		long r0 = LO_GetRealChunkInd(lo, chunks, nodes, obj, 1);
		long r1 = LO_GetRealChunkInd(lo, chunks, nodes, obj, n);
		if (lastFrom != -1
			&& ((real[r0] <= lastTo && real[r0] >= lastFrom)
				|| (real[r1] <= lastTo && real[r1] >= lastFrom)))
		{
			obj->fValue = 0xffff;
			continue;
		}
		tag_CHUNK* c0 = &chunks[real[r0]];
		if (c0->fPrev != -1)
			continue;
		if (chunks[real[r1]].fNext != -1)
			continue;
		long s = c0->fStroke;
		if (s == 0 && strokeCount <= 1)
			return 0;
		tag_STK* upper = nil;
		tag_STK* lower = nil;
		long found = 0;
		if (s != 0)
			found = S_StackedPair(&strokes[s - 1], &strokes[s],
								  s - 1 == prevStroke && obj->fValue == prevValue, line, &upper, &lower);
		if (!found && s + 1 < strokeCount)
			found = S_StackedPair(&strokes[s + 1], &strokes[s],
								  s + 1 == prevStroke && obj->fValue == prevValue, line, &upper, &lower);
		prevStroke = s;
		prevValue = obj->fValue;
		if (!found)
			continue;
		lastFrom = upper->fFirstChunk < lower->fFirstChunk ? upper->fFirstChunk : lower->fFirstChunk;
		lastTo = upper->fLastChunk > lower->fLastChunk ? upper->fLastChunk : lower->fLastChunk;
		int32_t from = chunks[lastFrom].fFrom;
		int32_t to = chunks[lastTo].fTo;
		long span = lower->fBottom - upper->fTop;
		long value = 0;
		if (lower->fTop - upper->fBottom < span / 4
			&& lower->fHeight + span / 8 > span / 3
			&& upper->fHeight + span / 8 > span / 3
			&& S_Overlaps(upper->fLeft, upper->fRight, lower->fLeft, lower->fRight))
			value = 1608;
		else if (S_PercentAround(staff, upper, lower, &from, &to))
			value = 1617;
		else if (lower->fHeight < span / 3 && upper->fHeight < span / 3
				 && lower->fTop - upper->fBottom > span / 4)
			value = 1615;
		if (value != 0)
			LO_Add(lo, nodes, 1300, from, to, value, 1);
		obj->fValue = 0xffff;
	}
	LO_SetWorkClass(lo, saved);
	return 0;
}


// ROM 0x00293f7c (unnamed) - the dots (class 1600, value 1614) put in as
// full stops (class 1300, extra 1) when they sit low in the line.  ROM
// QUIRK: a dot high in the line after a chunk that runs down to the left
// or up (direction under 5 or over 22) ends the pass at once, leaving the
// list's work class unrestored and answering 1.  ==> 0.
static long
S_DotsPass(tag_CHUNK_STAFF* staff, SLine* line)
{
	void* lo = staff->fLO;
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1600);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue != 1614)
			continue;
		long n = LO_HowManyChunks(lo, obj);
		long r0 = LO_GetRealChunkInd(lo, chunks, nodes, obj, 1);
		long r1 = LO_GetRealChunkInd(lo, chunks, nodes, obj, n);
		long first = real[r0];
		int32_t from = chunks[first].fFrom;
		tag_CHUNK* c1 = &chunks[real[r1]];
		int32_t to = c1->fTo;
		if (line->fBottom - line->fHeight / 3 < c1->fBottom)
			LO_Add(lo, nodes, 1300, from, to, 1614, 1);
		else if (first > 0)
		{
			long dir = chunks[first - 1].fDir;
			if (dir < 5 || dir > 22)
				return 1;
		}
		obj->fValue = 0xffff;
	}
	LO_SetWorkClass(lo, saved);
	return 0;
}


// ROM 0x002960d8 (unnamed) - the small rings (class 1600, value 1600): one
// small and low in the line a full stop (extra 4), one half the line's
// height reaching its foot a 0 (extra 4, taking in a stroke after it that
// runs through it) unless S_LoopTurnsBack says it is not closed.  ==> 0.
static long
S_RingsPass(tag_CHUNK_STAFF* staff, SLine* line)
{
	void* lo = staff->fLO;
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_STK* strokes = staff->fStrokes;
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1600);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue != 1600)
			continue;
		long n = LO_HowManyChunks(lo, obj);
		long r0 = LO_GetRealChunkInd(lo, chunks, nodes, obj, 1);
		long r1 = LO_GetRealChunkInd(lo, chunks, nodes, obj, n);
		int32_t from = chunks[real[r0]].fFrom;
		int32_t to = chunks[real[r1]].fTo;
		tag_STK* stk = &strokes[chunks[real[r0]].fStroke];
		long h = stk->fHeight;
		long lh = line->fHeight;
		if (h < lh / 3 && line->fBottom - lh / 3 < stk->fBottom
			&& line->fTop + lh / 2 < stk->fTop)
			LO_Add(lo, nodes, 1300, from, to, 1614, 4);
		else if (lh / 2 - h < lh / 8 && line->fBottom - lh / 2 < stk->fBottom
				 && !S_LoopTurnsBack(staff, r0, r1))
		{
			from = obj->fFrom;
			to = obj->fTo;
			S_StrokeThrough(staff, r0, &to);
			LO_Add(lo, nodes, 1300, from, to, 1600, 4);
		}
		obj->fValue = 0xffff;
	}
	LO_SetWorkClass(lo, saved);
	return 0;
}


// ROM 0x002949e0 (unnamed) - whether the stroke after the one real chunk ri
// is in runs through it (one or two chunks, the one its top is on going
// down, a line or an arc as tall as the stroke, from the ring's foot to its
// top and crossing its middle): *to that stroke's end.  ==> 1 found.
static long
S_StrokeThrough(tag_CHUNK_STAFF* staff, long ri, int32_t* to)
{
	tag_wapx_type* nodes = staff->fNodes;
	tag_STK* strokes = staff->fStrokes;
	tag_CHUNK* chunks = staff->fChunks;
	long s = chunks[staff->fRealChunks[ri]].fStroke + 1;
	if (s >= staff->fStrokeCount)
		return 0;
	tag_STK* b = &strokes[s];
	tag_STK* a = &strokes[s - 1];
	if (b->fLeft > a->fRight)
		return 0;
	if (b->fRight < a->fLeft || b->fHeight < a->fHeight)
		return 0;
	if (b->fLastChunk - b->fFirstChunk > 2)
		return 0;
	long ci = HWRAbs(nodes[b->fTopNode].f18);
	if (ci > 0)
		ci = ci - 1;
	if (ci == b->fFirstChunk && b->fLastChunk - b->fFirstChunk > 1)
		return 0;
	tag_CHUNK* c = &chunks[ci];
	if (c->fKind != 2)
		return 0;
	if (c->f74 != 300 && c->f74 != 400)
		return 0;
	if (b->fHeight > c->fHeight)
		return 0;
	long h6 = c->fHeight / 6;
	if (a->fBottom - c->fBottom > h6)
		return 0;
	if (c->fTop - a->fTop > h6)
		return 0;
	long x = x_in_line(c->fX0, c->fY0, c->fX1, c->fY1, (a->fBottom + a->fTop) / 2);
	if (a->fRight - x < a->fWidth / 4)
		return 0;
	if (a->fLeft > x)
		return 0;
	*to = chunks[b->fLastChunk].fTo;
	return 1;
}


// ROM 0x00294d78 (unnamed) - whether a stroke's first chunk a and last b
// turn back on themselves at either end (the chunk before a, or after b,
// running back across the start by more than an eighth of their height)
static long
S_EndsTurnBack(tag_CHUNK* chunks, tag_wapx_type* nodes, tag_CHUNK* a, tag_CHUNK* b)
{
	if (a->fPrev == -1 && b->fNext == -1)
		return 0;
	long start, end;
	if (a->fX0 <= b->fX1)
	{
		start = a->fFrom;
		end = b->fTo;
	}
	else
	{
		end = a->fFrom;
		start = b->fTo;
	}
	long tall = a->fHeight > b->fHeight ? a->fHeight : b->fHeight;
	if (a->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[a->fPrev];
		long run = 0;
		bool test = false;
		if (a->f78 == 402)
		{
			run = p->fX1 - p->fX0;
			test = run > tall / 8;
		}
		else if (a->f78 == 401)
		{
			run = p->fX0 - p->fX1;
			test = run > tall / 8;
		}
		if (test && S_NearX(chunks, nodes, &p->fFrom, start, end, p->fFrom, tall))
			return 1;
	}
	if (b->fNext == -1)
		return 0;
	tag_CHUNK* e = &chunks[b->fNext];
	long run = 0;
	if (a->f78 == 402)
		run = e->fX1 - e->fX0;
	else if (a->f78 == 401)
		run = e->fX0 - e->fX1;
	else
		return 0;
	if (run <= tall / 8)
		return 0;
	return S_NearX(chunks, nodes, &e->fFrom, start, end, e->fTo, tall) != 0 ? 1 : 0;
}


// ROM 0x00294b84 (unnamed) - whether the ring from real chunk ri to rj is
// not closed: its stroke goes on past rj's chunk (answer 1), or its ends
// turn back (S_EndsTurnBack), or the stroke after the bottom of the ring
// comes back up by the right amount.  A ring marked 13 is closed.
static long
S_LoopTurnsBack(tag_CHUNK_STAFF* staff, long ri, long rj)
{
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* a = &chunks[real[ri]];
	long last = real[rj];
	if (a->f6C == 13)
		return 0;
	tag_CHUNK* c = a;
	if (a->fKind != 2)
		c = &chunks[real[ri + 1]];
	if (c->fNext == -1 || c->fNext > last)
		return 1;
	tag_CHUNK* b = &chunks[c->fNext];
	long tall = c->fHeight > b->fHeight ? c->fHeight : b->fHeight;
	long wb = b->fWidth;
	if (c->f78 == 402)
	{
		if (c->fX0 - b->fX1 > wb / 3 && S_EndsTurnBack(chunks, nodes, c, b))
			return 1;
	}
	else if (c->f78 == 401)
	{
		if (b->fX1 - c->fX0 <= wb / 3)
			return 0;
		if (S_EndsTurnBack(chunks, nodes, c, b))
			return 1;
		return 0;
	}
	if (b->fNext == -1)
		return 0;
	tag_CHUNK* e = &chunks[b->fNext];
	long foot = e->fBottomNode;
	long mid = (e->fBottomNode + e->fFrom) / 2;
	if (mid == e->fFrom)
		mid = foot;
	if (e->fX1 < nodes[b->fBottomNode].x)
		return 0;
	if (nodes[mid].x - c->fLeft < wb / 3)
		return 0;
	long rise = b->fBottom - nodes[foot].y;
	long hb = b->fHeight;
	if (rise <= 0)
		return 1;
	if (hb < 2 * rise)
		return 0;
	if (hb > 4 * rise)
		return 1;
	if (tall <= hb)
		return 0;
	if (tall > 4 * rise)
		return 1;
	if (3 * rise >= hb)
		return 0;
	return 1;
}


// ROM 0x00294fd0 (unnamed) - the writing's line worked out again from the
// digits found so far (class 1300, the codes 0 to 9): the mean top and
// bottom of their boxes and the height between.  Unchanged with none.
static void
S_DigitsLine(tag_CHUNK_STAFF* staff, SLine* line)
{
	void* lo = staff->fLO;
	tag_LOWOBJ* obj = nil;
	long count = 0, tops = 0, bottoms = 0;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1300);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue == 0xffff)
			continue;
		ULong d = (uint32_t) obj->fValue % 100;
		if (d > 9)
			continue;
		count++;
		tops += obj->fTop;
		bottoms += obj->fBottom;
	}
	if (count != 0)
	{
		line->fHeight = (int32_t) ((bottoms - tops) / count);
		line->fTop = (int32_t) (tops / count);
		line->fBottom = (int32_t) (bottoms / count);
	}
	LO_SetWorkClass(lo, saved);
}


// ROM 0x002950d4 (unnamed) - the bars (class 1600, value 1613) judged
// against the strokes either side of them (their bottom and height, or the
// line's with none): a wide one, or one high above the foot, a minus (code
// 13, extra 3), anything else a full stop (code 14, extra 2).  ==> 0.
static long
S_BarsPass(tag_CHUNK_STAFF* staff, SLine* line)
{
	void* lo = staff->fLO;
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_STK* strokes = staff->fStrokes;
	long count = staff->fStrokeCount;
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1600);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue != 1613)
			continue;
		long n = LO_HowManyChunks(lo, obj);
		long r0 = LO_GetRealChunkInd(lo, chunks, nodes, obj, 1);
		long r1 = LO_GetRealChunkInd(lo, chunks, nodes, obj, n);
		tag_CHUNK* c = &chunks[real[r0]];
		int32_t from = c->fFrom;
		int32_t to = chunks[real[r1]].fTo;
		long s = c->fStroke;
		long bottom, height;
		if (s - 1 > 0)
		{
			bottom = strokes[s - 2].fBottom > strokes[s - 1].fBottom ? strokes[s - 2].fBottom : strokes[s - 1].fBottom;
			height = strokes[s - 2].fHeight > strokes[s - 1].fHeight ? strokes[s - 2].fHeight : strokes[s - 1].fHeight;
		}
		else if (s != 0)
		{
			height = strokes[s - 1].fHeight;
			bottom = strokes[s - 1].fBottom;
		}
		else
			bottom = height = 0;
		long nb, nh;
		if (s + 2 < count)
		{
			nb = strokes[s + 2].fBottom > strokes[s + 1].fBottom ? strokes[s + 2].fBottom : strokes[s + 1].fBottom;
			nh = strokes[s + 2].fHeight > strokes[s + 1].fHeight ? strokes[s + 2].fHeight : strokes[s + 1].fHeight;
		}
		else if (s + 1 < count)
		{
			nb = strokes[s + 1].fBottom;
			nh = strokes[s + 1].fHeight;
		}
		else
			nb = nh = 0;
		if (bottom == 0)
		{
			if (nb != 0)
			{
				bottom = nb;
				height = nh;
			}
			else
			{
				bottom = line->fBottom;
				height = line->fHeight;
			}
		}
		else if (nb != 0)
		{
			bottom = (bottom + nb) / 2;
			height = (nh + height) / 2;
		}
		long width = obj->fRight - obj->fLeft;
		long code;
		if (width > height / 3)
			code = 13;
		else if (bottom - c->fBottom <= height / 5)
			code = 14;
		else if (width <= height / 6 - height / 8)
			code = 14;
		else
			code = 13;
		if (code == 13)
			LO_Add(lo, nodes, 1300, from, to, 1613, 3);
		else
			LO_Add(lo, nodes, 1300, from, to, 1614, 2);
		obj->fValue = 0xffff;
	}
	LO_SetWorkClass(lo, saved);
	return 0;
}


#pragma mark - chunk by chunk

// ROM 0x0029634c (unnamed) - an unused real chunk i asked what it starts:
// an "@", a "(", a ")", a 9, a bar (and what it makes with its
// neighbours), a 3.  ==> 1 when one was read.
static long
S_PerChunk(tag_CHUNK_STAFF* staff, long i, SLine* line)
{
	tag_CHUNK* c = &staff->fChunks[staff->fRealChunks[i]];
	if (c->fPrev == -1 && c->fNext != -1 && S_At(staff, i))
	{
		S_PutStroke(staff, i, 20, "@");
		return 1;
	}
	if (S_LeftParen(staff, i, line))
		return 1;
	if (S_RightParen(staff, i, line))
		return 1;
	if (S_Nine(staff, i))
	{
		S_PutStroke(staff, i, 9, "9");
		return 1;
	}
	if (S_Bar(staff, i, line))
		return 1;
	if (!S_Three(staff, i))
		return 0;
	S_PutStroke(staff, i, 3, "3");
	return 1;
}


// ROM 0x00295bc4 (unnamed) - whether the stroke from real chunk ri is an
// "@": three to six chunks ending in two arcs 402 (the last going down
// round the outside), as wide as it is tall, its start and top near the
// box of the chunks inside.  ROM BUG (fixed): with three chunks it asks
// for the first's kind to be 701, a subclass, which no kind is - so a
// three-chunk "@" is never read.  The fix asks it of the first's subclass
// (f78), where 701 is one.
static long
S_At(tag_CHUNK_STAFF* staff, long ri)
{
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	long ci = staff->fRealChunks[ri];
	tag_CHUNK* c = &chunks[ci];
	long last = staff->fStrokes[c->fStroke].fLastChunk;
	if (chunks[last].fKind == 1)
	{
		if (chunks[last].fHeight > chunks[last - 1].fHeight)
			return 0;
		last = last - 1;
	}
	long n = last - ci + 1;
	if (n < 3 || n > 6)
		return 0;
	if (n < 4 && c->fKind == 1)
		return 0;
	if (n > 5 && c->fKind != 1)
		return 0;
	if (n == 3 && (RomBugFixed() ? c->f78 : c->fKind) != 701)
		return 0;
	tag_CHUNK* e = &chunks[last];
	if (e->f78 != 402 || e[-1].f78 != 402)
		return 0;
	tag_BOX box;
	DefRectForChunks(chunks, nodes, ci, last - 2, &box);
	long right = e[-1].fRight;
	long width = right - e->fLeft;
	long h = e->fHeight;
	if (h > 2 * width)
		return 0;
	if (3 * h < width || 3 * h < staff->fHeight)
		return 0;
	if (e->fLeft - box.left > width / 8)
		return 0;
	if (e->fY0 - box.top > h / 6)
		return 0;
	if (box.right - right > width / 8)
		return 0;
	return 1;
}


// ROM 0x00296bec (unnamed) - a "(" from the arc 402 at real chunk i: at
// least the line's height, narrow, bulging well (its chord no more than
// seven times its bulge), turning back at its foot, with at most a tiny
// hook either end.  Put in as code 10, extra 0x20.
static long
S_LeftParen(tag_CHUNK_STAFF* staff, long i, SLine* line)
{
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* c = &chunks[real[i]];
	if (staff->fRealCount <= 3)
		return 0;
	if (c->fHeight < line->fHeight)
		return 0;
	if (c->fWidth >= c->fHeight)
		return 0;
	if (c->fKind != 2 || c->f78 != 402)
		return 0;
	long h6 = c->fHeight / 6;
	if (find_direct_backward(nodes, c->fTo, 0, 0, h6) <= 11)
		return 0;
	long bulge = c->fBulge;
	if (bulge == 0 || c->fTopNode == c->fLeftNode || c->fBottomNode == c->fLeftNode)
		return 0;
	long prev = c->fPrev;
	if (prev != -1)
	{
		tag_CHUNK* p = &chunks[real[i - 1]];
		if (p->fHeight > h6 || p->fWidth > h6)
			return 0;
		if (p->fPrev != -1)
			return 0;
	}
	if (c->fNext != -1)
	{
		tag_CHUNK* n = &chunks[real[i + 1]];
		if (n->fHeight > h6 || n->fWidth > h6)
			return 0;
		if (n->fNext != -1)
			return 0;
	}
	if (c->fLength2 / bulge > 49)
		return 0;
	c->f6C = 10;
	long from, to;
	if (prev != -1)
	{
		from = chunks[real[i - 1]].fFrom;
		chunks[real[i - 1]].f6C = 10;
	}
	else
		from = c->fFrom;
	if (c->fNext != -1)
	{
		to = chunks[real[i + 1]].fTo;
		chunks[real[i + 1]].f6C = 10;
	}
	else
		to = c->fTo;
	LO_Add(staff->fLO, nodes, 1300, from, to, 1610, 0x20);
	return 1;
}


// ROM 0x002914b4 (unnamed) - a ")" from the arc 401 at real chunk i (not
// the first stroke's): as tall as the line and the stroke before it, no
// wider than tall, its rightmost point in the middle (or the ends pointing
// back left), a tiny hook either end at most.  Put in as code 11, extra
// 0x20.  ROM QUIRK: the chunk is marked 10, the code of "(".
static long
S_RightParen(tag_CHUNK_STAFF* staff, long i, SLine* line)
{
	void* lo = staff->fLO;
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	int32_t* real = staff->fRealChunks;
	if (i == 0)
		return 0;
	tag_CHUNK* c = &chunks[real[i]];
	long s = c->fStroke - 1;
	if (s < 0)
		return 0;
	if (c->f78 != 401)
		return 0;
	long h = c->fHeight;
	long w = c->fWidth;
	if (h < line->fHeight)
		return 0;
	if (w > h)
		return 0;
	if (c->fY1 - c->fY0 < h / 2)
		return 0;
	tag_STK* before = &strokes[s];
	long h6 = h / 6;
	if (before->fHeight - h > h6)
		return 0;
	if (before->fBottom - c->fBottom > h6)
		return 0;
	if (c->fTop - before->fTop > h6)
		return 0;
	long right = c->fRightNode;
	if (c->fBottomNode == right)
		return 0;
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[real[i - 1]];
		if (p->fHeight > h6 || p->fWidth > h6)
			return 0;
		if (p->fPrev != -1)
			return 0;
	}
	if (c->fNext != -1)
	{
		tag_CHUNK* n = &chunks[real[i + 1]];
		if (n->fHeight > h6 || n->fWidth > h6)
			return 0;
		if (n->fNext != -1)
			return 0;
	}
	long down = nodes[right].y - c->fTop;
	if (h - 2 * down > h / 8)
	{
		long turn = 5;
		if (3 * down - h < h / 10)
		{
			if (c->fTo - right < 2)
				return 0;
			turn = 4;
		}
		long d1 = find_direct_backward(nodes, right, 0, 0, w / 4);
		long d2 = find_direct_forward(nodes, c->fTo + 1, c->fRightNode, 0, 0, w / 4);
		if (distance_between_directions(d1, d2) >= turn)
			return 0;
		long d3 = find_direct_forward(nodes, c->fRightNode + 1, c->fFrom, w / 2 - w / 8, 0, 0);
		if (direct_suits(d3, 17, 19))
		{
			long d4 = find_direct_backward(nodes, c->fTo, w / 3, 0, h / 2);
			if (d4 >= 12)
				return 0;
			if (distance_between_directions(d3, d4 + 12) > 2)
				return 0;
		}
	}
	c->f6C = 10;
	long from = c->fPrev != -1 ? chunks[real[i - 1]].fFrom : c->fFrom;
	long to = c->fNext != -1 ? chunks[real[i + 1]].fTo : c->fTo;
	LO_Add(lo, nodes, 1300, from, to, 1611, 0x20);
	return 1;
}


// ROM 0x002958cc (unnamed) - a 9 from the arc 401 at real chunk i going up
// and the taller 401 after it: the first no more than two thirds of the
// second's height, anything before or after it short, the second passing
// well to the right of the first's leftmost point
static long
S_Nine(tag_CHUNK_STAFF* staff, long ri)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	tag_CHUNK* p = nil;
	if (c->fNext == -1 || c->fKind != 1)
		return 0;
	tag_CHUNK* n = &chunks[real[ri + 1]];
	long width = n->fRight - c->fLeft;
	if (c->f78 != 401 || n->f78 != 401)
		return 0;
	long hc = c->fHeight;
	long hn = n->fHeight;
	if (hc > 2 * hn / 3)
		return 0;
	if (5 * hc < hn)
		return 0;
	if (c->fPrev != -1)
	{
		p = &chunks[real[ri - 1]];
		if (p->fPrev != -1)
			return 0;
		if (p->fHeight > hc)
			return 0;
		if (p->f74 != 300 && p->f78 != 401)
			return 0;
		if (width < p->fWidth)
			return 0;
	}
	if (n->fNext != -1)
	{
		tag_CHUNK* nn = &chunks[real[ri + 2]];
		if (nn->fNext != -1)
			return 0;
		if (3 * nn->fHeight > hn)
			return 0;
		if (3 * nn->fWidth > width && direct_suits(nn->fDir, 14, 19))
			return 0;
	}
	long k;
	if (p != nil && p->fTop > c->fTop)
		k = p->fRightNode;
	else if (c->fRightNode < c->fLeftNode)
		k = c->fRightNode;
	else
	{
		k = c->fBottomNode;
		if (c->fX0 > nodes[k].x)
			k = c->fFrom;
	}
	long x = x_in_curve(nodes, n, nodes[k].y);
	if (width - width / 8 <= 2 * (x - nodes[k].x))
		return 0;
	return 1;
}


// ROM 0x002953f8 (unnamed) - a 3: the chunk at real chunk i going down, an
// S (702) whose middle comes far enough down and whose end runs out left,
// or a 401 followed by two more 401s, the second a line or an arc, all
// much of a height, the tail running down towards the foot
static long
S_Three(tag_CHUNK_STAFF* staff, long ri)
{
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	brack_type* brackets = staff->fBrackets;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[ri]];
	long sub = c->f78;
	if (sub != 702 && sub != 401)
		return 0;
	if (c->fKind != 2)
		return 0;
	long h = strokes[c->fStroke].fHeight;
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[real[ri - 1]];
		if (h < 2 * p->fHeight)
			return 0;
		if (p->fPrev != -1)
			return 0;
	}
	if (sub != 702)
	{
		// the 401 way
		if (c->fNext == -1)
			return 0;
		tag_CHUNK* n1 = &chunks[real[ri + 1]];
		if (n1->fNext == -1)
			return 0;
		tag_CHUNK* n2 = &chunks[real[ri + 2]];
		if (n2->fNext != -1)
		{
			tag_CHUNK* n3 = &chunks[real[ri + 3]];
			if (n3->fNext != -1)
				return 0;
			if (h < 2 * n3->fHeight)
				return 0;
		}
		if (n2->f78 != 401)
			return 0;
		if (n1->f78 != 401 && n1->f74 != 300)
			return 0;
		long h1 = n1->fHeight;
		if (c->fHeight < h1 * 3 / 2)
			return 0;
		long h2 = n2->fHeight;
		if (h2 * 3 / 2 > h + h / 8)
			return 0;
		if (h - h / 8 > 4 * h2)
			return 0;
		// (these two are of c's height again, not n1's)
		if (c->fHeight * 3 / 2 > h + h / 8)
			return 0;
		if (h - h / 8 > 4 * c->fHeight)
			return 0;
		if (!direct_suits(n2->fDir, 9, 14))
			return 0;
		long dir = GetDirection(nodes[c->fTopNode].x, c->fTop, nodes[n2->fBottomNode].x, n2->fBottom);
		return direct_suits(dir, 9, 14) ? 1 : 0;
	}
	brack_type* b = &brackets[c->fFirstBracket];
	if (b->fKind == 1 || b[1].fKind == 1)
		return 0;
	long mid = b->fTo;
	long midY = nodes[mid].y;
	long drop = c->fBottom - midY;
	if ((4 * h) / 5 < drop)
		return 0;
	if (h - h / 8 > 4 * drop)
		return 0;
	long dir = GetDirection(nodes[mid].x, midY, c->fX1, c->fY1);
	if (direct_suits(dir, 16, 23))
		return 0;
	if (direct_suits(dir, 14, 15) && 3 * drop < h + h / 8)
	{
		long d;
		if (c->fNext != -1)
			d = chunks[c->fNext].fDir;
		else
			d = nodes[c->fTo - 1].fDirOut;
		if (!direct_suits(d, 3, 6))
			return 0;
	}
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[real[ri - 1]];
		if (h <= 4 * p->fWidth && direct_suits(p->fDir, 3, 6))
			return 0;
	}
	if (c->fNext != -1)
	{
		tag_CHUNK* n = &chunks[real[ri + 1]];
		if (h < 2 * n->fHeight)
			return 0;
		if (n->fNext != -1)
			return 0;
		long rise = nodes[mid].y - c->fY0;
		if (5 * rise < c->fHeight)
			return 0;
		if (direct_suits(n->fDir, 16, 20) && c->fHeight <= 4 * n->fWidth && c->fHeight > 4 * rise)
			return 0;
	}
	long k = mid;
	while (k > c->fFrom)
	{
		if (nodes[k - 1].y > nodes[k].y)
			break;
		k = k - 1;
	}
	if (k <= c->fFrom)
		return 1;
	long d = GetDirection(c->fX0, c->fY0, nodes[k].x, nodes[k].y);
	if (!direct_suits(d, 3, 14))
		return 1;
	if (c->fX0 - nodes[k].x < h / 4)
		return 1;
	return 0;
}


#pragma mark - the bars

// ROM 0x0029188c (unnamed) - the stroke real chunk i is in, a short wide
// one of at most three chunks (one of them wide): with the stroke before
// or after it, a 5's bar (code 5, extra 0x21 or 0x22), a 7's (code 7,
// extra 0x23), or the bar of a + (code 12, over both strokes when the
// other is the first written) or a minus (code 13, extra 0x23 or 0x22 -
// ROM QUIRK: the minus after the other stroke is not put in at all when
// the bar is not the first thing written); failing all of them a bar
// (class 1600, value 1613, extra 0x24) for later.  Its chunks are marked
// 10.  ==> 1 when one was found.
static long
S_Bar(tag_CHUNK_STAFF* staff, long i, SLine* line)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	void* lo = staff->fLO;
	tag_STK* strokes = staff->fStrokes;
	long strokeCount = staff->fStrokeCount;
	long s = chunks[staff->fRealChunks[i]].fStroke;
	tag_STK* stk = &strokes[s];
	long w = stk->fWidth;
	long first = stk->fFirstChunk, last = stk->fLastChunk;
	long n = last - first + 1;
	long result = 0;
	if (3 * stk->fHeight > 2 * w)
		return 0;
	long h6 = line->fHeight / 6;
	if (!(h6 < w && 3 * stk->fHeight < line->fHeight))
		return 0;
	if (n > 3)
		return 0;
	if (n > 1)
	{
		long wide = 0;
		for (long j = first; j <= last; j++)
			if (3 * chunks[j].fHeight <= 2 * chunks[j].fWidth && chunks[j].fWidth > h6)
			{
				wide = 1;
				break;
			}
		if (!wide)
			return 0;
	}
	for (long j = first; j <= stk->fLastChunk; j++)
		chunks[j].f6C = 10;
	if (s != 0)
	{
		if (s + 1 >= strokeCount
			|| nodes[strokes[s + 1].fTopNode].x > (stk->fRight + stk->fLeft) / 2)
			result = S_FiveWithBar(staff, line, &strokes[s - 1], stk);
	}
	if (result != 0)
	{
		tag_STK* body = &strokes[s - 1];
		for (long j = body->fFirstChunk; j <= body->fLastChunk; j++)
			chunks[j].f6C = 10;
		LO_Add(lo, nodes, 1300, chunks[body->fFirstChunk].fFrom, chunks[stk->fLastChunk].fTo, 1605, 0x21);
		return result;
	}
	if (s + 1 < strokeCount)
	{
		result = S_FiveWithBar(staff, line, &strokes[s + 1], stk);
		if (result != 0)
		{
			tag_STK* body = &strokes[s + 1];
			for (long j = body->fFirstChunk; j <= body->fLastChunk; j++)
				chunks[j].f6C = 10;
			LO_Add(lo, nodes, 1300, chunks[stk->fFirstChunk].fFrom, chunks[body->fLastChunk].fTo, 1605, 0x22);
			return result;
		}
	}
	if (s != 0)
	{
		tag_STK* body = &strokes[s - 1];
		long r = S_SevenWithBar(staff, line, body, stk);
		if (r != 0)
		{
			for (long j = body->fFirstChunk; j <= body->fLastChunk; j++)
				chunks[j].f6C = 10;
			LO_Add(lo, nodes, 1300, chunks[body->fFirstChunk].fFrom, chunks[stk->fLastChunk].fTo, 1607, 0x23);
			return r;
		}
		r = S_CrossesBar(nodes, chunks, body, stk);
		if (r != 0)
		{
			for (long j = body->fFirstChunk; j <= body->fLastChunk; j++)
				chunks[j].f6C = 10;
			long from = chunks[body->fFirstChunk].fFrom;
			if (from == 0)
				LO_Add(lo, nodes, 1300, from, chunks[stk->fLastChunk].fTo, 1612, 0x23);
			else
				LO_Add(lo, nodes, 1300, chunks[stk->fFirstChunk].fFrom, chunks[stk->fLastChunk].fTo, 1613, 0x23);
			return r;
		}
	}
	if (s + 1 < strokeCount)
	{
		tag_STK* body = &strokes[s + 1];
		long r = S_CrossesBar(nodes, chunks, body, stk);
		if (r != 0)
		{
			for (long j = body->fFirstChunk; j <= body->fLastChunk; j++)
				chunks[j].f6C = 10;
			long from = chunks[stk->fFirstChunk].fFrom;
			if (from == 0)
				LO_Add(lo, nodes, 1300, from, chunks[body->fLastChunk].fTo, 1612, 0x22);
			return r;
		}
	}
	LO_Add(lo, nodes, 1600, chunks[stk->fFirstChunk].fFrom, chunks[stk->fLastChunk].fTo, 1613, 0x24);
	return 1;
}


// ROM 0x00291e08 (unnamed) - whether stroke a (one chunk, a line or an arc,
// taller than wide) crosses bar b in the middle both ways, as a +: b as
// tall as a third of a at most, the widest of its chunks flat, and a's
// start not running back against its way down
static long
S_CrossesBar(tag_wapx_type* nodes, tag_CHUNK* chunks, tag_STK* a, tag_STK* b)
{
	if (a->fLastChunk != a->fFirstChunk)
		return 0;
	tag_CHUNK* ca = &chunks[a->fFirstChunk];
	if (ca->f74 != 300 && ca->f74 != 400)
		return 0;
	long ha = a->fHeight;
	if (3 * a->fWidth > 2 * ha)
		return 0;
	long wb = b->fWidth;
	if (wb > 2 * ha)
		return 0;
	tag_CHUNK* widest = nil;
	for (long j = b->fFirstChunk; j <= b->fLastChunk; j++)
		if (widest == nil || chunks[j].fWidth > widest->fWidth)
			widest = &chunks[j];
	if (widest->f74 != 300 && widest->f74 != 400)
		return 0;
	long hb = b->fHeight;
	if (2 * hb - wb > wb / 6)
		return 0;
	if (2 * widest->fHeight - widest->fWidth > widest->fWidth / 6)
		return 0;
	if (3 * hb > ha)
		return 0;
	if (a->fRight > b->fRight)
		return 0;
	if (a->fLeft < b->fLeft)
		return 0;
	long mid = (a->fRight + a->fLeft) / 2;
	if (mid + wb / 4 > b->fRight)
		return 0;
	if (mid - wb / 4 < b->fLeft)
		return 0;
	if (b->fBottom > a->fBottom)
		return 0;
	if (b->fTop < a->fTop)
		return 0;
	long across = (b->fBottom + b->fTop) / 2;
	if (across + ha / 3 > a->fBottom)
		return 0;
	if (across - ha / 4 < a->fTop)
		return 0;
	long d = find_direct_backward(nodes, ca->fTo, 0, 0, ha / 8);
	if (ca->fDir >= d)
		return 1;
	if (ca->f78 == 402 && ca->fKind == 2)
		return 0;
	return 1;
}


// ROM 0x00292014 (unnamed) - whether stroke a is a 5's body under bar b: a
// short run of chunks from b's left end down, no wider than half as tall
// again, reaching from b to below it, its top on the right chunk
// (S_FiveBody)
static long
S_FiveWithBar(tag_CHUNK_STAFF* staff, SLine* line, tag_STK* a, tag_STK* b)
{
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	long h = a->fHeight;
	long w = a->fWidth;
	long dir = GetDirection(nodes[a->fTopNode].x, a->fBottom, b->fLeft, nodes[b->fLeftNode].y);
	if (direct_suits(dir, 2, 20))
		return 0;
	if (a->fRight - b->fLeft < 0)
		return 0;
	if (w > (3 * h) / 2 || h > 4 * w)
		return 0;
	if (3 * w < line->fHeight)
		return 0;
	if (a->fRight - b->fRight > (3 * line->fHeight) / 2)
		return 0;
	if (b->fLeft - a->fLeft > line->fHeight)
		return 0;
	long gap = a->fTop - b->fBottom;
	if (gap - h / 3 > h / 10)
		return 0;
	if (-gap > h / 4)
		return 0;
	long n = a->fLastChunk - a->fFirstChunk;
	if (n > 4)
		return 0;
	long nodesIn = chunks[a->fLastChunk].fTo - chunks[a->fFirstChunk].fFrom;
	if (nodesIn <= 3 || nodesIn >= 16)
		return 0;
	return S_FiveBody(nodes, chunks, a, b) != 0 ? 1 : 0;
}


// ROM 0x00292184 (unnamed) - whether stroke a's shape is a 5's body: the
// chunk its top is on (the last when it goes up, else the first; or the
// one after a corner) most of its height and an arc or S, or a short top
// with the bowl two chunks on; its top where the bar starts
static long
S_FiveBody(tag_wapx_type* nodes, tag_CHUNK* chunks, tag_STK* a, tag_STK* b)
{
	long up = 0;
	long first = a->fFirstChunk, last = a->fLastChunk;
	long count = last - first + 1;
	tag_wapx_type* top = &nodes[a->fTopNode];
	long ci = top->f18;
	tag_CHUNK* c;
	if (ci > 0)
	{
		ci = ci - 1;
		c = &chunks[ci];
		if (c->fKind == 1)
		{
			up = 1;
			if (last != ci)
				return 0;
		}
		else if (ci != first)
			return 0;
	}
	else
	{
		ci = -ci;
		count = count - 1;
		c = &chunks[ci];
		if (c->fKind == 1)
			return 0;
		if (first + 1 != ci)
			return 0;
	}
	long h = a->fHeight;
	if (h - c->fHeight < h / 4)
	{
		if (count > 2)
			return 0;
		long sub = c->f78;
		if (sub != 501)
		{
			bool check;
			if (c->f74 == 400)
				check = sub == 702;
			else
			{
				if (sub != 702)
					return 0;
				check = true;
			}
			if (check)
			{
				if (HWRAbs(b->fLeft - top->x) > b->fWidth / 8)
					return 0;
				if ((top->fDir & 0xff00) == 0 && (top->fFlags & 1) == 0)
					return 0;
				long leftY = nodes[b->fLeftNode].y;
				if (a->fTop - leftY > h / 4 + h / 10)
					return 0;
				if (leftY - a->fTop > h / 8)
					return 0;
			}
		}
		if (c->f74 == 400)
		{
			if (nodes[c->fTopNode].x - b->fLeft > b->fWidth / 4)
				return 0;
			if (up == 0)
			{
				if (c->f78 != 401)
					return 0;
			}
			else if (c->f78 != 402)
				return 0;
		}
		if (count <= 1)
			return 1;
		if (up == 0)
			return c->fHeight < 2 * chunks[a->fLastChunk].fHeight ? 0 : 1;
		return c->fHeight < 2 * chunks[a->fFirstChunk].fHeight ? 0 : 1;
	}
	if (up != 0)
		return 0;
	if (count < 3)
		return 0;
	tag_CHUNK* c2 = &chunks[ci + 2];
	// ROM BUG (fixed): the stroke's height less the bowl's foot less the
	// top's top, where the height less the distance between them was
	// meant.  The fix takes the distance, c2's bottom less c's top.
	if (RomBugFixed())
	{
		if (h - (c2->fBottom - c->fTop) > h / 8)
			return 0;
	}
	else if (h - c2->fBottom - c->fTop > h / 8)
		return 0;
	if (c2->f78 != 401)
		return 0;
	if (c2->fTop - c->fTop < c->fHeight / 3)
		return 0;
	if (c2->fNext == -1)
		return 1;
	if (chunks[ci + 3].fHeight > c2->fHeight)
		return 0;
	return 1;
}


// ROM 0x002924b4 (unnamed) - whether stroke a is a 7's stem under bar b: one
// or two chunks ending going down (a line after something, or an arc),
// from the bar's left end down and to the left, as tall as a third of the
// line and reaching its foot
static long
S_SevenWithBar(tag_CHUNK_STAFF* staff, SLine* line, tag_STK* a, tag_STK* b)
{
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	long w = a->fWidth;
	tag_CHUNK* c0 = &chunks[a->fFirstChunk];
	tag_CHUNK* c1 = &chunks[a->fLastChunk];
	long h = a->fHeight;
	long span = a->fLastChunk - a->fFirstChunk;
	if (span > 1)
		return 0;
	if (span == 1 && c1->fHeight < 3 * c0->fHeight)
		return 0;
	if (c1->fKind != 2)
		return 0;
	if (c1->f78 != 401)
	{
		if (c1->f78 != 301)
			return 0;
		if (c0 == c1)
			return 0;
	}
	if (a->fRight - b->fLeft < 0)
		return 0;
	if (b->fLeft - a->fLeft > line->fHeight)
		return 0;
	long dir = GetDirection(nodes[a->fBottomNode].x, a->fBottom, b->fLeft, nodes[b->fLeftNode].y);
	if (!direct_suits(dir, 22, 5))
		return 0;
	long back = find_direct_backward(nodes, c1->fTo, 0, h / 3, 0);
	if (!direct_suits(back, 9, 14))
		return 0;
	long lean = 12 - back;
	if (lean > 0 && dir >= 22)
		dir = dir + lean - 24;
	if (dir < 0 || dir > 20)
		return 0;
	long drop = a->fBottom - nodes[b->fLeftNode].y;
	if (7 * h < 10 * drop || 5 * drop < h)
		return 0;
	if (3 * w < line->fHeight)
		return 0;
	if (3 * (nodes[a->fTopNode].x - c0->fX0) < line->fHeight)
		return 0;
	return 1;
}


#pragma mark - the small marks

// ROM 0x0029698c (unnamed) - whether the line c of stroke s (not the first
// or last) is a solidus: no wider than half as tall again, taller by a
// quarter than the strokes either side, the stroke before it wholly to its
// left and the one after wholly to its right, each at the line's height
static long
S_Slash(tag_CHUNK_STAFF* staff, long s, tag_CHUNK* c, SLine* line)
{
	tag_wapx_type* nodes = staff->fNodes;
	tag_STK* strokes = staff->fStrokes;
	long count = staff->fStrokeCount;
	tag_STK* stk = &strokes[s];
	if (s == 0)
		return 0;
	long next = s + 1;
	if (next >= count)
		return 0;
	long h = stk->fHeight;
	long nh = strokes[s + 1].fHeight;
	long side = strokes[s - 1].fHeight;
	if (side <= nh)
		side = nh;
	if (stk->fWidth > (3 * h) / 2)
		return 0;
	if (h - side < side / 4)
		return 0;
	if (next < count && strokes[s - 1].fBottom - strokes[s + 1].fTop < nh / 4)
		return 0;
	if (stk->fRight < strokes[s - 1].fRight)
		return 0;
	long x = x_in_line(c->fX0, c->fY0, c->fX1, c->fY1, nodes[strokes[s - 1].fRightNode].y);
	if (strokes[s - 1].fRight >= x)
		return 0;
	long j;
	for (j = s - 1; j >= 0; j--)
	{
		if (line->fHeight > (3 * strokes[j].fHeight) / 2)
			continue;
		if (line->fBottom - strokes[j].fBottom < line->fHeight / 4)
			break;
	}
	if (j >= 0 && h - strokes[j].fHeight < strokes[j].fHeight / 4)
		return 0;
	if (next >= count)
		return 1;
	if (stk->fLeft > strokes[s + 1].fLeft)
		return 0;
	x = x_in_line(c->fX0, c->fY0, c->fX1, c->fY1, nodes[strokes[s + 1].fLeftNode].y);
	if (strokes[s + 1].fLeft <= x)
		return 0;
	for (j = next; j < count; j++)
	{
		if (line->fHeight > (3 * strokes[j].fHeight) / 2)
			continue;
		if (line->fBottom - strokes[j].fBottom < line->fHeight / 4)
			break;
	}
	if (j < count && h - strokes[j].fHeight < strokes[j].fHeight / 4)
		return 0;
	return 1;
}


// ROM 0x00296470 (unnamed) - the small strokes, by the unused real chunk i:
// a stroke that starts at it and is small and low a dot (class 1600, value
// 1614, extra 0x18) or, taller, a comma (code 19, extra 0x19); a line down
// a solidus (code 16, extra 0x1c), low in the line a comma (extra 0x1d) or
// a dot (class 1600, extra 0x1e).  Its chunks are marked 10.  ==> how many
// chunks the stroke has less one, 0 with nothing read.
static long
S_SmallMarks(tag_CHUNK_STAFF* staff, long i, SLine* line)
{
	void* lo = staff->fLO;
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* c = &chunks[real[i]];
	long s = c->fStroke;
	tag_STK* stk = &staff->fStrokes[s];
	long hc = c->fHeight;
	long last = stk->fLastChunk;
	long first = stk->fFirstChunk;
	if (c->fPrev == -1)
	{
		long w = stk->fWidth;
		long h = stk->fHeight;
		bool dot = true;
		if (w >= 2 * h && w >= line->fHeight / 4)
			dot = false;
		if (dot)
		{
			long third = line->fHeight / 3;
			if (!(third > h && third > w))
				dot = false;
		}
		if (dot && 3 * w <= 2 * h && h >= line->fHeight / 4)
			dot = false;
		if (dot && line->fBottom + line->fHeight / 6 <= stk->fTop)
			dot = false;
		if (dot && last - first >= 3)
			dot = false;
		if (dot)
		{
			for (long j = first; j <= last; j++)
				chunks[j].f6C = 10;
			LO_Add(lo, nodes, 1600, chunks[first].fFrom, chunks[last].fTo, 1614, 0x18);
			return last - first;
		}
		if (last - first < 2 && 3 * w < 2 * h
			&& line->fBottom - line->fHeight / 3 < stk->fTop
			&& h > line->fHeight / 4
			&& (2 * line->fHeight) / 3 > h)
		{
			for (long j = first; j <= last; j++)
				chunks[j].f6C = 10;
			LO_Add(lo, nodes, 1300, chunks[first].fFrom, chunks[last].fTo, 1619, 0x19);
			return last - first;
		}
	}
	if (c->f78 != 301 || c->fKind != 2)
		return 0;
	if (last - first > 2)
		return 0;
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[real[i - 1]];
		long h6 = hc / 6;
		if (p->fHeight > h6 || p->fWidth > h6)
			return 0;
		if (p->fPrev != -1)
			return 0;
	}
	if (c->fNext != -1)
	{
		tag_CHUNK* n = &chunks[real[i + 1]];
		long h6 = hc / 6;
		if (n->fHeight > h6 || n->fWidth > h6)
			return 0;
		if (n->fNext != -1)
			return 0;
	}
	int32_t from = chunks[first].fFrom;
	int32_t to = chunks[last].fTo;
	if (S_Slash(staff, s, c, line))
	{
		for (long j = first; j <= last; j++)
			chunks[j].f6C = 10;
		LO_Add(lo, nodes, 1300, from, to, 1616, 0x1c);
		return last - first;
	}
	long mid = line->fTop + line->fHeight / 2;
	if (mid < c->fTop && line->fBottom + line->fHeight / 6 < c->fBottom)
	{
		LO_Add(lo, nodes, 1300, from, to, 1619, 0x1d);
		for (long j = first; j <= last; j++)
			chunks[j].f6C = 10;
		return last - first;
	}
	if (mid >= c->fTop)
		return 0;
	if (line->fBottom + line->fHeight / 6 < c->fBottom)
		return 0;
	LO_Add(lo, nodes, 1600, from, to, 1614, 0x1e);
	for (long j = first; j <= last; j++)
		chunks[j].f6C = 10;
	return last - first;
}
