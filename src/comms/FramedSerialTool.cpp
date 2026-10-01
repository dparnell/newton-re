/*
	File:		comms/FramedSerialTool.cpp

	Contains:	TFramedAsyncSerTool and TFramedAsyncService (SerialTool.h),
				and the framing options.

	Reconstructed from the MP2x00 US ROM (0x000d2ecc-0x000d39c8); each
	function cites its origin.
*/

#include "SerialTool.h"
#include "NewtErrors.h"
#include "CommErrors.h"

#define OPTION_DATA_LENGTH(cls)	(sizeof(cls) - sizeof(TOption))

// the framing's fixed characters (the escape and end characters are the
// options')
#define chSYN	0x16
#define chDLE	0x10
#define chSTX	0x02

// the states of a framed put (fOutState)
enum
{
	kOutHeader,			// 0: SYN DLE STX, the CRC started
	kOutData,			// 1: the frame's bytes, each escape doubled
	kOutEscape,			// 2: the escape doubled
	kOutEndEscape,		// 3: DLE of DLE ETX
	kOutEnd,			// 4: ETX
	kOutCRC1,			// 5: the CRC's low byte
	kOutCRC2,			// 6: and its high one
	kOutDone,			// 7: the frame sent
	kOutError			// 8: the put's data could not be taken
};

// the states of a framed get (fInState)
enum
{
	kInSYN,				// 0: looking for SYN
	kInDLE,				// 1: DLE
	kInSTX,				// 2: STX
	kInData,			// 3: the frame's bytes
	kInEscape,			// 4: an escape: ETX ends the frame, another escape is one
	kInCRC1,			// 5
	kInCRC2,			// 6
	kInDone				// 7: the frame into the get
};


// ROM 0x001dde50 __ct__16TCMOFramingParmsFv
// 'fram: DLE and ETX, the header, the CRC both ways.
TCMOFramingParms::TCMOFramingParms()
	: TOption(kOptionType)
{
	SetLabel(kCMOFramingParms);
	SetLength(OPTION_DATA_LENGTH(TCMOFramingParms));
	escapeChar = chDLE;
	eomChar = 0x03;
	doHeader = true;
	doOutFCS = true;
	doInFCS = true;
}


// ROM 0x001ddebc __ct__20TCMOFramedAsyncStatsFv
TCMOFramedAsyncStats::TCMOFramedAsyncStats()
	: TOption(kOptionType)
{
	SetLabel(kCMOFramedAsyncStats);
	SetLength(OPTION_DATA_LENGTH(TCMOFramedAsyncStats));
	preHeaderByteCnt = 0;
}


// ROM 0x000d2ecc __ct__19TFramedAsyncSerToolFUl
TFramedAsyncSerTool::TFramedAsyncSerTool(ULong serviceId)
	: TAsyncSerTool(serviceId)
{
	fInCRC.Reset();
	fOutCRC.Reset();
}


// ROM 0x000d2f60 __dt__19TFramedAsyncSerToolFv
TFramedAsyncSerTool::~TFramedAsyncSerTool()
{ }


// ROM 0x000d3834 GetSizeOf__19TFramedAsyncSerToolFv
// DEVIATION (pointer size): the host's size (the ROM's 0x54c).
ULong
TFramedAsyncSerTool::GetSizeOf()
{
	return sizeof(TFramedAsyncSerTool);
}


// ROM 0x000d3810 GetToolName__19TFramedAsyncSerToolFv
UChar*
TFramedAsyncSerTool::GetToolName()
{
	return (UChar*) "Framed Async Serial Tool";
}


// ROM 0x000d37d8 TaskConstructor__19TFramedAsyncSerToolFv
// Frames of up to 0x200 bytes; the transport says it is framed (0x38).
// (Set whatever the async tool answered.)
NewtonErr
TFramedAsyncSerTool::TaskConstructor()
{
	NewtonErr err = TAsyncSerTool::TaskConstructor();
	fInState = kInSYN;
	fOutState = kOutHeader;
	fFrameBufSize = 0x200;
	fTransportInfo.flags = 0x38;
	return err;
}


// ROM 0x000d380c TaskDestructor__19TFramedAsyncSerToolFv
void
TFramedAsyncSerTool::TaskDestructor()
{
	TAsyncSerTool::TaskDestructor();
}


// ROM 0x000d3840 AllocateBuffers__19TFramedAsyncSerToolFv
NewtonErr
TFramedAsyncSerTool::AllocateBuffers()
{
	NewtonErr err = TAsyncSerTool::AllocateBuffers();
	if (err != noErr)
		return err;
	err = fInFrame.Allocate(fFrameBufSize);
	if (err != noErr)
		return err;
	return fOutFrame.Allocate(fFrameBufSize);
}


// ROM 0x000d3888 DeallocateBuffers__19TFramedAsyncSerToolFv
void
TFramedAsyncSerTool::DeallocateBuffers()
{
	TAsyncSerTool::DeallocateBuffers();
	fInFrame.Deallocate();
	fOutFrame.Deallocate();
}


// ROM 0x000d3790 SetFramingCtl__19TFramedAsyncSerToolFP16TCMOFramingParms
// (The whole option, its header too.)
void
TFramedAsyncSerTool::SetFramingCtl(TCMOFramingParms* opt)
{
	fFraming = *opt;
}


// ROM 0x000d37ac GetFramingCtl__19TFramedAsyncSerToolFP16TCMOFramingParms
void
TFramedAsyncSerTool::GetFramingCtl(TCMOFramingParms* opt)
{
	*opt = fFraming;
}


// ROM 0x000d37cc ResetFramingStats__19TFramedAsyncSerToolFv
void
TFramedAsyncSerTool::ResetFramingStats()
{
	fFramedStats.preHeaderByteCnt = 0;
}


// ROM 0x000d2fc0 ProcessOptionStart__19TFramedAsyncSerToolFP7TOptionUlT2
// 'fram and 'frst; anything else the async tool's (inline in the ROM).
ULong
TFramedAsyncSerTool::ProcessOptionStart(TOption* opt, ULong label, ULong opcode)
{
	if (label == kCMOFramingParms)
	{
		if (opcode == opSetNegotiate || opcode == opSetRequired)
			fFraming.CopyDataFrom(opt);
		else if (opcode == opGetDefault)
		{
			TCMOFramingParms def;
			opt->CopyDataFrom(&def);
		}
		else
			opt->CopyDataFrom(&fFraming);
		return noErr;
	}
	if (label == kCMOFramedAsyncStats)
	{
		if (opcode == opSetNegotiate || opcode == opSetRequired)
			return opReadOnly;
		if (opcode == opGetCurrent)
		{
			opt->CopyDataFrom(&fFramedStats);
			ResetFramingStats();
			return noErr;
		}
		return opFailure;
	}
	return TAsyncSerTool::ProcessOptionStart(opt, label, opcode);
}


// ROM 0x000d38b8 AddDefaultOptions__19TFramedAsyncSerToolFP12TOptionArray
NewtonErr
TFramedAsyncSerTool::AddDefaultOptions(TOptionArray* options)
{
	TCMOFramingParms framing;
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &framing);
	if (err == noErr)
		err = TAsyncSerTool::AddDefaultOptions(options);
	return err;
}


// ROM 0x000d38fc AddCurrentOptions__19TFramedAsyncSerToolFP12TOptionArray
NewtonErr
TFramedAsyncSerTool::AddCurrentOptions(TOptionArray* options)
{
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &fFraming);
	if (err == noErr)
		return TAsyncSerTool::AddCurrentOptions(options);
	return err;
}


// ROM 0x000d3370 KillPut__19TFramedAsyncSerToolFv
void
TFramedAsyncSerTool::KillPut()
{
	fOutState = kOutHeader;
	fOutFrame.FlushBytes();
	TAsyncSerTool::KillPut();
}


// ROM 0x000d3760 KillGet__19TFramedAsyncSerToolFv
void
TFramedAsyncSerTool::KillGet()
{
	fInState = kInSYN;
	fInFrame.FlushBytes();
	TAsyncSerTool::KillGet();
}


// ROM 0x000d3090 FillOutputBuffer__19TFramedAsyncSerToolFv
// A framed put is sent as a frame: the put's data taken into fOutFrame and
// from there into the output buffer a byte at a time, the state carried
// from call to call so a frame can span several fillings.  ==> 0 (some put
// in the output buffer, or it filled), 5 (nothing more), or an error.
// ROM QUIRK (kept): a put whose data only partly fitted fOutFrame (state
// kOutError, a full frame buffer) goes on as a new frame, header and all.
ULong
TFramedAsyncSerTool::FillOutputBuffer()
{
	if (!fPutFramed)
		return TAsyncSerTool::FillOutputBuffer();
	ULong result = noErr;
	ULong count = 0;
	UByte byte;
	for (;;)
	{
		switch (fOutState)
		{
		case kOutHeader:
			if (fFraming.doHeader)
			{
				fOutBuf.PutNextByte(chSYN);
				fOutBuf.PutNextByte(chDLE);
				fOutBuf.PutNextByte(chSTX);
				count += 3;
			}
			fOutCRC.Reset();
			fOutHaveSaved = false;
			fOutState = kOutData;
			continue;

		case kOutData:
			if (fOutHaveSaved)
			{
				byte = fOutSaved;
				fOutHaveSaved = false;
			}
			else if (fOutFrame.GetNextByte(&byte) != kCircleBufOK)
			{
				// the frame buffer is empty: more of the put, or the frame's end
				if (fPutSize == 0)
				{
					if (fPutEOF)
					{
						fOutState = kOutEndEscape;
						continue;
					}
					goto finish;
				}
				result = fOutFrame.CopyIn(fPutBuffer, &fPutSize);
				if (result == kCircleBufOK)
					continue;
				fOutState = kOutError;
				if (result == kCircleBufNothingCopied)
				{
					result = kSerErr_InternalError;
					goto nothingSent;
				}
				goto finish;
			}
			if (fOutBuf.PutNextByte(byte) != kCircleBufOK)
			{
				fOutHaveSaved = true;
				fOutSaved = byte;
				goto finish;
			}
			if (fFraming.doOutFCS)
				fOutCRC.ComputeCRC(byte);
			count++;
			if (fFraming.escapeChar == byte)
				fOutState = kOutEscape;
			continue;

		case kOutEscape:
			result = fOutBuf.PutNextByte(fFraming.escapeChar);
			if (result != kCircleBufOK)
				goto finish;
			count++;
			fOutState = kOutData;
			continue;

		case kOutEndEscape:
			byte = fFraming.escapeChar;
			result = fOutBuf.PutNextByte(byte);
			if (result != kCircleBufOK)
				goto finish;
			count++;
			fOutState = kOutEnd;
			continue;

		case kOutEnd:
			byte = fFraming.eomChar;
			result = fOutBuf.PutNextByte(byte);
			if (result != kCircleBufOK)
				goto finish;
			count++;
			if (!fFraming.doOutFCS)
			{
				fOutState = kOutDone;
				goto nothingSent;
			}
			fOutCRC.ComputeCRC(byte);
			fOutState = kOutCRC1;
			continue;

		case kOutCRC1:
			fOutCRC.Get();
			result = fOutBuf.PutNextByte(fOutCRC.fResult[1]);
			if (result != kCircleBufOK)
				goto finish;
			count++;
			fOutState = kOutCRC2;
			continue;

		case kOutCRC2:
			fOutCRC.Get();
			result = fOutBuf.PutNextByte(fOutCRC.fResult[0]);
			if (result == kCircleBufOK)
			{
				count++;
				fOutState = kOutDone;
			}
			goto finish;

		case kOutDone:
		case kOutError:
			fOutState = kOutHeader;
			if (fPutSize == 0)
			{
				result = 5;
				goto nothingSent;
			}
			continue;

		default:
			continue;			// (the ROM loops for ever)
		}
	}
finish:
	if (result == kCircleBufFull)
		return noErr;
nothingSent:
	if (count == 0 && fPutSize == 0 && result == noErr)
		result = 5;
	return result;
}


// ROM 0x000d33a0 EmptyInputBuffer__19TFramedAsyncSerToolFPUl
// A framed get takes a frame: the header found (the bytes before it
// counted in the statistics), the escapes taken out into fInFrame, the
// CRC checked, and the frame copied into the get.  ==> 0 (more wanted),
// kCircleBufCountExhausted (the get is full), kSerEndOfFrameMarker (the
// frame is all in), or an error (kSerErr_CRCError, kSerErr_AsyncError -
// the frame thrown away).
// ROM QUIRK (kept): an escape followed by anything but the end character
// or another escape is dropped together with the character.
ULong
TFramedAsyncSerTool::EmptyInputBuffer(ULong* markerValue)
{
	if (!fGetFramed)
		return TAsyncSerTool::EmptyInputBuffer(markerValue);
	ULong result = noErr;
	Boolean more = true;
	UByte byte;
	while (more)
	{
		switch (fInState)
		{
		case kInSYN:
			fInEscaped = false;
			fInHaveSaved = false;
			fInCRC.Reset();
			if (!fFraming.doHeader)
			{
				fInState = kInData;
				break;
			}
			for (;;)
			{
				result = fInBuf.GetNextByte(&byte, markerValue);
				if (result != kCircleBufOK)
				{
					more = false;
					break;
				}
				if (byte == chSYN)
				{
					fInState = kInDLE;
					break;
				}
				fFramedStats.preHeaderByteCnt++;
			}
			break;

		case kInDLE:
			result = fInBuf.GetNextByte(&byte, markerValue);
			if (result != kCircleBufOK)
				goto finish;
			if (byte == chDLE)
				fInState = kInSTX;
			else
			{
				fInState = kInSYN;
				fFramedStats.preHeaderByteCnt += 2;
			}
			break;

		case kInSTX:
			result = fInBuf.GetNextByte(&byte, markerValue);
			if (result != kCircleBufOK)
				goto finish;
			if (byte == chSTX)
				fInState = kInData;
			else
			{
				fInState = kInSYN;
				fFramedStats.preHeaderByteCnt += 3;
			}
			break;

		case kInData:
			if (fInHaveSaved)
			{
				fInHaveSaved = false;
				byte = fInSaved;
				result = noErr;
			}
			else
			{
				result = fInBuf.GetNextByte(&byte, markerValue);
				if (result != kCircleBufOK)
				{
					more = false;
					break;
				}
			}
			if (!fInEscaped && fFraming.escapeChar == byte)
			{
				fInState = kInEscape;
				break;
			}
			result = fInFrame.PutNextByte(byte);
			if (result == kCircleBufOK)
			{
				if (fFraming.doInFCS)
					fInCRC.ComputeCRC(byte);
				fInEscaped = false;
				break;
			}
			// the frame buffer is full: the byte kept, the frame so far into the get
			fInHaveSaved = true;
			fInSaved = byte;
			result = fInFrame.CopyOut(fGetBuffer, &fGetSize, nil);
			if (result != kCircleBufOK)
				goto finish;
			break;

		case kInEscape:
			result = fInBuf.GetNextByte(&byte, markerValue);
			if (result != kCircleBufOK)
				goto finish;
			if (fFraming.eomChar == byte)
			{
				if (fFraming.doInFCS)
				{
					fInCRC.ComputeCRC(byte);
					fInState = kInCRC1;
				}
				else
					fInState = kInDone;
			}
			else if (fFraming.escapeChar == byte)
			{
				fInHaveSaved = true;
				fInSaved = byte;
				fInState = kInData;
				fInEscaped = true;
			}
			else
				fInState = kInData;
			break;

		case kInCRC1:
			result = fInBuf.GetNextByte(&byte, markerValue);
			if (result != kCircleBufOK)
				goto finish;
			fInCRC.Get();
			if (fInCRC.fResult[1] != byte)
				goto badCRC;
			fInState = kInCRC2;
			break;

		case kInCRC2:
			result = fInBuf.GetNextByte(&byte, markerValue);
			if (result != kCircleBufOK)
				goto finish;
			fInCRC.Get();
			if (fInCRC.fResult[0] != byte)
				goto badCRC;
			fInState = kInDone;
			break;

		case kInDone:
			result = fInFrame.CopyOut(fGetBuffer, &fGetSize, nil);
			if (result == kCircleBufCountExhausted)
				return result;
			fInState = kInSYN;
			if (result == kCircleBufOK)
				return kSerEndOfFrameMarker;
			goto finish;

		default:
			break;				// (the ROM loops for ever)
		}
	}
finish:
	if (result == kCircleBufEOM)
	{
		// a byte that came with an error: the frame is lost
		result = kSerErr_AsyncError;
		fInState = kInSYN;
		fInFrame.FlushBytes();
		return result;
	}
	if (result == kCircleBufEmpty)
		result = noErr;
	return result;

badCRC:
	fInState = kInSYN;
	result = kSerErr_CRCError;
	fInFrame.FlushBytes();
	return result;
}


/*------------------------------------------------------------------------------
	TFramedAsyncService
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TFramedAsyncService)
PROTOCOL_CLASSINFO(TFramedAsyncService, "TCMService", "serv\0fser\0\0", 0x20000, 0, nil)	// ROM 0x00382d5c ClassInfo__19TFramedAsyncServiceSFv

// ROM 0x000d3944 New__19TFramedAsyncServiceFv
TFramedAsyncService*
TFramedAsyncService::New()
{
	return this;
}


// ROM 0x000d3948 Delete__19TFramedAsyncServiceFv
void
TFramedAsyncService::Delete()
{ }


// ROM 0x000d394c Start__19TFramedAsyncServiceFP12TOptionArrayUlP12TServiceInfo
NewtonErr
TFramedAsyncService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	TFramedAsyncSerTool tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}


// ROM 0x000d39c0 DoneStarting__19TFramedAsyncServiceFP7TAEventUlP12TServiceInfo
NewtonErr
TFramedAsyncService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}
