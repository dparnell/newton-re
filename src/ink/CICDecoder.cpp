/*
	File:		ink/CICDecoder.cpp

	Contains:	The ROM's ink decoder.  See CICCodec.h.
*/

#include "CICCodec.h"
#include "ByteOrder.h"


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
	T h e   c o d e c
------------------------------------------------------------------------------*/

TCICInkCodec	gCICInkCodec;


const char*
TCICInkCodec::Name(void) const
{
	return "CIC";
}


// The three formats the ROM's encoder writes.  The old uncompressed ink
// is somebody else's.
Boolean
TCICInkCodec::CanDecode(long format) const
{
	return format == kInkFormatCompressed || format == kInkFormatHigh || format == kInkFormatWide;
}


Boolean
TCICInkCodec::CanEncode(void) const
{
	return false;		// NOT YET: EncoderRun and what it calls
}


Boolean
TCICInkCodec::Decode(const void* /*data*/, long /*size*/, ULong /*group*/,
					 InkPointProc /*sink*/, void* /*refCon*/) const
{
	// NOT YET: DecoderRun (0x00282518) - ReadNewStroke, DecodeLongStroke
	// and DecodeShortStroke over the code books
	return false;
}


void*
TCICInkCodec::Encode(TStroke** /*strokes*/, long* outSize) const
{
	if (outSize != nil)
		*outSize = 0;
	return nil;
}


// (host: the ROM has no such call - it has one codec and reaches it by
// name.  A host program makes the register say what it holds.)
void
InitializeInkCodecs(void)
{
	RegisterInkCodec(&gCICInkCodec);
}
