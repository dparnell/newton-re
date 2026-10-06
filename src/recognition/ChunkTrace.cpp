/*
	File:		ChunkTrace.cpp

	Contains:	The digit reader's first steps over the trace: the turns
				marked (ExtrWordTrace_V) and a polyline fitted through the
				marked points (GetLineApprox, SetAllDirections) - see
				Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x00285dc8-0x00286834, 0x0028877c-0x00288d4c), all of it
				from the disassembly; each function cites its origin.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRMemoryAlloc, HWRMemoryFree, HWRAbs
#include "LowLevel.h"		// HWRLAbs
#include "host/RomBugs.h"
#include <string.h>


#pragma mark - the turns

// A stroke's extent as ExtrWordTrace_V measures it (ROM 0x34 bytes, no
// pointers): the points at its extremes, what they come to, and whether
// the sort by height has taken it yet.
struct ExtrStroke
{
	int32_t		fStart;			// +00
	int32_t		fStart2;		// +04  (the same)
	int32_t		fTop;			// +08  the point with the least y
	int32_t		fBottom;		// +0c  the greatest y
	int32_t		fRight;			// +10  the greatest x
	int32_t		fLeft;			// +14  the least x
	int32_t		fWidth;			// +18
	int32_t		fHeight;		// +1c
	int32_t		fLeftX;			// +20
	int32_t		fTopY;			// +24
	int32_t		fRightX;		// +28
	int32_t		fBottomY;		// +2c
	int32_t		fTaken;			// +30
};
static_assert(sizeof(ExtrStroke) == 0x34, "the ROM's stroke extent is 0x34 bytes");


// ROM 0x0028877c ExtrWordTrace_V__FP14tag_WORD_TRACEiT2Pi
// First each stroke's extent (at most 100 strokes), and *height the
// height of the stroke just past the middle of them sorted by height;
// then, stroke by stroke, the turns: the pen has to move divisor-th of
// that height (the threshold) from where a run began before its start
// is marked as a turn at the bottom (it went up) or the top (it went
// down), and once it has, a turn is looked for where the next point
// goes back the other way, which becomes the next run's start.  At the
// pen-up the stroke's last point is marked as the opposite of the last
// turn, and a run still waiting then is settled against it.
//
// The states are those of the ROM's r0: 0 looking for a turn, 1 a
// stroke's first run, 2 a run from a turn at the bottom, 3 one from a
// turn at the top; r2 (last) is which kind of turn was last marked and
// is not cleared between strokes.
// ROM QUIRK: "the height just past the middle" is the height at the
// sorted index one after the middle, not the first height greater than
// the middle one: the ROM's search stops at the first height not less.
// DEVIATION: the ROM keeps the stroke the sort last took in a stack word
// it never sets, read as it is when no stroke has a height under 0x7fff;
// the host starts it at nought.  The ROM also reads the sorted list at
// index nought when there are no strokes at all (a block of no size);
// the host takes no height then.
long
ExtrWordTrace_V(tag_WORD_TRACE* trace, long count, long divisor, long* height)
{
	ExtrStroke* strokes = (ExtrStroke*) HWRMemoryAlloc(100 * sizeof(ExtrStroke));
	if (strokes == nil)
		return -1;
	memset(strokes, 0, 100 * sizeof(ExtrStroke));
	long left = 0, right = 0, bottom = 0, top = 0, start2 = 0, start = 0;
	long n = -1;
	for (long i = 0; i < count && n < 100; i++)
	{
		long y = trace[i].y;
		if (y == -1)
		{
			if (n >= 0)
			{
				ExtrStroke* s = &strokes[n];
				s->fStart = (int32_t) start;
				s->fStart2 = (int32_t) start2;
				s->fTop = (int32_t) top;
				s->fBottom = (int32_t) bottom;
				s->fRight = (int32_t) right;
				s->fLeft = (int32_t) left;
				s->fRightX = trace[right].x;
				s->fTopY = trace[top].y;
				s->fLeftX = trace[left].x;
				s->fBottomY = trace[bottom].y;
				s->fWidth = trace[right].x - trace[left].x;
				s->fHeight = trace[bottom].y - trace[top].y;
			}
			n++;
			left = right = bottom = top = start2 = start = i + 1;
		}
		else
		{
			long x = trace[i].x;
			if (x < trace[left].x)
				left = i;
			else if (x > trace[right].x)
				right = i;
			if (y < trace[top].y)
				top = i;
			else if (y > trace[bottom].y)
				bottom = i;
		}
	}
	if (n >= 100)
	{
		HWRMemoryFree((Ptr) strokes);
		return -1;
	}
	long* sorted = (long*) HWRMemoryAlloc(n * sizeof(long));
	if (sorted == nil)
	{
		HWRMemoryFree((Ptr) strokes);
		return -1;
	}
	memset(sorted, 0, n * sizeof(long));
	long taken = 0;
	for (long k = 0; k < n; k++)
	{
		long least = 0x7fff;
		for (long j = 0; j < n; j++)
			if (strokes[j].fTaken == 0 && strokes[j].fHeight < least)
			{
				least = strokes[j].fHeight;
				taken = j;
			}
		sorted[k] = taken;
		strokes[taken].fTaken = 1;
	}
	long middle = n / 2;
	if (middle != 0 && n > middle * 2)
		middle++;
	middle--;
	if (middle == -1)
		middle = 0;
	long h = n > 0 ? strokes[sorted[middle]].fHeight : 0;
	for (long j = middle + 1; j < n; j++)
		if (strokes[sorted[j]].fHeight >= h)
		{
			h = strokes[sorted[j]].fHeight;
			break;
		}
	*height = h;
	if (divisor > 0)
		divisor = h / divisor;

	long stroke = -1, lastStroke = -1;
	long state = 0, last = 0;
	long from = 0, below = 0, above = 0;		// the run's start and the band round it (the ROM's r7, r9, r8)
	for (long i = 0; i < count; i++)
	{
		long y = trace[i].y;
		if (y == -1)
		{
			// the pen-up: the stroke's last point, and a run still waiting
			if (++stroke != 0)
			{
				tag_WORD_TRACE* prev = &trace[i - 1];
				switch (state)
				{
				case 0:
					if (last == 1)
						prev->fFlags |= kTraceHigh;
					else if (last == 2)
						prev->fFlags |= kTraceLow;
					break;
				case 1:
					if (trace[from].y >= trace[i - 1].y)
					{
						trace[from].fFlags |= kTraceLow;
						prev->fFlags |= kTraceHigh;
					}
					else
					{
						trace[from].fFlags |= kTraceHigh;
						prev->fFlags |= kTraceLow;
					}
					break;
				case 2:
					prev->fFlags |= kTraceHigh;
					break;
				case 3:
					prev->fFlags |= kTraceLow;
					break;
				}
			}
			continue;
		}
		if (stroke != lastStroke)
		{
			// a stroke's first point starts its first run
			lastStroke = stroke;
			from = i;
			below = y - divisor;
			above = y + divisor;
			state = 1;
			continue;
		}
		for (;;)
		{
			switch (state)
			{
			case 0:
				{
					long next = trace[i + 1].y;
					if (next != -1)
					{
						long turn = next < y ? 3 : 4;
						if (last == 1)
						{
							if (turn == 4)
							{
								from = i;
								below = y - divisor;
								above = y + divisor;
								state = 2;
							}
						}
						else if (last == 2 && turn == 3)
						{
							from = i;
							below = y - divisor;
							above = y + divisor;
							state = 3;
						}
					}
				}
				break;
			case 1:
				if (y <= below)
				{
					state = 0;
					trace[from].fFlags |= kTraceLow;
					last = 1;
				}
				else if (y >= above)
				{
					state = 0;
					trace[from].fFlags |= kTraceHigh;
					last = 2;
				}
				break;
			case 2:
				if (y > above)
				{
					state = 0;
					trace[from].fFlags |= kTraceHigh;
					last = 2;
					continue;				// and the point looked at again for a turn
				}
				if (y < below)
					state = 0;
				break;
			case 3:
				if (y <= above)
				{
					if (y < below)
					{
						state = 0;
						trace[from].fFlags |= kTraceLow;
						last = 1;
						continue;
					}
				}
				else
					state = 0;
				break;
			}
			break;
		}
	}
	HWRMemoryFree((Ptr) sorted);
	HWRMemoryFree((Ptr) strokes);
	return 0;
}


#pragma mark - the polyline

// A node of a segment being split (ROM 8 bytes): the first eight bytes of
// a tag_wapx_type.
struct ApxNode
{
	int32_t		fIndex;
	UByte		fFlags;
	UByte		fFirst;
	UByte		f06[2];
};
static_assert(sizeof(ApxNode) == 8, "the ROM's node is 8 bytes");

const long	kApxMaxMarks	= 200;			// the marked points
const long	kApxMaxSplit	= 50;			// the nodes of one segment
const long	kApxMaxNodes	= 200;			// the whole polyline (the block holds one more)


// ROM 0x00285dc8 GetLineApprox__FP14tag_WORD_TRACEiT2PP13tag_wapx_type
// Each pen-up marks the point before it as a stroke's end and the point
// after it as a stroke's start; the marked points (those and the turns)
// are listed in order, and each segment between two of them inside a
// stroke is split: the point furthest from the chord is taken as a node
// while the square of its distance is at least a hundredth of the
// chord's squared length times tolerance squared and no more than a
// quarter of the whole segment's squared length... (the comparisons are
// in the code), up to two hundred passes and fifty nodes a segment.
// The segments' nodes are run together, a segment's first node merged
// into the previous one's last when they are the same point; then the
// directions and the points.
// ROM QUIRK: the scan checking the marked points are in order finds
// where they are not, and nothing uses what it found.
// ROM BUG (fixed): a split in front of a segment's first node, when it is
// not the segment's first split, clears the second node's fFirst rather
// than the new first node's (left as the memset had it).  And at fifty
// nodes a split in front of the first node has already moved the nodes
// along when it finds there is no room, leaving the first node twice.
// The fix clears the new first node's fFirst, and looks for room before
// moving anything (with none the split is not made, as the ROM means).
long
GetLineApprox(tag_WORD_TRACE* trace, long count, long tolerance, tag_wapx_type** result)
{
	int32_t tolerance2 = (int32_t) (tolerance * tolerance);
	long made = 0;
	Boolean inStroke = false;
	const ULong outSize = (kApxMaxNodes + 1) * sizeof(tag_wapx_type);
	tag_wapx_type* out = (tag_wapx_type*) HWRMemoryAlloc(outSize);
	if (out == nil)
		return -1;
	memset(out, 0, outSize);
	const ULong workSize = kApxMaxSplit * sizeof(ApxNode) + kApxMaxMarks * sizeof(ApxNode);
	ApxNode* split = (ApxNode*) HWRMemoryAlloc(workSize);
	if (split == nil)
	{
		HWRMemoryFree((Ptr) out);
		return -1;
	}
	memset(split, 0, workSize);
	ApxNode* marks = split + kApxMaxSplit;

	for (long i = 0; i < count; i++)
		if (trace[i].y == -1)
		{
			if (i > 0)
				trace[i - 1].fFlags |= kTraceStrokeEnd;
			if (i < count - 1)
				trace[i + 1].fFlags |= kTraceStrokeStart;
		}

	long nMarks = 0;
	for (long i = 0; i < count && nMarks < kApxMaxMarks; i++)
	{
		Boolean marked = false;
		int32_t f = trace[i].fFlags;
		if (f & kTraceStrokeStart)
		{
			marked = true;
			marks[nMarks].fIndex = (int32_t) i;
			marks[nMarks].fFlags |= kApxStart;
		}
		if (f & kTraceStrokeEnd)
		{
			marked = true;
			marks[nMarks].fIndex = (int32_t) i;
			marks[nMarks].fFlags |= kApxEnd;
		}
		if (f & kTraceLow)
		{
			marked = true;
			marks[nMarks].fIndex = (int32_t) i;
			marks[nMarks].fFlags |= kApxLow;
		}
		if (f & kTraceHigh)
		{
			marks[nMarks].fIndex = (int32_t) i;
			marks[nMarks].fFlags |= kApxHigh;
			marked = true;
		}
		if (marked)
			nMarks++;
	}
	if (nMarks >= kApxMaxMarks)
	{
		HWRMemoryFree((Ptr) split);
		HWRMemoryFree((Ptr) out);
		return -1;
	}
	long lastMark = nMarks - 1;
	for (long k = 0; k < lastMark; k++)				// (found and not used)
		if (marks[k].fIndex > marks[k + 1].fIndex)
			break;

	for (long k = 0; k < lastMark; k++)
	{
		memset(split, 0, kApxMaxSplit * sizeof(ApxNode));
		ApxNode* mark = &marks[k];
		if (mark->fFlags & kApxStart)
		{
			if (inStroke)
				break;
			inStroke = true;
		}
		if (mark->fFlags & kApxEnd)
		{
			if (!inStroke)
				break;
			inStroke = false;
			continue;
		}
		if (!inStroke)
			continue;
		long from = mark->fIndex;
		long end = mark[1].fIndex;
		if (from == end)
			continue;
		Boolean first = true;
		int32_t dx = trace[end].x - trace[from].x;
		int32_t dy = trace[end].y - trace[from].y;
		int32_t whole = dx * dx + dy * dy;
		split[0] = mark[0];
		split[0].fFlags |= kApxSegStart;
		split[1] = mark[1];
		split[1].fFlags |= kApxSegEnd;
		long nSplit = 2;
		long at = 1;						// the node the segment being looked at ends at
		long to = end;
		for (long pass = 0; pass < 200; pass++)
		{
			long far = (to - from > 1) ? v_MostFarFromChord(trace, from, to) : to;
			int32_t cx = trace[to].x - trace[from].x;
			int32_t cy = trace[to].y - trace[from].y;
			int32_t chord = cx * cx + cy * cy;
			int32_t off = (far == to) ? 0 : (int32_t) v_QDistFromChord(trace[from].x, trace[from].y, trace[to].x, trace[to].y, trace[far].x, trace[far].y);
			if (chord != 0)
			{
				int32_t close = (int32_t) HWRLAbs((int32_t) ((uint32_t) chord * (uint32_t) tolerance2)) / 100;
				if (close <= off * 100 && whole <= off * 400)
				{
					// split at the furthest point
					long j;
					// (the fix: no room, no split - nothing moved)
					long top = RomBugFixed() && nSplit == kApxMaxSplit ? -1 : nSplit - 1;
					for (j = top; j >= 0; j--)
					{
						if (split[j].fIndex <= far)
						{
							if (nSplit != kApxMaxSplit)
							{
								to = far;
								split[j + 1].fIndex = (int32_t) far;
								split[j + 1].fFlags = 0;
								at = j + 1;
								nSplit++;
								if (first)
								{
									first = false;
									split[j + 1].fFirst = 1;
								}
								else
									split[j + 1].fFirst = 0;
							}
							break;
						}
						split[j + 1] = split[j];
						if (j == 0)
						{
							if (nSplit != kApxMaxSplit)
							{
								to = far;
								split[0].fIndex = (int32_t) far;
								split[0].fFlags = 0;
								nSplit++;
								at = 0;
								if (first)
								{
									first = false;
									split[0].fFirst = 1;
								}
								else if (RomBugFixed())
									split[0].fFirst = 0;
								else
									split[1].fFirst = 0;
							}
							break;
						}
					}
					continue;
				}
			}
			// close enough: on to the next piece
			if (to == end)
				break;
			from = split[at].fIndex;
			at++;
			to = split[at].fIndex;
		}
		long total = made + nSplit;
		if (total > kApxMaxNodes)
			break;
		if (made <= 0)
		{
			out[0].fIndex = split[0].fIndex;
			out[0].fFlags = split[0].fFlags;
			out[0].fFirst = split[0].fFirst;
			out[0].fDir = 0;
			made = nSplit;
		}
		else if (out[made - 1].fIndex == split[0].fIndex)
		{
			out[made - 1].fFlags |= split[0].fFlags;
			made = total - 1;
		}
		else
		{
			out[made].fIndex = split[0].fIndex;
			out[made].fFlags = split[0].fFlags;
			out[made].fFirst = split[0].fFirst;
			out[made].fDir = 0;
			made = total;
		}
		for (long j = 1; j < nSplit; j++)
		{
			tag_wapx_type* o = &out[made - nSplit + j];
			o->fIndex = split[j].fIndex;
			o->fFlags = split[j].fFlags;
			o->fFirst = split[j].fFirst;
			o->fDir = 0;
		}
	}
	SetAllDirections(trace, out, made);
	for (long k = 0; k < made; k++)
	{
		out[k].x = trace[out[k].fIndex].x;
		out[k].y = trace[out[k].fIndex].y;
	}
	*result = out;
	HWRMemoryFree((Ptr) split);
	return made;
}


// ROM 0x00286638 SetAllDirections__FP14tag_WORD_TRACEP13tag_wapx_typei
// Each node's directions: a segment's first node the direction on to the
// next both ways, its last the direction in from the one before both
// ways; any other node the direction from the node before to the node
// after coming in (the line through it) and on to the next going out -
// unless the line turns there by 120 degrees or more, when it is a
// corner and fDir keeps the two directions either side, packed.
// ==> count.
long
SetAllDirections(tag_WORD_TRACE* trace, tag_wapx_type* nodes, long count)
{
	for (long k = 0; k < count; k++)
	{
		tag_wapx_type* n = &nodes[k];
		UByte f = n->fFlags;
		long here = n->fIndex;
		if ((f & kApxSegStart) && !(f & kApxSegEnd))
		{
			long next = n[1].fIndex;
			n->fDir = (int32_t) GetDirection(trace[here].x, trace[here].y, trace[next].x, trace[next].y);
		}
		else if ((f & kApxSegEnd) && !(f & kApxSegStart))
		{
			long prev = n[-1].fIndex;
			n->fDir = (int32_t) GetDirection(trace[prev].x, trace[prev].y, trace[here].x, trace[here].y);
			n->fDirOut = (int32_t) GetDirection(trace[prev].x, trace[prev].y, trace[here].x, trace[here].y);
			continue;
		}
		else
		{
			long next = n[1].fIndex;
			long prev = n[-1].fIndex;
			long out = GetDirection(trace[here].x, trace[here].y, trace[next].x, trace[next].y);
			long in = GetDirection(trace[prev].x, trace[prev].y, trace[here].x, trace[here].y);
			long turn = HWRAbs(out - in);
			if (turn > 11)
				turn = 24 - turn;
			if (turn > 7)
			{
				n->fFlags |= kApxCorner;
				n->fDir = (int32_t) ((in & 0xff) | (out << 8));
			}
			else
				n->fDir = (int32_t) GetDirection(trace[prev].x, trace[prev].y, trace[next].x, trace[next].y);
		}
		long next = n[1].fIndex;
		n->fDirOut = (int32_t) GetDirection(trace[here].x, trace[here].y, trace[next].x, trace[next].y);
	}
	return count;
}
