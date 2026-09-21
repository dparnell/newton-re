/*
	File:		ink/CICEncoder.cpp

	Contains:	The ROM's ink encoder - the other way round from
				CICDecoder.cpp.  See CICCodec.h.
*/

#include "CICCodec.h"
#include "ByteOrder.h"

#include <string.h>


// ROM 0x00282aa0 PutBits__FP4_CDCUlUs
// n bits of a value written where the writer has got to, least
// significant first, which is the order GetNBit reads them in.  The
// bits are laid into the bytes a piece at a time, each piece as much of
// the current byte as is left, and only the bits being written are
// disturbed - so a buffer may be written over more than once, which is
// what the encoder does when it tries a stroke two ways and keeps the
// shorter.
//
// The writer refuses to pass the end of the buffer, and says which of
// the two things went wrong: no buffer at all, or no room in it.
Boolean
PutBits(CICEncoder* encoder, ULong value, ULong n)
{
	if (encoder->fOut == nil)
	{
		encoder->fError = kCICNoBuffer;
		return false;
	}
	ULong end = encoder->fBitPos + n;
	if (end > encoder->fBitLimit)
	{
		encoder->fError = kCICNoRoom;
		return false;
	}
	ULong bit = encoder->fBitPos & 7;
	UByte* at = encoder->fOut + (encoder->fBitPos >> 3);
	while (n != 0)
	{
		ULong take = n;
		if (n + bit > 7)
			take = 8 - bit;
		UByte mask = (UByte) ((0xff << bit) & (0xff >> (8 - (take + bit))));
		*at = (UByte) ((*at & ~mask) | ((value & 0xff) << bit));
		value >>= take;
		bit = 0;
		at++;
		n -= take;
	}
	encoder->fBitPos = end;
	if (encoder->fHighWater < end)
		encoder->fHighWater = end;
	return true;
}


// ROM 0x00282bb8 FindCodeWord__FsUsP9_CODEWORD
// Which of a book table's entries holds a value.  The decoder can walk
// the entries in order of code length; the encoder has only the value
// to go on, so it looks at all of them.  ==> -1 when none does.
long
FindCodeWord(short value, ULong count, const void* entries)
{
	for (ULong i = 0; i < count; i++)
		if ((short) GetBigEndianHalf((const char*) entries + i * kCodeTableEntrySize) == value)
			return (long) i;
	return -1;
}


// (the code and its length, out of a book table's entry array)
static Boolean
PutCodeWord(CICEncoder* encoder, const void* entries, long i)
{
	const char* at = (const char*) entries + i * kCodeTableEntrySize;
	ULong length = GetBigEndianHalf(at + 2);
	ULong code = ((ULong) GetBigEndianHalf(at + 4) << 16) | GetBigEndianHalf(at + 6);
	return PutBits(encoder, code, length);
}


// ROM 0x00282c04 EncodeWord_OLD__FP4_CDCsP10_CODETABLE
// A value written through one of a book's tables.  A value between the
// table's two bases goes out as its own code; one at or above the upper
// base goes out as the upward escape followed by what is left of it
// after the base is taken away, and one at or below the lower base the
// same downwards - and since what is left may still be out of range,
// that is the same call again.
Boolean
EncodeWord_OLD(CICEncoder* encoder, short value, const void* table)
{
	const char* entries = (const char*) table + kCodeTableHeaderSize;
	ULong count = CodeTableCount(table);
	long v = value;
	short base;
	if (v < CodeTableBaseUp(table))
	{
		if (v > CodeTableBaseDown(table))
		{
			long i = FindCodeWord(value, count, entries);
			if (i < 0)
				return false;
			return PutCodeWord(encoder, entries, i);
		}
		long i = FindCodeWord(CodeTableEscapeDown(table), count, entries);
		if (i < 0)
			return false;
		PutCodeWord(encoder, entries, i);
		base = CodeTableBaseDown(table);
	}
	else
	{
		long i = FindCodeWord(CodeTableEscapeUp(table), count, entries);
		if (i < 0)
			return false;
		PutCodeWord(encoder, entries, i);
		base = CodeTableBaseUp(table);
	}
	return EncodeWord_OLD(encoder, (short) (v - base), table);
}


// ROM 0x00282d38 EncodeWord_NEW__FP4_CDCsP9_CODEWORD
// And through one of the two static tables, which have no escapes: the
// entries are walked until one holds the value or the table runs out.
Boolean
EncodeWord_NEW(CICEncoder* encoder, short value, const unsigned short* table)
{
	long i = 0;
	while (CodeWordValue(table, i) != value && CodeWordLength(table, i) != 0)
		i++;
	if (CodeWordLength(table, i) == 0)
		return false;
	return PutBits(encoder, CodeWordCode(table, i), CodeWordLength(table, i));
}




/*------------------------------------------------------------------------------
	T h e   t r a c e
------------------------------------------------------------------------------*/

// ROM 0x00283c1c SQRT32__FUl
// A whole square root, a bit at a time: at each step the next bit of the
// answer is set if what is left is at least the trial value that bit
// would make, and taken away if it is.  The ROM writes the sixteen steps
// out one after another; they are all the same.
long
SQRT32(ULong n)
{
	ULong root = 0;
	for (long shift = 30; shift >= 0; shift -= 2)
	{
		ULong trial = (root * 4 + 1) << shift;
		if (n < trial)
			root = root * 2;
		else
		{
			n -= trial;
			root = root * 2 + 1;
		}
	}
	return (long) root;
}


// ROM 0x002802c4 AddPointToOdata__FP4_CDCP6_POINT
// A point added to the trace: where it is in the codec's own units, the
// step from the point before, how long that step was and how far along
// the stroke it is.
//
// The length is a square root, and the two sides are brought down by
// halves until their sum and the larger of them will fit in sixteen
// bits, so that squaring them cannot overflow; the answer is then shifted
// back up.  A point nearer than about one tablet unit to the one before
// is not kept at all.
//
// ==> 1 when the point was taken, 0 when it was too near the one before,
// -1 when the trace has no room left.
long
AddPointToOdata(CICEncoder* encoder, const InkPoint* pt)
{
	CICTracePoint* at = &encoder->fTrace[encoder->fPointCount];
	at->x = (encoder->fUnit * pt->x) / (long) encoder->fOne;
	at->y = (encoder->fUnit * pt->y) / (long) encoder->fOne;
	if (encoder->fPointCount == 0)
	{
		at->dx = 0;
		at->dy = 0;
		at->fLength = 0;
		at->fArc = 0;
	}
	else
	{
		long dx = at->x - at[-1].x;
		long dy = at->y - at[-1].y;
		at->dx = dx;
		at->dy = dy;
		if (dx < 0)
			dx = -dx;
		if (dy < 0)
			dy = -dy;
		long bigger = dy < dx ? dx : dy;
		ULong room = (ULong) (dx + dy + bigger);
		ULong shift = 0;
		while ((room >>= 1) > 0xffff)
		{
			dx >>= 1;
			dy >>= 1;
			shift++;
		}
		long length = SQRT32((ULong) (dy * dy + dx * dx)) << shift;
		at->fLength = length;
		at->fArc = at[-1].fArc + length;
		if (length < kCICShortestStep)
			return 0;
	}
	// (the ROM asks the same question twice)
	if (encoder->fPointCount != 0 && at->fLength < kCICShortestStep)
		return 0;
	encoder->fPointCount++;
	return encoder->fPointCount == (ULong) kCICTraceFull ? -1 : 1;
}




/*------------------------------------------------------------------------------
	N i n e   s a m p l e s ,   f o u r   n u m b e r s
------------------------------------------------------------------------------*/

// The three constants the transform turns on, each written out as a
// twenty-four bit fraction in three bytes so that an ARM can multiply by
// it without a long multiply: 0xb504f3 is a root half (0.7071),
// 0x61f78a the sine of an eighth of a right angle (0.3827) and 0xec835e
// its cosine (0.9239).  The shifts are one less in the forward
// transform, which doubles them, and the whole answer is divided by
// eight at the end.
static inline long
RootHalf(long v, long shift)
{
	return (v * 0xb5 >> shift) + (v * 4 >> (shift + 8)) + (v * 0xf3 >> (shift + 16));
}

static inline long
SinEighth(long v, long shift)
{
	return (v * 0x61 >> shift) + (v * 0xf7 >> (shift + 8)) + (v * 0x8a >> (shift + 16));
}

static inline long
CosEighth(long v, long shift)
{
	return (v * 0xec >> shift) + (v * 0x83 >> (shift + 8)) + (v * 0x5e >> (shift + 16));
}


// Nine samples into the four numbers that describe the segment through
// them.  The two that bend it are worked out from the samples alone; the
// other two follow from where the segment starts and ends, exactly as
// ReadSegmentNear works them out the other way about.
static void
RFFT_9_4(const CICSample* p, long* c, long first, long last, Boolean isY)
{
	const long* at = isY ? &p[0].y : &p[0].x;
	const long kStride = (long) (sizeof(CICSample) / sizeof(long));
	long v0 = at[0 * kStride], v1 = at[1 * kStride], v2 = at[2 * kStride];
	long v3 = at[3 * kStride], v4 = at[4 * kStride], v5 = at[5 * kStride];
	long v6 = at[6 * kStride], v7 = at[7 * kStride], v8 = at[8 * kStride];

	long alternating = ((v1 - v3) - v5) + v7;
	c[2] = (v0 + v8 - v4 * 2 + RootHalf(alternating, 7)) >> 3;
	c[0] = ((first + last) >> 1) - c[2];

	long outer = v1 - v7;
	long middle = v6 - v2;
	long inner = v5 - v3;
	c[3] = ((v0 - v8) + SinEighth(outer, 7) + RootHalf(middle, 7) + CosEighth(inner, 7)) >> 3;
	c[1] = ((first - last) >> 1) - c[3];
}


// And the four numbers back into nine samples.
static void
RIFT_4_9(CICSample* p, const long* c, Boolean isY)
{
	long* at = isY ? &p[0].y : &p[0].x;
	const long kStride = (long) (sizeof(CICSample) / sizeof(long));
	long difference = c[1] - c[3];
	long sum = c[1] + c[3];
	long middle = c[0] + c[2];
	at[0 * kStride] = middle + sum;
	at[8 * kStride] = middle - sum;
	at[4 * kStride] = c[0] - c[2];
	long quarter = RootHalf(difference, 8);
	at[2 * kStride] = c[0] + quarter;
	at[6 * kStride] = c[0] - quarter;
	long a = CosEighth(c[1], 8) + SinEighth(c[3], 8);
	long b = SinEighth(c[1], 8) - CosEighth(c[3], 8);
	long g = RootHalf(c[2], 8);
	at[1 * kStride] = c[0] + a + g;
	at[3 * kStride] = (c[0] + b) - g;
	at[5 * kStride] = (c[0] - b) - g;
	at[7 * kStride] = (c[0] - a) + g;
}


// ROM 0x002836a4 RFFT_9_4_X__FP6_RPR_PPllT3
void
RFFT_9_4_X(const CICSample* samples, long* coef, long first, long last)
{
	RFFT_9_4(samples, coef, first, last, false);
}


// ROM 0x002837d8 RFFT_9_4_Y__FP6_RPR_PPllT3
void
RFFT_9_4_Y(const CICSample* samples, long* coef, long first, long last)
{
	RFFT_9_4(samples, coef, first, last, true);
}


// ROM 0x0028390c RIFT_4_9_X__FP6_RPR_PPl
void
RIFT_4_9_X(CICSample* samples, const long* coef)
{
	RIFT_4_9(samples, coef, false);
}


// ROM 0x00283a94 RIFT_4_9_Y__FP6_RPR_PPl
void
RIFT_4_9_Y(CICSample* samples, const long* coef)
{
	RIFT_4_9(samples, coef, true);
}


/*------------------------------------------------------------------------------
	W h e r e   t h e   f i t t i n g   l o o k s
------------------------------------------------------------------------------*/

// ROM 0x0027f8cc ResetParam__FP4_CDC
// The nine places spread evenly along the stroke so far: a ninth of its
// whole length apart, the first at nought.
void
ResetParam(CICEncoder* encoder)
{
	long step = encoder->fTrace[encoder->fPointCount - 1].fArc / kCICSamples;
	for (long i = 0; i < kCICSamples; i++)
	{
		encoder->fSamples[i].fStep = step;
		encoder->fSamples[i].fAt = step * i;
	}
}


// ROM 0x00280440 StoreContext__FP4_CDC
// How far the trace had got and where the nine places were, kept so that
// a try that comes to nothing can be undone.
void
StoreContext(CICEncoder* encoder)
{
	encoder->fSavedCount = encoder->fPointCount;
	for (long i = 0; i < kCICSamples; i++)
	{
		encoder->fSavedSamples[i].fAt = encoder->fSamples[i].fAt;
		encoder->fSavedSamples[i].fStep = encoder->fSamples[i].fStep;
	}
}


// ROM 0x0028049c RestoreContext__FP4_CDC
void
RestoreContext(CICEncoder* encoder)
{
	encoder->fPointCount = encoder->fSavedCount;
	for (long i = 0; i < kCICSamples; i++)
	{
		encoder->fSamples[i].fAt = encoder->fSavedSamples[i].fAt;
		encoder->fSamples[i].fStep = encoder->fSavedSamples[i].fStep;
	}
}




/*------------------------------------------------------------------------------
	R o u n d i n g   a   s e g m e n t
------------------------------------------------------------------------------*/

// ROM 0x0028333c TryQuantVariant__FPlT1lT1
// How far a rounded segment is from the fitted one: the squares of the
// four differences, added up and given up on as soon as they pass the
// best so far.  The first two numbers are compared as they stand; the
// other two are what the decoder will work out from the segment's two
// ends, so they are compared the same way round - half the sum of the
// ends less the first bend, and half their difference less the second.
Boolean
TryQuantVariant(const long* coef, const long* trial, long best, long* outError)
{
	Boolean better = false;
	long d = coef[2] - trial[2];
	long error = d * d;
	if (error <= best)
	{
		d = coef[3] - trial[3];
		error += d * d;
		if (error <= best)
		{
			d = (coef[0] - (trial[0] + trial[1]) / 2) + trial[2];
			error += d * d;
			if (error <= best)
			{
				d = (coef[1] - (trial[0] - trial[1]) / 2) + trial[3];
				error += d * d;
				if (error <= best)
					better = true;
			}
		}
	}
	*outError = error;
	return better;
}


// ROM 0x0028279c SegVectQuant__FP4_CDCPsUi
// The three numbers of one coordinate - where the segment ends, and its
// two bends - have each been rounded to a whole step on their own.  The
// three roundings are not independent, though: the decoder works the
// other two numbers out from all three at once, so a rounding that is
// worse on its own can come out better together.  So all twenty-seven
// ways of nudging the three by one either way are tried, and whichever
// comes nearest the fitted segment is kept.
//
// ROM bug kept: which of the twenty-seven was best is only written down
// when one of them is better than the best so far, and the best so far
// starts at a number large enough that the first try all but always
// takes it - all but.  DEVIATION: the host starts the three at the first
// try's own offsets, where the ROM would use whatever was in the
// registers.
void
SegVectQuant(CICEncoder* encoder, short* rounded, ULong which)
{
	long step = encoder->fStepA;
	long limitB = encoder->fLimitB;
	long limitA = encoder->fLimitA;
	const long* coef = which == 0 ? encoder->fCoefX : encoder->fCoefY;
	long trial[4];
	trial[0] = which == 0 ? encoder->fPenX : encoder->fPenY;
	trial[1] = (rounded[1] * step + trial[0]) - step;
	trial[2] = limitA * rounded[2] - limitA;
	trial[3] = limitB * rounded[3] - limitB;
	long best = 0x40000000;
	long bestEnd = -1;
	long bestFirst = -1;
	long bestSecond = -1;
	for (long end = -1; end < 2; end++)
	{
		for (long first = -1; first < 2; first++)
		{
			for (long second = -1; second < 2; second++)
			{
				long error;
				if (TryQuantVariant(coef, trial, best, &error))
				{
					best = error;
					bestEnd = end;
					bestFirst = first;
					bestSecond = second;
				}
				trial[3] += limitB;
			}
			trial[3] -= limitB * 3;
			trial[2] += limitA;
		}
		trial[2] -= limitA * 3;
		trial[1] += step;
	}
	rounded[1] = (short) (rounded[1] + bestEnd);
	rounded[2] = (short) (rounded[2] + bestFirst);
	rounded[3] = (short) (rounded[3] + bestSecond);
}


// ROM 0x0028303c WriteSegment__FP4_CDCs
// A segment written out: where it ends, as a step from where the stroke
// last was, and the two numbers that bend each coordinate, each rounded
// to a whole one of the two lengths the book was opened with.  The
// roundings are then nudged together (SegVectQuant) unless the fitting
// was told not to bother.  The word at the end says whether the stroke
// goes on.
Boolean
WriteSegment(CICEncoder* encoder, short tag)
{
	short x[4];
	short y[4];
	x[0] = 0;
	y[0] = 0;
	x[1] = (short) QvantUN(encoder->fLastX - encoder->fStrokeX, encoder->fStepA);
	y[1] = (short) QvantUN(encoder->fLastY - encoder->fStrokeY, encoder->fStepA);
	x[2] = (short) QvantUN(encoder->fCoefX[2], encoder->fLimitA);
	y[2] = (short) QvantUN(encoder->fCoefY[2], encoder->fLimitA);
	x[3] = (short) QvantUN(encoder->fCoefX[3], encoder->fLimitB);
	y[3] = (short) QvantUN(encoder->fCoefY[3], encoder->fLimitB);
	if (encoder->fSlack != 0 && encoder->fSlack2 != 0)
	{
		SegVectQuant(encoder, x, 0);
		SegVectQuant(encoder, y, 1);
	}
	encoder->fPenX += encoder->fStepA * x[1];
	encoder->fLastX = encoder->fPenX;
	encoder->fPenY += encoder->fStepA * y[1];
	encoder->fLastY = encoder->fPenY;
	return EncodeWord_OLD(encoder, x[1], encoder->fTables[2])
		&& EncodeWord_OLD(encoder, y[1], encoder->fTables[3])
		&& EncodeWord_OLD(encoder, x[2], encoder->fTables[4])
		&& EncodeWord_OLD(encoder, y[2], encoder->fTables[5])
		&& EncodeWord_OLD(encoder, x[3], encoder->fTables[6])
		&& EncodeWord_OLD(encoder, y[3], encoder->fTables[7])
		&& EncodeWord_NEW(encoder, tag, kInkEncFormatCodes);
}


/*------------------------------------------------------------------------------
	T h e   n i n e   p l a c e s   o n   t h e   s t r o k e
------------------------------------------------------------------------------*/

// ROM 0x00283424 Repar__FP6_ORG_PT1P6_RPR_PT3
// Where the nine places the fitting looks at fall on the stroke itself.
//
// The places are spread along the *curve*, and the stroke is a chain of
// straight steps, so the two are of different lengths.  The ratio of one
// to the other is worked out first, to twenty-four binary places, by
// long division - the whole part by taking the divisor away while it
// will go, and then twelve rounds of two bits each.  Each place's
// distance along the curve, multiplied by that ratio, says how far along
// the stroke to look; the step that distance falls in is found by
// walking the trace, and the point is that far into it.
//
// Dividing into the step is a long division too, and it is done on the
// step's own two sides at once: the quotient is never formed, its bits
// are used as they come out to add a halving of each side.  The first
// eleven bits are written out one after another and the rest go round a
// loop until both sides have been shifted away to nothing.
//
// The first and last places are the stroke's own ends, taken as they
// are.  ==> the ratio, which is what tells the caller how far the fit
// has stretched the curve.
ULong
Repar(const CICTracePoint* last, const CICTracePoint* first,
	  const CICSample* samples, CICSample* out)
{
	out[0].x = first->x;
	out[0].y = first->y;
	out[8].x = last->x;
	out[8].y = last->y;

	long total = samples[8].fAt;
	long remainder = last->fArc;
	ULong ratio = 0;
	while (total < remainder)
	{
		remainder -= total;
		ratio++;
	}
	for (long round = 0; round < 12; round++)
	{
		remainder *= 2;
		ratio *= 2;
		if (total < remainder)
		{
			remainder -= total;
			ratio++;
		}
		remainder *= 2;
		ratio *= 2;
		if (total < remainder)
		{
			remainder -= total;
			ratio++;
		}
	}

	const CICTracePoint* at = first + 1;
	for (long i = 1; i < 8; i++)
	{
		long along = samples[i].fAt;
		// the distance times the ratio, a byte of it at a time
		long want = ((long) ratio >> 24) * along
				  + ((long) (((ratio & 0xff0000) >> 16) * (ULong) along) >> 8)
				  + ((long) (((ratio & 0xff00) >> 8) * (ULong) along) >> 16)
				  + ((long) ((ratio & 0xff) * (ULong) along) >> 24);
		while (at->fArc <= want)
			at++;
		long length = at->fLength;
		long into = length - (at->fArc - want);
		long dx = at->dx;
		long dy = at->dy;
		long sideX = dx < 0 ? -dx : dx;
		long sideY = dy < 0 ? -dy : dy;
		long partX = 0;
		long partY = 0;
		if (length <= into)
		{
			into -= length;
			partY = sideY;
			partX = sideX;
		}
		for (long bit = 1; bit <= 10; bit++)
		{
			into *= 2;
			if (length <= into)
			{
				into -= length;
				partX += sideX >> bit;
				partY += sideY >> bit;
			}
		}
		sideX >>= 11;
		sideY >>= 11;
		do
		{
			into *= 2;
			if (length <= into)
			{
				into -= length;
				partX += sideX;
				partY += sideY;
			}
			into *= 2;
			if (length <= into)
			{
				into -= length;
				partX += sideX >> 1;
				partY += sideY >> 1;
			}
			sideX >>= 2;
			sideY >>= 2;
		} while (sideX != 0 || sideY != 0);
		if (dx < 1)
			partX = -partX;
		if (dy < 1)
			partY = -partY;
		out[i].x = at[-1].x + partX;
		out[i].y = at[-1].y + partY;
	}
	return ratio;
}


// ROM 0x00283d9c Tracing__FlP6_RPR_P
// The places measured along the curve they now sit on: the distance from
// each to the next, and how far along the whole thing that comes to.
// The first is nought and nought, and every other one is written as the
// *next* record's pair - so the walk runs one past the end (see the
// comment on CICEncoder::fSamples).
//
// The length is the same square root AddPointToOdata takes, with the
// room test made on twice the sum rather than the sum.
void
Tracing(long count, CICSample* samples)
{
	long arc = 0;
	samples[0].fStep = 0;
	samples[0].fAt = 0;
	for (long i = 0; i < count; i++)
	{
		long dx = samples[i + 1].x - samples[i].x;
		long dy = samples[i + 1].y - samples[i].y;
		if (dx < 0)
			dx = -dx;
		if (dy < 0)
			dy = -dy;
		long bigger = dy < dx ? dx : dy;
		ULong room = (ULong) ((dx + dy + bigger) * 2);
		ULong shift = 0;
		while (room > 0xffff)
		{
			dx >>= 1;
			dy >>= 1;
			shift++;
			room >>= 1;
		}
		long length = SQRT32((ULong) (dy * dy + dx * dx)) << shift;
		samples[i + 1].fStep = length;
		arc += length;
		samples[i + 1].fAt = arc;
	}
}


// ROM 0x002833c8 MSQError__FUsP6_RPR_PT2
// How far apart two sets of samples are: the squares of the distances,
// added up.
long
MSQError(ULong count, const CICSample* a, const CICSample* b)
{
	long total = 0;
	for (ULong i = 0; i < count; i++)
	{
		long dx = a[i].x - b[i].x;
		long dy = a[i].y - b[i].y;
		total += dy * dy + dx * dx;
	}
	return total;
}


/*------------------------------------------------------------------------------
	T h e   b o o k ,   a n d   r o u n d i n g
------------------------------------------------------------------------------*/

// ROM 0x00282758 QvantUN__FlT1
// A length divided by a step and rounded to the nearest whole one, a
// half going away from nought: twice the quotient, one added in the
// value's own direction, and halved again.
//
// (The first quotient is taken as a short, so a length more than
// thirty-two thousand steps away wraps - which is the ROM's, not this
// reconstruction's.)
long
QvantUN(long value, long step)
{
	short quotient = (short) ((value * 2) / step);
	quotient = (short) (value < 0 ? quotient - 1 : quotient + 1);
	return quotient / 2;
}


// ROM 0x0028294c EcdrSelectCodeBook__FP4_CDC
// The mirror of DcdrSelectCodeBook: the same two books, the same two
// lengths and the same step, and the same walk of eight tables.
Boolean
EcdrSelectCodeBook(CICEncoder* encoder)
{
	const char* book;
	long step;
	if (encoder->fBookNumber == 1)
	{
		book = (const char*) LockCodeBook(1);
		encoder->fLimitA = 0x8cc;
		encoder->fLimitB = 0xa00;
		step = 0x800;
	}
	else if (encoder->fBookNumber == 2 || encoder->fBookNumber == 3)
	{
		book = (const char*) LockCodeBook(2);
		encoder->fLimitA = 0x1d50;
		encoder->fLimitB = 0x10aa;
		step = 0x2000;
	}
	else
		return false;
	encoder->fUnit = 1024;
	encoder->fOne = 1;
	encoder->fStepA = step;
	encoder->fStep = step;
	encoder->fError2 = 0xf0bc10;
	encoder->fSlack = 30;
	encoder->fSlack2 = 30;
	if (book == nil)
		return false;
	for (long i = 0; i < 8; i++)
	{
		encoder->fTables[i] = book;
		book += CodeTableSize(book);
	}
	return true;
}


/*------------------------------------------------------------------------------
	W r i t i n g   a   s t r o k e
------------------------------------------------------------------------------*/

// ROM 0x00282d84 WriteNewStroke__FP4_CDCs
// A stroke's kind, and where it starts as a step from where the pen
// already is - the mirror of ReadNewStroke, and the place where the
// run's format is settled.
//
// After the first stroke the kind goes out as a word and the two steps
// through the first two tables of the book.  The first stroke carries
// the format instead.  In the newer one the kind word and four bits
// saying how wide the coordinates are go out together as one byte - the
// kind's code is four bits long, so the byte is the width's nibble above
// it - followed by the two coordinates at that width.  In the older one
// nine bits each go out after the kind, and if either of them comes out
// at 501 or more the pen is walked that far and the whole thing written
// again, because nine bits will not reach: a short stroke starting at
// (511, 511) followed by a 7 is the marker the reader knows that by.
//
// ROM bug kept: a first stroke of the newer format that starts exactly
// where the pen is - both steps nought - sets the width to eight but
// never sets the byte that says so, and writes whatever was in the
// register.  DEVIATION: the host cannot reproduce which value that is,
// so it writes the one the next case would have used, which is the one
// that agrees with the width.
Boolean
WriteNewStroke(CICEncoder* encoder, short kind)
{
	if (kind == kCICEndOfGroup)
		return EncodeWord_NEW(encoder, kCICEndOfGroup, kInkEncStrokeCodes);

	long dx = QvantUN(encoder->fTrace[0].x - encoder->fPenX, encoder->fStep);
	long dy = QvantUN(encoder->fTrace[0].y - encoder->fPenY, encoder->fStep);
	if (encoder->fFirst == 0)
	{
		if (!EncodeWord_NEW(encoder, kind, kInkEncStrokeCodes))
			return false;
		if (!EncodeWord_OLD(encoder, (short) dx, encoder->fTables[0]))
			return false;
		if (!EncodeWord_OLD(encoder, (short) dy, encoder->fTables[1]))
			return false;
	}
	else
	{
		encoder->fFirst = 0;
		if (encoder->fBookNumber == 2)
		{
			if (dx > 0x1ff)
				dx = 0x1ff;
			if (dy > 0x1ff)
				dy = 0x1ff;
			long far = dx < 0x1f5 ? dy : dx;
			Boolean ok = far < 0x1f5
					   ? EncodeWord_NEW(encoder, kind, kInkEncStrokeCodes)
					   : EncodeWord_NEW(encoder, kCICShortStroke, kInkEncStrokeCodes);
			if (!ok || !PutBits(encoder, (ULong) dx, 9) || !PutBits(encoder, (ULong) dy, 9))
				return false;
			far = dx < 0x1f5 ? dy : dx;
			if (far > 500)
			{
				if (!EncodeWord_NEW(encoder, 7, kInkEncFormatCodes))
					return false;
				encoder->fPenX += encoder->fStep * dx;
				encoder->fPenY += encoder->fStep * dy;
				return WriteNewStroke(encoder, kind);
			}
		}
		else
		{
			ULong width;
			ULong how;
			if (dx == 0 && dy == 0)
			{
				width = 8;
				how = 0x18;			// (the ROM writes whatever was in the register)
			}
			else if ((dx < 0x100 ? dy : dx) < 0x100)
			{
				width = 8;
				how = 0x18;
			}
			else if ((dx < 0x1000 ? dy : dx) < 0x1000)
			{
				width = 12;
				how = 0x28;
			}
			else
			{
				width = 16;
				how = 0x38;
			}
			if (encoder->fBookNumber == 3)
				how |= 0x40;
			if (!PutBits(encoder, how, 8))
				return false;
			if (!PutBits(encoder, (ULong) dx, width))
				return false;
			if (!PutBits(encoder, (ULong) dy, width))
				return false;
			EncodeWord_NEW(encoder, kind, kInkEncStrokeCodes);
		}
	}
	encoder->fPenX += encoder->fStep * dx;
	encoder->fPenY += encoder->fStep * dy;
	encoder->fLastX = encoder->fPenX;
	encoder->fLastY = encoder->fPenY;
	encoder->fStrokeX = encoder->fPenX;
	encoder->fStrokeY = encoder->fPenY;
	return true;
}


// ROM 0x00283240 WriteShortStroke__FP4_CDC
// The stroke's points as they were drawn: for each one after the first,
// a word saying another follows and then the two steps to it, through
// the third and fourth tables of the book.  A 7 ends the stroke.
Boolean
WriteShortStroke(CICEncoder* encoder)
{
	for (long i = 1; i < (long) encoder->fPointCount; i++)
	{
		if (!EncodeWord_NEW(encoder, 8, kInkEncFormatCodes))
			return false;
		long dx = QvantUN(encoder->fTrace[i].x - encoder->fPenX, encoder->fStepA);
		long dy = QvantUN(encoder->fTrace[i].y - encoder->fPenY, encoder->fStepA);
		if (!EncodeWord_OLD(encoder, (short) dx, encoder->fTables[2]))
			return false;
		if (!EncodeWord_OLD(encoder, (short) dy, encoder->fTables[3]))
			return false;
		encoder->fPenX += encoder->fStepA * dx;
		encoder->fPenY += encoder->fStepA * dy;
	}
	return EncodeWord_NEW(encoder, 7, kInkEncFormatCodes);
}


/*------------------------------------------------------------------------------
	F i t t i n g   a   s e g m e n t
------------------------------------------------------------------------------*/

// ROM 0x0027fde0 TestStrokeSeg__FP4_CDCUlUs
// The curve fitted to the trace, over and over until it settles.
//
// Each try finds where the nine places fall on the stroke (Repar), turns
// those into the four numbers (RFFT), puts the four back into the nine
// places on the new curve (RIFT), and then measures the nine against the
// stroke again (Tracing) so that the next try looks in better places.
//
// It stops when the curve is close enough - the places it found and the
// places the curve puts them agreeing to within the allowance, and the
// stroke not stretched more than about seventeen sixteenths - or when
// the curve stops moving, or when the stroke has become so short that
// there is nothing left to fit.  A stroke stretched past sixteen times
// is given up on.
//
// The ROM's Tracing writes one record past the nine, and that record is
// where the first sample's saved pair lives; so the walk over the saved
// pair is done here, where the ROM's memory does it by itself.
Boolean
TestStrokeSeg(CICEncoder* encoder, ULong allowance, ULong tries)
{
	CICSample* samples = encoder->fSamples;
	CICSample* resampled = encoder->fResampled;
	CICTracePoint* trace = encoder->fTrace;
	long last = (long) encoder->fPointCount - 1;
	long x0 = trace[0].x;
	long y0 = trace[0].y;
	long x1 = trace[last].x;
	long y1 = trace[last].y;
	for (ULong tried = 0; tried < tries; tried++)
	{
		encoder->fWasX[2] = encoder->fCoefX[2];
		encoder->fWasY[2] = encoder->fCoefY[2];
		encoder->fWasX[3] = encoder->fCoefX[3];
		encoder->fWasY[3] = encoder->fCoefY[3];
		long stretch = (long) Repar(&trace[last], trace, samples, resampled);
		RFFT_9_4_X(resampled, encoder->fCoefX, x0, x1);
		RIFT_4_9_X(samples, encoder->fCoefX);
		RFFT_9_4_Y(resampled, encoder->fCoefY, y0, y1);
		RIFT_4_9_Y(samples, encoder->fCoefY);
		if (stretch < 0x11a9561 && allowance != 0
			&& (ULong) MSQError(kCICSamples, samples, resampled) < allowance)
			return true;
		if (stretch > 0x10000000)
			return false;
		long moved = 0;
		for (long i = 2; i < 4; i++)
		{
			long dx = encoder->fCoefX[i] - encoder->fWasX[i];
			long dy = encoder->fCoefY[i] - encoder->fWasY[i];
			moved += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
		}
		encoder->fMoved = moved;
		if (moved < 0x50)
		{
			if (allowance != 0)
				return false;
			if (tried != 0)
				return false;
		}
		Tracing(kCICSamples, samples);
		encoder->fSavedSamples[0].fStep = samples[kCICSamples].fStep;
		encoder->fSavedSamples[0].fAt = samples[kCICSamples].fAt;
		if ((ULong) samples[8].fAt < 0x400)
		{
			ResetParam(encoder);
			return true;
		}
	}
	return false;
}


/*------------------------------------------------------------------------------
	A   s t r o k e ,   a   s e g m e n t   a t   a   t i m e
------------------------------------------------------------------------------*/

// (the source asked for one more point; the answer says what came)
static long
NextPoint(CICEncoder* encoder, InkPoint* pt)
{
	return encoder->fSource(kInkAskNext, pt, encoder->fRefCon);
}


// ROM 0x0027fffc WriteLongStroke__FP4_CDC
// A stroke written as a chain of curved segments.
//
// The trace is grown a point at a time while the fitting still says a
// curve will go through it; when it will not, the trace is put back to
// what it was at the last point that worked and the curve is fitted
// again with no allowance at all, which always answers.  That segment is
// written, the points after the one it ended on move down to the front
// of the trace, and the whole thing begins again.  The last segment of a
// stroke is written with a 7 rather than an 8.
Boolean
WriteLongStroke(CICEncoder* encoder)
{
	encoder->fCoefX[2] = 1000000;
	encoder->fCoefY[2] = 1000000;
	encoder->fCoefX[3] = 1000000;
	encoder->fCoefY[3] = 1000000;
	ResetParam(encoder);
	StoreContext(encoder);
	encoder->fEndOfStroke = 0;
	ULong had = 0;
	for (;;)
	{
		Boolean more = true;
		while (more)
		{
			if (!TestStrokeSeg(encoder, (ULong) encoder->fError2, encoder->fSlack))
				break;
			StoreContext(encoder);
			if (encoder->fError != 0)
				return false;
			if (encoder->fEndOfStroke != 0)
				break;
			long added;
			for (;;)
			{
				InkPoint pt;
				long what = NextPoint(encoder, &pt);
				if (what == kInkEndStroke)
				{
					encoder->fEndOfStroke = 1;
					goto done;
				}
				if (what == kInkEnd)
				{
					encoder->fDone = 1;
					encoder->fEndOfStroke = 1;
					goto done;
				}
				if (what != kInkPoint)
					break;					// nothing this time: fit again
				added = AddPointToOdata(encoder, &pt);
				if (added != 0)
				{
					if (added == -1)
						more = false;		// the trace is full
					break;
				}
			}
		}
	done:
		if (encoder->fError != 0)
			return false;
		if (encoder->fEndOfStroke == 0)
		{
			had = encoder->fPointCount;
			RestoreContext(encoder);
		}
		TestStrokeSeg(encoder, 0, encoder->fSlack2);
		if (encoder->fError != 0)
			return false;
		encoder->fStrokeX = encoder->fPenX;
		encoder->fStrokeY = encoder->fPenY;
		long ended = (long) encoder->fPointCount - 1;
		encoder->fLastX = encoder->fTrace[ended].x;
		encoder->fLastY = encoder->fTrace[ended].y;
		if (encoder->fEndOfStroke != 0)
			break;
		// the points from the one the segment ended on to the ones
		// already read move down to the front, and their distances
		// along are worked out again from there
		ULong to = 0;
		for (long from = ended; from < (long) had; from++)
		{
			encoder->fTrace[to].x = encoder->fTrace[from].x;
			encoder->fTrace[to].y = encoder->fTrace[from].y;
			if (to == 0)
			{
				encoder->fTrace[0].dx = 0;
				encoder->fTrace[0].dy = 0;
				encoder->fTrace[0].fLength = 0;
				encoder->fTrace[0].fArc = 0;
			}
			else
			{
				encoder->fTrace[to].dx = encoder->fTrace[from].dx;
				encoder->fTrace[to].dy = encoder->fTrace[from].dy;
				long length = encoder->fTrace[from].fLength;
				encoder->fTrace[to].fLength = length;
				encoder->fTrace[to].fArc = encoder->fTrace[to - 1].fArc + length;
			}
			to++;
		}
		encoder->fPointCount = to;
		while (encoder->fPointCount < 2)
		{
			InkPoint pt;
			long what = NextPoint(encoder, &pt);
			if (what == kInkEndStroke)
			{
				encoder->fEndOfStroke = 1;
				break;
			}
			if (what == kInkEnd)
			{
				encoder->fDone = 1;
				encoder->fEndOfStroke = 1;
				break;
			}
			if (what == kInkPoint)
				AddPointToOdata(encoder, &pt);
		}
		if (encoder->fPointCount <= 1)
			break;
		if (!WriteSegment(encoder, 8))
			return false;
		encoder->fCoefX[2] = 1000000;
		encoder->fCoefY[2] = 1000000;
		encoder->fCoefX[3] = 1000000;
		encoder->fCoefY[3] = 1000000;
		ResetParam(encoder);
	}
	return WriteSegment(encoder, 7);
}


/*------------------------------------------------------------------------------
	T h e   r u n
------------------------------------------------------------------------------*/

// ROM 0x0027f938 EncoderOpen__FUsUlT1T2T1
// A context made ready.  (The ROM allocates one; the host is given the
// room to work in, as with the decoder.)  The four numbers that bend a
// segment start at a million, which is far enough out that the first try
// of the fitting always counts as having moved.
Boolean
EncoderOpen(CICEncoder* encoder, InkPointSource source, void* refCon,
			UByte* out, long size, ULong book)
{
	memset(encoder, 0, sizeof(*encoder));
	encoder->fSource = source;
	encoder->fRefCon = refCon;
	encoder->fOut = out;
	encoder->fBitLimit = (ULong) size * 8;
	encoder->fFirst = 1;
	encoder->fCoefX[2] = 1000000;
	encoder->fCoefY[2] = 1000000;
	encoder->fCoefX[3] = 1000000;
	encoder->fCoefY[3] = 1000000;
	encoder->fBookNumber = book;
	return EcdrSelectCodeBook(encoder);
}


// ROM 0x002804f8 EncoderRun__FUl
// The source told to begin, and then stroke after stroke: points are
// pulled until there are two, and a stroke of two or more is written as
// a chain of segments.  A stroke of one is a dot, and goes out as a
// short stroke with nothing after its first point.  The run ends with
// the word that says so.
Boolean
EncoderRun(CICEncoder* encoder)
{
	if (encoder->fSource == nil)
	{
		encoder->fError = 7;
		return false;
	}
	InkPoint pt;
	if (encoder->fSource(kInkAskBegin, &pt, encoder->fRefCon) == 0)
	{
		encoder->fError = 12;
		return false;
	}
	encoder->fDone = 0;
	for (;;)
	{
		if (encoder->fDone != 0)
		{
			WriteNewStroke(encoder, kCICEndOfGroup);
			break;
		}
		encoder->fPointCount = 0;
		while (encoder->fPointCount < 2)
		{
			long what = NextPoint(encoder, &pt);
			if (what == kInkEndStroke)
			{
				encoder->fEndOfStroke = 1;
				break;
			}
			if (what == kInkEnd)
			{
				encoder->fDone = 1;
				encoder->fEndOfStroke = 1;
				break;
			}
			if (what == kInkPoint)
				AddPointToOdata(encoder, &pt);
		}
		if (encoder->fDone != 0)
		{
			WriteNewStroke(encoder, kCICEndOfGroup);
			break;
		}
		if (encoder->fPointCount == 0)
			continue;
		Boolean ok;
		if (encoder->fPointCount < 2)
			ok = WriteNewStroke(encoder, kCICShortStroke) && WriteShortStroke(encoder);
		else
			ok = WriteNewStroke(encoder, kCICLongStroke) && WriteLongStroke(encoder);
		if (!ok)
			break;
	}
	return encoder->fError == 0;
}

