/*
	File:		text/TXStream.h

	Contains:	The text engine's byte streams.

				A `TXStream` is a position and three things a subclass
				supplies - how big it is, and how to read and write at
				the position.  The base turns those into `ReadBytes` and
				`WriteBytes`, which move the position along and stop at
				the end.

				Two of them exist.  `TXHandleStream` keeps its bytes in a
				`TXArray` of one-byte elements, which is the ordinary
				scratch stream; `TXBinaryStream` writes into a
				NewtonScript binary, growing it as it goes, which is how
				a document is turned into something a soup entry can
				hold.  Which of the two a piece of the engine gets is the
				*temporary stream factory*'s business: `TXGetTempStream`
				asks the registered factory for one big enough, and the
				ROM's own answers a handle stream for anything under four
				kilobytes and a compressed large binary on a store for
				anything larger.

				A `TXTextDescriptor` (text/TXChars.h) may name a stream at
				either end, so this is also how text gets into and out of
				the character storage.

	Reconstructed from the MP2x00 US ROM (0x0023e174-0x0023f1bc,
	0x00245ec8-0x002461bc); each function cites its origin.
*/

#ifndef __TXSTREAM_H
#define __TXSTREAM_H

#ifndef __TXARRAY_H
#include "TXArray.h"
#endif
#include "objects.h"
#include "NewtErrors.h"

// What ReadBytes answers when it ran off the end of the stream - it
// still reads what there was and moves the position, and this says that
// it is all there is.  (The ROM has no symbol for it.)
const NewtonErr	kTXErrEndOfStream	= ERRBASE_NEWT - 702;


// A position, and three things a subclass supplies.
class TXStream : public TXVirtualObject
{
public:
					TXStream();										// ROM 0x00245ec8 __ct__8TXStreamFv
	virtual			~TXStream();									// ROM 0x00245f04 __dt__8TXStreamFv

	virtual long	GetPosition(void) const;						// ROM 0x0024602c GetPosition__8TXStreamCFv
	virtual void	SetPosition(long at);							// ROM 0x00246034 SetPosition__8TXStreamFl
	virtual NewtonErr GetSize(long* size) = 0;
	virtual NewtonErr Write(const void* data, long count) = 0;
	virtual NewtonErr Read(void* into, long count) = 0;

	// Written at the position, which then moves past it.
	NewtonErr		WriteBytes(const void* data, long count);		// ROM 0x0024603c WriteBytes__8TXStreamFPCvl
	// Read from the position, which then moves past it; a read that ran
	// off the end reads what there was and answers kTXErrEndOfStream.
	NewtonErr		ReadBytes(void* into, long count);				// ROM 0x00246070 ReadBytes__8TXStreamFPvl

	long			fPosition;		// +0x04
};


// A stream over a TXArray of bytes: the scratch stream.
class TXHandleStream : public TXStream
{
public:
					TXHandleStream();								// ROM 0x002460fc __ct__14TXHandleStreamFv
	virtual			~TXHandleStream();								// ROM 0x00246150 __dt__14TXHandleStreamFv

	virtual NewtonErr GetSize(long* size);							// ROM 0x002461a8 GetSize__14TXHandleStreamFPl
	virtual NewtonErr Write(const void* data, long count);			// ROM 0x00245f1c Write__14TXHandleStreamFPCvl
	virtual NewtonErr Read(void* into, long count);					// ROM 0x00245f98 Read__14TXHandleStreamFPvl

	TXArray*		fBytes;			// +0x08  one-byte elements, thirty at a time
};


// A stream over a NewtonScript binary, grown as it is written to.
class TXBinaryStream : public TXStream
{
public:
	// `atEnd` starts the stream's size at the binary's length rather
	// than at nought - which is the difference between opening one to
	// read and making one to write.  `extra` is how much room past what
	// is needed the binary is grown to, and `trim` asks for it to be cut
	// back to what was written when the stream goes.
					TXBinaryStream(RefArg binary, Boolean atEnd, int extra, Boolean trim);	// ROM 0x0023e174 __ct__14TXBinaryStreamFRC6RefVarUciT2
	virtual			~TXBinaryStream();								// ROM 0x0023e21c __dt__14TXBinaryStreamFv

	virtual NewtonErr GetSize(long* size);							// ROM 0x0023e9b4 GetSize__14TXBinaryStreamFPl
	virtual NewtonErr Write(const void* data, long count);			// ROM 0x0023ec80 Write__14TXBinaryStreamFPCvl
	virtual NewtonErr Read(void* into, long count);					// ROM 0x0023f168 Read__14TXBinaryStreamFPvl

	RefStruct		fBinary;		// +0x08
	long			fSize;			// +0x0c  what has been written
	int				fExtra;			// +0x10  room kept past it
	Boolean			fTrim;			// +0x14  cut back to fSize at the end
};


// Where a piece of the engine gets a stream to work in.  The ROM
// registers TXNewtStreamFactory at startup; a port that wants its
// scratch space somewhere else registers its own.  (The ROM's object is
// four bytes and its vtable holds nothing but `Create`: it has no
// destructor, virtual or otherwise, and nothing ever deletes one.)
class TXTempStreamFactory
{
public:
					TXTempStreamFactory();							// ROM 0x00245fd8 __ct__19TXTempStreamFactoryFv
	virtual NewtonErr Create(TXStream** stream, long size) = 0;
};

// The ROM's own: a handle stream for anything under four kilobytes, a
// compressed large binary on a store for anything bigger.
class TXNewtStreamFactory : public TXTempStreamFactory
{
public:
	virtual NewtonErr Create(TXStream** stream, long size);			// ROM 0x0023efc8 Create__19TXNewtStreamFactoryFPP8TXStreaml
};

// Anything over this goes to a store rather than into the heap.
const long	kTXBigStream	= 0x1000;

extern TXTempStreamFactory*	gTXTempStreamFactory;					// ROM 0x0c104e8c gTXTempStreamFactory
void	TXSetTempStreamFactory(TXTempStreamFactory* factory);		// ROM 0x0024600c TXSetTempStreamFactory__FP19TXTempStreamFactory
TXTempStreamFactory* TXGetTempStreamFactory(void);					// ROM 0x0024601c TXGetTempStreamFactory__Fv

#endif	/* __TXSTREAM_H */
