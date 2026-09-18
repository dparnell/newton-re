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

				NOT YET RECONSTRUCTED: the read-only decompressors
				(TStoreDecompressor, TSimpleStoreDecompressor,
				TLZStoreDecompressor, the Zippy and reloc variants) and
				TStoreCompanderWrapper that drives them - they relocate the
				NewtonScript frames in the expanded page
				(RelocateFramesInPage, 0x000d1b50), which is a separate
				unit; TXIPStoreCompander and TPixelMapCompander.

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


// Register the companders with the protocol registry (a host subset of the
// ROM's InitializeStoreDecompressors, 0x001f824c) and make the shared LZ
// decompressor the companders can borrow.
void	InitializeStoreCompanders(void);

#endif	/* __STORECOMPANDER_H */
