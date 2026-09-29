/*
	File:		comms/irda/IrQOS.cpp

	Contains:	TIrQOS (IrQOS.h).

	Reconstructed from the MP2x00 US ROM (0x000f7e74-0x000f852c); each
	function cites its origin.
*/

#include "IrQOS.h"
#include "BufferSegment.h"
#include "CommErrors.h"
#include "NewtonTime.h"


// ROM 0x000f7e74 __ct__6TIrQOSFv
TIrQOS::TIrQOS()
{
	Reset();
}


// ROM 0x000f7ea8 __dt__6TIrQOSFv
TIrQOS::~TIrQOS()
{ }


// ROM 0x000f82cc Reset__6TIrQOSFv
// What a MessagePad can do: 9600 to 115200 bps, 500 ms, 64 to 512 bytes,
// one frame, 0 extra BOFs at 115200, 5 ms, any threshold.
void
TIrQOS::Reset(void)
{
	fBaudRate = 0x3e;
	fMaxTurnTime = 1;
	fDataSize = 0xf;
	fWindowSize = 1;
	fExtraBOFs = 0x20;
	fMinTurnTime = 2;
	fLinkDiscThreshold = 0xff;
}


// ROM 0x000f82a4 HighestBitOn__6TIrQOSFUc
// ==> the highest bit set (7..0), -1 for none.
int
TIrQOS::HighestBitOn(UByte bits)
{
	int n = 7;
	for (UByte bit = 0x80; bit != 0; bit >>= 1, n--)
		if (bits & bit)
			return n;
	return n;
}


// ROM 0x000f84bc GetBaudRate__6TIrQOSFv
ULong
TIrQOS::GetBaudRate(void)
{
	return IrBaudRateTable[HighestBitOn(fBaudRate) - 1];
}


// ROM 0x000f84e4 GetMaxTurnAroundTime__6TIrQOSFv
ULong
TIrQOS::GetMaxTurnAroundTime(void)
{
	return IrMaxTurnTimeTable[HighestBitOn(fMaxTurnTime)];
}


// ROM 0x000f8508 GetDataSize__6TIrQOSFv
ULong
TIrQOS::GetDataSize(void)
{
	return (1 << HighestBitOn(fDataSize)) << 6;
}


// ROM 0x000f7eb4 GetWindowSize__6TIrQOSFv
ULong
TIrQOS::GetWindowSize(void)
{
	return HighestBitOn(fWindowSize) + 1;
}


// ROM 0x000f7ed0 GetExtraBOFs__6TIrQOSFv
// The table's count is for 115200 bps; slower lines need fewer.
ULong
TIrQOS::GetExtraBOFs(void)
{
	ULong bofs = IrExtraBOFsTable[HighestBitOn(fExtraBOFs)];
	return (GetBaudRate() * bofs) / 115200;
}


// ROM 0x000f7f10 GetMinTurnAroundTime__6TIrQOSFv
ULong
TIrQOS::GetMinTurnAroundTime(void)
{
	return IrMinTurnTimeTable[HighestBitOn(fMinTurnTime)];
}


// ROM 0x000f7f34 GetLinkDiscThresholdTime__6TIrQOSFv
ULong
TIrQOS::GetLinkDiscThresholdTime(void)
{
	return IrLinkDiscThreshold[HighestBitOn(fLinkDiscThreshold)];
}


// ROM 0x000f7f58 AddInfoToBuffer__6TIrQOSFPUcUl
// The parameters as a SNRM's or UA's: normalised first.  ==> their length.
// (The size is not looked at.)
ULong
TIrQOS::AddInfoToBuffer(UByte* buffer, ULong size)
{
	NormalizeInfo();
	UByte* p = buffer;
	*p++ = 0x01;	*p++ = 1;	*p++ = fBaudRate;
	*p++ = 0x82;	*p++ = 1;	*p++ = fMaxTurnTime;
	*p++ = 0x83;	*p++ = 1;	*p++ = fDataSize;
	*p++ = 0x84;	*p++ = 1;	*p++ = fWindowSize;
	*p++ = 0x85;	*p++ = 1;	*p++ = fExtraBOFs;
	*p++ = 0x86;	*p++ = 1;	*p++ = fMinTurnTime;
	*p++ = 0x08;	*p++ = 1;	*p = fLinkDiscThreshold;
	return 0x15;
}


// ROM 0x000f8004 ExtractInfoFromBuffer__6TIrQOSFP14CBufferSegment
// The other station's parameters: what the standard says to assume for one
// not given (9600 bps and the least of everything), then each one read
// (a longer one's extra bytes skipped).
NewtonErr
TIrQOS::ExtractInfoFromBuffer(CBufferSegment* buffer)
{
	fBaudRate = 2;
	fMaxTurnTime = 1;
	fDataSize = 1;
	fWindowSize = 1;
	fExtraBOFs = 1;
	fMinTurnTime = 1;
	fLinkDiscThreshold = 0xff;
	for (;;)
	{
		UByte p[3];
		if (buffer->Getn(p, 3) != 3)
			return noErr;
		UByte value = p[2];
		switch (p[0])
		{
		case 0x01:	if (value & 0x3f) fBaudRate = value & 0x3f;				break;
		case 0x08:	if (value) fLinkDiscThreshold = value;					break;
		case 0x82:	if (value & 0xf) fMaxTurnTime = value & 0xf;			break;
		case 0x83:	if (value & 0x3f) fDataSize = value & 0x3f;				break;
		case 0x84:	if (value & 0x7f) fWindowSize = value & 0x7f;			break;
		case 0x85:	if (value) fExtraBOFs = value;							break;
		case 0x86:	if (value) fMinTurnTime = value;						break;
		}
		if (p[1] > 1)
			buffer->Seek(p[1] - 1, kSeekFromHere);
	}
}


// ROM 0x000f8128 NegotiateWith__6TIrQOSFP6TIrQOS
// What both can do of the baud rates and link thresholds (the other
// parameters are each station's own); nothing in common is a protocol
// error.
NewtonErr
TIrQOS::NegotiateWith(TIrQOS* other)
{
	fBaudRate &= other->fBaudRate;
	fLinkDiscThreshold &= other->fLinkDiscThreshold;
	if (fBaudRate == 0 || fLinkDiscThreshold == 0)
		return kIrDAErrProtocolError;
	return NormalizeInfo();
}


// ROM 0x000f8168 NormalizeInfo__6TIrQOSFv
// The data and window sizes cut down (the window first, down to its lowest
// size; then the data) until a window of frames, their extra BOFs and six
// bytes of framing each, and the minimum turn-around fit the line's
// capacity in the turn-around time.
NewtonErr
TIrQOS::NormalizeInfo(void)
{
	NewtonErr err = noErr;
	ULong minTurnBytes = IrMinTurnInBytesTable[(HighestBitOn(fBaudRate) - 1) * 8 + HighestBitOn(fMinTurnTime)];
	ULong capacity;
	if (GetBaudRate() == 115200)
		capacity = IrMaxLineCapacityTable2[HighestBitOn(fMaxTurnTime)];
	else
		capacity = IrMaxLineCapacityTable1[HighestBitOn(fBaudRate) - 1];
	ULong bofs = GetExtraBOFs();
	int lowest;
	for (lowest = 0; lowest < 8; lowest++)
		if (fWindowSize & (1 << lowest))
			break;
	for (;;)
	{
		ULong frame = GetDataSize() + bofs + 6;
		if (frame * GetWindowSize() + minTurnBytes < capacity)
			break;
		int highest = HighestBitOn(fWindowSize);
		if (highest != lowest)
		{
			fWindowSize &= ~(1 << highest);
			continue;
		}
		fDataSize &= ~(1 << HighestBitOn(fDataSize));
		if (fDataSize == 0)
		{
			err = kIrDAErrProtocolError;
			break;
		}
	}
	return err;
}


// ROM 0x000f8304 SetBaudRate__6TIrQOSFUl
// Every rate up to the one given (115200 and the faster rates the ROM
// knows of are all 115200 at most).
NewtonErr
TIrQOS::SetBaudRate(ULong rate)
{
	UByte bits = 0;
	switch (rate)
	{
	case 115200:
	case 576000:
	case 1152000:
	case 4000000:
		bits = 0x20;
		// fall through
	case 57600:		bits |= 0x10;
		// fall through
	case 38400:		bits |= 0x08;
		// fall through
	case 19200:		bits |= 0x04;
		// fall through
	case 9600:		fBaudRate = bits | 0x02;
		return noErr;
	}
	return kCommErrBadParameter;
}


// ROM 0x000f8384 SetDataSize__6TIrQOSFUl
NewtonErr
TIrQOS::SetDataSize(ULong size)
{
	UByte bits = 0;
	switch (size)
	{
	case 2048:	bits = 0x20;
		// fall through
	case 1024:	bits |= 0x10;
		// fall through
	case 512:	bits |= 0x08;
		// fall through
	case 256:	bits |= 0x04;
		// fall through
	case 128:	bits |= 0x02;
		// fall through
	case 64:	fDataSize = bits | 0x01;
		return noErr;
	}
	return kCommErrBadParameter;
}


// ROM 0x000f83f0 SetWindowSize__6TIrQOSFUl
NewtonErr
TIrQOS::SetWindowSize(ULong size)
{
	if (size - 1 >= 7)
		return kCommErrBadParameter;
	fWindowSize = (0x7f >> (6 - (size - 1))) & 0x7f;
	return noErr;
}


// ROM 0x000f8420 SetLinkDiscThresholdTime__6TIrQOSFUl
// Every threshold up to the one given (in seconds of ticks).
NewtonErr
TIrQOS::SetLinkDiscThresholdTime(ULong time)
{
	UByte bits = 0;
	switch (time / kSeconds)
	{
	case 40:	bits = 0x80;
		// fall through
	case 30:	bits |= 0x40;
		// fall through
	case 25:	bits |= 0x20;
		// fall through
	case 20:	bits |= 0x10;
		// fall through
	case 16:	bits |= 0x08;
		// fall through
	case 12:	bits |= 0x04;
		// fall through
	case 8:		bits |= 0x02;
		// fall through
	case 3:		fLinkDiscThreshold = bits | 0x01;
		return noErr;
	}
	return kCommErrBadParameter;
}
