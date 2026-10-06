/*
	File:		ChunkNumber.cpp

	Contains:	The digit reader's verdict on whether the writing is a
				number (SearchNumber): statistics gathered over the digits
				the second looks wrote out (class 1900) - how many of each,
				how many chunks they took, how their heights and bottoms
				vary from one to the next - and a decision over them.  See
				Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x002a28d0-0x002a3ef8, and 0x002a3608, 0x002a4608-
				0x002a4a34 from among FindPound's statics); each function
				cites its origin.  All of it is from the disassembly.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRAbs
#include "host/RomBugs.h"
#include <limits.h>


// What the statistics come to (SearchNumber's 0x44 bytes on the stack):
// chunk counts and digit counts by kind, and marks of how irregular the
// writing is (0 none, 1 a little, 2 much; 0x65 - 101 - rules the number
// out).
struct NumberStats
{
	int32_t		fChunks;			// +00  starts as the real chunks, less those that belong to a 7 that goes on, a 0 not whole, a code 12 or 69
	int32_t		fDigitChunks;		// +04  the chunks the digits took
	int32_t		fOtherChunks;		// +08  the chunks the other codes took
	int32_t		fFiveChunks;		// +0c
	int32_t		fFives;				// +10  the 5s whose chunks run on
	int32_t		fBroken;			// +14  7s that go on, whole 0s
	int32_t		fDigits;			// +18
	int32_t		fOthers;			// +1c
	int32_t		fJumps;				// +20  the pen's jumps between strokes
	int32_t		fShape;				// +24  0x65: not a number
	int32_t		fHeights;			// +28  how the heights vary
	int32_t		fMeanHeight;		// +2c
	int32_t		fBottoms;			// +30  how the bottoms step
	int32_t		fTops;				// +34  how the tops step
	int32_t		fOverlap;			// +38  one digit reaching into the next's line
	int32_t		fSmall;				// +3c  the small strokes written below the one before (and 0x65 with a digit of over eight chunks)
	int32_t		fSignChunks;		// +40  the chunks of a sign coded 70, 17 or 21
};

const int32_t	kNotANumber	= 0x65;

static inline bool
Is501(const tag_CHUNK* c)
{
	return c->f78 == 501;
}


// ROM 0x002a4608 (unnamed) - how many strokes (after the first) are small
// and written below the middle of the stroke before, with a chunk of that
// stroke tall beside them and above their bottom
static long
SmallStrokesBelow(tag_CHUNK* chunks, tag_STK* strokes, long count)
{
	long n = 0;
	if (count == 0)
		return 0;
	for (long k = 0; k < count; k++)
	{
		if (k == 0)
			continue;
		tag_STK* s = &strokes[k];
		tag_STK* p = &strokes[k - 1];
		if (s->fBottom - (p->fBottom + p->fTop) / 2 > p->fHeight / 8)
			continue;
		if (s->fHeight > p->fHeight)
			continue;
		if (s->fWidth > p->fHeight / 2)
			continue;
		if (p->fLastChunk < p->fFirstChunk)
			continue;
		for (long c = p->fFirstChunk; c <= p->fLastChunk; c++)
		{
			tag_CHUNK* ch = &chunks[c];
			if (ch->fTop > s->fBottom && ch->fHeight > s->fHeight * 2)
			{
				n++;
				break;
			}
		}
	}
	return n;
}


// ROM 0x002a3608 (unnamed) - how much of stroke b's width a chunk covers,
// where the chunk crosses the height of b's middle inside b: b's whole
// width when either lies across the other, nought when it crosses outside
static long
ChunkAcrossStroke(tag_wapx_type* n, tag_CHUNK* ch, tag_STK* b)
{
	long midY = (b->fTop + b->fBottom) / 2;
	if (ch->fRight > b->fRight && ch->fLeft < b->fLeft)
		return b->fWidth;
	if (ch->fRight < b->fRight && ch->fLeft > b->fLeft)
		return b->fWidth;
	long x;
	if (ch->f74 == 300)
		x = x_in_line(n[ch->fTopNode].x, ch->fTop, n[ch->fBottomNode].x, ch->fBottom, midY);
	else
		x = x_in_curve(n, ch, midY);
	if (b->fLeft >= x || b->fRight <= x)
		return 0;
	long right = ch->fRight >= b->fRight ? b->fRight : ch->fRight;
	long left = ch->fLeft > b->fLeft ? ch->fLeft : b->fLeft;
	return right - left;
}


// ROM 0x002a4978 (unnamed) - of stroke a's chunks lying within b's box, the
// one that covers most of b (ChunkAcrossStroke): ==> how much (b's whole
// width at once when one lies across it), *which the chunk
static long
MostAcross(tag_CHUNK_STAFF* staff, tag_STK* a, tag_STK* b, long* which)
{
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	long best = 0, at = 0;			// (at: the ROM leaves it unset when nothing is found)
	for (long k = a->fFirstChunk; k <= a->fLastChunk; k++)
	{
		tag_CHUNK* ch = &chunks[k];
		if (ch->fTop > b->fBottom)
			continue;
		if (ch->fBottom < b->fTop || ch->fRight < b->fLeft)
			continue;
		if (ch->fLeft > b->fRight)
			continue;
		long v = ChunkAcrossStroke(n, ch, b);
		if (v == b->fWidth)
			return v;
		if (v > best)
		{
			best = v;
			at = k;
		}
	}
	*which = at;
	return best;
}


// ROM 0x002a480c (unnamed) - how stroke a stands to stroke b: 1 when one
// lies within the other's span or they overlap too little to be one
// character, 2 when a is b's other half (a bar across it, a stroke meeting
// it), 0 when a lies clear of it
static long
StrokeBeside(tag_CHUNK_STAFF* staff, tag_STK* a, tag_STK* b)
{
	tag_wapx_type* n = staff->fNodes;
	if (a->fRight <= b->fRight && a->fLeft >= b->fLeft)
		return 1;
	if (a->fRight >= b->fRight && a->fLeft <= b->fLeft)
		return 1;
	long right = a->fRight >= b->fRight ? b->fRight : a->fRight;
	long left = a->fLeft <= b->fLeft ? b->fLeft : a->fLeft;
	long overlap = right - left;
	long quarterW = b->fWidth / 4;
	if (overlap < 0)
	{
		if (a->fTop + a->fHeight / 4 <= b->fTop)
			return 0;
		if (b->fRight < a->fLeft)
			return 0;
		if (-(n[a->fTopNode].x - b->fLeft) > quarterW)
			return 0;
		return 2;
	}
	long d;
	if (b->fBottom > a->fBottom)
	{
		d = n[a->fBottomNode].x;
		d = b->fRight < a->fRight ? b->fRight - d : d - b->fLeft;
		return d <= b->fWidth / 2 ? 2 : 1;
	}
	if (b->fTop < a->fTop)
	{
		d = n[a->fTopNode].x;
		d = b->fRight <= a->fRight ? b->fRight - d : d - b->fLeft;
	}
	else
	{
		long which;
		d = MostAcross(staff, a, b, &which);
	}
	return d <= quarterW ? 2 : 1;
}


// ROM 0x002a3700 (unnamed) - the digits (class 1900) with a chunk in stroke
// s taken out (value 0xffff), up to the first beyond it
static void
DropDigitsOfStroke(tag_CHUNK_STAFF* staff, long s)
{
	void* lo = staff->fLO;
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	if (LO_PickFirst(lo, &obj))
	{
		do
		{
			long many = LO_HowManyChunks(lo, obj);
			long first = LO_GetRealChunkInd(lo, chunks, n, obj, 1);
			long last = LO_GetRealChunkInd(lo, chunks, n, obj, many);
			if (chunks[real[first]].fStroke == s || chunks[real[last]].fStroke == s)
				obj->fValue = 0xffff;
			if (chunks[real[first]].fStroke > s)
				break;
		} while (LO_PickNext(lo, &obj));
	}
	LO_SetWorkClass(lo, saved);
}


// ROM 0x002a46fc (unnamed) - a sign coded 13 (the object's last real chunk
// given) against the strokes about its own: StrokeBeside of the stroke
// before, the stroke after, then any other; a 1 there takes the digits of
// that stroke out.  ==> that answer.  (The box it is given is not used:
// the ROM loads over the register before reading it.)
static long
CheckSignStroke(tag_CHUNK_STAFF* staff, long lastReal, const int32_t* /*box*/)
{
	tag_STK* strokes = staff->fStrokes;
	long count = staff->fStrokeCount;
	long s = staff->fChunks[staff->fRealChunks[lastReal]].fStroke;
	tag_STK* own = &strokes[s];
	// ROM QUIRK: with no stroke but its own to look at, the answer is the
	// register the caller had (its last real chunk) and the stroke taken
	// out is the caller's r9 (a pointer, which no stroke index equals)
	long answer = lastReal;
	long other = LONG_MAX;
	bool done = false;
	if (s > 0)
	{
		answer = StrokeBeside(staff, &strokes[s - 1], own);
		if (answer != 0)
		{
			other = s - 1;
			done = true;
		}
	}
	if (!done && s + 1 < count)
	{
		answer = StrokeBeside(staff, &strokes[s + 1], own);
		if (answer != 0)
		{
			other = s + 1;
			done = true;
		}
	}
	if (!done)
	{
		for (long k = 0; k < count; k++)
		{
			if (k == s || k == s - 1 || k == s + 1)
				continue;
			answer = StrokeBeside(staff, &strokes[k], own);
			if (answer != 0)
			{
				other = k;
				break;
			}
		}
	}
	if (answer == 1)
		DropDigitsOfStroke(staff, other);
	return answer;
}


// ROM 0x002a3838 (unnamed) - the next object of a class list, skipping
// those taken out (value 0xffff), from the list's own links; ==> 1 when it
// moved, *obj updated.  ROM QUIRK: it stops at the list's end on a taken-
// out object and answers that one.
static long
NextLiveObject(tag_LOWOBJ* objects, tag_LOWOBJ** obj)
{
	long moved = 0;
	long steps = 0;
	tag_LOWOBJ* o = *obj;
	while (o->fNext != -1 && steps < 100)
	{
		steps++;
		o = &objects[o->fNext];
		if (o->fValue != 0xffff)
			break;
	}
	if (steps != 0 && steps != 100)
	{
		moved = 1;
		*obj = o;
	}
	return moved;
}


// ROM 0x002a3db4 (unnamed) - the signs coded 13 among the digits checked
// against the strokes beside them (CheckSignStroke) and taken out when
// they belong to another character: ==> how many were answered 1
static long
CheckSigns13(tag_CHUNK_STAFF* staff)
{
	void* lo = staff->fLO;
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	tag_LOWOBJ* objects = ((LOBlock*) lo)->fObjects;
	long count = 0;
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	if (LO_PickFirst(lo, &obj))
	{
		do
		{
			if (obj->fValue != 0xffff)
			{
				long many = LO_HowManyChunks(lo, obj);
				long last = LO_GetRealChunkInd(lo, chunks, n, obj, many);
				if ((ULong) obj->fValue % 100 == 13)
				{
					long r = CheckSignStroke(staff, last, &obj->fLeft);
					if (r == 1)
					{
						obj->fValue = 0xffff;
						count++;
					}
					else if (r == 2)
						obj->fValue = 0xffff;
				}
			}
		} while (NextLiveObject(objects, &obj));
	}
	LO_SetWorkClass(lo, saved);
	return count;
}


// ROM 0x002a2954 (unnamed) - the statistics over the digits (class 1900)
static void
GatherNumberStats(tag_CHUNK_STAFF* staff, NumberStats* st)
{
	void* lo = staff->fLO;
	tag_wapx_type* n = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	long realCount = staff->fRealCount;
	int32_t* real = staff->fRealChunks;
	tag_STK* strokes = staff->fStrokes;
	long strokeCount = staff->fStrokeCount;
	tag_LOWOBJ* obj = nil;
	long ones = 0, digits = 0, fives = 0, zeros = 0, zeroChunks = 0, sevens = 0;
	long sixes = 0, nines = 0, twos = 0, ninesTall = 0;
	long codes14 = 0, codes11 = 0, codes10 = 0;
	long prevBottom = 0, prevTop = 0, prevHeight = 0;
	long maxHeight = 0, minHeight = 10000;
	long sumHeight = 0, heights = 0;
	long oneStroke = 0;				// (the ROM's stack word is set only when a 1 is found, and read only then)
	if (staff->f54 != 0)
		for (long i = 0; i < realCount; i++)
			chunks[real[i]].f6C = 0xff;
	long signs13 = CheckSigns13(staff);
	st->fSmall = (int32_t) SmallStrokesBelow(chunks, strokes, strokeCount);
	LO_SetWorkClass(lo, 1900);
	if (LO_PickFirst(lo, &obj))
	{
		do
		{
			if (obj->fValue == 0xffff)
				continue;
			long many = LO_HowManyChunks(lo, obj);
			long first = LO_GetRealChunkInd(lo, chunks, n, obj, 1);
			long last = LO_GetRealChunkInd(lo, chunks, n, obj, many);
			if (many > 8)
				st->fSmall = kNotANumber;
			long d = (long) ((ULong) obj->fValue % 100);
			if (staff->f54 != 0)
				for (long i = first; i <= last; i++)
					chunks[real[i]].f6C = (int32_t) d;
			long h = obj->fBottom - obj->fTop;
			bool counted = false;		// (to 0x002a2cc8: a digit)
			if (d >= 1 && d <= 9 && d != 7)
			{
				st->fDigits++;
				st->fDigitChunks += many;
				counted = true;
			}
			else if (d == 7)
			{
				if (chunks[real[last]].fNext == -1)
				{
					st->fDigits++;
					st->fDigitChunks += many;
				}
				else
				{
					st->fChunks -= many;
					st->fBroken++;
				}
				if (real[last] - real[first] == last - first)
					sevens++;
				counted = true;
			}
			else if (d == 0x45 || d == 0xc)
				st->fChunks -= many;
			else if (d == 0)
				counted = true;
			else
			{
				st->fOtherChunks += many;
				st->fOthers++;
				if (d == 0xe || d == 0x13)
					codes14++;
				if (d == 0xa)
				{
					codes10++;
					continue;
				}
				if (d == 0xb)
				{
					codes11++;
					continue;
				}
				if (d == 0x46 || d == 0x11 || d == 0x15)
					st->fSignChunks = (int32_t) many;
			}
			if (!counted && d > 9)
				continue;
			if (d <= 9)
			{
				digits++;
				if (d == 1)
				{
					ones++;
					oneStroke = chunks[real[first]].fStroke;
				}
				else if (d == 9)
				{
					if (first == 0)
						nines++;
					tag_CHUNK* c = &chunks[real[first]];
					if (Is501(c) && h - c->fHeight < h / 8)
						ninesTall++;
					else if (first + 1 <= last)
					{
						tag_CHUNK* c2 = &chunks[real[first + 1]];
						if (Is501(c2) && h - c2->fHeight < h / 8)
							ninesTall++;
					}
				}
				else if (d == 2)
				{
					if (first == 0)
						twos++;
				}
				else if (d == 6)
				{
					if (first == 0)
						sixes++;
				}
				else if (d == 5)
				{
					if (real[last] - real[first] == last - first)
					{
						st->fFiveChunks += many;
						st->fFives++;
						fives++;
					}
				}
				else if (d == 0)
				{
					if (real[last] - real[first] == last - first)
					{
						zeros++;
						zeroChunks += many;
						st->fBroken++;
					}
					else
					{
						st->fDigits++;
						st->fDigitChunks += many;
					}
				}
			}

			// how this one stands to the one before
			long w = obj->fRight - obj->fLeft;
			long ref = (prevHeight != 0 && h <= prevHeight) ? prevHeight : h;
			if (prevBottom != 0)
			{
				long db = HWRAbs(prevBottom - obj->fBottom);
				if (db > ref / 2)
					st->fBottoms = 2;
				else if (db > ref / 3 && st->fBottoms == 0)
					st->fBottoms = 1;
			}
			if (prevTop != 0)
			{
				long dt = HWRAbs(prevTop - obj->fTop);
				if (dt > ref / 2)
					st->fTops = 2;
				else if (dt > ref / 3 && st->fTops == 0)
					st->fTops = 1;
				if (obj->fBottom - prevTop < ref / 3)
					st->fOverlap = 1;
			}
			if (prevBottom != 0 && prevBottom - obj->fTop < ref / 3)
				st->fOverlap = 1;
			prevBottom = obj->fBottom;
			prevTop = obj->fTop;
			prevHeight = h;
			if (h > maxHeight)
				maxHeight = h;
			if (h < minHeight)
				minHeight = h;
			// ROM BUG (fixed): a character wider than two and a half
			// times its height is ruled out, and at once marked 2 by the
			// next test (wider than twice), which overwrites it - so width
			// alone never rules a number out.  The fix lets the ruling-out
			// stand, against this character's next test and a later
			// character's.
			if (RomBugFixed())
			{
				if (st->fShape == kNotANumber)
					;
				else if (w - h * 2 > h / 2)
					st->fShape = kNotANumber;
				else if (w > h * 2)
					st->fShape = 2;
				else if (st->fShape == 0 && w * 2 - h * 3 > w / 8)
					st->fShape = 1;
			}
			else
			{
				if (w - h * 2 > h / 2)
					st->fShape = kNotANumber;
				if (w > h * 2)
					st->fShape = 2;
				else if (st->fShape == 0 && w * 2 - h * 3 > w / 8)
					st->fShape = 1;
			}
			sumHeight += h;
			heights++;
		} while (LO_PickNext(lo, &obj));
	}
	long codes1011 = codes11 + codes10;
	st->fMeanHeight = (int32_t) (heights != 0 ? sumHeight / heights : 0);
	if (minHeight * 3 < maxHeight)
		st->fHeights = 2;
	else if (maxHeight > minHeight * 2)
		st->fHeights = 1;

	// a 1 (or 7, or 2) and one other character in two strokes: whether the
	// other's top reaches the 1's line
	if (digits == 1 && (ones == 1 || sevens == 1 || twos == 1) && st->fOthers == 1 && codes14 == 1 && strokeCount == 2)
	{
		tag_STK* s0 = &strokes[0];
		tag_STK* s1 = &strokes[1];
		long h0 = s0->fBottom - s0->fTop;
		long gap;
		long a, b;
		bool test = true;
		if (s1->fLeft > s0->fRight)
		{
			a = s1->fLeft;
			b = s0->fRight;
		}
		else if (s1->fRight >= s0->fLeft)
		{
			gap = 0;
			test = false;
		}
		else
		{
			a = s0->fLeft;
			b = s1->fRight;
		}
		if (test)
		{
			gap = a - b;
			if (gap != 0 && ones == 1 && s1->fTop - s0->fBottom > h0 / 8)
			{
				long x = x_in_line(n[s0->fTopNode].x, s0->fTop, n[s0->fBottomNode].x, s0->fBottom, s1->fTop);
				long d;
				if (s1->fLeft > x)
					d = s1->fLeft - x;
				else if (s1->fRight >= x)
					d = 0;
				else
					d = x - s1->fRight;
				if (gap > d)
					gap = d;
			}
		}
		if (s1->fBottom - s1->fTop < h0 && s0->fBottom - s1->fTop < h0 / 10 + 1 && gap < h0 / 8)
			st->fShape = kNotANumber;
	}
	if (digits == ones)
	{
		if (ones != 0)
		{
			if (ones < 5 && st->fSignChunks == 0
			 && ((st->fOthers < 2 && codes1011 == 0) || (codes10 == 1 && codes11 == 1 && st->fOthers < 4)))
			{
				if (st->fBottoms != 0 || st->fHeights != 0)
					st->fShape = kNotANumber;
				if (oneStroke > 2 && maxHeight > strokes[oneStroke - 2].fHeight * 2)
					st->fShape = kNotANumber;
			}
		}
	}
	bool pair = false;
	if (ones == 1)
	{
		if (codes11 == 1 && digits == 1 && st->fOthers == 1 && strokeCount == 2)
		{
			tag_STK* s0 = &strokes[0];
			tag_STK* s1 = &strokes[1];
			long gap = s1->fLeft - s0->fRight;
			long h1 = s1->fHeight;
			long q = h1 / 6;
			if (gap < q)
			{
				long dh = h1 - s0->fHeight;
				if (dh < q
				 || (dh < h1 / 4 && (s0->fTop - s1->fTop < q || s1->fBottom - s0->fBottom < q)))
					st->fShape = kNotANumber;
			}
		}
		pair = fives == 1;
	}
	if (pair || (zeros == 1 && fives == 1))
	{
		if (digits == 2 && st->fSignChunks == 0
		 && ((st->fOthers < 2 && codes1011 == 0) || (codes10 == 1 && codes11 == 1 && st->fOthers < 4)))
			st->fShape = kNotANumber;
	}
	if ((ones == 1 && ninesTall == 1) || (zeros == 1 && ninesTall == 1))
	{
		if (digits == 2 && st->fSignChunks == 0
		 && ((st->fOthers < 2 && codes1011 == 0) || (codes10 == 1 && codes11 == 1 && st->fOthers < 4)))
			st->fShape = kNotANumber;
	}
	if (sixes == 1 && digits == 2 && zeros == 1 && st->fOthers == 0 && st->fTops != 0)
		st->fShape = kNotANumber;
	bool zeroChunksBack = true;			// (to 0x002a35e0)
	if (nines == 1)
	{
		if (digits == 2)
		{
			if (zeros == 1)
			{
				if (st->fOthers == 0 && st->fBottoms != 0)
					st->fShape = kNotANumber;
			}
			else
				zeroChunksBack = false;		// (to 0x002a3598)
		}
		else
		{
			if (digits == 1 && st->fOthers == 0 && strokeCount == 2 && strokes[0].fHeight == sumHeight
			 && sumHeight > strokes[1].fHeight * 3 / 2
			 && strokes[0].fBottom - strokes[1].fBottom > sumHeight / 3)
				st->fShape = kNotANumber;
			zeroChunksBack = false;
		}
	}
	else
		zeroChunksBack = false;
	if (!zeroChunksBack && zeros > 4 && st->fDigits != 0)
	{
		st->fBroken -= (int32_t) zeros;
		st->fDigits += (int32_t) zeros;
		st->fDigitChunks += (int32_t) zeroChunks;
	}
	else
		st->fChunks -= (int32_t) zeroChunks;
	if (signs13 > 0)
		st->fSmall += 2;
}


// ROM 0x002a3ad8 (unnamed) - whether a chunk of stroke s crosses line c
// and the stroke lies more to the right of c's middle than to its left
static long
StrokeCrossesToRight(tag_CHUNK_STAFF* staff, tag_CHUNK* c, long s)
{
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* n = staff->fNodes;
	tag_STK* st = &staff->fStrokes[s];
	long mid = (c->fLeft + c->fRight) / 2;
	for (long k = st->fFirstChunk; k <= st->fLastChunk; k++)
	{
		if (CrossArcs(n, 1, c->fFrom, c->fTo, 2, chunks[k].fFrom, chunks[k].fTo)
		 && st->fRight - mid > mid - st->fLeft)
			return 1;
	}
	return 0;
}


// ROM 0x002a3898 (unnamed) - whether a line read as a 1 (or not read,
// f6C 0xff) that makes a stroke of its own - but for tiny hooks - is
// crossed by the stroke before or after it, or has the stroke after it
// lying across its top: a letter such as a t or f, not a number
static long
LineIsLetterStem(tag_CHUNK_STAFF* staff)
{
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_STK* strokes = staff->fStrokes;
	long strokeCount = staff->fStrokeCount;
	for (long i = 0; i < staff->fRealCount; i++)
	{
		tag_CHUNK* c = &chunks[real[i]];
		if (c->f74 != 300)
			continue;
		if (c->f6C != 0xff && c->f6C != 1)
			continue;
		long h = c->fHeight;
		long s = c->fStroke;
		tag_STK* st = &strokes[s];
		if (st->fHeight > h || st->fWidth > h)
			continue;
		tag_CHUNK* p = c->fPrev == -1 ? nil : &chunks[c->fPrev];
		tag_CHUNK* nx = c->fNext == -1 ? nil : &chunks[c->fNext];
		if (p != nil && (p->fPrev != -1 || p->fHeight > h / 6 || p->fWidth > h / 6))
			continue;
		if (nx != nil && (nx->fNext != -1 || nx->fHeight > h / 6 || nx->fWidth > h / 4))
			continue;
		if (s != 0)
		{
			long v = chunks[strokes[s - 1].fLastChunk].f6C;
			if ((v == 0xff || v == 1 || v == 0xd) && StrokeCrossesToRight(staff, c, s - 1))
				return 1;
		}
		if (s + 1 < strokeCount)
		{
			tag_CHUNK* n2 = &chunks[strokes[s + 1].fFirstChunk];
			long v = n2->f6C;
			if (v == 0xff || v == 1 || v == 0xd)
			{
				if (StrokeCrossesToRight(staff, c, s + 1))
					return 1;
				if (n2->fBottom < c->fTop && n2->fLeft < c->fX0 && n2->fRight > c->fX0)
					return 1;
			}
		}
	}
	return 0;
}


// ROM 0x002a3b8c (unnamed) - the verdict over the statistics: ==> 1 when
// the writing is a number
static long
DecideNumber(tag_CHUNK_STAFF* staff, NumberStats* st)
{
	long runs = 0;
	long mean = 0;
	if (staff->f54 != 0 && LineIsLetterStem(staff))
		return 0;
	long wd, wo;
	if (staff->fRealCount > 15 || st->fSignChunks != 0)
	{
		wd = 2;
		wo = 1;
	}
	else
	{
		wd = 3;
		wo = 2;
	}
	if (st->fDigits == 0)
		return 0;
	if (st->fDigits == 1 && st->fOthers == 0 && st->fBroken == 0)
		return 0;
	long marks = st->fSmall + st->fShape + st->fHeights + st->fBottoms + st->fTops + st->fOverlap;
	if (marks > 100)
		return 0;
	long moreDigits = st->fDigits - st->fFives;
	if (moreDigits > 4)
		runs = 2;
	else if (moreDigits > 3)
		runs = 1;
	if (st->fSignChunks != 0)
	{
		st->fDigitChunks += st->fSignChunks;
		st->fDigits++;
		st->fOtherChunks -= st->fSignChunks;
		st->fOthers--;
	}
	long rest = st->fChunks - st->fOtherChunks;
	long dc = st->fDigitChunks;
	long oc = st->fOtherChunks;
	long chars = st->fDigits + st->fOthers;
	if (chars != 0)
		mean = (st->fDigitChunks + st->fOtherChunks) * 10 / chars;
	long spare = rest - dc;
	if (mean != 0)
		spare = spare * 10 / mean;
	switch (marks)
	{
	case 0:
		if (runs != 0)
			return 1;
		return wd * dc > wo * rest;
	case 1:
		if (runs == 2)
			return 1;
		if (runs != 0 && spare < 3)
			return 1;
		return wd * dc > wo * rest;
	case 2:
		if (runs == 2)
			return 1;
		if (runs != 0 && spare < 2)
			return 1;
		if (spare == 0)
			return 1;
		// (and on as with three)
	case 3:
		return rest == dc && oc < dc;
	}
	return 0;
}


// ROM 0x002a28d0 SearchNumber__FP15tag_CHUNK_STAFF
long
SearchNumber(tag_CHUNK_STAFF* staff)
{
	NumberStats st = { };
	st.fChunks = staff->fRealCount;
	st.fJumps = staff->fChunkCount - staff->fRealCount;
	GatherNumberStats(staff, &st);
	return DecideNumber(staff, &st);
}
