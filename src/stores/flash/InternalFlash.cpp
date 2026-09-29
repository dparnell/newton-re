/*
	File:		stores/flash/InternalFlash.cpp

	Contains:	TNewInternalFlash, the TFlash of the internal flash
				(Flash.h), and the TFlash protocol's glue.

	Reconstructed from the MP2x00 US ROM (0x0013afa0-0x0013cfc0); each
	function cites its origin.
*/

#include "Flash.h"
#include "MemoryAllocator.h"
#include "SemaphoreGrabber.h"
#include "UserSemaphore.h"
#include "UserTasks.h"
#include "ROMPackages.h"
#include "ByteOrder.h"
#include "Reboot.h"
#include "hal/MMU.h"
#include "hal/System.h"

#include <new>

TNewInternalFlash*	gInternalFlash = nil;				// ROM 0x0c101568 gInternalFlash
ULong				gInternalFlashStoreSize = 0;		// ROM 0x0c106520 (unnamed)
ULong				gInternalFlashNeedsVpp = 0;			// ROM 0x0c106524 (unnamed)

// a region's header: which logical region it holds, and what state it is in
enum
{
	kRegionMapped		= 0x00FF,		// holds the logical region its first two bytes name
	kRegionErasing		= 0x000F,		// being given up
	kNoRegion			= 0xFFFF
};


static inline ULong
MapEntry(const UChar* map, ULong logical)
{
	// the ROM loads the word at the halfword's address and shifts it down,
	// which on the ARM (an unaligned load rotates) is the big-endian halfword
	return ((ULong) map[logical * 2] << 8) | map[logical * 2 + 1];
}

static inline void
SetMapEntry(UChar* map, ULong logical, ULong physical)
{
	map[logical * 2] = (UChar) (physical >> 8);
	map[logical * 2 + 1] = (UChar) physical;
}


/*------------------------------------------------------------------------------
	T F l a s h
------------------------------------------------------------------------------*/

// ROM 0x00386474 New__6TFlashSFPc
TFlash*
TFlash::New(const char* implementation)
{
	TFlash* p = (TFlash*) AllocInstanceByName("TFlash", implementation);
	return p != nil ? (TFlash*) p->GlueNew() : nil;
}


// ROM 0x003864a0 Delete__6TFlashFv
void
TFlash::Delete(void)
{
	GlueDelete();
}


// ROM 0x0013bfe8 __eq__FRC21SFlashChipInformationT1
Boolean
operator==(const SFlashChipInformation& a, const SFlashChipInformation& b)
{
	return a.fManufacturer == b.fManufacturer
		&& a.fDevice == b.fDevice
		&& a.fVppKind == b.fVppKind
		&& a.fWidth == b.fWidth
		&& a.fChipSize == b.fChipSize
		&& a.fBlockSize == b.fBlockSize;
}


/*------------------------------------------------------------------------------
	T N e w I n t e r n a l F l a s h
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TNewInternalFlash)		// ROM 0x0013afa0 Sizeof__17TNewInternalFlashSFv
PROTOCOL_CLASSINFO(TNewInternalFlash, "TFlash", "", 0, 0, nil)	// ROM 0x00384654 ClassInfo__17TNewInternalFlashSFv


// ROM 0x0013bf34 New__17TNewInternalFlashFv
// Nothing: Init (or InitForReservedBlock) makes it ready.
TNewInternalFlash*
TNewInternalFlash::New(void)
{
	return this;
}


// ROM 0x0013c50c Delete__17TNewInternalFlashFv
void
TNewInternalFlash::Delete(void)
{
	CleanUp();
}


// ROM 0x0013c8d0 InitializeState__17TNewInternalFlashFP16TMemoryAllocatorQ217TNewInternalFlash13eInitHWOption
// Everything emptied, a lock made (unless this is the boot, which runs
// privileged, before there are semaphores), and the drivers found.
NewtonErr
TNewInternalFlash::InitializeState(TMemoryAllocator* allocator, eInitHWOption option)
{
	fAllocator = allocator;
	fDriverCount = 0;
	fRangeCount = 0;
	fEraseRegionSize = 0;
	fTotalSize = 0;
	fBlockMap = nil;
	fRegionCount = 0;
	fSpareRegion = kNoRegion;
	fErasingRange = nil;
	fEraseResult = noErr;
	fNeedsVpp = false;
	if (!IsSuperMode())
	{
		fLock = new TULockingSemaphore;
		if (fLock == nil)
			return kError_No_Memory;
		NewtonErr err = fLock->Init();
		if (err != noErr)
			return err;
	}
	else
		fLock = nil;
	fMapWindows = option;
	fBankControl = TBankControlRegister::GetBankControlRegister();
	// (the ROM has SearchForFlashDrivers inline here)
	return SearchForFlashDrivers();
}


// ROM 0x0013b908 SearchForFlashDrivers__17TNewInternalFlashFv
// Every driver the ROM extensions name in an 'fdrv entry (the entry: 1,
// then the driver's class info), from the fourth extension down, up to
// six; the ROM's own if there were none.
// DEVIATION: an 'fdrv entry's class info is ARM code, which a host cannot
// run, so the host passes over them; the driver it falls back on is the
// port's (DefaultFlashDriverClassInfo), not T28F016_SA_SVDriver.
NewtonErr
TNewInternalFlash::SearchForFlashDrivers(void)
{
	for (long rexId = 3; rexId >= 0; rexId--)
	{
		ULong length;
		ULong index = 0;
		VAddr entry;
		while ((entry = PrimNextRExConfigEntry((ULong) rexId, 'fdrv', &length, &index)) != 0)
		{
			if (GetBigEndianWord((const void*) entry) == 1)
			{
				// (the driver's class info would be made here - see above)
			}
		}
		if (fDriverCount == 6)
			return noErr;
	}
	if (fDriverCount == 0)
	{
		const TClassInfo* info = DefaultFlashDriverClassInfo();
		void* memory = fAllocator->Allocate((ULong) info->Size());
		if (memory == nil)
			return kError_No_Memory;
		info->MakeAt(memory);
		TFlashDriver* driver = (TFlashDriver*) memory;
		fDrivers[fDriverCount++] = driver;
		NewtonErr err = driver->Init(*fAllocator);
		if (err != noErr)
			return err;
	}
	return noErr;
}


// ROM 0x0013b7f0 CleanUp__17TNewInternalFlashFv
// Everything given back, the last thing made first.
void
TNewInternalFlash::CleanUp(void)
{
	if (fRangeCount != 0)
	{
		for (long i = (long) fRangeCount - 1; i >= 0; i--)
		{
			TFlashRange* range = fRanges[i];
			fRanges[i] = nil;
			range->Delete(*fAllocator);
			range->~TFlashRange();
			fAllocator->Deallocate(range);
		}
		fRangeCount = 0;
	}
	if (fDriverCount != 0)
	{
		for (long i = (long) fDriverCount - 1; i >= 0; i--)
		{
			TFlashDriver* driver = fDrivers[i];
			fDrivers[i] = nil;
			driver->CleanUp(*fAllocator);
			fAllocator->Deallocate(driver);
		}
		fDriverCount = 0;
	}
	if (fBlockMap != nil)
	{
		fAllocator->Deallocate(fBlockMap);
		fBlockMap = nil;
	}
	if (fLock != nil)
	{
		delete fLock;
		fLock = nil;
	}
	gInternalFlash = nil;
}


// ROM 0x0013b714 FlashAllowedLocations__17TNewInternalFlashFRUcT1
// Which of the two banks may hold flash: both, unless the last ROM
// extension with an 'flsa entry (1, a count of at most two, then the
// banks' physical addresses) says otherwise.  An extension already in the
// I/O space rules that bank out.
// ROM QUIRK: allowing only the I/O bank is refused, allowing neither is
// not (the test is `flashBank || ioBank != 1`).
// DEVIATION: the entry's words are big-endian in the host's copy of the image.
NewtonErr
TNewInternalFlash::FlashAllowedLocations(UChar& flashBank, UChar& ioBank)
{
	ioBank = true;
	flashBank = true;
	ULong length;
	VAddr entry = GetLastRExConfigEntry('flsa', &length);
	if (entry != 0 && GetBigEndianWord((const void*) entry) == 1)
	{
		ULong count = GetBigEndianWord((const void*) (entry + 4));
		if (count > 2)
			return kError_Flash_Unsupported_Configuration;
		ioBank = false;
		flashBank = false;
		for (ULong i = 0; i < count; i++)
		{
			ULong bank = GetBigEndianWord((const void*) (entry + 8 + i * 4));
			if (bank == kInternalFlashBank)
				flashBank = true;
			else if (bank == kInternalFlashIOBank)
				ioBank = true;
			else
				return kError_Flash_Unsupported_Configuration;
		}
	}
	if (!flashBank && ioBank == 1)
		return kError_Flash_Unsupported_Configuration;
	if (ioBank)
		AvoidConflictWithRexInIOSpace(ioBank);
	return noErr;
}


// ROM 0x0013b6a4 AvoidConflictWithRexInIOSpace__17TNewInternalFlashFRUc
void
TNewInternalFlash::AvoidConflictWithRexInIOSpace(UChar& ioBank)
{
	for (ULong rexId = 0; rexId < 4; rexId++)
		if (RExAddress(rexId) == kInternalFlashIOBank)
			ioBank = false;
}


// ROM 0x0013b630 FindDriverAble__17TNewInternalFlashFRP12TFlashDriverUl11eMemoryLaneR21SFlashChipInformation
Boolean
TNewInternalFlash::FindDriverAble(TFlashDriver*& driver, ULong address, eMemoryLane lanes, SFlashChipInformation& info)
{
	for (ULong i = 0; i < fDriverCount; i++)
	{
		TFlashDriver* candidate = fDrivers[i];
		if (candidate->Identify(address, lanes, info))
		{
			driver = candidate;
			return true;
		}
	}
	return false;
}


// ROM 0x0013c510 CheckFor4LaneFlash__17TNewInternalFlashFUlR21SFlashChipInformationRP12TFlashDriver
// A 32-bit bank: two identical 16-bit chips, or four identical 8-bit ones,
// all answering to one driver.
Boolean
TNewInternalFlash::CheckFor4LaneFlash(ULong address, SFlashChipInformation& info, TFlashDriver*& driver)
{
	TFlashDriver* driver0;
	TFlashDriver* driver1;
	TFlashDriver* driver2;
	TFlashDriver* driver3;
	SFlashChipInformation info0, info1, info2, info3;
	if (FindDriverAble(driver0, address, kHighHalfLanes, info0)
	 && FindDriverAble(driver1, address, kLowHalfLanes, info1)
	 && driver0 == driver1
	 && info0 == info1)
		goto found;
	if (FindDriverAble(driver0, address, 0x00FF0000, info0)
	 && FindDriverAble(driver1, address, 0x000000FF, info1)
	 && FindDriverAble(driver2, address, 0x0000FF00, info2)
	 && FindDriverAble(driver3, address, 0xFF000000, info3)
	 && driver0 == driver1 && driver0 == driver2 && driver0 == driver3
	 && info0 == info1 && info0 == info2 && info0 == info3)
		goto found;
	return false;

found:
	info = info0;
	driver = driver0;
	return true;
}


// ROM 0x0013c6d8 CheckFor2LaneFlash__17TNewInternalFlashFUlR21SFlashChipInformationRP12TFlashDriver11eMemoryLane
// One 16-bit chip on the half, or two identical 8-bit ones.
Boolean
TNewInternalFlash::CheckFor2LaneFlash(ULong address, SFlashChipInformation& info, TFlashDriver*& driver, eMemoryLane lanes)
{
	TFlashDriver* driver0;
	TFlashDriver* driver1;
	SFlashChipInformation info0, info1;
	if (!FindDriverAble(driver0, address, lanes, info0))
	{
		if (!FindDriverAble(driver0, address, lanes & 0xFF00FF00, info0)
		 || !FindDriverAble(driver1, address, lanes & 0x00FF00FF, info1)
		 || driver0 != driver1
		 || !(info0 == info1))
			return false;
	}
	info = info0;
	driver = driver0;
	return true;
}


// ROM 0x0013c864 CheckFor1LaneFlash__17TNewInternalFlashFUlR21SFlashChipInformationRP12TFlashDriver11eMemoryLane
Boolean
TNewInternalFlash::CheckFor1LaneFlash(ULong address, SFlashChipInformation& info, TFlashDriver*& driver, eMemoryLane lanes)
{
	TFlashDriver* found;
	SFlashChipInformation foundInfo;
	Boolean isThere = FindDriverAble(found, address, lanes, foundInfo);
	if (isThere)
	{
		info = foundInfo;
		driver = found;
	}
	return isThere;
}


// ROM 0x0013c7d4 AlignAndMapVMRange__17TNewInternalFlashFRUlUllUc4Perm
// A window of whole megabytes, mapped section by section if this instance
// maps its windows; virtualAddress moves on past it.
void
TNewInternalFlash::AlignAndMapVMRange(ULong& virtualAddress, ULong physicalAddress, long size, UChar cacheable, Perm perm)
{
	long rounded = size + 0xFFFFF;
	if (rounded < 0)
		rounded = size + 0x1FFFFE;
	ULong start = virtualAddress;
	virtualAddress = start + (rounded >> 20) * 0x100000;
	if (fMapWindows != 0)
	{
		for (ULong v = start; v < virtualAddress; v += 0x100000, physicalAddress += 0x100000)
			AddNewSecPNJT(v, physicalAddress, 0, perm, cacheable);
	}
}


// ROM 0x0013c9c8 AddFlashRange__17TNewInternalFlashFP11TFlashRangeRUlN22Ul
// A range taken on: the flash addresses move past it, it gets a read
// window (cached) and a write window (uncached; twice or four times as big
// for a narrower range, a word of the bus to every two bytes or byte), and
// its blocks' status is cleared.
NewtonErr
TNewInternalFlash::AddFlashRange(TFlashRange* range, ULong& flashAddress, ULong& readAddress, ULong& writeAddress, ULong physicalAddress)
{
	ULong index = fRangeCount;
	if (index >= 3)
		return kError_Flash_Unsupported_Configuration;
	fRangeCount = index + 1;
	fRanges[index] = range;
	fNeedsVpp = fNeedsVpp || range->fChipInfo.fVppKind != 0;
	long size = (long) range->fSize;
	flashAddress += size;
	AlignAndMapVMRange(readAddress, physicalAddress, size, 1, kReadOnly);
	AlignAndMapVMRange(writeAddress, physicalAddress, size * (4 / range->fLaneCount), 0, kReadWrite);
	if (fEraseRegionSize < range->fBlockSize)
		fEraseRegionSize = range->fBlockSize;
	range->ResetAllBlocksStatus();
	return noErr;
}


// ROM 0x0013cc44 ConfigureFlashBank__17TNewInternalFlashFRUlN21
// The flash bank at 0x02000000: 32 bits wide if the drivers find chips on
// all four lanes; otherwise each 16-bit half on its own, the low half
// first, each a range of its own - the low half must be there.
// DEVIATION: the ranges are as big as a range is on the host (the ROM
// allocates 0x4C bytes: a TFlashRange holds a pointer).
NewtonErr
TNewInternalFlash::ConfigureFlashBank(ULong& flashAddress, ULong& readAddress, ULong& writeAddress)
{
	if (fMapWindows != 0)
		AddNewSecPNJT(kInternalFlashWriteWindow, kInternalFlashBank, 0, kReadWrite, 0);
	fBankControl->ConfigureFlashBankDataSize(kAllLanes);
	SFlashChipInformation info;
	TFlashDriver* driver;
	if (!CheckFor4LaneFlash(kInternalFlashWriteWindow, info, driver))
	{
		TFlashRange* range;
		NewtonErr err = ConfigureNot32BitFlashBank(flashAddress, readAddress, writeAddress, kLowHalfLanes, range);
		if (err != noErr)
			return err;
		if (range == nil)
			return kError_Flash_Unsupported_Configuration;
		err = AddFlashRange(range, flashAddress, readAddress, writeAddress, kInternalFlashBank);
		if (err != noErr)
			return err;
		err = ConfigureNot32BitFlashBank(flashAddress, readAddress, writeAddress, kHighHalfLanes, range);
		if (err != noErr)
			return err;
		if (range == nil)
			return noErr;
		return AddFlashRange(range, flashAddress, readAddress, writeAddress, kInternalFlashBank);
	}
	void* memory = fAllocator->Allocate(sizeof(T32BitFlashRange));
	if (memory == nil)
		return kError_No_Memory;
	TFlashRange* range = new (memory) T32BitFlashRange(driver, flashAddress, readAddress, writeAddress, kAllLanes, info, *fAllocator);
	return AddFlashRange(range, flashAddress, readAddress, writeAddress, kInternalFlashBank);
}


// ROM 0x0013ce2c ConfigureNot32BitFlashBank__17TNewInternalFlashFRUlN2111eMemoryLaneRP11TFlashRange
// A range for one half of the bank: 16 bits wide, or failing that the
// half's upper byte alone.  No range, and no error, if there is neither.
NewtonErr
TNewInternalFlash::ConfigureNot32BitFlashBank(ULong& flashAddress, ULong& readAddress, ULong& writeAddress,
											  eMemoryLane lanes, TFlashRange*& range)
{
	range = nil;
	SFlashChipInformation info;
	TFlashDriver* driver;
	if (CheckFor2LaneFlash(kInternalFlashWriteWindow, info, driver, lanes))
	{
		void* memory = fAllocator->Allocate(sizeof(T16BitFlashRange));
		if (memory == nil)
			return kError_No_Memory;
		range = new (memory) T16BitFlashRange(driver, flashAddress, readAddress, writeAddress, lanes, info, *fAllocator);
	}
	else
	{
		if (!CheckFor1LaneFlash(kInternalFlashWriteWindow, info, driver, lanes & 0xFF00FF00))
			return noErr;
		void* memory = fAllocator->Allocate(sizeof(T8BitFlashRange));
		if (memory == nil)
			return kError_No_Memory;
		range = new (memory) T8BitFlashRange(driver, flashAddress, readAddress, writeAddress, lanes & 0xFF00FF00, info, *fAllocator);
	}
	return noErr;
}


// ROM 0x0013caf0 ConfigureIOBank__17TNewInternalFlashFRUlN21
// The second bank, at 0x10000000 in I/O space: only ever 32 bits wide.
NewtonErr
TNewInternalFlash::ConfigureIOBank(ULong& flashAddress, ULong& readAddress, ULong& writeAddress)
{
	if (fMapWindows != 0)
		AddNewSecPNJT(writeAddress, kInternalFlashIOBank, 0, kReadWrite, 0);
	SFlashChipInformation info;
	TFlashDriver* driver;
	if (!CheckFor4LaneFlash(writeAddress, info, driver))
		return noErr;
	void* memory = fAllocator->Allocate(sizeof(T32BitFlashRange));
	if (memory == nil)
		return kError_No_Memory;
	TFlashRange* range = new (memory) T32BitFlashRange(driver, flashAddress, readAddress, writeAddress, kAllLanes, info, *fAllocator);
	return AddFlashRange(range, flashAddress, readAddress, writeAddress, kInternalFlashIOBank);
}


// ROM 0x0013b444 InternalInit__17TNewInternalFlashFP16TMemoryAllocatorQ217TNewInternalFlash13eInitHWOption
// The banks found and their ranges made.  The first erase region - the
// reserved block - is then taken out of the flash addresses: the first
// range starts that much later in its windows and everything after it
// that much earlier.  What is left is fRegionCount regions, one of them
// the spare, so the store gets one region less again.
NewtonErr
TNewInternalFlash::InternalInit(TMemoryAllocator* allocator, eInitHWOption option)
{
	NewtonErr err = InitializeState(allocator, option);
	if (err != noErr)
		return err;
	UChar flashBank, ioBank;
	err = FlashAllowedLocations(flashBank, ioBank);
	if (err != noErr)
		return err;
	ULong flashAddress = 0;
	ULong readAddress = kInternalFlashReadWindow;
	ULong writeAddress = kInternalFlashWriteWindow;
	if (flashBank && (err = ConfigureFlashBank(flashAddress, readAddress, writeAddress)) != noErr)
		return err;
	if (ioBank && (err = ConfigureIOBank(flashAddress, readAddress, writeAddress)) != noErr)
		return err;
	fTotalSize = flashAddress;
	if (fRangeCount != 0)
	{
		fTotalSize = flashAddress - fEraseRegionSize;
		TFlashRange* first = fRanges[0];
		first->fSize -= fEraseRegionSize;
		first->AdjustVirtualAddresses((long) fEraseRegionSize);
		for (ULong i = 1; i < fRangeCount; i++)
			fRanges[i]->fStart -= fEraseRegionSize;
	}
	// DEVIATION: with no flash at all the ROM divides by nought here, which
	// traps into its division handler; the host answers that there is none.
	if (fEraseRegionSize == 0)
		return kError_No_Flash_In_MotherBoard;
	fRegionCount = fTotalSize / fEraseRegionSize;
	gInternalFlashNeedsVpp = (fNeedsVpp != 0);
	gInternalFlashStoreSize = fTotalSize - fEraseRegionSize;
	gInternalFlash = this;
	return noErr;
}


// ROM 0x0013b5b8 InitForReservedBlock__17TNewInternalFlashFP16TMemoryAllocatorQ217TNewInternalFlash13eInitHWOption
// The ranges only, for the reserved block's accessor (a branch to
// InternalInit).
NewtonErr
TNewInternalFlash::InitForReservedBlock(TMemoryAllocator* allocator, eInitHWOption option)
{
	return InternalInit(allocator, option);
}


// ROM 0x0013b5bc Init__17TNewInternalFlashFP16TMemoryAllocator
// Ready for the flash store: the ranges (their windows already mapped at
// boot), then the regions' map read off the flash.  A flash whose regions
// make no sense is wiped - and still answers kSError_NeedsFormat, so that
// the store above formats it.
NewtonErr
TNewInternalFlash::Init(TMemoryAllocator* allocator)
{
	NewtonErr err = InternalInit(allocator, kDontMapWindows);
	if (err == noErr)
	{
		UChar* map = (UChar*) fAllocator->Allocate(fRegionCount * 2 - 2);
		if (map == nil)
			return kError_No_Memory;
		fBlockMap = map;
		err = SetupVirtualMappings();
		if (err == kSError_NeedsFormat)
			Clobber();
	}
	return err;
}


// ROM 0x0013afe0 AllocateReservedBlockRange__17TNewInternalFlashFRP11TFlashRange
// A range for the reserved block alone: the bank's low half, from flash
// address 0 (the reserved block's accessor reads it 16 bits wide).
NewtonErr
TNewInternalFlash::AllocateReservedBlockRange(TFlashRange*& range)
{
	ULong writeAddress = kInternalFlashWriteWindow;
	ULong readAddress = kInternalFlashReadWindow;
	ULong flashAddress = 0;
	fBankControl->ConfigureFlashBankDataSize(kAllLanes);
	TFlashRange* made;
	NewtonErr err = ConfigureNot32BitFlashBank(flashAddress, readAddress, writeAddress, kLowHalfLanes, made);
	range = made;
	return err;
}


// ROM 0x0013b050 GatherBlockMappingInfo__17TNewInternalFlashFRUlN31
// Each physical region's header read: a mapped region goes into the map
// (a logical region mapped twice, or out of range, means the flash needs
// formatting), and there may be one each of an erased region, a region
// being erased, a region with any other header (a stray) and a logical
// region nothing holds.
NewtonErr
TNewInternalFlash::GatherBlockMappingInfo(ULong& erasedRegion, ULong& erasingRegion, ULong& strayRegion, ULong& unmappedRegion)
{
	UChar header[4];
	header[0] = header[1] = header[2] = header[3] = 0xFF;
	ULong physicalAddress = 0;
	ULong regionSize = fEraseRegionSize;
	UChar* map = fBlockMap;
	ULong regionCount = fTotalSize / regionSize;
	ULong region = 0;
	if (regionCount != 0)
	{
		do
		{
			NewtonErr err = ReadPhysical(physicalAddress, 4, (char*) header);
			if (err != noErr)
				return err;
			ULong logical = ((ULong) header[0] << 8) | header[1];
			ULong state = ((ULong) header[2] << 8) | header[3];
			if (state == kRegionMapped)
			{
				if (logical < regionCount - 1)
				{
					if (MapEntry(map, logical) != kNoRegion)
						return kSError_NeedsFormat;
					SetMapEntry(map, logical, region);
				}
				else
				{
					if (strayRegion != kNoRegion)
						return kSError_NeedsFormat;
					strayRegion = region;
				}
			}
			else if (state == 0xFFFF && logical == 0xFFFF)
			{
				if (erasedRegion != kNoRegion)
					return kSError_NeedsFormat;
				erasedRegion = region;
			}
			else if (state == kRegionErasing)
			{
				if (erasingRegion != kNoRegion)
					return kSError_NeedsFormat;
				erasingRegion = region;
			}
			else
			{
				if (strayRegion != kNoRegion)
					return kSError_NeedsFormat;
				strayRegion = region;
			}
			region++;
			physicalAddress += regionSize;
		} while (region < regionCount);
	}
	ULong logical = 0;
	if (regionCount != 1)
	{
		do
		{
			if (MapEntry(map, logical) == kNoRegion)
			{
				if (unmappedRegion != kNoRegion)
					return kSError_NeedsFormat;
				unmappedRegion = logical;
			}
			logical++;
		} while (logical < regionCount - 1);
	}
	return noErr;
}


// ROM 0x0013b214 SetupVirtualMappings__17TNewInternalFlashFv
// The map read off the flash, and an erase an interruption left half done
// finished, so that there is a spare.  What the flash may be found in:
// 1 (the spare erased: the usual), 2 (a region part-way through giving
// itself up), 3 (both), 4 (a stray) and 11 (a region part-way through, the
// spare erased and a logical region with nowhere to be); anything else
// needs formatting.
// ROM BUG: 11 is what an interruption between the first two writes of
// Erase leaves - the region giving itself up marked, the spare not yet
// taken, so the logical region is held by neither.  The marked region is
// erased and made the spare, but the logical region is left without a
// place and the old spare stays erased too; the start after that finds two
// erased regions, and the flash is wiped.
// (The ROM keeps the table as a twelve-character string on its stack,
// "011110000001", indexed by the combination; 12-15 index past it into
// the next local, a region number whose high byte is nought, so they are
// refused too.)
NewtonErr
TNewInternalFlash::SetupVirtualMappings(void)
{
	UChar* map = fBlockMap;
	ULong logical = 0;
	if (fRegionCount != 1)
	{
		do
		{
			SetMapEntry(map, logical, kNoRegion);
			logical++;
		} while (logical < fRegionCount - 1);
	}
	ULong erasedRegion = kNoRegion;
	ULong erasingRegion = kNoRegion;
	ULong strayRegion = kNoRegion;
	ULong unmappedRegion = kNoRegion;
	NewtonErr err = GatherBlockMappingInfo(erasedRegion, erasingRegion, strayRegion, unmappedRegion);
	if (err != noErr)
		return err;
	ULong combination = 0;
	if (unmappedRegion != kNoRegion)
		combination = 8;
	if (strayRegion != kNoRegion)
		combination |= 4;
	if (erasingRegion != kNoRegion)
		combination |= 2;
	if (erasedRegion != kNoRegion)
		combination |= 1;
	static const char kAcceptable[16] = { '0','1','1','1','1','0','0','0', '0','0','0','1', 0,0,0,0 };
	if (kAcceptable[combination] != '1')
		return kSError_NeedsFormat;
	if (erasedRegion != kNoRegion)
		fSpareRegion = erasedRegion;
	if (erasingRegion == kNoRegion)
	{
		if (strayRegion == kNoRegion)
			return noErr;
		SyncErasePhysicalBlock(strayRegion * fEraseRegionSize);
		erasingRegion = strayRegion;
	}
	else
		SyncErasePhysicalBlock(erasingRegion * fEraseRegionSize);
	fSpareRegion = erasingRegion;
	return noErr;
}


// ROM 0x0013b38c FindRange__17TNewInternalFlashFUlRP11TFlashRange
NewtonErr
TNewInternalFlash::FindRange(ULong physicalAddress, TFlashRange*& range)
{
	if (physicalAddress < fTotalSize && fRangeCount != 0)
	{
		for (ULong i = 0; i < fRangeCount; i++)
		{
			TFlashRange* candidate = fRanges[i];
			if (physicalAddress < candidate->fStart + candidate->fSize)
			{
				range = candidate;
				return noErr;
			}
		}
	}
	return kError_Flash_AddressOutOfRange;
}


// ROM 0x0013b3e8 SyncErasePhysicalBlock__17TNewInternalFlashFUl
// Erased and waited for - once any erase under way is over (whose error,
// if it had one, is the answer instead).
NewtonErr
TNewInternalFlash::SyncErasePhysicalBlock(ULong physicalAddress)
{
	TFlashRange* range;
	long err = FindRange(physicalAddress, range);
	if (err != noErr)
		return err;
	InternalCheckEraseCompletion(err, kWaitForErase);
	if (err != noErr)
		return err;
	return range->SyncErase(physicalAddress, fEraseRegionSize);
}


// ROM 0x0013ba80 ReadWrite__17TNewInternalFlashFM11TFlashRangeFUlT2Pc_lN22Pc
// Logical addresses to physical ones, a region at a time.
NewtonErr
TNewInternalFlash::ReadWrite(long (TFlashRange::*op)(ULong, ULong, char*), ULong address, ULong size, char* buffer)
{
	while (size != 0)
	{
		ULong regionSize = fEraseRegionSize;
		ULong logical = address / regionSize;
		ULong offset = address % regionSize;
		ULong piece = regionSize - offset;
		if (piece > size)
			piece = size;
		NewtonErr err = ReadWritePhysical(op, MapEntry(fBlockMap, logical) * regionSize + offset, piece, buffer);
		if (err != noErr)
			return err;
		size -= piece;
		address += piece;
		buffer += piece;
	}
	return noErr;
}


// ROM 0x0013bb10 ReadWritePhysical__17TNewInternalFlashFM11TFlashRangeFUlT2Pc_lN22Pc
// Within one range; out of range if it runs past the range's end.
NewtonErr
TNewInternalFlash::ReadWritePhysical(long (TFlashRange::*op)(ULong, ULong, char*), ULong address, ULong size, char* buffer)
{
	ULong i;
	TFlashRange* range = nil;
	for (i = 0; i < fRangeCount; i++)
	{
		range = fRanges[i];
		if (address < range->fStart + range->fSize)
			break;
	}
	if (i >= fRangeCount)
		return kError_Flash_AddressOutOfRange;
	ULong available = (range->fSize + range->fStart) - address;
	if (available < size)
		return kError_Flash_AddressOutOfRange;
	if (size < available)
		available = size;
	return (range->*op)(address, available, buffer);
}


// ROM 0x0013bb98 ReadPhysical__17TNewInternalFlashFUlT1Pc
NewtonErr
TNewInternalFlash::ReadPhysical(ULong address, ULong size, char* buffer)
{
	return ReadWritePhysical(&TFlashRange::Read, address, size, buffer);
}


// ROM 0x0013bbc0 WritePhysical__17TNewInternalFlashFUlT1Pc
// A write that failed for want of programming voltage is tried again; the
// second time the machine is turned off and rebooted (and should that
// return, it goes on trying).
NewtonErr
TNewInternalFlash::WritePhysical(ULong address, ULong size, char* buffer)
{
	int tries = 0;
	NewtonErr err;
	do
	{
		while ((err = ReadWritePhysical(&TFlashRange::Write, address, size, buffer)) == kError_Flash_Vpp_Low)
		{
			tries++;
			if (tries == 2)
				PowerOffAndReboot(kError_Reboot_Power_Fault);
		}
	} while (err == kError_Flash_Vpp_Low);
	return err;
}


// ROM 0x0013bc40 Read__17TNewInternalFlashFUlT1Pc
NewtonErr
TNewInternalFlash::Read(ULong address, ULong size, char* buffer)
{
	TULockingSemaphoreGrabber lock(fLock);
	return ReadWrite(&TFlashRange::Read, address, size, buffer);
}


// ROM 0x0013bca8 Write__17TNewInternalFlashFUlT1Pc
NewtonErr
TNewInternalFlash::Write(ULong address, ULong size, char* buffer)
{
	TULockingSemaphoreGrabber lock(fLock);
	return ReadWrite(&TFlashRange::Write, address, size, buffer);
}


// ROM 0x0013bd10 TurnPowerOn__17TNewInternalFlashFv
// The programming voltage, if these chips need it.  (The ROM has
// InternalVppOn inline; in a privileged mode it sets the GPIO bits
// itself.)  DEVIATION: the privileged path is hardware, and a host has
// nothing to switch.
void
TNewInternalFlash::TurnPowerOn(void)
{
	if (!fNeedsVpp)
		return;
	if (!IsSuperMode())
		InternalVppOn();
}


// ROM 0x0013bd9c TurnPowerOff__17TNewInternalFlashFv
void
TNewInternalFlash::TurnPowerOff(void)
{
	if (!fNeedsVpp)
		return;
	if (!IsSuperMode())
		InternalVppOff();
}


// ROM 0x0013bdd0 Clobber__17TNewInternalFlashFv
void
TNewInternalFlash::Clobber(void)
{
	TULockingSemaphoreGrabber lock(fLock);
	InternalClobber();
}


// ROM 0x0013be0c InternalClobber__17TNewInternalFlashFv
// Every range erased and every region but the last given a logical region
// in order; the last is the spare.  Anything going wrong reboots.
void
TNewInternalFlash::InternalClobber(void)
{
	NewtonErr err = noErr;
	TurnPowerOn();
	for (ULong i = 0; i < fRangeCount; i++)
	{
		err = fRanges[i]->EraseRange();
		if (err != noErr)
			Reboot(0, 0, false);
	}
	UChar header[4];
	header[3] = 0xFF;
	header[2] = 0;
	ULong physicalAddress = 0;
	ULong region = 0;
	if (fRegionCount != 1)
	{
		do
		{
			header[0] = (UChar) (region >> 8);
			header[1] = (UChar) region;
			err = WritePhysical(physicalAddress, 4, (char*) header);
			if (err != noErr)
				Reboot(0, 0, false);
			region++;
			physicalAddress += fEraseRegionSize;
		} while (region < fRegionCount - 1);
	}
	if (fBlockMap != nil)
		err = SetupVirtualMappings();
	if (err != noErr)
		Reboot(0, 0, false);
	TurnPowerOff();
}


// ROM 0x0013bf7c CheckEraseCompletion__17TNewInternalFlashFRlQ217TNewInternalFlash17eCheckEraseOption
// Only if the lock can be had at once.
// ROM QUIRK: an instance with no lock (one made at boot) always answers
// "not complete".
Boolean
TNewInternalFlash::CheckEraseCompletion(long& result, eCheckEraseOption option)
{
	TULockingSemaphoreGrabber lock(fLock, TULockingSemaphoreGrabber::kNonBlocking);
	if (!lock.Held())
		return false;
	return InternalCheckEraseCompletion(result, option);
}


// ROM 0x0013c040 InternalCheckEraseCompletion__17TNewInternalFlashFRlQ217TNewInternalFlash17eCheckEraseOption
// Whether the erase Erase started is over, waiting for it if asked to;
// result is its error, or an earlier one not yet seen to.
Boolean
TNewInternalFlash::InternalCheckEraseCompletion(long& result, eCheckEraseOption option)
{
	result = fEraseResult;
	if (fErasingRange != nil)
	{
		while (!fErasingRange->IsEraseComplete(result))
		{
			if (option == kDontWaitForErase)
				return false;
			Sleep(0x47fe);
		}
		fErasingRange = nil;
		if (result != noErr)
			fEraseResult = result;
	}
	return true;
}


// ROM 0x0013c0c4 IsInternalFlashEraseActive__Fv
Boolean
IsInternalFlashEraseActive(void)
{
	if (gInternalFlash != nil)
	{
		long result;
		return !gInternalFlash->CheckEraseCompletion(result, TNewInternalFlash::kDontWaitForErase);
	}
	return false;
}


// ROM 0x0013c10c Erase__17TNewInternalFlashFUl
// A logical region erased by swapping it with the spare: its region
// marked as being erased, the spare marked as holding it and put in the
// map, its old header wiped to nought, and its old region's erase started
// (left to finish in the background) - so the region Erase is asked about
// reads as erased at once.  An earlier erase that failed is first done
// again on the spare.  Any write going wrong turns the machine off.
NewtonErr
TNewInternalFlash::Erase(ULong address)
{
	TULockingSemaphoreGrabber lock(fLock);
	long result;
	InternalCheckEraseCompletion(result, kWaitForErase);
	if (result != noErr)
	{
		fEraseResult = noErr;
		result = SyncErasePhysicalBlock(fEraseRegionSize * fSpareRegion);
		if (result != noErr)
			PowerOffAndReboot(kError_Reboot_Power_Fault);
	}
	ULong regionSize = fEraseRegionSize;
	ULong logical = address / regionSize;
	ULong physical = MapEntry(fBlockMap, logical);
	ULong physicalAddress = physical * regionSize;
	ULong spare = fSpareRegion;
	UChar header[4];
	header[0] = (UChar) (logical >> 8);
	header[1] = (UChar) logical;
	header[3] = 0x0F;
	header[2] = 0;
	result = WritePhysical(physicalAddress, 4, (char*) header);
	if (result != noErr)
		PowerOffAndReboot(kError_Reboot_Power_Fault);
	header[3] = 0xFF;
	header[2] = 0;
	result = WritePhysical(spare * regionSize, 4, (char*) header);
	if (result != noErr)
		PowerOffAndReboot(kError_Reboot_Power_Fault);
	header[3] = 0;
	header[2] = 0;
	header[1] = 0;
	header[0] = 0;
	result = WritePhysical(physicalAddress, 4, (char*) header);
	if (result != noErr)
		PowerOffAndReboot(kError_Reboot_Power_Fault);
	SetMapEntry(fBlockMap, logical, fSpareRegion);
	fSpareRegion = physical;
	TFlashRange* range;
	result = FindRange(physicalAddress, range);
	if (result != noErr)
		PowerOffAndReboot(kError_Reboot_Power_Fault);
	InternalVppOn();
	result = range->StartErase(physicalAddress, fEraseRegionSize);
	if (result != noErr)
		PowerOffAndReboot(kError_Reboot_Power_Fault);
	fErasingRange = range;
	InternalVppOff();
	return noErr;
}


// ROM 0x0013c2ec CopyUsingBuffer__17TNewInternalFlashFUlN21PvT1
NewtonErr
TNewInternalFlash::CopyUsingBuffer(ULong from, ULong to, ULong size, void* buffer, ULong bufferSize)
{
	while (size != 0)
	{
		ULong piece = size;
		if (bufferSize < size)
			piece = bufferSize;
		NewtonErr err = Read(from, piece, (char*) buffer);
		if (err != noErr)
			return err;
		err = Write(to, piece, (char*) buffer);
		if (err != noErr)
			return err;
		from += piece;
		to += piece;
		size -= piece;
	}
	return noErr;
}


// ROM 0x0013c378 Copy__17TNewInternalFlashFUlN21
NewtonErr
TNewInternalFlash::Copy(ULong from, ULong to, ULong size)
{
	char buffer[256];
	return CopyUsingBuffer(from, to, size, buffer, sizeof(buffer));
}


// ROM 0x0013c3ac IsVirgin__17TNewInternalFlashFUlT1
// Whether the logical bytes are all erased - a region's header, which is
// the flash's own, not counting.
// ROM BUG: a piece that starts a region and is shorter than its header
// asks about a length that has wrapped round.
Boolean
TNewInternalFlash::IsVirgin(ULong address, ULong size)
{
	TULockingSemaphoreGrabber lock(fLock);
	if (address >= fTotalSize - fEraseRegionSize)
		return false;
	for ( ; size != 0; )
	{
		ULong regionSize = fEraseRegionSize;
		ULong logical = address / regionSize;
		ULong offset = address % regionSize;
		ULong piece = regionSize - offset;
		if (size < piece)
			piece = size;
		ULong physicalAddress = regionSize * MapEntry(fBlockMap, logical);
		TFlashRange* range;
		if (FindRange(physicalAddress, range) != noErr)
			return false;
		ULong length = piece;
		if (offset == 0)
		{
			offset = 4;
			length = piece - 4;
		}
		if (!range->IsVirgin(physicalAddress + offset, length))
			return false;
		address += piece;
		size -= piece;
	}
	return true;
}


// ROM 0x0013c4d8 ClobberInternalFlash__Fv
// The whole internal flash wiped, by an instance made for the purpose.
void
ClobberInternalFlash(void)
{
	TMemoryAllocator* allocator = THeapAllocator::GetGlobalAllocator();
	const TClassInfo* info = TNewInternalFlash::ClassInfo();
	alignas(TNewInternalFlash) char memory[sizeof(TNewInternalFlash)];
	info->MakeAt(memory);
	TNewInternalFlash* flash = (TNewInternalFlash*) memory;
	flash->Init(allocator);
	flash->Clobber();
	flash->CleanUp();
}


/*------------------------------------------------------------------------------
	The rest of TFlash, which the internal flash has little to say to.
------------------------------------------------------------------------------*/

// ROM 0x0013c998 SuspendErase__17TNewInternalFlashFUlN21
NewtonErr	TNewInternalFlash::SuspendErase(ULong, ULong, ULong)	{ return noErr; }
// ROM 0x0013c9a0 ResumeErase__17TNewInternalFlashFUl
NewtonErr	TNewInternalFlash::ResumeErase(ULong)					{ return noErr; }
// ROM 0x0013c9a8 DeepSleep__17TNewInternalFlashFUl
NewtonErr	TNewInternalFlash::DeepSleep(ULong)						{ return noErr; }
// ROM 0x0013c9b0 Wakeup__17TNewInternalFlashFUl
NewtonErr	TNewInternalFlash::Wakeup(ULong)						{ return noErr; }
// ROM 0x0013bf68 Status__17TNewInternalFlashFUl
NewtonErr	TNewInternalFlash::Status(ULong)						{ return 1; }
// ROM 0x0013c9b8 ResetCard__17TNewInternalFlashFv
NewtonErr	TNewInternalFlash::ResetCard(void)						{ return noErr; }
// ROM 0x0013c994 AcknowledgeReset__17TNewInternalFlashFv
void		TNewInternalFlash::AcknowledgeReset(void)				{ }
// ROM 0x0013c9c0 GetPhysResource__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetPhysResource(void)				{ return 0; }
// ROM 0x0013caa4 RegisterClientInfo__17TNewInternalFlashFUl
void		TNewInternalFlash::RegisterClientInfo(ULong)			{ }
// ROM 0x0013bf70 GetWriteProtected__17TNewInternalFlashFPUc
void		TNewInternalFlash::GetWriteProtected(UChar* isProtected)	{ *isProtected = false; }
// ROM 0x0013caa8 GetWriteErrorAddress__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetWriteErrorAddress(void)			{ return 0; }
// ROM 0x0013bf38 GetAttributes__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetAttributes(void)					{ return fNeedsVpp ? kFlashAttrNeedsVpp : (kFlashAttrNeedsVpp | kFlashAttrNoVpp); }
// ROM 0x0013cab0 GetDataOffset__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetDataOffset(void)					{ return 0; }
// ROM 0x0013bf58 GetTotalSize__17TNewInternalFlashFv
// (the spare region is not the store's)
ULong		TNewInternalFlash::GetTotalSize(void)					{ return fTotalSize - fEraseRegionSize; }
// ROM 0x0013cab8 GetGroupSize__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetGroupSize(void)					{ return 0; }
// ROM 0x0013bf50 GetEraseRegionSize__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetEraseRegionSize(void)				{ return fEraseRegionSize; }
// ROM 0x0013cac0 GetChipsPerGroup__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetChipsPerGroup(void)				{ return 0; }
// ROM 0x0013cac8 GetBlocksPerPartition__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetBlocksPerPartition(void)			{ return 0; }
// ROM 0x0013cad0 GetMaxConcurrentVppOps__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetMaxConcurrentVppOps(void)			{ return 0; }
// ROM 0x0013cad8 GetEraseRegionCurrent__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetEraseRegionCurrent(void)			{ return 0; }
// ROM 0x0013cae0 GetWriteRegionCurrent__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetWriteRegionCurrent(void)			{ return 0; }
// ROM 0x0013c98c GetEraseRegionTime__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetEraseRegionTime(void)				{ return 0; }
// ROM 0x0013cae8 GetWriteAccessTime__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetWriteAccessTime(void)				{ return 0; }
// ROM 0x0013cbfc GetReadAccessTime__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetReadAccessTime(void)				{ return 0; }
// ROM 0x0013cc04 GetVendorInfo__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetVendorInfo(void)					{ return 0; }
// ROM 0x0013cc0c GetSocketNumber__17TNewInternalFlashFv
ULong		TNewInternalFlash::GetSocketNumber(void)				{ return 0; }
// ROM 0x0013cc14 VppStatus__17TNewInternalFlashFv
ULong		TNewInternalFlash::VppStatus(void)						{ return 0; }
// ROM 0x0013cc1c VppRisingTime__17TNewInternalFlashFv
ULong		TNewInternalFlash::VppRisingTime(void)					{ return 0; }
// ROM 0x0013cc24 FlashSpecific__17TNewInternalFlashFUlPvT1
NewtonErr	TNewInternalFlash::FlashSpecific(ULong, void*, ULong)	{ return noErr; }
// ROM 0x0013cc2c Initialize__17TNewInternalFlashFP11TCardSocketP11TCardPCMCIAUlT3
NewtonErr	TNewInternalFlash::Initialize(TCardSocket*, TCardPCMCIA*, ULong, ULong)	{ return noErr; }
// ROM 0x0013cc34 SuspendService__17TNewInternalFlashFv
NewtonErr	TNewInternalFlash::SuspendService(void)					{ return noErr; }
// ROM 0x0013cc3c ResumeService__17TNewInternalFlashFP11TCardSocketP11TCardPCMCIAUl
NewtonErr	TNewInternalFlash::ResumeService(TCardSocket*, TCardPCMCIA*, ULong)	{ return noErr; }
