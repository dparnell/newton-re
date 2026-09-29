/*
	File:		sound/SoundServer.cpp

	Contains:	The sound server and its channels - see SoundServer.h.
*/

#include "SoundServer.h"
#include "SoundCodec.h"
#include "SoundChannel.h"		// gSndPort, TGestaltVolumeInfo
#include "SampleConvert.h"
#include "NewtonGestalt.h"
#include "NewtonMemory.h"
#include "UserGlobals.h"
#include "hal/Atomic.h"
#include <string.h>

long	gMaxFilterNodes = 0;		// ROM 0x0c101b18 gMaxFilterNodes
long	gNumFilterNodes = 0;		// ROM 0x0c101b1c gNumFilterNodes

const ULong	kDMABufferSize = 0xea0;


/*------------------------------------------------------------------------------
	T S o u n d C h a n n e l
------------------------------------------------------------------------------*/

// ROM 0x001e36fc __ct__13TSoundChannelFUl
TSoundChannel::TSoundChannel(ULong id)
{
	fStartToken = TUMsgToken();
	fNext = nil;
	fId = id;
	fNodes = nil;
	fFreeNodes = nil;
	fFlags = 0;
	fHaveStartToken = 0;
	fResampleDirty = true;
	fDevice = 0;
}


// ROM 0x001e3764 __dt__13TSoundChannelFv
// Every node still scheduled answered as closed, the spare nodes freed.
//
// ROM BUG kept: the loop goes on to a node's fNext after FreeNode has
// put the node on the spare list, whose link it overwrote - so it walks
// into the spare list instead.  (FreeNode keeps one spare and deletes the
// rest; with one node left it walks on to the spares, which the next loop
// deletes.)
TSoundChannel::~TSoundChannel()
{
	for (ChannelNode* node = fNodes; node != nil; node = node->fNext)
	{
		CleanupNode(node);
		FreeNode(node, kSndErrNoChannel, 1);
	}
	ChannelNode* spare = fFreeNodes;
	while (spare != nil)
	{
		ChannelNode* next = spare->fNext;
		delete spare;
		spare = next;
	}
}


// ROM 0x001e5f34 MakeNode__13TSoundChannelFPP11ChannelNode
// A node off the spare list, or a new one.
NewtonErr
TSoundChannel::MakeNode(ChannelNode** node)
{
	if (fFreeNodes == nil)
	{
		ChannelNode* made = new ChannelNode;
		if (made != nil)
			made->fToken = TUMsgToken();
		*node = made;
		if (made == nil)
			return MemError();
	}
	else
	{
		*node = fFreeNodes;
		fFreeNodes = fFreeNodes->fNext;
	}
	(*node)->fNext = nil;
	(*node)->fFlags = 0;
	return noErr;
}


// ROM 0x001e42ec Schedule__13TSoundChannelFP18TUSoundNodeRequestP10TUMsgToken
// A block put at the end of the channel's list, its request's token kept
// to answer when it has been played.  Only the three sample codings a
// DMA channel converts are taken.
long
TSoundChannel::Schedule(TUSoundNodeRequest* request, TUMsgToken* token)
{
	ChannelNode* node = nil;
	long err;
	long format = request->fBlock.fFormat;
	if (format != kSoundFormatStd8 && format != kSoundFormatLinear16 && format != kSoundFormatMuLaw)
		err = kSndErrBadFormat;
	else
	{
		err = MakeNode(&node);
		if (err == noErr)
		{
			node->fToken = *token;
			node->fNodeId = request->fNodeId;
			node->fPosition = 0;
			node->fUnknown58 = 0;
			node->fData = request->fBlock.fData;
			node->fCount = request->fBlock.fCount;
			node->fSampleBits = request->fBlock.fSampleBits;
			node->fFormat = request->fBlock.fFormat;
			node->fSampleRate = request->fBlock.fSampleRate;
			node->fVolume = request->fBlock.fVolume;
			node->fStart = request->fBlock.fStart;
			node->fPlayCount = request->fBlock.fPlayCount;
			node->fLoops = request->fBlock.fLoops;
			node->fRefCon = request->fBlock.fRefCon;
			node->fCodec = request->fBlock.fCodec;
			node->fUnknown3C = request->fBlock.fUnknown2C;
			node->fUnknown40 = request->fBlock.fUnknown30;
			node->fChannelVolume = request->fChannelVolume;
			if (fNodes == nil)
				SetupNode(node);
			else
			{
				ChannelNode* last = fNodes;
				while (last->fNext != nil)
					last = last->fNext;
				last->fNext = node;
			}
			node->fNext = nil;
		}
		if (err != noErr && node != nil)
		{
			CleanupNode(node);
			FreeNode(node, kSndErrNoDriver, 1);
		}
	}
	return err;
}


// ROM 0x001e464c Cancel__13TSoundChannelFP18TUSoundNodeRequest
// The node the request names taken off the list and answered as cancelled;
// the one after it set up when it was the one playing.
long
TSoundChannel::Cancel(TUSoundNodeRequest* request)
{
	ChannelNode* prev = nil;
	for (ChannelNode* node = fNodes; node != nil; prev = node, node = node->fNext)
	{
		if (node->fNodeId == request->fNodeId)
		{
			CleanupNode(node);
			if (prev == nil)
				SetupNode(node->fNext);
			else
				prev->fNext = node->fNext;
			FreeNode(node, kSndErrCancelled, 1);
			return noErr;
		}
		if (node->fNext == nil)
			break;
	}
	return kSndErrNothingToPlay;
}


// ROM 0x001e4eec Start__13TSoundChannelFP10TUMsgToken
// The channel set running (nothing to play: an error); a start that waits
// keeps its token, answered when the channel has run dry (FreeNode).  A
// paused channel goes on.
long
TSoundChannel::Start(TUMsgToken* token)
{
	if (fNodes == nil)
		return kSndErrNothingToPlay;
	if ((fFlags & kSndChannelRunning) == 0)
	{
		fFlags |= kSndChannelRunning;
		if (token != nil)
		{
			fStartToken = *token;
			fHaveStartToken = 1;
			fResampleDirty = true;
		}
	}
	if ((fFlags & kSndChannelPaused) != 0)
		fFlags &= ~kSndChannelPaused;
	return noErr;
}


// ROM 0x001e5b78 Pause__13TSoundChannelFP16TUSoundNodeReply
// Paused, or going again when it already was (==> 1 then); the reply says
// where the playing node had got to.
long
TSoundChannel::Pause(TUSoundNodeReply* reply)
{
	long resumed = 0;
	if (reply != nil)
	{
		long position;
		if (fNodes == nil)
		{
			position = 0;
			reply->fNodeId = 0;
			reply->fState = 2;
		}
		else
		{
			reply->fNodeId = fNodes->fNodeId;
			reply->fState = 2;
			position = fNodes->fPosition;
		}
		reply->fPosition = position;
	}
	if (fNodes != nil)
	{
		if ((fFlags & kSndChannelPaused) == 0)
			fFlags |= kSndChannelPaused;
		else
		{
			fFlags &= ~kSndChannelPaused;
			resumed = 1;
		}
	}
	return resumed;
}


// ROM 0x001e5e80 Stop__13TSoundChannelFP16TUSoundNodeReplyl
// Every node answered with the error, the channel neither running nor
// paused; the reply says where the playing node had got to.
void
TSoundChannel::Stop(TUSoundNodeReply* reply, long error)
{
	if (reply != nil)
	{
		if (fNodes == nil)
		{
			reply->fState = 1;
			reply->fNodeId = 0;
			reply->fPosition = 0;
		}
		else
		{
			reply->fState = 1;
			reply->fNodeId = fNodes->fNodeId;
			reply->fPosition = fNodes->fPosition;
		}
	}
	ChannelNode* node = fNodes;
	while (node != nil)
	{
		ChannelNode* next = node->fNext;
		CleanupNode(node);
		FreeNode(node, error, 1);
		node = next;
	}
	fNodes = nil;
	fFlags &= ~(kSndChannelRunning | kSndChannelPaused);
}


// ROM 0x001e5fb8 FreeNode__13TSoundChannelFP11ChannelNodeli
// The node's request answered - its id, the error, the state and how far
// it got - and the node kept as the one spare (any more are deleted).
// When the channel has nothing left it stops, and a start that waited is
// answered too.
long
TSoundChannel::FreeNode(ChannelNode* node, long error, int state)
{
	TUSoundNodeReply reply;
	reply.fEvent.fAEventClass = kNewtEventClass;
	reply.fEvent.fAEventID = 'usnd';
	reply.fUnknown10 = 0;
	reply.fChannel = fId;
	reply.fNodeId = node->fNodeId;
	reply.fPosition = node->fPosition;
	reply.fError = error;
	reply.fState = state;
	long result = node->fToken.ReplyRPC(&reply, sizeof(reply), 0);
	long spares = 0;
	for (ChannelNode* spare = fFreeNodes; spare != nil; spare = spare->fNext)
		spares++;
	if (spares > 1)
		delete node;
	else
	{
		node->fNext = fFreeNodes;
		fFreeNodes = node;
	}
	if (fNodes == nil && (fFlags & kSndChannelKeepRunning) == 0)
	{
		fFlags &= ~(kSndChannelRunning | kSndChannelPaused);
		if (fHaveStartToken != 0)
		{
			result = fStartToken.ReplyRPC(&reply, 0x14, 0);
			fHaveStartToken = 0;
		}
	}
	return result;
}


// ROM 0x001e60d0 CleanupNode__13TSoundChannelFP11ChannelNode
// A node that was filtered no longer counted.
void
TSoundChannel::CleanupNode(ChannelNode* node)
{
	if ((node->fFlags & 1) == 0)
		return;
	node->fFlags &= ~1;
	gNumFilterNodes--;
}


/*------------------------------------------------------------------------------
	T D M A C h a n n e l
------------------------------------------------------------------------------*/

// ROM 0x001e3804 __ct__11TDMAChannelFUlRC16TSoundDriverInfo
// The hardware's format, sample size and rate; no node yet.  The volume
// gestalt's third byte (the driver's say that the server answers the
// volume) marks the channel.
TDMAChannel::TDMAChannel(ULong id, const TSoundDriverInfo& info)
	:	TSoundChannel(id)
{
	fBlockConvert = nil;
	fSampleConvert = nil;
	fResampleProc = nil;
	fHardwareFormat = info.fFormat;
	fHardwareBits = info.fSampleBits;
	fHardwareRate = info.fSampleRate >> 16;
	fNodeRate = 0;
	fNodeVolume = 0x7fffffff;
	fChannelVolume = 0x7fffffff;
	fFilteredResample = nil;
	fDevice = 0;
	TUGestalt gestalt;
	TGestaltVolumeInfo info2;
	memset(&info2, 0, sizeof(info2));
	gestalt.Gestalt(kGestalt_Ext_VolumeInfo, &info2, sizeof(info2));
	if (info2.fServerAnswersVolume)
		fFlags |= kSndChannelFixedVolume;
}


// ROM 0x001e38e4 __dt__11TDMAChannelFv
TDMAChannel::~TDMAChannel()
{ }


// ROM 0x001e3924 GetVolume__11TDMAChannelFv
// The playing node's volume, else the channel's - 0x7ffffffe (full) when
// the hardware's volume is fixed.
long
TDMAChannel::GetVolume(void)
{
	if (fNodeVolume == 0x7fffffff)
		return (fFlags & kSndChannelFixedVolume) == 0 ? fChannelVolume : 0x7ffffffe;
	return fNodeVolume;
}


// ROM 0x001e3948 SetupNode__11TDMAChannelFP11ChannelNode
// The node that plays next made current: its volume, rate and sample
// size, and the conversion from its coding to the hardware's (or back, for
// input) - a block converter where the rates agree to within 1/128,
// otherwise the plain resampler, or the filtered one while there are
// fewer filtered nodes than gMaxFilterNodes allows.
void
TDMAChannel::SetupNode(ChannelNode* node)
{
	if (fNodes == nil)
		fResampleDirty = true;
	if (node == nil)
	{
		fNodeVolume = 0x7fffffff;
		fNodeRate = 0;
		fChannelVolume = 0x7fffffff;
		fResampleProc = nil;
		fBlockConvert = nil;
		fSampleConvert = nil;
		fNodes = node;
		return;
	}
	fNodeVolume = node->fVolume;
	fChannelVolume = node->fChannelVolume;
	long nodeRate = node->fSampleRate >> 16;
	fNodeRate = nodeRate;
	fNodeBits = node->fSampleBits;
	long srcFormat, srcBits, dstFormat, dstBits, dstRate, srcRate;
	if ((fFlags & kSndChannelInput) == 0)
	{
		srcFormat = node->fFormat;
		srcBits = node->fSampleBits;
		dstFormat = fHardwareFormat;
		dstBits = fHardwareBits;
		dstRate = fHardwareRate;
		srcRate = nodeRate;
	}
	else
	{
		srcFormat = fHardwareFormat;
		srcBits = fHardwareBits;
		dstFormat = node->fFormat;
		dstBits = node->fSampleBits;
		dstRate = nodeRate;
		srcRate = fHardwareRate;
	}
	if (srcFormat == dstFormat && srcBits == dstBits)
	{
		fBlockConvert = nil;
		fSampleConvert = nil;
	}
	else if (srcFormat == kSoundFormatStd8 && dstFormat == kSoundFormatLinear16)
	{
		fBlockConvert = BlockConvertStd8ToLin16;
		fSampleConvert = SampleConvertStd8ToLin16;
	}
	else if (srcFormat == kSoundFormatMuLaw && dstFormat == kSoundFormatLinear16)
	{
		fBlockConvert = BlockConvertMuLawToLin16;
		fSampleConvert = SampleConvertMuLawToLin16;
	}
	else if (srcFormat == kSoundFormatLinear16 && dstFormat == kSoundFormatStd8)
	{
		fBlockConvert = BlockConvertLin16ToStd8;
		fSampleConvert = SampleConvertLin16ToStd8;
	}
	else if (srcFormat == kSoundFormatLinear16 && dstFormat == kSoundFormatMuLaw)
	{
		fBlockConvert = BlockConvertLin16ToMuLaw;
		fSampleConvert = SampleConvertLin16ToMuLaw;
	}
	else
	{
		fBlockConvert = nil;
		fSampleConvert = nil;
	}
	long difference = dstRate - srcRate;
	if (difference < 0)
		difference = -difference;
	long tolerance = dstRate / 128;			// (the ROM: +0x7f then >>7, rounding towards nought)
	if (gNumFilterNodes < gMaxFilterNodes)
	{
		fResampleProc = nil;
		fFilteredResample = nil;
		if (tolerance < difference)
		{
			node->fFlags |= 1;
			gNumFilterNodes++;
			fFilteredResample = ResampleFiltered;
			if (fResampleState.fSrcRate != srcRate) { fResampleState.fSrcRate = srcRate; fResampleDirty = true; }
			if (fResampleState.fSrcSampleBits != srcBits) { fResampleState.fSrcSampleBits = srcBits; fResampleDirty = true; }
			if (fResampleState.fSrcFormat != srcFormat) { fResampleState.fSrcFormat = srcFormat; fResampleDirty = true; }
			if (fResampleState.fDstRate != dstRate) { fResampleState.fDstRate = dstRate; fResampleDirty = true; }
			if (fResampleState.fDstSampleBits != dstBits) { fResampleState.fDstSampleBits = dstBits; fResampleDirty = true; }
			if (fResampleState.fDstFormat != dstFormat)
			{
				fResampleState.fDstFormat = dstFormat;
				fResampleDirty = true;
			}
			if (fResampleDirty)
			{
				InitResampleState(&fResampleState);
				fResampleDirty = false;
			}
		}
	}
	else if (difference < tolerance)
		fResampleProc = nil;
	else
		fResampleProc = Resample;
	fNodes = node;
}


// ROM 0x001e3bbc Prep__11TDMAChannelFv
// Nodes at silence (a volume of 0x80000000) dropped as though played;
// ==> whether the channel has anything to play.
Boolean
TDMAChannel::Prep(void)
{
	if (IsActive())
	{
		ChannelNode* node = fNodes;
		do
		{
			if (fNodes->fVolume != (long) 0x80000000)
				break;
			CleanupNode(node);
			SetupNode(fNodes->fNext);
			FreeNode(node, noErr, 0);
			node = fNodes;
		} while (node != nil);
	}
	return IsActive();
}


// ROM 0x001e3c74 Produce__11TDMAChannelFPvPl
// Up to *count samples of the hardware's coding put in the buffer from the
// nodes in turn - copied, converted, or resampled - a node that has been
// played through looped or freed; ==> in *count the samples produced (a
// multiple of four).
long
TDMAChannel::Produce(void* buffer, long* count)
{
	if (fNodes == nil)
	{
		*count = 0;
		return kSndErrNothingToPlay;
	}
	long done = 0;
	*count &= ~3;
	for (;;)
	{
		ChannelNode* node = fNodes;
		if (node == nil || *count <= done)
		{
			*count = done & ~3;
			return noErr;
		}
		long srcCount = node->fPlayCount - node->fPosition;
		long dstCount = *count - done;
		void* src = (char*) node->fData + (node->fSampleBits / 8) * node->fPosition;
		void* dst = (char*) buffer + done * (fHardwareBits / 8);
		if (fResampleProc == nil)
		{
			if (fFilteredResample == nil)
			{
				if (fBlockConvert == nil)
				{
					long n = dstCount;
					if (srcCount < dstCount)
						n = srcCount;
					memcpy(dst, src, n * (fHardwareBits / 8));
					done += n;
					node->fPosition += n;
					goto played;
				}
				fBlockConvert(dst, &dstCount, src, &srcCount);
			}
			else
			{
				fResampleState.fDstBuffer = (short*) dst;
				fResampleState.fSrcBuffer = (short*) src;
				fFilteredResample(&fResampleState, &dstCount, &srcCount);
			}
		}
		else
		{
			SampleSpec spec;
			spec.fDstBuffer = dst;
			spec.fDstRate = fHardwareRate;
			spec.fDstSampleBits = fHardwareBits;
			spec.fSrcBuffer = src;
			spec.fSrcRate = fNodeRate;
			spec.fSrcSampleBits = fNodeBits;
			spec.fConvert = fSampleConvert;
			fResampleProc(&spec, &dstCount, &srcCount);
		}
		node->fPosition += srcCount;
		done += dstCount;
played:
		if (node->fPosition == node->fPlayCount)
		{
			if (node->fLoops < 1)
			{
				CleanupNode(node);
				SetupNode(fNodes->fNext);
				FreeNode(node, noErr, 0);
			}
			else
			{
				node->fLoops--;
				node->fPosition = 0;
			}
		}
	}
}


// ROM 0x001e3ec4 Consume__11TDMAChannelFPvPl
// Produce's mirror for input: up to *count of the hardware's samples in
// the buffer put into the nodes in turn, a node that is full freed (input
// never loops).
long
TDMAChannel::Consume(void* buffer, long* count)
{
	if (fNodes == nil)
	{
		*count = 0;
		return kSndErrNothingToPlay;
	}
	long done = 0;
	*count &= ~3;
	for (;;)
	{
		ChannelNode* node = fNodes;
		if (node == nil || *count <= done)
		{
			*count = done & ~3;
			if (fNodes == nil)
				*count = 0;
			return noErr;
		}
		long dstCount = node->fPlayCount - node->fPosition;
		long srcCount = *count - done;
		void* dst = (char*) node->fData + (node->fSampleBits / 8) * node->fPosition;
		void* src = (char*) buffer + done * (fHardwareBits / 8);
		if (fResampleProc == nil)
		{
			if (fFilteredResample == nil)
			{
				if (fBlockConvert == nil)
				{
					long n = dstCount;
					if (srcCount < dstCount)
						n = srcCount;
					memcpy(dst, src, n * (fHardwareBits / 8));
					done += n;
					node->fPosition += n;
					goto filled;
				}
				fBlockConvert(dst, &dstCount, src, &srcCount);
			}
			else
			{
				fResampleState.fDstBuffer = (short*) dst;
				fResampleState.fSrcBuffer = (short*) src;
				fFilteredResample(&fResampleState, &dstCount, &srcCount);
			}
		}
		else
		{
			SampleSpec spec;
			spec.fDstBuffer = dst;
			spec.fDstRate = fNodeRate;
			spec.fDstSampleBits = fNodeBits;
			spec.fSrcBuffer = src;
			spec.fSrcRate = fHardwareRate;
			spec.fSrcSampleBits = fHardwareBits;
			spec.fConvert = fSampleConvert;
			fResampleProc(&spec, &dstCount, &srcCount);
		}
		node->fPosition += dstCount;
		done += srcCount;
filled:
		if (node->fPosition == node->fPlayCount)
		{
			CleanupNode(node);
			SetupNode(fNodes->fNext);
			FreeNode(node, noErr, 0);
		}
	}
}


/*------------------------------------------------------------------------------
	T C o d e c C h a n n e l
------------------------------------------------------------------------------*/

// ROM 0x001e4110 __ct__13TCodecChannelFUlRC16TSoundDriverInfo
// Its lock and its port; the task that decompresses into the output
// channel is started when coded sound is scheduled (NOT YET).
TCodecChannel::TCodecChannel(ULong id, const TSoundDriverInfo& /*info*/)
	:	TSoundChannel(id)
{
	fCodecNodes = nil;
	fLock.Init();
	fUnknown1E4 = 0;
	fUnknown1F4 = 0;
	fUnknown208 = 0;
	fPort.Init();
	fOutputChannelId = 0;
	fOutputChannel = nil;
}


// ROM 0x001e419c __dt__13TCodecChannelFv
// Every node answered as closed.  NOT YET RECONSTRUCTED: ReleaseNode for
// the decompressing loop's nodes (fCodecNodes), which nothing makes yet.
TCodecChannel::~TCodecChannel()
{
	ChannelNode* node = fNodes;
	while (node != nil)
	{
		ChannelNode* next = node->fNext;
		fNodes = next;
		FreeNode(node, kSndErrNoChannel, 1);
		node = next;
	}
}


// ROM 0x001e442c SetupNode__13TCodecChannelFP11ChannelNode
// NOT YET RECONSTRUCTED: InitNode (0x001e494c), the codec opened on the
// node's data - only coded sound is scheduled on a decompressor, and
// TFrameSoundChannel does not schedule it yet.
void
TCodecChannel::SetupNode(ChannelNode* node)
{
	fNodes = node;
}


/*------------------------------------------------------------------------------
	T S o u n d S e r v e r H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x001e7ef0 Init__19TSoundServerHandlerFP12TSoundServer
long
TSoundServerHandler::Init(TSoundServer* server)
{
	fServer = server;
	return TAEventHandler::Init('usnd', kNewtEventClass);
}


// ROM 0x001e7f08 AEHandlerProc__19TSoundServerHandlerFP10TUMsgTokenPUlP7TAEvent
// A command: the interrupts' own (a buffer played - the next one filled
// and scheduled, or the output turned off when there is nothing more),
// and a client's, answered at once - except a start that waits and a
// schedule, answered when the playing is done.
void
TSoundServerHandler::AEHandlerProc(TUMsgToken* token, ULong* /*size*/, TAEvent* event)
{
	TUSoundNodeRequest* request = (TUSoundNodeRequest*) event;
	ULong command = request->fCommand;
	TSoundServer* server = fServer;
	if (command == kSndOutputDone)
	{
		Swap((ULong*) &server->fOutputIntMessage->fEvent.fCount, 0);
		ULong which = server->FillDMABuffer();
		if (server->fDMASize[which] < 1 && !gSndDriver->OutputIsRunning())
		{
			gSndDriver->StopOutput();
			gSndDriver->PowerOutputOff();
			server->fOutputIntMessage->fMessage.Abort();
			server->fOutputIntMessage->fEvent.fCount = 0;
			return;
		}
		gSndDriver->ScheduleOutputBuffer(which, server->fDMASize[which]);
		return;
	}
	if (command == kSndInputDone)
	{
		// NOT YET RECONSTRUCTED: EmptyDMABuffer and the input side
		return;
	}
	if (command == 0xffffffff)
	{
		// (the ROM: +0x28 of the server's +0x38 set - a field of the app
		// world's state the reconstruction does not name)
		return;
	}
	TUSoundNodeReply reply;
	reply.fEvent.fAEventClass = kNewtEventClass;
	reply.fEvent.fAEventID = 'usnd';
	reply.fError = 0;
	reply.fUnknown10 = 0;
	reply.fNodeId = 0;
	reply.fState = 0;
	reply.fPosition = 0;
	ULong size = 0x14;
	reply.fChannel = request->fChannel;
	ULong value = ((ULong*) event)[4];					// (the event's +0x10: an immediate command's value)
	switch (command)
	{
	case kSndStopAll:
		server->StopAll();
		DeferReply();
		return;
	case kSndOpenOutput:
		reply.fError = server->OpenOutputChannel(&reply.fChannel, value);
		break;
	case kSndClose:
		reply.fError = server->CloseChannel(request->fChannel);
		break;
	case kSndStart:
		reply.fError = server->StartChannel(request->fChannel, nil);
		break;
	case kSndStartWait:
		reply.fError = server->StartChannel(request->fChannel, token);
		if (reply.fError == noErr)
		{
			DeferReply();
			return;
		}
		break;
	case kSndPause:
		reply.fError = server->PauseChannel(request->fChannel, &reply);
		size = 0x20;
		break;
	case kSndStop:
		reply.fError = server->StopChannel(request->fChannel, &reply);
		size = 0x20;
		break;
	case kSndSchedule:
		reply.fError = server->ScheduleNode(request, token);
		if (reply.fError == noErr)
		{
			DeferReply();
			return;
		}
		reply.fUnknown10 = request->fNodeId;
		reply.fNodeId = 0;
		size = 0x20;
		break;
	case kSndCancel:
		reply.fError = server->CancelNode(request);
		break;
	case kSndSetVolume:
		reply.fError = server->SetOutputVolume(value);
		reply.fUnknown10 = gSndDriver->OutputVolume();
		break;
	case kSndSetOutputDevice:
		reply.fError = server->SetOutputDevice(request->fChannel, value);
		break;
	case kSndOpenDecompressor:
		reply.fError = server->OpenDecompressorChannel(&reply.fChannel, value);
		break;
	case kSndGetVolume:
		reply.fUnknown10 = gSndDriver->OutputVolume();
		break;
	case kSndOpenInput:				// NOT YET RECONSTRUCTED: the input side
	case kSndSetInputGain:
	case kSndSetInputDevice:
	case kSndOpenCompressor:
	default:
		reply.fError = kSndErrGeneric;
		break;
	}
	SetReply(size, &reply.fEvent);
	ReplyImmed();
}


/*------------------------------------------------------------------------------
	T S o u n d S e r v e r
------------------------------------------------------------------------------*/

// ROM 0x001e81cc __ct__12TSoundServerFv
TSoundServer::TSoundServer()
{
	fOutputIntMessage = nil;
	fInputIntMessage = nil;
	fLastId = 0;
	fMixBuffer = nil;
	fMix = nil;
	fSilence = 0;
	fOutputChannels = nil;
	fDMABuffer[0] = fDMABuffer[1] = nil;
	fDMASize[0] = fDMASize[1] = 0;
	fDMAIndex = 0;
	fInputChannels = nil;
	fInputBuffer[0] = fInputBuffer[1] = nil;
	fInputSize[0] = fInputSize[1] = 0;
	fDecompressorChannels = nil;
	fCompressorChannels = nil;
	fVolume = 0;
}


// ROM 0x001e86c4 GetSizeOf__12TSoundServerFv
ULong
TSoundServer::GetSizeOf()
{
	return sizeof(TSoundServer);		// (the ROM: 0x104)
}


// ROM 0x001e8274 MainConstructor__12TSoundServerFv
// The driver made (the machine's own, PMainSoundDriver, before the
// Cirrus one), the mixing buffer and the two DMA buffers, the interrupt
// callbacks, the hardware powered down, the handler installed, and the
// two messages the interrupts send made; gSndPort is set.
long
TSoundServer::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	gSndDriver = (PSoundDriver*) NewByName("PSoundDriver", "PMainSoundDriver");
	if (gSndDriver == nil)
		gSndDriver = (PSoundDriver*) NewByName("PSoundDriver", "PCirrusSoundDriver");
	if (gSndDriver == nil)
		return kSndErrNoDriver;
	LockPtr((Ptr) gSndDriver);
	fMixBuffer = NewPtr(kDMABufferSize);
	if (fMixBuffer == nil)
		return MemError();
	fDMABuffer[0] = NewWiredPtr(kDMABufferSize);
	if (fDMABuffer[0] == nil)
		return MemError();
	fDMABuffer[1] = NewWiredPtr(kDMABufferSize);
	if (fDMABuffer[1] == nil)
		return MemError();
	gSndDriver->SetOutputBuffers((VAddr) fDMABuffer[0], kDMABufferSize, (VAddr) fDMABuffer[1], kDMABufferSize);
	gSndDriver->SetOutputCallbackProc(SoundOutputIH, this);
	if (gSndDriver->ClassInfo()->GetCapability("SoundInput") != nil)
	{
		fInputBuffer[0] = NewWiredPtr(kDMABufferSize);
		if (fInputBuffer[0] == nil)
			return MemError();
		fInputBuffer[1] = NewWiredPtr(kDMABufferSize);
		if (fInputBuffer[1] == nil)
			return MemError();
		gSndDriver->SetInputBuffers((VAddr) fInputBuffer[0], kDMABufferSize, (VAddr) fInputBuffer[1], kDMABufferSize);
	}
	gSndDriver->SetInputCallbackProc(nil, this);		// (the ROM: SoundInputIH - NOT YET)
	gSndDriver->PowerOutputOff();
	gSndDriver->PowerInputOff();
	err = fHandler.Init(this);
	if (err != noErr)
		return err;
	// NOT YET RECONSTRUCTED: TSoundPowerHandler::Init (the hardware powered
	// down when the machine is)
	gSndPort = *GetMyPort();
	for (long i = 0; i < 2; i++)
	{
		TSoundIntMessage* message = new TSoundIntMessage;
		if (message != nil)
		{
			message->fEvent.fEvent.fAEventClass = kNewtEventClass;
			message->fEvent.fEvent.fAEventID = 'usnd';
			message->fEvent.fCount = 0;
			message->fEvent.fCommand = 0;
			message->fEvent.fUnknown10 = 0;
		}
		if (i == 0)
			fOutputIntMessage = message;
		else
			fInputIntMessage = message;
		if (message == nil)
			return MemError();
		err = message->fMessage.Init(false);
		if (err != noErr)
			return err;
		message->fEvent.fCommand = (i == 0) ? kSndOutputDone : kSndInputDone;
	}
	fSilence = 0;
	fMix = MixLin16;
	return noErr;
}


// ROM 0x001e8690 TheMain__12TSoundServerFv
// The event loop, on a locked stack.  DEVIATION: the host locks nothing
// (LockStack and the stack manager's unlock are the MMU's).
void
TSoundServer::TheMain()
{
	TAppWorld::TheMain();
}


// ROM 0x001e86cc CloseChannel__12TSoundServerFUl
// The channel found in whichever list holds it, stopped, taken out and
// deleted.
long
TSoundServer::CloseChannel(ULong id)
{
	TSoundChannel** lists[4] = { &fOutputChannels, &fInputChannels, &fDecompressorChannels, &fCompressorChannels };
	for (long i = 0; i < 4; i++)
	{
		TSoundChannel* prev = nil;
		for (TSoundChannel* channel = *lists[i]; channel != nil; prev = channel, channel = channel->fNext)
		{
			if (channel->fId == id)
			{
				channel->Stop(nil, noErr);
				if (prev == nil)
					*lists[i] = channel->fNext;
				else
					prev->fNext = channel->fNext;
				delete channel;
				return noErr;
			}
		}
	}
	return kSndErrNoChannel;
}


// ROM 0x001e8818 StartChannel__12TSoundServerFUlP10TUMsgToken
// A decompressor or compressor with nothing of its own (or paused) starts
// the channel it feeds instead, and a paused one is let go on; the
// hardware is started for the kind of channel started.
long
TSoundServer::StartChannel(ULong id, TUMsgToken* token)
{
	TSoundChannel* channel = FindChannel(id);
	long err = kSndErrNoChannel;
	if (channel == nil)
		return err;
	TSoundChannel* codec = nil;
	ULong flags = channel->fFlags;
	if ((flags & (kSndChannelDecompressor | kSndChannelCompressor)) != 0
	 && ((flags & kSndChannelPaused) != 0 || channel->fNodes == nil))
	{
		if ((flags & kSndChannelPaused) != 0)
			codec = channel;
		channel = FindChannel(((TCodecChannel*) channel)->fOutputChannelId);
	}
	if (channel != nil
	 && ((channel->fFlags & kSndChannelRunning) == 0 || (channel->fFlags & kSndChannelPaused) != 0))
	{
		err = channel->Start(token);
		if (err == noErr)
		{
			ULong kind = channel->fFlags;
			if ((kind & kSndChannelOutput) != 0)
				StartOutput(channel->fDevice);
			else if ((kind & kSndChannelInput) != 0)
				;		// NOT YET RECONSTRUCTED: StartInput
			else if ((kind & kSndChannelDecompressor) != 0)
				StartDecompressor(channel->fDevice);
			else if ((kind & kSndChannelCompressor) != 0)
				;		// NOT YET RECONSTRUCTED: StartCompressor
		}
	}
	if (codec != nil)
	{
		if (err == kSndErrNothingToPlay)
			err = noErr;
		codec->Pause(nil);
	}
	return err;
}


// ROM 0x001e8944 PauseChannel__12TSoundServerFUlP16TUSoundNodeReply
// A decompressor or compressor pauses the channel it feeds as well (the
// reply then left to it).
long
TSoundServer::PauseChannel(ULong id, TUSoundNodeReply* reply)
{
	TSoundChannel* channel = FindChannel(id);
	if (channel == nil)
		return kSndErrNoChannel;
	ULong flags = channel->fFlags;
	Boolean codec = (flags & (kSndChannelDecompressor | kSndChannelCompressor)) != 0;
	TSoundChannel* target = channel;
	if (codec)
		target = FindChannel(((TCodecChannel*) channel)->fOutputChannelId);
	if (target == nil)
		return kSndErrNoChannel;
	if (codec && (flags & kSndChannelRunning) != 0)
	{
		channel->Pause(reply);
		reply = nil;
	}
	target->Pause(reply);
	return noErr;
}


// ROM 0x001e8aac StopChannel__12TSoundServerFUlP16TUSoundNodeReply
// A decompressor or compressor that is not running stops the channel it
// feeds instead; an input channel's stopping turns the input off once no
// input channel has anything left.
long
TSoundServer::StopChannel(ULong id, TUSoundNodeReply* reply)
{
	TSoundChannel* channel = FindChannel(id);
	if (channel == nil)
		return kSndErrNoChannel;
	ULong flags = channel->fFlags;
	if ((flags & (kSndChannelDecompressor | kSndChannelCompressor)) != 0 && (flags & kSndChannelRunning) == 0)
		channel = FindChannel(((TCodecChannel*) channel)->fOutputChannelId);
	if (channel == nil)
		return kSndErrNoChannel;
	channel->Stop(reply, kSndErrNoChannel);
	// NOT YET RECONSTRUCTED: the input side (AllInputChannelsEmpty, the
	// input stopped and powered off)
	return noErr;
}


// ROM 0x001e8b70 ScheduleNode__12TSoundServerFP18TUSoundNodeRequestP10TUMsgToken
long
TSoundServer::ScheduleNode(TUSoundNodeRequest* request, TUMsgToken* token)
{
	TSoundChannel* channel = FindChannel(request->fChannel);
	if (channel != nil)
		return channel->Schedule(request, token);
	return noErr;		// ROM QUIRK kept: an unknown channel answers whatever r0 held - the channel's lookup's nil
}


// ROM 0x001e8bb0 CancelNode__12TSoundServerFP18TUSoundNodeRequest
long
TSoundServer::CancelNode(TUSoundNodeRequest* request)
{
	TSoundChannel* channel = FindChannel(request->fChannel);
	if (channel != nil)
		return channel->Cancel(request);
	return noErr;
}


// ROM 0x001e8be8 FindChannel__12TSoundServerFUl
// The output channels, then the decompressors, the input channels and the
// compressors.
TSoundChannel*
TSoundServer::FindChannel(ULong id)
{
	TSoundChannel* lists[4] = { fOutputChannels, fDecompressorChannels, fInputChannels, fCompressorChannels };
	for (long i = 0; i < 4; i++)
		for (TSoundChannel* channel = lists[i]; channel != nil; channel = channel->fNext)
			if (channel->fId == id)
				return channel;
	return nil;
}


// ROM 0x001e8ca0 StopAll__12TSoundServerFv
void
TSoundServer::StopAll(void)
{
	// NOT YET RECONSTRUCTED: StopCompressor
	StopDecompressor(1);
	StopOutput(1);
	for (TSoundChannel* channel = fInputChannels; channel != nil; channel = channel->fNext)
		channel->Stop(nil, kSndErrStopped);
	gSndDriver->StopInput();
	gSndDriver->PowerInputOff();
}


// ROM 0x001e8ce0 UniqueId__12TSoundServerFv
ULong
TSoundServer::UniqueId(void)
{
	do
	{
		if (++fLastId == 0)
			fLastId = 1;
	} while (FindChannel(fLastId) != nil);
	return fLastId;
}


// ROM 0x001e8d20 OpenOutputChannel__12TSoundServerFPUlUl
// A DMA channel for the hardware's format, at the front of the list.
NewtonErr
TSoundServer::OpenOutputChannel(ULong* id, ULong device)
{
	if (gSndDriver->ClassInfo()->GetCapability("SoundOutput") == nil)
		return kSndErrBadFormat;
	*id = 0;
	TSoundDriverInfo info;
	info.fUnknown00 = 1;
	gSndDriver->GetSoundHardwareInfo(&info);
	TDMAChannel* channel = new TDMAChannel(UniqueId(), info);
	if (channel == nil)
		return MemError();
	channel->fFlags |= kSndChannelOutput;
	channel->fNext = fOutputChannels;
	channel->fDevice = device;
	fOutputChannels = channel;
	*id = channel->fId;
	return noErr;
}


// ROM 0x001e8dec AllOutputChannelsEmpty__12TSoundServerFv
Boolean
TSoundServer::AllOutputChannelsEmpty(void)
{
	for (TSoundChannel* channel = fOutputChannels; channel != nil; channel = channel->fNext)
		if (channel->IsActive())
			return false;
	return true;
}


// ROM 0x001e8e2c StartOutput__12TSoundServerFi
// When the hardware is not already playing and some channel has
// something: the first buffer filled and scheduled, the output powered
// and started, and the second buffer filled and scheduled behind it.
void
TSoundServer::StartOutput(long device)
{
	if (gSndDriver->OutputIsRunning())
		return;
	if (AllOutputChannelsEmpty())
		return;
	fOutputIntMessage->fMessage.Abort();
	fOutputIntMessage->fEvent.fCount = 0;
	ULong which = FillDMABuffer();
	if (fDMASize[which] < 1)
		return;
	gSndDriver->PowerOutputOn(device);
	gSndDriver->ScheduleOutputBuffer(which, fDMASize[which]);
	if (gSndDriver->OutputIsEnabled())
		return;
	if (gSndDriver->StartOutput() == noErr)
		return;
	which = FillDMABuffer();
	gSndDriver->ScheduleOutputBuffer(which, fDMASize[which]);
}


// ROM 0x001e8ef4 StopOutput__12TSoundServerFi
void
TSoundServer::StopOutput(long hardware)
{
	for (TSoundChannel* channel = fOutputChannels; channel != nil; channel = channel->fNext)
		channel->Stop(nil, kSndErrStopped);
	if (hardware != 0)
	{
		gSndDriver->StopOutput();
		gSndDriver->PowerOutputOff();
	}
}


// ROM 0x001e8f60 ScheduleOutputBuffer__12TSoundServerFv
void
TSoundServer::ScheduleOutputBuffer(void)
{
	ULong which = FillDMABuffer();
	if (fDMASize[which] < 1 && !gSndDriver->OutputIsRunning())
	{
		gSndDriver->StopOutput();
		gSndDriver->PowerOutputOff();
		fOutputIntMessage->fMessage.Abort();
		fOutputIntMessage->fEvent.fCount = 0;
		return;
	}
	gSndDriver->ScheduleOutputBuffer(which, fDMASize[which]);
}


// ROM 0x001e8fdc PrepOutputChannels__12TSoundServerFv
Boolean
TSoundServer::PrepOutputChannels(void)
{
	Boolean any = false;
	for (TSoundChannel* channel = fOutputChannels; channel != nil; channel = channel->fNext)
		if (((TDMAChannel*) channel)->Prep())
			any = true;
	return any;
}


// ROM 0x001e901c FillDMABuffer__12TSoundServerFv
// The buffer whose turn it is filled: the first active output channel's
// samples, silence after them, every other active channel mixed in, and
// the loudest channel's volume given to the hardware.  ==> which buffer;
// its size (bytes) is fDMASize[which], nought when there was nothing.
ULong
TSoundServer::FillDMABuffer(void)
{
	ULong which = fDMAIndex;
	void* buffer = fDMABuffer[which];
	TDMAChannel* first = (TDMAChannel*) fOutputChannels;
	long samples = (first->fHardwareBits == 16) ? 0x750 : 0xea0;
	fDMAIndex = 1 - which;
	if (!PrepOutputChannels())
		memset(buffer, fSilence, samples * (first->fHardwareBits == 8 ? 1 : 2));
	else if (samples != 0)
	{
		long volume = 0;
		for (TSoundChannel* c = fOutputChannels; c != nil; c = c->fNext)
		{
			if (!c->IsActive())
				continue;
			TDMAChannel* channel = (TDMAChannel*) c;
			volume = channel->GetVolume();
			long count = samples;
			channel->Produce(buffer, &count);
			long played = count;
			if (count < samples)
			{
				long bytes = (first->fHardwareBits != 8) ? 2 : 1;
				memset((char*) buffer + bytes * count, fSilence, bytes * (samples - count));
			}
			for (TSoundChannel* o = channel->fNext; o != nil; o = o->fNext)
			{
				if (!o->IsActive())
					continue;
				TDMAChannel* other = (TDMAChannel*) o;
				if (volume <= other->GetVolume())
					volume = other->GetVolume();
				count = samples;
				other->Produce(fMixBuffer, &count);
				fMix(buffer, fMixBuffer, count);
				if (played <= count)
					played = count;
			}
			samples -= played;
			break;
		}
		SetOutputVolume(volume);
	}
	if (first->fHardwareBits == 16)
		samples <<= 1;
	fDMASize[which] = kDMABufferSize - samples;
	return which;
}


// ROM 0x001e9568 SetOutputVolume__12TSoundServerFl
long
TSoundServer::SetOutputVolume(long decibels)
{
	if (fVolume != decibels)
	{
		gSndDriver->OutputVolume(decibels);
		fVolume = decibels;
	}
	return noErr;
}


// ROM 0x001e95a8 SoundOutputIH__12TSoundServerFv
// The driver's output interrupt: one more buffer played, and the server
// told so.
long
TSoundServer::SoundOutputIH(void* refCon)
{
	TSoundServer* server = (TSoundServer*) refCon;
	server->fOutputIntMessage->fEvent.fCount++;
	return SendForInterrupt(gSndPort, server->fOutputIntMessage->fMessage.GetMsgId(), 0,
							&server->fOutputIntMessage->fEvent, sizeof(TSoundIntEvent), 0x04000000, 0, nil, false);
}


// ROM 0x001e9694 SetOutputDevice__12TSoundServerFUll
long
TSoundServer::SetOutputDevice(ULong id, long device)
{
	TSoundChannel* channel = FindChannel(id);
	if (channel != nil)
		channel->fDevice = device;
	return noErr;
}


// ROM 0x001e96b4 OpenDecompressorChannel__12TSoundServerFPUlUl
// A codec channel feeding the output channel given, at the front of the
// decompressors.
NewtonErr
TSoundServer::OpenDecompressorChannel(ULong* id, ULong outputId)
{
	if (gSndDriver->ClassInfo()->GetCapability("SoundOutput") == nil)
		return kSndErrBadFormat;
	*id = 0;
	TSoundDriverInfo info;
	info.fUnknown00 = 1;
	gSndDriver->GetSoundHardwareInfo(&info);
	TCodecChannel* channel = new TCodecChannel(UniqueId(), info);
	if (channel == nil)
		return MemError();
	channel->fFlags |= kSndChannelDecompressor;
	channel->fOutputChannelId = outputId;
	channel->fNext = fDecompressorChannels;
	channel->fOutputChannel = FindChannel(outputId);
	fDecompressorChannels = channel;
	*id = channel->fId;
	return noErr;
}


// ROM 0x001e9790 StartDecompressor__12TSoundServerFi
void
TSoundServer::StartDecompressor(long /*device*/)
{ }


// ROM 0x001e9794 StopDecompressor__12TSoundServerFi
void
TSoundServer::StopDecompressor(long /*hardware*/)
{
	for (TSoundChannel* channel = fDecompressorChannels; channel != nil; channel = channel->fNext)
		channel->Stop(nil, kSndErrStopped);
}


// ROM 0x001e7380 MixLin16__FPvT1l
// One buffer of 16-bit samples added into another, clamped to a short.
// (The samples are the host's own shorts, as sound/SampleConvert.h's are;
// the ROM's are the ARM's big-endian ones.)
void
MixLin16(void* dst, void* src, long count)
{
	short* d = (short*) dst;
	const short* s = (const short*) src;
	for (long i = 0; i < count; i++)
	{
		long sum = (long) s[i] + (long) d[i];
		if (sum > 0x7fff)
			sum = 0x7fff;
		else if (sum < -0x8000)
			sum = -0x8000;
		d[i] = (short) sum;
	}
}
