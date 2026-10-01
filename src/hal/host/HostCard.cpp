/*
	File:		hal/host/HostCard.cpp

	Contains:	A PC card on a host (HostCard.h).

	Written by:	the reconstruction (DEVIATION: a card is hardware)
*/

#include "HostCard.h"
#include "OSErrors.h"
#include "NewtErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void	HostATACardChanged(ULong socket);		// HostATA.cpp: the drive starts afresh

namespace
{
	struct HostCardState
	{
		Boolean			fInserted;
		Boolean			fReadOnly;
		FILE*			fFile;
		ULong			fDataStart;		// the common memory's place in the file
		unsigned char*	fCommon;
		ULong			fCommonSize;
		unsigned char*	fAttribute;		// kHostCardAttrSize bytes
		ULong			fCISSize;
		ULong			fType;
		char			fName[64];
		Boolean			fATA;			// fFile is the disk (kept open, read-only or not)
		ULong			fSectors;
	};

	HostCardState		gCards[kHostCardSockets];
	HostCardChangeProc	gChangeProc = nil;
	Boolean				gExitRegistered = false;

	ULong
	GetWord(const unsigned char* p)
	{
		return ((ULong) p[0] << 24) | ((ULong) p[1] << 16) | ((ULong) p[2] << 8) | p[3];
	}

	void
	PutWord(unsigned char* p, ULong w)
	{
		p[0] = (unsigned char) (w >> 24);
		p[1] = (unsigned char) (w >> 16);
		p[2] = (unsigned char) (w >> 8);
		p[3] = (unsigned char) w;
	}

	HostCardState*
	Card(ULong socket)
	{
		return socket < kHostCardSockets && gCards[socket].fInserted ? &gCards[socket] : nil;
	}

	void
	WriteThrough(HostCardState* card, ULong offset, ULong size)
	{
		if (card->fFile == nil)
			return;
		fseek(card->fFile, (long) (card->fDataStart + offset), SEEK_SET);
		fwrite(card->fCommon + offset, 1, size, card->fFile);
	}

	void
	FlushAtExit(void)
	{
		for (ULong i = 0; i < kHostCardSockets; i++)
			HostCardFlush(i);
	}

	// A socket's windows onto its card: at the same host addresses for
	// every card put in it, as a socket's windows are at fixed addresses
	// on the machine - a store that has a card's memory mapped goes on
	// seeing it there when the same card comes back.  Kept (with what the
	// last card left in them) while the socket is empty, and grown when a
	// bigger card goes in.  Given back when the program ends.
	struct HostCardWindows
	{
		unsigned char*	fCommon;
		ULong			fCommonSize;
		unsigned char*	fAttribute;
	};
	HostCardWindows	gWindows[kHostCardSockets];

	void
	FreeWindows(void)
	{
		for (ULong i = 0; i < kHostCardSockets; i++)
		{
			free(gWindows[i].fCommon);
			free(gWindows[i].fAttribute);
			gWindows[i].fCommon = gWindows[i].fAttribute = nil;
			gWindows[i].fCommonSize = 0;
		}
	}

	Boolean
	GetWindows(ULong socket, ULong commonSize)
	{
		HostCardWindows* w = &gWindows[socket];
		static Boolean registered = false;
		if (!registered)
		{
			registered = true;
			atexit(FreeWindows);
		}
		if (w->fCommonSize < commonSize)
		{
			unsigned char* common = (unsigned char*) realloc(w->fCommon, commonSize);
			if (common == nil)
				return false;
			w->fCommon = common;
			w->fCommonSize = commonSize;
		}
		if (w->fAttribute == nil && (w->fAttribute = (unsigned char*) malloc(kHostCardAttrSize)) == nil)
			return false;
		memset(w->fAttribute, 0, kHostCardAttrSize);
		return true;
	}

	void
	Changed(ULong socket)
	{
		HostATACardChanged(socket);
		if (gChangeProc != nil)
			gChangeProc(socket, gCards[socket].fInserted);
	}
}


NewtonErr
HostCardCreate(const char* path, ULong sizeMB, const char* name)
{
	if (sizeMB < 2 || sizeMB > 64 || (sizeMB & 1) != 0)
		return kError_Bad_Parameters;
	if (name == nil)
		name = "";
	FILE* f = fopen(path, "wb");
	if (f == nil)
		return kError_Bad_Parameters;
	ULong dataSize = sizeMB << 20;
	unsigned char* blank = (unsigned char*) malloc(0x10000);
	if (blank == nil)
	{
		fclose(f);
		return kError_No_Memory;
	}
	memset(blank, 0xFF, 0x10000);
	for (ULong done = 0; done < dataSize; done += 0x10000)
		fwrite(blank, 1, 0x10000, f);
	free(blank);

	// the CIS, in its natural order
	unsigned char cis[160];
	ULong n = 0;
	cis[n++] = 0x01;						// CISTPL_DEVICE
	cis[n++] = 3;
	cis[n++] = (5 << 4) | 3;				//   DTYPE_FLASH, 150 ns
	cis[n++] = (unsigned char) (((sizeMB / 2 - 1) << 3) | 6);	//   units of 2 MB, less one
	cis[n++] = 0xFF;						//   end of the device list
	cis[n++] = 0x18;						// CISTPL_JEDEC_C
	cis[n++] = 2;
	cis[n++] = 0x89;						//   Intel
	cis[n++] = 0xA0;						//   28F016SA (Series 2)
	cis[n++] = 0x1E;						// CISTPL_DEVICE_GEO
	cis[n++] = 6;
	cis[n++] = 2;							//   a 16-bit bus (2^(n-1) bytes)
	cis[n++] = 0x11;						//   64 KB erase blocks
	cis[n++] = 1;							//   reads,
	cis[n++] = 1;							//   writes a byte at a time
	cis[n++] = 1;							//   one block to a partition
	cis[n++] = 1;							//   no interleave
	const char* vendor = "Newton host";
	const char* product = name[0] != 0 ? name : "Flash card";
	ULong vendorLength = (ULong) strlen(vendor) + 1;
	ULong productLength = (ULong) strlen(product) + 1;
	if (productLength > 100)
		productLength = 100;
	cis[n++] = 0x15;						// CISTPL_VERS_1
	cis[n++] = (unsigned char) (2 + vendorLength + productLength + 1);
	cis[n++] = 4;							//   PC Card standard 4.1
	cis[n++] = 1;
	memcpy(&cis[n], vendor, vendorLength);
	n += vendorLength;
	memcpy(&cis[n], product, productLength - 1);
	n += productLength - 1;
	cis[n++] = 0;
	cis[n++] = 0xFF;
	cis[n++] = 0xFF;						// CISTPL_END
	ULong cisStart = dataSize;
	fwrite(cis, 1, n, f);

	ULong nameStart = cisStart + n;
	ULong nameSize = (ULong) strlen(name) + 1;
	fwrite(name, 1, nameSize, f);

	unsigned char info[kHostCardImageInfoSize];
	memset(info, 0, sizeof(info));
	PutWord(info + 0, nameSize);
	PutWord(info + 4, nameStart);
	PutWord(info + 8, 0);					// no icon
	PutWord(info + 12, nameStart);
	PutWord(info + 16, n);
	PutWord(info + 20, cisStart);
	PutWord(info + 24, dataSize);
	PutWord(info + 28, 0);
	PutWord(info + 32, 5);					// the type: flash
	PutWord(info + 36, 1);					// the version
	memcpy(info + 40, "TLinearCard", 12);
	fwrite(info, 1, sizeof(info), f);
	Boolean ok = ferror(f) == 0;
	fclose(f);
	return ok ? noErr : kError_Bad_Parameters;
}


NewtonErr
HostCardInsert(ULong socket, const char* path, Boolean readOnly)
{
	if (socket >= kHostCardSockets)
		return kError_Bad_Parameters;
	HostCardRemove(socket);
	HostCardState* card = &gCards[socket];
	FILE* f = readOnly ? nil : fopen(path, "r+b");
	if (f == nil)
	{
		f = fopen(path, "rb");
		readOnly = true;
	}
	if (f == nil)
		return kError_Bad_Parameters;
	unsigned char info[kHostCardImageInfoSize];
	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	if (fileSize < (long) sizeof(info) || fseek(f, fileSize - (long) sizeof(info), SEEK_SET) != 0
	 || fread(info, 1, sizeof(info), f) != sizeof(info) || memcmp(info + 40, "TLinearCard", 12) != 0)
	{
		fclose(f);
		return kError_Bad_Parameters;
	}
	ULong nameSize = GetWord(info + 0), nameStart = GetWord(info + 4);
	ULong cisSize = GetWord(info + 16), cisStart = GetWord(info + 20);
	ULong dataSize = GetWord(info + 24), dataStart = GetWord(info + 28);
	if ((long) (dataStart + dataSize) > fileSize || (long) (cisStart + cisSize) > fileSize
	 || (long) (nameStart + nameSize) > fileSize || cisSize > kHostCardAttrSize / 2 || dataSize == 0)
	{
		fclose(f);
		return kError_Bad_Parameters;
	}
	Boolean isATA = GetWord(info + 32) == kHostCardTypeATA;
	if (isATA && (dataSize % 512) != 0)
	{
		fclose(f);
		return kError_Bad_Parameters;
	}
	ULong windowSize = isATA ? kHostCardATAWindowSize : dataSize;
	unsigned char* cis = (unsigned char*) calloc(cisSize + 1, 1);
	if (cis == nil || !GetWindows(socket, windowSize))
	{
		free(cis);
		fclose(f);
		return kError_No_Memory;
	}
	card->fCommon = gWindows[socket].fCommon;
	card->fAttribute = gWindows[socket].fAttribute;
	if (isATA)
		memset(card->fCommon, 0, kHostCardATAWindowSize);
	else
	{
		fseek(f, (long) dataStart, SEEK_SET);
		fread(card->fCommon, 1, dataSize, f);
	}
	fseek(f, (long) cisStart, SEEK_SET);
	fread(cis, 1, cisSize, f);
	// attribute byte o is CIS byte (o / 2) ^ 1 (Einstein's ReadAttrB)
	for (ULong o = 0; o < kHostCardAttrSize; o++)
	{
		ULong i = (o / 2) ^ 1;
		card->fAttribute[o] = i < cisSize ? cis[i] : 0;
	}
	free(cis);
	memset(card->fName, 0, sizeof(card->fName));
	if (nameSize > 1)
	{
		fseek(f, (long) nameStart, SEEK_SET);
		fread(card->fName, 1, nameSize < sizeof(card->fName) ? nameSize : sizeof(card->fName) - 1, f);
	}
	card->fFile = readOnly && !isATA ? nil : f;
	if (readOnly && !isATA)
		fclose(f);
	card->fATA = isATA;
	card->fSectors = isATA ? dataSize / 512 : 0;
	card->fReadOnly = readOnly;
	card->fDataStart = dataStart;
	card->fCommonSize = windowSize;
	card->fCISSize = cisSize;
	card->fType = GetWord(info + 32);
	card->fInserted = true;
	if (!gExitRegistered)
	{
		gExitRegistered = true;
		atexit(FlushAtExit);
	}
	Changed(socket);
	return noErr;
}


void
HostCardRemove(ULong socket)
{
	HostCardState* card = Card(socket);
	if (card == nil)
		return;
	if (card->fFile != nil)
		fclose(card->fFile);
	// (the socket's windows stay: see GetWindows)
	memset(card, 0, sizeof(HostCardState));
	Changed(socket);
}


Boolean
HostCardIsInserted(ULong socket)
{
	return Card(socket) != nil;
}


Boolean
HostCardIsWriteProtected(ULong socket)
{
	HostCardState* card = Card(socket);
	return card != nil && card->fReadOnly;
}


const char*
HostCardName(ULong socket)
{
	HostCardState* card = Card(socket);
	return card != nil ? card->fName : "";
}


ULong
HostCardType(ULong socket)
{
	HostCardState* card = Card(socket);
	return card != nil ? card->fType : 0;
}


Ptr
HostCardAttributeMemory(ULong socket)
{
	HostCardState* card = Card(socket);
	return card != nil ? (Ptr) card->fAttribute : nil;
}


Ptr
HostCardCommonMemory(ULong socket)
{
	HostCardState* card = Card(socket);
	return card != nil ? (Ptr) card->fCommon : nil;
}


ULong
HostCardCommonSize(ULong socket)
{
	HostCardState* card = Card(socket);
	return card != nil ? card->fCommonSize : 0;
}


ULong
HostCardCISSize(ULong socket)
{
	HostCardState* card = Card(socket);
	return card != nil ? card->fCISSize : 0;
}


NewtonErr
HostCardFlashWrite(ULong socket, ULong offset, const void* bytes, ULong count)
{
	HostCardState* card = Card(socket);
	if (card == nil)
		return kError_Bad_Parameters;
	if (card->fReadOnly)
		return kError_Bad_Parameters;
	if (offset > card->fCommonSize || count > card->fCommonSize - offset)
		return kError_Bad_Parameters;
	const unsigned char* from = (const unsigned char*) bytes;
	for (ULong i = 0; i < count; i++)
		card->fCommon[offset + i] &= from[i];
	WriteThrough(card, offset, count);
	return noErr;
}


NewtonErr
HostCardFlashErase(ULong socket, ULong offset, ULong size)
{
	HostCardState* card = Card(socket);
	if (card == nil)
		return kError_Bad_Parameters;
	if (card->fReadOnly)
		return kError_Bad_Parameters;
	if (offset > card->fCommonSize || size > card->fCommonSize - offset)
		return kError_Bad_Parameters;
	memset(card->fCommon + offset, 0xFF, size);
	WriteThrough(card, offset, size);
	return noErr;
}


void
HostCardFlush(ULong socket)
{
	HostCardState* card = Card(socket);
	if (card != nil && card->fFile != nil)
		fflush(card->fFile);
}


void
HostCardSetChangeProc(HostCardChangeProc proc)
{
	gChangeProc = proc;
}


Boolean
HostCardIsATA(ULong socket)
{
	HostCardState* card = Card(socket);
	return card != nil && card->fATA;
}


ULong
HostCardATASectors(ULong socket)
{
	HostCardState* card = Card(socket);
	return card != nil && card->fATA ? card->fSectors : 0;
}


NewtonErr
HostCardATARead(ULong socket, ULong sector, void* buffer)
{
	HostCardState* card = Card(socket);
	if (card == nil || !card->fATA || sector >= card->fSectors)
		return kError_Bad_Parameters;
	if (fseek(card->fFile, (long) (card->fDataStart + sector * 512), SEEK_SET) != 0
	 || fread(buffer, 1, 512, card->fFile) != 512)
		return kError_Bad_Parameters;
	return noErr;
}


NewtonErr
HostCardATAWrite(ULong socket, ULong sector, const void* buffer)
{
	HostCardState* card = Card(socket);
	if (card == nil || !card->fATA || sector >= card->fSectors || card->fReadOnly)
		return kError_Bad_Parameters;
	if (fseek(card->fFile, (long) (card->fDataStart + sector * 512), SEEK_SET) != 0
	 || fwrite(buffer, 1, 512, card->fFile) != 512)
		return kError_Bad_Parameters;
	return noErr;
}


Boolean
HostCardATAAttribute(const volatile void* address, ULong* socket, ULong* offset)
{
	const volatile unsigned char* a = (const volatile unsigned char*) address;
	for (ULong i = 0; i < kHostCardSockets; i++)
	{
		HostCardState* card = Card(i);
		if (card != nil && card->fATA && a >= card->fAttribute + kHostCardATAConfigBase
		&&  a < card->fAttribute + kHostCardATAConfigBase + kHostCardATAConfigSize)
		{
			*socket = i;
			*offset = (ULong) (a - card->fAttribute);
			return true;
		}
	}
	return false;
}


Boolean
HostCardATAWindow(const volatile void* address, ULong* socket, ULong* offset)
{
	const volatile unsigned char* a = (const volatile unsigned char*) address;
	for (ULong i = 0; i < kHostCardSockets; i++)
	{
		HostCardState* card = Card(i);
		if (card != nil && card->fATA && a >= card->fCommon && a < card->fCommon + kHostCardATAWindowSize)
		{
			*socket = i;
			*offset = (ULong) (a - card->fCommon);
			return true;
		}
	}
	return false;
}
