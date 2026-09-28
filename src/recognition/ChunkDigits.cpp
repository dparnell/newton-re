/*
	File:		ChunkDigits.cpp

	Contains:	The digit reader's first passes over the chunks, the ones
				Digits runs before any searcher: the writing's line
				(DefHeightsForNumber - the strokes' boxes put together, the
				mean top and bottom taken, each chunk's ends placed in the
				line) and the circles (GetCircles - a chunk going down and
				the next one coming back up, turning far enough, closed
				enough and round enough to be an 0, put in the list of low
				objects as class 200).  See Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x002853ec-0x00285a64, 0x00288d4c-0x00289604); each
				function cites its origin.  GetCircles and the four
				unnamed helpers are from the disassembly: the decompiler
				drops HWRAbs's argument in one and reads a quotient as a
				remainder in the other.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRMemoryAlloc, HWRMemoryFree, HWRAbs
#include <string.h>


#pragma mark - the searchers' geometry

// ROM 0x002a7fd0 direct_suits__FiN21
// Whether a direction lies from lo round to hi (anticlockwise; when hi is
// below lo the range wraps past straight up).
long
direct_suits(long dir, long lo, long hi)
{
	if (hi > lo)
		return lo <= dir && dir <= hi;
	if (hi < lo)
		return dir >= lo || dir <= hi;
	return dir == lo;
}


// ROM 0x002a83d8 distance_between_directions__FiT1
// How many steps apart two directions are, the short way round (0..12).
long
distance_between_directions(long a, long b)
{
	long d = HWRAbs(a - b);
	if (d > 12)
		d = 24 - d;
	return d;
}


// The test take_next_point and take_prev_point make of a node dx across
// and dy up or down from where they started: past both limits when both
// are given, past the one given, or further in all than sum; with no
// limit at all the first node is taken.
static bool
PastLimits(long ax, long ay, long dx, long dy, long sum)
{
	if (dx != 0)
	{
		if (dy != 0 && ax > dx && ay > dy)
			return true;
		if (dy == 0)
		{
			if (ax > dx)
				return true;
			goto summed;
		}
	}
	if (dy != 0 && dx == 0 && ay > dy)
		return true;
summed:
	if (sum != 0)
	{
		if (ax + ay > sum)
			return true;
		if (sum >= 1)
			return false;
	}
	return !(dx >= 1 || dy >= 1);
}


// ROM 0x002a7868 take_next_point__FP13tag_wapx_typeiN42
// The first node after start (up to end, not including it) further from
// start than the limits (PastLimits).  ==> it, -1 for none.
long
take_next_point(tag_wapx_type* n, long end, long start, long dx, long dy, long sum)
{
	int32_t x0 = n[start].x, y0 = n[start].y;
	for (long k = start + 1; k < end; k++)
	{
		long ax = HWRAbs(n[k].x - x0);
		long ay = HWRAbs(n[k].y - y0);
		if (PastLimits(ax, ay, dx, dy, sum))
			return k;
	}
	return -1;
}


// ROM 0x002a7980 take_prev_point__FP13tag_wapx_typeiN32
// The same looking back from start to node 0.
long
take_prev_point(tag_wapx_type* n, long start, long dx, long dy, long sum)
{
	int32_t x0 = n[start].x, y0 = n[start].y;
	for (long k = start - 1; k >= 0; k--)
	{
		long ax = HWRAbs(n[k].x - x0);
		long ay = HWRAbs(n[k].y - y0);
		if (PastLimits(ax, ay, dx, dy, sum))
			return k;
	}
	return -1;
}


// ROM 0x002a82a4 x_in_line__FiN41
// Where the line through (x1, y1) and (x2, y2) is at height y: x1 when the
// line is upright or y is y1's, -1 when it is level.
long
x_in_line(long x1, long y1, long x2, long y2, long y)
{
	if (x2 == x1 || y == y1)
		return x1;
	if (y2 == y1)
		return -1;
	return (int32_t) ((x2 - x1) * (y - y1)) / (int32_t) (y2 - y1) + x1;
}


// ROM 0x002a8174 x_in_curve__FP13tag_wapx_typeP9tag_CHUNKi
// Where a chunk's polyline is at height y: between the first two nodes
// either side of it (the x of a node exactly at it), or when it never
// gets there the x of its top end if that is below y, else of its bottom.
long
x_in_curve(tag_wapx_type* n, tag_CHUNK* c, long y)
{
	long top, bottom;
	if (c->fY0 <= c->fY1)
	{
		bottom = c->fTo;
		top = c->fFrom;
	}
	else
	{
		bottom = c->fFrom;
		top = c->fTo;
	}
	long prev = -1, found = -1;
	for (long k = c->fFrom; k <= c->fTo; k++)
	{
		int32_t yk = n[k].y;
		if (yk == y)
			return n[k].x;
		if (prev != -1)
		{
			if ((yk > y && n[prev].y < y) || (yk < y && n[prev].y > y))
			{
				found = k;
				break;
			}
		}
		prev = k;
	}
	if (found == -1)
	{
		if (n[top].y > y)
			return n[top].x;
		return n[bottom].x;
	}
	long lower, upper;
	if (n[prev].y <= n[found].y)
	{
		lower = found;
		upper = prev;
	}
	else
	{
		lower = prev;
		upper = found;
	}
	return x_in_line(n[upper].x, n[upper].y, n[lower].x, n[lower].y, y);
}


// ROM 0x002a82f4 cross_with_line__FP13tag_wapx_typeP9tag_CHUNKiN33
// Whether the segment (x1, y1)-(x2, y2) crosses the chunk's polyline: for
// each of its steps that reaches the segment's height, where the segment
// is at the step's middle height lies within the step's span across.
long
cross_with_line(tag_wapx_type* n, tag_CHUNK* c, long x1, long y1, long x2, long y2)
{
	for (long k = c->fFrom; k < c->fTo; k++)
	{
		int32_t ya = n[k].y, yb = n[k + 1].y;
		int32_t yMax = ya <= yb ? yb : ya;
		int32_t yMin = ya >= yb ? yb : ya;
		if (yMin > y1 && yMin > y2)
			continue;
		if (yMax < y1 && yMax < y2)
			continue;
		int32_t xa = n[k].x, xb = n[k + 1].x;
		int32_t xMax = xa <= xb ? xb : xa;
		int32_t xMin = xa >= xb ? xb : xa;
		long x = x_in_line(x1, y1, x2, y2, (ya + yb) / 2);
		if (x > xMax)
			continue;
		if (x >= xMin)
			return 1;
	}
	return 0;
}


#pragma mark - the line

// ROM 0x00285508 (unnamed) - the strokes' boxes copied into an array of
// tag_BOX, one a stroke
static void
StrokeBoxes(tag_STK* strokes, long count, tag_BOX* boxes)
{
	for (long k = 0; k < count; k++)
	{
		boxes[k].left = strokes[k].fLeft;
		boxes[k].right = strokes[k].fRight;
		boxes[k].top = strokes[k].fTop;
		boxes[k].bottom = strokes[k].fBottom;
	}
}


// A box taken out of the array, those after it moved down.
static void
RemoveBox(tag_BOX* boxes, long at, long count)
{
	for (long k = at; k < count; k++)
		boxes[k] = boxes[k + 1];
}


// ROM 0x00285554 (unnamed) - a box that overlaps the one before it across
// the line meant to be joined to it.  ==> how many boxes there are now.
// ROM BUG: the test that the two are close enough is HWRAbs(0) * 3 >
// the earlier box's height - the argument is a register the code set to
// nought and never loaded (the disassembly: `mov r0,r9` with r9 = 0) -
// and a height is never below nought, so no two boxes are ever joined.
static long
JoinOverlappingBoxes(tag_BOX* boxes, long count)
{
	for (long k = 1; k < count; k++)
	{
		tag_BOX* b = &boxes[k];
		tag_BOX* a = &boxes[k - 1];
		int32_t height = a->bottom - a->top;
		if (b->left > a->right)
			continue;
		if (!(HWRAbs(0) * 3 > height))
			continue;
		if (a->left > b->left)
			a->left = b->left;
		if (a->right <= b->right)
			a->right = b->right;
		if (a->top >= b->top)
			a->top = b->top;
		if (a->bottom < b->bottom)
			a->bottom = b->bottom;
		count--;
		RemoveBox(boxes, k, count);
		k--;
	}
	return count;
}


// ROM 0x0028564c (unnamed) - the small boxes dealt with: one under a
// quarter of the tallest and under the mean height dropped; one under
// half the tallest, or under the mean, joined to the nearer neighbour
// (across the line) when the gap to it is under the mean gap and the two
// do not share most of their height - its bottom more than a third of
// the neighbour's height above the neighbour's, or its top more than a
// third below.  ==> how many boxes there are now.
static long
DropSmallBoxes(tag_BOX* boxes, long count)
{
	int32_t gaps = 0;
	int32_t widest = 0;
	int32_t mean = boxes[0].bottom - boxes[0].top;
	int32_t tallest = mean;
	for (long k = 1; k < count; k++)
	{
		int32_t h = boxes[k].bottom - boxes[k].top;
		if (tallest < h)
			tallest = h;
		mean += h;
		int32_t gap = boxes[k].left - boxes[k - 1].right;
		if (gap < 0)
			gap = 0;
		gaps += gap;
		if (widest < gap)
			widest = gap;
	}
	if (count != 0)
	{
		mean = mean / (int32_t) count;
		if (count > 1)
			gaps = gaps / (int32_t) (count - 1);
	}
	for (long k = 0; k < count; k++)
	{
		tag_BOX* b = &boxes[k];
		int32_t top = b->top;
		int32_t bottom = b->bottom;
		int32_t h = bottom - top;
		if (h * 4 < tallest && h < mean)
		{
			count--;
			RemoveBox(boxes, k, count);
			k--;
			continue;
		}
		if (!(h * 2 < tallest) && !(h < mean))
			continue;
		tag_BOX* other;
		int32_t gap;
		if (k == 0)
		{
			if (count < 2)
				continue;
			other = &boxes[1];
			gap = boxes[1].left - boxes[0].right;
		}
		else if (k + 1 < count)
		{
			int32_t before = b->left - boxes[k - 1].right;
			int32_t after = boxes[k + 1].left - b->right;
			if (before < after)
			{
				other = &boxes[k - 1];
				gap = before;
			}
			else
			{
				other = &boxes[k + 1];
				gap = after;
			}
		}
		else
		{
			other = &boxes[k - 1];
			gap = b->left - boxes[k - 1].right;
		}
		if (other == nil || gap >= gaps)
			continue;
		int32_t otherTop = other->top;
		int32_t otherBottom = other->bottom;
		int32_t third = (otherBottom - otherTop) / 3;
		if (!(otherBottom - bottom > third) && !(top - otherTop > third))
			continue;
		if (other->left > b->left)
			other->left = b->left;
		if (other->right <= b->right)
			other->right = b->right;
		other->top = otherTop < top ? otherTop : top;
		other->bottom = bottom < otherBottom ? otherBottom : bottom;
		count--;
		RemoveBox(boxes, k, count);
		k--;
	}
	return count;
}


// ROM 0x00285960 (unnamed) - where (x, y) is in the line: the box whose
// span across the line holds x (the mean of two when the next one holds
// it too; the first box left of them all, the last right of them, the
// mean lines in a gap), its height cut by a quarter at each end - 60 at
// or above what is left, 45 inside it, 30 below.
static UByte
ZoneOf(int32_t x, int32_t y, tag_BOX* boxes, long count, int32_t* heights)
{
	long k;
	for (k = 0; k < count; k++)
		if (boxes[k].left <= x && boxes[k].right >= x)
			break;
	int32_t top, bottom;
	if (k == count)
	{
		if (x < boxes[0].left)
		{
			bottom = boxes[0].bottom;
			top = boxes[0].top;
		}
		else if (boxes[count - 1].right >= x)
		{
			top = heights[1];
			bottom = heights[2];
		}
		else
		{
			bottom = boxes[count - 1].bottom;
			top = boxes[count - 1].top;
		}
	}
	else if (k + 1 < count && boxes[k + 1].left <= x && boxes[k + 1].right >= x)
	{
		bottom = (boxes[k].bottom + boxes[k + 1].bottom) / 2;
		top = (boxes[k].top + boxes[k + 1].top) / 2;
	}
	else
	{
		bottom = boxes[k].bottom;
		top = boxes[k].top;
	}
	int32_t q = (bottom - top) / 4;
	bottom -= q;
	top += q;
	if (y <= top)
		return 60;
	return y < bottom ? 45 : 30;
}


// ROM 0x002858b8 (unnamed) - each chunk that is not a jump has its end
// (and, a stroke's first, its start) placed in the line.
static void
PlaceChunks(tag_CHUNK* chunks, long count, tag_BOX* boxes, long nBoxes, int32_t* heights)
{
	for (long k = 0; k < count; k++)
	{
		tag_CHUNK* c = &chunks[k];
		if (c->fKind == 3)
			continue;
		if (c->fPrev == -1)
			c->fZoneStart = ZoneOf(c->fX0, c->fY0, boxes, nBoxes, heights);
		c->fZoneEnd = ZoneOf(c->fX1, c->fY1, boxes, nBoxes, heights);
	}
}


// ROM 0x002853ec DefHeightsForNumber__FP15tag_CHUNK_STAFF
// The writing's line: the strokes' boxes, the small ones dropped or
// joined to a neighbour, the mean top and bottom of what is left - the
// staff's fTopLine, fBottomLine and their difference fHeight, nought when
// there is nothing (or no memory) - and each chunk's ends placed in it.
void
DefHeightsForNumber(tag_CHUNK_STAFF* staff)
{
	int32_t top = 0, bottom = 0;
	tag_CHUNK* chunks = staff->fChunks;
	long count = staff->fChunkCount;
	long n = staff->fStrokeCount;
	staff->fBottomLine = 0;
	staff->fTopLine = 0;
	staff->fHeight = 0;
	tag_BOX* boxes = (tag_BOX*) HWRMemoryAlloc(n << 4);
	if (boxes == nil)
		return;
	StrokeBoxes(staff->fStrokes, n, boxes);
	n = JoinOverlappingBoxes(boxes, n);
	n = DropSmallBoxes(boxes, n);
	if (n != 0)
	{
		for (long k = 0; k < n; k++)
		{
			bottom += boxes[k].bottom;
			top += boxes[k].top;
		}
		bottom = bottom / (int32_t) n;
		top = top / (int32_t) n;
		staff->fHeight = bottom - top;
		staff->fTopLine = top;
		staff->fBottomLine = bottom;
		PlaceChunks(chunks, count, boxes, n, &staff->fHeight);
	}
	HWRMemoryFree((Ptr) boxes);
}


#pragma mark - circles

// The direction a node leaves in (a corner keeps two: in | out << 8).
static inline ULong
DirOut(tag_wapx_type* n, long k)
{
	return (n[k].fFlags & kApxCorner) ? (ULong) (n[k].fDir >> 8) : (ULong) n[k].fDir;
}


// ROM 0x00288d4c GetCircles__FP15tag_CHUNK_STAFF
// The circles - a 0, or the loop of a 6, 8 or 9 - put in the list of low
// objects as class 200 over their nodes, each one's object kept in the
// chunk it starts at (f7C).  A circle is a chunk going down (sub 402, an
// arc) followed by one coming back up (sub 402 or class 300, a line),
// the taller of the two between two thirds and four thirds of the
// writing's height, whose directions turn by more than eleven steps
// (sixteen when its ends are further apart than a fifth of that height)
// counting the chunks either side that carry the turn on; it must be
// narrower than one and a half times its height where its ends are far
// apart, its end not far right of its start, and a handful of shapes a
// 2, a 3 or a 6 make are turned away.  ==> 1.
long
GetCircles(tag_CHUNK_STAFF* staff)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* n = staff->fNodes;
	long count = staff->fChunkCount;
	int32_t H = staff->fHeight;
	void* lo = staff->fLO;
	for (long i = 0; i < count - 1; i++)
	{
		tag_CHUNK* c = &chunks[i];
		if (c->fKind != 2 || c->fNext == -1)
			continue;
		tag_CHUNK* next = &chunks[i + 1];
		if (!(next->fKind == 1 && c->f78 == 402))
			continue;
		if (!(next->f78 == 402 || next->f74 == 300))
			continue;
		long from = c->fFrom;
		long to = next->fTo;
		long start = from, end = to;
		int32_t down = c->fY1 - c->fY0;
		int32_t up = next->fY0 - next->fY1;
		int32_t tall = down > up ? down : up;
		if (!(tall < (H << 2) / 3))
			continue;
		if (!(tall > (H << 1) / 3))
			continue;

		int32_t minX = n[from].x, maxX = minX;
		int32_t minY = n[from].y, maxY = minY;
		ULong startDir = DirOut(n, from);
		ULong dir = startDir;
		int32_t turn = 0;
		for (long k = from + 1; k <= to - 1; k++)
		{
			ULong d;
			if (n[k].fFlags & kApxCorner)
				d = (ULong) GetDirection(n[k - 1].x, n[k - 1].y, n[k + 1].x, n[k + 1].y);
			else
				d = (ULong) n[k].fDir;
			turn += GetAngleBetweenTwoDir(dir, d);
			dir = d;
			if (n[k].x < minX)
				minX = n[k].x;
			else if (n[k].x > maxX)
				maxX = n[k].x;
			if (n[k].y < minY)
				minY = n[k].y;
			else if (n[k].y > maxY)
				maxY = n[k].y;
		}
		ULong last = (n[to].fFlags & kApxCorner) ? (ULong) (n[to].fDir & 0xff) : (ULong) n[to].fDir;
		long a = GetAngleBetweenTwoDir(dir, last);
		if (a < 0)
		{
			last = (ULong) GetDirection(n[to - 1].x, n[to - 1].y, n[to].x, n[to].y);
			a = GetAngleBetweenTwoDir(dir, last);
		}
		turn += a;
		if (n[to].x < minX)
			minX = n[to].x;
		else if (n[to].x > maxX)
			maxX = n[to].x;
		if (n[to].y < minY)
			minY = n[to].y;
		else if (n[to].y > maxY)
			maxY = n[to].y;

		// the chunk before carries the turn on: it starts right of this
		// one and is an arc or a line (the circle then starts where it
		// does), or its last step goes left into this one's start
		if (i > 0 && chunks[i - 1].fKind != 3)
		{
			tag_CHUNK* prev = &chunks[i - 1];
			ULong d;
			bool counted = false;
			if (prev->fX0 > c->fX0 && (prev->f78 == 402 || prev->f74 == 300))
			{
				start = prev->fFrom;
				d = prev->fDir;
				counted = true;
			}
			else
			{
				long pt = prev->fTo;
				if (n[pt].x < n[pt - 1].x)
				{
					start = pt - 1;
					d = (ULong) GetDirection(n[start].x, n[start].y, n[from].x, n[from].y);
					counted = true;
				}
			}
			if (counted)
				turn += GetAngleBetweenTwoDir(d, startDir);
		}
		// and the chunk after next, going back left and ending above
		// this one's end, when it turns on the same way
		if (i < count - 2 && chunks[i + 2].fKind != 3)
		{
			tag_CHUNK* c2 = &chunks[i + 2];
			if (c2->fX1 < c2->fX0 && c2->fY1 < c->fY1)
			{
				long a2 = GetAngleBetweenTwoDir(last, c2->fDir);
				if (a2 >= 0)
				{
					end = c2->fTo;
					turn += a2;
				}
			}
		}

		int32_t dx = c->fX0 - next->fX1;
		int32_t dy = c->fY0 - next->fY1;
		int32_t gap2 = dy * dy + dx * dx;
		int32_t tall2 = tall * tall;
		int32_t near2 = tall2 / 25;
		if (near2 > gap2)
		{
			if (turn <= 11)
				continue;
		}
		else
		{
			if (turn <= 16)
				continue;
			if (!(maxX - minX < ((maxY - minY) * 3) / 2))
				continue;
		}
		if (n[to].x > n[from].x && n[to].x - n[from].x >= ((maxX - minX) * 2) / 3)
			continue;
		if (next->fDir >= 17 && next->fDir <= 20)
		{
			ULong back = (ULong) GetDirection(n[c->fTo].x, n[c->fTo].y, n[c->fFrom].x, n[c->fFrom].y);
			if (GetAngleBetweenTwoDir(next->fDir, back) > 3)
				continue;
		}
		int32_t ex = n[start].x - n[end].x;
		int32_t ey = n[start].y - n[end].y;
		int32_t ends2 = ey * ey + ex * ex;
		if (HWRAbs(c->fY0 - next->fY1) >= tall / 4 || c->fX0 <= next->fX1)
		{
			if (ends2 > tall2 / 4)
				continue;
		}
		if (next->fNext != -1 && chunks[i + 2].fY1 > next->fY0 && chunks[i + 2].fLeft < minX)
			continue;
		if (HWRAbs(c->fY0 - next->fY1) < tall / 4)
		{
			bool test = true;
			if (next->fNext != -1)
			{
				ULong d1 = (ULong) GetDirection(n[to].x, n[to].y, n[to - 1].x, n[to - 1].y);
				ULong d2 = (ULong) GetDirection(n[to].x, n[to].y, n[to + 1].x, n[to + 1].y);
				test = GetAngleBetweenTwoDir(d1, d2) >= -1;
			}
			if (test && n[to].x - n[start].x > ((maxX - minX) * 3) / 7)
				continue;
		}
		// a 3's lower bowl after it (a 3 written as two circles)
		if (i + 3 < count && next->fNext != -1 && chunks[i + 2].fNext != -1)
		{
			tag_CHUNK* c2 = &chunks[i + 2];
			tag_CHUNK* c3 = &chunks[i + 3];
			if (c2->fKind == 2 && c2->f78 == 402 && c3->fKind == 1 && c3->f78 == 402
				&& HWRAbs(c->fY1 - c2->fY1) < tall / 3
				&& c3->fBottom - c3->fTop > (down * 2) / 3
				&& n[c3->fTo].x < n[c3->fTo - 1].x)
				continue;
		}
		if (i + 3 < count && next->fNext != -1 && chunks[i + 2].fNext != -1)
		{
			tag_CHUNK* c2 = &chunks[i + 2];
			tag_CHUNK* c3 = &chunks[i + 3];
			if (c2->fKind == 2 && c2->f74 == 300 && c3->fKind == 1 && c3->f78 == 401)
			{
				int32_t h2 = c2->fBottom - c2->fTop;
				int32_t lim = (down * 2) / 3;
				if (h2 > lim && h2 < (down * 4) / 3 && c3->fBottom - c3->fTop > lim)
					continue;
			}
		}
		if (i < count - 2 && next->f74 == 300 && HWRAbs(down - up) < tall / 3
			&& c->fBulge < c->fLength2 / 16 && next->fNext != -1)
		{
			tag_CHUNK* c2 = &chunks[i + 2];
			UByte zone = c2->fZoneEnd;
			if (c2->f78 == 401 && zone != 0 && zone != 10 && zone != 20)
			{
				ULong back = (ULong) GetDirection(c->fX1, c->fY1, c->fX0, c->fY0);
				if (HWRAbs(GetAngleBetweenTwoDir(back, next->fDir)) <= 1)
					continue;
			}
		}
		long obj = LO_Add(lo, n, 200, start, end, 0, -1);
		if (obj >= 0)
			c->f7C = (int32_t) obj;
	}
	return 1;
}


#pragma mark - after the searchers

// ROM 0x002a7eac (unnamed) - which side of the line through (x1, y1) and
// (x2, y2) the point (x, y) is on: 1, -1, or 0 on it
static long
SideOfLine(long x1, long y1, long x2, long y2, long x, long y)
{
	int32_t d = (int32_t) ((x - x1) * (y2 - y1)) - (int32_t) ((x2 - x1) * (y - y1));
	if (d > 0)
		return 1;
	return d >= 0 ? 0 : -1;
}


// ROM 0x002a7ee8 CheckQIntersecXY__FiN71
// Whether the segments (x1, y1)-(x2, y2) and (x3, y3)-(x4, y4) meet (their
// ends not strictly on the same side of each other's line).
long
CheckQIntersecXY(long x1, long y1, long x2, long y2, long x3, long y3, long x4, long y4)
{
	long s1 = SideOfLine(x1, y1, x2, y2, x3, y3);
	long s2 = SideOfLine(x1, y1, x2, y2, x4, y4);
	if (s1 * s2 > 0)
		return 0;
	long t1 = SideOfLine(x3, y3, x4, y4, x1, y1);
	long t2 = SideOfLine(x3, y3, x4, y4, x2, y2);
	return t1 * t2 <= 0 ? 1 : 0;
}


// ROM 0x002a8020 CheckQIntersec__FP13tag_wapx_typeiN32
// Whether the segments between nodes a-b and c-d meet.
long
CheckQIntersec(tag_wapx_type* n, long a, long b, long c, long d)
{
	return CheckQIntersecXY(n[a].x, n[a].y, n[b].x, n[b].y, n[c].x, n[c].y, n[d].x, n[d].y);
}


// ROM 0x0028fa14 ThreeToFive__FPvP9tag_CHUNKP13tag_wapx_typePiPP10tag_LOWOBJi
// The 3s that are 5s: a 3 (value 1300 + 3 mod 100) whose writing turns
// back at its right and then at its left again, the left turn sharp (more
// than four steps) and the right one hardly a turn at all, or one of two
// steps whose line back points at the start, and the left turn left of
// the start - a 5 whose bar was not lifted - becomes 1305.  ROM BUG: the
// two turns found are not forgotten between one digit and the next, so a
// 3 that has neither is judged by the last one's.  ==> 0 with no digits,
// else 1.
long
ThreeToFive(void* lo, tag_CHUNK* chunks, tag_wapx_type* n, int32_t* real, tag_LOWOBJ** objs, long count)
{
	long right = -1, left = -1;
	if (count == 0)
		return 0;
	for (long k = 0; k < count; k++)
	{
		tag_LOWOBJ* obj = objs[k];
		if (obj->fValue == 0xffff)
			continue;
		if ((uint32_t) (obj->fValue - 1300) % 100 != 3)
			continue;
		long m = LO_HowManyChunks(lo, obj);
		long a0 = chunks[real[LO_GetRealChunkInd(lo, chunks, n, obj, 1)]].fFrom;
		long last = chunks[real[LO_GetRealChunkInd(lo, chunks, n, obj, m)]].fTo - 1;
		for (long i = a0 + 1; i <= last; i++)
			if (n[i - 1].x < n[i].x && n[i + 1].x < n[i].x)
			{
				right = i;
				break;
			}
		if (right == -1)
			continue;
		for (long i = right + 1; i <= last; i++)
			if (n[i - 1].x > n[i].x && n[i + 1].x > n[i].x)
			{
				left = i;
				break;
			}
		if (left == -1)
			continue;
		long d1 = GetDirection(n[left].x, n[left].y, n[left + 1].x, n[left + 1].y);
		long d2 = GetDirection(n[left].x, n[left].y, n[left - 1].x, n[left - 1].y);
		long turnLeft = GetAngleBetweenTwoDir(d1, d2);
		if (turnLeft < 3)
			continue;
		long e1 = GetDirection(n[right].x, n[right].y, n[right - 1].x, n[right - 1].y);
		long e2 = GetDirection(n[right].x, n[right].y, n[right + 1].x, n[right + 1].y);
		long turnRight = GetAngleBetweenTwoDir(e1, e2);
		if (turnLeft > 4 && turnRight < 2)
		{
			if (turnRight < 1 || n[left].x < n[a0].x)
				obj->fValue = 0x519;
		}
		if (turnLeft > 4 && turnRight == 2 && a0 + 1 != right && n[left].x < n[a0].x)
		{
			long f = GetDirection(n[right].x, n[right].y, n[a0].x, n[a0].y);
			if (GetAngleBetweenTwoDir(f, e2) < 1)
				obj->fValue = 0x519;
		}
	}
	return 1;
}


// ROM 0x0028fd18 RecognizeZCCW__FPvP9tag_CHUNKP13tag_wapx_typePiPP10tag_LOWOBJi
// The digits of two chunks - one going down that turns the other way
// (value 502), then an arc up (402) - looked at again: wide on the right
// and starting away from its leftmost point, with the first chunk twice
// the second's height, it is a 6 (1306); an 8 whose closing line does not
// cross its start, or crosses it far from where it began, is a 0 (1300);
// a 2 narrower than the arc is tall a 6.  ==> 1.
long
RecognizeZCCW(void* lo, tag_CHUNK* chunks, tag_wapx_type* n, int32_t* real, tag_LOWOBJ** objs, long count)
{
	for (long k = 0; k < count; k++)
	{
		tag_LOWOBJ* obj = objs[k];
		long m = LO_HowManyChunks(lo, obj);
		long i0 = real[LO_GetRealChunkInd(lo, chunks, n, obj, 1)];
		long i1 = real[LO_GetRealChunkInd(lo, chunks, n, obj, m)];
		if (i0 + 1 != i1)
			continue;
		tag_CHUNK* c0 = &chunks[i0];
		tag_CHUNK* c1 = &chunks[i1];
		if (!(c0->fKind == 2 && c0->f78 == 502 && c1->f78 == 402 && c0->fPrev == -1 && c1->fNext == -1))
			continue;
		int32_t w = c1->fRight - c0->fLeft;
		if ((c1->fRight - c1->fX0) * 3 > w && (c1->fRight - c1->fX1) * 3 > w
			&& c0->fLeftNode != c0->fFrom && c0->fHeight > c1->fHeight * 2)
		{
			obj->fValue = 0x51a;
			continue;
		}
		if ((uint32_t) (obj->fValue - 1300) % 100 == 8 && c1->fHeight * 3 > c0->fHeight * 2)
		{
			long a = c0->fTo - c0->f91;
			long b = c0->fFrom + c0->f90;
			if (!CheckQIntersec(n, c1->fRightNode, c1->fTo, a, b))
			{
				obj->fValue = 0x514;
				continue;
			}
			int32_t x1 = c1->fX1, y1 = c1->fY1;
			int32_t xa = n[a].x, ya = n[a].y;
			int32_t da = (x1 - xa) * (x1 - xa) + (y1 - ya) * (y1 - ya);
			int32_t d0 = (x1 - c0->fX0) * (x1 - c0->fX0) + (y1 - c0->fY0) * (y1 - c0->fY0);
			if (d0 * 9 > da * 4 && xa - x1 > 0 && c1->fRight - n[b].x > (xa - x1) * 3)
			{
				obj->fValue = 0x514;
				continue;
			}
		}
		if ((uint32_t) (obj->fValue - 1300) % 100 == 2 && w * 7 < c1->fHeight * 10)
			obj->fValue = 0x51a;
	}
	return 1;
}
