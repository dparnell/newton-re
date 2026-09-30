/*
	File:		hal/host/HostFlash.cpp

	Contains:	The internal flash's chips over a host file (HostFlash.h):
				a flat file of the flash's bytes, or a sparse image of only
				the chunks that are not erased.

	Written by:	the reconstruction (DEVIATION: the machine's chips are hardware)
*/

#include "HostFlash.h"
#include "Host.h"
#include "OSErrors.h"
#include "NewtErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

ULong	gHostFlashWrites = 0;
ULong	gHostFlashErases = 0;

namespace
{
	const char		kSparseMagic[8] = { 'N', 'e', 'w', 't', 'F', 'l', 's', 'h' };
	const uint32_t	kSparseVersion = 1;
	const uint32_t	kSparseHeaderSize = 0x40;
	const uint32_t	kSparseFlagsOffset = 0x28;
	const uint32_t	kSparseBigEndianBinaries = 0x01;		// (HostFlash.h)

	std::vector<unsigned char>	gFlash;		// the banks, one after the other
	FILE*						gFile = nil;
	HostFlashFormat				gFormat = kHostFlashFlat;
	ULong						gBankSize = 0;

	// a sparse image's state: which slot holds each chunk (0: none), the
	// slots given back, and the next slot never used
	std::vector<uint32_t>		gMap;
	std::vector<uint32_t>		gFreeSlots;
	uint32_t					gNextSlot = 1;
	uint32_t					gDataOffset = 0;
	uint32_t					gSparseFlags = 0;

	uint32_t
	GetWord(const unsigned char* p)
	{
		return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
	}

	void
	PutWord(unsigned char* p, uint32_t w)
	{
		p[0] = (unsigned char) (w >> 24);
		p[1] = (unsigned char) (w >> 16);
		p[2] = (unsigned char) (w >> 8);
		p[3] = (unsigned char) w;
	}

	void
	WriteAt(ULong fileOffset, const void* bytes, ULong size)
	{
		fseek(gFile, (long) fileOffset, SEEK_SET);
		fwrite(bytes, 1, size, gFile);
	}

	ULong
	SlotOffset(uint32_t slot)
	{
		return gDataOffset + (slot - 1) * kHostFlashChunkSize;
	}

	void
	WriteMapEntry(ULong chunk)
	{
		unsigned char word[4];
		PutWord(word, gMap[chunk]);
		WriteAt(kSparseHeaderSize + chunk * 4, word, 4);
	}

	Boolean
	ChunkIsErased(ULong chunk)
	{
		const unsigned char* p = &gFlash[chunk * kHostFlashChunkSize];
		for (ULong i = 0; i < kHostFlashChunkSize; i++)
			if (p[i] != 0xFF)
				return false;
		return true;
	}

	// The bytes [offset, offset + size) of the flash, just changed, taken to
	// the file: in place in a flat file; in a sparse one, into the chunks
	// that hold them - a chunk that had no slot getting one (its bytes
	// first, then the map word naming them), a chunk left all erased
	// giving its slot back (the map word cleared, the slot reused later).
	void
	WriteThrough(ULong offset, ULong size)
	{
		if (gFile == nil || size == 0)
			return;
		if (gFormat == kHostFlashFlat)
		{
			WriteAt(offset, &gFlash[offset], size);
			return;
		}
		ULong first = offset / kHostFlashChunkSize;
		ULong last = (offset + size - 1) / kHostFlashChunkSize;
		for (ULong chunk = first; chunk <= last; chunk++)
		{
			ULong start = chunk * kHostFlashChunkSize;
			ULong from = offset > start ? offset : start;
			ULong to = offset + size < start + kHostFlashChunkSize ? offset + size : start + kHostFlashChunkSize;
			if (ChunkIsErased(chunk))
			{
				if (gMap[chunk] != 0)
				{
					gFreeSlots.push_back(gMap[chunk]);
					gMap[chunk] = 0;
					WriteMapEntry(chunk);
					fflush(gFile);
				}
			}
			else if (gMap[chunk] != 0)
				WriteAt(SlotOffset(gMap[chunk]) + (from - start), &gFlash[from], to - from);
			else
			{
				uint32_t slot;
				if (!gFreeSlots.empty())
				{
					slot = gFreeSlots.back();
					gFreeSlots.pop_back();
				}
				else
					slot = gNextSlot++;
				WriteAt(SlotOffset(slot), &gFlash[start], kHostFlashChunkSize);
				fflush(gFile);
				gMap[chunk] = slot;
				WriteMapEntry(chunk);
			}
		}
	}

	// what is still in the C library's buffer goes to the file when the
	// program ends, however it ends
	void
	FlushAtExit(void)
	{
		HostFlashFlush();
	}

	long
	FileLength(FILE* f)
	{
		fseek(f, 0, SEEK_END);
		return ftell(f);
	}

	ULong
	BankSizeFor(ULong size)
	{
		return size <= kHostFlashBankSize ? size : size / 2;
	}

	// A sparse image read in: the header checked, the chunks it holds put
	// in place, and the map's slots accounted for.
	NewtonErr
	ReadSparse(long length)
	{
		unsigned char header[kSparseHeaderSize];
		fseek(gFile, 0, SEEK_SET);
		if (length < (long) kSparseHeaderSize || fread(header, 1, kSparseHeaderSize, gFile) != kSparseHeaderSize)
			return kError_Bad_Parameters;
		ULong size = GetWord(header + 0x10);
		if (GetWord(header + 0x08) != kSparseVersion
		 || GetWord(header + 0x0c) != kSparseHeaderSize
		 || !HostFlashValidSize(size)
		 || GetWord(header + 0x14) != BankSizeFor(size)
		 || GetWord(header + 0x18) != kHostFlashChunkSize
		 || GetWord(header + 0x1c) != size / kHostFlashChunkSize
		 || GetWord(header + 0x20) != kSparseHeaderSize)
			return kError_Bad_Parameters;
		ULong chunks = size / kHostFlashChunkSize;
		gDataOffset = GetWord(header + 0x24);
		gSparseFlags = GetWord(header + kSparseFlagsOffset);
		if (gDataOffset < kSparseHeaderSize + chunks * 4 || gDataOffset % kHostFlashChunkSize != 0)
			return kError_Bad_Parameters;
		gFlash.assign(size, 0xFF);
		gBankSize = BankSizeFor(size);
		gMap.assign(chunks, 0);
		// the map (a file cut short in it has the rest of its words nought)
		std::vector<unsigned char> words(chunks * 4, 0);
		fseek(gFile, kSparseHeaderSize, SEEK_SET);
		fread(&words[0], 1, words.size(), gFile);
		ULong slotsInFile = length > (long) gDataOffset ? (ULong) (length - gDataOffset) / kHostFlashChunkSize : 0;
		std::vector<unsigned char> used(slotsInFile + 1, 0);
		uint32_t highest = 0;
		for (ULong chunk = 0; chunk < chunks; chunk++)
		{
			uint32_t slot = GetWord(&words[chunk * 4]);
			if (slot == 0)
				continue;
			if (slot > slotsInFile || used[slot])
			{
				// a chunk that never reached the file, or a slot named
				// twice: erased (HostFlash.h), and the map made to say so
				WriteMapEntry(chunk);
				continue;
			}
			used[slot] = 1;
			gMap[chunk] = slot;
			if (highest < slot)
				highest = slot;
			fseek(gFile, (long) SlotOffset(slot), SEEK_SET);
			if (fread(&gFlash[chunk * kHostFlashChunkSize], 1, kHostFlashChunkSize, gFile) != kHostFlashChunkSize)
				return kError_Bad_Parameters;
		}
		gNextSlot = highest + 1;
		gFreeSlots.clear();
		for (uint32_t slot = highest; slot >= 1; slot--)
			if (!used[slot])
				gFreeSlots.push_back(slot);
		return noErr;
	}

	// A new sparse image: the header and an empty map.
	void
	CreateSparse(ULong size)
	{
		ULong chunks = size / kHostFlashChunkSize;
		gDataOffset = (ULong) ((kSparseHeaderSize + chunks * 4 + kHostFlashChunkSize - 1) & ~(kHostFlashChunkSize - 1));
		gMap.assign(chunks, 0);
		gFreeSlots.clear();
		gNextSlot = 1;
		unsigned char header[kSparseHeaderSize];
		memset(header, 0, sizeof(header));
		memcpy(header, kSparseMagic, 8);
		PutWord(header + 0x08, kSparseVersion);
		PutWord(header + 0x0c, kSparseHeaderSize);
		PutWord(header + 0x10, (uint32_t) size);
		PutWord(header + 0x14, (uint32_t) BankSizeFor(size));
		PutWord(header + 0x18, kHostFlashChunkSize);
		PutWord(header + 0x1c, (uint32_t) chunks);
		PutWord(header + 0x20, kSparseHeaderSize);
		PutWord(header + 0x24, gDataOffset);
		gSparseFlags = getenv("NEWTON_OLD_BYTE_ORDER") != nil ? 0 : kSparseBigEndianBinaries;	// (tests: an older host's file)
		PutWord(header + kSparseFlagsOffset, gSparseFlags);
		WriteAt(0, header, kSparseHeaderSize);
		std::vector<unsigned char> map(gDataOffset - kSparseHeaderSize, 0);		// and the padding to the first chunk
		WriteAt(kSparseHeaderSize, &map[0], (ULong) map.size());
		fflush(gFile);
	}

	void
	Forget(void)
	{
		if (gFile != nil)
		{
			fclose(gFile);
			gFile = nil;
		}
		gFlash.clear();
		gFlash.shrink_to_fit();
		gMap.clear();
		gMap.shrink_to_fit();
		gFreeSlots.clear();
		gBankSize = 0;
	}
}


Boolean
HostFlashValidSize(ULong size)
{
	for (ULong s = kHostFlashMinSize; s <= kHostFlashMaxSize && s != 0; s <<= 1)
		if (s == size)
			return true;
	return false;
}


void
HostFlashFlush(void)
{
	if (gFile != nil)
		fflush(gFile);
}


NewtonErr
HostFlashOpen(const char* path, ULong size, HostFlashFormat format)
{
	HostFlashClose();
	if (!HostFlashValidSize(size))
		return kError_Bad_Parameters;
	gFormat = format;
	if (path != nil)
	{
		gFile = fopen(path, "r+b");
		if (gFile != nil)
		{
			long existing = FileLength(gFile);
			unsigned char magic[8];
			fseek(gFile, 0, SEEK_SET);
			if (existing >= 8 && fread(magic, 1, 8, gFile) == 8 && memcmp(magic, kSparseMagic, 8) == 0)
			{
				gFormat = kHostFlashSparse;
				if (ReadSparse(existing) != noErr)
				{
					Forget();
					return kError_Bad_Parameters;
				}
			}
			else
			{
				gFormat = kHostFlashFlat;
				if (existing < 0 || !HostFlashValidSize((ULong) existing))
				{
					Forget();
					return kError_Bad_Parameters;
				}
				gFlash.assign((size_t) existing, 0xFF);
				gBankSize = BankSizeFor((ULong) existing);
				fseek(gFile, 0, SEEK_SET);
				if (fread(&gFlash[0], 1, gFlash.size(), gFile) != gFlash.size())
				{
					Forget();
					return kError_Bad_Parameters;
				}
			}
		}
		else
		{
			gFile = fopen(path, "w+b");
			if (gFile == nil)
				return kError_Bad_Parameters;
			gFlash.assign(size, 0xFF);
			gBankSize = BankSizeFor(size);
			if (gFormat == kHostFlashSparse)
				CreateSparse(size);
			else
				WriteThrough(0, (ULong) gFlash.size());
		}
	}
	else
	{
		gFlash.assign(size, 0xFF);
		gBankSize = BankSizeFor(size);
	}
	static Boolean registered = false;
	if (!registered)
	{
		registered = true;
		atexit(FlushAtExit);
	}
	HostRegisterPhysicalMemory(kHostFlashBank1, gBankSize, (Ptr) &gFlash[0]);
	if (gFlash.size() > gBankSize)
		HostRegisterPhysicalMemory(kHostFlashBank2, gBankSize, (Ptr) &gFlash[gBankSize]);
	return noErr;
}


void
HostFlashClose(void)
{
	HostUnregisterPhysicalMemory(kHostFlashBank1);
	HostUnregisterPhysicalMemory(kHostFlashBank2);
	Forget();
}


Boolean
HostFlashIsOpen(void)
{
	return !gFlash.empty();
}


ULong
HostFlashSize(void)
{
	return (ULong) gFlash.size();
}


ULong
HostFlashBankSize(void)
{
	return gBankSize;
}


ULong
HostFlashChipSize(void)
{
	return gBankSize / 2;
}


HostFlashFormat
HostFlashFileFormat(void)
{
	return gFormat;
}


Boolean
HostFlashBinariesBigEndian(void)
{
	return gFile == nil || gFormat != kHostFlashSparse || (gSparseFlags & kSparseBigEndianBinaries) != 0;
}


void
HostFlashSetBinariesBigEndian(void)
{
	if (gFile == nil || gFormat != kHostFlashSparse)
		return;
	gSparseFlags |= kSparseBigEndianBinaries;
	unsigned char word[4];
	PutWord(word, gSparseFlags);
	WriteAt(kSparseFlagsOffset, word, 4);
	fflush(gFile);
}


Boolean
HostFlashContains(Ptr p)
{
	return !gFlash.empty() && (unsigned char*) p >= &gFlash[0] && (unsigned char*) p < &gFlash[0] + gFlash.size();
}


// A chip programs by clearing bits: what the word becomes is what it was
// AND what was written, the lanes the mask leaves out being written as ones.
// (Einstein's own flash replaces the masked bits instead - the same thing
// whenever, as the ROM's code always does, only ones are turned into
// noughts.)  A write that changes nothing is not taken to the file, so
// writing ones into an erased chunk of a sparse image costs no room.
void
HostFlashWrite(Ptr word, ULong value, ULong mask)
{
	if (!HostFlashContains(word) || !HostFlashContains(word + 3))
		return;
	unsigned char* p = (unsigned char*) word;
	ULong programmed = value | ~mask;
	unsigned char before[4] = { p[0], p[1], p[2], p[3] };
	p[0] &= (unsigned char) (programmed >> 24);
	p[1] &= (unsigned char) (programmed >> 16);
	p[2] &= (unsigned char) (programmed >> 8);
	p[3] &= (unsigned char) programmed;
	gHostFlashWrites++;
	if (memcmp(before, p, 4) != 0)
		WriteThrough((ULong) (p - &gFlash[0]), 4);
}


void
HostFlashErase(Ptr start, ULong size, ULong lanes)
{
	if (!HostFlashContains(start) || !HostFlashContains(start + size - 1))
		return;
	unsigned char* p = (unsigned char*) start;
	for (ULong i = 0; i < size; i++)
		if ((lanes >> (8 * (3 - ((p - &gFlash[0]) + i) % 4))) & 0xFF)
			p[i] = 0xFF;
	gHostFlashErases++;
	WriteThrough((ULong) (p - &gFlash[0]), size);
}
