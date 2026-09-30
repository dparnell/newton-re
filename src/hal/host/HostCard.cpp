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

	struct Retired { void* fBlock; Retired* fNext; };
	Retired*	gRetired = nil;

	void
	FreeRetired(void)
	{
		while (gRetired != nil)
		{
			Retired* next = gRetired->fNext;
			free(gRetired->fBlock);
			delete gRetired;
			gRetired = next;
		}
	}

	void
	RetireWindow(void* block)
	{
		if (block == nil)
			return;
		if (gRetired == nil)
			atexit(FreeRetired);
		Retired* r = new Retired;
		r->fBlock = block;
		r->fNext = gRetired;
		gRetired = r;
	}

	void
	Changed(ULong socket)
	{
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
	card->fCommon = (unsigned char*) malloc(dataSize);
	card->fAttribute = (unsigned char*) calloc(kHostCardAttrSize, 1);
	unsigned char* cis = (unsigned char*) calloc(cisSize + 1, 1);
	if (card->fCommon == nil || card->fAttribute == nil || cis == nil)
	{
		free(card->fCommon);
		free(card->fAttribute);
		free(cis);
		card->fCommon = card->fAttribute = nil;
		fclose(f);
		return kError_No_Memory;
	}
	fseek(f, (long) dataStart, SEEK_SET);
	fread(card->fCommon, 1, dataSize, f);
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
	card->fFile = readOnly ? nil : f;
	if (readOnly)
		fclose(f);
	card->fReadOnly = readOnly;
	card->fDataStart = dataStart;
	card->fCommonSize = dataSize;
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
	// (the windows are kept, as they were: the machine goes on reading a
	// card it has not yet noticed is gone - its stores are unmounted a
	// moment later - where a MessagePad's reads fault into the card
	// domains' monitor; they are given back when the program ends)
	RetireWindow(card->fCommon);
	RetireWindow(card->fAttribute);
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
