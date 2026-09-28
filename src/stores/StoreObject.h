/*
	File:		stores/StoreObject.h

	Contains:	How a frames object is kept as a store object: the format
				TStoreObjectWriter writes and TStoreObjectReader reads, the
				pipes they stream through (TStoreWritePipe, TStoreReadPipe -
				buffered, with the Unicode text compressor for strings), the
				precedent tables that turn shared references into back
				references (TPrecedentsForWriting, TPrecedentsForReading over
				TBucketArray), and StorePermObject/LoadPermObject/
				DeletePermObject, the entry points the soups use.

	A store object is a StoreObjectHeader (uniqueID, modTime, the id of the
	text object, hint chunks, flags, text size), the hint chunks (8 bytes
	each; NOT YET RECONSTRUCTED: TWordHintsHandler writes none) and then
	the object as a tagged byte stream:

		0  immediate: the ref itself as a long		1  character: byte
		2  character: 2 bytes						3  binary: length, class object, data
		4  array: length, class object, elements	5  array of class 'array: length, elements
		6  frame: map reference (3 bytes), slots	7  symbol: symbol reference (3 bytes)
		   in the map's order
		8  string: length, class object; the text	9  precedent: index (long)
		   goes to the text object
		10 nil										11 small rect: 4 bytes (top, left, bottom, right)
		12 large binary: id and class (LargeBinaries.h)

	A long is one byte 0..254 or 0xff and four bytes; every word is
	big-endian, as the MessagePad writes it (the header's words and the
	text's UniChars are converted on the host).  Every pointer object
	is a precedent (in the order met): an object met again is written as
	tag 9 and its index.  The strings' text is collected in a second
	object, compressed by TUnicodeCompressor (CompressionType 2).
	The ROM's layouts: TStoreWritePipe 0x230, TStoreReadPipe 0x120,
	TBucketArray 0x10, TStoreObjectWriter 0x4b0, TStoreObjectReader 0x258.
*/

#ifndef __STOREOBJECT_H
#define __STOREOBJECT_H

#ifndef __STOREWRAPPER_H
#include "StoreWrapper.h"
#endif
#ifndef __COMPRESSION_H
#include "Compression.h"
#endif
#ifndef __INTERPRETER_H
#include "Interpreter.h"
#endif

class CDynamicArray;

enum CompressionType
{
	kNoCompression = 1,
	kUnicodeCompression = 2			// TUnicodeCompressor/Decompressor, for a store object's text
};

// the tags
enum
{
	kSOImmediate = 0, kSOChar, kSOUniChar, kSOBinary, kSOArray, kSOPlainArray, kSOFrame,
	kSOSymbol, kSOString, kSOPrecedent, kSONil, kSOSmallRect, kSOLargeBinary
};

// the store object's header (0x10 bytes)
struct StoreObjectHeader
{
	Long32		fUniqueId;			// +0x00  the entry's _uniqueID (-1: none)
	ULong32		fModTime;			// +0x04  its _modTime (-1: none)
	StorePSSId	fTextBlockId;		// +0x08  the text object (0: none)
	UByte		fNumHints;			// +0x0c  hint chunks of 8 bytes following
	UByte		fFlags;				// +0x0d  1 has large binaries, 4 one of them is a string; hints handler id in bits 1 and 3
	UByte		fTextSizeHi;		// +0x0e  the text's size in bytes (big-endian 16 bits)
	UByte		fTextSizeLo;		// +0x0f

	int			GetHintsHandlerId(void) const;
	void		SetHintsHandlerId(int id);
	long		TextSize(void) const			{ return ((long) fTextSizeHi << 8) | fTextSizeLo; }

	// as it lies in the store: big-endian words (host: ByteOrder.h)
	void		ReadFrom(const void* bytes);
	void		WriteTo(void* bytes) const;
};
const long kStoreObjectHeaderSize = 0x10;
const long kStoreObjectHintChunkSize = 8;
const long kNumHintsHandlers = 2;

// What writes and tests the hint chunks of an entry.  NOT YET
// RECONSTRUCTED: TWordHintsHandler, the only one the ROM registers; the
// class is named here so that TestObjHints can ask whether there is
// one, which is what decides whether the hints are trusted.
class THintsHandler;
extern THintsHandler*	gHintsHandlers[kNumHintsHandlers];	// ROM 0x0c107998 gHintsHandlers
const UByte kSOFlagsHasLargeBinaries = 0x01;
const UByte kSOFlagsLargeBinaryIsString = 0x04;


/* -------------------------------------------------------------------------------
	TStoreWritePipe
	Writes bytes to a store object through a buffer: a small object is
	assembled whole in memory and written at Complete; a larger one is
	written piece by piece as the buffer fills.  With a compressor the
	bytes go through it first and its output goes to the object.
------------------------------------------------------------------------------- */

const long kStorePipeBufferSize = 0x200;

class TStoreWritePipe
{
public:
				TStoreWritePipe();
				~TStoreWritePipe();

	void		Init(TStoreWrapper* wrapper, PSSId id, long size, CompressionType compression);
	void		SetPosition(long position);
	char*		GetDataPtr(long offset);			// into the whole-object buffer (nil when not buffering)
	void		Write(char* data, long count);
	void		Flush(void);						// the buffer to the object
	void		Complete(void);						// everything written; the object sized
	TStoreWritePipe&	operator<<(UByte b);
	TStoreWritePipe&	operator<<(long l);			// 0..254 as one byte, else 0xff and four

	void		BufferToObject(char* data, long size);		// the whole object made (or replaced) from data
	void		WriteToStore(char* data, long count);
	NewtonErr	CompCallback(void* data, long count, Boolean isLast);	// the compressor's output

	TStoreWrapper*	fWrapper;			// +0x000
	PSSId		fObjectId;				// +0x004  -1: to be made
	long		fObjectSize;			// +0x008  -1: unknown
	TCallbackCompressor*	fCompressor;	// +0x00c
	long		fPosition;				// +0x010  in the object
	long		fBufferCount;			// +0x014  bytes waiting in fData
	char		fBuffer[kStorePipeBufferSize];	// +0x018
	char*		fData;					// +0x218  fBuffer, or one for the whole object
	long		fDataSize;				// +0x21c
	char*		fCompBuffer;			// +0x220  the compressor's output when buffering
	long		fCompBufferSize;		// +0x224
	long		fCompCount;				// +0x228
	Boolean		fBuffering;				// +0x22c  the whole object is assembled in fData
};


/* -------------------------------------------------------------------------------
	TStoreReadPipe
------------------------------------------------------------------------------- */

const long kStoreReadPipeBufferSize = 0x100;

class TStoreReadPipe
{
public:
				TStoreReadPipe(TStoreWrapper* wrapper, CompressionType compression);
				TStoreReadPipe(char* data, long size);		// over memory
				~TStoreReadPipe();

	void		SetPSSID(PSSId id);
	void		SetPosition(long position);
	void		Read(char* data, long count);
	void		Skip(long count);
	void		SkipUByte(void);
	TStoreReadPipe&	operator>>(UByte& b);
	TStoreReadPipe&	operator>>(long& l);

	long		ReadFromStore(char* data, long count);		// ==> what was read
	void		FillBuffer(void);
	NewtonErr	DecompCallback(void* data, long* count, Boolean* underflow);

	TStoreWrapper*	fWrapper;			// +0x000
	PSSId		fObjectId;				// +0x004
	TCallbackDecompressor*	fDecompressor;	// +0x008
	long		fObjectOffset;			// +0x00c  the next byte to read from the object
	long		fBytesLeft;				// +0x010  in the object
	long		fBufferPos;				// +0x014
	long		fBufferEnd;				// +0x018
	char		fBuffer[kStoreReadPipeBufferSize];	// +0x01c
	char*		fData;					// +0x11c  fBuffer, or the memory read from
};


/* -------------------------------------------------------------------------------
	Precedents
------------------------------------------------------------------------------- */

// An array of fixed-size elements in buckets of 64, so that it grows
// without moving what is there.
class TBucketArray
{
public:
				TBucketArray(long elementSize);
				~TBucketArray();

	void*		ElementAt(long index);
	void		SetNumberOfElements(long count);

	long		fElementSize;			// +0x00
	long		fNumElements;			// +0x04
	long		fNumBuckets;			// +0x08
	void**		fBuckets;				// +0x0c
};

// The objects written so far, each findable by its ref.  Element 0 is
// the trie's root; an object's index is its element number less one.
// DEVIATION: the ROM finds an object with a PATRICIA trie over the bits
// of its ref (Search, GenerateLinks); here a hash table over the same
// bucket array does the finding.  Registered with the collector: the
// refs are marked and updated, and the table rebuilt after a collection.
class TPrecedentsForWriting : public TBucketArray
{
public:
				TPrecedentsForWriting();
				~TPrecedentsForWriting();

	long		Append(RefArg obj);				// ==> its index
	long		Find(RefArg obj);				// -1 for none
	void		Reset(void);

	void		RebuildTable(void);
	void		MarkAllRefs(void);
	void		UpdateAllRefs(void);
	static void	GCOccured(void* refCon);
	static void	GCMark(void* refCon);
	static void	GCUpdate(void* refCon);

	Ref*		fHashTable;				// host: the index of each element by its ref
	long		fHashSize;
	long*		fHashIndexes;
};

class TPrecedentsForReading : public TBucketArray
{
public:
				TPrecedentsForReading();
				~TPrecedentsForReading();

	long		Append(RefArg obj);				// ==> its index
	void		Replace(long index, RefArg obj);
	Ref			Get(long index)					{ return *(Ref*) ElementAt(index); }
	void		Reset(void);

	void		MarkAllRefs(void);
	void		UpdateAllRefs(void);
	static void	GCMark(void* refCon);
	static void	GCUpdate(void* refCon);
};


// the shared sets (0x0c102a30, 0x0c102a34, 0x0c102a28, 0x0c102a2d): a writer or
// reader takes the shared one when it is free, else makes its own
extern TPrecedentsForWriting*	gPrecedentsForWriting;
extern Boolean					gPrecedentsForWritingUsed;
extern TPrecedentsForReading*	gPrecedentsForReading;
extern Boolean					gPrecedentsForReadingUsed;


/* -------------------------------------------------------------------------------
	The writer and the reader
------------------------------------------------------------------------------- */

class TStoreObjectWriter
{
public:
				TStoreObjectWriter(RefArg obj, TStoreWrapper* wrapper, PSSId id);
				~TStoreObjectWriter();

	PSSId		Write(void);					// ==> the object's id
	void		Prescan(void);					// the sizes: the stream's, the text's
	void		Scan(void);						// the stream written
	void		WriteLargeBinary(void);			// tag 12
	void		NextHintChunk(void);

	RefStruct	fRootObject;			// +0x000
	TRefStack	fStack;					// +0x004  the objects on the way down (the collector sees them)
	TStoreWrapper*	fWrapper;			// +0x014
	RefStruct	fObject;				// +0x018  the object being scanned
	PSSId		fObjectId;				// +0x01c  -1: a new object
	PSSId		fTextBlockId;			// +0x020
	TStoreWritePipe	fPipe;				// +0x024
	TStoreWritePipe	fTextPipe;			// +0x254
	TPrecedentsForWriting*	fPrecedents;	// +0x484
	long		fStreamSize;			// +0x488  what Prescan counted, plus the header and hints
	char*		fHeader;				// +0x48c  the header and hints (in the pipe's buffer or its own)
	long		fTextSize;				// +0x490  the strings' bytes
	UByte		fNumHints;				// +0x494
	long*		fHints;					// +0x498  the hint chunks
	long*		fHintChunk;				// +0x49c  the current one
	long		fHintWords;				// +0x4a0  words hinted in the current chunk
	long		fHintTextSize;			// +0x4a4  (what the hints handler was asked with)
	CDynamicArray*	fLargeBinaries;		// +0x4a8
	Boolean		fDuplicatedLargeBinary;	// +0x4ac
	Boolean		fHasLargeBinaries;		// +0x4ad
	Boolean		fLargeBinaryIsString;	// +0x4ae
};


class TStoreObjectReader
{
public:
				TStoreObjectReader(TStoreWrapper* wrapper, PSSId id, CDynamicArray** largeBinaries);
				~TStoreObjectReader();

	Ref			Read(void);						// the object, with _uniqueID and _modTime for an entry
	Ref			Scan(void);
	Boolean		EachLargeObjectDo(Boolean (*fn)(TStoreWrapper*, PSSId, long, void*), void* refCon);	// fn(wrapper, id, classRef, refCon) for each large binary; ==> true when fn stopped it

	long		fUniqueId;				// +0x000
	long		fModTime;				// +0x004
	TStoreReadPipe	fPipe;				// +0x008
	TStoreReadPipe	fTextPipe;			// +0x128
	TPrecedentsForReading*	fPrecedents;	// +0x248
	TStoreWrapper*	fWrapper;			// +0x24c
	CDynamicArray**	fLargeBinaries;		// +0x250
	RefStruct	fEntry;					// +0x254  the first frame read
};

Boolean	PackSmallRect(Ref frame, long* packed);		// {top, left, bottom, right} all 0..255
Ref		UnpackSmallRect(long packed);
void	WriteReference(char* bytes, long reference);	// 3 bytes, big-endian
void	WriteReference(TStoreWritePipe& pipe, long reference);
long	ReadReference(TStoreReadPipe& pipe);
TCallbackDecompressor*	NewDecompressor(CompressionType compression, DecompressorReadProcPtr readProc, void* refCon);

Ref		LoadPermObject(TStoreWrapper* wrapper, PSSId id, CDynamicArray** largeBinaries);
void	StorePermObject(RefArg obj, TStoreWrapper* wrapper, PSSId& id, CDynamicArray* largeBinaries, Boolean* duplicatedLargeBinary);
void	DeletePermObject(TStoreWrapper* wrapper, PSSId id);
PSSId	CopyPermObject(PSSId id, TStoreWrapper* from, TStoreWrapper* to);		// as it lies, the references translated
void	CopyObjectReferences(TStoreReadPipe& pipe, TStoreWrapper* from, TStoreWrapper* to);

/* -------------------------------------------------------------------------------
	T h e   t e x t   o f   a   s t o r e   o b j e c t

	Every string an entry holds is kept together in one object beside it,
	compressed, so that a search can read all of an entry's text without
	reading the entry back into frames.  That is what Find walks.
------------------------------------------------------------------------------- */

const long	kObjTextReadSize = 1000;		// the compressed bytes the object holds
const long	kObjTextBufferSize = 2000;		// ... and the text they come out as

// The two buffers are the object itself: a text that fits in them needs
// no allocation at all, and one that does not comes back in a buffer of
// its own - which is how the caller tells them apart.  (The ROM's is
// 0xbc8 bytes; this is the same shape.)
class TObjTextDecompressor
{
public:
				TObjTextDecompressor();					// ROM 0x002dfbe0 __ct__20TObjTextDecompressorFv
				~TObjTextDecompressor();				// ROM 0x002dfc30 __dt__20TObjTextDecompressorFv

	char*		Decompress(TStoreWrapper* wrapper, PSSId id, long* size);	// ROM 0x002dfe70 Decompress__20TObjTextDecompressorFP13TStoreWrapperUlPl
	char*		SlowDecompress(TStoreWrapper* wrapper, PSSId id, long* size);	// ROM 0x002dfdec SlowDecompress__20TObjTextDecompressorFP13TStoreWrapperUlPl
	const char*	Output(void) const				{ return fOutput; }

	static NewtonErr	TextDecompCallback(void* refCon, void* into, long* size, Boolean* underflow);	// ROM 0x002dfd78 TextDecompCallback__20TObjTextDecompressorFPvPlPUc

	char		fRead[kObjTextReadSize];		// +0x000  the compressed bytes, read whole
	char		fText[kObjTextBufferSize];	// +0x3e8  ... and the text they come out as
	TCallbackDecompressor*	fDecompressor;	// +0xbb8
	char*		fOutput;					// +0xbbc  fText
	long		fRemaining;					// +0xbc0  compressed bytes left to hand over
	long		fPosition;					// +0xbc4  where in fRead they are
};

// What a search does with an entry's text; ==> true to stop the walk.
typedef Boolean (*ObjTextProcPtr)(UniChar* text, long length, void* refCon);

struct ObjTextProcArgs
{
	ObjTextProcPtr	fProc;
	void*			fRefCon;
};

// Whether every bit the query wants is among the ones the entry has.
Boolean	TestHintBits(const long* wanted, const long* has);	// ROM 0x002e0b88 TestHintBits__FPlT1
// The hint chunks an entry carries tested against a query's words
// before its text is read at all.
Boolean	TestObjHints(const char* hints, long count, TStoreWrapper* wrapper, PSSId id);	// ROM 0x002dc934 TestObjHints__FPclP13TStoreWrapperUl
// All of an entry's text handed to a callback.
Boolean	WithPermObjectTextDo(TStoreWrapper* wrapper, PSSId id, ObjTextProcPtr proc, void* refCon, TObjTextDecompressor** decompressor);	// ROM 0x002e0008 WithPermObjectTextDo__FP13TStoreWrapperUlPFPUslPv_UcPvPPv

#endif	/* __STOREOBJECT_H */
