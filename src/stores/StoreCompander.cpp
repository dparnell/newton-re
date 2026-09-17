/*
	File:		stores/StoreCompander.cpp

	Contains:	The store companders (StoreCompander.h): the uncompressed
				TSimpleStoreCompander and the LZ-compressed
				TLZStoreCompander, the TStoreCompander interface's
				New(char*)/Delete() glue, the shared-LZ-objects helper and
				the companders' registration.  A compander keeps a store's
				data as fixed 0x400-byte blocks, one store object per block,
				indexed by a chunk-table object; the NewPtr and operator new
				here are the memory manager's.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "StoreCompander.h"
#include "Compression.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

#include <string.h>


// ---------------------------------------------------------------------------
//	PackageRoot and the transaction / shared-objects helpers.
// ---------------------------------------------------------------------------

// ROM 0x00162b30 __ct__11PackageRootFv
PackageRoot::PackageRoot()
{
	fChunkTableId = 0;
	fUnknown4 = 0;
	fUnknown8 = 0;
	fKind = 1;
	fUnknown10 = 0;
}


// ROM 0x001f80f4 LODefaultDoTransaction__FP6TStoreUlT2lUc
long
LODefaultDoTransaction(TStore* /*store*/, ULong /*rootId*/, ULong /*chunkTableId*/, long /*arg*/, UChar /*flag*/)
{
	return noErr;
}


// The one shared LZ compressor/decompressor pair a compander borrows in
// its shared mode.  gLZDecompressor and gSharedLZBuffer are made once by
// InitializeStoreCompanders; the compressor is made (and reference
// counted) lazily here.
static TProtocol*	gLZCompressor = nil;
static TProtocol*	gLZDecompressor = nil;
static long			gLZRefCount = 0;
static void*		gSharedLZBuffer = nil;


// ROM 0x001f80fc GetSharedLZObjects__FPP11TCompressorPP13TDecompressorPPcPl
long
GetSharedLZObjects(TCompressor** compressor, TDecompressor** decompressor, char** buffer, long* bufferSize)
{
	long err = noErr;
	if (compressor != nil)
	{
		*compressor = nil;
		if (gLZCompressor == nil)
		{
			gLZCompressor = NewByName("TCompressor", "TLZCompressor");
			if (gLZCompressor == nil)
				err = kError_No_Memory;
			else
			{
				gLZRefCount = 1;
				err = ((TCompressor*) gLZCompressor)->Init(nil);
			}
			if (err != noErr)
			{
				// undo the reference we just took, freeing the compressor
				if (gLZRefCount == 1)
				{
					gLZRefCount = 0;
					if (gLZCompressor != nil)
						((TCompressor*) gLZCompressor)->Delete();
					gLZCompressor = nil;
				}
				return err;
			}
		}
		else
			gLZRefCount++;
		*compressor = (TCompressor*) gLZCompressor;
	}
	if (decompressor != nil)
		*decompressor = (TDecompressor*) gLZDecompressor;
	if (buffer != nil)
		*buffer = (char*) gSharedLZBuffer;
	if (bufferSize != nil)
		*bufferSize = kLZCompanderBufferSize;
	return noErr;
}


// ROM 0x001f8208 ReleaseSharedLZObjects__FP11TCompressorP13TDecompressorPc
void
ReleaseSharedLZObjects(TCompressor* compressor, TDecompressor* /*decompressor*/, char* /*buffer*/)
{
	if (compressor == nil)
		return;
	gLZRefCount--;
	if (gLZRefCount != 0)
		return;
	if (gLZCompressor != nil)
		((TCompressor*) gLZCompressor)->Delete();
	gLZCompressor = nil;
}


// ---------------------------------------------------------------------------
//	TStoreCompander - the interface's New(char*)/Delete() glue.
// ---------------------------------------------------------------------------

// ROM 0x0037dbfc New__15TStoreCompanderSFPc
TStoreCompander*
TStoreCompander::New(const char* implementation)
{
	TStoreCompander* p = (TStoreCompander*) AllocInstanceByName("TStoreCompander", implementation);
	return p != nil ? (TStoreCompander*) p->GlueNew() : nil;
}


// ROM 0x0037dc28 Delete__15TStoreCompanderFv
void
TStoreCompander::Delete()
{
	GlueDelete();
}


// ---------------------------------------------------------------------------
//	TSimpleStoreCompander - the blocks kept uncompressed.
// ---------------------------------------------------------------------------

PROTOCOL_IMPL_SOURCE_MACRO(TSimpleStoreCompander)


// ROM 0x001f7c4c New__21TSimpleStoreCompanderFv
TSimpleStoreCompander*
TSimpleStoreCompander::New()
{
	fStore = nil;
	fReadOnly = false;
	return this;
}


// ROM 0x001f7c5c Delete__21TSimpleStoreCompanderFv
void
TSimpleStoreCompander::Delete()
{
}


// ROM 0x001f7c60 Init__21TSimpleStoreCompanderFP6TStoreUlT2UcT4
NewtonErr
TSimpleStoreCompander::Init(TStore* store, ULong rootId, ULong /*arg3*/, UChar readOnly, UChar /*shared*/)
{
	fRootId = rootId;
	fStore = store;
	fReadOnly = readOnly;
	PackageRoot root;
	store->Read(rootId, 0, (char*) &root, sizeof(PackageRoot));
	fChunkTableId = root.fChunkTableId;
	return noErr;
}


// ROM 0x001f7cc0 BlockSize__21TSimpleStoreCompanderFv
ULong
TSimpleStoreCompander::BlockSize()
{
	return kCompanderBlockSize;
}


// ROM 0x001f7cc8 Read__21TSimpleStoreCompanderFUlPclT1
NewtonErr
TSimpleStoreCompander::Read(ULong offset, char* buffer, long count, ULong /*page*/)
{
	StorePSSId blockId = 0;
	NewtonErr err = fStore->Read(fChunkTableId, (offset >> 10) << 2, (char*) &blockId, 4);
	if (err == noErr)
	{
		long objectSize = 0;
		err = fStore->GetObjectSize(blockId, &objectSize);
		if (err == noErr)
		{
			if (objectSize == 0)
				ZeroBytes(buffer, count);
			else
			{
				if (objectSize < count)
					count = objectSize;
				err = fStore->Read(blockId, 0, buffer, count);
			}
		}
	}
	return err;
}


// ROM 0x001f7d7c Write__21TSimpleStoreCompanderFUlPclT1
NewtonErr
TSimpleStoreCompander::Write(ULong offset, char* buffer, long count, ULong /*page*/)
{
	StorePSSId blockId = 0;
	NewtonErr err = fStore->Read(fChunkTableId, (offset >> 10) << 2, (char*) &blockId, 4);
	if (err == noErr)
		err = fStore->ReplaceObject(blockId, buffer, count);
	return err;
}


// ROM 0x001f7de0 DoTransactionAgainst__21TSimpleStoreCompanderFlUl
void
TSimpleStoreCompander::DoTransactionAgainst(long arg, ULong /*page*/)
{
	LODefaultDoTransaction(fStore, fRootId, fChunkTableId, arg, 1);
}


// ROM 0x001f7e0c IsReadOnly__21TSimpleStoreCompanderFv
Boolean
TSimpleStoreCompander::IsReadOnly()
{
	return fReadOnly;
}


// ---------------------------------------------------------------------------
//	TLZStoreCompander - the blocks LZ-compressed.
// ---------------------------------------------------------------------------

PROTOCOL_IMPL_SOURCE_MACRO(TLZStoreCompander)


// ROM 0x001f788c New__17TLZStoreCompanderFv
TLZStoreCompander*
TLZStoreCompander::New()
{
	fBuffer = nil;
	fDecompressor = nil;
	fCompressor = nil;
	fOwnsCoders = false;
	return this;
}


// ROM 0x001f78a4 Delete__17TLZStoreCompanderFv
void
TLZStoreCompander::Delete()
{
	if (fOwnsCoders)
	{
		if (fDecompressor != nil)
			fDecompressor->Delete();
		if (fCompressor != nil)
			fCompressor->Delete();
		if (fBuffer != nil)
			DisposePtr((Ptr) fBuffer);
	}
	else
		ReleaseSharedLZObjects(fCompressor, fDecompressor, (char*) fBuffer);
}


// ROM 0x001f78f8 Init__17TLZStoreCompanderFP6TStoreUlT2UcT4
NewtonErr
TLZStoreCompander::Init(TStore* store, ULong rootId, ULong /*arg3*/, UChar /*readOnly*/, UChar shared)
{
	fStore = store;
	fRootId = rootId;
	if (shared == 0)
	{
		fOwnsCoders = true;
		fCompressor = (TCompressor*) NewByName("TCompressor", "TLZCompressor");
		fDecompressor = (TDecompressor*) NewByName("TDecompressor", "TLZDecompressor");
		fBuffer = NewPtr(kLZCompanderBufferSize);
		if (fBuffer == nil || fCompressor == nil || fDecompressor == nil)
			return kError_No_Memory;
	}
	else
	{
		long bufferSize = 0;
		NewtonErr err = GetSharedLZObjects(&fCompressor, &fDecompressor, (char**) &fBuffer, &bufferSize);
		if (err != noErr)
			return err;
	}
	PackageRoot root;
	NewtonErr err = store->Read(fRootId, 0, (char*) &root, sizeof(PackageRoot));
	fChunkTableId = root.fChunkTableId;
	return err;
}


// ROM 0x001f7a18 BlockSize__17TLZStoreCompanderFv
ULong
TLZStoreCompander::BlockSize()
{
	return kCompanderBlockSize;
}


// ROM 0x001f7a20 Read__17TLZStoreCompanderFUlPclT1
NewtonErr
TLZStoreCompander::Read(ULong offset, char* buffer, long count, ULong /*page*/)
{
	StorePSSId blockId = 0;
	ULong outSize = count;
	NewtonErr err = fStore->Read(fChunkTableId, (offset >> 10) << 2, (char*) &blockId, 4);
	if (err == noErr)
	{
		long objectSize = 0;
		err = fStore->GetObjectSize(blockId, &objectSize);
		if (err == noErr)
		{
			if (objectSize == 0)
				ZeroBytes(buffer, outSize);
			else
			{
				err = fStore->Read(blockId, 0, (char*) fBuffer, objectSize);
				if (err == noErr)
					err = fDecompressor->Decompress(&outSize, buffer, outSize, fBuffer, objectSize);
			}
		}
	}
	return err;
}


// ROM 0x001f7afc Write__17TLZStoreCompanderFUlPclT1
NewtonErr
TLZStoreCompander::Write(ULong offset, char* buffer, long count, ULong /*page*/)
{
	StorePSSId blockId = 0;
	ULong compressedSize = count;
	NewtonErr err = fStore->Read(fChunkTableId, (offset >> 10) << 2, (char*) &blockId, 4);
	if (err == noErr)
	{
		err = fCompressor->Compress(&compressedSize, fBuffer, kLZCompanderBufferSize, buffer, compressedSize);
		if (err == noErr)
			err = fStore->ReplaceObject(blockId, (char*) fBuffer, compressedSize);
	}
	return err;
}


// ROM 0x001f7b90 DoTransactionAgainst__17TLZStoreCompanderFlUl
void
TLZStoreCompander::DoTransactionAgainst(long arg, ULong /*page*/)
{
	LODefaultDoTransaction(fStore, fRootId, fChunkTableId, arg, 1);
}


// ROM 0x001f7bbc IsReadOnly__17TLZStoreCompanderFv
Boolean
TLZStoreCompander::IsReadOnly()
{
	return fCompressor == nil;
}


// ---------------------------------------------------------------------------
//	Registration.
// ---------------------------------------------------------------------------

PROTOCOL_CLASSINFO(TSimpleStoreCompander, "TStoreCompander", "", 0, 0, nil)	// ROM 0x0037e1a8 ClassInfo__21TSimpleStoreCompanderSFv
PROTOCOL_CLASSINFO(TLZStoreCompander, "TStoreCompander", "", 0, 0, nil)		// ROM 0x0037e244 ClassInfo__17TLZStoreCompanderSFv


// A host subset of the ROM's InitializeStoreDecompressors (0x001f824c):
// the two companders, plus the shared LZ decompressor and its buffer that
// the shared-mode compander borrows.
// host: (partial InitializeStoreDecompressors 0x001f824c)
void
InitializeStoreCompanders(void)
{
	TSimpleStoreCompander::ClassInfo()->Register();
	TLZStoreCompander::ClassInfo()->Register();
	if (gSharedLZBuffer == nil)
		gSharedLZBuffer = NewPtr(kLZCompanderBufferSize);
	if (gLZDecompressor == nil)
	{
		gLZDecompressor = NewByName("TDecompressor", "TLZDecompressor");
		if (gLZDecompressor != nil)
			((TDecompressor*) gLZDecompressor)->Init(nil);
	}
}
