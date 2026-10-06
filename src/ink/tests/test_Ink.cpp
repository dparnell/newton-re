// Ink test: the three classes of ink binary told apart, the eight bytes
// an ink word carries about itself packed, read back, opened out and
// changed, and the codec boundary - the format in a block of ink, the
// bit reader, and the two code books, which are ROM objects.

#include "Ink.h"
#include "CICCodec.h"
#include "ROMImport.h"
#include "NewtonMemory.h"
#include "Stroke.h"
#include "Ports.h"
#include "Draw.h"
#include "Regions.h"
#include "InkShapes.h"
#include "StrokeBundle.h"
#include "InkFont.h"
#include "Fonts.h"
#include "Text.h"
#include "PicPlay.h"
#include "PicRecord.h"
#include "ROMConstants.h"
#include "DrawShape.h"
#include "Rects.h"
#include "ROMConstants.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"
#include "FixedMath.h"
#include "Ports.h"		// ToFixed, RoundFixed
#include "memory/host/KernelHeap.h"

#include "host/RomBugs.h"
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
	// the low nibble is 8 in every form the codec's newer header marks
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
	EXPECT(InkCodecFor(data) == &gCICInkCodec);		// the older format, code book 2, is the codec's too
	EXPECT(InkCodecForWriting() == &gCICInkCodec);	// and it is what writes it too
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

	// a block that stops in the middle: the run gives up, but Decode
	// answers what the sink said to the end of the group (DecoderClose),
	// as the ROM's does - it pays no heed to the run's own result
	gPointCount = 0;
	gBegins = gEnds = gStrokeEnds = 0;
	EXPECT(gCICInkCodec.Decode(w.fBytes, 2, 1, CollectAll, (void*) &w));
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


// The bit writer and the word encoders, against the readers.
static void
TestEncodeWord()
{
	// what PutBits writes, GetNBit reads back
	UByte bytes[64];
	memset(bytes, 0xa5, sizeof(bytes));		// (the writer only disturbs the bits it writes)
	CICEncoder e;
	memset(&e, 0, sizeof(e));
	e.fOut = bytes;
	e.fBitLimit = sizeof(bytes) * 8;
	static const ULong kValues[] = { 1, 0, 3, 0x55, 0x1ff, 0xffff, 7, 0x3ffff };
	static const ULong kWidths[] = { 1, 1, 2,    8,     9,     16, 3,     18 };
	for (long i = 0; i < 8; i++)
		EXPECT(PutBits(&e, kValues[i], kWidths[i]));
	EXPECT(e.fBitPos == 1 + 1 + 2 + 8 + 9 + 16 + 3 + 18);
	EXPECT(e.fHighWater == e.fBitPos);
	CICDecoder d;
	memset(&d, 0, sizeof(d));
	d.fData = bytes;
	d.fBitCount = e.fBitPos;
	for (long i = 0; i < 8; i++)
		EXPECT(GetNBit(&d, kWidths[i]) == kValues[i]);
	EXPECT(d.fError == 0);

	// and it will not write past the end
	memset(&e, 0, sizeof(e));
	e.fOut = bytes;
	e.fBitLimit = 10;
	EXPECT(PutBits(&e, 0x3ff, 10));
	EXPECT(!PutBits(&e, 1, 1) && e.fError == kCICNoRoom);
	e.fOut = nil;
	EXPECT(!PutBits(&e, 1, 1) && e.fError == kCICNoBuffer);

	// a value through a book's table and back
	InitializeParagraphCompression();
	CICDecoder book;
	memset(&book, 0, sizeof(book));
	book.fBookNumber = 1;
	EXPECT(DcdrSelectCodeBook(&book));
	const void* table = book.fTables[0];
	EXPECT(CodeTableBaseUp(table) == 307 && CodeTableBaseDown(table) == -802);
	static const short kWords[] = { 0, 1, -1, 5, -5, 306, -801, 307, -802, 500, -1000, 2000, -3000 };
	for (long i = 0; i < 13; i++)
	{
		memset(&e, 0, sizeof(e));
		e.fOut = bytes;
		e.fBitLimit = sizeof(bytes) * 8;
		EXPECT(EncodeWord_OLD(&e, kWords[i], table));
		memset(&d, 0, sizeof(d));
		d.fData = bytes;
		d.fBitCount = e.fBitPos;
		short got = 0;
		EXPECT(DecodeWord_OLD(&d, table, &got));
		EXPECT(got == kWords[i]);
		EXPECT(d.fBitPos == e.fBitPos);
	}

	// and through the static tables
	static const short kKinds[] = { kCICLongStroke, kCICShortStroke, kCICEndOfGroup };
	for (long i = 0; i < 3; i++)
	{
		memset(&e, 0, sizeof(e));
		e.fOut = bytes;
		e.fBitLimit = sizeof(bytes) * 8;
		EXPECT(EncodeWord_NEW(&e, kKinds[i], kInkStrokeCodes));
		memset(&d, 0, sizeof(d));
		d.fData = bytes;
		d.fBitCount = e.fBitPos;
		short got = -1;
		EXPECT(DecodeWord_NEW(&d, kInkStrokeCodes, &got) && got == kKinds[i]);
	}
	memset(&e, 0, sizeof(e));
	e.fOut = bytes;
	e.fBitLimit = sizeof(bytes) * 8;
	EXPECT(!EncodeWord_NEW(&e, 99, kInkStrokeCodes));		// no such word
	EXPECT(EncodeWord_NEW(&e, 7, kInkFormatCodes));
	EXPECT(EncodeWord_NEW(&e, 8, kInkFormatCodes));
}


// A stroke written by the encoder and read back by the decoder.
static void
TestEncodeStroke()
{
	InitializeParagraphCompression();
	UByte bytes[256];
	memset(bytes, 0, sizeof(bytes));
	CICEncoder e;
	memset(&e, 0, sizeof(e));
	e.fOut = bytes;
	e.fBitLimit = sizeof(bytes) * 8;
	e.fBookNumber = 1;
	e.fFirst = 1;
	EXPECT(EcdrSelectCodeBook(&e));
	EXPECT(e.fStep == 0x800 && e.fTables[0] == gCodeBook);

	// a stroke of three points.  The writing book's step is two tablet
	// units, so only even ones come back exactly.
	static const short kX[] = { 40, 46, 52 };
	static const short kY[] = { 60, 64, 60 };
	for (long i = 0; i < 3; i++)
	{
		e.fTrace[i].x = kX[i] * 1024;
		e.fTrace[i].y = kY[i] * 1024;
	}
	e.fPointCount = 3;
	EXPECT(WriteNewStroke(&e, kCICShortStroke));
	EXPECT(e.fPenX == 40 * 1024 && e.fPenY == 60 * 1024);
	EXPECT(WriteShortStroke(&e));
	EXPECT(e.fPenX == 52 * 1024 && e.fPenY == 60 * 1024);
	EXPECT(WriteNewStroke(&e, kCICEndOfGroup));
	EXPECT(e.fError == 0);
	long size = (long) ((e.fBitPos + 7) / 8);

	gPointCount = 0;
	gBegins = gEnds = gStrokeEnds = 0;
	EXPECT(gCICInkCodec.Decode(bytes, size, 1, CollectAll, nil));
	EXPECT(gBegins == 1 && gEnds == 1 && gStrokeEnds == 1);
	EXPECT(gPointCount == 3);
	for (long i = 0; i < gPointCount && i < 3; i++)
		EXPECT(gPoints[i].x == kX[i] && gPoints[i].y == kY[i]);

	// two strokes, the second written as a step from where the first
	// left off
	memset(bytes, 0, sizeof(bytes));
	memset(&e, 0, sizeof(e));
	e.fOut = bytes;
	e.fBitLimit = sizeof(bytes) * 8;
	e.fBookNumber = 1;
	e.fFirst = 1;
	EXPECT(EcdrSelectCodeBook(&e));
	e.fTrace[0].x = 10 * 1024;	e.fTrace[0].y = 10 * 1024;
	e.fTrace[1].x = 20 * 1024;	e.fTrace[1].y = 10 * 1024;
	e.fPointCount = 2;
	EXPECT(WriteNewStroke(&e, kCICShortStroke) && WriteShortStroke(&e));
	e.fTrace[0].x = 100 * 1024;	e.fTrace[0].y = 50 * 1024;
	e.fTrace[1].x = 100 * 1024;	e.fTrace[1].y = 70 * 1024;
	e.fPointCount = 2;
	EXPECT(WriteNewStroke(&e, kCICShortStroke) && WriteShortStroke(&e));
	EXPECT(WriteNewStroke(&e, kCICEndOfGroup));
	size = (long) ((e.fBitPos + 7) / 8);

	gPointCount = 0;
	gBegins = gEnds = gStrokeEnds = 0;
	EXPECT(gCICInkCodec.Decode(bytes, size, 1, CollectAll, nil));
	EXPECT(gStrokeEnds == 2);
	EXPECT(gPointCount == 4);
	EXPECT(gPoints[0].x == 10 && gPoints[0].y == 10);
	EXPECT(gPoints[1].x == 20 && gPoints[1].y == 10);
	EXPECT(gPoints[2].x == 100 && gPoints[2].y == 50);
	EXPECT(gPoints[3].x == 100 && gPoints[3].y == 70);

	// the ink book, whose step is eight units
	memset(bytes, 0, sizeof(bytes));
	memset(&e, 0, sizeof(e));
	e.fOut = bytes;
	e.fBitLimit = sizeof(bytes) * 8;
	e.fBookNumber = 2;
	e.fFirst = 1;
	EXPECT(EcdrSelectCodeBook(&e) && e.fStep == 0x2000);
	e.fTrace[0].x = 80 * 1024;	e.fTrace[0].y = 40 * 1024;
	e.fTrace[1].x = 96 * 1024;	e.fTrace[1].y = 40 * 1024;
	e.fPointCount = 2;
	EXPECT(WriteNewStroke(&e, kCICShortStroke) && WriteShortStroke(&e));
	EXPECT(WriteNewStroke(&e, kCICEndOfGroup));
	size = (long) ((e.fBitPos + 7) / 8);
	gPointCount = 0;
	EXPECT(gCICInkCodec.Decode(bytes, size, 1, CollectAll, nil));
	EXPECT(gPointCount == 2);
	EXPECT(gPoints[0].x == 80 && gPoints[0].y == 40);
	EXPECT(gPoints[1].x == 96 && gPoints[1].y == 40);

	// rounding: half a step goes away from nought
	EXPECT(QvantUN(2048, 2048) == 1);
	EXPECT(QvantUN(1024, 2048) == 1);
	EXPECT(QvantUN(1023, 2048) == 0);
	EXPECT(QvantUN(-1024, 2048) == -1);
	EXPECT(QvantUN(-1023, 2048) == 0);
	EXPECT(QvantUN(0, 2048) == 0);
}


// The trace the encoder builds a stroke up in, and the pieces that work
// over it.
static void
TestTrace()
{
	// the whole square root
	EXPECT(SQRT32(0) == 0 && SQRT32(1) == 1 && SQRT32(3) == 1 && SQRT32(4) == 2);
	EXPECT(SQRT32(99) == 9 && SQRT32(100) == 10);
	EXPECT(SQRT32(0xffffUL * 0xffffUL) == 0xffff);
	EXPECT(SQRT32(0xfffffffful) == 0xffff);
	for (ULong n = 0; n < 2000; n += 7)
	{
		long r = SQRT32(n);
		EXPECT((ULong) (r * r) <= n && (ULong) ((r + 1) * (r + 1)) > n);
	}

	CICEncoder e;
	memset(&e, 0, sizeof(e));
	e.fUnit = 1024;
	e.fOne = 1;
	InkPoint pt;

	// the first point has no step and no length
	pt.x = 10;	pt.y = 20;
	EXPECT(AddPointToOdata(&e, &pt) == 1);
	EXPECT(e.fPointCount == 1);
	EXPECT(e.fTrace[0].x == 10 * 1024 && e.fTrace[0].y == 20 * 1024);
	EXPECT(e.fTrace[0].fLength == 0 && e.fTrace[0].fArc == 0);

	// a step of three by four is five units long, and the arc adds up
	pt.x = 13;	pt.y = 24;
	EXPECT(AddPointToOdata(&e, &pt) == 1);
	EXPECT(e.fTrace[1].dx == 3 * 1024 && e.fTrace[1].dy == 4 * 1024);
	EXPECT(e.fTrace[1].fLength == 5 * 1024);
	EXPECT(e.fTrace[1].fArc == 5 * 1024);
	pt.x = 13;	pt.y = 34;
	EXPECT(AddPointToOdata(&e, &pt) == 1);
	EXPECT(e.fTrace[2].fLength == 10 * 1024 && e.fTrace[2].fArc == 15 * 1024);
	EXPECT(e.fPointCount == 3);

	// a point too near the one before is not kept
	pt.x = 13;	pt.y = 34;
	EXPECT(AddPointToOdata(&e, &pt) == 0);
	EXPECT(e.fPointCount == 3);

	// the nine places spread evenly along what there is
	ResetParam(&e);
	long step = (15 * 1024) / 9;
	for (long i = 0; i < kCICSamples; i++)
	{
		EXPECT(e.fSamples[i].fStep == step);
		EXPECT(e.fSamples[i].fAt == step * i);
	}

	// kept and put back
	StoreContext(&e);
	e.fPointCount = 99;
	for (long i = 0; i < kCICSamples; i++)
	{
		e.fSamples[i].fAt = -1;
		e.fSamples[i].fStep = -1;
	}
	RestoreContext(&e);
	EXPECT(e.fPointCount == 3);
	for (long i = 0; i < kCICSamples; i++)
		EXPECT(e.fSamples[i].fAt == step * i && e.fSamples[i].fStep == step);

	// the nine places measured along the curve they sit on
	{
		CICSample line[kCICSamples + 1];
		memset(line, 0, sizeof(line));
		for (long i = 0; i <= kCICSamples; i++)
		{
			line[i].x = i * 30;
			line[i].y = i * 40;
		}
		Tracing(kCICSamples, line);
		EXPECT(line[0].fStep == 0 && line[0].fAt == 0);
		for (long i = 1; i <= kCICSamples; i++)
		{
			EXPECT(line[i].fStep == 50);		// three by four is five
			EXPECT(line[i].fAt == 50 * i);
		}
	}

	// how far apart two sets of samples are
	CICSample a[2], b[2];
	memset(a, 0, sizeof(a));
	memset(b, 0, sizeof(b));
	EXPECT(MSQError(2, a, b) == 0);
	a[0].x = 3;	a[0].y = 4;
	b[1].x = -5;
	EXPECT(MSQError(2, a, b) == 9 + 16 + 25);
}


// Nine samples into four numbers and back.  The four are a curve in a
// cosine basis - the samples are not evenly spaced in the parameter -
// and they are the same four the decoder's RestoreSegment draws out.
static void
TestTransform()
{
	CICSample p[kCICSamples];
	long c[4];
	long got[4];
	memset(p, 0, sizeof(p));

	// coefficients out and back again
	static const long kCoef[3][4] = {
		{ 40 * 1024, -40 * 1024, 0, 0 },			// no bend: a chord
		{ 50 * 1024, -30 * 1024, 4 * 1024, 0 },
		{ 0, 100 * 1024, -7 * 1024, 3 * 1024 },
	};
	for (long k = 0; k < 3; k++)
	{
		for (long i = 0; i < 4; i++)
			c[i] = kCoef[k][i];
		memset(p, 0, sizeof(p));
		RIFT_4_9_X(p, c);
		RIFT_4_9_Y(p, c);
		// the ends are exactly what the coefficients say
		EXPECT(p[0].x == c[0] + c[2] + c[1] + c[3]);
		EXPECT(p[8].x == c[0] + c[2] - c[1] - c[3]);
		EXPECT(p[0].y == p[0].x && p[8].y == p[8].x);
		RFFT_9_4_X(p, got, p[0].x, p[8].x);
		for (long i = 0; i < 4; i++)
			EXPECT(got[i] >= c[i] - 8 && got[i] <= c[i] + 8);
		RFFT_9_4_Y(p, got, p[0].y, p[8].y);
		for (long i = 0; i < 4; i++)
			EXPECT(got[i] >= c[i] - 8 && got[i] <= c[i] + 8);
	}

	// The encoder and the decoder draw the same curve, but not at the
	// same places along it: the decoder steps evenly through the
	// parameter (RestoreSegment, seventeen points) and the fitting looks
	// at the nine where the cosine of an even step lands, which is what
	// makes the transform a cosine one.  The two grids share three
	// points - the ends and the middle - and there the answers agree.
	for (long k = 0; k < 3; k++)
	{
		long seg[4];
		for (long i = 0; i < 4; i++)
			seg[i] = kCoef[k][i];
		memset(p, 0, sizeof(p));
		RIFT_4_9_X(p, seg);
		long drawn[kCICSegmentPoints];
		RestoreSegment(drawn, seg);
		static const long kShared[3][2] = { { 0, 0 }, { 4, 8 }, { 8, 16 } };
		for (long i = 0; i < 3; i++)
		{
			long d = drawn[kShared[i][1]] - p[kShared[i][0]].x;
			if (d < 0)
				d = -d;
			EXPECT(d <= 0x40);		// (a sixteenth of a tablet unit)
		}
	}
}



// The nine places found on the stroke itself.
static void
TestRepar()
{
	CICEncoder e;
	memset(&e, 0, sizeof(e));
	e.fUnit = 1024;
	e.fOne = 1;
	// a straight stroke of nine points, ten tablet units apart
	for (long i = 0; i < 9; i++)
	{
		InkPoint pt;
		pt.x = (short) (i * 10);
		pt.y = 0;
		EXPECT(AddPointToOdata(&e, &pt) == 1);
	}
	EXPECT(e.fPointCount == 9);
	EXPECT(e.fTrace[8].fArc == 80 * 1024);
	ResetParam(&e);
	// the nine places stop eight ninths of the way along, so the ratio
	// that brings them back to the whole stroke is about nine eighths -
	// exactly the stroke's length over the last place's distance, to
	// twenty-four binary places
	ULong ratio = Repar(&e.fTrace[8], &e.fTrace[0], e.fSamples, e.fResampled);
	ULong want = (ULong) (((long long) e.fTrace[8].fArc << 24) / e.fSamples[8].fAt);
	EXPECT(ratio == want);
	EXPECT(ratio > (9UL << 24) / 8 && ratio < (9UL << 24) / 8 + (1UL << 16));
	// and they land at the eighths of a straight stroke
	EXPECT(e.fResampled[0].x == 0 && e.fResampled[0].y == 0);
	EXPECT(e.fResampled[8].x == 80 * 1024 && e.fResampled[8].y == 0);
	for (long i = 1; i < 8; i++)
	{
		long want = i * 10 * 1024;
		EXPECT(e.fResampled[i].x >= want - 64 && e.fResampled[i].x <= want + 64);
		EXPECT(e.fResampled[i].y == 0);
	}

	// a stroke that turns a corner: the ends are still exact, and the
	// places walk round the corner in order
	memset(&e, 0, sizeof(e));
	e.fUnit = 1024;
	e.fOne = 1;
	static const short kX[] = { 0, 20, 40, 40, 40, 40 };
	static const short kY[] = { 0,  0,  0, 20, 40, 60 };
	for (long i = 0; i < 6; i++)
	{
		InkPoint pt;
		pt.x = kX[i];
		pt.y = kY[i];
		EXPECT(AddPointToOdata(&e, &pt) == 1);
	}
	ResetParam(&e);
	Repar(&e.fTrace[5], &e.fTrace[0], e.fSamples, e.fResampled);
	EXPECT(e.fResampled[0].x == 0 && e.fResampled[0].y == 0);
	EXPECT(e.fResampled[8].x == 40 * 1024 && e.fResampled[8].y == 60 * 1024);
	Boolean along = true;
	for (long i = 1; i < kCICSamples; i++)
		if (e.fResampled[i].x < e.fResampled[i - 1].x
			|| e.fResampled[i].y < e.fResampled[i - 1].y)
			along = false;
	EXPECT(along);
	// the corner is at forty units of x, so no place passes it
	for (long i = 0; i < kCICSamples; i++)
		EXPECT(e.fResampled[i].x <= 40 * 1024);
}


// Points handed to the encoder and read back out of the ink it wrote.
struct Trace
{
	const short*	fX;
	const short*	fY;
	long			fCount;
	long			fAt;
};

static short
HandOutPoints(short what, InkPoint* pt, void* refCon)
{
	Trace* t = (Trace*) refCon;
	if (what == kInkAskBegin)
	{
		t->fAt = 0;
		return 1;
	}
	if (t->fAt >= t->fCount)
		return kInkEnd;
	pt->x = t->fX[t->fAt];
	pt->y = t->fY[t->fAt];
	t->fAt++;
	return kInkPoint;
}


static void
TestEncodeRun()
{
	InitializeParagraphCompression();
	EXPECT(gCICInkCodec.CanEncode());

	// a curve of twenty points, a quarter turn of a circle of radius
	// eighty about (100, 100)
	short xs[20];
	short ys[20];
	for (long i = 0; i < 20; i++)
	{
		double a = 3.14159265358979 / 2 * i / 19;
		xs[i] = (short) (100 + 80 * (1 - (a * a) / 2 + (a * a * a * a) / 24));
		ys[i] = (short) (100 + 80 * (a - (a * a * a) / 6));
	}
	Trace t;
	t.fX = xs;
	t.fY = ys;
	t.fCount = 20;
	t.fAt = 0;

	long size = 0;
	void* ink = gCICInkCodec.Encode(HandOutPoints, &t, &size);
	EXPECT(ink != nil && size > 0);
	if (ink == nil)
		return;
	// what came out is a block the reader knows
	EXPECT(GetInkFormat(ink) == kInkFormatCompressed);
	EXPECT(InkCodecFor(ink) == &gCICInkCodec);
	// and it is smaller than the points were
	EXPECT(size < 20 * 4);

	gPointCount = 0;
	gBegins = gEnds = gStrokeEnds = 0;
	EXPECT(gCICInkCodec.Decode(ink, size, 1, CollectAll, nil));
	EXPECT(gBegins == 1 && gEnds == 1 && gStrokeEnds == 1);
	EXPECT(gPointCount > 4);

	// every point that comes back is near the curve that went in
	long worst = 0;
	for (long i = 0; i < gPointCount; i++)
	{
		long nearest = 0x7fffffff;
		for (long j = 0; j < 20; j++)
		{
			long dx = gPoints[i].x - xs[j];
			long dy = gPoints[i].y - ys[j];
			long d = dx * dx + dy * dy;
			if (d < nearest)
				nearest = d;
		}
		if (nearest > worst)
			worst = nearest;
	}
	EXPECT(worst <= 16 * 16);		// (within sixteen tablet units, which is two pixels)

	// and it starts and ends where the stroke did
	EXPECT(gPoints[0].x >= xs[0] - 8 && gPoints[0].x <= xs[0] + 8);
	EXPECT(gPoints[0].y >= ys[0] - 8 && gPoints[0].y <= ys[0] + 8);
	EXPECT(gPoints[gPointCount - 1].x >= xs[19] - 8 && gPoints[gPointCount - 1].x <= xs[19] + 8);
	EXPECT(gPoints[gPointCount - 1].y >= ys[19] - 8 && gPoints[gPointCount - 1].y <= ys[19] + 8);
	DisposPtr((Ptr) ink);
}


// Strokes into ink and back out again.
static void
TestStrokeRoundTrip()
{
	InitializeParagraphCompression();
	// a stroke of sixteen points along a gentle curve, in pixels
	const long kPoints = 16;
	short px[kPoints];
	short py[kPoints];
	TStroke* stroke = TStroke::Make(0);		// (a count makes that many points, not room for them)
	EXPECT(stroke != nil);
	if (stroke == nil)
		return;
	for (long i = 0; i < kPoints; i++)
	{
		px[i] = (short) (20 + i * 4);
		py[i] = (short) (40 + (i * i) / 6);
		TabPt tab;
		tab.x = ToFixed(px[i]);
		tab.y = ToFixed(py[i]);
		tab.z = 0;
		tab.p = 0;
			EXPECT(stroke->AddPoint(&tab) == 0);
	}
	stroke->EndStroke();
	TStroke* list[2];
	list[0] = stroke;
	list[1] = nil;

	RefVar ink(InkCompress(list, false));
	EXPECT(NOTNIL(ink));
	EXPECT(IsRawInk(ink) && !IsInkWord(ink));
	EXPECT(Length(ink) > 0 && Length(ink) < kPoints * 4);

	TStroke** back = InkExpand(ink, 1, 0, 0);
	EXPECT(back != nil);
	if (back == nil)
		return;
	EXPECT(back[0] != nil && back[1] == nil);
	long count = (long) back[0]->fCount;
	// a long stroke comes back with sixteen points to a segment, so
	// there are more of them than went in
	// every point that came back is near the curve that went in
	long worst = 0;
	for (long i = 0; i < count; i++)
	{
		FPoint at;
		back[0]->GetFPoint(i, &at);
		long x = RoundFixed(at.x);
		long y = RoundFixed(at.y);
		long nearest = 0x7fffffff;
		for (long j = 0; j < kPoints; j++)
		{
			long dx = x - px[j];
			long dy = y - py[j];
			long d = dx * dx + dy * dy;
			if (d < nearest)
				nearest = d;
		}
		if (nearest > worst)
			worst = nearest;
	}
	EXPECT(worst <= 3 * 3);		// within three pixels of the stroke

	// and an ink word carries what it measures after its strokes: it is
	// as wide as its box, and with no recogniser to say where its
	// baseline is the whole of it stands above one
	RefVar word(InkCompress(list, true));
	EXPECT(NOTNIL(word) && IsInkWord(word));
	EXPECT(Length(word) == Length(ink) + (long) sizeof(PackedInkWordInfo));
	{
		InkWordInfo info;
		GetInkWordInfo(word, &info);
		EXPECT(info.fWidth == (ULong) (px[kPoints - 1] - px[0] + 2));
		EXPECT(info.fPenSize == 2);
		EXPECT(info.fScale == 0x10000);		// a hundred per cent
		EXPECT(info.fDescent == 0 && info.fAscent > 0);
		EXPECT(info.fXHeight > 0 && info.fXHeight <= info.fAscent);
	}
	TStroke** wordBack = InkExpand(word, 1, 0, 0);
	EXPECT(wordBack != nil && wordBack[0] != nil);

	// InkConvert: the ink re-encoded in the other code book and back,
	// never becoming points on the way, draws the same strokes
	{
		RefVar asInk(InkConvert(ink, RSSYMink));
		EXPECT(NOTNIL(asInk) && EQRef(ClassOf(asInk), RSSYMink));
		EXPECT(NOTNIL(asInk) && GetInkFormat(BinaryData(asInk)) == 2 && GetInkFormat(BinaryData(ink)) != 2);
		RefVar again(InkConvert(asInk, RSSYMink2));
		EXPECT(NOTNIL(again) && EQRef(ClassOf(again), RSSYMink2) && GetInkFormat(BinaryData(again)) == 3);
		TStroke** converted = NOTNIL(asInk) ? InkExpand(asInk, 1, 0, 0) : nil;
		TStroke** round = NOTNIL(again) ? InkExpand(again, 1, 0, 0) : nil;
		EXPECT(converted != nil && converted[0] != nil && converted[1] == nil);
		EXPECT(round != nil && round[0] != nil && round[1] == nil);
		if (converted != nil && converted[0] != nil && round != nil && round[0] != nil)
		{
			Rect a, b, c;
			InkBounds(back, &a);
			InkBounds(converted, &b);
			InkBounds(round, &c);
			EXPECT(abs(a.left - b.left) <= 2 && abs(a.right - b.right) <= 2 && abs(a.top - b.top) <= 2 && abs(a.bottom - b.bottom) <= 2);
			EXPECT(abs(a.left - c.left) <= 2 && abs(a.right - c.right) <= 2 && abs(a.bottom - c.bottom) <= 2);
		}
		// ink already of the class is cloned; anything not ink is nil
		RefVar same(InkConvert(ink, ClassOf(ink)));
		EXPECT(NOTNIL(same) && same != ink && Length(same) == Length(ink));
		EXPECT(ISNIL(InkConvert(RefVar(MAKEINT(3)), RSSYMink)));
		// an ink word stays one, its measurements worked out again
		RefVar wordAgain(InkConvert(ink, RSSYMinkword));
		EXPECT(NOTNIL(wordAgain) && IsInkWord(wordAgain));

		// the copy the converter is handed: kept by the ROM (a leak), given
		// back with the fix - the heap's free space as it was
		SetRomBugFixed(false);
		Size before = TotalFreeInHeap();
		{
			RefVar leaked(InkConvert(ink, RSSYMink));
		}
		EXPECT(TotalFreeInHeap() < before);
		SetRomBugFixed(true);
		before = TotalFreeInHeap();
		{
			RefVar kept(InkConvert(ink, RSSYMink));
		}
		EXPECT(TotalFreeInHeap() == before);
	}

	// the ink put down somewhere else moves with it
	TStroke** moved = InkExpand(ink, 1, 100, 0);
	EXPECT(moved != nil && moved[0] != nil);
	if (moved != nil && moved[0] != nil && back[0] != nil)
	{
		FPoint first, shifted;
		back[0]->GetFPoint(0, &first);
		moved[0]->GetFPoint(0, &shifted);
		EXPECT(RoundFixed(shifted.x) == RoundFixed(first.x) + 100);
		EXPECT(RoundFixed(shifted.y) == RoundFixed(first.y));
	}

	DisposeTStrokes(moved);
	DisposeTStrokes(wordBack);
	DisposeTStrokes(back);
	stroke->IDispose();
}


// A stroke of `n` points from (x0, y0) to (x1, y1), finished.
static TStroke*	MakeLine(long x0, long y0, long x1, long y1, long n);

// A stroke made into a sketch and into a word.
static void
TestStrokesToInk()
{
	InitializeParagraphCompression();
	// a stroke from (50, 60) to (70, 80)
	TStroke* stroke = TStroke::Make(0);
	EXPECT(stroke != nil);
	if (stroke == nil)
		return;
	for (long i = 0; i <= 10; i++)
	{
		TabPt tab;
		tab.x = ToFixed(50 + i * 2);
		tab.y = ToFixed(60 + i * 2);
		tab.z = 0;
		tab.p = 0;
		stroke->AddPoint(&tab);
	}
	stroke->EndStroke();
	TStroke* list[2];
	list[0] = stroke;
	list[1] = nil;

	Rect box;
	UnionBounds(list, &box);
	EXPECT(box.left == 50 && box.top == 60);
	EXPECT(box.right >= 70 && box.bottom >= 80);
	Rect inked;
	InkBounds(list, &inked);
	EXPECT(inked.left == box.left - 2 && inked.top == box.top - 2);
	EXPECT(inked.right == box.right + 2 && inked.bottom == box.bottom + 2);

	// made into a sketch: the strokes come away to the origin and the
	// box says where they were
	Rect where;
	RefVar ink(TStrokesToInk(list, &where));
	EXPECT(NOTNIL(ink) && IsRawInk(ink));
	EXPECT(where.left == 48 && where.top == 58);
	Rect moved;
	UnionBounds(list, &moved);
	EXPECT(moved.left == 2 && moved.top == 2);

	// and read back it starts where it now is
	TStroke** back = InkExpand(ink, 1, 0, 0);
	EXPECT(back != nil && back[0] != nil);
	if (back != nil && back[0] != nil)
	{
		FPoint at;
		back[0]->GetFPoint(0, &at);
		EXPECT(RoundFixed(at.x) == 2 && RoundFixed(at.y) == 2);
	}
	DisposeTStrokes(back);
	stroke->IDispose();

	// a word too wide for a line is brought down to two hundred and
	// forty pixels, and keeps its shape
	TStroke* wide = TStroke::Make(0);
	for (long i = 0; i <= 10; i++)
	{
		TabPt tab;
		tab.x = ToFixed(i * 48);		// 480 across
		tab.y = ToFixed(i * 3);			// 30 down
		tab.z = 0;
		tab.p = 0;
		wide->AddPoint(&tab);
	}
	wide->EndStroke();
	list[0] = wide;
	UnionBounds(list, &box);
	EXPECT(box.right - box.left >= 480);
	ScaleStrokesForInkWord(list, &box);
	EXPECT(box.right - box.left == 240);
	EXPECT(box.bottom - box.top >= 14 && box.bottom - box.top <= 17);	// half of thirty-odd
	wide->IDispose();

	// BUG (the ROM's): a perfectly flat stroke in a word that has to be
	// scaled has its box blown up to about sixteen thousand pixels each
	// way, and its points are mapped into that - which is nonsense.
	//
	// A stroke's box is made a Fixed unit - 1/65536 of a pixel - past its
	// points as the first point goes in and again when it is finished, so
	// a stroke drawn along one exact y is two of those high.  GetMapper
	// divides that by the width and gets 0, and then divides the
	// destination's height by that 0; FixedDivide saturates, and the
	// inset worked out from it is about -2^30.  Every add and subtract
	// after that wraps, as the ARM's own do.
	//
	// The reconstruction keeps it: a Newton really does make nonsense of
	// such a word.  It is easy to hit on a host because the mouse gives
	// exactly equal y values, where a tablet's samples always jitter.
	{
		TStroke* word[3];
		word[0] = MakeLine(10, 20, 40, 120, 20);	// tall enough to need scaling
		word[1] = MakeLine(50, 100, 160, 100, 20);	// ... and perfectly flat
		word[2] = nil;
		Rect box;
		UnionBounds(word, &box);
		EXPECT(box.bottom - box.top > 60);			// so ScaleStrokesForInkWord does something
		EXPECT(word[1]->fBBox.bottom - word[1]->fBBox.top == 2);	// two Fixed units high, 1/32768 of a pixel
		ScaleStrokesForInkWord(word, &box);
		ULong blownUp = (ULong) word[1]->fBBox.right - (ULong) word[1]->fBBox.left;
		EXPECT(blownUp > 0x7f000000);				// about 2^31: sixteen thousand pixels
		word[0]->IDispose();
		word[1]->IDispose();
	}
}


// Ink drawn into an offscreen port.
static const long kDrawWidth = 128;
static const long kDrawHeight = 64;
static unsigned char	gDrawBits[kDrawWidth * kDrawHeight / 8];
static PixelMap			gDrawMap;
static GrafPort			gDrawPort;

static long
DrawnPixels(void)
{
	long lit = 0;
	for (long y = 0; y < kDrawHeight; y++)
		for (long x = 0; x < kDrawWidth; x++)
			if (GetPixel(&gDrawMap, x, y) != 0)
				lit++;
	return lit;
}


static void
TestInkDraw()
{
	InitializeParagraphCompression();
	InitGraf();
	gDrawMap.baseAddr = (Ptr) gDrawBits;
	gDrawMap.rowBytes = kDrawWidth / 8;
	SetRect(&gDrawMap.bounds, 0, 0, kDrawWidth, kDrawHeight);
	gDrawMap.pixMapFlags = kPixMapPtr | 1;
	gDrawMap.deviceRes.v = kDefaultDPI;
	gDrawMap.deviceRes.h = kDefaultDPI;
	gDrawMap.grayTable = nil;
	OpenPort(&gDrawPort);
	SetPortBits(&gDrawMap);
	gDrawPort.portRect = gDrawMap.bounds;
	RectRgn(gDrawPort.visRgn, &gDrawMap.bounds);
	memset(gDrawBits, 0, sizeof(gDrawBits));

	// a stroke straight across, forty pixels of it
	TStroke* stroke = TStroke::Make(0);
	EXPECT(stroke != nil);
	if (stroke == nil)
		return;
	for (long i = 0; i <= 20; i++)
	{
		TabPt tab;
		tab.x = ToFixed(i * 2);
		tab.y = ToFixed(10);
		tab.z = 0;
		tab.p = 0;
		stroke->AddPoint(&tab);
	}
	stroke->EndStroke();
	TStroke* list[2];
	list[0] = stroke;
	list[1] = nil;
	Rect where;
	RefVar ink(TStrokesToInk(list, &where));
	EXPECT(NOTNIL(ink));

	EXPECT(DrawnPixels() == 0);
	InkDraw(ink, 1, 20, 30, false);
	long lit = DrawnPixels();
	EXPECT(lit > 20 && lit < 80);		// a line about forty pixels long

	// it lands where it was told to: the row it was drawn on is lit and
	// the ones well away from it are not
	long onRow = 0;
	for (long x = 0; x < kDrawWidth; x++)
		if (GetPixel(&gDrawMap, x, 32) != 0)
			onRow++;
	EXPECT(onRow > 20);
	for (long x = 0; x < kDrawWidth; x++)
		EXPECT(GetPixel(&gDrawMap, x, 10) == 0);

	// and drawn again ten to the right it moves with it
	memset(gDrawBits, 0, sizeof(gDrawBits));
	InkDraw(ink, 1, 30, 30, false);
	long first = -1;
	for (long x = 0; x < kDrawWidth; x++)
		if (GetPixel(&gDrawMap, x, 32) != 0)
		{
			first = x;
			break;
		}
	EXPECT(first >= 30 && first <= 34);

	// half the size is half as wide
	memset(gDrawBits, 0, sizeof(gDrawBits));
	{
		long inkSize = 0;
		const void* inkData = InkData(ink, &inkSize);
		InkDrawScaled(inkData, inkSize, 1, ToFixed(0), ToFixed(4), 0x8000, 0x8000, false);
	}
	long widest = 0;
	for (long y = 0; y < kDrawHeight; y++)
	{
		long across = 0;
		for (long x = 0; x < kDrawWidth; x++)
			if (GetPixel(&gDrawMap, x, y) != 0)
				across++;
		if (across > widest)
			widest = across;
	}
	EXPECT(widest > 10 && widest < 30);

	// the same ink as a shape, drawn through the shape machinery: a
	// frame of class 'ink with the box it was made in, the box it is to
	// fill now, and the ink itself
	memset(gDrawBits, 0, sizeof(gDrawBits));
	{
		Rect from;
		UnionBounds(list, &from);
		RefVar shape(AllocateFrame());
		SetClass(shape, RefVar(RSSYMink));
		SetFrameSlot(shape, RefVar(RSSYMdata), ink);
		SetFrameSlot(shape, RefVar(RSSYMoriginalbounds), RefVar(ToObject(from)));
		Rect to = from;
		OffsetRect(&to, 10, 20);
		SetFrameSlot(shape, RefVar(RSSYMbounds), RefVar(ToObject(to)));
		Point origin;
		origin.h = 0;
		origin.v = 0;
		DrawShape(shape, RefVar(NILREF), origin);
		EXPECT(DrawnPixels() > 20);
		// and stretched to twice the width it covers twice as much
		long plain = DrawnPixels();
		memset(gDrawBits, 0, sizeof(gDrawBits));
		to.right = (short) (to.left + (from.right - from.left) * 2);
		SetFrameSlot(shape, RefVar(RSSYMbounds), RefVar(ToObject(to)));
		DrawShape(shape, RefVar(NILREF), origin);
		long stretched = DrawnPixels();
		EXPECT(stretched > plain);
	}

	stroke->IDispose();
	ClosePort(&gDrawPort);
}

static TStroke*
MakeLine(long x0, long y0, long x1, long y1, long n)
{
	TStroke* stroke = TStroke::Make(0);
	for (long i = 0; i <= n; i++)
	{
		TabPt tab;
		tab.x = ToFixed(x0 + (x1 - x0) * i / n);
		tab.y = ToFixed(y0 + (y1 - y0) * i / n);
		tab.z = 0;
		tab.p = 0;
		stroke->AddPoint(&tab);
	}
	stroke->EndStroke();
	return stroke;
}


// Ink as a shape: the frame a word is passed about in, taken apart
// again, cut in two and put back together.
static void
TestInkShapes()
{
	InitializeParagraphCompression();

	// two strokes with a clear gap between them
	TStroke* list[3];
	list[0] = MakeLine(10, 20, 30, 40, 10);
	list[1] = MakeLine(60, 20, 80, 40, 10);
	list[2] = nil;
	RefVar form(MakeInkWordPoly(list));
	EXPECT(NOTNIL(form));
	RefVar ink(GetFrameSlot(form, RSSYMink));
	EXPECT(IsInkWord(ink));
	Rect box;
	EXPECT(FromObject(RefVar(GetFrameSlot(form, RSSYMviewbounds)), box));

	// the shape opened back up gives the strokes where the bounds say
	TStroke** back = GetPolyAsTStrokes(form, 0);
	EXPECT(back != nil);
	EXPECT(CountTStrokes(back) == 2);
	if (back != nil && CountTStrokes(back) == 2)
	{
		Rect where;
		UnionBounds(back, &where);
		// the bounds are the inked box, so the strokes themselves start
		// the pen's slop inside them
		EXPECT(where.left == box.left + kInkSlop);
	}
	DisposeTStrokes(back);

	// cut between the two strokes: one each side
	RefVar halves(SplitInkAt(form, (box.left + box.right) / 2, 2));
	EXPECT(IsArray(halves) && Length(halves) == 2);
	if (IsArray(halves) && Length(halves) == 2)
	{
		RefVar first(GetArraySlot(halves, 0));
		RefVar second(GetArraySlot(halves, 1));
		EXPECT(IsInkWord(RefVar(GetFrameSlot(first, RSSYMink))));
		EXPECT(IsInkWord(RefVar(GetFrameSlot(second, RSSYMink))));
		TStroke** left = GetPolyAsTStrokes(first, 0);
		TStroke** right = GetPolyAsTStrokes(second, 0);
		EXPECT(CountTStrokes(left) == 1);
		EXPECT(CountTStrokes(right) == 1);
		DisposeTStrokes(left);
		DisposeTStrokes(right);

		// and joined again it is one word of two strokes, as wide as
		// the two of them
		RefVar joined(MergeInk(first, second));
		EXPECT(NOTNIL(joined));
		RefVar joinedInk(GetFrameSlot(joined, RSSYMink));
		EXPECT(IsInkWord(joinedInk));
		TStroke** both = GetPolyAsTStrokes(joined, 0);
		EXPECT(CountTStrokes(both) == 2);
		DisposeTStrokes(both);
		InkWordInfo whole;
		InkWordInfo half;
		GetInkWordInfo(joinedInk, &whole);
		GetInkWordInfo(RefVar(GetFrameSlot(first, RSSYMink)), &half);
		EXPECT(whole.fWidth > half.fWidth);
	}
	list[0]->Dispose();
	list[1]->Dispose();

	// a single stroke written right across the cut cannot be divided
	TStroke* wide[2];
	wide[0] = MakeLine(10, 20, 80, 40, 20);
	wide[1] = nil;
	RefVar oneWord(MakeInkWordPoly(wide));
	Rect wideBox;
	FromObject(RefVar(GetFrameSlot(oneWord, RSSYMviewbounds)), wideBox);
	EXPECT(ISNIL(SplitInkAt(oneWord, (wideBox.left + wideBox.right) / 2, 2)));
	wide[0]->Dispose();
}


// Stroke bundles: the NewtonScript form of a handful of strokes, and
// the ink they pack up into.
static void
TestStrokeBundles()
{
	InitializeParagraphCompression();
	TStroke* one = MakeLine(10, 20, 30, 40, 10);
	TStroke* two = MakeLine(60, 20, 80, 40, 10);

	RefVar bundle(Clone(RefVar(Rstrokebundle)));
	RefVar list(AllocateArray(RSSYMarray, 2));
	SetArraySlot(list, 0, RefVar(MakeStrokeRef(one)));
	SetArraySlot(list, 1, RefVar(MakeStrokeRef(two)));
	SetFrameSlot(bundle, RSSYMstrokes, list);
	CalcBundleBounds(bundle);

	EXPECT(CountStrokes(bundle) == 2);
	RefVar first(GetStroke(bundle, 0));
	EXPECT(CountPoints(first) == one->Count());

	// a point comes back in pixels, or in the eighths it is kept in
	Point at;
	GetStrokePoint(first, 0, &at, 0);
	EXPECT(at.h == 10 && at.v == 20);
	GetStrokePoint(first, 0, &at, 2);
	EXPECT(at.h == 80 && at.v == 160);

	Rect box;
	GetStrokeBounds(first, &box);
	EXPECT(box.left == 10 && box.top == 20 && box.right == 30 && box.bottom == 40);
	Rect all;
	GetBundleBounds(bundle, &all);
	EXPECT(all.left == 10 && all.right == 80);
	Rect stored;
	EXPECT(FromObject(RefVar(GetFrameSlot(bundle, RSSYMbounds)), stored));
	EXPECT(stored.left == all.left && stored.right == all.right);

	// every point, v before h; and the same asked for thinned, which
	// keeps the first and then only those far enough from the one before
	RefVar points(GetStrokePointsArray(first, 1));
	EXPECT(Length(points) == one->Count() * 2);
	EXPECT(RINT(GetArraySlot(points, 0)) == 20 && RINT(GetArraySlot(points, 1)) == 10);
	RefVar thinned(GetStrokePointsArray(first, 0x1000));	// ten pixels apart
	EXPECT(Length(thinned) < Length(points) && Length(thinned) >= 2);
	RefVar xy(GetStrokePointsArray(first, 0x10001));
	EXPECT(RINT(GetArraySlot(xy, 0)) == 10 && RINT(GetArraySlot(xy, 1)) == 20);

	// a bundle made from such an array is the one it came from
	RefVar arrays(AllocateArray(RSSYMarray, 1));
	SetArraySlot(arrays, 0, points);
	RefVar remade(MakeStrokeBundle(arrays, 1));
	EXPECT(CountStrokes(remade) == 1);
	Rect remadeBox;
	GetBundleBounds(remade, &remadeBox);
	EXPECT(remadeBox.left == box.left && remadeBox.right == box.right);

	// and back into strokes
	TStroke** back = StrokeBundleToTStrokes(bundle);
	EXPECT(CountTStrokes(back) == 2);
	if (CountTStrokes(back) == 2)
	{
		Rect where;
		UnionBounds(back, &where);
		EXPECT(where.left == 10 && where.top == 20);
	}
	DisposeTStrokes(back);

	// the ink word a bundle makes is kept in the bundle
	RefVar ink(StrokeBundleToInkWord(bundle));
	EXPECT(IsInkWord(ink));
	EXPECT(EQ(ink, RefVar(StrokeBundleToInkWord(bundle))));

	// a shape of it, and that shape opened back up into a bundle again
	RefVar form(CompressStrokes(bundle));
	EXPECT(IsInkWord(RefVar(GetFrameSlot(form, RSSYMink))));
	RefVar again(ExpandInk(form, 0));
	EXPECT(CountStrokes(again) == 2);
	RefVar sketch(CompressStrokesToInk(bundle));
	EXPECT(IsRawInk(RefVar(GetFrameSlot(sketch, RSSYMink))));

	one->Dispose();
	two->Dispose();
}


// An ink word restyled from a font spec frame - the same frame a
// paragraph's style run carries, so that restyling a run of text
// restyles the writing in it.
static void
TestInkWordFontParms()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();

	TStroke* list[2];
	list[0] = MakeLine(10, 20, 40, 44, 12);
	list[1] = nil;
	Rect box;
	RefVar word(TStrokesToInkWord(list, &box));
	EXPECT(IsInkWord(word));
	InkWordInfo was;
	GetInkWordInfo(word, &was);

	// a spec that says nothing changes nothing
	RefVar spec(AllocateFrame());
	SetInkWordFontParms(word, spec);
	InkWordInfo now;
	GetInkWordInfo(word, &now);
	EXPECT(now.fScale == was.fScale && now.fFace == was.fFace
		   && now.fPenSize == was.fPenSize);

	// `scale` is a percentage of the word's own size
	SetFrameSlot(spec, RSSYMscale, MAKEINT(50));
	SetInkWordFontParms(word, spec);
	GetInkWordInfo(word, &now);
	EXPECT(now.fScale == ToFixed(1) / 2);
	EXPECT(now.fScaledWidth < was.fWidth);

	// `size` is a point size, which comes to a scale against the size
	// the word's x-height makes it; `scale` wins when both are there
	RefVar sized(AllocateFrame());
	SetFrameSlot(sized, RSSYMsize, MAKEINT(was.fFontSize * 2));
	SetInkWordFontParms(word, sized);
	GetInkWordInfo(word, &now);
	EXPECT(now.fScale == ToFixed(2));
	SetFrameSlot(sized, RSSYMscale, MAKEINT(100));
	SetInkWordFontParms(word, sized);
	GetInkWordInfo(word, &now);
	EXPECT(now.fScale == ToFixed(1));

	// the face is packed down to the six bits an ink word has room for,
	// and the pen size is kept as it comes
	RefVar faced(AllocateFrame());
	SetFrameSlot(faced, RSSYMface, MAKEINT(kBoldFace | kItalicFace));
	SetFrameSlot(faced, RSSYMpensize, MAKEINT(3));
	SetInkWordFontParms(word, faced);
	GetInkWordInfo(word, &now);
	EXPECT(now.fFace == (ULong) (kBoldFace | kItalicFace));
	EXPECT(now.fPenSize == 3);
	// and the scale it was left at is kept, the spec not saying
	EXPECT(now.fScale == ToFixed(1));

	// the glyph asks the word again once it has been restyled
	TInkWordGlyph glyph(word, (ULong) -1, (ULong) -1);
	long wide = glyph.fWidth;
	RefVar bigger(AllocateFrame());
	SetFrameSlot(bigger, RSSYMscale, MAKEINT(200));
	EXPECT(NOTNIL(RefVar(glyph.SetFontParms(bigger))));
	// the size and face go back to "the word's own", which ReadMetrics
	// then fills in from the word it has just been told about
	EXPECT(glyph.fFontSize == (ULong) glyph.fInfo.fScaledFontSize);
	EXPECT(glyph.fFace == glyph.fInfo.fFace);
	EXPECT(glyph.fWidth > wide);
	EXPECT(glyph.fInfo.fScale == ToFixed(2));
}


// An ink word opened as a font: the text engine sees a font with one
// glyph, and asking for that glyph draws the writing into a bitmap.
static void
TestInkFont()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();

	TStroke* list[2];
	list[0] = MakeLine(10, 20, 40, 44, 12);
	list[1] = nil;
	Rect box;
	RefVar word(TStrokesToInkWord(list, &box));
	EXPECT(IsInkWord(word));
	InkWordInfo info;
	GetInkWordInfo(word, &info);

	// the style whose font is the word itself
	// (a StyleRecord holds a RefStruct, so it is filled in rather than
	//  cleared; CreateTextStyleRecord does not know about ink words -
	//  the paragraph's own CreateParagraphStyleRecord is what puts one
	//  here, and is NOT YET)
	StyleRecord style;
	style.fFontFamily = word;
	style.fFontSize = ToFixed(info.fScaledFontSize);
	style.fFontFace = 0;
	style.fFontPattern = 0;
	style.fPattern = nil;
	style.fTransferMode = 0;

	FontEngineInfo font;
	EXPECT(OpenFont(&gDrawMap, &style, 0x10000, 0x10000, &font) == 0);
	// its line metrics are the word's own
	EXPECT(font.fAscent > 0 && font.fDescent >= 0);
	EXPECT(font.fAscent == (long) info.fScaledAscent);
	EXPECT(font.fDescent == (long) info.fScaledDescent);
	EXPECT(font.fMaxBeforeBL == font.fAscent && font.fMinAfterBL == -font.fDescent);
	EXPECT(font.fGetGlyph != nil && font.fMap != nil && font.fClose != nil);

	// one glyph, and a blank one for the space
	EXPECT(font.fMap(0x41, font.fCmap) == 0);
	EXPECT(font.fMap(' ', font.fCmap) == 0xffff);
	font.fGetGlyphInfo(0, 0, &font);
	Fixed advance = font.fGlyphAdvance;
	EXPECT(advance > 0);
	EXPECT(RoundFixed(advance) >= (long) info.fWidth);	// the slop is in it
	font.fGetGlyphInfo(' ', 0, &font);
	EXPECT(font.fGlyphAdvance > 0 && font.fGlyphAdvance < advance);

	// the glyph itself: the word drawn into a bitmap of its own
	font.fGetGlyph(0, 0, &font);
	EXPECT(font.fGlyphBits != nil);
	EXPECT(font.fGlyphWidth == RoundFixed(advance));
	EXPECT(font.fGlyphHeight == font.fAscent + font.fDescent);
	EXPECT(font.fGlyphBearingX == 0 && font.fGlyphBearingY == font.fAscent);
	EXPECT(font.fGlyphRowBytes * 8 >= font.fGlyphWidth);
	long inked = 0;
	for (long i = 0; i < font.fGlyphRowBytes * font.fGlyphHeight; i++)
		for (long bit = 0; bit < 8; bit++)
			if (font.fGlyphBits[i] & (1 << bit))
				inked++;
	EXPECT(inked > 20);			// a line of about forty pixels

	// and the space draws nothing
	font.fGetGlyph(' ', 0, &font);
	EXPECT(font.fGlyphBits == nil && font.fGlyphWidth == 0);

	CloseFont(&font);
	EXPECT(font.fInkGlyphBits == nil && font.fCmap == nil);

	// asked for at half the size the word is half as wide and the pen
	// comes from the size rather than from the word
	StyleRecord small;
	small.fFontFamily = word;
	small.fFontSize = ToFixed(info.fFontSize / 2);
	small.fFontFace = 0;
	small.fFontPattern = 0;
	small.fPattern = nil;
	small.fTransferMode = 0;
	FontEngineInfo smallFont;
	EXPECT(OpenFont(&gDrawMap, &small, 0x10000, 0x10000, &smallFont) == 0);
	smallFont.fGetGlyphInfo(0, 0, &smallFont);
	EXPECT(smallFont.fGlyphAdvance < advance);
	EXPECT(smallFont.fAscent < font.fAscent + 1);
	CloseFont(&smallFont);

	// the size and face a font spec asks for: an ink word answers its
	// own, which only a glyph made for it knows
	EXPECT(GetFontSize(word) == (long) info.fScaledFontSize);
	EXPECT(GetFontFace(word) == (long) info.fFace);
	EXPECT(GetFontSize(RefVar(MAKEINT(PackFont(2, 12, 1)))) == 12);
	EXPECT(GetFontFace(RefVar(MAKEINT(PackFont(2, 12, 1)))) == 1);

	list[0]->Dispose();
}


// Ink words in a picture: text whose runs' fonts are ink words is recorded
// with the words' bytes carried in the picture (0x81a2 styles with the
// family 0x800000, 0x81a4 the words) and played back to the same pixels.
// A single run whose font is an ink word is recorded the same way, but -
// a ROM bug, fixed by default - playing it back never puts the word back
// into the one style, so nothing is drawn (the ROM draws with whatever lies
// at the integer 0x800000); with the fix it is drawn as the several are.
static void
TestInkWordPicture()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();
	// vars.fonts as the boot makes it (a picture begins in the system font)
	InitFonts();
	RefVar fonts(AllocateFrame());
	RefVar fontList(Rromfontlist);
	for (long i = 0; i < Length(fontList); i++)
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), RefVar(GetArraySlotRef(fontList, i)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(RSSYMfonts), fonts);
	memset(gDrawBits, 0, sizeof(gDrawBits));
	OpenPort(&gDrawPort);
	SetPortBits(&gDrawMap);
	gDrawPort.portRect = gDrawMap.bounds;
	RectRgn(gDrawPort.visRgn, &gDrawMap.bounds);
	RectRgn(gDrawPort.clipRgn, &gDrawMap.bounds);

	TStroke* list[2];
	list[0] = MakeLine(10, 20, 40, 44, 12);
	list[1] = nil;
	Rect box;
	RefVar across(TStrokesToInkWord(list, &box));
	list[0]->Dispose();
	list[0] = MakeLine(10, 40, 40, 20, 12);
	RefVar up(TStrokesToInkWord(list, &box));
	list[0]->Dispose();
	InkWordInfo info;
	GetInkWordInfo(across, &info);
	StyleRecord first, second;
	first.fFontFamily = across;
	first.fFontSize = ToFixed(info.fScaledFontSize);
	first.fFontFace = 0;
	first.fFontPattern = 0;
	first.fPattern = nil;
	first.fTransferMode = 0;
	second = first;
	second.fFontFamily = up;
	StyleRecord* styles[2] = { &first, &second };
	const short runs[2] = { 1, 1 };
	const UniChar chars[2] = { 0xF700, 0xF700 };
	FPoint where = { ToFixed(4), ToFixed(40) };

	DrawTextOnce(chars, 2, styles, runs, where, nil, nil);
	unsigned char direct[sizeof(gDrawBits)];
	memcpy(direct, gDrawBits, sizeof(gDrawBits));
	EXPECT(DrawnPixels() > 20);

	memset(gDrawBits, 0, sizeof(gDrawBits));
	PicHandle picture = OpenPicture(&gDrawMap.bounds, false);
	DrawTextOnce(chars, 2, styles, runs, where, nil, nil);
	ClosePicture();
	EXPECT(DrawnPixels() == 0);
	// the words' bytes are in it: 0x81a4 follows the text
	long size = GetHandleSize((Handle) picture);
	const unsigned char* p = (const unsigned char*) *picture;
	long carried = 0;
	for (long i = 10; i + 1 < size; i += 2)
		if (p[i] == 0x81 && p[i + 1] == 0xa4)
			carried++;
	EXPECT(carried >= 1);
	Rect frame = gDrawMap.bounds;
	DrawPicture(picture, &frame, false);
	EXPECT(memcmp(direct, gDrawBits, sizeof(gDrawBits)) == 0);
	KillPicture(picture);

	// one run alone: recorded, and (the ROM bug) played back as nothing
	SetRomBugFixed(false);
	memset(gDrawBits, 0, sizeof(gDrawBits));
	StyleRecord* one[1] = { &first };
	picture = OpenPicture(&gDrawMap.bounds, false);
	DrawTextOnce(chars, 1, one, nil, where, nil, nil);
	ClosePicture();
	DrawPicture(picture, &frame, false);
	EXPECT(DrawnPixels() == 0);
	KillPicture(picture);
	SetRomBugFixed(true);
	memset(gDrawBits, 0, sizeof(gDrawBits));
	DrawTextOnce(chars, 1, one, nil, where, nil, nil);
	EXPECT(DrawnPixels() > 10);
	unsigned char single[sizeof(gDrawBits)];
	memcpy(single, gDrawBits, sizeof(gDrawBits));

	// the fix: one run alone is played back to the same pixels, and twice
	// running (the second text in the same style) as well
	memset(gDrawBits, 0, sizeof(gDrawBits));
	picture = OpenPicture(&gDrawMap.bounds, false);
	DrawTextOnce(chars, 1, one, nil, where, nil, nil);
	DrawTextOnce(chars, 1, one, nil, where, nil, nil);
	ClosePicture();
	DrawPicture(picture, &frame, false);
	EXPECT(memcmp(single, gDrawBits, sizeof(gDrawBits)) == 0);
	KillPicture(picture);
	ClosePort(&gDrawPort);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_Ink: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	// an ink word asks the user's preferences for its scale and its pen;
	// on a Newton the boot has set them long before anything makes one
	{
		RefVar config(AllocateFrame());
		SetFrameSlot(config, RefVar(RSSYMinkwordscaling), RefVar(MAKEINT(100)));
		SetFrameSlot(config, RefVar(RSSYMuserpensize), RefVar(MAKEINT(2)));
		SetFrameSlot(RefVar(gVarFrame), RefVar(RSSYMuserconfiguration), config);
	}

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
	TestEncodeWord();
	TestEncodeStroke();
	TestTrace();
	TestTransform();
	TestRepar();
	TestEncodeRun();
	TestStrokeRoundTrip();
	TestStrokesToInk();
	TestInkDraw();
	TestInkShapes();
	TestStrokeBundles();
	TestInkFont();
	TestInkWordPicture();
	TestInkWordFontParms();

	if (failures == 0)
		printf("test_Ink: all passed\n");
	else
		printf("test_Ink: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
