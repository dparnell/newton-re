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


// The decoder's context (the CIC library's _DCC, 0x1f0 bytes; the ROM
// offsets of the fields that are known are noted, but the struct is the
// host's own - it never leaves the codec).
struct CICDecoder
{
	InkPointProc	fSink;			// +0x00  where the points go
	void*			fSinkData;		// +0x04  the sink's own working store
	void*			fRefCon;		// +0x14  what the caller passed
	const UByte*	fData;			// +0x20  the bits
	ULong			fBitCount;		// +0x28  how many there are
	ULong			fBitPos;		// +0x2c  where the reader has got to
	ULong			fStrokeBits;	// +0x30  bits read, counted twice over
	ULong			fTotalBits;		// +0x34
	ULong			fCodeBook;		// +0x76  which code book the run uses
};


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
