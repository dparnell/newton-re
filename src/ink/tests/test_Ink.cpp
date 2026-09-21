// Ink test: the three classes of ink binary told apart, the eight bytes
// an ink word carries about itself packed, read back, opened out and
// changed, and the codec boundary - the format in a block of ink, the
// bit reader, and the two code books, which are ROM objects.

#include "Ink.h"
#include "CICCodec.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "FixedMath.h"
#include "Ports.h"		// ToFixed, RoundFixed
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// An ink word of `data` bytes of (made-up) stroke data with its eight
// bytes of information after them.
static Ref
MakeInkWord(long data, ULong width, ULong ascent, ULong descent, ULong xHeight,
			Fixed scale, ULong face, ULong penSize)
{
	RefVar ink(AllocateBinary(RSSYMinkword, data + (long) sizeof(PackedInkWordInfo)));
	memset(BinaryData(ink), 0x5a, (size_t) data);
	PackedInkWordInfo packed;
	PackInkWordInfo(&packed, width, ascent, descent, xHeight, scale, face, penSize);
	SetPackedInkWordInfo(ink, &packed);
	return ink;
}


static void
TestClasses()
{
	RefVar word(MakeInkWord(4, 40, 12, 4, 7, 0x10000, 0, 1));
	RefVar raw(AllocateBinary(RSSYMink2, 8));
	RefVar old(AllocateBinary(RSSYMink, 8));
	RefVar other(AllocateBinary(RSSYMstring, 8));

	EXPECT(IsInk(word) && IsInk(raw) && IsInk(old) && !IsInk(other));
	EXPECT(!IsRawInk(word) && IsRawInk(raw) && IsRawInk(old));
	EXPECT(!IsOldRawInk(word) && !IsOldRawInk(raw) && IsOldRawInk(old));
	EXPECT(IsInkWord(word) && !IsInkWord(raw) && !IsInkWord(old));
	// a frame is not ink, whatever its class
	EXPECT(!IsInk(RefVar(AllocateFrame())));
}


static void
TestFaces()
{
	// bold, italic, underline and outline stay where they are; the two
	// script bits come down to 0x10 and 0x20 and go back up again
	for (ULong face = 0; face < 16; face++)
		EXPECT(GetQDFace(GetRawFace(face)) == face);
	EXPECT(GetRawFace(0x80) == 0x10 && GetQDFace(0x10) == 0x80);
	EXPECT(GetRawFace(0x100) == 0x20 && GetQDFace(0x20) == 0x100);
	EXPECT(GetRawFace(0x183) == 0x33 && GetQDFace(0x33) == 0x183);
}


static void
TestSizes()
{
	// the font size is seven quarters of the x-height, rounded
	EXPECT(GetInkWordFontSize(0) == 0);
	EXPECT(GetInkWordFontSize(4) == 7);
	EXPECT(GetInkWordFontSize(7) == 12);		// 12.25
	EXPECT(GetInkWordFontSize(8) == 14);
	// the pen is one pixel up to ten, then forty over the size and two
	EXPECT(GetStdInkWordPenWidth(9) == 1 && GetStdInkWordPenWidth(10) == 1);
	EXPECT(GetStdInkWordPenWidth(11) == 5);
	EXPECT(GetStdInkWordPenWidth(20) == 4);
	EXPECT(GetStdInkWordPenWidth(41) == 2);
}


static void
TestPacking()
{
	PackedInkWordInfo packed;
	PackInkWordInfo(&packed, 100, 20, 6, 9, 0x18000, 0x101, 3);
	InkWordInfo info;
	ExpandPackedInkWordInfo(&packed, &info);
	EXPECT(info.fWidth == 100 && info.fAscent == 20 && info.fDescent == 6);
	EXPECT(info.fXHeight == 9 && info.fPenSize == 3);
	EXPECT(info.fFace == 0x101);
	EXPECT(info.fScale == 0x18000);			// a scale and a half, to eight fractional bits
	// what follows: the font size the x-height comes to, that size
	// scaled, and the pen that wants
	EXPECT(info.fFontSize == GetInkWordFontSize(9));
	EXPECT(info.fScaledFontSize == RoundFixed(FixedMultiply(ToFixed(info.fFontSize), 0x18000)));
	EXPECT(info.fPenWidth == GetStdInkWordPenWidth((ULong) info.fScaledFontSize));
	EXPECT(info.fScaledWidth == info.fPenWidth + 150);
	EXPECT(info.fScaledAscent == info.fPenWidth + 30);
	EXPECT(info.fScaledDescent == 9);
	EXPECT(info.fScaledHeight == info.fPenWidth + 39);
	EXPECT(info.fScaledXHeight == RoundFixed(FixedMultiply(ToFixed(9), 0x18000)));

	// the scale keeps only eight fractional bits
	PackInkWordInfo(&packed, 1, 1, 1, 1, 0x123ff, 0, 1);
	ExpandPackedInkWordInfo(&packed, &info);
	EXPECT(info.fScale == 0x12300);
}


static void
TestInkWord()
{
	RefVar ink(MakeInkWord(16, 100, 20, 6, 9, 0x10000, 0, 1));
	EXPECT(Length(ink) == 16 + (long) sizeof(PackedInkWordInfo));
	InkWordInfo info;
	GetInkWordInfo(ink, &info);
	EXPECT(info.fWidth == 100 && info.fAscent == 20 && info.fXHeight == 9 && info.fPenSize == 1);

	// the stroke data in front of the eight bytes is left alone
	SetInkWordPenSize(ink, 4);
	SetInkWordFontFace(ink, 0x83);
	SetInkWordScale(ink, 0x20000);
	const unsigned char* data = (const unsigned char*) BinaryData(ink);
	for (long i = 0; i < 16; i++)
		EXPECT(data[i] == 0x5a);
	GetInkWordInfo(ink, &info);
	EXPECT(info.fPenSize == 4 && info.fFace == 0x83 && info.fScale == 0x20000);
	EXPECT(info.fWidth == 100 && info.fAscent == 20 && info.fDescent == 6 && info.fXHeight == 9);

	// a font size is kept as the scale that gets there from the size the
	// x-height comes to
	SetInkWordScale(ink, 0x10000);
	SetInkWordFontSize(ink, 32);
	GetInkWordInfo(ink, &info);
	EXPECT(info.fScaledFontSize == 32);
}


static void
TestXHeight()
{
	// a word of letters: the x-height is left alone unless the word is
	// more than four times as tall as it is wide
	RefVar wide(MakeInkWord(4, 100, 20, 6, 9, 0x10000, 0, 1));
	AdjustInkWordXHeight(wide, false);
	InkWordInfo info;
	GetInkWordInfo(wide, &info);
	EXPECT(info.fXHeight == 9);

	RefVar narrow(MakeInkWord(4, 5, 20, 6, 9, 0x10000, 0, 1));
	AdjustInkWordXHeight(narrow, false);
	GetInkWordInfo(narrow, &info);
	EXPECT(info.fXHeight == (ULong) RoundFixed(FixedMultiply(0x6666, ToFixed(20))));	// two fifths of the ascent

	// numbers: an x-height of more than three fifths of the ascent, with
	// almost nothing below the baseline, is the recogniser having found
	// no ascenders or descenders to measure against
	RefVar digits(MakeInkWord(4, 40, 20, 1, 18, 0x10000, 0, 1));
	AdjustInkWordXHeight(digits, true);
	GetInkWordInfo(digits, &info);
	EXPECT(info.fXHeight == (ULong) RoundFixed(FixedMultiply(0x8ccd, ToFixed(20))));	// 0.55 of it

	// but a word with a real descender is left alone
	RefVar descender(MakeInkWord(4, 40, 20, 8, 18, 0x10000, 0, 1));
	AdjustInkWordXHeight(descender, true);
	GetInkWordInfo(descender, &info);
	EXPECT(info.fXHeight == 18);
}



// The codec boundary: the format in a block of ink, and which codec
// answers for it.
static void
TestCodecs()
{
	// the low nibble is 8 in every form the codec writes
	unsigned char data[4];
	data[0] = 0x00; EXPECT(GetInkFormat(data) == kInkFormatOld);
	data[0] = 0x17; EXPECT(GetInkFormat(data) == kInkFormatOld);
	data[0] = 0x08; EXPECT(GetInkFormat(data) == kInkFormatCompressed);
	data[0] = 0x88; EXPECT(GetInkFormat(data) == kInkFormatHigh);
	data[0] = 0x48; EXPECT(GetInkFormat(data) == kInkFormatWide);
	data[0] = 0xc8; EXPECT(GetInkFormat(data) == kInkFormatHigh);		// bit 7 first

	InitializeInkCodecs();
	EXPECT(CountInkCodecs() == 1);
	EXPECT(IndexedInkCodec(0) == &gCICInkCodec);
	InitializeInkCodecs();		// registering twice does not add it twice
	EXPECT(CountInkCodecs() == 1);
	data[0] = 0x08;
	EXPECT(InkCodecFor(data) == &gCICInkCodec);
	data[0] = 0x00;
	EXPECT(InkCodecFor(data) == nil);		// nobody reads the old ink yet
	EXPECT(InkCodecForWriting() == nil);	// and nobody writes any
}


// The bit reader: bytes are read from the least significant bit up, and
// the bits of a value come out in that order too.
static void
TestBitReader()
{
	const UByte bits[] = { 0x4d, 0x80, 0xff };		// 0100 1101, ...
	CICDecoder decoder;
	decoder.fData = bits;
	decoder.fBitPos = 0;
	decoder.fStrokeBits = 0;
	decoder.fTotalBits = 0;
	EXPECT(GetNBit(&decoder, 1) == 1);		// 0x4d bit 0
	EXPECT(GetNBit(&decoder, 1) == 0);
	EXPECT(GetNBit(&decoder, 2) == 3);		// bits 2 and 3, least first
	EXPECT(GetNBit(&decoder, 4) == 4);		// bits 4..7: 0,0,1,0 -> 0b0100
	EXPECT(decoder.fBitPos == 8 && decoder.fStrokeBits == 8 && decoder.fTotalBits == 8);
	// across a byte boundary
	EXPECT(GetNBit(&decoder, 8) == 0x80);
	EXPECT(GetNBit(&decoder, 0) == 0);
	EXPECT(decoder.fBitPos == 16);
	// and a whole word of ones
	EXPECT(GetNBit(&decoder, 8) == 0xff);
	EXPECT(decoder.fTotalBits == 24);
}


// The code books: two binaries in the ROM, put in place by
// InitializeParagraphCompression and handed out by number.
static void
TestCodeBooks()
{
	EXPECT(gCodeBook == nil && gInkCodeBook == nil);
	InitializeParagraphCompression();
	EXPECT(gCodeBook != nil && gInkCodeBook != nil);
	EXPECT(gCodeBook != gInkCodeBook);
	EXPECT(gCodeBook == BinaryData(RefVar(Rparagraphcodebook1)));
	EXPECT(gInkCodeBook == BinaryData(RefVar(Rparagraphcodebook2)));

	// opened by number, and counted while it is open
	EXPECT(CodeBookUseCount(1) == 0);
	EXPECT(LockCodeBook(1) == gCodeBook);
	EXPECT(CodeBookUseCount(1) == 1);
	EXPECT(LockCodeBook(1) == gCodeBook);
	EXPECT(CodeBookUseCount(1) == 2);
	UnlockCodeBook(1);
	EXPECT(CodeBookUseCount(1) == 1);
	UnlockCodeBook(1);
	UnlockCodeBook(1);		// one too many is not an error, and does not go below nought
	EXPECT(CodeBookUseCount(1) == 0);
	EXPECT(LockCodeBook(2) == gInkCodeBook && CodeBookUseCount(2) == 1);
	UnlockCodeBook(2);
	// and a number that is no book at all comes back as itself
	EXPECT(LockCodeBook(7) == (void*) 7 && UnlockCodeBook(7));
}


// A bit stream written the way the codec reads one: least significant
// bit of each byte first, and the bits of a value in that order too.
struct BitWriter
{
	UByte	fBytes[256];
	ULong	fPos;
};

static void
StartBits(BitWriter* w)
{
	memset(w->fBytes, 0, sizeof(w->fBytes));
	w->fPos = 0;
}

static void
PutBits(BitWriter* w, ULong value, ULong n)
{
	for (ULong i = 0; i < n; i++)
	{
		if ((value & (1UL << i)) != 0)
			w->fBytes[w->fPos >> 3] = (UByte) (w->fBytes[w->fPos >> 3] | (1 << (w->fPos & 7)));
		w->fPos++;
	}
}

static void
OpenOver(CICDecoder* d, const BitWriter* w)
{
	memset(d, 0, sizeof(*d));
	d->fData = w->fBytes;
	d->fBitCount = w->fPos;
}


// The entropy decoder over the two static tables and over a real one out
// of the ROM's code book.
static void
TestDecodeWord()
{
	// kInkStrokeCodes: one bit set is a long stroke, "01" a short one,
	// "0001" the end of the group
	BitWriter w;
	StartBits(&w);
	PutBits(&w, 1, 1);
	PutBits(&w, 2, 2);
	PutBits(&w, 8, 4);
	CICDecoder d;
	OpenOver(&d, &w);
	short value = -1;
	EXPECT(DecodeWord_NEW(&d, kInkStrokeCodes, &value) && value == kCICLongStroke);
	EXPECT(DecodeWord_NEW(&d, kInkStrokeCodes, &value) && value == kCICShortStroke);
	EXPECT(DecodeWord_NEW(&d, kInkStrokeCodes, &value) && value == kCICEndOfGroup);
	EXPECT(d.fBitPos == 7 && d.fTotalBits == 7);
	// and the stream runs out
	EXPECT(!DecodeWord_NEW(&d, kInkStrokeCodes, &value) && d.fError != 0);

	// kInkFormatCodes: a single bit, 0 for 7 and 1 for 8
	StartBits(&w);
	PutBits(&w, 0, 1);
	PutBits(&w, 1, 1);
	OpenOver(&d, &w);
	EXPECT(DecodeWord_NEW(&d, kInkFormatCodes, &value) && value == 7);
	EXPECT(DecodeWord_NEW(&d, kInkFormatCodes, &value) && value == 8);

	// bits that are no code at all: nothing but zeros never matches, and
	StartBits(&w);		// the walk gives up when the table runs out
	PutBits(&w, 0, 12);
	OpenOver(&d, &w);
	EXPECT(!DecodeWord_NEW(&d, kInkStrokeCodes, &value) && value == 0);
	EXPECT(d.fBitPos == 4);		// four bits is as far as the table goes
}


// A table out of the ROM's own code book, read with the codes the table
// itself gives.
static void
TestCodeBookTables()
{
	CICDecoder d;
	memset(&d, 0, sizeof(d));
	d.fBookNumber = 1;
	EXPECT(DcdrSelectCodeBook(&d));
	EXPECT(d.fScale == 0x800 && d.fUnit == 1024 && d.fOne == 1);
	EXPECT(d.fTables[0] == gCodeBook);
	// the eight tables lie one after another and fill the book exactly
	long total = 0;
	for (long i = 0; i < 8; i++)
	{
		EXPECT(d.fTables[i] == (const char*) gCodeBook + total);
		total += (long) CodeTableSize(d.fTables[i]);
	}
	EXPECT(total == Length(RefVar(Rparagraphcodebook1)));
	// each table's size is its header and its entries
	for (long i = 0; i < 8; i++)
		EXPECT((long) CodeTableSize(d.fTables[i])
			   == kCodeTableHeaderSize + (long) CodeTableCount(d.fTables[i]) * kCodeTableEntrySize);
	// the ink book too, with the other step
	memset(&d, 0, sizeof(d));
	d.fBookNumber = 2;
	EXPECT(DcdrSelectCodeBook(&d) && d.fScale == 0x2000 && d.fTables[0] == gInkCodeBook);
	memset(&d, 0, sizeof(d));
	d.fBookNumber = 3;
	EXPECT(DcdrSelectCodeBook(&d) && d.fScale == 0x2000 && d.fTables[0] == gInkCodeBook);

	// the first few entries of a table, fed back to the decoder as the
	// bits they say they are
	memset(&d, 0, sizeof(d));
	d.fBookNumber = 1;
	EXPECT(DcdrSelectCodeBook(&d));
	const void* table = d.fTables[0];
	long tried = 0;
	for (long i = 0; i < (long) CodeTableCount(table) && tried < 8; i++)
	{
		short expect = CodeTableValue(table, i);
		if (expect == CodeTableEscapeUp(table) || expect == CodeTableEscapeDown(table))
			continue;				// an escape wants another word after it
		BitWriter w;
		StartBits(&w);
		PutBits(&w, CodeTableCode(table, i), CodeTableLength(table, i));
		CICDecoder one;
		OpenOver(&one, &w);
		short got = 0;
		EXPECT(DecodeWord_OLD(&one, table, &got));
		EXPECT(got == expect);
		EXPECT(one.fBitPos == CodeTableLength(table, i));
		tried++;
	}
	EXPECT(tried == 8);

	// an escape: the code for 30000 followed by the code for a small
	// value gives that value on top of the table's upward base
	long escape = -1;
	long small = -1;
	for (long i = 0; i < (long) CodeTableCount(table); i++)
	{
		if (escape < 0 && CodeTableValue(table, i) == CodeTableEscapeUp(table))
			escape = i;
		if (small < 0 && CodeTableValue(table, i) == 0)
			small = i;
	}
	EXPECT(escape >= 0 && small >= 0);
	if (escape >= 0 && small >= 0)
	{
		BitWriter w;
		StartBits(&w);
		PutBits(&w, CodeTableCode(table, escape), CodeTableLength(table, escape));
		PutBits(&w, CodeTableCode(table, small), CodeTableLength(table, small));
		CICDecoder one;
		OpenOver(&one, &w);
		short got = 0;
		EXPECT(DecodeWord_OLD(&one, table, &got));
		EXPECT(got == CodeTableBaseUp(table));
	}
}


// The code for a value out of one of a book's tables.
static Boolean
PutValue(BitWriter* w, const void* table, short value)
{
	for (long i = 0; i < (long) CodeTableCount(table); i++)
		if (CodeTableValue(table, i) == value)
		{
			PutBits(w, CodeTableCode(table, i), CodeTableLength(table, i));
			return true;
		}
	return false;
}


// Where the points went.
static InkPoint	gPoints[64];
static long		gPointCount = 0;
static void*	gLastRefCon = nil;

static short
CollectPoint(short what, const InkPoint* pt, void* refCon)
{
	gLastRefCon = refCon;
	if (what == kInkPoint && gPointCount < 64)
		gPoints[gPointCount++] = *pt;
	return 1;
}


// A short stroke: the point it starts at and a step in x and y for each
// point after it, ended by a 7 out of the format table.
static void
TestShortStroke()
{
	CICDecoder d;
	memset(&d, 0, sizeof(d));
	d.fBookNumber = 1;
	EXPECT(DcdrSelectCodeBook(&d));

	BitWriter w;
	StartBits(&w);
	// two more points: (+3, -2) and (+1, +4), then the end
	PutBits(&w, 1, 1);							// the format table's 8: another point
	EXPECT(PutValue(&w, d.fTables[2], 3));
	EXPECT(PutValue(&w, d.fTables[3], -2));
	PutBits(&w, 1, 1);
	EXPECT(PutValue(&w, d.fTables[2], 1));
	EXPECT(PutValue(&w, d.fTables[3], 4));
	PutBits(&w, 0, 1);							// the format table's 7: the end
	d.fData = w.fBytes;
	d.fBitCount = w.fPos;
	d.fBitPos = 0;
	d.fX = 100 << 10;			// the codec counts in thousand-and-twenty-fourths
	d.fY = 200 << 10;

	EXPECT(ReadShortStroke(&d));
	EXPECT(d.fPointCount == 3);
	EXPECT(d.fPointsX[0] == (100 << 10) && d.fPointsY[0] == (200 << 10));
	EXPECT(d.fPointsX[1] == (100 << 10) + 0x800 * 3);
	EXPECT(d.fPointsY[1] == (200 << 10) - 0x800 * 2);
	EXPECT(d.fPointsX[2] == d.fPointsX[1] + 0x800);
	EXPECT(d.fPointsY[2] == d.fPointsY[1] + 0x800 * 4);

	// and the same stroke handed to a sink, in whole tablet units
	d.fBitPos = 0;
	d.fX = 100 << 10;
	d.fY = 200 << 10;
	d.fSink = CollectPoint;
	d.fRefCon = (void*) &w;
	gPointCount = 0;
	EXPECT(DecodeShortStroke(&d));
	EXPECT(gPointCount == 3);
	EXPECT(gLastRefCon == (void*) &w);
	EXPECT(gPoints[0].x == 100 && gPoints[0].y == 200);
	EXPECT(gPoints[1].x == 100 + 6 && gPoints[1].y == 200 - 4);		// a step is two units
	EXPECT(gPoints[2].x == 100 + 8 && gPoints[2].y == 200 + 4);

	// a stroke that never ends runs the stream out
	StartBits(&w);
	for (long i = 0; i < 4; i++)
	{
		PutBits(&w, 1, 1);
		PutValue(&w, d.fTables[2], 1);
		PutValue(&w, d.fTables[3], 1);
	}
	d.fData = w.fBytes;
	d.fBitCount = w.fPos;
	d.fBitPos = 0;
	d.fError = 0;
	EXPECT(!ReadShortStroke(&d) && d.fError != 0);
}


// A segment of a long stroke: where it ends and the four numbers that
// bend it, and the seventeen points they come to.
static void
TestSegment()
{
	CICDecoder d;
	memset(&d, 0, sizeof(d));
	d.fBookNumber = 1;
	EXPECT(DcdrSelectCodeBook(&d));

	BitWriter w;
	StartBits(&w);
	EXPECT(PutValue(&w, d.fTables[2], 10));		// the chord: ten steps right,
	EXPECT(PutValue(&w, d.fTables[3], 4));		// four down
	EXPECT(PutValue(&w, d.fTables[4], 0));		// and no bend at all
	EXPECT(PutValue(&w, d.fTables[5], 0));
	EXPECT(PutValue(&w, d.fTables[6], 0));
	EXPECT(PutValue(&w, d.fTables[7], 0));
	PutBits(&w, 0, 1);							// the format table's 7: the last segment
	d.fData = w.fBytes;
	d.fBitCount = w.fPos;
	d.fX = 0;
	d.fY = 0;

	short tag = -1;
	EXPECT(ReadSegmentNear(&d, &tag));
	EXPECT(tag == 7);
	EXPECT(d.fSegStartX == 0 && d.fSegStartY == 0);
	EXPECT(d.fSegEndX == 0x800 * 10 && d.fSegEndY == 0x800 * 4);
	EXPECT(d.fSegX[2] == 0 && d.fSegX[3] == 0);
	EXPECT(d.fSegX[0] == (d.fSegStartX + d.fSegEndX) / 2);
	EXPECT(d.fSegX[1] == (d.fSegStartX - d.fSegEndX) / 2);

	// with nothing bending it the seventeen points are a straight line
	// from where the segment starts to where it ends
	long px[kCICSegmentPoints];
	long py[kCICSegmentPoints];
	RestoreSegment(px, d.fSegX);
	RestoreSegment(py, d.fSegY);
	EXPECT(px[kCICSegmentMiddle] == (d.fSegStartX + d.fSegEndX) / 2);
	EXPECT(px[0] == d.fSegStartX && px[kCICSegmentPoints - 1] == d.fSegEndX);
	EXPECT(py[0] == d.fSegStartY && py[kCICSegmentPoints - 1] == d.fSegEndY);
	Boolean even = true;
	long stepX = px[1] - px[0];
	for (long i = 1; i < kCICSegmentPoints; i++)
		if (px[i] - px[i - 1] != stepX)
			even = false;
	EXPECT(even && stepX == (d.fSegEndX - d.fSegStartX) / 16);

	// a bend pulls the middle off the chord, and the ends stay put
	d.fSegX[2] = 1000 << 10;
	d.fSegX[0] = ((d.fSegStartX + d.fSegEndX) >> 1) - d.fSegX[2];
	RestoreSegment(px, d.fSegX);
	EXPECT(px[0] == d.fSegStartX && px[kCICSegmentPoints - 1] == d.fSegEndX);
	EXPECT(px[kCICSegmentMiddle] != (d.fSegStartX + d.fSegEndX) / 2);
}


static long gBegins = 0;
static long gEnds = 0;
static long gStrokeEnds = 0;

static short
CollectAll(short what, const InkPoint* pt, void* refCon)
{
	gLastRefCon = refCon;
	if (what == kInkBegin)
		gBegins++;
	else if (what == kInkEnd)
		gEnds++;
	else if (what == kInkEndStroke)
		gStrokeEnds++;
	else if (what == kInkPoint && gPointCount < 64)
		gPoints[gPointCount++] = *pt;
	return 1;
}


// A whole block of ink read from end to end: the newer format's header,
// one long stroke of one straight segment, and the word that ends the
// group.
static void
TestDecodeRun()
{
	InitializeParagraphCompression();
	CICDecoder d;
	memset(&d, 0, sizeof(d));
	d.fBookNumber = 1;
	EXPECT(DcdrSelectCodeBook(&d));

	BitWriter w;
	StartBits(&w);
	PutBits(&w, 8, 4);			// the stroke table's 2: the newer format's marker
	PutBits(&w, 1, 4);			// eight-bit coordinates, and the writing book
	PutBits(&w, 20, 8);			// where the first stroke starts
	PutBits(&w, 30, 8);
	PutBits(&w, 1, 1);			// the stroke table's 0: a long stroke
	EXPECT(PutValue(&w, d.fTables[2], 10));		// the segment's chord
	EXPECT(PutValue(&w, d.fTables[3], 4));
	EXPECT(PutValue(&w, d.fTables[4], 0));		// and no bend
	EXPECT(PutValue(&w, d.fTables[5], 0));
	EXPECT(PutValue(&w, d.fTables[6], 0));
	EXPECT(PutValue(&w, d.fTables[7], 0));
	PutBits(&w, 0, 1);			// the format table's 7: the stroke's last segment
	PutBits(&w, 8, 4);			// the stroke table's 2: the end of the group
	long size = (long) ((w.fPos + 7) / 8);

	gPointCount = 0;
	gBegins = gEnds = gStrokeEnds = 0;
	EXPECT(gCICInkCodec.Decode(w.fBytes, size, 1, CollectAll, (void*) &w));
	EXPECT(gBegins == 1 && gEnds == 1 && gStrokeEnds == 1);
	EXPECT(gLastRefCon == (void*) &w);
	// the pen starts where the header said, in tablet units - a step of
	// the writing book is worth two of them
	EXPECT(gPointCount >= 5 && gPointCount <= 17);
	EXPECT(gPoints[0].x == 40 && gPoints[0].y == 60);
	EXPECT(gPoints[gPointCount - 1].x == 60 && gPoints[gPointCount - 1].y == 68);
	Boolean rising = true;
	for (long i = 1; i < gPointCount; i++)
		if (gPoints[i].x < gPoints[i - 1].x || gPoints[i].y < gPoints[i - 1].y)
			rising = false;
	EXPECT(rising);

	// the same block read the other way about: the cell thinner leaves
	// fewer points, and the stroke still starts and ends where it did
	long handThinned = gPointCount;
	gPointCount = 0;
	gBegins = gEnds = gStrokeEnds = 0;
	EXPECT(gCICInkCodec.Decode(w.fBytes, size, 0, CollectAll, (void*) &w));
	EXPECT(gBegins == 1 && gEnds == 1 && gStrokeEnds == 1);
	EXPECT(gPointCount > 0 && gPointCount <= handThinned);
	// the thinner keeps whichever point of a cell is nearest its middle,
	// so the first one out is near where the stroke starts rather than on
	// it - but the last is the stroke's end, which is let out whole
	EXPECT(gPoints[0].x >= 40 && gPoints[0].x <= 47);
	EXPECT(gPoints[0].y >= 60 && gPoints[0].y <= 63);
	EXPECT(gPoints[gPointCount - 1].x == 60 && gPoints[gPointCount - 1].y == 68);

	// a block that stops in the middle is not a run
	gPointCount = 0;
	gBegins = gEnds = gStrokeEnds = 0;
	EXPECT(!gCICInkCodec.Decode(w.fBytes, 2, 1, CollectAll, (void*) &w));
	EXPECT(gBegins == 1 && gEnds == 1);
}


// The thinner on its own: points crowded into one cell come out as one,
// and a staircase loses its middle.
static void
TestSkipPoints()
{
	CICSkipPoints skip;
	memset(&skip, 0, sizeof(skip));
	skip.fStarted = 1;
	skip.fIndex = -1;
	skip.fHalfCell = 4;
	skip.fCellSize = 8;

	// three points in the cell at (0, 0): nothing comes out, and the one
	// kept is the one nearest the cell's middle at (4, 4)
	EXPECT(!GetSkipPoint(&skip, 0, 0));
	EXPECT(!GetSkipPoint(&skip, 3, 3));
	EXPECT(!GetSkipPoint(&skip, 7, 7));
	EXPECT(skip.fPoints[0].x == 3 && skip.fPoints[0].y == 3);

	// three cells are held, so it is the fourth that pushes the first out
	EXPECT(!GetSkipPoint(&skip, 40, 40));
	EXPECT(!GetSkipPoint(&skip, 80, 80));
	EXPECT(GetSkipPoint(&skip, 120, 120));
	EXPECT(skip.fOut.x == 3 && skip.fOut.y == 3);
	// and what is left drains, oldest first
	EXPECT(ClearSkipPoint(&skip) && skip.fOut.x == 40 && skip.fOut.y == 40);
	EXPECT(ClearSkipPoint(&skip) && skip.fOut.x == 80 && skip.fOut.y == 80);
	EXPECT(ClearSkipPoint(&skip) && skip.fOut.x == 120 && skip.fOut.y == 120);
	EXPECT(!ClearSkipPoint(&skip));

	// a staircase: cells (0,0), (1,0) and (1,1) step once in x and once
	// in y, so when the fourth cell arrives the middle of the three is
	// thrown away instead of being let out
	memset(&skip, 0, sizeof(skip));
	skip.fStarted = 1;
	skip.fIndex = -1;
	EXPECT(!GetSkipPoint(&skip, 4, 4));			// cell (0, 0)
	EXPECT(!GetSkipPoint(&skip, 12, 4));		// cell (1, 0)
	EXPECT(!GetSkipPoint(&skip, 12, 12));		// cell (1, 1)
	EXPECT(GetSkipPoint(&skip, 44, 44));		// cell (5, 5)
	EXPECT(skip.fOut.x == 4 && skip.fOut.y == 4);
	EXPECT(skip.fIndex == 1);					// the middle was thrown away
	EXPECT(skip.fPoints[0].x == 12 && skip.fPoints[0].y == 12);
	EXPECT(skip.fPoints[1].x == 44 && skip.fPoints[1].y == 44);
	// three cells that are not a staircase keep all three
	memset(&skip, 0, sizeof(skip));
	skip.fStarted = 1;
	skip.fIndex = -1;
	EXPECT(!GetSkipPoint(&skip, 4, 4));			// cell (0, 0)
	EXPECT(!GetSkipPoint(&skip, 12, 4));		// cell (1, 0)
	EXPECT(!GetSkipPoint(&skip, 20, 4));		// cell (2, 0): straight on
	EXPECT(GetSkipPoint(&skip, 44, 44));
	EXPECT(skip.fIndex == 2 && skip.fPoints[0].x == 12);
}

int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_IMAGE) != noErr)
	{
		printf("test_Ink: cannot import %s\n", NEWTON_ROM_IMAGE);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();

	TestClasses();
	TestFaces();
	TestSizes();
	TestPacking();
	TestInkWord();
	TestXHeight();
	TestCodecs();
	TestBitReader();
	TestCodeBooks();
	TestDecodeWord();
	TestCodeBookTables();
	TestShortStroke();
	TestSegment();
	TestSkipPoints();
	TestDecodeRun();

	if (failures == 0)
		printf("test_Ink: all passed\n");
	else
		printf("test_Ink: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
