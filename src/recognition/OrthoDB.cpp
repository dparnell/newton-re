/*
	File:		OrthoDB.cpp

	Contains:	The orthographic learning's letter-shape database: a
				letter's trace turned into a sample of fourteen bytes, the
				search for the samples near it, and the adding.  See
				Ortho.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM (from the
				disassembly throughout).

	The arithmetic is the ARM's: 32-bit products and sums that wrap
	(Mul32), shifts right that keep the sign, and __rt_sdiv's division
	that truncates towards nought.
*/

#include "Ortho.h"
#include "ParaGraph.h"
#include "WordSegment.h"
#include "ByteOrder.h"
#include "host/RomBugs.h"
#include <string.h>

extern const int	_2C16[8];

static inline int32_t	Mul32(int32_t a, int32_t b)	{ return (int32_t) ((uint32_t) a * (uint32_t) b); }
static inline int32_t	Add32(int32_t a, int32_t b)	{ return (int32_t) ((uint32_t) a + (uint32_t) b); }
static inline int32_t	Sub32(int32_t a, int32_t b)	{ return (int32_t) ((uint32_t) a - (uint32_t) b); }
static inline int32_t	Shl32(int32_t a, int s)		{ return (int32_t) ((uint32_t) a << s); }

// __rt_sdiv: dividend / divisor, truncated towards nought.
// DEVIATION: a divisor of nought throws evt.ex.div0 in the ROM; the host
// answers nought (it happens only for a sample whose coefficients are all
// nought, or two answers at no distance at all)
static inline int32_t
SDiv(int32_t dividend, int32_t divisor)
{
	if (divisor == 0)
		return 0;
	if (divisor == -1)
		return Sub32(0, dividend);
	return dividend / divisor;
}

// a multiply by a constant the ROM writes as shifts and adds: the product
// shifted right by 8, plus the product by the fraction shifted right by 16
static inline int32_t
KMul(int32_t x, int32_t whole, int32_t fraction)
{
	return Add32(Mul32(x, whole) >> 8, Mul32(x, fraction) >> 16);
}

// the database's big-endian fields
static inline ULong		DBWord(const void* db, long at)			{ return GetBigEndianWord((const UByte*) db + at); }
static inline void		DBSetWord(void* db, long at, ULong v)	{ PutBigEndianWord((UByte*) db + at, (unsigned int) v); }
static inline ULong		DBHalf(const void* p, long at)			{ return GetBigEndianHalf((const UByte*) p + at); }
static inline void		DBSetHalf(void* p, long at, ULong v)	{ PutBigEndianHalf((UByte*) p + at, (unsigned short) v); }


/*------------------------------------------------------------------------------
	A   l e t t e r ' s   t r a c e   a s   a   s a m p l e
------------------------------------------------------------------------------*/

// ROM 0x0012d178 TraceToOdata__FP6_ODATAP6_POINTPi
// A letter's points (strokes ended with a pen-up, the list with an empty
// stroke, a pen-up first) into the approximation's units: each point
// shifted to the middle of the letter's box and scaled so that the
// larger side is 32 * 1024 across, the same point twice running left out,
// with the step from the point before, its length and how far along.
// ==> the points (the strokes in *strokes), 0 for a letter that is a
// point, less than four across, or more than 128 points long.
long
TraceToOdata(ODATA* odata, const PS_point_type* points, long* strokes)
{
	long nStrokes = 0;
	int32_t maxX = -0x7fff, maxY = -0x7fff;
	int32_t minX = 0x7fff, minY = 0x7fff;
	const PS_point_type* p = points + 1;
	const PS_point_type* start = p;
	if (p->y != -1)
	{
		do
		{
			while (p->y != -1)
			{
				if (p->x > maxX)
					maxX = p->x;
				if (p->x < minX)
					minX = p->x;
				if (p->y > maxY)
					maxY = p->y;
				if (p->y < minY)
					minY = p->y;
				p++;
			}
			nStrokes++;
			p++;
		} while (p->y != -1);
	}
	if (maxX == minX && maxY == minY)
		return 0;
	int32_t midX = Shl32(minX + maxX, 9);
	int32_t midY = Shl32(minY + maxY, 9);
	int32_t width = maxX - minX;
	int32_t height = maxY - minY;
	long n = 0;
	int32_t lastX = -0x7fff, lastY = -0x7fff;
	p = start;
	if (p->y != -1)
	{
		do
		{
			while (p->y != -1)
			{
				int32_t x = p->x;
				if (x == lastX && p->y == lastY)
				{
					p++;
					continue;
				}
				lastX = x;
				lastY = p->y;
				odata[n].x = Shl32(x, 10);
				odata[n].y = Shl32(lastY, 10);
				p++;
				if (++n == 0x80)
					return 0;
			}
			p++;
		} while (p->y != -1);
	}
	int32_t side = (width > height) ? width : height;
	if (side < 4)
		return 0;
	for (long i = 0; i < n; i++)
	{
		odata[i].x = SDiv(Shl32(Sub32(odata[i].x, midX), 5), side);
		odata[i].y = SDiv(Shl32(Sub32(odata[i].y, midY), 5), side);
	}
	for (long i = 0; i < n; i++)
	{
		if (i == 0)
		{
			odata[0].dx = 0;
			odata[0].dy = 0;
			odata[0].len = 0;
			odata[0].arc = 0;
			continue;
		}
		int32_t dx = Sub32(odata[i].x, odata[i - 1].x);
		odata[i].dx = dx;
		int32_t dy = Sub32(odata[i].y, odata[i - 1].y);
		odata[i].dy = dy;
		if (dx < 0)
			dx = -dx;
		if (dy < 0)
			dy = -dy;
		int32_t len = (int32_t) SQRT32_ORTO((ULong) (uint32_t) Add32(Mul32(dy, dy), Mul32(dx, dx)));
		odata[i].len = len;
		odata[i].arc = Add32(odata[i - 1].arc, len);
	}
	*strokes = nStrokes;
	return n;
}


// ROM 0x0012d400 NormCdata__FPl
// The fourteen coefficients scaled to a length of 1024.
void
NormCdata(int32_t* coef)
{
	int32_t sum = 0;
	for (long i = 0; i < 14; i++)
		sum = Add32(Mul32(coef[i], coef[i]), sum);
	int32_t length = (int32_t) SQRT32_ORTO((ULong) (uint32_t) sum);
	for (long i = 0; i < 14; i++)
		coef[i] = SDiv(Shl32(coef[i], 10), length);
}


// ROM 0x0012d460 TrainTrajectory__FP6_POINTPvUs
// A letter's points learnt: made a sample, looked up, and added when
// Occam says it is worth it.  ==> 1, 0 when it was not added (or could
// not be looked at).
long
TrainTrajectory(const PS_point_type* points, void* db, UShort sym)
{
	NWTSAMPLE sample;
	long result = 1;
	if (FillNwtSample(points, sample) == 0)
		return 0;
	ALIST* list = CreateAlist(0x100);
	if (list == nil)
		return 0;
	ClearAlist(list);
	SearchInDataBase(list, sample, db, nil);
	if (Occam(sym, list) == 0 || AddToDataBase(db, sample, sym) == 0)
		result = 0;
	DestroyAlist(list);
	return result;
}


// ROM 0x0012d508 RjctAppr__FiP6_ODATAP7_ARDATAPlT1
// The letter resampled at sixteen points evenly along it and each
// coordinate put through the sixteen-point DCT; with more than one
// iteration each pass but the last smooths the points by keeping only
// the low half of the coefficients (the constant a sixteenth, the rest an
// eighth) and averaging with the transform back, and measures the
// smoothed curve again for the next pass.  The answer is coefficients one
// to seven of x and of y (each a sixteenth), interleaved and normalised.
void
RjctAppr(long count, ODATA* odata, ARDATA* ardata, int32_t* coef, long iterations)
{
	int32_t cx[16];
	int32_t cy[16];
	ResetParam(0x10, ardata, odata[count - 1].arc);
	for (long pass = 0; pass < iterations; pass++)
	{
		Repar(count, odata, 0x10, ardata);
		for (long i = 0; i < 16; i++)
			cx[i] = ardata[i].x;
		FDCT16(cx);
		if (pass < iterations - 1)
		{
			cx[0] >>= 4;
			for (long i = 1; i < 8; i++)
				cx[i] >>= 3;
			for (long i = 8; i < 16; i++)
				cx[i] = 0;
			IDCT16(cx);
			for (long i = 0; i < 16; i++)
				ardata[i].sx = Add32(ardata[i].x, cx[i]) >> 1;
		}
		for (long i = 0; i < 16; i++)
			cy[i] = ardata[i].y;
		FDCT16(cy);
		if (pass < iterations - 1)
		{
			cy[0] >>= 4;
			for (long i = 1; i < 8; i++)
				cy[i] >>= 3;
			for (long i = 8; i < 16; i++)
				cy[i] = 0;
			IDCT16(cy);
			for (long i = 0; i < 16; i++)
				ardata[i].sy = Add32(ardata[i].y, cy[i]) >> 1;
		}
		if (pass < iterations - 1)
			Tracing(0x10, ardata);
	}
	for (long i = 1; i < 8; i++)
	{
		coef[(i - 1) * 2] = cx[i] >> 4;
		coef[(i - 1) * 2 + 1] = cy[i] >> 4;
	}
	NormCdata(coef);
}


// ROM 0x0012d728 FillNwtSample__FP6_POINTP10_NWTSAMPLE
// A letter's points made a sample: the strokes (+2) and the fourteen
// coefficients, an eighth each and centred on 0x80.  (+0 and +4 are left
// for AddToDataBase.)  ==> 1, 0 for a letter that cannot be one.
long
FillNwtSample(const PS_point_type* points, UByte* sample)
{
	long result = 0;
	ODATA* odata = (ODATA*) HWRMemoryAlloc(0xd80);
	if (odata == nil)
		return result;
	long strokes;
	long n = TraceToOdata(odata, points, &strokes);
	if (n != 0)
	{
		int32_t coef[14];
		RjctAppr(n, odata, (ARDATA*) ((UByte*) odata + 0xc00), coef, 1);
		sample[3] = (UByte) strokes;
		sample[2] = (UByte) (strokes >> 8);
		for (long i = 0; i < 14; i++)
			sample[kNwtCoefficients + i] = (UByte) (0x80 + (coef[i] >> 3));
		result = 1;
	}
	HWRMemoryFree((Ptr) odata);
	return result;
}


/*------------------------------------------------------------------------------
	T h e   a n s w e r   l i s t
------------------------------------------------------------------------------*/

// ROM 0x0012d7d8 CreateAlist__Fi
// An answer list with room for so many, its count left for ClearAlist.
ALIST*
CreateAlist(long room)
{
	ALIST* list = (ALIST*) HWRMemoryAlloc((room - 1) * 0x10 + 0x14);
	if (list == nil)
		return nil;
	list->room = (UShort) room;
	list->count = 0;
	return list;
}


// ROM 0x0012d820 ClearAlist__FP6_ALIST
void
ClearAlist(ALIST* list)
{
	list->count = 0;
}


// ROM 0x0012d830 DestroyAlist__FP6_ALIST
void
DestroyAlist(ALIST* list)
{
	HWRMemoryFree((Ptr) list);
}


// ROM 0x0012d834 Occam__FUsP6_ALIST
// Whether a new sample of this letter is worth keeping: yes when the
// nearest letter found is another one; when it is this one, yes only if
// it is not already within 8 (it alone found) or not nearer than half
// the distance of the next letter.
// ROM BUG (fixed): an empty list is not looked for - its first answer,
// never written, is compared as though it were one (CreateAlist's memory
// is not cleared, so it is whatever the block last held: often the last
// list's best answer, the same size of block being had again).  The fix
// answers yes for an empty list: nothing near was found, which is the
// case the first test means ("the nearest is another letter").
long
Occam(UShort sym, ALIST* list)
{
	if (RomBugFixed() && list->count == 0)
		return 1;
	if (list->e[0].sym != sym)
		return 1;
	if (list->count < 2)
		return (list->e[0].dist >= 8) ? 1 : 0;
	int32_t ratio = SDiv(Shl32((int32_t) list->e[0].dist, 7), (int32_t) list->e[1].dist);
	return (ratio >= 0x40) ? 1 : 0;
}


// ROM 0x0012d8a0 SwapMem__FPvT1Ui
void
SwapMem(void* a, void* b, ULong count)
{
	UByte* p = (UByte*) a;
	UByte* q = (UByte*) b;
	while (count-- != 0)
	{
		UByte t = *p;
		*p++ = *q;
		*q++ = t;
	}
}


/*------------------------------------------------------------------------------
	T h e   d a t a b a s e
------------------------------------------------------------------------------*/

// ROM 0x0012d8cc InitDataBase__FPvUi
// An empty database: version 0x71, no classes, its size, 0x10 used.
void
InitDataBase(void* db, ULong size)
{
	if (db == nil)
		return;
	memset(db, 0, size);
	DBSetWord(db, 0, 0x71);
	((UByte*) db)[7] = 0;
	((UByte*) db)[6] = 0;
	DBSetWord(db, kOrtoDBSize, size);
	DBSetWord(db, kOrtoDBUsed, 0x10);
}


// the sample's letter, its class and a nought written over the copy of it
// kept in the database
static void
StoreSample(UByte* at, const UByte* sample, UShort sym, long classIndex)
{
	memcpy(at, sample, kNwtSampleSize);
	DBSetHalf(at, 0, sym);
	at[3] = (UByte) classIndex;
	at[2] = (UByte) ((ULong) classIndex >> 8);
	at[5] = 0;
	at[4] = 0;
}


// ROM 0x0012d918 AddToDataBase__FPvP10_NWTSAMPLEUs
// A sample added to its class - the letter written with that many
// strokes - at the front of the class's samples.  A class with room (at
// most 32, and room in the database) grows by one, the classes after it
// moving along; a full one (or a full database) loses its last.  A
// letter not seen before gets a new class at the end of the table (the
// samples all moving along by the twelve bytes of it) when there is room
// for it and a sample.  ==> 1, 0 when there was no room for a new class.
long
AddToDataBase(void* db, const UByte* sample, UShort sym)
{
	if (sample == nil || sym == 0 || db == nil)
		return 0;
	UByte* base = (UByte*) db;
	UByte* cls = base + kOrtoDBClassTable;
	long classes = DBHalf(db, kOrtoDBClasses);
	long index = 0;
	for ( ; index < classes; index++, cls += kOrtoDBClassSize)
		if (DBHalf(cls, 0) == sym && DBHalf(sample, 2) == DBHalf(cls, 2))
			break;
	if (index == classes)
	{
		ULong used = DBWord(db, kOrtoDBUsed);
		if (used + 0x20 > DBWord(db, kOrtoDBSize))
			return 0;
		memmove(cls + kOrtoDBClassSize, cls, used - (cls - base));
		DBSetWord(db, kOrtoDBUsed, DBWord(db, kOrtoDBUsed) + kOrtoDBClassSize);
		DBSetHalf(db, kOrtoDBClasses, DBHalf(db, kOrtoDBClasses) + 1);
		DBSetHalf(cls, 0, sym);
		DBSetHalf(cls, 2, DBHalf(sample, 2));
		cls[5] = 0;
		cls[4] = 0;
		DBSetWord(cls, 8, DBWord(db, kOrtoDBUsed));
		StoreSample(base + DBWord(db, kOrtoDBUsed), sample, sym, index);
		cls[7] = 1;
		cls[6] = 0;
		DBSetWord(db, kOrtoDBUsed, DBWord(db, kOrtoDBUsed) + kNwtSampleSize);
		for (UByte* before = cls - kOrtoDBClassSize; before >= base + kOrtoDBClassTable; before -= kOrtoDBClassSize)
			DBSetWord(before, 8, DBWord(before, 8) + kOrtoDBClassSize);
		return 1;
	}
	UByte* samples = base + DBWord(cls, 8);
	long count = DBHalf(cls, 6);
	ULong used = DBWord(db, kOrtoDBUsed);
	if (count < 0x20 && used + kNwtSampleSize <= DBWord(db, kOrtoDBSize))
	{
		memmove(samples + kNwtSampleSize, samples, used - DBWord(cls, 8));
		StoreSample(samples, sample, sym, index);
		DBSetHalf(cls, 6, DBHalf(cls, 6) + 1);
		DBSetWord(db, kOrtoDBUsed, DBWord(db, kOrtoDBUsed) + kNwtSampleSize);
		UByte* after = cls + kOrtoDBClassSize;
		for (long j = index + 1; j < (long) DBHalf(db, kOrtoDBClasses); j++, after += kOrtoDBClassSize)
			DBSetWord(after, 8, DBWord(after, 8) + kNwtSampleSize);
		return 1;
	}
	memmove(samples + kNwtSampleSize, samples, (count - 1) * kNwtSampleSize);
	StoreSample(samples, sample, sym, index);
	return 1;
}


// ROM 0x0012dbb4 SortAnswerList__FP6_ALIST
// The answers nearest first.
void
SortAnswerList(ALIST* list)
{
	ULong n = list->count;
	for (ULong i = 0; i < n; i++)
		for (ULong j = i + 1; j < n; j++)
			if (list->e[j].dist < list->e[i].dist)
				SwapMem(&list->e[i], &list->e[j], sizeof(ALISTEntry));
}


// ROM 0x0012dc24 isSymInCharSet__FUsPUc
long
isSymInCharSet(UShort sym, const UByte* charset)
{
	if (charset == nil)
		return 1;
	for ( ; *charset != 0; charset++)
		if (sym == *charset)
			return 1;
	return 0;
}


// ROM 0x0012dc64 FirstSearch__FPPvPvP10_NWTSAMPLEPUcl
// Every sample of every class written with the same number of strokes
// (and, with a character set, of a letter in it) whose first eight
// coefficients are all within the window of the new sample's.  ==> how
// many were found (at most 256), 0xffff when a character set was given
// and no class of the right strokes is in it.
long
FirstSearch(UByte** found, void* db, const UByte* sample, const UByte* charset, long window)
{
	Boolean inSet = false;
	long lo[8], hi[8];
	for (long k = 0; k < 8; k++)
	{
		lo[k] = sample[6 + k] - window;
		hi[k] = sample[6 + k] + window;
	}
	long n = 0;
	UByte* base = (UByte*) db;
	ULong classes = DBHalf(db, kOrtoDBClasses);
	UByte* cls = base + kOrtoDBClassTable;
	UByte* s = cls + classes * kOrtoDBClassSize;
	for (ULong c = 0; c < classes; c++, cls += kOrtoDBClassSize)
	{
		ULong count = DBHalf(cls, 6);
		if (DBHalf(cls, 2) != DBHalf(sample, 2)
		|| (charset != nil && !isSymInCharSet((UShort) DBHalf(cls, 0), charset)))
		{
			s += count * kNwtSampleSize;
			continue;
		}
		if (charset != nil)
			inSet = true;
		for (ULong k = 0; k < count; k++, s += kNwtSampleSize)
		{
			Boolean near = true;
			for (long b = 0; b < 8 && near; b++)
				near = s[6 + b] >= lo[b] && s[6 + b] <= hi[b];
			if (near)
			{
				*found++ = s;
				if (++n == 0x100)
					return 0x100;
			}
		}
	}
	if (charset != nil && !inSet)
		n = 0xffff;
	return n;
}


// ROM 0x0012deec SecondSearch__FPPvPvP10_NWTSAMPLEPUclT5
// As FirstSearch, in a window with a hole in the middle: each of the
// eight coefficients within the window and not strictly inside the hole.
long
SecondSearch(UByte** found, void* db, const UByte* sample, const UByte* charset, long window, long hole)
{
	long lo[8], hi[8], holeLo[8], holeHi[8];
	for (long k = 0; k < 8; k++)
	{
		lo[k] = sample[6 + k] - window;
		hi[k] = sample[6 + k] + window;
		holeLo[k] = sample[6 + k] - hole;
		holeHi[k] = sample[6 + k] + hole;
	}
	long n = 0;
	UByte* base = (UByte*) db;
	ULong classes = DBHalf(db, kOrtoDBClasses);
	UByte* cls = base + kOrtoDBClassTable;
	UByte* s = cls + classes * kOrtoDBClassSize;
	for (ULong c = 0; c < classes; c++, cls += kOrtoDBClassSize)
	{
		ULong count = DBHalf(cls, 6);
		if (DBHalf(cls, 2) != DBHalf(sample, 2)
		|| (charset != nil && !isSymInCharSet((UShort) DBHalf(cls, 0), charset)))
		{
			s += count * kNwtSampleSize;
			continue;
		}
		for (ULong k = 0; k < count; k++, s += kNwtSampleSize)
		{
			Boolean near = true;
			for (long b = 0; b < 8 && near; b++)
			{
				long v = s[6 + b];
				near = v >= lo[b] && v <= hi[b] && (v <= holeLo[b] || v >= holeHi[b]);
			}
			if (near)
			{
				*found++ = s;
				if (++n == 0x100)
					return 0x100;
			}
		}
	}
	return n;
}


// ROM 0x0012e2a4 SearchInDataBase__FP6_ALISTP10_NWTSAMPLEPvPUc
// The samples near a new one, gathered by letter.  FirstSearch's window
// grows by 0x30 until something is found (giving up past 0x430); each
// sample found is measured - the squared distance over all fourteen
// coefficients - and counted against its letter, whose answer keeps the
// nearest (a letter not in the list is added while there is room: the
// search stops when there is not).  If the best distance is further than
// 0x900 the samples within its square root are searched for once more,
// the window first searched left out.  Then every answer's distance is
// made its square root and the list sorted.  ==> 1, 0 when nothing was
// found.
long
SearchInDataBase(ALIST* list, const UByte* sample, void* db, const UByte* charset)
{
	uint32_t best = 0x7fffffff;
	long window = 0x30;
	Boolean first = true;
	UByte** found = (UByte**) HWRMemoryAlloc(0x400 * sizeof(UByte*) / 4);	// (DEVIATION: 256 pointers, host-sized)
	if (found == nil)
		return 0;
	uint32_t lastBest = 0x900;
	long n;
	for ( ; ; )
	{
		n = FirstSearch(found, db, sample, charset, window);
		if (n == 0xffff)
			goto none;
		if (n != 0)
			break;
		if (window > 0x400)
			goto none;
		window += 0x30;
	}
	for ( ; ; )
	{
		for (long i = 0; i < n; i++)
		{
			UByte* s = found[i];
			uint32_t dist = 0;
			for (long b = kNwtCoefficients; b < kNwtSampleSize; b++)
			{
				int32_t d = (int32_t) s[b] - (int32_t) sample[b];
				dist = (uint32_t) Add32(Mul32(d, d), (int32_t) dist);
			}
			if (dist < best)
				best = dist;
			UShort sym = (UShort) DBHalf(s, 0);
			long k = 0;
			long count = list->count;
			for ( ; k < count; k++)
				if (list->e[k].sym == sym)
					break;
			if (k == count)
			{
				if (k == list->room)
					goto done;
				ALISTEntry* a = &list->e[k];
				a->sym = sym;
				a->count = 1;
				a->dist = dist;
				a->classOffset = DBHalf(s, 2) * kOrtoDBClassSize + kOrtoDBClassTable;
				a->sampleOffset = (ULong32) (s - (UByte*) db);
				list->count++;
			}
			else
			{
				ALISTEntry* a = &list->e[k];
				a->count++;
				if (a->dist > dist)
					a->dist = dist;
			}
		}
		if (best <= lastBest || !first)
			break;
		long hole = window;
		window = SQRT32_ORTO(best);
		lastBest = best;
		n = SecondSearch(found, db, sample, charset, window, hole);
		first = false;
		if (n == 0)
			break;
	}
done:
	for (long k = 0; k < list->count; k++)
		list->e[k].dist = (ULong32) SQRT32_ORTO(list->e[k].dist);
	SortAnswerList(list);
	HWRMemoryFree((Ptr) found);
	return 1;
none:
	HWRMemoryFree((Ptr) found);
	return 0;
}


/*------------------------------------------------------------------------------
	T h e   s i x t e e n   p o i n t s
------------------------------------------------------------------------------*/

// ROM 0x001528b0 ResetParam__FiP7_ARDATAl
// The points' target distances along the letter: evenly spaced over its
// length.
void
ResetParam(long count, ARDATA* ardata, long length)
{
	int32_t step = SDiv((int32_t) length, (int32_t) (count - 1));
	for (long i = 0; i < count; i++)
	{
		if (i == 0)
		{
			ardata[0].len = 0;
			ardata[0].arc = 0;
			continue;
		}
		ardata[i].len = step;
		ardata[i].arc = Add32(ardata[i - 1].arc, step);
	}
}


// ROM 0x00152924 Repar__FiP6_ODATAT1P7_ARDATA
// The letter resampled: the ends where they are, and each point between
// at its target distance along (scaled by how the letter's length stands
// to the targets', a ratio worked out to 24 bits of fraction) found on
// the step that reaches it, the way along the step worked out a bit at a
// time.
void
Repar(long count, const ODATA* odata, long points, ARDATA* ardata)
{
	ardata[0].x = odata[0].x;
	ardata[0].y = odata[0].y;
	ardata[points - 1].x = odata[count - 1].x;
	ardata[points - 1].y = odata[count - 1].y;
	int32_t length = odata[count - 1].arc;
	int32_t target = ardata[points - 1].arc;
	int32_t ratio = 0;
	while (length > target)
	{
		length = Sub32(length, target);
		ratio++;
	}
	for (long i = 0; i < 12; i++)
	{
		length = Shl32(length, 1);
		ratio = Shl32(ratio, 1);
		if (length > target)
		{
			length = Sub32(length, target);
			ratio++;
		}
		length = Shl32(length, 1);
		ratio = Shl32(ratio, 1);
		if (length > target)
		{
			length = Sub32(length, target);
			ratio++;
		}
	}
	int32_t r3 = ratio >> 24;
	int32_t r2 = (int32_t) (((uint32_t) ratio & 0xff0000) >> 16);
	int32_t r1 = (int32_t) (((uint32_t) ratio & 0xff00) >> 8);
	int32_t r0 = (int32_t) ((uint32_t) ratio & 0xff);
	const ODATA* o = odata + 1;
	ARDATA* a = ardata + 1;
	long left = points;
	do
	{
		int32_t arc = a->arc;
		int32_t t = Add32(Add32(Add32(Mul32(arc, r3), Mul32(r2, arc) >> 8), Mul32(r1, arc) >> 16), Mul32(r0, arc) >> 24);
		while (o->arc <= t)
			o++;
		int32_t len = o->len;
		int32_t rest = Sub32(len, Sub32(o->arc, t));
		int32_t dx = o->dx;
		int32_t dy = o->dy;
		int32_t ax = (dx <= 0) ? -dx : dx;
		int32_t ay = (dy <= 0) ? -dy : dy;
		int32_t px = 0, py = 0;
		if (rest >= len)
		{
			rest = Sub32(rest, len);
			px = ax;
			py = ay;
		}
		for (int s = 1; s <= 10; s++)
		{
			rest = Shl32(rest, 1);
			if (rest >= len)
			{
				rest = Sub32(rest, len);
				px = Add32(px, ax >> s);
				py = Add32(py, ay >> s);
			}
		}
		rest = Shl32(rest, 1);
		ax >>= 11;
		ay >>= 11;
		do
		{
			if (rest >= len)
			{
				rest = Sub32(rest, len);
				px = Add32(px, ax);
				py = Add32(py, ay);
			}
			rest = Shl32(rest, 1);
			if (rest >= len)
			{
				rest = Sub32(rest, len);
				px = Add32(px, ax >> 1);
				py = Add32(py, ay >> 1);
			}
			rest = Shl32(rest, 1);
			ax >>= 2;
			ay >>= 2;
		} while ((ax | ay) != 0);
		if (dx <= 0)
			px = -px;
		if (dy <= 0)
			py = -py;
		a->x = Add32(o[-1].x, px);
		a->y = Add32(o[-1].y, py);
		a++;
	} while (--left > 2);
}


// ROM 0x00152bd0 SQRT32_ORTO__FUl
// The whole square root a bit at a time (the ink codec's SQRT32 again,
// written out step by step).
long
SQRT32_ORTO(ULong value)
{
	uint32_t n = (uint32_t) value;
	uint32_t root = 0;
	for (int shift = 30; shift >= 0; shift -= 2)
	{
		uint32_t trial = (root * 4 + 1) << shift;
		if (n >= trial)
		{
			n -= trial;
			root = root * 2 + 1;
		}
		else
			root = root * 2;
	}
	return (long) root;
}


// ROM 0x00152d10 Tracing__FiP7_ARDATA
// The smoothed points measured: each step's length and how far along.
void
Tracing(long count, ARDATA* ardata)
{
	int32_t arc = 0;
	ardata[0].arc = 0;
	ardata[0].len = 0;
	for (long i = 1; i < count; i++)
	{
		int32_t dx = Sub32(ardata[i].sx, ardata[i - 1].sx);
		int32_t dy = Sub32(ardata[i].sy, ardata[i - 1].sy);
		if (dx < 0)
			dx = -dx;
		if (dy < 0)
			dy = -dy;
		int32_t len = (int32_t) SQRT32_ORTO((ULong) (uint32_t) Add32(Mul32(dy, dy), Mul32(dx, dx)));
		ardata[i].len = len;
		arc = Add32(arc, len);
		ardata[i].arc = arc;
	}
}


/*------------------------------------------------------------------------------
	T h e   D C T s

	A fast discrete cosine transform in whole numbers, split in halves
	down to four points: each multiply by a constant is a whole byte and a
	fraction byte (KMul).
------------------------------------------------------------------------------*/

// ROM 0x00079ff8 FDCT4__FPl
void
FDCT4(int32_t* a)
{
	int32_t s03 = Add32(a[0], a[3]);
	int32_t d03 = Sub32(a[0], a[3]);
	int32_t s12 = Add32(a[1], a[2]);
	int32_t d12 = Sub32(a[1], a[2]);
	int32_t u = KMul(d03, 0x8a, 0x8b);
	int32_t v = KMul(d12, 0x14e, 0x7a);
	int32_t e = Sub32(s03, s12);
	int32_t f = Sub32(u, v);
	f = KMul(f, 0xb5, 4);
	a[3] = f;
	a[2] = KMul(e, 0xb5, 4);
	a[1] = Add32(Add32(u, v), f);
	a[0] = Add32(s03, s12);
}


// ROM 0x0007a0bc IDCT4__FPl
void
IDCT4(int32_t* a)
{
	int32_t a1 = a[1];
	int32_t t2 = KMul(a[2], 0xb5, 4);
	int32_t t3 = KMul(Add32(a[3], a1), 0xb5, 4);
	int32_t p = Add32(a[0], t2);
	int32_t m = Sub32(a[0], t2);
	int32_t q = Add32(a1, t3);
	int32_t r = Sub32(a1, t3);
	q = KMul(q, 0x8a, 0x8b);
	r = KMul(r, 0x14e, 0x7a);
	a[2] = Sub32(m, r);
	a[3] = Sub32(p, q);
	a[1] = Add32(m, r);
	a[0] = Add32(p, q);
}


// ROM 0x0007a17c FDCT8__FPl
void
FDCT8(int32_t* a)
{
	int32_t a7 = a[7];
	int32_t d07 = Sub32(a[0], a7);
	int32_t d34 = Sub32(a[3], a[4]);
	a[3] = Add32(a[3], a[4]);
	a[4] = KMul(d07, 0x82, 0x81);
	a[7] = KMul(d34, 0x290, 0x1b);
	a[0] = Add32(a[0], a7);
	int32_t a6 = a[6];
	int32_t d16 = Sub32(a[1], a6);
	int32_t d25 = Sub32(a[2], a[5]);
	a[2] = Add32(a[2], a[5]);
	a[5] = KMul(d16, 0x99, 0xf1);
	a[6] = KMul(d25, 0xe6, 100);
	a[1] = Add32(a[1], a6);
	FDCT4(a);
	FDCT4(a + 4);
	int32_t o6 = a[6];
	a[6] = a[3];
	int32_t o5 = a[5];
	a[5] = Add32(a[7], o6);
	int32_t o4 = a[4];
	a[3] = Add32(o6, o5);
	a[4] = a[2];
	int32_t o1 = a[1];
	a[1] = Add32(o4, o5);
	a[2] = o1;
}


// ROM 0x0007a2b4 IDCT8__FPl
void
IDCT8(int32_t* a)
{
	int32_t o4 = a[4];
	int32_t o1 = a[1];
	a[4] = o1;
	a[1] = a[2];
	a[2] = o4;
	int32_t o3 = a[3];
	a[3] = a[6];
	a[6] = Add32(a[5], o3);
	a[7] = Add32(a[7], a[5]);
	a[5] = Add32(o3, o1);
	IDCT4(a);
	IDCT4(a + 4);
	int32_t t4 = KMul(a[4], 0x82, 0x81);
	int32_t t5 = KMul(a[5], 0x99, 0xf1);
	int32_t t6 = KMul(a[6], 0xe6, 100);
	int32_t t7 = KMul(a[7], 0x290, 0x1b);
	int32_t a0 = a[0];
	a[0] = Add32(a0, t4);
	a[4] = Sub32(a[3], t7);
	a[7] = Sub32(a0, t4);
	a[3] = Add32(a[3], t7);
	int32_t a1 = a[1];
	a[1] = Add32(a1, t5);
	int32_t a2 = a[2];
	a[2] = Add32(a2, t6);
	a[5] = Sub32(a2, t6);
	a[6] = Sub32(a1, t5);
}


// ROM 0x0007a3e8 FDCT16__FPl
void
FDCT16(int32_t* a)
{
	int32_t a15 = a[15];
	int32_t d = Sub32(a[0], a15);
	int32_t e = Sub32(a[7], a[8]);
	a[7] = Add32(a[7], a[8]);
	a[8] = KMul(d, 0x80, 0x9e);
	a[15] = KMul(e, 0x519, 0xe4);
	a[0] = Add32(a[0], a15);
	d = Sub32(a[1], a[14]);
	int32_t a9 = a[9];
	e = Sub32(a[6], a9);
	a[1] = Add32(a[1], a[14]);
	a[9] = KMul(d, 0x85, 0x2c);
	a[14] = KMul(e, 0x1b8, 0xf2);
	a[6] = Add32(a[6], a9);
	int32_t a13 = a[13];
	d = Sub32(a[2], a13);
	e = Sub32(a[5], a[10]);
	a[5] = Add32(a[5], a[10]);
	a[10] = KMul(d, 0x91, 0x23);
	a[13] = KMul(e, 0x10f, 0x88);
	a[2] = Add32(a[2], a13);
	e = Sub32(a[3], a[12]);
	d = Sub32(a[4], a[11]);
	a[3] = Add32(a[3], a[12]);
	a[4] = Add32(a[4], a[11]);
	a[11] = KMul(e, 0xa5, 0x96);
	a[12] = KMul(d, 0xc9, 0xc4);
	FDCT8(a);
	FDCT8(a + 8);
	for (long i = 8; i < 15; i++)
		a[i] = Add32(a[i], a[i + 1]);
	for (long i = 1; i < 8; i += 2)
	{
		long j = i;
		int32_t carry = a[i];
		do
		{
			j *= 2;
			if (j > 15)
				j -= 15;
			int32_t t = a[j];
			a[j] = carry;
			carry = t;
		} while (j != i);
	}
}


// ROM 0x0007a628 IDCT16__FPl
void
IDCT16(int32_t* a)
{
	for (long i = 1; i < 8; i += 2)
	{
		int32_t keep = a[i];
		long j = i;
		long prev;
		do
		{
			prev = j;
			j *= 2;
			if (j > 15)
				j -= 15;
			a[prev] = a[j];
		} while (j != i);
		a[prev] = keep;
	}
	for (long i = 15; i > 8; i--)
		a[i] = Add32(a[i], a[i - 1]);
	IDCT8(a);
	IDCT8(a + 8);
	for (long i = 0; i < 8; i++)
	{
		int32_t c = _2C16[i];
		int32_t t = Add32(Mul32(c >> 8, a[8 + i]) >> 8, Mul32(c & 0xff, a[8 + i]) >> 16);
		int32_t v = a[i];
		a[i] = Add32(v, t);
		a[8 + i] = Sub32(v, t);
	}
	for (long i = 8, j = 15; i < j; i++, j--)
	{
		int32_t t = a[i];
		a[i] = a[j];
		a[j] = t;
	}
}
