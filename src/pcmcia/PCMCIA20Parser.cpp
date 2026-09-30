/*
	File:		pcmcia/PCMCIA20Parser.cpp

	Contains:	TPCMCIA20Parser (PCMCIA20Parser.h): a card's CIS read into a
				TCardPCMCIA.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PCMCIA20Parser.h"
#include "CardCISIterator.h"
#include "CardSocket.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

#include <string.h>
#include <stdint.h>

extern const unsigned char	speedMantissasX10[16];		// CISTables.cpp
extern const unsigned int	speedExps[8];
extern const unsigned char	kPowerMantissas[16];
extern const unsigned int	kPowerExponents[8];


// The byte lane of a card address (CardCISIterator.cpp says why)
static inline UChar*
Lane(UChar* p)
{
	return (UChar*) ((uintptr_t) p ^ 3);
}


// ROM 0x0004bd40 __ct__15TPCMCIA20ParserFv
TPCMCIA20Parser::TPCMCIA20Parser()
{
	fDefaultConfig = nil;
	fCard = nil;
	fSocket = nil;
	Reset();
}


// ROM 0x0004bd8c __dt__15TPCMCIA20ParserFv
TPCMCIA20Parser::~TPCMCIA20Parser()
{
	if (fDefaultConfig != nil)
		delete fDefaultConfig;
}


// ROM 0x0004e288 Version__15TPCMCIA20ParserFv
ULong
TPCMCIA20Parser::Version(void)
{
	return 0x20200;
}


// ROM 0x0004d218 Reset__15TPCMCIA20ParserFv
// Ready for a CIS (the next function's, on a multi-function card).
NewtonErr
TPCMCIA20Parser::Reset(void)
{
	fError = noErr;
	fTupleEnd = nil;
	fNullTuples = 0;
	fDeviceTuples = 0;
	fTupleCount = 0;
	fFunction = nil;
	fConfigBase = 0;
	fConfigEntryStart = 0;
	fConfigEntryEnd = 0;
	fNewFunction = 0;
	fIOFunctions = 1;
	fLongLink.Clear();
	if (fDefaultConfig != nil)
		delete fDefaultConfig;
	fDefaultConfig = nil;
	return noErr;
}


/*------------------------------------------------------------------------------
	R e a d i n g   a   t u p l e
------------------------------------------------------------------------------*/

// ROM 0x0004e0fc GetBits__15TPCMCIA20ParserFUlN21
// count bits of a byte, the highest of them highBit.
ULong
TPCMCIA20Parser::GetBits(ULong byte, ULong highBit, ULong count)
{
	return (((uint32_t) byte << (31 - highBit)) >> (32 - count)) & 0xFF;
}


// ROM 0x0004d64c pow__15TPCMCIA20ParserFUlT1
ULong
TPCMCIA20Parser::pow(ULong base, ULong exponent)
{
	uint32_t result = 1;
	if (exponent == 0)
		return 1;
	for (ULong i = 0; i < exponent; i++)
		result = (uint32_t) base * result;
	return result;
}


// ROM 0x0004e3bc IncrAddr__15TPCMCIA20ParserFRPUcUl
// On count bytes; a tuple read past its end is kError_Card_Tuple_Size.
UChar*
TPCMCIA20Parser::IncrAddr(UChar*& p, ULong count)
{
	UChar* was = p;
	p = was + count;
	if (fTupleEnd < was + count)
		fError = kError_Card_Tuple_Size;
	return p;
}


// ROM 0x0004e3e4 StartTuple__15TPCMCIA20ParserFRPUc
// p from the tuple's code to its link, the tuple's end noted (a link of
// 0xFF, the end of a chain, taken as 0xFE).
UChar*
TPCMCIA20Parser::StartTuple(UChar*& p)
{
	p = p + 1;
	ULong link = *p;
	if (link == 0xFF)
		link = 0xFE;
	fTupleEnd = p + link;
	return p;
}


// ROM 0x0004e294 GetShort__15TPCMCIA20ParserFRPUc
// The little-endian short after p, signed.
Long
TPCMCIA20Parser::GetShort(UChar*& p)
{
	UChar lo = p[1];
	UChar hi = p[2];
	IncrAddr(p, 2);
	return (int16_t) (lo + hi * 0x100);
}


// ROM 0x0004e2c8 GetWord__15TPCMCIA20ParserFRPUc
ULong
TPCMCIA20Parser::GetWord(UChar*& p)
{
	UChar* q = p;
	UChar b1 = q[1], b2 = q[2], b3 = q[3], b4 = q[4];
	IncrAddr(p, 4);
	return (uint32_t) (b1 + b2 * 0x100 + b3 * 0x10000 + ((uint32_t) b4 << 24));
}


// ROM 0x0004c020 GetExtendedDeviceSpeed__15TPCMCIA20ParserFRPUc
// A speed byte (mantissa and exponent, in nanoseconds) and its extensions,
// which are passed over.
TNanoSecond
TPCMCIA20Parser::GetExtendedDeviceSpeed(UChar*& p)
{
	ULong byte = *IncrAddr(p, 1);
	ULong mantissa = GetBits(byte, 6, 4);
	ULong exponent = GetBits(byte, 2, 3);
	TNanoSecond speed = (uint32_t) (speedExps[exponent] * speedMantissasX10[mantissa]) / 10;
	while (p < fTupleEnd && GetBits(byte, 7, 1) != 0)
		byte = *IncrAddr(p, 1);
	return speed;
}


// ROM 0x0004bdd0 GetPowerValue__15TPCMCIA20ParserFPUcUcPvPUl
// A power parameter (mantissa and exponent) and its extension bytes, each a
// further two decimal digits (a value over 124 is a special one, kept in
// *extension).  inTenths: a voltage (the value is in tenths of the unit
// the exponent gives); a current is ten times it.
UChar*
TPCMCIA20Parser::GetPowerValue(UChar* p, UChar inTenths, void* value, ULong* extension)
{
	ULong byte = *IncrAddr(p, 1);
	ULong exponent = GetBits(byte, 2, 3);
	ULong mantissa = GetBits(byte, 6, 4);
	uint32_t scale = kPowerExponents[exponent];
	uint32_t result = scale * kPowerMantissas[mantissa];
	*extension = 0;
	ULong more = GetBits(byte, 7, 1);
	scale = scale * 10;
	while (p < fTupleEnd && more != 0)
	{
		ULong next = *IncrAddr(p, 1);
		ULong digits = GetBits(next, 6, 7);
		scale = scale / 100;
		if (digits < 100)
			result = scale * (uint32_t) digits + result;
		if (digits > 0x7C)
			*extension = digits;
		more = GetBits(next, 7, 1);
	}
	if (inTenths == 0)
		result = result * 10;
	*(uint32_t*) value = result;
	return p;
}


// ROM 0x0004e304 GetTuple__15TPCMCIA20ParserFPUcT1Uc
// A tuple read whole (0x100 bytes) from where it lies ==> where the next one
// is, nil after the end.
UChar*
TPCMCIA20Parser::GetTuple(UChar* from, UChar* buffer, UChar inAttrMemory)
{
	ULong stride;
	if (inAttrMemory == 0)
	{
		stride = 1;
		if (fCard->f16BitOnlyCard != 0)
		{
			// a CIS mirrored in common memory, a byte out of each word
			for (ULong i = 0; i < 0x100; i++)
				buffer[i] = *Lane(from + i);
			goto read;
		}
	}
	else
		stride = 2;
	for (ULong i = 0; i < 0x100; i++)
		buffer[i] = CardAttrMemReadByte(Lane(from + stride * i));
read:
	if (buffer[0] == 0)
		return from + stride;
	if (buffer[1] == 0xFF || buffer[0] == 0xFF)
		return nil;
	return from + stride * ((ULong) buffer[1] + 2);
}


/*------------------------------------------------------------------------------
	T h e   t u p l e s
------------------------------------------------------------------------------*/

// ROM 0x0004d6dc CisTpl_Null__15TPCMCIA20ParserFPUc
// (a null is not counted as a tuple, but as a null)
UChar*
TPCMCIA20Parser::CisTpl_Null(UChar* tuple)
{
	fTupleCount--;
	fNullTuples++;
	return tuple;
}


// ROM 0x0004d280 CisTpl_End__15TPCMCIA20ParserFPUc
UChar*
TPCMCIA20Parser::CisTpl_End(UChar* tuple)
{
	return tuple;
}


// ROM 0x0004d0f0 CisTpl_Device__15TPCMCIA20ParserFPUc
// Only the first CISTPL_DEVICE is read (==> nil for any other).
UChar*
TPCMCIA20Parser::CisTpl_Device(UChar* tuple)
{
	if (++fDeviceTuples <= 1)
		return DeviceParser(tuple, 0, 0);
	return nil;
}


// ROM 0x0004d118 CisTpl_Device_A__15TPCMCIA20ParserFPUc
UChar*
TPCMCIA20Parser::CisTpl_Device_A(UChar* tuple)
{
	return DeviceParser(tuple, 1, 0);
}


// ROM 0x0004cd6c DeviceParser__15TPCMCIA20ParserFPUcUcT2
// The devices of a CISTPL_DEVICE (common memory), _DEVICE_A (attribute
// memory) or _DEVICE_OC/_OA (a list of its own, with the conditions they
// are for: MWAIT and the Vcc in the first byte).  Each is a type, speed
// and size, laid one after another from the start of the space; the
// common devices' total is the card's.
UChar*
TPCMCIA20Parser::DeviceParser(UChar* tuple, UChar forAttrMemory, UChar otherConditions)
{
	StartTuple(tuple);
	Boolean mwait = false;
	TMicroVolt vcc = 5000000;
	Long listNumber = 0;
	if (otherConditions != 0)
	{
		listNumber = fCard->AddCardOtherCondDeviceList(forAttrMemory);
		if (listNumber < 0)
		{
			fError = listNumber;
			return tuple;
		}
		ULong byte = *IncrAddr(tuple, 1);
		mwait = GetBits(byte, 0, 1) != 0;
		ULong voltage = GetBits(byte, 2, 2);
		if (voltage == 1)
			vcc = 3300000;
		else if (voltage == 2)
			vcc = 2200000;
		else if (voltage == 3)
			vcc = 1100000;
		ULong more = GetBits(byte, 7, 1);
		while (more != 0)
			more = GetBits(*IncrAddr(tuple, 1), 7, 1);
	}
	ULong startOffset = 0;
	Boolean done = false;
	do
	{
		if (tuple >= fTupleEnd)
			break;
		ULong info = *IncrAddr(tuple, 1);
		if (info == 0xFF)
			break;
		ULong type = GetBits(info, 7, 4);
		Boolean wps = GetBits(info, 3, 1);
		ULong speedCode = GetBits(info, 2, 3);
		TNanoSecond speed = 0;
		if (speedCode < 5 && speedCode > 0)
			speed = 300 - 50 * speedCode;
		if (speedCode == 7)
			speed = GetExtendedDeviceSpeed(tuple);
		if (type == 0x0E)
		{
			// DTYPE_EXTEND: the extension bytes passed over
			type = 0;
			while (GetBits(*IncrAddr(tuple, 1), 7, 1) != 0)
				;
		}
		ULong sizeByte = tuple < fTupleEnd ? *IncrAddr(tuple, 1) : 0xFE;
		if (sizeByte == 0xFF)
			done = true;
		else
		{
			ULong units = GetBits(sizeByte, 7, 5);
			ULong code = GetBits(sizeByte, 2, 3);
			ULong size = (uint32_t) (0x200 << ((code & 0x7F) << 1)) * ((units + 1) & 0xFF);
			TCardDevice* device = new TCardDevice(speed, size, vcc, mwait, forAttrMemory, wps, (UChar) type, startOffset);
			startOffset += size;
			if (device == nil)
			{
				done = true;
				fError = kError_No_Memory;
			}
			else if (otherConditions == 0)
			{
				fCard->AddCardDevice(device);
				fCard->fNumOfDevice++;
			}
			else
				fCard->AddCardOtherCondDevice(forAttrMemory, listNumber, device);
		}
	} while (!done);
	if (forAttrMemory == 0 && otherConditions == 0)
		fCard->fTotalDeviceSize = startOffset;
	return tuple;
}


// ROM 0x0004d124 CisTpl_Device_GEO__15TPCMCIA20ParserFPUc
// Each common device's geometry, six bytes a device.
// ROM BUG kept: the device is not checked - a CISTPL_DEVICE_GEO naming more
// devices than the CISTPL_DEVICE did writes through a nil one.
UChar*
TPCMCIA20Parser::CisTpl_Device_GEO(UChar* tuple)
{
	StartTuple(tuple);
	ULong count = *tuple / 6;
	ULong deviceNumber = 0;
	for ( ; count != 0; count--)
	{
		TCardDevice* device = fCard->GetCardDevice(deviceNumber);
		device->fBusSize = *IncrAddr(tuple, 1);
		device->fEraseBlockSize = *IncrAddr(tuple, 1);
		device->fReadBlockSize = *IncrAddr(tuple, 1);
		device->fWriteBlockSize = *IncrAddr(tuple, 1);
		device->fPart = *IncrAddr(tuple, 1);
		device->fInterleave = *IncrAddr(tuple, 1);
		deviceNumber++;
	}
	return tuple;
}


// ROM 0x0004d464 JedecInfoParser__15TPCMCIA20ParserFPUcUc
// The JEDEC manufacturer and information bytes of the devices, from the
// first device of the space on.  ==> kError_Card_Tuple_Jedec when the
// space has no device.
UChar*
TPCMCIA20Parser::JedecInfoParser(UChar* tuple, UChar forAttrMemory)
{
	StartTuple(tuple);
	UChar count = fCard->fNumOfDevice;
	ULong deviceNumber = 0;
	Boolean found = false;
	for (ULong i = 0; i < count && !found; i++)
	{
		TCardDevice* device = fCard->GetCardDevice(i);
		if (device != nil && (ULong) (device->fAttributeMemoryDescr & 1) == forAttrMemory)
		{
			found = true;
			deviceNumber = i;
		}
	}
	if (found)
	{
		while (tuple < fTupleEnd)
		{
			UChar manufacturer = *IncrAddr(tuple, 1);
			if (manufacturer == 0xFF)
				return tuple;
			TCardDevice* device = fCard->GetCardDevice(deviceNumber);
			if (device != nil)
			{
				device->fJedecMfr = manufacturer;
				device->fJedecMfrInfo = *IncrAddr(tuple, 1);
				deviceNumber++;
			}
		}
	}
	else
		fError = kError_Card_Tuple_Jedec;
	return tuple;
}


// ROM 0x0004d574 CisTpl_Jedec_C__15TPCMCIA20ParserFPUc
UChar*
TPCMCIA20Parser::CisTpl_Jedec_C(UChar* tuple)
{
	return JedecInfoParser(tuple, 0);
}


// ROM 0x0004d56c CisTpl_Jedec_A__15TPCMCIA20ParserFPUc
UChar*
TPCMCIA20Parser::CisTpl_Jedec_A(UChar* tuple)
{
	return JedecInfoParser(tuple, 1);
}


// ROM 0x0004cae8 CisTpl_Checksum__15TPCMCIA20ParserFPUcT1UlUc
// A checksum over a stretch of the CIS relative to this tuple.  The
// stretch is looked for as the tuple's offset says in its space and, if
// that fails, as though every byte took one address even in attribute
// memory.  ==> kError_Card_Checksum when neither sums right.
UChar*
TPCMCIA20Parser::CisTpl_Checksum(UChar* tuple, UChar* base, ULong offset, UChar inAttrMemory)
{
	StartTuple(tuple);
	Long relative = GetShort(tuple);
	ULong count = (ULong) GetShort(tuple);
	UChar sum = *IncrAddr(tuple, 1);
	ULong stride = inAttrMemory == 0 ? 1 : 2;
	UChar* alternative = base + ((offset + relative) & 0x3FFFFFF);
	if (!ChecksumOK(sum, base + ((stride * relative + offset) & 0x3FFFFFF), count, stride)
	 && !ChecksumOK(sum, alternative, count, stride))
		fError = kError_Card_Checksum;
	return tuple;
}


// ROM 0x0004bf0c ChecksumOK__15TPCMCIA20ParserFUlPUcN21
// Whether count bytes of the card sum (modulo 256) to the checksum.  An
// abort reading them is a failed sum.
Boolean
TPCMCIA20Parser::ChecksumOK(ULong checksum, UChar* from, ULong count, ULong stride)
{
	ULong total = 0;
	newton_try
	{
		for (ULong i = 0; i < count; i++)
		{
			UChar byte;
			if (stride == 2)
				byte = CardAttrMemReadByte(Lane(from));
			else
				byte = *from;
			total += byte;
			from += stride;
		}
	}
	newton_catch(exPermissionViolation)
	{
		total = ~checksum;
	}
	newton_catch(exBusError)
	{
		total = ~checksum;
	}
	newton_catch(exWriteProtected)
	{
		total = ~checksum;
	}
	end_try;
	return (total & 0xFF) == checksum;
}


// ROM 0x0004d5b0 SetLongLink__15TPCMCIA20ParserFPUcUc
// ==> kError_Card_Tuple_NoLinkAndLink after a CISTPL_NO_LINK.
UChar*
TPCMCIA20Parser::SetLongLink(UChar* tuple, UChar inAttrMemory)
{
	if (fLongLink.fNoLongLinkTupleSeen == 0)
	{
		fLongLink.fLongLinkTupleSeen = -1;
		fLongLink.fInAttributeMemory = inAttrMemory != 0 ? -1 : 0;
		ULong address = GetWord(tuple);
		if (inAttrMemory != 0)
			address = address << 1;
		fLongLink.fLinkAddress = (UChar*) address;		// (an offset into the space, kept where the address goes)
	}
	else
		fError = kError_Card_Tuple_NoLinkAndLink;
	return tuple;
}


// ROM 0x0004d620 CisTpl_LongLink_A__15TPCMCIA20ParserFPUc
UChar*
TPCMCIA20Parser::CisTpl_LongLink_A(UChar* tuple)
{
	StartTuple(tuple);
	return SetLongLink(tuple, 1);
}


// ROM 0x0004d670 CisTpl_LongLink_C__15TPCMCIA20ParserFPUc
UChar*
TPCMCIA20Parser::CisTpl_LongLink_C(UChar* tuple)
{
	StartTuple(tuple);
	return SetLongLink(tuple, 0);
}


// ROM 0x0004d69c CisTpl_No_Link__15TPCMCIA20ParserFPUc
// ==> kError_Card_Tuple_LinkAndNoLink after a long link.
UChar*
TPCMCIA20Parser::CisTpl_No_Link(UChar* tuple)
{
	StartTuple(tuple);
	if (fLongLink.fLongLinkTupleSeen == 0)
		fLongLink.fNoLongLinkTupleSeen = -1;
	else
		fError = kError_Card_Tuple_LinkAndNoLink;
	return tuple;
}


// ROM 0x0004d57c CisTpl_LinkTarget__15TPCMCIA20ParserFPUcT1
UChar*
TPCMCIA20Parser::CisTpl_LinkTarget(UChar* tuple, UChar* /*where*/)
{
	if (!(tuple[0] == 0x13 && tuple[2] == 'C' && tuple[3] == 'I' && tuple[4] == 'S'))
		fError = kError_Card_Tuple_LinkTarget;
	return tuple;
}


// ROM 0x0004d8c8 CisTpl_Vers_1__15TPCMCIA20ParserFRPUc
// The standard's version and up to four strings: the manufacturer, the
// product and two more.
UChar*
TPCMCIA20Parser::CisTpl_Vers_1(UChar*& tuple)
{
	StartTuple(tuple);
	fCard->fV1Major = *IncrAddr(tuple, 1);
	fCard->fV1Minor = *IncrAddr(tuple, 1);
	tuple = tuple + 1;
	if (tuple < fTupleEnd && *tuple != 0xFF)
	{
		fCard->SetCardManufacturer((char*) tuple);
		tuple = tuple + strlen((char*) tuple) + 1;
		if (tuple < fTupleEnd && *tuple != 0xFF)
		{
			fCard->SetCardProduct((char*) tuple);
			tuple = tuple + strlen((char*) tuple) + 1;
			if (tuple < fTupleEnd && *tuple != 0xFF)
			{
				fCard->SetCardV1String3((char*) tuple);
				tuple = tuple + strlen((char*) tuple) + 1;
				if (tuple < fTupleEnd && *tuple != 0xFF)
				{
					fCard->SetCardV1String4((char*) tuple);
					tuple = tuple + strlen((char*) tuple) + 1;
				}
			}
		}
	}
	return tuple;
}


// ROM 0x0004da24 CisTpl_Vers_2__15TPCMCIA20ParserFPUc
// The first data byte's address, the two vendor-specific bytes, and the
// vendor and information strings.
UChar*
TPCMCIA20Parser::CisTpl_Vers_2(UChar* tuple)
{
	StartTuple(tuple);
	IncrAddr(tuple, 1);
	IncrAddr(tuple, 1);
	fCard->fFirstDataByteAddress = (ULong) GetShort(tuple);
	GetShort(tuple);
	fCard->fVendorSpecificV2Bytes[0] = *IncrAddr(tuple, 1);
	fCard->fVendorSpecificV2Bytes[1] = *IncrAddr(tuple, 1);
	IncrAddr(tuple, 1);
	fCard->SetCardV2Vendor((char*) IncrAddr(tuple, 1));
	tuple = tuple + strlen((char*) tuple);
	fCard->SetCardV2Info((char*) IncrAddr(tuple, 1));
	return tuple + strlen((char*) tuple);
}


// ROM 0x0004cbd0 CisTpl_Conf__15TPCMCIA20ParserFPUc
// The configuration registers: where they are (for the current function
// too) and which are there.
UChar*
TPCMCIA20Parser::CisTpl_Conf(UChar* tuple)
{
	StartTuple(tuple);
	ULong sizes = *IncrAddr(tuple, 1);
	ULong reservedSize = GetBits(sizes, 7, 2);
	ULong maskSize = (GetBits(sizes, 5, 4) + 1) & 0xFF;
	ULong addressSize = (GetBits(sizes, 1, 2) + 1) & 0xFF;
	fCard->fConfigurationLastEntryNumber = (UChar) GetBits(*IncrAddr(tuple, 1), 5, 6);
	uint32_t base = *IncrAddr(tuple, 1);
	for (ULong i = 1; i < addressSize; i++)
		base = base + ((uint32_t) *IncrAddr(tuple, 1) << ((i & 0x1F) << 3));
	fCard->fRegisterBaseAddress = base;
	fConfigBase = base;
	if (fFunction != nil)
		fFunction->fRegisterBaseAddress = base;
	for (ULong i = 0; i < maskSize; i++)
	{
		ULong* present = &fCard->fRegistersPresent;
		*present = (uint32_t) (((uint32_t) *IncrAddr(tuple, 1) << ((i & 0x1F) << 3)) + *present);
	}
	for (ULong i = 0; (Long) i < (Long) reservedSize; i = (i + 1) & 0xFF)
		IncrAddr(tuple, 1);
	return tuple;
}


// A little-endian number of `size` bytes (the entry tuple's addresses and
// lengths).
static uint32_t
GetBytes(TPCMCIA20Parser* parser, UChar*& p, Long size)
{
	uint32_t value = 0;
	for (ULong i = 0; (Long) i < size; i = (i + 1) & 0xFF)
		value = value + ((uint32_t) *parser->IncrAddr(p, 1) << ((i & 0x1F) << 3));
	return value;
}


// ROM 0x0004c0dc CisTpl_CE__15TPCMCIA20ParserFPUc
// A configuration entry (CISTPL_CFTABLE_ENTRY): its number, interface, the
// power, timing, I/O, interrupt, memory and miscellaneous features it has
// - starting from the default entry unless it is one itself, when it
// becomes the default.  The functions the entries belong to are given the
// range of entries seen for them.
// ROM BUG kept: an entry may name up to sixteen I/O ranges where the
// configuration keeps eight, so the ninth on write over what follows.
UChar*
TPCMCIA20Parser::CisTpl_CE(UChar* tuple)
{
	StartTuple(tuple);
	TCardConfiguration* config = new TCardConfiguration;
	if (config == nil)
	{
		fError = kError_No_Memory;
		return nil;
	}
	ULong byte = *IncrAddr(tuple, 1);
	ULong hasInterface = GetBits(byte, 7, 1);
	ULong isDefault = GetBits(byte, 6, 1);
	if (fDefaultConfig == nil)
	{
		fDefaultConfig = new TCardConfiguration;
		if (fDefaultConfig == nil)
		{
			fError = kError_No_Memory;
			return nil;
		}
	}
	if (isDefault == 0)
		*config = *fDefaultConfig;
	else
		fDefaultConfig->Clear();
	config->fConfigurationNumber = (UChar) GetBits(byte, 5, 6);
	fCard->AddCardConfiguration(config);
	fCard->fNumOfConfigEntry++;
	if (hasInterface != 0)
	{
		ULong interface = *IncrAddr(tuple, 1);
		config->fInterfaceType = (UChar) GetBits(interface, 3, 4);
		config->fActiveBits = (UChar) GetBits(interface, 7, 4);
	}
	ULong features = *IncrAddr(tuple, 1);
	config->fFeatureSelection |= (UChar) features;
	ULong power = GetBits(features, 1, 2);
	ULong timing = GetBits(features, 2, 1);
	ULong io = GetBits(features, 3, 1);
	ULong irq = GetBits(features, 4, 1);
	ULong memory = GetBits(features, 6, 2);
	ULong misc = GetBits(features, 7, 1);
	if ((Long) power > 0)
	{
		ULong present = *IncrAddr(tuple, 1);
		for (ULong i = 0; i < 7; i = (i + 1) & 0xFF)
			if (GetBits(present, i, 1) != 0)
				tuple = GetPowerValue(tuple, i < 3, &config->fVcc[i], &config->fVccAttr[i]);
	}
	if ((Long) power > 1)
	{
		ULong present = *IncrAddr(tuple, 1);
		for (ULong i = 0; i < 7; i = (i + 1) & 0xFF)
		{
			if (GetBits(present, i, 1) != 0)
				tuple = GetPowerValue(tuple, i < 3, &config->fVpp1[i], &config->fVpp1Attr[i]);
			if (power == 2)
				config->fVpp2[i] = config->fVpp1[i];
		}
	}
	if ((Long) power > 2)
	{
		ULong present = *IncrAddr(tuple, 1);
		for (ULong i = 0; i < 7; i = (i + 1) & 0xFF)
			if (GetBits(present, i, 1) != 0)
				tuple = GetPowerValue(tuple, i < 3, &config->fVpp2[i], &config->fVpp2Attr[i]);
	}
	if (timing != 0)
	{
		ULong scales = *IncrAddr(tuple, 1);
		ULong waitScale = GetBits(scales, 1, 2);
		ULong readyScale = GetBits(scales, 4, 3);
		ULong reservedScale = GetBits(scales, 7, 3);
		if ((Long) waitScale < 3)
		{
			TNanoSecond speed = GetExtendedDeviceSpeed(tuple);
			config->fWaitTimeNSecs = pow(10, waitScale) * speed;
		}
		else
			config->fWaitTimeNSecs = 0;
		if ((Long) readyScale < 7)
		{
			TNanoSecond speed = GetExtendedDeviceSpeed(tuple);
			config->fRdyBsyTimeNSecs = pow(10, readyScale) * speed;
		}
		else
			config->fRdyBsyTimeNSecs = 0;
		if ((Long) reservedScale < 7)
			GetExtendedDeviceSpeed(tuple);
	}
	if (io != 0)
	{
		ULong ioByte = *IncrAddr(tuple, 1);
		config->fIoAddrLines = (UChar) GetBits(ioByte, 4, 5);
		config->fIo8BitOK = (UChar) GetBits(ioByte, 5, 1);
		config->fIo16BitOK = (UChar) GetBits(ioByte, 6, 1);
		if (GetBits(ioByte, 7, 1) != 0)
		{
			ULong ranges = *IncrAddr(tuple, 1);
			ULong count = (GetBits(ranges, 3, 4) + 1) & 0xFF;
			if (fIOFunctions < count)
				fIOFunctions = (UChar) count;
			config->fNumOfIOSpace = (UChar) count;
			Long lengthSize = GetBits(ranges, 7, 2);
			Long addressSize = GetBits(ranges, 5, 2);
			if (lengthSize == 3)
				lengthSize = 4;
			if (addressSize == 3)
				addressSize = 4;
			for (ULong i = 0; i < count; i = (i + 1) & 0xFF)
			{
				uint32_t address = GetBytes(this, tuple, addressSize);
				uint32_t length = GetBytes(this, tuple, lengthSize);
				config->fIoAddresses[i] = address;
				config->fIoLengths[i] = length + 1;
			}
		}
	}
	if (irq != 0)
	{
		UChar info = *IncrAddr(tuple, 1);
		config->fInterruptInfo = info;
		config->fInterruptShare = (Boolean) GetBits(info, 7, 1);
		if (GetBits(info, 4, 1) != 0)
		{
			// the IRQ mask's two bytes, passed over
			IncrAddr(tuple, 1);
			tuple = tuple + 1;
		}
	}
	if (memory == 0)
		config->fNumOfMemMap = 0;
	else if (memory == 1)
	{
		config->fNumOfMemMap = 1;
		config->fMemAddresses[0] = 0;
		config->fMemLengths[0] = (uint32_t) (GetShort(tuple) << 8);
	}
	else if (memory == 2)
	{
		config->fNumOfMemMap = 1;
		config->fMemLengths[0] = (uint32_t) (GetShort(tuple) << 8);
		config->fMemAddresses[0] = (uint32_t) (GetShort(tuple) << 8);
	}
	else if (memory == 3)
	{
		ULong spaces = *IncrAddr(tuple, 1);
		Long lengthSize = GetBits(spaces, 4, 2);
		Long addressSize = GetBits(spaces, 6, 2);
		ULong count = (GetBits(spaces, 2, 3) + 1) & 0xFF;
		config->fNumOfMemMap = (UChar) count;
		ULong hasHost = GetBits(spaces, 7, 1);
		for (ULong i = 0; i < count; i = (i + 1) & 0xFF)
		{
			uint32_t length = GetBytes(this, tuple, lengthSize);
			uint32_t address = GetBytes(this, tuple, addressSize);
			uint32_t host = 0;
			if (hasHost != 0)
				host = GetBytes(this, tuple, addressSize);
			config->fMemAddresses[i] = address << 8;
			config->fMemLengths[i] = length << 8;
			config->fHostAddresses[i] = host << 8;
		}
	}
	if (misc != 0)
	{
		ULong more = *IncrAddr(tuple, 1);
		config->fMiscBits = (UChar) more;
		while (tuple < fTupleEnd && GetBits(more, 7, 1) != 0)
			more = *IncrAddr(tuple, 1);
	}
	if (isDefault != 0)
		*fDefaultConfig = *config;
	ULong functions = fCard->GetNumOfCardFunctions();
	UChar entries = fCard->fNumOfConfigEntry;
	fConfigEntryEnd = (UChar) (entries - 1);
	if (fNewFunction != 0 && functions != 1)
		fConfigEntryStart = (UChar) (entries - 1);
	UChar k = 0;
	if (fIOFunctions != 0)
	{
		do
		{
			functions--;
			TCardFunction* function = fCard->GetCardFunction(functions);
			if (function != nil)
			{
				function->fRegisterBaseAddress = fConfigBase;
				function->fConfigEntryNumberStart = fConfigEntryStart;
				function->fConfigEntryNumberEnd = (UChar) (entries - 1);
			}
			k++;
		} while (k < fIOFunctions);
	}
	fNewFunction = 0;
	return tuple;
}


// ROM 0x0004d288 CisTpl_Manuf_Id__15TPCMCIA20ParserFPUc
UChar*
TPCMCIA20Parser::CisTpl_Manuf_Id(UChar* tuple)
{
	StartTuple(tuple);
	fCard->fManufactureId = (UShort) GetShort(tuple);
	fCard->fManufactureIdInfo = (UShort) GetShort(tuple);
	return tuple;
}


// ROM 0x0004d2e8 CisTpl_Func_Id__15TPCMCIA20ParserFPUc
// A function: its id and system initialisation byte, and a TCardFunction
// that the entries after it fill in.
UChar*
TPCMCIA20Parser::CisTpl_Func_Id(UChar* tuple)
{
	StartTuple(tuple);
	UChar id = *IncrAddr(tuple, 1);
	UChar sysInit = *IncrAddr(tuple, 1);
	TCardPCMCIA* card = fCard;
	card->fFunctionIdAvail = -1;
	card->fFunctionId = id;
	card->fFunctionSysInit = sysInit;
	TCardFunction* function = new TCardFunction;
	if (function == nil)
		fError = kError_No_Memory;
	else
	{
		function->fFuncId = id;
		function->fFuncIdSysInits = sysInit;
		function->fConfigEntryNumberStart = fConfigEntryStart;
		function->fConfigEntryNumberEnd = fConfigEntryEnd;
		function->fRegisterBaseAddress = fConfigBase;
		card->AddCardFunction(function);
		fFunction = function;
		fNewFunction = 1;
		fIOFunctions = 1;
	}
	return tuple;
}


// ROM 0x0004d3c0 CisTpl_Func_Ext__15TPCMCIA20ParserFPUc
// A function extension: kept whole with its function, and the first four
// bytes of the first two in the card too.
UChar*
TPCMCIA20Parser::CisTpl_Func_Ext(UChar* tuple)
{
	StartTuple(tuple);
	ULong size = *tuple;
	TCardPCMCIA* card = fCard;
	if (fFunction != nil)
		fFunction->AddFuncExt(size, tuple + 1);
	if (card->fNumOfFuncExt < 2)
	{
		if (size > 4)
			size = 4;
		for (ULong i = 0; i < size; i++)
			card->fFuncExt[card->fNumOfFuncExt][i] = *IncrAddr(tuple, 1);
		card->fNumOfFuncExt++;
	}
	return tuple;
}


// ROM 0x0004d6fc CisTpl_Vendor_Unique__15TPCMCIA20ParserFPUc
// Apple's package tuple (manufacturer 200, code 0x2000), kept when the
// package is for the Newton OS on an ARM 610.
// ROM QUIRK kept: it answers nil where the others answer where they
// stopped (nothing uses it).
UChar*
TPCMCIA20Parser::CisTpl_Vendor_Unique(UChar* tuple)
{
	StartTuple(tuple);
	if (GetShort(tuple) == 200 && GetShort(tuple) == 0x2000)
	{
		TCardPackage* package = new TCardPackage;
		if (package == nil)
			fError = kError_No_Memory;
		else
		{
			package->fType = *IncrAddr(tuple, 1);
			package->fAttribute = *IncrAddr(tuple, 1);
			package->fAddress = GetWord(tuple);
			package->fLength = GetWord(tuple);
			package->fVersion = GetWord(tuple);
			package->fReserved0 = *IncrAddr(tuple, 1);
			package->fReserved1 = *IncrAddr(tuple, 1);
			char* name = (char*) IncrAddr(tuple, 1);
			char* cpu = name + strlen(name) + 1;
			char* os = cpu + strlen(cpu) + 1;
			if (strcmp(os, kNewtOSString) == 0 && strcmp(cpu, kArmCPU610String) == 0)
			{
				package->SetName(name);
				package->SetCPUType(cpu);
				package->SetOSType(os);
				fCard->AddCardPackage(package);
				fCard->fNumOfPackage++;
			}
			else
				delete package;
		}
	}
	return nil;
}


/*------------------------------------------------------------------------------
	T h e   C I S
------------------------------------------------------------------------------*/

// ROM 0x0004db38 ProcessTuple__15TPCMCIA20ParserFPUcN21Uc
// A tuple to its handler.  where: the tuple's address on the card; base:
// the start of its space.  ==> kError_Card_Tuple_Unknown (in fError) for a
// tuple it does not know, which is counted.
// ROM QUIRK kept: CISTPL_ALTSTR (0x16), CISTPL_DEVICE_OA (0x1D),
// CISTPL_DEVICE_GEO_A (0x1F) and 0x43-0x45 are all read as an attribute
// memory other-conditions device list; 0x41, 0x42 and 0x46 are passed
// over.
UChar*
TPCMCIA20Parser::ProcessTuple(UChar* tuple, UChar* base, UChar* where, UChar inAttrMemory)
{
	fTupleCount++;
	UChar code = tuple[0];
	UChar* result = nil;
	switch (code)
	{
	case 0x00:	return CisTpl_Null(tuple);
	case 0x01:	return CisTpl_Device(tuple);
	case 0x10:	return CisTpl_Checksum(tuple, base, where - base, inAttrMemory);
	case 0x11:	return CisTpl_LongLink_A(tuple);
	case 0x12:	return CisTpl_LongLink_C(tuple);
	case 0x13:	return CisTpl_LinkTarget(tuple, where);
	case 0x14:	return CisTpl_No_Link(tuple);
	case 0x15:	return CisTpl_Vers_1(tuple);
	case 0x17:	return CisTpl_Device_A(tuple);
	case 0x18:	return CisTpl_Jedec_C(tuple);
	case 0x19:	return CisTpl_Jedec_A(tuple);
	case 0x1A:	return CisTpl_Conf(tuple);
	case 0x1B:	return CisTpl_CE(tuple);
	case 0x1C:	return DeviceParser(tuple, 0, 1);
	case 0x16:
	case 0x1D:
	case 0x1F:
	case 0x43:
	case 0x44:
	case 0x45:	return DeviceParser(tuple, 1, 1);
	case 0x1E:	return CisTpl_Device_GEO(tuple);
	case 0x20:	return CisTpl_Manuf_Id(tuple);
	case 0x21:	return CisTpl_Func_Id(tuple);
	case 0x22:	return CisTpl_Func_Ext(tuple);
	case 0x40:	return CisTpl_Vers_2(tuple);
	case 0x41:
	case 0x42:
	case 0x46:	return nil;
	case 0x8E:	return CisTpl_Vendor_Unique(tuple);
	case 0xFF:	return CisTpl_End(tuple);
	}
	fError = kError_Card_Tuple_Unknown;
	fCard->fNumOfUnknownTuples++;
	return result;
}


// ROM 0x0004ddc0 ProcessCIS__15TPCMCIA20ParserFPUcT1
// Every tuple of every CIS (a function-specific TCardPCMCIA made for each
// function's after the first), read with a TCardCISIterator and handed to
// ProcessTuple.  A CIS stops at its end, after more than 40 tuples or 128
// nulls, or at an error - an unknown tuple being passed over until there
// have been sixteen, a tuple too short simply passed over.
NewtonErr
TPCMCIA20Parser::ProcessCIS(UChar* attrMem, UChar* commonMem)
{
	UChar buffer[0x100];
	TCardCISIterator iterator;
	NewtonErr err = iterator.Init(fSocket);
	if (err != noErr)
	{
		fError = err;
		fCard->fBadCIS = -1;
	}
	for (ULong cisNumber = 0; ; cisNumber++)
	{
		if (err != noErr || cisNumber >= iterator.fNumOfCISs)
			return err;
		if (cisNumber != 0)
		{
			iterator.SelectCIS(cisNumber);
			err = fCard->AddFuncSpecificCIS();
			TCardPCMCIA* cis = fCard->GetCardCIS(cisNumber);
			fCard = cis;
			if (err != noErr || cis == nil)
				return err;
			Reset();
		}
		Boolean first = true;
		Boolean done = false;
		for (;;)
		{
			if (err != noErr || done)
				break;
			err = iterator.GetTuple(first);
			if (err == noErr)
			{
				first = false;
				err = iterator.GetTupleData(buffer, 0x100);
				if (err != noErr)
					continue;
				UChar* base = (iterator.GetStatus() & kCISStatusInAttrMemory) != 0 ? attrMem : commonMem;
				ProcessTuple(buffer, base, iterator.fTupleAddress, (UChar) (iterator.GetStatus() & kCISStatusInAttrMemory));
				NewtonErr tupleErr = fError;
				if (tupleErr == noErr)
				{
					if (fNullTuples <= 0x80 && fTupleCount <= 0x28)
						continue;
					tupleErr = err;
				}
				else if (tupleErr == kError_Card_Tuple_Unknown)
				{
					if (fCard->fNumOfUnknownTuples < 0x10)
						continue;
					fCard->fTooManyUnknownTuples = -1;
					tupleErr = err;
				}
				else if (tupleErr == kError_Card_Tuple_Size)
					continue;
				done = true;
				err = tupleErr;
				continue;
			}
			if (err == kError_Card_No_CIS)
			{
				if (!first)
					err = noErr;
				break;
			}
		}
	}
}


// ROM 0x0004df88 ValidateCIS__15TPCMCIA20ParserFPUcT1P11TCardPCMCIAP11TCardSocket
// Whether the card has a CIS worth reading: after any nulls, a
// CISTPL_DEVICE with a link, or - on a card with no attribute memory, whose
// attribute space ends at once - a CISTPL_LINKTARGET at the start of common
// memory.  A card whose common and attribute memory both start 03 03 01 01
// has its CIS mirrored in common memory.  ==> kError_Card_Blank_CIS,
// kError_Card_Bad_CIS or kError_Card_No_CIS (fBadCIS set).
NewtonErr
TPCMCIA20Parser::ValidateCIS(UChar* attrMem, UChar* commonMem, TCardPCMCIA* card, TCardSocket* /*socket*/)
{
	UChar buffer[0x100];
	Boolean mirrored = commonMem[0] == 3 && commonMem[1] == 3 && commonMem[2] == 1 && commonMem[3] == 1
					&& attrMem[1] == 3 && attrMem[3] == 1;
	fCard->f16BitOnlyCard = mirrored ? -1 : 0;
	if (mirrored)
		fSocket->SetControl(fSocket->GetControl() & ~kCardByteAccess);
	ULong i = 0;
	UChar code;
	for (;;)
	{
		code = CardAttrMemReadByte(Lane(attrMem + i));
		if (i + 2 > 0xF || code != 0)
			break;
		i += 2;
	}
	if (code == 0)
		fError = kError_Card_Blank_CIS;
	else
	{
		if (code == 0x01)
			fError = CardAttrMemReadByte(Lane(attrMem + i + 2)) == 0 ? kError_Card_Bad_CIS : noErr;
		else if (code != 0xFF)
		{
			fError = kError_Card_Bad_CIS;
			card->fBadCIS = -1;
			return fError;
		}
		else
		{
			card->fNoAttrMem = -1;
			GetTuple(commonMem, buffer, 0);
			CisTpl_LinkTarget(buffer, commonMem);
			if (fError != noErr)
				fError = kError_Card_No_CIS;
		}
		if (fError == noErr)
			return fError;
	}
	card->fBadCIS = -1;
	return fError;
}


// ROM 0x0004e114 ParsePCCardCIS__15TPCMCIA20ParserFPUcT1P11TCardPCMCIAP11TCardSocket
// The card in a socket parsed into card, which is cleared first.  An abort
// reading the card is kError_Access_Permission, kError_Write_Protected or
// kError_Bus_Access.
NewtonErr
TPCMCIA20Parser::ParsePCCardCIS(UChar* attrMem, UChar* commonMem, TCardPCMCIA* card, TCardSocket* socket)
{
	NewtonErr err = noErr;
	newton_try
	{
		fCard = card;
		fSocket = socket;
		card->Clear();
		err = Reset();
		if (err == noErr && (err = ValidateCIS(attrMem, commonMem, card, socket)) == noErr)
			err = ProcessCIS(attrMem, commonMem);
	}
	newton_catch(exPermissionViolation)
	{
		err = kError_Access_Permission;
	}
	newton_catch(exWriteProtected)
	{
		err = kError_Write_Protected;
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	return err;
}


// ROM 0x0004e23c ParsePCCardCIS__15TPCMCIA20ParserFP11TCardPCMCIAP11TCardSocket
NewtonErr
TPCMCIA20Parser::ParsePCCardCIS(TCardPCMCIA* card, TCardSocket* socket)
{
	UChar* common = (UChar*) socket->CommonMemBaseAddr();
	UChar* attr = (UChar*) socket->AttributeMemBaseAddr();
	return ParsePCCardCIS(attr, common, card, socket);
}
