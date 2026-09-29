/*
	File:		comms/HostOptionLayouts.cpp

	Contains:	The option classes' fields, and the rewriting between the
				device's layout and the host's (HostOptionLayouts.h).  Not
				in the ROM.
*/

#include "HostOptionLayouts.h"
#include "SerialOptions.h"
#include "MNPOptions.h"
#include "CommOptions.h"
#include "HALOptions.h"
#include "NewtonMemory.h"
#include "toolbox/ByteOrder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// a field: 'u' an unsigned word (ULong, BitRate, TTimeout, a pointer) and
// 'w' a signed one (FastInt, long) - four bytes on the device, pointer-
// sized on the host; 'i' four bytes on both (an enum); 'h' two bytes; 'b'
// one byte (Boolean, UChar, UByte); "=" alone: the option's tool reads the
// device's bytes itself, so they are passed on as they are.  Every field but a byte is big-endian
// on the device.  (A class of bytes alone is listed too: its bytes are the
// same either way, and the listing says it was looked at.)
static const HostOptionLayout kLayouts[] =
{
	// serial (SerialOptions.h)
	{ kCMOSerialChipSpec,			"uubbbbbbbbhh" },
	{ kCMOSerialHWChipLoc,			"uu" },
	{ kCMOSerialMiscConfig,			"ubbbbb" },
	{ kCMOBreakFraming,				"uubu" },
	{ kCMOSerialEventEnables,		"uu" },
	{ kCMOSerialIOStats,			"uuuubbb" },
	{ kCMOInputFlowControlParms,	"bbbbbb" },
	{ kCMOOutputFlowControlParms,	"bbbbbb" },
	{ kCMOSerialHardware,			"iuw" },
	{ kCMOSerialBuffers,			"uuu" },
	{ kCMOSerialIOParms,			"wwwu" },
	{ kCMOSerialBitRate,			"u" },
	{ kCMOSerialHalfDuplex,			"b" },
	{ kCMOSerialDTRControl,			"b" },
	{ kHMOHiSpeedClockOption,		"b" },
	{ kCMOSerialBytesAvailable,		"u" },
	{ kCMOFramingParms,				"bbbbb" },
	{ kCMOFramedAsyncStats,			"u" },
	// MNP (MNPOptions.h; 'mnps' is also the MNP service's name, which is
	// never rewritten)
	{ kCMOMNPAllocate,				"b" },
	{ kCMOMNPCompression,			"w" },
	{ kCMOMNPDataRate,				"u" },
	{ kCMOMNPSpeedNegotiation,		"u" },
	{ kCMOMNPStatistics,			"uuuuuuuuuuuuuuuu" },
	{ kCMOMNPDebugConnect,			"bbbbu" },
	// the modem navigator's (ModemNavigator.h)
	{ 'mpre',						"bbbbbbbbbbuuub" },
	{ 'mcto',						"bbbbb" },
	// read as the device's bytes by their tool ("=": passed on as they
	// are) - the host's TCP tool (comms/host/HostTCPTool.h)
	{ 'itrs',						"=" },
	{ 'ilpt',						"=" },
	{ 0, nil }
};


const HostOptionLayout*
HostOptionLayoutFor(ULong label)
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


static size_t
FieldSize(char kind, Boolean host)
{
	switch (kind)
	{
	case 'u': case 'w':	return host ? sizeof(ULong) : 4;
	case 'i':			return 4;
	case 'h':			return 2;
	default:			return 1;
	}
}


size_t
HostOptionLayoutSize(const HostOptionLayout* layout, Boolean host)
{
	size_t offset = 0;
	for (const char* f = layout->fFields; *f != 0; f++)
	{
		size_t size = FieldSize(*f, host);
		offset = AlignUp(offset, size) + size;
	}
	// (a class ends on a whole host word - TOption holds pointer-sized
	// fields - and the device's option on a whole word)
	return AlignUp(offset, host ? sizeof(ULong) : 4);
}


// (the rewriting both ways: device words read big-endian, host words
// written in the host's order, and the reverse)
static void
Rewrite(const HostOptionLayout* layout, const UByte* in, long inLength, Boolean inIsHost, UByte* out, long outLength)
{
	size_t inOffset = 0, outOffset = 0;
	for (const char* f = layout->fFields; *f != 0; f++)
	{
		size_t inSize = FieldSize(*f, inIsHost), outSize = FieldSize(*f, !inIsHost);
		inOffset = AlignUp(inOffset, inSize);
		outOffset = AlignUp(outOffset, outSize);
		if ((long) (inOffset + inSize) <= inLength && (long) (outOffset + outSize) <= outLength)
		{
			switch (*f)
			{
			case 'b':
				out[outOffset] = in[inOffset];
				break;
			case 'h':
				if (inIsHost)
				{
					UShort v;
					memcpy(&v, in + inOffset, 2);
					out[outOffset] = (UByte) (v >> 8);
					out[outOffset + 1] = (UByte) v;
				}
				else
				{
					UShort v = (UShort) (in[inOffset] << 8 | in[inOffset + 1]);
					memcpy(out + outOffset, &v, 2);
				}
				break;
			case 'i':
				if (inIsHost)
				{
					uint32_t v;
					memcpy(&v, in + inOffset, 4);
					PutBigEndianWord(out + outOffset, v);
				}
				else
				{
					uint32_t v = GetBigEndianWord(in + inOffset);
					memcpy(out + outOffset, &v, 4);
				}
				break;
			default:
				if (inIsHost)
				{
					ULong v;
					memcpy(&v, in + inOffset, sizeof(ULong));
					PutBigEndianWord(out + outOffset, (unsigned int) v);
				}
				else if (*f == 'w')
				{
					FastInt v = (FastInt) (Long32) GetBigEndianWord(in + inOffset);
					memcpy(out + outOffset, &v, sizeof(FastInt));
				}
				else
				{
					ULong v = GetBigEndianWord(in + inOffset);
					memcpy(out + outOffset, &v, sizeof(ULong));
				}
				break;
			}
		}
		inOffset += inSize;
		outOffset += outSize;
	}
}


// An option no table lists is said to be passed on as it is: once per
// label, and always - that it may be read wrong is worth hearing about
// (a serial tool that set its speed to 4 took a while to find).  An option
// whose tool reads the device's bytes itself is listed as "=";
// NEWTON_QUIET_OPTIONS (labels) hushes others.
static void
SayUnlisted(ULong label)
{
	static ULong said[64];
	static int saidCount = 0;
	for (int i = 0; i < saidCount; i++)
		if (said[i] == label)
			return;
	if (saidCount < 64)
		said[saidCount++] = label;
	char name[5] = { (char) (label >> 24), (char) (label >> 16), (char) (label >> 8), (char) label, 0 };
	const char* quiet = getenv("NEWTON_QUIET_OPTIONS");
	if (quiet != nil && strstr(quiet, name) != nil)
		return;
	fprintf(stderr, "[comms] option '%s' from a script is passed on in the device's layout: comms/HostOptionLayouts.cpp does not list it\n", name);
}


TOption*
HostOptionFromDevice(TOption* option)
{
	if (option == nil || option->IsService() || option->Length() == 0)
		return option;
	const HostOptionLayout* layout = HostOptionLayoutFor(option->Label());
	if (layout != nil && layout->fFields[0] == '=')
		return option;
	if (layout == nil)
	{
		if (option->Label() != kCMOServiceIdentifier)		// (Translators.cpp rewrites it itself)
			SayUnlisted(option->Label());
		return option;
	}
	size_t hostSize = sizeof(TOption) + HostOptionLayoutSize(layout, true);
	TOption* host = (TOption*) NewPtrClear(hostSize);
	if (host == nil)
	{
		DisposPtr((Ptr) option);
		return nil;
	}
	memcpy(host, option, sizeof(TOption));
	host->SetLength(hostSize - sizeof(TOption));
	Rewrite(layout, (const UByte*) (option + 1), option->Length(), false, (UByte*) (host + 1), hostSize - sizeof(TOption));
	DisposPtr((Ptr) option);
	return host;
}


long
HostOptionToDevice(const TOption* option, UByte* data, long length)
{
	if (((TOption*) option)->IsService())
		return -1;
	const HostOptionLayout* layout = HostOptionLayoutFor(((TOption*) option)->Label());
	if (layout == nil || layout->fFields[0] == '=')
		return -1;
	long deviceLength = (long) HostOptionLayoutSize(layout, false);
	if (deviceLength > length)
		deviceLength = length;
	memset(data, 0, deviceLength);
	Rewrite(layout, (const UByte*) (option + 1), ((TOption*) option)->Length(), true, data, deviceLength);
	return deviceLength;
}
