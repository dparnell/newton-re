/*
	File:		ChunkPound.cpp

	Contains:	The digit reader's pound sign (FindPound): a sign coded 13
				- a bar across - that is the last thing written, or starts
				a second stroke, taken with the stroke before it for a £
				when that stroke falls steeply, turns left at its foot and
				the bar crosses it about its middle.  See Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x002a3ef8-0x002a4608); each function cites its origin.
				All of it is from the disassembly.
*/

#include "Chunk.h"
#include "host/RomBugs.h"


// ROM 0x002a40a0 (unnamed) - whether the stroke before the bar obj (whose
// first real chunk is first) is a £'s body: ==> 1, *start its first node
static long
PoundBody(tag_CHUNK_STAFF* staff, tag_LOWOBJ* obj, long first, long* start)
{
	tag_STK* strokes = staff->fStrokes;
	int32_t* real = staff->fRealChunks;
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	long realCount = staff->fRealCount;
	long fall = 0;
	long maxY = 0, minY = 30000;
	long at = -1;
	if (first == 0)
		return 0;
	tag_CHUNK* c = &chunks[real[first - 1]];
	long cTo = c->fTo;
	long s = c->fStroke;
	tag_STK* st = &strokes[s];
	long firstChunk = st->fFirstChunk;
	long g = firstChunk;
	tag_CHUNK* f = &chunks[firstChunk];
	if (f->fY0 > f->fY1)
	{
		if (f->fNext == -1)
			return 0;
		g = firstChunk + 1;
	}
	long stLeft = st->fLeft;
	tag_CHUNK* body = &chunks[g];
	long h = body->fHeight;
	long stWidth = st->fWidth;
	long stChunks = st->fLastChunk - firstChunk + 1;
	long stRight = st->fRight;
	if (firstChunk != 0 && realCount - 1 != first)
		return 0;
	*start = chunks[firstChunk].fFrom;

	// the steepest fall between the body's first node and its lowest
	long k = body->fBottomNode;
	if (body->fFrom >= k)
		return 0;
	do
	{
		long d = n[k].y - n[k - 1].y;
		if (d > fall)
		{
			fall = d;
			at = k - 1;
		}
		k--;
	} while (body->fFrom < k);
	if (at == -1)
		return 0;
	tag_wapx_type* steep = &n[at];
	long turn = 12 - GetDirection(steep->x, steep->y, n[at + 1].x, n[at + 1].y);
	if (at != *start)
	{
		long from = body->fTopNode;
		if (!(from < at && (n[from].fFlags & 3) == 0))
			from = at;
		long dir = find_direct_backward(n, from, 0, 0, h / 8);
		if (turn > 0 && direct_suits(dir, 0x14, 7))
			dir += turn;
		// ROM BUG (fixed): a direction past the last (23) is brought round
		// by 23, not by the 24 steps a turn has.  The fix takes 24 off.
		if (dir > 0x17)
			dir -= RomBugFixed() ? 0x18 : 0x17;
		if (!direct_suits(dir, 1, 9))
			return 0;
	}

	// the bar about the body's middle, across and up and down
	long objRight = obj->fRight;
	long objMidX = (obj->fRight + obj->fLeft) / 2;
	long objMidY = (obj->fBottom + obj->fTop) / 2;
	long stMidX = (strokes[s].fRight + stLeft) / 2;
	if (!(stRight - stWidth / 4 >= objMidX && objMidX >= stLeft))
	{
		if (!(objRight >= stMidX && obj->fLeft + stWidth / 4 <= stMidX))
		{
			if (objRight > stRight || obj->fLeft < stLeft)
			{
				if (objRight < stRight || obj->fLeft > stLeft)
					return 0;
			}
		}
	}
	if (body->fTop + h / 6 > objMidY)
		return 0;
	if (body->fBottom - h / 4 < objMidY)
		return 0;
	if (stChunks > 4)
		return 0;
	if (stWidth < h / 8)
		return 0;

	// the foot: from the end of the chunk before the bar back to where the
	// writing last went left
	long left = -1;
	k = cTo - 1;
	if (body->fFrom >= k)
		return 0;
	do
	{
		long y = n[k].y;
		if (y > maxY)
			maxY = y;
		if (y < minY)
			minY = y;
		if (n[k].x - n[k - 1].x < 0)
		{
			left = k;
			break;
		}
		k--;
	} while (body->fFrom < k);
	if (left == -1)
		return 0;
	tag_wapx_type* foot = &n[left];
	long footY = foot->y;
	if (objRight - (steep->x + stLeft) / 2 < (footY - steep->y) / 10)
		return 0;
	long span = maxY - minY;
	if (span < 0)
		return 0;
	tag_wapx_type* end = &n[cTo];
	long run = end->x - foot->x;
	if (span - run > h / 8)
		return 0;
	if (span * 2 - h > h / 8)
		return 0;
	if (!direct_suits(GetDirection(n[body->fTopNode].x, n[body->fTopNode].y, foot->x, footY), 8, 11))
		return 0;
	if (!direct_suits(GetDirection(foot->x, foot->y, end->x, end->y), 0xf, 0x13))
		return 0;
	long back = find_direct_backward(n, left, 0, 0, h / 8);
	if (direct_suits(back, 0xd, 0))
		return 0;
	if (direct_suits(back, 8, 0xc))
	{
		if (!(chunks[real[first - 1]].fY1 > minY && footY > minY))
			return 0;
	}
	return 1;
}


// ROM 0x002a3ef8 FindPound__FP15tag_CHUNK_STAFF
long
FindPound(tag_CHUNK_STAFF* staff)
{
	void* lo = staff->fLO;
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* n = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	long realCount = staff->fRealCount;
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1300);
	if (LO_PickFirst(lo, &obj))
	{
		do
		{
			if (obj->fValue == 0xffff || (ULong) obj->fValue % 100 != 13)
				continue;
			long many = LO_HowManyChunks(lo, obj);
			long first = LO_GetRealChunkInd(lo, chunks, n, obj, 1);
			long last = LO_GetRealChunkInd(lo, chunks, n, obj, many);
			if (realCount - 1 != first && chunks[real[first]].fStroke != 1)
				continue;
			long start = 0;
			if (PoundBody(staff, obj, first, &start))
				LO_Add(lo, n, 1300, start, chunks[real[last]].fTo, 1570, -1);
		} while (LO_PickNext(lo, &obj));
	}
	LO_SetWorkClass(lo, saved);
	return 0;
}
