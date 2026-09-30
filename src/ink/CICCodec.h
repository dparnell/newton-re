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

	The strokes themselves are read by ReadNewStroke, DecodeLongStroke
	and DecodeShortStroke over the code books (LockCodeBook,
	DecodeWord_NEW/OLD) and the segment quantisation; the encoder is
	EncoderRun and what it calls - all here.
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
// The encoder has its own pair, byte for byte the same as the reader's.
extern const unsigned short	kInkEncFormatCodes[12];	// ROM 0x0c105020
extern const unsigned short	kInkEncStrokeCodes[16];	// ROM 0x0c105038


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


// One point of the trace the encoder is working from (the CIC library's
// _ORG_P): where it is, the step from the point before, how long that
// step was, and how far along the stroke it is.  All of it is in the
// thousand-and-twenty-fourths the codec counts in.
struct CICTracePoint
{
	long	x;				// +0x00
	long	y;				// +0x04
	long	dx;				// +0x08  the step from the point before
	long	dy;				// +0x0c
	long	fLength;		// +0x10  how long that step was
	long	fArc;			// +0x14  and how far along the stroke this is
};

// How many of them the ROM's context has room for.  A stroke that
// reaches the last of them is told so.
const long kCICMaxTracePoints = 128;
const long kCICTraceFull = 0x7f;

// The shortest step worth keeping: a point nearer than this to the one
// before it is thrown away.
const long kCICShortestStep = 0x401;


// One of the nine places along a segment the fitting works at (the CIC
// library's _RPR_P): where the curve is there, and how far along the
// stroke that is.
struct CICSample
{
	long	x;				// +0x00
	long	y;				// +0x04
	long	fStep;			// +0x08  the distance between samples
	long	fAt;			// +0x0c  and where this one is
};

// How many of them there are.
const long kCICSamples = 9;


// The encoder's context (the CIC library's _CDC; as with the decoder's,
// the fields are the ROM's but the offsets are not).
struct CICEncoder
{
	UByte*			fOut;			// +0x38    where the bits go
	ULong			fError;			// +0x3c    what went wrong (a halfword)
	// (one more than there are, because Tracing writes the step and the
	// distance of the place *after* the one it is looking at, and so
	// runs one past the end.  In the ROM that tenth record is the first
	// of the resampled array, whose two spare fields are also where
	// StoreContext keeps the first sample's - so the ROM's Tracing
	// walks over the saved pair, and TestStrokeSeg does the same here.)
	CICSample		fSamples[kCICSamples + 1];	// +0x40   where the fitting is looking
	CICSample		fResampled[kCICSamples];	// +0xd0   and what it found there
	long			fCoefX[4];		// +0x160   the segment being fitted
	long			fCoefY[4];		// +0x170
	long			fWasX[4];		// +0x1a0   and what it was on the try before
	long			fWasY[4];		// +0x1b0
	CICTracePoint	fTrace[kCICMaxTracePoints];	// +0x1c0  the stroke being written
	ULong			fPointCount;	// +0xdc0   how many points it has (a halfword)
	ULong			fSavedCount;	// +0xdc2   and what it was at the last save
	long			fPenX;			// +0xdc4   where the pen is
	long			fPenY;			// +0xdc8
	long			fStrokeX;		// +0xdd4   where this stroke began
	long			fStrokeY;		// +0xdd8
	long			fLastX;			// +0xde4   and the last point written
	long			fLastY;			// +0xde8
	ULong			fBitLimit;		// +0xdfc   how many bits there is room for
	ULong			fHighWater;		// +0xe00   the furthest the writer has got
	ULong			fBitPos;		// +0xe04   and where it is now
	ULong			fUnit;			// +0xe10   (a halfword, always 1024)
	ULong			fOne;			// +0xe12   (a halfword, always 1)
	ULong			fSlack;			// +0xe14   (a halfword, always thirty)
	ULong			fSlack2;		// +0xe16   (likewise)
	long			fLimitA;		// +0xe18   the two lengths the book is measured in
	long			fLimitB;		// +0xe1c
	long			fStepA;			// +0xe20   what one step is worth
	long			fStep;			// +0xe24
	long			fError2;		// +0xe28   (the fitting's allowance)
	const void*		fTables[8];		// +0xe38 to +0xe54
	ULong			fBookNumber;	// +0xe58   the book in use (a halfword)
	ULong			fFirst;			// +0xe5a   this is the run's first stroke
	InkPointSource	fSource;		// +0x1c    where the points come from
	void*			fRefCon;		// +0x18    and what it is given
	ULong			fEndOfStroke;	// +0xdf8   the stroke has ended
	ULong			fDone;			// +0xdf4   and so has the run
	long			fMoved;			// +0xe5c   how far the last try moved the curve
	CICSample		fSavedSamples[kCICSamples];	// +0xd8, sharing fResampled's
								//          two spare fields - see StoreContext
};

// What the encoder puts in fError.
const ULong kCICNoRoom		= 5;
const ULong kCICNoBuffer	= 15;


// The transform the fitting turns on: nine samples of a segment into
// the four numbers that describe it, and back again.  The four are the
// segment's own - the same four ReadSegmentNear reads - so the first two
// come out of the endpoints once the other two are known.
//
// The ROM has each of them twice over, once for each coordinate.
void	RFFT_9_4_X(const CICSample* samples, long* coef, long first, long last);	// ROM 0x002836a4 RFFT_9_4_X__FP6_RPR_PPllT3
void	RFFT_9_4_Y(const CICSample* samples, long* coef, long first, long last);	// ROM 0x002837d8 RFFT_9_4_Y__FP6_RPR_PPllT3
void	RIFT_4_9_X(CICSample* samples, const long* coef);		// ROM 0x0028390c RIFT_4_9_X__FP6_RPR_PPl
void	RIFT_4_9_Y(CICSample* samples, const long* coef);		// ROM 0x00283a94 RIFT_4_9_Y__FP6_RPR_PPl


// ROM 0x00283c1c SQRT32__FUl
// A whole square root, a bit at a time.
long	SQRT32(ULong n);

// ROM 0x002802c4 AddPointToOdata__FP4_CDCP6_POINT
// A point added to the trace.  ==> 1 when it was taken, 0 when it was
// too near the one before, -1 when the trace is now full.
long	AddPointToOdata(CICEncoder* encoder, const InkPoint* pt);

// ROM 0x0027f8cc ResetParam__FP4_CDC
// The nine sample places spread evenly along the stroke so far.
void	ResetParam(CICEncoder* encoder);

// ROM 0x00280440 StoreContext__FP4_CDC
// Where the fitting had got to, kept and put back.
void	StoreContext(CICEncoder* encoder);
void	RestoreContext(CICEncoder* encoder);	// ROM 0x0028049c RestoreContext__FP4_CDC

// ROM 0x0027fde0 TestStrokeSeg__FP4_CDCUlUs
// The curve fitted to the trace, over and over until it settles or the
// tries run out.  ==> whether it came within the allowance.
Boolean	TestStrokeSeg(CICEncoder* encoder, ULong allowance, ULong tries);

// ROM 0x0027fffc WriteLongStroke__FP4_CDC
// A stroke written as a chain of segments, points pulled from the
// source as the fitting asks for them.
Boolean	WriteLongStroke(CICEncoder* encoder);

// ROM 0x0027f938 EncoderOpen__FUsUlT1T2T1
// A context made ready to write a block of ink.
Boolean	EncoderOpen(CICEncoder* encoder, InkPointSource source, void* refCon,
					UByte* out, long size, ULong book);

// ROM 0x002804f8 EncoderRun__FUl
// Stroke after stroke written into it.
Boolean	EncoderRun(CICEncoder* encoder);

// How much the fitting is allowed to be out by, and how many tries it
// gets - the numbers EcdrSelectCodeBook puts in the context.
const long kCICAllowance = 0xf0bc10;


// ROM 0x0028333c TryQuantVariant__FPlT1lT1
// How far a rounded segment is from the fitted one, in all four of its
// numbers.  ==> whether it came to less than the best so far, which is
// in `outError` either way.
Boolean	TryQuantVariant(const long* coef, const long* trial, long best, long* outError);

// ROM 0x0028279c SegVectQuant__FP4_CDCPsUi
// The three rounded numbers of one coordinate nudged, each by one
// either way, to whichever of the twenty-seven comes nearest.
void	SegVectQuant(CICEncoder* encoder, short* rounded, ULong which);

// ROM 0x0028303c WriteSegment__FP4_CDCs
// A segment written: where it ends and the four numbers that bend it,
// and the word that says whether the stroke goes on.
Boolean	WriteSegment(CICEncoder* encoder, short tag);


// ROM 0x00283424 Repar__FP6_ORG_PT1P6_RPR_PT3
// The nine places found on the stroke itself: each sample's distance
// along the curve, taken as the same fraction of the stroke's whole
// length, and the point that far along the trace.  ==> the ratio of the
// two lengths, as a fraction of twenty-four bits.
ULong	Repar(const CICTracePoint* last, const CICTracePoint* first,
			  const CICSample* samples, CICSample* out);

// ROM 0x00283d9c Tracing__FlP6_RPR_P
// The nine places measured along the curve they now sit on: how far
// each is from the one before, and how far along it is altogether.
void	Tracing(long count, CICSample* samples);

// ROM 0x002833c8 MSQError__FUsP6_RPR_PT2
// How far apart two sets of samples are, squared and summed.
long	MSQError(ULong count, const CICSample* a, const CICSample* b);


// ROM 0x00282758 QvantUN__FlT1
// A length in the encoder's units divided by a step, rounded to the
// nearest whole one - away from nought when it falls half way.
long	QvantUN(long value, long step);

// ROM 0x0028294c EcdrSelectCodeBook__FP4_CDC
// The book the run is to use, and what a step in it is worth; the
// mirror of DcdrSelectCodeBook.
Boolean	EcdrSelectCodeBook(CICEncoder* encoder);

// ROM 0x00282d84 WriteNewStroke__FP4_CDCs
// A stroke's kind and where it starts.
Boolean	WriteNewStroke(CICEncoder* encoder, short kind);

// ROM 0x00283240 WriteShortStroke__FP4_CDC
// And its points, as they were drawn.
Boolean	WriteShortStroke(CICEncoder* encoder);

// ROM 0x00280980 ConvertData__FPPvPUiUs
// Ink re-encoded in another code book without becoming points
// (CICConvert.cpp): *data and *size replaced by a new block and its length.
Boolean	ConvertData(void** data, ULong* size, UShort format);


// ROM 0x00282aa0 PutBits__FP4_CDCUlUs
// n bits of a value written where the writer has got to, least
// significant first.
Boolean	PutBits(CICEncoder* encoder, ULong value, ULong n);

// ROM 0x00282bb8 FindCodeWord__FsUsP9_CODEWORD
// Which entry of a code book's table holds a value, or -1.
long	FindCodeWord(short value, ULong count, const void* table);

// ROM 0x00282c04 EncodeWord_OLD__FP4_CDCsP10_CODETABLE
// A value written through one of a book's tables, escapes and all.
Boolean	EncodeWord_OLD(CICEncoder* encoder, short value, const void* table);

// ROM 0x00282d38 EncodeWord_NEW__FP4_CDCsP9_CODEWORD
// And through one of the static ones.
Boolean	EncodeWord_NEW(CICEncoder* encoder, short value, const unsigned short* table);


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
	virtual void*		Encode(InkPointSource source, void* refCon, long* outSize) const;
};

extern TCICInkCodec	gCICInkCodec;

// The codecs the machine starts with: the ROM's, and nothing else.
void	InitializeInkCodecs(void);

#endif	/* __CICCODEC_H */
