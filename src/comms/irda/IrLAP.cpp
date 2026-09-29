/*
	File:		comms/irda/IrLAP.cpp

	Contains:	TIrLAP (IrLAP.h).

	Reconstructed from the MP2x00 US ROM (0x000f23dc-0x000f2474,
	0x000f3144-0x000f54a4, 0x000f5698-0x000f5e84); each function cites its
	origin.
*/

#include "IrLAP.h"
#include "IrGlue.h"
#include "IrLMP.h"
#include "IrQOS.h"
#include "IrDscInfo.h"
#include "BufferSegment.h"
#include "CommErrors.h"
#include "NewtonTime.h"
#include "utility/Random.h"

#include <stdio.h>
#include <string.h>

// the frame's control byte
#define kIrPF			0x10

// the times the link waits
#define kIrMediaBusyTime	(600 * kMilliseconds)
#define kIrSlotTime			(25 * kMilliseconds)
#define kIrSlotInputTime	(45 * kMilliseconds)
#define kIrSNRMTime			(500 * kMilliseconds)
#define kIrReplySlotTime	(140 * kMilliseconds)
#define kIrDisconnectTime	(3 * kSeconds)		// when the tool is told the link is going

// the words in a frame are big-endian, as the ROM stores them
static ULong
GetWordBE(const UByte* p)
{
	return ((ULong) p[0] << 24) | ((ULong) p[1] << 16) | ((ULong) p[2] << 8) | p[3];
}

static void
SetWordBE(UByte* p, ULong w)
{
	p[0] = (UByte) (w >> 24);
	p[1] = (UByte) (w >> 16);
	p[2] = (UByte) (w >> 8);
	p[3] = (UByte) w;
}


// ROM 0x000f23dc __ct__6TIrLAPFv
TIrLAP::TIrLAP()
{
	fPrimary = false;
	fRequest = nil;
	fDisconnectPending = false;
	fDisconnectRequest = nil;
	fState = kIrLAPDisconnected;
	fExtraBOFs = 10;
	fNumGetBuffers = 0;
	fPutRequests = nil;
	fLocalBusy = false;
	fSetLocalBusy = false;
	fFRMRPending = false;
	fNeedGetBuffer = true;
	fInputActive = false;
	fOutputActive = false;
	fTestFrameActive = false;
}


// ROM 0x000f3144 __dt__6TIrLAPFv
TIrLAP::~TIrLAP()
{
	DeInit();
}


// ROM 0x000f42d0 Init__6TIrLAPFP7TIrGlueP6TIrLMP
// A device address at random, the frame buffer, the put list.
NewtonErr
TIrLAP::Init(TIrGlue* glue, TIrLMP* lmp)
{
	fIrGlue = glue;
	fLMP = lmp;
	NewtonErr err = TIrStream::Init(glue);
	if (err == noErr)
	{
		fMyDevAddr = (ULong) NewtonRand() % 0xfffffffe + 1;
		err = fControlSegment.Init(fFrame, sizeof(fFrame), false, 0, -1);
		if (err == noErr)
		{
			err = -7000;
			fPutRequests = new CList;
			if (fPutRequests != nil)
			{
				for (ULong i = 0; i < 8; i++)
					fSentFrames[i] = nil;
				ResetStats();
				return noErr;
			}
		}
	}
	DeInit();
	return err;
}


// ROM 0x000f4e0c DeInit__6TIrLAPFv
void
TIrLAP::DeInit(void)
{
	FreeGetBuffers();
	if (fPutRequests != nil)
	{
		delete fPutRequests;
		fPutRequests = nil;
	}
}


// ROM 0x000f499c Reset__6TIrLAPFv
void
TIrLAP::Reset(void)
{
	fState = kIrLAPDisconnected;
	FreeGetBuffers();
}


// ROM 0x000f5440 NextState__6TIrLAPFUl
void
TIrLAP::NextState(ULong event)
{
	switch (fState)
	{
	case kIrLAPDisconnected:	HandleDisconnectedStateEvent(event); break;
	case kIrLAPQuery:			HandleQueryStateEvent(event); break;
	case kIrLAPConnect:			HandleConnectStateEvent(event); break;
	case kIrLAPListen:			HandleListenStateEvent(event); break;
	case kIrLAPReply:			HandleReplyStateEvent(event); break;
	case kIrLAPPriReceive:		HandlePriReceiveStateEvent(event); break;
	case kIrLAPPriTransmit:		HandlePriTransmitStateEvent(event); break;
	case kIrLAPPriClose:		HandlePriCloseStateEvent(event); break;
	case kIrLAPSecReceive:		HandleSecReceiveStateEvent(event); break;
	case kIrLAPSecTransmit:		HandleSecTransmitStateEvent(event); break;
	case kIrLAPSecClose:		HandleSecCloseStateEvent(event); break;
	}
}


/*------------------------------------------------------------------------------
	Out of a connection
------------------------------------------------------------------------------*/

// ROM 0x000f5698 HandleDisconnectedStateEvent__6TIrLAPFUl
// NDM.  A discovery listens for other traffic first (600 ms) and then
// starts the slots; a connect sends SNRM; a listen listens.  An XID from
// another station is answered (the reply state); an SNRM taken as a
// connection, or - discovering - as a device found.
void
TIrLAP::HandleDisconnectedStateEvent(ULong event)
{
	TIrEvent* request;
	switch (event)
	{
	case kIrPutDataRequest:
		NotConnectedCompletion();
		break;

	case kIrInputComplete:
		StopTimer();
		if (RecdPollCmd(kIrXIDCmd))
		{
			fFirstXID = true;
			HandleReplyStateEvent(kIrInputComplete);
		}
		else if (RecdPollCmd(kIrSNRM))
		{
			request = fRequest;
			if (request != nil && request->fEvent == kIrDiscoverRequest)
			{
				Boolean restart = true;
				UByte info[12];
				if (GotData(info + 4, 9) && GetWordBE(info + 8) == fMyDevAddr)
				{
					TIrDscInfo* device = new TIrDscInfo;
					if (device != nil)
					{
						restart = false;
						device->fVersion = 0;
						device->fDevAddr = GetWordBE(info + 4);
						device->SetNickname("");
						fRequest = nil;
						request->fEvent = kIrDiscoverReply;
						request->fDiscoveredList->InsertAt(request->fDiscoveredList->Count(), device);
						request->fResult = noErr;
						request->fPassiveDiscovery = 1;
						fLMP->EnqueueEvent(request);
					}
				}
				if (restart)
					StartInput(&fControlSegment);
			}
			else
				HandleListenStateEvent(kIrInputComplete);
		}
		else
			StartInput(&fControlSegment);
		// still discovering: listen again for the next
		if (fState == kIrLAPDisconnected && fRequest != nil && fRequest->fEvent == kIrDiscoverRequest)
			StartTimer(kIrMediaBusyTime, kIrMediaBusyTimer);
		break;

	case kIrDiscoverRequest:
		fRequest = fCurrentEvent;
		if (fCurrentEvent->fMediaBusyCheck == 0)
		{
			HandleDisconnectedStateEvent(kIrMediaBusyTimer);
			return;
		}
		StartTimer(kIrMediaBusyTime, kIrMediaBusyTimer);
		StartInput(&fControlSegment);
		break;

	case kIrConnectRequest:
		request = fCurrentEvent;
		fRequest = request;
		fMyQOS = request->fMyQOS;
		fPeerQOS = request->fPeerQOS;
		fPeerDevAddr = request->fDevAddr;
		fConnAddr = (NewtonRand() & 0xff) % 0x7c + 2;
		if (fConnAddr == 0x60 || fConnAddr == 0x3e)
			fConnAddr++;
		fRetryCount = 0;
		fState = kIrLAPConnect;
		StartInput(&fControlSegment);
		HandleConnectStateEvent(kIrFinalTimer);
		break;

	case kIrListenRequest:
		request = fCurrentEvent;
		fRequest = request;
		fMyQOS = request->fMyQOS;
		fPeerQOS = request->fPeerQOS;
		StartInput(&fControlSegment);
		break;

	case kIrCancelPutRequest:
		CancelPutRequest();
		break;

	case kIrDisconnectRequest:
		HandleNDMDisconnectRequest();
		break;

	case kIrMediaBusyTimer:
		request = fRequest;
		if (request->fMediaBusyCheck != 0 && fIrGlue->MediaBusy())
		{
			fIrGlue->SetMediaBusy(false);
			StartTimer(kIrMediaBusyTime, kIrMediaBusyTimer);
			return;
		}
		// the medium is free: the discovery's first slot
		StopInput();
		fDiscoveryFlags = 0;
		fMaxSlot = 0;
		for (UByte i = 0; i < 4; i++)
		{
			if (IrSlotCounts[i] == request->fNumSlots)
			{
				fDiscoveryFlags = i;
				fMaxSlot = request->fNumSlots - 1;
				break;
			}
		}
		fConflictDevAddr = request->fDevAddr;
		if (fConflictDevAddr != kIrAllDevices)
			fDiscoveryFlags |= 4;
		fSlot = 0;
		fState = kIrLAPQuery;
		OutputXIDCommand();
		break;
	}
}


// ROM 0x000f59fc HandleQueryStateEvent__6TIrLAPFUl
// Discovering: an XID in each slot, the answers collected; after the last
// slot the discovery is done.
void
TIrLAP::HandleQueryStateEvent(ULong event)
{
	switch (event)
	{
	case kIrDisconnectRequest:
		HandleNDMDisconnectRequest();
		break;

	case kIrOutputComplete:
		if (fSlot != 0xff)
		{
			StartTimer(kIrSlotTime, kIrSlotTimer);
			StartInput(&fControlSegment);
		}
		else
		{
			TIrEvent* request = fRequest;
			fRequest = nil;
			request->fEvent = kIrDiscoverReply;
			request->fPassiveDiscovery = 0;
			fState = kIrLAPDisconnected;
			fLMP->EnqueueEvent(request);
		}
		break;

	case kIrInputComplete:
		if (fRxConnAddr == 0x7f && RecdFinalRsp(kIrXIDRsp))
		{
			TXIDPacket packet;
			if (GotData(&packet.fFormat, 12) && GetWordBE(packet.fDstDevAddr) == fMyDevAddr)
			{
				TIrDscInfo* device = new TIrDscInfo;
				if (device != nil)
				{
					TIrEvent* request = fRequest;
					device->fVersion = packet.fVersion;
					device->fDevAddr = GetWordBE(packet.fSrcDevAddr);
					device->ExtractDevInfoFromBuffer(fInputBuffer);
					request->fDiscoveredList->InsertAt(request->fDiscoveredList->Count(), device);
				}
			}
		}
		StartInput(&fControlSegment);
		break;

	case kIrSlotTimer:
		if (InputHappening())
		{
			StartTimer(kIrSlotInputTime, kIrQueryInputTimer);
			break;
		}
		// fall through
	case kIrQueryInputTimer:
		StopInput();
		fSlot = ((Long) fSlot >= (Long) fMaxSlot) ? 0xff : fSlot + 1;
		OutputXIDCommand();
		break;
	}
}


// ROM 0x000f4b8c OutputXIDCommand__6TIrLAPFv
// To all (the final slot's with our discovery information).
void
TIrLAP::OutputXIDCommand(void)
{
	UByte* frame = fFrame;
	ULong infoLength = 0;
	fSent = kIrXIDCmd;
	frame[1] = 0xff;
	frame[2] = kIrXIDCmd | kIrPF;
	frame[3] = 1;
	SetWordBE(frame + 4, fMyDevAddr);
	SetWordBE(frame + 8, fConflictDevAddr);
	frame[0xc] = fDiscoveryFlags;
	frame[0xd] = fSlot;
	frame[0xe] = 0;
	if (fSlot == 0xff)
		infoLength = fIrGlue->fDscInfo.AddDevInfoToBuffer(frame + 0xf, 0x32);
	fPutBuffer.SetControlBuffer(frame + 1, infoLength + 0xe, true);
	StartOutput(&fPutBuffer, 10);
}


// ROM 0x000f331c HandleReplyStateEvent__6TIrLAPFUl
// Another station discovering: our XID sent in a slot picked at random
// (with a new address if it asked for one); its final slot ends it - and
// if we were discovering ourselves, it is the device found.
void
TIrLAP::HandleReplyStateEvent(ULong event)
{
	switch (event)
	{
	case kIrOutputComplete:
		StartInput(&fControlSegment);
		break;

	case kIrInputComplete:
		{
			Boolean restart = true;
			if (!RecdPollCmd(kIrXIDCmd))
			{
				StartInput(&fControlSegment);
				return;
			}
			TXIDPacket packet;
			if (GotData(&packet.fFormat, 12) && packet.fFormat == 1)
			{
				ULong src = GetWordBE(packet.fSrcDevAddr);
				ULong dst = GetWordBE(packet.fDstDevAddr);
				if (src != 0 && src != kIrAllDevices
				 && (dst == kIrAllDevices || (dst == fMyDevAddr && (packet.fFlags & 4) != 0)))
				{
					if (packet.fSlot == 0xff)
					{
						StopTimer();
						if (fNewDevAddr != 0)
							fMyDevAddr = fNewDevAddr;
						fState = kIrLAPDisconnected;
						// (the ROM reads location 0 when nothing is pending,
						// whose byte is not a discover request)
						TIrEvent* request = fRequest;
						if (request != nil && request->fEvent == kIrDiscoverRequest)
						{
							TIrDscInfo* device = new TIrDscInfo;
							fRequest = nil;
							request->fEvent = kIrDiscoverReply;
							if (device == nil)
								request->fResult = -7000;
							else
							{
								device->fVersion = packet.fVersion;
								device->fDevAddr = src;
								device->ExtractDevInfoFromBuffer(fInputBuffer);
								request->fDiscoveredList->InsertAt(request->fDiscoveredList->Count(), device);
								request->fResult = noErr;
								request->fPassiveDiscovery = 1;
							}
							restart = false;
							fLMP->EnqueueEvent(request);
						}
					}
					else
					{
						if (fFirstXID)
						{
							fState = kIrLAPReply;
							fMaxSlot = IrSlotCounts[packet.fFlags & 3];
							fSlot = NewtonRand() % (int) fMaxSlot;
							fReplied = false;
							fNewDevAddr = 0;
							StartTimer(fMaxSlot * kIrReplySlotTime, kIrQueryTimer);
							fFirstXID = false;
						}
						if (!fReplied && (Long) packet.fSlot >= (Long) fSlot)
						{
							OutputXIDResponse(packet);
							fReplied = true;
							restart = false;
						}
					}
				}
			}
			if (restart)
				StartInput(&fControlSegment);
		}
		break;

	case kIrDisconnectRequest:
		HandleNDMDisconnectRequest();
		break;

	case kIrQueryTimer:
		if (fNewDevAddr != 0)
			fMyDevAddr = fNewDevAddr;
		fState = kIrLAPDisconnected;
		break;
	}
}


// ROM 0x000f4c38 OutputXIDResponse__6TIrLAPFR10TXIDPacket
// Our discovery information, from a new address if we were asked for one.
void
TIrLAP::OutputXIDResponse(TXIDPacket& packet)
{
	UByte* frame = fFrame;
	UByte flags = packet.fFlags;
	fSent = kIrXIDRsp;
	if (flags & 4)
		fNewDevAddr = (ULong) NewtonRand() % 0xfffffffe + 1;
	else
		fNewDevAddr = 0;
	frame[1] = 0xfe;
	frame[2] = kIrXIDRsp | kIrPF;
	frame[3] = 1;
	SetWordBE(frame + 4, (fNewDevAddr != 0) ? fNewDevAddr : fMyDevAddr);
	memcpy(frame + 8, packet.fSrcDevAddr, 4);
	frame[0xc] = flags & 7;
	frame[0xd] = fSlot;
	frame[0xe] = 0;
	ULong infoLength = fIrGlue->fDscInfo.AddDevInfoToBuffer(frame + 0xf, 0x32);
	fPutBuffer.SetControlBuffer(frame + 1, infoLength + 0xe, true);
	StartOutput(&fPutBuffer, 10);
}


// ROM 0x000f5b88 HandleConnectStateEvent__6TIrLAPFUl
// SNRM sent (up to five times, after listening for other traffic); UA is
// the connection, with this side the primary.  An SNRM from a station with
// a higher address than ours is taken instead.
void
TIrLAP::HandleConnectStateEvent(ULong event)
{
	switch (event)
	{
	case kIrDisconnectRequest:
		HandleNDMDisconnectRequest();
		break;

	case kIrOutputComplete:
		if (fSent == kIrRR)
		{
			StartDataReceive();
			fExtraBOFs = fPeerQOS->GetExtraBOFs();
			ConnLstnComplete(noErr);
			StartTimer(fPeerMaxTurnTime, kIrFinalTimer);
			fState = kIrLAPPriReceive;
		}
		else if (fSent == kIrSNRM)
		{
			fRetryCount++;
			StartTimer(kIrSNRMTime, kIrFinalTimer);
			StartInput(&fControlSegment);
		}
		break;

	case kIrInputComplete:
		{
			Boolean restart = true;
			UByte info[16];
			if (RecdFinalRsp(kIrUA))
			{
				if (GotData(info + 4, 8)
				 && GetWordBE(info + 4) == fPeerDevAddr && GetWordBE(info + 8) == fMyDevAddr)
				{
					StopTimer();
					if (ParseNegotiateAndInitConnState(true) != noErr)
						return;
					fIrGlue->ChangeSpeed(fMyQOS->GetBaudRate());
					OutputControlFrame(kIrRR);
					restart = false;
				}
			}
			else if (RecdPollCmd(kIrSNRM))
			{
				if (GotData(info + 4, 9) && GetWordBE(info + 8) == fMyDevAddr
				 && fMyDevAddr < GetWordBE(info + 4))
				{
					UByte connAddr = info[0xc] >> 1;
					if (connAddr > 0 && connAddr < 0x7f)
					{
						fState = kIrLAPListen;
						fConnAddr = connAddr;
						fPeerDevAddr = GetWordBE(info + 4);
						StopTimer();
						if (ParseNegotiateAndInitConnState(false) != noErr)
							return;
						OutputUAResponse();
						restart = false;
					}
				}
			}
			else if (RecdCmd(kIrDISC) || RecdRsp(kIrDM))
			{
				ConnLstnComplete(kIrDAErrLAPUnexpectedDisconnect);
				return;
			}
			if (restart)
				StartInput(&fControlSegment);
		}
		break;

	case kIrBackoffTimer:
		if (InputHappening())
		{
			fIrGlue->SetMediaBusy(false);
			fRetryCount++;
			StartTimer(kIrSNRMTime, kIrFinalTimer);
		}
		else
		{
			StopInput();
			OutputSNRMCommand();
		}
		break;

	case kIrFinalTimer:
		if (fRetryCount >= 5)
		{
			StopInput();
			ConnLstnComplete(kIrDAErrLAPFailedConnection);
		}
		else
			StartTimer(kIrSlotTime + (NewtonRand() % 50) * kMilliseconds, kIrBackoffTimer);
		break;
	}
}


// ROM 0x000f4d0c OutputSNRMCommand__6TIrLAPFv
void
TIrLAP::OutputSNRMCommand(void)
{
	UByte* frame = fFrame;
	fSent = kIrSNRM;
	frame[2] = 0xff;
	frame[3] = kIrSNRM | kIrPF;
	SetWordBE(frame + 4, fMyDevAddr);
	SetWordBE(frame + 8, fPeerDevAddr);
	frame[0xc] = fConnAddr << 1;
	ULong qosLength = fMyQOS->AddInfoToBuffer(frame + 0xd, 0x35);
	fPutBuffer.SetControlBuffer(frame + 2, qosLength + 0xb, true);
	StartOutput(&fPutBuffer, fExtraBOFs);
}


// ROM 0x000f31a4 HandleListenStateEvent__6TIrLAPFUl
// An SNRM taken (UA sent): the connection is up, this side secondary,
// when the primary's first poll comes.
void
TIrLAP::HandleListenStateEvent(ULong event)
{
	switch (event)
	{
	case kIrOutputComplete:
		fIrGlue->ChangeSpeed(fMyQOS->GetBaudRate());
		StartDataReceive();
		StartTimer(fMyMaxTurnTime << 1, kIrWatchdogTimer);
		break;

	case kIrInputComplete:
		{
			Boolean restart = true;
			if (RecdPollCmd(kIrSNRM))
			{
				UByte info[16];
				if (GotData(info + 4, 9) && GetWordBE(info + 8) == fMyDevAddr)
				{
					UByte connAddr = info[0xc] >> 1;
					if (connAddr > 0 && connAddr < 0x7f)
					{
						fState = kIrLAPListen;
						fConnAddr = connAddr;
						fPeerDevAddr = GetWordBE(info + 4);
						if (ParseNegotiateAndInitConnState(false) != noErr)
							return;
						OutputUAResponse();
						restart = false;
					}
				}
			}
			else if (RecdPollCmd(kIrRR))
			{
				fExtraBOFs = fPeerQOS->GetExtraBOFs();
				ConnLstnComplete(noErr);
				fState = kIrLAPSecReceive;
				NextState(kIrInputComplete);
				return;
			}
			if (restart)
				StartInput(&fControlSegment);
		}
		break;

	case kIrDisconnectRequest:
		HandleNDMDisconnectRequest();
		break;

	case kIrWatchdogTimer:
		ApplyDefaultConnParms();
		fState = kIrLAPDisconnected;
		break;
	}
}


// ROM 0x000f4d90 OutputUAResponse__6TIrLAPFv
void
TIrLAP::OutputUAResponse(void)
{
	UByte* frame = fFrame;
	fSent = kIrUA;
	frame[2] = fConnAddr << 1;
	frame[3] = kIrUA | kIrPF;
	SetWordBE(frame + 4, fMyDevAddr);
	SetWordBE(frame + 8, fPeerDevAddr);
	ULong qosLength = fMyQOS->AddInfoToBuffer(frame + 0xc, 0x36);
	fPutBuffer.SetControlBuffer(frame + 2, qosLength + 0xa, true);
	StartOutput(&fPutBuffer, fExtraBOFs);
}


// ROM 0x000f3548 HandleNDMDisconnectRequest__6TIrLAPFv
// Out of a connection: whatever is pending answered, and the disconnect.
void
TIrLAP::HandleNDMDisconnectRequest(void)
{
	TIrEvent* current = fCurrentEvent;
	StopInput();
	StopOutput();
	StopTimer();
	if (fRequest != nil)
	{
		TIrEvent* request = fRequest;
		fRequest = nil;
		request->fEvent++;
		request->fResult = (current->fResult != noErr) ? current->fResult : kCommErrRequestCanceled;
		fLMP->EnqueueEvent(request);
	}
	current->fEvent = kIrDisconnectReply;
	fState = kIrLAPDisconnected;
	fLMP->EnqueueEvent(current);
}


// ROM 0x000f4594 ParseNegotiateAndInitConnState__6TIrLAPFUc
// The other side's parameters (in the SNRM or UA) negotiated with ours;
// the timers and retry counts, the windows and the receive buffers (a
// window of them, of our data size) from what was agreed.
NewtonErr
TIrLAP::ParseNegotiateAndInitConnState(UByte primary)
{
	NewtonErr err;
	fPrimary = primary;
	if ((err = fPeerQOS->ExtractInfoFromBuffer(fInputBuffer)) == noErr
	 && (err = fMyQOS->NegotiateWith(fPeerQOS)) == noErr
	 && (err = fPeerQOS->NegotiateWith(fMyQOS)) == noErr)
	{
		fMyMaxTurnTime = fMyQOS->GetMaxTurnAroundTime();
		fPeerMaxTurnTime = fPeerQOS->GetMaxTurnAroundTime();
		fWatchdogTime = fPeerQOS->GetMaxTurnAroundTime() + (fPeerQOS->GetMaxTurnAroundTime() >> 2);
		fPeerMinTurnTime = fPeerQOS->GetMinTurnAroundTime();
		ULong turnTime = primary ? fPeerMaxTurnTime : fWatchdogTime;
		ULong threshold = fMyQOS->GetLinkDiscThresholdTime();
		fRetryLimit = (threshold + (turnTime >> 1)) / turnTime;
		if (threshold == kIrDisconnectTime)
			fRetryWarning = 0;
		else
			fRetryWarning = (kIrDisconnectTime + (turnTime >> 1)) / turnTime - 1;
		fPeerWindowSize = fPeerQOS->GetWindowSize();
		fMyWindowSize = fMyQOS->GetWindowSize();
		ULong count = fMyQOS->GetWindowSize();
		ULong size = fMyQOS->GetDataSize();
		fFreeGetBuffers = 0;
		fNumGetBuffers = 0;
		for ( ; fNumGetBuffers < count; fNumGetBuffers++)
		{
			CBufferSegment* buffer = new CBufferSegment;
			err = -7000;
			if (buffer == nil)
				break;
			if ((err = buffer->Init(size)) != noErr)
			{
				delete buffer;
				break;
			}
			fGetBuffers[fNumGetBuffers] = buffer;
			fFreeGetBuffers |= 1 << fNumGetBuffers;
			err = noErr;
		}
		if (err == noErr)
		{
			fVr = 0;
			fVs = 0;
			fWindow = fPeerWindowSize;
			fRxWindow = 0xff >> (8 - fMyWindowSize);
			fRetryCount = 0;
			fRemoteBusy = false;
			fLocalBusy = false;
			fSetLocalBusy = false;
			fClearLocalBusy = false;
			fRxNr = 0;
			fRxNs = 0;
			fNrProcessed = 0;
			return noErr;
		}
		FreeGetBuffers();
	}
	ConnLstnComplete(err);
	return err;
}


// ROM 0x000f4794 ConnLstnComplete__6TIrLAPFl
// The connect or listen answered; a connect that came up with this side
// secondary says so.
void
TIrLAP::ConnLstnComplete(NewtonErr result)
{
	TIrEvent* request = fRequest;
	fRequest = nil;
	request->fEvent++;
	request->fPassive = (request->fEvent == kIrConnectReply && !fPrimary);
	request->fResult = result;
	request->fDevAddr = fPeerDevAddr;
	if (result != noErr)
		fState = kIrLAPDisconnected;
	fLMP->EnqueueEvent(request);
}


// ROM 0x000f49d0 ApplyDefaultConnParms__6TIrLAPFv
// Back to the contention parameters: 9600 bps, ten extra BOFs.
void
TIrLAP::ApplyDefaultConnParms(void)
{
	fIrGlue->ChangeSpeed(9600);
	fExtraBOFs = 10;
	fLocalBusy = false;
	fSetLocalBusy = false;
	fDisconnectPending = false;
	fFRMRPending = false;
	fNeedGetBuffer = true;
}


// ROM 0x000f47e8 DisconnectComplete__6TIrLAPFl
// The connection gone: the puts answered, then the disconnect asked for -
// or, the link having gone by itself, the tool told to end it.
void
TIrLAP::DisconnectComplete(NewtonErr result)
{
	StopTimer();
	StopInput();
	StopOutput();
	CancelPendingPutRequests(nil, (result != noErr) ? result : kCommErrRequestCanceled);
	fState = kIrLAPDisconnected;
	if (fDisconnectRequest != nil)
	{
		TIrEvent* request = fDisconnectRequest;
		fDisconnectRequest = nil;
		request->fEvent = kIrDisconnectReply;
		request->fResult = result;
		fLMP->EnqueueEvent(request);
	}
	else
		fIrGlue->StartTerminate(result);
	ApplyDefaultConnParms();
}


// ROM 0x000f49a8 NotConnectedCompletion__6TIrLAPFv
void
TIrLAP::NotConnectedCompletion(void)
{
	TIrEvent* current = fCurrentEvent;
	if (current->fEvent == kIrPutDataRequest)
		current->fEvent = kIrPutDataReply;
	current->fResult = kCommErrNotConnected;
	current->fLSAPConn->EnqueueEvent(current);
}


/*------------------------------------------------------------------------------
	The primary
------------------------------------------------------------------------------*/

// ROM 0x000f35c8 HandlePriReceiveStateEvent__6TIrLAPFUl
// Polled the secondary: its frames until the final one; then, after its
// turn-around time, our turn (or, with no answer, polled again - the tool
// warned after N1 polls, the link given up after N2).
void
TIrLAP::HandlePriReceiveStateEvent(ULong event)
{
	switch (event)
	{
	case kIrDisconnectRequest:
		fDisconnectRequest = fCurrentEvent;
		fDisconnectPending = true;
		break;

	case kIrOutputComplete:
		if (fSent == kIrRR || fSent == kIrRNR)
		{
			StartDataReceive();
			StartTimer(fPeerMaxTurnTime, kIrFinalTimer);
		}
		break;

	case kIrInputComplete:
		if (fRxPF)
			StopTimer();
		fRetryCount = 0;
		fSent = 0;
		if ((fRxType & 3) != 3 || RecdRsp(kIrUI))
		{
			if (fRxErrors & (kIrNrInvalid | kIrNsInvalid))
			{
				fProtocolErrors++;
				fDisconnectPending = true;
			}
			else
				ProcessRecdInfoOrSuperFrame();
		}
		else if (RecdFinalRsp(kIrFRMR) || RecdFinalRsp(kIrDISC) || RecdFinalRsp(kIrSNRM))
			fDisconnectPending = true;
		else
			fProtocolErrors++;
		if (fState == kIrLAPPriReceive)
		{
			if (fRxPF)
				StartTimer(fPeerMinTurnTime, kIrTurnaroundTimer);
			else
				StartDataReceive();
		}
		break;

	case kIrPutDataRequest:
		PostponePutRequest();
		break;

	case kIrCancelPutRequest:
		CancelPutRequest();
		break;

	case kIrLocalBusyCleared:
		break;

	case kIrFinalTimer:
		StopInput();
		if (fRetryCount == fRetryLimit)
			DisconnectComplete(kIrDAErrLAPUnexpectedDisconnect);
		else if (fIrGlue->fDisconnectState != 0)
			DisconnectComplete(noErr);
		else
		{
			if (fRetryCount == fRetryWarning)
				fIrGlue->PostAsyncEvent(1);
			fRetryCount++;
			OutputControlFrame(fLocalBusy ? kIrRNR : kIrRR);
		}
		break;

	case kIrTurnaroundTimer:
		if (fDisconnectPending)
		{
			fSent = kIrDISC;
			fState = kIrLAPPriClose;
		}
		else if (fSetLocalBusy)
		{
			fSent = kIrRNR;
			fLocalBusy = true;
			fSetLocalBusy = false;
		}
		else if (fClearLocalBusy)
		{
			fSent = kIrRR;
			fLocalBusy = false;
			fClearLocalBusy = false;
		}
		else if (fSent == 0)
		{
			fUnacked = 0;
			fState = kIrLAPPriTransmit;
			NextState(kIrTurnaroundTimer);
			break;
		}
		OutputControlFrame(fSent);
		break;
	}
}


// ROM 0x000f3858 HandlePriTransmitStateEvent__6TIrLAPFUl
// Our turn: up to a window of I frames (the last with P), or RR to pass
// the turn.
void
TIrLAP::HandlePriTransmitStateEvent(ULong event)
{
	TIrDataXferEvent* request;
	switch (event)
	{
	case kIrDisconnectRequest:
		fDisconnectRequest = fCurrentEvent;
		fDisconnectPending = true;
		break;

	case kIrOutputComplete:
		if (fSent == 0)
		{
			fWindow--;
			if (fWindow > 0 && fMoreToSend)
			{
				if (fPutRequests->Count() == 0)
					break;
				request = (TIrDataXferEvent*) fPutRequests->At(fPutRequests->Count() - 1);
				fPutRequests->RemoveElementsAt(fPutRequests->Count() - 1, 1);
				fMoreToSend = (fPutRequests->Count() != 0);
				if (fWindow == 1 || !fMoreToSend)
					StopTimer();
				OutputDataFrame(request, !fLocalBusy && (fWindow == 1 || !fMoreToSend));
				break;
			}
			fWindow = fPeerWindowSize;
			if (fLocalBusy)
			{
				OutputControlFrame(kIrRNR);
				break;
			}
		}
		else if (fSent != kIrRR && fSent != kIrRNR)
			break;
		StartDataReceive();
		StartTimer(fPeerMaxTurnTime, kIrFinalTimer);
		fState = kIrLAPPriReceive;
		break;

	case kIrPutDataRequest:
		PostponePutRequest();
		break;

	case kIrCancelPutRequest:
		CancelPutRequest();
		break;

	case kIrLocalBusyCleared:
		break;

	case kIrPollTimer:
		StopInput();
		OutputControlFrame(kIrRR);
		break;

	case kIrTurnaroundTimer:
		if (fPutRequests->Count() != 0 && !fRemoteBusy)
		{
			request = (TIrDataXferEvent*) fPutRequests->At(fPutRequests->Count() - 1);
			fPutRequests->RemoveElementsAt(fPutRequests->Count() - 1, 1);
			fMoreToSend = (fPutRequests->Count() != 0);
			if (fWindow > 1 && fMoreToSend)
				StartTimer(fMyMaxTurnTime, kIrPollTimer);
			OutputDataFrame(request, !fLocalBusy && (fWindow == 1 || !fMoreToSend));
		}
		else if (!fRemoteBusy && fRxType == kIrIFrame)
			NextState(kIrPollTimer);
		else
			StartTimer(fMyMaxTurnTime >> 2, kIrPollTimer);
		break;
	}
}


// ROM 0x000f3ae8 HandlePriCloseStateEvent__6TIrLAPFUl
// DISC sent (three times at most) until UA or DM.
void
TIrLAP::HandlePriCloseStateEvent(ULong event)
{
	switch (event)
	{
	case kIrCancelPutRequest:
		CancelPutRequest();
		break;

	case kIrOutputComplete:
		if (fSent != kIrDISC)
			break;
		if (fDisconnectPending)
		{
			fDisconnectPending = false;
			fRetryCount = 0;
		}
		StartTimer(fPeerMaxTurnTime, kIrFinalTimer);
		StartInput(&fControlSegment);
		break;

	case kIrInputComplete:
		if (RecdFinalRsp(kIrUA))
			DisconnectComplete(noErr);
		else if (RecdFinalRsp(kIrDM))
			DisconnectComplete(kIrDAErrLAPUnexpectedDisconnect);
		else
			StartInput(&fControlSegment);
		break;

	case kIrPutDataRequest:
		NotConnectedCompletion();
		break;

	case kIrDisconnectRequest:
		fDisconnectRequest = fCurrentEvent;
		break;

	case kIrLocalBusyCleared:
		break;

	case kIrFinalTimer:
		StopInput();
		if (++fRetryCount < 3)
			OutputControlFrame(kIrDISC);
		else
			DisconnectComplete(kIrDAErrLAPUnexpectedDisconnect);
		break;
	}
}


/*------------------------------------------------------------------------------
	The secondary
------------------------------------------------------------------------------*/

// ROM 0x000f3c08 HandleSecReceiveStateEvent__6TIrLAPFUl
// The primary's frames until one with P; then, after its turn-around time,
// our answer: an FRMR owed, RNR/RR, our I frames, or the close.
void
TIrLAP::HandleSecReceiveStateEvent(ULong event)
{
	switch (event)
	{
	case kIrDisconnectRequest:
		fDisconnectRequest = fCurrentEvent;
		fDisconnectPending = true;
		fDisconnectWithUA = false;
		break;

	case kIrOutputComplete:
		if (fSent != kIrRR && fSent != kIrRNR && fSent != kIrFRMR)
			break;
		StartDataReceive();
		StartTimer(fWatchdogTime, kIrWatchdogTimer);
		break;

	case kIrInputComplete:
		if (fRxPF)
			StopTimer();
		fRetryCount = 0;
		fSent = 0;
		if (fFRMRPending && !fRxPF)
			;										// only a poll is looked at while an FRMR is owed
		else if ((fRxType & 3) != 3 || RecdCmd(kIrUI))
		{
			if (fRxErrors & (kIrNrInvalid | kIrNsInvalid))
			{
				fProtocolErrors++;
				PrepareFRMRResponse();
			}
			else
				ProcessRecdInfoOrSuperFrame();
		}
		else if (RecdPollCmd(kIrDISC))
		{
			fDisconnectPending = true;
			fDisconnectWithUA = true;
		}
		else if (RecdPollCmd(kIrSNRM))
		{
			if (fRxConnAddr != fConnAddr)
				fRxPF = 0;
			else
			{
				fDisconnectPending = true;
				fDisconnectWithUA = false;
			}
		}
		else if (RecdFinalRsp(kIrDM))
			DisconnectComplete(kIrDAErrLAPUnexpectedDisconnect);
		else
		{
			fProtocolErrors++;
			PrepareFRMRResponse();
		}
		if (fState == kIrLAPSecReceive)
		{
			if (fRxPF)
				StartTimer(fPeerMinTurnTime, kIrTurnaroundTimer);
			else
				StartDataReceive();
		}
		break;

	case kIrPutDataRequest:
		PostponePutRequest();
		break;

	case kIrCancelPutRequest:
		CancelPutRequest();
		break;

	case kIrLocalBusyCleared:
		break;

	case kIrWatchdogTimer:
		if (fRetryCount == fRetryLimit)
		{
			StopInput();
			DisconnectComplete(kIrDAErrLAPUnexpectedDisconnect);
		}
		else if (fIrGlue->fDisconnectState != 0)
		{
			StopInput();
			DisconnectComplete(noErr);
		}
		else
		{
			if (fRetryCount == fRetryWarning)
				fIrGlue->PostAsyncEvent(1);
			fRetryCount++;
			StartTimer(fWatchdogTime, kIrWatchdogTimer);
		}
		break;

	case kIrTurnaroundTimer:
		if (fDisconnectPending)
		{
			fSent = fDisconnectWithUA ? kIrUA : kIrDISC;
			fState = kIrLAPSecClose;
		}
		else if (fFRMRPending)
		{
			OutputFRMRResponse();
			fFRMRPending = false;
			break;
		}
		else if (fSetLocalBusy)
		{
			fSent = kIrRNR;
			fLocalBusy = true;
			fSetLocalBusy = false;
		}
		else if (fClearLocalBusy)
		{
			fSent = kIrRR;
			fLocalBusy = false;
			fClearLocalBusy = false;
		}
		else if (fSent == 0)
		{
			if (!fRemoteBusy && fPutRequests->Count() != 0)
			{
				fUnacked = 0;
				fState = kIrLAPSecTransmit;
				NextState(kIrTurnaroundTimer);
				break;
			}
			fSent = fLocalBusy ? kIrRNR : kIrRR;
		}
		OutputControlFrame(fSent);
		break;
	}
}


// ROM 0x000f3f40 HandleSecTransmitStateEvent__6TIrLAPFUl
// Our I frames, the last with F.
void
TIrLAP::HandleSecTransmitStateEvent(ULong event)
{
	TIrDataXferEvent* request;
	switch (event)
	{
	case kIrDisconnectRequest:
		fDisconnectRequest = fCurrentEvent;
		fDisconnectPending = true;
		break;

	case kIrOutputComplete:
		if (fSent == 0)
		{
			fWindow--;
			if (fWindow > 0 && fMoreToSend)
			{
				request = (TIrDataXferEvent*) fPutRequests->At(fPutRequests->Count() - 1);
				fPutRequests->RemoveElementsAt(fPutRequests->Count() - 1, 1);
				fMoreToSend = (fPutRequests->Count() != 0);
				OutputDataFrame(request, !fLocalBusy && (fWindow == 1 || !fMoreToSend));
				break;
			}
			fWindow = fPeerWindowSize;
			if (fLocalBusy)
			{
				OutputControlFrame(kIrRNR);
				break;
			}
		}
		else if (fSent != kIrRR && fSent != kIrRNR)
			break;
		StartDataReceive();
		StartTimer(fWatchdogTime, kIrWatchdogTimer);
		fState = kIrLAPSecReceive;
		break;

	case kIrPutDataRequest:
		PostponePutRequest();
		break;

	case kIrCancelPutRequest:
		CancelPutRequest();
		break;

	case kIrLocalBusyCleared:
		break;

	case kIrTurnaroundTimer:
		request = (TIrDataXferEvent*) fPutRequests->At(fPutRequests->Count() - 1);
		fPutRequests->RemoveElementsAt(fPutRequests->Count() - 1, 1);
		fMoreToSend = (fPutRequests->Count() != 0);
		OutputDataFrame(request, !fLocalBusy && (fWindow == 1 || !fMoreToSend));
		break;
	}
}


// ROM 0x000f4110 HandleSecCloseStateEvent__6TIrLAPFUl
// Answering a DISC with UA (and gone), or asking with RD until the
// primary sends DISC.
void
TIrLAP::HandleSecCloseStateEvent(ULong event)
{
	switch (event)
	{
	case kIrDisconnectRequest:
		fDisconnectRequest = fCurrentEvent;
		break;

	case kIrOutputComplete:
		if (fSent == kIrDISC)
		{
			fDisconnectPending = false;
			StartTimer(fWatchdogTime, kIrWatchdogTimer);
			StartInput(&fControlSegment);
		}
		else if (fSent == kIrUA)
			DisconnectComplete(noErr);
		break;

	case kIrInputComplete:
		StopTimer();
		if (RecdPollCmd(kIrDISC))
		{
			fSent = kIrUA;
			StartTimer(fPeerMinTurnTime, kIrTurnaroundTimer);
		}
		else if (RecdFinalRsp(kIrDM))
			DisconnectComplete(kIrDAErrLAPUnexpectedDisconnect);
		else if (fRxPF)
		{
			fSent = kIrDISC;
			StartTimer(fPeerMinTurnTime, kIrTurnaroundTimer);
		}
		else
		{
			StartTimer(fWatchdogTime, kIrWatchdogTimer);
			StartInput(&fControlSegment);
		}
		break;

	case kIrPutDataRequest:
		NotConnectedCompletion();
		break;

	case kIrCancelPutRequest:
		CancelPutRequest();
		break;

	case kIrLocalBusyCleared:
		break;

	case kIrWatchdogTimer:
		StopInput();
		DisconnectComplete(kIrDAErrLAPUnexpectedDisconnect);
		break;

	case kIrTurnaroundTimer:
		OutputControlFrame(fSent);
		break;
	}
}


/*------------------------------------------------------------------------------
	I and S frames
------------------------------------------------------------------------------*/

// ROM 0x000f4400 ProcessRecdInfoOrSuperFrame__6TIrLAPFv
// N(R) acknowledges our frames (a frame from a station of our own kind is a
// protocol error); an I frame in sequence goes up, one out of it asks for
// RR; RNR and RR say whether the other side is busy; REJ and a gap in
// N(R) have the frames sent again.
void
TIrLAP::ProcessRecdInfoOrSuperFrame(void)
{
	if (fRxType != kIrUI)
	{
		if (fPrimary ? (fRxCommand == 1) : (fRxCommand != 1))
		{
			fProtocolErrors++;
			DisconnectComplete(kIrDAErrProtocolError);
			return;
		}
		UpdateNrReceived();
	}
	switch (fRxType)
	{
	case kIrRNR:
		fRemoteBusy = true;
		fSent = fLocalBusy ? kIrRNR : kIrRR;
		break;

	case kIrIFrame:
		if (fLocalBusy || fSetLocalBusy)
			return;
		if (fRxErrors & kIrNsUnexpected)
		{
			fSent = kIrRR;
			return;
		}
		fLMP->Demultiplexor(fInputBuffer);
		fRxWindow &= ~(1 << fVr);
		fRxWindow |= 1 << ((fVr + fMyWindowSize) & 7);
		fVr = (fVr + 1) & 7;
		fNeedGetBuffer = true;
		if (fFreeGetBuffers == 0)
			fSetLocalBusy = true;
		if (fRxErrors & kIrNrUnexpected)
			ResendRejectedFrames();
		break;

	case kIrRR:
		fRemoteBusy = false;
		if ((fRxErrors & kIrNrUnexpected) && !fLocalBusy)
			ResendRejectedFrames();
		break;

	case kIrREJ:
	case kIrSREJ:
		ResendRejectedFrames();
		break;
	}
}


// ROM 0x000f425c UpdateNrReceived__6TIrLAPFv
// The frames N(R) acknowledges answered.
void
TIrLAP::UpdateNrReceived(void)
{
	while (fNrProcessed != fRxNr)
	{
		TIrDataXferEvent* request = fSentFrames[fNrProcessed];
		fSentFrames[fNrProcessed] = nil;
		PutComplete(request, noErr);
		fUnacked &= ~(1 << fNrProcessed);
		fNrProcessed = (fNrProcessed + 1) & 7;
	}
}


// ROM 0x000f4394 ResendRejectedFrames__6TIrLAPFv
// The frames not acknowledged back in the put list, oldest to be sent
// first, and V(S) back to N(R).
void
TIrLAP::ResendRejectedFrames(void)
{
	UByte i = (fVs - 1) & 7;
	for (;;)
	{
		TIrDataXferEvent* request = fSentFrames[i];
		if (request == nil)
			break;
		fPutRequests->InsertAt(fPutRequests->Count(), request);
		fSentFrames[i] = nil;
		fFramesResent++;
		if (fRxNr == i)
			break;
		i = (i - 1) & 7;
	}
	fVs = fRxNr;
}


// ROM 0x000f4b18 PostponePutRequest__6TIrLAPFv
void
TIrLAP::PostponePutRequest(void)
{
	fPutRequests->InsertAt(0, fCurrentEvent);
}


// ROM 0x000f4874 CancelPutRequest__6TIrLAPFv
void
TIrLAP::CancelPutRequest(void)
{
	TIrEvent* current = fCurrentEvent;
	CancelPendingPutRequests(current->fLSAPConn, kCommErrRequestCanceled);
	current->fEvent = kIrCancelPutReply;
	current->fResult = noErr;
	current->fLSAPConn->EnqueueEvent(current);
}


// ROM 0x000f48b4 CancelPendingPutRequests__6TIrLAPFP9TLSAPConnl
// A connection's puts (all of them, with none) answered, those sent and
// those waiting.
void
TIrLAP::CancelPendingPutRequests(TLSAPConn* lsapConn, NewtonErr result)
{
	for (int i = 0; i < 8; i++)
	{
		TIrDataXferEvent* request = fSentFrames[i];
		if (request != nil && (lsapConn == nil || request->fLSAPConn == lsapConn))
		{
			fSentFrames[i] = nil;
			PutComplete(request, result);
		}
	}
	for (ArrayIndex i = fPutRequests->Count() - 1; i >= 0; i--)
	{
		TIrDataXferEvent* request = (TIrDataXferEvent*) fPutRequests->At(i);
		if (request != nil && (lsapConn == nil || request->fLSAPConn == lsapConn))
		{
			fPutRequests->RemoveElementsAt(i, 1);
			PutComplete(request, result);
		}
	}
}


// ROM 0x000f4970 PutComplete__6TIrLAPFP16TIrDataXferEventl
// (A put for no connection - a reply to a stray frame - just given back.)
void
TIrLAP::PutComplete(TIrDataXferEvent* request, NewtonErr result)
{
	if (request == nil)
		return;
	if (request->fLSAPConn == nil)
	{
		fIrGlue->ReleaseEventBlock(request);
		return;
	}
	request->fEvent = kIrPutDataReply;
	request->fResult = result;
	request->fLSAPConn->EnqueueEvent(request);
}


// ROM 0x000f4570 CopyStatsTo__6TIrLAPFP15TCMOSlowIRStats
void
TIrLAP::CopyStatsTo(TCMOSlowIRStats* stats)
{
	stats->dataRetries = fFramesResent;
	stats->protocolErrs = fProtocolErrors;
}


// ROM 0x000f4584 ResetStats__6TIrLAPFv
void
TIrLAP::ResetStats(void)
{
	fFramesResent = 0;
	fProtocolErrors = 0;
}


/*------------------------------------------------------------------------------
	Receive buffers
------------------------------------------------------------------------------*/

// ROM 0x000f4a14 StartDataReceive__6TIrLAPFv
// Into a free get buffer when the last went up with a frame (or, none
// free, the control segment); otherwise into the same one again.
void
TIrLAP::StartDataReceive(void)
{
	CBufferSegment* buffer;
	if (!fNeedGetBuffer)
		buffer = fInputBuffer;
	else
	{
		buffer = &fControlSegment;
		UByte bit = 1;
		for (ULong i = 0; i < fNumGetBuffers; i++, bit <<= 1)
		{
			if (fFreeGetBuffers & bit)
			{
				fFreeGetBuffers &= ~bit;
				buffer = fGetBuffers[i];
				fNeedGetBuffer = false;
				break;
			}
		}
	}
	StartInput(buffer);
}


// ROM 0x000f4a88 ReleaseInputBuffer__6TIrLAPFP14CBufferSegment
// A get buffer free again; local busy cleared (by RR at our next turn).
void
TIrLAP::ReleaseInputBuffer(CBufferSegment* buffer)
{
	UByte bit = 1;
	for (ULong i = 0; i < fNumGetBuffers; i++, bit <<= 1)
		if (fGetBuffers[i] == buffer)
			fFreeGetBuffers |= bit;
	if (fSetLocalBusy)
	{
		fSetLocalBusy = false;
		return;
	}
	if (!fLocalBusy || fClearLocalBusy)
		return;
	fLocalBusyEvent.fEvent = kIrLocalBusyCleared;
	fLocalBusyEvent.fResult = noErr;
	fClearLocalBusy = true;
	EnqueueEvent(&fLocalBusyEvent);
}


// ROM 0x000f5110 FreeGetBuffers__6TIrLAPFv
void
TIrLAP::FreeGetBuffers(void)
{
	for (ULong i = 0; i < fNumGetBuffers; i++)
		if (fGetBuffers[i] != nil)
			delete fGetBuffers[i];
	fNumGetBuffers = 0;
	fFreeGetBuffers = 0;
}


/*------------------------------------------------------------------------------
	Frames out
------------------------------------------------------------------------------*/

// ROM 0x000f4b2c PrepareFRMRResponse__6TIrLAPFv
// The frame rejected, our V(S), V(R) and whether it was a command, and why
// (an invalid N(R), an invalid N(S), or neither: not understood).
void
TIrLAP::PrepareFRMRResponse(void)
{
	fFRMRInfo[0] = fRxControl;
	fFRMRInfo[1] = (fVr << 5) | (fVs << 1) | (fRxCommand ? 0x10 : 0);
	if (fRxErrors & kIrNrInvalid)
		fFRMRInfo[2] = 8;
	else if (fRxErrors & kIrNsInvalid)
		fFRMRInfo[2] = 0;
	else
		fFRMRInfo[2] = 1;
	fFRMRPending = true;
}


// ROM 0x000f4e40 OutputFRMRResponse__6TIrLAPFv
void
TIrLAP::OutputFRMRResponse(void)
{
	UByte* frame = fFrame;
	fSent = kIrFRMR;
	frame[0] = fConnAddr << 1;
	frame[1] = kIrFRMR | kIrPF;
	frame[2] = fFRMRInfo[0];
	frame[3] = fFRMRInfo[1];
	frame[4] = fFRMRInfo[2];
	fPutBuffer.SetControlBuffer(frame, 5, true);
	StartOutput(&fPutBuffer, fExtraBOFs);
}


// ROM 0x000f4eb0 OutputControlFrame__6TIrLAPFUc
// A U or S frame with P/F (an S frame with N(R)).
void
TIrLAP::OutputControlFrame(UByte control)
{
	UByte* frame = fFrame;
	fSent = control;
	frame[2] = (fConnAddr << 1) | (fPrimary ? 1 : 0);
	frame[3] = control | kIrPF;
	if ((control & 3) != 3)
		frame[3] |= fVr << 5;
	fPutBuffer.SetControlBuffer(frame + 2, 2, true);
	StartOutput(&fPutBuffer, fExtraBOFs);
}


// ROM 0x000f4f34 OutputDataFrame__6TIrLAPFP16TIrDataXferEventUc
// An I frame: kept by N(S) until acknowledged; the LMPDU header and then
// the put's data.
void
TIrLAP::OutputDataFrame(TIrDataXferEvent* request, UByte final)
{
	UByte* frame = fFrame;
	UByte pf = final ? kIrPF : 0;
	fSent = 0;
	fSentFrames[fVs] = request;
	fUnacked |= 1 << fVs;
	frame[2] = (fConnAddr << 1) | (fPrimary ? 1 : 0);
	frame[3] = pf | (fVr << 5) | (fVs << 1);
	fVs = (fVs + 1) & 7;
	ULong headerLength = fLMP->FillInLMPDUHeader(request, frame + 4);
	fPutBuffer.SetControlBuffer(frame + 2, headerLength + 2, true);
	fPutBuffer.SetDataBuffer(request->fBuffer, request->fOffset, request->fLength);
	StartOutput(&fPutBuffer, fExtraBOFs);
}


/*------------------------------------------------------------------------------
	Frames in
------------------------------------------------------------------------------*/

// ROM 0x000f5008 GotData__6TIrLAPFPUcUl
Boolean
TIrLAP::GotData(UByte* buffer, ULong length)
{
	return fInputBuffer->Getn(buffer, length) == (Size) length;
}


// ROM 0x000f5040 RecdCmd__6TIrLAPFUc
Boolean
TIrLAP::RecdCmd(UByte type)
{
	return fRxCommand != 0 && fRxType == type;
}


// ROM 0x000f506c RecdPollCmd__6TIrLAPFUc
Boolean
TIrLAP::RecdPollCmd(UByte type)
{
	return fRxPF != 0 && RecdCmd(type);
}


// ROM 0x000f50a4 RecdRsp__6TIrLAPFUc
Boolean
TIrLAP::RecdRsp(UByte type)
{
	return fRxCommand == 0 && fRxType == type;
}


// ROM 0x000f50c8 RecdFinalRsp__6TIrLAPFUc
Boolean
TIrLAP::RecdFinalRsp(UByte type)
{
	return fRxPF != 0 && RecdRsp(type);
}


// ROM 0x000f5250 InputComplete__6TIrLAPFUcT1
// A frame in: its address and control taken apart, N(R) and N(S) checked
// against what we sent and what the window allows; a TEST frame answered
// here, anything else to the state.
void
TIrLAP::InputComplete(UByte address, UByte control)
{
	UByte nrMask = 0, nsMask = 0;
	fInputActive = false;
	fRxCommand = address & 1;
	fRxConnAddr = address >> 1;
	fRxPF = control & kIrPF;
	fRxErrors = 0;
	if ((control & 3) != 3)
	{
		nrMask = 0xe0;
		fRxNr = (control & 0xe0) >> 5;
		if (fRxNr != fVs)
		{
			fRxErrors = kIrNrUnexpected;
			if ((fUnacked & (1 << fRxNr)) == 0)
				fRxErrors = kIrNrUnexpected | kIrNrInvalid;
		}
	}
	if ((control & 1) == 0)
	{
		nsMask = 0x0e;
		fRxNs = (control & 0x0e) >> 1;
		if (fRxNs != fVr)
		{
			fRxErrors |= kIrNsUnexpected;
			if ((fRxWindow & (1 << fRxNs)) == 0)
				fRxErrors |= kIrNsInvalid;
		}
	}
	fRxType = control & ~(nrMask | nsMask | kIrPF);
	fRxControl = control;
	if (IrDATrace())
		fprintf(stderr, "[irda %p] link state %d heard address %02x control %02x (%ld bytes)\n", (void*) fIrGlue,
			fState, address, control, (long) fInputBuffer->GetSize());
	if (fRxType == kIrTEST)
		HandleTestFrame();
	else
		NextState(kIrInputComplete);
}


// ROM 0x000f5350 HandleTestFrame__6TIrLAPFv
// A TEST for us (or for all, carrying an address for us or for all):
// echoed, to its sender from us when it was sent to all.
void
TIrLAP::HandleTestFrame(void)
{
	Size length = fInputBuffer->GetSize();
	UByte* data = fInputBuffer->fBufStart;
	if (fRxConnAddr != fConnAddr)
	{
		if (length < 8)
		{
			TestFrameComplete();
			return;
		}
		ULong dst = GetWordBE(data + 4);
		if (dst != fMyDevAddr && dst != kIrAllDevices)
		{
			TestFrameComplete();
			return;
		}
	}
	fTestFrameActive = true;
	fTestHeader[0] = (fPrimary ? 1 : 0) | (fRxConnAddr << 1);
	fTestHeader[1] = kIrTEST | kIrPF;
	if (fRxConnAddr == 0x7f)
	{
		memcpy(data + 4, data, 4);
		SetWordBE(data, fMyDevAddr);
	}
	fPutBuffer.SetControlBuffer(fTestHeader, 2, true);
	fPutBuffer.SetDataBuffer(fInputBuffer, 0, length);
	StartOutput(&fPutBuffer, fExtraBOFs);
}


// ROM 0x000f5430 TestFrameComplete__6TIrLAPFv
void
TIrLAP::TestFrameComplete(void)
{
	fTestFrameActive = false;
	StartInput(fInputBuffer);
}


/*------------------------------------------------------------------------------
	The tool, through the glue
------------------------------------------------------------------------------*/

// ROM 0x000f5100 StartTimer__6TIrLAPFUli
void
TIrLAP::StartTimer(ULong delay, int kind)
{
	fIrGlue->StartTimer1(delay, kind);
}


// ROM 0x000f5108 StopTimer__6TIrLAPFv
void
TIrLAP::StopTimer(void)
{
	fIrGlue->StopTimer1();
}


// ROM 0x000f516c TimerComplete__6TIrLAPFUl
// The link's timers to its state; the rest (the ticker) to the multiplexer.
void
TIrLAP::TimerComplete(ULong kind)
{
	if (kind >= 0x1b && kind <= 0x25)
		NextState(kind);
	else
		fLMP->TimerComplete(kind);
}


// ROM 0x000f5194 StartOutput__6TIrLAPFP15TIrLAPPutBufferUl
void
TIrLAP::StartOutput(TIrLAPPutBuffer* frame, ULong extraBOFs)
{
	fOutputActive = true;
	if (IrDATrace())
		fprintf(stderr, "[irda %p] link state %d sends control %02x\n", (void*) fIrGlue, fState, frame->fControl[1]);
	fIrGlue->StartTransmit(frame, extraBOFs);
}


// ROM 0x000f51a4 StopOutput__6TIrLAPFv
void
TIrLAP::StopOutput(void)
{
	fOutputActive = false;
	fIrGlue->StopTransmit();
}


// ROM 0x000f51b4 StartInput__6TIrLAPFP14CBufferSegment
// A frame received into the buffer (kept, cut short, if too long while we
// are busy).
void
TIrLAP::StartInput(CBufferSegment* buffer)
{
	fInputActive = true;
	fInputBuffer = buffer;
	buffer->Reset();
	fIrGlue->StartReceive(buffer, fConnAddr, (fLocalBusy || fSetLocalBusy) ? 1 : 0);
}


// ROM 0x000f5210 StopInput__6TIrLAPFv
void
TIrLAP::StopInput(void)
{
	fInputActive = false;
	fIrGlue->StopReceive();
}


// ROM 0x000f5220 InputHappening__6TIrLAPFv
Boolean
TIrLAP::InputHappening(void)
{
	return fIrGlue->ReceivingInput();
}


// ROM 0x000f5228 OutputComplete__6TIrLAPFv
void
TIrLAP::OutputComplete(void)
{
	fOutputActive = false;
	if (fTestFrameActive)
		TestFrameComplete();
	else
		NextState(kIrOutputComplete);
}
