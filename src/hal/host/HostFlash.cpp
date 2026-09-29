/*
	File:		hal/host/HostFlash.cpp

	Contains:	The internal flash's chips over a host file (HostFlash.h).

	Written by:	the reconstruction (DEVIATION: the machine's chips are hardware)
*/

#include "HostFlash.h"
#include "Host.h"
#include "OSErrors.h"
#include "NewtErrors.h"

#include <stdio.h>
#include <string.h>
#include <vector>

ULong	gHostFlashWrites = 0;
ULong	gHostFlashErases = 0;

namespace
{
	std::vector<unsigned char>	gFlash;		// the banks, one after the other
	FILE*						gFile = nil;

	void
	WriteThrough(ULong offset, ULong size)
	{
		if (gFile == nil)
			return;
		fseek(gFile, (long) offset, SEEK_SET);
		fwrite(&gFlash[offset], 1, size, gFile);
		fflush(gFile);
	}
}


NewtonErr
HostFlashOpen(const char* path, ULong size)
{
	HostFlashClose();
	if (size != kHostFlashBankSize && size != 2 * kHostFlashBankSize)
		return kError_Bad_Parameters;
	gFlash.assign(size, 0xFF);
	if (path != nil)
	{
		gFile = fopen(path, "r+b");
		if (gFile != nil)
		{
			fseek(gFile, 0, SEEK_END);
			long existing = ftell(gFile);
			if (existing != (long) kHostFlashBankSize && existing != (long) (2 * kHostFlashBankSize))
			{
				fclose(gFile);
				gFile = nil;
				gFlash.clear();
				return kError_Bad_Parameters;
			}
			gFlash.assign((size_t) existing, 0xFF);
			fseek(gFile, 0, SEEK_SET);
			if (fread(&gFlash[0], 1, gFlash.size(), gFile) != gFlash.size())
			{
				fclose(gFile);
				gFile = nil;
				gFlash.clear();
				return kError_Bad_Parameters;
			}
		}
		else
		{
			gFile = fopen(path, "w+b");
			if (gFile == nil)
			{
				gFlash.clear();
				return kError_Bad_Parameters;
			}
			WriteThrough(0, (ULong) gFlash.size());
		}
	}
	HostRegisterPhysicalMemory(kHostFlashBank1, kHostFlashBankSize, (Ptr) &gFlash[0]);
	if (gFlash.size() > kHostFlashBankSize)
		HostRegisterPhysicalMemory(kHostFlashBank2, kHostFlashBankSize, (Ptr) &gFlash[kHostFlashBankSize]);
	return noErr;
}


void
HostFlashClose(void)
{
	HostUnregisterPhysicalMemory(kHostFlashBank1);
	HostUnregisterPhysicalMemory(kHostFlashBank2);
	if (gFile != nil)
	{
		fclose(gFile);
		gFile = nil;
	}
	gFlash.clear();
	gFlash.shrink_to_fit();
}


Boolean
HostFlashIsOpen(void)
{
	return !gFlash.empty();
}


ULong
HostFlashSize(void)
{
	return (ULong) gFlash.size();
}


Boolean
HostFlashContains(Ptr p)
{
	return !gFlash.empty() && (unsigned char*) p >= &gFlash[0] && (unsigned char*) p < &gFlash[0] + gFlash.size();
}


// A chip programs by clearing bits: what the word becomes is what it was
// AND what was written, the lanes the mask leaves out being written as ones.
// (Einstein's own flash replaces the masked bits instead - the same thing
// whenever, as the ROM's code always does, only ones are turned into
// noughts.)
void
HostFlashWrite(Ptr word, ULong value, ULong mask)
{
	if (!HostFlashContains(word) || !HostFlashContains(word + 3))
		return;
	unsigned char* p = (unsigned char*) word;
	ULong programmed = value | ~mask;
	p[0] &= (unsigned char) (programmed >> 24);
	p[1] &= (unsigned char) (programmed >> 16);
	p[2] &= (unsigned char) (programmed >> 8);
	p[3] &= (unsigned char) programmed;
	gHostFlashWrites++;
	WriteThrough((ULong) (p - &gFlash[0]), 4);
}


void
HostFlashErase(Ptr start, ULong size, ULong lanes)
{
	if (!HostFlashContains(start) || !HostFlashContains(start + size - 1))
		return;
	unsigned char* p = (unsigned char*) start;
	for (ULong i = 0; i < size; i++)
		if ((lanes >> (8 * (3 - ((p - &gFlash[0]) + i) % 4))) & 0xFF)
			p[i] = 0xFF;
	gHostFlashErases++;
	WriteThrough((ULong) (p - &gFlash[0]), size);
}
