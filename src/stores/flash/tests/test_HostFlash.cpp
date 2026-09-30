// Host unit test for the host's flash files (hal/host/HostFlash.h) and the
// bigger flash they allow (docs/stores/README.md, "Bigger flash"):
//
//   - a sparse image: a new one is a header and a map; a word written into
//     an erased chunk appends that chunk and nothing else, writing ones
//     costs nothing, an erase gives the chunks back and a later write
//     reuses their slots, the image reads back as it was (erased chunks
//     0xFF), and a map word naming a chunk that never reached the file
//     reads as erased;
//   - a flat file still opens as the bytes of the flash;
//   - the internal flash (TNewInternalFlash) finds 64 MB - the most the
//     ROM's windows hold - and 128 MB (the write windows moved up, a
//     DEVIATION), and a flash store formatted on 128 MB of sparse image
//     takes 12 MB of objects, the file growing with them and not with
//     the flash, and gives them back after it is opened again.
//
// Runs as the kernel services task of a booted OS, as test_Flash does
// (the internal flash takes a locking semaphore).

#include "FlashStore.h"
#include "MemoryAllocator.h"
#include "HostFlash.h"
#include "Host.h"
#include "Boot.h"
#include "UserBoot.h"
#include "OSErrors.h"
#include "hal/MMU.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char*	kSparseFile = "test_HostFlash.sparse";
static const char*	kFlatFile = "test_HostFlash.flat";
static const char*	kBigFile = "test_HostFlash.big";

static long
FileSize(const char* path)
{
	HostFlashFlush();
	FILE* f = fopen(path, "rb");
	if (f == nil)
		return -1;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fclose(f);
	return size;
}

static uint32_t
FileWord(const char* path, long offset)
{
	HostFlashFlush();
	unsigned char b[4] = { 0, 0, 0, 0 };
	FILE* f = fopen(path, "rb");
	if (f != nil)
	{
		fseek(f, offset, SEEK_SET);
		if (fread(b, 1, 4, f) != 4)
			b[0] = b[1] = b[2] = b[3] = 0;
		fclose(f);
	}
	return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | b[3];
}

// a byte of the flash, by its offset in the file's flash (bank 1, then bank 2)
static Ptr
FlashByte(ULong offset)
{
	ULong bank = HostFlashBankSize();
	HostClearSections();
	PAddr physical = offset < bank ? kHostFlashBank1 + offset : kHostFlashBank2 + (offset - bank);
	AddNewSecPNJT(0x30000000, physical & 0xFFF00000, 0, kReadWrite, 0);
	return VirtualAddressToPointer(0x30000000 + (physical & 0x000FFFFF));
}

static uint32_t
FlashWordAt(ULong offset)
{
	unsigned char* p = (unsigned char*) FlashByte(offset);
	return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}


static void
TestSparse(void)
{
	remove(kSparseFile);
	EXPECT(HostFlashOpen(kSparseFile, 0x400000, kHostFlashSparse) == noErr);
	EXPECT(HostFlashFileFormat() == kHostFlashSparse);
	EXPECT(HostFlashSize() == 0x400000 && HostFlashBankSize() == 0x400000 && HostFlashChipSize() == 0x200000);
	// the header and a map of 4096 words, to a chunk: 17 KB
	const long empty = 0x4400;
	EXPECT(FileSize(kSparseFile) == empty);
	EXPECT(FileWord(kSparseFile, 0x10) == 0x400000 && FileWord(kSparseFile, 0x18) == 0x400 && FileWord(kSparseFile, 0x24) == 0x4400);

	// ones cost nothing; a word appends its chunk
	HostFlashWrite(FlashByte(0x1000), 0xFFFFFFFF, 0xFFFFFFFF);
	EXPECT(FileSize(kSparseFile) == empty);
	HostFlashWrite(FlashByte(0x1000), 0x12345678, 0xFFFFFFFF);
	EXPECT(FileSize(kSparseFile) == empty + 0x400);
	EXPECT(FileWord(kSparseFile, 0x40 + 4 * 4) == 1);					// chunk 4 in slot 1
	HostFlashWrite(FlashByte(0x1004), 0x9ABCDEF0, 0xFFFFFFFF);			// the same chunk
	EXPECT(FileSize(kSparseFile) == empty + 0x400);
	HostFlashWrite(FlashByte(0x3FFFFC), 0x0000FFFF, 0xFFFFFFFF);		// the last word of the flash
	EXPECT(FileSize(kSparseFile) == empty + 0x800);
	// flash only clears bits
	HostFlashWrite(FlashByte(0x1000), 0xFFFF0000, 0xFFFFFFFF);
	EXPECT(FlashWordAt(0x1000) == 0x12340000);
	HostFlashClose();

	// back as it was
	EXPECT(HostFlashOpen(kSparseFile, 0x800000, kHostFlashFlat) == noErr);		// (an existing file keeps its size and format)
	EXPECT(HostFlashFileFormat() == kHostFlashSparse && HostFlashSize() == 0x400000);
	EXPECT(FlashWordAt(0x1000) == 0x12340000 && FlashWordAt(0x1004) == 0x9ABCDEF0);
	EXPECT(FlashWordAt(0x3FFFFC) == 0x0000FFFF);
	EXPECT(FlashWordAt(0) == 0xFFFFFFFF && FlashWordAt(0x0FFC) == 0xFFFFFFFF && FlashWordAt(0x1400) == 0xFFFFFFFF && FlashWordAt(0x200000) == 0xFFFFFFFF);

	// an erase gives the chunk back, and the next chunk written takes its slot
	HostFlashErase(FlashByte(0), 0x20000, 0xFFFFFFFF);
	EXPECT(FlashWordAt(0x1000) == 0xFFFFFFFF);
	EXPECT(FileWord(kSparseFile, 0x40 + 4 * 4) == 0);
	HostFlashWrite(FlashByte(0x100000), 0x00000000, 0xFFFFFFFF);
	EXPECT(FileSize(kSparseFile) == empty + 0x800);
	EXPECT(FileWord(kSparseFile, 0x40 + 0x400 * 4) == 1);
	// an erase of some lanes only keeps the chunk
	HostFlashWrite(FlashByte(0x200000), 0x00000000, 0xFFFFFFFF);
	HostFlashErase(FlashByte(0x200000), 0x20000, 0xFFFF0000);
	EXPECT(FlashWordAt(0x200000) == 0xFFFF0000);
	HostFlashClose();
	EXPECT(HostFlashOpen(kSparseFile) == noErr);
	EXPECT(FlashWordAt(0x200000) == 0xFFFF0000 && FlashWordAt(0x100000) == 0 && FlashWordAt(0x1000) == 0xFFFFFFFF);
	HostFlashClose();

	// a map word naming a slot past the end of the file (a chunk that never
	// got there): erased, and the map put right
	FILE* f = fopen(kSparseFile, "r+b");
	EXPECT(f != nil);
	if (f != nil)
	{
		unsigned char slot[4] = { 0, 0, 0, 99 };
		fseek(f, 0x40 + 4 * 8, SEEK_SET);
		fwrite(slot, 1, 4, f);
		fclose(f);
	}
	EXPECT(HostFlashOpen(kSparseFile) == noErr);
	EXPECT(FlashWordAt(0x2000) == 0xFFFFFFFF);
	EXPECT(FileWord(kSparseFile, 0x40 + 4 * 8) == 0);
	EXPECT(FlashWordAt(0x100000) == 0);
	HostFlashClose();
	remove(kSparseFile);

	// neither too small nor too big, nor between
	EXPECT(HostFlashOpen(kSparseFile, 0x200000, kHostFlashSparse) != noErr);
	EXPECT(HostFlashOpen(kSparseFile, 0x10000000, kHostFlashSparse) != noErr);
	EXPECT(HostFlashOpen(kSparseFile, 0xC00000, kHostFlashSparse) != noErr);
	EXPECT(HostFlashValidSize(0x8000000) && HostFlashValidSize(0x1000000) && !HostFlashValidSize(0x1800000));
}


static void
TestFlat(void)
{
	remove(kFlatFile);
	EXPECT(HostFlashOpen(kFlatFile, 0x800000) == noErr);
	EXPECT(HostFlashFileFormat() == kHostFlashFlat && FileSize(kFlatFile) == 0x800000);
	EXPECT(HostFlashBankSize() == 0x400000);
	HostFlashWrite(FlashByte(0x400000), 0x11223344, 0xFFFFFFFF);		// bank 2's first word
	EXPECT(FileWord(kFlatFile, 0x400000) == 0x11223344);
	HostFlashClose();
	EXPECT(HostFlashOpen(kFlatFile, 0x400000, kHostFlashSparse) == noErr);
	EXPECT(HostFlashFileFormat() == kHostFlashFlat && HostFlashSize() == 0x800000);
	EXPECT(FlashWordAt(0x400000) == 0x11223344);
	HostFlashClose();
	remove(kFlatFile);
}


static TNewInternalFlash*	gFlash;

static NewtonErr
OpenFlash(void)
{
	alignas(TNewInternalFlash) char memory[sizeof(TNewInternalFlash)];
	TNewInternalFlash::ClassInfo()->MakeAt(memory);
	TNewInternalFlash* boot = (TNewInternalFlash*) memory;
	EXPECT(boot->InitForReservedBlock(THeapAllocator::GetGlobalAllocator(), TNewInternalFlash::kMapWindows) == noErr);
	boot->CleanUp();
	gFlash = (TNewInternalFlash*) TFlash::New("TNewInternalFlash");
	return gFlash->Init(THeapAllocator::GetGlobalAllocator());
}


// 64 MB: two banks of two 16 MB chips, the read windows filling
// 0x30000000-0x34000000 and the write windows where the ROM puts them
static void
Test64MB(void)
{
	remove(kBigFile);
	EXPECT(HostFlashOpen(kBigFile, 0x4000000, kHostFlashSparse) == noErr);
	HostClearSections();
	EXPECT(InternalFlashWriteWindow() == 0x34000000);
	EXPECT(OpenFlash() == kSError_NeedsFormat);
	EXPECT(gFlash->fRangeCount == 2);
	EXPECT(gFlash->fRanges[0]->fSize == 0x2000000 - 0x20000 && gFlash->fRanges[1]->fSize == 0x2000000);
	EXPECT(gFlash->fRanges[1]->fReadAddress == 0x32000000 && gFlash->fRanges[1]->fWriteAddress == 0x36000000);
	EXPECT(gFlash->GetEraseRegionSize() == 0x20000);
	EXPECT(gFlash->fRegionCount == 511);
	EXPECT(gFlash->GetTotalSize() == 0x4000000 - 2 * 0x20000);
	// the last word of the store, which is in bank 2
	char word[4] = { 'l', 'a', 's', 't' };
	ULong last = gFlash->GetTotalSize() - 4;
	EXPECT(gFlash->Write(last, 4, word) == noErr);
	char back[4] = { 0, 0, 0, 0 };
	EXPECT(gFlash->Read(last, 4, back) == noErr && memcmp(back, "last", 4) == 0);
	gFlash->Delete();
	// Clobber wrote 510 region headers: 510 chunks, and the last word's
	EXPECT(FileSize(kBigFile) == 0x40400 + 511 * 0x400);
	HostFlashClose();
	remove(kBigFile);
}


static Boolean
FillObject(char* data, ULong size, ULong n)
{
	for (ULong i = 0; i < size; i++)
		data[i] = (char) ((i * 7 + n * 13 + (i >> 8) * n) & 0xFF);
	return true;
}


// 128 MB: two banks of two 32 MB chips, the write windows moved up past
// the read windows (Flash.h's DEVIATION), 1022 store blocks - as many as
// a migrated directory entry can name - and 12 MB of objects on them
static void
Test128MB(void)
{
	remove(kBigFile);
	EXPECT(HostFlashOpen(kBigFile, 0x8000000, kHostFlashSparse) == noErr);
	HostClearSections();
	EXPECT(InternalFlashWriteWindow() == 0x38000000);
	EXPECT(OpenFlash() == kSError_NeedsFormat);
	EXPECT(gFlash->fRangeCount == 2);
	EXPECT(gFlash->fRanges[1]->fReadAddress == 0x34000000 && gFlash->fRanges[1]->fWriteAddress == 0x3C000000);
	EXPECT(gFlash->fRegionCount == 1023);
	EXPECT(gInternalFlashStoreSize == 0x8000000 - 2 * 0x20000);
	TFlashStore* store = (TFlashStore*) TStore::New("TFlashStore");
	EXPECT(store != nil);
	EXPECT(store->Init(nil, gInternalFlashStoreSize, 0, 0, kFlashStoreUsesTFlash, gFlash) == noErr);
	EXPECT(store->fBlockCount == 1022);
	EXPECT(store->Format() == noErr);
	long total = 0, used = 0;
	EXPECT(store->GetStoreSizes(&total, &used) == noErr);
	printf("128 MB store: total size %ld, used %ld\n", total, used);
	// every block's root directory, log and headers, and the slop, taken off
	EXPECT(total > 0x7A00000 && total < 0x8000000);
	long formatted = FileSize(kBigFile);
	printf("128 MB formatted: %ld bytes on disk\n", formatted);
	EXPECT(formatted < 0x300000);

	enum { kObjects = 420, kObjectSize = 0x7800 };		// 30 KB each: 12.3 MB
	static PSSId ids[kObjects];
	static char data[kObjectSize];
	for (ULong n = 0; n < kObjects; n++)
	{
		FillObject(data, kObjectSize, n);
		ids[n] = 0;
		EXPECT(store->NewObject(data, kObjectSize, &ids[n]) == noErr && ids[n] != 0);
	}
	long written = FileSize(kBigFile);
	printf("128 MB with %d KB of objects: %ld bytes on disk\n", kObjects * kObjectSize / 1024, written);
	EXPECT(written > (long) (kObjects * kObjectSize) && written < (long) (kObjects * kObjectSize) + 0x400000);
	store->Delete();
	gFlash->Delete();
	HostFlashClose();

	EXPECT(HostFlashOpen(kBigFile) == noErr);
	EXPECT(HostFlashSize() == 0x8000000);
	HostClearSections();
	EXPECT(OpenFlash() == noErr);
	store = (TFlashStore*) TStore::New("TFlashStore");
	EXPECT(store->Init(nil, gInternalFlashStoreSize, 0, 0, kFlashStoreUsesTFlash, gFlash) == noErr);
	Boolean needsFormat = true;
	EXPECT(store->NeedsFormat(&needsFormat) == noErr && !needsFormat);
	static char back[kObjectSize];
	int bad = 0;
	for (ULong n = 0; n < kObjects; n++)
	{
		FillObject(data, kObjectSize, n);
		long size = 0;
		if (store->GetObjectSize(ids[n], &size) != noErr || size != kObjectSize
		 || store->Read(ids[n], 0, back, kObjectSize) != noErr || memcmp(back, data, kObjectSize) != 0)
			bad++;
	}
	EXPECT(bad == 0);
	store->Delete();
	gFlash->Delete();
	HostFlashClose();
	remove(kBigFile);
}


static void
Scenario(void)
{
	TNewInternalFlash::ClassInfo()->Register();
	TFlashStore::ClassInfo()->Register();
	TestSparse();
	TestFlat();
	Test64MB();
	Test128MB();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;
	OsBoot();
	if (failures == 0)
		printf("test_HostFlash: all passed\n");
	else
		printf("test_HostFlash: %d failures\n", failures);
	return failures != 0;
}
