/*
	File:		ChunkSearchV.cpp

	Contains:	The digit reader's searcher that walks the chunks one by
				one and asks of each what it could be the start of
				(New_SearchDigit_V): an upright line (class 300) a 1, a 7
				or a "H"; a curve down (class 500) a 2, 7, 1 or 9; an arc
				(class 400) a 6, 9, 5 or 7; an S (class 700) a 2, 3 or 5;
				a pair of short sections a sign.  What it finds goes in
				the list of low objects as class 1300 (a digit at value
				1300 + the digit, a sign at 1300 + its code; 1381, 1399,
				1371, 1372, 1323 and 1324 are the codes the searchers
				share), or class 2200 for a grey 9.  See Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x0028eb9c-0x0028f17c, 0x0028f478-0x0028fa14,
				0x00296e04-0x0029bba8); each function cites its origin.
				All of it is from the disassembly: the decompiler loses
				the stack arguments of most of the statics and the
				registers the ROM reuses between tests.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRAbs, HWRMemoryAlloc, HWRMemoryFree
#include "host/RomBugs.h"


// What each of V's tests is given about the chunk it is looking at: the
// chunk and the two before and after it in its stroke (nil where the
// stroke has none), and the writing's height.  ROM tagLocalStuff, 0x18
// bytes.  DEVIATION: its pointers are wider on the host, so it is
// allocated at sizeof.
struct tagLocalStuff
{
	tag_CHUNK*	fCur;				// +00
	tag_CHUNK*	fPrev;				// +04
	tag_CHUNK*	fPrev2;				// +08
	tag_CHUNK*	fNext;				// +0c
	tag_CHUNK*	fNext2;				// +10
	long		fHeight;			// +14
};

// A chunk of nothing, which a nil pointer's fields are read from.
// DEVIATION: two tests of DgtFromDnHorseshoe read the chunk two after the
// one looked at without asking whether there is one, and the ROM reads a
// nil pointer's fields out of low memory (whatever the MMU maps at
// virtual 0); the host reads them as nought instead, which makes the test
// of its fNext fail as the ROM's (a word there that is not -1) does.
static const tag_CHUNK	kNoChunk = { };


// __rt_sdiv: the quotient, truncated towards nought.  DEVIATION: a
// divisor of nought, which traps on the ARM, answers nought.
static inline long
Quot(long n, long d)
{
	return d == 0 ? 0 : n / d;
}

static inline bool
Is(const tag_CHUNK* c, long v)
{
	return c->f78 == v;
}


#pragma mark - the named statics

// ROM 0x0028eb9c DgtFromDnHorseshoe__FP13tagLocalStuffPvP13tag_wapx_typeP9tag_CHUNKPiiT6P7tag_STKT6
// A 4, 5 or 9 from an arc down (the chunk at real index a) followed by a
// line or arc (the next chunk, at b) curving away beneath it - a
// horseshoe opening down, whose left leg the next chunk after b draws.
static long
DgtFromDnHorseshoe(tagLocalStuff* ls, void* lo, tag_wapx_type* n, tag_CHUNK* chunks, int32_t* real, long a, long b, tag_STK* strokes, long strokeCount)
{
	tag_CHUNK* cur = ls->fCur;
	tag_CHUNK* prev = ls->fPrev;
	tag_CHUNK* next = ls->fNext;
	tag_CHUNK* next2 = ls->fNext2;
	const tag_CHUNK* n2 = next2 != nil ? next2 : &kNoChunk;		// (see kNoChunk)
	tag_CHUNK* cB = &chunks[real[b]];
	if (cB->fNext == -1)
		return -1;
	tag_CHUNK* cA = &chunks[real[a]];
	long from = cA->fFrom;
	tag_CHUNK* nb = &chunks[cB->fNext];
	long to = nb->fTo;
	if (cA->fPrev != -1)
	{
		tag_CHUNK* pp = &chunks[cA->fPrev];
		if (!Is(pp, 301) && !Is(pp, 402))
			return -1;
		if (pp->fBottom >= cA->fBottom)
			return -1;
		if (pp->fLeft <= cA->fLeft)
			return -1;
		from = pp->fFrom;
	}
	tag_wapx_type* start = &n[from];
	long dir = GetDirection(cA->fX1, cA->fY1, start->x, start->y);
	if ((ULong) dir < 0x11 || (ULong) dir > 0x17)
		return -1;
	long value, extra;
	if ((Is(nb, 301) || Is(nb, 402)) && nb->fBottom - cA->fBottom > (cA->fBottom - cA->fTop) / 2)
	{
		if (Is(nb, 402))
		{
			if (nb->fLength2 <= nb->fBulge << 4)
				return -1;
			if (nb->fDir >= 13)
				return -1;
			if (nb->fNext != -1)
				return -1;
		}
		if (nb->fNext != -1)
		{
			tag_CHUNK* nn = &chunks[nb->fNext];
			if (nn->fKind == 1)
			{
				if (Is(nn, 402))
					return -1;
				if (nn->fX0 < nn->fX1 && nb->fLength2 < nn->fLength2 << 4)
					return -1;
			}
		}
		if (prev == nil && next->fLength2 >= cur->fLength2 << 2)
			return -1;
		// a 4 when the leg is a line coming down to the left that starts
		// well below the curve's top and the curve's steepest fall is at
		// its start
		value = 1309;
		extra = 30;
		if (cur->fPrev == -1 && n2->fNext == -1 && (Is(n2, 301) || Is(n2, 402)))
		{
			tag_wapx_type* legStart = &n[n2->fFrom];
			if ((n[n2->fFrom + 1].y - n2->fY0) * 3 > n2->fHeight)
			{
				long best = 0, fall = 0;
				for (long k = cur->fFrom; k <= cur->fTo - 1; k++)
				{
					long d = n[k + 1].y - n[k].y;
					if (d > fall)
					{
						fall = d;
						best = k;
					}
				}
				if ((cur->fHeight < fall * 2 && n2->fRight - cur->fLeft < (n2->fX0 - cur->fX0) * 2)
				 || (best == cur->fFrom && fall * 3 > cur->fHeight * 2 && legStart->fDirOut < n[best].fDirOut + 2))
				{
					value = 1304;
					extra = 0;
				}
			}
		}
	}
	else
	{
		if (next->fLength2 >= cur->fLength2 << 2)
			return -1;
		if (!(nb->fNext == -1 && Is(nb, 401) && nb->fBottom > cA->fBottom && nb->fX0 > nb->fX1))
		{
			if (nb->fNext == -1 || !Is(nb, 401) || nb->fBottom <= cA->fBottom)
				return -1;
			tag_CHUNK* nn = &chunks[nb->fNext];
			if (!Is(nn, 401) && !Is(nn, 301))
				return -1;
			if (nn->fX0 <= nn->fX1)
				return -1;
			to = nn->fTo;
		}
		value = 1309;
		extra = 32;
		if (Is(next, 301) && prev == nil)
		{
			long d1 = GetDirection(n2->fX0, n2->fY0, next->fX0, next->fY0);
			long d2 = GetDirection(n2->fX0, n2->fY0, n[n2->fFrom + 1].x, n[n2->fFrom + 1].y);
			long ang = GetAngleBetweenTwoDir((ULong) d1, (ULong) d2);
			if (ang > 5)
			{
				long d3 = GetDirection(cur->fX1, cur->fY1, n[cur->fTo - 1].x, n[cur->fTo - 1].y);
				if (ang + 1 >= GetAngleBetweenTwoDir(next->fDir, (ULong) d3))
				{
					// ROM BUG (fixed): the direction to the leg's end is
					// taken from (fX1, fX1) - the curve's end's x twice, its
					// y never.  The fix takes it from (fX1, fY1).
					if (cur != nil && next2 != nil && (next2->fBottom - cur->fBottom) * 3 < next2->fBottom - cur->fTop)
					{
						long fromY = RomBugFixed() ? cur->fY1 : cur->fX1;
						if ((ULong) GetDirection(cur->fX1, fromY, next2->fX1, next2->fY1) > 14)
							return -1;
					}
					// a 5: a bar written as a stroke of its own beside the top
					long s = cur->fStroke + 1;
					if (s < strokeCount && strokes[s].fFirstChunk == strokes[s].fLastChunk)
					{
						long first = strokes[s].fFirstChunk;
						// ROM BUG (fixed): the bar's subclass, start and end
						// are read from the chunk `first` places after this
						// one, not from chunk `first` itself (the index is
						// added to the chunk's own address rather than the
						// array's); only its box and size are read from the
						// bar.  DEVIATION: the host takes a chunk past the
						// array's end as not a line, where the ROM reads
						// beyond it.  The fix reads them all from the bar.
						tag_CHUNK* bar = &chunks[first];
						long wrong = (cur - chunks) + first;
						tag_CHUNK* w = wrong < kMaxChunks ? &chunks[wrong] : nil;
						if (RomBugFixed())
							w = bar;
						if (w != nil && Is(w, 301)
						 && bar->fX0 < bar->fX1 && bar->fHeight * 3 < bar->fWidth * 2
						 && HWRAbs(start->y - w->fY0) < bar->fHeight
						 && bar->fWidth > HWRAbs(start->x - w->fX0) * 2)
							to = w->fTo;
					}
					value = 1305;
					extra = 31;
				}
			}
		}
	}
	LO_Add(lo, n, 1300, from, to, (ULong) value, extra);
	return -1;
}


// ROM 0x0028f478 DgtFromUpCCWArc__FP13tagLocalStuffPvP13tag_wapx_type
// Two arcs up, one on another, the lower between four and seven tenths
// the upper's height: the sign coded 99.
static long
DgtFromUpCCWArc(tagLocalStuff* ls, void* lo, tag_wapx_type* n)
{
	tag_CHUNK* cur = ls->fCur;
	tag_CHUNK* next = ls->fNext;
	tag_CHUNK* next2 = ls->fNext2;
	long height = ls->fHeight;
	if (cur == nil || next == nil)
		return -1;
	if (!Is(cur, 402) || !Is(next, 402) || cur->fKind != 1)
		return -1;
	long h = cur->fHeight;
	if (h * 3 < height * 2 || h == 0 || ls->fPrev != nil)
		return -1;
	if (!(h > cur->fWidth && next->fHeight > next->fWidth))
		return -1;
	if (next2 != nil && !Is(next2, 402))
		return -1;
	long q = next->fHeight * 100 / h;
	if (q <= 40 || q >= 75)
		return -1;
	if (next->fLength2 / 25 > next->fBulge / 4)
		return -1;
	long to = next->fTo;
	if (next2 != nil && Is(next2, 402) && (next2->fRight - cur->fRight) * 3 < h)
		to = next2->fTo;
	LO_Add(lo, n, 1300, cur->fFrom, to, 1399, 0x21);
	return -1;
}


// ROM 0x0028f5c8 DgtFromAloneDnCCWArc__FP13tagLocalStuffPvP13tag_wapx_type
// An arc down on its own, deep for its chord, as tall as the writing
// within reason and ending close to where it began: the sign coded 81.
static long
DgtFromAloneDnCCWArc(tagLocalStuff* ls, void* lo, tag_wapx_type* n)
{
	tag_CHUNK* cur = ls->fCur;
	long height = ls->fHeight;
	if (ls->fPrev != nil || ls->fNext != nil)
		return -1;
	if (cur->fLength2 <= cur->fBulge << 4)
		return -1;
	if (!(height < cur->fHeight * 2 && cur->fHeight * 5 < height * 7))
		return -1;
	long dir = GetDirection(n[cur->fTopNode].x, n[cur->fTopNode].y, n[cur->fBottomNode].x, n[cur->fBottomNode].y);
	if ((ULong) dir <= 8 || (ULong) dir >= 14)
		return -1;
	int32_t x = n[cur->fFrom + 1].x, y = n[cur->fFrom + 1].y;
	long d1 = (cur->fX0 - x) * (cur->fX0 - x) + (cur->fY0 - y) * (cur->fY0 - y);
	long d2 = (cur->fX1 - x) * (cur->fX1 - x) + (cur->fY1 - y) * (cur->fY1 - y);
	if (d2 * 25 >= d1 * 4)
		return -1;
	LO_Add(lo, n, 1300, cur->fFrom, cur->fTo, 1381, 0);
	return -1;
}


// ROM 0x0028f6f4 GreyDgtFromELink__FP13tagLocalStuffPvP13tag_wapx_type
// A chunk going down whose stroke is only it and the arc before it: that
// arc put in as a grey 9 (class 2200).
static long
GreyDgtFromELink(tagLocalStuff* ls, void* lo, tag_wapx_type* n)
{
	tag_CHUNK* prev = ls->fPrev;
	if (ls->fCur->fKind != 2 || prev == nil)
		return -1;
	if (ls->fPrev2 == nil && ls->fNext == nil && Is(prev, 402))
		LO_Add(lo, n, 2200, prev->fFrom, prev->fTo, 1309, 0);
	return -1;
}


// ROM 0x0028f778 SignFromTwoSections__FP13tagLocalStuffPvP13tag_wapx_type
// A stroke of two sections going down (three nodes), its middle node
// above its end: the signs coded 23 and 24, by its subclass or by the
// shape of the corner.
static long
SignFromTwoSections(tagLocalStuff* ls, void* lo, tag_wapx_type* n)
{
	tag_CHUNK* cur = ls->fCur;
	long from = cur->fFrom, to = cur->fTo;
	if (ls->fPrev != nil || ls->fNext != nil || cur->fKind != 2 || to != from + 2)
		return -1;
	long h = cur->fHeight;
	if (ls->fHeight >= h * 2)
		return -1;
	long midY = n[from + 1].y;
	if (midY >= n[to].y)
		return -1;
	long value;
	if (Is(cur, 601))
		value = 1324;
	else if (Is(cur, 602))
		value = 1323;
	else
	{
		value = 0;
		if (Is(cur, 402))
		{
			if (cur->fBulge * 9 <= cur->fLength2)
				return -1;
			long q = h / 3;
			if (!(cur->fTop + q < midY && cur->fBottom - q > midY) && cur->fWidth <= h)
				return -1;
			if (GetAngleBetweenTwoDir((ULong) n[from].fDirOut, (ULong) n[from + 1].fDirOut) < 5)
				return -1;
		}
		else if (Is(cur, 401))
		{
			if (cur->fBulge * 9 <= cur->fLength2)
				return -1;
			long q = h / 3;
			if (!(cur->fTop + q < midY && cur->fBottom - q > midY) && cur->fWidth <= h)
				return -1;
			if ((ULong) n[from].fDirOut < 13 || (ULong) n[from].fDirOut > 16)
				return -1;
			if (GetAngleBetweenTwoDir((ULong) n[from + 1].fDirOut, (ULong) n[from].fDirOut) < 5)
				return -1;
		}
		else
			return -1;
		int32_t x = n[from + 1].x, y = n[from + 1].y;
		long d1 = (cur->fX0 - x) * (cur->fX0 - x) + (cur->fY0 - y) * (cur->fY0 - y);
		long d2 = (cur->fX1 - x) * (cur->fX1 - x) + (cur->fY1 - y) * (cur->fY1 - y);
		long big = d1 > d2 ? d1 : d2;
		long small = d1 < d2 ? d1 : d2;
		if (Quot(big, small) >= 2)
			return -1;
		value = Is(cur, 402) ? 1324 : 1323;
	}
	LO_Add(lo, n, 1300, from, to, (ULong) value, 0);
	return -1;
}


#pragma mark - the unnamed statics

// ROM 0x00298608 (unnamed) - whether nodes from..to all lie within the box
static long
NodesInBox(tag_wapx_type* n, long from, long to, long left, long top, long right, long bottom)
{
	for (long k = from; k <= to; k++)
	{
		if (n[k].x > right || n[k].x < left || n[k].y < top || n[k].y > bottom)
			return 0;
	}
	return 1;
}


// ROM 0x00297438 (unnamed) - a # of this chunk (a stroke of four nodes
// going down) and two single-line strokes, the two strokes before it or
// the two after: the stroke's two halves each cross both lines and the
// lines do not cross each other.  ==> 1 when found (the sign coded 71).
static long
HashFromSticks(tagLocalStuff* ls, void* lo, tag_wapx_type* n, tag_CHUNK* chunks, int32_t* /*real*/, long a, long b, tag_STK* strokes, long strokeCount)
{
	tag_CHUNK* cur = ls->fCur;
	if (a == b && cur->fKind == 2 && Is(cur, 501))
		return 0;
	long s = cur->fStroke;
	long mode = 0;
	tag_CHUNK* sa = nil;
	tag_CHUNK* sb = nil;
	if (s > 1 && strokes[s - 2].fFirstChunk == strokes[s - 2].fLastChunk
	 && strokes[s - 1].fFirstChunk == strokes[s - 1].fLastChunk
	 && Is(&chunks[strokes[s - 2].fFirstChunk], 301) && Is(&chunks[strokes[s - 1].fFirstChunk], 301))
	{
		sa = &chunks[strokes[s - 2].fFirstChunk];
		sb = &chunks[strokes[s - 1].fFirstChunk];
		mode = 1;
	}
	else
	{
		if (strokeCount - 2 <= s)
			return 0;
		if (!(strokes[s + 1].fFirstChunk == strokes[s + 1].fLastChunk && strokes[s + 2].fFirstChunk == strokes[s + 2].fLastChunk))
			return 0;
		sa = &chunks[strokes[s + 1].fFirstChunk];
		if (!Is(sa, 301))
			return 0;
		sb = &chunks[strokes[s + 2].fFirstChunk];
		if (!Is(sb, 301))
			return 0;
		mode = 2;
	}
	long f = cur->fFrom;
	if (CheckQIntersec(n, f, f + 1, f + 2, f + 3))
		return 0;
	if (!CheckQIntersec(n, f, f + 1, sa->fFrom, sa->fTo))
		return 0;
	if (!CheckQIntersec(n, f, f + 1, sb->fFrom, sb->fTo))
		return 0;
	if (!CheckQIntersec(n, f + 2, f + 3, sa->fFrom, sa->fTo))
		return 0;
	if (!CheckQIntersec(n, f + 2, f + 3, sb->fFrom, sb->fTo))
		return 0;
	if (CheckQIntersec(n, sa->fFrom, sa->fTo, sb->fFrom, sb->fTo))
		return 0;
	long from, to;
	if (mode == 2)
	{
		from = f;
		to = sb->fTo;
	}
	else
	{
		from = sa->fFrom;
		to = f + 3;
	}
	LO_Add(lo, n, 1300, from, to, 1371, 0);
	return 1;
}


// ROM 0x002976e4 (unnamed) - a 0 or a 9 from a circle (the class-200
// object found at this arc): a 9 when a line or arc going down on its own
// follows the circle's last chunk and reaches below it by more than half
// the circle's height; else a 0 when the circle is at least two thirds
// the writing's height and no chunk going down after it reaches below it.
static long
DgtFromCircle(tagLocalStuff* ls, void* lo, tag_LOWOBJ* obj, tag_wapx_type* n, tag_CHUNK* chunks, int32_t* real, long /*a*/, long b)
{
	long height = ls->fHeight;
	long objTo = obj->fTo;
	long objFrom = obj->fFrom;
	long objBottom = obj->fBottom;
	long objHeight = obj->fBottom - obj->fTop;
	tag_CHUNK* last = &chunks[real[b]];
	long nextIdx = last->fNext;
	if (nextIdx != -1)
	{
		tag_CHUNK* t = &chunks[nextIdx];
		if (t->fNext == -1 && (Is(t, 301) || Is(t, 401))
		 && t->fBottom - last->fBottom > (last->fBottom - last->fTop) / 2)
		{
			LO_Add(lo, n, 1300, objFrom, t->fTo, 1309, 0x18);
			return -1;
		}
	}
	if (height * 2 / 3 >= objHeight)
		return -1;
	if (nextIdx != -1)
	{
		tag_CHUNK* t = &chunks[nextIdx];
		// ROM BUG (fixed): the tail's left is compared with half the
		// circle's width, not with the circle's middle.  The fix compares
		// it with the middle.
		long middle = RomBugFixed() ? (obj->fRight + obj->fLeft) / 2 : (obj->fRight - obj->fLeft) / 2;
		if (t->fKind == 2 && t->fLeft > middle)
		{
			if (objBottom - objHeight / 3 < t->fBottom)
				return -1;
		}
	}
	LO_Add(lo, n, 1300, objFrom, objTo, 1300, 0x19);
	return -1;
}


// ROM 0x00298528 (unnamed) - after the chunk at real index a, a level
// line on its own (wider than two and a half times its height) starting
// right of the chunk's end and near it: ==> its last node, -1 for none.
static long
BarAfterChunk(tag_CHUNK* chunks, int32_t* real, long realCount, long a)
{
	if (a + 1 >= realCount)
		return -1;
	tag_CHUNK* bar = &chunks[real[a + 1]];
	if (bar->fNext != -1)
		return -1;
	tag_CHUNK* c = &chunks[real[a]];
	if (bar->fX0 >= c->fX1)
		return -1;
	long w = bar->fRight - bar->fLeft;
	if (w <= (bar->fBottom - bar->fTop) * 5 / 2)
		return -1;
	long mx = (bar->fRight + bar->fLeft) / 2 - c->fX1;
	long my = (bar->fBottom + bar->fTop) / 2 - c->fY1;
	long ch = c->fBottom - c->fTop, cw = c->fRight - c->fLeft;
	if (mx * mx + my * my < (ch * ch + cw * cw) / 9)
		return bar->fTo;
	return -1;
}


// ROM 0x0029867c (unnamed) - the two chunks after chunk next going right
// and down the way the bottom of a 2 does (neither turning back left):
// ==> the last node, -1 when they do not.  The chunks are read by index
// after next, not by following the links.
static long
TwoTail(tag_wapx_type* n, tag_CHUNK* chunks, long left, long top, long right, long bottom, long next)
{
	tag_CHUNK* c = &chunks[next];
	long last = c->fTo, lowest = c->fTo, highest = c->fTo;
	long maxY = c->fY1, minY = c->fY1;
	long maxX = c->fX1;
	for (long k = 0; k < 2; k++)
	{
		long following = chunks[next + k].fNext;
		if (following == -1)
			break;
		if (k == 1 && chunks[chunks[next + 1].fNext].fNext != -1)
			return -1;
		tag_CHUNK* m = &chunks[following];
		if (m->fX1 < m->fX0)
			return -1;
		if (maxX < m->fX1)
			maxX = m->fX1;
		if (minY > m->fY1)
		{
			minY = m->fY1;
			highest = m->fTo;
		}
		if (maxY < m->fY1)
		{
			maxY = m->fY1;
			lowest = m->fTo;
		}
		last = m->fTo;
	}
	long run = n[last].x - c->fX0;
	long w3 = (right - left) * 2 / 3;
	if (run <= w3 || run >= w3 * 4)
		return -1;
	if (n[highest].y <= top + (bottom - top) / 3)
		return -1;
	long dir = GetDirection(c->fX0, c->fY0, n[lowest].x, n[lowest].y);
	if ((ULong) dir < 0x10 || (ULong) dir >= 0x15)
		return -1;
	return last;
}


// ROM 0x00297860 (unnamed) - an upright line (a chunk of class 300): a
// "H" of two uprights and a bar; else not when a digit found already
// covers it; else a 1 on its own, a 1 with a hook (the arc before it) or
// a 7 (a bar before it and the upright slanting), or a 2 when a hook
// before it and a tail after it close round.
static long
DgtFromStick(tagLocalStuff* ls, void* lo, tag_WORD_TRACE* trace, long /*traceCount*/, tag_LOWOBJ* obj,
			 tag_wapx_type* n, tag_CHUNK* chunks, brack_type* brackets, int32_t* real, long realCount,
			 long a, long b, tag_STK* strokes, long strokeCount)
{
	tag_CHUNK* cur = ls->fCur;
	tag_CHUNK* prev = ls->fPrev;
	tag_CHUNK* prev2 = ls->fPrev2;
	LO_GetWorkClassID(lo);
	if (a != b)
		return -1;
	{
		tag_CHUNK* c = &chunks[real[a]];
		if (ls->fHeight / 3 > c->fBottom - c->fTop)
			return -1;
	}

	// a "H": the next stroke (or two) are lines whose ends lie either side
	// of this one, the two outer lines crossing it
	long s1 = cur->fStroke + 1;
	if (s1 != strokeCount && cur->fRealIndex != 0)
	{
		long firstC = strokes[s1].fFirstChunk, lastC = strokes[s1].fLastChunk;
		long p1 = chunks[firstC].fFrom;
		long p2 = chunks[lastC].fTo;
		long p3 = -1, p4 = -1;
		bool ok = false;
		tag_CHUNK* fc = &chunks[firstC];
		if (firstC == lastC && Is(fc, 301) && fc->fRealIndex < realCount - 1 && chunks[firstC + 2].fNext == -1)
		{
			p3 = chunks[firstC + 2].fFrom;
			p4 = chunks[firstC + 2].fTo;
			ok = true;
		}
		else if (firstC == lastC && Is(fc, 502) && fc->fKind == 2)
		{
			p4 = p2;
			p2 = fc->fTo - fc->f91;
			p3 = fc->fFrom + fc->f90;
			ok = true;
		}
		else if (p2 - p1 <= 3)
		{
			if (p2 - p1 == 3)
			{
				p4 = p2;
				p2 = p1 + 1;
				p3 = p2 + 1;
				ok = true;
			}
			else if (strokeCount - 1 != s1)
			{
				long s2 = s1 + 1;
				p3 = chunks[strokes[s2].fFirstChunk].fFrom;
				p4 = chunks[strokes[s2].fLastChunk].fTo;
				ok = p4 - p3 <= 2;
			}
		}
		if (ok)
		{
			int32_t x1 = n[p1].x, y1 = n[p1].y;
			int32_t x2 = n[p2].x, y2 = n[p2].y;
			int32_t x3 = n[p3].x, y3 = n[p3].y;
			int32_t x4 = n[p4].x, y4 = n[p4].y;
			if (x1 < x2 && x3 < x2 && x3 < x4)
			{
				long maxX = x2 <= x4 ? x4 : x2;
				long minX = x1 >= x3 ? x3 : x1;
				long w = maxX - minX;
				long maxY = y3 <= y4 ? y4 : y3;
				long minY = y1 >= y2 ? y2 : y1;
				if (cur->fHeight * 3 >= w * 2 && maxY - minY <= cur->fHeight
				 && CheckQIntersecXY(x1, y1, x2, y2, cur->fX0, cur->fY0, cur->fX1, cur->fY1)
				 && CheckQIntersecXY(x2, y2, x3, y3, cur->fX0, cur->fY0, cur->fX1, cur->fY1)
				 && CheckQIntersecXY(x3, y3, x4, y4, cur->fX0, cur->fY0, cur->fX1, cur->fY1)
				 && (prev == nil || (prev2 != nil && (Is(prev, 301) || Is(prev, 402)))))
				{
					long idx = real[cur->fRealIndex] - 2;
					tag_CHUNK* c2 = &chunks[idx];
					if (c2->fPrev == -1 && c2->fDir > 11 && c2->fHeight < cur->fHeight
					 && cur->fBottom - cur->fHeight / 4 > c2->fBottom
					 && c2->fWidth < w * 2
					 && HWRAbs((cur->fRight + cur->fLeft) / 2 - c2->fRight) < c2->fHeight
					 && (Is(c2, 301) || Is(c2, 402)))
					{
						LO_Add(lo, n, 1300, c2->fFrom, p4, 1372, 0);
						return -1;
					}
				}
			}
		}
	}

	// not when a digit found already runs through this chunk
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1300);
	if (LO_PickFirst(lo, &obj))
	{
		do
		{
			long many = LO_HowManyChunks(lo, obj);
			long first = LO_GetRealChunkInd(lo, chunks, n, obj, 1);
			long last = LO_GetRealChunkInd(lo, chunks, n, obj, many);
			if (first <= a && a <= last)
			{
				LO_SetWorkClass(lo, saved);
				return -1;
			}
		} while (LO_PickNext(lo, &obj));
	}
	LO_SetWorkClass(lo, saved);

	long ci = real[a];
	tag_CHUNK* c = &chunks[ci];
	if (c->fKind == 2 && c->fPrev == -1 && c->fNext == -1)
	{
		LO_Add(lo, n, 1300, c->fFrom, c->fTo, 1301, 0x1a);			// a 1 on its own
		return -1;
	}
	if (c->fKind == 2 && c->fPrev != -1)
	{
		long pi = c->fPrev;
		tag_CHUNK* p = &chunks[pi];
		bool hook = false;
		if (c->fRight + p->fHeight / 9 >= p->fRight && p->fPrev == -1)
		{
			if (Is(p, 301))
				hook = true;
			else if (Is(p, 401) || Is(p, 402))
			{
				brack_type* br = &brackets[p->fFirstBracket];
				hook = br->fHeight2 < br->fLength2 / 9;
			}
		}
		if (hook && c->fNext == -1)
		{
			long from = p->fFrom;
			long to = c->fTo;
			long d1 = GetDirection(p->fX1, p->fY1, p->fX0, p->fY0);
			long d2 = GetDirection(c->fX0, c->fY0, c->fX1, c->fY1);
			long ang = GetAngleBetweenTwoDir((ULong) d1, (ULong) d2);
			bool one = false;
			if (ang >= 0 && ang <= 2 && (Is(p, 402) || (ULong) d1 > 8))
			{
				long bar = BarAfterChunk(chunks, real, realCount, a);
				if (bar != -1)
					to = bar;
				one = true;
			}
			else if (ang == 2)						// (and the direction 8 or under)
			{
				if (prev->fLength2 <= cur->fLength2 / 16)
					one = true;
				else if ((ULong) d1 >= 8)
				{
					long mf = v_MostFarFromChord(trace, n[p->fFrom].fIndex, n[p->fTo].fIndex);
					long cross = (p->fY0 - p->fY1) * (trace[mf].x - p->fX0) + (p->fX1 - p->fX0) * (trace[mf].y - p->fY0);
					one = cross > 0;
				}
			}
			if (one)
			{
				LO_Add(lo, n, 1300, from, to, 1301, 0x6f);		// a 1 with its hook
				return -1;
			}
			if (ang >= 2)
			{
				// a 7: the upright slants, and the bar is the hook
				to = c->fTo;
				from = p->fFrom;
				if (HWRAbs(p->fY0 - c->fY1) < (c->fBottom - c->fTop) / 3)
					return -1;
				long ch = c->fBottom - c->fTop;
				if (a + 1 < realCount)
				{
					tag_CHUNK* m = &chunks[real[a + 1]];
					if (m->fRight - m->fLeft > m->fBottom - m->fTop && Is(m, 301) && m->fLeft < c->fRight)
						to = m->fTo;
				}
				if (a - 2 >= 0)
				{
					tag_CHUNK* bb = &chunks[real[a - 2]];
					if (bb->f74 == 300 && bb->fPrev == -1 && bb->fHeight < ch / 2
					 && chunks[real[a - 1]].fBottom - bb->fTop < bb->fHeight * 2 / 3)
					{
						long dx = bb->fX0 - p->fX0, dy = bb->fY0 - p->fY0;
						if (dx * dx + dy * dy < bb->fLength2 * 4 / 9)
							from = bb->fFrom;
					}
				}
				long dir = GetDirection(n[to].x, n[to].y, n[from].x, n[from].y);
				if ((ULong) dir <= 4 || (ULong) dir >= 22)
					LO_Add(lo, n, 1300, from, to, 1307, 0x1c);
				return -1;
			}
		}
	}

	// a 2: a hook before the upright and a tail after it
	if (c->fPrev == -1 || c->fNext == -1)
		return -1;
	long nextIdx = c->fNext;
	tag_CHUNK* nx = &chunks[nextIdx];
	if (nx->fX0 >= nx->fX1)
		return -1;
	tag_CHUNK* p = &chunks[c->fPrev];
	if (p->fX0 >= p->fX1)
		return -1;
	if (c->fHeight * 3 <= p->fHeight * 4)
		return -1;
	if (!Is(p, 301) && !Is(p, 401))
		return -1;
	long start = p->fFrom;
	long d1 = GetDirection(p->fX1, p->fY1, p->fX0, p->fY0);
	long d2 = GetDirection(c->fX0, c->fY0, c->fX1, c->fY1);
	if (GetAngleBetweenTwoDir((ULong) d1, (ULong) d2) <= 0)
		return -1;
	if (p->fPrev != -1)
	{
		tag_CHUNK* pp = &chunks[p->fPrev];
		if (pp->fPrev != -1)
			return -1;
		long pw = p->fRight - p->fLeft;
		long ph3 = (p->fBottom - p->fTop) / 3;
		long pw3 = pw / 3;
		if (!NodesInBox(n, pp->fFrom, pp->fTo, p->fLeft - ph3, p->fTop - pw3, p->fRight + ph3, p->fBottom + pw3))
			return -1;
		start = pp->fFrom;
	}
	long right = c->fRight <= p->fRight ? p->fRight : c->fRight;
	long left = c->fLeft >= p->fLeft ? p->fLeft : c->fLeft;
	long top = c->fTop >= p->fTop ? p->fTop : c->fTop;
	long bottom = c->fBottom <= p->fBottom ? p->fBottom : c->fBottom;
	long end = TwoTail(n, chunks, left, top, right, bottom, nextIdx);
	if (end == -1)
		return -1;
	if (n[end].x - n[start].x > cur->fHeight * 2)
		return -1;
	LO_Add(lo, n, 1300, start, end, 1302, 0x1d);
	return -1;
}


// ROM 0x00298814 (unnamed) - an S (a chunk of subclass 702, the chunks at
// real indexes a..b): a 2 when its last bracket runs right to left and a
// line before it, a 5 when a level bar lies at its start, else a 3.
static long
DgtFromS(tagLocalStuff* ls, void* lo, tag_wapx_type* n, tag_CHUNK* chunks, brack_type* brackets, int32_t* real, long realCount, long a, long b)
{
	long last = b;
	if (ls->fCur->fHeight < ls->fHeight / 3)
		return -1;
	long from = chunks[real[a]].fFrom;
	tag_CHUNK* cb = &chunks[real[b]];
	long to = cb->fTo;
	brack_type* br = &brackets[cb->fLastBracket];
	long dir = GetDirection(n[br->fFrom].x, n[br->fFrom].y, n[br->fTo].x, n[br->fTo].y);
	if ((ULong) dir > 14)
	{
		tag_CHUNK* ca = &chunks[real[a]];
		if (ca->fPrev != -1)
		{
			tag_CHUNK* p = &chunks[ca->fPrev];
			if (!Is(p, 301) && !Is(p, 401))
				return -1;
			if (p->fX0 > p->fX1)
				return -1;
			from = p->fFrom;
		}
		LO_Add(lo, n, 1300, from, to, 1302, 1);
	}
	if ((ULong) dir >= 15)
		return -1;
	tag_CHUNK* cb2 = &chunks[real[b]];
	if (cb2->fNext != -1)
	{
		tag_CHUNK* nx = &chunks[cb2->fNext];
		if (!Is(nx, 301) && !Is(nx, 401))
			return -1;
		if (nx->fX0 < nx->fX1)
			return -1;
		to = nx->fTo;
		last = nx->fRealIndex;
	}
	if (last + 1 < realCount)
	{
		tag_CHUNK* m = &chunks[real[last + 1]];
		long w = m->fRight - m->fLeft;
		if (w > 0)
		{
			long lx = m->fX0 >= m->fX1 ? m->fX1 : m->fX0;
			long ly = m->fX0 >= m->fX1 ? m->fY1 : m->fY0;
			tag_CHUNK* ca = &chunks[real[a]];
			long dx = lx - ca->fX0, dy = ly - ca->fY0;
			if (dx * dx + dy * dy < w * w / 4)
			{
				LO_Add(lo, n, 1300, from, m->fTo, 1305, 2);
				return -1;
			}
		}
	}
	tag_CHUNK* ca = &chunks[real[a]];
	if (ca->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[ca->fPrev];
		if (!Is(p, 301) && !Is(p, 401))
			return -1;
		if (p->fX0 > p->fX1 || p->fHeight * 3 > ca->fHeight * 2)
			return -1;
		from = p->fFrom;
	}
	LO_Add(lo, n, 1300, from, to, 1303, 3);
	return -1;
}


// ROM 0x00298b04 (unnamed) - a 3 from a chunk of class 1400 drawn as three
// brackets or more: out to the right, round and down, and back out to the
// right again.
static long
DgtFromThreeBrackets(tagLocalStuff* ls, void* lo, tag_wapx_type* n, tag_CHUNK* chunks, brack_type* brackets, int32_t* real, long a, long b)
{
	if (a != b)
		return -1;
	tag_CHUNK* c = &chunks[real[a]];
	long from = c->fFrom, to = c->fTo;
	if (ls->fHeight * 2 / 3 >= c->fBottom - c->fTop)
		return -1;
	long first = c->fFirstBracket, last = c->fLastBracket;
	if (last - first < 2)
		return -1;
	brack_type* b1 = &brackets[first];
	if (!(b1->fKind == 1 || (b1->fKind == 2 && b1->fSign > 0)))
		return -1;
	if (n[b1->fFrom].x >= n[b1->fTo].x)
		return -1;
	brack_type* b2 = &brackets[first + 1];
	if (b2->fKind != 2 || b2->fSign >= 0)
		return -1;
	if (n[b2->fFrom].y >= n[b2->fTo].y)
		return -1;
	brack_type* b3 = &brackets[last];
	if (!(b3->fKind == 1 || (b3->fKind == 2 && b3->fSign > 0)))
		return -1;
	if (n[b3->fFrom].x <= n[b3->fTo].x)
		return -1;
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[c->fPrev];
		if (!Is(p, 301) && !Is(p, 401))
			return -1;
		if (p->fX0 > p->fX1)
			return -1;
		from = p->fFrom;
	}
	if (c->fNext != -1)
	{
		tag_CHUNK* nx = &chunks[c->fNext];
		if (!Is(nx, 301) && !Is(nx, 401))
			return -1;
		if (nx->fX0 < nx->fX1)
			return -1;
		to = nx->fTo;
	}
	LO_Add(lo, n, 1300, from, to, 1303, 4);
	return -1;
}


// ROM 0x00299e40 (unnamed) - a hook at the end of the chunk at real index
// a's first bracket (the last or last-but-one section steep and more than
// two thirds the chunk's height), in the middle third of its last
// bracket's width, after a line or shallow arc on its own going the same
// way: ==> that line's first node, -1 when there is none.
static long
HookBeforeCurve(tag_CHUNK* chunks, tag_wapx_type* n, brack_type* brackets, int32_t* real, long a)
{
	long result = -1;
	tag_CHUNK* c = &chunks[real[a]];
	long lastB = c->fLastBracket;
	brack_type* bf = &brackets[c->fFirstBracket];
	long nb = bf->fTo - bf->fFrom;
	if (nb < 2)
		return -1;
	long ch = c->fBottom - c->fTop;
	int32_t x1 = n[bf->fTo - 1].x, y1 = n[bf->fTo - 1].y;
	int32_t x0 = n[bf->fTo - 2].x, y0 = n[bf->fTo - 2].y;
	long ady = HWRAbs(y0 - y1);
	if (!(ady > HWRAbs(x0 - x1) * 2 && ch * 2 / 3 < ady))
	{
		if (nb <= 2)
			return -1;
		x1 = n[bf->fTo - 2].x;
		y1 = n[bf->fTo - 2].y;
		x0 = n[bf->fTo - 3].x;
		y0 = n[bf->fTo - 3].y;
		ady = HWRAbs(y0 - y1);
		if (ady <= HWRAbs(x0 - x1) * 2)
			return -1;
		if (ch * 2 / 3 >= ady)
			return -1;
	}
	brack_type* bl = &brackets[lastB];
	long best = bl->fFrom;
	for (long k = bl->fFrom; k <= bl->fTo; k++)
		if (n[k].x < n[best].x)
			best = k;
	long minX = n[best].x;
	long w = n[bl->fTo].x - minX;
	if (minX + w / 3 >= x1)
		return result;
	if (minX + w * 2 / 3 <= x1)
		return result;
	if (c->fPrev == -1)
		return result;
	tag_CHUNK* p = &chunks[c->fPrev];
	if (p->fPrev != -1)
		return result;
	if (!Is(p, 301))
	{
		if (!Is(p, 401) && !Is(p, 402))
			return result;
		brack_type* pb = &brackets[p->fFirstBracket];
		if (pb->fHeight2 >= pb->fLength2 / 9)
			return result;
	}
	if (c->fNext != -1)
		return result;
	long d1 = GetDirection(p->fX1, p->fY1, p->fX0, p->fY0);
	long d2 = GetDirection(x0, y0, x1, y1);
	long ang = GetAngleBetweenTwoDir((ULong) d1, (ULong) d2);
	if (ang >= 0 && ang <= 3)
		result = p->fFrom;
	return result;
}


// ROM 0x0029a150 (unnamed) - an arc down (the chunk at real index a) more
// than half the writing's height: a 6 when an arc up closes it below
// (and there is no circle there already), or when an S-shaped tail
// closes it; a 9 when a line comes back down from its end; a 2 when a line
// before it leads in from the left.
static long
DgtFromDnArc(tagLocalStuff* ls, void* lo, tag_wapx_type* n, tag_CHUNK* chunks, int32_t* real,
			 long /*boxLeft*/, long /*boxTop*/, long /*boxRight*/, long boxBottom, long a, long b)
{
	tag_CHUNK* cur = ls->fCur;
	tag_CHUNK* prev = ls->fPrev;
	tag_CHUNK* next = ls->fNext;
	tag_CHUNK* next2 = ls->fNext2;
	long height = ls->fHeight;
	tag_LOWOBJ* obj = nil;
	if (a != b)
		return -1;
	tag_CHUNK* c = &chunks[real[a]];
	long from = c->fFrom;
	long to = c->fTo;
	long cBottom = c->fBottom;
	long ch = c->fBottom - c->fTop;
	if (ch > height / 2 && c->fNext != -1)
	{
		long ni = c->fNext;
		tag_CHUNK* nx = &chunks[ni];
		long sub = nx->f78;
		bool six = false;
		if (sub == 402
		 && nx->fBottom - nx->fTop < ch * 7 / 10
		 && nx->fNext == -1
		 && nx->fLeft < c->fRight
		 && nx->fY1 - c->fY1 < ch / 4)
		{
			to = nx->fTo;
			ULong saved = LO_GetWorkClassID(lo);
			LO_SetWorkClass(lo, 200);
			bool circle = false;
			if (LO_PickFirst(lo, &obj))
			{
				do
				{
					if (obj->fFrom == from && obj->fTo == to)
					{
						circle = true;
						break;
					}
				} while (LO_PickNext(lo, &obj));
			}
			LO_SetWorkClass(lo, saved);
			if (!circle)
			{
				tag_CHUNK* c2 = &chunks[real[a]];
				long pi = c2->fPrev;
				bool ok = true;
				if (pi != -1)
				{
					tag_CHUNK* p = &chunks[pi];
					if (p->fBottom - p->fTop >= ch / 2 || p->fRight - p->fLeft >= ch * 3)
						ok = false;
					else
					{
						from = p->fFrom;
						if (Is(p, 401) || Is(p, 501))
							ok = false;
					}
				}
				if (ok)
				{
					long si = c2->fNext;
					if (si != -1)
					{
						tag_CHUNK* s = &chunks[si];
						long ti = s->fNext;
						if (ti != -1)
						{
							tag_CHUNK* t = &chunks[ti];
							if (t->fBottom - c2->fBottom > (c2->fBottom - c2->fTop) / 4)
								ok = false;
							else if (s->fRight - t->fLeft < (s->fRight - c2->fLeft) / 3)
								ok = false;
						}
						else if (s->fRightNode == to)
							ok = false;
						if (ok && ti == -1 && s->fBulge < s->fLength2 / 4 && s->fDir < 22)
							ok = false;
					}
				}
				if (ok && next != nil && next2 == nil
				 && next->fRight - next->fX1 < (next->fRight - cur->fLeft) / 4)
					ok = false;
				if (ok)
					LO_Add(lo, n, 1300, from, to, 1306, 0xc);
			}
			six = true;
		}
		if (!six && sub == 402 && nx->fX0 < nx->fX1
		 && nx->fBottom - nx->fTop < ch * 7 / 10 && nx->fNext != -1)
		{
			tag_CHUNK* m = &chunks[nx->fNext];
			if ((Is(m, 402) || Is(m, 301)) && m->fX0 > m->fX1)
			{
				bool done = false;
				if (Is(m, 301) && m->fBottom - m->fTop > (m->fRight - m->fLeft) * 3
				 && m->fBottom - cBottom > (m->fBottom - m->fTop) / 3)
				{
					// a 9: the tail comes straight back down
					to = m->fTo;
					bool ok = true;
					if (m->fNext != -1)
					{
						tag_CHUNK* q = &chunks[m->fNext];
						if (q->fKind == 1 && q->fX0 < q->fX1 && q->fHeight > m->fHeight / 4)
							ok = false;
					}
					if (ok)
						LO_Add(lo, n, 1300, from, to, 1309, 0xd);
					done = true;
				}
				if (!done)
				{
					to = m->fTo;
					bool ok = true;
					long pi = c->fPrev;
					if (pi != -1)
					{
						tag_CHUNK* p = &chunks[pi];
						if (p->fBottom - p->fTop >= ch / 2 || p->fRight - p->fLeft >= ch * 3)
							ok = false;
						else
						{
							from = p->fFrom;
							if (Is(p, 401) || Is(p, 501))
								ok = false;
						}
					}
					if (ok && m->fBottom - cBottom > ch / 4)
						ok = false;
					if (ok && nx->fRight - m->fLeft < (nx->fRight - c->fLeft) / 3)
						ok = false;
					if (ok)
					{
						long k = nx->fTo;
						long d1 = GetDirection(n[k].x, n[k].y, n[k - 1].x, n[k - 1].y);
						long d2 = GetDirection(n[k].x, n[k].y, n[k + 1].x, n[k + 1].y);
						if (GetAngleBetweenTwoDir((ULong) d1, (ULong) d2) <= 0)
							LO_Add(lo, n, 1300, from, to, 1306, 0xe);
					}
				}
			}
		}
	}

	// a 2: a line before the arc, which ends on the writing's line
	tag_CHUNK* c3 = &chunks[real[a]];
	if (c3->fPrev == -1)
		return -1;
	tag_CHUNK* p = &chunks[c3->fPrev];
	if (p->fPrev != -1)
		return -1;
	if (!Is(p, 301) && !Is(p, 401))
		return -1;
	if (p->fX0 >= p->fX1)
		return -1;
	if (c3->fNext != -1)
		return -1;
	if (HWRAbs(c3->fBottom - boxBottom) >= height / 3)
		return -1;
	if (c3->fLastBracket != c3->fFirstBracket)
		return -1;
	if (c3->fBulge <= c3->fLength2 / 4)
		return -1;
	tag_wapx_type* pc = &n[cur->fFrom];
	tag_wapx_type* pc1 = &n[cur->fFrom + 1];
	tag_wapx_type* pp = &n[prev->fTo - 1];
	long s = (pc->x - pp->x) * (pc1->y - pc->y) + (pc1->x - pc->x) * (pp->y - pc->y);
	if (s < 0)
		return -1;
	// (the end is the last node the tests above reached for: the arc's,
	// or its closing arc's or tail's when one was found)
	LO_Add(lo, n, 1300, p->fFrom, to, 1302, 0xf);
	return -1;
}


// ROM 0x0029bac0 ComposeTrace__FP13tag_wapx_typeiT2P14tag_WORD_TRACE
// The polyline's nodes from..to as a trace of their own: a pen-up, the
// points (their flags left as they were), a pen-up.  ==> 1.
long
ComposeTrace(tag_wapx_type* n, long from, long to, tag_WORD_TRACE* trace)
{
	trace[0].x = -1;
	trace[0].y = -1;
	long k = 1;
	long count = to - from + 1;
	for ( ; k <= count; k++)
	{
		trace[k].x = (short) n[from + k - 1].x;
		trace[k].y = (short) n[from + k - 1].y;
	}
	trace[k].x = -1;
	trace[k].y = -1;
	return 1;
}


// ROM 0x00298d2c (unnamed) - a curve down (subclass 502, the chunk at real
// index a): an 8 when its end comes back to its hook's start, or when a
// hook before and an arc after close it; a 1 when a hook at its top is a
// 1's flag (HookBeforeCurve); a 7 when its first bracket turns the other
// way from its last, or its bottom is well right of its top, or it turns
// back at a local leftmost point; a 9 from the stroke two before; else a
// 2.  Anything that fails early is offered to DgtFromDnArc as a narrow
// curve with an arc after it.
static long
DgtFromDnCurve(tagLocalStuff* ls, void* lo, tag_wapx_type* n, tag_CHUNK* chunks, brack_type* brackets, int32_t* real,
			   long realCount, tag_STK* strokes, long /*strokeCount*/, tag_BOX box, long a, long b)
{
	tag_CHUNK* cur = ls->fCur;
	tag_CHUNK* prev = ls->fPrev;
	tag_CHUNK* prev2 = ls->fPrev2;
	tag_CHUNK* next = ls->fNext;
	tag_CHUNK* next2 = ls->fNext2;
	long height = ls->fHeight;
	if (a != b)
		goto fallback;
	{
		long ci0 = real[a];
		long prevIdx = ci0, nextIdx = ci0;
		tag_CHUNK* c = &chunks[ci0];
		long bl = c->fLeft, bt = c->fTop, br = c->fRight, bb = c->fBottom;
		long from = c->fFrom;
		long to = c->fTo;
		if (c->fY0 > c->fY1)
			return -1;
		long ch = c->fBottom - c->fTop;
		long h3 = height / 3;
		if (h3 >= ch)
		{
			if (ch <= height / 2)
				goto fallback;
			if (HWRAbs(c->fBottom - box.bottom) >= h3)
				goto fallback;
		}
		long cPrev = c->fPrev;
		if (cPrev != -1)
		{
			long q = ch * ch / 16;
			tag_CHUNK* p = &chunks[cPrev];
			from = p->fFrom;
			long dx = n[to].x - n[from].x, dy = n[to].y - n[from].y;
			if (dx * dx + dy * dy < q)
			{
				// an 8: the curve's end comes back to its hook's start
				if (c->fNext != -1)
				{
					tag_CHUNK* nx = &chunks[c->fNext];
					if (!Is(nx, 301) && !Is(nx, 402))
						goto fallback;
					to = nx->fTo;
				}
				LO_Add(lo, n, 1300, from, to, 1308, 5);
				return -1;
			}
			if (!Is(p, 301) && !Is(p, 401))
				goto fallback;
			if (p->fX0 > p->fX1)
				goto fallback;
			if (cur->f90 != 0 && prev->fY0 > n[cur->fFrom + cur->f90].y)
				goto fallback;
			prevIdx = cPrev;
			if (p->fLeft < bl) bl = p->fLeft;
			if (!(bt < p->fTop)) bt = p->fTop;
			if (!(br > p->fRight)) br = p->fRight;
			if (!(bb > p->fBottom)) bb = p->fBottom;
		}
		c = &chunks[real[a]];
		if (c->fNext != -1)
		{
			tag_CHUNK* nx = &chunks[c->fNext];
			if (nx->fNext != -1)
				goto fallback;
			if (!Is(nx, 301) && !Is(nx, 402))
				goto fallback;
			if (nx->fX0 > nx->fX1)
				goto fallback;
			to = nx->fTo;
			nextIdx = c->fNext;
			if (nx->fLeft < bl) bl = nx->fLeft;
			if (!(bt < nx->fTop)) bt = nx->fTop;
			if (!(br > nx->fRight)) br = nx->fRight;
			if (!(bb > nx->fBottom)) bb = nx->fBottom;
		}
		else
		{
			brack_type* blast = &brackets[c->fLastBracket];
			tag_wapx_type* lTo = &n[blast->fTo];
			tag_wapx_type* lFrom = &n[blast->fFrom];
			long dxl = lTo->x - lFrom->x;
			brack_type* bfirst = &brackets[c->fFirstBracket];
			long q = (n[bfirst->fTo].x - n[from].x) / 3;
			bool seven = false;
			bool checkLines = false;
			if (!(dxl >= 0 && dxl >= q))
			{
				if (blast->fHeight2 < blast->fLength2 / 9 || lTo->y - lFrom->y < (c->fBottom - c->fTop) / 4)
				{
					// a 7: the last bracket goes back left, or is straight
					if (a + 1 < realCount)
					{
						tag_CHUNK* m = &chunks[real[a + 1]];
						if (m->fRight - m->fLeft > m->fBottom - m->fTop && Is(m, 301) && m->fLeft < c->fRight)
							to = m->fTo;
					}
					if (cPrev != -1 && chunks[cPrev].fPrev != -1)
						return -1;
					LO_Add(lo, n, 1300, from, to, 1307, 6);
					return -1;
				}
			}
			if (blast->fSign * bfirst->fSign < 0 && bfirst->fLength2 > blast->fLength2 * 9 && bfirst->fHeight2 > blast->fHeight2 * 4)
				seven = true;
			else if (blast->fKind == 1)
			{
				// ROM BUG (fixed): the curve's height is measured from
				// node fKind (+0x08) rather than from its first node
				// (+0x00).  The fix measures it from fFrom.
				long t = HWRAbs(dxl) * 3 / 2;
				long top = RomBugFixed() ? c->fFrom : c->fKind;
				if (t < HWRAbs(n[top].y - n[c->fTo].y))
					seven = true;
			}
			if (seven)
			{
				checkLines = true;
				if (a + 1 < realCount)
				{
					tag_CHUNK* m = &chunks[real[a + 1]];
					if (m->fRight - m->fLeft <= (m->fBottom - m->fTop) * 2 || m->fLeft >= chunks[real[a]].fRight)
						checkLines = false;
					else
						to = m->fTo;
				}
			}
			if (checkLines)
			{
				long k = cur->fFrom + cur->f90;
				long x0 = n[k].x;
				long x1 = (cur->fTo > k && cur->fX1 < n[k + 1].x) ? n[k + 1].x : cur->fX1;
				long r3 = n[cur->fTo - cur->f91].x - n[from].x;
				if ((ULong) n[k].fDirOut >= 20 || r3 >= (x1 - x0) * 2)
				{
					LO_Add(lo, n, 1300, from, to, 1307, 7);
					return -1;
				}
			}
			long hook = HookBeforeCurve(chunks, n, brackets, real, a);
			if (hook != -1)
			{
				LO_Add(lo, n, 1300, hook, chunks[real[a]].fTo, 1301, 8);
				return -1;
			}
		}

		// 0x0029941c
		long ci = real[a];
		c = &chunks[ci];
		long cNext = c->fNext;
		if (cNext != -1)
		{
			tag_CHUNK* nx = &chunks[cNext];
			if (nx->fKind == 1)
			{
				tag_wapx_type* p = &n[brackets[c->fFirstBracket].fTo];
				long dx = p->x - nx->fX1, dy = p->y - nx->fY1;
				long nh = nx->fBottom - nx->fTop;
				if (dx * dx + dy * dy < nh * nh / 9)
					goto fallback;
			}
		}
		if (c->fKind == 2 && ci - 1 == prevIdx && ci + 1 == nextIdx)
		{
			tag_CHUNK* p = &chunks[prevIdx];
			if (Is(p, 401) && n[p->fLeftNode].x < n[from].x)
			{
				tag_CHUNK* nx = &chunks[nextIdx];
				if (Is(nx, 402) && n[nx->fRightNode].x > n[to].x)
				{
					long dx = n[from].x - n[to].x, dy = n[from].y - n[to].y;
					long hh = c->fBottom - c->fTop;
					if (hh * hh / 9 > dx * dx + dy * dy)
					{
						LO_Add(lo, n, 1300, from, to, 1308, 9);		// an 8
						return -1;
					}
				}
			}
		}
		if (n[from].y > c->fBottom)
			goto fallback;
		if (n[to].y < c->fTop)
			goto fallback;
		if (c->fPrev == -1 && cNext == -1 && c->fBottomNode != to)
		{
			long k = c->fBottomNode - 1;
			if (c->fFrom <= k)
			{
				while (n[k].x <= n[k + 1].x)
				{
					k--;
					if (c->fFrom > k)
						break;
				}
			}
			k++;
			if (n[to].x - n[k].x < c->fWidth / 3)
			{
				LO_Add(lo, n, 1300, from, to, 1307, 10);
				return -1;
			}
		}
		if (cNext != -1)
		{
			tag_wapx_type* q = &n[c->fFrom];
			tag_CHUNK* nx = &chunks[cNext];
			tag_wapx_type* p = &n[nx->fTo];
			long dx = q->x - p->x, dy = q->y - p->y;
			long hh = c->fHeight;
			if (dx * dx + dy * dy < (hh * hh / 9) * 4)
				goto fallback;
			if (c->fTop + hh / 3 > nx->fTop)
				goto fallback;
		}
		if (br - bl > (bb - bt) * 2)
			return -1;
		if (c->fPrev == -1 && cNext == -1 && to - from + 1 > 10)
			return -1;
		if (cPrev != -1)
		{
			tag_CHUNK* p = &chunks[cPrev];
			if (p->fKind == 1 && c->fHeight < p->fHeight * 2 && p->fX0 > n[to].x && p->fDir > 10)
				return -1;
		}

		// a 7 that turns back at a leftmost point of its curve
		if (next == nil && cur != nil)
		{
			long found = 0, kbest = 0;
			long start = cur->fFrom + 1;
			for (long k = start; k <= cur->fTo - 1; k++)
			{
				long xp = n[k - 1].x, xk = n[k].x, xn = n[k + 1].x;
				if (xp < xk && xn < xk)
				{
					kbest = k;
					continue;
				}
				if (start >= k)
					continue;
				if (!(xp > xk && xn > xk))
					continue;
				long my = n[k].y;
				long dir = GetDirection(xk, my, cur->fX1, cur->fY1);
				if (dir < 14)
					found = 1;
				else
				{
					long wide = prev == nil ? cur->fWidth : cur->fRight - prev->fLeft;
					if (wide > (cur->fX1 - xk) * 4)
						found = 1;
					else if (kbest != 0)
					{
						long bx = n[kbest].x, by = n[kbest].y;
						if (GetAngleBetweenTwoDir((ULong) dir, (ULong) GetDirection(xk, my, bx, by)) < 2)
						{
							long dA = (cur->fX1 - xk) * (cur->fX1 - xk) + (cur->fY1 - my) * (cur->fY1 - my);
							long dB = (bx - xk) * (bx - xk) + (by - my) * (by - my);
							if (dB > dA << 4)
								found = 1;
						}
					}
				}
				if (!found)
					break;
				if (prev2 == nil && kbest != 0)
				{
					long bx = n[kbest].x, by = n[kbest].y;
					long d;
					if (prev != nil)
					{
						if (!Is(prev, 401) && !Is(prev, 301))
							return -1;
						d = GetDirection(bx, by, prev->fX0, prev->fY0);
					}
					else
						d = GetDirection(bx, by, cur->fX0, cur->fY0);
					if (d > 3 && d < 8)
					{
						LO_Add(lo, n, 1300, from, to, 1307, 0);
						return -1;
					}
				}
				return -1;							// (found)
			}
		}

		long f91 = cur->f91;
		bool checkStart = false;
		if (f91 != 0 && next2 == nil)
		{
			if (next == nil)
				checkStart = true;
			else if (Is(next, 402))
			{
				tag_wapx_type* p = &n[cur->fTo - f91];
				long dx = p->x - next->fX1, dy = p->y - next->fY1;
				if (next->fLength2 > (dx * dx + dy * dy) * 2)
					return -1;
			}
		}
		if (checkStart || next == nil)
		{
			long f90 = cur->f90;
			if (f90 != 0)
			{
				tag_wapx_type* q = &n[cur->fFrom + f90];
				long d1 = (cur->fX1 - q->x) * (cur->fX1 - q->x) + (cur->fY1 - q->y) * (cur->fY1 - q->y);
				tag_wapx_type* p = &n[cur->fTo - f91];
				long d2 = (p->x - q->x) * (p->x - q->x) + (p->y - q->y) * (p->y - q->y);
				if (d2 > d1 << 4)
					return -1;
			}
		}
		// a 9: the stroke two before is its loop
		if (prev != nil && prev2 != nil && prev->fHeight * 3 > cur->fHeight && prev2->fPrev == -1)
		{
			long x = n[cur->fTo - f91].x;
			if (x < prev2->fX0 && prev2->fDir < 11 && prev2->fDir > 5
			 && (cur->fX1 - n[cur->fFrom + cur->f90].x) * 5 < (cur->fRight - prev->fLeft) * 2)
			{
				LO_Add(lo, n, 1300, prev2->fFrom, to, 1309, 0);
				return -1;
			}
		}
		// a 2, its hook within a box about the stroke's start
		if (prev != nil && from == prev->fFrom && prev2 != nil)
		{
			if (prev2->fPrev != -1)
				return -1;
			long sf = chunks[strokes[cur->fStroke].fFirstChunk].fFrom;
			long left = n[prev->fLeftNode].x;
			long k = cur->fTo - cur->f91;
			long right = n[k].x;
			if (k - 1 >= cur->fFrom && n[k - 1].x > right)
				right = n[k - 1].x;
			long q = (right - left) / 3;
			long hh = (prev->fBottom - prev->fTop) / 2;
			if (!NodesInBox(n, sf, from, left - q, prev->fTop - hh, right + q, prev->fBottom + hh))
				return -1;
		}
		if (prev != nil && from == prev->fFrom && prev->fHeight * 3 > cur->fHeight * 2)
			return -1;
		LO_Add(lo, n, 1300, from, to, 1302, 11);
		return -1;
	}

fallback:
	{
		tag_CHUNK* c = &chunks[real[a]];
		if (c->fPrev != -1)
			return -1;
		if (c->fRight - c->fLeft >= (c->fBottom - c->fTop) * 6 / 10)
			return -1;
		if (c->fKind != 2 || c->fNext == -1)
			return -1;
		if (!Is(&chunks[c->fNext], 402))
			return -1;
		DgtFromDnArc(ls, lo, n, chunks, real, box.left, box.top, box.right, box.bottom, a, b);
	}
	return -1;
}


// ROM 0x0029a8a8 (unnamed) - an arc down (subclass 401, or a curve's first
// chunk): a 7 when a line before it makes its bar; a 9 when an arc after
// it closes a loop at its start, a 5 when a level bar lies at its start,
// a 7 of the arc alone; else a 2 from a line and an arc after it, or a 2,
// 7 or 8 from the chunk after it.
static long
DgtFromAloneDnArc(tagLocalStuff* ls, void* lo, tag_wapx_type* n, tag_CHUNK* chunks, brack_type* brackets, int32_t* real,
				  long chunkCount, long realCount, tag_BOX box, long a, long b, tag_STK* strokes)
{
	tag_CHUNK* cur = ls->fCur;
	tag_CHUNK* prev = ls->fPrev;
	tag_CHUNK* prev2 = ls->fPrev2;
	tag_CHUNK* next = ls->fNext;
	tag_CHUNK* next2 = ls->fNext2;
	long height = ls->fHeight;
	long half = height / 2;

	// a 7 whose bar is the line before the arc
	if (cur->fHeight > half && cur->fKind == 2 && cur != nil && prev != nil
	 && (Is(prev, 401) || Is(prev, 301)) && prev->fX0 < prev->fX1
	 && cur->fMidY < (cur->fTop + cur->fBottom) / 2)
	{
		long midX = cur->fMidX, midY = cur->fMidY;
		tag_wapx_type* bn = &n[cur->fBottomNode];
		long dA = (midX - prev->fX0) * (midX - prev->fX0) + (midY - prev->fY0) * (midY - prev->fY0);
		long dB = (midX - bn->x) * (midX - bn->x) + (midY - bn->y) * (midY - bn->y);
		if (dA * 9 >= dB)
		{
			long d1 = GetDirection(midX, midY, prev->fX0, prev->fY0);
			if ((ULong) d1 <= 8
			 && GetAngleBetweenTwoDir((ULong) d1, (ULong) GetDirection(midX, midY, bn->x, bn->y)) >= 2)
			{
				long from = prev->fFrom, to = cur->fTo;
				long flag = 0;
				if (prev2 == nil)
				{
					if (next == nil)
						flag = prev->fHeight * 3 < cur->fHeight * 2;
					else if (next2 == nil && Is(next, 301) && next->fLength2 * 9 < dB
						  && HWRAbs(GetAngleBetweenTwoDir(next->fDir, (ULong) GetDirection(bn->x, bn->y, midX, midY))) <= 1)
						flag = 1;
				}
				if (flag)
				{
					if (cur->fRealIndex < realCount - 1)
					{
						tag_CHUNK* m = &chunks[real[cur->fRealIndex + 1]];
						if ((Is(m, 301) || Is(m, 401) || Is(m, 402)) && m->fWidth > m->fHeight
						 && CheckQIntersec(n, cur->fFrom, cur->fTo, m->fFrom, m->fTo))
							to = m->fTo;
					}
					LO_Add(lo, n, 1300, from, to, 1307, 0);
					return -1;
				}
			}
		}
	}

	if (a != b)
		return -1;
	tag_CHUNK* c = &chunks[real[a]];
	long from = c->fFrom, to = c->fTo;
	long ch = c->fBottom - c->fTop;
	if (height / 3 >= ch)
	{
		if (half >= ch)
			return -1;
		if (HWRAbs(c->fBottom - box.bottom) >= height / 4)
			return -1;
	}
	if (c->fPrev == -1 && c->fNext == -1)
	{
		brack_type* fb = &brackets[c->fFirstBracket];
		if (fb->fHeight2 < fb->fLength2 / 25)
			return -1;
		if (a + 1 < realCount)
		{
			tag_CHUNK* nx = &chunks[real[a + 1]];
			if (Is(nx, 402) && nx->fRight > c->fLeft && c->fBottom - nx->fBottom > ch / 4)
			{
				long dx = c->fX0 - nx->fX0, dy = c->fY0 - nx->fY0;
				if (dx * dx + dy * dy < ch * ch / 25)
				{
					LO_Add(lo, n, 1300, from, nx->fTo, 1309, 16);
					return -1;
				}
			}
		}
		if (a + 1 < realCount)
		{
			tag_CHUNK* nx = &chunks[real[a + 1]];
			long nh = nx->fBottom - nx->fTop, nw = nx->fRight - nx->fLeft;
			if (nw > nh * 3 / 2 && nx->fNext == -1
			 && !(nh * 3 < nw && nx->fTop - c->fTop > c->fHeight / 4))
			{
				long lx = nx->fX0 < nx->fX1 ? nx->fX0 : nx->fX1;
				long ly = nx->fX0 >= nx->fX1 ? nx->fY1 : nx->fY0;
				long dx = lx - c->fX0, dy = ly - c->fY0;
				if ((nh * nh + nw * nw) / 9 > dx * dx + dy * dy)
				{
					LO_Add(lo, n, 1300, from, nx->fTo, 1305, 17);
					return -1;
				}
			}
		}
		long f = c->fFrom;
		if (f + 1 <= chunks[chunkCount - 1].fTo
		 && (ULong) GetDirection(n[f + 1].x, n[f + 1].y, n[f].x, n[f].y) <= 3)
			return -1;
		long nb = fb->fTo - fb->fFrom + 1;
		if (nb >= 4 && nb < 20)
		{
			// DEVIATION: the ROM's trace is on the stack, the flags of its
			// points never set; the host's starts cleared
			tag_WORD_TRACE t[22] = { };
			long bar = 0;
			ComposeTrace(n, fb->fFrom, fb->fTo, t);
			long mf = v_MostFarFromChord(t, 1, nb);
			long d = GetDirection(t[mf].x, t[mf].y, t[1].x, t[1].y);
			if (a + 1 < realCount)
			{
				tag_CHUNK* nx = &chunks[real[a + 1]];
				if (nx->fRight - nx->fLeft > nx->fBottom - nx->fTop && Is(nx, 301)
				 && nx->fLeft < chunks[real[a]].fRight && nx->fNext == -1)
				{
					bar = 1;
					to = nx->fTo;
				}
			}
			if ((d >= 5 && d <= 6) || (bar && d >= 2))
				LO_Add(lo, n, 1300, from, to, 1307, 18);
			else if (!bar && d >= 2)
			{
				tag_CHUNK* cc = &chunks[real[a]];
				long hh = cc->fBottom - cc->fTop;
				if (hh < (t[mf + 1].y - t[mf].y) * 2 && n[cc->fRightNode].y - cc->fTop < hh / 3)
					LO_Add(lo, n, 1300, from, to, 1307, 19);
			}
			return -1;
		}
		if (a + 1 < realCount)
		{
			tag_CHUNK* nx = &chunks[real[a + 1]];
			if (nx->fRight - nx->fLeft > nx->fBottom - nx->fTop && Is(nx, 301) && nx->fLeft < c->fRight)
				to = nx->fTo;
		}
		if (a - 1 >= 0)
		{
			tag_CHUNK* bb = &chunks[real[a - 1]];
			if (bb->f74 == 300 && bb->fHeight > bb->fWidth && ch > bb->fHeight * 2)
			{
				long dx = bb->fX0 - c->fX0, dy = bb->fY0 - c->fY0;
				if (dx * dx + dy * dy < bb->fLength2 * 4 / 9)
					from = bb->fFrom;
			}
		}
		if (cur != nil && prev == nil && next == nil)
		{
			long d1 = GetDirection(cur->fX0, cur->fY0, cur->fMidX, cur->fMidY);
			long d2 = GetDirection(cur->fMidX, cur->fMidY, cur->fX1, cur->fY1);
			if ((ULong) d1 < 14 || (ULong) d2 < 7)
				return -1;
		}
		LO_Add(lo, n, 1300, from, to, 1307, 20);
		return -1;
	}

	if (c->fNext == -1)
		return -1;
	tag_CHUNK* nx = &chunks[c->fNext];
	// a 2: a line after the arc and a chunk after that along the line
	if (nx->fNext != -1 && (Is(nx, 401) || Is(nx, 301)))
	{
		tag_CHUNK* m = &chunks[nx->fNext];
		if ((Is(m, 301) || Is(m, 401) || Is(m, 402) || Is(m, 502)) && nx->fKind == 1
		 && nx->fBottom - nx->fTop < ch * 2 / 3
		 && HWRAbs(nx->fBottom - m->fBottom) < ch / 3
		 && m->fRight > (c->fLeft + c->fRight) / 2)
		{
			to = m->fTo;
			if (c->fPrev != -1)
			{
				tag_CHUNK* p = &chunks[c->fPrev];
				if (!Is(p, 301) && !Is(p, 401))
					return -1;
				if (p->fX0 > p->fX1)
					return -1;
				from = p->fFrom;
			}
			if (m->fNext != -1)
			{
				tag_CHUNK* q = &chunks[m->fNext];
				if (!Is(q, 301) && !Is(q, 402))
					return -1;
				if (q->fNext != -1)
					return -1;
				if (q->fX1 <= q->fX0)
					return -1;
				to = q->fTo;
			}
			long fy = n[from].y;
			if (c->fBottom - fy < ch / 4)
				return -1;
			if (n[to].x < n[c->fBottomNode].x)
				return -1;
			if (fy > c->fBottom)
				return -1;
			if (n[to].y < c->fTop)
				return -1;
			long sfirst = strokes[cur->fStroke].fFirstChunk;
			long sf = chunks[sfirst].fFrom;
			if (sf != from)
			{
				if (real[a] - sfirst > 3)
					return -1;
				long px = n[from].x, py = n[from].y;
				if (!NodesInBox(n, sf, from, px, cur->fTop - cur->fHeight / 4, cur->fRight + (cur->fRight - px) / 4, py))
					return -1;
				from = sf;
			}
			LO_Add(lo, n, 1300, from, to, 1302, 21);
			return -1;
		}
	}

	// the chunk after the arc ends the stroke
	if (nx->fNext != -1)
		return -1;
	long sub = nx->f78;
	if (!(sub == 301 || sub == 401 || sub == 402))
	{
		if (sub != 502)
			return -1;
		if (nx->fBottom - nx->fTop >= ch / 2)
			return -1;
	}
	if (nx->fRight <= (c->fLeft + c->fRight) / 2)
		return -1;
	if (!(nx->fX1 > nx->fX0 && nx->fY1 > c->fY0))
		return -1;
	from = c->fFrom;
	to = nx->fTo;
	if (c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[c->fPrev];
		if (!Is(p, 301) && !Is(p, 401))
			return -1;
		if (p->fX0 > p->fX1)
			return -1;
		from = p->fFrom;
	}
	else if (c->fBulge < c->fLength2 / 36)
		return -1;
	if (c->fBottom - n[from].y < ch / 4)
		return -1;
	if (n[to].x < n[c->fBottomNode].x)
		return -1;
	if (n[from].y > c->fBottom)
		return -1;
	if (n[to].y < c->fTop)
		return -1;
	long fx = n[from].x;
	{
		long dx = fx - n[to].x, dy = n[from].y - n[to].y;
		if (c->fHeight * c->fHeight / 9 > dx * dx + dy * dy)
		{
			if (sub != 401)
				LO_Add(lo, n, 1300, from, to, 1308, 22);			// an 8
			return -1;
		}
	}
	bool tryStroke = true;
	if (Is(next, 401))
	{
		if (cur->fY0 + cur->fHeight / 3 > next->fY1)
			return -1;
	}
	else if (!Is(next, 301) && !Is(next, 401))
		tryStroke = false;
	if (tryStroke && next->fX0 < next->fX1
	 && (cur->fBottom - cur->fMidY) * 3 > cur->fHeight * 2
	 && cur->fLength2 > next->fLength2 * 4
	 && cur->fRight - fx > next->fWidth)
	{
		// a 7: the arc's bar is the stroke before, reached from its start
		long sfirst = strokes[cur->fStroke].fFirstChunk;
		long sf = chunks[sfirst].fFrom;
		if (sf != from)
		{
			if (real[a] - sfirst > 3)
				return -1;
			long q = (cur->fRight - fx) / 3;
			if (!NodesInBox(n, sf, from, fx - q, cur->fTop - cur->fHeight / 4, cur->fRight, cur->fBottom - cur->fHeight / 2))
				return -1;
			from = sf;
		}
		long d = GetDirection(cur->fMidX, cur->fMidY, cur->fX0, cur->fY0);
		if ((ULong) d > 3 && (ULong) d < 8
		 && GetAngleBetweenTwoDir(next->fDir, (ULong) GetDirection(cur->fX1, cur->fY1, cur->fMidX, cur->fMidY)) < 4)
		{
			LO_Add(lo, n, 1300, from, to, 1307, 0);
			return -1;
		}
	}
	long sfirst = strokes[cur->fStroke].fFirstChunk;
	long sf = chunks[sfirst].fFrom;
	if (sf != from)
	{
		if (real[a] - sfirst > 3)
			return -1;
		long px = n[from].x, py = n[from].y;
		if (!NodesInBox(n, sf, from, px, cur->fTop - cur->fHeight / 4, cur->fRight + (cur->fRight - px) / 4, py))
			return -1;
		from = sf;
	}
	if (Is(next, 401) && next->fRight < cur->fRight
	 && (ULong) GetDirection(n[next->fLeftNode].x, n[next->fLeftNode].y, next->fX1, next->fY1) > 19)
		return -1;
	LO_Add(lo, n, 1300, from, to, 1302, 23);
	return -1;
}


#pragma mark - New_SearchDigit_V

// ROM 0x0029b9fc (unnamed) - a curve down (subclass 501) that is the first
// chunk of a stroke of at most three, and whose stroke reaches further
// right than it does: DgtFromAloneDnArc.
static long
DgtFromCurveStroke(tagLocalStuff* ls, void* lo, tag_wapx_type* n, tag_CHUNK* chunks, brack_type* brackets, int32_t* real,
				   long chunkCount, long realCount, tag_BOX box, long a, long b, tag_STK* strokes)
{
	long ci = real[a];
	tag_CHUNK* c = &chunks[ci];
	tag_STK* s = &strokes[c->fStroke];
	if (ci != s->fFirstChunk)
		return -1;
	if (s->fLastChunk - s->fFirstChunk >= 3)
		return -1;
	if (s->fRight <= c->fRight)
		return -1;
	DgtFromAloneDnArc(ls, lo, n, chunks, brackets, real, chunkCount, realCount, box, a, b, strokes);
	return -1;
}


// ROM 0x00296e04 New_SearchDigit_V__FPvP14tag_WORD_TRACEiP13tag_wapx_typeP9tag_CHUNKP10brack_typePiN237tag_BOXP7tag_STKN23
long
New_SearchDigit_V(void* lo, tag_WORD_TRACE* trace, long traceCount, tag_wapx_type* n, tag_CHUNK* chunks, brack_type* brackets,
				  int32_t* real, long chunkCount, long realCount, tag_BOX box, tag_STK* strokes, long strokeCount, long height)
{
	tag_LOWOBJ* obj = nil;
	tagLocalStuff* ls = (tagLocalStuff*) HWRMemoryAlloc(sizeof(tagLocalStuff));	// DEVIATION: the ROM's 0x18
	if (ls == nil)
		return -1;
	ls->fHeight = height;
	for (long k = 0; k < chunkCount; k++)
	{
		tag_CHUNK* cur = &chunks[k];
		if (cur->fKind == 3)
			continue;
		tag_STK* s = &strokes[cur->fStroke];
		long before = k - s->fFirstChunk;
		long after = s->fLastChunk - k;
		tag_CHUNK* prev = before > 0 ? cur - 1 : nil;
		tag_CHUNK* prev2 = before > 1 ? cur - 2 : nil;
		tag_CHUNK* next = after > 0 ? cur + 1 : nil;
		tag_CHUNK* next2 = after > 1 ? cur + 2 : nil;
		ls->fCur = cur;
		ls->fPrev = prev;
		ls->fPrev2 = prev2;
		ls->fNext = next;
		ls->fNext2 = next2;

		// a # of this stroke of four nodes and two lines
		tag_CHUNK* first = &chunks[s->fFirstChunk];
		tag_CHUNK* last = &chunks[s->fLastChunk];
		long f = first->fFrom;
		if (last->fTo - f + 1 == 4 && s->fFirstChunk == k)
		{
			long ang = GetAngleBetweenTwoDir((ULong) n[f].fDirOut, (ULong) n[f + 2].fDirOut);
			if (ang > -3 && ang < 7
			 && HashFromSticks(ls, lo, n, chunks, real, first->fRealIndex, last->fRealIndex, strokes, strokeCount) != 0)
				continue;
		}

		// a sign of two short sections
		if (prev == nil && next == nil && cur->fKind == 2 && cur->fTo == cur->fFrom + 2
		 && height < cur->fHeight * 2 && n[cur->fFrom + 1].y < n[cur->fTo].y)
			SignFromTwoSections(ls, lo, n);

		long ri = cur->fRealIndex;
		switch (cur->f74)
		{
		case 500:
			if (Is(cur, 502))
				DgtFromDnCurve(ls, lo, n, chunks, brackets, real, realCount, strokes, strokeCount, box, ri, ri);
			if (Is(cur, 501))
				DgtFromCurveStroke(ls, lo, n, chunks, brackets, real, chunkCount, realCount, box, ri, ri, strokes);
			break;
		case 300:
			if (cur->fWidth < cur->fHeight)
				DgtFromStick(ls, lo, trace, traceCount, obj, n, chunks, brackets, real, realCount, ri, ri, strokes, strokeCount);
			break;
		case 400:
			if (Is(cur, 402))
			{
				if (cur->fKind == 2)
				{
					if (prev == nil && next == nil)
						DgtFromAloneDnCCWArc(ls, lo, n);
					else
					{
						DgtFromDnArc(ls, lo, n, chunks, real, box.left, box.top, box.right, box.bottom, ri, ri);
						if (next != nil && (Is(next, 402) || Is(next, 301)) && next->fX1 > next->fX0)
							DgtFromDnHorseshoe(ls, lo, n, chunks, real, ri, next->fRealIndex, strokes, strokeCount);
					}
				}
				if (cur->f7C != -1)
				{
					LO_PickDirectInd(lo, cur->f7C, &obj);
					long many = LO_HowManyChunks(lo, obj);
					long firstReal = LO_GetRealChunkInd(lo, chunks, n, obj, 1);
					long lastReal = LO_GetRealChunkInd(lo, chunks, n, obj, many);
					DgtFromCircle(ls, lo, obj, n, chunks, real, firstReal, lastReal);
				}
				if (cur->fKind == 1)
					DgtFromUpCCWArc(ls, lo, n);
			}
			else if (Is(cur, 401) && cur->fKind == 2)
				DgtFromAloneDnArc(ls, lo, n, chunks, brackets, real, chunkCount, realCount, box, ri, ri, strokes);
			break;
		case 700:
			if (Is(cur, 702))
				DgtFromS(ls, lo, n, chunks, brackets, real, realCount, ri, ri);
			if (Is(cur, 701) && cur->fKind == 2)
				GreyDgtFromELink(ls, lo, n);
			break;
		case 1400:
			DgtFromThreeBrackets(ls, lo, n, chunks, brackets, real, ri, ri);
			break;
		}
	}
	HWRMemoryFree((Ptr) ls);
	return -1;
}
