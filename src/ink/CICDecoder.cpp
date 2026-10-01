/*
	File:		ink/CICDecoder.cpp

	Contains:	The ROM's ink decoder.  See CICCodec.h.
*/

#include "CICCodec.h"
#include "ByteOrder.h"
#include "NewtonMemory.h"

#include <string.h>


// ROM 0x00280d88 GetNBit__FP4_DCCUs
// The next n bits of the stream.  A byte is read from its least
// significant bit upwards, and the bits of the answer are filled in the
// same order, so the first bit read is the answer's bit 0.  Two counts
// are kept of how much has been read - one the whole run's and one the
// stroke's - and both are stepped here.
//
// DEVIATION: the ROM reads on past the end of the block, because on a
// Newton the bytes after it are simply more memory; a host cannot.  The
// read stops at the end and sets the error halfword, which is the one
// the callers already test after every run of bits.
ULong
GetNBit(CICDecoder* decoder, ULong n)
{
	ULong value = 0;
	for (ULong i = 0; i < n; i++)
	{
		ULong at = decoder->fBitPos;
		if (at >= decoder->fBitCount)
		{
			decoder->fError = 1;
			break;
		}
		value |= (ULong) ((decoder->fData[at >> 3] >> (at & 7)) & 1) << i;
		decoder->fBitPos = at + 1;
	}
	decoder->fStrokeBits += n;
	decoder->fTotalBits += n;
	return value;
}


/*------------------------------------------------------------------------------
	T h e   c o d e   b o o k ' s   t a b l e s
------------------------------------------------------------------------------*/

// A code book is a block of big-endian halfwords in the ROM, so every
// field is read out rather than cast to.
static inline unsigned short
BookHalf(const void* table, long offset)
{
	return GetBigEndianHalf((const char*) table + offset);
}

ULong	CodeTableSize(const void* t)			{ return BookHalf(t, 0); }
ULong	CodeTableCount(const void* t)			{ return BookHalf(t, 2); }
short	CodeTableBaseDown(const void* t)		{ return (short) BookHalf(t, 4); }
short	CodeTableBaseUp(const void* t)			{ return (short) BookHalf(t, 6); }
short	CodeTableEscapeUp(const void* t)		{ return (short) BookHalf(t, 8); }
short	CodeTableEscapeDown(const void* t)		{ return (short) BookHalf(t, 10); }

short
CodeTableValue(const void* t, long i)
{
	return (short) BookHalf(t, kCodeTableHeaderSize + i * kCodeTableEntrySize);
}

ULong
CodeTableLength(const void* t, long i)
{
	return BookHalf(t, kCodeTableHeaderSize + i * kCodeTableEntrySize + 2);
}

ULong
CodeTableCode(const void* t, long i)
{
	const char* at = (const char*) t + kCodeTableHeaderSize + i * kCodeTableEntrySize + 4;
	return ((ULong) GetBigEndianHalf(at) << 16) | GetBigEndianHalf(at + 2);
}


// ROM 0x00280df0 DcdrSelectCodeBook__FP4_DCC
// The book the run is to use, and what a step in it is worth.  Book 1 is
// the writing one and books 2 and 3 the ink one; the two of them differ
// only in the two lengths and the step, book 3 being read with the same
// tables as book 2.
//
// The eight tables lie one after another, each starting with its own
// size, so finding them is a walk.
//
// ROM oddity kept: a book number that is none of the three leaves the
// book pointer at the context itself, and the walk then reads the
// context as though it were a book.  Nothing asks for such a number.
Boolean
DcdrSelectCodeBook(CICDecoder* decoder)
{
	const char* book;
	long step;
	if (decoder->fBookNumber == 1)
	{
		book = (const char*) LockCodeBook(1);
		decoder->fUnit = 1024;
		decoder->fOne = 1;
		decoder->fLimitA = 0x8cc;
		decoder->fLimitB = 0xa00;
		step = 0x800;
	}
	else if (decoder->fBookNumber == 2 || decoder->fBookNumber == 3)
	{
		book = (const char*) LockCodeBook(2);
		decoder->fUnit = 1024;
		decoder->fOne = 1;
		decoder->fLimitA = 0x1d50;
		decoder->fLimitB = 0x10aa;
		step = 0x2000;
	}
	else
		return decoder->fTables[0] != nil;		// (the ROM walks the context itself)
	decoder->fScaleA = step;
	decoder->fScale = step;
	if (book == nil)
		return false;
	for (long i = 0; i < 8; i++)
	{
		decoder->fTables[i] = book;
		book += CodeTableSize(book);
	}
	return true;
}


/*------------------------------------------------------------------------------
	T h e   e n t r o p y   d e c o d e r
------------------------------------------------------------------------------*/

// ROM 0x00281b90 DecodeWord_NEW__FP4_DCCP9_CODEWORDPs
// A word out of a static table: a bit at a time, and after each bit the
// entries whose codes are that long are tried in turn.  A zero length
// is the end of the table, and means the bits were not a code at all.
//
// (The ROM reads its bits here rather than calling GetNBit, but counts
// them the same way.)
Boolean
DecodeWord_NEW(CICDecoder* decoder, const unsigned short* table, short* out)
{
	ULong length = 0;
	ULong code = 0;
	long entry = 0;
	for (;;)
	{
		code |= GetNBit(decoder, 1) << length;
		length = (length + 1) & 0xffff;
		if (decoder->fError != 0)
		{
			*out = 0;
			return false;
		}
		while (CodeWordLength(table, entry) == length)
		{
			if (code == CodeWordCode(table, entry))
			{
				*out = CodeWordValue(table, entry);
				return true;
			}
			entry++;
			if (CodeWordLength(table, entry) == 0)
			{
				*out = 0;
				return false;
			}
		}
	}
}


// ROM 0x00281c48 DecodeWord_OLD__FP4_DCCP10_CODETABLEPs
// A word out of one of a code book's tables, the same way - except that
// two of the values are escapes: one means the answer was too big to
// hold and the rest of it follows as another word to be added to the
// table's upward base, and the other the same downwards.  The escape is
// read again for the second test, so a value that is both escapes at
// once takes the second.
//
// The walk gives up after the table's own count of entries, or after
// thirty-two bits, whichever comes first.
Boolean
DecodeWord_OLD(CICDecoder* decoder, const void* table, short* out)
{
	long entry = 0;
	ULong length = 1;
	ULong bit = 0;
	ULong code = 0;
	long index = 0;
	long count = (long) CodeTableCount(table);
	for (;;)
	{
		code |= GetNBit(decoder, 1) << bit;
		if (decoder->fError != 0)
			return false;
		for (; CodeTableLength(table, entry) == length; entry++)
		{
			if (code == CodeTableCode(table, entry))
			{
				short value = CodeTableValue(table, entry);
				long answer = value;
				if (answer == CodeTableEscapeDown(table))
				{
					short more;
					if (!DecodeWord_OLD(decoder, table, &more))
						return false;
					answer = (short) (CodeTableBaseDown(table) + more);
				}
				short result = (short) answer;
				if (value == CodeTableEscapeUp(table))
				{
					short more;
					if (!DecodeWord_OLD(decoder, table, &more))
						return false;
					result = (short) (CodeTableBaseUp(table) + more);
				}
				*out = result;
				return true;
			}
			if (index == (short) (count - 1))
				return false;
			index = (short) (index + 1);
		}
		length = (short) (length + 1);
		bit = (short) (bit + 1);
		if ((long) bit > 31)
			return false;
	}
}


/*------------------------------------------------------------------------------
	T h e   s t r o k e   h e a d e r
------------------------------------------------------------------------------*/

// ROM 0x00280f1c ReadNewStroke__FP4_DCCPs
// The next stroke's kind, and the pen moved to where it starts.
//
// The first stroke of a run carries the format with it.  The kind read
// first is normally 0 (a long stroke) or 1 (a short one), and 2 ends the
// group - but on the *first* stroke a 2 means something else: it is the
// marker for the newer format, and four bits follow saying how wide the
// starting coordinates are (nothing, eight, twelve or sixteen bits) and
// which book to read with (1 or 3).  The older format has no marker at
// all: book 2, and nine bits each.
//
// The older format has one more trick.  A first stroke that is short and
// starts at (511, 511) - both coordinates all ones, which is no place at
// all - is a marker too: a word out of the format table must follow and
// be 7, and then the real first stroke comes, with its start decoded out
// of the book like any other.
//
// Every start is a step in the book's units, and is added to where the
// pen already is.
Boolean
ReadNewStroke(CICDecoder* decoder, short* outKind)
{
	short x = 0;
	short y = 0;
	if (decoder->fFirst == 0)
	{
		if (!DecodeWord_NEW(decoder, kInkStrokeCodes, outKind))
			return false;
		if (*outKind == kCICEndOfGroup)
			return true;
		if (!DecodeWord_OLD(decoder, decoder->fTables[0], &x))
			return false;
		if (!DecodeWord_OLD(decoder, decoder->fTables[1], &y))
			return false;
	}
	else
	{
		decoder->fFirst = 0;
		if (!DecodeWord_NEW(decoder, kInkStrokeCodes, outKind))
			return false;
		if (*outKind == kCICEndOfGroup)
		{
			// the newer format
			ULong how = GetNBit(decoder, 4);
			ULong width = 0;
			switch (how & 3)
			{
			case 0:		width = 0;	break;
			case 1:		width = 8;	break;
			case 2:		width = 12;	break;
			default:	width = 16;	break;
			}
			decoder->fBookNumber = (how & 4) != 0 ? 3 : 1;
			if (!DcdrSelectCodeBook(decoder))
				return false;
			x = (short) GetNBit(decoder, width);
			y = (short) GetNBit(decoder, width);
			if (decoder->fError != 0)
				return false;
			if (!DecodeWord_NEW(decoder, kInkStrokeCodes, outKind))
				return false;
			if (*outKind == kCICEndOfGroup)
				return false;
		}
		else
		{
			// the older format
			decoder->fBookNumber = 2;
			if (!DcdrSelectCodeBook(decoder))
				return false;
			x = (short) GetNBit(decoder, 9);
			y = (short) GetNBit(decoder, 9);
			if (decoder->fError != 0)
				return false;
			if (*outKind == kCICShortStroke && (x == 0x1ff || y == 0x1ff))
			{
				if (!DecodeWord_NEW(decoder, kInkFormatCodes, outKind))
					return false;
				if (*outKind != 7)
					return false;
				if (!DecodeWord_NEW(decoder, kInkStrokeCodes, outKind))
					return false;
				if (*outKind == kCICEndOfGroup)
					return false;
				decoder->fX += decoder->fScale * x;
				decoder->fY += decoder->fScale * y;
				if (!DecodeWord_OLD(decoder, decoder->fTables[0], &x))
					return false;
				if (!DecodeWord_OLD(decoder, decoder->fTables[1], &y))
					return false;
			}
		}
	}
	decoder->fX += decoder->fScale * x;
	decoder->fY += decoder->fScale * y;
	return true;
}






/*------------------------------------------------------------------------------
	A   l o n g   s t r o k e ' s   s e g m e n t s
------------------------------------------------------------------------------*/

// ROM 0x00281240 ReadSegmentNear__FP4_DCCPs
// A long stroke is written as a chain of curved segments.  Each one
// carries where it ends - a step in x and y, out of the same two tables
// a short stroke's steps come from - and then four numbers that bend it,
// two out of the fifth and sixth tables and two out of the seventh and
// eighth, each worth one of the two lengths the book was opened with.
//
// Those four are turned into the segment's own numbers here: the middle
// of the chord less the first pair, and half the chord less the second.
// A word out of the format table follows and says whether the stroke
// goes on (8) or this was its last segment (7).
Boolean
ReadSegmentNear(CICDecoder* decoder, short* outTag)
{
	decoder->fSegStartX = decoder->fX;
	decoder->fSegStartY = decoder->fY;
	short dx, dy;
	if (!DecodeWord_OLD(decoder, decoder->fTables[2], &dx))
		return false;
	if (!DecodeWord_OLD(decoder, decoder->fTables[3], &dy))
		return false;
	decoder->fX += decoder->fScaleA * dx;
	decoder->fSegEndX = decoder->fX;
	decoder->fY += decoder->fScaleA * dy;
	decoder->fSegEndY = decoder->fY;

	short a, b;
	if (!DecodeWord_OLD(decoder, decoder->fTables[4], &a))
		return false;
	if (!DecodeWord_OLD(decoder, decoder->fTables[5], &b))
		return false;
	decoder->fSegX[2] = decoder->fLimitA * a;
	decoder->fSegY[2] = decoder->fLimitA * b;
	if (!DecodeWord_OLD(decoder, decoder->fTables[6], &a))
		return false;
	if (!DecodeWord_OLD(decoder, decoder->fTables[7], &b))
		return false;
	decoder->fSegX[3] = decoder->fLimitB * a;
	decoder->fSegY[3] = decoder->fLimitB * b;

	decoder->fSegX[0] = ((decoder->fSegStartX + decoder->fSegEndX) >> 1) - decoder->fSegX[2];
	decoder->fSegY[0] = ((decoder->fSegStartY + decoder->fSegEndY) >> 1) - decoder->fSegY[2];
	decoder->fSegX[1] = ((decoder->fSegStartX - decoder->fSegEndX) >> 1) - decoder->fSegX[3];
	decoder->fSegY[1] = ((decoder->fSegStartY - decoder->fSegEndY) >> 1) - decoder->fSegY[3];
	return DecodeWord_NEW(decoder, kInkFormatCodes, outTag);
}


// ROM 0x00281a70 RestoreSegment__FPlT1
// A segment's four numbers drawn out into seventeen points by forward
// differences: the middle one first, then eight forward and eight back,
// each got from the one before by adding a step, and each step from the
// one before by adding a second difference that itself grows by a fixed
// amount.  The two halves differ only in the sign of that growth and in
// the step they start from, so the curve is symmetrical about its
// middle.  With both of the bending numbers nought it comes out a
// straight line from where the segment starts to where it ends.
//
// The points are in the thousand-and-twenty-fourths the codec counts in,
// which is what the shift of six at the end brings them back to.
void
RestoreSegment(long* points, const long* seg)
{
	long first = seg[2] >> 10;
	long second = seg[3] >> 10;
	long value = (long) ((ULong) ((seg[0] >> 10) - first) << 16);
	long step = first * 0x800 + (second * 3 - (seg[1] >> 10)) * 0x2000 - second * 0x200;
	long growth = (long) ((ULong) first << 12);
	points[kCICSegmentMiddle] = value >> 6;
	for (long i = 1; i < 9; i++)
	{
		growth -= second * 0xc00;
		value += step;
		step += growth;
		points[kCICSegmentMiddle + i] = value >> 6;
	}
	first = seg[2] >> 10;
	second = seg[3] >> 10;
	value = (long) ((ULong) ((seg[0] >> 10) - first) << 16);
	step = first * 0x800 + (seg[1] >> 10) * 0x2000 - second * 0x6000 + second * 0x200;
	growth = (long) ((ULong) first << 12);
	for (long i = 1; i < 9; i++)
	{
		growth += second * 0xc00;
		value += step;
		step += growth;
		points[kCICSegmentMiddle - i] = value >> 6;
	}
}


/*------------------------------------------------------------------------------
	A   s h o r t   s t r o k e
------------------------------------------------------------------------------*/

// ROM 0x00281424 ReadShortStroke__FP4_DCC
// A short stroke is written out as it was drawn: the point it starts at
// (where ReadNewStroke has already left the pen), and then a step in x
// and a step in y for each point after it, until a word out of the
// format table comes back 7.  Both steps are read out of the third and
// fourth tables of the book and are worth the book's step each.
//
// DEVIATION: the ROM writes past the end of its two buffers if a stroke
// has more points than they hold; the host stops taking them.
Boolean
ReadShortStroke(CICDecoder* decoder)
{
	decoder->fPointsX[0] = decoder->fX;
	decoder->fPointsY[0] = decoder->fY;
	decoder->fPointCount = 1;
	for (;;)
	{
		short tag;
		if (!DecodeWord_NEW(decoder, kInkFormatCodes, &tag))
			return false;
		if (tag == 7)
			return true;
		short dx, dy;
		if (!DecodeWord_OLD(decoder, decoder->fTables[2], &dx))
			return false;
		if (!DecodeWord_OLD(decoder, decoder->fTables[3], &dy))
			return false;
		decoder->fX += decoder->fScaleA * dx;
		decoder->fY += decoder->fScaleA * dy;
		if (decoder->fPointCount >= (ULong) kCICMaxPoints)
			continue;
		decoder->fPointsX[decoder->fPointCount] = decoder->fX;
		decoder->fPointsY[decoder->fPointCount] = decoder->fY;
		decoder->fPointCount++;
	}
}


// ROM 0x002820c8 DecodeShortStroke__FP4_DCC
// The points handed to the sink, each brought down from the thousand
// and twenty-fourths the codec counts in to whole tablet units.
Boolean
DecodeShortStroke(CICDecoder* decoder)
{
	if (!ReadShortStroke(decoder))
		return false;
	for (ULong i = 0; i < decoder->fPointCount; i++)
	{
		InkPoint pt;
		pt.x = (short) (decoder->fPointsX[i] >> 10);
		pt.y = (short) (decoder->fPointsY[i] >> 10);
		if (decoder->fSink != nil)
			decoder->fSink(kInkPoint, &pt, decoder->fRefCon);	// (the ROM hands the sink its context)
	}
	return true;
}




/*------------------------------------------------------------------------------
	T h e   p o i n t   t h i n n e r
------------------------------------------------------------------------------*/

static inline long
CICAbs(long n)
{
	return n < 0 ? -n : n;
}


// ROM 0x0028153c GetSkipPoint__FP7tag_SKPsT2
// A point offered to the thinner.  The plane is cut into cells eight
// units square: of all the points falling in one cell only the one
// nearest its middle is kept, so a stroke that dawdles comes out as one
// point rather than a cluster.
//
// Three cells are held back before anything comes out, because of the
// one shape worth undoing: three cells in a row where the second is one
// step from the first and the third one step from the second, both in x
// and in y, is a staircase across the diagonal, and the middle of it is
// thrown away.  Anything else lets the oldest of the three out.
//
// (The four and the eight are kept in the context and then hard-coded
// here, which is the ROM's doing, not this reconstruction's.)
Boolean
GetSkipPoint(CICSkipPoints* skip, short x, short y)
{
	if (skip->fStarted == 0)
	{
		skip->fCount = 1;
		skip->fOut.x = x;
		skip->fOut.y = y;
		return true;
	}
	skip->fCount = 0;
	skip->fIn.x = x;
	skip->fIn.y = y;
	skip->fCell.x = (short) (x >> 3);
	skip->fCell.y = (short) (y >> 3);
	long dx = (skip->fCell.x * 8 + 4) - x;
	long dy = (skip->fCell.y * 8 + 4) - y;
	skip->fDist = dy * dy + dx * dx;

	long index = skip->fIndex;
	if (index == -1)
	{
		skip->fIndex = 0;
		skip->fPoints[0] = skip->fIn;
		skip->fCells[0] = skip->fCell;
		skip->fBest = skip->fDist;
		return false;
	}
	if (skip->fCells[index].x != skip->fCell.x || skip->fCells[index].y != skip->fCell.y)
	{
		skip->fIndex = (short) (index + 1);
		if (skip->fIndex == 3)
		{
			skip->fOut = skip->fPoints[0];
			skip->fCount = 1;
			if (CICAbs(skip->fCells[1].x - skip->fCells[0].x)
					+ CICAbs(skip->fCells[2].x - skip->fCells[1].x) == 1
				&& CICAbs(skip->fCells[1].y - skip->fCells[0].y)
					+ CICAbs(skip->fCells[2].y - skip->fCells[1].y) == 1)
			{
				// a staircase: the middle one goes
				skip->fPoints[0] = skip->fPoints[2];
				skip->fCells[0] = skip->fCells[2];
				skip->fIndex = 1;
			}
			else
			{
				skip->fPoints[0] = skip->fPoints[1];
				skip->fCells[0] = skip->fCells[1];
				skip->fPoints[1] = skip->fPoints[2];
				skip->fCells[1] = skip->fCells[2];
				skip->fIndex = 2;
			}
		}
		skip->fPoints[skip->fIndex] = skip->fIn;
		skip->fCells[skip->fIndex] = skip->fCell;
	}
	else
	{
		// the same cell again: whichever point is nearer its middle wins
		if (skip->fBest <= skip->fDist)
			return skip->fCount != 0;
		skip->fPoints[index] = skip->fIn;
	}
	skip->fBest = skip->fDist;
	return skip->fCount != 0;
}


// ROM 0x002819a0 ClearSkipPoint__FP7tag_SKP
// The next point still held, at the end of a stroke: the oldest comes
// out and the rest move up.
Boolean
ClearSkipPoint(CICSkipPoints* skip)
{
	if (skip->fStarted == 0)
		return false;
	if (skip->fIndex == -1)
		skip->fCount = 0;
	else
	{
		skip->fCount = 1;
		skip->fOut = skip->fPoints[0];
		for (long i = 0; i < skip->fIndex; i++)
			skip->fPoints[i] = skip->fPoints[i + 1];
		skip->fIndex = (short) (skip->fIndex - 1);
	}
	return skip->fCount != 0;
}


/*------------------------------------------------------------------------------
	A   l o n g   s t r o k e
------------------------------------------------------------------------------*/

// ROM 0x00281dd0 DecodeLongStroke__FP4_DCC
// Segment after segment, each drawn out into seventeen points of which
// the first sixteen go to the sink - the seventeenth is the next
// segment's first.  The very first of the sixteen is not the segment's
// own point but the average of it and where the segment starts, which is
// what joins one segment smoothly to the last.
//
// In mode 1 the points are thinned by hand: one within a unit of the
// last one let through, in both x and y, is dropped.  Otherwise they go
// through GetSkipPoint, which thins them by cells and can undo a
// staircase.  Either way the comparison starts each segment from
// (-1000, -1000), which is no place at all, so the first point always
// goes out.
//
// When the segment was the stroke's last its end point goes out too, and
// then whatever the thinner is still holding.
Boolean
DecodeLongStroke(CICDecoder* decoder)
{
	for (;;)
	{
		short tag;
		if (!ReadSegmentNear(decoder, &tag))
			return false;
		RestoreSegment(decoder->fPointsX, decoder->fSegX);
		RestoreSegment(decoder->fPointsY, decoder->fSegY);
		InkPoint last;
		last.x = -1000;
		last.y = -1000;
		for (long i = 0; i < 16; i++)
		{
			InkPoint pt;
			if (i == 0)
			{
				pt.x = (short) ((decoder->fPointsX[0] + decoder->fSegStartX) >> 11);
				pt.y = (short) ((decoder->fPointsY[0] + decoder->fSegStartY) >> 11);
			}
			else
			{
				pt.x = (short) (decoder->fPointsX[i] >> 10);
				pt.y = (short) (decoder->fPointsY[i] >> 10);
			}
			if (decoder->fMode == 1)
			{
				if (CICAbs(pt.x - last.x) < 2 && CICAbs(pt.y - last.y) < 2)
					continue;
			}
			else
			{
				if (!GetSkipPoint(&decoder->fSkip, pt.x, pt.y))
					continue;
				pt = decoder->fSkip.fOut;
			}
			last = pt;
			if (decoder->fSink != nil)
				decoder->fSink(kInkPoint, &pt, decoder->fRefCon);
		}
		if (tag != 7)
			continue;
		InkPoint end;
		end.x = (short) (decoder->fSegEndX >> 10);
		end.y = (short) (decoder->fSegEndY >> 10);
		if (decoder->fMode == 3)
		{
			if (GetSkipPoint(&decoder->fSkip, end.x, end.y))
			{
				end = decoder->fSkip.fOut;
				if (decoder->fSink != nil)
					decoder->fSink(kInkPoint, &end, decoder->fRefCon);
			}
			while (ClearSkipPoint(&decoder->fSkip))
			{
				end = decoder->fSkip.fOut;
				if (decoder->fSink != nil)
					decoder->fSink(kInkPoint, &end, decoder->fRefCon);
			}
		}
		else if (decoder->fSink != nil)
			decoder->fSink(kInkPoint, &end, decoder->fRefCon);
		return true;
	}
}


/*------------------------------------------------------------------------------
	T h e   r u n
------------------------------------------------------------------------------*/

// ROM 0x0028240c DecoderOpen__FUsUlT1T2T1
// A context made ready.  (The ROM allocates one and hands back a
// handle; the host is given the room to work in, the context never
// leaving the codec.)
void
DecoderOpen(CICDecoder* decoder, const void* data, long size,
			InkPointProc sink, void* refCon, ULong mode)
{
	memset(decoder, 0, sizeof(*decoder));
	decoder->fSink = sink;
	decoder->fRefCon = refCon;
	decoder->fData = (const UByte*) data;
	decoder->fBitCount = (ULong) size * 8;
	decoder->fMode = mode;
	decoder->fFirst = 1;
}


// ROM 0x00282518 DecoderRun__FUl
// The sink told the run has begun, then stroke after stroke until one
// says the group has ended, each followed by the end-of-stroke word.
Boolean
DecoderRun(CICDecoder* decoder)
{
	if (decoder->fSink != nil && decoder->fSink(kInkBegin, nil, decoder->fRefCon) == 0)
		return false;
	decoder->fSkip.fStarted = 1;
	decoder->fSkip.fCount = 0;
	decoder->fSkip.fIndex = -1;
	decoder->fSkip.fHalfCell = 4;
	decoder->fSkip.fCellSize = 8;
	short kind;
	while (ReadNewStroke(decoder, &kind))
	{
		if (kind == kCICEndOfGroup)
			return true;
		if (kind == kCICLongStroke && !DecodeLongStroke(decoder))
			break;
		if (kind == kCICShortStroke && !DecodeShortStroke(decoder))
			break;
		if (decoder->fSink != nil)
			decoder->fSink(kInkEndStroke, nil, decoder->fRefCon);
	}
	return false;
}


// ROM 0x002826a0 DecoderClose__FUlPl
// The sink told the group is over and the code book let go of.  (The
// ROM's second argument is told how many points its default sink stored;
// its one caller, Decode, passes nil - and the host has no default sink,
// Decode replacing it before any use as the ROM's does.)
Boolean
DecoderClose(CICDecoder* decoder)
{
	Boolean ok = false;
	if (decoder->fSink != nil)
		ok = decoder->fSink(kInkEnd, nil, decoder->fRefCon) != 0;
	if (decoder->fBookNumber == 1)
		UnlockCodeBook(1);
	if (decoder->fBookNumber == 3 || decoder->fBookNumber == 2)
		UnlockCodeBook(2);
	return ok;
}


/*------------------------------------------------------------------------------
	T h e   c o d e c
------------------------------------------------------------------------------*/

TCICInkCodec	gCICInkCodec;


const char*
TCICInkCodec::Name(void) const
{
	return "CIC";
}


// Every format GetInkFormat tells apart: the three the newer header
// marks, and the older format (2) - book 2 with no header, which
// ReadNewStroke reads when the first stroke word is not the end of a group
// (and which InkConvert writes for 'ink).
Boolean
TCICInkCodec::CanDecode(long format) const
{
	return format == kInkFormatCompressed || format == kInkFormatHigh || format == kInkFormatWide
		|| format == kInkFormatOld;
}


Boolean
TCICInkCodec::CanEncode(void) const
{
	return true;
}


// ROM 0x001539c8 Decode__FP14CSStrokeHeaderUsPvPFsP6_POINTP4_DCC_s
// The ROM's Decode, which is what CSExpandGroup and CSDraw reach the
// codec through: a context opened over the block, run, and the sink
// told when it is over.  The group is a mode rather than an index - 1
// thins the points by hand and anything else by cells.  What it answers
// is what the sink said to the end of the group: the ROM pays no heed to
// whether the run itself got to the end (kept).
Boolean
TCICInkCodec::Decode(const void* data, long size, ULong group,
					 InkPointProc sink, void* refCon) const
{
	CICDecoder decoder;
	DecoderOpen(&decoder, data, size, sink, refCon, group == 1 ? 1 : 3);
	DecoderRun(&decoder);
	return DecoderClose(&decoder);
}


// ROM 0x0015362c GenericCSCompress__FPP7TStrokeUs
// The ROM's compressor: a context opened over a buffer four bytes a
// point and a hundred more, run, and what it wrote copied out.  The
// ROM's buffer is a handle it frees; the host's is a plain block.
void*
TCICInkCodec::Encode(InkPointSource source, void* refCon, long* outSize) const
{
	if (outSize != nil)
		*outSize = 0;
	if (source == nil)
		return nil;
	const long kRoom = kCICMaxTracePoints * 4 + 100;
	UByte* bits = (UByte*) NewPtrClear(kRoom);
	if (bits == nil)
		return nil;
	CICEncoder encoder;
	if (!EncoderOpen(&encoder, source, refCon, bits, kRoom, 1))
	{
		DisposPtr((Ptr) bits);
		return nil;
	}
	Boolean ok = EncoderRun(&encoder);
	long bitCount;
	EncoderClose(&encoder, &bitCount);
	if (!ok)
	{
		DisposPtr((Ptr) bits);
		return nil;
	}
	long size = (bitCount + 7) / 8;
	void* ink = NewPtr(size);
	if (ink == nil)
	{
		DisposPtr((Ptr) bits);
		return nil;
	}
	BlockMove(bits, ink, size);
	DisposPtr((Ptr) bits);
	if (outSize != nil)
		*outSize = size;
	return ink;
}


// (host: the ROM has no such call - it has one codec and reaches it by
// name.  A host program makes the register say what it holds.)
void
InitializeInkCodecs(void)
{
	RegisterInkCodec(&gCICInkCodec);
}
