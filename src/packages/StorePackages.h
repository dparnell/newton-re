/*
	File:		packages/StorePackages.h

	Contains:	A package kept on a store: how it is written there and read
				back a page at a time.

				A package on a store is a large object whose root is a
				PackageRoot (stores/PackageObjects.cpp) - the index table
				(one page object per 0x400 bytes of the package), the name
				of the TStoreDecompressor that reads the pages, its
				parameters, the kind (1) and 'paok' once it is complete.
				Each page object is:

				  [C relocation data][frame relocation header][the page]

				  * the C relocation data (TCRelocationGenerator) - only in
				    a package with native code (the relocation flag): a
				    16-byte header (reserved, the page's entry count, the
				    page size, the address the package was linked at) and
				    the word offsets in the page that hold addresses, padded
				    to four;
				  * the frame relocation header (TFrameRelocationGenerator,
				    one big-endian word): where the first whole object of a
				    frames part starts in the page and where the frames end,
				    and what is left over of an object begun on the page
				    before - so that the page's pointer refs can be moved to
				    where it is mapped, whichever page is read first;
				  * the page, compressed or not.

				TStorePackageWriter makes them: TPackageIterator::Store
				hands it the package a chunk at a time (the directory, the
				relocation chunk, then the parts), and it fills a page, has
				its TCallbackCompressor compress it (the compressor's
				callback writing into the page object) and writes the
				relocation data and the page's id into the index.

				The decompressors read one page back: Simple (stored as it
				is), LZ (the LZ coder, with the shared 0x520-byte buffer as
				its parameter), Zippy (its own 0x408-byte buffer), each also
				as a Reloc variant that skips (and applies) the C relocation
				data first; which one a package gets depends on its flags
				(StorePackage: 0x10000000 uncompressed, 0x02000000 Zippy,
				0x04000000 relocation).  TStoreCompanderWrapper is the
				compander over one of them that the ROM domain manager maps
				a package with.

				TLOPackageStore is the TLrgObjStore that claims the six
				decompressor names, so that CreateLargeObject makes a
				package (AllocatePackage) when it is given one of them, and
				deleting, duplicating and backing it up go the default ways.

				DEVIATION: the host maps a package unrelocated (the pages
				as they were written - see kHostPageNotRelocated,
				stores/StoreCompander.h), which is also what the ROM's own
				BackupPackage reads them as (base 0); the relocation of a
				page to another base (RelocateFramesInPage, applying the C
				relocation) is NOT YET.  NOT YET: TXIPStoreCompander /
				TXIPPackageStore (a package executed in place), the backup
				progress callback (TLOCallback).

	The layouts have no DDK header; they follow the ROM's code.
	Reconstructed from the MP2x00 US ROM (0x00049e44-0x0004a1b4,
	0x000d17b0-0x000d1a50, 0x0015c88c-0x0015c8b0, 0x00161360-0x00161b68,
	0x001f9d1c-0x001fc2dc, 0x001015c4-0x0010176c); each function cites its
	origin.
*/

#ifndef __STOREPACKAGES_H
#define __STOREPACKAGES_H

#ifndef __STORECOMPANDER_H
#include "StoreCompander.h"
#endif
#ifndef __LARGEOBJECTS_H
#include "LargeObjects.h"
#endif
#include "Frames.h"
#ifndef __PACKAGEITERATOR_H
#include "PackageIterator.h"
#endif

class TCallbackCompressor;
class TDecompressor;


/*------------------------------------------------------------------------------
	C   r e l o c a t i o n
	(a package with native code: which words of each page hold addresses)
------------------------------------------------------------------------------*/

const long kCRelocationBlockHeaderSize = 0x10;	// before a page's offsets

// What goes in front of each page: the package's relocation entries picked
// out a page at a time.  (The ROM's is 0x18 bytes.)
class TCRelocationGenerator
{
public:
				TCRelocationGenerator();						// ROM 0x00049e44 __ct__21TCRelocationGeneratorFv
				~TCRelocationGenerator();						// ROM 0x00049e88 __dt__21TCRelocationGeneratorFv

	NewtonErr	Init(RelocationHeader* header, RelocationEntry* entries);	// ROM 0x00049e94 Init__21TCRelocationGeneratorFP16RelocationHeaderP15RelocationEntry
	long		GetRelocDataSizeForBlock(ULong block);			// ROM 0x00049ecc GetRelocDataSizeForBlock__21TCRelocationGeneratorFUl
	NewtonErr	GetRelocDataForBlock(ULong block, char** header, long* headerSize, char** offsets);	// ROM 0x00049f60 GetRelocDataForBlock__21TCRelocationGeneratorFUlPPcPlT2

	RelocationHeader*	fHeader;		// +0x00  nil: no relocation
	RelocationEntry*	fEntries;		// +0x04
	// +0x08  the block header, as it lies in front of a page: the chunk's
	// reserved word, the entry count (two bytes, big-endian), two bytes
	// never set, the page size and the address the package was linked at
	UByte		fBlockHeader[kCRelocationBlockHeaderSize];
	UByte*		fEntriesEnd;			// (host: see GetRelocDataSizeForBlock)
};


// What a page is relocated by once it is read.
class TCRelocator
{
public:
	virtual NewtonErr	Relocate(char* page, ULong base) = 0;
	virtual long		GetTheNextRelocEntry(void) = 0;
	virtual				~TCRelocator() {}
};

// The C relocation data in front of a page read back.  (0x134 bytes in the
// ROM: the offsets of a page fit in the block.)
class TSimpleCRelocator : public TCRelocator
{
public:
				TSimpleCRelocator();							// ROM 0x0004a0e8 __ct__17TSimpleCRelocatorFv
	virtual		~TSimpleCRelocator();							// ROM 0x0004a130 __dt__17TSimpleCRelocatorFv

	NewtonErr	Init(TStore* store, PSSId pageId, long* size);	// ROM 0x0004a03c Init__17TSimpleCRelocatorFP6TStoreUlPl
	NewtonErr	Relocate(char* page, ULong base);				// ROM 0x0004a148 Relocate__17TSimpleCRelocatorFPcUl
	long		GetTheNextRelocEntry(void);						// ROM 0x0004a1b4 GetTheNextRelocEntry__17TSimpleCRelocatorFv

	long		fSize;					// +0x04  the relocation data's size, header included
	long		fNext;					// +0x08  GetTheNextRelocEntry's place
	UByte		fBlockHeader[kCRelocationBlockHeaderSize];	// +0x0c
	UByte		fOffsets[0x118];		// +0x1c
};


/*------------------------------------------------------------------------------
	F r a m e   r e l o c a t i o n
------------------------------------------------------------------------------*/

// One big-endian word in front of every page (after the C relocation data):
//   bits 22-31  the first whole object's offset in the page, in words
//   bits 12-21  where the frames end in the page, in words
//   0x800       the page begins with the rest of an object begun before it
//   bits 9-10   ... of which that many header words (up to 3) came before
//   0x100       ... which is slotted (its words are refs)
//   0x080       ... whose last word is padding
//   0x040       the part's objects are aligned to four bytes (else eight)
struct FrameRelocationHeader
{
	UByte		fWord[4];
};

class TFrameRelocationGenerator
{
public:
				TFrameRelocationGenerator();					// ROM 0x000d17b0 __ct__25TFrameRelocationGeneratorFv
				TFrameRelocationGenerator(int aligned4);		// ROM 0x000d17f4 __ct__25TFrameRelocationGeneratorFi

	void		Update(long offsetInPage, char* data, long count, UChar isFrames);	// ROM 0x000d1844 Update__25TFrameRelocationGeneratorFlPcT1Uc
	void		GetHeader(FrameRelocationHeader* header);		// ROM 0x000d1a50 GetHeader__25TFrameRelocationGeneratorFP21FrameRelocationHeader

	ULong		fHeader;				// +0x00  the bits being made
	long		fFirstObject;			// +0x04  -1: none begun on this page yet
	long		fEnd;					// +0x08
	long		fObjectSize;			// +0x0c  the object being walked, aligned
	long		fRemaining;				// +0x10  of it, still to come
	UChar		fStarted;				// +0x14  the part's first chunk seen
	UChar		fSlotted;				// +0x15
	UChar		fPadded;				// +0x16
	UChar		fAlignment;				// +0x17  0: eight, 1: four, 2: not known yet
};


/*------------------------------------------------------------------------------
	W r i t i n g
------------------------------------------------------------------------------*/

class TStorePackageWriter
{
public:
				TStorePackageWriter();							// ROM 0x001fbdf0 __ct__19TStorePackageWriterFv
	virtual		~TStorePackageWriter();							// ROM 0x001fbe38 __dt__19TStorePackageWriterFv

	NewtonErr	Init(TStore* store, PSSId indexId, ULong packageSize, TCallbackCompressor* compressor,
					 RelocationHeader* relocationHeader, RelocationEntry* relocationEntries);	// ROM 0x001fbe90 Init__19TStorePackageWriterFP6TStoreUlT2P19TCallbackCompressorP16RelocationHeaderP15RelocationEntry
	NewtonErr	WriteCompressedData(void* data, long size);		// ROM 0x001fbf78 WriteCompressedData__19TStorePackageWriterFPvl
	NewtonErr	WriteChunk(char* data, long size, UChar isFrames);	// ROM 0x001fbff8 WriteChunk__19TStorePackageWriterFPclUc
	void		Abort(void);									// ROM 0x001fc264 Abort__19TStorePackageWriterFv
	NewtonErr	Flush(void);									// ROM 0x001fc2dc Flush__19TStorePackageWriterFv

	TCallbackCompressor*	fCompressor;	// +0x04  nil: the pages stored as they are
	char*		fPage;					// +0x08  0x400 bytes
	long		fFill;					// +0x0c  of the page
	long		fWritten;				// +0x10  compressed bytes in the page object so far
	ULong		fPageIndex;				// +0x14
	TStore*		fStore;					// +0x18
	PSSId		fIndexId;				// +0x1c
	PSSId		fPageId;				// +0x20  the page object being written
	long		fRelocSize;				// +0x24  of the C relocation data in front of it
	TCRelocationGenerator*		fCGenerator;		// +0x28
	TFrameRelocationGenerator*	fFrameGenerator;	// +0x2c
};

// A TCallbackCompressor's output handed to the writer (its refCon).
NewtonErr	StorePackageWriterCallback(void* writer, void* data, ULong size, Boolean isLast);	// ROM 0x001fbe8c callback__FUlPvlUc


/*------------------------------------------------------------------------------
	R e a d i n g
------------------------------------------------------------------------------*/

PROTOCOL TSimpleStoreDecompressor : public TStoreDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TSimpleStoreDecompressor);

	TSimpleStoreDecompressor*	New();			// ROM 0x001facf0 New__24TSimpleStoreDecompressorFv
	void		Delete();						// ROM 0x001fae48 Delete__24TSimpleStoreDecompressorFv
	NewtonErr	Init(TStore* store, ULong parameter);	// ROM 0x001fae4c Init__24TSimpleStoreDecompressorFP6TStoreUl
	NewtonErr	Read(ULong pageId, char* buffer, long count, ULong baseAddress);	// ROM 0x001fae58 Read__24TSimpleStoreDecompressorFUlPclT1

	TStore*		fStore;					// +0x10
};

PROTOCOL TSimpleRelocStoreDecompressor : public TStoreDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TSimpleRelocStoreDecompressor);

	TSimpleRelocStoreDecompressor*	New();		// ROM 0x001f9f0c New__29TSimpleRelocStoreDecompressorFv
	void		Delete();						// ROM 0x001f9f10 Delete__29TSimpleRelocStoreDecompressorFv
	NewtonErr	Init(TStore* store, ULong parameter);	// ROM 0x001f9f14 Init__29TSimpleRelocStoreDecompressorFP6TStoreUl
	NewtonErr	Read(ULong pageId, char* buffer, long count, ULong baseAddress);	// ROM 0x001f9f20 Read__29TSimpleRelocStoreDecompressorFUlPclT1

	TStore*		fStore;					// +0x10
};

PROTOCOL TLZStoreDecompressor : public TStoreDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TLZStoreDecompressor);

	TLZStoreDecompressor*	New();				// ROM 0x001f9d24 New__20TLZStoreDecompressorFv
	void		Delete();						// ROM 0x001fa024 Delete__20TLZStoreDecompressorFv
	NewtonErr	Init(TStore* store, ULong parameter);	// ROM 0x001fa38c Init__20TLZStoreDecompressorFP6TStoreUl
	NewtonErr	Read(ULong pageId, char* buffer, long count, ULong baseAddress);	// ROM 0x001fa5dc Read__20TLZStoreDecompressorFUlPclT1

	char*		fBuffer;				// +0x10  the shared buffer (the parameter)
	TDecompressor*	fDecompressor;		// +0x14
	TStore*		fStore;					// +0x18
};

PROTOCOL TLZRelocStoreDecompressor : public TStoreDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TLZRelocStoreDecompressor);

	TLZRelocStoreDecompressor*	New();			// ROM 0x001f9d3c New__25TLZRelocStoreDecompressorFv
	void		Delete();						// ROM 0x001f9d4c Delete__25TLZRelocStoreDecompressorFv
	NewtonErr	Init(TStore* store, ULong parameter);	// ROM 0x001f9d5c Init__25TLZRelocStoreDecompressorFP6TStoreUl
	NewtonErr	Read(ULong pageId, char* buffer, long count, ULong baseAddress);	// ROM 0x001f9dcc Read__25TLZRelocStoreDecompressorFUlPclT1

	char*		fBuffer;				// +0x10
	TDecompressor*	fDecompressor;		// +0x14
	TStore*		fStore;					// +0x18
};

PROTOCOL TZippyStoreDecompressor : public TStoreDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TZippyStoreDecompressor);

	TZippyStoreDecompressor*	New();			// ROM 0x001facb8 New__23TZippyStoreDecompressorFv
	void		Delete();						// ROM 0x001facc8 Delete__23TZippyStoreDecompressorFv
	NewtonErr	Init(TStore* store, ULong parameter);	// ROM 0x001facf4 Init__23TZippyStoreDecompressorFP6TStoreUl
	NewtonErr	Read(ULong pageId, char* buffer, long count, ULong baseAddress);	// ROM 0x001fad78 Read__23TZippyStoreDecompressorFUlPclT1

	char*		fBuffer;				// +0x10  its own, 0x408 bytes
	TDecompressor*	fDecompressor;		// +0x14
	TStore*		fStore;					// +0x18
};

PROTOCOL TZippyRelocStoreDecompressor : public TStoreDecompressor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TZippyRelocStoreDecompressor);

	TZippyRelocStoreDecompressor*	New();		// ROM 0x001faac0 New__28TZippyRelocStoreDecompressorFv
	void		Delete();						// ROM 0x001faad0 Delete__28TZippyRelocStoreDecompressorFv
	NewtonErr	Init(TStore* store, ULong parameter);	// ROM 0x001faaf8 Init__28TZippyRelocStoreDecompressorFP6TStoreUl
	NewtonErr	Read(ULong pageId, char* buffer, long count, ULong baseAddress);	// ROM 0x001fab78 Read__28TZippyRelocStoreDecompressorFUlPclT1

	char*		fBuffer;				// +0x10
	TDecompressor*	fDecompressor;		// +0x14
	TStore*		fStore;					// +0x18
};


// The compander a package is mapped with: the offset's page read through
// the decompressor the root names.  It cannot be written.
PROTOCOL TStoreCompanderWrapper : public TStoreCompander
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TStoreCompanderWrapper);

	TStoreCompanderWrapper*	New();				// ROM 0x001fa5cc New__22TStoreCompanderWrapperFv
	void		Delete();						// ROM 0x001fa6ac Delete__22TStoreCompanderWrapperFv

	NewtonErr	Init(TStore* store, ULong rootId, ULong arg3, UChar readOnly, UChar shared);	// ROM 0x001fa724 Init__22TStoreCompanderWrapperFP6TStoreUlT2UcT4
	NewtonErr	Init(TStore* store, char* decompressor, ULong rootId, ULong parameter);			// ROM 0x001fa730 Init__22TStoreCompanderWrapperFP6TStorePcUlT3
	ULong		BlockSize();					// ROM 0x001fa7e8 BlockSize__22TStoreCompanderWrapperFv
	NewtonErr	Read(ULong offset, char* buffer, long count, ULong page);	// ROM 0x001fa7f0 Read__22TStoreCompanderWrapperFUlPclT1
	NewtonErr	Write(ULong offset, char* buffer, long count, ULong page);	// ROM 0x001fa864 Write__22TStoreCompanderWrapperFUlPclT1
	void		DoTransactionAgainst(long arg, ULong page);					// ROM 0x001fa870 DoTransactionAgainst__22TStoreCompanderWrapperFlUl
	Boolean		IsReadOnly();					// ROM 0x001fa89c IsReadOnly__22TStoreCompanderWrapperFv

	TStore*		fStore;					// +0x10
	ULong		fRootId;				// +0x14
	PSSId		fIndexId;				// +0x18
	TStoreDecompressor*	fDecompressor;	// +0x1c
	char*		fName;					// +0x20  the decompressor's
};


/*------------------------------------------------------------------------------
	T L O P a c k a g e S t o r e
------------------------------------------------------------------------------*/

PROTOCOL TLOPackageStore : public TLrgObjStore
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TLOPackageStore);

	TLOPackageStore*	New();					// ROM 0x00102f68 New__15TLOPackageStoreFv
	void		Delete();						// ROM 0x00102f6c Delete__15TLOPackageStoreFv

	NewtonErr	Init();							// ROM 0x001015c4 Init__15TLOPackageStoreFv
	NewtonErr	Create(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters,
					   long parametersSize, TLOCallback* callback);		// ROM 0x001015cc Create__15TLOPackageStoreFPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
	NewtonErr	CreateFromCompressed(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters,
					   long parametersSize, TLOCallback* callback);		// ROM 0x001016c4 CreateFromCompressed__15TLOPackageStoreFPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
	NewtonErr	DeleteObject(TStore* store, PSSId id);					// ROM 0x00101714 DeleteObject__15TLOPackageStoreFP6TStoreUl
	NewtonErr	Duplicate(PSSId* newId, TStore* store, PSSId id, TStore* toStore);	// ROM 0x00101720 Duplicate__15TLOPackageStoreFPUlP6TStoreUlT2
	NewtonErr	Resize(TStore* store, PSSId id, ULong size);			// ROM 0x0010173c Resize__15TLOPackageStoreFP6TStoreUlT2
	long		StorageSize(TStore* store, PSSId id);					// ROM 0x00101748 StorageSize__15TLOPackageStoreFP6TStoreUl
	long		SizeOfStream(TStore* store, PSSId id, UChar compressed);	// ROM 0x00101754 SizeOfStream__15TLOPackageStoreFP6TStoreUlUc
	NewtonErr	Backup(CPipe* pipe, TStore* store, PSSId id, UChar compressed, TLOCallback* callback);	// ROM 0x0010176c Backup__15TLOPackageStoreFP5CPipeP6TStoreUlUcP11TLOCallback
};


/*------------------------------------------------------------------------------
	P a c k a g e s   o n   a   s t o r e
------------------------------------------------------------------------------*/

// The package read out of the pipe onto the store under root rootId (an
// object already made, grown to a PackageRoot here), its pages compressed
// by compressor and read back by the decompressor named.  ==> 1 for the
// patch package, which is loaded (not stored) and the store aborted.
NewtonErr	AllocatePackage(CPipe* pipe, TStore* store, PSSId rootId, char* decompressor, void* parameters, long parametersSize,
							TCallbackCompressor* compressor, TLOCallback* callback);	// ROM 0x00161360 AllocatePackage__FP5CPipeP6TStoreUlPcPvlP19TCallbackCompressorP11TLOCallback
NewtonErr	AllocatePackage(CPipe* pipe, TStore* store, PSSId rootId, char* decompressor, void* parameters, long parametersSize,
							TCallbackCompressor* compressor);	// ROM 0x001617b4 AllocatePackage__FP5CPipeP6TStoreUlPcPvlP19TCallbackCompressor
// ... stored, committed and installed: *packageId (0 for one only dispatched).
NewtonErr	NewPackage(CPipe* pipe, TStore* store, PSSId rootId, ULong* packageId, char* decompressor, void* parameters, long parametersSize,
					   TCallbackCompressor* compressor);	// ROM 0x001619f4 NewPackage__FP5CPipeP6TStoreUlPUlPcPvlP19TCallbackCompressor

// The decompressors, the wrapper, the companders and TLOPackageStore
// registered, and the shared LZ decompressor made.
void		InitializeStoreDecompressors(void);			// ROM 0x001fa9fc InitializeStoreDecompressors__Fv


/*------------------------------------------------------------------------------
	T h e   N e w t o n S c r i p t   s i d e   (StorePackageNatives.cpp)
------------------------------------------------------------------------------*/

NewtonErr	StorePackage(CPipe* pipe, TStore* store, TLOCallback* callback, ULong* id);	// ROM 0x003215f8 StorePackage__FP5CPipeP6TStoreP11TLOCallbackPUl
Ref			WrapPackage(ULong id, TStore* store);										// ROM 0x0032180c WrapPackage__FUlP6TStore
Ref			AllocatePackage(CPipe* pipe, RefArg storeObject, RefArg callback, ULong callbackFrequency, int activate);	// ROM 0x003218f4 AllocatePackage__FP5CPipeRC6RefVarT2Uli
Ref			AllocatePackage(CPipe* pipe, RefArg storeObject, RefArg parameters);	// ROM 0x00321a6c AllocatePackage__FP5CPipeRC6RefVarT2
Ref			SuckPackageThruPipe(CPipe* pipe, RefArg storeObject, RefArg callback, ULong callbackFrequency, int activate);	// ROM 0x00321234 SuckPackageThruPipe__FP5CPipeRC6RefVarT2Uli
Ref			SuckPackageThruPipe(CPipe* pipe, RefArg storeObject, RefArg parameters);	// ROM 0x00321258 SuckPackageThruPipe__FP5CPipeRC6RefVarT2
NewtonErr	NewPackage(CPipe* pipe, RefArg storeObject, RefArg callback, ULong callbackFrequency);	// ROM 0x0032125c NewPackage__FP5CPipeRC6RefVarT2Ul
Ref			GetPkgInfoFromVAddr(ULong address);										// ROM 0x003220a4 GetPkgInfoFromVAddr__FUl
Boolean		IsPackage(RefArg obj);													// ROM 0x00321ef8 IsPackage__FRC6RefVar (ROMPackages.cpp)
Ref			FSuckPackageFromBinary(RefArg rcvr, RefArg binary, RefArg parameters);	// store:SuckPackageFromBinary
void		RegisterStorePackageNatives(void);

#endif	/* __STOREPACKAGES_H */
