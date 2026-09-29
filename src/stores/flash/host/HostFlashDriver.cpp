/*
	File:		stores/flash/host/HostFlashDriver.cpp

	Contains:	THostFlashDriver, the TFlashDriver of the host's flash chips
				(hal/host/HostFlash.h), which the internal flash falls back
				on where the ROM's falls back on T28F016_SA_SVDriver.

				DEVIATION: the ROM's driver speaks the Intel 28F016SA/SV
				command set to the chips - page buffer loads, block erases
				polled through the status registers.  The host's chips are
				bytes in a file, so this driver does what those commands
				come to: a word ANDed in, a block set to 0xFF, and every
				status "ready, no error".  It says it has found what an
				MP2x00 has, two 16-bit Intel 28F016SA parts (manufacturer
				0x89, device 0x66A0: 2 MB each in 64 KB blocks) making a
				32-bit bank - the answers T28F016_SA_SVDriver::Identify
				(ROM 0x002044ec) gives for them - so the ranges, the erase
				regions and the whole layout of the flash are the machine's.

	Written by:	the reconstruction
*/

#include "Flash.h"
#include "MemoryAllocator.h"
#include "HostFlash.h"
#include "hal/MMU.h"


PROTOCOL THostFlashDriver : public TFlashDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostFlashDriver);

	THostFlashDriver*	New(void)		{ return this; }
	void		Delete(void)			{ }

	Boolean		Identify(ULong address, eMemoryLane lanes, SFlashChipInformation& info);
	void		CleanUp(TMemoryAllocator& allocator);
	NewtonErr	Init(TMemoryAllocator& allocator);
	NewtonErr	InitializeDriverData(TFlashRange& range, TMemoryAllocator& allocator);
	void		CleanUpDriverData(TFlashRange& range, TMemoryAllocator& allocator);
	void		StartReadingArray(TFlashRange& range);
	void		DoneReadingArray(TFlashRange& range);
	void		Write(ULong value, ULong mask, ULong address, TFlashRange& range);
	NewtonErr	StartErase(TFlashRange& range, ULong blockAddress);
	void		ResetBlockStatus(TFlashRange& range, ULong blockAddress);
	Boolean		IsEraseComplete(TFlashRange& range, ULong blockAddress, long& result);
	NewtonErr	LockBlock(TFlashRange& range, ULong blockAddress);
	void		BeginWrite(TFlashRange& range, ULong address, ULong wordCount);
	NewtonErr	ReportWriteResult(TFlashRange& range, ULong blockAddress);
};

PROTOCOL_IMPL_SOURCE_MACRO(THostFlashDriver)
PROTOCOL_CLASSINFO(THostFlashDriver, "TFlashDriver", "", 0, 0, nil)


const TClassInfo*
DefaultFlashDriverClassInfo(void)
{
	return THostFlashDriver::ClassInfo();
}


// Chips answer only where the host has flash, and only as 16-bit parts:
// on either half of the bus, as the internal flash asks first.
Boolean
THostFlashDriver::Identify(ULong address, eMemoryLane lanes, SFlashChipInformation& info)
{
	Ptr p = VirtualAddressToPointer((VAddr) address);
	if (p == nil || !HostFlashContains(p))
		return false;
	if (lanes != kLowHalfLanes && lanes != kHighHalfLanes)
		return false;
	info.fManufacturer = 0x89;
	info.fDevice = 0x66A0;
	info.fVppKind = 2;
	info.fWidth = 2;
	info.fChipSize = 0x200000;
	info.fBlockSize = 0x10000;
	return true;
}


void		THostFlashDriver::CleanUp(TMemoryAllocator&)							{ }
NewtonErr	THostFlashDriver::Init(TMemoryAllocator&)								{ return noErr; }
NewtonErr	THostFlashDriver::InitializeDriverData(TFlashRange& range, TMemoryAllocator&)	{ range.fDriverData = nil; return noErr; }
void		THostFlashDriver::CleanUpDriverData(TFlashRange& range, TMemoryAllocator&)		{ range.fDriverData = nil; }
void		THostFlashDriver::StartReadingArray(TFlashRange&)						{ }
void		THostFlashDriver::DoneReadingArray(TFlashRange&)						{ }
void		THostFlashDriver::BeginWrite(TFlashRange&, ULong, ULong)				{ }
void		THostFlashDriver::ResetBlockStatus(TFlashRange&, ULong)					{ }
NewtonErr	THostFlashDriver::LockBlock(TFlashRange&, ULong)						{ return noErr; }
NewtonErr	THostFlashDriver::ReportWriteResult(TFlashRange&, ULong)				{ return noErr; }


void
THostFlashDriver::Write(ULong value, ULong mask, ULong address, TFlashRange&)
{
	HostFlashWrite(VirtualAddressToPointer((VAddr) address), value, mask);
}


// A block of every chip at once: the range's erase unit, spread over the
// write window as widely as the range's words are (a word of the bus for
// every four, two or one of its bytes), only its own lanes touched.
NewtonErr
THostFlashDriver::StartErase(TFlashRange& range, ULong blockAddress)
{
	ULong span = range.fBlockSize * (4 / range.fLaneCount);
	HostFlashErase(VirtualAddressToPointer((VAddr) blockAddress), span, range.fLanes);
	return noErr;
}


Boolean
THostFlashDriver::IsEraseComplete(TFlashRange&, ULong, long& result)
{
	result = noErr;
	return true;
}
