/*
	File:		text/TXChars.h

	Contains:	The text engine's character storage.

				`TXChars` is the face: count the characters, replace a
				range, copy a range out, get one character, find one, ask
				for a run of them to look at.  Everything above it - the
				formatter, the lines, the view - only ever talks to this.

				`TXChunkedChars` is the implementation that makes a long
				document possible: the text is kept in *chunks* of at most
				`chunkSize` characters (512 by default), and a `TXRanges`
				holds where each chunk ends.  An edit therefore touches
				one chunk, or two, rather than moving a whole document
				about; and because the chunks are handed out by three
				virtuals - `GetChunkPtr`, `AllocateChunks`,
				`RemoveChunks` - the same code works over a binary in the
				heap and over a large binary paged in from a store.

				`TXTextDescriptor` is how text is handed between them: it
				describes a source or a sink, which may be a plain UniChar
				buffer, a stream, or another `TXChars` at an offset.  One
				`CopyTo` moves characters between any two of those.

	Reconstructed from the MP2x00 US ROM (0x002315e8-0x00232c80,
	0x00234278-0x00234384); each function cites its origin.
*/

#ifndef __TXCHARS_H
#define __TXCHARS_H

#ifndef __TXARRAY_H
#include "TXArray.h"
#endif
#include "Unicode.h"

class TXChars;
class TXStream;
class TXTextDescriptor;


// Where a character is looked for, and what is found.  A carriage
// return is looked for as a line feed as well - which is how text that
// came in with either line ending is read the same way.
long	SearchChar(UniChar c, const UniChar* text, long count);		// ROM 0x00234278 SearchChar__FUsPCUsl
long	SearchCharBack(UniChar c, const UniChar* text, long count);	// ROM 0x002342d8 SearchCharBack__FUsPCUsl - `text` points past the run; ==> how far back
// The first character below 0x20, and which it was; -1 for none.
long	GetCtrlCharOffset(const UniChar* text, long count, UniChar* found);	// ROM 0x00234334 GetCtrlCharOffset__FPCUslPUs

// GetLineChars answers a run of at most this many characters, and when
// the run it wants crosses a chunk it gathers them here instead.
const long	kTXLineCharsMax	= 0x80;
extern UniChar	gTXLineCharsBuffer[kTXLineCharsMax];				// ROM 0x0c104d70 gTXLineCharsBuffer


// A source or a sink of text: one of a UniChar buffer, a stream, or
// another TXChars at an offset.  `fPosition` is how far through it the
// copying has got, and CopyTo moves it along.
class TXTextDescriptor
{
public:
					TXTextDescriptor();								// ROM 0x00232820 __ct__16TXTextDescriptorFv

	void			Set(TXStream* stream, long count);				// ROM 0x0023285c Set__16TXTextDescriptorFP8TXStreaml
	void			Set(UniChar* text, long count);					// ROM 0x00232874 Set__16TXTextDescriptorFPUsl
	void			Set(TXChars* chars, long offset, long count);	// ROM 0x002328fc Set__16TXTextDescriptorFP7TXCharslT2

	// `count` characters moved from this to `to`, both positions moved
	// along; ==> an error, or noErr.
	NewtonErr		CopyTo(TXTextDescriptor* to, long count);		// ROM 0x00232914 CopyTo__16TXTextDescriptorFP16TXTextDescriptorl

	TXChars*		fChars;			// +0x00
	UniChar*		fText;			// +0x04
	long			fCount;			// +0x08
	long			fPosition;		// +0x0c  characters copied so far (the offset, for fChars)
	TXStream*		fStream;		// +0x10
};


// The face everything above the storage talks to.
class TXChars : public TXVirtualObject
{
public:
					TXChars();										// ROM 0x002315e8 __ct__7TXCharsFv
	virtual			~TXChars();										// ROM 0x0023161c __dt__7TXCharsFv

	virtual long	Count(void) const = 0;
	// `count` characters at `at` replaced by everything `source` has.
	virtual NewtonErr Replace(long at, long count, TXTextDescriptor* source) = 0;
	virtual NewtonErr CopyTo(TXTextDescriptor* to, long at, long count) = 0;
	// A run of the text to look at, starting at `at`: ==> where it is
	// and, through `count`, how much of it there is.  ReleaseCharChunk
	// gives it back.
	virtual UniChar* AcquireCharChunk(long at, long* chunk, long* count) = 0;
	virtual void	ReleaseCharChunk(long chunk) = 0;
	// A run of at most 128 characters to lay a line out from - either
	// the text itself when it lies in one chunk (`chunk` comes back as
	// its index) or a copy in gTXLineCharsBuffer (`chunk` comes back -1).
	virtual UniChar* GetLineChars(long at, long count, long* chunk) = 0;
	virtual UniChar	GetChar(long at) = 0;
	virtual long	SearchChar(UniChar c, long at, long count) = 0;
	virtual long	SearchCharBack(UniChar c, long at, long count) = 0;
	virtual long	GetCtrlCharOffset(long at, long count, UniChar* found) = 0;
	virtual void	Compact(void);									// ROM 0x0023227c Compact__7TXCharsFv
};


// The text in chunks of at most `chunkSize` characters, with a TXRanges
// saying where each chunk ends.  The chunks themselves belong to the
// subclass: it supplies GetChunkPtr, AllocateChunks and RemoveChunks,
// which is what lets the same code work over a heap binary and over a
// large binary on a store.  The ROM's object is 0x10 bytes.
class TXChunkedChars : public TXChars
{
public:
					TXChunkedChars(int chunkSize);					// ROM 0x0023288c __ct__14TXChunkedCharsFi (0 or less: 512)
	virtual			~TXChunkedChars();								// ROM 0x00232a64 __dt__14TXChunkedCharsFv

	virtual long	Count(void) const;								// ROM 0x00232abc Count__14TXChunkedCharsCFv
	virtual NewtonErr Replace(long at, long count, TXTextDescriptor* source);	// ROM 0x00232ad0 Replace__14TXChunkedCharsFlT1P16TXTextDescriptor
	virtual NewtonErr CopyTo(TXTextDescriptor* to, long at, long count);	// ROM 0x00232074 CopyTo__14TXChunkedCharsFP16TXTextDescriptorlT2
	virtual UniChar* AcquireCharChunk(long at, long* chunk, long* count);	// ROM 0x002321c8 AcquireCharChunk__14TXChunkedCharsFlPlT2
	virtual void	ReleaseCharChunk(long chunk);					// ROM 0x0023226c ReleaseCharChunk__14TXChunkedCharsFl
	virtual UniChar* GetLineChars(long at, long count, long* chunk);	// ROM 0x00232280 GetLineChars__14TXChunkedCharsFlT1Pl
	virtual UniChar	GetChar(long at);								// ROM 0x00232158 GetChar__14TXChunkedCharsFl
	virtual long	SearchChar(UniChar c, long at, long count);		// ROM 0x00232374 SearchChar__14TXChunkedCharsFUslT2
	virtual long	SearchCharBack(UniChar c, long at, long count);	// ROM 0x0023244c SearchCharBack__14TXChunkedCharsFUslT2
	virtual long	GetCtrlCharOffset(long at, long count, UniChar* found);	// ROM 0x00232524 GetCtrlCharOffset__14TXChunkedCharsFlT1PUs
	virtual void	Compact(void);									// ROM 0x002325f4 Compact__14TXChunkedCharsFv
	// (TXChunkedChars's own: the base's vtable ends at Compact)
	virtual NewtonErr Preflight(long chunks);						// ROM 0x00232ac8 Preflight__14TXChunkedCharsFl

	// What the subclass supplies: the chunk's characters (`forWrite`
	// says they are about to be written to, `lock` that they must not
	// move), and the making and unmaking of chunks.
	virtual UniChar* GetChunkPtr(long chunk, Boolean forWrite, Boolean lock) = 0;	// (pure: vtable +0x34)
	virtual void	UnlockChunk(long chunk);						// ROM 0x00232ac4 UnlockChunk__14TXChunkedCharsFl
	virtual NewtonErr AllocateChunks(long at, long count) = 0;		// (pure: vtable +0x3c)
	virtual void	RemoveChunks(long at, long count) = 0;			// (pure: vtable +0x40)
	// `oldLen` characters at `at` of the chunk replaced by `newLen`
	// from the source; the chunk's end moves by the difference.
	virtual NewtonErr MungeChunk(long chunk, long at, long oldLen, TXTextDescriptor* source, long newLen);	// ROM 0x00231eac MungeChunk__14TXChunkedCharsFlN21P16TXTextDescriptorT1

	// The same, with the source being a run of another chunk.
	NewtonErr		MungeChunk(long chunk, long at, long oldLen, long srcChunk, long srcAt, long srcLen);	// ROM 0x00231fd8 MungeChunk__14TXChunkedCharsFlN51
	// The two chunks made one, when they will fit; ==> whether they did.
	Boolean			ConcatChunks(long a, long b);					// ROM 0x00231e30 ConcatChunks__14TXChunkedCharsFlT1
	void			Remove(long at, long count);					// ROM 0x00231c4c Remove__14TXChunkedCharsFlT1

	// The chunk lengths written out and read back: how a document is kept
	// on a store.  What is written is the default length repeated, with
	// only the chunks that differ from it named - see the .cpp.
	NewtonErr		WriteChunksRanges(TXStream* stream);			// ROM 0x002325f8 WriteChunksRanges__14TXChunkedCharsFP8TXStream
	NewtonErr		ReadChunksRanges(TXStream* stream);			// ROM 0x00232704 ReadChunksRanges__14TXChunkedCharsFP8TXStream

	// The three ways text is put in, tried in turn; each answers
	// whether it managed it.
	Boolean			InsertInChunk(long chunk, long at, TXTextDescriptor* source);	// ROM 0x00232c60 InsertInChunk__14TXChunkedCharsFlT1P16TXTextDescriptor
	Boolean			InsertUsingNearChunk(long chunk, long near, long at, TXTextDescriptor* source);	// ROM 0x00231634 InsertUsingNearChunk__14TXChunkedCharsFlN21P16TXTextDescriptor
	NewtonErr		InsertUsingExtraChunks(long chunk, long at, TXTextDescriptor* source);	// ROM 0x00231970 InsertUsingExtraChunks__14TXChunkedCharsFlT1P16TXTextDescriptor

	int				fChunkSize;		// +0x04  the most a chunk holds
	TXRanges*		fChunks;		// +0x08  where each chunk ends
	long			fCount;			// +0x0c  the characters in all of them
};

#endif	/* __TXCHARS_H */
