/*
	File:		stores/flash/host/HostCardFlash.cpp

	Contains:	TFlashSeries2 on a host (CardFlash.h): a flash card's chips
				done as their commands come to on the card's bytes
				(hal/host/HostCard.h).  What does not touch the chips - the
				geometry IdentifyCard works out, the getters, write
				protection - follows the ROM's and cites it; what stands for
				a chip command says DEVIATION.

	Written by:	the reconstruction, after the ROM's (0x000c3328-0x000c4aa4)
*/

#include "CardFlash.h"
#include "CardSocket.h"
#include "CardPCMCIA.h"
#include "CardPower.h"
#include "HostCard.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <string.h>


// the JEDEC id the host's card chips answer: Intel's 28F016SA
const ULong	kHostCardChipId = 0x89A0;


// An ARM register shift right: by 32 or more, nought.
static inline uint32_t
ArmLSR(uint32_t value, ULong amount)
{
	amount &= 0xFF;
	return amount >= 32 ? 0 : value >> amount;
}


PROTOCOL_IMPL_SOURCE_MACRO(TFlashSeries2)						// ROM 0x000c3328 Sizeof__13TFlashSeries2SFv
PROTOCOL_CLASSINFO(TFlashSeries2, "TFlash", "", 0, 0, nil)		// ROM 0x00386828 ClassInfo__13TFlashSeries2SFv


// ROM 0x000c3330 New__13TFlashSeries2Fv
TFlashSeries2*
TFlashSeries2::New(void)
{
	fSocket = nil;
	fRegisterBase = 0;
	fBase = nil;
	fEraseErrorAddress = 0;
	fWriteErrorAddress = 0;
	fVendor = 0;
	fDataOffset = 0;
	fAttributes = 0x0D;
	fFlags = 0x13;
	return this;
}


// ROM 0x000c39f0 Delete__13TFlashSeries2Fv
void
TFlashSeries2::Delete(void)
{ }


// The card's physical offset of a flash address.
ULong
TFlashSeries2::PhysicalOffset(ULong address)
{
	return (ULong) (fBase - (char*) fSocket->CommonMemBaseAddr()) + address;
}


// ROM 0x000c3de0 CheckWriteProtected__13TFlashSeries2Fv
Boolean
TFlashSeries2::CheckWriteProtected(void)
{
	if (fSocket->IsIOInteface())
		return false;
	return fSocket->IsWriteProtected();
}


// ROM 0x000c45d8 Read__13TFlashSeries2FUlT1Pc
// DEVIATION: the ROM puts the chips in read-array mode first (and reads
// their status, unless the card is write-protected); the host's always are.
NewtonErr
TFlashSeries2::Read(ULong address, ULong size, char* buffer)
{
	if (fTotalSize < address + size)
		return kError_Bad_Parameters;
	BlockMove(fBase + address, buffer, size);
	return noErr;
}


// ROM 0x000c4754 Write__13TFlashSeries2FUlT1Pc
// DEVIATION: the ROM writes a byte or a halfword at a time through the
// chips' program command, polling their status after each; the host's card
// takes the bytes as the chips would (clearing bits only).
NewtonErr
TFlashSeries2::Write(ULong address, ULong size, char* buffer)
{
	if (fTotalSize < address + size)
		return kError_Bad_Parameters;
	if (CheckWriteProtected())
		return kError_Write_Protected;
	fWriteErrorAddress = 0;
	NewtonErr err = HostCardFlashWrite(fSocket->SocketNumber(), PhysicalOffset(address), buffer, size);
	if (err != noErr)
	{
		fWriteErrorAddress = (ULong) (fBase + address);
		return kError_Flash_Write_Failed;
	}
	return noErr;
}


// ROM 0x000c4a94 Erase__13TFlashSeries2FUl
// DEVIATION: the ROM sends the chips' block erase (0x20, 0xD0) at the
// address and waits; the chips side by side each erase a block, so the
// host sets the erase region that holds the address to 0xFF.
NewtonErr
TFlashSeries2::Erase(ULong address)
{
	if (fTotalSize <= address)
		return kError_Bad_Parameters;
	if (CheckWriteProtected())
		return kError_Write_Protected;
	ULong region = GetEraseRegionSize();
	ULong physical = PhysicalOffset(address) & ~(region - 1);
	NewtonErr err = HostCardFlashErase(fSocket->SocketNumber(), physical, region);
	if (err != noErr)
	{
		fEraseErrorAddress = (ULong) (fBase + address);
		return kError_Flash_Erase_Failed;
	}
	return noErr;
}


// ROM 0x000c3534 SuspendErase__13TFlashSeries2FUlN21
// DEVIATION: an erase is done at once on the host; there is none to suspend.
NewtonErr
TFlashSeries2::SuspendErase(ULong, ULong, ULong)
{
	return kError_Flash_Not_Erasing;
}


// ROM 0x000c3364 ResumeErase__13TFlashSeries2FUl
// DEVIATION: (DoErase resuming the suspended erase) nothing to resume.
NewtonErr
TFlashSeries2::ResumeErase(ULong)
{
	return noErr;
}


NewtonErr	TFlashSeries2::DeepSleep(ULong)		{ return kError_Call_Not_Implemented; }	// ROM 0x000c3744 DeepSleep__13TFlashSeries2FUl
NewtonErr	TFlashSeries2::Wakeup(ULong)		{ return kError_Call_Not_Implemented; }	// ROM 0x000c3750 Wakeup__13TFlashSeries2FUl


// ROM 0x000c375c Status__13TFlashSeries2FUl
// DEVIATION: the chips' status register read; the host's are always ready (1).
NewtonErr
TFlashSeries2::Status(ULong address)
{
	if (fTotalSize <= address)
		return kError_Bad_Parameters;
	return 1;
}


// ROM 0x000c3964 ResetCard__13TFlashSeries2Fv
// DEVIATION: the ROM resets the card through the socket and waits for it.
NewtonErr
TFlashSeries2::ResetCard(void)
{
	fSocket->PCMCIAReset();
	return noErr;
}


// ROM 0x000c39f4 AcknowledgeReset__13TFlashSeries2Fv
void
TFlashSeries2::AcknowledgeReset(void)
{
	fFlags &= ~2;
}


ULong		TFlashSeries2::GetPhysResource(void)		{ return 0; }						// ROM 0x000c3a08 GetPhysResource__13TFlashSeries2Fv
void		TFlashSeries2::RegisterClientInfo(ULong)	{ }									// ROM 0x000c3a04 RegisterClientInfo__13TFlashSeries2FUl
ULong		TFlashSeries2::GetWriteErrorAddress(void)	{ return fWriteErrorAddress; }		// ROM 0x000c3a30 GetWriteErrorAddress__13TFlashSeries2Fv
ULong		TFlashSeries2::GetAttributes(void)			{ return fAttributes; }				// ROM 0x000c3a20 GetAttributes__13TFlashSeries2Fv
ULong		TFlashSeries2::GetDataOffset(void)			{ return fDataOffset; }				// ROM 0x000c3a28 GetDataOffset__13TFlashSeries2Fv
ULong		TFlashSeries2::GetTotalSize(void)			{ return fTotalSize; }				// ROM 0x000c3a38 GetTotalSize__13TFlashSeries2Fv
ULong		TFlashSeries2::GetGroupSize(void)			{ return fChipsPerGroup * fChipSize; }	// ROM 0x000c3a40 GetGroupSize__13TFlashSeries2Fv
ULong		TFlashSeries2::GetEraseRegionSize(void)		{ return fChipsPerGroup * fBlockSize; }	// ROM 0x000c3ad0 GetEraseRegionSize__13TFlashSeries2Fv
ULong		TFlashSeries2::GetChipsPerGroup(void)		{ return fChipsPerGroup; }			// ROM 0x000c3ae0 GetChipsPerGroup__13TFlashSeries2Fv
ULong		TFlashSeries2::GetBlocksPerPartition(void)	{ return fBlocksPerPartition; }		// ROM 0x000c3ae8 GetBlocksPerPartition__13TFlashSeries2Fv
ULong		TFlashSeries2::GetEraseRegionCurrent(void)	{ return 60000000; }				// ROM 0x000c3b18 GetEraseRegionCurrent__13TFlashSeries2Fv
ULong		TFlashSeries2::GetWriteRegionCurrent(void)	{ return 60000000; }				// ROM 0x000c3b24 GetWriteRegionCurrent__13TFlashSeries2Fv
ULong		TFlashSeries2::GetEraseRegionTime(void)		{ return 1600000000; }				// ROM 0x000c3b30 GetEraseRegionTime__13TFlashSeries2Fv
ULong		TFlashSeries2::GetWriteAccessTime(void)		{ return 1000; }					// ROM 0x000c3b3c GetWriteAccessTime__13TFlashSeries2Fv
ULong		TFlashSeries2::GetReadAccessTime(void)		{ return fReadAccessTime; }			// ROM 0x000c3b44 GetReadAccessTime__13TFlashSeries2Fv
ULong		TFlashSeries2::GetVendorInfo(void)			{ return fVendor; }					// ROM 0x000c3a18 GetVendorInfo__13TFlashSeries2Fv
ULong		TFlashSeries2::GetSocketNumber(void)		{ return fSocket->SocketNumber(); }	// ROM 0x000c3a10 GetSocketNumber__13TFlashSeries2Fv
ULong		TFlashSeries2::VppStatus(void)				{ return 0; }						// ROM 0x000c3b6c VppStatus__13TFlashSeries2Fv
NewtonErr	TFlashSeries2::SuspendService(void)			{ return noErr; }					// ROM 0x000c3b64 SuspendService__13TFlashSeries2Fv
NewtonErr	TFlashSeries2::ResumeService(TCardSocket*, TCardPCMCIA*, ULong)	{ return noErr; }	// ROM 0x000c3fb4 ResumeService__13TFlashSeries2FP11TCardSocketP11TCardPCMCIAUl
NewtonErr	TFlashSeries2::Copy(ULong, ULong, ULong)	{ return kError_Call_Not_Implemented; }	// ROM 0x000c45c4 Copy__13TFlashSeries2FUlN21
Boolean		TFlashSeries2::IsVirgin(ULong, ULong)		{ return false; }					// ROM 0x000c45d0 IsVirgin__13TFlashSeries2FUlT1


// ROM 0x000c3af0 GetMaxConcurrentVppOps__13TFlashSeries2Fv
// (as many as the socket's Vpp current allows, at 60 mA each)
ULong
TFlashSeries2::GetMaxConcurrentVppOps(void)
{
	return (uint32_t) fSocket->VppMaxCurrent() / 60000000;
}


// ROM 0x000c3b4c VppRisingTime__13TFlashSeries2Fv
ULong
TFlashSeries2::VppRisingTime(void)
{
	if ((fAttributes & kFlashAttrNoVpp) != 0)
		return 0;
	return (fSocket->VppVoltageSpec() & kPCMCIA12VAvailable) != 0 ? 50000000 : 0;
}


// ROM 0x000c3884 GetWriteProtected__13TFlashSeries2FPUc
void
TFlashSeries2::GetWriteProtected(UChar* isProtected)
{
	*isProtected = true;
	*isProtected = CheckWriteProtected();
}


// ROM 0x000c40b8 FlashSpecific__13TFlashSeries2FUlPvT1
// 0: whether the card in socket (data) is one of these (size is its
// TCardPCMCIA); 1: the chips' programming voltage generator enabled.
// DEVIATION: the host's chips need no generator (CardEnableVppGenerator).
NewtonErr
TFlashSeries2::FlashSpecific(ULong selector, void* data, ULong size)
{
	if (selector == 0)
	{
		fSocket = (TCardSocket*) data;
		return IdentifyCard((TCardPCMCIA*) size, 0xFFFFFFFF);
	}
	return noErr;
}


// ROM 0x000c3a50 Initialize__13TFlashSeries2FP11TCardSocketP11TCardPCMCIAUlT3
// The device the card's CIS describes identified, and the card
// initialised.
// DEVIATION: CardInit (ROM 0x000c3e14) resets the card, enables its Vpp
// generator and clears every chip's status; the host's need none of it,
// so only the card's power is counted on and off again.
NewtonErr
TFlashSeries2::Initialize(TCardSocket* socket, TCardPCMCIA* card, ULong, ULong deviceNumber)
{
	fFlags = 0x13;
	if (socket == nil || card == nil)
		return kError_Bad_Parameters;
	fSocket = socket;
	fRegisterBase = socket->AttributeMemBaseAddr() + card->fRegisterBaseAddress;
	NewtonErr err = IdentifyCard(card, deviceNumber);
	if (err == noErr)
	{
		int socketNumber = (int) socket->SocketNumber();
		VccOn(socketNumber, false);
		fFlags |= 2;
		VccOff(socketNumber);
		fFlags &= ~1;
	}
	return err;
}


// ROM 0x000c40e0 IdentifyCard__13TFlashSeries2FP11TCardPCMCIAUl
// What the chips are - the CIS's JEDEC id for the device, or failing that
// the chips' own - and from it how big a chip and its blocks are; then the
// device's size (the chips' ids probed through the space when the CIS
// gives none), the data offset past a CIS kept in common memory, and where
// flash address 0 is.  A card with no CISTPL_DEVICE is given one.
// ==> kError_Unrecognized_Card for chips it does not know.
// DEVIATION: where the ROM reads the chips' id and probes the size through
// their read-identifier command, the host's chips answer kHostCardChipId
// and the card's size.
NewtonErr
TFlashSeries2::IdentifyCard(TCardPCMCIA* card, ULong deviceNumber)
{
	NewtonErr err = noErr;
	newton_try
	{
		fVendor = 0;
		TCardDevice* device = card->GetCardDevice(deviceNumber);
		if (device != nil)
			fVendor = device->fJedecMfrInfo + device->fJedecMfr * 0x100;
		if (fVendor == 0 && !CheckWriteProtected())
		{
			fSocket->SetControl(0);
			fVendor = kHostCardChipId;
		}
		fSocket->SetControl(10);
		// PatchPoint
		TNanoSecond speed = 100;
		UChar partition = 1;
		switch (fVendor)
		{
		case 0x89A7:	fBlocksPerChip = 8;		break;
		case 0x89A0:	fBlocksPerChip = 0x20;	speed = 150;	break;
		case 0x89A1:
		case 0x89A2:	fBlocksPerChip = 0x10;	speed = 250;	partition = 3;	break;
		case 0x89A6:	fBlocksPerChip = 0x10;	break;
		case 0x89AA:
		case 0xB088:	fBlocksPerChip = 0x20;	break;
		case 0xB0A8:	fBlocksPerChip = 0x10;	break;
		default:		err = kError_Unrecognized_Card;	break;
		}
		// PatchPoint
		if (err == noErr)
		{
			if ((fSocket->VppVoltageSpec() & kPCMCIA12VAvailable) == 0)
				fAttributes |= 0x80;
			// a card with no attribute memory mirrors its CIS in common memory
			const UByte* attr = (const UByte*) fSocket->AttributeMemBaseAddr();
			const UByte* common = (const UByte*) fSocket->CommonMemBaseAddr();
			Boolean same;
			for (ULong i = 0; ; i++)
			{
				same = attr[4 * i + 1] == common[4 * i + 1] && attr[4 * i + 3] == common[4 * i + 3];
				if (i + 1 > 7)
				{
					if (same)
					{
						fFlags |= 8;
						speed = 100;
						fAttributes = (fAttributes & ~0x80) | 0x30;
						card->fNoAttrMem = -1;
					}
					break;
				}
				if (!same)
					break;
			}
			// PatchPoint
			if (device == nil)
			{
				device = new TCardDevice;
				if (device == nil)
				{
					err = kError_No_Memory;
					goto done;
				}
				device->fStartOffset = 0;
				device->fSize = 0;
				device->fnsecSpeed = speed;
				device->fVcc = 0;
				device->fWPS = 0;
				device->fMwait = -1;
				device->fAttributeMemoryDescr = 0;
				device->fDeviceType = 5;
				device->fJedecMfr = (UChar) (fVendor >> 8);
				device->fJedecMfrInfo = (UChar) fVendor;
				device->fBusSize = 2;
				device->fEraseBlockSize = 0x11;
				device->fReadBlockSize = 1;
				device->fWriteBlockSize = 1;
				device->fPart = partition;
				device->fInterleave = 1;
				card->AddCardDevice(device);
				card->fNumOfDevice++;
				card->fBadCIS = 0;
			}
			// PatchPoint
			fBlockSize = ArmLSR(0xFFFFFFFF, 32 - (device->fEraseBlockSize - 1)) + 1;
			fChipSize = fBlocksPerChip * fBlockSize;
			fChipsPerGroup = (UChar) (device->fInterleave << 1);
			fBlocksPerPartition = (UChar) (ArmLSR(0xFFFFFFFF, 32 - (device->fPart - 1)) + 1);
			fReadAccessTime = device->fnsecSpeed;
			// PatchPoint
			if (device->fSize == 0 && !CheckWriteProtected())
			{
				ULong size = HostCardCommonSize(fSocket->SocketNumber()) - device->fStartOffset;
				device->fSize = size;
				card->fTotalDeviceSize = size;
			}
			// PatchPoint
			fDataOffset = 0;
			fTotalSize = device->fSize;
			fChips = (UChar) (fChipSize != 0 ? fTotalSize / fChipSize : 0);
			ULong start = device->fStartOffset;
			ULong first = card->fFirstDataByteAddress;
			if ((fFlags & 8) != 0 && card->fNoCIS == 0 && (fFlags & 0x10) != 0 && deviceNumber == 0 && first == 0 && start == 0)
			{
				first = fChipsPerGroup * fBlockSize;
				card->fFirstDataByteAddress = first;
			}
			if (start < first && first < fTotalSize + start)
			{
				fDataOffset = first;
				fTotalSize -= first;
			}
			fBase = (char*) fSocket->CommonMemBaseAddr() + start + fDataOffset;
			// PatchPoint
		}
	done: ;
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	fSocket->SetControl(10);
	return err;
}
