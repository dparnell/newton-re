/*
	File:		hal/host/HostATA.cpp

	Contains:	The host's card bus (hal/CardBus.h) and, behind it, a model
				of an ATA card's task file: an ATA card's register window
				(hal/host/HostCard.h, the first 0x800 bytes of its common
				memory) answers as a CompactFlash card in memory mode does,
				the disk being the card image's data section.

				The registers are where the MessagePad's bus puts them (a
				card byte at address a is at a ^ 3): register r at window
				offset r ^ 3, the 16-bit data register the word access at
				offsets 0-3 (its first byte, D0-D7, at 3) and again at
				0x400-0x7FF.  A word access there moves one data word; a
				byte access at offset 2 is the error/features register, as
				on a card, where the access width tells them apart.

				The drive does a command the moment it is written, so it is
				never busy: READ SECTORS (0x20/0x21, READ LONG and READ
				MULTIPLE alike, without the ECC bytes), WRITE SECTORS
				(0x30/0x31, WRITE VERIFY, WRITE MULTIPLE), IDENTIFY DRIVE,
				READ/WRITE BUFFER, the power commands (CHECK POWER MODE
				answers 0xFF, active), SET FEATURES, SET MULTIPLE MODE,
				INITIALIZE DRIVE PARAMETERS, RECALIBRATE, SEEK and READ
				VERIFY; anything else is aborted.  After a transfer the
				address registers say the last sector moved and the count
				is nought.  The identity it gives: a CompactFlash card
				("Newton host ATA card") taking logical block addresses,
				4 heads and 32 sectors to a track.  Only drive 0 is there.

	Written by:	the reconstruction (DEVIATION: a card is hardware)
*/

#include "CardBus.h"
#include "HostCard.h"
#include "OSErrors.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

namespace
{
	enum
	{
		kStatusError = 0x01, kStatusDRQ = 0x08, kStatusSeekDone = 0x10, kStatusReady = 0x40,
		kErrorAborted = 0x04, kErrorIDNotFound = 0x10
	};
	const ULong	kHeads = 4;
	const ULong	kSectorsPerTrack = 32;

	struct HostATADrive
	{
		UByte		fFeatures;
		UByte		fError;
		UByte		fCount;
		UByte		fSector;
		UByte		fCylinderLow;
		UByte		fCylinderHigh;
		UByte		fDriveHead;
		UByte		fStatus;
		UByte		fDeviceControl;
		UByte		fCommand;
		UByte		fBuffer[512];
		ULong		fIndex;			// the next byte of fBuffer the data register moves
		ULong		fLeft;			// sectors still to move
		ULong		fSector32;		// the sector being moved
		Boolean		fIn;			// data from the drive
		Boolean		fOut;			// data to the drive
		Boolean		fToDisk;		// the data out is a sector (not WRITE BUFFER)
		Boolean		fTrace;
		Boolean		fTraceRegisters;	// NEWTON_TRACE_ATA=2: every register access but the data's
		Boolean		fINTRQ;			// the interrupt request, before nIEN
		UByte		fConfigOption;	// the configuration option register (COR)
		UByte		fSocketCopy;	// the socket and copy register
	};
	HostATADrive	gDrives[kHostCardSockets];
	Boolean			gStarted[kHostCardSockets];

	void
	Reset(HostATADrive* d)
	{
		UByte control = d->fDeviceControl;
		UByte option = d->fConfigOption, copy = d->fSocketCopy;
		memset(d, 0, sizeof(HostATADrive));
		d->fDeviceControl = control & ~4;
		d->fConfigOption = option;
		d->fSocketCopy = copy;
		d->fError = 1;				// diagnostics passed
		d->fCount = 1;
		d->fSector = 1;
		d->fStatus = kStatusReady | kStatusSeekDone;
		const char* trace = getenv("NEWTON_TRACE_ATA");
		d->fTrace = trace != nil;
		d->fTraceRegisters = trace != nil && trace[0] == '2';
	}

	HostATADrive*
	Drive(ULong socket)
	{
		if (!gStarted[socket])
		{
			Reset(&gDrives[socket]);
			gStarted[socket] = true;
		}
		return &gDrives[socket];
	}

	ULong
	AddressOf(HostATADrive* d)
	{
		if (d->fDriveHead & 0x40)
			return ((ULong) (d->fDriveHead & 0x0F) << 24) | ((ULong) d->fCylinderHigh << 16)
				 | ((ULong) d->fCylinderLow << 8) | d->fSector;
		ULong cylinder = d->fCylinderLow | ((ULong) d->fCylinderHigh << 8);
		return (cylinder * kHeads + (d->fDriveHead & 0x0F)) * kSectorsPerTrack + d->fSector - 1;
	}

	void	DoCommand(ULong socket, HostATADrive* d, UByte command);

	// INTRQ asserted (the socket told unless nIEN masks it)
	void
	Interrupt(ULong socket, HostATADrive* d)
	{
		d->fINTRQ = true;
		if ((d->fDeviceControl & 2) == 0)
			HostCardSocketIREQ(socket);
	}

	void
	SetAddress(HostATADrive* d, ULong sector)
	{
		if (d->fDriveHead & 0x40)
		{
			d->fSector = (UByte) sector;
			d->fCylinderLow = (UByte) (sector >> 8);
			d->fCylinderHigh = (UByte) (sector >> 16);
			d->fDriveHead = (UByte) ((d->fDriveHead & 0xF0) | ((sector >> 24) & 0x0F));
		}
		else
		{
			ULong cylinder = sector / (kHeads * kSectorsPerTrack);
			d->fCylinderLow = (UByte) cylinder;
			d->fCylinderHigh = (UByte) (cylinder >> 8);
			d->fDriveHead = (UByte) ((d->fDriveHead & 0xF0) | ((sector / kSectorsPerTrack) % kHeads));
			d->fSector = (UByte) (sector % kSectorsPerTrack + 1);
		}
	}

	void
	Fail(HostATADrive* d, UByte error)
	{
		d->fError = error;
		d->fStatus = kStatusReady | kStatusSeekDone | kStatusError;
		d->fIn = d->fOut = false;
		d->fLeft = 0;
	}

	void
	Done(HostATADrive* d)
	{
		d->fStatus = kStatusReady | kStatusSeekDone;
		d->fIn = d->fOut = false;
	}

	// an ATA string: two characters to a word, the first in the high byte
	void
	PutString(UByte* words, ULong length, const char* s)
	{
		for (ULong i = 0; i < length; i++)
		{
			char c = *s != 0 ? *s++ : ' ';
			words[i ^ 1] = (UByte) c;
		}
	}

	void
	PutWord(UByte* buffer, ULong word, ULong value)
	{
		buffer[word * 2] = (UByte) value;
		buffer[word * 2 + 1] = (UByte) (value >> 8);
	}

	void
	Identify(ULong socket, HostATADrive* d)
	{
		UByte* b = d->fBuffer;
		memset(b, 0, 512);
		ULong sectors = HostCardATASectors(socket);
		ULong cylinders = sectors / (kHeads * kSectorsPerTrack);
		if (cylinders > 0xFFFF)
			cylinders = 0xFFFF;
		PutWord(b, 0, 0x848A);				// CompactFlash, removable
		PutWord(b, 1, cylinders);
		PutWord(b, 3, kHeads);
		PutWord(b, 6, kSectorsPerTrack);
		PutWord(b, 7, sectors >> 16);		// CF: the sectors on the card
		PutWord(b, 8, sectors & 0xFFFF);
		PutString(b + 20, 20, "HOSTATA1");
		PutWord(b, 20, 2);					// a dual-ported buffer
		PutWord(b, 21, 2);
		PutWord(b, 22, 4);					// ECC bytes on a long transfer
		PutString(b + 46, 8, "1.0");
		PutString(b + 54, 40, "Newton host ATA card");
		PutWord(b, 47, 0x8001);				// one sector to a READ MULTIPLE
		PutWord(b, 49, 0x0200);				// logical block addresses
		PutWord(b, 51, 0x0200);
		PutWord(b, 53, 0x0001);				// words 54-58 are good
		PutWord(b, 54, cylinders);
		PutWord(b, 55, kHeads);
		PutWord(b, 56, kSectorsPerTrack);
		ULong capacity = cylinders * kHeads * kSectorsPerTrack;
		PutWord(b, 57, capacity & 0xFFFF);
		PutWord(b, 58, capacity >> 16);
		PutWord(b, 60, sectors & 0xFFFF);
		PutWord(b, 61, sectors >> 16);
	}

	void
	Command(ULong socket, HostATADrive* d, UByte command)
	{
		DoCommand(socket, d, command);
		// a command interrupts when it is done or its first data is ready;
		// one waiting for data from the host interrupts when it has it
		if (!d->fOut)
			Interrupt(socket, d);
	}

	void
	DoCommand(ULong socket, HostATADrive* d, UByte command)
	{
		d->fINTRQ = false;
		d->fCommand = command;
		if (d->fTrace)
			fprintf(stderr, "[ata] %lu: command %02x count %u address %ld\n", (unsigned long) socket,
					command, d->fCount, (long) (Long32) AddressOf(d));
		if (d->fDriveHead & 0x10)
			return;							// no drive 1: nobody answers
		ULong count = d->fCount != 0 ? d->fCount : 256;
		switch (command)
		{
		case 0x20: case 0x21: case 0x22: case 0x23: case 0xC4:		// READ SECTORS, READ LONG, READ MULTIPLE
		{
			ULong sector = AddressOf(d);
			if (sector + count > HostCardATASectors(socket))
			{
				Fail(d, kErrorIDNotFound);
				return;
			}
			if (HostCardATARead(socket, sector, d->fBuffer) != noErr)
			{
				Fail(d, kErrorAborted);
				return;
			}
			d->fSector32 = sector;
			d->fLeft = count;
			d->fIndex = 0;
			d->fIn = true;
			d->fStatus = kStatusReady | kStatusSeekDone | kStatusDRQ;
			break;
		}
		case 0x30: case 0x31: case 0x32: case 0x33: case 0x3C: case 0xC5:	// WRITE SECTORS, WRITE LONG, WRITE VERIFY, WRITE MULTIPLE
		{
			ULong sector = AddressOf(d);
			if (sector + count > HostCardATASectors(socket))
			{
				Fail(d, kErrorIDNotFound);
				return;
			}
			if (HostCardIsWriteProtected(socket))
			{
				Fail(d, kErrorAborted);
				return;
			}
			d->fSector32 = sector;
			d->fLeft = count;
			d->fIndex = 0;
			d->fOut = true;
			d->fToDisk = true;
			d->fStatus = kStatusReady | kStatusSeekDone | kStatusDRQ;
			break;
		}
		case 0xEC:											// IDENTIFY DRIVE
			Identify(socket, d);
			d->fLeft = 1;
			d->fIndex = 0;
			d->fIn = true;
			d->fSector32 = AddressOf(d);
			d->fStatus = kStatusReady | kStatusSeekDone | kStatusDRQ;
			break;
		case 0xE4:											// READ BUFFER
			d->fLeft = 1;
			d->fIndex = 0;
			d->fIn = true;
			d->fSector32 = AddressOf(d);
			d->fStatus = kStatusReady | kStatusSeekDone | kStatusDRQ;
			break;
		case 0xE8:											// WRITE BUFFER
			d->fLeft = 1;
			d->fIndex = 0;
			d->fOut = true;
			d->fToDisk = false;
			d->fSector32 = AddressOf(d);
			d->fStatus = kStatusReady | kStatusSeekDone | kStatusDRQ;
			break;
		case 0x98: case 0xE5:								// CHECK POWER MODE
			d->fCount = 0xFF;
			Done(d);
			break;
		case 0x94: case 0x95: case 0x96: case 0x97: case 0x99:	// the old power commands
		case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE6:	// and the new
		case 0xEF: case 0xC6: case 0x91:					// SET FEATURES, SET MULTIPLE, INITIALIZE DRIVE PARAMETERS
		case 0x40: case 0x41: case 0x70:					// READ VERIFY, SEEK
		case 0x10:											// RECALIBRATE
			Done(d);
			break;
		default:
			Fail(d, kErrorAborted);
			break;
		}
	}

	// a sector's worth of data moved: the next one, or the command done
	void
	SectorMoved(ULong socket, HostATADrive* d)
	{
		if (d->fOut && d->fToDisk && HostCardATAWrite(socket, d->fSector32, d->fBuffer) != noErr)
		{
			Fail(d, kErrorAborted);
			return;
		}
		if (d->fCommand != 0xEC && d->fCommand != 0xE4 && d->fCommand != 0xE8)
			SetAddress(d, d->fSector32);
		d->fLeft--;
		d->fCount = (UByte) d->fLeft;
		d->fIndex = 0;
		Boolean out = d->fOut;
		if (d->fLeft == 0)
		{
			Done(d);
			if (out)
				Interrupt(socket, d);		// (a read's last sector taken is no interrupt)
			return;
		}
		d->fSector32++;
		if (d->fIn && HostCardATARead(socket, d->fSector32, d->fBuffer) != noErr)
			Fail(d, kErrorAborted);
		Interrupt(socket, d);				// the next sector ready, or wanted
	}

	UByte
	ReadData(ULong socket, HostATADrive* d)
	{
		if (!d->fIn)
			return 0xFF;
		UByte b = d->fBuffer[d->fIndex++];
		if (d->fIndex == 512)
			SectorMoved(socket, d);
		return b;
	}

	void
	WriteData(ULong socket, HostATADrive* d, UByte b)
	{
		if (!d->fOut)
			return;
		d->fBuffer[d->fIndex++] = b;
		if (d->fIndex == 512)
			SectorMoved(socket, d);
	}

	UByte	DoReadRegister(ULong socket, ULong offset);

	UByte
	ReadRegister(ULong socket, ULong offset)
	{
		UByte value = DoReadRegister(socket, offset);
		HostATADrive* d = Drive(socket);
		if (d->fTraceRegisters && (offset ^ 3) != 0)
			fprintf(stderr, "[ata] %lu: read %03lx -> %02x\n", (unsigned long) socket, (unsigned long) offset, value);
		return value;
	}

	UByte
	DoReadRegister(ULong socket, ULong offset)
	{
		HostATADrive* d = Drive(socket);
		switch (offset ^ 3)
		{
		case 0x0:	return ReadData(socket, d);
		case 0x1:	return d->fError;
		case 0x2:	return d->fCount;
		case 0x3:	return d->fSector;
		case 0x4:	return d->fCylinderLow;
		case 0x5:	return d->fCylinderHigh;
		case 0x6:	return d->fDriveHead;
		case 0x7:	d->fINTRQ = false;		// (reading the status acknowledges the interrupt)
					return (d->fDriveHead & 0x10) ? 0 : d->fStatus;
		case 0xE:	return (d->fDriveHead & 0x10) ? 0 : d->fStatus;
		case 0xF:	return (UByte) (0xC0 | ((~d->fDriveHead & 0x0F) << 2) | ((d->fDriveHead & 0x10) ? 1 : 2));
		}
		return 0xFF;
	}

	void
	WriteRegister(ULong socket, ULong offset, UByte value)
	{
		HostATADrive* d = Drive(socket);
		if (d->fTraceRegisters && (offset ^ 3) != 0)
			fprintf(stderr, "[ata] %lu: write %03lx <- %02x\n", (unsigned long) socket, (unsigned long) offset, value);
		switch (offset ^ 3)
		{
		case 0x0:	WriteData(socket, d, value); break;
		case 0x1:	d->fFeatures = value; break;
		case 0x2:	d->fCount = value; break;
		case 0x3:	d->fSector = value; break;
		case 0x4:	d->fCylinderLow = value; break;
		case 0x5:	d->fCylinderHigh = value; break;
		case 0x6:	d->fDriveHead = value; break;
		case 0x7:	Command(socket, d, value); break;
		case 0xE:
			if ((value & 4) != 0 && (d->fDeviceControl & 4) == 0)
			{
				d->fDeviceControl = value;
				Reset(d);
			}
			d->fDeviceControl = value;
			break;
		}
	}

	// The card's configuration registers in attribute memory, as a
	// CompactFlash card has them (the PC Card standard's): the option
	// register (bit 7 a soft reset, the low bits the interface chosen),
	// the configuration and status register (bit 1 the interrupt pending),
	// the pin replacement register (bit 1 ready - the model is never busy -
	// bit 0 write protect) and the socket and copy register.  The registers
	// sit on the card's even addresses; the attribute window's byte lanes
	// are swapped as its common memory's are, so card address a is window
	// offset a ^ 3.
	UByte
	ReadConfig(ULong socket, ULong offset)
	{
		HostATADrive* d = Drive(socket);
		UByte value = 0;
		switch ((offset ^ 3) - kHostCardATAConfigBase)
		{
		case 0:	value = d->fConfigOption; break;
		case 2:	value = d->fINTRQ ? 0x02 : 0x00; break;
		case 4:	value = (UByte) (0x02 | (HostCardIsWriteProtected(socket) ? 0x01 : 0x00)); break;
		case 6:	value = d->fSocketCopy; break;
		}
		if (d->fTraceRegisters)
			fprintf(stderr, "[ata] %lu: config read %03lx -> %02x\n", (unsigned long) socket, (unsigned long) (offset ^ 3), value);
		return value;
	}

	void
	WriteConfig(ULong socket, ULong offset, UByte value)
	{
		HostATADrive* d = Drive(socket);
		if (d->fTraceRegisters)
			fprintf(stderr, "[ata] %lu: config write %03lx <- %02x\n", (unsigned long) socket, (unsigned long) (offset ^ 3), value);
		switch ((offset ^ 3) - kHostCardATAConfigBase)
		{
		case 0:
			if ((value & 0x80) != 0)
			{
				Reset(d);				// (a soft reset: the drive starts afresh)
				d->fConfigOption = 0;
			}
			else
				d->fConfigOption = value;
			break;
		case 2:
			if ((value & 0x02) == 0)
				d->fINTRQ = false;
			break;
		case 6:	d->fSocketCopy = value; break;
		}
	}

	Boolean
	IsDataWord(ULong offset)
	{
		return offset < 4 || (offset >= 0x400 && offset < 0x800);
	}

	ULong32
	BigEndianAt(const volatile UByte* p)
	{
		return ((ULong32) p[0] << 24) | ((ULong32) p[1] << 16) | ((ULong32) p[2] << 8) | p[3];
	}
}


Boolean
HostATAInterrupt(ULong socket)
{
	if (socket >= kHostCardSockets || !gStarted[socket] || !HostCardIsATA(socket))
		return false;
	HostATADrive* d = &gDrives[socket];
	return d->fINTRQ && (d->fDeviceControl & 2) == 0;
}


// A card put in or taken out: its drive starts afresh (HostCard.cpp).
void
HostATACardChanged(ULong socket)
{
	if (socket < kHostCardSockets)
		gStarted[socket] = false;
}


UByte
CardBusReadByte(volatile void* address)
{
	ULong socket, offset;
	if (HostCardATAWindow(address, &socket, &offset))
		return ReadRegister(socket, offset);
	if (HostCardATAAttribute(address, &socket, &offset))
		return ReadConfig(socket, offset);
	return *(volatile UByte*) address;
}


void
CardBusWriteByte(volatile void* address, UByte value)
{
	ULong socket, offset;
	if (HostCardATAWindow(address, &socket, &offset))
		WriteRegister(socket, offset, value);
	else if (HostCardATAAttribute(address, &socket, &offset))
		WriteConfig(socket, offset, value);
	else
		*(volatile UByte*) address = value;
}


// The aligned word, rotated as an ARM `ldr` at an unaligned address
// rotates it.  In an ATA card's data lanes the word's low half is the
// next data word (its first byte at 3).
ULong32
CardBusReadWord(volatile void* address)
{
	uintptr_t a = (uintptr_t) address;
	volatile UByte* aligned = (volatile UByte*) (a & ~(uintptr_t) 3);
	ULong32 w;
	ULong socket, offset;
	if (HostCardATAWindow(aligned, &socket, &offset) && IsDataWord(offset))
	{
		HostATADrive* d = Drive(socket);
		UByte first = ReadData(socket, d);
		UByte second = ReadData(socket, d);
		w = ((ULong32) second << 8) | first;
	}
	else if (HostCardATAWindow(aligned, &socket, &offset))
		w = ((ULong32) ReadRegister(socket, offset) << 24) | ((ULong32) ReadRegister(socket, offset + 1) << 16)
		  | ((ULong32) ReadRegister(socket, offset + 2) << 8) | ReadRegister(socket, offset + 3);
	else
		w = BigEndianAt(aligned);
	ULong rotate = (ULong) (a & 3) * 8;
	return rotate != 0 ? (w >> rotate) | (w << (32 - rotate)) : w;
}


// The whole aligned word written, whatever the address's low bits (an ARM
// `str`); in an ATA card's data lanes its low half is one data word.
void
CardBusWriteWord(volatile void* address, ULong32 value)
{
	uintptr_t a = (uintptr_t) address;
	volatile UByte* aligned = (volatile UByte*) (a & ~(uintptr_t) 3);
	ULong socket, offset;
	if (HostCardATAWindow(aligned, &socket, &offset) && IsDataWord(offset))
	{
		HostATADrive* d = Drive(socket);
		WriteData(socket, d, (UByte) value);
		WriteData(socket, d, (UByte) (value >> 8));
	}
	else if (HostCardATAWindow(aligned, &socket, &offset))
	{
		for (ULong i = 0; i < 4; i++)
			WriteRegister(socket, offset + i, (UByte) (value >> (24 - 8 * i)));
	}
	else
	{
		aligned[0] = (UByte) (value >> 24);
		aligned[1] = (UByte) (value >> 16);
		aligned[2] = (UByte) (value >> 8);
		aligned[3] = (UByte) value;
	}
}
