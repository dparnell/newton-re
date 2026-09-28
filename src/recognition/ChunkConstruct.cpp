/*
	File:		ChunkConstruct.cpp

	Contains:	The digit reader's chunks: the polyline cut into chunks
				between its marked points (ChunkFillMainData), the strokes
				they make (ChunkMakeStrokes), each chunk's brackets - the
				lines and arcs it is drawn with (ApxToBrackets) - and the
				class of shape that makes it (ApxToCLine), all put
				together by ChunkConstruct.  See Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x00285a64-0x00285dc8, 0x00286834-0x00286a54 in part,
				0x00286a54-0x002884c8 in part, 0x002a7a84-0x002a8090 in
				part); each function cites its origin.  The unnamed
				functions and the one the decompiler garbles (the one
				ChunkConstruct calls after ApxToCLine) are from the
				disassembly.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRMemoryAlloc, HWRMemoryFree, HWRAbs
#include <string.h>


#pragma mark - measures

// ROM 0x002a7cb0 SgnArc__FP13tag_wapx_typeiN22
// Which way the path a, b, c turns: the sign of the cross product of
// (b - a) and (c - a) with y growing down - 1, -1 or 0 for none.
long
SgnArc(tag_wapx_type* n, long a, long b, long c)
{
	int32_t d = (n[c].y - n[a].y) * (n[b].x - n[a].x) - (n[b].y - n[a].y) * (n[c].x - n[a].x);
	if (d >= 1)
		return 1;
	return d < 0 ? -1 : 0;
}


// ROM 0x002a7d18 H2Arc__FP13tag_wapx_typeiT2
// The square of the greatest distance of a node between a and b from the
// chord a-b.
long
H2Arc(tag_wapx_type* n, long a, long b)
{
	long h = 0;
	for (long k = a + 1; k < b; k++)
	{
		long d = v_QDistFromChord(n[a].x, n[a].y, n[b].x, n[b].y, n[k].x, n[k].y);
		if (d < 0)
			d = -d;
		if (h < d)
			h = d;
	}
	return h;
}


// ROM 0x002a7d9c L2Arc__FP13tag_wapx_typeiT2
// The square of the chord's length.
long
L2Arc(tag_wapx_type* n, long a, long b)
{
	int32_t dy = n[b].y - n[a].y;
	int32_t dx = n[b].x - n[a].x;
	return dy * dy + dx * dx;
}


// ROM 0x002a7e24 GetBox__FP13tag_wapx_typeiT2P7tag_BOX
void
GetBox(tag_wapx_type* n, long a, long b, tag_BOX* box)
{
	box->left = box->right = n[a].x;
	box->top = box->bottom = n[a].y;
	for (long k = a + 1; k <= b; k++)
	{
		if (n[k].x < box->left)
			box->left = n[k].x;
		if (box->right < n[k].x)
			box->right = n[k].x;
		if (n[k].y < box->top)
			box->top = n[k].y;
		if (box->bottom < n[k].y)
			box->bottom = n[k].y;
	}
}


// ROM 0x002a83f8 (unnamed) - whether the segments a-b and c-d meet
// Each segment's ends on either side of the other's line (or on it); two
// segments on one line meet when their boxes overlap.
static long
SegmentsMeet(tag_wapx_type* n, long a, long b, long c, long d)
{
	long s1 = SgnArc(n, a, c, b);
	long s2 = SgnArc(n, a, d, b);
	if (s1 == 0 && s2 == 0)
	{
		int32_t hi = n[a].x < n[b].x ? n[b].x : n[a].x;
		int32_t hi2 = n[c].x < n[d].x ? n[d].x : n[c].x;
		if (hi2 <= hi)
			hi = hi2;
		int32_t lo = n[a].x < n[b].x ? n[a].x : n[b].x;
		int32_t lo2 = n[c].x < n[d].x ? n[c].x : n[d].x;
		if (lo2 < lo)
			lo2 = lo;
		if (hi < lo2)
			return 0;
		hi = n[a].y < n[b].y ? n[b].y : n[a].y;
		hi2 = n[c].y < n[d].y ? n[d].y : n[c].y;
		if (hi2 <= hi)
			hi = hi2;
		lo = n[a].y < n[b].y ? n[a].y : n[b].y;
		lo2 = n[c].y < n[d].y ? n[c].y : n[d].y;
		if (lo2 < lo)
			lo2 = lo;
		if (hi < lo2)
			return 0;
	}
	else if (s1 * s2 == 1)
		return 0;
	if (SgnArc(n, c, a, d) * SgnArc(n, c, b, d) == 1)
		return 0;
	return 1;
}


// ROM 0x002a7a84 CrossArcs__FP13tag_wapx_typeiN52
// Whether two pieces of the polyline cross: each a line (kind 1, taken
// as one segment from its first node to its last) or an arc (taken
// segment by segment); a segment is not tried against the one that
// starts where it ends.
long
CrossArcs(tag_wapx_type* n, long kind1, long a0, long a1, long kind2, long b0, long b1)
{
	for (long i = a0; i < a1; )
	{
		long i2 = kind1 == 1 ? a1 : i + 1;
		for (long j = b0; j < b1; )
		{
			long j2 = kind2 == 1 ? b1 : j + 1;
			if (i2 != j && SegmentsMeet(n, i, i2, j, j2) != 0)
				return 1;
			j = j2;
		}
		i = i2;
	}
	return 0;
}


// ROM 0x00286834 GetAngleBetweenTwoDir__FUiT1
// The turn from one of GetDirection's directions to another, in its
// fifteen-degree steps, -12..12 (anticlockwise positive).
long
GetAngleBetweenTwoDir(ULong a, ULong b)
{
	long d = a < b ? (long) (b - a) : (long) (a - b);
	if (d < 13)
	{
		if (a <= b)
			return d;
	}
	else
	{
		d = 24 - d;
		if (b <= a)
			return d;
	}
	return -d;
}


// ROM 0x002a7dd0 midL2Chunks__FP9tag_CHUNKi
// The mean of the chunks' squared chord lengths (the jumps left out).
// DEVIATION: with no chunk but jumps the ROM divides by nought, which
// throws evt.ex.div0; the host answers nought.
long
midL2Chunks(tag_CHUNK* chunks, long count)
{
	int32_t sum = 0;
	long n = 0;
	for (long k = 0; k < count; k++)
		if (chunks[k].fKind != 3)
		{
			sum += chunks[k].fLength2;
			n++;
		}
	if (n == 0)
		return 0;
	return sum / (int32_t) n;
}


#pragma mark - the chunks

// ROM 0x00287f60 ChunkFillMainData__FP9tag_CHUNKP13tag_wapx_typei
// The polyline cut into chunks at its segments (a segment runs between
// two marked points, kApxSegStart to kApxSegEnd) with a jump chunk (kind
// 3) where the pen was lifted between them: each chunk's ends, box and
// the nodes of its extremes, its chord and how far its segment's first
// split lies from it, whether it goes up or down, and its links to the
// chunks of its stroke either side.  Then each node is told its chunk
// (f18, from one, negative at a chunk's end the next one continues).
// ==> how many chunks, -1 with more than a hundred.
// ROM QUIRK: a first split at node nought is taken for none.
long
ChunkFillMainData(tag_CHUNK* chunks, tag_wapx_type* n, long count)
{
	long made = 0, real = 0;
	Boolean inChunk = false, done = true;
	long mid = -1;
	int32_t left = 0, top = 0, right = 0, bottom = 0;
	long leftNode = 0, rightNode = 0, topNode = 0, bottomNode = 0;
	long i = 0;
	if (count >= 1)
		for (;;)
		{
			if (n[i].fFlags & kApxSegStart)
			{
				tag_CHUNK* c = &chunks[made];
				inChunk = true;
				done = false;
				c->fPrev = (n[i].fFlags & kApxSegEnd) ? (int32_t) (made - 1) : -1;
				c->fFrom = (int32_t) i;
				c->fX0 = n[i].x;
				c->fY0 = n[i].y;
				left = right = c->fX0;
				top = bottom = c->fY0;
				leftNode = rightNode = topNode = bottomNode = i;
			}
			long prev;
			do
			{
				prev = i;
				i++;
				if (i >= count)
					goto finish;
				if (made > 99)
					return -1;
				if (inChunk)
				{
					if (n[i].x < left)
					{
						left = n[i].x;
						leftNode = i;
					}
					else if (n[i].x > right)
					{
						right = n[i].x;
						rightNode = i;
					}
					if (n[i].y < top)
					{
						top = n[i].y;
						topNode = i;
					}
					else if (n[i].y > bottom)
					{
						bottom = n[i].y;
						bottomNode = i;
					}
					if (n[i].fFirst == 1)
					{
						n[i].fFirst = 0;
						mid = i;
					}
					if (n[i].fFlags & kApxSegEnd)
					{
						tag_CHUNK* c = &chunks[made];
						inChunk = false;
						done = true;
						c->fNext = (n[i].fFlags & kApxSegStart) ? (int32_t) (made + 1) : -1;
						c->f70 = -1;
						c->f7C = -1;
						c->fTo = (int32_t) i;
						c->fRealIndex = (int32_t) real;
						c->fX1 = n[i].x;
						c->fY1 = n[i].y;
						c->fDir = (UByte) GetDirection(c->fX0, c->fY0, c->fX1, c->fY1);
						c->fLeft = left;
						c->fLeftNode = (int32_t) leftNode;
						c->fTop = top;
						c->fTopNode = (int32_t) topNode;
						c->fRight = right;
						c->fRightNode = (int32_t) rightNode;
						c->fBottom = bottom;
						c->fHeight = bottom - top;
						c->fWidth = right - left;
						c->fBottomNode = (int32_t) bottomNode;
						int32_t dx = c->fX1 - c->fX0, dy = c->fY1 - c->fY0;
						c->fLength2 = dx * dx + dy * dy;
						if (mid < 1)
						{
							c->fMidX = -1;
							c->fBulge = 0;
							c->fMidY = -1;
						}
						else
						{
							c->fMidX = n[mid].x;
							c->fMidY = n[mid].y;
							c->fBulge = (int32_t) v_QDistFromChord(c->fX0, c->fY0, c->fX1, c->fY1, c->fMidX, c->fMidY);
							mid = -1;
						}
						c->fKind = c->fY1 < c->fY0 ? 1 : 2;
						real++;
						made++;
						break;
					}
				}
			} while (!done);
			if (i != 0 && (n[i].fFlags & kApxStart))
			{
				// the pen's jump from the stroke before
				tag_CHUNK* c = &chunks[made];
				c->fKind = 3;
				c->fFrom = (int32_t) prev;
				c->fX0 = n[i - 1].x;
				c->fY0 = n[i - 1].y;
				c->fTo = (int32_t) i;
				c->fX1 = n[i].x;
				c->fY1 = n[i].y;
				c->fDir = (UByte) GetDirection(c->fX0, c->fY0, c->fX1, c->fY1);
				if (c->fX0 < c->fX1)
				{
					c->fLeft = c->fX0;
					c->fLeftNode = c->fFrom;
				}
				else
				{
					c->fLeft = c->fX1;
					c->fLeftNode = c->fTo;
				}
				if (c->fX1 < c->fX0)
				{
					c->fRight = c->fX0;
					c->fRightNode = c->fFrom;
				}
				else
				{
					c->fRight = c->fX1;
					c->fRightNode = c->fTo;
				}
				if (c->fY0 < c->fY1)
				{
					c->fTop = c->fY0;
					c->fTopNode = c->fFrom;
				}
				else
				{
					c->fTop = c->fY1;
					c->fTopNode = c->fTo;
				}
				if (c->fY1 < c->fY0)
				{
					c->fBottom = c->fY0;
					c->fBottomNode = c->fFrom;
				}
				else
				{
					c->fBottom = c->fY1;
					c->fBottomNode = c->fTo;
				}
				c->fWidth = c->fRight - c->fLeft;
				c->fHeight = c->fBottom - c->fTop;
				c->fMidX = -1;
				c->fMidY = -1;
				int32_t dx = c->fX1 - c->fX0, dy = c->fY1 - c->fY0;
				c->fLength2 = dy * dy + dx * dx;
				c->fBulge = 0;
				c->fPrev = -1;
				c->fNext = -1;
				c->f70 = -1;
				c->f7C = -1;
				c->fRealIndex = -1;
				made++;
				mid = -1;
			}
		}
finish:
	for (long k = 0; k < made; k++)
	{
		tag_CHUNK* c = &chunks[k];
		if (c->fKind == 3)
			continue;
		for (long j = c->fFrom; j <= c->fTo; j++)
			if (n[j].f18 == 0)
				n[j].f18 = (int32_t) (k + 1);
		if (c->fNext != -1)
			n[c->fTo].f18 = (int32_t) -(k + 1);
	}
	return made;
}


// ROM 0x002884c8 ChunkMakeStrokes__FP9tag_CHUNKP13tag_wapx_typeiPP7tag_STKPi
// The strokes the chunks make (a run of linked chunks, the jumps left
// out): their first and last chunks, the nodes of their extremes and
// their boxes; each chunk told its stroke.  ==> how many (also in
// *count), -1 for want of memory.
long
ChunkMakeStrokes(tag_CHUNK* chunks, tag_wapx_type* n, long count, tag_STK** strokes, long* strokeCount)
{
	long made = 0;
	for (long k = 0; k < count; k++)
		if (chunks[k].fKind != 3 && chunks[k].fPrev == -1)
			made++;
	*strokes = (tag_STK*) HWRMemoryAlloc(made * sizeof(tag_STK));
	if (*strokes == nil)
		return -1;
	memset(*strokes, 0, made * sizeof(tag_STK));
	long stroke = 0, first = 0;
	long leftNode = 0, rightNode = 0, topNode = 0, bottomNode = 0;
	for (long k = 0; k < count; k++)
	{
		tag_CHUNK* c = &chunks[k];
		if (c->fKind == 3)
			continue;
		c->fStroke = (int32_t) stroke;
		if (c->fPrev == -1)
		{
			leftNode = c->fLeftNode;
			rightNode = c->fRightNode;
			topNode = c->fTopNode;
			bottomNode = c->fBottomNode;
			first = k;
		}
		else
		{
			if (n[c->fLeftNode].x < n[leftNode].x)
				leftNode = c->fLeftNode;
			if (n[rightNode].x < n[c->fRightNode].x)
				rightNode = c->fRightNode;
			if (n[c->fTopNode].y < n[topNode].y)
				topNode = c->fTopNode;
			if (n[bottomNode].y < n[c->fBottomNode].y)
				bottomNode = c->fBottomNode;
		}
		if (c->fNext == -1)
		{
			tag_STK* s = &(*strokes)[stroke];
			s->fFirstChunk = (int32_t) first;
			s->fLastChunk = (int32_t) k;
			s->fLeftNode = (int32_t) leftNode;
			s->fLeft = n[leftNode].x;
			s->fRightNode = (int32_t) rightNode;
			s->fRight = n[rightNode].x;
			s->fTopNode = (int32_t) topNode;
			s->fTop = n[topNode].y;
			s->fBottomNode = (int32_t) bottomNode;
			s->fBottom = n[bottomNode].y;
			s->fWidth = s->fRight - s->fLeft;
			s->fHeight = s->fBottom - s->fTop;
			stroke++;
		}
	}
	*strokeCount = made;
	return made;
}


// ROM 0x00287e7c CreateRealChunkInd__FP9tag_CHUNKiPPi
// The indexes of the chunks that are not jumps, in a block with room for
// twenty more.  ==> how many, -1 for want of memory.
long
CreateRealChunkInd(tag_CHUNK* chunks, long count, int32_t** real)
{
	long n = 0;
	for (long k = 0; k < count; k++)
		if (chunks[k].fKind != 3)
			n++;
	ULong size = (n + 20) * sizeof(int32_t);
	*real = (int32_t*) HWRMemoryAlloc(size);
	if (*real == nil)
		return -1;
	memset(*real, 0, size);
	n = 0;
	for (long k = 0; k < count; k++)
		if (chunks[k].fKind != 3)
			(*real)[n++] = (int32_t) k;
	return n;
}


// ROM 0x00287f34 (unnamed) - a block given back and its pointer cleared
static void
FreeAndClear(void** block)
{
	if (*block == nil)
		return;
	HWRMemoryFree((Ptr) *block);
	*block = nil;
}


#pragma mark - the brackets

// ROM 0x00287a44 (unnamed) - a bracket written: its chunk, nodes, kind
// and sign, the squares of its chord and of its bulge
static void
SetBracket(tag_wapx_type* n, long sign, long kind, long from, long to, long chunk, brack_type* brackets, long at)
{
	brack_type* b = &brackets[at];
	b->fSign = (int32_t) sign;
	b->fKind = (int32_t) kind;
	b->fFrom = (int32_t) from;
	b->fTo = (int32_t) to;
	b->fChunk = (int32_t) chunk;
	b->fHeight2 = (int32_t) H2Arc(n, from, to);
	b->fLength2 = (int32_t) L2Arc(n, from, to);
}


// ROM 0x002878c8 (unnamed) - a chunk's nodes from..to cut into brackets
// A run turning one way is an arc (kind 2), a node where a stroke ends or
// the line turns sharply ends a piece, and a piece of one segment is a
// line (kind 1); a chunk of one node is one bracket of kind nought.
// ==> how many were written, from brackets[at].
static long
MakeBrackets(tag_wapx_type* n, long from, long to, long chunk, brack_type* brackets, long at)
{
	if (from == to)
	{
		SetBracket(n, 0, 0, from, to, chunk, brackets, at);
		return 1;
	}
	long made = 0;
	while (from < to)
	{
		long sign, kind, end;
		UByte f = n[from + 1].fFlags;
		if ((f & kApxEnd) || (f & kApxCorner) || from + 1 == to)
		{
			kind = 1;
			sign = 0;
			end = from + 1;
		}
		else
		{
			sign = SgnArc(n, from, from + 1, from + 2);
			long j = from + 1;
			for (;;)
			{
				if (j >= to - 1)
					break;
				long s = SgnArc(n, j, j + 1, j + 2);
				UByte g = n[j + 1].fFlags;
				if ((g & kApxEnd) || (g & kApxCorner) || sign != s)
					break;
				j++;
			}
			kind = 2;
			end = j + 1;
		}
		SetBracket(n, sign, kind, from, end, chunk, brackets, at + made);
		made++;
		from = end;
	}
	return made;
}


// ROM 0x00286ce8 (unnamed) - brackets at and at+1 made one
// The first takes the second's end and the kind, sign and measures given;
// the rest move down one.  ==> the new count.
static long
JoinBrackets(brack_type* b, long count, long at, long kind, long sign, long height2, long length2)
{
	b[at].fLength2 = (int32_t) length2;
	b[at].fHeight2 = (int32_t) height2;
	b[at].fSign = (int32_t) sign;
	b[at].fKind = (int32_t) kind;
	b[at].fTo = b[at + 1].fTo;
	if (at + 2 < count)
		memmove(&b[at + 1], &b[at + 2], (count - at - 2) * sizeof(brack_type));	// (the ROM's memcpy copies forwards when the source is above)
	return count - 1;
}


// ROM 0x00286fb4 (unnamed) - the brackets tidied until nothing changes
// An arc of too little bulge (under ten) is a line; a bracket small
// against the mean chunk (a hundredth of three times the mean squared
// chord) is joined to its neighbour in the same segment as an arc - to
// the next unless it ends a segment, else to the one before - unless it
// lies well above it; an arc is run on into the next piece when it
// carries on the same way without crossing it, and two lines meeting at
// a gentle turn, or at a sharp one that turns the right way, become one
// line or an arc.
static long
TidyBrackets(tag_wapx_type* n, brack_type* b, long count, long mean)
{
	long small = mean * 3;
	for (;;)
	{
		Boolean changed = false;
		if (count < 1)
			return count;
		for (long i = 0; i < count; i++)
		{
			brack_type* B = &b[i];
			if (B->fKind == 2 && B->fHeight2 < 10)
			{
				B->fKind = 1;
				changed = true;
				B->fSign = 0;
			}
			int32_t l = B->fLength2, h = B->fHeight2;
			int32_t m = l < h ? h : l;
			if (m * 100 <= small)
			{
				if (B->fKind == 2)
				{
					B->fKind = 1;
					changed = true;
					B->fSign = 0;
				}
				long to = B->fTo;
				if (!(n[to].fFlags & kApxSegEnd))
				{
					// joined to the next
					brack_type* N = &b[i + 1];
					long from = B->fFrom;
					if (n[from].y - n[N->fTo].y < 0 && mean < m * 100)
						goto shapes;
					int32_t m2 = N->fLength2 < N->fHeight2 ? N->fHeight2 : N->fLength2;
					long sign, height;
					if (small < m2 * 100 && N->fKind != 1)
					{
						sign = N->fSign;
						long q = HWRAbs(v_QDistFromChord(n[from].x, n[from].y, n[N->fFrom + 1].x, n[N->fFrom + 1].y, n[to].x, n[to].y));
						height = N->fHeight2;
						if (height < q)
							height = HWRAbs(v_QDistFromChord(n[from].x, n[from].y, n[N->fFrom + 1].x, n[N->fFrom + 1].y, n[to].x, n[to].y));
					}
					else
					{
						sign = SgnArc(n, from, N->fFrom, N->fTo);
						height = HWRAbs(v_QDistFromChord(n[from].x, n[from].y, n[N->fTo].x, n[N->fTo].y, n[N->fFrom].x, n[N->fFrom].y));
					}
					long length = L2Arc(n, from, N->fTo);
					count = JoinBrackets(b, count, i, 2, sign, height, length);
					changed = true;
					m = B->fLength2 < B->fHeight2 ? B->fHeight2 : B->fLength2;
					if (m * 100 <= small)
					{
						B->fKind = 1;
						B->fSign = 0;
					}
				}
				else
				{
					// joined to the one before
					long from = B->fFrom;
					if (n[from].fFlags & kApxSegStart)
						goto shapes;
					brack_type* P = &b[i - 1];
					if (n[P->fFrom].y - n[to].y < 0 && mean < m * 100)
						goto shapes;
					int32_t m2 = P->fLength2 < P->fHeight2 ? P->fHeight2 : P->fLength2;
					long sign, height;
					if (small < m2 * 100 && P->fKind != 1)
					{
						sign = P->fSign;
						long q = HWRAbs(v_QDistFromChord(n[P->fTo - 1].x, n[P->fTo - 1].y, n[to].x, n[to].y, n[from].x, n[from].y));
						height = P->fHeight2;
						if (height < q)
							height = HWRAbs(v_QDistFromChord(n[P->fTo - 1].x, n[P->fTo - 1].y, n[to].x, n[to].y, n[from].x, n[from].y));
					}
					else
					{
						sign = SgnArc(n, P->fFrom, P->fTo, to);
						height = HWRAbs(v_QDistFromChord(n[P->fFrom].x, n[P->fFrom].y, n[to].x, n[to].y, n[from].x, n[from].y));
					}
					long length = L2Arc(n, P->fFrom, to);
					count = JoinBrackets(b, count, i - 1, 2, sign, height, length);
					m = P->fHeight2 <= P->fLength2 ? P->fLength2 : P->fHeight2;
					if (m * 100 <= small)
					{
						P->fKind = 1;
						P->fSign = 0;
					}
					changed = true;
				}
			}
		shapes:
			B = &b[i];
			if (B->fKind == 2)
			{
				if (B->fLength2 * 3 > B->fHeight2 * 300)
				{
					B->fKind = 1;
					B->fSign = 0;
					changed = true;
					continue;
				}
				if (n[B->fTo].fFlags & kApxSegEnd)
					continue;
				brack_type* N = &b[i + 1];
				long sign, height, length;
				if (PreservNextSgn(n, b, i) != 0)
				{
					height = H2Arc(n, B->fFrom, N->fTo);
					length = L2Arc(n, B->fFrom, N->fTo);
					sign = B->fSign;
				}
				else
				{
					long near;
					if (N->fKind == 1)
					{
						if (CrossArcs(n, 2, B->fFrom, B->fTo, 1, N->fFrom, N->fTo) != 0
						 || SgnArc(n, B->fTo - 2, B->fTo - 1, N->fFrom + 1) != B->fSign)
							continue;
						near = H2Arc(n, B->fTo - 1, N->fTo);
					}
					else
					{
						if (!(N->fKind == 2 && B->fSign == N->fSign)
						 || CrossArcs(n, 2, B->fFrom, B->fTo, 2, N->fFrom, N->fTo) != 0
						 || SgnArc(n, B->fTo - 2, B->fTo - 1, N->fFrom + 1) != B->fSign
						 || SgnArc(n, B->fTo - 1, N->fFrom + 1, N->fFrom + 2) != B->fSign)
							continue;
						near = H2Arc(n, B->fTo - 1, N->fFrom + 1);
					}
					if (near >= 10)
						continue;
					length = L2Arc(n, B->fFrom, N->fTo);
					height = H2Arc(n, B->fFrom, N->fTo);
					sign = B->fSign;
				}
				count = JoinBrackets(b, count, i, 2, sign, height, length);
				changed = true;
			}
			else if (B->fKind == 1)
			{
				if (n[B->fTo].fFlags & kApxSegEnd)
					continue;
				brack_type* N = &b[i + 1];
				long kind, sign, height, length;
				if (N->fKind == 1)
				{
					if (n[B->fTo].fFlags & kApxCorner)
					{
						long s = SgnArc(n, B->fFrom, B->fTo, N->fTo);
						if (s <= 0 && mean <= B->fLength2 * 2 && mean <= N->fLength2 * 2)
							continue;
						length = L2Arc(n, B->fFrom, N->fTo);
						height = H2Arc(n, B->fFrom, N->fTo);
						sign = SgnArc(n, B->fFrom, B->fTo, N->fTo);
						kind = 2;
					}
					else
					{
						height = H2Arc(n, B->fFrom, N->fTo);
						length = L2Arc(n, B->fFrom, N->fTo);
						if (length * 3 <= height * 300)
						{
							sign = SgnArc(n, B->fFrom, B->fTo, N->fTo);
							kind = 2;
						}
						else
						{
							sign = 0;
							kind = 1;
						}
					}
				}
				else if (N->fKind == 2)
				{
					if (PreservNextSgn(n, b, i) == 0)
					{
						if ((n[B->fTo].fFlags & kApxCorner)
						 || CrossArcs(n, 1, B->fFrom, B->fTo, 2, N->fFrom, N->fTo) != 0
						 || SgnArc(n, B->fFrom, N->fFrom + 1, N->fFrom + 2) != N->fSign
						 || H2Arc(n, B->fFrom, N->fFrom + 1) > 9)
							continue;
						length = L2Arc(n, B->fFrom, N->fTo);
						height = H2Arc(n, B->fFrom, N->fTo);
					}
					else
					{
						height = H2Arc(n, B->fFrom, N->fTo);
						length = L2Arc(n, B->fFrom, N->fTo);
					}
					sign = N->fSign;
					kind = 2;
				}
				else
					continue;
				count = JoinBrackets(b, count, i, kind, sign, height, length);
				changed = true;
			}
		}
		if (!changed)
			return count;
	}
}


// ROM 0x002a7b2c PreservNextSgn__FP13tag_wapx_typeP10brack_typei
// Whether bracket i and the next can be run together keeping the way
// they turn: a line and an arc when the arc turns on from the line's
// start and they do not cross; two arcs turning the same way through
// the join without crossing; an arc and a line when the arc keeps
// turning onto the line.  ==> 1 or 0.
// ROM QUIRK: for a bracket of neither kind it answers the node array's
// address (true).
long
PreservNextSgn(tag_wapx_type* n, brack_type* b, long i)
{
	brack_type* B = &b[i];
	brack_type* N = &b[i + 1];
	long crossed;
	if (B->fKind == 1)
	{
		if (N->fKind != 2)
			return 1;
		if (SgnArc(n, B->fFrom, B->fTo, N->fFrom + 1) != N->fSign)
			return 0;
		crossed = CrossArcs(n, 1, B->fFrom, B->fTo, 2, N->fFrom + 1, N->fTo);
	}
	else
	{
		if (B->fKind != 2)
			return n != nil;
		if (N->fKind != 2)
		{
			if (SgnArc(n, B->fTo - 1, B->fTo, N->fTo) != B->fSign)
				return 0;
			if (CrossArcs(n, 2, B->fFrom, B->fTo - 1, 1, N->fFrom, N->fTo) != 0)
				return 0;
			return 1;
		}
		if (!(B->fSign == N->fSign && SgnArc(n, B->fTo - 1, B->fTo, N->fFrom + 1) == B->fSign))
			return 0;
		crossed = CrossArcs(n, 2, B->fFrom, B->fTo, 2, N->fFrom, N->fTo);
	}
	return crossed == 0 ? 1 : 0;
}


// ROM 0x00286e24 (unnamed) - whether a stroke's first bracket is a hook
// It is when it is small against its chunk's height: its box no taller
// than a sixth of it, and either narrower than an eighth too, or the
// node it ends at is a corner and it is narrower than a sixth.
static long
IsStartHook(tag_wapx_type* n, tag_CHUNK* chunk, brack_type* b)
{
	int32_t height = chunk->fHeight;
	tag_BOX box;
	GetBox(n, b->fFrom, b->fTo, &box);
	int32_t h = box.bottom - box.top;
	int32_t w = box.right - box.left;
	if (h * 6 > height)
		return 0;
	int32_t limit = h * 8;
	if (limit < height)
		limit = w * 8;
	if (height > limit || ((n[b->fTo].fDir & 0xff00) != 0 && w * 6 < height))
		return 1;
	return 0;
}


// ROM 0x00286f20 (unnamed) - whether a stroke's last bracket is a hook
// The same, the corner looked for at the node it starts from.
static long
IsEndHook(tag_wapx_type* n, tag_CHUNK* chunk, brack_type* b)
{
	int32_t height = chunk->fHeight;
	tag_BOX box;
	GetBox(n, b->fFrom, b->fTo, &box);
	int32_t h = box.bottom - box.top;
	int32_t w = box.right - box.left;
	if (h * 6 > height)
		return 0;
	int32_t limit = h * 8;
	if (limit < height)
		limit = w * 8;
	if (height > limit || ((n[b->fFrom].fDir & 0xff00) != 0 && w * 6 < height))
		return 1;
	return 0;
}


// ROM 0x00286d54 (unnamed) - the hooks at the ends of strokes dropped
// (their chunk made -1, to be left out)
static long
DropHooks(tag_wapx_type* n, tag_CHUNK* chunks, brack_type* b, long count)
{
	long last = -1;
	for (long i = 0; i < count; i++)
	{
		long c = b[i].fChunk;
		if (c != last && chunks[c].fPrev == -1 && IsStartHook(n, &chunks[c], &b[i]) != 0)
			b[i].fChunk = -1;
		if (chunks[c].fNext == -1 && (count <= i + 1 || b[i + 1].fChunk != c) && IsEndHook(n, &chunks[c], &b[i]) != 0)
			b[i].fChunk = -1;
		last = c;
	}
	return 1;
}


// ROM 0x00286a54 ApxToBrackets__FP13tag_wapx_typeP9tag_CHUNKiPP10brack_type
// Each chunk cut into brackets, tidied, the hooks at the strokes' ends
// dropped, and the rest copied into a block (with room for forty more)
// and each chunk told its first and last.  ==> how many; nought for want
// of memory.
// DEVIATION: with no bracket left the ROM tells the chunk named by the
// word before the block that its last is -1; the host does nothing.
long
ApxToBrackets(tag_wapx_type* n, tag_CHUNK* chunks, long count, brack_type** result)
{
	long made = 0;
	brack_type* work = (brack_type*) HWRMemoryAlloc(chunks[count - 1].fTo * sizeof(brack_type));
	if (work == nil)
		return 0;
	long mean = midL2Chunks(chunks, count);
	for (long k = 0; k < count; k++)
	{
		chunks[k].fLastBracket = -1;
		chunks[k].fFirstBracket = -1;
	}
	for (long k = 0; k < count; k++)
		if (chunks[k].fKind != 3)
			made += MakeBrackets(n, chunks[k].fFrom, chunks[k].fTo, k, work, made);
	made = TidyBrackets(n, work, made, mean);
	DropHooks(n, chunks, work, made);
	ULong size = (made + 40) * sizeof(brack_type);
	brack_type* b = (brack_type*) HWRMemoryAlloc(size);
	*result = b;
	if (b == nil)
	{
		HWRMemoryFree((Ptr) work);
		return 0;
	}
	memset(b, 0, size);
	long kept = 0;
	for (long i = 0; i < made; i++)
		if (work[i].fChunk != -1)
			b[kept++] = work[i];
	long last = -1;
	for (long i = 0; i < kept; i++)
	{
		long c = b[i].fChunk;
		if (c != last)
		{
			chunks[c].fFirstBracket = (int32_t) i;
			last = c;
		}
		if (i < kept - 1 && b[i + 1].fChunk != last)
			chunks[last].fLastBracket = (int32_t) i;
	}
	if (kept > 0)
		chunks[b[kept - 1].fChunk].fLastBracket = (int32_t) (kept - 1);
	HWRMemoryFree((Ptr) work);
	return kept;
}


#pragma mark - the classes

// ROM 0x00287ac0 (unnamed) - a chunk of mixed brackets turning the other
// way: a stroke's last chunk going down with little bulge for its length
// is a line (300/301), anything else 500/502
static void
MixedClass(tag_CHUNK* c, int32_t* cls, int32_t* sub)
{
	if (c->fKind == 2 && c->fNext == -1 && c->fBulge * 25 < c->fLength2)
	{
		*cls = 300;
		*sub = 301;
	}
	else
	{
		*cls = 500;
		*sub = 502;
	}
}


// ROM 0x00287b14 (unnamed) - a chunk's class (f74) and subclass (f78)
// from the shape of its brackets and which way it turns
static void
SetChunkClass(long sign, long shape, tag_CHUNK* chunks, long k)
{
	tag_CHUNK* c = &chunks[k];
	int32_t cls, sub;
	switch (shape)
	{
	default:
		cls = 1400;
		sub = 0;
		break;
	case 1:							// a line
		cls = 300;
		sub = 301;
		break;
	case 2:							// an arc
		cls = 400;
		sub = sign == -1 ? 402 : 401;
		break;
	case 3:							// two lines
		cls = 600;
		sub = sign == -1 ? 601 : 602;
		break;
	case 4:							// two arcs turning one way
		cls = 700;
		sub = sign == -1 ? 701 : 702;
		break;
	case 5:							// an arc and a line, or two arcs turning apart
		cls = 500;
		if (sign == -1)
			sub = 501;
		else
			MixedClass(c, &cls, &sub);
		break;
	}
	c->f74 = cls;
	c->f78 = sub;
}


// ROM 0x00287c18 (unnamed) - each chunk classed by its brackets: one
// bracket its kind, two the pair's (the turn between two lines, whether
// two arcs turn the same way), more than two 1400
static long
ClassChunks(tag_wapx_type* n, brack_type* b, long count, tag_CHUNK* chunks)
{
	for (long i = 0; i < count; )
	{
		long c = b[i].fChunk;
		long j = i;
		do
			j++;
		while (j < count && b[j].fChunk == c);
		long sign, shape;
		if (j - i == 1)
		{
			shape = b[i].fKind;
			sign = b[i].fSign;
		}
		else if (j - i == 2)
		{
			brack_type* A = &b[i];
			brack_type* Z = &b[j - 1];
			if (A->fKind == Z->fKind)
			{
				if (A->fKind == 1)
				{
					sign = SgnArc(n, A->fFrom, A->fTo, Z->fTo);
					shape = 3;
				}
				else
				{
					sign = A->fSign;
					shape = sign == Z->fSign ? 4 : 5;
				}
			}
			else
			{
				sign = A->fKind == 1 ? -Z->fSign : A->fSign;
				shape = 5;
			}
		}
		else
		{
			sign = b[i].fSign;
			shape = 8;
		}
		SetChunkClass(sign, shape, chunks, c);
		i = j;
	}
	return 0;
}


// ROM 0x00287aa8 ApxToCLine__FP13tag_wapx_typeP10brack_typeiP9tag_CHUNKT3
long
ApxToCLine(tag_wapx_type* n, brack_type* b, long count, tag_CHUNK* chunks, long chunkCount)
{
	ClassChunks(n, b, count, chunks);
	return 1;
}


// ROM 0x00285bc8 (unnamed) - the classes looked at again
// A chunk classed 501, 502, 702, 701 or with class 1400 that is a tail -
// a stroke's piece whose neighbour in the array has more than four times
// its squared chord - is a line (301).  Then a chunk classed 501, 502,
// 702 or 1400 has the turns along it counted from each end (f90 from the
// start, f91 from the end): how far the directions keep turning one way
// before they turn back.  ==> -1.
// ROM QUIRK: the neighbours are the chunks either side in the array, not
// the ones the chunk is linked to; and 701 is a tail's class only.
static long
ReclassChunks(tag_wapx_type* n, tag_CHUNK* chunks, long count)
{
	for (long k = 0; k < count; k++)
	{
		tag_CHUNK* c = &chunks[k];
		int32_t sub = c->f78;
		if (sub == 501 || sub == 502 || c->f74 == 1400 || sub == 702 || sub == 701)
		{
			Boolean tail = false;
			if (c->fPrev == -1)
			{
				if (c->fNext != -1 && chunks[k + 1].fLength2 > c->fLength2 * 4)
					tail = true;
			}
			else if (chunks[k - 1].fLength2 > c->fLength2 * 4)
				tail = true;
			else if (c->fNext != -1 && chunks[k + 1].fLength2 > c->fLength2 * 4)
				tail = true;
			if (tail)
			{
				c->f78 = 301;
				continue;
			}
		}
		if (!(sub == 501 || sub == 502 || c->f74 == 1400 || sub == 702))
			continue;
		long to = c->fTo, from = c->fFrom;
		long nodes = to - from + 1;
		long turn = 100, at = 0;
		for (long k2 = c->f90; k2 < nodes - 2; k2++)
		{
			long a = GetAngleBetweenTwoDir(n[from + k2].fDirOut, n[from + k2 + 1].fDirOut);
			if (turn == 100)
			{
				at = k2 + 2;
				if (a != 0)
					turn = a;
			}
			else if (turn * a < 0)
			{
				c->f90 = (UByte) at;
				break;
			}
			else
				at = k2 + 2;
		}
		turn = 100;
		at = 0;
		for (long k2 = c->f91 + 1; k2 < nodes - 1; k2++)
		{
			long a = GetAngleBetweenTwoDir(n[to - k2].fDirOut, n[to - k2 - 1].fDirOut);
			if (turn == 100)
			{
				at = k2 + 1;
				if (a != 0)
					turn = a;
			}
			else if (turn * a < 0)
			{
				c->f91 = (UByte) at;
				break;
			}
			else
				at = k2 + 1;
		}
	}
	return -1;
}


#pragma mark -

// ROM 0x00285a64 ChunkConstruct__FP15tag_CHUNK_STAFF
// The staff's polyline cut into chunks, strokes and brackets, the chunks
// classed and the real chunks listed.  ==> how many chunks, -1 on
// failure (what was made is left for ChunkDestroyData).
long
ChunkConstruct(tag_CHUNK_STAFF* staff)
{
	tag_wapx_type* n = staff->fNodes;
	long nodes = staff->fNodeCount;
	tag_STK* strokes = nil;
	long strokeCount = 0;
	brack_type* brackets = nil;
	int32_t* real = nil;
	tag_CHUNK* chunks = (tag_CHUNK*) HWRMemoryAlloc(kMaxChunks * sizeof(tag_CHUNK));
	if (chunks == nil)
		return -1;
	memset(chunks, 0, kMaxChunks * sizeof(tag_CHUNK));
	staff->fChunks = chunks;
	long count = ChunkFillMainData(chunks, n, nodes);
	if (count == -1)
		return -1;
	staff->fChunkCount = (int32_t) count;
	if (ChunkMakeStrokes(chunks, n, count, &strokes, &strokeCount) <= 0)
		return -1;
	staff->fStrokes = strokes;
	staff->fStrokeCount = (int32_t) strokeCount;
	long nBrackets = ApxToBrackets(n, chunks, count, &brackets);
	staff->fBrackets = brackets;
	staff->fBracketCount = (int32_t) nBrackets;
	if (nBrackets <= 0)
		return -1;
	ApxToCLine(n, brackets, nBrackets, chunks, count);
	ReclassChunks(n, chunks, count);
	long nReal = CreateRealChunkInd(chunks, count, &real);
	staff->fRealCount = (int32_t) nReal;
	staff->fRealChunks = real;
	if (nReal <= 0)
		return -1;
	staff->f34 = 100;
	staff->f38 = (int32_t) (nBrackets + 40);
	staff->f3C = (int32_t) (nReal + 20);
	return count;
}


// ROM 0x00286eb8 ChunkDestroyData__FP15tag_CHUNK_STAFF
// What ChunkConstruct made given back.
long
ChunkDestroyData(tag_CHUNK_STAFF* staff)
{
	FreeAndClear((void**) &staff->fRealChunks);
	if (staff->fBrackets != nil)
		HWRMemoryFree((Ptr) staff->fBrackets);
	if (staff->fStrokes != nil)
		HWRMemoryFree((Ptr) staff->fStrokes);
	if (staff->fChunks != nil)
		HWRMemoryFree((Ptr) staff->fChunks);
	staff->fChunks = nil;
	staff->fChunkCount = 0;
	staff->fStrokes = nil;
	staff->fStrokeCount = 0;
	staff->fBrackets = nil;
	staff->fBracketCount = 0;
	staff->fRealChunks = nil;
	staff->fRealCount = 0;
	return 1;
}


#pragma mark - what the searchers start from

// ROM 0x00287d48 ChunkPutClassesToLO__FPvP13tag_wapx_typeP9tag_CHUNKi
// Each chunk that is not a jump put in the list of low objects as its
// class (f74, subclass f78 the object's value) over its nodes, the
// object's index kept in f70 (-1 for a jump).  ==> how many were put.
long
ChunkPutClassesToLO(void* lo, tag_wapx_type* n, tag_CHUNK* chunks, long count)
{
	long put = 0;
	for (long k = 0; k < count; k++)
	{
		tag_CHUNK* c = &chunks[k];
		if (c->fKind == 3)
			c->f70 = -1;
		else
		{
			c->f70 = (int32_t) LO_Add(lo, n, (ULong) c->f74, c->fFrom, c->fTo, (ULong) c->f78, -1);
			put++;
		}
	}
	return put;
}


// ROM 0x00287de0 DefRectForChunks__FP9tag_CHUNKP13tag_wapx_typeiT3P5_RECT
// The box round the nodes from chunk first's start to chunk last's end.
// ROM QUIRK: its type is named _RECT, but it writes four words - not the
// four halfwords of the low level's _RECT (LowLevel.h): ParaGraph's
// sources had two types of that name; the host takes a tag_BOX.
long
DefRectForChunks(tag_CHUNK* chunks, tag_wapx_type* n, long first, long last, tag_BOX* r)
{
	long k = chunks[first].fFrom;
	int32_t left = n[k].x, right = left;
	int32_t top = n[k].y, bottom = top;
	for (k++; k <= chunks[last].fTo; k++)
	{
		if (n[k].x < left)
			left = n[k].x;
		else if (n[k].x > right)
			right = n[k].x;
		if (n[k].y < top)
			top = n[k].y;
		else if (n[k].y > bottom)
			bottom = n[k].y;
	}
	r->left = left;
	r->top = top;
	r->right = right;
	r->bottom = bottom;
	return 1;
}
