/*
	File:		stores/PixelMapCompander.cpp

	Contains:	TPixelMapCompander, the default compander of a bitmap kept
				on a store - see StoreCompander.h.

				A page is compressed with LZ after a *row-delta* filter:
				walking back from the end of the last whole row, every word
				is XORed with the word one row above it, so that rows much
				like the one before become mostly nought, which LZ packs
				well; reading undoes it walking forward, each row XORed with
				the one above, already restored.  The row length comes from
				the bitmap's PixelMap, a copy of which the first write keeps
				in the compander's parameter object - a 0x2c-byte *header*:
				its size, then the Newton's PixelMap (0x1c bytes).  A page
				of noughts is kept as an empty object.

				ROM QUIRKS kept: Write filters the caller's page in place
				and leaves it so (the domain manager writes a page out as it
				lets it go); only 0x20 of the header's 0x2c bytes are ever
				set, the rest written to the store as the allocation left
				them; for a 1-bit map the copy's grayTable word is
				overwritten with the row's word count.

	Reconstructed from the MP2x00 US ROM (0x0018a95c-0x0018b22c); each
	function cites its origin.
*/

#include "StoreCompander.h"
#include "LargeObjects.h"			// LODefaultDoTransaction
#include "Compression.h"
#include "NewtonMemory.h"
#include "ByteOrder.h"
#include "OSErrors.h"
#include <stdint.h>
#include <string.h>

extern TClassInfoRegistry*	gProtocolRegistry;

const long kPixelMapHeaderSize = 0x2c;		// the parameter object: the size word and a Newton PixelMap
const long kNewtonPixelMapSize = 0x1c;		// a PixelMap with four-byte pointers


namespace
{
// The bitmap's row length, from the header's copy of its PixelMap (its
// rowBytes, the high half of the word at +8), and the words in a row.
inline long
HeaderRowBytes(const UByte* header)
{
	return (long) ((int32_t) GetBigEndianWord(header + 8) >> 16);
}

inline long
HeaderRowWords(const UByte* header)
{
	return (long) ((int32_t) GetBigEndianWord(header + 8) >> 18);
}

// the words of a page the filter covers - whole rows, less the first - and
// the bytes after the last whole row, for a page of this size
// (DEVIATION: a map with no row length is refused where the ROM's
// __rt_sdiv would trap)
void
Measure(long count, long rowBytes, long rowWords, long* xorWords, long* tailBytes)
{
	long whole = rowBytes != 0 ? rowBytes * (count / rowBytes) : 0;
	*xorWords = (whole >> 2) - rowWords;
	*tailBytes = count - whole;
}
}


PROTOCOL_IMPL_SOURCE_MACRO(TPixelMapCompander)


// ROM 0x0018a984 New__18TPixelMapCompanderFv
TPixelMapCompander*
TPixelMapCompander::New()
{
	fDecompressor = nil;
	fCompressor = nil;
	fBuffer = nil;
	fHeader = nil;
	fOwnsCoders = false;
	return this;
}


// ROM 0x0018a9a0 Delete__18TPixelMapCompanderFv
void
TPixelMapCompander::Delete()
{
	DisposeAllocations();
}


// ROM 0x0018aba4 DisposeAllocations__18TPixelMapCompanderFv
// The coders and the buffer given back (or the shared ones let go), and
// the header.
void
TPixelMapCompander::DisposeAllocations()
{
	if (!fOwnsCoders)
		ReleaseSharedLZObjects(fCompressor, fDecompressor, fBuffer);
	else
	{
		if (fDecompressor != nil)
			fDecompressor->Delete();
		if (fCompressor != nil)
			fCompressor->Delete();
		if (fBuffer != nil)
			DisposPtr(fBuffer);
	}
	if (fHeader != nil)
		DisposPtr((Ptr) fHeader);
	fCompressor = nil;
	fDecompressor = nil;
	fBuffer = nil;
	fHeader = nil;
}


// ROM 0x0018a9a4 Init__18TPixelMapCompanderFP6TStoreUlT2UcT4
// Over the store, the large object's root and the header object: LZ coders
// of its own (or the shared ones), the header read in when it has been
// written, and the chunk table's id from the root.  ==> the error.
NewtonErr
TPixelMapCompander::Init(TStore* store, ULong rootId, ULong headerId, UChar readOnly, UChar shared)
{
	fStore = store;
	fRootId = rootId;
	fHeaderId = headerId;
	fReadOnly = readOnly;
	NewtonErr err;
	if (shared == 0)
	{
		fCompressor = (TCompressor*) NewByName("TCompressor", "TLZCompressor");
		fDecompressor = (TDecompressor*) NewByName("TDecompressor", "TLZDecompressor");
		fBufferSize = 0x408;
		fBuffer = NewPtr(0x408);
		fOwnsCoders = true;
	}
	else if ((err = GetSharedLZObjects(&fCompressor, &fDecompressor, &fBuffer, &fBufferSize)) != noErr)
		return err;
	if (fCompressor == nil || fDecompressor == nil || fBuffer == nil)
	{
		DisposeAllocations();
		return kError_No_Memory;
	}
	if (fOwnsCoders)
	{
		if ((err = fCompressor->Init(nil)) != noErr)
			return err;
		if ((err = fDecompressor->Init(nil)) != noErr)
			return err;
	}
	long size = 0;
	if ((err = fStore->GetObjectSize(fHeaderId, &size)) != noErr)
		return err;
	if (size > 0)
	{
		fHeader = (UByte*) NewPtr(size);
		if (fHeader == nil)
			return kError_No_Memory;
		err = fStore->Read(fHeaderId, 0, (char*) fHeader, size);
		fRowWords = HeaderRowWords(fHeader);
		if (err != noErr)
			return err;
	}
	fCachedCount = -1;
	PackageRoot root;
	err = fStore->Read(fRootId, 0, (char*) &root, sizeof(PackageRoot));
	fChunkTableId = GetBigEndianWord((const UByte*) &root.fChunkTableId);	// (a big-endian word on the store)
	return err;
}


// ROM 0x0018ab9c BlockSize__18TPixelMapCompanderFv
ULong
TPixelMapCompander::BlockSize()
{
	return 0x1000;
}


// ROM 0x0018ac1c Read__18TPixelMapCompanderFUlPclT1
// The page read: its object (an empty one is a page of noughts) expanded,
// then the row filter undone - each word XORed with the word a row above,
// walking forward.  ==> the error (the expansion's, the filter being run
// whatever it said).
NewtonErr
TPixelMapCompander::Read(ULong offset, char* buffer, long count, ULong /*objectBase*/)
{
	UByte word[4];
	NewtonErr err = fStore->Read(fChunkTableId, (offset >> 10) << 2, (char*) word, 4);
	if (err != noErr)
		return err;
	PSSId blockId = GetBigEndianWord(word);		// (the chunk table's words are big-endian)
	long objectSize = 0;
	if ((err = fStore->GetObjectSize(blockId, &objectSize)) != noErr)
		return err;
	if (objectSize == 0)
	{
		ZeroBytes(buffer, count);
		return noErr;
	}
	if (fBufferSize < objectSize)
		return kError_No_Memory;
	if ((err = fStore->Read(blockId, 0, fBuffer, objectSize)) != noErr)
		return err;
	ULong outSize = 0;
	err = fDecompressor->Decompress(&outSize, buffer, count, fBuffer, objectSize);
	if (fHeader == nil)
		return err;
	long rowBytes = HeaderRowBytes(fHeader);
	fRowWords = HeaderRowWords(fHeader);
	long n;
	if (fCachedCount == count)
		n = fXorWords;
	else
	{
		Measure(count, rowBytes, fRowWords, &fXorWords, &fTailBytes);
		n = fXorWords;
		fCachedCount = count;
	}
	uint32_t* above = (uint32_t*) buffer;
	uint32_t* row = above + fRowWords;
	for (; n > 0; n--)
		*row++ ^= *above++;
	return err;
}


// ROM 0x0018ae9c Write__18TPixelMapCompanderFUlPclT1
// The page written: on the first write the header made from the PixelMap
// at the front of the object and kept; a page of noughts made an empty
// object; anything else row-filtered (in place, walking back from the end
// of the last whole row) and compressed into the page's object.  ==> the
// error.
NewtonErr
TPixelMapCompander::Write(ULong offset, char* buffer, long count, ULong objectBase)
{
	UByte word[4];
	NewtonErr err = fStore->Read(fChunkTableId, (offset >> 10) << 2, (char*) word, 4);
	if (err != noErr)
		return err;
	PSSId blockId = GetBigEndianWord(word);		// (the chunk table's words are big-endian)
	if (fHeader == nil)
	{
		fHeader = (UByte*) NewPtr(kPixelMapHeaderSize);
		if (fHeader == nil)
			return kError_No_Memory;
		PutBigEndianWord(fHeader, kPixelMapHeaderSize);
		// ROM BUG: FillChunkArray (an object filled from a pipe as it is
		// made) passes nought as the object's base, so the ROM copies its
		// "PixelMap" from address 0 - the vectors page.  DEVIATION: the host
		// cannot read what is there; it copies noughts, so the row length
		// is nought and such a page goes unfiltered.
		if (objectBase == 0)
			memset(fHeader + 4, 0, kNewtonPixelMapSize);
		else
			memmove(fHeader + 4, (const void*) objectBase, kNewtonPixelMapSize);	// (the 'pixels binary's own header, big-endian - qd/Pictures.h)
		long rowBytes = HeaderRowBytes(fHeader);
		fRowWords = rowBytes >> 2;
		// ROM QUIRK kept: a 1-bit map's copy has its grayTable word
		// overwritten with the words in a row
		if ((GetBigEndianWord(fHeader + 0x14) & 0xff) == 1)
			PutBigEndianWord(fHeader + 0x1c, (ULong32) fRowWords);
		fCachedCount = count;
		Measure(count, rowBytes, fRowWords, &fXorWords, &fTailBytes);
		fStore->SetObjectSize(fHeaderId, kPixelMapHeaderSize);
		if ((err = fStore->Write(fHeaderId, 0, (char*) fHeader, kPixelMapHeaderSize)) != noErr)
			return err;
	}
	// a page of noughts: an empty object
	long n = count >> 2;
	const uint32_t* w = (const uint32_t*) buffer;
	do
	{
		if (*w++ != 0)
			break;
		n--;
	} while (n > 0);
	if (n == 0)
		return fStore->SetObjectSize(blockId, 0);
	if (fCachedCount != count)
	{
		Measure(count, HeaderRowBytes(fHeader), fRowWords, &fXorWords, &fTailBytes);
		fCachedCount = count;
	}
	n = fXorWords;
	if (n > 0)
	{
		uint32_t* end = (uint32_t*) (buffer + count - fTailBytes);
		uint32_t* above = end - fRowWords;
		for (; n > 0; n--)
			*--end ^= *--above;
	}
	ULong outSize = 0;
	if ((err = fCompressor->Compress(&outSize, fBuffer, fBufferSize, buffer, count)) != noErr)
		return err;
	return fStore->ReplaceObject(blockId, fBuffer, outSize);
}


// ROM 0x0018b200 DoTransactionAgainst__18TPixelMapCompanderFlUl
void
TPixelMapCompander::DoTransactionAgainst(long arg, ULong /*page*/)
{
	LODefaultDoTransaction(fStore, fRootId, fChunkTableId, arg, 1);
}


// ROM 0x0018a97c IsReadOnly__18TPixelMapCompanderFv
Boolean
TPixelMapCompander::IsReadOnly()
{
	return fReadOnly;
}


PROTOCOL_CLASSINFO(TPixelMapCompander, "TStoreCompander", "TLZDecompressor; TLZCompressor", 0x10000, 0, nil)	// ROM 0x00388688 ClassInfo__18TPixelMapCompanderSFv


// ROM 0x0018a964 InitQDCompression__Fv
// TPixelMapCompander registered, for a store bitmap to be made with it.
// DEVIATION: a host that starts QuickDraw without the OS running (the
// QuickDraw tests, newtonscript) has no protocol registry to register it
// with, and the ROM's call would trap; it is skipped.
void
InitQDCompression(void)
{
	if (gProtocolRegistry != nil)
		TPixelMapCompander::ClassInfo()->Register();
}
