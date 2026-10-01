/*
	File:		pcmcia/CardATALoader.h

	Contains:	How the card server takes an ATA card on: TCardATALoader
				reads the card's partition map through a TATA (the ROM's own
				TATASimple unless a driver brings one) and loads the
				packages the card carries for itself - an Apple_Newton_Driver
				partition's (the card's driver) and an Apple_Newton
				partition's - and, in the Apple_Newton partition's boot area,
				ARM610 code the ROM jumps into.  TATAPartitionInfo is what it
				found, which the card's handler is handed
				(kCardSpecificATASetPartitionInfo); TATABootParamBlock is
				what the boot code is handed.

				The disk the ROM reads: block 0 may be a PC master boot
				record, in which case the map starts at the first partition
				of type 0x83 (a bootable one first); there, a driver
				descriptor block ('ER'), and from the block after it Apple
				partition map entries ('PM', the map's length in the first
				one's pmMapBlkCnt).  An entry is the Newton's when its pmPad
				says 'newt' and the word after it has bit 8 set; its type
				names it, the word at +0x94 is how many blocks of package
				lie at the start of the partition (pmPyPartStart, a block
				number on the disk itself), and the Apple_Newton entry's
				boot area is pmLgBootStart/pmBootSize/pmBootCksum with
				pmProcessor "ARM610".  tools/cards/atacard.py makes such a
				disk.

				Not in the DDK; layouts from the ROM (0x20, 0x14 and 0x14
				bytes).  Reconstructed from the MP2x00 US ROM
				(0x0004a1f4-0x0004ac54).
*/

#ifndef __CARDATALOADER_H
#define __CARDATALOADER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TCardSocket;
class TCardPCMCIA;
class TATA;


// What the loader found on the card (0x20 bytes)
class TATAPartitionInfo
{
public:
					TATAPartitionInfo();			// ROM 0x0004ab50 __ct__17TATAPartitionInfoFv
					~TATAPartitionInfo();			// ROM 0x0004ab98 __dt__17TATAPartitionInfoFv
	void			Clear(void);					// ROM 0x0004abc4 Clear__17TATAPartitionInfoFv

	Long			fPCPartition;		// +00 the MBR partition the map is in (-1: none)
	Long			fMapBlock;			// +04 the driver descriptor block
	Long			fDriverBlock;		// +08 the Apple_Newton_Driver entry's block
	UByte*			fDriverEntry;		// +0C a copy of that entry
	Long			fNewtonBlock;		// +10 the Apple_Newton entry's block
	UByte*			fNewtonEntry;		// +14 a copy of that entry
	ULong			fField18;			// +18
	ULong			fField1C;			// +1C
};


// What the card's boot code is handed (0x14 bytes)
class TATABootParamBlock
{
public:
					TATABootParamBlock();			// ROM 0x0004ac08 __ct__18TATABootParamBlockFv
					~TATABootParamBlock();			// ROM 0x0004ac48 __dt__18TATABootParamBlockFv

	TCardSocket*		fSocket;			// +00
	TATA*				fATA;				// +04
	TATAPartitionInfo*	fPartitionInfo;		// +08 (the socket state's, set by its constructor)
	ULong				fField0C;			// +0C
	ULong				fField10;			// +10
};


// The loader (0x14 bytes)
class TCardATALoader
{
public:
					TCardATALoader();				// ROM 0x0004a1f4 __ct__14TCardATALoaderFv
					~TCardATALoader();				// ROM 0x0004a234 __dt__14TCardATALoaderFv

	Boolean			GetCardType(TCardPCMCIA* card);			// ROM 0x0004a274 GetCardType__14TCardATALoaderFP11TCardPCMCIA
	NewtonErr		LoadATAPackages(TCardSocket* socket, TCardPCMCIA* card, TATABootParamBlock* boot, void* ata, ULong version);	// ROM 0x0004a2d0 LoadATAPackages__14TCardATALoaderFP11TCardSocketP11TCardPCMCIAP18TATABootParamBlockPvUl
	NewtonErr		LoadDriverPackage(TATA* ata, TCardSocket* socket, ULong block, ULong count, UByte** buffer, ULong* packageId);	// ROM 0x0004a934 LoadDriverPackage__14TCardATALoaderFP4TATAP11TCardSocketUlT3PPUcPUl
	NewtonErr		RemoveATAPackages(TATABootParamBlock* boot, void* data, ULong version);	// ROM 0x0004aa1c RemoveATAPackages__14TCardATALoaderFP18TATABootParamBlockPvUl
	Boolean			SameStrings(char* a, char* b, ULong length);	// ROM 0x0004aa9c SameStrings__14TCardATALoaderFPcT1Ul
	ULong			ChecksumOf(UByte* bytes, ULong count);			// ROM 0x0004ab0c ChecksumOf__14TCardATALoaderFPUcUl

	UByte*			fBootCode;			// +00 the Apple_Newton partition's boot code
	UByte*			fDriverPackage;		// +04 the Apple_Newton_Driver partition's package
	ULong			fDriverPackageId;	// +08
	UByte*			fNewtonPackage;		// +0C the Apple_Newton partition's package
	ULong			fNewtonPackageId;	// +10
};

#endif /* __CARDATALOADER_H */
