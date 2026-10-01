/*
	File:		pcmcia/ATA.h

	Contains:	ATA (PC Card "fixed disk") cards: the TATA protocol a driver
				for one implements, the command blocks it is driven with,
				and TATASimple - the ROM's own implementation, which drives
				one drive (or two) through the card's task file in
				programmed I/O, a sector at a time.

				The ROM has no store on an ATA card and no card handler for
				one: what it does with such a card is read the driver for it
				off the card itself (pcmcia/CardATALoader.h), through
				TATASimple.  A driver package (Apple's ATA Support, say)
				brings the rest.

				Not in the DDK; layouts from the ROM.  Reconstructed from
				the MP2x00 US ROM: TATASimple 0x00026408-0x000271a4 and its
				class info 0x00386678, the TATA glue 0x00386218-0x00386344.
*/

#ifndef __ATA_H
#define __ATA_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

class TCardSocket;
class TCardPCMCIA;


// The commands TATASimple knows (the task file's command register)
enum
{
	kATACmdReadSectors			= 0x20,
	kATACmdReadLong				= 0x22,
	kATACmdWriteSectors			= 0x30,
	kATACmdWriteLong			= 0x32,
	kATACmdWriteVerify			= 0x3C,
	kATACmdFormatTrack			= 0x50,
	kATACmdInitDriveParams		= 0x91,
	kATACmdCheckPowerMode		= 0x98,
	kATACmdReadMultiple			= 0xC4,
	kATACmdWriteMultiple		= 0xC5,
	kATACmdSetMultipleMode		= 0xC6,
	kATACmdReadBuffer			= 0xE4,
	kATACmdWriteBuffer			= 0xE8,
	kATACmdIdentifyDrive		= 0xEC,
	kATACmdSetFeatures			= 0xEF
};

// The status register's bits
enum
{
	kATAStatusError		= 0x01,
	kATAStatusDRQ		= 0x08,
	kATAStatusReady		= 0x40,
	kATAStatusBusy		= 0x80
};

// The task file as it lies in the card's register window.  A card byte at
// address a is on the MessagePad's bus at a ^ 3 (pcmcia/CardCISIterator.cpp's
// Lane), so the ATA registers 0-7, 0xE and 0xF are at these offsets; the
// 16-bit data register is the word the bus reads at offset 0 (its first
// byte, D0-D7, at 3).
enum
{
	kATARegSectorNumber		= 0x00,		// register 3
	kATARegSectorCount		= 0x01,		// 2
	kATARegError			= 0x02,		// 1 (written: the features)
	kATARegData				= 0x03,		// 0
	kATARegCommand			= 0x04,		// 7 (read: the status)
	kATARegDriveHead		= 0x05,		// 6
	kATARegCylinderHigh		= 0x06,		// 5
	kATARegCylinderLow		= 0x07,		// 4
	kATARegDriveAddress		= 0x0C,		// 0xF
	kATARegDeviceControl	= 0x0D		// 0xE (read: the alternate status)
};
typedef UByte TATARegisters;


// A command put to the task file (0x18 bytes)
struct TATARegCommandBlock
{
	UByte*		fBuffer;			// +00 the data, a sector after another
	UByte		fFeatures;			// +04 in: the features; out: the error register
	UByte		fSectorCount;		// +05
	UByte		fSectorNumber;		// +06
	UByte		fCylinderLow;		// +07
	UByte		fCylinderHigh;		// +08
	UByte		fDriveHead;			// +09 bit 4 the drive
	UByte		fCommand;			// +0A in: the command; out: the status
	UByte		fDriveAddress;		// +0B out: the drive address register
	ULong		fField0C;			// +0C (from TATALBACommandBlock's +14; never looked at)
	ULong		fField10;			// +10
	ULong		fField14;			// +14
};

// A transfer of sectors by their logical block numbers (0x20 bytes)
struct TATALBACommandBlock
{
	UByte*		fBuffer;			// +00
	ULong		fBlock;				// +04 the first block
	ULong		fCount;				// +08 the blocks left to do
	ULong		fCurrentBlock;		// +0C the last block the drive was at
	ULong		fDone;				// +10 the blocks done
	ULong		fField14;			// +14
	UByte		fCommand;			// +18
	UByte		fDrive;				// +19
	UByte		fFeatures;			// +1A
	UByte		fStatusMask;		// +1B (0x50 put there; never looked at)
	UByte		fField1C;			// +1C
	UByte		fField1D[3];		// +1D
};

// What IDENTIFY DRIVE answers: 256 words, the first 0x78 bytes of which a
// TATASimple keeps for each drive - in the ROM's processor's order, the
// words swapped from the drive's little-endian ones and the current
// capacity (words 57-58) made one big-endian long (SwapDriveInfoBytes).
// The bytes are kept as the ROM keeps them, big-endian on every host (a
// driver package's ARM code may read them): read a field with
// toolbox/ByteOrder.h's GetBigEndianHalf.
struct TATADriveBasicInfo
{
	UShort		fConfiguration;				// +00 word 0
	UShort		fCylinders;					// +02 word 1
	UShort		fReserved2;					// +04
	UShort		fHeads;						// +06 word 3
	UShort		fUnformattedBytesPerTrack;	// +08
	UShort		fUnformattedBytesPerSector;	// +0A
	UShort		fSectorsPerTrack;			// +0C word 6
	UShort		fVendor7[3];				// +0E
	char		fSerialNumber[20];			// +14 words 10-19
	UShort		fBufferType;				// +28 word 20
	UShort		fBufferSize;				// +2A
	UShort		fECCBytes;					// +2C word 22: a long transfer's extra bytes
	char		fFirmwareRevision[8];		// +2E words 23-26
	char		fModelNumber[40];			// +36 words 27-46
	UShort		fMultiple;					// +5E word 47 (SetMultipleMode writes the count here)
	UShort		fDoubleWordIO;				// +60
	UShort		fCapabilities;				// +62 word 49: bit 9 LBA
	UShort		fReserved50;				// +64
	UShort		fPIOTiming;					// +66
	UShort		fDMATiming;					// +68
	UShort		fFieldValidity;				// +6A word 53
	UShort		fCurrentCylinders;			// +6C word 54
	UShort		fCurrentHeads;				// +6E word 55
	UShort		fCurrentSectorsPerTrack;	// +70 word 56
	UByte		fCurrentCapacity[4];		// +72 words 57-58
	UShort		fField76;					// +76
};

struct TATADriveInfo
{
	TATADriveBasicInfo	fBasic;
	UByte		fRest[0x200 - sizeof(TATADriveBasicInfo)];
};


/*------------------------------------------------------------------------------
	T A T A
	The interface a driver for an ATA card implements.  The ROM makes
	TATASimple by name ("TATASimple") when no driver is handed it.
------------------------------------------------------------------------------*/

PROTOCOL TATA : public TProtocol
{
public:
	static TATA*	New(const char* implementation);			// ROM 0x00386218 New__4TATASFPc
	void			Delete(void);								// ROM 0x00386244 Delete__4TATAFv

	VIRTUAL void		SetAttributes(ULong attributes) ENDVIRTUAL;												// ROM 0x00386260 SetAttributes__4TATAFUl
	VIRTUAL ULong		GetAttributes(void) ENDVIRTUAL;															// ROM 0x0038626c GetAttributes__4TATAFv
	VIRTUAL NewtonErr	Read(UByte* buffer, ULong block, ULong count, UByte command, UByte drive) ENDVIRTUAL;	// ROM 0x00386278 Read__4TATAFPUcUlT2UcT4
	VIRTUAL NewtonErr	Write(UByte* buffer, ULong block, ULong count, UByte command, UByte drive) ENDVIRTUAL;	// ROM 0x00386284 Write__4TATAFPUcUlT2UcT4
	VIRTUAL NewtonErr	Format(UByte* buffer, ULong cylinder, ULong head, ULong count, UByte drive) ENDVIRTUAL;	// ROM 0x00386290 Format__4TATAFPUcUlN22Uc
	VIRTUAL NewtonErr	Reset(UByte wait) ENDVIRTUAL;															// ROM 0x0038629c Reset__4TATAFUc
	VIRTUAL NewtonErr	IdentifyDrive(TATADriveInfo* info, UByte drive) ENDVIRTUAL;							// ROM 0x003862a8 IdentifyDrive__4TATAFP13TATADriveInfoUc
	VIRTUAL NewtonErr	CheckPowerMode(UByte* mode, UByte drive) ENDVIRTUAL;									// ROM 0x003862b4 CheckPowerMode__4TATAFPUcUc
	VIRTUAL NewtonErr	SetMultipleMode(UByte count, UByte drive) ENDVIRTUAL;									// ROM 0x003862c0 SetMultipleMode__4TATAFUcT1
	VIRTUAL NewtonErr	SetFeatures(UByte feature, UByte value, UByte drive) ENDVIRTUAL;						// ROM 0x003862cc SetFeatures__4TATAFUcN21
	VIRTUAL NewtonErr	SetPowerMode(UByte command, UByte count, UByte drive) ENDVIRTUAL;						// ROM 0x003862d8 SetPowerMode__4TATAFUcN21
	VIRTUAL NewtonErr	InitDriveParam(UByte sectors, UByte heads, UByte drive) ENDVIRTUAL;					// ROM 0x003862e4 InitDriveParam__4TATAFUcN21
	VIRTUAL NewtonErr	DoATALBACommand(TATALBACommandBlock* block) ENDVIRTUAL;								// ROM 0x003862f0 DoATALBACommand__4TATAFP19TATALBACommandBlock
	VIRTUAL NewtonErr	DoATARegCommand(TATARegCommandBlock* block) ENDVIRTUAL;								// ROM 0x003862fc DoATARegCommand__4TATAFP19TATARegCommandBlock
	VIRTUAL void		SetDeviceControlReg(UByte value) ENDVIRTUAL;											// ROM 0x00386308 SetDeviceControlReg__4TATAFUc
	VIRTUAL NewtonErr	ATASpecific(ULong selector, void* data, ULong size) ENDVIRTUAL;						// ROM 0x00386314 ATASpecific__4TATAFUlPvT1
	VIRTUAL NewtonErr	Initialize(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber) ENDVIRTUAL;	// ROM 0x00386320 Initialize__4TATAFP11TCardSocketP11TCardPCMCIAUl
	VIRTUAL NewtonErr	SuspendService(void) ENDVIRTUAL;														// ROM 0x0038632c SuspendService__4TATAFv
	VIRTUAL NewtonErr	ResumeService(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber) ENDVIRTUAL;	// ROM 0x00386338 ResumeService__4TATAFP11TCardSocketP11TCardPCMCIAUl
};


/*------------------------------------------------------------------------------
	T A T A S i m p l e
	0x120 bytes in the ROM.
------------------------------------------------------------------------------*/

PROTOCOL TATASimple : public TATA
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TATASimple);

	TATASimple*		New(void);
	void			Delete(void);

	void			SetAttributes(ULong attributes);
	ULong			GetAttributes(void);
	NewtonErr		Read(UByte* buffer, ULong block, ULong count, UByte command, UByte drive);
	NewtonErr		Write(UByte* buffer, ULong block, ULong count, UByte command, UByte drive);
	NewtonErr		Format(UByte* buffer, ULong cylinder, ULong head, ULong count, UByte drive);
	NewtonErr		Reset(UByte wait);
	NewtonErr		IdentifyDrive(TATADriveInfo* info, UByte drive);
	NewtonErr		CheckPowerMode(UByte* mode, UByte drive);
	NewtonErr		SetMultipleMode(UByte count, UByte drive);
	NewtonErr		SetFeatures(UByte feature, UByte value, UByte drive);
	NewtonErr		SetPowerMode(UByte command, UByte count, UByte drive);
	NewtonErr		InitDriveParam(UByte sectors, UByte heads, UByte drive);
	NewtonErr		DoATALBACommand(TATALBACommandBlock* block);
	NewtonErr		DoATARegCommand(TATARegCommandBlock* block);
	void			SetDeviceControlReg(UByte value);
	NewtonErr		ATASpecific(ULong selector, void* data, ULong size);
	NewtonErr		Initialize(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber);
	NewtonErr		SuspendService(void);
	NewtonErr		ResumeService(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber);

	// not in the interface
	void			SwapDriveInfoBytes(TATADriveBasicInfo* info);		// ROM 0x00026e28 SwapDriveInfoBytes__10TATASimpleFP18TATADriveBasicInfo
	void			SwapShorts(UByte* bytes, ULong count);			// ROM 0x00026e64 SwapShorts__10TATASimpleFPUcUl
	NewtonErr		WaitFor(UByte mask, TATARegisters* registers);	// ROM 0x00026ea0 WaitFor__10TATASimpleFUcP13TATARegisters
	NewtonErr		CheckError(UByte error);						// ROM 0x00026f58 CheckError__10TATASimpleFUc

	enum
	{
		kIdentified		= 0x01,		// fFlags: the drives have been identified
		kVppPower		= 0x02		// the card wants 12 V on Vpp (its configuration's Vpp1)
	};

	TCardSocket*	fSocket;			// +10
	ULong			fAttributes;		// +14
	ULong			fFlags;				// +18
	UByte*			fConfigRegister;	// +1C the configuration option register (attribute memory)
	UByte			fConfigIndex;		// +20
	UByte			fInterfaceType;		// +21 0 memory, 1 I/O
	UByte			fLastDrive;			// +22 the highest drive number there is
	UByte			fDeviceControl;		// +23 what was last written to the device control register
	UByte			fField24;			// +24
	UByte			fField25;			// +25
	UByte			fField26;			// +26
	UByte			fField27;			// +27
	UByte*			fDataWindow;		// +28 common memory + 0x400 (never looked at)
	TATARegisters*	fRegisters;			// +2C the task file
	TATADriveBasicInfo	fDriveInfo[2];	// +30
};

#endif /* __ATA_H */
