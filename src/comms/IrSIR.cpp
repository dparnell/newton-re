/*
	File:		comms/IrSIR.cpp

	Contains:	TIrSIR and TIrLAPPutBuffer (IrSIR.h).

	Reconstructed from the MP2x00 US ROM (0x000f852c-0x000f8bc4,
	0x000f54a4-0x000f5698); each function cites its origin.
*/

#include "IrSIR.h"

#define kSIRBOF		0xc0
#define kSIREOF		0xc1
#define kSIRCE		0x7d			// the escape
#define kSIRGoodFCS	0xf0b8			// the CRC over a frame and its own FCS


/*------------------------------------------------------------------------------
	TIrLAPPutBuffer
------------------------------------------------------------------------------*/

// ROM 0x000f54a4 __ct__15TIrLAPPutBufferFv
TIrLAPPutBuffer::TIrLAPPutBuffer()
{
	Init();
}


// ROM 0x000f54e4 __dt__15TIrLAPPutBufferFv
TIrLAPPutBuffer::~TIrLAPPutBuffer()
{ }


// ROM 0x000f54fc Init__15TIrLAPPutBufferFv
void
TIrLAPPutBuffer::Init(void)
{
	fControl = nil;
	fControlSize = 0;
	fControlIndex = 0;
	fData = nil;
	fDataOffset = 0;
	fDataSize = 0;
	fDataIndex = 0;
}


// ROM 0x000f5520 SetControlBuffer__15TIrLAPPutBufferFPUcUlUc
void
TIrLAPPutBuffer::SetControlBuffer(UByte* buffer, ULong size, Boolean reset)
{
	if (reset)
		Init();
	fControl = buffer;
	fControlSize = size;
	fControlIndex = 0;
}


// ROM 0x000f5558 SetDataBuffer__15TIrLAPPutBufferFP7CBufferUlT2
void
TIrLAPPutBuffer::SetDataBuffer(CBuffer* buffer, ULong offset, ULong size)
{
	fData = buffer;
	fDataOffset = offset;
	fDataSize = size;
	fDataIndex = 0;
}


// ROM 0x000f5570 Get__15TIrLAPPutBufferFv
// The next byte: the control part's, then the data's.  (Past the end,
// 0xff.)
int
TIrLAPPutBuffer::Get(void)
{
	if (fControlIndex < fControlSize)
		return fControl[fControlIndex++];
	if (fDataIndex >= fDataSize)
		return 0xff;
	int c = fData->Get() & 0xff;
	fDataIndex++;
	return c;
}


// ROM 0x000f55dc Seek__15TIrLAPPutBufferFli
// From the beginning (dir -1): back to the start of both parts.  Anything
// else steps back one byte - the offset is not looked at.
void
TIrLAPPutBuffer::Seek(Long offset, int dir)
{
	if (dir == kSeekFromBeginning)
	{
		fControlIndex = 0;
		if (fData == nil)
			return;
		fData->Seek(fDataOffset, kSeekFromBeginning);
		fDataIndex = 0;
		return;
	}
	if (fDataIndex != 0)
	{
		fData->Seek(-1, kSeekFromHere);
		fDataIndex--;
	}
	else if (fControlIndex != 0)
		fControlIndex--;
}


// ROM 0x000f5670 AtEOF__15TIrLAPPutBufferCFv
Boolean
TIrLAPPutBuffer::AtEOF(void) const
{
	return fControlIndex == fControlSize && fDataSize == fDataIndex;
}


/*------------------------------------------------------------------------------
	TIrSIR
------------------------------------------------------------------------------*/

// ROM 0x000f852c __ct__6TIrSIRFP10TCircleBufT1
TIrSIR::TIrSIR(TCircleBuf* inBuf, TCircleBuf* outBuf)
{
	fCRC.Reset();
	fInBuf = inBuf;
	fOutBuf = outBuf;
	Reset();
}


// ROM 0x000f8588 __dt__6TIrSIRFv
TIrSIR::~TIrSIR()
{ }


// ROM 0x000f85a0 ReceivingInput__6TIrSIRFv
// Something coming in: bytes waiting, the medium busy, a frame started.
Boolean
TIrSIR::ReceivingInput(void)
{
	return fInBuf->BufferCount() != 0 || fMediaBusy || fInFrame || fRxCount != 0;
}


// ROM 0x000f85e4 SetMediaBusy__6TIrSIRFUc
void
TIrSIR::SetMediaBusy(Boolean busy)
{
	fMediaBusy = busy;
}


// ROM 0x000f8bbc MediaBusy__6TIrSIRFv
Boolean
TIrSIR::MediaBusy(void)
{
	return fMediaBusy;
}


// ROM 0x000f85ec ValidFrameAddress__6TIrSIRFUc
// To all (0x7f) or to this station (the address's top seven bits).
Boolean
TIrSIR::ValidFrameAddress(UByte address)
{
	return (address >> 1) == 0x7f || (address >> 1) == fAddress;
}


// ROM 0x000f8610 CopyStatsTo__6TIrSIRFP15TCMOSlowIRStats
void
TIrSIR::CopyStatsTo(TCMOSlowIRStats* stats)
{
	stats->dataPacketsIn = fStats.dataPacketsIn;
	stats->dataPacketsOut = fStats.dataPacketsOut;
	stats->checkSumErrs = fStats.checkSumErrs;
	stats->serialErrs = fStats.serialErrs;
}


// ROM 0x000f8634 ResetStats__6TIrSIRFv
void
TIrSIR::ResetStats(void)
{
	fStats.dataPacketsIn = 0;
	fStats.dataPacketsOut = 0;
	fStats.checkSumErrs = 0;
	fStats.serialErrs = 0;
}


// ROM 0x000f864c Reset__6TIrSIRFv
void
TIrSIR::Reset(void)
{
	fXBOFChar = 0xff;
}


// ROM 0x000f8658 StartTransmit__6TIrSIRFP15TIrLAPPutBufferUl
void
TIrSIR::StartTransmit(TIrLAPPutBuffer* frame, ULong extraBOFs)
{
	fFrame = frame;
	frame->Seek(0, kSeekFromBeginning);
	fXBOFs = extraBOFs;
	fTxState = 0;
}


// ROM 0x000f8690 FillOutputBuffer__6TIrSIRFv
// The frame into the output buffer, as far as it goes: the extra BOFs, BOF,
// the frame escaped as the CRC is worked out, the CRC, EOF.  ==> 1 while
// there is more (and once when it has all gone in), 0 once it was all in
// last time (and counted); the buffer's answer when it fails otherwise
// (ROM QUIRK: during the extra BOFs, even when it is only full).
ULong
TIrSIR::FillOutputBuffer(void)
{
	ULong result = 0;
	switch (fTxState)
	{
	case 0:
		for ( ; fXBOFs > 0; fXBOFs--)
		{
			result = fOutBuf->PutNextByte(fXBOFChar);
			if (result != kCircleBufOK)
				return result;
		}
		fTxState = 1;
		// fall through
	case 1:
		result = fOutBuf->PutNextByte(kSIRBOF);
		if (result != kCircleBufOK)
			break;
		fCRC.Reset();
		fTxState = 2;
		// fall through
	case 2:
		while (!fFrame->AtEOF())
		{
			UByte c = fFrame->Get();
			result = EscapePutChar(c);
			if (result != kCircleBufOK)
			{
				fFrame->Seek(-1, kSeekFromHere);
				goto done;
			}
			fCRC.ComputeCRC(c);
		}
		fCRC.Finalize();
		fTxState = 3;
		// fall through
	case 3:
		fCRC.Get();
		result = EscapePutChar(fCRC.fResult[1]);
		if (result != kCircleBufOK)
			break;
		fTxState = 4;
		// fall through
	case 4:
		fCRC.Get();
		result = EscapePutChar(fCRC.fResult[0]);
		if (result != kCircleBufOK)
			break;
		fTxState = 5;
		// fall through
	case 5:
		result = fOutBuf->PutNextByte(kSIREOF);
		if (result == kCircleBufOK)
		{
			fTxState = 6;
			return 1;
		}
		break;
	case 6:
		fStats.dataPacketsOut++;
		return 0;
	default:
		break;
	}
done:
	if (result == kCircleBufFull)
		return 1;
	return result;
}


// ROM 0x000f8834 EscapePutChar__6TIrSIRFUc
// A byte out, escaped if it is BOF, EOF or the escape - so two bytes' room
// is wanted.  ==> kCircleBufFull without it.
ULong
TIrSIR::EscapePutChar(UByte c)
{
	if (fOutBuf->BufferSpace() < 2)
		return kCircleBufFull;
	if (c == kSIRBOF || c == kSIREOF || c == kSIRCE)
	{
		c ^= 0x20;
		fOutBuf->PutNextByte(kSIRCE);
	}
	return fOutBuf->PutNextByte(c);
}


// ROM 0x000f8890 StartReceive__6TIrSIRFP14CBufferSegmentUcT2
void
TIrSIR::StartReceive(CBufferSegment* buffer, UByte address, Boolean keepLong)
{
	fRxBuffer = buffer;
	fAddress = address;
	fKeepLong = keepLong;
	fInFrame = false;
	InitReceiveState();
	fMediaBusy = false;
	fLastByte = 0xff;
}


// ROM 0x000f8b7c InitReceiveState__6TIrSIRFv
void
TIrSIR::InitReceiveState(void)
{
	fEscaped = false;
	fRxCount = 0;
	fOverflow = 0;
	fCRC.Reset();
	fRxBuffer->Seek(0, kSeekFromBeginning);
}


// ROM 0x000f88d0 EmptyInputBuffer__6TIrSIRFv
// The input read for a frame.  ==> 0 when a good one is in the buffer (its
// data, the FCS hidden, the buffer rewound), 1 when the input has run out
// first.  Anything but a good frame for us marks the medium busy.
ULong
TIrSIR::EmptyInputBuffer(void)
{
	UByte byte = 0;
	Boolean first = true;
	for (;;)
	{
		UByte previous = first ? fLastByte : byte;
		first = false;
		ULong value;
		ULong result = fInBuf->GetNextByte(&byte, &value);
		if (result == kCircleBufEmpty)
		{
			fLastByte = previous;
			return 1;
		}
		if (result == kCircleBufEOM)
		{
			fStats.serialErrs++;
			fMediaBusy = true;
			continue;
		}
		if (byte == kSIRCE)
		{
			if (fInFrame)
				fEscaped = true;
			else
				fMediaBusy = true;
			continue;
		}
		if (byte == kSIRBOF)
		{
			if (fInFrame && fRxCount > 0)
				fMediaBusy = true;
			fInFrame = true;
			InitReceiveState();
			fByteBeforeBOF = previous;
			continue;
		}
		if (byte == kSIREOF)
		{
			if (!fInFrame)
				continue;
			fInFrame = false;
			fCRC.Get();
			if ((ULong) ((fCRC.fResult[0] << 8) | fCRC.fResult[1]) != kSIRGoodFCS)
			{
				fStats.checkSumErrs++;
				InitReceiveState();
				fMediaBusy = true;
				continue;
			}
			if (fEscaped || fRxCount < 2 || !ValidFrameAddress(fRxAddress))
			{
				fMediaBusy = true;
				fEscaped = false;
				InitReceiveState();
				continue;
			}
			// the frame: its data less the FCS (as much of it as fitted)
			CBufferSegment* buffer = fRxBuffer;
			Size size = buffer->GetSize();
			Size position = buffer->Position();
			buffer->Hide(size - (position - (2 - fOverflow)), kSeekFromEnd);
			fRxBuffer->Seek(0, kSeekFromBeginning);
			fMediaBusy = false;
			fStats.dataPacketsIn++;
			if (fRxControl == 0x73)		// SNRM: the other side's extra BOFs are what it led with
				fXBOFChar = (fByteBeforeBOF == 0xff || fByteBeforeBOF == kSIRBOF) ? fByteBeforeBOF : 0xff;
			return 0;
		}
		if (!fInFrame)
		{
			fMediaBusy = true;
			continue;
		}
		if (fEscaped)
		{
			fEscaped = false;
			byte ^= 0x20;
		}
		fCRC.ComputeCRC(byte);
		fRxCount++;
		if (fRxCount == 1)
			fRxAddress = byte;
		else if (fRxCount == 2)
			fRxControl = byte;
		else if (!fRxBuffer->AtEOF())
			fRxBuffer->Put(byte);
		else
		{
			fOverflow++;
			if (fOverflow > 2 && !fKeepLong)
			{
				fInFrame = false;
				InitReceiveState();
				fMediaBusy = true;
			}
		}
	}
}
