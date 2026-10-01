/*
	File:		pcmcia/ATASimple.cpp

	Contains:	The TATA glue and TATASimple, the ROM's ATA card driver
				(ATA.h): programmed I/O through the card's task file, a
				sector at a time, polling the status register.

	DEVIATION: the task file is read and written through hal/CardBus.h, so
	a host can stand a model of a card behind it; the ROM loads and stores
	straight at the window.  Everything else is the ROM's, the 16-bit data
	register included - it reads the word at the register window + 2 (an
	unaligned `ldr`, the aligned word rotated by 16 bits) and takes its two
	upper bytes, the first byte off the wire being bits 16-23.

	Reconstructed from the MP2x00 US ROM (0x00026408-0x000271a4,
	0x00386218-0x00386344); each function cites its origin.
*/

#include "ATA.h"
#include "CardBus.h"
#include "CardSocket.h"
#include "CardPCMCIA.h"
#include "CardPower.h"
#include "DelayTimer.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "ByteOrder.h"

#include <string.h>

// the TCardConfiguration slot that says the card wants 12 V on Vpp1
// (Initialize: 0xb71b00 microvolts)
static const ULong kTwelveVolts = 12000000;
// how long WaitFor polls the status register (ResetTimeOut's hardware time
// units: three seconds of the 3.6864 MHz timer)
static const ULong kATAWaitTimeout = 0xA8BB50;
// the drive bit of the drive/head register
#define ATADriveBit(drive)	((UByte) (((drive) << 4) & 0x10))


/*------------------------------------------------------------------------------
	T A T A
------------------------------------------------------------------------------*/

// ROM 0x00386218 New__4TATASFPc
TATA*
TATA::New(const char* implementation)
{
	TATA* p = (TATA*) AllocInstanceByName("TATA", implementation);
	return p != nil ? (TATA*) p->GlueNew() : nil;
}


// ROM 0x00386244 Delete__4TATAFv
void
TATA::Delete(void)
{
	GlueDelete();
}


/*------------------------------------------------------------------------------
	T A T A S i m p l e
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TATASimple)		// ROM 0x00026408 Sizeof__10TATASimpleSFv
PROTOCOL_CLASSINFO(TATASimple, "TATA", "", 0, 0, nil)	// ROM 0x003866b4 ClassInfo__10TATASimpleSFv


// ROM 0x00026410 New__10TATASimpleFv
TATASimple*
TATASimple::New(void)
{
	fSocket = nil;
	fAttributes = 0;
	fFlags = 0;
	fConfigRegister = nil;
	fConfigIndex = 0;
	fInterfaceType = 0;
	fLastDrive = 0;
	fDeviceControl = 0;
	fField25 = 0;
	fField24 = 0;
	fField27 = 0;
	fField26 = 0;
	fRegisters = nil;
	return this;
}


// ROM 0x00026e10 Delete__10TATASimpleFv
void
TATASimple::Delete(void)
{ }


// ROM 0x000270bc SetAttributes__10TATASimpleFUl
void
TATASimple::SetAttributes(ULong attributes)
{
	fAttributes = attributes;
}


// ROM 0x000270c4 GetAttributes__10TATASimpleFv
ULong
TATASimple::GetAttributes(void)
{
	return fAttributes;
}


// ROM 0x000270cc Read__10TATASimpleFPUcUlT2UcT4
// count blocks from block on, by the command given (READ SECTORS, or for
// Write the writing one: Write is this with its own command).
NewtonErr
TATASimple::Read(UByte* buffer, ULong block, ULong count, UByte command, UByte drive)
{
	if (fLastDrive < drive || buffer == nil)
		return kError_Bad_Parameters;
	TATALBACommandBlock lba;
	lba.fBuffer = buffer;
	lba.fBlock = block;
	lba.fCount = count;
	lba.fCurrentBlock = 0;		// (the ROM's are whatever was on the stack; DoATALBACommand sets them)
	lba.fDone = 0;
	lba.fField14 = 0;
	lba.fCommand = command;
	lba.fDrive = drive;
	lba.fFeatures = 0;
	lba.fStatusMask = 0;
	lba.fField1C = 0;
	lba.fField1D[0] = lba.fField1D[1] = lba.fField1D[2] = 0;
	return DoATALBACommand(&lba);
}


// ROM 0x00027140 Write__10TATASimpleFPUcUlT2UcT4
NewtonErr
TATASimple::Write(UByte* buffer, ULong block, ULong count, UByte command, UByte drive)
{
	return Read(buffer, block, count, command, drive);
}


// ROM 0x0002644c Format__10TATASimpleFPUcUlN22Uc
// FORMAT TRACK of a cylinder and head, the interleave table in the buffer.
NewtonErr
TATASimple::Format(UByte* buffer, ULong cylinder, ULong head, ULong count, UByte drive)
{
	if (fLastDrive < drive)
		return kError_Bad_Parameters;
	TATARegCommandBlock reg;
	reg.fBuffer = buffer;
	reg.fFeatures = 0;
	reg.fSectorCount = (UByte) count;
	reg.fSectorNumber = 0;
	reg.fCylinderLow = (UByte) cylinder;
	reg.fCylinderHigh = (UByte) (cylinder >> 8);
	reg.fDriveHead = (UByte) (ATADriveBit(drive) + head);
	reg.fCommand = kATACmdFormatTrack;
	reg.fDriveAddress = 0;		// (the ROM's: whatever was on the stack)
	reg.fField0C = 0;
	reg.fField10 = 0;			// (the ROM's: whatever was on the stack)
	reg.fField14 = 0;			// (the ROM clears its upper three bytes only)
	return DoATARegCommand(&reg);
}


// ROM 0x000264d0 Reset__10TATASimpleFUc
// A software reset (SRST in the device control register for 100 timer
// units), then - asked to - waiting for the drive to be ready again.
NewtonErr
TATASimple::Reset(UByte wait)
{
	CardBusWriteByte(fRegisters + kATARegDeviceControl, 4);
	TDelayTimer timer;
	timer.ShortTimerDelay(100);
	CardBusWriteByte(fRegisters + kATARegDeviceControl, fDeviceControl & ~4);
	timer.ShortTimerDelay(4);
	if (wait)
		return WaitFor(kATAStatusReady, fRegisters);
	return noErr;
}


// ROM 0x00026548 IdentifyDrive__10TATASimpleFP13TATADriveInfoUc
// IDENTIFY DRIVE: the drive's 512 bytes into info, and (DoATARegCommand)
// the first 0x78 of them kept.
NewtonErr
TATASimple::IdentifyDrive(TATADriveInfo* info, UByte drive)
{
	if (fLastDrive < drive)
		return kError_Bad_Parameters;
	TATARegCommandBlock reg;
	// (the ROM leaves the features, sector and cylinder registers' values
	// as whatever was on the stack: they are written to the drive, which
	// does not look at them)
	memset(&reg, 0, sizeof(reg));
	reg.fBuffer = (UByte*) info;
	reg.fSectorCount = 1;
	reg.fDriveHead = ATADriveBit(drive);
	reg.fCommand = kATACmdIdentifyDrive;
	reg.fField0C = 0;
	return DoATARegCommand(&reg);
}


// ROM 0x000265a4 CheckPowerMode__10TATASimpleFPUcUc
// CHECK POWER MODE: the sector count register after it (0xFF active or
// idle, 0 standby).
NewtonErr
TATASimple::CheckPowerMode(UByte* mode, UByte drive)
{
	if (fLastDrive < drive)
		return kError_Bad_Parameters;
	TATARegCommandBlock reg;
	memset(&reg, 0, sizeof(reg));		// (the ROM's: whatever was on the stack)
	reg.fDriveHead = ATADriveBit(drive);
	reg.fCommand = kATACmdCheckPowerMode;
	reg.fField0C = 0;
	NewtonErr err = DoATARegCommand(&reg);
	*mode = reg.fSectorCount;
	return err;
}


// ROM 0x00026604 SetPowerMode__10TATASimpleFUcN21
// One of the power commands (STANDBY, IDLE, SLEEP and their timers), its
// count in the sector count register.
NewtonErr
TATASimple::SetPowerMode(UByte command, UByte count, UByte drive)
{
	if (fLastDrive < drive)
		return kError_Bad_Parameters;
	TATARegCommandBlock reg;
	memset(&reg, 0, sizeof(reg));		// (the ROM's: whatever was on the stack)
	reg.fSectorCount = count;
	reg.fDriveHead = ATADriveBit(drive);
	reg.fCommand = command;
	reg.fField0C = 0;
	return DoATARegCommand(&reg);
}


// ROM 0x00026660 SetFeatures__10TATASimpleFUcN21
NewtonErr
TATASimple::SetFeatures(UByte feature, UByte value, UByte drive)
{
	if (fLastDrive < drive)
		return kError_Bad_Parameters;
	TATARegCommandBlock reg;
	memset(&reg, 0, sizeof(reg));		// (the ROM's: whatever was on the stack)
	reg.fFeatures = feature;
	reg.fSectorCount = value;
	reg.fDriveHead = ATADriveBit(drive);
	reg.fCommand = kATACmdSetFeatures;
	reg.fField0C = 0;
	return DoATARegCommand(&reg);
}


// ROM 0x000266c4 SetMultipleMode__10TATASimpleFUcT1
NewtonErr
TATASimple::SetMultipleMode(UByte count, UByte drive)
{
	if (fLastDrive < drive)
		return kError_Bad_Parameters;
	TATARegCommandBlock reg;
	memset(&reg, 0, sizeof(reg));		// (the ROM's: whatever was on the stack)
	reg.fSectorCount = count;
	reg.fDriveHead = ATADriveBit(drive);
	reg.fCommand = kATACmdSetMultipleMode;
	reg.fField0C = 0;
	return DoATARegCommand(&reg);
}


// ROM 0x00026720 InitDriveParam__10TATASimpleFUcN21
// INITIALIZE DRIVE PARAMETERS: sectors per track, and the heads less one.
NewtonErr
TATASimple::InitDriveParam(UByte sectors, UByte heads, UByte drive)
{
	if (fLastDrive < drive)
		return kError_Bad_Parameters;
	TATARegCommandBlock reg;
	memset(&reg, 0, sizeof(reg));		// (the ROM's: whatever was on the stack)
	reg.fSectorCount = sectors;
	reg.fDriveHead = (UByte) (ATADriveBit(drive) | (heads & 0x0F));
	reg.fCommand = kATACmdInitDriveParams;
	reg.fField0C = 0;
	return DoATARegCommand(&reg);
}


// ROM 0x00026788 DoATALBACommand__10TATASimpleFP19TATALBACommandBlock
// A transfer of blocks, up to 255 to a command: each block number made the
// drive's address - a logical one when the drive takes them (its
// capabilities' bit 9), else a cylinder, head and sector out of its current
// geometry - and the block after the last the drive says it was at the
// next to start from.  The drive is identified first if it has not been.
// ROM QUIRK: after a command that failed, the blocks done are counted up
// by the sector count register as the drive left it, not by what was asked.
NewtonErr
TATASimple::DoATALBACommand(TATALBACommandBlock* block)
{
	UByte drive = block->fDrive;
	if (fLastDrive < drive)
		return kError_Bad_Parameters;
	ULong next = block->fBlock;
	block->fDone = 0;
	block->fCurrentBlock = next;
	block->fStatusMask = 0x50;
	block->fField1C = 0;
	TATADriveBasicInfo* info = &fDriveInfo[drive];
	NewtonErr err;
	if ((fFlags & kIdentified) == 0)
	{
		TATADriveInfo identity;
		if ((err = IdentifyDrive(&identity, drive)) != noErr)
			return err;
		fFlags |= kIdentified;
	}
	ULong left;
	do
	{
		TATARegCommandBlock reg;
		reg.fDriveAddress = 0;		// (the ROM's are whatever was on the stack)
		reg.fField10 = 0;
		reg.fField14 = 0;
		if (GetBigEndianHalf(&info->fCapabilities) & 0x0200)
		{
			reg.fSectorNumber = (UByte) next;
			reg.fCylinderLow = (UByte) (next >> 8);
			reg.fCylinderHigh = (UByte) (next >> 16);
			reg.fDriveHead = (UByte) (((next >> 24) & 0x0F) | ATADriveBit(block->fDrive) | 0xE0);
		}
		else
		{
			ULong sectors = GetBigEndianHalf(&info->fCurrentSectorsPerTrack);
			ULong heads = GetBigEndianHalf(&info->fCurrentHeads);
			ULong cylinder = next / (sectors * heads);
			reg.fCylinderLow = (UByte) cylinder;
			reg.fCylinderHigh = (UByte) (cylinder >> 8);
			reg.fSectorNumber = (UByte) (next % sectors + 1);
			ULong head = (next / sectors) % heads;
			reg.fDriveHead = (UByte) (head | ATADriveBit(block->fDrive) | 0xA0);
		}
		reg.fField0C = block->fField14;
		reg.fFeatures = block->fFeatures;
		reg.fCommand = block->fCommand;
		ULong count = block->fCount <= 0xFF ? block->fCount : 0xFF;
		reg.fSectorCount = (UByte) count;
		reg.fBuffer = block->fBuffer + (block->fDone << 9);
		err = DoATARegCommand(&reg);
		block->fDone += (err != noErr) ? reg.fSectorCount : count;
		left = block->fCount - count;
		block->fCount = left;
		if (GetBigEndianHalf(&info->fCapabilities) & 0x0200)
			block->fCurrentBlock = ((ULong) (reg.fDriveHead & 0x0F) << 24) + ((ULong) reg.fCylinderHigh << 16)
								 + ((ULong) reg.fCylinderLow << 8) + reg.fSectorNumber;
		else
			block->fCurrentBlock = ((reg.fCylinderLow + ((ULong) reg.fCylinderHigh << 8)) * GetBigEndianHalf(&info->fCurrentHeads)
								 + (reg.fDriveHead & 0x0F)) * GetBigEndianHalf(&info->fCurrentSectorsPerTrack) + reg.fSectorNumber - 1;
		next = block->fCurrentBlock + 1;
	}
	while (err == noErr && left > 0);
	return err;
}


// ROM 0x000269dc DoATARegCommand__10TATASimpleFP19TATARegCommandBlock
// One command through the task file, the card powered for it (Vpp if the
// card wants 12 V, else Vcc) and the socket's control bits put back
// afterwards: wait for the drive to be ready, write the registers, move
// the data a sector at a time for a command that has any (each sector
// waiting for DRQ), wait for ready again and read the registers back into
// the block.  IDENTIFY DRIVE's answer is turned into the processor's order
// and its first 0x78 bytes kept; SET MULTIPLE MODE's count is kept too.
// Any exception on the way (a bus error: the card gone) is -10059.
// ROM QUIRK: the first wait looks at the status register before a command
// is written, and a drive keeps the last command's ERR bit there until the
// next one is - so after a command fails, every command fails with the
// same error until the drive is reset.
NewtonErr
TATASimple::DoATARegCommand(TATARegCommandBlock* block)
{
	NewtonErr err = noErr;
	ULong control = fSocket->GetControl();
	if (fFlags & kVppPower)
		VppOn(fSocket->SocketNumber(), false);
	else
		VccOn(fSocket->SocketNumber(), false);
	ULong drive = (block->fDriveHead & 0x10) >> 4;
	if (fLastDrive < drive)
		err = kError_Bad_Parameters;
	else
	{
		newton_try
		{
			TATARegisters* regs = fRegisters;
			UByte command = block->fCommand;
			if (command == kATACmdIdentifyDrive || command == kATACmdReadLong
			 || command == kATACmdWriteLong || command == kATACmdFormatTrack)
				block->fSectorCount = 1;
			if ((err = WaitFor(kATAStatusReady, regs)) == noErr)
			{
				CardBusWriteByte(regs + kATARegError, block->fFeatures);
				CardBusWriteByte(regs + kATARegSectorCount, block->fSectorCount);
				CardBusWriteByte(regs + kATARegSectorNumber, block->fSectorNumber);
				CardBusWriteByte(regs + kATARegCylinderLow, block->fCylinderLow);
				CardBusWriteByte(regs + kATARegCylinderHigh, block->fCylinderHigh);
				CardBusWriteByte(regs + kATARegDriveHead, block->fDriveHead);
				CardBusWriteByte(regs + kATARegCommand, block->fCommand);
				Boolean dataIn = false;
				Boolean data = false;
				switch (block->fCommand)
				{
				case kATACmdReadSectors:
				case kATACmdReadLong:
				case kATACmdReadMultiple:
				case kATACmdReadBuffer:
				case kATACmdIdentifyDrive:
					dataIn = true;
					data = true;
					break;
				case kATACmdWriteSectors:
				case kATACmdWriteLong:
				case kATACmdWriteVerify:
				case kATACmdFormatTrack:
				case kATACmdWriteMultiple:
				case kATACmdWriteBuffer:
					data = true;
					break;
				case kATACmdSetMultipleMode:
					PutBigEndianHalf(&fDriveInfo[drive].fMultiple, block->fSectorCount);
					break;
				}
				if (data)
				{
					ULong sectors = block->fSectorCount;
					if (sectors == 0)
						sectors = 0x100;
					ULong bytes = 0x200;
					if (block->fCommand == kATACmdWriteLong)
						bytes = GetBigEndianHalf(&fDriveInfo[drive].fECCBytes) + 0x200;
					// the data register: the ROM's (registers + 3) & ~1
					volatile UByte* port = (volatile UByte*) (((uintptr_t) regs + 3) & ~(uintptr_t) 1);
					ULong words = bytes >> 1;
					UByte* p = block->fBuffer;
					fSocket->SetControl(control & ~0x1A);
					for (ULong sector = 0; sector < sectors && err == noErr; sector++)
					{
						if ((err = WaitFor(kATAStatusDRQ, regs)) != noErr)
							break;
						if (dataIn)
							for (ULong i = 0; i < words; i++)
							{
								ULong32 w = CardBusReadWord(port);
								*p++ = (UByte) (w >> 16);
								*p++ = (UByte) (w >> 24);
							}
						else
							for (ULong i = 0; i < words; i++)
							{
								ULong32 w = p[0] + ((ULong32) p[1] << 8);
								p += 2;
								CardBusWriteWord(port, w);
							}
					}
				}
				if (err == noErr && (err = WaitFor(kATAStatusReady, regs)) == noErr)
				{
					if (block->fCommand == kATACmdIdentifyDrive)
					{
						SwapDriveInfoBytes((TATADriveBasicInfo*) block->fBuffer);
						memcpy(&fDriveInfo[drive], block->fBuffer, sizeof(TATADriveBasicInfo));
					}
					block->fFeatures = CardBusReadByte(regs + kATARegError);
					block->fSectorCount = CardBusReadByte(regs + kATARegSectorCount);
					block->fSectorNumber = CardBusReadByte(regs + kATARegSectorNumber);
					block->fCylinderLow = CardBusReadByte(regs + kATARegCylinderLow);
					block->fCylinderHigh = CardBusReadByte(regs + kATARegCylinderHigh);
					block->fDriveHead = CardBusReadByte(regs + kATARegDriveHead);
					block->fCommand = CardBusReadByte(regs + kATARegCommand);
					block->fDriveAddress = CardBusReadByte(regs + kATARegDriveAddress);
					if (block->fCommand & kATAStatusError)
						err = CheckError(block->fFeatures);
				}
			}
		}
		newton_catch_all
		{
			err = kError_Bus_Access;
		}
		end_try;
	}
	if (fFlags & kVppPower)
		VppOff(fSocket->SocketNumber());
	else
		VccOff(fSocket->SocketNumber());
	fSocket->SetControl(control);
	return err;
}


// ROM 0x00026e14 SetDeviceControlReg__10TATASimpleFUc
void
TATASimple::SetDeviceControlReg(UByte value)
{
	CardBusWriteByte(fRegisters + kATARegDeviceControl, value);
	fDeviceControl = value;
}


// ROM 0x00026e98 ATASpecific__10TATASimpleFUlPvT1
NewtonErr
TATASimple::ATASpecific(ULong selector, void* data, ULong size)
{
	return noErr;
}


// ROM 0x00026f90 Initialize__10TATASimpleFP11TCardSocketP11TCardPCMCIAUl
// The card put in the configuration asked for (its index written to the
// configuration option register), its task file found - in common memory
// or in I/O space, as the configuration's interface says - and each of
// its drives identified.
NewtonErr
TATASimple::Initialize(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber)
{
	NewtonErr err = noErr;
	fSocket = socket;
	TCardConfiguration* config = card->GetCardConfiguration(configNumber);
	fInterfaceType = config->fInterfaceType;
	fConfigIndex = config->fConfigurationNumber;
	fConfigRegister = (UByte*) socket->AttributeMemBaseAddr() + card->fRegisterBaseAddress;
	CardBusWriteByte(fConfigRegister + 3, fConfigIndex);
	if (fInterfaceType == 0)
		fRegisters = (TATARegisters*) socket->CommonMemBaseAddr() + config->fMemAddresses[0];
	else
		fRegisters = (TATARegisters*) socket->IOBaseAddr() + config->fIoAddresses[0];
	fDataWindow = (UByte*) socket->CommonMemBaseAddr() + 0x400;
	if (config->fVpp1[0] == kTwelveVolts)
		fFlags |= kVppPower;
	UByte drive = 0;
	while (fLastDrive >= drive)
	{
		TATADriveInfo identity;
		err = IdentifyDrive(&identity, drive);
		drive++;
		if (err != noErr)
			return err;
	}
	fFlags |= kIdentified;
	if (fInterfaceType == 1)
		socket->SelectIOInterface();
	return err;
}


// ROM 0x000270ac SuspendService__10TATASimpleFv
NewtonErr
TATASimple::SuspendService(void)
{
	return noErr;
}


// ROM 0x000270b4 ResumeService__10TATASimpleFP11TCardSocketP11TCardPCMCIAUl
NewtonErr
TATASimple::ResumeService(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber)
{
	return noErr;
}


// ROM 0x00026e28 SwapDriveInfoBytes__10TATASimpleFP18TATADriveBasicInfo
// The drive's little-endian words made the processor's, and the current
// capacity (two words, low first) one long.
void
TATASimple::SwapDriveInfoBytes(TATADriveBasicInfo* info)
{
	UByte* p = (UByte*) info;
	SwapShorts(p, 0x72);
	UByte b = p[0x72];
	p[0x72] = p[0x75];
	p[0x75] = b;
	b = p[0x73];
	p[0x73] = p[0x74];
	p[0x74] = b;
}


// ROM 0x00026e64 SwapShorts__10TATASimpleFPUcUl
// Each pair of bytes swapped (the words made big-endian).
void
TATASimple::SwapShorts(UByte* bytes, ULong count)
{
	for (ULong i = 0; i < count / 2; i++, bytes += 2)
	{
		UByte b = bytes[0];
		bytes[0] = bytes[1];
		bytes[1] = b;
	}
}


// ROM 0x00026ea0 WaitFor__10TATASimpleFUcP13TATARegisters
// The status register polled, for up to three seconds, until the drive is
// not busy and shows one of the bits in mask, or shows an error.  ==> the
// error register's meaning, kError_ATA_Busy, or - the bit never came -
// kError_ATA_Not_Ready for DRDY and kError_ATA_No_DRQ for anything else.
NewtonErr
TATASimple::WaitFor(UByte mask, TATARegisters* registers)
{
	NewtonErr err = noErr;
	TDelayTimer timer;
	timer.ResetTimeOut(kATAWaitTimeout);
	UByte status;
	for ( ; ; )
	{
		status = CardBusReadByte(registers + kATARegCommand);
		if (timer.TimedOut() || (status & kATAStatusError) != 0)
			break;
		if ((status & kATAStatusBusy) == 0 && (status & mask) != 0)
			break;
	}
	if (status & kATAStatusError)
		err = CheckError(CardBusReadByte(registers + kATARegError));
	else if (status & kATAStatusBusy)
		err = kError_ATA_Busy;
	else if ((status & mask) == 0)
		err = mask == kATAStatusReady ? kError_ATA_Not_Ready : kError_ATA_No_DRQ;
	return err;
}


// ROM 0x00026f58 CheckError__10TATASimpleFUc
// The error register's lowest bit set, as an error: AMNF, TK0NF, ABRT, MCR,
// IDNF, MC, UNC, BBK; kError_ATA_Unknown_Error for none.
NewtonErr
TATASimple::CheckError(UByte error)
{
	NewtonErr err = kError_ATA_Address_Mark_Not_Found;
	for (ULong i = 0; i < 8; i++)
	{
		if (error & 1)
			return err;
		error >>= 1;
		err--;
	}
	return err;
}
