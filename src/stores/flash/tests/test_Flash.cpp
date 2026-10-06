// Host unit test for the internal flash (src/stores/flash/Flash.h) over the
// host's flash chips (hal/host/HostFlash.h): the banks found the way the
// ROM finds them, a fresh flash formatted into its regions, bytes written
// and erased through TFlash landing in the file where the machine (and
// Einstein) keeps them, and the flash coming back as it was when the file
// is opened again.
//
// The internal flash takes a locking semaphore, so the test runs as the
// kernel services task of a booted OS (as stores/tests/test_Store.cpp
// does).  The windows are mapped the way the boot maps them - an instance
// made with kMapWindows and cleaned up (InitCGlobals, through
// InitForReservedBlock) - before the one the store would use is made with
// Init.

#include "Flash.h"
#include "MemoryAllocator.h"
#include "HostFlash.h"
#include "Host.h"
#include "Boot.h"
#include "UserBoot.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>

// (not <vector>: with the object system's include paths, <locale.h> finds
// intl/Locale.h on a case-insensitive file system)
struct FileBytes
{
	static unsigned char	sBytes[2 * kHostFlashBankSize];
	ULong					fSize;
	ULong			size() const					{ return fSize; }
	unsigned char&	operator[](ULong i)				{ return sBytes[i]; }
	const unsigned char&	operator[](ULong i) const	{ return sBytes[i]; }
};
unsigned char FileBytes::sBytes[2 * kHostFlashBankSize];

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char*	kFlashFile = "test_Flash.flash";

// the geometry of two 16-bit 28F016SA parts making a 32-bit bank
static const ULong	kRegion = 0x20000;						// 2 x 64 KB
static const ULong	kRegions = (kHostFlashBankSize - kRegion) / kRegion;	// the reserved region is not one of them: 31
static const ULong	kStoreSize = kHostFlashBankSize - 2 * kRegion;		// less the spare


static FileBytes
ReadFile(void)
{
	FileBytes bytes;
	bytes.fSize = 0;
	HostFlashFlush();
	FILE* f = fopen(kFlashFile, "rb");
	if (f == nil)
		return bytes;
	fseek(f, 0, SEEK_END);
	bytes.fSize = (ULong) ftell(f);
	fseek(f, 0, SEEK_SET);
	if (bytes.fSize > sizeof(FileBytes::sBytes) || fread(FileBytes::sBytes, 1, bytes.fSize, f) != bytes.fSize)
		bytes.fSize = 0;
	fclose(f);
	return bytes;
}


// the physical region's header in the file (the reserved region first)
static ULong
HeaderAt(const FileBytes& file, ULong physicalRegion)
{
	const unsigned char* h = &file[kRegion + physicalRegion * kRegion];
	return ((ULong) h[0] << 24) | ((ULong) h[1] << 16) | ((ULong) h[2] << 8) | h[3];
}


static void
MapWindowsAsTheBootDoes(void)
{
	alignas(TNewInternalFlash) char memory[sizeof(TNewInternalFlash)];
	TNewInternalFlash::ClassInfo()->MakeAt(memory);
	TNewInternalFlash* boot = (TNewInternalFlash*) memory;
	EXPECT(boot->InitForReservedBlock(THeapAllocator::GetGlobalAllocator(), TNewInternalFlash::kMapWindows) == noErr);
	boot->CleanUp();
}


static TNewInternalFlash*
MakeFlash(NewtonErr* initResult)
{
	TNewInternalFlash* flash = (TNewInternalFlash*) TFlash::New("TNewInternalFlash");
	EXPECT(flash != nil);
	*initResult = flash->Init(THeapAllocator::GetGlobalAllocator());
	return flash;
}


static void
TestFreshFlash(void)
{
	remove(kFlashFile);
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	HostClearSections();
	MapWindowsAsTheBootDoes();

	NewtonErr err;
	TNewInternalFlash* flash = MakeFlash(&err);
	// an erased flash is every region erased at once: it is wiped into
	// regions, and the store is told to format
	EXPECT(err == kSError_NeedsFormat);
	EXPECT(gInternalFlash == flash);
	EXPECT(flash->fRangeCount == 1);
	EXPECT(flash->fRanges[0]->fLaneCount == 4 && flash->fRanges[0]->fChipCount == 2);
	EXPECT(flash->GetEraseRegionSize() == kRegion);
	EXPECT(flash->fRegionCount == kRegions);
	EXPECT(flash->GetTotalSize() == kStoreSize);
	EXPECT(gInternalFlashStoreSize == kStoreSize);
	EXPECT(flash->fSpareRegion == kRegions - 1);
	EXPECT(flash->GetAttributes() == kFlashAttrNeedsVpp);

	FileBytes file = ReadFile();
	EXPECT(file.size() == kHostFlashBankSize);
	if (file.size() == kHostFlashBankSize)
	{
		for (ULong i = 0; i < kRegions - 1; i++)
			EXPECT(HeaderAt(file, i) == ((i << 16) | 0x00FF));
		EXPECT(HeaderAt(file, kRegions - 1) == 0xFFFFFFFF);
		EXPECT(file[0] == 0xFF && file[kRegion - 1] == 0xFF);		// the reserved region untouched
	}

	// bytes written at a logical address land in the region the map says,
	// after its header; an odd start and length exercise the masked words
	char text[] = "The internal flash, as the machine keeps it.";
	ULong address = 2 * kRegion + 0x101;
	EXPECT(flash->Write(address, sizeof(text), text) == noErr);
	char back[sizeof(text)];
	memset(back, 0, sizeof(back));
	EXPECT(flash->Read(address, sizeof(back), back) == noErr);
	EXPECT(memcmp(text, back, sizeof(text)) == 0);
	file = ReadFile();
	EXPECT(memcmp(&file[kRegion + 2 * kRegion + 0x101], text, sizeof(text)) == 0);
	EXPECT(!flash->IsVirgin(address, sizeof(text)));
	EXPECT(flash->IsVirgin(3 * kRegion, 64));
	EXPECT(flash->IsVirgin(3 * kRegion, 2));		// ROM bug fixed: shorter than the header (the ROM's length wraps)

	// flash only clears bits: writing over what is there ANDs
	char ones[4] = { (char) 0xF0, (char) 0xF0, (char) 0xF0, (char) 0xF0 };
	char zeros[4] = { 0x3C, 0x3C, 0x3C, 0x3C };
	EXPECT(flash->Write(4 * kRegion + 8, 4, ones) == noErr);
	EXPECT(flash->Write(4 * kRegion + 8, 4, zeros) == noErr);
	EXPECT(flash->Read(4 * kRegion + 8, 4, back) == noErr);
	EXPECT((unsigned char) back[0] == 0x30 && (unsigned char) back[3] == 0x30);

	// erasing logical region 2 swaps it with the spare
	EXPECT(flash->Erase(2 * kRegion) == noErr);
	EXPECT(flash->fSpareRegion == 2);
	EXPECT(flash->IsVirgin(2 * kRegion, kRegion));
	memset(back, 0, sizeof(back));
	EXPECT(flash->Read(address, sizeof(back), back) == noErr);
	EXPECT((unsigned char) back[0] == 0xFF);
	file = ReadFile();
	EXPECT(HeaderAt(file, kRegions - 1) == ((2 << 16) | 0x00FF));	// the old spare holds region 2
	EXPECT(HeaderAt(file, 2) == 0xFFFFFFFF);						// the old region is erased: the new spare

	// written again after the erase, it goes to the new place
	EXPECT(flash->Write(address, sizeof(text), text) == noErr);
	file = ReadFile();
	EXPECT(memcmp(&file[kRegion + (kRegions - 1) * kRegion + 0x101], text, sizeof(text)) == 0);

	// Copy, through a buffer
	EXPECT(flash->Copy(address, 5 * kRegion + 0x40, sizeof(text)) == noErr);
	memset(back, 0, sizeof(back));
	EXPECT(flash->Read(5 * kRegion + 0x40, sizeof(back), back) == noErr);
	EXPECT(memcmp(text, back, sizeof(text)) == 0);

	flash->Delete();
	EXPECT(gInternalFlash == nil);
	HostFlashClose();
}


static void
TestReopen(void)
{
	// the file as it was left: the map is read back off the regions' headers
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	NewtonErr err;
	TNewInternalFlash* flash = MakeFlash(&err);
	EXPECT(err == noErr);
	EXPECT(flash->fSpareRegion == 2);
	char text[] = "The internal flash, as the machine keeps it.";
	char back[sizeof(text)];
	memset(back, 0, sizeof(back));
	EXPECT(flash->Read(2 * kRegion + 0x101, sizeof(back), back) == noErr);
	EXPECT(memcmp(text, back, sizeof(text)) == 0);
	flash->Delete();

	// a region left half-way through an erase (its header marked, the
	// spare not yet taken) is erased again at the next start and becomes
	// the spare (the ROM's recovery: NEWTON_ROM_BUGS=1)
	SetRomBugFixed(false);
	FileBytes file = ReadFile();
	HostFlashClose();
	file[kRegion + 3 * kRegion + 3] = 0x0F;			// region 3: {0, 3, 0, 0x0F}
	FILE* f = fopen(kFlashFile, "r+b");
	fseek(f, (long) (kRegion + 3 * kRegion + 3), SEEK_SET);
	fputc(0x0F, f);
	fclose(f);
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	flash = MakeFlash(&err);
	EXPECT(err == noErr);
	EXPECT(flash->fSpareRegion == 3);
	file = ReadFile();
	EXPECT(HeaderAt(file, 3) == 0xFFFFFFFF);
	EXPECT(HeaderAt(file, 2) == 0xFFFFFFFF);			// the old spare, still erased
	flash->Delete();
	HostFlashClose();

	// ROM BUG (see SetupVirtualMappings): that recovery leaves two erased
	// regions and logical region 3 held by neither, so the start after it
	// finds the flash inconsistent and wipes it
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	flash = MakeFlash(&err);
	EXPECT(err == kSError_NeedsFormat);
	file = ReadFile();
	EXPECT(HeaderAt(file, 2) == ((2 << 16) | 0x00FF));
	EXPECT(HeaderAt(file, 3) == ((3 << 16) | 0x00FF));
	EXPECT(HeaderAt(file, kRegions - 1) == 0xFFFFFFFF);
	flash->Delete();
	HostFlashClose();
	SetRomBugFixed(true);

	// fixed: the interrupted Erase is finished - the spare (now the last
	// region) takes logical region 3, region 3 is erased and is the spare,
	// and the start after that finds a consistent flash
	f = fopen(kFlashFile, "r+b");
	fseek(f, (long) (kRegion + 3 * kRegion + 3), SEEK_SET);
	fputc(0x0F, f);						// region 3: {0, 3, 0, 0x0F}
	fclose(f);
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	flash = MakeFlash(&err);
	EXPECT(err == noErr);
	EXPECT(flash->fSpareRegion == 3);
	file = ReadFile();
	EXPECT(HeaderAt(file, kRegions - 1) == ((3 << 16) | 0x00FF));
	EXPECT(HeaderAt(file, 3) == 0xFFFFFFFF);
	flash->Delete();
	HostFlashClose();
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	flash = MakeFlash(&err);
	EXPECT(err == noErr);
	EXPECT(flash->fSpareRegion == 3);
	EXPECT(flash->IsVirgin(3 * kRegion, 64));
	flash->Delete();
	HostFlashClose();

	// two regions claiming the same logical region: the flash is wiped
	f = fopen(kFlashFile, "r+b");
	fseek(f, (long) (kRegion + 4 * kRegion), SEEK_SET);
	fputc(0x00, f);
	fputc(0x05, f);						// region 4 now says it holds region 5 too
	fclose(f);
	EXPECT(HostFlashOpen(kFlashFile) == noErr);
	flash = MakeFlash(&err);
	EXPECT(err == kSError_NeedsFormat);
	file = ReadFile();
	EXPECT(HeaderAt(file, 4) == ((4 << 16) | 0x00FF));
	EXPECT(HeaderAt(file, kRegions - 1) == 0xFFFFFFFF);
	flash->Delete();
	HostFlashClose();
	remove(kFlashFile);
}


static void
TestNoFlash(void)
{
	// with no chips the drivers find nothing on the bank
	HostFlashClose();
	alignas(TNewInternalFlash) char memory[sizeof(TNewInternalFlash)];
	TNewInternalFlash::ClassInfo()->MakeAt(memory);
	TNewInternalFlash* flash = (TNewInternalFlash*) memory;
	EXPECT(flash->Init(THeapAllocator::GetGlobalAllocator()) == kError_Flash_Unsupported_Configuration);
	flash->CleanUp();
}


static void
FlashScenario(void)
{
	TNewInternalFlash::ClassInfo()->Register();
	TestFreshFlash();
	TestReopen();
	TestNoFlash();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = FlashScenario;
	OsBoot();
	if (failures == 0)
		printf("test_Flash: all passed\n");
	else
		printf("test_Flash: %d failures\n", failures);
	return failures != 0;
}
