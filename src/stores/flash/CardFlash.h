/*
	File:		stores/flash/CardFlash.h

	Contains:	TFlashSeries2, the TFlash of a linear flash card of Intel
				Series 2 chips (28F008SA, 28F016SA and their kind), which
				TCHMemModem makes for a card's flash device and the card's
				flash store writes through (it reads the card directly,
				through its window).

				The ROM's (0x000c3328-0x000c4aa4) works the chips through
				the Voyager's PCMCIA bus: their command set written as bus
				cycles, 16-bit writes by swapping the two halves of the
				word (SetControl), status polled between.  That is the
				chips, so - as the host's TFlashDriver stands in for the
				internal flash's T28F016_SA_SVDriver - a host implementation
				stands in for it under the same name
				(host/HostCardFlash.cpp): the geometry is worked out as the
				ROM's IdentifyCard works it out, from the card's CIS and
				the chips' JEDEC id, and a write or an erase is done to the
				card's bytes (hal/host/HostCard.h) as the chips would do it.
				TFlashAMD (0x000c1e8c on) is NOT YET.
*/

#ifndef __CARDFLASH_H
#define __CARDFLASH_H

#ifndef __FLASH_H
#include "Flash.h"
#endif

#ifndef __CARDDEFINES_H
#include "CardDefines.h"
#endif

class TCardDevice;


PROTOCOL TFlashSeries2 : public TFlash
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TFlashSeries2);

	TFlashSeries2*	New(void);
	void			Delete(void);

	NewtonErr		Read(ULong address, ULong size, char* buffer);
	NewtonErr		Write(ULong address, ULong size, char* buffer);
	NewtonErr		Erase(ULong address);
	NewtonErr		SuspendErase(ULong, ULong, ULong);
	NewtonErr		ResumeErase(ULong);
	NewtonErr		DeepSleep(ULong);
	NewtonErr		Wakeup(ULong);
	NewtonErr		Status(ULong);
	NewtonErr		ResetCard(void);
	void			AcknowledgeReset(void);
	ULong			GetPhysResource(void);
	void			RegisterClientInfo(ULong);
	void			GetWriteProtected(UChar* isProtected);
	ULong			GetWriteErrorAddress(void);
	ULong			GetAttributes(void);
	ULong			GetDataOffset(void);
	ULong			GetTotalSize(void);
	ULong			GetGroupSize(void);
	ULong			GetEraseRegionSize(void);
	ULong			GetChipsPerGroup(void);
	ULong			GetBlocksPerPartition(void);
	ULong			GetMaxConcurrentVppOps(void);
	ULong			GetEraseRegionCurrent(void);
	ULong			GetWriteRegionCurrent(void);
	ULong			GetEraseRegionTime(void);
	ULong			GetWriteAccessTime(void);
	ULong			GetReadAccessTime(void);
	ULong			GetVendorInfo(void);
	ULong			GetSocketNumber(void);
	ULong			VppStatus(void);
	ULong			VppRisingTime(void);
	NewtonErr		FlashSpecific(ULong selector, void* data, ULong size);
	NewtonErr		Initialize(TCardSocket* socket, TCardPCMCIA* card, ULong, ULong deviceNumber);
	NewtonErr		SuspendService(void);
	NewtonErr		ResumeService(TCardSocket* socket, TCardPCMCIA* card, ULong);
	NewtonErr		Copy(ULong from, ULong to, ULong size);
	Boolean			IsVirgin(ULong address, ULong size);

	NewtonErr		IdentifyCard(TCardPCMCIA* card, ULong deviceNumber);
	Boolean			CheckWriteProtected(void);
	ULong			PhysicalOffset(ULong address);

	TCardSocket*	fSocket;			// +10
	ULong			fRegisterBase;		// +14 the card's configuration registers
	char*			fBase;				// +18 flash address 0: the device's data in the socket's common memory
	ULong			fVendor;			// +1C the chips' JEDEC id: manufacturer << 8 | device
	ULong			fAttributes;		// +20
	ULong			fChipSize;			// +24
	ULong			fBlockSize;			// +28 a chip's erase block
	UChar			fChips;				// +2C
	UChar			fChipsPerGroup;		// +2D chips side by side on the bus
	UChar			fBlocksPerPartition;	// +2E
	TNanoSecond		fReadAccessTime;	// +3C
	ULong			fTotalSize;			// +34
	ULong			fDataOffset;		// +38
	ULong			fWriteErrorAddress;	// +40
	ULong			fEraseErrorAddress;	// +44
	ULong			fFlags;				// +48 bit 0: not yet initialised; 2: initialising; 8: the CIS mirrored in common memory
	ULong			fBlocksPerChip;		// +4C
};

#endif	/* __CARDFLASH_H */
