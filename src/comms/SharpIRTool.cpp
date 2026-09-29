/*
	File:		comms/SharpIRTool.cpp

	Contains:	TSharpIRTool and TIRService (SharpIRTool.h).

	Reconstructed from the MP2x00 US ROM (0x001e07b4-0x001e2bbc,
	0x000e8f20-0x000e9034); each function cites its origin.
*/

#include "SharpIRTool.h"
#include "HALOptions.h"
#include "BufferList.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "NewtonTime.h"

// the offsets of a packet's parts in fPacket (the ROM's +0x4b0)
#define kCtlPacket		0					// +0x4b0  a control or negotiation packet, and a received packet's type byte
#define kDataPacket		4					// +0x4b4  a data packet

// the timers' kinds (TSharpIRTimerEvent::fKind)
#define kTimeoutControl		1
#define kTimeoutData		2
#define kTimeoutNegotiate	3
#define kTimerResend		4				// event 0xd
#define kTimerCrossed		5				// event 0x1c

// the control bytes
#define kIRENQ	0x05
#define kIRACK	0x06
#define kIRNAK	0x15
#define kIRSYN	0x16
#define kIRCAN	0x18


// ROM 0x001e07b4 __ct__12TSharpIRToolFUl
TSharpIRTool::TSharpIRTool(ULong serviceId)
	: TAsyncSerTool(serviceId)
{ }


// ROM 0x001e0844 __dt__12TSharpIRToolFv
TSharpIRTool::~TSharpIRTool()
{ }


// ROM 0x001e274c GetSizeOf__12TSharpIRToolFv
// DEVIATION (pointer size): the host's size (the ROM's 0x790).
ULong
TSharpIRTool::GetSizeOf()
{
	return sizeof(TSharpIRTool);
}


// ROM 0x001e0d74 GetToolName__12TSharpIRToolFv
UChar*
TSharpIRTool::GetToolName()
{
	return (UChar*) "SlowIR";
}


// ROM 0x001e1340 TaskConstructor__12TSharpIRToolFv
// The built-in IR (SCC service 2), the two timers' messages, a transport of
// up to 0x1fffe00 bytes a message.
NewtonErr
TSharpIRTool::TaskConstructor()
{
	NewtonErr err = TAsyncSerTool::TaskConstructor();
	if (err != noErr)
		return err;
	fSCCService = 2;
	err = fTimer1Msg.Init(true);
	if (err != noErr)
		return err;
	fTimer1MsgId = fTimer1Msg.GetMsgId();
	fTimer1Event.fField8 = 0;
	fTimer1Event.fAEventID = 'slir';
	fTimer1Event.fKind = 0;
	err = fTimer2Msg.Init(true);
	if (err != noErr)
		return err;
	fTimer2MsgId = fTimer2Msg.GetMsgId();
	fTimer2Event.fField8 = 0;
	fTimer2Event.fAEventID = 'slir';
	fTimer2Event.fKind = 0;
	fLeadInCount = 0;
	fLeadInByte = 0;
	fSequence = 0;
	fRxSequence = 0;
	fDataLength = 0;
	fPacketLength = 0;
	fPacketIndex = 0;
	fRetries = 0;
	fState = 0;
	fPacketKind = 1;
	fRxState = 0;
	fResend = false;
	fLastRxSequence = 0;
	fTransportInfo.serviceType = 1;
	fTransportInfo.flags = 0x1a;
	fTransportInfo.tdsu = 0x1fffe00;
	fTransportInfo.etdsu = -2;
	fLastRxChecksum = 0;
	fTransportInfo.connect = -2;
	fTransportInfo.discon = -2;
	fTransportInfo.addr = -2;
	fTransportInfo.opt = -1;
	return noErr;
}


// ROM 0x001e24c4 TaskDestructor__12TSharpIRToolFv
void
TSharpIRTool::TaskDestructor()
{
	TAsyncSerTool::TaskDestructor();
}


// ROM 0x001e08a0 HandleRequest__12TSharpIRToolFR10TUMsgTokenUl
// A timer run out: what it was timing.  Anything else is the serial tool's.
void
TSharpIRTool::HandleRequest(TUMsgToken& msgToken, ULong msgType)
{
	ULong kind;
	if (msgToken.GetMsgId() == fTimer1MsgId)
	{
		kind = fTimer1Event.fKind;
		fTimer1Event.fKind = 0;
	}
	else if (msgToken.GetMsgId() == fTimer2MsgId)
	{
		kind = fTimer2Event.fKind;
		fTimer2Event.fKind = 0;
	}
	else
	{
		TSerTool::HandleRequest(msgToken, msgType);
		return;
	}
	if (kind == kTimeoutControl)
		HandleControl(kIRErrTimeout);
	else if (kind == kTimeoutData)
		HandleData(kIRErrTimeout);
	else if (kind == kTimeoutNegotiate)
		HandleNegotiate(kIRErrTimeout);
	else if (kind == kTimerResend)
		NextState(0xd);
	else if (kind == kTimerCrossed)
		NextState(0x1c);
}


/*------------------------------------------------------------------------------
	The timers: delayed messages to the tool's own port
------------------------------------------------------------------------------*/

// ROM 0x001e0924 StartTimer1__12TSharpIRToolFUli
void
TSharpIRTool::StartTimer1(ULong delay, ULong kind)
{
	fTimer1Event.fKind = kind;
	TTime when = TimeFromNow(delay);
	fToolPort.Send(&fTimer1Msg, &fTimer1Event, sizeof(fTimer1Event), kNoTimeout, &when);
}


// ROM 0x001e099c StopTimer1__12TSharpIRToolFv
void
TSharpIRTool::StopTimer1()
{
	if (fTimer1Event.fKind == 0)
		return;
	fTimer1Msg.Abort();
	fTimer1Event.fKind = 0;
}


// ROM 0x001e09d0 StartTimer2__12TSharpIRToolFUli
void
TSharpIRTool::StartTimer2(ULong delay, ULong kind)
{
	fTimer2Event.fKind = kind;
	TTime when = TimeFromNow(delay);
	fToolPort.Send(&fTimer2Msg, &fTimer2Event, sizeof(fTimer2Event), kNoTimeout, &when);
}


// ROM 0x001e0a48 StopTimer2__12TSharpIRToolFv
void
TSharpIRTool::StopTimer2()
{
	if (fTimer2Event.fKind == 0)
		return;
	fTimer2Msg.Abort();
	fTimer2Event.fKind = 0;
}


/*------------------------------------------------------------------------------
	Sending
------------------------------------------------------------------------------*/

// ROM 0x001e0a78 StartTransmit__12TSharpIRToolF12IRPacketType
// The transmitter on (half duplex), the output taken (a put's buffer or,
// with none, -1 for the tool's own packet) and the packet started.
void
TSharpIRTool::StartTransmit(Long packetKind)
{
	if (fFeatures & kSerFeatureTxConfigNeeded)
		fChip->ConfigureForOutput(true);
	if (fPutBuffer == nil)
		fPutBuffer = (CBufferList*) -1;
	FlushOutputBytes();
	fPacketKind = packetKind;
	DoOutput();
}


// ROM 0x001e0acc StopTransmit__12TSharpIRToolFv
void
TSharpIRTool::StopTransmit()
{
	fIntMask &= ~kSerIntOutputDone;
	if (fPutBuffer == (CBufferList*) -1)
		fPutBuffer = nil;
	if (fFeatures & kSerFeatureTxConfigNeeded)
		fChip->ConfigureForOutput(false);
}


// ROM 0x001e0b00 StartOutput__12TSharpIRToolFP11CBufferList
// A put: sent as data packets once the other side has answered an ENQ.  Not
// while a get holds a packet it has not taken all of (state 0xd).
void
TSharpIRTool::StartOutput(CBufferList* clientBuffer)
{
	if (!fChipOn)
	{
		DoPutComplete(kSerErr_ToolNotReady);
		return;
	}
	if (fState == 0xd)
	{
		DoPutComplete(kCommErrBufferOverflow);
		return;
	}
	fPutBuffer = clientBuffer;
	clientBuffer->Seek(0, kSeekFromBeginning);
	fPutSize = clientBuffer->GetSize();
	NextState(0xa);
}


// ROM 0x001e0b8c DoOutput__12TSharpIRToolFv
// The packet put in the output buffer; once all of it is sent, told to the
// handler of its kind.
void
TSharpIRTool::DoOutput()
{
	ULong result = FillOutputBuffer();
	if (result == 1)
	{
		fIntMask |= kSerIntOutputDone;
		ContinueOutputST(true);
		return;
	}
	StopTransmit();
	if (fPacketKind == -1)
		HandleControl(result);
	else if (fPacketKind != 0)
		HandleNegotiate(result);
	else
		HandleData(result);
}


// ROM 0x001e0c0c FillOutputBuffer__12TSharpIRToolFv
// The lead-in, then the packet, into the output buffer as far as it goes.
// ==> 1 while there is more (or all of it has just gone in), 0 once it was
// all in last time, else what the buffer answered.
ULong
TSharpIRTool::FillOutputBuffer()
{
	if (fPacketIndex == fPacketLength)
		return 0;
	for ( ; fLeadInCount > 0; fLeadInCount--)
	{
		ULong result = fOutBuf.PutNextByte(fLeadInByte);
		if (result != kCircleBufOK)
			return result;
	}
	const UByte* packet = (fPacketKind == 0) ? fPacket + kDataPacket : fPacket + kCtlPacket;
	do
	{
		ULong result = fOutBuf.PutNextByte(packet[fPacketIndex++]);
		if (result == kCircleBufFull)
		{
			fPacketIndex--;
			return 1;
		}
		if (result != kCircleBufOK)
			return result;
	} while (fPacketIndex < fPacketLength);
	return 1;
}


// ROM 0x001e0d84 PrepLeadIn__12TSharpIRToolFv
// Sharp's and the negotiation's: five zeros.  The Newton's own protocols
// (2, 4): zeros (0xff for 4), five, ten or forty by the speed.
// (ROM QUIRK: with any other speed the count is left as it was.)
void
TSharpIRTool::PrepLeadIn()
{
	ULong count = 5;
	UByte lead = 0;
	ULong protocol = fProtocolType.protocol;
	if (protocol == irUsingNewtIR || protocol == irUsingSeniorIR)
	{
		if (protocol == irUsingSeniorIR)
			lead = 0xff;
		fLeadInByte = lead;
		ULong options = fProtocolType.options;
		if (options == irUsing9600)
			count = 5;
		else if (options == irUsing19200)
			count = 10;
		else if (options == irUsing38400)
			count = 40;
		else
			return;
	}
	else
		fLeadInByte = 0;
	fLeadInCount = count;
}


// ROM 0x001e0dd8 PrepDataPacket__12TSharpIRToolFv
// The next 0x200 bytes of the put (or the same packet again, fResend), the
// last numbered 0xffff when the put ends a frame.
void
TSharpIRTool::PrepDataPacket()
{
	fPacketIndex = 0;
	PrepLeadIn();
	if (fResend)
	{
		fPacketLength = fDataLength + 12;
		return;
	}
	fRetries = 3;
	ULong size = fPutSize;
	if (size > 0x200)
	{
		fPutSize = size - 0x200;
		fDataLength = 0x200;
		fSequence++;
	}
	else
	{
		fDataLength = size;
		fPutSize = 0;
		if (fPutEOF)
			fSequence = 0xffff;
		else
			fSequence++;
	}
	fPacketLength = fDataLength + 12;
	UByte* p = fPacket + kDataPacket;
	ULong protocol = fProtocolType.protocol;
	if (protocol == irUsingNewtIR || protocol == irUsingSeniorIR)
		*p++ = 0x9b;
	else if (protocol == irUsingSharpIR)
		*p++ = 0x96;
	*p++ = 0x81;
	*p++ = 0x10;
	*p++ = (UByte) fSequence;
	*p++ = (UByte) (fSequence >> 8);
	*p++ = 0x01;
	*p++ = 0x40;
	*p++ = 0xfe;
	*p++ = (UByte) fDataLength;
	*p++ = (UByte) (fDataLength >> 8);
	Size count = fDataLength;
	fPutBuffer->CopyOut(p, count);
	ULong sum = 0;
	for (ULong i = 0; i < fDataLength; i++)
		sum += *p++;
	sum &= 0xffff;
	*p++ = (UByte) sum;
	*p = (UByte) (sum >> 8);
}


// ROM 0x001e0f68 PrepControlPacket__12TSharpIRToolFUc
void
TSharpIRTool::PrepControlPacket(UByte control)
{
	PrepLeadIn();
	UByte* p = fPacket + kCtlPacket;
	ULong protocol = fProtocolType.protocol;
	if (protocol == irUsingNewtIR || protocol == irUsingSeniorIR)
		*p++ = 0x9b;
	else if (protocol == irUsingSharpIR || protocol == irUsingNegotiateIR)
		*p++ = 0x96;
	*p++ = 0x82;
	*p = control;
	fPacketIndex = 0;
	fPacketLength = 3;
}


// ROM 0x001e0fd8 PrepNegotiatePacket__12TSharpIRToolFUcN21
void
TSharpIRTool::PrepNegotiatePacket(UByte what, UByte protocols, UByte speeds)
{
	fLeadInCount = 5;
	fLeadInByte = 0;
	UByte* p = fPacket + kCtlPacket;
	p[0] = 0x90;
	p[1] = what;
	p[2] = protocols;
	p[3] = speeds;
	fPacketLength = 4;
	fPacketIndex = 0;
}


/*------------------------------------------------------------------------------
	Receiving
------------------------------------------------------------------------------*/

// ROM 0x001e1020 StartReceive__12TSharpIRToolF12IRPacketType
void
TSharpIRTool::StartReceive(Long packetKind)
{
	FlushInputBytes();
	if (fGetBuffer == nil)
		fGetBuffer = (CBufferList*) -1;
	fRxState = 1;
	fPacketIndex = 0;
	fPacketLength = 0;
	fPacketKind = packetKind;
	DoInput();
}


// ROM 0x001e1070 StopReceive__12TSharpIRToolFv
void
TSharpIRTool::StopReceive()
{
	fIntMask &= ~kSerIntInputReady;
	if (fGetBuffer == (CBufferList*) -1)
		fGetBuffer = nil;
	fRxState = 0;
}


// ROM 0x001e1094 StartInput__12TSharpIRToolFP11CBufferList
// A get: filled from data packets.  What a packet held beyond the last get
// (state 0xd) goes into this one first.
void
TSharpIRTool::StartInput(CBufferList* clientBuffer)
{
	if (fSequence < 1)
		fSequence = 1;
	fGetBuffer = clientBuffer;
	clientBuffer->Seek(0, kSeekFromBeginning);
	fGetSize = clientBuffer->GetSize();
	if (fState == 0xd)
	{
		if (CheckReceiveDone())
			return;
		fState = 0;
	}
	NextState(1);
}


// ROM 0x001e111c DoInput__12TSharpIRToolFv
// What has come in read into the packet; once all of it has, told to the
// handler of its kind.
void
TSharpIRTool::DoInput()
{
	fIntMask |= kSerIntInputReady;
	SyncInputBuffer();
	ULong result = EmptyInputBuffer(nil);
	if (result == 1)
		return;
	StopReceive();
	if (fPacketKind == -1)
		HandleControl(result);
	else if (fPacketKind != 0)
		HandleNegotiate(result);
	else
		HandleData(result);
}


// ROM 0x001e119c EmptyInputBuffer__12TSharpIRToolFPUl
// The lead-in skipped to a packet's type byte, which must be the protocol's,
// and the packet the state waits for read.  ==> 1 while it is incomplete,
// 0 when it is in, or the error.
ULong
TSharpIRTool::EmptyInputBuffer(ULong* markerValue)
{
	NewtonErr result = noErr;
	if (fRxState == 1 || fRxState == 2 || fRxState == 3)
	{
		result = ReceiveLeadIn();
		if (result != noErr)
			return result;
	}
	ULong protocol = fProtocolType.protocol;
	UByte type = fPacket[kCtlPacket];
	Boolean ok;
	if (protocol == irUsingNewtIR || protocol == irUsingSeniorIR)
		ok = (type == 0x9b);
	else if (protocol == irUsingSharpIR)
		ok = (type == 0x96);
	else if (protocol == irUsingNegotiateIR)
		ok = (type == 0x90 || type == 0x96);
	else
		ok = true;
	if (!ok)
	{
		fStats.protocolErrs++;
		return kIRErrPacket;
	}
	switch (fState)
	{
	case 0xc:
		return ReceiveControl(kIRACK);
	case 4:
		return ReceiveData();
	case 1:
	case 2:
		return ReceiveControl(kIRENQ);
	case 9:
	case 0xa:
		return ReceiveControl(kIRSYN);
	case 0xe:
	case 0xf:
		return ReceiveNegotiate(0x85);
	case 0x15:
	case 0x16:
		return ReceiveNegotiate(0x86);
	}
	return result;
}


// ROM 0x001e12c8 ReceiveLeadIn__12TSharpIRToolFv
// Bytes passed over to a packet's type byte (0x96, 0x9b, 0x90).
// ==> 1: none yet.
NewtonErr
TSharpIRTool::ReceiveLeadIn()
{
	for (;;)
	{
		UByte byte;
		ULong value = 0;
		ULong result = fInBuf.GetNextByte(&byte, &value);
		if (result == kCircleBufEmpty)
			return 1;
		if (result == kCircleBufEOM)
			continue;
		if (byte == 0x96 || byte == 0x9b || byte == 0x90)
		{
			fPacket[kCtlPacket] = byte;
			fRxState = 4;
			return noErr;
		}
	}
}


// ROM 0x001e1440 ReceiveControl__12TSharpIRToolFUc
// A control packet (0x82 and the control byte) after its type: the one
// expected, or CAN for a SYN (the other side cancelled), NAK for an ACK.
// ==> 1 while incomplete; a serial error (a byte with a marker: the chip
// saw an error on it) or a malformed packet counted.
NewtonErr
TSharpIRTool::ReceiveControl(UByte expected)
{
	UByte byte;
	ULong value;
	ULong result;
	if (fRxState == 4)
	{
		result = fInBuf.GetNextByte(&byte, &value);
		if (result == kCircleBufEmpty)
			return 1;
		if (result == kCircleBufEOM)
		{
			fStats.serialErrs++;
			return kIRErrSerial;
		}
		if (result != kCircleBufOK)
			return result;
		if (byte != 0x82)
		{
			fStats.protocolErrs++;
			return kIRErrPacket;
		}
		fPacket[1] = byte;
		fRxState = 5;
	}
	else if (fRxState != 5)
		// ROM QUIRK: the ROM answers the tool's own address here (whatever r0
		// held); no caller reads a control packet in any other state
		return kIRErrGeneric;
	result = fInBuf.GetNextByte(&byte, &value);
	if (result == kCircleBufEmpty)
		return 1;
	if (result == kCircleBufEOM)
	{
		fStats.serialErrs++;
		return kIRErrSerial;
	}
	if (result != kCircleBufOK)
		return result;
	fPacket[2] = byte;
	if (byte == expected)
	{
		fRxState = 0;
		return noErr;
	}
	if (expected == kIRSYN && byte == kIRCAN)
		return kIRErrCancel;
	if (expected == kIRACK && byte == kIRNAK)
		return kIRErrNAK;
	fStats.protocolErrs++;
	return kIRErrPacket;
}


// ROM 0x001e1570 HandleControl__12TSharpIRToolFl
// A control packet sent or received (or its timer run out), by the state.
void
TSharpIRTool::HandleControl(NewtonErr result)
{
	if (result == 1)
		return;
	switch (fState)
	{
	case 1:		// waiting for an ENQ (a get)
		if (result == noErr)
			NextState(4);
		else if (result == kIRErrPacket || result == kIRErrSerial)
			NextState(2);
		else
			AbortReceive(result);
		break;
	case 2:
		if (result == noErr)
			NextState(4);
		else if (result == kIRErrPacket || result == kIRErrSerial)
			NextState(3);
		else
			AbortReceive(result);
		break;
	case 3:		// SYN sent
		if (result == noErr)
			NextState(5);
		else
			AbortReceive(result);
		break;
	case 5:		// ACK sent
		if (result == noErr)
			NextState(8);
		else
			AbortReceive(result);
		break;
	case 6:		// NAK sent
		if (result == noErr)
			NextState(9);
		else
			AbortReceive(result);
		break;
	case 7:		// ENQ sent (a put)
		if (result == noErr)
			NextState(0xb);
		else
			AbortSend(result);
		break;
	case 8:		// ENQ sent again
		if (result == noErr)
			NextState(0xc);
		else
			AbortSend(result);
		break;
	case 9:		// waiting for the SYN
		if (result == noErr)
			NextState(0x10);
		else if (result == kIRErrPacket || result == kIRErrSerial)
			NextState(0xe);
		else
			AbortSend(result);
		break;
	case 0xa:
		if (result == noErr)
			NextState(0x10);
		else if (result == kIRErrPacket || result == kIRErrSerial)
			NextState(0xf);
		else
			AbortSend(result);
		break;
	case 0xc:	// waiting for the ACK
		if (result == kIRErrCancel)
			NextState(0x14);
		else if (result == kIRErrTimeout)
		{
			StopReceive();
			NextState(0x14);
		}
		else if (result == noErr)
			NextState(0x12);
		else if (result == kIRErrSerial || result == kIRErrPacket)
		{
			StopTimer1();
			StopReceive();
			NextState(0x14);
		}
		else if (result == kIRErrNAK)
			NextState(0x13);
		else
			AbortSend(result);
		break;
	}
}


// ROM 0x001e1758 ReceiveData__12TSharpIRToolFv
// A data packet after its type: the header (0x81, 0x10, the number, the
// length - no more than 0x200 - and the number the one expected or 0xffff),
// then the data and the checksum, which must be the data's bytes summed.
// ==> 1 while incomplete.
NewtonErr
TSharpIRTool::ReceiveData()
{
	UByte* packet = fPacket + kDataPacket;
	UByte byte;
	ULong value;
	ULong result;
	if (fRxState == 4)
	{
		result = fInBuf.GetNextByte(&byte, &value);
		if (result == kCircleBufEmpty)
			return 1;
		if (result == kCircleBufEOM)
		{
			fStats.serialErrs++;
			return kIRErrSerial;
		}
		if (result != kCircleBufOK)
			return result;
		if (byte != 0x81)
		{
			fStats.protocolErrs++;
			return kIRErrPacket;
		}
		fPacketIndex = 2;
		fPacketLength = 10;
		fRxState = 6;
	}
	if (fRxState == 6)
	{
		do
		{
			result = fInBuf.GetNextByte(&packet[fPacketIndex++], &value);
			if (result != kCircleBufOK)
			{
				if (result == kCircleBufEmpty)
				{
					fPacketIndex--;
					return 1;
				}
				if (result == kCircleBufEOM)
				{
					fStats.serialErrs++;
					return kIRErrSerial;
				}
				return result;
			}
		} while (fPacketIndex < fPacketLength);
		if ((packet[2] & 0xf0) != 0x10)
		{
			fStats.protocolErrs++;
			return kIRErrPacket;
		}
		fDataLength = packet[9];
		fDataLength = (fDataLength << 8) + packet[8];
		if (fDataLength > 0x200)
		{
			fStats.protocolErrs++;
			return kIRErrPacket;
		}
		fRxSequence = packet[4];
		fRxSequence = packet[3] + (fRxSequence << 8);
		if (fSequence != fRxSequence && fRxSequence != 0xffff)
		{
			fStats.protocolErrs++;
			return kIRErrPacket;
		}
		fPacketLength = fDataLength + 2;
		fPacketIndex = 0;
		fRxState = 7;
	}
	else if (fRxState != 7)
		// ROM QUIRK: the ROM answers the tool's own address here; no caller
		// reads a data packet in any other state
		return kIRErrGeneric;
	do
	{
		result = fInBuf.GetNextByte(&packet[fPacketIndex++], &value);
		if (result != kCircleBufOK)
		{
			if (result == kCircleBufEmpty)
			{
				fPacketIndex--;
				return 1;
			}
			if (result == kCircleBufEOM)
			{
				fStats.serialErrs++;
				return kIRErrSerial;
			}
			return result;
		}
	} while (fPacketIndex < fPacketLength);
	ULong length = fDataLength;
	ULong checksum = packet[length] + (packet[length + 1] << 8);
	ULong sum = 0;
	for (ULong i = 0; i < length; i++)
		sum += packet[i];
	if ((sum & 0xffff) != checksum)
	{
		fStats.checkSumErrs++;
		return kIRErrCheckSum;
	}
	fLastRxSequence = fRxSequence;
	fLastRxChecksum = checksum;
	fSequence = (fRxSequence != 0xffff) ? fRxSequence + 1 : 0xffff;
	fStats.dataPacketsIn++;
	return noErr;
}


// ROM 0x001e1a08 CheckReceiveDone__12TSharpIRToolFv
// The packet's data (what is left of it) put in the get.  ==> true when the
// get is answered: it is full, or the last packet (0xffff) is all in it -
// the end of the frame.  A full get with data still in the packet leaves
// the rest for the next get (state 0xd).
Boolean
TSharpIRTool::CheckReceiveDone()
{
	Size put = fGetBuffer->Putn(fPacket + kDataPacket + fPacketIndex, fDataLength);
	fGetSize -= put;
	fDataLength -= put;
	fPacketIndex += put;
	ULong count = fGetBuffer->GetSize() - fGetSize;
	if (fGetSize == 0)
	{
		if (fDataLength == 0)
		{
			GetComplete(noErr, fSequence == 0xffff, count);
			if (fSequence != 0xffff)
			{
				fState = 0;
				fRxState = 0;
			}
			else
				ResetStateMachine();
		}
		else
		{
			GetComplete(noErr, false, count);
			fState = 0xd;
			fRxState = 0;
		}
		return true;
	}
	if (fDataLength == 0 && fSequence == 0xffff)
	{
		GetComplete(noErr, true, count);
		ResetStateMachine();
		return true;
	}
	return false;
}


// ROM 0x001e1b4c HandleData__12TSharpIRToolFl
// A data packet received (state 4) or sent (state 0xb).
void
TSharpIRTool::HandleData(NewtonErr result)
{
	if (result == 1)
		return;
	if (fState == 4)
	{
		if (result == kIRErrPacket || result == kIRErrSerial)
			NextState(2);
		else if (result == kIRErrTimeout)
		{
			StopReceive();
			NextState(2);
		}
		else if (result == noErr)
			NextState(6);
		else if (result == kIRErrCheckSum)
			NextState(7);
		else
			AbortReceive(result);
	}
	else if (fState == 0xb)
	{
		if (result != noErr)
			AbortSend(result);
		else
			NextState(0x11);
	}
}


// ROM 0x001e1c04 ReceiveNegotiate__12TSharpIRToolFUc
// A negotiation packet after its type: the offer (0x85) or answer (0x86)
// expected - or, connecting symmetrically and waiting for an answer, the
// other side's own offer (their offers crossed) - then its protocols and
// speeds.  A Sharp control packet in its place is read as one (ENQ or
// SYN).  ==> 1 while incomplete.
NewtonErr
TSharpIRTool::ReceiveNegotiate(UByte expected)
{
	if (fPacket[kCtlPacket] == 0x96)
		return ReceiveControl(expected == 0x85 ? kIRENQ : kIRSYN);
	UByte byte;
	ULong value;
	ULong result;
	if (fRxState == 4)
	{
		result = fInBuf.GetNextByte(&byte, &value);
		if (result == kCircleBufEmpty)
			return 1;
		if (result == kCircleBufEOM)
		{
			fStats.serialErrs++;
			return kIRErrSerial;
		}
		if (result != kCircleBufOK)
			return result;
		Boolean matched = (byte == expected);
		if ((fConnect.connectOptions & irSymmetricConnect) && expected == 0x86 && byte == 0x85)
			fOfferCrossed = true;
		else if (!matched)
		{
			fStats.protocolErrs++;
			return kIRErrPacket;
		}
		fPacket[1] = byte;
		fRxState = 8;
	}
	if (fRxState == 8)
	{
		result = fInBuf.GetNextByte(&byte, &value);
		if (result == kCircleBufEmpty)
			return 1;
		if (result == kCircleBufEOM)
		{
			fStats.serialErrs++;
			return kIRErrSerial;
		}
		if (result != kCircleBufOK)
			return result;
		fPacket[2] = byte;
		fRxState = 9;
	}
	else if (fRxState != 9)
		// ROM QUIRK: the ROM answers the tool's own address here; no caller
		// reads a negotiation packet in any other state
		return kIRErrGeneric;
	result = fInBuf.GetNextByte(&byte, &value);
	if (result == kCircleBufEmpty)
		return 1;
	if (result == kCircleBufEOM)
	{
		fStats.serialErrs++;
		return kIRErrSerial;
	}
	if (result == kCircleBufOK)
		fPacket[3] = byte;
	return result;
}


// ROM 0x001e1d84 HandleNegotiate__12TSharpIRToolFl
// A negotiation packet sent or received (or its timer run out), by the
// state: states 0xe-0x12 are listening, 0x13-0x16 connecting.
void
TSharpIRTool::HandleNegotiate(NewtonErr result)
{
	if (result == 1)
		return;
	UByte type = fPacket[kCtlPacket];
	Boolean gotOffer = (type == 0x90 && fPacket[1] == 0x85);
	Boolean gotAnswer = (type == 0x90 && fPacket[1] == 0x86);
	Boolean gotENQ = (type == 0x96 && fPacket[2] == kIRENQ);
	Boolean gotSYN = (type == 0x96 && fPacket[2] == kIRSYN);
	// (the ROM works out one more - a negotiation's third byte - and drops it)
	switch (fState)
	{
	case 0xe:	// listening, waiting for an offer
	case 0xf:
		if (result == noErr)
		{
			if (gotOffer)
				NextState(0x17);
			else if (gotENQ)
				NextState(0x18);
		}
		else if (result == kIRErrSerial || result == kIRErrPacket)
			NextState(0x16);
		else
			DoListenComplete(result);
		break;
	case 0x10:	// the answer sent: the speed and protocol agreed
		fProtocolType.protocol = fPacket[2];
		fProtocolType.options = 3;
		if (fOfferCrossed)
			DoConnectComplete(result);
		else
			DoListenComplete(result);
		break;
	case 0x11:	// the answer sent to an ENQ: wait for the offer
		if (result == noErr)
			NextState(0x19);
		else
			DoListenComplete(result);
		break;
	case 0x12:	// SYN sent to an ENQ: an old Sharp device
		fProtocolType.protocol = irUsingSharpIR;
		fProtocolType.options = irUsing9600;
		DoListenComplete(result);
		break;
	case 0x13:	// the offer sent
	case 0x14:	// the ENQ sent
		if (result == noErr)
			NextState(0x1b);
		else
			DoConnectComplete(result);
		break;
	case 0x15:	// waiting for the answer
	case 0x16:
		if (result != noErr)
		{
			if (result == kIRErrPacket || result == kIRErrSerial)
				NextState(0x1f);
			else
				DoConnectComplete(result);
		}
		else if (gotAnswer)
			NextState(0x1d);
		else if (gotSYN)
			NextState(0x1e);
		else if (gotOffer && fOfferCrossed)
		{
			fState = 0xe;
			NextState(0x17);
		}
		else
		{
			StopTimer2();
			fState = 0x16;
			NextState(0x1c);
		}
		break;
	}
}


/*------------------------------------------------------------------------------
	The state machine
------------------------------------------------------------------------------*/

// ROM 0x001e1f74 NextState__12TSharpIRToolF7IREvent
void
TSharpIRTool::NextState(ULong event)
{
	switch (event)
	{
	case 1:		// a get: wait two minutes for an ENQ
		fState = 1;
		fDataLength = 0;
		fPacketIndex = 0;
		StartTimer1(120000 * kMilliseconds, kTimeoutControl);
		StartReceive(-1);
		break;
	case 2:		// a bad packet: wait ten seconds more
		StopTimer1();
		StartTimer1(10000 * kMilliseconds, kTimeoutControl);
		fState = 2;
		StartReceive(-1);
		break;
	case 3:
		fState = 2;
		StartReceive(-1);
		break;
	case 4:		// an ENQ: answer SYN
		StopTimer1();
		fState = 3;
		PrepControlPacket(kIRSYN);
		StartTransmit(-1);
		break;
	case 5:		// then the data packet, within a second
		fState = 4;
		StartTimer1(1000 * kMilliseconds, kTimeoutData);
		StartReceive(0);
		break;
	case 6:		// a good packet: ACK
		StopTimer1();
		fState = 5;
		PrepControlPacket(kIRACK);
		StartTransmit(-1);
		break;
	case 7:		// a bad checksum: NAK
		StopTimer1();
		fState = 6;
		PrepControlPacket(kIRNAK);
		StartTransmit(-1);
		break;
	case 8:		// the ACK sent: into the get
		fPacketIndex = 0;
		if (CheckReceiveDone())
			return;
		// fall through: wait for the next packet
	case 9:
		fState = 2;
		StartTimer1(10000 * kMilliseconds, kTimeoutControl);
		StartReceive(-1);
		break;
	case 0xa:	// a put: ENQ
		fState = 7;
		fResend = false;
		PrepControlPacket(kIRENQ);
		StartTransmit(-1);
		break;
	case 0xb:	// the ENQ sent: wait for the SYN, sending ENQ again every half second
		fState = 9;
		StartTimer1(120000 * kMilliseconds, kTimeoutControl);
		StartTimer2(500 * kMilliseconds, kTimerResend);
		StartReceive(-1);
		break;
	case 0xc:
		fState = fSavedState;
		StartTimer2(500 * kMilliseconds, kTimerResend);
		StartReceive(-1);
		break;
	case 0xd:	// the half second is up: ENQ again
		fSavedState = fState;
		fState = 8;
		StopReceive();
		PrepControlPacket(kIRENQ);
		StartTransmit(-1);
		break;
	case 0xe:	// a bad packet: wait ten seconds more for the SYN
		StopTimer1();
		StartTimer1(10000 * kMilliseconds, kTimeoutControl);
		// fall through
	case 0xf:
		fState = 0xa;
		StartReceive(-1);
		break;
	case 0x10:	// the SYN: send the data packet
		StopTimer1();
		StopTimer2();
		fState = 0xb;
		PrepDataPacket();
		StartTransmit(0);
		break;
	case 0x11:	// sent: wait a second for the ACK
		fState = 0xc;
		StartTimer1(1000 * kMilliseconds, kTimeoutControl);
		StartReceive(-1);
		break;
	case 0x12:	// ACKed: the next packet, or the put is done
		StopTimer1();
		fResend = false;
		fStats.dataPacketsOut++;
		if (fPutSize != 0)
		{
			fState = 7;
			PrepControlPacket(kIRENQ);
			StartTransmit(-1);
			break;
		}
		DoPutComplete(noErr);
		if (!fPutEOF)
		{
			fState = 0;
			fRxState = 0;
		}
		else
			ResetStateMachine();
		break;
	case 0x13:	// NAKed
		StopTimer1();
		// fall through
	case 0x14:	// not ACKed: the packet again, three times at most
		if (--fRetries <= 0)
		{
			AbortSend(kIRErrRetry);
			break;
		}
		fStats.dataRetries++;
		fResend = true;
		fState = 8;
		fSavedState = 0xa;
		StartTimer1(10000 * kMilliseconds, kTimeoutControl);
		PrepControlPacket(kIRENQ);
		StartTransmit(-1);
		break;
	case 0x15:	// listening: wait two minutes for an offer
		fState = 0xe;
		StartTimer1(120000 * kMilliseconds, kTimeoutNegotiate);
		StartReceive(1);
		break;
	case 0x16:
		StartReceive(1);
		break;
	case 0x17:	// an offer: answer with what both can do
		fState = 0x10;
		PrepNegotiatePacket(0x86, fPacket[2] & fProtocolsOffered, fPacket[3] & 3);
		StartTransmit(1);
		break;
	case 0x18:	// an ENQ while listening: answer, or SYN an old device
		if (fState == 0xe)
		{
			fState = 0x11;
			PrepNegotiatePacket(0x86, fPacket[2] & fProtocolsOffered, fPacket[3] & 3);
		}
		else
		{
			fState = 0x12;
			PrepControlPacket(kIRSYN);
		}
		StartTransmit(1);
		break;
	case 0x19:
		fState = 0xf;
		StartReceive(1);
		break;
	case 0x1a:	// connecting: offer, and give it two minutes
		fState = 0x13;
		PrepNegotiatePacket(0x85, fProtocolsOffered, 3);
		StartTimer1(120000 * kMilliseconds, kTimeoutNegotiate);
		StartTransmit(1);
		break;
	case 0x1b:	// sent: wait half a second for the answer
		fState = (fState == 0x13) ? 0x15 : 0x16;
		StartTimer2(500 * kMilliseconds, kTimerCrossed);
		StartReceive(1);
		break;
	case 0x1c:	// no answer: offer again (after an offer, ENQ)
		if (fState == 0x15)
		{
			fState = 0x14;
			PrepControlPacket(kIRENQ);
		}
		else
		{
			fState = 0x13;
			PrepNegotiatePacket(0x85, fProtocolsOffered, 3);
		}
		StartTransmit(1);
		break;
	case 0x1d:	// the answer: connected with what it says
		StopTimer2();
		if (fState != 0x15)
		{
			fState = 0x13;
			PrepNegotiatePacket(0x85, fProtocolsOffered, 3);
			StartTransmit(1);
			break;
		}
		StopTimer1();
		fProtocolType.protocol = fPacket[2];
		fProtocolType.options = fPacket[3];
		DoConnectComplete(noErr);
		break;
	case 0x1e:	// a SYN: an old Sharp device
		StopTimer2();
		if (fState == 0x15)
			break;
		StopTimer1();
		fProtocolType.protocol = irUsingSharpIR;
		fProtocolType.options = irUsing9600;
		DoConnectComplete(noErr);
		break;
	case 0x1f:	// a bad packet
		StopTimer2();
		StopReceive();
		if (fState == 0x15)
		{
			fState = 0x14;
			PrepControlPacket(kIRENQ);
		}
		else
		{
			fState = 0x13;
			PrepNegotiatePacket(0x85, fProtocolsOffered, 3);
		}
		StartTransmit(1);
		break;
	}
}


// ROM 0x001e2494 AbortReceive__12TSharpIRToolFl
void
TSharpIRTool::AbortReceive(NewtonErr error)
{
	ResetStateMachine();
	DoGetComplete(error, false);
}


// ROM 0x001e24f8 AbortSend__12TSharpIRToolFl
void
TSharpIRTool::AbortSend(NewtonErr error)
{
	ResetStateMachine();
	DoPutComplete(error);
}


// ROM 0x001e2464 KillGet__12TSharpIRToolFv
void
TSharpIRTool::KillGet()
{
	AbortReceive(kCommErrRequestCanceled);
	KillGetComplete(noErr);
}


// ROM 0x001e24c8 KillPut__12TSharpIRToolFv
void
TSharpIRTool::KillPut()
{
	AbortSend(kCommErrRequestCanceled);
	KillPutComplete(noErr);
}


// ROM 0x001e2524 StartAbort__12TSharpIRToolFl
NewtonErr
TSharpIRTool::StartAbort(NewtonErr abortError)
{
	ResetStateMachine();
	return TCommTool::StartAbort(abortError);
}


// ROM 0x001e254c TerminateComplete__12TSharpIRToolFv
void
TSharpIRTool::TerminateComplete()
{
	ResetStateMachine();
	TSerTool::TerminateComplete();
}


// ROM 0x001e2a5c ResetStateMachine__12TSharpIRToolFv
void
TSharpIRTool::ResetStateMachine()
{
	StopTransmit();
	StopReceive();
	StopTimer1();
	StopTimer2();
	fState = 0;
	fRxState = 0;
	fResend = false;
	fLeadInCount = 0;
	fSequence = 0;
	fRxSequence = 0;
	fDataLength = 0;
	fPacketLength = 0;
	fPacketIndex = 0;
	fRetries = 0;
}


/*------------------------------------------------------------------------------
	Opening, connecting, listening
------------------------------------------------------------------------------*/

// ROM 0x001e256c OpenStart__12TSharpIRToolFP12TOptionArray
NewtonErr
TSharpIRTool::OpenStart(TOptionArray* options)
{
	return TCommTool::OpenStart(options);
}


// ROM 0x001e2570 ListenStart__12TSharpIRToolFv
// The chip on, the negotiation's protocol, and wait for an offer.
void
TSharpIRTool::ListenStart()
{
	NewtonErr err = TurnOn();
	if (err != noErr)
	{
		ListenComplete(err);
		return;
	}
	fOfferCrossed = false;
	fProtocolsOffered = 7;
	SelectProtocol(true);
	NextState(0x15);
}


// ROM 0x001e25d4 DoListenComplete__12TSharpIRToolFl
void
TSharpIRTool::DoListenComplete(NewtonErr result)
{
	ResetStateMachine();
	if (result != noErr)
	{
		StartAbort(result);
		return;
	}
	fConnect.connectOptions &= ~irActiveConnection;
	ListenComplete(result);
}


// ROM 0x001e262c AcceptStart__12TSharpIRToolFv
void
TSharpIRTool::AcceptStart()
{
	SelectProtocol(false);
	AcceptComplete(noErr);
}


// ROM 0x001e2658 ConnectStart__12TSharpIRToolFv
// The chip on and an offer made - unless the probe has already agreed the
// protocol (irNegotiatedConnection).
void
TSharpIRTool::ConnectStart()
{
	NewtonErr err = TurnOn();
	if (err != noErr)
	{
		ConnectComplete(err);
		return;
	}
	fOfferCrossed = false;
	if ((fConnect.connectOptions & irNegotiatedConnection) && fProtocolType.protocol != irUsingNegotiateIR)
	{
		DoConnectComplete(noErr);
		return;
	}
	fProtocolsOffered = 7;
	SelectProtocol(true);
	NextState(0x1a);
}


// ROM 0x001e26dc DoConnectComplete__12TSharpIRToolFl
// Connected with the protocol agreed: active unless the offers crossed and
// the other side's was the one answered.
void
TSharpIRTool::DoConnectComplete(NewtonErr result)
{
	ResetStateMachine();
	if (result != noErr)
	{
		StartAbort(result);
		return;
	}
	SelectProtocol(false);
	if (!fOfferCrossed)
		fConnect.connectOptions |= irActiveConnection;
	else
		fConnect.connectOptions &= ~irActiveConnection;
	ConnectComplete(result);
}


// ROM 0x001e2754 SelectSpeed__12TSharpIRToolFUc
// 9600 bps, or with the Newton's own protocols the fastest agreed.
InterfaceSpeed
TSharpIRTool::SelectSpeed(Boolean reset)
{
	ULong protocol = fProtocolType.protocol;
	if (!reset && (protocol == irUsingNewtIR || protocol == irUsingSeniorIR))
	{
		ULong options = fProtocolType.options;
		if (options & irUsing38400)
		{
			fProtocolType.options = irUsing38400;
			return ChangeSpeed(38400);
		}
		if (options & irUsing19200)
		{
			fProtocolType.options = irUsing19200;
			return ChangeSpeed(19200);
		}
	}
	fProtocolType.options = irUsing9600;
	return ChangeSpeed(9600);
}


// ROM 0x001e27a8 SelectProtocol__12TSharpIRToolFUc
// Back to negotiating, or the best of the protocols agreed.
InterfaceSpeed
TSharpIRTool::SelectProtocol(Boolean reset)
{
	ULong protocol;
	if (reset)
		protocol = irUsingNegotiateIR;
	else if (fProtocolType.protocol & irUsingSeniorIR)
		protocol = irUsingSeniorIR;
	else if (fProtocolType.protocol & irUsingNewtIR)
		protocol = irUsingNewtIR;
	else
		protocol = irUsingSharpIR;
	fProtocolType.protocol = protocol;
	return SelectSpeed(reset);
}


/*------------------------------------------------------------------------------
	Options and the chip
------------------------------------------------------------------------------*/

// ROM 0x001e2894 ProcessOptionStart__12TSharpIRToolFP7TOptionUlT2
// 'irco', 'irpt' set and got; 'irst' got (and the statistics cleared); the
// buffers ('sbuf') of 0x20c bytes each way at least.
ULong
TSharpIRTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	ULong result = noErr;
	if (label == kCMOSlowIRConnect)
	{
		if (opcode == opSetNegotiate || opcode == opSetRequired)
			fConnect.CopyDataFrom(theOption);
		else if (opcode == opGetDefault)
		{
			TCMOSlowIRConnect connect;
			theOption->CopyDataFrom(&connect);
		}
		else
			theOption->CopyDataFrom(&fConnect);
	}
	else if (label == kCMOSlowIRProtocolType)
	{
		if (opcode == opSetNegotiate || opcode == opSetRequired)
			fProtocolType.CopyDataFrom(theOption);
		else if (opcode == opGetDefault)
		{
			TCMOSlowIRProtocolType protocolType;
			theOption->CopyDataFrom(&protocolType);
		}
		else
			theOption->CopyDataFrom(&fProtocolType);
	}
	else if (label == kCMOSlowIRStats)
	{
		if (opcode == opSetNegotiate || opcode == opSetRequired)
			result = opReadOnly;
		else if (opcode == opGetCurrent)
		{
			theOption->CopyDataFrom(&fStats);
			theOption->SetOpCodeResult(opSuccess);
			fStats = TCMOSlowIRStats();
		}
		else
			result = opFailure;
	}
	else if (label == kCMOSerialBuffers)
	{
		if (opcode == opSetNegotiate || opcode == opSetRequired)
		{
			if (fBuffersAllocated)
				result = opFailure;
			else
			{
				fBuffers.CopyDataFrom(theOption);
				fBuffers.fRecvSize = 0x20c;
				if (fBuffers.fSendSize < 0x20c)
					fBuffers.fSendSize = 0x20c;
			}
		}
		else if (opcode == opGetDefault)
		{
			TCMOSerialBuffers buffers;
			buffers.fRecvSize = 0x20c;
			buffers.fSendSize = 0x20c;
			theOption->CopyDataFrom(&buffers);
		}
		else
			theOption->CopyDataFrom(&fBuffers);
	}
	else
		return TAsyncSerTool::ProcessOptionStart(theOption, label, opcode);
	return result;
}


// ROM 0x001e27dc AddDefaultOptions__12TSharpIRToolFP12TOptionArray
NewtonErr
TSharpIRTool::AddDefaultOptions(TOptionArray* options)
{
	TCMOSlowIRProtocolType protocolType;
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &protocolType);
	if (err == noErr)
		err = TAsyncSerTool::AddDefaultOptions(options);
	return err;
}


// ROM 0x001e2820 AddCurrentOptions__12TSharpIRToolFP12TOptionArray
NewtonErr
TSharpIRTool::AddCurrentOptions(TOptionArray* options)
{
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &fProtocolType);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fStats);
	if (err == noErr)
		err = options->InsertOptionAt(options->GetArrayCount(), &fConnect);
	if (err == noErr)
		err = TAsyncSerTool::AddCurrentOptions(options);
	return err;
}


// ROM 0x001e2ab8 SetChannelFilter__12TSharpIRToolF19CommToolRequestTypeUc
// A put and a get go together: the state machine uses both channels.
void
TSharpIRTool::SetChannelFilter(CommToolRequestType request, Boolean enable)
{
	if (request & 3)
		request = (CommToolRequestType) (request | 3);
	TCommTool::SetChannelFilter(request, enable);
}


// ROM 0x001e2ac8 AllocateBuffers__12TSharpIRToolFv
// Room for a whole packet each way; the IR port in Sharp's mode (ASK).
NewtonErr
TSharpIRTool::AllocateBuffers()
{
	if (fBuffers.fRecvSize < 0x234)
		fBuffers.fRecvSize = 0x234;
	if (fBuffers.fSendSize < 0x234)
		fBuffers.fSendSize = 0x234;
	THMOSerIRLinkConfig link;
	link.SetOpCode(opSetRequired);
	link.fIRLinkMode = kSerIRLink_SharpIR;
	fChip->ProcessOption(&link);
	return TAsyncSerTool::AllocateBuffers();
}


// ROM 0x001e2b30 SetIOParms__12TSharpIRToolFP17TCMOSerialIOParms
// Always 9600 bps, 8 bits, odd parity, one stop bit - and that is what the
// option is told.
void
TSharpIRTool::SetIOParms(TCMOSerialIOParms* opt)
{
	TCMOSerialIOParms parms;
	parms.fStopBits = k1StopBits;
	parms.fParity = kOddParity;
	parms.fDataBits = k8DataBits;
	parms.fSpeed = 9600;
	TSerTool::SetIOParms(&parms);
	opt->CopyDataFrom(&parms);
}


// ROM 0x001e2b8c SetSerialChipSelect__12TSharpIRToolFP18TCMOSerialHardware
// The built-in IR unless the option names another service.
NewtonErr
TSharpIRTool::SetSerialChipSelect(TCMOSerialHardware* opt)
{
	NewtonErr err = TSerTool::SetSerialChipSelect(opt);
	if (fSCCService == 0)
	{
		opt->fSCCService = 2;
		fSCCService = 2;
	}
	return err;
}


/*------------------------------------------------------------------------------
	TIRService ('slir')
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TIRService)
PROTOCOL_CLASSINFO(TIRService, "TCMService", "serv\0slir\0\0", 0x20000, 0, nil)

// ROM 0x000e8f28 New__10TIRServiceFv
TIRService*
TIRService::New()
{
	return this;
}


// ROM 0x000e8f2c Delete__10TIRServiceFv
void
TIRService::Delete()
{ }


// ROM 0x000e8f30 Start__10TIRServiceFP12TOptionArrayUlP12TServiceInfo
// The tool already running (its port named for the service): that one;
// otherwise a tool started and opened with the endpoint's options.
NewtonErr
TIRService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	TUPort port;
	NewtonErr err = ServiceToPort(serviceId, &port);
	if (err == kError_Not_Registered)
	{
		TSharpIRTool tool(serviceId);
		err = StartCommTool(&tool, serviceId, serviceInfo);
		if (err != noErr)
			return err;
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	}
	else if (err == noErr)
	{
		serviceInfo->SetPortId(port.fId);
		serviceInfo->SetServiceId(serviceId);
	}
	return err;
}


// ROM 0x000e902c DoneStarting__10TIRServiceFP7TAEventUlP12TServiceInfo
// What the open answered.
NewtonErr
TIRService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}


void
RegisterIRCommServices(void)
{
	TIRService::ClassInfo()->Register();
}
