/*
	File:		stores/flash/FlashRange.cpp

	Contains:	TFlashRange and its 32-, 16- and 8-bit forms (Flash.h): a set
				of chips on some of a bank's byte lanes, read through one
				window of virtual space and written, a word of the bus at a
				time, through another by the chips' driver.

	Reconstructed from the MP2x00 US ROM (0x000c2690-0x000c3328); each
	function cites its origin.
*/

#include "Flash.h"
#include "MemoryAllocator.h"
#include "NewtonMemory.h"
#include "UserTasks.h"
#include "DelayTimer.h"
#include "hal/MMU.h"
#include "hal/System.h"
#include "host/RomBugs.h"

#include <new>


// DEVIATION: a window's bytes are where the MMU puts them (hal/MMU.h);
// on the machine the address is simply used.
static inline unsigned char*
WindowBytes(ULong virtualAddress)
{
	return (unsigned char*) VirtualAddressToPointer((VAddr) virtualAddress);
}


// ROM 0x000c2690 FetchReallyUnalignedWord__FPc
// The ROM loads the two aligned words either side and shifts them
// together, which is the big-endian word at p.  DEVIATION: the host reads
// the four bytes, rather than the aligned words round them, which may lie
// outside the caller's buffer.
ULong
FetchReallyUnalignedWord(char* p)
{
	const unsigned char* b = (const unsigned char*) p;
	return ((ULong) b[0] << 24) | ((ULong) b[1] << 16) | ((ULong) b[2] << 8) | (ULong) b[3];
}


// The word DoWrite takes from the caller's buffer: a load if it is aligned,
// FetchReallyUnalignedWord if not - four bytes either way, even where fewer
// are left to write, the rest being masked off.  DEVIATION: the host reads
// only the bytes left (`available`), the others as noughts, so as not to
// read past the buffer; what is written is the same.
static ULong
SourceWord(const char* p, ULong available)
{
	const unsigned char* b = (const unsigned char*) p;
	ULong word = 0;
	for (ULong i = 0; i < 4; i++)
		word = (word << 8) | (i < available ? b[i] : 0);
	return word;
}


/*------------------------------------------------------------------------------
	T F l a s h R a n g e
------------------------------------------------------------------------------*/

// ROM 0x000c26e0 __ct__11TFlashRangeFP12TFlashDriverUlN2211eMemoryLaneRC21SFlashChipInformationR16TMemoryAllocator
// How many lanes, and so how many chips (a lane each, or two for a 16-bit
// chip), how big and how big an erase unit: a block of every chip at once.
TFlashRange::TFlashRange(TFlashDriver* driver, ULong start, ULong readAddress, ULong writeAddress,
						 eMemoryLane lanes, const SFlashChipInformation& info, TMemoryAllocator& allocator)
{
	fDriver = driver;
	fStart = start;
	fReadAddress = readAddress;
	fWriteAddress = writeAddress;
	fLanes = lanes;
	fChipInfo = info;
	fEraseRemaining = 0;
	// (the ROM leaves these two as the allocator gave them; the driver sets
	// the first, and the second is read only while an erase is under way)
	fDriverData = nil;
	fEraseAddress = 0;

	ULong laneCount = 0;
	ULong laneMask = 0xFF;
	for (int i = 0; i < 4; i++)
	{
		if (laneMask & lanes)
			laneCount++;
		laneMask <<= 8;
	}
	fChipCount = laneCount / info.fWidth;
	fSize = fChipCount * info.fChipSize;
	fBlockSize = fChipCount * info.fBlockSize;
	fLaneCount = laneCount;
	fDriver->InitializeDriverData(*this, allocator);
}


// ROM 0x000c3114 __dt__11TFlashRangeFv
// (the ROM's frees the range itself when asked to: TNewInternalFlash gives
// the memory back to its allocator instead)
TFlashRange::~TFlashRange()
{
}


// ROM 0x000c312c Delete__11TFlashRangeFR16TMemoryAllocator
void
TFlashRange::Delete(TMemoryAllocator& allocator)
{
	fDriver->CleanUpDriverData(*this, allocator);
}


// ROM 0x000c29ec StartOfBlockFlashAddress__11TFlashRangeCFUl
ULong
TFlashRange::StartOfBlockFlashAddress(ULong flashAddress) const
{
	return fBlockSize * ((flashAddress - fStart) / fBlockSize) + fStart;
}


// ROM 0x000c2928 StartReadingArray__11TFlashRangeFv
// The chips put back into reading their contents, and the bus made the
// range's width.  A lane set the register has no setting for answers the
// register's error (see kError_Flash_Bad_Lanes: a positive number in
// the ROM: see (the ROM bug fixed in) hal/host/Flash.cpp).
NewtonErr
TFlashRange::StartReadingArray(void)
{
	TBankControlRegister* bank = TBankControlRegister::GetBankControlRegister();
	bank->ConfigureFlashBankDataSize(kAllLanes);
	fDriver->StartReadingArray(*this);
	if (fLanes == kAllLanes)
		return noErr;
	// (the ROM has ConfigureFlashBankDataSize inline here)
	return bank->ConfigureFlashBankDataSize(fLanes);
}


// ROM 0x000c296c DoneReadingArray__11TFlashRangeFv
void
TFlashRange::DoneReadingArray(void)
{
	TBankControlRegister::GetBankControlRegister()->ConfigureFlashBankDataSize(kAllLanes);
	fDriver->DoneReadingArray(*this);
}


// ROM 0x000c299c Read__11TFlashRangeFUlT1Pc
NewtonErr
TFlashRange::Read(ULong flashAddress, ULong size, char* buffer)
{
	ULong start = fStart;
	ULong window = fReadAddress;
	StartReadingArray();
	BlockMove(WindowBytes((flashAddress - start) + window), buffer, size);
	DoneReadingArray();
	return noErr;
}


// ROM 0x000c2874 Write__11TFlashRangeFUlT1Pc
// Cut at block boundaries, each piece written by the range's DoWrite.
NewtonErr
TFlashRange::Write(ULong flashAddress, ULong size, char* buffer)
{
	FlushDataCache((flashAddress - fStart) + fReadAddress, size);
	while (size != 0)
	{
		ULong piece = size;
		if (StartOfBlockFlashAddress(flashAddress) + fBlockSize < flashAddress + size)
			piece = fBlockSize - (flashAddress - StartOfBlockFlashAddress(flashAddress));
		NewtonErr err = DoWrite(flashAddress, piece, buffer);
		if (err != noErr)
			return err;
		flashAddress += piece;
		size -= piece;
		buffer += piece;
	}
	return noErr;
}


// ROM 0x000c2a14 IsVirgin__11TFlashRangeFUlT1
// Whether every byte is 0xFF: single bytes up to a word boundary, then
// words, then the bytes left.
// ROM BUG (fixed): the answer "no" comes back without DoneReadingArray, so
// the bus is left at the range's width (harmless on a 32-bit range, whose
// width it is anyway) and the driver is not told the read is over.  The
// fix calls DoneReadingArray before answering "no" as well.
Boolean
TFlashRange::IsVirgin(ULong flashAddress, ULong size)
{
	StartReadingArray();
	unsigned char* p = WindowBytes((flashAddress - fStart) + fReadAddress);
	while (((uintptr_t) p & 3) != 0 && size != 0)
	{
		if (*p != 0xFF)
		{
			if (RomBugFixed())
				DoneReadingArray();
			return false;
		}
		p++;
		size--;
	}
	ULong words = size & ~3;
	ULong bytes = size - words;
	for ( ; words != 0; words -= 4)
	{
		if (p[0] != 0xFF || p[1] != 0xFF || p[2] != 0xFF || p[3] != 0xFF)
		{
			if (RomBugFixed())
				DoneReadingArray();
			return false;
		}
		p += 4;
	}
	for ( ; bytes != 0; bytes--)
	{
		if (*p != 0xFF)
		{
			if (RomBugFixed())
				DoneReadingArray();
			return false;
		}
		p++;
	}
	DoneReadingArray();
	return true;
}


// ROM 0x000c2cd8 ResetAllBlocksStatus__11TFlashRangeFv
void
TFlashRange::ResetAllBlocksStatus(void)
{
	ULong start = fStart;
	ULong address = start;
	if (fSize + start <= start)
		return;
	do
	{
		fDriver->ResetBlockStatus(*this, PrepareForBlockCommand(address));
		address += fBlockSize;
	} while (address < fSize + start);
}


// ROM 0x000c3140 FlushDataCache__11TFlashRangeCFUlT1
// DEVIATION: the ROM cleans and invalidates the StrongARM's data cache over
// the range (a system call in user mode; on an ARM710 the whole of it), so
// that a read through the cached window sees what the chips now hold.  A
// host has no cache to keep in step.
void
TFlashRange::FlushDataCache(ULong, ULong) const
{
}


// ROM 0x000c3184 EraseRange__11TFlashRangeFv
NewtonErr
TFlashRange::EraseRange(void)
{
	ULong start = fStart;
	for (ULong address = start; address < fSize + start; address += fBlockSize)
	{
		NewtonErr err = SyncErase(address, fBlockSize);
		if (err != noErr)
			return err;
	}
	return noErr;
}


// ROM 0x000c31e4 SyncErase__11TFlashRangeFUlT1
// An erase started and waited for: a task sleeps between looks, an
// interrupt handler (or the boot) spins.
NewtonErr
TFlashRange::SyncErase(ULong flashAddress, ULong size)
{
	long result = StartErase(flashAddress, size);
	if (result == noErr)
	{
		while (!IsEraseComplete(result))
		{
			if (IsSuperMode())
				ShortTimerDelay(0xe66);
			else
				Sleep(0x47fe);
		}
	}
	return result;
}


// ROM 0x000c326c StartErase__11TFlashRangeFUlT1
// Each block's erase started; IsEraseComplete says when they are done.
NewtonErr
TFlashRange::StartErase(ULong flashAddress, ULong size)
{
	FlushDataCache((flashAddress - fStart) + fReadAddress, size);
	fEraseRemaining = size;
	fEraseAddress = flashAddress;
	while (size != 0)
	{
		NewtonErr err = fDriver->StartErase(*this, PrepareForBlockCommand(flashAddress));
		if (err != noErr)
		{
			fEraseRemaining = 0;
			return err;
		}
		size -= fBlockSize;
		flashAddress += fBlockSize;
	}
	return noErr;
}


// ROM 0x000c27d8 IsEraseComplete__11TFlashRangeFRl
// Asks each block of the erase under way; true once they are all done,
// with the first error any of them met.
Boolean
TFlashRange::IsEraseComplete(long& result)
{
	long firstError = noErr;
	ULong remaining = fEraseRemaining;
	ULong address = fEraseAddress;
	while (remaining != 0)
	{
		long error;
		if (!fDriver->IsEraseComplete(*this, PrepareForBlockCommand(address), error))
			return false;
		if (error != noErr && firstError == noErr)
			firstError = error;
		remaining -= fBlockSize;
		address += fBlockSize;
	}
	fEraseRemaining = 0;
	result = firstError;
	return true;
}


// ROM 0x000c32f8 LockBlock__11TFlashRangeFUl
NewtonErr
TFlashRange::LockBlock(ULong flashAddress)
{
	return fDriver->LockBlock(*this, PrepareForBlockCommand(flashAddress));
}


/*------------------------------------------------------------------------------
	T 3 2 B i t F l a s h R a n g e
------------------------------------------------------------------------------*/

// ROM 0x000c2d40 StartOfBlockWriteVirtualAddress__16T32BitFlashRangeCFUl
ULong
T32BitFlashRange::StartOfBlockWriteVirtualAddress(ULong address) const
{
	return fBlockSize * ((address - fWriteAddress) / fBlockSize) + fWriteAddress;
}


// ROM 0x000c2d6c AdjustVirtualAddresses__16T32BitFlashRangeFl
void
T32BitFlashRange::AdjustVirtualAddresses(long delta)
{
	fReadAddress += delta;
	fWriteAddress += delta;
}


// ROM 0x000c2ca4 PrepareForBlockCommand__16T32BitFlashRangeFUl
ULong
T32BitFlashRange::PrepareForBlockCommand(ULong flashAddress)
{
	TBankControlRegister::GetBankControlRegister()->ConfigureFlashBankDataSize(kAllLanes);
	return (flashAddress - fStart) + fWriteAddress;
}


// ROM 0x000c2abc DoWrite__16T32BitFlashRangeFUlT1Pc
// Whole words of the bus: a first word with the lanes before the start
// masked off (and those after the end, if it ends in the same word), the
// words in between, and a last word with the lanes after the end masked
// off.  The driver is told how many words are coming first.
NewtonErr
T32BitFlashRange::DoWrite(ULong flashAddress, ULong size, char* buffer)
{
	TBankControlRegister::GetBankControlRegister()->ConfigureFlashBankDataSize(kAllLanes);
	ULong address = (flashAddress - fStart) + fWriteAddress;
	ULong offset = address & 3;
	ULong head = 4 - offset;
	ULong wordCount = (head != 0);
	if (head < size)
	{
		wordCount += (size - head) >> 2;
		if (((size - head) & 3) != 0)
			wordCount++;
	}
	ULong word = address & ~3;
	fDriver->BeginWrite(*this, word, wordCount);
	if (offset != 0)
	{
		ULong value = SourceWord(buffer, size) >> (offset << 3);
		ULong mask = 0;
		if (offset == 1)
			mask = 0x00FFFFFF;
		else if (offset == 2)
			mask = 0x0000FFFF;
		else if (offset == 3)
			mask = 0x000000FF;
		if (size < head)
		{
			if (size == 1 && head != 2)
			{
				mask &= 0xFFFF0000;
				value &= 0xFFFF0000;
			}
			else
			{
				mask &= 0xFFFFFF00;
				value &= 0xFFFFFF00;
			}
			head = size;
		}
		fDriver->Write(value, mask, word, *this);
		size -= head;
		word += 4;
		buffer += head;
	}
	ULong words = size & ~3;
	ULong tail = size - words;
	for ( ; words != 0; words -= 4)
	{
		fDriver->Write(SourceWord(buffer, 4), 0xFFFFFFFF, word, *this);
		word += 4;
		buffer += 4;
	}
	if (tail != 0)
	{
		ULong mask = 0xFFFFFFFF << ((4 - tail) * 8);
		fDriver->Write(SourceWord(buffer, tail) & mask, mask, word, *this);
	}
	return fDriver->ReportWriteResult(*this, StartOfBlockWriteVirtualAddress(address));
}


/*------------------------------------------------------------------------------
	T 1 6 B i t F l a s h R a n g e
------------------------------------------------------------------------------*/

// ROM 0x000c2f58 StartOfBlockWriteVirtualAddress__16T16BitFlashRangeCFUl
ULong
T16BitFlashRange::StartOfBlockWriteVirtualAddress(ULong address) const
{
	return fWriteAddress + ((address - fWriteAddress) / (fBlockSize << 1)) * fBlockSize * 2;
}


// ROM 0x000c2f8c AdjustVirtualAddresses__16T16BitFlashRangeFl
void
T16BitFlashRange::AdjustVirtualAddresses(long delta)
{
	fReadAddress += delta;
	fWriteAddress += delta * 2;
}


// ROM 0x000c2f24 PrepareForBlockCommand__16T16BitFlashRangeFUl
ULong
T16BitFlashRange::PrepareForBlockCommand(ULong flashAddress)
{
	TBankControlRegister::GetBankControlRegister()->ConfigureFlashBankDataSize(kAllLanes);
	return fWriteAddress + (flashAddress - fStart) * 2;
}


// ROM 0x000c2d88 DoWrite__16T16BitFlashRangeFUlT1Pc
// Two bytes to each word of the bus, in the range's half of it: an odd
// first byte alone in its halfword's low byte, then halfwords, then a last
// byte alone in its halfword's high byte.
NewtonErr
T16BitFlashRange::DoWrite(ULong flashAddress, ULong size, char* buffer)
{
	TBankControlRegister::GetBankControlRegister()->ConfigureFlashBankDataSize(kAllLanes);
	ULong address = (((flashAddress - fStart) * 2) & ~3) + fWriteAddress;
	ULong odd = flashAddress & 1;
	ULong rest = size;
	if (odd != 0)
		rest = size - 1;
	fDriver->BeginWrite(*this, address, (rest & 1) + (odd != 0) + (rest >> 1));
	ULong lanes = fLanes;
	int shift = (lanes == kLowHalfLanes) ? 0 : 16;
	ULong word = address;
	if (odd != 0)
	{
		fDriver->Write(((ULong) 0xFF << shift) & ((ULong) (unsigned char) buffer[0] << shift), (ULong) 0xFF << shift, address, *this);
		size--;
		word = address + 4;
		buffer++;
	}
	for (ULong halves = size & ~1; halves != 0; halves -= 2)
	{
		ULong half = ((ULong) (unsigned char) buffer[0] << 8) | (unsigned char) buffer[1];
		fDriver->Write((half & 0xFFFF) << shift, lanes, word, *this);
		word += 4;
		buffer += 2;
	}
	if (size != (size & ~1))
	{
		ULong mask = (ULong) 0xFF << (shift + 8);
		fDriver->Write(mask & ((ULong) (unsigned char) buffer[0] << (shift + 8)), mask, word, *this);
	}
	return fDriver->ReportWriteResult(*this, StartOfBlockWriteVirtualAddress(address));
}


/*------------------------------------------------------------------------------
	T 8 B i t F l a s h R a n g e
------------------------------------------------------------------------------*/

// ROM 0x000c2fdc StartOfBlockWriteVirtualAddress__15T8BitFlashRangeCFUl
ULong
T8BitFlashRange::StartOfBlockWriteVirtualAddress(ULong address) const
{
	return fWriteAddress + ((address - fWriteAddress) / (fBlockSize << 2)) * fBlockSize * 4;
}


// ROM 0x000c3010 AdjustVirtualAddresses__15T8BitFlashRangeFl
void
T8BitFlashRange::AdjustVirtualAddresses(long delta)
{
	fReadAddress += delta;
	fWriteAddress += delta * 4;
}


// ROM 0x000c2fa8 PrepareForBlockCommand__15T8BitFlashRangeFUl
ULong
T8BitFlashRange::PrepareForBlockCommand(ULong flashAddress)
{
	TBankControlRegister::GetBankControlRegister()->ConfigureFlashBankDataSize(kAllLanes);
	return fWriteAddress + (flashAddress - fStart) * 4;
}


// ROM 0x000c302c DoWrite__15T8BitFlashRangeFUlT1Pc
// One byte to each word of the bus, in the range's lane.
NewtonErr
T8BitFlashRange::DoWrite(ULong flashAddress, ULong size, char* buffer)
{
	TBankControlRegister::GetBankControlRegister()->ConfigureFlashBankDataSize(kAllLanes);
	ULong address = (flashAddress - fStart) * 4 + fWriteAddress;
	fDriver->BeginWrite(*this, address, size);
	ULong lanes = fLanes;
	int shift;
	if (lanes == 0xFF)
		shift = 0;
	else if (lanes == 0xFF00)
		shift = 8;
	else if (lanes == 0xFF0000)
		shift = 16;
	else
		shift = 24;
	ULong word = address;
	for ( ; size != 0; size--)
	{
		fDriver->Write(lanes & ((ULong) (unsigned char) *buffer << shift), lanes, word, *this);
		buffer++;
		word += 4;
	}
	return fDriver->ReportWriteResult(*this, StartOfBlockWriteVirtualAddress(address));
}
