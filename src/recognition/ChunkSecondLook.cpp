/*
	File:		ChunkSecondLook.cpp

	Contains:	The digit reader's second looks: the pass Digits runs once
				every searcher has had its turn, over the digits found in
				the order they were written left to right - twelve small
				corrections, each a rule about what a digit is taken for
				beside its neighbours (a low "1" between two digits is a
				comma, a lone slanting one a solidus, a "-" over the stroke
				before it that stroke's bar, a ")" with no "(" that is
				straight and short a "1", a 7 after a leftmost "(" a ")",
				a "<" and another "<" a guillemet, ...) - then ThreeToFive
				and RecognizeZCCW, then the digits and the gaps between
				them (the class-1200 objects) written out as class 1900
				in that order.  See Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x0029ce20-0x0029fbcc, 0x002a01dc-0x002a09b0); each
				function cites its origin.  All of it is from the
				disassembly: the decompiler loses the stack arrays the
				pass keeps the digits in, and the order of the arguments
				the twelve are called with.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRMemoryAlloc, HWRMemoryFree, HWRAbs
#include <string.h>


#pragma mark - helpers

// A digit object's digit: its value less 1300, modulo 100 (unsigned, as
// the ROM's __rt_udiv works it out - a value under 1300 wraps).
static inline ULong
DigitOf(const tag_LOWOBJ* obj)
{
	return (uint32_t) (obj->fValue - 1300) % 100;
}


// The digits taken out (value 0xffff) dropped from the array, the rest
// moved up, the end cleared, *count the ones left.  Not a function in the
// ROM: written out at the end of five of the twelve.
static void
DropTakenOut(tag_LOWOBJ** sorted, long* count)
{
	long n = *count, kept = 0;
	for (long i = 0; i < n; i++)
		if (sorted[i]->fValue != 0xffff)
			sorted[kept++] = sorted[i];
	for (long i = kept; i < n; i++)
		sorted[i] = nil;
	*count = kept;
}


static inline int32_t
Mul32(int32_t a, int32_t b)
{
	return (int32_t) ((uint32_t) a * (uint32_t) b);
}


// The chunk an object's k'th chunk is, among all of them.
static inline long
ObjChunk(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_LOWOBJ* obj, long k)
{
	return real[LO_GetRealChunkInd(lo, chunks, nodes, obj, k)];
}


#pragma mark - the twelve

// ROM 0x0029d900 (unnamed) - "1"s that are commas: a "1" (value 1301 or
// 1381) between two digits whose top is below both neighbours' top two
// fifths and whose bottom is below both their bottoms; a "1" before a
// digit more than twice its height, hanging a third of its own height or
// more below that digit's bottom; and the last digit, a "1" in one chunk
// written last, in the lower half of the digit before it (written just
// before it), below its bottom and shorter.  Each becomes 1319.  ==> 1.
static long
OnesToCommas(void* lo, tag_wapx_type* nodes, tag_CHUNK* chunks, int32_t* real, tag_LOWOBJ** sorted, long count, tag_STK* strokes, long strokeCount)
{
	long last = count - 1;
	for (long i = 1; last > i; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		if (obj->fValue != 1301 && obj->fValue != 1381)
			continue;
		tag_LOWOBJ* prev = sorted[i - 1];
		int32_t pb = prev->fBottom, pt = prev->fTop;
		if (pt + (pb - pt) * 2 / 5 >= obj->fTop)
			continue;
		tag_LOWOBJ* next = sorted[i + 1];
		int32_t nb = next->fBottom, nt = next->fTop;
		if (nt + (nb - nt) * 2 / 5 >= obj->fTop)
			continue;
		if (obj->fBottom > pb && obj->fBottom > nb)
			obj->fValue = 1319;
	}
	long i = 0;
	for ( ; last > i; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		if (obj->fValue != 1301 && obj->fValue != 1381)
			continue;
		tag_LOWOBJ* next = sorted[i + 1];
		int32_t nh = next->fBottom - next->fTop;
		int32_t oh = obj->fBottom - obj->fTop;
		if (nh > oh * 2 && (obj->fBottom - next->fBottom) * 3 > oh)
			obj->fValue = 1319;
	}
	tag_LOWOBJ* obj = sorted[i];
	if (obj->fValue != 1301 || LO_HowManyChunks(lo, obj) != 1)
		return 1;
	if (chunks[ObjChunk(lo, chunks, nodes, real, obj, 1)].fStroke != strokeCount - 1 || i <= 0)
		return 1;
	tag_LOWOBJ* prev = sorted[i - 1];
	ULong pd = DigitOf(prev);
	if (pd > 9)
		return 1;
	int32_t pb = prev->fBottom, pt = prev->fTop;
	if (obj->fTop <= (pt + pb) / 2 || obj->fBottom <= pb || obj->fBottom - obj->fTop >= pb - pt)
		return 1;
	long m = LO_HowManyChunks(lo, prev);
	for (long k = 1; k <= m; k++)
		if (chunks[ObjChunk(lo, chunks, nodes, real, prev, k)].fStroke == strokeCount - 2)
		{
			obj->fValue = 1319;
			break;
		}
	return 1;
}


// ROM 0x0029dd6c (unnamed) - "1"s that are solidi: a "1" (1301) of one
// straight chunk (class 300) slanting (direction 7..11) is 1316 when it is
// taller than the others - the tallest of the other digits but the
// brackets, solidi, percents, dollars and ones, else of any other digit,
// else of any other stroke - by a margin that grows with its slant.  With
// nothing to compare it with, it is a solidus if it slants less than 11;
// ROM QUIRK: and the pass stops there, the digits after it not looked at.
// ==> 1.
static long
OnesToSlashes(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* strokes, long strokeCount, tag_LOWOBJ** sorted, long count)
{
	for (long i = 0; i < count; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		if (obj->fValue != 1301 || LO_HowManyChunks(lo, obj) != 1)
			continue;
		long ci = ObjChunk(lo, chunks, nodes, real, obj, 1);
		tag_CHUNK* c = &chunks[ci];
		if (c->f74 != 300)
			continue;
		ULong dir = c->fDir;
		if (dir < 7 || dir > 11)
			continue;
		int32_t h = 0;
		for (long j = 0; j < count; j++)
		{
			if (j == i)
				continue;
			ULong d = DigitOf(sorted[j]);
			if (d == 16 || d == 10 || d == 11 || d == 17 || d == 21 || d == 1)
				continue;
			int32_t dh = sorted[j]->fBottom - sorted[j]->fTop;
			if (dh > h)
				h = dh;
		}
		if (h == 0)
			for (long j = 0; j < count; j++)
			{
				if (j == i)
					continue;
				int32_t dh = sorted[j]->fBottom - sorted[j]->fTop;
				if (dh > h)
					h = dh;
			}
		if (h == 0)
			for (long s = 0; s < strokeCount; s++)
			{
				if (s == c->fStroke)
					continue;
				if (strokes[s].fHeight > h)
					h = strokes[s].fHeight;
			}
		if (h == 0)
		{
			if (c->fDir < 11)
				obj->fValue = 1316;
			return 1;
		}
		Boolean slash;
		switch (dir)
		{
		case 7:		slash = c->fHeight > h * 3 / 4; break;
		case 8:		slash = c->fHeight >= h; break;
		case 9:		slash = c->fHeight - h >= h / 4; break;
		case 10:	slash = c->fHeight - h >= h / 3; break;
		default:	slash = c->fHeight - h >= h / 2; break;		// 11
		}
		if (slash)
			obj->fValue = 1316;
	}
	return 1;
}


// ROM 0x0029dbd8 (unnamed) - commas that are full stops: a comma (19)
// under three tenths of the height of the digit beside it (the shorter
// of the two when both are digits) becomes a full stop (14).  ==> 1.
static long
CommasToPoints(tag_LOWOBJ** sorted, long count)
{
	for (long i = 0; i < count; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		int32_t base = (int32_t) ((uint32_t) obj->fValue / 100 * 100);
		if (obj->fValue - base != 19)
			continue;
		long p = i > 0 ? i - 1 : -1;
		long n = count - 1 > i ? i + 1 : -1;
		long pd = p >= 0 ? (long) ((uint32_t) sorted[p]->fValue % 100) : -1;
		long nd = n > 0 ? (long) ((uint32_t) sorted[n]->fValue % 100) : -1;
		int32_t h;
		if (pd >= 0 && pd <= 9 && nd >= 0 && nd <= 9)
		{
			int32_t ph = sorted[p]->fBottom - sorted[p]->fTop;
			int32_t nh = sorted[n]->fBottom - sorted[n]->fTop;
			h = ph >= nh ? nh : ph;
		}
		else if (pd >= 0 && pd <= 9)
			h = sorted[p]->fBottom - sorted[p]->fTop;
		else if (nd >= 0 && nd <= 9)
			h = sorted[n]->fBottom - sorted[n]->fTop;
		else
			continue;
		if (obj->fBottom - obj->fTop < h * 3 / 10)
			obj->fValue = base + 14;
	}
	return 1;
}


// ROM 0x0029d808 (unnamed) - a "1" that is an opening bracket: the last
// "1" of value 1381 before a ")" with no "(" since the last pair, the ")"
// not followed by a "(", becomes 1310.  ==> 1.
static long
OnesToOpenBrackets(tag_LOWOBJ** sorted, long count)
{
	long one = -1;
	Boolean open = false;
	for (long i = 0; i < count; i++)
	{
		ULong d = DigitOf(sorted[i]);
		if (d == 10)
		{
			open = true;
			one = -1;
		}
		else if (d == 11)
		{
			if (one == -1 || open)
			{
				if (open)
				{
					one = -1;
					open = false;
				}
			}
			else
			{
				if (count - 1 > i && DigitOf(sorted[i + 1]) == 10)
					continue;
				sorted[one]->fValue = 1310;
				one = -1;
			}
		}
		else if (d == 81)
			one = i;
	}
	return 1;
}


// ROM 0x0029e30c (unnamed) - closing brackets that are "1"s: with no "("
// among the digits, a ")" (11) that is not the last, of one nearly
// straight chunk (its bulge squared under a twenty-fifth of its length
// squared), whose top is not a quarter of its neighbours' height above
// them and which is under 1.3 times that height, becomes 1301 - except
// the fifth digit when the first is a 1 or a solidus.  ==> 1.
static long
ClosesToOnes(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_LOWOBJ** sorted, long count)
{
	if (count <= 0)
		return 1;
	ULong first = DigitOf(sorted[0]);
	Boolean open = false;
	for (long i = 0; i < count; i++)
		if (DigitOf(sorted[i]) == 10)
		{
			open = true;
			break;
		}
	for (long i = 0; i < count; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		if (DigitOf(obj) != 11 || open)
			continue;
		if (i == 4 && (first == 1 || first == 16))
			continue;
		if (count - 1 == i || LO_HowManyChunks(lo, obj) != 1)
			continue;
		tag_CHUNK* c = &chunks[ObjChunk(lo, chunks, nodes, real, obj, 1)];
		if (Mul32(c->fBulge, 25) >= c->fLength2)
			continue;
		int32_t top, h;
		if (i != 0)
		{
			tag_LOWOBJ* p = sorted[i - 1];
			tag_LOWOBJ* n = sorted[i + 1];
			top = p->fTop >= n->fTop ? n->fTop : p->fTop;
			int32_t ph = p->fBottom - p->fTop, nh = n->fBottom - n->fTop;
			h = ph <= nh ? nh : ph;
		}
		else
		{
			top = sorted[1]->fTop;
			h = sorted[1]->fBottom - sorted[1]->fTop;
		}
		if (top - obj->fTop >= h / 4)
			continue;
		if (h * 13 > (obj->fBottom - obj->fTop) * 5 * 2)
			obj->fValue = 1301;
	}
	return 1;
}


// ROM 0x0029e530 (unnamed) - a 0 or a 6 that overlaps an eight of class
// 2200 (up to five of them are looked at) is taken out, and so is the
// eight, which takes its place in the array.  ==> 0 with no such eight,
// else 1.
static long
ZerosInEights(void* lo, tag_LOWOBJ** sorted, long count)
{
	tag_LOWOBJ* obj = nil;
	tag_LOWOBJ* eights[5];
	long n = 0;
	ULong was = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 2200);
	for (long more = LO_PickFirst(lo, &obj); more; )
	{
		if (DigitOf(obj) == 8)
			eights[n++] = obj;
		if (!LO_PickNext(lo, &obj) || n == 5)
			break;
	}
	LO_SetWorkClass(lo, was);
	if (n == 0)
		return 0;
	for (long i = 0; i < count; i++)
	{
		tag_LOWOBJ* d = sorted[i];
		ULong digit = DigitOf(d);
		if (digit != 0 && digit != 6)
			continue;
		for (long j = 0; j < n; j++)
		{
			int32_t from = d->fFrom > eights[j]->fFrom ? d->fFrom : eights[j]->fFrom;
			int32_t to = d->fTo < eights[j]->fTo ? d->fTo : eights[j]->fTo;
			if (to - from > 0)
			{
				d->fValue = 0xffff;
				sorted[i] = eights[j];
				eights[j]->fValue = 0xffff;
				break;
			}
		}
	}
	return 1;
}


// ROM 0x0029e048 (unnamed) - a 7 that is a closing bracket: with a "("
// the leftmost digit found and no ")" (nor a "(" and ")" facing the wrong
// way), the first 7 in one chunk becomes 1311 unless a stroke no digit
// uses is a lone arc (class 401) going down more than two thirds the
// bracket's height.  ==> 1 (0 with no digits or more than forty
// strokes).
static long
SevenToCloseBracket(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* strokes, long strokeCount, tag_LOWOBJ** sorted, long count)
{
	long closeAt = -1, openAt = -1, sevenAt = -1;
	Boolean haveOpen = false, haveClose = false, haveSeven = false;
	UByte used[40];
	if (count == 0 || strokeCount > 40)
		return 0;
	int32_t minLeft = sorted[0]->fLeft;
	for (long s = 0; s < 40; s++)
		used[s] = 0;
	for (long i = 0; i < count; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		if (obj->fValue == 0xffff)
			continue;
		int32_t left = obj->fLeft;
		if (left < minLeft)
			minLeft = left;
		ULong d = DigitOf(obj);
		if (d == 10)
		{
			haveOpen = true;
			openAt = i;
			if (haveClose && left < sorted[closeAt]->fRight)
				return 1;
		}
		else if (d == 11)
		{
			haveClose = true;
			closeAt = i;
			if (haveOpen && obj->fRight > sorted[openAt]->fLeft)
				return 1;
		}
		long m = LO_HowManyChunks(lo, obj);
		for (long k = 1; k <= m; k++)
			used[chunks[ObjChunk(lo, chunks, nodes, real, sorted[i], k)].fStroke] = 1;
		if (d == 7 && m == 1 && !haveSeven)
		{
			haveSeven = true;
			sevenAt = i;
		}
	}
	if (!haveOpen)
		return 1;
	tag_LOWOBJ* open = sorted[openAt];
	if (open->fLeft != minLeft || !haveSeven)
		return 1;
	for (long s = 0; s < strokeCount; s++)
	{
		if (used[s] || strokes[s].fFirstChunk != strokes[s].fLastChunk)
			continue;
		tag_CHUNK* c = &chunks[strokes[s].fFirstChunk];
		if (c->fKind != 2 || c->f78 != 401)
			continue;
		if (c->fHeight * 3 > (open->fBottom - open->fTop) * 2)
			return 1;
	}
	sorted[sevenAt]->fValue = 1311;
	return 1;
}


// ROM 0x0029d428 (unnamed) - a minus that is a bar: a "-" written as a
// stroke of its own straight after a stroke no digit was found in, when
// that stroke is taller than two thirds its width and the "-" is in its
// upper third, not a stroke's width to its right and not half its
// height above it - the bar of a 5 or a 7 - is taken out.  ==> 1, -1
// for want of memory.
static long
DropMinusBars(void* lo, tag_wapx_type* nodes, tag_CHUNK* chunks, int32_t* real, tag_LOWOBJ** sorted, long* count, tag_STK* strokes, long strokeCount)
{
	long n = *count;
	int32_t* mark = (int32_t*) HWRMemoryAlloc(strokeCount * 4);
	if (mark == nil)
		return -1;
	memset(mark, 0, strokeCount * 4);
	for (long k = 0; k < n; k++)
	{
		tag_LOWOBJ* obj = sorted[k];
		long m = LO_HowManyChunks(lo, obj);
		for (long c = 1; c <= m; c++)
		{
			long s = chunks[ObjChunk(lo, chunks, nodes, real, obj, c)].fStroke;
			mark[s] = DigitChar(DigitOf(obj)) == '-' ? (int32_t) (k + 2) : 1;
		}
	}
	for (long s = 0; s < strokeCount; s++)
	{
		int32_t v = mark[s];
		if (v < 2 || s <= 0 || mark[s - 1] != 0)
			continue;
		tag_STK* p = &strokes[s - 1];
		tag_STK* cur = &strokes[s];
		int32_t ph = p->fHeight;
		if (ph * 3 <= p->fWidth * 2)
			continue;
		if (cur->fBottom >= (p->fBottom + p->fTop) / 2)
			continue;
		if ((cur->fBottom + cur->fTop) / 2 >= p->fTop + ph / 3)
			continue;
		if (cur->fLeft - p->fRight >= cur->fWidth)
			continue;
		if (p->fTop - cur->fBottom >= ph / 2)
			continue;
		sorted[v - 2]->fValue = 0xffff;
	}
	DropTakenOut(sorted, count);
	HWRMemoryFree((Ptr) mark);
	return 1;
}


// ROM 0x0029e6bc (unnamed) - a 3 whose first chunk (a hook, class 1400,
// or two arcs, 702, turning twice) starts right and then turns sharply
// back left is taken out.  ==> 1.
static long
DropHookedThrees(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* strokes, long strokeCount, tag_LOWOBJ** sorted, long* count)
{
	long n = *count;
	for (long i = 0; i < n; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		if (DigitOf(obj) != 3)
			continue;
		LO_HowManyChunks(lo, obj);			// (the answer is not used)
		tag_CHUNK* c = &chunks[ObjChunk(lo, chunks, nodes, real, sorted[i], 1)];
		if (c->f74 != 1400 && !(c->f78 == 702 && c->f90 == 2))
			continue;
		long from = c->fFrom;
		if (from + 2 > c->fTo)
			continue;
		int32_t x0 = nodes[from].x, y0 = nodes[from].y;
		int32_t x1 = nodes[from + 1].x, y1 = nodes[from + 1].y;
		int32_t x2 = nodes[from + 2].x, y2 = nodes[from + 2].y;
		if (x1 <= x0 || x2 >= x1)
			continue;
		long d1 = GetDirection(x1, y1, x0, y0);
		long d2 = GetDirection(x1, y1, x2, y2);
		if (GetAngleBetweenTwoDir(d1, d2) < 1)
			sorted[i]->fValue = 0xffff;
	}
	DropTakenOut(sorted, count);
	return 1;
}


// ROM 0x0029eaac (unnamed) - guillemets: two ">"s (23) running, or two
// "<"s (24), become one of 1326 or 1325 over both, the first taken out.
// Then a ">" or "<" whose stroke shares a third of the width of the
// stroke before it or after it is taken out.  ROM QUIRK: the two flags
// are not cleared by a "<" after a ">", so "><" and "<>" keep looking.
// ==> 1.
static long
MergeGuillemets(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* strokes, long strokeCount, tag_LOWOBJ** sorted, long* count)
{
	long n = *count;
	Boolean less = false, greater = false;
	for (long i = 0; i < n; i++)
	{
		ULong d = DigitOf(sorted[i]);
		if (d == 23)
		{
			if (!greater)
			{
				greater = true;
				continue;
			}
		}
		else if (d == 24)
		{
			if (!less)
			{
				less = true;
				continue;
			}
		}
		else
			continue;
		tag_LOWOBJ* cur = sorted[i];
		tag_LOWOBJ* prev = sorted[i - 1];
		if ((less && d == 24) || (greater && d == 23))
		{
			prev->fValue = 0xffff;
			cur->fValue = (less && d == 24) ? 1325 : 1326;
			cur->fFrom = prev->fFrom;
			cur->fFirstPoint = prev->fFirstPoint;
			if (prev->fLeft < cur->fLeft)
				cur->fLeft = prev->fLeft;
			if (prev->fRight > cur->fRight)
				cur->fRight = prev->fRight;
			if (prev->fTop < cur->fTop)
				cur->fTop = prev->fTop;
			if (prev->fBottom > cur->fBottom)
				cur->fBottom = prev->fBottom;
		}
		less = false;
		greater = false;
	}
	for (long i = 0; i < n; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		ULong d = DigitOf(obj);
		if (d != 23 && d != 24)
			continue;
		LO_HowManyChunks(lo, obj);			// (the answer is not used)
		long s = chunks[ObjChunk(lo, chunks, nodes, real, sorted[i], 1)].fStroke;
		obj = sorted[i];
		if (s > 0)
		{
			tag_STK* p = &strokes[s - 1];
			if (p->fRight >= obj->fLeft && p->fLeft <= obj->fRight)
			{
				int32_t r = obj->fRight < p->fRight ? obj->fRight : p->fRight;
				int32_t l = obj->fLeft > p->fLeft ? obj->fLeft : p->fLeft;
				if ((r - l) * 3 >= p->fWidth)
				{
					obj->fValue = 0xffff;
					continue;
				}
			}
		}
		if (strokeCount - 1 > s)
		{
			tag_STK* x = &strokes[s + 1];
			if (x->fRight >= obj->fLeft && x->fLeft <= obj->fRight)
			{
				int32_t r = obj->fRight < x->fRight ? obj->fRight : x->fRight;
				int32_t l = obj->fLeft > x->fLeft ? obj->fLeft : x->fLeft;
				if ((r - l) * 3 >= x->fWidth)
					obj->fValue = 0xffff;
			}
		}
	}
	DropTakenOut(sorted, count);
	return 1;
}


// ROM 0x0029eeb4 (unnamed) - the digits checked against the strokes
// beside them that no digit was found in:
//   a 0 with such a stroke before or after it made of two arcs (class 402)
//     whose middle is inside the 0's stroke and a third of its height or
//     more is taken out - ROM QUIRK: and the rest of the 0s are not
//     looked at;
//   a 1 in one chunk after a small stroke of fewer than four chunks going
//     right (direction 13..16) that ends low beside it is taken out;
//   a 2 in one chunk (a curve down, 502) whose start is more than three
//     times as far from its end's turn as its end is from its start's
//     turn is taken out, and so is the digit after it when that is in the
//     next stroke, not a 0, and hangs half the 2's height below it -
//     ROM QUIRK: the last digit is never looked at, and not counted;
//   a 2 whose next stroke is one line (301) crossing it is taken out;
//   a 3, 7 or ")" in one stroke after a stroke whose first chunk is a line
//     down (301, direction 10..13) at least half its height, and which
//     crosses the line from the digit's rightmost point to the middle of
//     its ends, becomes a new digit over both strokes, "B" (1330) after a
//     3 and "D" (1331) otherwise, in the digit's place;
//   a 5 or 9 in one stroke with a taller-than-wide stroke after it no
//     digit uses, crossing its middle, is taken out.
// ==> 1, -1 for want of memory.
static long
CheckNeighbourStrokes(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* strokes, long strokeCount, tag_LOWOBJ** sorted, long* count)
{
	long n = *count;
	long twos = 0;
	tag_LOWOBJ* made = nil;
	UByte* used = (UByte*) HWRMemoryAlloc(strokeCount);
	if (used == nil)
		return -1;
	memset(used, 0, strokeCount);
	for (long i = 0; i < n; i++)
	{
		long m = LO_HowManyChunks(lo, sorted[i]);
		long first = ObjChunk(lo, chunks, nodes, real, sorted[i], 1);
		long last = ObjChunk(lo, chunks, nodes, real, sorted[i], m);
		for (long s = chunks[first].fStroke; s <= chunks[last].fStroke; s++)
			used[s] = 1;
	}
	long lastStroke = strokeCount - 1;
	for (long i = 0; i < n; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		ULong d = DigitOf(obj);
		if (d == 0)
		{
			long s = chunks[ObjChunk(lo, chunks, nodes, real, obj, 1)].fStroke;
			tag_STK* cur = &strokes[s];
			if (s != 0 && used[s - 1] == 0)
			{
				tag_STK* p = &strokes[s - 1];
				if (p->fLastChunk - p->fFirstChunk == 1 && chunks[p->fFirstChunk].f78 == 402 && chunks[p->fLastChunk].f78 == 402)
				{
					int32_t mid = (p->fLeft + p->fRight) / 2;
					if (mid < cur->fRight && mid > cur->fLeft && p->fHeight * 3 > cur->fHeight)
					{
						sorted[i]->fValue = 0xffff;
						break;
					}
				}
			}
			if (lastStroke > s && used[s + 1] == 0)
			{
				tag_STK* x = &strokes[s + 1];
				if (x->fLastChunk - x->fFirstChunk == 1 && chunks[x->fFirstChunk].f78 == 402 && chunks[x->fLastChunk].f78 == 402)
				{
					int32_t mid = (x->fLeft + x->fRight) / 2;
					if (mid < cur->fRight && mid > cur->fLeft && x->fHeight * 3 > cur->fHeight)
					{
						sorted[i]->fValue = 0xffff;
						break;
					}
				}
			}
		}
		else if (d == 1)
		{
			if (LO_HowManyChunks(lo, obj) != 1)
				continue;
			tag_CHUNK* c = &chunks[ObjChunk(lo, chunks, nodes, real, sorted[i], 1)];
			long s = c->fStroke;
			if (s == 0 || used[s - 1] == 1)
				continue;
			tag_STK* cur = &strokes[s];
			tag_STK* p = &strokes[s - 1];
			int32_t ch = c->fHeight;
			if ((cur->fLeft + cur->fRight) / 2 - ch >= p->fLeft)
				continue;
			if (cur->fBottom - p->fBottom <= ch / 5)
				continue;
			if (p->fBottom - p->fTop >= ch)
				continue;
			if (p->fLastChunk - p->fFirstChunk >= 4)
				continue;
			ULong dir = GetDirection(chunks[p->fFirstChunk].fX0, chunks[p->fFirstChunk].fY0, chunks[p->fLastChunk].fX1, chunks[p->fLastChunk].fY1);
			if (dir > 12 && dir < 17)
				sorted[i]->fValue = 0xffff;
		}
	}
	for (long i = 0; n - 1 > i; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		if (DigitOf(obj) != 2)
			continue;
		twos++;
		if (LO_HowManyChunks(lo, obj) != 1)
			continue;
		tag_CHUNK* c = &chunks[ObjChunk(lo, chunks, nodes, real, sorted[i], 1)];
		if (c->f78 != 502 || c->fKind != 2)
			continue;
		long s = c->fStroke;
		long from = c->fFrom;
		int32_t lx = c->fX0, ly = c->fY0;
		if (nodes[from + 1].x < lx)
		{
			lx = nodes[from + 1].x;
			ly = nodes[from + 1].y;
			if (nodes[from + 2].x < nodes[from + 1].x)
			{
				lx = nodes[from + 2].x;
				ly = nodes[from + 2].y;
			}
		}
		tag_wapx_type* a = &nodes[c->fTo - c->f91];
		int32_t dStart = Mul32(a->x - lx, a->x - lx) + Mul32(a->y - ly, a->y - ly);
		tag_wapx_type* b = &nodes[c->fFrom + c->f90];
		int32_t dEnd = Mul32(b->x - c->fX1, b->x - c->fX1) + Mul32(b->y - c->fY1, b->y - c->fY1);
		if (Mul32(dStart, 9) >= dEnd)
			continue;
		sorted[i]->fValue = 0xffff;
		twos--;
		LO_HowManyChunks(lo, sorted[i + 1]);		// (the answer is not used)
		if (chunks[ObjChunk(lo, chunks, nodes, real, sorted[i + 1], 1)].fStroke != s + 1)
			continue;
		tag_LOWOBJ* next = sorted[i + 1];
		if (DigitOf(next) != 0 && c->fHeight < (next->fBottom - sorted[i]->fBottom) * 2)
			next->fValue = 0xffff;
	}
	if (twos > 0)
		for (long i = 0; i < n; i++)
		{
			tag_LOWOBJ* obj = sorted[i];
			if (DigitOf(obj) != 2)
				continue;
			long s = chunks[ObjChunk(lo, chunks, nodes, real, obj, 1)].fStroke;
			if (lastStroke <= s)
				continue;
			tag_STK* x = &strokes[s + 1];
			if (x->fFirstChunk != x->fLastChunk)
				continue;
			tag_CHUNK* nc = &chunks[x->fFirstChunk];
			if (nc->fKind != 2 || nc->f78 != 301)
				continue;
			if (CheckQIntersec(nodes, sorted[i]->fFrom, sorted[i]->fTo, nc->fFrom, nc->fTo))
				sorted[i]->fValue = 0xffff;
		}
	for (long i = 0; i < n; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		ULong d = DigitOf(obj);
		if (d != 7 && d != 3 && d != 11)
			continue;
		long m = LO_HowManyChunks(lo, obj);
		long first = ObjChunk(lo, chunks, nodes, real, sorted[i], 1);
		long last = ObjChunk(lo, chunks, nodes, real, sorted[i], m);
		int32_t lastTo = chunks[last].fTo;
		long s = chunks[first].fStroke;
		if (s != chunks[last].fStroke || s == 0)
			continue;
		tag_STK* p = &strokes[s - 1];
		long pf = p->fFirstChunk;
		tag_CHUNK* pc = &chunks[pf];
		if (pc->fKind != 2)
			continue;
		if (sorted[i]->fBottom - sorted[i]->fTop >= pc->fHeight * 2)
			continue;
		if (pc->f78 != 301)
			continue;
		ULong dir = pc->fDir;
		if ((long) dir <= 9 || (long) dir >= 14)
			continue;
		if (pc->fNext != -1)
		{
			tag_CHUNK* nc = &chunks[pc->fNext];
			if (nc->fNext != -1)
				continue;
			if (Mul32(nc->fLength2, 9) >= pc->fLength2)
			{
				if (pc->fLength2 <= Mul32(nc->fLength2, 4))
					continue;
				if (HWRAbs(GetAngleBetweenTwoDir(nc->fDir, dir)) <= 9)
					continue;
			}
		}
		// the digit's own stroke's rightmost node, and the middle of its
		// first chunk's start and last chunk's end (the ROM reaches them
		// from the stroke before's record, +0x30 on)
		tag_STK* cur = &strokes[s];
		tag_wapx_type* r = &nodes[cur->fRightNode];
		int32_t mx = (chunks[cur->fFirstChunk].fX0 + chunks[cur->fLastChunk].fX1) / 2;
		int32_t my = (chunks[cur->fFirstChunk].fY0 + chunks[cur->fLastChunk].fY1) / 2;
		if (!CheckQIntersecXY(pc->fX0, pc->fY0, pc->fX1, pc->fY1, r->x, r->y, mx, my))
			continue;
		if (p->fBottom - sorted[i]->fBottom > (p->fBottom - p->fTop) >> 2)
			continue;
		ULong value = (d == 7 || d == 11) ? 1331 : 1330;
		long at = LO_Add(lo, nodes, 1300, chunks[pf].fFrom, lastTo, value, 0);
		if (LO_PickDirectInd(lo, at, &made))
			sorted[i] = made;
	}
	for (long i = 0; i < n; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		ULong d = DigitOf(obj);
		if (d != 5 && d != 9)
			continue;
		long m = LO_HowManyChunks(lo, obj);
		long first = ObjChunk(lo, chunks, nodes, real, sorted[i], 1);
		long last = ObjChunk(lo, chunks, nodes, real, sorted[i], m);
		long s = chunks[first].fStroke;
		if (s != chunks[last].fStroke || lastStroke <= s || used[s + 1] != 0)
			continue;
		tag_STK* x = &strokes[s + 1];
		if (x->fBottom - x->fTop <= x->fRight - x->fLeft)
			continue;
		obj = sorted[i];
		int32_t my = (obj->fTop + obj->fBottom) / 2;
		tag_wapx_type* t = &nodes[x->fTopNode];
		tag_wapx_type* b = &nodes[x->fBottomNode];
		if (CheckQIntersecXY(t->x, t->y, b->x, b->y, obj->fLeft, my, obj->fRight, my))
			sorted[i]->fValue = 0xffff;
	}
	DropTakenOut(sorted, count);
	HWRMemoryFree((Ptr) used);
	return 1;
}


// ROM 0x002a01dc (unnamed) - which of the letter table's variants of its
// digit a digit found was written as, a bit each (1, 2, 4, 8; 3 and 6 for
// either of two), or 0xff for any: worked out from its tallest chunk that
// is not a jump, the strokes it runs over, the chunks' classes and how it
// was found (fExtra, and the hundreds of its value).
static ULong
DigitVariant(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* strokes, tag_LOWOBJ* obj)
{
	int32_t maxH = 0;
	long best = -1;
	int32_t v = obj->fValue;
	ULong d = (uint32_t) (v - 1300) % 100;
	UByte ch = DigitChar(d);
	if (ch < '0' || ch > '9')
		return 0xff;
	long digit = ch - '0';
	int32_t base = v - (int32_t) d;
	int32_t extra = obj->fExtra;
	long m = LO_HowManyChunks(lo, obj);
	long first = ObjChunk(lo, chunks, nodes, real, obj, 1);
	long last = ObjChunk(lo, chunks, nodes, real, obj, m);
	long firstStroke = chunks[first].fStroke;
	long lastStroke = chunks[last].fStroke;
	if (first > last)
		return 0xff;
	for (long c = first; c <= last; c++)
		if (chunks[c].f0C != 3 && chunks[c].fHeight > maxH)
		{
			maxH = chunks[c].fHeight;
			best = c;
		}
	if (best == -1)
		return 0xff;
	switch (digit)
	{
	case 0:
		if (firstStroke == lastStroke)
			for (long c = first; c <= last; c++)
			{
				if (chunks[c].f78 == 402)
					return 1;
				if (chunks[c].f78 == 401)
					return 2;
			}
		if (firstStroke + 1 != lastStroke)
			return 0xff;
		{
			long s0 = -1;
			for (long s = firstStroke; s < lastStroke; s++)
				if (strokes[s].fFirstChunk != strokes[s].fLastChunk)
				{
					s0 = s;
					break;
				}
			if (s0 == -1)
				return 0xff;
			long a = strokes[s0].fFirstChunk, b = strokes[s0].fLastChunk;
			if (b < a)
				return 0xff;
			for (long c = a; c <= b; c++)
			{
				if (chunks[c].f78 == 402)
					return 4;
				if (chunks[c].f78 == 401)
					return 8;
			}
		}
		return 0xff;
	case 1:
		return (base == 1300 && extra == 8) ? 2 : 1;
	case 2:
		if (best == last && chunks[best].f78 == 502)
			return 1;
		if (last - 1 == best && chunks[last].f78 == 301)
			return 1;
		return 6;
	case 4:
		if (lastStroke == firstStroke)
		{
			if (chunks[best].fKind == 2)
				return 1;
			if (chunks[best].fKind == 1)
				return 4;
		}
		if (firstStroke + 1 != lastStroke)
			return 0xff;
		return strokes[lastStroke].fBottom <= strokes[firstStroke].fBottom ? 2 : 1;
	case 5:
	case 8:
		if (firstStroke + 1 == lastStroke)
			return 1;
		if (lastStroke == firstStroke)
			return 2;
		return 0xff;
	case 6:
		if (chunks[best].fKind == 2)
			return 3;
		if (chunks[best].fKind == 1)
			return 4;
		return 0xff;
	case 7:
		return (base == 1300 || base == 1400) ? 1 : 6;
	case 9:
		if (base == 1300)
		{
			if (chunks[best].fKind == 2)
				return 8;
			if (chunks[best].fKind == 1)
				return 4;
		}
		if (chunks[best].fKind == 2)
			return 3;
		return 0xff;
	}
	return 0xff;		// 3
}


// ROM 0x002a0740 (unnamed) - the digits written as a variant the field
// does not allow (allowed: a byte of variant bits per digit, the staff's
// fDigits) taken out.  ==> 1.
static long
DropDisallowedVariants(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* strokes, const UByte* allowed, tag_LOWOBJ** sorted, long* count)
{
	long n = *count;
	for (long i = 0; i < n; i++)
	{
		tag_LOWOBJ* obj = sorted[i];
		UByte ch = DigitChar(DigitOf(obj));
		if (ch < '0' || ch > '9')
			continue;
		UByte variant = (UByte) DigitVariant(lo, chunks, nodes, real, strokes, obj);
		if ((allowed[ch - '0'] & variant) == 0)
			sorted[i]->fValue = 0xffff;
	}
	DropTakenOut(sorted, count);
	return 1;
}


#pragma mark - the pass

// ROM 0x0029ce20 (unnamed) - Digits' second looks: the digits found (class
// 1300, up to thirty, the ones taken out left out) sorted by their first
// node, the twelve corrections and ThreeToFive and RecognizeZCCW run over
// them, and the gaps (class 1200) placed between them - a gap goes before
// the first digit it lies more than 60% inside the room between (the box
// the writing is in closing the two ends; a gap of no width strictly
// inside it) - then the digits, with their values and fExtra, and the
// gaps, value 0xffff, put in the list as class 1900 in order.
// ROM BUG: nothing limits the gaps kept to the 33 words the ROM has for
// them; past that they run into the sorted digits (the two arrays are one
// block here as on the stack) and, past both, into the counts - DEVIATION:
// the host stops at the end of the block.
// ==> how many objects went in (0 with no digits).
long
DigitsSecondLooks(void* lo, tag_CHUNK* chunks, int32_t* real, tag_STK* strokes, long strokeCount, tag_wapx_type* nodes, const UByte* allowed, tag_BOX box)
{
	const long kBlock = 33 + 33;
	tag_LOWOBJ* block[kBlock];			// sp+0x8: the digits picked, then the gaps; sp+0x8c: the digits sorted
	tag_LOWOBJ** picked = &block[0];
	tag_LOWOBJ** sorted = &block[33];
	tag_LOWOBJ* obj = nil;
	long total = 0, count = 0, n = 0;
	ULong was = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1300);
	for (long more = LO_PickFirst(lo, &obj); more; )
	{
		if (obj->fValue != 0xffff)
			picked[n++] = obj;
		if (!LO_PickNext(lo, &obj) || n == 30)
			break;
	}
	LO_SetWorkClass(lo, was);
	if (n == 0)
		return 0;
	for (long i = 0; i < n; i++)
	{
		long at = 0;
		for ( ; at < count; at++)
			if (picked[i]->fFrom < sorted[at]->fFrom)
			{
				for (long j = count - 1; j >= at; j--)
					sorted[j + 1] = sorted[j];
				break;
			}
		sorted[at] = picked[i];
		count++;
	}
	OnesToCommas(lo, nodes, chunks, real, sorted, count, strokes, strokeCount);
	OnesToSlashes(lo, chunks, nodes, real, strokes, strokeCount, sorted, count);
	CommasToPoints(sorted, count);
	OnesToOpenBrackets(sorted, count);
	ClosesToOnes(lo, chunks, nodes, real, sorted, count);
	ZerosInEights(lo, sorted, count);
	SevenToCloseBracket(lo, chunks, nodes, real, strokes, strokeCount, sorted, count);
	DropMinusBars(lo, nodes, chunks, real, sorted, &count, strokes, strokeCount);
	DropHookedThrees(lo, chunks, nodes, real, strokes, strokeCount, sorted, &count);
	MergeGuillemets(lo, chunks, nodes, real, strokes, strokeCount, sorted, &count);
	CheckNeighbourStrokes(lo, chunks, nodes, real, strokes, strokeCount, sorted, &count);
	DropDisallowedVariants(lo, chunks, nodes, real, strokes, allowed, sorted, &count);
	ThreeToFive(lo, chunks, nodes, real, sorted, count);
	RecognizeZCCW(lo, chunks, nodes, real, sorted, count);

	long gaps = 0;
	tag_LOWOBJ** kept = &block[0];
	was = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1200);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		int32_t l = obj->fLeft, r = obj->fRight, w = r - l;
		for (long at = 0; at <= count; at++)
		{
			int32_t lo1, hi1;
			if (at == 0)
			{
				lo1 = box.left;
				hi1 = count > 0 ? sorted[0]->fLeft : box.right;
			}
			else if (at == count)
			{
				lo1 = sorted[count - 1]->fRight;
				hi1 = box.right;
			}
			else
			{
				lo1 = sorted[at - 1]->fRight;
				hi1 = sorted[at]->fLeft;
			}
			int32_t hi = r <= hi1 ? r : hi1;
			int32_t low = l >= lo1 ? l : lo1;
			int32_t overlap = hi - low;
			if ((w == 0 && l > lo1 && l < hi1) || (w > 0 && overlap * 100 / w > 60))
			{
				if (gaps < kBlock)		// DEVIATION: see above
					kept[gaps] = obj;
				gaps++;
				obj->fValue = (int32_t) at;
				break;
			}
		}
	}
	LO_SetWorkClass(lo, was);
	if (gaps > kBlock)
		gaps = kBlock;
	long g = 0;
	long i = 0;
	for ( ; i < count; i++)
	{
		for ( ; g < gaps && kept[g]->fValue <= i; g++)
		{
			LO_Add(lo, nodes, 1900, kept[g]->fFrom, kept[g]->fTo, 0xffff, -1);
			total++;
		}
		LO_Add(lo, nodes, 1900, sorted[i]->fFrom, sorted[i]->fTo, sorted[i]->fValue, sorted[i]->fExtra);
		total++;
	}
	for ( ; g < gaps && kept[g]->fValue <= i; g++)
	{
		LO_Add(lo, nodes, 1900, kept[g]->fFrom, kept[g]->fTo, 0xffff, -1);
		total++;
	}
	return total;
}
