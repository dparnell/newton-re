/*
	File:		ink/CICCodec.h

	Contains:	The ROM's ink codec - the CIC handwriting library's, not
				Apple's - as an implementation of TInkCodec.

				The ROM's shape is a context, opened and closed round a
				run: DecoderOpen (0x0028240c) makes one, DecoderRun
				(0x00282518) reads stroke after stroke out of the bit
				stream and hands the points to the context's point
				proc, and DecoderClose (0x002826a0) takes it down;
				EncoderOpen/Run/Close (0x0027f938, 0x002804f8,
				0x0027fae8) are the other way about, pulling points from
				a proc and writing bits.  Both contexts are the CIC
				library's _DCC and _CDC; the reconstruction keeps their
				fields but not their byte offsets, because nothing
				outside the codec ever sees one.

				The bit stream is read least significant bit first
				within each byte, and the bits of a value come out in
				that order too (GetNBit).

	NOT YET RECONSTRUCTED: everything but the bit reader.  The strokes
	themselves are read by ReadNewStroke (0x00280f1c), DecodeLongStroke
	(0x00281dd0) and DecodeShortStroke (0x002820c8) over the code books
	(LockCodeBook 0x002808d4, DecodeWord_NEW/OLD 0x00281b90,
	0x00281c48) and the segment quantisation; the encoder is
	EncoderRun (0x002804f8) and what it calls.
*/

#ifndef __CICCODEC_H
#define __CICCODEC_H

#ifndef __INKCODEC_H
#include "InkCodec.h"
#endif


// The point thinner a decoded stroke goes through (the CIC library's
// tag_SKP).  The plane is cut into cells eight units square; of all the
// points that fall in one cell only the one nearest its middle is kept,
// and three cells are held back so that three in a row making a single
// step across the diagonal can have their middle thrown away - which is
// what takes the staircase off a line drawn nearly straight.
struct CICSkipPoints
{
	long		fStarted;		// +0x00  the filter is in use
	short		fHalfCell;		// +0x04  four: the middle of a cell
	short		fCellSize;		// +0x06  eight: and how big one is
	InkPoint	fIn;			// +0x08  the point coming in
	InkPoint	fCell;			// +0x0c  the cell it falls in
	InkPoint	fOut;			// +0x10  the point going out
	long		fDist;			// +0x14  how far it is from its cell's middle
	long		fBest;			// +0x18  and the best so far in that cell
	InkPoint	fCells[3];		// +0x1c  the cells held
	InkPoint	fPoints[3];		// +0x28  and the point kept in each
	long		fCount;			// +0x34  whether one is ready to come out
	short		fIndex;			// +0x38  how many are held, less one
};


// The decoder's context (the CIC library's _DCC, 0x1f0 bytes; the ROM
// offsets of the fields that are known are noted, but the struct is the
// host's own - it never leaves the codec).
struct CICDecoder
{
	InkPointProc	fSink;			// +0x00  where the points go
	void*			fSinkData;		// +0x04  the sink's own working store
	ULong			fError;			// +0x08  the stream ran out (a halfword)
	void*			fRefCon;		// +0x14  what the caller passed
	const UByte*	fData;			// +0x20  the bits
	ULong			fBitCount;		// +0x28  how many there are
	ULong			fBitPos;		// +0x2c  where the reader has got to
	ULong			fStrokeBits;	// +0x30  bits read, counted twice over
	ULong			fTotalBits;		// +0x34
	long			fX;				// +0x38  where the pen is, in the book's units
	long			fY;				// +0x3c
	ULong			fUnit;			// +0x40  (a halfword, always 1024)
	ULong			fOne;			// +0x42  (a halfword, always 1)
	long			fLimitA;		// +0x44  two lengths the book is measured in
	long			fLimitB;		// +0x48
	long			fScaleA;		// +0x4c
	long			fScale;			// +0x50  what one decoded step is worth
	const void*		fTables[8];		// +0x54 to +0x70  the book's eight tables
	ULong			fBookNumber;	// +0x74  the book in use (a halfword)
	ULong			fMode;			// +0x76  the mode the run was opened with
	ULong			fFirst;			// +0x78  the run has not started yet
	ULong			fPointCount;	// +0x7a  how many points the stroke came to
	long			fPointsX[33];	// +0x7c  and where they are, in units of
	long			fPointsY[33];	// +0x100  a thousand and twenty-fourth
	long			fSegX[4];		// +0x184  the segment being drawn out
	long			fSegY[4];		// +0x194
	long			fSegStartX;		// +0x1a4  where it begins
	long			fSegStartY;		// +0x1a8
	long			fSegEndX;		// +0x1ac  and where it ends
	long			fSegEndY;		// +0x1b0
	CICSkipPoints	fSkip;			// +0x1b4  the thinner the points go through
};

// How many points either buffer holds - the ROM's are the 33 longs
// between the fields that follow them.
const long kCICMaxPoints = 33;


// A code word table as the ROM keeps one: four halfwords to an entry -
// the value, how many bits its code is, and the code in two halves - and
// a zero length ends it.  The entries are in order of length, which is
// what lets the decoder read a bit at a time and only look at the
// entries whose codes are as long as what it has.
inline short	CodeWordValue(const unsigned short* t, long i)	{ return (short) t[i * 4]; }
inline ULong	CodeWordLength(const unsigned short* t, long i)	{ return t[i * 4 + 1]; }
inline ULong	CodeWordCode(const unsigned short* t, long i)	{ return ((ULong) t[i * 4 + 2] << 16) | t[i * 4 + 3]; }

extern const unsigned short	kInkFormatCodes[12];	// ROM 0x0c104fe8
extern const unsigned short	kInkStrokeCodes[16];	// ROM 0x0c105000


// A table of a code book (the CIC library's _CODETABLE), which is a
// block of big-endian halfwords in the ROM: a twelve-byte header and
// then eight bytes an entry, the same shape as a code word.  The
// header's first halfword is the table's whole size, which is how the
// eight tables of a book are found one after another; the second is how
// many entries it has.  The other four are two escape values and the
// base each of them counts from - a value of 30000 means "too big to
// hold, read another word and add it to this base", and -30000 the same
// downwards.
const long kCodeTableHeaderSize = 12;
const long kCodeTableEntrySize = 8;

ULong	CodeTableSize(const void* table);
ULong	CodeTableCount(const void* table);
short	CodeTableBaseDown(const void* table);
short	CodeTableBaseUp(const void* table);
short	CodeTableEscapeUp(const void* table);
short	CodeTableEscapeDown(const void* table);
short	CodeTableValue(const void* table, long i);
ULong	CodeTableLength(const void* table, long i);
ULong	CodeTableCode(const void* table, long i);


// ROM 0x00280df0 DcdrSelectCodeBook__FP4_DCC
// The book the run is to use opened, and its eight tables found.
Boolean	DcdrSelectCodeBook(CICDecoder* decoder);

// ROM 0x00281b90 DecodeWord_NEW__FP4_DCCP9_CODEWORDPs
// One word out of a static code word table.
Boolean	DecodeWord_NEW(CICDecoder* decoder, const unsigned short* table, short* out);

// ROM 0x00281c48 DecodeWord_OLD__FP4_DCCP10_CODETABLEPs
// One word out of a code book's table, escapes followed.
Boolean	DecodeWord_OLD(CICDecoder* decoder, const void* table, short* out);

// ROM 0x00280f1c ReadNewStroke__FP4_DCCPs
// The next stroke's kind, and the pen moved to where it starts.
Boolean	ReadNewStroke(CICDecoder* decoder, short* outKind);

// ROM 0x00281240 ReadSegmentNear__FP4_DCCPs
// One segment of a long stroke read: where it ends and the four numbers
// that bend it.  ==> and the word that says whether the stroke goes on.
Boolean	ReadSegmentNear(CICDecoder* decoder, short* outTag);

// ROM 0x00281a70 RestoreSegment__FPlT1
// A segment's four numbers opened out into seventeen points.
void	RestoreSegment(long* points, const long* seg);

// How many points a segment comes to, and which of them is its middle.
const long kCICSegmentPoints = 17;
const long kCICSegmentMiddle = 8;


// ROM 0x00281424 ReadShortStroke__FP4_DCC
// A short stroke's points collected into the context's two buffers.
Boolean	ReadShortStroke(CICDecoder* decoder);

// ROM 0x002820c8 DecodeShortStroke__FP4_DCC
// And handed to the sink.
Boolean	DecodeShortStroke(CICDecoder* decoder);


// ROM 0x0028153c GetSkipPoint__FP7tag_SKPsT2
// A point offered to the thinner.  ==> whether one has come out the far
// end, which is then in fOut.
Boolean	GetSkipPoint(CICSkipPoints* skip, short x, short y);

// ROM 0x002819a0 ClearSkipPoint__FP7tag_SKP
// The next point still held, at the end of a stroke.
Boolean	ClearSkipPoint(CICSkipPoints* skip);

// ROM 0x00281dd0 DecodeLongStroke__FP4_DCC
// A long stroke read segment by segment and handed to the sink.
Boolean	DecodeLongStroke(CICDecoder* decoder);

// ROM 0x0028240c DecoderOpen__FUsUlT1T2T1
// A context made ready to read a block of ink.
void	DecoderOpen(CICDecoder* decoder, const void* data, long size,
					InkPointProc sink, void* refCon, ULong mode);

// ROM 0x00282518 DecoderRun__FUl
// Stroke after stroke read out of it.
Boolean	DecoderRun(CICDecoder* decoder);


// What ReadNewStroke answers: a long stroke, a short one, or the end.
const short kCICLongStroke	= 0;
const short kCICShortStroke	= 1;
const short kCICEndOfGroup	= 2;


// The two code books, and who has them open.  A book is a block of
// bytes in the ROM; InitializeParagraphCompression is what says where.
struct BookEntry;

extern void*	gCodeBook;		// ROM 0x0c104fc8 globalCodeBookPtr - the writing book
extern void*	gInkCodeBook;	// ROM 0x0c104fcc globalCodeBookPtrInk - the ink book

void	InitializeParagraphCompression(void);	// ROM 0x001543a4 InitializeParagraphCompression__Fv
void*	LockBook(const char* name, BookEntry* entry);	// ROM 0x002806cc LockBook__FPcP10_BOOKENTRY
Boolean	UnlockBook(BookEntry* entry);			// ROM 0x002808ac UnlockBook__FP10_BOOKENTRY
void*	LockCodeBook(ULong which);				// ROM 0x002808d4 LockCodeBook__FUs
Boolean	UnlockCodeBook(ULong which);			// ROM 0x00280914 UnlockCodeBook__FUs
ULong	CodeBookUseCount(ULong which);			// host: what the tests ask


// ROM 0x00280d88 GetNBit__FP4_DCCUs
// The next n bits, least significant first.
ULong	GetNBit(CICDecoder* decoder, ULong n);


// The ROM's codec.
class TCICInkCodec : public TInkCodec
{
public:
	virtual const char*	Name(void) const;
	virtual Boolean		CanDecode(long format) const;
	virtual Boolean		CanEncode(void) const;
	virtual Boolean		Decode(const void* data, long size, ULong group,
							   InkPointProc sink, void* refCon) const;
	virtual void*		Encode(TStroke** strokes, long* outSize) const;
};

extern TCICInkCodec	gCICInkCodec;

// The codecs the machine starts with: the ROM's, and nothing else.
void	InitializeInkCodecs(void);

#endif	/* __CICCODEC_H */
