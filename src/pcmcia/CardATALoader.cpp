/*
	File:		pcmcia/CardATALoader.cpp

	Contains:	TCardATALoader, TATAPartitionInfo and TATABootParamBlock
				(CardATALoader.h): an ATA card's partition map read and the
				packages it carries loaded.

	Reconstructed from the MP2x00 US ROM (0x0004a1f4-0x0004ac54); each
	function cites its origin.
*/

#include "CardATALoader.h"
#include "ATA.h"
#include "CardSocket.h"
#include "CardPCMCIA.h"
#include "PackageManager.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "ByteOrder.h"

#include <string.h>
#include <stdio.h>

static const char kParType_Apple_Newton[] = "Apple_Newton";				// ROM 0x00366f00 kParType_Apple_Newton
static const char kParType_Apple_Newton_Driver[] = "Apple_Newton_Driver";	// ROM 0x00366f10 kParType_Apple_Newton_Driver
static const char kProcType_ARM610[] = "ARM610";							// ROM 0x00366f24 kProcType_ARM610

#include "PackageTypes.h"
#include "PartHandler.h"


/*------------------------------------------------------------------------------
	T A T A P a r t i t i o n I n f o
------------------------------------------------------------------------------*/

// ROM 0x0004ab50 __ct__17TATAPartitionInfoFv
TATAPartitionInfo::TATAPartitionInfo()
	:	fDriverEntry(nil), fNewtonEntry(nil), fField18(0), fField1C(0)
{
	Clear();
}


// ROM 0x0004ab98 __dt__17TATAPartitionInfoFv
TATAPartitionInfo::~TATAPartitionInfo()
{
	Clear();
}


// ROM 0x0004abc4 Clear__17TATAPartitionInfoFv
void
TATAPartitionInfo::Clear(void)
{
	delete[] fDriverEntry;
	fDriverEntry = nil;
	delete[] fNewtonEntry;
	fPCPartition = -1;
	fNewtonEntry = nil;
	fMapBlock = -1;
	fDriverBlock = -1;
	fNewtonBlock = -1;
}


/*------------------------------------------------------------------------------
	T A T A B o o t P a r a m B l o c k
------------------------------------------------------------------------------*/

// ROM 0x0004ac08 __ct__18TATABootParamBlockFv
TATABootParamBlock::TATABootParamBlock()
	:	fSocket(nil), fATA(nil), fPartitionInfo(nil), fField0C(0), fField10(0)
{ }


// ROM 0x0004ac48 __dt__18TATABootParamBlockFv
TATABootParamBlock::~TATABootParamBlock()
{ }


/*------------------------------------------------------------------------------
	T C a r d A T A L o a d e r
------------------------------------------------------------------------------*/

// ROM 0x0004a1f4 __ct__14TCardATALoaderFv
TCardATALoader::TCardATALoader()
	:	fBootCode(nil), fDriverPackage(nil), fDriverPackageId(0), fNewtonPackage(nil), fNewtonPackageId(0)
{ }


// ROM 0x0004a234 __dt__14TCardATALoaderFv
TCardATALoader::~TCardATALoader()
{
	delete[] fBootCode;
	delete[] fDriverPackage;
	delete[] fNewtonPackage;
}


// ROM 0x0004a274 GetCardType__14TCardATALoaderFP11TCardPCMCIA
// Whether the card is an ATA disk: its CISTPL_FUNCID says a fixed disk
// (4) and one of its CISTPL_FUNCE says the interface is ATA (type 1,
// value 1).
Boolean
TCardATALoader::GetCardType(TCardPCMCIA* card)
{
	Boolean isATA = false;
	if (!card->fFunctionIdAvail)
		return isATA;
	if (card->fFunctionId != 4)
		return isATA;
	for (ULong i = 0; card->fNumOfFuncExt > i; i++)
	{
		if (card->fFuncExt[i][0] == 1 && card->fFuncExt[i][1] == 1)
			isATA = true;
		if (isATA)
			return isATA;
	}
	return isATA;
}


// ROM 0x0004a2d0 LoadATAPackages__14TCardATALoaderFP11TCardSocketP11TCardPCMCIAP18TATABootParamBlockPvUl
// The card's partition map read through a TATA (the driver given, else a
// TATASimple made for the purpose) put in the first configuration with a
// memory interface - else the last I/O one with sixteen addresses, else
// configuration 0 - and the Newton's two partitions found: each one's
// package loaded (where it lies on the card, read whole into memory),
// the Apple_Newton entry and the Apple_Newton_Driver entry kept in the
// partition info, and the Apple_Newton partition's ARM610 boot code read,
// checked against its checksum and called with the boot block.  The drive
// is put on standby (0x96) at the end, and a TATASimple made here deleted
// - though the boot block keeps pointing at it.
// ROM QUIRK: the packages are read from pmPyPartStart as a block number on
// the disk itself, not counted from the map's start, so a map inside a PC
// partition finds them only if the entries say where they are absolutely.
// DEVIATION: the host cannot jump into a card's ARM code (the ROM calls the
// address in the boot code's first word with the boot block in r0); a card
// with boot code fails as if the code had thrown (kError_Call_Aborted).
NewtonErr
TCardATALoader::LoadATAPackages(TCardSocket* socket, TCardPCMCIA* card, TATABootParamBlock* boot, void* driver, ULong version)
{
	NewtonErr err = noErr;
	Boolean made = false;
	if (version != 1)
		return kError_Bad_Parameters;
	TATA* ata = (TATA*) driver;
	if (ata == nil)
	{
		ata = TATA::New("TATASimple");
		if (ata == nil)
			return kError_No_Memory;
		made = true;
	}
	boot->fSocket = socket;
	boot->fATA = ata;
	TATAPartitionInfo* info = boot->fPartitionInfo;
	info->Clear();

	Long memoryConfig = -1, ioConfig = -1;
	Long config = 0;
	if (card->fNumOfConfigEntry > 0)
	{
		for (ULong i = 0; card->fNumOfConfigEntry > i; i++)
		{
			TCardConfiguration* c = card->GetCardConfiguration(i);
			if (c->fInterfaceType == 0)
			{
				memoryConfig = i;
				break;
			}
			if (c->fInterfaceType == 1 && c->fIoAddrLines == 4)
				ioConfig = i;
		}
		if (memoryConfig != -1)
			config = memoryConfig;
		else if (ioConfig != -1)
			config = ioConfig;
	}

	UByte block[0x200];
	if ((err = ata->Initialize(socket, card, config)) != noErr)
		goto done;
	if ((err = ata->Read(block, 0, 1, kATACmdReadSectors, 0)) != noErr)
		goto done;
	{
		ULong mapBlock = 0;
		if (block[0x1FE] == 0x55 && block[0x1FF] == 0xAA)
		{
			// a PC master boot record: the map is in a partition of type 0x83
			ULong found = 4, first = 4;
			for (ULong i = 0; i < 4; i++)
			{
				UByte* entry = block + 0x1BE + i * 16;
				if (entry[4] == 0x83)
				{
					if (entry[0] == 0x80)
					{
						found = i;
						break;
					}
					if (first == 4)
						first = i;
				}
			}
			if (found == 4)
			{
				if (first == 4)
				{
					err = kError_ATA_No_Partition;
					goto done;
				}
				found = first;
			}
			info->fPCPartition = found;
			UByte* entry = block + 0x1BE + found * 16;
			mapBlock = ((ULong) entry[11] << 24) + ((ULong) entry[10] << 16) + ((ULong) entry[9] << 8) + entry[8];
		}
		if ((err = ata->Read(block, mapBlock, 1, kATACmdReadSectors, 0)) != noErr)
			goto done;
		if (GetBigEndianHalf(block) != 0x4552)		// 'ER', the driver descriptor block
		{
			err = kError_ATA_No_Partition;
			goto done;
		}
		info->fMapBlock = mapBlock;

		// the partition map, from the block after
		ULong entryBlock = mapBlock + 1;
		Boolean sawMap = false;
		ULong mapCount = 100;
		for (ULong i = 0; i < mapCount; i++, entryBlock++)
		{
			if ((err = ata->Read(block, entryBlock, 1, kATACmdReadSectors, 0)) != noErr)
				goto done;
			if (GetBigEndianHalf(block) != 0x504D)		// 'PM'
				continue;
			if (!sawMap)
			{
				sawMap = true;
				mapCount = GetBigEndianWord(block + 4);		// pmMapBlkCnt
				i = 0;
			}
			if (GetBigEndianWord(block + 0x88) != 'newt' || (GetBigEndianWord(block + 0x8C) & 0x100) == 0)
				continue;
			if (SameStrings((char*) block + 0x30, (char*) kParType_Apple_Newton, 0x20))
			{
				if (info->fNewtonBlock != -1)
					continue;
				info->fNewtonBlock = entryBlock;
				if ((info->fNewtonEntry = new UByte[0x200]) == nil)
				{
					err = kError_No_Memory;
					goto done;
				}
				memcpy(info->fNewtonEntry, block, 0x200);
				ULong count = GetBigEndianWord(block + 0x94);
				if (count == 0)
					continue;
				if ((err = LoadDriverPackage(ata, socket, GetBigEndianWord(block + 8), count, &fNewtonPackage, &fNewtonPackageId)) != noErr)
					goto done;
			}
			else if (SameStrings((char*) block + 0x30, (char*) kParType_Apple_Newton_Driver, 0x20))
			{
				if (info->fDriverBlock != -1)
					continue;
				info->fDriverBlock = entryBlock;
				if ((info->fDriverEntry = new UByte[0x200]) == nil)
				{
					err = kError_No_Memory;
					goto done;
				}
				memcpy(info->fDriverEntry, block, 0x200);
				ULong count = GetBigEndianWord(block + 0x94);
				if (count == 0)
					continue;
				if ((err = LoadDriverPackage(ata, socket, GetBigEndianWord(block + 8), count, &fDriverPackage, &fDriverPackageId)) != noErr)
					goto done;
			}
		}
		if (!sawMap || info->fNewtonBlock == -1)
		{
			err = kError_ATA_No_Partition;
			goto done;
		}
	}

	// the Apple_Newton partition's boot code
	{
		UByte* entry = info->fNewtonEntry;
		ULong bootSize;
		if (entry == nil || (bootSize = GetBigEndianWord(entry + 0x60)) == 0)
			goto done;
		if (!SameStrings((char*) entry + 0x78, (char*) kProcType_ARM610, 0x10))
			goto done;
		ULong blocks = (bootSize + 0x1FF) >> 9;
		if ((fBootCode = new UByte[blocks << 9]) == nil)
		{
			err = kError_No_Memory;
			goto done;
		}
		err = ata->Read(fBootCode, GetBigEndianWord(entry + 8) + GetBigEndianWord(entry + 0x5C), blocks, kATACmdReadSectors, 0);
		if (err == noErr)
		{
			if (ChecksumOf(fBootCode, bootSize) != GetBigEndianWord(entry + 0x74))
				err = kError_Card_Checksum;
			else
			{
				newton_try
				{
					// DEVIATION: the ROM's `ldr pc, [fBootCode]` with r0 = boot
					err = kError_Call_Aborted;
				}
				newton_catch_all
				{
					err = kError_Call_Aborted;
				}
				end_try;
			}
		}
		if (err != noErr)
		{
			delete[] fBootCode;
			fBootCode = nil;
		}
	}

done:
	ata->SetPowerMode(0x96, 0, 0);
	if (made)
		ata->Delete();
	return err;
}


// ROM 0x0004a934 LoadDriverPackage__14TCardATALoaderFP4TATAP11TCardSocketUlT3PPUcPUl
// count blocks from block read into a new buffer and, when they start as a
// package does, loaded from there as a package in memory on the socket's
// card (source format 3, device kind 1); the buffer is kept while the
// package is loaded, and given back otherwise.
NewtonErr
TCardATALoader::LoadDriverPackage(TATA* ata, TCardSocket* socket, ULong block, ULong count, UByte** buffer, ULong* packageId)
{
	NewtonErr err;
	if ((*buffer = new UByte[count << 9]) == nil)
		return kError_No_Memory;
	if ((err = ata->Read(*buffer, block, count, kATACmdReadSectors, 0)) == noErr
	 && strncmp((const char*) *buffer, (const char*) kPackageMagicNumber, 7) == 0)
	{
		SourceType source;
		source.format = 3;
		source.deviceKind = 1;
		source.deviceNumber = (UShort) socket->SocketNumber();
		source.deviceId = 0;
		err = LoadPackage((Ptr) *buffer, source, packageId);
	}
	if (*packageId == 0)
	{
		delete[] *buffer;
		*buffer = nil;
	}
	return err;
}


// ROM 0x0004aa1c RemoveATAPackages__14TCardATALoaderFP18TATABootParamBlockPvUl
// The card's two packages removed and every buffer given back, the
// partition info cleared and the boot block forgotten.
NewtonErr
TCardATALoader::RemoveATAPackages(TATABootParamBlock* boot, void* data, ULong version)
{
	if (fDriverPackageId != 0)
	{
		RemovePackage(fDriverPackageId);
		fDriverPackageId = 0;
	}
	if (fNewtonPackageId != 0)
	{
		RemovePackage(fNewtonPackageId);
		fNewtonPackageId = 0;
	}
	delete[] fBootCode;
	fBootCode = nil;
	delete[] fDriverPackage;
	fDriverPackage = nil;
	delete[] fNewtonPackage;
	fNewtonPackage = nil;
	if (boot->fPartitionInfo != nil)
		boot->fPartitionInfo->Clear();
	boot->fSocket = nil;
	boot->fATA = nil;
	return noErr;
}


// ROM 0x0004aa9c SameStrings__14TCardATALoaderFPcT1Ul
// The two strings the same up to their ends, ignoring the case of A-Z,
// within length characters.
// ROM QUIRK: two strings that agree for all length characters without
// ending are not the same.
Boolean
TCardATALoader::SameStrings(char* a, char* b, ULong length)
{
	UByte c1, c2;
	do
	{
		c1 = (UByte) *a++;
		if (c1 >= 'A' && c1 <= 'Z')
			c1 = (UByte) (c1 + 0x20);
		c2 = (UByte) *b++;
		if (c2 >= 'A' && c2 <= 'Z')
			c2 = (UByte) (c2 + 0x20);
		if (c1 == 0)
			return c2 == 0;
		if (c2 == 0 || c1 != c2)
			break;
	}
	while (--length != 0);
	return false;
}


// ROM 0x0004ab0c ChecksumOf__14TCardATALoaderFPUcUl
// The Apple partition map's boot checksum: each byte added, then the
// whole shifted left one and bit 16 brought round to bit 0; the low
// sixteen bits, 0xFFFF for nought.
ULong
TCardATALoader::ChecksumOf(UByte* bytes, ULong count)
{
	ULong32 sum = 0;
	for (ULong i = 0; i < count; i++)
	{
		sum = (sum + *bytes++) << 1;
		sum |= (sum & 0x10000) >> 16;
	}
	sum &= 0xFFFF;
	if (sum == 0)
		sum = 0xFFFF;
	return sum;
}
