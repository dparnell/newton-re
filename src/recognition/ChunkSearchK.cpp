/*
	File:		ChunkSearchK.cpp

	Contains:	The digit reader's searcher for the digits built from
				lines and arcs (SearchDigit_K): it walks the chunks that
				are straight lines (class 300), arcs (400) and curves
				(500) and asks, of each, whether it is the start of a 1,
				4, 7 or x (two strokes crossing), a + or = and so on,
				putting what it finds in the list of low objects as class
				1300, value 1500 + the digit.  See Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x00289604-0x0028d9d0, 0x002a8090-0x002a8174); each
				function cites its origin.  All of it is from the
				disassembly: the decompiler loses the stack arguments of
				most of the statics.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRAbs
#include <string.h>


#pragma mark - directions

// ROM 0x002a8090 find_direct_forward__FP13tag_wapx_typeiN42
// The direction from node start to the first node after it take_next_point
// finds (the node before end when there is none).
long
find_direct_forward(tag_wapx_type* n, long end, long start, long dx, long dy, long sum)
{
	long p = take_next_point(n, end, start, dx, dy, sum);
	int32_t x, y;
	if (p == -1)
	{
		x = n[end - 1].x;
		y = n[end - 1].y;
	}
	else
	{
		x = n[p].x;
		y = n[p].y;
	}
	return GetDirection(n[start].x, n[start].y, x, y);
}


// ROM 0x002a810c find_direct_backward__FP13tag_wapx_typeiN32
// The direction to node start from the first node before it
// take_prev_point finds (the polyline's first when there is none).
long
find_direct_backward(tag_wapx_type* n, long start, long dx, long dy, long sum)
{
	long p = take_prev_point(n, start, dx, dy, sum);
	if (p == -1)
		return GetDirection(n[0].x, n[0].y, n[start].x, n[start].y);
	return GetDirection(n[p].x, n[p].y, n[start].x, n[start].y);
}


#pragma mark - small tests

// ROM 0x0028d4b0 (unnamed) - whether a chunk is small: the writing more
// than six times its height
static long
ChunkTooSmall(tag_CHUNK* chunks, long c, long height)
{
	return height > chunks[c].fHeight * 3 * 2;
}


// ROM 0x0028d4d4 (unnamed) - whether a chunk is not wider than tall by
// more than a tenth of its height
static long
ChunkNotWide(tag_CHUNK* chunks, long c)
{
	return chunks[c].fWidth - chunks[c].fHeight <= chunks[c].fHeight / 10;
}


// ROM 0x0028d50c (unnamed) - a digit found: put in the list over nodes
// from..to as class 1300, value 1500 + the digit.  The character (unused)
// is only for whoever reads the calls.  ==> LO_Add's answer.
static long
AddDigitK(tag_CHUNK_STAFF* staff, long from, long to, long digit, long /*character*/, long extra)
{
	return LO_Add(staff->fLO, staff->fNodes, 1300, from, to, (ULong) (digit + 1500), extra);
}


// ROM 0x0028d260 (unnamed) - whether the level stretch at the top of a
// chunk's stroke is short: from the chunk's top node the polyline is
// followed right while it climbs or falls less than it goes across, and
// back left likewise; ==> whether the two ends are no more than a
// quarter of the stroke's height apart.
static long
ShortTopBar(tag_CHUNK_STAFF* staff, tag_CHUNK* c)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	tag_STK* s = &strokes[c->fStroke];
	long left = -1;
	long top = c->fTopNode;
	long k = top + 1;
	for ( ; chunks[s->fLastChunk].fTo > k; k++)
	{
		int32_t dx = n[k].x - n[k - 1].x;
		if (HWRAbs(n[k].y - n[k - 1].y) > dx)
			break;
	}
	long right = k - 1;
	for (k = top - 1; chunks[s->fFirstChunk].fFrom <= k; k--)
	{
		int32_t dx = n[k + 1].x - n[k].x;
		if (HWRAbs(n[k + 1].y - n[k].y) > dx)
		{
			left = k + 1;
			break;
		}
	}
	if (left == -1)
		left = chunks[s->fFirstChunk].fFrom;
	return n[right].x - n[left].x <= s->fHeight / 4;
}


// ROM 0x0028d3fc (unnamed) - whether the stroke after a chunk's starts
// left of its stroke's top by a sixth of its height or more, and the
// stroke before it (if any) lies within the next one's span.
static long
NextStrokeCrosses(tag_CHUNK_STAFF* staff, long idx)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* n = staff->fNodes;
	tag_STK* strokes = staff->fStrokes;
	long s = chunks[staff->fRealChunks[idx]].fStroke;
	if (s + 1 >= staff->fStrokeCount)
		return 0;
	tag_STK* cur = &strokes[s];
	tag_STK* next = &strokes[s + 1];
	tag_STK* prev = s != 0 ? &strokes[s - 1] : nil;
	int32_t nextLeft = next->fLeft;
	if (n[cur->fTopNode].x - nextLeft < cur->fHeight / 6)
		return 0;
	if (prev == nil)
		return 1;
	if (prev->fRight <= next->fRight && prev->fLeft <= nextLeft)
		return 1;
	return 0;
}


// ROM 0x0028d558 (unnamed) - how far back from the real chunk idx the
// stroke's upright goes: walking back chunk by chunk (counting the ones
// taller or wider than a sixth of height) to the stroke's start, or to a
// chunk whose stroke starts after a jump that is not small or that does
// not go right (direction 17..19); then forward again past the ones that
// do not come down more than a sixth.  *from is the first chunk's first
// node, *first the chunk (a real index), box the lowest bottom and the
// furthest right they reach (box->right starting at the chunk's end x).
// ==> 0 with more than three chunks counted (two after the walk forward),
// else 1.
static long
UprightStart(tag_CHUNK_STAFF* staff, long idx, long height, int32_t* from, int32_t* first, tag_BOX* box)
{
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	long found = 0, counted = 0;
	box->bottom = chunks[real[idx]].fBottom;
	box->right = chunks[real[idx]].fX1;
	if (idx >= 0)
	{
		long small = height / 6;
		long k = idx;
		for ( ; k >= 0; k--)
		{
			tag_CHUNK* c = &chunks[real[k]];
			if (c->fHeight > small || c->fWidth > small)
				counted++;
			if (box->bottom < c->fBottom)
				box->bottom = c->fBottom;
			if (box->right < c->fRight)
				box->right = c->fRight;
			if (c->fPrev != -1)
				continue;
			if (k == 0)
			{
				found = k;
				break;
			}
			tag_CHUNK* jump = c - 1;
			if (jump->fWidth + jump->fHeight > small || direct_suits(c->fDir, 0x11, 0x13) == 0)
			{
				found = k;
				break;
			}
		}
		if (counted > 3)
			return 0;
	}
	*from = chunks[real[found]].fFrom;
	long k = found;
	if (k <= idx)
	{
		long small = height / 6;
		for ( ; k <= idx; k++)
		{
			tag_CHUNK* c = &chunks[real[k]];
			if (c->fBottom - c->fY0 > small)
			{
				found = k;
				break;
			}
			counted--;
		}
	}
	if (counted > 2)
		return 0;
	*first = (int32_t) found;
	return 1;
}


// ROM 0x0028d71c (unnamed) - the stroke after the real chunk idx's, as the
// bar of a 4 or a 7: its first chunk that comes down more than a sixth of
// height (*down, as a real index), its last (*last) and that one's last
// node (*lastNode); box the lowest bottom and furthest right of the
// chunks from the one coming down to the end of the stroke's first
// piece, which must not have more than two chunks taller or wider than a
// sixth.  The stroke must cross (NextStrokeCrosses), the chunk coming
// down must be well right of c's middle and not in line with it half way
// down, and *dir is the direction from the stroke's end to c's start
// (plus 12 less c's own direction when it is more than 6).  ==> 1, or 0.
static long
BarStroke(tag_CHUNK_STAFF* staff, long idx, long height, tag_CHUNK* c, int32_t* down, int32_t* last, int32_t* lastNode, tag_BOX* box, int32_t* dir)
{
	tag_wapx_type* n = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	long counted = 0;
	if (NextStrokeCrosses(staff, idx) == 0)
		return 0;
	tag_STK* next = &strokes[chunks[real[idx]].fStroke + 1];
	long first = next->fFirstChunk;
	long lastChunk = next->fLastChunk;
	long found = -1;
	tag_CHUNK* end = &chunks[lastChunk];
	int32_t endNode = end->fTo;
	if (first > lastChunk)
		return 0;
	long small = height / 6;
	for (long k = first; k <= lastChunk; k++)
	{
		tag_CHUNK* c2 = &chunks[k];
		int32_t drop = c2->fBottom - c2->fY0;
		if (drop > small && found == -1)
		{
			found = k;
			box->bottom = c2->fBottom;
			box->right = c2->fRight;
		}
		else if (drop <= small && found == -1)
			continue;
		if (c2->fHeight > small || c2->fWidth > small)
			counted++;
		if (box->bottom < c2->fBottom)
			box->bottom = c2->fBottom;
		if (box->right < c2->fRight)
			box->right = c2->fRight;
		if (c2->fNext != -1)
			continue;
		if (k == lastChunk)
			break;
		if (c2[1].fWidth + c2[1].fHeight > small)
			break;
	}
	if (counted > 2 || found == -1)
		return 0;
	tag_CHUNK* d = &chunks[found];
	if ((c->fLeft + c->fRight) / 2 - d->fLeft < (box->right - d->fLeft) / 3)
		return 0;
	int32_t midY = (d->fBottom + d->fTop) / 2;
	long xd = x_in_curve(n, d, midY);
	long xc = x_in_curve(n, c, midY);
	if ((box->right - d->fLeft) / 6 > xc - xd)
		return 0;
	*dir = (int32_t) GetDirection(end->fX1, end->fY1, c->fX0, c->fY0);
	if (*dir > 6)
		*dir = *dir + 12 - c->fDir;
	long jumps = real[idx] - idx;
	*down = (int32_t) (found - jumps - 1);
	*last = (int32_t) (lastChunk - jumps - 1);
	*lastNode = endNode;
	return 1;
}


#pragma mark - x

// ROM 0x0028b44c (unnamed) - the 4s (digits 4, 41, 44) over exactly the
// nodes from..to taken out
static void
DropFoursOver(void* lo, long from, long to)
{
	tag_LOWOBJ* obj = nil;
	ULong was = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1300);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue == 0xffff)
			continue;
		ULong d = (uint32_t) obj->fValue % 100;
		if ((d == 4 || d == 41 || d == 44) && obj->fFrom == from && obj->fTo == to)
			obj->fValue = 0xffff;
	}
	LO_SetWorkClass(lo, was);
}


// ROM 0x0028b1f4 (unnamed) - two chunks crossing as an x: taken left and
// right by their top nodes, their tops and bottoms level (within a third
// of the taller's height and a tenth more), their tops and bottoms a
// quarter of their mean height apart or more (less a tenth), the left one
// going down to the right (direction 13..16, or straight down with its
// bottom right of its top) and the right one down to the left (8..11): an
// x (69) over from..to, the 4s over the same nodes taken out.  ==> 1, or
// 0.
static long
CrossedX(tag_CHUNK_STAFF* staff, tag_CHUNK* a, tag_CHUNK* b, long from, long to)
{
	void* lo = staff->fLO;
	tag_wapx_type* n = staff->fNodes;
	if (n[a->fTopNode].x > n[b->fTopNode].x)
	{
		tag_CHUNK* t = a;
		a = b;
		b = t;
	}
	int32_t maxH = a->fHeight <= b->fHeight ? b->fHeight : a->fHeight;
	int32_t meanH = (a->fHeight + b->fHeight) / 2;
	int32_t topApart = n[b->fTopNode].x - n[a->fTopNode].x;
	int32_t bottomApart = n[a->fBottomNode].x - n[b->fBottomNode].x;
	long dirA = GetDirection(n[a->fTopNode].x, a->fTop, n[a->fBottomNode].x, a->fBottom);
	long dirB = GetDirection(n[b->fTopNode].x, b->fTop, n[b->fBottomNode].x, b->fBottom);
	int32_t third = maxH / 3;
	int32_t tenth = maxH / 10;
	if (HWRAbs(a->fBottom - b->fBottom) - third >= tenth)
		return 0;
	if (HWRAbs(a->fTop - b->fTop) - third >= tenth)
		return 0;
	int32_t meanTenth = meanH / 10;
	if (meanH / 4 - topApart >= meanTenth)
		return 0;
	if (meanH / 4 - bottomApart >= meanTenth)
		return 0;
	if (direct_suits(dirA, 13, 16) == 0)
	{
		if (dirA != 12)
			return 0;
		if (n[a->fBottomNode].x <= n[a->fTopNode].x)
			return 0;
	}
	if (direct_suits(dirB, 8, 11) == 0)
		return 0;
	AddDigitK(staff, from, to, 0x45, 'x', 9);
	DropFoursOver(lo, from, to);
	return 1;
}


// ROM 0x0028ad84 (unnamed) - an x written as two strokes of one or two
// chunks each, the first stroke starting at the real chunk idx: each
// stroke's tallest chunk a line (301) or an arc (401/402) not too curved,
// the second not much wider nor much shorter than the first, the two not
// in line with each other where one is an arc, and their tops and bottoms
// level - then CrossedX decides.  ==> its answer, or 0.
static long
TwoStrokeX(tag_CHUNK_STAFF* staff, long idx)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* n = staff->fNodes;
	tag_STK* strokes = staff->fStrokes;
	tag_CHUNK* c = &chunks[staff->fRealChunks[idx]];
	int32_t width = c->fWidth;
	int32_t h = c->fHeight;
	if (c->fStroke != 0 || staff->fStrokeCount < 2)
		return 0;
	if (strokes[0].fLastChunk - strokes[0].fFirstChunk > 1)
		return 0;
	long second = strokes[1].fFirstChunk, secondLast = strokes[1].fLastChunk;
	if (secondLast - second > 1)
		return 0;
	int32_t cls = c->f78;
	if (cls != 301 && c->fLength2 < c->fBulge * 4)
		return 0;
	if (!(c->fNext == -1 && c->fPrev == -1))
	{
		tag_CHUNK* o = &chunks[c->fNext != -1 ? c->fNext : c->fPrev];
		if (o->f78 != 301)
		{
			if (o->f78 != 401 && o->f78 != 402)
				return 0;
			if (o->fHeight * 3 > h)
				return 0;
		}
		else if (o->fHeight * 3 - h * 2 > h / 8)
			return 0;
		if (h < o->fHeight * 4 && o->fWidth - width > h / 10)
			return 0;
		if (o->fWidth - width > h / 6)
			return 0;
	}
	tag_CHUNK* tall;
	tag_CHUNK* other;
	if (second == secondLast)
	{
		tall = &chunks[second];
		other = nil;
	}
	else if (chunks[second].fHeight <= chunks[secondLast].fHeight)
	{
		tall = &chunks[secondLast];
		other = &chunks[second];
	}
	else
	{
		tall = &chunks[second];
		other = &chunks[secondLast];
	}
	int32_t maxH = tall->fHeight <= h ? h : tall->fHeight;
	h = tall->fHeight;
	width = tall->fWidth;
	if (other != nil)
	{
		if (other->fHeight * 3 > h)
			return 0;
		if (h < other->fHeight * 4 && other->fWidth - width > h / 10)
			return 0;
		if (other->fWidth - width > h / 6)
			return 0;
	}
	if (tall->f78 != 301)
	{
		if (tall->f78 != 401 && tall->f78 != 402)
			return 0;
		if (tall->fLength2 < tall->fBulge * 4)
			return 0;
	}
	if ((cls == 402 && c->fKind == 2) || (cls == 401 && c->fKind == 1))
		if (c->fLength2 < c->fBulge * 25)
		{
			long x = x_in_line(n[tall->fTopNode].x, tall->fTop, n[tall->fBottomNode].x, tall->fBottom, c->fMidY);
			if (c->fMidX < x)
				return 0;
		}
	if ((tall->f78 == 402 && tall->fKind == 2) || (tall->f78 == 401 && tall->fKind == 1))
		if (tall->fLength2 < tall->fBulge * 25)
		{
			long x = x_in_line(n[c->fTopNode].x, c->fTop, n[c->fBottomNode].x, c->fBottom, tall->fMidY);
			if (tall->fMidX < x)
				return 0;
		}
	int32_t third = maxH / 3;
	int32_t tenth = maxH / 10;
	if (HWRAbs(c->fTop - tall->fTop) - third > tenth)
		return 0;
	if (HWRAbs(c->fBottom - tall->fBottom) - third > tenth)
		return 0;
	long from = chunks[0].fFrom;
	long to = chunks[secondLast].fTo;
	if (n[c->fTopNode].x <= n[tall->fTopNode].x)
		return CrossedX(staff, c, tall, from, to);
	return CrossedX(staff, tall, c, from, to);
}


#pragma mark - 4 in one stroke

// ROM 0x0028aa5c (unnamed) - a 4 written in one stroke from the real
// chunk idx: down (c), across and back up (the next one, c1, and the one
// after if the stroke goes on, c2) - the stroke of no more than four
// chunks (three when idx has no chunk before it to check), c1 starting a
// quarter of c's height right of c's middle and c's end a quarter of its
// height above c1's lowest point, the crossing well below the top, and
// either a corner at c's end or a turn of five steps or more there: a 4
// over the whole stroke.  ROM QUIRK: `was` is whatever the caller had in
// its third register - only whether it is nought matters.  ==> 1, or 0.
static long
OneStrokeFour(tag_CHUNK_STAFF* staff, long idx, void* was)
{
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* n = staff->fNodes;
	tag_STK* strokes = staff->fStrokes;
	tag_CHUNK* c = &chunks[real[idx]];
	tag_CHUNK* c1 = &chunks[real[idx + 1]];
	tag_CHUNK* c2 = c1->fNext != -1 ? &chunks[real[idx + 2]] : nil;
	int32_t h = c->fHeight;
	if (idx != 0 && c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[real[idx - 1]];
		if (p->fPrev != -1 || idx < 2 || h < p->fHeight * 2 || p->fWidth > h / 8)
			return 0;
		was = p;
	}
	long pieces = strokes[c->fStroke].fLastChunk - strokes[c->fStroke].fFirstChunk;
	if (pieces > 3)
		return 0;
	if (was == nil && pieces > 2)
		return 0;
	int32_t mid = (c->fX1 + c->fX0) / 2;
	int32_t across = mid - c1->fLeft;
	int32_t below = n[c1->fLeftNode].y - c->fY1;
	if (across < h / 4)
		return 0;
	if (below < h / 4)
		return 0;
	if (below * 3 - h * 2 > h / 8)
		return 0;
	int32_t right = c1->fRightNode;
	if (c2 != nil && (c2->fRight > c1->fRight || c1->fLeftNode > right))
		right = c2->fRightNode;
	if (c1->fLeftNode > right)
		right = c1->fTo;
	tag_wapx_type* r = &n[right];
	if (mid - r->x > across / 4)
		return 0;
	int32_t bottom = (c2 != nil && c2->fBottom > c1->fBottom) ? c2->fBottom : c1->fBottom;
	if (h - (bottom - c1->fY0) < h / 4)
		return 0;
	if (c2 != nil && c2->fTop - c1->fY0 < h / 5)
		return 0;
	int32_t rise = c->fBottom - r->y;
	if (rise < h / 4)
		return 0;
	if (rise > h * 3 / 4)
		return 0;
	long end = c->fTo;
	if ((n[end].fFlags & kApxCorner) == 0)
	{
		long sixth = h / 6;
		long back = find_direct_backward(n, end, 0, 0, sixth);
		long on = find_direct_forward(n, c1->fTo + 1, c->fTo, 0, 0, sixth);
		if (distance_between_directions(back, on) < 5)
			return 0;
	}
	tag_STK* s = &strokes[c->fStroke];
	AddDigitK(staff, chunks[s->fFirstChunk].fFrom, chunks[s->fLastChunk].fTo, 4, '4', 8);
	return 1;
}


#pragma mark - %

// ROM 0x0028a8fc (unnamed) - whether stroke s is a small ring (one of a
// per cent sign's): at most four chunks, the middle two arcs (class 400)
// of one kind with a line or arc of the same kind at either end no taller
// than them, and where it starts and ends no more than a quarter of w
// apart across.
static long
SmallRing(tag_CHUNK_STAFF* staff, long s, long h)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* stroke = &staff->fStrokes[s];
	long first = stroke->fFirstChunk;
	long pieces = stroke->fLastChunk - first;
	tag_CHUNK* before = nil;
	tag_CHUNK* after = nil;
	if (pieces > 3)
		return 0;
	// ROM BUG: the chunk before the arcs is meant to be the first one, but
	// it is taken after the index has moved on - it is the first arc, so
	// its tests always pass and the ends are the arc's own
	if (chunks[first].fKind != 2)
	{
		first++;
		before = &chunks[first];
	}
	if (pieces < 2 && before != nil)
		return 0;
	tag_CHUNK* m = &chunks[first];
	if (!(m->f74 == 400 && m[1].f74 == 400 && m->f78 == m[1].f78))
		return 0;
	int32_t kind = m->f78;
	if (before != nil)
	{
		if (before->fHeight > m->fHeight)
			return 0;
		if (before->f74 != 300 && !(before->f74 == 400 && before->f78 == kind))
			return 0;
	}
	if (m[1].fNext != -1)
	{
		after = &m[2];
		if (after->fHeight > m->fHeight)
			return 0;
		if (after->f74 != 300 && !(after->f74 == 400 && after->f78 == kind))
			return 0;
	}
	int32_t a, b;
	if (kind == 402)
	{
		b = before != nil ? before->fRight : m->fX0;
		a = after != nil ? after->fLeft : m->fX1;
	}
	else
	{
		a = before != nil ? before->fLeft : m->fX0;
		b = after != nil ? after->fRight : m->fX1;
	}
	return a - b <= h / 4;
}


// ROM 0x0028a550 (unnamed) - a per cent sign whose stroke is the real
// chunk idx: the chunk a line that, if it goes on, goes on up and is short
// and not much wider; its stroke not the first nor the last, tall against
// the strokes either side (twice their heights, their widths about half
// its height), those two level with its middle - the one before ending
// low, the one after starting high - and small rings (SmallRing) where
// they are not plainly small, and not far from it: a % over the three.
// ==> 1, or 0.
static long
PercentSign(tag_CHUNK_STAFF* staff, long idx)
{
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	tag_CHUNK* c = &chunks[real[idx]];
	int32_t width = c->fWidth;
	int32_t h = c->fHeight;
	if (c->fNext != -1)
	{
		tag_CHUNK* n = &chunks[real[idx + 1]];
		if (n->fNext != -1)
			return 0;
		if (n->fY1 > n->fY0 || n->fHeight * 3 > h)
			return 0;
		if (h < n->fHeight * 4 && n->fWidth - width > h / 10)
			return 0;
		if (n->fWidth - width > h / 6)
			return 0;
	}
	if (idx != 0 && c->fPrev != -1)
	{
		tag_CHUNK* p = &chunks[real[idx - 1]];
		if (p->fPrev != -1 || idx < 2 || h < p->fHeight * 2)
			return 0;
		if (p->fX1 - p->fX0 > h / 8)
			return 0;
	}
	long s = c->fStroke;
	if (s == 0 || s + 1 >= staff->fStrokeCount)
		return 0;
	tag_STK* cur = &strokes[s];
	tag_STK* prev = &strokes[s - 1];
	tag_STK* next = &strokes[s + 1];
	int32_t sw = cur->fWidth, sh = cur->fHeight;
	int32_t pw = prev->fWidth, ph = prev->fHeight;
	int32_t nw = next->fWidth, nh = next->fHeight;
	if (sh < ph * 2 || pw * 2 - sh > sh / 8)
		return 0;
	if (sh < nh * 2 || nw * 2 - sh > sh / 8)
		return 0;
	int32_t mid = (cur->fBottom + cur->fTop) / 2;
	if (prev->fBottom - mid > sh / 8)
		return 0;
	if (mid - next->fTop > sh / 8)
		return 0;
	Boolean check = true;
	if (!(mid - prev->fBottom < sh / 8 && sh < ph * 4) && sh >= pw * 2)
	{
		if (pw > nw * 2 && ph > nh * 2 && (pw * 3 > sh || sh < ph * 4))
			;
		else if (!(pw * 3 > sh && ph * 3 > sh))
			check = false;
	}
	if (check && SmallRing(staff, s - 1, pw) == 0)
		return 0;
	check = true;
	if (!(next->fTop - mid < sh / 8 && sh < nh * 4) && sh >= nw * 2)
	{
		if (nw > pw * 2 && nh > ph * 2 && (nw * 3 > sh || sh < nh * 4))
			;
		else if (!(nw * 3 > sh && nh * 3 > sh))
			check = false;
	}
	if (check && SmallRing(staff, s + 1, nw) == 0)
		return 0;
	if (sw < sh / 4)
		sw = sh / 2;
	int32_t gap = sw / 2;
	if (cur->fLeft - prev->fRight > gap || next->fLeft - cur->fRight > gap)
		return 0;
	AddDigitK(staff, chunks[prev->fFirstChunk].fFrom, chunks[next->fLastChunk].fTo, 0x11, '%', 7);
	return 1;
}


#pragma mark - 7

// ROM 0x0028b514 (unnamed) - a 7 whose stem is the real chunk idx: a chunk
// going down that continues the stroke's bar.  If the stem goes on, the
// pieces after it (a turn back up and a foot) must stay left of it, and
// either the stroke must come back left of the stem's foot or the pieces
// be small; failing that the next stroke may be the 7's cross-bar, level
// and a sixth to three quarters of the stem up.  Then the bar, walked
// back to the stroke's start (no more than three pieces of any size), must
// be wide and flat enough against the stem and not bend down more than a
// quarter (a half with a cross-bar) of the stem's height, and go right: a
// 7 from the bar's start to the stem's (or cross-bar's) end.  ==> 1, or 0.
static long
SevenK(tag_CHUNK_STAFF* staff, long idx)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	long realCount = staff->fRealCount;
	brack_type* brackets = staff->fBrackets;
	tag_STK* strokes = staff->fStrokes;
	Boolean crossBar = false;
	long counted = 0;
	tag_CHUNK* c = &chunks[real[idx]];
	if (c->fPrev == -1 || c->fKind != 2)
		return 0;
	int32_t cw = c->fWidth, h = c->fHeight;
	long k = idx;
	int32_t right = 0, n1Left = 0, n1Bottom = 0, leftNode = 0, rightNode = 0, top = 0;
	if (c->fNext != -1)
	{
		k = idx + 1;
		tag_CHUNK* n1 = &chunks[real[k]];
		right = n1->fX1;
		n1Left = n1->fLeft;
		n1Bottom = n1->fBottom;
		leftNode = n1->fLeftNode;
		top = n1->fTop;
		rightNode = n1->fTo;
		if (n1->fNext != -1 && n1->fRight <= c->fRight && n1->fY1 - c->fTop >= h / 4
			&& direct_suits(n1->fDir + c->fDir - 12, 6, 23) == 0 && c->fBottom - n1->fBottom >= h / 4)
		{
			k++;
			tag_CHUNK* n2 = &chunks[real[k]];
			if (n2->fX1 < n2->fX0 || c->fBottom - n2->fBottom < h / 6)
				k--;
			else
			{
				if (n2->fRight > right)
				{
					right = n2->fRight;
					rightNode = n2->fRightNode;
				}
				if (n2->fTop < top)
					top = n2->fTop;
				if (n2->fNext != -1)
					k++;
			}
		}
	}
	tag_CHUNK* e = &chunks[real[k]];
	int32_t endNode = e->fTo;
	long endStroke = e->fStroke;
	if (k != idx)
	{
		Boolean small = true;
		if (c->fX1 > n1Left && n1Bottom - top >= h / 4)
		{
			int32_t mid = (c->fX0 + c->fX1) / 2;
			if (mid < right && leftNode < rightNode)
			{
				small = false;
				for (long j = c->fTo + 1; j < rightNode; j++)
					if (n[j].x < c->fX1)
					{
						crossBar = true;
						break;
					}
				if (!crossBar)
					return 0;
			}
		}
		if (small)
		{
			if (k - idx > 1)
				return 0;
			int32_t down = n1Bottom - top;
			if (down > h / 2)
				return 0;
			int32_t across = right - n1Left;
			if (across > cw && down > h / 4)
				return 0;
			if (across > h / 4)
				return 0;
		}
	}
	if (!crossBar && k + 1 < realCount && e->fNext == -1)
	{
		tag_CHUNK* x = e + 1;
		long dir = x->fDir + 12 - c->fDir;
		if (dir > 23)
			dir -= 24;
		long xl = x_in_line(c->fX0, c->fY0, c->fX1, c->fY1, x->fY1);
		int32_t up = c->fBottom - x->fY1;
		if (up > h / 6 && up < h * 3 / 4)
			if (direct_suits(dir, 2, 5) || (direct_suits(dir, 0, 1) && x->fX1 < xl))
			{
				crossBar = true;
				tag_CHUNK* e2 = &chunks[real[k + 1]];
				endNode = e2->fTo;
				endStroke = e2->fStroke;
			}
	}
	if (chunks[real[k]].fNext != -1 && !crossBar)
		return 0;
	tag_CHUNK* p = &chunks[real[idx - 1]];
	int32_t x0 = c->fX0;
	int32_t barBottom = p->fBottom, barTop = p->fTop;
	tag_CHUNK* slant = nil;
	long start = 0;
	long small = h / 6;
	for (long j = idx - 1; j >= 0; j--)
	{
		tag_CHUNK* q = &chunks[real[j]];
		if (barBottom < q->fBottom)
			barBottom = q->fBottom;
		if (barTop > q->fTop)
			barTop = q->fTop;
		if (direct_suits(q->fDir, 7, 15) && (slant == nil || slant->fHeight < q->fHeight))
			slant = q;
		if (q->fBottom - q->fTop > small || q->fRight - q->fLeft > small)
			counted++;
		if (q->fPrev == -1)
		{
			start = j;
			break;
		}
	}
	if (counted > 3)
		return 0;
	tag_CHUNK* sc = &chunks[real[start]];
	long from = sc->fFrom;
	int32_t barW = x0 - sc->fLeft;
	int32_t barH = barBottom - barTop;
	int32_t bend = slant != nil ? slant->fBottom - slant->fTop : 0;
	if (barW * 3 < barH * 2 || h * 3 < barW * 2)
		return 0;
	if (h > barW * 3 * 2 || barH * 3 > h * 2)
		return 0;
	if (bend != 0)
	{
		brack_type* b = &brackets[p->fFirstBracket];
		if (!(b->fKind == 2 && b->fSign == 1))
		{
			int32_t drop = p->fY0 - p->fY1;
			if (drop > bend)
				bend = drop;
		}
	}
	if (slant != nil || barH > barW || h < barH * 2)
	{
		if (!crossBar && bend > h / 4)
			return 0;
		if (bend > h / 2)
			return 0;
	}
	if (!crossBar && bend == 0)
	{
		long dirBar = p->fDir + 12 - c->fDir;
		long dirEnd = dirBar;
		if (p->fMidX != -1 && p->f74 != 300)
			dirEnd = GetDirection(p->fMidX, p->fMidY, p->fX1, p->fY1) + 12 - c->fDir;
		if (!direct_suits(dirBar, 17, 20) && !direct_suits(dirEnd, 17, 20))
			return 0;
	}
	if (strokes[endStroke].fRight - n[endNode].x > c->fHeight * 2)
		return 0;
	AddDigitK(staff, from, endNode, 7, '7', 6);
	return 1;
}


#pragma mark - #

// ROM 0x0028bca8 (unnamed) - a # of four strokes of no more than four
// chunks each (eleven in all): two level ones (twice as wide as tall) of
// about one width and two upright ones of about one height, each pair
// overlapping at least half its extent and the other pair inside it, each
// level one crossing each upright one, the upright ones not crossing and
// pointing the same way within a step: a # over all four.  ==> 1, or 0.
static long
HashSign(tag_CHUNK_STAFF* staff)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_STK* strokes = staff->fStrokes;
	tag_wapx_type* n = staff->fNodes;
	if (staff->fStrokeCount < 4 || strokes[3].fLastChunk > 11)
		return 0;
	tag_STK* up2 = nil;
	tag_STK* up1 = nil;
	tag_STK* level2 = nil;
	tag_STK* level1 = nil;
	for (long s = 0; s < 4; s++)
	{
		tag_STK* st = &strokes[s];
		if (st->fLastChunk - st->fFirstChunk > 3)
			return 0;
		if (st->fWidth > st->fHeight * 2)
		{
			if (level2 != nil)
				return 0;
			if (level1 != nil)
				level2 = st;
			else
				level1 = st;
		}
		else
		{
			if (st->fHeight <= st->fWidth || up2 != nil)
				return 0;
			if (up1 != nil)
				up2 = st;
			else
				up1 = st;
		}
	}
	if (level1->fWidth - level2->fWidth > level1->fWidth / 2 || level2->fWidth - level1->fWidth > level2->fWidth / 2)
		return 0;
	if (up1->fHeight - up2->fHeight > up1->fHeight / 2 || up2->fHeight - up1->fHeight > up2->fHeight / 2)
		return 0;
	int32_t maxR, minR, minL, maxL;
	if (level1->fRight <= level2->fRight) { maxR = level2->fRight; minR = level1->fRight; }
	else { maxR = level1->fRight; minR = level2->fRight; }
	if (level1->fLeft >= level2->fLeft) { minL = level2->fLeft; maxL = level1->fLeft; }
	else { minL = level1->fLeft; maxL = level2->fLeft; }
	int32_t span = maxR - minL;
	if (span > (minR - maxL) * 2)
		return 0;
	if (up1->fRight - maxR > span / 8 || up2->fRight - maxR > span / 8)
		return 0;
	if (minL - up1->fLeft > span / 8 || minL - up2->fLeft > span / 8)
		return 0;
	int32_t maxB, minB, minT, maxT;
	if (up1->fBottom <= up2->fBottom) { maxB = up2->fBottom; minB = up1->fBottom; }
	else { maxB = up1->fBottom; minB = up2->fBottom; }
	if (up1->fTop >= up2->fTop) { minT = up2->fTop; maxT = up1->fTop; }
	else { minT = up1->fTop; maxT = up2->fTop; }
	span = maxB - minT;
	if (span > (minB - maxT) * 2)
		return 0;
	if (level1->fBottom - maxB > span / 8 || level2->fBottom - maxB > span / 8)
		return 0;
	if (minT - level1->fTop > span / 8 || minT - level2->fTop > span / 8)
		return 0;
	int32_t l1f = chunks[level1->fFirstChunk].fFrom, l1t = chunks[level1->fLastChunk].fTo;
	int32_t l2f = chunks[level2->fFirstChunk].fFrom, l2t = chunks[level2->fLastChunk].fTo;
	int32_t u1f = chunks[up1->fFirstChunk].fFrom, u1t = chunks[up1->fLastChunk].fTo;
	int32_t u2f = chunks[up2->fFirstChunk].fFrom, u2t = chunks[up2->fLastChunk].fTo;
	if (!CheckQIntersec(n, l1f, l1t, u1f, u1t) || !CheckQIntersec(n, l1f, l1t, u2f, u2t))
		return 0;
	if (!CheckQIntersec(n, l2f, l2t, u1f, u1t) || !CheckQIntersec(n, l2f, l2t, u2f, u2t))
		return 0;
	if (CheckQIntersec(n, u1f, u1t, u2f, u2t))
		return 0;
	if (GetAngleBetweenTwoDir(chunks[up2->fFirstChunk].fDir, chunks[up1->fFirstChunk].fDir) > 1)
		return 0;
	AddDigitK(staff, chunks[strokes[0].fFirstChunk].fFrom, chunks[strokes[3].fLastChunk].fTo, 0x47, '#', -1);
	return 1;
}


#pragma mark - 8

// ROM 0x0028c704 (unnamed) - whether chunk ci and its partner (the chunk
// before or after it) cross as an 8's middle does: the partner going on
// far enough below; a first chunk of value 501 not ending in a flat hook;
// and where c's first bracket ends (a line down, or an arc up then a
// line) the partner's curve crosses to the proper side by more than a
// third of c's width - or, with no such bracket, at a quarter or a third
// of the way down the two curves are on the proper sides of one another.
static long
EightCrossing(tag_CHUNK_STAFF* staff, long ci, long partner)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	brack_type* brackets = staff->fBrackets;
	tag_CHUNK* c = &chunks[ci];
	tag_CHUNK* pc = &chunks[partner];
	int32_t h = c->fHeight;
	long b0 = c->fFirstBracket;
	int32_t w = c->fWidth;
	if (partner > ci && pc->fNext != -1)
	{
		tag_CHUNK* t = &chunks[pc->fNext];
		int32_t d = c->fKind == 2 ? c->fY1 - t->fY1 : t->fY1 - c->fY1;
		if (d < h / 4)
			return 0;
	}
	if (partner < ci && c->f78 == 501 && pc->fHeight >= h && direct_suits(pc->fDir, 9, 15) && brackets[b0 + 1].fKind == 1)
	{
		tag_wapx_type* e = &n[brackets[b0].fTo];
		int32_t dx = c->fX1 - e->x;
		int32_t dy = e->y - c->fY1;
		if (h - h / 8 < dx * 2 && dx > dy * 4)
			return 0;
	}
	if ((c->fKind == 2 && brackets[b0].fKind == 1) || (c->fKind == 1 && brackets[b0 + 1].fKind == 1))
	{
		tag_wapx_type* e = &n[brackets[b0].fTo];
		if (e->y - c->fTop > h / 4)
		{
			long x = x_in_curve(n, pc, e->y);
			int32_t d = c->f78 == 501 ? e->x - (int32_t) x : (int32_t) x - e->x;
			if (w / 3 > -d)
				return 0;
			return 1;
		}
	}
	long parts = 4;
	int32_t y;
	if (c->fHeight > pc->fHeight * 2)
	{
		parts = 3;
		y = pc->fTop;
	}
	else if (pc->fHeight > c->fHeight * 5 / 4)
		y = c->fTop + h / 4;
	else
		y = c->fTop + h / 3;
	long xp = x_in_curve(n, pc, y);
	long xc = x_in_curve(n, c, y);
	int32_t d = c->f78 == 501 ? (int32_t) (xc - xp) : (int32_t) (xp - xc);
	return c->fHeight / (int32_t) parts >= d;
}


// ROM 0x0028c158 (unnamed) - an 8 from the curve chunk ci (class 500):
// paired with its taller neighbour in the stroke (the first of the pair
// going down), the two of a height and each with at most one tail of
// its own at either end - short, and turning the way an 8's curve does
// (or its turning noted as wrong, which then asks for a narrow tail) -
// the two not too far apart across, and crossing (EightCrossing): an 8
// over the pair and its tails; *first and *last the chunks it runs over.
// ==> 1, or 0.
static long
EightK(tag_CHUNK_STAFF* staff, long ci, int32_t* first, int32_t* last)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	brack_type* brackets = staff->fBrackets;
	tag_CHUNK* before = nil;
	tag_CHUNK* after = nil;
	Boolean afterWrong = false, beforeWrong = false, swapped = false;
	tag_CHUNK* c = &chunks[ci];
	long next = c->fNext;
	if (next == -1 && c->fPrev == -1)
		return 0;
	int32_t width = c->fWidth, h = c->fHeight;
	long partner = ci - 1;
	tag_CHUNK* a;
	tag_CHUNK* b;
	if (next == -1 || (c->fPrev != -1 && chunks[ci + 1].fHeight < chunks[ci - 1].fHeight))
	{
		a = &chunks[ci - 1];
		b = c;
	}
	else
	{
		partner = ci + 1;
		a = c;
		b = &chunks[ci + 1];
	}
	if (a->fKind != 2)
	{
		if (next == -1 || partner > ci)
			return 0;
		swapped = true;
		partner = next;
		a = c;
		b = &chunks[next];
	}
	tag_CHUNK* pc = &chunks[partner];
	if (h > pc->fHeight * 4 || pc->fHeight > h * 2)
		return 0;
	int32_t expected;
	if (c->f78 == 501)
		expected = partner < ci ? 502 : 501;
	else
		expected = partner >= ci ? 502 : 501;
	if (a->fPrev != -1)
	{
		before = &chunks[a->fPrev];
		if (before->fPrev != -1)
			return 0;
		if (h < before->fHeight * 3 / 2 && !swapped)
			return 0;
		if (before->fHeight > h || before->fWidth > h)
			return 0;
		if (before->fWidth > h * 3 / 4 && a->f74 == 500)
			return 0;
		int32_t turn;
		if (direct_suits(before->fDir, 3, 9))
			turn = 501;
		else if (direct_suits(before->fDir, 15, 20))
			turn = 502;
		else
			turn = n[before->fTo - 1].fDirOut < 12 ? 501 : 502;
		if (expected != turn)
			beforeWrong = true;
	}
	if (b->fNext != -1)
	{
		after = &chunks[b->fNext];
		if (after->fNext != -1)
			return 0;
		if (after->fHeight * 3 / 2 > h || after->fWidth > h)
			return 0;
		if (after->fWidth > h * 3 / 4 && b->f74 == 500)
			return 0;
		int32_t turn;
		if (direct_suits(after->fDir, 3, 9))
			turn = 501;
		else if (direct_suits(after->fDir, 15, 21))
			turn = 502;
		else
			turn = n[after->fTo - 1].fDirOut < 12 ? 501 : 502;
		if (expected != turn)
			afterWrong = true;
	}
	int32_t bh = b->fHeight, ah = a->fHeight;
	if (bh < ah * 3 / 4 && after != nil)
	{
		int32_t small = h / 6;
		if (after->fHeight > small || after->fWidth > small)
			return 0;
	}
	long end = brackets[c->fFirstBracket].fTo;
	if (ah > bh * 2 && (n[end].fDir & 0xff00) != 0)
		return 0;
	if (afterWrong && after->fWidth > h / 3)
		return 0;
	if (beforeWrong && before->fWidth > h / 4)
		return 0;
	if (bh * 3 < ah)
	{
		if (before == nil || beforeWrong)
			return 0;
		if (b->fY1 - before->fY0 >= h / 2)
			return 0;
	}
	int32_t x1 = b->fX1, x0 = a->fX0;
	int32_t d = x0 - x1;
	if (before != nil)
	{
		if (d < 0 && before->fX0 >= x0)
			x0 = before->fX0;
		else if (d > 0 && before->fX0 <= x0)
			x0 = before->fX0;
	}
	if (after != nil)
	{
		if (d < 0 && after->fX1 <= x1)
			x1 = after->fX1;
		else if (d > 0 && after->fX1 >= x1)
			x1 = after->fX1;
	}
	int32_t apart = HWRAbs(x0 - x1);
	if (apart * 3 / 2 > h || apart > width * 2)
		return 0;
	if (width * 4 / 3 < apart && c->fHeight - c->fHeight / 8 > pc->fHeight)
		return 0;
	if (EightCrossing(staff, ci, partner) == 0)
		return 0;
	long fromChunk = before == nil ? b->fPrev : a->fPrev;
	long toChunk = after == nil ? a->fNext : b->fNext;
	*first = (int32_t) fromChunk;
	*last = (int32_t) toChunk;
	AddDigitK(staff, chunks[fromChunk].fFrom, chunks[toChunk].fTo, 8, '8', 5);
	return 1;
}


#pragma mark - 4

// ROM 0x0028c9a4 (unnamed) - a 4 whose slant is the chunk before the real
// chunk idx (p, going down to the left, direction 19..0, one or two
// brackets not a single arc the wrong way) and whose upright is idx (c):
// the stroke walked back from p to its start (no more than three pieces
// of any size) and forward again to the first piece coming down more than
// a sixth of h (d, the 4's left side); then the proportions of a 4 - the
// cross-bar (p) running from d's foot to past c, c going on below it by a
// fifth, the parts before it not reaching right of it - and the gap
// between d and p where there is one.  A 4 over from the stroke's start
// to lastNode.  value 401 (an arc) is refused.  ==> 1, or 0.
static long
FourWithSlant(tag_CHUNK_STAFF* staff, long value, long idx, long h, long lastNode)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	brack_type* brackets = staff->fBrackets;
	long chunkCount = staff->fChunkCount;
	long realCount = staff->fRealCount;
	long counted = 0;
	tag_CHUNK* c = &chunks[real[idx]];
	tag_CHUNK* p = &chunks[real[idx - 1]];
	if (p->fPrev == -1 || value == 401)
		return 0;
	long b0 = p->fFirstBracket, b1 = p->fLastBracket;
	if (b1 - b0 > 1)
		return 0;
	if (brackets[b0].fKind == 2 && brackets[b0].fSign == 1 && b0 == b1)
		return 0;
	if (p->f74 == 300 && !direct_suits(p->fDir, 15, 19))
	{
		long x = x_in_curve(n, c, p->fY0);
		int32_t below = c->fBottom - p->fBottom;
		int32_t across = p->fRight - chunks[real[idx - 2]].fLeft;
		if (x - p->fX0 > across / 4 && below < c->fHeight * 3 / 4)
			return 0;
	}
	if (!direct_suits(p->fDir, 19, 0))
		return 0;
	int32_t maxRight = p->fRight, maxBottom = p->fBottom;
	long start = 0;
	long last = idx - 1;
	long k = last;
	if (k >= 0)
	{
		long small = h / 6;
		for ( ; k >= 0; k--)
		{
			tag_CHUNK* q = &chunks[real[k]];
			if (q->fRight > maxRight)
				maxRight = q->fRight;
			if (q->fBottom > maxBottom)
				maxBottom = q->fBottom;
			if (q->fBottom - q->fTop > small || q->fRight - q->fLeft > small)
				counted++;
			if (q->fPrev == -1)
			{
				start = k;
				break;
			}
		}
		if (counted > 3)
			return 0;
	}
	int32_t from = chunks[real[start]].fFrom;
	long down = -1;
	if (last > start)
	{
		long small = h / 6;
		for (k = start; last > k; k++)
		{
			tag_CHUNK* q = &chunks[real[k]];
			if (q->fY1 - q->fY0 > small)
			{
				down = k;
				break;
			}
			counted--;
		}
	}
	if (counted > 2 || down == -1)
		return 0;
	tag_CHUNK* d = &chunks[real[down]];
	int32_t hp;
	if (p->fDir == 19)
	{
		hp = d->fY1 - d->fY0;
		if (h - hp * 2 > h / 8)
			return 0;
	}
	else
		hp = p->fY0 - p->fY1;
	int32_t dY0 = d->fY0;
	int32_t span = c->fY1 - dY0;
	if (h > span)
		span = h;
	int32_t dLeft = d->fLeft;
	int32_t sixth = span / 6;
	if (c->fRight - dLeft - span > sixth)
		return 0;
	int32_t w = p->fX1 - dLeft;
	if (w < span / 8)
		return 0;
	if (c->fNext != -1)
		for (k = idx; k < realCount; k++)
		{
			tag_CHUNK* q = &chunks[real[k]];
			if (q->fPrev == -1)
				break;
			if (c->fX1 - q->fLeft > w / 3)
				return 0;
		}
	if (hp * 4 - span < span / 8 && d->fY1 - dY0 < span / 8)
		return 0;
	int32_t twoThirds = span * 2 / 3;
	if (hp - twoThirds > span / 8)
		return 0;
	if (maxBottom - p->fY1 - twoThirds > span / 8)
		return 0;
	int32_t below = c->fBottom - maxBottom;
	if (below < h / 5 || below < sixth)
		return 0;
	if (hp * 3 - span < span / 8 && chunks[chunkCount - 1].fTo > lastNode)
	{
		long ci = c->fNext != -1 ? real[idx + 1] : real[idx];
		long dir = chunks[ci + 1].fDir;
		long turn = 12 - c->fDir;
		if (turn > 0)
			dir += turn;
		if (dir > 23)
			dir -= 24;
		if (direct_suits(dir, 1, 5))
			return 0;
	}
	if (maxRight - p->fX1 > h / 8)
		return 0;
	if (c->f78 == 402 && p->fY1 - dY0 > hp / 6 && c->fBottom - maxBottom < span / 2)
	{
		int32_t midY = (p->fBottom + p->fTop) / 2;
		long xp = x_in_curve(n, p, midY);
		long xc = x_in_curve(n, c, midY);
		if (xp - xc >= w / 4)
			return 0;
	}
	if (dY0 - p->fY1 > hp / 2)
		return 0;
	if (w < hp / 4)
		return 0;
	if (start != down)
	{
		tag_CHUNK* s = &chunks[real[start]];
		int32_t sRight = s->fRight;
		long x = x_in_curve(n, p, s->fBottom);
		if (x == -1)
			x = p->fX1;
		int32_t gap = (int32_t) x - sRight;
		if (gap < hp / 4 || gap < w / 4)
			return 0;
		if ((sRight - d->fX0) * 3 > w / 6 + w)
			return 0;
		long q1 = v_QDistFromChord(c->fX0, c->fY0, c->fX1, c->fY1, s->fX0, s->fY0);
		tag_wapx_type* ln = &n[d->fLeftNode];
		long q2 = v_QDistFromChord(c->fX0, c->fY0, c->fX1, c->fY1, ln->x, ln->y);
		if (q2 > q1 * 4)
			return 0;
		if (q1 * 9 < q2 * 4 && s->f78 == 402 && d->fTo - d->fFrom > 2
			&& GetAngleBetweenTwoDir(s->fDir, n[d->fFrom].fDirOut) < 8)
			return 0;
	}
	else
	{
		int32_t gap = c->fX0 - chunks[real[start]].fRight;
		if (gap < hp / 4 || gap < w / 4)
			return 0;
	}
	long t = take_next_point(n, d->fTo + 1, d->fFrom, 0, 0, hp / 4);
	if (t != -1 && maxBottom - n[t].y > (maxBottom - d->fY0) / 2)
	{
		int32_t topX = n[c->fTopNode].x;
		int32_t reach = topX - d->fLeft;
		int32_t edge;
		if (d->fRightNode < d->fLeftNode && d->fRightNode != d->fFrom)
			edge = d->fRight;
		else
			edge = d->fX0;
		if (d->fPrev != -1 && chunks[d->fPrev].fRight > edge)
			edge = chunks[d->fPrev].fRight;
		if (reach > (topX - edge) * 2)
			return 0;
	}
	AddDigitK(staff, from, lastNode, 4, '4', 4);
	return 1;
}


// ROM 0x00289a0c (unnamed) - a 4 from the line or arc object over the real
// chunk idx (its upright, c).  In one stroke it may be a 4 written without
// lifting the pen (OneStrokeFour), or with its slant just before
// (FourWithSlant).  Otherwise the 4's other parts are looked for in the
// stroke after it (BarStroke: a bar that comes down, as a 4 written
// upright first) or before it (UprightStart: the left side and bar
// written first, the piece of stroke before c, `*after` saying where the
// last 4 found ended so the same parts are not used twice): a, the piece
// the bar ends with, and d, the left side coming down.  The proportions
// then decide - the bar level and reaching c, d a proper height against
// c, the two not in line - and the kind of 4: 4 (the letter table's
// first variant, extra 1), 41 (an open top, extra 2) or, when d's top
// turns too sharply to be a 4's corner and ShortTopBar says so, none.
// ROM QUIRK: the third kind the code handles, 44 (extra 3), is never
// made.  ==> 1 when something was found or taken as found, else 0.
static long
FourK(tag_CHUNK_STAFF* staff, long value, long idx, int32_t* after)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* n = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	long realCount = staff->fRealCount;
	tag_CHUNK* pp = nil;
	int32_t start = -1;
	long kind = 0;
	tag_CHUNK* p = nil;
	tag_CHUNK* c = &chunks[real[idx]];
	int32_t cw = c->fWidth, h = c->fHeight;
	int32_t lastNode = c->fTo;
	long upright = idx;
	if (c->fNext != -1)
	{
		// (the ROM's third argument register holds the chunks here: never nil)
		if (idx + 1 < realCount && c->fY1 < c->fY0 && OneStrokeFour(staff, idx, chunks) != 0)
			return 1;
		tag_CHUNK* nx = &chunks[real[idx + 1]];
		if (nx->fNext != -1)
			return 0;
		if (nx->fY1 > nx->fY0 || nx->fHeight * 3 > h)
			return 0;
		if (h < nx->fHeight * 4 && nx->fWidth - cw > h / 10)
			return 0;
		if (nx->fWidth - cw > h / 6)
			return 0;
		if (nx->fX0 - nx->fX1 > h / 8 && (n[nx->fFrom].fDir & 0xff00) == 0)
			return 0;
		lastNode = nx->fTo;
		upright = idx + 1;
	}
	if (c->fPrev != -1)
	{
		if (idx == 0)
			goto bar;
		if (FourWithSlant(staff, value, idx, h, lastNode) != 0)
			return 1;
	}
	if (idx == 0)
		goto bar;
	if (c->fPrev != -1)
	{
		p = &chunks[real[idx - 1]];
		if (p->fPrev != -1 || idx < 2 || h < p->fHeight * 2)
			return 0;
		if (p->fX1 - p->fX0 > h / 8)
			return 0;
		pp = p - 1;
		start = (int32_t) (idx - 2);
	}
	else
	{
		pp = c - 1;
		start = (int32_t) (idx - 1);
	}
bar:
	int32_t first = 0, from = 0, dir = 0;
	tag_BOX box;
	if (BarStroke(staff, upright, h, c, &first, &start, &lastNode, &box, &dir) != 0)
	{
		from = p == nil ? c->fFrom : p->fFrom;
		kind = 4;
	}
	else
	{
		if (start < 0 || idx == 0)
			return 0;
		if (*after != -1 && start <= *after)
			return 0;
		dir = pp->fDir;
		if (dir > 6)
			dir += 12 - c->fDir;
		if (UprightStart(staff, start, h, &from, &first, &box) == 0)
			return 0;
	}
	tag_CHUNK* a = &chunks[real[start]];
	tag_CHUNK* d = &chunks[real[first]];
	int32_t dTop = d->fTop, dLeft = d->fLeft;
	int32_t aTo = a->fTo;
	if (start == first && c->fY1 - d->fY1 <= h / 3)
	{
		if (d->f78 == 301 || d->f78 == 401)
			return 0;
		if (d->f78 == 402 && d->fLength2 > d->fBulge * 25)
			return 0;
	}
	int32_t sixth = h / 6;
	if (c->fBottom - a->fY1 < sixth)
		return 0;
	if (c->fLeft - a->fX1 > h / 2)
		return 0;
	if (!direct_suits(dir, 21, 6))
		return 0;
	int32_t ex, ey;
	if (a->fRightNode != d->fFrom)
	{
		ex = n[a->fRightNode].x;
		ey = n[a->fRightNode].y;
	}
	else
	{
		ex = a->fX1;
		ey = a->fY1;
	}
	if (!direct_suits(GetDirection(n[d->fTopNode].x, n[d->fTopNode].y, ex, ey), 10, 17))
		return 0;
	int32_t barH = box.bottom - dTop;
	int32_t barW = a->fX1 - dLeft;
	int32_t cH = c->fBottom - dTop;
	if (cH < h)
		cH = h;
	long xc = x_in_curve(n, c, a->fY1);
	if (xc == -1)
		xc = (c->fX1 + c->fX0) / 2;
	int32_t rightNode = a->fRightNode;
	int32_t off = (int32_t) xc - (rightNode <= (a->fTo + d->fFrom) / 2 ? a->fX1 : a->fRight);
	if ((n[c->fTopNode].x + n[c->fBottomNode].x) / 2 - dLeft - cH > cH / 4)
		return 0;
	int32_t aX1 = a->fX1;
	if (box.right - aX1 > barW / 6 && box.right != d->fX0)
	{
		if (a->fRight < box.right)
			return 0;
		if (!direct_suits(GetDirection(n[rightNode].x, n[rightNode].y, aX1, a->fY1), 4, 8))
			return 0;
	}
	if (direct_suits(find_direct_forward(n, a->fTo + 1, d->fFrom, barW / 3, 0, 0), 4, 6))
		return 0;
	int32_t topX = n[c->fTopNode].x;
	Boolean turnCheck = topX - d->fX0 < barW / 4;
	if (!turnCheck && d->fPrev != -1)
		turnCheck = topX - chunks[d->fPrev].fRight < barW / 4;
	if (turnCheck)
	{
		long back = find_direct_backward(n, d->fLeftNode, 0, 0, barH / 4);
		long on = find_direct_forward(n, a->fTo + 1, d->fLeftNode, 0, 0, barH / 4);
		if (distance_between_directions(back, on) < 5)
			return 0;
	}
	int32_t aY1 = a->fY1;
	if (aY1 - d->fY0 < barH / 4 && a->fX1 - d->fX0 < barH / 4)
		return 0;
	int32_t barH3 = barH * 3;
	if (barW - barH3 > h / 8)
		return 0;
	if (barH / 8 > barW || h > barH * 4)
		return 0;
	int32_t cH3 = cH / 3;
	if (cH3 < off)
		return 0;
	if (barH * 2 / 3 < off)
		return 0;
	if (off > cH / 4 || barH / 2 < off || barW * 3 < off * 2)
		kind = 0x29;
	else
	{
		int32_t dropTop = (d->fTop - c->fTop) * 3;
		if (!(dropTop <= (c->fBottom - a->fBottom) * 2 && dropTop <= barH))
			if (barH - off * 2 < barH / 4 || off > barW)
				kind = 0x29;
	}
	if (h - barH3 > h / 8 && c->fY0 + h / 3 > aY1)
		return 0;
	int32_t tail = c->fBottom - box.bottom;
	if (tail < sixth)
		return 0;
	if (tail < h / 4 && (int32_t) xc - n[aTo].x > h / 10)
		return 0;
	if (c->fLeft - a->fX1 > sixth || barH - barW * 3 > h / 8)
	{
		if (cH3 - cH / 8 > barH)
			return 0;
		if (cH * 2 / 3 + cH / 8 < barH)
			return 0;
		if (tail <= cH3 - cH / 8)
			return 0;
		if (tail >= h * 2 / 3 + h / 8)
			return 0;
	}
	if (barW - barH * 2 > h / 8 || direct_suits(dir, 4, 6))
		if ((int32_t) xc - dLeft < barW / 2)
			return 0;
	if (kind != 4)
	{
		tag_wapx_type* t = &n[d->fTopNode];
		Boolean sharp = direct_suits(t->fDir, 16, 21) != 0;
		if (!sharp && t->fDir == 15 && (t->fFlags & 3) == 0 && direct_suits(n[d->fTopNode - 1].fDirOut, 16, 18))
			sharp = true;
		if (sharp)
			kind = ShortTopBar(staff, d) ? 4 : -1;
	}
	if (kind == 0 || kind == 4)
		AddDigitK(staff, from, lastNode, 4, '4', 1);
	else if (kind == 0x29)
		AddDigitK(staff, from, lastNode, 0x29, '4', 2);
	else if (kind == 0x2c)
		AddDigitK(staff, from, lastNode, 0x2c, '4', 3);
	else
		return 1;
	*after = start;
	return 1;
}


#pragma mark - the searcher

// ROM 0x00289604 SearchDigit_K__FP15tag_CHUNK_STAFF
// The digits made of lines and arcs.  A # first (HashSign); then each
// line (class 300) of one chunk, not the first two where it could be an x
// of two strokes (TwoStrokeX), not wider than tall nor tiny: a 4 (FourK),
// failing that a 7 (SevenK), failing that a per cent sign (PercentSign);
// each arc (class 400) likewise a 4, if its first bracket is flat enough
// (value 401 must be five times, 402 three times as long as it bulges);
// each curve (class 500) not a whole stroke of its own and not inside the
// last 8 found, an 8 (EightK).  ROM QUIRK: the arcs' bracket test reads
// the same bracket every time round its loop, so only the first is
// tested.  ==> the last EightK's answer, -1 when there was no curve to
// try.
long
SearchDigit_K(tag_CHUNK_STAFF* staff)
{
	void* lo = staff->fLO;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_wapx_type* nodes = staff->fNodes;
	brack_type* brackets = staff->fBrackets;
	long result = -1;
	tag_LOWOBJ* obj = nil;
	int32_t height = staff->fHeight;
	ULong was = LO_GetWorkClassID(lo);
	HashSign(staff);
	int32_t after = -1;
	LO_SetWorkClass(lo, 300);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		long m = LO_HowManyChunks(lo, obj);
		long first = LO_GetRealChunkInd(lo, chunks, nodes, obj, 1);
		long last = LO_GetRealChunkInd(lo, chunks, nodes, obj, m);
		if (last != first)
			continue;
		if (first <= 1 && TwoStrokeX(staff, first) != 0)
			continue;
		if (ChunkNotWide(chunks, real[last]) == 0 || ChunkTooSmall(chunks, real[last], height) != 0)
			continue;
		if (FourK(staff, obj->fValue, first, &after) != 0)
			continue;
		if (SevenK(staff, first) == 0)
			PercentSign(staff, first);
	}
	LO_SetWorkClass(lo, 400);
	after = -1;
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		long m = LO_HowManyChunks(lo, obj);
		long first = LO_GetRealChunkInd(lo, chunks, nodes, obj, 1);
		long last = LO_GetRealChunkInd(lo, chunks, nodes, obj, m);
		if (last != first)
			continue;
		if (first <= 1 && TwoStrokeX(staff, first) != 0)
			continue;
		if (ChunkNotWide(chunks, real[last]) == 0 || ChunkTooSmall(chunks, real[last], height) != 0)
			continue;
		int32_t value = obj->fValue;
		long times = value == 401 ? 25 : 9;
		tag_CHUNK* c = &chunks[real[first]];
		long b = c->fFirstBracket;
		if (c->fLastBracket >= b)
		{
			brack_type* br = &brackets[b];
			int32_t limit = (int32_t) (times * br->fHeight2);
			Boolean flat = true;
			for ( ; c->fLastBracket >= b; b++)
				if (br->fLength2 < limit)
				{
					flat = false;
					break;
				}
			if (!flat)
				continue;
		}
		FourK(staff, value, first, &after);
	}
	int32_t eightFrom = -1, eightTo = -1;
	LO_SetWorkClass(lo, 500);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		long m = LO_HowManyChunks(lo, obj);
		long first = LO_GetRealChunkInd(lo, chunks, nodes, obj, 1);
		long last = LO_GetRealChunkInd(lo, chunks, nodes, obj, m);
		if (eightFrom != -1 && eightTo != -1 && real[first] >= eightFrom && real[last] <= eightTo)
			continue;
		if (chunks[real[last]].fNext == -1 && chunks[real[first]].fPrev == -1)
			continue;
		result = EightK(staff, real[first], &eightFrom, &eightTo);
	}
	LO_SetWorkClass(lo, was);
	return result;
}
