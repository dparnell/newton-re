/*
	File:		ChunkSearchL.cpp

	Contains:	SearchDigit_L, the digit searcher that starts from a chunk
				going down - the 2, 3, 4, 5, 7 and 9 whose main stroke is a
				curve or line down (class 500, value 501), and the $ -
				each digit it finds put in the list of low objects as class
				1300 with the digit (plus 1400) as its value and how it was
				found (1 whole, 2 with a bar, 3 from a bar) as its extra.
				See Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x0028dd18-0x0028eb9c, 0x0028f17c-0x0028f478,
				0x0028ffe8-0x00290ed8 in part); each function cites its
				origin.  All of them are from the disassembly: the
				decompiler drops the arguments of several divisions and
				gets the order of more than one test wrong.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRAbs
#include <string.h>


// ROM 0x00290de8 (unnamed) - a digit found: class 1300 over its nodes,
// the digit plus 1400 its value, how it was found its extra (the
// character code the callers pass is not used)
static long
AddDigit(void* lo, tag_wapx_type* n, long from, long to, long digit, long /*ch*/, long kind)
{
	return LO_Add(lo, n, 0x514, from, to, (ULong) (digit + 0x578), kind);
}


#pragma mark - bars beside

// ROM 0x0028e09c (unnamed) - whether the stroke before stroke k is a bar:
// no taller than it is wide, at most half k's height (and a bit), of one,
// two or three chunks, and of three only when the middle one - the jump -
// is the widest
static long
PrevStrokeIsBar(tag_CHUNK* chunks, tag_STK* strokes, long k)
{
	if (k == 0)
		return 0;
	tag_STK* a = &strokes[k - 1];
	if (a->fHeight > a->fWidth)
		return 0;
	if (a->fHeight * 2 - strokes[k].fHeight > strokes[k].fHeight / 8)
		return 0;
	long d = a->fLastChunk - a->fFirstChunk;
	if (d > 2)
		return 0;
	if (d == 0 || d != 2)
		return 1;
	int32_t mid = chunks[a->fFirstChunk + 1].fWidth;
	if (chunks[a->fFirstChunk].fWidth > mid || chunks[a->fLastChunk].fWidth > mid)
		return 0;
	return 1;
}


// ROM 0x00290e2c (unnamed) - the same of the stroke after stroke k
static long
NextStrokeIsBar(tag_CHUNK* chunks, tag_STK* strokes, long count, long k)
{
	if (k + 1 >= count)
		return 0;
	tag_STK* b = &strokes[k + 1];
	if (b->fHeight > b->fWidth)
		return 0;
	long d = b->fLastChunk - b->fFirstChunk;
	if (d > 2)
		return 0;
	if (b->fHeight * 2 - strokes[k].fHeight > strokes[k].fHeight / 8)
		return 0;
	if (d == 0 || d != 2)
		return 1;
	int32_t mid = chunks[b->fFirstChunk + 1].fWidth;
	if (chunks[b->fFirstChunk].fWidth > mid || chunks[b->fLastChunk].fWidth > mid)
		return 0;
	return 1;
}


// ROM 0x0028e224 (unnamed) - whether stroke a sits on stroke b's top left,
// as a 5's bar does: its left end at b's top (or no further right than
// its own middle), its leftmost point and its bottom within a fifth of
// b's height of b's top
static long
StrokeAtTopLeft(tag_CHUNK_STAFF* staff, tag_STK* a, tag_STK* b)
{
	tag_wapx_type* n = staff->fNodes;
	int32_t tol = b->fHeight / 5;
	int32_t yLeftA = n[a->fLeftNode].y;
	int32_t xTopB = n[b->fTopNode].x;
	if (HWRAbs(a->fLeft - xTopB) > tol)
	{
		if (xTopB < a->fLeft)
			return 0;
		if (a->fLeft + a->fWidth / 2 < xTopB)
			return 0;
	}
	if (HWRAbs(yLeftA - b->fTop) <= tol && a->fBottom - b->fTop <= tol)
		return 1;
	return 0;
}


// ROM 0x0028e140 (unnamed) - a 5's bar written as a stroke of its own,
// after the chunk's stroke or before it: the digit then runs to the
// bar's end (or from its start).
static long
FindFiveBar(tag_CHUNK_STAFF* staff, long idx, int32_t* from, int32_t* to)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	long k = chunks[idx].fStroke;
	tag_STK* s = &strokes[k];
	if (NextStrokeIsBar(chunks, strokes, staff->fStrokeCount, k) && StrokeAtTopLeft(staff, s + 1, s))
	{
		*to = chunks[s[1].fLastChunk].fTo;
		return 1;
	}
	if (PrevStrokeIsBar(chunks, strokes, k) && StrokeAtTopLeft(staff, s - 1, s))
	{
		*from = chunks[s[-1].fFirstChunk].fFrom;
		return 1;
	}
	return 0;
}


// ROM 0x0028e394 (unnamed) - whether stroke b is a 5's bar for stroke a
// (whose chunk c is): b no more than half a's height and near a's top,
// the faults it has - being tall for its width, being more than one line,
// reaching down, lying away from a's top - scored, and too many of them
// (more than 100: it reaches down, and one more) forgiven only when its
// start is near c's end across the line.
static long
BarBeside(tag_CHUNK_STAFF* staff, tag_CHUNK* c, tag_STK* a, tag_STK* b)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	long score = 0;
	long end = c->fTo - c->f91;
	int32_t wide = b->fWidth > a->fWidth ? b->fWidth : a->fWidth;
	int32_t xTop = n[a->fTopNode].x;
	if (a->fHeight < b->fHeight * 2)
		return 0;
	if ((b->fHeight * 3) / 2 > b->fWidth || b->fHeight * 3 > a->fHeight)
		score = 10;
	if (b->fLastChunk != b->fFirstChunk || chunks[b->fFirstChunk].f74 != 300)
		score += 10;
	int32_t mid = (a->fBottom + a->fTop) / 2;
	if (mid + a->fHeight / 8 < b->fBottom)
		return 0;
	if (mid + a->fHeight / 8 < b->fTop)
		return 0;
	if (b->fBottom - a->fTop > a->fHeight / 4)
		score += 100;
	if (b->fLeft < xTop)
	{
		if (xTop > b->fRight)
			return 0;
	}
	else
	{
		int32_t d = b->fLeft - xTop;
		if (d > wide / 2)
		{
			if (b->fLastChunk + 1 != staff->fChunkCount)
				return 0;
			if (d > wide)
				return 0;
			score += 10;
		}
		else if (d > wide / 3)
			score += 10;
	}
	if (score > 100)
	{
		tag_CHUNK* f = &chunks[b->fFirstChunk];
		int32_t dx = f->fX0 < f->fX1 ? f->fX0 - n[end].x : f->fX1 - n[end].x;
		if (dx >= wide / 2)
			return 0;
	}
	return 1;
}


// ROM 0x0028e2cc (unnamed) - a 5's bar in the stroke after the chunk's or
// the one before (BarBeside): the digit then runs to its end (or from its
// start).
static long
FindBarBeside(tag_CHUNK_STAFF* staff, long idx, int32_t* from, int32_t* to)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_CHUNK* c = &chunks[idx];
	long k = c->fStroke;
	tag_STK* s = &staff->fStrokes[k];
	if (k + 1 < staff->fStrokeCount && BarBeside(staff, c, s, s + 1))
	{
		*to = chunks[s[1].fLastChunk].fTo;
		return 1;
	}
	if (k > 0 && BarBeside(staff, c, s, s - 1))
	{
		*from = chunks[s[-1].fFirstChunk].fFrom;
		return 1;
	}
	return 0;
}


#pragma mark - 3, 5 and 9

// ROM 0x0028e7a0 (unnamed) - the chunk before a 3, 5 or 9's curve: a
// small stroke's start.  A level one leading into the curve makes a 3; a
// small one ending at the curve's start makes the 5 a 9 when it is an
// arc; one pointing up and right must be small.  ==> the digit, -1 when
// it cannot be one.
static long
CheckPrevForFive(tag_CHUNK_STAFF* staff, long idx, long digit, long a, long b)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* c = &staff->fChunks[idx];
	tag_CHUNK* p = c - 1;
	int32_t h = c->fHeight;
	int32_t half = h / 2;
	if (half + h / 8 < p->fHeight)
		return -1;
	if (half + h / 8 < p->fWidth)
		return -1;
	if (p->fPrev != -1)
		return -1;
	if (direct_suits(p->fDir, 17, 21) && (p->f74 == 300 || p->f78 == 401) && p->fWidth > p->fHeight
		&& c->fX0 - p->fX0 > h / 5)
	{
		long d = distance_between_directions(p->fDir, n[c->fFrom].fDirOut);
		if (d < 11)
			return 3;
		if (d == 11 && p->fDir >= 19 && n[c->fFrom].fDirOut + 12 > p->fDir)
			return 3;
	}
	if (digit == 5)
	{
		int32_t sixth = h / 6;
		if (p->fX0 - n[a].x < sixth && n[a].y - p->fY0 < sixth)
		{
			if (p->f78 != 402)
				return -1;
			if (n[b].x - p->fX0 > h / 5)
				return -1;
			return 9;
		}
	}
	if (direct_suits(p->fDir, 17, 23))
	{
		if (p->fWidth > half || p->fHeight > half)
			return -1;
		if (distance_between_directions(p->fDir, n[c->fFrom].fDirOut) < 11)
		{
			if (p->fWidth > h / 8 || p->fHeight > h / 8)
				return -1;
		}
	}
	if (digit == 5)
	{
		if (p->fX0 - p->fX1 > p->fHeight)
			return 5;
		if (p->fHeight > h / 4)
			return -1;
		if (p->fWidth > h / 4 && p->fX0 > p->fX1)
			return -1;
	}
	return digit;
}


// ROM 0x0028ea4c (unnamed) - the chunk after a 3, 5 or 9's curve: small,
// the stroke's last, and not going far right.  ==> the digit, -1 when it
// cannot be one.
static long
CheckNextForFive(tag_CHUNK_STAFF* staff, long idx, long digit)
{
	tag_CHUNK* c = &staff->fChunks[idx];
	tag_CHUNK* nx = c + 1;
	int32_t h = c->fHeight;
	if (h / 2 + h / 8 < nx->fHeight)
		return -1;
	if (nx->fWidth > (h * 3) / 4)
		return -1;
	if (nx->fNext != -1)
		return -1;
	if (nx->fX1 - nx->fX0 <= h / 4)
		return digit;
	return -1;
}


// ROM 0x0028e588 (unnamed) - a 3, 5 or 9 read from its curve down: 9 when
// its first turn is a corner well above the bottom, 5 otherwise, the
// chunks either side looked at (CheckPrevForFive, CheckNextForFive) and
// the two kept apart by a quarter of the height.  ==> the digit, *from
// and *to the nodes it runs over; -1 when it is none of them.
static long
SearchThreeFiveNine(tag_CHUNK_STAFF* staff, long idx, int32_t* from, int32_t* to)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* c = &staff->fChunks[idx];
	tag_CHUNK* before = nil;
	tag_CHUNK* after = nil;
	long a = c->fFrom + c->f90;
	long b = c->fTo - c->f91;
	int32_t h = c->fHeight;
	if (a == b && direct_suits(n[a].fDirOut, 17, 19))
		a++;
	int32_t dyEnd = c->fBottom - n[b].y;
	int32_t dyStart = c->fBottom - n[a].y;
	int32_t third = h / 3;
	if (third > dyEnd)
		return -1;
	*from = c->fFrom;
	*to = c->fTo;
	long digit = ((n[a].fDir & 0xff00) && third < dyStart) ? 9 : 5;
	if (h - dyEnd < h / 8)
	{
		if (digit == 9)
			return -1;
		if (third > c->fX0 - n[b].x)
			return -1;
	}
	if (c->fPrev != -1)
	{
		before = c - 1;
		digit = CheckPrevForFive(staff, idx, digit, a, b);
		if (digit == -1)
			return -1;
		*from = before->fFrom;
	}
	if (c->fNext != -1)
	{
		after = c + 1;
		digit = CheckNextForFive(staff, idx, digit);
		if (digit == -1)
			return -1;
		*to = after->fTo;
	}
	if (before == nil || after == nil)
		return digit;
	if (after->fTop - before->fBottom >= h / 4)
		return digit;
	return -1;
}


// ROM 0x0028ead0 (unnamed) - whether a 5 (from its curve alone) is not
// something else: its first line does not go right a long way and then
// turn back, and a stroke that starts at its top does not start going
// left and down.
static long
LooksLikeFive(tag_CHUNK_STAFF* staff, long idx)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* c = &staff->fChunks[idx];
	long bf = staff->fBrackets[c->fFirstBracket].fFrom;
	if (c->fX0 < n[c->fFrom + 1].x && c->fFrom < bf)
	{
		if (n[bf].x - c->fX0 > c->fHeight / 8 && n[bf].fDirOut > n[bf - 1].fDirOut - 12)
			return 0;
	}
	if (c->fTopNode == c->fFrom && c->fPrev == -1 && direct_suits(n[c->fFrom].fDirOut, 10, 14))
		return 0;
	return 1;
}


// ROM 0x0029082c (unnamed) - a 5 read from its bar: a chunk that does not
// go down, at least half the writing's height, alone or with only small
// neighbours, whose last step goes a long way left.  ==> 1, *from and *to
// the whole stroke's nodes; 0 when it is not one.
static long
BarOfFive(tag_CHUNK_STAFF* staff, long idx, int32_t* from, int32_t* to)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_CHUNK* c = &chunks[idx];
	int32_t h = c->fHeight;
	if (staff->fHeight > h * 2)
		return 0;
	if (c->fNext != -1)
	{
		tag_CHUNK* nx = c + 1;
		if (nx->fNext != -1)
			return 0;
		if (h < nx->fHeight * 6 || h < nx->fWidth * 4)
			return 0;
	}
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = c - 1;
		if (p->fPrev != -1)
			return 0;
		if (h < p->fHeight * 6 || h < p->fWidth * 4)
			return 0;
	}
	long e = c->fTo, p = e - 1;
	if (HWRAbs(n[e].x - n[p].x) < h / 8 && HWRAbs(n[e].y - n[p].y) < h / 8)
	{
		e--;
		p--;
	}
	long dir = GetDirection(n[e].x, n[e].y, n[p].x, n[p].y);
	if (n[e].x - n[p].x <= h / 4 + h / 8)
		return 0;
	if (!direct_suits(dir, 5, 7))
		return 0;
	tag_STK* s = &staff->fStrokes[c->fStroke];
	*from = chunks[s->fFirstChunk].fFrom;
	*to = chunks[s->fLastChunk].fTo;
	return 1;
}


#pragma mark - 4 and 9

// ROM 0x002909f0 (unnamed) - whether a chunk going down is a 4's (or a
// 9's) upright and the turn at its foot: tall against the chunk after it,
// its first turn going down no more than across, its bottom a node or two
// after that turn and between a quarter and seven eighths of its height
// below it.  ROM QUIRK: when the chunk after is between a quarter and a
// half of its size, the code asks whether that chunk points up and right
// and throws the answer away - the chunk is refused either way.
static long
LooksLikeFour(tag_CHUNK_STAFF* staff, long idx)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_CHUNK* c = &chunks[idx];
	long k = c->fStroke;
	int32_t h = c->fHeight;
	if (c->fKind != 2)
		return 0;
	if (c->fNext != -1)
	{
		tag_CHUNK* nx = c + 1;
		if (h < nx->fWidth * 2 || h < nx->fHeight * 2)
			return 0;
		if (!(h >= nx->fWidth * 4 && h >= nx->fHeight * 4))
		{
			if (!(n[c->fTo].fDir & 0xff00))
				return 0;
			if (nx->f74 != 300)
				return 0;
			direct_suits(nx->fDir, 21, 3);
			return 0;
		}
	}
	long a = c->fFrom + c->f90;
	long b = c->fTo - c->f91;
	int32_t xa = n[a].x, ya = n[a].y;
	if (ya - n[b].y > xa - n[b].x)
		return 0;
	long bot = c->fBottomNode;
	if (bot <= a || bot - a > 2)
		return 0;
	int32_t depth = c->fBottom - ya;
	if (HWRAbs(xa - c->fX1) > (depth * 3) / 2)
		return 0;
	if (bot - a > 1)
	{
		if (c->fBottom - n[bot - 1].y < (depth * 2) / 3 && n[bot - 1].x - n[bot].x > depth / 4)
			return 0;
	}
	if ((h * 3) / 4 + h / 8 < depth)
		return 0;
	if (depth < h / 4)
		return 0;
	if (!(n[bot].fFlags & kApxEnd) && !(n[bot].fDir & 0xff00))
	{
		if (direct_suits(n[bot].fDirOut, 2, 8))
			return 0;
	}
	long first = staff->fStrokes[k].fFirstChunk;
	long d = idx - first;
	if (d > 2)
		return 0;
	if (d <= 0)
		return 1;
	if (c->fRight - chunks[first].fLeft > c->fHeight)
		return 0;
	if (ya + c->fY1 < chunks[idx - 1].fBottom * 2)
		return 0;
	return 1;
}


// ROM 0x00290c68 (unnamed) - a 4 or a 9: the chunk before reaching right
// of this one's start without a corner makes it a 9; a straight first
// bracket or a single step a 4; otherwise the direction from the start to
// the first node a sixth of the height away decides (up or right a 9,
// left a 4).  ==> 4, 9, -1 for neither.
static long
FourOrNine(tag_CHUNK_STAFF* staff, long idx)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* c = &staff->fChunks[idx];
	brack_type* br = &staff->fBrackets[c->fFirstBracket];
	int32_t h = c->fHeight;
	if (c->fPrev != -1)
	{
		int32_t d = staff->fChunks[idx - 1].fRight - c->fX0;
		if (h / 6 < d)
			return 9;
		if (d > h / 8 && !(n[c->fFrom].fDir & 0xff00))
			return 9;
		if (d > 0 && !(n[c->fFrom].fDir & 0xff00))
			return 9;
	}
	if (br->fKind == 1)
		return 4;
	if ((c->fTo - c->f91) - c->fFrom == 1)
		return 4;
	long k = take_next_point(n, br->fTo + 1, c->fFrom, 0, 0, h / 6);
	if (k == -1)
		return -1;
	long dir = GetDirection(c->fX0, c->fY0, n[k].x, n[k].y);
	if (direct_suits(dir, 0, 8))
		return 9;
	if (direct_suits(dir, 9, 15))
		return 4;
	return -1;
}


#pragma mark - 2 and 7

// ROM 0x0028f17c (unnamed) - a 2 or a 7 from the chunk going down: its
// first turn (past a step down and right) near the top and well right of
// its start, the line to it heading right, and from there down to the
// bottom left; a small stroke start before it may be the 7's top.  With
// nothing after it that is a 7; a line or curve going right a long way
// after its foot makes a 2, a small one a 7.  ==> 2, 7, or 0; *from and
// *to the nodes (ROM QUIRK: *from is written before the tests that may
// still refuse it).
static long
SearchTwoSeven(tag_CHUNK_STAFF* staff, long idx, int32_t* from, int32_t* to)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_CHUNK* c = &chunks[idx];
	bool bent = false;
	long a = c->fFrom + c->f90;
	int32_t h = c->fHeight;
	if (a + 1 < c->fTo && direct_suits(n[a].fDirOut, 13, 18))
	{
		a = a + 1;
		bent = true;
	}
	int32_t rise = 0;
	int32_t v;
	if (c->fTopNode >= a)
		v = HWRAbs(n[a].y - c->fY0);
	else
	{
		int32_t ya = n[a].y;
		v = ya - c->fTop;
		rise = v;
		if (a - 1 > c->fFrom && n[a - 1].y > ya)
			rise = n[a - 1].y - c->fTop;
		if (bent && a - 2 > c->fFrom && n[a - 2].y > ya && n[a - 2].y > n[a - 1].y)
			rise = n[a - 2].y - c->fTop;
	}
	int32_t dx = n[a].x - (c->fLeftNode < a ? c->fLeft : c->fX0);
	if (HWRAbs(v) > (dx * 2) / 3)
		return 0;
	if (h < rise * 4)
		return 0;
	int32_t below = c->fBottom - n[a].y;
	int32_t across = n[a].x - c->fX0;
	if (c->fBottomNode - a > 2)
		return 0;
	if (!direct_suits(GetDirection(c->fX0, c->fY0, n[a].x, n[a].y), 15, 19))
		return 0;
	int32_t start;
	if (c->fPrev == -1)
		start = c->fFrom;
	else
	{
		tag_CHUNK* p = c - 1;
		if (p->fPrev != -1)
			return 0;
		if (p->fHeight * 2 - h > h / 8)
			return 0;
		start = p->fFrom;
	}
	*from = start;
	if (h / 5 > across)
		return 0;
	if (below * 2 - h < h / 8)
		return 0;
	if (c->fNext == -1)
	{
		*to = c->fTo;
		return 7;
	}
	tag_CHUNK* nx = &chunks[c->fNext];
	if (nx->f78 == 401 || (nx->f74 == 300 && nx->fWidth > nx->fHeight))
	{
		int32_t t, xe;
		if (nx->fNext == -1)
		{
			t = nx->fTo;
			xe = nx->fX1;
		}
		else
		{
			tag_CHUNK* nn = &chunks[nx->fNext];
			if (nn->fNext != -1)
				return 0;
			if (h < nn->fHeight * 2)
				return 0;
			xe = nn->fX1;
			t = nn->fTo;
		}
		*to = t;
		if (xe - c->fX1 > h / 4 && h > nx->fHeight * 2)
			return 2;
	}
	if (nx->fNext != -1)
		return 0;
	if (h >= nx->fHeight * 4 && h >= nx->fWidth * 6)
	{
		*to = chunks[idx + 1].fTo;
		return 7;
	}
	return 0;
}


#pragma mark - $

// ROM 0x00290668 (unnamed) - whether stroke k is a $'s upright: one chunk
// going down (after at most a small start), about as tall as the stroke
// and no wider than a sixth of it, any small start or end hooked on at a
// corner, and a line or a flat arc.  ==> 1, *chunk the chunk.
static long
StrokeIsUpright(tag_CHUNK_STAFF* staff, long k, int32_t* chunk)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* t = &staff->fStrokes[k];
	tag_CHUNK* before = nil;
	tag_CHUNK* after = nil;
	int32_t h = t->fHeight;
	long i = t->fFirstChunk;
	long count = t->fLastChunk - t->fFirstChunk + 1;
	tag_CHUNK* c = &chunks[i];
	if (c->fKind != 2)
	{
		if (count < 2)
			return 0;
		before = c;
		c++;
		i++;
	}
	*chunk = (int32_t) i;
	if (c->fNext != -1)
		after = &chunks[c->fNext];
	if (c->fKind != 2)
		return 0;
	int32_t sixth = h / 6;
	if (c->fWidth - c->fHeight > sixth)
		return 0;
	if (h - c->fHeight > h / 10)
		return 0;
	if (before != nil)
	{
		if (before->fWidth > sixth)
			return 0;
		if (before->fHeight > h / 3)
			return 0;
		if (before->fHeight > sixth)
		{
			if (before->f74 != 300)
				return 0;
			if (!(n[c->fFrom].fDir & 0xff00))
				return 0;
		}
	}
	if (after != nil)
	{
		if (after->fWidth > sixth)
			return 0;
		if (after->fHeight > h / 3)
			return 0;
		if (after->fHeight > sixth)
		{
			if (after->f74 != 300)
				return 0;
			if (!(n[c->fTo].fDir & 0xff00))
				return 0;
		}
	}
	if (c->f74 == 300)
		return 1;
	if (c->f74 != 400)
		return 0;
	if (c->fLength2 < c->fBulge * 9)
		return 0;
	return 1;
}


// ROM 0x0028ffe8 (unnamed) - a $: an S (the chunk, going down and left
// then right, in a stroke of one to three chunks near the start or end of
// the writing) with one or two uprights through it (StrokeIsUpright,
// crossing it or lying across its middle), all the strokes between
// accounted for.  ROM BUG: the test of the chunk after it asks whether a
// comparison's answer (0 or 1) is more than an eighth of the height,
// where the one before compares a width - so it never refuses anything.
// ==> 1, *from and *to the whole $'s nodes.
static long
SearchDollar(tag_CHUNK_STAFF* staff, long idx, int32_t* from, int32_t* to)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	brack_type* brackets = staff->fBrackets;
	tag_STK* strokes = staff->fStrokes;
	long count = staff->fStrokeCount;
	tag_CHUNK* c = &chunks[idx];
	long s = c->fStroke;
	long middle = -1, first = -1, second = -1, rightOf = -1, leftOf = -1;
	tag_STK* S = &strokes[s];
	if (staff->fHeight > S->fHeight * 4)
		return 0;
	if (S->fWidth > (S->fHeight * 3) / 2)
		return 0;
	if (!direct_suits(c->fDir, 8, 15))
		return 0;
	if (S->fLastChunk - S->fFirstChunk > 2)
		return 0;
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = c - 1;
		if (p->fPrev != -1)
			return 0;
		if (c->fHeight < (p->fHeight * 3) / 2)
			return 0;
		if ((p->fWidth * 3) / 2 - c->fHeight > c->fHeight / 8)
			return 0;
	}
	if (c->fNext != -1)
	{
		tag_CHUNK* nx = c + 1;
		if (nx->fNext != -1)
			return 0;
		if (c->fHeight < (nx->fHeight * 3) / 2)
			return 0;
		long wider = c->fHeight < (nx->fWidth * 3) / 2 ? 1 : 0;
		if (wider > c->fHeight / 8)
			return 0;
	}
	brack_type* br = &brackets[c->fFirstBracket];
	int32_t d = c->fBottom - n[br->fTo].y;
	if (c->fHeight > d * 6)
		return 0;
	if (c->fHeight > d * 4 && br[1].fKind != 2)
		return 0;
	long lo = s - 2;
	if (lo <= 0)
		lo = 0;
	long hi = s + 3;
	if (hi >= count)
		hi = count;
	if (lo != 0 && hi != count)
		return 0;
	int32_t sLeft = S->fLeft, sRight = S->fRight, sTop = S->fTop, sBottom = S->fBottom;
	int32_t sHeight = S->fHeight, sWidth = S->fWidth;
	int32_t mid = (sBottom + sTop) / 2;
	for (long k = lo; k < hi; k++)
	{
		if (k == s)
			continue;
		tag_STK* T = &strokes[k];
		if (T->fLastChunk - T->fFirstChunk > 2)
			continue;
		if (!((T->fRight > sLeft && T->fRight < sRight) || (T->fLeft > sLeft && T->fLeft < sRight)))
			continue;
		int32_t bar;
		if (!StrokeIsUpright(staff, k, &bar))
			continue;
		long x = x_in_curve(n, &chunks[bar], mid);
		if (T->fRight > sRight && x - sRight > sHeight / 6)
			continue;
		if (T->fLeft < sLeft && sLeft - x > sHeight / 6)
			continue;
		if (k < s && T->fLeft < sLeft && sLeft > x)
			continue;
		if (sHeight > (T->fHeight * 3) / 2)
			continue;
		int32_t third = sHeight / 3;
		if (sBottom - T->fBottom > third || T->fTop - sTop > third)
			continue;
		bool through = !(sRight - x < sWidth / 4) && !(x - sLeft < sWidth / 4);
		if (!through && !cross_with_line(n, c, n[T->fTopNode].x, T->fTop, n[T->fBottomNode].x, T->fBottom))
		{
			if (sRight <= x || sLeft >= x)
				continue;
			if (k >= s)
				rightOf = k;
			else
				leftOf = k;
			continue;
		}
		if (first == -1)
			first = k;
		else
		{
			second = k;
			break;
		}
	}
	if (first == -1)
		return 0;
	long a = first, z = s;
	if (a >= z)
	{
		a = s;
		z = first;
	}
	if (second != -1)
	{
		if (second < a)
		{
			middle = a;
			a = second;
		}
		else if (second <= z)
			middle = second;
		else
		{
			middle = z;
			z = second;
		}
	}
	if (middle == -1)
	{
		if (z - a != 1)
		{
			if (leftOf == -1 && rightOf == -1)
				return 0;
			if (a + 1 == leftOf)
				middle = leftOf;
			else if (a + 1 == rightOf)
				middle = rightOf;
			else
				return 0;
			if (z - a != 2)
				return 0;
		}
	}
	else if (z - a != 2)
		return 0;
	if (a != 0 && count - 1 != z)
		return 0;
	*from = chunks[strokes[a].fFirstChunk].fFrom;
	*to = chunks[strokes[z].fLastChunk].fTo;
	long f = strokes[a].fFirstChunk;
	long l = strokes[z].fLastChunk;
	tag_CHUNK* C1 = &chunks[f];
	tag_CHUNK* C3 = &chunks[l];
	if (l - f == 2 && C1->f78 == 501 && C3->f78 == 301 && C1->fKind == 2 && C3->fKind == 2)
	{
		if (C3->fBottom - C1->fBottom > C3->fHeight / 4 && C1->fDir > 10
			&& C1->fTo - C1->f91 == C1->fFrom + 1)
		{
			if (n[C1->fFrom + 1].y - C1->fY0 > C3->fHeight / 3)
				return 0;
		}
	}
	return 1;
}


#pragma mark - the searcher

// ROM 0x0028ddb4 (unnamed) - the digits read from one chunk: a $ near the
// writing's ends, then (for a chunk going down at least half the
// writing's height, not much shorter than three times the next one) a 2
// or 7, a 5 with its bar, a 4 or 9, a 3, 5 or 9; a chunk that does not go
// down may be a 5's bar.  ==> 1 when one was put in the list.
static long
SearchDigitAt(tag_CHUNK_STAFF* staff, long idx)
{
	tag_wapx_type* n = staff->fNodes;
	void* lo = staff->fLO;
	tag_CHUNK* chunks = staff->fChunks;
	tag_CHUNK* c = &chunks[idx];
	int32_t from = -1, to = -1;
	long digit, ch, kind;
	if (c->fKind != 2)
	{
		if (!BarOfFive(staff, idx, &from, &to))
			return 0;
		FindBarBeside(staff, idx, &from, &to);
		kind = 3;
		goto five;
	}
	if (c->fStroke <= 2 || c->fStroke >= staff->fStrokeCount - 3)
	{
		if (SearchDollar(staff, idx, &from, &to))
		{
			digit = 0x15;
			ch = '$';
			kind = 1;
			goto add;
		}
	}
	if (staff->fHeight > c->fHeight * 2)
		return 0;
	if (c->fNext != -1 && chunks[idx + 1].fHeight * 3 - c->fHeight > c->fHeight / 6)
		return 0;
	{
		long r = SearchTwoSeven(staff, idx, &from, &to);
		if (r == 2)
		{
			digit = 2;
			ch = '2';
			kind = 1;
			goto add;
		}
		if (r == 7)
		{
			digit = 7;
			ch = '7';
			kind = 1;
			goto add;
		}
	}
	if (FindFiveBar(staff, idx, &from, &to))
	{
		if (from == -1)
		{
			if (c->fPrev == -1 || c->fHeight < (chunks[idx - 1].fHeight * 5) / 2)
				from = c->fFrom;
			else
				from = chunks[idx - 1].fFrom;
		}
		if (to == -1)
		{
			if (c->fNext == -1 || c->fHeight < chunks[idx + 1].fHeight * 2)
				to = c->fTo;
			else
				to = chunks[idx + 1].fTo;
		}
		kind = 2;
		goto five;
	}
	if (LooksLikeFour(staff, idx))
	{
		from = c->fPrev != -1 ? chunks[idx - 1].fFrom : c->fFrom;
		to = c->fNext != -1 ? chunks[idx + 1].fTo : c->fTo;
		long r = FourOrNine(staff, idx);
		if (r != -1)
		{
			if (r != 9)
			{
				digit = 4;
				ch = '4';
				kind = 1;
				goto add;
			}
			goto nine;
		}
	}
	{
		long r = SearchThreeFiveNine(staff, idx, &from, &to);
		if (r == 3)
		{
			digit = 3;
			ch = '3';
			kind = 1;
			goto add;
		}
		if (r == 5)
		{
			if (!FindBarBeside(staff, idx, &from, &to) && !LooksLikeFive(staff, idx))
				return 0;
			kind = 3;
			goto five;
		}
		if (r != 9)
			return 0;
	}
nine:
	digit = 9;
	ch = '9';
	kind = 1;
	goto add;
five:
	digit = 5;
	ch = '5';
add:
	AddDigit(lo, n, from, to, digit, ch, kind);
	return 1;
}


// ROM 0x0028dd18 SearchDigit_L__FP15tag_CHUNK_STAFF
// Each chunk of class 500 value 501 (a curve down and round) tried as the
// main stroke of a digit (SearchDigitAt).  ==> 1.
long
SearchDigit_L(tag_CHUNK_STAFF* staff)
{
	void* lo = staff->fLO;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* n = staff->fNodes;
	tag_LOWOBJ* obj = nil;
	LO_SetWorkClass(lo, 500);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		long r = LO_GetRealChunkInd(lo, chunks, n, obj, 1);
		if (obj->fValue == 501)
			SearchDigitAt(staff, real[r]);
	}
	return 1;
}
