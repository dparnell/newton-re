/*
	File:		stores/StoreCompander.h

	Contains:	The store companders - the compression layer a frames store
				keeps its data behind.  A soup's (or package's) large data
				is held on the underlying TStore as a series of fixed 0x400
				(kCompanderBlockSize) byte *blocks*, each in its own store
				object; a *chunk table* object holds the block objects' ids
				(one StorePSSId per block).  The compander maps a byte
				offset to its block (offset >> 10) and reads or writes that
				block's object:

				  * TSimpleStoreCompander keeps the blocks uncompressed;
				  * TLZStoreCompander compresses each block with the LZ
				    coder (compression/LZCompression.h) on Write and expands
				    it on Read - an empty (zero-length) block object reads
				    back as zeroes.

				Both are made by name through the protocol registry
				(TStoreCompander::New("TLZStoreCompander"), ...) and share
				the TStoreCompander interface: Init over a store and the
				root object that names the chunk table (a PackageRoot, whose
				first word is the chunk-table id), BlockSize, Read, Write,
				DoTransactionAgainst and IsReadOnly.

				A package kept on a store is read a 0x400-byte page at a
				time through a TStoreDecompressor (the interface below; the
				implementations - simple, LZ, Zippy and their relocating
				variants - and TStoreCompanderWrapper, the compander that
				drives one, are packages/StorePackages.h, because a page
				is relocated as it is read).

				TPixelMapCompander, a bitmap's, is below.

				NOT YET RECONSTRUCTED: TXIPStoreCompander.

	The interface has no DDK header; it follows the dispatch tables
	tools/newton-rom/analysis/classinfo.py decodes (docs/protocols/).
	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __STORECOMPANDER_H
#define __STORECOMPANDER_H

#ifndef __STORE_H
#include "Store.h"
#endif

class TCompressor;
class TDecompressor;


const long kCompanderBlockSize = 0x400;		// bytes of a store's data per block
const long kLZCompanderBufferSize = 0x520;	// a compressed 0x400 block, at worst, plus its framing


// The small root object a compander (or a large-object store) is built
// over: its first word is the id of the chunk table (the array of the
// block objects' ids).  fKind is 1 for a PackageRoot, 2 for a
// LargeObjectRoot.  0x14 bytes, as the ROM lays it (0x00162b30).
struct PackageRoot
{
	StorePSSId	fChunkTableId;		// +0x00  the object holding the block-object ids
	ULong32		fUnknown4;			// +0x04
	ULong32		fUnknown8;			// +0x08
	ULong32		fKind;				// +0x0c  1 = PackageRoot, 2 = LargeObjectRoot
	ULong32		fUnknown10;			// +0x10

				PackageRoot();		// ROM 0x001608a8 __ct__11PackageRootFv
};


// The default transaction hook a compander uses; the ROM's is a no-op.
long	LODefaultDoTransaction(TStore* store, ULong rootId, ULong chunkTableId, long arg, UChar flag);	// ROM 0x001fa8a4 LODefaultDoTransaction__FP6TStoreUlT2lUc

// The one shared LZ compressor/decompressor pair (and its scratch buffer)
// a compander may borrow rather than owning its own (ROM 0x001f80fc /
// 0x001f8208); InitializeStoreCompanders makes the shared decompressor.
long	GetSharedLZObjects(TCompressor** compressor, TDecompressor** decompressor, char** buffer, long* bufferSize);	// ROM 0x001fa8ac GetSharedLZObjects__FPP11TCompressorPP13TDecompressorPPcPl
void	ReleaseSharedLZObjects(TCompressor* compressor, TDecompressor* decompressor, char* buffer);					// ROM 0x001fa9b8 ReleaseSharedLZObjects__FP11TCompressorP13TDecompressorPc


// ---------------------------------------------------------------------------
//	The interface every compander implements.
// ---------------------------------------------------------------------------

PROTOCOL TStoreCompander : public TProtocol
{
public:
	static TStoreCompander*	New(const char* implementation);	// ROM 0x0038735c New__15TStoreCompanderSFPc
	void			Delete();										// ROM 0x00387388 Delete__15TStoreCompanderFv

	VIRTUAL NewtonErr	Init(TStore* store, ULong rootId, ULong arg3, UChar readOnly, UChar shared) ENDVIRTUAL;	// ROM 0x003873a4 Init__15TStoreCompanderFP6TStoreUlT2UcT4
	VIRTUAL ULong		BlockSize() ENDVIRTUAL;																		// ROM 0x003873b0 BlockSize__15TStoreCompanderFv
	VIRTUAL NewtonErr	Read(ULong offset, char* buffer, long count, ULong page) ENDVIRTUAL;						// ROM 0x003873bc Read__15TStoreCompanderFUlPclT1
	VIRTUAL NewtonErr	Write(ULong offset, char* buffer, long count, ULong page) ENDVIRTUAL;					// ROM 0x003873c8 Write__15TStoreCompanderFUlPclT1
	VIRTUAL void		DoTransactionAgainst(long arg, ULong page) ENDVIRTUAL;									// ROM 0x003873d4 DoTransactionAgainst__15TStoreCompanderFlUl
	VIRTUAL Boolean		IsReadOnly() ENDVIRTUAL;																// ROM 0x003873e0 IsReadOnly__15TStoreCompanderFv
};


// ---------------------------------------------------------------------------
//	The interface a package page reader implements: Init over the store
//	(and, for the LZ ones, a scratch buffer), Read(page object, into,
//	count, the address the page is mapped at - what it is relocated to).
// ---------------------------------------------------------------------------

// DEVIATION: the base the host's domain manager maps a package's pages at.
// On the MessagePad a page is read at the address it is mapped to and
// relocated there - its frames' pointer refs moved to that address and
// their import refs resolved (RelocateFramesInPage), its code's words by
// the relocation entries (TSimpleCRelocator).  The host imports a frames
// part out of the package's own, package-relative words and resolves its
// import refs itself (frames/FramesPart.h, packages/Units.h), and runs no
// package code; so it maps a package with the pages exactly as they were
// written, and this base - which no real mapping has - tells the
// relocators to leave a page alone.
const ULong kHostPageNotRelocated = 0;

PROTOCOL TStoreDecompressor : public TProtocol
{
public:
	static TStoreDecompressor*	New(const char* implementation);	// ROM 0x003872e4 New__18TStoreDecompressorSFPc
	void			Delete();										// ROM 0x00387310 Delete__18TStoreDecompressorFv

	VIRTUAL NewtonErr	Init(TStore* store, ULong parameter) ENDVIRTUAL;							// ROM 0x0038732c Init__18TStoreDecompressorFP6TStoreUl
	VIRTUAL NewtonErr	Read(ULong pageId, char* buffer, long count, ULong baseAddress) ENDVIRTUAL;	// ROM 0x00387338 Read__18TStoreDecompressorFUlPclT1
};


// ---------------------------------------------------------------------------
//	The blocks kept uncompressed.
// ---------------------------------------------------------------------------

PROTOCOL TSimpleStoreCompander : public TStoreCompander
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TSimpleStoreCompander);

	TSimpleStoreCompander*	New();				// ROM 0x001fa3fc New__21TSimpleStoreCompanderFv
	void			Delete();					// ROM 0x001fa40c Delete__21TSimpleStoreCompanderFv

	NewtonErr		Init(TStore* store, ULong rootId, ULong arg3, UChar readOnly, UChar shared);	// ROM 0x001fa410 Init__21TSimpleStoreCompanderFP6TStoreUlT2UcT4
	ULong			BlockSize();				// ROM 0x001fa470 BlockSize__21TSimpleStoreCompanderFv
	NewtonErr		Read(ULong offset, char* buffer, long count, ULong page);	// ROM 0x001fa478 Read__21TSimpleStoreCompanderFUlPclT1
	NewtonErr		Write(ULong offset, char* buffer, long count, ULong page);	// ROM 0x001fa52c Write__21TSimpleStoreCompanderFUlPclT1
	void			DoTransactionAgainst(long arg, ULong page);					// ROM 0x001fa590 DoTransactionAgainst__21TSimpleStoreCompanderFlUl
	Boolean			IsReadOnly();				// ROM 0x001fa5bc IsReadOnly__21TSimpleStoreCompanderFv

	TStore*			fStore;				// +0x10
	ULong			fRootId;			// +0x14  the root object (its first word names the chunk table)
	PSSId			fChunkTableId;		// +0x18  the object of block-object ids
	Boolean			fReadOnly;			// +0x1c
};


// ---------------------------------------------------------------------------
//	The blocks LZ-compressed.
// ---------------------------------------------------------------------------

PROTOCOL TLZStoreCompander : public TStoreCompander
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TLZStoreCompander);

	TLZStoreCompander*	New();				// ROM 0x001fa03c New__17TLZStoreCompanderFv
	void			Delete();					// ROM 0x001fa054 Delete__17TLZStoreCompanderFv

	NewtonErr		Init(TStore* store, ULong rootId, ULong arg3, UChar readOnly, UChar shared);	// ROM 0x001fa0a8 Init__17TLZStoreCompanderFP6TStoreUlT2UcT4
	ULong			BlockSize();				// ROM 0x001fa1c8 BlockSize__17TLZStoreCompanderFv
	NewtonErr		Read(ULong offset, char* buffer, long count, ULong page);	// ROM 0x001fa1d0 Read__17TLZStoreCompanderFUlPclT1
	NewtonErr		Write(ULong offset, char* buffer, long count, ULong page);	// ROM 0x001fa2ac Write__17TLZStoreCompanderFUlPclT1
	void			DoTransactionAgainst(long arg, ULong page);					// ROM 0x001fa340 DoTransactionAgainst__17TLZStoreCompanderFlUl
	Boolean			IsReadOnly();				// ROM 0x001fa36c IsReadOnly__17TLZStoreCompanderFv

	void*			fBuffer;			// +0x10  scratch for a compressed block (kLZCompanderBufferSize)
	TDecompressor*	fDecompressor;		// +0x14
	TCompressor*	fCompressor;		// +0x18  nil when read-only
	TStore*			fStore;				// +0x1c
	ULong			fRootId;			// +0x20
	PSSId			fChunkTableId;		// +0x24
	Boolean			fOwnsCoders;		// +0x28  its own compressor/decompressor (vs the shared ones)
};


// TPixelMapCompander - the default compander of a bitmap kept on a store
// (MakeBitmap's): LZ over 1K pages, each page first *row-delta filtered* -
// every word XORed with the word one row above it (the row length from a
// copy of the bitmap's PixelMap kept in the compander's parameter object,
// made on the first write) - so that the runs a drawing leaves compress.
// The ROM's instance is 0x4c bytes; it is in the QuickDraw part of the ROM
// (0x0018a95c-0x0018b22c) and QuickDraw's InitGraf registers it
// (InitQDCompression).  PixelMapCompander.cpp.
PROTOCOL TPixelMapCompander : public TStoreCompander
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TPixelMapCompander);

	TPixelMapCompander*	New();			// ROM 0x0018a984 New__18TPixelMapCompanderFv
	void			Delete();					// ROM 0x0018a9a0 Delete__18TPixelMapCompanderFv

	NewtonErr		Init(TStore* store, ULong rootId, ULong headerId, UChar readOnly, UChar shared);	// ROM 0x0018a9a4 Init__18TPixelMapCompanderFP6TStoreUlT2UcT4
	ULong			BlockSize();				// ROM 0x0018ab9c BlockSize__18TPixelMapCompanderFv
	NewtonErr		Read(ULong offset, char* buffer, long count, ULong objectBase);	// ROM 0x0018ac1c Read__18TPixelMapCompanderFUlPclT1
	NewtonErr		Write(ULong offset, char* buffer, long count, ULong objectBase);	// ROM 0x0018ae9c Write__18TPixelMapCompanderFUlPclT1
	void			DoTransactionAgainst(long arg, ULong page);					// ROM 0x0018b200 DoTransactionAgainst__18TPixelMapCompanderFlUl
	Boolean			IsReadOnly();				// ROM 0x0018a97c IsReadOnly__18TPixelMapCompanderFv

	void			DisposeAllocations();		// ROM 0x0018aba4 DisposeAllocations__18TPixelMapCompanderFv

	TStore*			fStore;				// +0x10
	ULong			fRootId;			// +0x14
	PSSId			fChunkTableId;		// +0x18
	Boolean			fReadOnly;			// +0x1c
	TDecompressor*	fDecompressor;		// +0x20
	TCompressor*	fCompressor;		// +0x24
	char*			fBuffer;			// +0x28  scratch for a compressed page
	long			fBufferSize;		// +0x2c
	PSSId			fHeaderId;			// +0x30  the parameter object: the header (0x2c bytes, big-endian)
	UByte*			fHeader;			// +0x34  ... read in: 0x2c, then a Newton PixelMap (0x1c bytes)
	long			fCachedCount;		// +0x38  the page size the next three are for (-1: none)
	long			fXorWords;			// +0x3c  words XORed: the whole rows in a page, less the first
	long			fTailBytes;			// +0x40  bytes after the last whole row
	long			fRowWords;			// +0x44  the bitmap's rowBytes / 4
	Boolean			fOwnsCoders;		// +0x48  its own LZ coders (vs the shared ones)
};

void	InitQDCompression(void);		// ROM 0x0018a964 InitQDCompression__Fv - TPixelMapCompander registered (InitGraf)


// Register the companders with the protocol registry (a host subset of the
// ROM's InitializeStoreDecompressors, 0x001f824c) and make the shared LZ
// decompressor the companders can borrow.
void	InitializeStoreCompanders(void);

#endif	/* __STORECOMPANDER_H */
