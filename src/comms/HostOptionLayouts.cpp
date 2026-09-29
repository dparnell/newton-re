/*
	File:		comms/HostOptionLayouts.cpp

	Contains:	The options whose host layout is not the device's, and the
				rewriting between the two (HostOptionLayouts.h).  Not in
				the ROM.
*/

#include "HostOptionLayouts.h"
#include "SerialOptions.h"
#include "MNPOptions.h"
#include "NewtonMemory.h"
#include "toolbox/ByteOrder.h"

#include <string.h>

// a field: 'w' a signed word (FastInt, long), 'u' an unsigned one (ULong,
// BitRate) - four bytes on the device, pointer-sized on the host; 'b' a
// byte (Boolean, UChar), the same on both
struct HostOptionLayout
{
	ULong		fLabel;
	const char*	fFields;
	size_t		fHostSize;			// the host class's size, header and all
};

static const HostOptionLayout kLayouts[] =
{
	{ kCMOSerialIOParms,		"wwwu",	sizeof(TCMOSerialIOParms) },
	{ kCMOMNPSpeedNegotiation,	"u",	sizeof(TCMOMNPSpeedNegotiation) },
	{ kCMOMNPCompression,		"w",	sizeof(TCMOMNPCompression) },
	{ kCMOMNPDataRate,			"u",	sizeof(TCMOMNPDataRate) },
	{ 0, nil, 0 }
};


static const HostOptionLayout*
LayoutFor(ULong label)
{
	for (const HostOptionLayout* layout = kLayouts; layout->fFields != nil; layout++)
		if (layout->fLabel == label)
			return layout;
	return nil;
}


static size_t
AlignUp(size_t offset, size_t alignment)
{
	return (offset + alignment - 1) & ~(alignment - 1);
}


TOption*
HostOptionFromDevice(TOption* option)
{
	if (option == nil)
		return nil;
	const HostOptionLayout* layout = LayoutFor(option->Label());
	if (layout == nil)
		return option;
	TOption* host = (TOption*) NewPtrClear(layout->fHostSize);
	if (host == nil)
	{
		DisposPtr((Ptr) option);
		return nil;
	}
	memcpy(host, option, sizeof(TOption));
	host->SetLength(layout->fHostSize - sizeof(TOption));
	const UByte* in = (const UByte*) (option + 1);
	long inLength = option->Length();
	UByte* out = (UByte*) (host + 1);
	size_t inOffset = 0, outOffset = 0;
	for (const char* f = layout->fFields; *f != 0; f++)
	{
		if (*f == 'b')
		{
			if ((long) inOffset + 1 <= inLength)
				out[outOffset] = in[inOffset];
			inOffset += 1;
			outOffset += 1;
		}
		else
		{
			inOffset = AlignUp(inOffset, 4);
			outOffset = AlignUp(outOffset, sizeof(ULong));
			if ((long) inOffset + 4 <= inLength)
			{
				ULong word = GetBigEndianWord(in + inOffset);
				if (*f == 'w')
					*(FastInt*) (out + outOffset) = (FastInt) (Long32) word;
				else
					*(ULong*) (out + outOffset) = word;
			}
			inOffset += 4;
			outOffset += sizeof(ULong);
		}
	}
	DisposPtr((Ptr) option);
	return host;
}


long
HostOptionToDevice(const TOption* option, UByte* data, long length)
{
	const HostOptionLayout* layout = LayoutFor(((TOption*) option)->Label());
	if (layout == nil)
		return -1;
	const UByte* in = (const UByte*) (option + 1);
	size_t inOffset = 0, outOffset = 0;
	for (const char* f = layout->fFields; *f != 0; f++)
	{
		if (*f == 'b')
		{
			if ((long) outOffset + 1 <= length)
				data[outOffset] = in[inOffset];
			inOffset += 1;
			outOffset += 1;
		}
		else
		{
			inOffset = AlignUp(inOffset, sizeof(ULong));
			outOffset = AlignUp(outOffset, 4);
			if ((long) outOffset + 4 <= length)
				PutBigEndianWord(data + outOffset, (unsigned int) *(const ULong*) (in + inOffset));
			inOffset += sizeof(ULong);
			outOffset += 4;
		}
	}
	return (long) AlignUp(outOffset, 4);
}
