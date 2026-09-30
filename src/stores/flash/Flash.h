/*
	File:		stores/flash/Flash.h

	Contains:	The flash devices the flash store keeps its data on.

				TFlash is the protocol a flash device is reached through -
				the internal flash (TNewInternalFlash) or a card's chips
				(TFlashSeries2, TFlashAMD: NOT YET) - addressed in bytes from
				0 to GetTotalSize, erased GetEraseRegionSize bytes at a time.

				Under the internal flash are TFlashRanges: each a set of
				identical chips on some of the bank's byte lanes (all four:
				a T32BitFlashRange; a half: T16Bit; one byte: T8Bit), with
				the flash addresses it answers for, a window of virtual
				space it is read through and one it is written through.  A
				range does what the chips need through a TFlashDriver, the
				protocol a chip family's commands live behind (the ROM's is
				T28F016_SA_SVDriver, for Intel's and Sharp's 28F016 parts;
				more come from the ROM extension's 'fdrv config entries).

				The internal flash's first erase region - both chips' first
				blocks - is the reserved block (the calibration data, the
				patches: TReservedBlockAccessor, NOT YET), kept out of the
				flash addresses.  The rest is divided into erase regions of
				which one is always a spare: a region starts with four bytes
				saying which logical region it holds (two bytes, big-endian)
				and 0x00FF, and erasing a logical region means copying
				nothing and remapping - its physical region is marked 0x000F
				("being erased"), the spare marked with the logical number
				and taken into use, and the old region wiped and made the
				spare.  So what the store above sees is GetTotalSize bytes of
				logical regions whose first four bytes belong to the flash.

	Not in the DDK.  The protocols' method order is the ROM's dispatch
	tables (classinfo.py --name TNewInternalFlash, --name
	T28F016_SA_SVDriver); TFlashRange's virtuals are in its vtable's order
	(vtable.py build/MP2x00US 0x0001e3d4).  Implementations at
	0x000c2690-0x000c3328 (the ranges) and 0x0013afa0-0x0013cfc0 (the
	internal flash).
*/

#ifndef __FLASH_H
#define __FLASH_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

#ifndef __OSERRORS_H
#include "OSErrors.h"
#endif

#ifndef __HAL_FLASH_H
#include "hal/Flash.h"
#endif

#ifndef __KERNELTYPES_H
#include "KernelTypes.h"
#endif

class TMemoryAllocator;
class TFlashRange;
class TCardSocket;
class TCardPCMCIA;
class TULockingSemaphore;


/*------------------------------------------------------------------------------
	S F l a s h C h i p I n f o r m a t i o n
	What a driver's Identify says about the chips at an address.  The field
	names are ours; what each holds is how the ROM's code uses it.
------------------------------------------------------------------------------*/

struct SFlashChipInformation
{
	ULong		fManufacturer;		// +0x00  0x89 Intel, 0xB0 Sharp
	ULong		fDevice;			// +0x04  0x66A0 28F016SA, 0x6688 28F016SV, 0x66A8 (1 MB)
	ULong		fVppKind;			// +0x08  2 Intel, 1 Sharp; not nought: the chips need Vpp switched on
	ULong		fWidth;				// +0x0c  lanes a chip takes: 1 (x8) or 2 (x16)
	ULong		fChipSize;			// +0x10  bytes in one chip
	ULong		fBlockSize;			// +0x14  bytes in one of a chip's erase blocks
};

Boolean		operator==(const SFlashChipInformation& a, const SFlashChipInformation& b);	// ROM 0x0013bfe8 __eq__FRC21SFlashChipInformationT1


/*------------------------------------------------------------------------------
	T F l a s h D r i v e r
	A family of chips' commands.  Addresses are the virtual addresses the
	range computes: the start of a word in the write window, or of a block
	(PrepareForBlockCommand).
------------------------------------------------------------------------------*/

PROTOCOL TFlashDriver : public TProtocol
{
public:
	VIRTUAL Boolean		Identify(ULong address, eMemoryLane lanes, SFlashChipInformation& info) ENDVIRTUAL;		// ROM 0x0038473c Identify__12TFlashDriverFUlT1R21SFlashChipInformation
	VIRTUAL void		CleanUp(TMemoryAllocator& allocator) ENDVIRTUAL;										// ROM 0x00384748 CleanUp__12TFlashDriverFR16TMemoryAllocator
	VIRTUAL NewtonErr	Init(TMemoryAllocator& allocator) ENDVIRTUAL;											// ROM 0x00384754 Init__12TFlashDriverFR16TMemoryAllocator
	VIRTUAL NewtonErr	InitializeDriverData(TFlashRange& range, TMemoryAllocator& allocator) ENDVIRTUAL;		// ROM 0x00384760 InitializeDriverData__12TFlashDriverFR11TFlashRangeR16TMemoryAllocator
	VIRTUAL void		CleanUpDriverData(TFlashRange& range, TMemoryAllocator& allocator) ENDVIRTUAL;		// ROM 0x0038476c CleanUpDriverData__12TFlashDriverFR11TFlashRangeR16TMemoryAllocator
	VIRTUAL void		StartReadingArray(TFlashRange& range) ENDVIRTUAL;										// ROM 0x00384778 StartReadingArray__12TFlashDriverFR11TFlashRange
	VIRTUAL void		DoneReadingArray(TFlashRange& range) ENDVIRTUAL;										// ROM 0x00384784 DoneReadingArray__12TFlashDriverFR11TFlashRange
	VIRTUAL void		Write(ULong value, ULong mask, ULong address, TFlashRange& range) ENDVIRTUAL;			// ROM 0x00384790 Write__12TFlashDriverFUlN21R11TFlashRange
	VIRTUAL NewtonErr	StartErase(TFlashRange& range, ULong blockAddress) ENDVIRTUAL;						// ROM 0x0038479c StartErase__12TFlashDriverFR11TFlashRangeUl
	VIRTUAL void		ResetBlockStatus(TFlashRange& range, ULong blockAddress) ENDVIRTUAL;					// ROM 0x003847a8 ResetBlockStatus__12TFlashDriverFR11TFlashRangeUl
	VIRTUAL Boolean		IsEraseComplete(TFlashRange& range, ULong blockAddress, long& result) ENDVIRTUAL;		// ROM 0x003847b4 IsEraseComplete__12TFlashDriverFR11TFlashRangeUlRl
	VIRTUAL NewtonErr	LockBlock(TFlashRange& range, ULong blockAddress) ENDVIRTUAL;							// ROM 0x003847c0 LockBlock__12TFlashDriverFR11TFlashRangeUl
	VIRTUAL void		BeginWrite(TFlashRange& range, ULong address, ULong wordCount) ENDVIRTUAL;			// ROM 0x003847cc BeginWrite__12TFlashDriverFR11TFlashRangeUlT2
	VIRTUAL NewtonErr	ReportWriteResult(TFlashRange& range, ULong blockAddress) ENDVIRTUAL;					// ROM 0x003847d8 ReportWriteResult__12TFlashDriverFR11TFlashRangeUl
};

// The driver the internal flash falls back on when the ROM extension
// names none: the ROM's T28F016_SA_SVDriver.  DEVIATION: a port supplies
// its own, which answers for whatever chips it has (the host's:
// stores/flash/host/HostFlashDriver.cpp).
const TClassInfo*	DefaultFlashDriverClassInfo(void);


/*------------------------------------------------------------------------------
	T F l a s h R a n g e
	0x4C bytes.
------------------------------------------------------------------------------*/

class TFlashRange
{
public:
					TFlashRange(TFlashDriver* driver, ULong start, ULong readAddress, ULong writeAddress,
								eMemoryLane lanes, const SFlashChipInformation& info, TMemoryAllocator& allocator);		// ROM 0x000c26e0 __ct__11TFlashRangeFP12TFlashDriverUlN2211eMemoryLaneRC21SFlashChipInformationR16TMemoryAllocator

	virtual void	Delete(TMemoryAllocator& allocator);						// ROM 0x000c312c Delete__11TFlashRangeFR16TMemoryAllocator
	virtual			~TFlashRange();												// ROM 0x000c3114 __dt__11TFlashRangeFv
	virtual ULong	StartOfBlockWriteVirtualAddress(ULong address) const = 0;	// a write window address
	virtual void	AdjustVirtualAddresses(long delta) = 0;
	virtual NewtonErr	DoWrite(ULong flashAddress, ULong size, char* buffer) = 0;	// within one block
	virtual ULong	PrepareForBlockCommand(ULong flashAddress) = 0;				// the block's write window address

	NewtonErr		Read(ULong flashAddress, ULong size, char* buffer);		// ROM 0x000c299c Read__11TFlashRangeFUlT1Pc
	NewtonErr		Write(ULong flashAddress, ULong size, char* buffer);		// ROM 0x000c2874 Write__11TFlashRangeFUlT1Pc
	Boolean			IsVirgin(ULong flashAddress, ULong size);					// ROM 0x000c2a14 IsVirgin__11TFlashRangeFUlT1
	NewtonErr		StartReadingArray(void);									// ROM 0x000c2928 StartReadingArray__11TFlashRangeFv
	void			DoneReadingArray(void);										// ROM 0x000c296c DoneReadingArray__11TFlashRangeFv
	ULong			StartOfBlockFlashAddress(ULong flashAddress) const;			// ROM 0x000c29ec StartOfBlockFlashAddress__11TFlashRangeCFUl
	void			ResetAllBlocksStatus(void);									// ROM 0x000c2cd8 ResetAllBlocksStatus__11TFlashRangeFv
	void			FlushDataCache(ULong address, ULong size) const;			// ROM 0x000c3140 FlushDataCache__11TFlashRangeCFUlT1
	NewtonErr		EraseRange(void);											// ROM 0x000c3184 EraseRange__11TFlashRangeFv
	NewtonErr		SyncErase(ULong flashAddress, ULong size);					// ROM 0x000c31e4 SyncErase__11TFlashRangeFUlT1
	NewtonErr		StartErase(ULong flashAddress, ULong size);					// ROM 0x000c326c StartErase__11TFlashRangeFUlT1
	Boolean			IsEraseComplete(long& result);								// ROM 0x000c27d8 IsEraseComplete__11TFlashRangeFRl
	NewtonErr		LockBlock(ULong flashAddress);								// ROM 0x000c32f8 LockBlock__11TFlashRangeFUl

	TFlashDriver*	fDriver;			// +0x04
	ULong			fStart;				// +0x08  the first flash address it answers for
	ULong			fReadAddress;		// +0x0c  where fStart is in the read window
	ULong			fWriteAddress;		// +0x10  where fStart is in the write window
	eMemoryLane		fLanes;				// +0x14
	SFlashChipInformation	fChipInfo;	// +0x18
	ULong			fSize;				// +0x30  bytes: chips x chip size
	ULong			fChipCount;			// +0x34  lanes / a chip's width
	ULong			fLaneCount;			// +0x38
	ULong			fBlockSize;			// +0x3c  chips x a chip's block: the range's erase unit
	void*			fDriverData;		// +0x40  the driver's own (InitializeDriverData)
	ULong			fEraseAddress;		// +0x44  the erase under way
	ULong			fEraseRemaining;	// +0x48  bytes of it; 0 when none
};


// All four lanes: flash addresses are the write window's bytes one for one.
class T32BitFlashRange : public TFlashRange
{
public:
					T32BitFlashRange(TFlashDriver* driver, ULong start, ULong readAddress, ULong writeAddress,
								eMemoryLane lanes, const SFlashChipInformation& info, TMemoryAllocator& allocator)
						: TFlashRange(driver, start, readAddress, writeAddress, lanes, info, allocator) { }

	ULong			StartOfBlockWriteVirtualAddress(ULong address) const;		// ROM 0x000c2d40 StartOfBlockWriteVirtualAddress__16T32BitFlashRangeCFUl
	void			AdjustVirtualAddresses(long delta);							// ROM 0x000c2d6c AdjustVirtualAddresses__16T32BitFlashRangeFl
	NewtonErr		DoWrite(ULong flashAddress, ULong size, char* buffer);		// ROM 0x000c2abc DoWrite__16T32BitFlashRangeFUlT1Pc
	ULong			PrepareForBlockCommand(ULong flashAddress);					// ROM 0x000c2ca4 PrepareForBlockCommand__16T32BitFlashRangeFUl
};

// Two lanes: each word of the write window carries two bytes.
class T16BitFlashRange : public TFlashRange
{
public:
					T16BitFlashRange(TFlashDriver* driver, ULong start, ULong readAddress, ULong writeAddress,
								eMemoryLane lanes, const SFlashChipInformation& info, TMemoryAllocator& allocator)
						: TFlashRange(driver, start, readAddress, writeAddress, lanes, info, allocator) { }

	ULong			StartOfBlockWriteVirtualAddress(ULong address) const;		// ROM 0x000c2f58 StartOfBlockWriteVirtualAddress__16T16BitFlashRangeCFUl
	void			AdjustVirtualAddresses(long delta);							// ROM 0x000c2f8c AdjustVirtualAddresses__16T16BitFlashRangeFl
	NewtonErr		DoWrite(ULong flashAddress, ULong size, char* buffer);		// ROM 0x000c2d88 DoWrite__16T16BitFlashRangeFUlT1Pc
	ULong			PrepareForBlockCommand(ULong flashAddress);					// ROM 0x000c2f24 PrepareForBlockCommand__16T16BitFlashRangeFUl
};

// One lane: each word of the write window carries one byte.
class T8BitFlashRange : public TFlashRange
{
public:
					T8BitFlashRange(TFlashDriver* driver, ULong start, ULong readAddress, ULong writeAddress,
								eMemoryLane lanes, const SFlashChipInformation& info, TMemoryAllocator& allocator)
						: TFlashRange(driver, start, readAddress, writeAddress, lanes, info, allocator) { }

	ULong			StartOfBlockWriteVirtualAddress(ULong address) const;		// ROM 0x000c2fdc StartOfBlockWriteVirtualAddress__15T8BitFlashRangeCFUl
	void			AdjustVirtualAddresses(long delta);							// ROM 0x000c3010 AdjustVirtualAddresses__15T8BitFlashRangeFl
	NewtonErr		DoWrite(ULong flashAddress, ULong size, char* buffer);		// ROM 0x000c302c DoWrite__15T8BitFlashRangeFUlT1Pc
	ULong			PrepareForBlockCommand(ULong flashAddress);					// ROM 0x000c2fa8 PrepareForBlockCommand__15T8BitFlashRangeFUl
};

// a big-endian word from any address (the ARM faults an unaligned load)
ULong	FetchReallyUnalignedWord(char* p);										// ROM 0x000c2690 FetchReallyUnalignedWord__FPc


/*------------------------------------------------------------------------------
	T F l a s h
------------------------------------------------------------------------------*/

// GetAttributes' bits
enum
{
	kFlashAttrNeedsVpp		= 0x08,		// what the internal flash always answers
	kFlashAttrNoVpp			= 0x10		// set with it when the chips need no programming voltage
};

PROTOCOL TFlash : public TProtocol
{
public:
	static TFlash*	New(const char* implementation);		// ROM 0x00386474 New__6TFlashSFPc
	void			Delete(void);							// ROM 0x003864a0 Delete__6TFlashFv

	VIRTUAL NewtonErr	Read(ULong address, ULong size, char* buffer) ENDVIRTUAL;			// ROM 0x003864bc Read__6TFlashFUlT1Pc
	VIRTUAL NewtonErr	Write(ULong address, ULong size, char* buffer) ENDVIRTUAL;			// ROM 0x003864c8 Write__6TFlashFUlT1Pc
	VIRTUAL NewtonErr	Erase(ULong address) ENDVIRTUAL;										// ROM 0x003864d4 Erase__6TFlashFUl
	VIRTUAL NewtonErr	SuspendErase(ULong, ULong, ULong) ENDVIRTUAL;						// ROM 0x003864e0 SuspendErase__6TFlashFUlN21
	VIRTUAL NewtonErr	ResumeErase(ULong) ENDVIRTUAL;										// ROM 0x003864ec ResumeErase__6TFlashFUl
	VIRTUAL NewtonErr	DeepSleep(ULong) ENDVIRTUAL;											// ROM 0x003864f8 DeepSleep__6TFlashFUl
	VIRTUAL NewtonErr	Wakeup(ULong) ENDVIRTUAL;											// ROM 0x00386504 Wakeup__6TFlashFUl
	VIRTUAL NewtonErr	Status(ULong) ENDVIRTUAL;											// ROM 0x00386510 Status__6TFlashFUl
	VIRTUAL NewtonErr	ResetCard(void) ENDVIRTUAL;											// ROM 0x0038651c ResetCard__6TFlashFv
	VIRTUAL void		AcknowledgeReset(void) ENDVIRTUAL;									// ROM 0x00386528 AcknowledgeReset__6TFlashFv
	VIRTUAL ULong		GetPhysResource(void) ENDVIRTUAL;									// ROM 0x00386534 GetPhysResource__6TFlashFv
	VIRTUAL void		RegisterClientInfo(ULong) ENDVIRTUAL;								// ROM 0x00386540 RegisterClientInfo__6TFlashFUl
	VIRTUAL void		GetWriteProtected(UChar* isProtected) ENDVIRTUAL;					// ROM 0x0038654c GetWriteProtected__6TFlashFPUc
	VIRTUAL ULong		GetWriteErrorAddress(void) ENDVIRTUAL;								// ROM 0x00386558 GetWriteErrorAddress__6TFlashFv
	VIRTUAL ULong		GetAttributes(void) ENDVIRTUAL;										// ROM 0x00386564 GetAttributes__6TFlashFv
	VIRTUAL ULong		GetDataOffset(void) ENDVIRTUAL;										// ROM 0x00386570 GetDataOffset__6TFlashFv
	VIRTUAL ULong		GetTotalSize(void) ENDVIRTUAL;										// ROM 0x0038657c GetTotalSize__6TFlashFv
	VIRTUAL ULong		GetGroupSize(void) ENDVIRTUAL;										// ROM 0x00386588 GetGroupSize__6TFlashFv
	VIRTUAL ULong		GetEraseRegionSize(void) ENDVIRTUAL;									// ROM 0x00386594 GetEraseRegionSize__6TFlashFv
	VIRTUAL ULong		GetChipsPerGroup(void) ENDVIRTUAL;									// ROM 0x003865a0 GetChipsPerGroup__6TFlashFv
	VIRTUAL ULong		GetBlocksPerPartition(void) ENDVIRTUAL;								// ROM 0x003865ac GetBlocksPerPartition__6TFlashFv
	VIRTUAL ULong		GetMaxConcurrentVppOps(void) ENDVIRTUAL;								// ROM 0x003865b8 GetMaxConcurrentVppOps__6TFlashFv
	VIRTUAL ULong		GetEraseRegionCurrent(void) ENDVIRTUAL;								// ROM 0x003865c4 GetEraseRegionCurrent__6TFlashFv
	VIRTUAL ULong		GetWriteRegionCurrent(void) ENDVIRTUAL;								// ROM 0x003865d0 GetWriteRegionCurrent__6TFlashFv
	VIRTUAL ULong		GetEraseRegionTime(void) ENDVIRTUAL;									// ROM 0x003865dc GetEraseRegionTime__6TFlashFv
	VIRTUAL ULong		GetWriteAccessTime(void) ENDVIRTUAL;									// ROM 0x003865e8 GetWriteAccessTime__6TFlashFv
	VIRTUAL ULong		GetReadAccessTime(void) ENDVIRTUAL;									// ROM 0x003865f4 GetReadAccessTime__6TFlashFv
	VIRTUAL ULong		GetVendorInfo(void) ENDVIRTUAL;										// ROM 0x00386600 GetVendorInfo__6TFlashFv
	VIRTUAL ULong		GetSocketNumber(void) ENDVIRTUAL;									// ROM 0x0038660c GetSocketNumber__6TFlashFv
	VIRTUAL ULong		VppStatus(void) ENDVIRTUAL;											// ROM 0x00386618 VppStatus__6TFlashFv
	VIRTUAL ULong		VppRisingTime(void) ENDVIRTUAL;										// ROM 0x00386624 VppRisingTime__6TFlashFv
	VIRTUAL NewtonErr	FlashSpecific(ULong selector, void* data, ULong size) ENDVIRTUAL;	// ROM 0x00386630 FlashSpecific__6TFlashFUlPvT1
	VIRTUAL NewtonErr	Initialize(TCardSocket* socket, TCardPCMCIA* card, ULong, ULong) ENDVIRTUAL;	// ROM 0x0038663c Initialize__6TFlashFP11TCardSocketP11TCardPCMCIAUlT3
	VIRTUAL NewtonErr	SuspendService(void) ENDVIRTUAL;										// ROM 0x00386648 SuspendService__6TFlashFv
	VIRTUAL NewtonErr	ResumeService(TCardSocket* socket, TCardPCMCIA* card, ULong) ENDVIRTUAL;	// ROM 0x00386654 ResumeService__6TFlashFP11TCardSocketP11TCardPCMCIAUl
	VIRTUAL NewtonErr	Copy(ULong from, ULong to, ULong size) ENDVIRTUAL;					// ROM 0x00386660 Copy__6TFlashFUlN21
	VIRTUAL Boolean		IsVirgin(ULong address, ULong size) ENDVIRTUAL;						// ROM 0x0038666c IsVirgin__6TFlashFUlT1
};


/*------------------------------------------------------------------------------
	T N e w I n t e r n a l F l a s h
	The internal flash: up to three ranges (a 32-bit bank, or two halves of
	one; and the I/O-space bank at 0x10000000), found by asking each driver
	in turn what is at the bank's write window.  0x6C bytes.
------------------------------------------------------------------------------*/

// the windows the ranges are read and written through: the read windows
// from 0x30000000 up, the write windows from 0x34000000 up
// (InternalInit 0x0013b484/0x0013b48c), so the read windows of all the
// ranges together may take 64 MB before they reach the first write window
const ULong	kInternalFlashReadWindow	= 0x30000000;
const ULong	kInternalFlashWriteWindow	= 0x34000000;

// DEVIATION: where the write windows start, which the ROM has as the
// constant above.  A port with more than 64 MB of internal flash (the host's
// can have 128 MB: docs/stores/README.md, "Bigger flash") moves them up to
// 0x38000000, out of the read windows' way - nothing else is there, the
// next thing mapped being the ROM domain at 0x60000000.  With 64 MB or less
// it answers the ROM's 0x34000000.  (The port's: HostFlashDriver.cpp.)
ULong		InternalFlashWriteWindow(void);
const PAddr	kInternalFlashBank			= 0x02000000;	// the flash bank's physical address
const PAddr	kInternalFlashIOBank		= 0x10000000;	// the second bank's, in I/O space

PROTOCOL TNewInternalFlash : public TFlash
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TNewInternalFlash);

	enum eInitHWOption		{ kDontMapWindows = 0, kMapWindows = 1 };
	enum eCheckEraseOption	{ kDontWaitForErase = 0, kWaitForErase = 1 };

	TNewInternalFlash*	New(void);							// ROM 0x0013bf34 New__17TNewInternalFlashFv
	void			Delete(void);							// ROM 0x0013c50c Delete__17TNewInternalFlashFv

	NewtonErr		Read(ULong address, ULong size, char* buffer);			// ROM 0x0013bc40 Read__17TNewInternalFlashFUlT1Pc
	NewtonErr		Write(ULong address, ULong size, char* buffer);			// ROM 0x0013bca8 Write__17TNewInternalFlashFUlT1Pc
	NewtonErr		Erase(ULong address);									// ROM 0x0013c10c Erase__17TNewInternalFlashFUl
	NewtonErr		SuspendErase(ULong, ULong, ULong);						// ROM 0x0013c998 SuspendErase__17TNewInternalFlashFUlN21
	NewtonErr		ResumeErase(ULong);										// ROM 0x0013c9a0 ResumeErase__17TNewInternalFlashFUl
	NewtonErr		DeepSleep(ULong);										// ROM 0x0013c9a8 DeepSleep__17TNewInternalFlashFUl
	NewtonErr		Wakeup(ULong);											// ROM 0x0013c9b0 Wakeup__17TNewInternalFlashFUl
	NewtonErr		Status(ULong);											// ROM 0x0013bf68 Status__17TNewInternalFlashFUl
	NewtonErr		ResetCard(void);										// ROM 0x0013c9b8 ResetCard__17TNewInternalFlashFv
	void			AcknowledgeReset(void);									// ROM 0x0013c994 AcknowledgeReset__17TNewInternalFlashFv
	ULong			GetPhysResource(void);									// ROM 0x0013c9c0 GetPhysResource__17TNewInternalFlashFv
	void			RegisterClientInfo(ULong);								// ROM 0x0013caa4 RegisterClientInfo__17TNewInternalFlashFUl
	void			GetWriteProtected(UChar* isProtected);					// ROM 0x0013bf70 GetWriteProtected__17TNewInternalFlashFPUc
	ULong			GetWriteErrorAddress(void);								// ROM 0x0013caa8 GetWriteErrorAddress__17TNewInternalFlashFv
	ULong			GetAttributes(void);									// ROM 0x0013bf38 GetAttributes__17TNewInternalFlashFv
	ULong			GetDataOffset(void);									// ROM 0x0013cab0 GetDataOffset__17TNewInternalFlashFv
	ULong			GetTotalSize(void);										// ROM 0x0013bf58 GetTotalSize__17TNewInternalFlashFv
	ULong			GetGroupSize(void);										// ROM 0x0013cab8 GetGroupSize__17TNewInternalFlashFv
	ULong			GetEraseRegionSize(void);								// ROM 0x0013bf50 GetEraseRegionSize__17TNewInternalFlashFv
	ULong			GetChipsPerGroup(void);									// ROM 0x0013cac0 GetChipsPerGroup__17TNewInternalFlashFv
	ULong			GetBlocksPerPartition(void);							// ROM 0x0013cac8 GetBlocksPerPartition__17TNewInternalFlashFv
	ULong			GetMaxConcurrentVppOps(void);							// ROM 0x0013cad0 GetMaxConcurrentVppOps__17TNewInternalFlashFv
	ULong			GetEraseRegionCurrent(void);							// ROM 0x0013cad8 GetEraseRegionCurrent__17TNewInternalFlashFv
	ULong			GetWriteRegionCurrent(void);							// ROM 0x0013cae0 GetWriteRegionCurrent__17TNewInternalFlashFv
	ULong			GetEraseRegionTime(void);								// ROM 0x0013c98c GetEraseRegionTime__17TNewInternalFlashFv
	ULong			GetWriteAccessTime(void);								// ROM 0x0013cae8 GetWriteAccessTime__17TNewInternalFlashFv
	ULong			GetReadAccessTime(void);								// ROM 0x0013cbfc GetReadAccessTime__17TNewInternalFlashFv
	ULong			GetVendorInfo(void);									// ROM 0x0013cc04 GetVendorInfo__17TNewInternalFlashFv
	ULong			GetSocketNumber(void);									// ROM 0x0013cc0c GetSocketNumber__17TNewInternalFlashFv
	ULong			VppStatus(void);										// ROM 0x0013cc14 VppStatus__17TNewInternalFlashFv
	ULong			VppRisingTime(void);									// ROM 0x0013cc1c VppRisingTime__17TNewInternalFlashFv
	NewtonErr		FlashSpecific(ULong selector, void* data, ULong size);	// ROM 0x0013cc24 FlashSpecific__17TNewInternalFlashFUlPvT1
	NewtonErr		Initialize(TCardSocket* socket, TCardPCMCIA* card, ULong, ULong);	// ROM 0x0013cc2c Initialize__17TNewInternalFlashFP11TCardSocketP11TCardPCMCIAUlT3
	NewtonErr		SuspendService(void);									// ROM 0x0013cc34 SuspendService__17TNewInternalFlashFv
	NewtonErr		ResumeService(TCardSocket* socket, TCardPCMCIA* card, ULong);	// ROM 0x0013cc3c ResumeService__17TNewInternalFlashFP11TCardSocketP11TCardPCMCIAUl
	NewtonErr		Copy(ULong from, ULong to, ULong size);					// ROM 0x0013c378 Copy__17TNewInternalFlashFUlN21
	Boolean			IsVirgin(ULong address, ULong size);					// ROM 0x0013c3ac IsVirgin__17TNewInternalFlashFUlT1

	// the rest
	NewtonErr		Init(TMemoryAllocator* allocator);															// ROM 0x0013b5bc Init__17TNewInternalFlashFP16TMemoryAllocator
	NewtonErr		InitForReservedBlock(TMemoryAllocator* allocator, eInitHWOption option);					// ROM 0x0013b5b8 InitForReservedBlock__17TNewInternalFlashFP16TMemoryAllocatorQ217TNewInternalFlash13eInitHWOption
	NewtonErr		InternalInit(TMemoryAllocator* allocator, eInitHWOption option);							// ROM 0x0013b444 InternalInit__17TNewInternalFlashFP16TMemoryAllocatorQ217TNewInternalFlash13eInitHWOption
	NewtonErr		InitializeState(TMemoryAllocator* allocator, eInitHWOption option);						// ROM 0x0013c8d0 InitializeState__17TNewInternalFlashFP16TMemoryAllocatorQ217TNewInternalFlash13eInitHWOption
	NewtonErr		SearchForFlashDrivers(void);																// ROM 0x0013b908 SearchForFlashDrivers__17TNewInternalFlashFv
	void			CleanUp(void);																				// ROM 0x0013b7f0 CleanUp__17TNewInternalFlashFv
	NewtonErr		FlashAllowedLocations(UChar& flashBank, UChar& ioBank);									// ROM 0x0013b714 FlashAllowedLocations__17TNewInternalFlashFRUcT1
	void			AvoidConflictWithRexInIOSpace(UChar& ioBank);												// ROM 0x0013b6a4 AvoidConflictWithRexInIOSpace__17TNewInternalFlashFRUc
	NewtonErr		ConfigureFlashBank(ULong& flashAddress, ULong& readAddress, ULong& writeAddress);			// ROM 0x0013cc44 ConfigureFlashBank__17TNewInternalFlashFRUlN21
	NewtonErr		ConfigureNot32BitFlashBank(ULong& flashAddress, ULong& readAddress, ULong& writeAddress,
											   eMemoryLane lanes, TFlashRange*& range);							// ROM 0x0013ce2c ConfigureNot32BitFlashBank__17TNewInternalFlashFRUlN2111eMemoryLaneRP11TFlashRange
	NewtonErr		ConfigureIOBank(ULong& flashAddress, ULong& readAddress, ULong& writeAddress);				// ROM 0x0013caf0 ConfigureIOBank__17TNewInternalFlashFRUlN21
	Boolean			CheckFor4LaneFlash(ULong address, SFlashChipInformation& info, TFlashDriver*& driver);		// ROM 0x0013c510 CheckFor4LaneFlash__17TNewInternalFlashFUlR21SFlashChipInformationRP12TFlashDriver
	Boolean			CheckFor2LaneFlash(ULong address, SFlashChipInformation& info, TFlashDriver*& driver, eMemoryLane lanes);	// ROM 0x0013c6d8 CheckFor2LaneFlash__17TNewInternalFlashFUlR21SFlashChipInformationRP12TFlashDriver11eMemoryLane
	Boolean			CheckFor1LaneFlash(ULong address, SFlashChipInformation& info, TFlashDriver*& driver, eMemoryLane lanes);	// ROM 0x0013c864 CheckFor1LaneFlash__17TNewInternalFlashFUlR21SFlashChipInformationRP12TFlashDriver11eMemoryLane
	Boolean			FindDriverAble(TFlashDriver*& driver, ULong address, eMemoryLane lanes, SFlashChipInformation& info);		// ROM 0x0013b630 FindDriverAble__17TNewInternalFlashFRP12TFlashDriverUl11eMemoryLaneR21SFlashChipInformation
	NewtonErr		AddFlashRange(TFlashRange* range, ULong& flashAddress, ULong& readAddress, ULong& writeAddress, ULong physicalAddress);	// ROM 0x0013c9c8 AddFlashRange__17TNewInternalFlashFP11TFlashRangeRUlN22Ul
	void			AlignAndMapVMRange(ULong& virtualAddress, ULong physicalAddress, long size, UChar cacheable, Perm perm);	// ROM 0x0013c7d4 AlignAndMapVMRange__17TNewInternalFlashFRUlUllUc4Perm
	NewtonErr		AllocateReservedBlockRange(TFlashRange*& range);											// ROM 0x0013afe0 AllocateReservedBlockRange__17TNewInternalFlashFRP11TFlashRange
	NewtonErr		GatherBlockMappingInfo(ULong& erasedRegion, ULong& erasingRegion, ULong& strayRegion, ULong& unmappedRegion);	// ROM 0x0013b050 GatherBlockMappingInfo__17TNewInternalFlashFRUlN31
	NewtonErr		SetupVirtualMappings(void);																	// ROM 0x0013b214 SetupVirtualMappings__17TNewInternalFlashFv
	NewtonErr		FindRange(ULong physicalAddress, TFlashRange*& range);										// ROM 0x0013b38c FindRange__17TNewInternalFlashFUlRP11TFlashRange
	NewtonErr		SyncErasePhysicalBlock(ULong physicalAddress);												// ROM 0x0013b3e8 SyncErasePhysicalBlock__17TNewInternalFlashFUl
	NewtonErr		ReadWrite(long (TFlashRange::*op)(ULong, ULong, char*), ULong address, ULong size, char* buffer);			// ROM 0x0013ba80 ReadWrite__17TNewInternalFlashFM11TFlashRangeFUlT2Pc_lN22Pc
	NewtonErr		ReadWritePhysical(long (TFlashRange::*op)(ULong, ULong, char*), ULong address, ULong size, char* buffer);	// ROM 0x0013bb10 ReadWritePhysical__17TNewInternalFlashFM11TFlashRangeFUlT2Pc_lN22Pc
	NewtonErr		ReadPhysical(ULong address, ULong size, char* buffer);										// ROM 0x0013bb98 ReadPhysical__17TNewInternalFlashFUlT1Pc
	NewtonErr		WritePhysical(ULong address, ULong size, char* buffer);										// ROM 0x0013bbc0 WritePhysical__17TNewInternalFlashFUlT1Pc
	void			TurnPowerOn(void);																			// ROM 0x0013bd10 TurnPowerOn__17TNewInternalFlashFv
	void			TurnPowerOff(void);																			// ROM 0x0013bd9c TurnPowerOff__17TNewInternalFlashFv
	void			Clobber(void);																				// ROM 0x0013bdd0 Clobber__17TNewInternalFlashFv
	void			InternalClobber(void);																		// ROM 0x0013be0c InternalClobber__17TNewInternalFlashFv
	Boolean			CheckEraseCompletion(long& result, eCheckEraseOption option);								// ROM 0x0013bf7c CheckEraseCompletion__17TNewInternalFlashFRlQ217TNewInternalFlash17eCheckEraseOption
	Boolean			InternalCheckEraseCompletion(long& result, eCheckEraseOption option);						// ROM 0x0013c040 InternalCheckEraseCompletion__17TNewInternalFlashFRlQ217TNewInternalFlash17eCheckEraseOption
	NewtonErr		CopyUsingBuffer(ULong from, ULong to, ULong size, void* buffer, ULong bufferSize);			// ROM 0x0013c2ec CopyUsingBuffer__17TNewInternalFlashFUlN21PvT1

	TMemoryAllocator*	fAllocator;			// +0x10
	ULong			fRangeCount;			// +0x14
	ULong			fTotalSize;				// +0x18  flash addresses: every range's, less the reserved region
	ULong			fEraseRegionSize;		// +0x1c  the largest range's erase unit
	TFlashDriver*	fDrivers[6];			// +0x20
	TFlashRange*	fRanges[3];				// +0x38
	TFlashRange*	fErasingRange;			// +0x44  an erase under way
	long			fEraseResult;			// +0x48  an erase that failed, still to be seen to
	ULong			fDriverCount;			// +0x4c
	TBankControlRegister*	fBankControl;	// +0x50
	ULong			fMapWindows;			// +0x54  the eInitHWOption
	UChar			fNeedsVpp;				// +0x58
	UChar*			fBlockMap;				// +0x5c  per logical region, its physical region: two bytes, big-endian
	ULong			fRegionCount;			// +0x60  physical regions
	ULong			fSpareRegion;			// +0x64  0xFFFF: none
	TULockingSemaphore*	fLock;				// +0x68  nil when made at boot, before there are semaphores
};

extern TNewInternalFlash*	gInternalFlash;				// ROM 0x0c101568 gInternalFlash
extern ULong				gInternalFlashStoreSize;	// ROM 0x0c106520 (unnamed) - what InternalStoreInfo answers
extern ULong				gInternalFlashNeedsVpp;		// ROM 0x0c106524 (unnamed)

Boolean		IsInternalFlashEraseActive(void);		// ROM 0x0013c0c4 IsInternalFlashEraseActive__Fv
void		ClobberInternalFlash(void);				// ROM 0x0013c4d8 ClobberInternalFlash__Fv

#endif	/* __FLASH_H */
