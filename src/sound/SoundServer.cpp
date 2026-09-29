/*
	File:		sound/SoundServer.cpp

	Contains:	The sound server and its channels - see SoundServer.h.
*/

#include "SampleWords.h"
#include "SoundServer.h"
#include "SoundCodec.h"
#include "SoundChannel.h"		// gSndPort, TGestaltVolumeInfo
#include "SampleConvert.h"
#include "NewtonGestalt.h"
#include "NewtonMemory.h"
#include "UserGlobals.h"
#include "hal/Atomic.h"
#include "UserTasks.h"
#include "OSErrors.h"
#include "FrameSoundChannel.h"		// ConvertCodecBlock
#include <stdlib.h>
#include <stddef.h>
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
			node->fCodecState = nil;
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
	A decompressor: when started, a task of its own ('codc, MainEventLoop)
	runs each node's codec a buffer at a time (the frame's bufferSize, up to
	bufferCount of them out at once) and schedules the buffers on the
	output channel it feeds, through gSndPort as any client would; each
	buffer's reply comes back to the channel's own port, and a node is
	answered (ReleaseNode) once its last buffer is back.
------------------------------------------------------------------------------*/

// ROM 0x001e4110 __ct__13TCodecChannelFUlRC16TSoundDriverInfo
TCodecChannel::TCodecChannel(ULong id, const TSoundDriverInfo& /*info*/)
	:	TSoundChannel(id)
{
	fCodecNodes = nil;
	fLock.Init();
	fCodecFlags = 0;
	fOutstanding = 0;
	fCodecState = nil;
	fPort.Init();
	fOutputChannelId = 0;
	fOutputChannel = nil;
	fStoppedNodeId = 0;
	fStoppedPosition = 0;
}


// ROM 0x001e419c __dt__13TCodecChannelFv
// Every node answered as closed, and the ones waiting for their buffers
// let go.
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
	node = fCodecNodes;
	while (node != nil)
	{
		ChannelNode* next = node->fNext;
		ReleaseNode(node);
		node = next;
	}
}


// ROM 0x001e442c SetupNode__13TCodecChannelFP11ChannelNode
// The node to decode next: its codec state made and its codec started.
void
TCodecChannel::SetupNode(ChannelNode* node)
{
	if (node != nil)
		InitNode(node);
	fNodes = node;
}


// ROM 0x001e4454 FreeNode__13TCodecChannelFP11ChannelNodeli
// A node being decoded is not answered yet: it waits at the end of
// fCodecNodes, with the answer it is to have, until its buffers are back.
long
TCodecChannel::FreeNode(ChannelNode* node, long error, int state)
{
	CodecState* codecState = GetCodecState(node);
	if (codecState == nil)
		return TSoundChannel::FreeNode(node, error, state);
	codecState->fError = error;
	codecState->fState = state;
	if (fCodecNodes == nil)
		fCodecNodes = node;
	else
	{
		ChannelNode* last = fCodecNodes;
		while (last->fNext != nil)
			last = last->fNext;
		last->fNext = node;
	}
	node->fNext = nil;
	return noErr;
}


// ROM 0x001e44e8 ReleaseNode__13TCodecChannelFP11ChannelNode
// A node answered at last: its codec stopped, its buffers given back when
// nothing else uses the state, and the client told.
long
TCodecChannel::ReleaseNode(ChannelNode* node)
{
	if (fCodecNodes == node)
		fCodecNodes = node->fNext;
	else if (fNodes == node)
		fNodes = node->fNext;
	CodecState* codecState = GetCodecState(node);
	SafeCodecStop(GetCodec(node), codecState->fState);
	if (--codecState->fUsers == 0)
	{
		DeleteCodecNodes(node);
		SetCodecState(node, nil);
	}
	void* recordState = GetRecordState(node);
	if (recordState != nil)
		delete (RecordState*) recordState;
	long result = TSoundChannel::FreeNode(node, codecState->fError, codecState->fState);
	if (codecState->fUsers == 0)
		delete codecState;
	return result;
}


// ROM 0x001e45b4 Cancel__13TCodecChannelFP18TUSoundNodeRequest
// (A decompressor's nodes are not cancelled.)
long
TCodecChannel::Cancel(TUSoundNodeRequest* /*request*/)
{
	return noErr;
}


// ROM 0x001e45bc GetCodecState__13TCodecChannelFP11ChannelNode
// A decompressor's (or an output channel's) state is the node's own; any
// other kind of channel has one for all its nodes.
CodecState*
TCodecChannel::GetCodecState(ChannelNode* node)
{
	if ((fFlags & kSndChannelOutput) != 0 || (fFlags & kSndChannelDecompressor) != 0)
		return (node != nil) ? (CodecState*) node->fCodecState : nil;
	return fCodecState;
}


// ROM 0x001e45e0 SetCodecState__13TCodecChannelFP11ChannelNodeP10CodecState
void
TCodecChannel::SetCodecState(ChannelNode* node, CodecState* state)
{
	if ((fFlags & kSndChannelOutput) == 0 && (fFlags & kSndChannelDecompressor) == 0)
		fCodecState = state;
	else
		node->fCodecState = state;
}


// ROM 0x001e4634 GetCodec__13TCodecChannelFP11ChannelNode
TSoundCodec*
TCodecChannel::GetCodec(ChannelNode* node)
{
	return ((CodecState*) node->fCodecState)->fCodec;
}


// ROM 0x001e4640 SetCodec__13TCodecChannelFP11ChannelNodeP11TSoundCodec
void
TCodecChannel::SetCodec(ChannelNode* node, TSoundCodec* codec)
{
	((CodecState*) node->fCodecState)->fCodec = codec;
}


// ROM 0x001e4708 GetNodeRefCount__13TCodecChannelFP11ChannelNode
long
TCodecChannel::GetNodeRefCount(ChannelNode* node)
{
	return ((CodecState*) node->fCodecState)->fRefCount;
}


// ROM 0x001e4714 SetNodeRefCount__13TCodecChannelFP11ChannelNodel
void
TCodecChannel::SetNodeRefCount(ChannelNode* node, long count)
{
	((CodecState*) node->fCodecState)->fRefCount = count;
}


// ROM 0x001e4250 GetNextNode__13TCodecChannelFv
// The node decoded: played again if it loops (its codec reset on the
// data), else the next one set up and this one freed - which, the node
// still having buffers out, only puts it on fCodecNodes.
ChannelNode*
TCodecChannel::GetNextNode(void)
{
	ChannelNode* node = fNodes;
	if (node != nil)
	{
		if (node->fLoops < 1)
		{
			ChannelNode* next = node->fNext;
			SetupNode(next);
			FreeNode(node, noErr, 0);
			node = next;
		}
		else
		{
			node->fLoops--;
			CodecBlock block;
			ConvertCodecBlock((SoundBlock*) &node->fData, &block);
			SafeCodecReset(GetCodec(node), &block);
		}
	}
	return node;
}


// ROM 0x001e4720 Start__13TCodecChannelFP10TUMsgToken
// Started as any channel is, and the decompressing task started unless it
// is already running: an aborting one is waited out first.
long
TCodecChannel::Start(TUMsgToken* token)
{
	long err = TSoundChannel::Start(token);
	if (err == noErr)
	{
		if ((fCodecFlags & kCodecAbort) != 0)
		{
			fLock.Acquire(kWaitOnBlock);
			fLock.Release();
		}
		if (fLock.Acquire(kNoWaitOnBlock) == noErr)
		{
			TUTask task;
			fCodecFlags &= ~kCodecAbort;
			TCodecChannel* self = this;
			err = task.Init((TaskProcPtr) MainEventLoop, 2000, sizeof(self), &self, 0x0c, 'codc');
			fLock.Release();
			if (err == noErr)
				err = task.Start();
			if (err != noErr)
				fCodecFlags |= kCodecAbort;
		}
	}
	return err;
}


// ROM 0x001e480c MainEventLoop__13TCodecChannelSFPP13TCodecChannel
// The 'codc task: a decompressor decompresses, a compressor compresses,
// and the task ends.
void
TCodecChannel::MainEventLoop(TCodecChannel** channel)
{
	TCodecChannel* self = *channel;
	if ((self->fFlags & kSndChannelDecompressor) != 0)
		self->DecompressLoop();
	else if ((self->fFlags & kSndChannelCompressor) != 0)
		self->CompressLoop();
}


// ROM 0x001e45fc GetRecordState__13TCodecChannelFP11ChannelNode
// An input channel's or compressor's node keeps its RecordState where a
// decompressor's keeps its CodecState.
void*
TCodecChannel::GetRecordState(ChannelNode* node)
{
	if ((fFlags & kSndChannelInput) != 0 || (fFlags & kSndChannelCompressor) != 0)
		return node->fCodecState;
	return nil;
}


// ROM 0x001e461c SetRecordState__13TCodecChannelFP11ChannelNodeP11RecordState
void
TCodecChannel::SetRecordState(ChannelNode* node, void* state)
{
	if ((fFlags & kSndChannelInput) != 0 || (fFlags & kSndChannelCompressor) != 0)
		node->fCodecState = state;
}


// ROM 0x001e5558 CompressLoop__13TCodecChannelFv
// Recording through a codec: every buffer handed to the input channel to
// be filled, the input channel started and kept running; then, as each
// buffer comes back full, it is run through the codec into the node's
// data (EmptyDMABuffer) and handed back to be filled again.  A node the
// codec has filled moves on (GetNextNode); a Stop, or an error, ends it,
// and the input channel is stopped.
void
TCodecChannel::CompressLoop(void)
{
	UChar failed = false;
	ULong size;
	SoundBlock block;
	fLock.Acquire(kWaitOnBlock);
	fCodecFlags &= ~kCodecStarted;
	ChannelNode* node = fNodes;
	if (node != nil)
	{
		CodecState* codecState = GetCodecState(node);
		memcpy(&block, &node->fData, sizeof(block));
		for (ULong i = 0; i < codecState->fBufferCount; i++)
		{
			codecState->fIndex = i;
			if (GetNodeBuffer(node, nil) != noErr
			 || ScheduleDMA(node, codecState->fBufferSize, &block) != noErr)
			{
				failed = true;
				goto done;
			}
		}
		codecState->fIndex = 0;
		SendStart();
		fOutputChannel->fFlags |= kSndChannelKeepRunning;
		for (;;)
		{
			node = fNodes;
			if (node == nil || (fCodecFlags & kCodecAbort) != 0)
				goto done;
			if (WaitForNextBuffer(0) != noErr)
			{
				failed = true;
				goto done;
			}
			TSoundChannel* input = fOutputChannel;
			if ((input->fFlags & kSndChannelRunning) != 0 && input->fNodes != nil
			 && (input->fFlags & kSndChannelPaused) == 0)
			{
				if (EmptyDMABuffer(node, &size, &block) != noErr)
				{
					failed = true;
					goto done;
				}
				node = fNodes;
				codecState = GetCodecState(node);
			}
			if ((fCodecFlags & kCodecAbort) != 0)
				break;
			if (fNodes == nil)
				fOutputChannel->fFlags &= ~kSndChannelKeepRunning;
			else
			{
				memcpy(&block, &node->fData, sizeof(block));
				if (ScheduleDMA(node, codecState->fBufferSize, &block) != noErr)
				{
					failed = true;
					goto done;
				}
				if (++codecState->fIndex >= codecState->fBufferCount)
					codecState->fIndex = 0;
			}
			if (codecState != nil && codecState->fDone && (fCodecFlags & kCodecAbort) == 0)
			{
				codecState->fDone = false;
				GetNextNode();
			}
		}
		fStoppedNodeId = node->fNodeId;
		fStoppedPosition = node->fPosition;
		fCodecFlags |= kCodecStopped;
	}
done:
	fOutputChannel->Stop(nil, noErr);
	Abort(failed);
	fLock.Release();
	fCodecFlags = 0;
	fOutstanding = 0;
}


// ROM 0x001e57c4 EmptyDMABuffer__13TCodecChannelFP11ChannelNodePUlP10SoundBlock
// The buffer just filled by the input channel run through the codec into
// the node (fDone when the codec took less than all of it, or says the
// node is full); what the node could not take goes on into the next one.
long
TCodecChannel::EmptyDMABuffer(ChannelNode* node, ULong* size, SoundBlock* block)
{
	CodecState* codecState = GetCodecState(node);
	TSoundCodec* codec = GetCodec(node);
	ULong coded = 0;
	void* buffer;
	CodecBlock codecBlock;
	long err = GetNodeBuffer(node, &buffer);
	if (err != noErr)
		return err;
	*size = codecState->fBufferSize;
	ConvertCodecBlock(block, &codecBlock);
	err = SafeCodecConsume(codec, buffer, size, &coded, &codecBlock);
	if (err != noErr)
		return err;
	node->fPosition += coded;
	codecState->fDone = (*size < codecState->fBufferSize || codec->BufferCompleted());
	if (*size < codecState->fBufferSize && (node = GetNextNode()) != nil)
	{
		CodecState* nextState = GetCodecState(node);
		codec = GetCodec(node);
		ULong rest = nextState->fBufferSize - *size;
		buffer = (char*) buffer + *size;
		err = SafeCodecConsume(codec, buffer, &rest, &coded, &codecBlock);
		if (err != noErr)
			return err;
		node->fPosition += coded;
		ULong taken = *size;
		*size = taken + rest;
		nextState->fDone = (taken + rest < nextState->fBufferSize || codec->BufferCompleted());
	}
	if (*size != 0)
		ConvertCodecBlock(&codecBlock, block);
	return noErr;
}


// ROM 0x001e4cd0 DecompressLoop__13TCodecChannelFv
// Under the channel's lock: fill a buffer from the codec and schedule it
// on the output channel, until bufferCount of them are out; then start the
// output channel, and from then on wait for a buffer to come back (a
// paused channel waits for as long as it takes) before filling the next.
// A node the codec has finished moves on (GetNextNode).  An error, or a
// Stop (kCodecAbort), ends it: where it had got to is kept for the
// Stop's reply, and Abort gives everything back.
void
TCodecChannel::DecompressLoop(void)
{
	UChar failed = false;
	CodecState* codecState = nil;
	ULong size;
	SoundBlock block;
	fLock.Acquire(kWaitOnBlock);
	fCodecFlags &= ~kCodecStarted;
	for (;;)
	{
		ChannelNode* node = fNodes;
		if ((node == nil && fOutstanding == 0) || (fCodecFlags & kCodecAbort) != 0)
			break;
		codecState = GetCodecState(node);
		if (fNodes != nil && (fCodecFlags & kCodecAbort) == 0)
		{
			memcpy(&block, &node->fData, sizeof(block));
			if (FillDMABuffer(node, &size, &block) != noErr)
			{
				failed = true;
				break;
			}
			node = fNodes;
			codecState = GetCodecState(node);
			if (size != 0 && (fCodecFlags & kCodecAbort) == 0)
			{
				if (ScheduleDMA(node, size, &block) != noErr)
				{
					failed = true;
					break;
				}
				if (++codecState->fIndex >= codecState->fBufferCount)
					codecState->fIndex = 0;
			}
		}
		if ((fCodecFlags & kCodecStarted) == 0
		 && (node == nil || codecState->fBufferCount <= fOutstanding)
		 && (fCodecFlags & kCodecAbort) == 0)
			SendStart();
		if ((fCodecFlags & kCodecStarted) != 0 && (fCodecFlags & kCodecAbort) == 0)
		{
			if (fOutstanding == 0)
				break;
			long err;
			do
			{
				ULong timeout;
				GetBufferTimeout(&timeout);
				err = WaitForNextBuffer(timeout);
			} while ((fFlags & kSndChannelPaused) != 0 && err == kError_Message_Timed_Out);
			if (err != noErr)
			{
				failed = true;
				break;
			}
		}
		if (codecState != nil && codecState->fDone)
			GetNextNode();
	}
	if ((fCodecFlags & kCodecAbort) != 0 && fNodes != nil)
	{
		fStoppedNodeId = fNodes->fNodeId;
		fStoppedPosition = fNodes->fPosition;
		fCodecFlags |= kCodecStopped;
	}
	Abort(failed);
	fLock.Release();
	fCodecFlags = 0;
	fOutstanding = 0;
}


// ROM 0x001e4828 Pause__13TCodecChannelFP16TUSoundNodeReply
// Paused, or going again; the reply says where the node being decoded is.
long
TCodecChannel::Pause(TUSoundNodeReply* reply)
{
	long resumed = 0;
	if (reply != nil)
	{
		if (fNodes == nil)
		{
			reply->fNodeId = 0;
			reply->fState = 2;
			reply->fPosition = 0;
		}
		else
		{
			reply->fNodeId = fNodes->fNodeId;
			reply->fState = 2;
			reply->fPosition = fNodes->fPosition;
		}
	}
	if ((fFlags & kSndChannelRunning) != 0)
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


// ROM 0x001e4898 Stop__13TCodecChannelFP16TUSoundNodeReplyl
// The task told to stop, the output channel it feeds stopped (which gives
// back the buffers it holds, and so wakes the task), and the task waited
// out; the reply says where the decoding had got to.
void
TCodecChannel::Stop(TUSoundNodeReply* reply, long /*error*/)
{
	if ((fCodecFlags & kCodecAbort) == 0)
	{
		fCodecFlags |= kCodecAbort;
		fOutputChannel->Stop(reply, noErr);		// (the ROM passes the reply and whatever r2 held)
		fLock.Acquire(kWaitOnBlock);
		fCodecFlags &= ~kCodecAbort;
		fLock.Release();
		if (reply != nil)
		{
			if ((fCodecFlags & kCodecStopped) == 0)
			{
				reply->fNodeId = 0;
				reply->fState = 1;
				reply->fPosition = 0;
			}
			else
			{
				reply->fNodeId = fStoppedNodeId;
				reply->fState = 1;
				reply->fPosition = fStoppedPosition;
			}
		}
	}
	fFlags &= ~(kSndChannelRunning | kSndChannelPaused);
}


// ROM 0x001e494c InitNode__13TCodecChannelFP11ChannelNode
// A node's codec state (made the first time), its codec reset on the
// node's data and started; the first user of a state sets up its buffers:
// the frame's bufferSize, and bufferCount held to 2..8.
NewtonErr
TCodecChannel::InitNode(ChannelNode* node)
{
	NewtonErr err = noErr;
	if (node == nil)
		return noErr;
	CodecState* codecState = GetCodecState(node);
	if (codecState == nil)
	{
		codecState = new CodecState;
		if (codecState == nil)
			return MemError();
		SetCodecState(node, codecState);
		codecState->fUsers = 0;
		codecState->fBufferCount = 0;
		codecState->fIndex = 0;
		codecState->fDone = false;
	}
	if ((fFlags & kSndChannelInput) != 0 || (fFlags & kSndChannelCompressor) != 0)
	{
		RecordState* recordState = new RecordState;
		if (recordState == nil)
			return MemError();
		SetRecordState(node, recordState);
	}
	SetNodeRefCount(node, 0);
	codecState->fUsers++;
	TSoundCodec* codec = (TSoundCodec*) node->fCodec;
	SetCodec(node, codec);
	if (codec != nil)
	{
		if (codecState->fUsers == 1)
		{
			codecState->fBufferSize = node->fUnknown3C;
			ULong count = node->fUnknown40;
			if (count < 2)
				count = 2;
			else if (count >= 8)
				count = 8;
			codecState->fBufferCount = count;
			codecState->fUnknown3C = 0;
			InitCodecNodes(node);
		}
		CodecBlock block;
		ConvertCodecBlock((SoundBlock*) &node->fData, &block);
		err = SafeCodecReset(codec, &block);
		if (err == noErr)
			err = SafeCodecStart(codec);
	}
	return err;
}


// ROM 0x001e4ab4 InitCodecNodes__13TCodecChannelFP11ChannelNode
// A request for each buffer, addressed to the output channel and carrying
// the node's block (its data filled in by GetNodeBuffer).
long
TCodecChannel::InitCodecNodes(ChannelNode* node)
{
	CodecState* codecState = GetCodecState(node);
	for (long i = 0; i < 8; i++)
		codecState->fNodes[i] = nil;
	for (ULong i = 0; i < codecState->fBufferCount; i++)
	{
		CodecNode* codecNode = new CodecNode;
		if (codecNode != nil)
		{
			memset(&codecNode->fRequest, 0, sizeof(codecNode->fRequest));
			memset(&codecNode->fReply, 0, sizeof(codecNode->fReply));
			codecNode->fRequest.fBlock.fSampleRate = 0x560a6e85;
			codecNode->fRequest.fBlock.fVolume = 0x7fffffff;
			codecNode->fRequest.fChannelVolume = 0x7fffffff;
			codecNode->fReply.fEvent.fAEventClass = kNewtEventClass;
			codecNode->fReply.fEvent.fAEventID = 'usnd';
		}
		codecState->fNodes[i] = codecNode;
		codecNode->fRequest.fEvent.fAEventClass = kNewtEventClass;
		codecNode->fRequest.fEvent.fAEventID = 'usnd';
		codecNode->fRequest.fCommand = kSndSchedule;
		codecNode->fRequest.fChannel = fOutputChannelId;
		memcpy(&codecNode->fRequest.fBlock, &node->fData, sizeof(SoundBlock));
		codecNode->fRequest.fChannelVolume = node->fChannelVolume;
		codecNode->fRequest.fBlock.fData = nil;
		codecNode->fRequest.fNodeId = node->fNodeId;
		codecNode->fIndex = i;
	}
	return noErr;
}


// ROM 0x001e4c40 DeleteCodecNodes__13TCodecChannelFP11ChannelNode
long
TCodecChannel::DeleteCodecNodes(ChannelNode* node)
{
	long err = noErr;
	CodecState* codecState = GetCodecState(node);
	for (ULong i = 0; i < codecState->fBufferCount; i++)
	{
		CodecNode* codecNode = codecState->fNodes[i];
		if (codecNode->fRequest.fBlock.fData != nil)
		{
			free(codecNode->fRequest.fBlock.fData);
			codecNode->fRequest.fBlock.fData = nil;
			err = codecNode->fMessage.Abort();
		}
		if (codecState->fNodes[i] != nil)
			delete codecState->fNodes[i];
		codecState->fNodes[i] = nil;
	}
	return err;
}


// ROM 0x001e4f60 GetNodeBuffer__13TCodecChannelFP11ChannelNodePPv
// The buffer whose turn it is, made the first time with the message it is
// sent in, collected on this channel's port.
NewtonErr
TCodecChannel::GetNodeBuffer(ChannelNode* node, void** buffer)
{
	NewtonErr err = noErr;
	CodecState* codecState = GetCodecState(node);
	CodecNode* codecNode = codecState->fNodes[codecState->fIndex];
	void* data = codecNode->fRequest.fBlock.fData;
	if (data == nil)
	{
		data = malloc(codecState->fBufferSize);
		if (data == nil)
			err = MemError();
		else
		{
			codecNode->fRequest.fBlock.fData = data;
			err = codecNode->fMessage.Init(true);
			if (err == noErr)
				err = codecNode->fMessage.SetCollectorPort(fPort);
		}
	}
	if (buffer != nil)
		*buffer = data;
	return err;
}


// ROM 0x001e4fe8 FillDMABuffer__13TCodecChannelFP11ChannelNodePUlP10SoundBlock
// A buffer's worth out of the codec (fDone when it came short or the
// codec says it has finished); nothing at all moves on to the next node
// and tries that.  The node's position counts the coded bytes used, and
// the block becomes what the codec produced.
long
TCodecChannel::FillDMABuffer(ChannelNode* node, ULong* size, SoundBlock* block)
{
	CodecState* codecState = GetCodecState(node);
	TSoundCodec* codec = GetCodec(node);
	ULong coded = 0;
	void* buffer;
	CodecBlock codecBlock;
	long err = GetNodeBuffer(node, &buffer);
	if (err != noErr)
		return err;
	*size = codecState->fBufferSize;
	ConvertCodecBlock(block, &codecBlock);
	err = SafeCodecProduce(codec, buffer, size, &coded, &codecBlock);
	if (err != noErr)
		return err;
	codecState->fDone = (*size < codecState->fBufferSize || codec->BufferCompleted());
	if (*size == 0 && (node = GetNextNode()) != nil)
	{
		err = GetNodeBuffer(node, &buffer);
		if (err != noErr)
			return err;
		CodecState* nextState = GetCodecState(node);
		codec = GetCodec(node);
		*size = nextState->fBufferSize;
		err = SafeCodecProduce(codec, buffer, size, &coded, &codecBlock);
		if (err != noErr)
			return err;
		nextState->fDone = (*size < nextState->fBufferSize || codec->BufferCompleted());
	}
	if (*size != 0)
	{
		node->fPosition += coded;
		ConvertCodecBlock(&codecBlock, block);
	}
	return noErr;
}


// ROM 0x001e5190 GetBufferTimeout__13TCodecChannelFPUl
// How long to wait for a buffer to come back: as long as the first
// buffer takes to play, and two seconds.
void
TCodecChannel::GetBufferTimeout(ULong* timeout)
{
	*timeout = 0;
	ChannelNode* node = fCodecNodes;
	if (node == nil)
		node = fNodes;
	if (node != nil)
	{
		CodecState* codecState = GetCodecState(node);
		CodecNode* first = codecState->fNodes[0];
		ULong perMillisecond = (ULong) first->fRequest.fBlock.fSampleRate / 1000;
		*timeout = ((ULong) first->fRequest.fBlock.fPlayCount / (perMillisecond >> 16)) * kMilliseconds;
	}
	*timeout += 0x707ce0;			// (7372000 ticks: two seconds)
}


// ROM 0x001e5208 WaitForNextBuffer__13TCodecChannelFUl
// A buffer's reply received on the channel's port (nothing to wait for:
// at once); a buffer played is one fewer out, and the node waiting at the
// head (or the one being decoded) let go when it was its last.
long
TCodecChannel::WaitForNextBuffer(ULong timeout)
{
	TUSoundNodeReply reply;
	memset(&reply, 0, sizeof(reply));
	reply.fEvent.fAEventClass = kNewtEventClass;
	reply.fEvent.fAEventID = 'usnd';
	if (fOutstanding == 0)
		return noErr;
	ULong size;
	ULong msgType = 0;
	TUMsgToken token;
	long err = fPort.Receive(&size, &reply, sizeof(reply), &token, &msgType, timeout, kMsgType_MatchAll, false, false);
	// (the ROM sizes a shared-memory reply here and drops the answer)
	if (err != noErr)
		return err;
	if (reply.fError == noErr)
	{
		ChannelNode* node = fCodecNodes;
		if (node == nil)
			node = fNodes;
		if (node != nil)
		{
			fOutstanding--;
			long count = GetNodeRefCount(node) - 1;
			SetNodeRefCount(node, count);
			if (count == 0)
				ReleaseNode(node);
		}
	}
	return reply.fError;
}


// ROM 0x001e5380 ScheduleDMA__13TCodecChannelFP11ChannelNodeiP10SoundBlock
// The buffer just filled scheduled on the output channel, asynchronously,
// as samples of the block's (the codec's output) kind.
long
TCodecChannel::ScheduleDMA(ChannelNode* node, long size, SoundBlock* block)
{
	CodecState* codecState = GetCodecState(node);
	CodecNode* codecNode = codecState->fNodes[codecState->fIndex];
	codecNode->fRequest.fBlock.fSampleRate = block->fSampleRate;
	codecNode->fRequest.fBlock.fFormat = block->fFormat;
	codecNode->fRequest.fBlock.fSampleBits = block->fSampleBits;
	codecNode->fRequest.fBlock.fPlayCount = size / (block->fSampleBits / 8);
	TUPort port(gSndPort);
	long err = port.SendRPC(&codecNode->fMessage, &codecNode->fRequest, sizeof(codecNode->fRequest),
							&codecNode->fReply, sizeof(codecNode->fReply));
	if (err == noErr)
	{
		fOutstanding++;
		SetNodeRefCount(node, GetNodeRefCount(node) + 1);
	}
	return err;
}


// ROM 0x001e5478 SendStart__13TCodecChannelFv
// The output channel started (the server's command 9), once.
void
TCodecChannel::SendStart(void)
{
	if ((fCodecFlags & kCodecStarted) != 0)
		return;
	TUSoundNodeRequest request;
	memset(&request, 0, sizeof(request));
	request.fEvent.fAEventClass = kNewtEventClass;
	request.fEvent.fAEventID = 'usnd';
	request.fChannel = fOutputChannelId;
	request.fCommand = kSndStart;
	TUSoundNodeReply reply;
	memset(&reply, 0, sizeof(reply));
	reply.fEvent.fAEventClass = kNewtEventClass;
	reply.fEvent.fAEventID = 'usnd';
	TUPort port(gSndPort);
	ULong replySize;
	port.SendRPC(&replySize, &request, offsetof(TUSoundNodeRequest, fBlock), &reply, 0x14);
	fCodecFlags |= kCodecStarted;
}


// ROM 0x001e5990 Abort__13TCodecChannelFUc
// The end of the decoding: after an error the output channel is stopped
// first; every buffer still out waited for; then every node answered
// (-30000, stopped) and the ones waiting let go.
void
TCodecChannel::Abort(UChar stopOutput)
{
	long err = noErr;
	if (stopOutput)
	{
		TUSoundNodeRequest request;
		memset(&request, 0, sizeof(request));
		request.fEvent.fAEventClass = kNewtEventClass;
		request.fEvent.fAEventID = 'usnd';
		request.fBlock.fSampleRate = 0x560a6e85;
		request.fBlock.fVolume = 0x7fffffff;
		request.fChannelVolume = 0x7fffffff;
		request.fCommand = kSndStop;
		request.fChannel = fOutputChannelId;
		TUSoundNodeReply reply;
		memset(&reply, 0, sizeof(reply));
		reply.fEvent.fAEventClass = kNewtEventClass;
		reply.fEvent.fAEventID = 'usnd';
		TUPort port(gSndPort);
		ULong replySize;
		port.SendRPC(&replySize, &request, sizeof(request), &reply, sizeof(reply));
		err = reply.fError;
	}
	while (err == noErr && fOutstanding != 0)
		err = WaitForNextBuffer(0);
	ChannelNode* node;
	while ((node = fNodes) != nil)
	{
		fNodes = node->fNext;
		CodecState* codecState = GetCodecState(node);
		if (codecState != nil)
			for (ULong i = 0; i < codecState->fBufferCount; i++)
				codecState->fNodes[i]->fMessage.Abort();
		FreeNode(node, ERRBASE_SOUND, 1);
	}
	node = fCodecNodes;
	while (node != nil)
	{
		ChannelNode* next = node->fNext;
		ReleaseNode(node);
		node = next;
	}
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
		long count = Swap((ULong*) &server->fInputIntMessage->fEvent.fCount, 0);
		ULong which = server->EmptyDMABuffer(count);
		if (gSndDriver->InputIsRunning() && server->fInputSize[which] == 0)
		{
			gSndDriver->StopInput();
			gSndDriver->PowerInputOff();
			server->fInputIntMessage->fMessage.Abort();
			server->fInputIntMessage->fEvent.fCount = 0;
			return;
		}
		gSndDriver->ScheduleInputBuffer(which, kDMABufferSize);
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
	ULong value = request->fNodeId;						// (the event's +0x10: an immediate command's value)
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
	case kSndOpenInput:
		reply.fError = server->OpenInputChannel(&reply.fChannel, value);
		break;
	case kSndSetInputGain:
		reply.fError = server->SetInputVolume(value);
		break;
	case kSndSetInputDevice:
		reply.fError = server->SetInputDevice(request->fChannel, value);
		break;
	case kSndOpenCompressor:
		reply.fError = server->OpenCompressorChannel(&reply.fChannel, value);
		break;
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
	fInputIndex = 0;
	fInputSkip = false;
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


// DEVIATION: the ROM's DMA buffers are NewWiredPtr blocks - memory whose
// pages stay put for the DMA controller - and the reconstruction's
// NewWiredPtr is NOT YET (memory/MemoryManager.cpp answers nil); the host
// driver reads the buffers with the CPU, so ordinary blocks serve.
static Ptr
NewDMABuffer(void)
{
	Ptr p = NewWiredPtr(kDMABufferSize);
	if (p == nil)
		p = NewPtr(kDMABufferSize);
	return p;
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
	fDMABuffer[0] = NewDMABuffer();
	if (fDMABuffer[0] == nil)
		return MemError();
	fDMABuffer[1] = NewDMABuffer();
	if (fDMABuffer[1] == nil)
		return MemError();
	gSndDriver->SetOutputBuffers((VAddr) fDMABuffer[0], kDMABufferSize, (VAddr) fDMABuffer[1], kDMABufferSize);
	gSndDriver->SetOutputCallbackProc(SoundOutputIH, this);
	if (gSndDriver->ClassInfo()->GetCapability("SoundInput") != nil)
	{
		fInputBuffer[0] = NewDMABuffer();
		if (fInputBuffer[0] == nil)
			return MemError();
		fInputBuffer[1] = NewDMABuffer();
		if (fInputBuffer[1] == nil)
			return MemError();
		gSndDriver->SetInputBuffers((VAddr) fInputBuffer[0], kDMABufferSize, (VAddr) fInputBuffer[1], kDMABufferSize);
	}
	gSndDriver->SetInputCallbackProc(SoundInputIH, this);
	gSndDriver->PowerOutputOff();
	gSndDriver->PowerInputOff();
	err = fHandler.Init(this);
	if (err != noErr)
		return err;
	err = fPowerHandler.Init(this);
	if (err != noErr)
		return err;
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
				StartInput(channel->fDevice);
			else if ((kind & kSndChannelDecompressor) != 0)
				StartDecompressor(channel->fDevice);
			else if ((kind & kSndChannelCompressor) != 0)
				StartCompressor(channel->fDevice);
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
	if (((channel->fFlags & kSndChannelInput) != 0 || (channel->fFlags & kSndChannelCompressor) != 0)
	 && AllInputChannelsEmpty())
	{
		gSndDriver->StopInput();
		gSndDriver->PowerInputOff();
	}
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
	StopCompressor(1);
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


/*------------------------------------------------------------------------------
	T S o u n d P o w e r H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x001e996c __ct__18TSoundPowerHandlerFv
TSoundPowerHandler::TSoundPowerHandler()
{
	fServer = nil;
}


// ROM 0x001e99b4 Init__18TSoundPowerHandlerFP12TSoundServer
// Registered for the power-off system event ('powf).
NewtonErr
TSoundPowerHandler::Init(TSoundServer* server)
{
	fServer = server;
	return TSystemEventHandler::Init('powf');
}


// ROM 0x001e99c8 PowerOff__18TSoundPowerHandlerFP7TAEvent
// The machine going off: every channel stopped, and output and input
// stopped and powered down.
void
TSoundPowerHandler::PowerOff(TAEvent* /*event*/)
{
	TSoundServer* server = fServer;
	server->StopCompressor(1);
	server->StopDecompressor(1);
	server->StopOutput(1);
	for (TSoundChannel* channel = server->fInputChannels; channel != nil; channel = channel->fNext)
		channel->Stop(nil, kSndErrStopped);
	gSndDriver->StopInput();
	gSndDriver->PowerInputOff();
}


// ROM 0x001e7380 MixLin16__FPvT1l
// One buffer of 16-bit samples added into another, clamped to a short.
// (The samples big-endian, as everywhere in memory: SampleWords.h.)
void
MixLin16(void* dst, void* src, long count)
{
	UByte* d = (UByte*) dst;
	const UByte* s = (const UByte*) src;
	for (long i = 0; i < count; i++)
	{
		long sum = (long) GetSampleAt(s, i) + (long) GetSampleAt(d, i);
		if (sum > 0x7fff)
			sum = 0x7fff;
		else if (sum < -0x8000)
			sum = -0x8000;
		PutSampleAt(d, i, (short) sum);
	}
}


/*------------------------------------------------------------------------------
	T S o u n d S e r v e r   -   i n p u t
	The mirror of output: the driver fills the two input buffers in turn,
	its interrupt (SoundInputIH) says one is full, and EmptyDMABuffer hands
	it to every running input channel (TDMAChannel::Consume into their
	nodes).  A compressor sits on an input channel as a decompressor sits
	on an output one.
------------------------------------------------------------------------------*/

// ROM 0x001e91fc OpenInputChannel__12TSoundServerFPUlUl
NewtonErr
TSoundServer::OpenInputChannel(ULong* id, ULong device)
{
	if (gSndDriver->ClassInfo()->GetCapability("SoundInput") == nil)
		return kSndErrBadFormat;
	*id = 0;
	TSoundDriverInfo info;
	info.fUnknown00 = 1;
	gSndDriver->GetSoundHardwareInfo(&info);
	TDMAChannel* channel = new TDMAChannel(UniqueId(), info);
	if (channel == nil)
		return MemError();
	channel->fFlags |= kSndChannelInput;
	channel->fNext = fInputChannels;
	channel->fDevice = device;
	fInputChannels = channel;
	*id = channel->fId;
	return noErr;
}


// ROM 0x001e92c8 AllInputChannelsEmpty__12TSoundServerFv
Boolean
TSoundServer::AllInputChannelsEmpty(void)
{
	for (TSoundChannel* channel = fInputChannels; channel != nil; channel = channel->fNext)
		if (channel->IsActive())
			return false;
	return true;
}


// ROM 0x001e9308 StartInput__12TSoundServerFi
// When the hardware is not already recording and some channel wants
// sound: the buffer whose turn it is given to the driver, the input
// powered and started, and the other buffer queued behind it.
void
TSoundServer::StartInput(long device)
{
	if (gSndDriver->InputIsRunning())
		return;
	if (AllInputChannelsEmpty())
		return;
	fInputIntMessage->fMessage.Abort();
	fInputIntMessage->fEvent.fCount = 0;
	fInputSkip = false;
	gSndDriver->ScheduleInputBuffer(fInputIndex, kDMABufferSize);
	gSndDriver->PowerInputOn(device);
	if (!gSndDriver->InputIsEnabled())
		gSndDriver->StartInput();
	gSndDriver->ScheduleInputBuffer(1 - fInputIndex, kDMABufferSize);
}


// ROM 0x001e93a4 StopInput__12TSoundServerFi
void
TSoundServer::StopInput(long hardware)
{
	for (TSoundChannel* channel = fInputChannels; channel != nil; channel = channel->fNext)
		channel->Stop(nil, kSndErrStopped);
	if (hardware != 0)
	{
		gSndDriver->StopInput();
		gSndDriver->PowerInputOff();
	}
}


// ROM 0x001e9410 ScheduleInputBuffer__12TSoundServerFi
// A full buffer emptied and given back to the driver - unless nobody
// wanted it, when the input is turned off.
void
TSoundServer::ScheduleInputBuffer(long count)
{
	ULong which = EmptyDMABuffer(count);
	if (gSndDriver->InputIsRunning() && fInputSize[which] == 0)
	{
		gSndDriver->StopInput();
		gSndDriver->PowerInputOff();
		fInputIntMessage->fMessage.Abort();
		fInputIntMessage->fEvent.fCount = 0;
		return;
	}
	gSndDriver->ScheduleInputBuffer(which, kDMABufferSize);
}


// ROM 0x001e948c EmptyDMABuffer__12TSoundServerFi
// The buffer whose turn it is handed to every running, unpaused input
// channel; its size says whether anyone took any (or a channel is kept
// running).  count is how many buffers the interrupts said were full: more
// than one and the next is thrown away, the machine having fallen behind.
// ==> the buffer emptied.
ULong
TSoundServer::EmptyDMABuffer(long count)
{
	long samples = (((TDMAChannel*) fInputChannels)->fHardwareBits == 16) ? 0x750 : 0xea0;
	Boolean taken = false;
	long which = fInputIndex;
	fInputSize[which] = samples;
	void* buffer = fInputBuffer[which];
	for (TSoundChannel* c = fInputChannels; c != nil; c = c->fNext)
	{
		if ((c->fFlags & kSndChannelRunning) != 0 && c->fNodes != nil
		 && (c->fFlags & kSndChannelPaused) == 0 && !fInputSkip)
		{
			long n = samples;
			((TDMAChannel*) c)->Consume(buffer, &n);
			if (n > 0)
				taken = true;
		}
		if ((c->fFlags & kSndChannelKeepRunning) != 0)
			taken = true;
	}
	if (!taken)
		fInputSize[which] = 0;
	fInputSkip = count > 1;
	fInputIndex = 1 - fInputIndex;
	return which;
}


// ROM 0x001e9610 SetInputVolume__12TSoundServerFl
long
TSoundServer::SetInputVolume(long gain)
{
	if (gSndDriver->ClassInfo()->GetCapability("SoundInput") == nil)
		return kSndErrBadFormat;
	if (gSndDriver->InputVolume() != gain)
		gSndDriver->InputVolume(gain);
	return noErr;
}


// ROM 0x001e9674 SetInputDevice__12TSoundServerFUll
// ROM BUG kept: the channel is not looked for before it is written to.
long
TSoundServer::SetInputDevice(ULong id, long device)
{
	FindChannel(id)->fDevice = device;
	return noErr;
}


// ROM 0x001e97dc OpenCompressorChannel__12TSoundServerFPUlUl
// A codec channel fed by the input channel given, at the front of the
// compressors.
NewtonErr
TSoundServer::OpenCompressorChannel(ULong* id, ULong inputId)
{
	if (gSndDriver->ClassInfo()->GetCapability("SoundInput") == nil)
		return kSndErrBadFormat;
	*id = 0;
	TSoundDriverInfo info;
	info.fUnknown00 = 1;
	gSndDriver->GetSoundHardwareInfo(&info);
	TCodecChannel* channel = new TCodecChannel(UniqueId(), info);
	if (channel == nil)
		return MemError();
	channel->fFlags |= kSndChannelCompressor;
	channel->fOutputChannelId = inputId;
	channel->fNext = fCompressorChannels;
	channel->fOutputChannel = FindChannel(inputId);
	fCompressorChannels = channel;
	*id = channel->fId;
	return noErr;
}


// ROM 0x001e98b8 StartCompressor__12TSoundServerFi
void
TSoundServer::StartCompressor(long /*device*/)
{ }


// ROM 0x001e98bc SoundInputIH__12TSoundServerFv
// The driver's input interrupt: one more buffer full, and the server told.
long
TSoundServer::SoundInputIH(void* refCon)
{
	TSoundServer* server = (TSoundServer*) refCon;
	server->fInputIntMessage->fEvent.fCount++;
	return SendForInterrupt(gSndPort, server->fInputIntMessage->fMessage.GetMsgId(), 0,
							&server->fInputIntMessage->fEvent, sizeof(TSoundIntEvent), 0x04000000, 0, nil, false);
}


// ROM 0x001e9924 StopCompressor__12TSoundServerFi
void
TSoundServer::StopCompressor(long /*hardware*/)
{
	for (TSoundChannel* channel = fCompressorChannels; channel != nil; channel = channel->fNext)
		channel->Stop(nil, kSndErrStopped);
}
