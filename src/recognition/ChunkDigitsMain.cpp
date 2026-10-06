/*
	File:		ChunkDigitsMain.cpp

	Contains:	The digit reader's Digits: every searcher run over the
				chunks, the doubtful digits taken out again, the number cut
				into cells (CutNumberInDigits), the second looks, and the
				verdict on whether the writing is a number - with the
				statics that make it: a digit read inside or across
				another taken out, a 7 or 1 that is part of a letter,
				words that look like numbers ("good" is 9009), an area
				code in brackets, a lone digit written its usual way - and
				the characters and the runs of strokes that are not digits
				handed back.  See Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x0029c94c-0x0029ce20, 0x0029e888, 0x0029fbcc-0x002a01dc,
				0x002a09b0-0x002a28d0, 0x002a7168-0x002a7868); each
				function cites its origin.  All of it is from the
				disassembly.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRAbs, HWRMemoryAlloc, HWRMemoryFree
#include "host/RomBugs.h"
#include <string.h>


// How many objects a class of the list of low objects has, read straight
// out of the list's own record as the ROM does (the class's index: 16 is
// 1900, 18 is 2100 - ROM +0x184 and +0x1ac).
static inline long
ClassCount(void* lo, long index)
{
	return ((LOBlock*) lo)->fClasses[index].fCount;
}

const long	kClass1900 = 16;
const long	kClass2100 = 18;

// a digit object's code: its value less 1300, modulo 100, unsigned
static inline ULong
CodeOf(const tag_LOWOBJ* obj)
{
	return (uint32_t) (obj->fValue - 1300) % 100;
}

// 32-bit multiplication as the ARM does it, wrapping
static inline long
Mul32(long a, long b)
{
	return (int32_t) ((uint32_t) a * (uint32_t) b);
}


#pragma mark - the next live object

// ROM 0x002a0d14 (unnamed) - the next object of a class list, skipping
// those taken out (value 0xffff), from the list's own links; ==> 1 when it
// moved, *obj updated.  ROM QUIRK: it stops at the list's end on a taken-
// out object and answers that one.  (The same walk as SearchNumber's
// 0x002a3838.)
static long
NextLive(tag_LOWOBJ* objects, tag_LOWOBJ** obj)
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


#pragma mark - the doubtful digits

// ROM 0x002a0b38 (unnamed) - the digits (class 1300) lying inside digit a
// taken out - but for a code 99, and a ")" inside a 5 (which a 4 or 9
// whose start turns sharply back loses instead) - and of two digits that
// overlap in part, the shorter taken out when they read the same, both
// when they do not.  ==> 0.
static long
TakeOutOverlapping(void* lo, tag_LOWOBJ* a, tag_wapx_type* nodes)
{
	tag_LOWOBJ* o = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1300);
	for (long more = LO_PickFirst(lo, &o); more; more = LO_PickNext(lo, &o))
	{
		if (o->fValue == 0xffff || o == a)
			continue;
		long aFrom = a->fFrom;
		if (o->fFrom >= aFrom && o->fTo <= a->fTo && o->fValue != 1399)
		{
			if (o->fValue != 1611)
				o->fValue = 0xffff;
			else
			{
				ULong d = (uint32_t) a->fValue % 100;
				if (d == 5)
					o->fValue = 0xffff;
				else if ((d == 4 || d == 9) && nodes[aFrom].fDirOut < 14)
					a->fValue = 0xffff;
			}
		}
		long to = a->fTo < o->fTo ? a->fTo : o->fTo;
		long from = a->fFrom > o->fFrom ? a->fFrom : o->fFrom;
		long overlap = to - from;
		long aSpan = a->fTo - a->fFrom;
		long oSpan = o->fTo - o->fFrom;
		if (overlap <= 0)
			continue;
		if (overlap >= aSpan || overlap >= oSpan)
			continue;
		if ((uint32_t) o->fValue % 100 != (uint32_t) a->fValue % 100)
		{
			o->fValue = 0xffff;
			a->fValue = 0xffff;
		}
		else if (oSpan < aSpan)
			o->fValue = 0xffff;
		else
			a->fValue = 0xffff;
	}
	LO_SetWorkClass(lo, saved);
	return 0;
}


// ROM 0x002a1320 (unnamed) - whether the digit obj (code d: a 7, 1, 2, 81
// or a solidus) belongs with the strokes about it rather than being a
// digit - a 7 whose neighbouring stroke crosses its middle (put in again
// with that stroke as a crossed 7, extra 0x21 before or 0x22 after), a 1
// with a stroke across or over it, a stroke lying across the middle third
// of it or cutting its line - and if so taken out.  ==> 1 taken out.
static long
PartOfLetters(void* lo, tag_LOWOBJ* obj, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* strokes, long strokeCount, ULong d)
{
	long h = obj->fBottom - obj->fTop;
	long w = obj->fRight - obj->fLeft;
	long n = LO_HowManyChunks(lo, obj);
	long firstStroke = chunks[real[LO_GetRealChunkInd(lo, chunks, nodes, obj, 1)]].fStroke;
	if (d == 7)
	{
		long s = firstStroke - 1;
		if (s >= 0)
		{
			tag_STK* p = &strokes[s];
			long mid = (p->fBottom + p->fTop) / 2;
			if (mid < obj->fBottom - h / 4 && mid > obj->fTop + h / 4 && p->fWidth > (3 * p->fHeight) / 2)
			{
				long right = obj->fRight < p->fRight ? obj->fRight : p->fRight;
				long left = obj->fLeft > p->fLeft ? obj->fLeft : p->fLeft;
				long across = right - left;
				if (!(p->fWidth / 2 >= across && w / 2 >= across))
				{
					LO_Add(lo, nodes, 1300, chunks[p->fFirstChunk].fFrom, obj->fTo, (ULong) obj->fValue, 0x21);
					obj->fValue = 0xffff;
					return 1;
				}
			}
		}
	}
	else if (d == 1)
	{
		long s = firstStroke - 1;
		if (s >= 0)
		{
			tag_STK* p = &strokes[s];
			long right = obj->fRight < p->fRight ? obj->fRight : p->fRight;
			long left = obj->fLeft > p->fLeft ? obj->fLeft : p->fLeft;
			long across = right - left;
			if (p->fWidth / 2 < across || w / 2 < across
				|| (across == 0 && obj->fLeft > p->fLeft && obj->fLeft < p->fRight))
			{
				obj->fValue = 0xffff;
				return 1;
			}
		}
	}
	long lastStroke = chunks[real[LO_GetRealChunkInd(lo, chunks, nodes, obj, n)]].fStroke;
	if (d == 7)
	{
		long s = lastStroke + 1;
		if (s < strokeCount)
		{
			tag_STK* p = &strokes[s];
			long mid = (p->fBottom + p->fTop) / 2;
			if (mid < obj->fBottom - h / 4 && mid > obj->fTop + h / 4 && p->fWidth > (3 * p->fHeight) / 2)
			{
				long right = obj->fRight < p->fRight ? obj->fRight : p->fRight;
				long left = obj->fLeft > p->fLeft ? obj->fLeft : p->fLeft;
				long across = right - left;
				if (!(p->fWidth / 2 >= across && w / 2 >= across))
				{
					LO_Add(lo, nodes, 1300, obj->fFrom, chunks[p->fLastChunk].fTo, (ULong) obj->fValue, 0x22);
					obj->fValue = 0xffff;
					return 1;
				}
			}
		}
	}
	else if (d == 1)
	{
		long s = lastStroke + 1;
		if (s < strokeCount)
		{
			tag_STK* p = &strokes[s];
			long right = obj->fRight < p->fRight ? obj->fRight : p->fRight;
			long left = obj->fLeft > p->fLeft ? obj->fLeft : p->fLeft;
			long across = right - left;
			if (p->fWidth / 2 < across || w / 2 < across
				|| (across == 0 && obj->fLeft > p->fLeft && obj->fLeft < p->fRight))
			{
				obj->fValue = 0xffff;
				return 1;
			}
		}
	}
	// (a stroke of one chunk has no last stroke of its own: -1, which no
	// stroke is)
	long first = firstStroke;
	long last = n > 1 ? lastStroke : -1;
	if (d == 16 || d == 1 || d == 2 || d == 81)
	{
		for (long s = 0; s < strokeCount; s++)
		{
			if (s == first || s == last)
				continue;
			tag_STK* p = &strokes[s];
			if (d == 1 || d == 16 || d == 81)
			{
				if (obj->fLeft + w / 3 > p->fLeft && obj->fRight - w / 3 < p->fRight)
				{
					obj->fValue = 0xffff;
					return 1;
				}
			}
			else if (d == 2)
			{
				if (obj->fLeft + w / 3 > p->fLeft && obj->fRight < p->fRight)
				{
					obj->fValue = 0xffff;
					return 1;
				}
			}
		}
	}
	if (d == 16 || d == 1 || d == 81)
	{
		for (long s = 0; s < strokeCount; s++)
		{
			if (s == first || s == last)
				continue;
			tag_STK* p = &strokes[s];
			long right = obj->fRight < p->fRight ? obj->fRight : p->fRight;
			long left = obj->fLeft > p->fLeft ? obj->fLeft : p->fLeft;
			if (right - left <= 0)
				continue;
			for (long c = p->fFirstChunk; c <= p->fLastChunk; c++)
			{
				tag_CHUNK* cc = &chunks[c];
				long r2 = obj->fRight < cc->fRight ? obj->fRight : cc->fRight;
				long l2 = obj->fLeft > cc->fLeft ? obj->fLeft : cc->fLeft;
				if (r2 - l2 <= 0)
					continue;
				if (CheckQIntersec(nodes, obj->fFrom, obj->fTo, cc->fFrom, cc->fTo)
					|| CheckQIntersec(nodes, obj->fFrom, obj->fTo, cc->fTopNode, cc->fBottomNode))
				{
					obj->fValue = 0xffff;
					return 1;
				}
			}
		}
	}
	return 0;
}


// ROM 0x002a09b0 (unnamed) - every digit (class 1300) looked at again: a
// 7, 1, 2, 81 or solidus that is part of a letter taken out, a solidus S
// read too (code 16 over 1600), and the digits inside and across it taken
// out.  ==> 1.
static long
TakeOutDoubtful(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* strokes, long strokeCount)
{
	tag_LOWOBJ* objects = ((LOBlock*) lo)->fObjects;
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1300);
	if (LO_PickFirst(lo, &obj))
	{
		do
		{
			if (obj->fValue == 0xffff)
				continue;
			ULong d = CodeOf(obj);
			if ((d == 7 || d == 16 || d == 1 || d == 2 || d == 81)
				&& PartOfLetters(lo, obj, chunks, nodes, real, strokes, strokeCount, d))
				continue;
			ULong d2 = (uint32_t) (obj->fValue - 1600) % 100;
			if (d2 == 16 && PartOfLetters(lo, obj, chunks, nodes, real, strokes, strokeCount, d2))
				continue;
			TakeOutOverlapping(lo, obj, nodes);
		} while (NextLive(objects, &obj));
	}
	LO_SetWorkClass(lo, saved);
	return 1;
}


// ROM 0x002a2758 (unnamed) - the 3s SearchDigit_S read (value 1603) whose
// chunk is a three-bracket one starting between directions 3 and 10, and
// the dots (code 14) of one chunk wider than half as tall again, taken
// out.  ==> 0.
static long
TakeOutThreesAndDots(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, int32_t* real, tag_STK* /*strokes*/, long /*strokeCount*/)
{
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1300);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue == 0xffff)
			continue;
		ULong v = (uint32_t) obj->fValue;
		ULong hundreds = (v / 100) * 100;
		ULong d = v - hundreds;
		long n = LO_HowManyChunks(lo, obj);
		long ri = LO_GetRealChunkInd(lo, chunks, nodes, obj, 1);
		tag_CHUNK* c = &chunks[real[ri]];
		if (d == 3)
		{
			if (hundreds == 1600 && c->f74 == 1400)
			{
				long dir = nodes[c->fFrom].fDir;
				if (dir > 2 && dir < 11)
					obj->fValue = 0xffff;
			}
		}
		else if (d == 14 && n == 1)
		{
			if (c->fWidth > (3 * c->fHeight) / 2)
				obj->fValue = 0xffff;
		}
	}
	LO_SetWorkClass(lo, saved);
	return 0;
}


#pragma mark - the verdicts

// ROM 0x002a1e98 (unnamed) - whether the digit obj is written its usual
// way: a > < pound or yen sign always, a 2, 3, 6, 7 (one chunk), 8 or 9 in
// one stroke (the 2 and 3 of at most three chunks), a 0, 1, 4 or 5 in two
// strokes one after the other.  ==> 1 so.
static long
UsualWay(tag_CHUNK_STAFF* staff, tag_LOWOBJ* obj)
{
	void* lo = staff->fLO;
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	ULong d = CodeOf(obj);
	if (d == 23 || d == 24 || d == 70 || d == 72)
		return 1;
	if (d > 9)
		return 0;
	long n = LO_HowManyChunks(lo, obj);
	long s0 = chunks[real[LO_GetRealChunkInd(lo, chunks, nodes, obj, 1)]].fStroke;
	long s1 = chunks[real[LO_GetRealChunkInd(lo, chunks, nodes, obj, n)]].fStroke;
	long strokes;
	if (s0 + 1 == s1)
		strokes = 2;
	else if (s1 != s0)
		strokes = 3;
	else
		strokes = 1;
	switch (d)
	{
	case 0: case 1: case 4: case 5:
		return strokes == 2 ? 1 : 0;
	case 2: case 3:
		return strokes == 1 && n <= 3 ? 1 : 0;
	case 6: case 8: case 9:
		return strokes == 1 ? 1 : 0;
	case 7:
		return strokes == 1 && n == 1 ? 1 : 0;
	}
	return 0;
}


// ROM 0x0029ccd4 (unnamed) - the verdict with staff f50 set: a lone digit
// (and nothing that is not one) written its usual way; otherwise every
// digit not written its usual way taken out and SearchNumber asked if any
// is left.  ==> 1 a number.
static long
UsualDigitsOnly(tag_CHUNK_STAFF* staff)
{
	void* lo = staff->fLO;
	tag_LOWOBJ* obj = nil;
	long answer = 0;
	long usual = 0;
	if (ClassCount(lo, kClass2100) == 0 && ClassCount(lo, kClass1900) == 1)
	{
		ULong saved = LO_GetWorkClassID(lo);
		LO_SetWorkClass(lo, 1900);
		if (LO_PickFirst(lo, &obj))
			answer = UsualWay(staff, obj);
		LO_SetWorkClass(lo, saved);
		return answer;
	}
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (CodeOf(obj) > 9)
			continue;
		if (UsualWay(staff, obj) == 0)
			obj->fValue = 0xffff;
		else
			usual++;
	}
	LO_SetWorkClass(lo, saved);
	if (usual != 0)
		answer = SearchNumber(staff);
	return answer;
}


// ROM 0x002a1a98 (unnamed) - whether the writing is a number in brackets,
// "(" digits ")": at most five objects, all digits or brackets, one "("
// first and one ")" last, three or more in all, the strokes no more than
// four beyond the digits', and the brackets together a half to a fifth of
// the width.  ROM BUG (fixed): it counts the brackets from the entry after
// the one it has just filled, which is whatever the stack held there -
// DEVIATION: the host's entries start at nought, so the counts stay
// nought.  The fix counts them from the entry just filled, so a second
// "(" or ")" rules the writing out as the test after the loop means to.
static long
InBrackets(tag_CHUNK_STAFF* staff)
{
	struct Entry { UByte fChar; UByte f01[3]; int32_t fLeft, fTop, fRight, fBottom; };
	Entry entries[6];
	memset(entries, 0, sizeof(entries));
	void* lo = staff->fLO;
	tag_wapx_type* nodes = staff->fNodes;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	tag_LOWOBJ* obj = nil;
	long ok = 1;
	long count = 0, opens = 0, closes = 0, strokes = 0;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue == 0xffff)
			continue;
		ULong d = CodeOf(obj);
		UByte c;
		// (the rest of the usual chain of characters follows in the ROM,
		// unreachable: anything but a digit or a bracket has ended it)
		if (d <= 9)
			c = (UByte) (d + '0');
		else if (d == 10)
			c = '(';
		else if (d == 11)
			c = ')';
		else
		{
			ok = 0;
			break;
		}
		entries[count].fChar = c;
		entries[count].fLeft = obj->fLeft;
		entries[count].fTop = obj->fTop;
		entries[count].fRight = obj->fRight;
		entries[count].fBottom = obj->fBottom;
		count++;
		if (count > 5)
		{
			ok = 0;
			break;
		}
		UByte counted = RomBugFixed() ? entries[count - 1].fChar : entries[count].fChar;
		if (counted == '(')
			opens++;
		else if (counted == ')')
			closes++;
		long n = LO_HowManyChunks(lo, obj);
		if (n == 0)
			continue;
		long s0 = chunks[real[LO_GetRealChunkInd(lo, chunks, nodes, obj, 1)]].fStroke;
		long s1 = chunks[real[LO_GetRealChunkInd(lo, chunks, nodes, obj, n)]].fStroke;
		strokes += s1 - s0 + 1;
	}
	LO_SetWorkClass(lo, saved);
	if (staff->fStrokeCount > strokes + 4 || ok == 0)
		return 0;
	if (opens > 1 || closes > 1)
		return 0;
	if (entries[0].fChar != '(')
		return 0;
	if (entries[count - 1].fChar != ')')
		return 0;
	if (count <= 2)
		return 0;
	long brackets = (entries[0].fRight - entries[0].fLeft) + (entries[count - 1].fRight - entries[count - 1].fLeft);
	if (brackets <= 0)
		return 0;
	long ratio = (entries[count - 1].fRight - entries[0].fLeft) / brackets;
	if (ratio < 2 || ratio > 5)
		return 0;
	return 1;
}


// ROM 0x0029e888 (unnamed) - whether the writing is a lone # (and nothing
// that is not a digit) taking all the chunks.  ==> 1 so.
static long
LoneHash(tag_CHUNK_STAFF* staff)
{
	void* lo = staff->fLO;
	tag_LOWOBJ* obj = nil;
	long answer = 0;
	if (!(ClassCount(lo, kClass2100) == 0 && ClassCount(lo, kClass1900) == 1))
		return 0;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	LO_PickFirst(lo, &obj);
	if (obj != nil && staff->fRealCount == LO_HowManyChunks(lo, obj))
		if (DigitChar(CodeOf(obj)) == '#')
			answer = 1;
	LO_SetWorkClass(lo, saved);
	return answer;
}


// ROM 0x002a19ec (unnamed) - whether the writing is a lone digit (and
// nothing that is not a digit) taking all the chunks and written its usual
// way.  ==> 4 so, else 0.
static long
LoneUsualDigit(tag_CHUNK_STAFF* staff)
{
	void* lo = staff->fLO;
	tag_LOWOBJ* obj = nil;
	long answer = 0;
	if (!(ClassCount(lo, kClass2100) == 0 && ClassCount(lo, kClass1900) == 1))
		return 0;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	LO_PickFirst(lo, &obj);
	if (obj != nil && staff->fRealCount == LO_HowManyChunks(lo, obj) && UsualWay(staff, obj))
		answer = 4;
	LO_SetWorkClass(lo, saved);
	return answer;
}


// ROM 0x002a0d74 (unnamed) - whether the digits written out (up to four)
// spell a word the digit reader is known to take for a number: "15" with
// the 5 in one stroke (is), 7. 7- .7 (?), 9// 91/ 9/1 (a tall 1 after), 995
// (gas, with one run of other strokes), 7.- 7-. 7-- , 9004 and 9009 (good)
// or 900 (goo) with other strokes about - or two full stops one after the
// other.  ROM QUIRK: with more than four, and with the two full stops, it
// answers at once and leaves the list's work class unrestored.  ==> 1 not
// a number.
static long
LooksLikeAWord(tag_CHUNK_STAFF* staff)
{
	void* lo = staff->fLO;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t* real = staff->fRealChunks;
	UByte c[4] = { 0, 0, 0, 0 };
	int32_t heights[4] = { 0, 0, 0, 0 };
	long n = 0;
	long fiveInOne = 0;
	long afterDot = 0;
	long dotChunk = -1;
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue == 0xffff)
			continue;
		n++;
		if (n > 4)
			return 0;
		UByte ch = DigitChar(CodeOf(obj));
		long k = LO_HowManyChunks(lo, obj);
		long first = real[LO_GetRealChunkInd(lo, chunks, staff->fNodes, obj, 1)];
		long last = real[LO_GetRealChunkInd(lo, chunks, staff->fNodes, obj, k)];
		long oneStroke = chunks[first].fStroke == chunks[last].fStroke ? 1 : 0;
		if (ch == '5')
		{
			if (oneStroke)
				fiveInOne = 1;
		}
		if (ch != '.')
		{
			if (afterDot)
			{
				afterDot = 0;
				dotChunk = -1;
			}
		}
		else if (!afterDot)
		{
			afterDot = 1;
			dotChunk = LO_GetRealChunkInd(lo, chunks, staff->fNodes, obj, 1);
		}
		else if (dotChunk + 1 == LO_GetRealChunkInd(lo, chunks, staff->fNodes, obj, 1))
			return 1;
		c[n - 1] = ch;
		heights[n - 1] = obj->fBottom - obj->fTop;
	}
	LO_SetWorkClass(lo, saved);
	long others = ClassCount(lo, kClass2100);
	if (others == 0 && n == 2)
	{
		if (c[0] == '1')
			return c[1] == '5' && fiveInOne ? 1 : 0;
	}
	else if (others == 0 && n == 3)
	{
		if (c[0] == '9')
		{
			if (c[1] == '/')
			{
				if (c[2] == '/')
					return 1;
			}
			else if (c[1] == '1')
				return c[2] == '/' ? 1 : 0;
			else
				return 0;
			// 9/ and then a 1 taller than the solidus
			if (c[2] != '1')
				return 0;
			return 3 * heights[2] > 2 * heights[1] ? 1 : 0;
		}
		goto seven3;
	}
	if (n == 2)
	{
		if (c[0] == '7')
		{
			if (c[1] == '.')
				return 1;
			if (c[1] == '-')
				return 1;
		}
		else if (c[0] == '.' && c[1] == '7')
			return 1;
	}
	if (others == 1 && n == 3)
	{
		if (fiveInOne && c[0] == '9')
		{
			if (c[1] == '9')
				return c[2] == '5' ? 1 : 0;
			goto zeros;
		}
	}
	else if (n != 3)
		goto last;
seven3:
	if (c[0] == '7')
	{
		if (c[1] == '.')
		{
			if (c[2] == '-')
				return 1;
		}
		else if (c[1] == '-')
		{
			if (c[2] == '.')
				return 1;
			if (c[2] == '-')
				return 1;
		}
	}
last:
	if (others == 0)
	{
		if (n == 4 && c[0] == '9' && c[1] == '0' && c[2] == '0' && (c[3] == '9' || c[3] == '4'))
			return 1;
		return 0;
	}
	if (!(n == 3 && c[0] == '9'))
		return 0;
zeros:
	return c[1] == '0' && c[2] == '0' ? 1 : 0;
}


// ROM 0x0029ffc8 (unnamed) - whether a colon is among the digits written
// out.  ==> 1 so.  ROM QUIRK: found, it answers at once and leaves the
// list's work class unrestored.
static long
HasColon(void* lo)
{
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue == 0xffff)
			continue;
		if (DigitChar(CodeOf(obj)) == ':')
			return 1;
	}
	LO_SetWorkClass(lo, saved);
	return 0;
}


#pragma mark - handing it back

// ROM 0x002a2078 (unnamed) - the digits written out (class 1900) into
// numbox, a character, box and trace points each (a 1 carrying its height
// when a bar ran through it, extra 0x1a or 0x6f, a solidus always; an 8 in
// one stroke that starts going up and backwards or is short made '&' as
// its alternative, staff f58 set - only with flags bit 4), up to max - 1 of
// them; with the writing a number, the grey digits (class 2200) put in
// among them in their places left to right and the list ended with a 0.
// ==> how many.  ROM QUIRK: a code of 400 or more keeps the character
// before it - DEVIATION: at the first object that is whatever the register
// held, nought on the host.
static long
WriteNumBoxes(tag_CHUNK_STAFF* staff, tagNumBox* box, long flags, long max)
{
	void* lo = staff->fLO;
	int32_t* real = staff->fRealChunks;
	tag_CHUNK* chunks = staff->fChunks;
	tag_LOWOBJ* obj = nil;
	long count = 0;
	UByte ch = 0;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	long more = LO_PickFirst(lo, &obj);
	long room = max - 1;
	long eights = flags & 4;
	for (; more; more = LO_PickNext(lo, &obj))
	{
		if (room == count)
			break;
		long v = obj->fValue;
		if (v == 0xffff)
			continue;
		ULong code = (uint32_t) (v - 1300);
		ULong q = code / 100;
		if (q <= 3)
		{
			ULong d = code - q * 100;
			if (d <= 99)
				ch = DigitChar(d);
		}
		tagNumBox* b = &box[count];
		b->fChar = ch;
		b->fLeft = obj->fLeft;
		b->fTop = obj->fTop;
		b->fRight = obj->fRight;
		b->fBottom = obj->fBottom;
		b->fFirstPoint[1] = (UByte) obj->fFirstPoint;
		b->fFirstPoint[0] = (UByte) (obj->fFirstPoint >> 8);
		b->fLastPoint[1] = (UByte) obj->fLastPoint;
		b->fLastPoint[0] = (UByte) (obj->fLastPoint >> 8);
		if ((ch == '1' && (obj->fExtra == 0x1a || obj->fExtra == 0x6f)) || ch == '/')
		{
			long h = obj->fBottom - obj->fTop;
			b->fHeight[1] = (UByte) h;
			b->fHeight[0] = (UByte) (h >> 8);
		}
		else if (ch == '8' && eights != 0)
		{
			long n = LO_HowManyChunks(lo, obj);
			long first = real[LO_GetRealChunkInd(lo, chunks, staff->fNodes, obj, 1)];
			long last = real[LO_GetRealChunkInd(lo, chunks, staff->fNodes, obj, n)];
			tag_CHUNK* c = &chunks[first];
			if (c->fStroke == chunks[last].fStroke && c->fKind == 1
				&& (c->fX0 > c->fX1 || obj->fBottom - obj->fTop < 3 * c->fHeight))
			{
				b->fAlt = '&';
				staff->f58 = 1;
			}
		}
		count++;
	}
	LO_SetWorkClass(lo, saved);
	if (flags == 0)
		return count;
	saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 2200);
	for (more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (room == count)
			break;
		if (obj->fValue == 0xffff)
			continue;
		UByte c = DigitChar(CodeOf(obj));
		long left = obj->fLeft;
		long at;
		if (count == 0 || left < box[0].fLeft)
			at = 0;
		else if (left > box[count - 1].fLeft)
			at = count;
		else
		{
			at = -1;
			for (long k = 1; k < count; k++)
				if (left > box[k - 1].fLeft && left < box[k].fLeft)
				{
					at = k;
					break;
				}
			if (at < 0)
				continue;		// (on a digit's own left edge: not put in)
		}
		// everything from at on moved up one (the slot past the end too);
		// the new entry's halfwords are left as the moved one's were
		for (long k = count; k >= at; k--)
			box[k + 1] = box[k];
		box[at].fChar = c;
		box[at].fLeft = obj->fLeft;
		box[at].fTop = obj->fTop;
		box[at].fRight = obj->fRight;
		box[at].fBottom = obj->fBottom;
		count++;
	}
	LO_SetWorkClass(lo, saved);
	box[count].fChar = 0;
	return count;
}


// ROM 0x0029fbcc (unnamed) - each stroke marked in used[] when a digit
// written out (class 1900), or with the writing a number a grey digit
// (class 2200), has a chunk in it; each stroke used put in as class 2000
// (value the stroke, extra 4) and each run of strokes not used as class
// 2100 (value its number, extra 5, or 6 for the last).  ==> 0.
static long
StrokesAndRuns(void* lo, tag_CHUNK* chunks, tag_wapx_type* nodes, tag_STK* strokes, long strokeCount, int32_t* real, UByte* used, long isNumber)
{
	tag_LOWOBJ* obj = nil;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 1900);
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
	{
		if (obj->fValue == 0xffff)
			continue;
		long n = LO_HowManyChunks(lo, obj);
		for (long j = 1; j <= n; j++)
		{
			long s = chunks[real[LO_GetRealChunkInd(lo, chunks, nodes, obj, j)]].fStroke;
			if (used[s] == 0)
				used[s] = 1;
		}
	}
	LO_SetWorkClass(lo, saved);
	if (isNumber != 0)
	{
		ULong saved2 = LO_GetWorkClassID(lo);
		LO_SetWorkClass(lo, 2200);
		for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
		{
			if (obj->fValue == 0xffff)
				continue;
			long n = LO_HowManyChunks(lo, obj);
			for (long j = 1; j <= n; j++)
			{
				long s = chunks[real[LO_GetRealChunkInd(lo, chunks, nodes, obj, j)]].fStroke;
				if (used[s] == 0)
					used[s] = 1;
			}
		}
		LO_SetWorkClass(lo, saved2);
	}
	for (long s = 0; s < strokeCount; s++)
		if (used[s] != 0)
			LO_Add(lo, nodes, 2000, chunks[strokes[s].fFirstChunk].fFrom, chunks[strokes[s].fLastChunk].fTo, (ULong) s, 4);
	long open = 0, runs = 0;
	long from = 0, to = 0;
	for (long s = 0; s < strokeCount; s++)
	{
		if (s == 0)
		{
			if (used[0] == 0)
			{
				open = 1;
				from = chunks[strokes[0].fFirstChunk].fFrom;
				to = chunks[strokes[0].fLastChunk].fTo;
			}
		}
		else
		{
			if (used[s - 1] != 0 && used[s] == 0)
			{
				if (open)
					LO_Add(lo, nodes, 2100, from, to, (ULong) runs, 5);
				open = 1;
				from = chunks[strokes[s].fFirstChunk].fFrom;
				runs++;
				to = chunks[strokes[s].fLastChunk].fTo;
			}
			if (used[s - 1] == 0 && used[s] == 0)
				to = chunks[strokes[s].fLastChunk].fTo;
		}
		if (s == strokeCount - 1 && open)
		{
			LO_Add(lo, nodes, 2100, from, to, (ULong) runs, 6);
			runs++;
		}
	}
	return 0;
}


#pragma mark - Digits

// ROM 0x0029c94c Digits__FP15tag_CHUNK_STAFF7tag_BOXiP9tagNumBoxPPvPi
long
Digits(tag_CHUNK_STAFF* staff, tag_BOX box, long height, tagNumBox* numbox, int32_t** out, int32_t* count)
{
	void* lo = staff->fLO;
	tag_CHUNK* chunks = staff->fChunks;
	tag_wapx_type* nodes = staff->fNodes;
	tag_STK* strokes = staff->fStrokes;
	long strokeCount = staff->fStrokeCount;
	int32_t* real = staff->fRealChunks;
	DefHeightsForNumber(staff);
	ChunkPutClassesToLO(lo, nodes, chunks, staff->fChunkCount);
	GetCircles(staff);
	SearchDigit_L(staff);
	SearchDigit_K(staff);
	New_SearchDigit_V(lo, staff->fTrace, staff->fTraceCount, nodes, chunks, staff->fBrackets, real,
					  staff->fChunkCount, staff->fRealCount, box, strokes, strokeCount, height);
	SearchDigit_S(staff);
	FindPound(staff);
	Check_4(staff);
	TakeOutDoubtful(lo, chunks, nodes, real, strokes, strokeCount);
	TakeOutThreesAndDots(lo, chunks, nodes, real, strokes, strokeCount);
	CutNumberInDigits(staff);
	DigitsSecondLooks(lo, chunks, real, strokes, strokeCount, nodes, staff->fDigits, box);
	long answer;
	if (staff->f50 != 0)
		answer = UsualDigitsOnly(staff);
	else
	{
		answer = InBrackets(staff);
		if (answer == 0)
			answer = LoneHash(staff);
		if (answer == 0)
			answer = LoneUsualDigit(staff);
		if (answer == 0)
			answer = SearchNumber(staff);
	}
	WriteNumBoxes(staff, numbox, answer, staff->f5C);
	UByte* used = (UByte*) HWRMemoryAlloc(strokeCount);
	if (used == nil)
		return 0;
	memset(used, 0, strokeCount);
	StrokesAndRuns(lo, chunks, nodes, strokes, strokeCount, real, used, answer);
	HWRMemoryFree((Ptr) used);
	if (answer != 0)
		answer = LooksLikeAWord(staff) ? 0 : 1;
	ULong saved = LO_GetWorkClassID(lo);
	LO_SetWorkClass(lo, 2100);
	tag_LOWOBJ* obj = nil;
	long runs = 0;
	for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
		runs++;
	*count = 0;
	*out = nil;
	if (runs != 0)
	{
		int32_t* pairs = (int32_t*) HWRMemoryAlloc(runs * 8);
		if (pairs == nil)
			answer = 0;
		else
		{
			long k = 0;
			for (long more = LO_PickFirst(lo, &obj); more; more = LO_PickNext(lo, &obj))
			{
				pairs[2 * k] = obj->fFirstPoint;
				pairs[2 * k + 1] = obj->fLastPoint;
				k++;
			}
			*count = (int32_t) runs;
			*out = pairs;
		}
	}
	LO_SetWorkClass(lo, saved);
	if (answer != 0)
	{
		if (HasColon(lo))
			answer = 0;
		else if (answer != 0 && ClassCount(lo, kClass2100) == 0)
			answer |= 2;
	}
	return answer;
}


#pragma mark - the cells

// ROM 0x002a7168 (unnamed) - whether stroke group k (groups: first and last
// real chunk and the box, six words each) joins the cell so far (its box
// the cell's): it overlaps the cell by more than two fifths of its own
// width; or, one chunk flatter than wide, starts near where the stroke
// before began; or, tall and narrow and reaching below the stroke before,
// stands over its end (the third of three only when the three steps
// down); or, two chunks and tall and narrow, likewise; or, the second
// group, the first being a short line at the top of the writing, begins
// close to it.  ==> 1 it joins.
static long
JoinsCell(long k, tag_BOX cell, tag_CHUNK* chunks, int32_t* real, int32_t* line, int32_t (*groups)[6])
{
	int32_t* g = groups[k];
	long w = g[4] - g[2];
	if (cell.right - g[2] > (2 * w) / 5)
		return 1;
	if (g[0] == g[1] && k > 0)
	{
		long h = g[5] - g[3];
		if (h < w)
		{
			long diag = Mul32(h, h) + Mul32(w, w);
			tag_CHUNK* c = &chunks[real[g[0]]];
			tag_CHUNK* p = &chunks[real[groups[k - 1][1]]];
			bool back = c->fX0 >= c->fX1;
			long dx = (back ? c->fX1 : c->fX0) - p->fX0;
			long dy = (back ? c->fY1 : c->fY0) - p->fY0;
			if (Mul32(dy, dy) + Mul32(dx, dx) < diag / 4)
				return 1;
		}
		if (h > 2 * w && h > line[0] / 2)
		{
			tag_CHUNK* c = &chunks[real[g[0]]];
			tag_CHUNK* p = &chunks[real[groups[k - 1][1]]];
			if (c->fBottom - p->fBottom > h / 4
				&& HWRAbs((c->fRight + c->fLeft) / 2 - p->fX1) < h / 3)
			{
				if (k - 1 != 1)
					return 1;
				if (HWRAbs(groups[0][5] - groups[2][5]) >= line[0] / 3)
					return 1;
				if (groups[0][5] <= groups[1][5])
					return 1;
				long end = chunks[real[g[1]]].fX1;
				if (groups[0][2] >= end || groups[0][4] >= end)
					return 1;
			}
		}
	}
	if (g[1] - g[0] == 1 && k > 0)
	{
		long h = g[5] - g[3];
		long gw = g[4] - g[2];
		if (h > 2 * gw && h > line[0] / 2)
		{
			tag_CHUNK* p = &chunks[real[groups[k - 1][1]]];
			if (g[5] - p->fBottom > h / 4
				&& HWRAbs((g[4] + g[2]) / 2 - p->fX1) < h / 3)
				return 1;
		}
	}
	if (k - 1 != 0)
		return 0;
	int32_t* g0 = groups[0];
	if (g0[1] != g0[0])
		return 0;
	if (chunks[real[g0[1]]].f78 != 301)
		return 0;
	if (g0[5] - g0[3] >= line[0] / 2)
		return 0;
	if (HWRAbs(g0[3] - line[1]) >= line[0] / 4)
		return 0;
	long h0 = g0[5] - g0[3];
	long w0 = g0[4] - g0[2];
	if (h0 <= w0)
		return 0;
	tag_CHUNK* next = &chunks[real[groups[1][0]]];
	if (HWRAbs(g0[4] - next->fX0) >= w0)
		return 0;
	tag_CHUNK* c = &chunks[real[g0[1]]];
	long dx = c->fX0 - next->fX0;
	long dy = c->fY0 - next->fY0;
	if (Mul32(dy, dy) + Mul32(dx, dx) < (Mul32(h0, h0) + Mul32(w0, w0)) / 4)
		return 1;
	return 0;
}


// ROM 0x002a75b0 CutNumberInDigits__FP15tag_CHUNK_STAFF
long
CutNumberInDigits(tag_CHUNK_STAFF* staff)
{
	void* lo = staff->fLO;
	tag_wapx_type* nodes = staff->fNodes;
	int32_t* real = staff->fRealChunks;
	long realCount = staff->fRealCount;
	tag_CHUNK* chunks = staff->fChunks;
	int32_t groups[3][6];
	long cells = 0;
	long last = -1;
	if (realCount - 1 == -1)
		return 0;
	int32_t* line = &staff->fHeight;		// the staff's height and top line, in that order
	do
	{
		long start = last + 1;
		long r = start;
		long joined = 0;
		tag_CHUNK* c = &chunks[real[r]];
		tag_BOX cell = { c->fLeft, c->fTop, c->fRight, c->fBottom };
		long ok = 1;
		for (long k = 0; ; k++)
		{
			int32_t* g = groups[k];
			g[0] = g[1] = (int32_t) r;
			tag_CHUNK* cr = &chunks[real[r]];
			g[2] = cr->fLeft;
			g[3] = cr->fTop;
			g[4] = cr->fRight;
			g[5] = cr->fBottom;
			for (r = r + 1; r < realCount; r++)
			{
				tag_CHUNK* cn = &chunks[real[r]];
				if (cn->fPrev == -1)
					break;
				g[1] = (int32_t) r;
				if (cn->fTop < g[3]) g[3] = cn->fTop;
				if (cn->fBottom > g[5]) g[5] = cn->fBottom;
				if (cn->fLeft < g[2]) g[2] = cn->fLeft;
				if (cn->fRight > g[4]) g[4] = cn->fRight;
			}
			if (k > 0)
				ok = JoinsCell(k, cell, chunks, real, line, groups);
			if (ok == 0)
				break;
			if (cell.right < g[4]) cell.right = g[4];
			if (g[2] < cell.left) cell.left = g[2];
			if (cell.top > g[3]) cell.top = g[3];
			if (cell.bottom < g[5]) cell.bottom = g[5];
			joined++;
			if (joined == 3 || realCount == r)
				break;
			if (k + 1 >= 3)
				break;
		}
		last = groups[joined - 1][1];
		LO_Add(lo, nodes, 1200, chunks[real[start]].fFrom, chunks[real[last]].fTo, 0, -1);
		cells++;
	} while (realCount - 1 != last);
	return cells;
}
