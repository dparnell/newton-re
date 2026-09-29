/*
	File:		comms/irda/IrDscInfo.cpp

	Contains:	TIrDscInfo (IrDscInfo.h).

	Reconstructed from the MP2x00 US ROM (0x000ef35c-0x000ef538); each
	function cites its origin.
*/

#include "IrDscInfo.h"
#include "BufferSegment.h"
#include "CommErrors.h"

#include <string.h>

// ROM 0x00371784 kIASDeviceNameNewton
const char	kIASDeviceNameNewton[] = "Newton";


// ROM 0x000ef35c __ct__10TIrDscInfoFv
// A PDA, ASCII, "Newton".  (Its address is the discovery's to set.)
TIrDscInfo::TIrDscInfo()
{
	fHints = 2;
	fCharSet = 0;
	SetNickname(kIASDeviceNameNewton);
}


// ROM 0x000ef3a8 __dt__10TIrDscInfoFv
TIrDscInfo::~TIrDscInfo()
{ }


// ROM 0x000ef3b4 SetNickname__10TIrDscInfoFPCUc
NewtonErr
TIrDscInfo::SetNickname(const char* name)
{
	if (strlen(name) > 0x15)
		return kCommErrBadParameter;
	strcpy(fNickname, name);
	return noErr;
}


// ROM 0x000ef3f4 AddDevInfoToBuffer__10TIrDscInfoFPUcUl
// The hints (at most four bytes), the character set and as much of the
// nickname as fits the 23 bytes.  ==> the length.  (The size is not read.)
ULong
TIrDscInfo::AddDevInfoToBuffer(UByte* buffer, ULong size)
{
	ULong hints = fHints;
	ULong count = 0;
	for (int i = 0; ; )
	{
		UByte b = hints & 0xff;
		hints >>= 8;
		if (hints != 0)
			b |= 0x80;
		*buffer++ = b;
		count++;
		if (hints == 0)
			break;
		if (++i >= 4)
			break;
	}
	*buffer++ = fCharSet;
	ULong header = count + 1;
	ULong length = strlen(fNickname);
	ULong room = 0x15 - (header - 2);
	if ((long) length >= (long) room)
		length = room;
	memcpy(buffer, fNickname, length);
	return header + length;
}


// ROM 0x000ef480 ExtractDevInfoFromBuffer__10TIrDscInfoFP14CBufferSegment
// The other station's: hints, character set and nickname out of the XID's
// discovery information.
NewtonErr
TIrDscInfo::ExtractDevInfoFromBuffer(CBufferSegment* buffer)
{
	UByte info[0x1c];
	fHints = 0;
	fNickname[0] = 0;
	ULong count = buffer->Getn(info, 0x1a);
	ULong i = 0;
	while (i < count)
	{
		UByte b = info[i];
		fHints |= (ULong) (b & 0x7f) << (i * 8);
		i++;
		if (!(b & 0x80))
			break;
	}
	if (i < count)
		fCharSet = info[i++];
	// ROM BUG: up to 26 bytes are read, so a nickname longer than 21
	// characters runs past fNickname's 22 (into what follows the object)
	memcpy(fNickname, info + i, count - i);
	fNickname[count - i] = 0;
	return noErr;
}
