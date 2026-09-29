/*
	File:		sound/SoundChannel.cpp

	Contains:	TUSoundChannel's settings and the global channel
				(SoundChannel.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SoundChannel.h"
#include "SoundServer.h"
#include <stddef.h>
#include "NewtonGestalt.h"
#include "UserGlobals.h"
#include "OSErrors.h"
#include "NewtonMemory.h"

#include <string.h>


TObjectId		gSndPort = 0;			// ROM 0x0c101cec
TUSoundChannel*	gSoundChannel = nil;	// ROM 0x0c101ce4


// ROM 0x0025a7a4 __ct__14TUSoundChannelFv
// Nothing scheduled, no channel open, the volume "never set"
// (0x7fffffff) and the input gain at 0x80; the gestalt says whether the
// server can be asked for the volume, which is what GetVolume goes on.
TUSoundChannel::TUSoundChannel()
{
	fFlags = 0;
	fChannelId = 0;
	fCodecChannelId = 0;
	fLastNodeId = 0;
	fBusyNodes = nil;
	fFreeNodes = nil;
	fVolume = 0x7fffffff;
	fInputGain = 0x80;
	fUnknown34 = 0;
	fOutputDevice = 0;

	TGestaltVolumeInfo info;
	memset(&info, 0, sizeof(info));
	// DEVIATION: the gestalt is a monitor call, and answering it wants a
	// shared-memory object from the object manager - so the OS has to be
	// running.  On the Newton it always is by the time a sound channel is
	// made; a host program that runs user-side code without booting
	// (build/host/host/newtonscript) has no object manager, and with no
	// sound server the answer would be false in any case.
	if (gUObjectMgrMonitor != nil)
	{
		TUGestalt gestalt;
		gestalt.Gestalt(kGestalt_Ext_VolumeInfo, &info, sizeof(info));
	}
	if (info.fServerAnswersVolume)
		fFlags |= kSoundChannelAskForVolume;
}


// Host: the volume information the ROM's sound driver would have
// registered (see SoundChannel.h).  Called from InitializeSound, which is
// where the ROM registers its driver.
void
RegisterHostVolumeInfo(void)
{
	// the name server keeps the block's *address*, so it has to outlive
	// this call - the ROM's is a field of the sound driver
	static TGestaltVolumeInfo info;
	memset(&info, 0, sizeof(info));
	info.fServerAnswersVolume = false;			// there is no sound server
	info.fHighestSetting = 4;					// five settings, 0 to 4
	// the decibels between the quietest audible setting and full, which is
	// what VolumeToDecibels(1) says: -18.0618 dB in 16.16
	double range = -(double) 0x00121FD2 / 65536.0;
	memcpy(info.fDecibelRange, &range, sizeof(info.fDecibelRange));
	TUGestalt gestalt;
	gestalt.RegisterGestalt(kGestalt_Ext_VolumeInfo, &info, sizeof(info));
}


// ROM 0x0025b0fc SendImmediate__14TUSoundChannelFUlN21P12TUSoundReplyT1
// One 'newt/'usnd event carrying {channel, command, value}, sent to the
// sound server's port and answered on the spot.
//
// DEVIATION: with no sound server (a host with no sound driver, or one
// running user-side code without booting - the NewtonScript boot reaches
// here before there is an OS to ask) the ROM would send to port 0 and let
// the kernel refuse it; the host refuses it here.  Every caller falls
// back on the value the channel keeps itself, which is why the volume a
// script sets is the volume it reads back.
NewtonErr
TUSoundChannel::SendImmediate(ULong command, ULong channelId, ULong value,
							  TUSoundReply* reply, ULong replySize)
{
	if (gSndPort == 0)
		return kError_Bad_ObjectId;
	TUSoundNodeRequest request;
	request.fEvent.fAEventClass = kNewtEventClass;
	request.fEvent.fAEventID = kSoundEventId;
	request.fChannel = channelId;
	request.fCommand = command;
	request.fNodeId = value;
	TUPort port(gSndPort);
	ULong size = 0;
	return port.SendRPC(&size, &request, offsetof(TUSoundNodeRequest, fBlock), reply, replySize);
}


// ROM 0x0025ad6c SetVolume__14TUSoundChannelFl
// The volume in decibels, 16.16 fixed.  The channel keeps it whatever
// happens; an open channel - or the global one, which the sound functions
// all go through - also tells the server, and answers what the server made
// of it.
long
TUSoundChannel::SetVolume(long decibels)
{
	fVolume = decibels;
	if (fBusyNodes != nil || this == gSoundChannel)
	{
		TUSoundReply reply;
		reply.fEvent.fAEventClass = kNewtEventClass;
		reply.fEvent.fAEventID = kSoundEventId;
		reply.fChannel = 0;
		reply.fError = 0;
		reply.fValue = 0;
		if (SendImmediate(kSoundSetVolume, 0, decibels, &reply, sizeof(reply)) == noErr)
			decibels = reply.fValue;
	}
	return decibels;
}


// ROM 0x0025ae14 GetVolume__14TUSoundChannelFv
// The server is only asked when the gestalt said it would answer.
long
TUSoundChannel::GetVolume(void)
{
	long decibels = fVolume;
	if ((fBusyNodes != nil || this == gSoundChannel) && (fFlags & kSoundChannelAskForVolume) != 0)
	{
		TUSoundReply reply;
		reply.fEvent.fAEventClass = kNewtEventClass;
		reply.fEvent.fAEventID = kSoundEventId;
		reply.fChannel = 0;
		reply.fError = 0;
		reply.fValue = 0;
		if (SendImmediate(kSoundGetVolume, 0, 0, &reply, sizeof(reply)) == noErr)
			decibels = reply.fValue;
	}
	return decibels;
}


// ROM 0x0025aec0 SetInputGain__14TUSoundChannelFl
void
TUSoundChannel::SetInputGain(long gain)
{
	fInputGain = gain;
	if (fChannelId == 0)
		return;
	TUSoundReply reply;
	reply.fEvent.fAEventClass = kNewtEventClass;
	reply.fEvent.fAEventID = kSoundEventId;
	reply.fChannel = 0;
	reply.fError = 0;
	reply.fValue = 0;
	SendImmediate(kSoundSetInputGain, fChannelId, gain, &reply, sizeof(reply));
}


// ROM 0x0025af3c GetInputGain__14TUSoundChannelFv
// The channel's own; the server is never asked.
long
TUSoundChannel::GetInputGain(void)
{
	return fInputGain;
}


// ROM 0x0025b080 SetOutputDevice__14TUSoundChannelFl
void
TUSoundChannel::SetOutputDevice(long device)
{
	fOutputDevice = device;
	if (fChannelId == 0)
		return;
	TUSoundReply reply;
	reply.fEvent.fAEventClass = kNewtEventClass;
	reply.fEvent.fAEventID = kSoundEventId;
	reply.fChannel = 0;
	reply.fError = 0;
	reply.fValue = 0;
	SendImmediate(kSoundSetOutputDevice, fChannelId, device, &reply, sizeof(reply));
}


/*------------------------------------------------------------------------------
	S o u n d N o d e
	A block scheduled: the request that carries it to the server, the reply
	the server answers it with when it has been played, and the message
	they travel in (0x88 bytes in the ROM).
------------------------------------------------------------------------------*/

struct SoundNode
{
	SoundNode*			fNext;			// +0x00
	ULong				fId;			// +0x04
	TUSoundCallback*	fCallback;		// +0x08
	TUSoundNodeRequest	fRequest;		// +0x0c  its fBlock (+0x20) is the block as the client gave it
	TUSoundNodeReply	fReply;			// +0x58
	TUAsyncMessage		fMessage;		// +0x78
};


/*------------------------------------------------------------------------------
	T U S o u n d C a l l b a c k
------------------------------------------------------------------------------*/

// ROM 0x0025abec __ct__15TUSoundCallbackFv
TUSoundCallback::TUSoundCallback()
{ }


// ROM 0x0025ac20 __dt__15TUSoundCallbackFv
TUSoundCallback::~TUSoundCallback()
{ }


// ROM 0x0025ac38 __ct__19TUSoundCallbackProcFv
TUSoundCallbackProc::TUSoundCallbackProc()
{
	fProc = nil;
}


// ROM 0x0025acfc __dt__19TUSoundCallbackProcFv
TUSoundCallbackProc::~TUSoundCallbackProc()
{ }


// ROM 0x0025ad3c SetCallback__19TUSoundCallbackProcFPFP10SoundBlockil_v
void
TUSoundCallbackProc::SetCallback(SoundCallbackProc proc)
{
	fProc = proc;
}


// ROM 0x0025ad44 Complete__19TUSoundCallbackProcFP10SoundBlockil
void
TUSoundCallbackProc::Complete(SoundBlock* block, int state, long error)
{
	if (fProc != nil)
		fProc(block, state, error);
}


/*------------------------------------------------------------------------------
	T U S o u n d C h a n n e l
------------------------------------------------------------------------------*/

static void
InitReply(TUSoundReply* reply)
{
	reply->fEvent.fAEventClass = kNewtEventClass;
	reply->fEvent.fAEventID = kSoundEventId;
	reply->fChannel = 0;
	reply->fError = 0;
	reply->fValue = 0;
}


static void
InitNodeReply(TUSoundNodeReply* reply)
{
	memset(reply, 0, sizeof(*reply));
	reply->fEvent.fAEventClass = kNewtEventClass;
	reply->fEvent.fAEventID = kSoundEventId;
}


// ROM 0x0025ac80 __dt__14TUSoundChannelFv
// Closed, and the spare nodes given back.
TUSoundChannel::~TUSoundChannel()
{
	Close();
	SoundNode* node = fFreeNodes;
	while (node != nil)
	{
		SoundNode* next = node->fNext;
		delete node;
		node = next;
	}
}


// ROM 0x0025af44 Open__14TUSoundChannelFiT1
// The server's channel opened for output or input on the device, and
// the decompressor (compressor) that feeds it.  The handler is installed
// first, so the answers to what is scheduled find their way back here.
NewtonErr
TUSoundChannel::Open(int input, int device)
{
	TAEventHandler::Init(kSoundEventId, kNewtEventClass);
	if (gSndPort == 0)
		return ERRBASE_SOUND;
	TUSoundReply reply;
	InitReply(&reply);
	ULong openCommand, codecCommand;
	if (input == 0)
	{
		openCommand = kSndOpenOutput;
		codecCommand = kSndOpenDecompressor;
		fFlags |= kSoundChannelOutput;
	}
	else
	{
		openCommand = kSndOpenInput;
		codecCommand = kSndOpenCompressor;
		fFlags |= kSoundChannelInput;
	}
	NewtonErr err = SendImmediate(openCommand, 0, device, &reply, sizeof(reply));
	if (err == noErr)
		err = reply.fError;
	if (err != noErr)
		return err;
	fChannelId = reply.fChannel;
	err = SendImmediate(codecCommand, 0, fChannelId, &reply, sizeof(reply));
	if (err == noErr)
		err = reply.fError;
	if (err == noErr)
		fCodecChannelId = reply.fChannel;
	else
		Close();
	return err;
}


// ROM 0x0025b1a4 Close__14TUSoundChannelFv
// Both of the server's channels closed; what was scheduled is answered as
// aborted.  ==> the second close's error, or the reply's.
NewtonErr
TUSoundChannel::Close(void)
{
	NewtonErr err = noErr;
	TUSoundReply reply;
	InitReply(&reply);
	if (fCodecChannelId != 0)
		err = SendImmediate(kSndClose, fCodecChannelId, 0, &reply, sizeof(reply));
	if (fChannelId != 0)
	{
		NewtonErr closeErr = SendImmediate(kSndClose, fChannelId, 0, &reply, sizeof(reply));
		if (closeErr != noErr)
			err = closeErr;
	}
	if (err == noErr)
	{
		fChannelId = 0;
		fCodecChannelId = 0;
		AbortBusy();
		err = reply.fError;
	}
	return err;
}


// ROM 0x0025b278 Schedule__14TUSoundChannelFP10SoundBlockP15TUSoundCallback
// The block sent to the server in a node of its own, asynchronously - the
// answer, when it has been played, reaching AECompletionProc.  Plain
// samples go to the output channel, coded ones to the decompressor; the
// start and count are brought within the samples, and the node's data
// points at the first sample to play.
//
// DEVIATION: with no sound server the host's channel plays nothing and
// the block is dropped (the ROM's would answer kSndErrNoChannel, and then
// every sound the ROM's scripts play would throw).
NewtonErr
TUSoundChannel::Schedule(SoundBlock* block, TUSoundCallback* callback)
{
	if (gSndPort == 0)
		return noErr;
	SoundNode* node = nil;
	NewtonErr err;
	if (fVolume == (long) 0x80000000 && (fFlags & kSoundChannelAskForVolume) != 0)
		fVolume = GetVolume();
	ULong channel = (block->fCodec == nil) ? fChannelId : fCodecChannelId;
	long format = block->fFormat;
	if (format != kSoundFormatStd8 && format != kSoundFormatLinear16 && format != kSoundFormatMuLaw)
		err = kSndErrBadFormat;
	else if (channel == 0)
		err = kSndErrNoChannel;
	else
	{
		err = MakeNode(&node);
		if (err == noErr)
		{
			node->fCallback = callback;
			node->fRequest.fCommand = kSndSchedule;
			node->fRequest.fChannel = channel;
			node->fRequest.fNodeId = node->fId;
			node->fRequest.fBlock = *block;
			node->fRequest.fChannelVolume = fVolume;
			long start = block->fStart;
			long count = block->fPlayCount;
			long samples = block->fCount;
			if (start < 0 || samples <= start)
				start = 0;
			if (count < 1 || samples < start + count)
				count = samples - start;
			node->fRequest.fBlock.fStart = start;
			node->fRequest.fBlock.fPlayCount = count;
			node->fRequest.fBlock.fData = (char*) block->fData + (start * block->fSampleBits) / 8;
			TUPort port(gSndPort);
			err = port.SendRPC(&node->fMessage, &node->fRequest, sizeof(node->fRequest), &node->fReply, sizeof(node->fReply));
			if (err == noErr)
			{
				node->fNext = fBusyNodes;
				fBusyNodes = node;
				return noErr;
			}
		}
	}
	if (node != nil)
		FreeNode(node);
	return err;
}


// ROM 0x0025b46c Cancel__14TUSoundChannelFUl
// The block scheduled with this refCon taken off the server's list.
NewtonErr
TUSoundChannel::Cancel(ULong refCon)
{
	if (fChannelId == 0)
		return kSndErrNoChannel;
	SoundNode* node = FindRefCon(refCon);
	if (node == nil)
		return kSndErrNothingToPlay;
	TUSoundReply reply;
	InitReply(&reply);
	NewtonErr err = SendImmediate(kSndCancel, fChannelId, node->fId, &reply, sizeof(reply));
	if (err == noErr)
		err = reply.fError;
	return err;
}


// ROM 0x0025b50c Start__14TUSoundChannelFi
// The channel told to play what is scheduled: async 0 answers when all
// of it has gone to the hardware (the server's command 10), anything else
// at once (9) and marks the channel running.  A paused channel goes on.
// (DEVIATION: with no sound server, nothing - see Schedule.)
NewtonErr
TUSoundChannel::Start(int async)
{
	if (gSndPort == 0)
		return noErr;
	if (fCodecChannelId == 0)
		return kSndErrNoChannel;
	TUSoundReply reply;
	InitReply(&reply);
	NewtonErr err = SendImmediate(async == 0 ? kSndStartWait : kSndStart, fCodecChannelId, 0, &reply, sizeof(reply));
	if (err == noErr)
		err = reply.fError;
	if (err == noErr)
	{
		fFlags &= ~kSoundChannelPaused;
		if (async != 0)
			fFlags |= kSoundChannelRunning;
	}
	return err;
}


// The block a node was scheduled with copied out, and how far through it
// the server had got (Pause and Stop).
static void
CopyOutPlayed(SoundNode* node, TUSoundNodeReply* reply, SoundBlock* block, long* samplesPlayed)
{
	if (block != nil)
		*block = node->fRequest.fBlock;
	if (samplesPlayed != nil)
		*samplesPlayed = node->fRequest.fBlock.fStart + reply->fPosition;
}


// ROM 0x0025b5c0 Pause__14TUSoundChannelFP10SoundBlockPl
// Paused, with where it had got to; a paused channel is started again.
NewtonErr
TUSoundChannel::Pause(SoundBlock* block, long* samplesPlayed)
{
	if (samplesPlayed != nil)
		*samplesPlayed = -1;
	if (gSndPort == 0)
		return noErr;			// DEVIATION: see Schedule
	if (fChannelId == 0)
		return kSndErrNoChannel;
	if ((fFlags & kSoundChannelPaused) != 0)
		return Start(1);
	TUSoundNodeReply reply;
	InitNodeReply(&reply);
	NewtonErr err = SendImmediate(kSndPause, fCodecChannelId, 0, (TUSoundReply*) &reply, sizeof(reply));
	if (err == noErr)
		err = reply.fError;
	if (err == noErr)
	{
		fFlags |= kSoundChannelPaused;
		SoundNode* node = FindNode(reply.fNodeId);
		if (node != nil)
			CopyOutPlayed(node, &reply, block, samplesPlayed);
	}
	return err;
}


// ROM 0x0025b6f4 Stop__14TUSoundChannelFP10SoundBlockPl
// A running channel stopped, with where it had got to, and everything
// scheduled on it answered as aborted.  A channel that was only ever
// started to be waited for is not running, and is left alone.
NewtonErr
TUSoundChannel::Stop(SoundBlock* block, long* samplesPlayed)
{
	NewtonErr err = noErr;
	if (samplesPlayed != nil)
		*samplesPlayed = -1;
	if (gSndPort == 0)
		return noErr;			// DEVIATION: see Schedule
	if (fChannelId == 0)
		return kSndErrNoChannel;
	if ((fFlags & kSoundChannelRunning) != 0)
	{
		TUSoundNodeReply reply;
		InitNodeReply(&reply);
		err = SendImmediate(kSndStop, fCodecChannelId, 0, (TUSoundReply*) &reply, sizeof(reply));
		if (err == noErr)
			err = reply.fError;
		if (err == noErr)
		{
			fFlags &= ~(kSoundChannelRunning | kSoundChannelPaused);
			SoundNode* node = FindNode(reply.fNodeId);
			if (node != nil)
				CopyOutPlayed(node, &reply, block, samplesPlayed);
			AbortBusy();
		}
	}
	return err;
}


// ROM 0x0025a868 AECompletionProc__14TUSoundChannelFP10TUMsgTokenPUlP7TAEvent
// A schedule answered: the node taken off the busy list, its callback
// told the state and error, and the node kept for the next one.
void
TUSoundChannel::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* event)
{
	TUSoundNodeReply* reply = (TUSoundNodeReply*) event;
	SoundNode* prev = nil;
	SoundNode* node;
	for (node = fBusyNodes; node != nil; prev = node, node = node->fNext)
		if (node->fId == reply->fNodeId)
			break;
	if (node == nil)
		return;
	if (prev == nil)
		fBusyNodes = node->fNext;
	else
		prev->fNext = node->fNext;
	if (node->fCallback != nil)
		node->fCallback->Complete(&node->fRequest.fBlock, reply->fState, reply->fError);
	FreeNode(node);
}


// ROM 0x0025a904 MakeNode__14TUSoundChannelFPP9SoundNode
// The spare node, or a new one whose message is collected by this task's
// app world and names this channel as the handler of its answer.
NewtonErr
TUSoundChannel::MakeNode(SoundNode** nodep)
{
	if (fFreeNodes == nil)
	{
		SoundNode* node = new SoundNode;
		if (node != nil)
		{
			node->fRequest.fEvent.fAEventClass = kNewtEventClass;
			node->fRequest.fEvent.fAEventID = kSoundEventId;
			node->fRequest.fChannel = 0;
			node->fRequest.fCommand = 0;
			node->fRequest.fNodeId = 0;
			node->fRequest.fBlock.fData = nil;
			node->fRequest.fBlock.fCount = 0;
			node->fRequest.fBlock.fSampleBits = 0;
			node->fRequest.fBlock.fFormat = 0;
			node->fRequest.fBlock.fSampleRate = 0x560a6e85;		// 22026.43 a second
			node->fRequest.fBlock.fVolume = 0x7fffffff;
			node->fRequest.fBlock.fStart = 0;
			node->fRequest.fBlock.fPlayCount = 0;
			node->fRequest.fBlock.fLoops = 0;
			node->fRequest.fBlock.fRefCon = nil;
			node->fRequest.fChannelVolume = 0x7fffffff;
			InitNodeReply(&node->fReply);
		}
		*nodep = node;
		if (node == nil)
			return MemError();
		NewtonErr err = node->fMessage.Init(true);
		if (err != noErr)
			return err;
		err = node->fMessage.SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
		if (err != noErr)
			return err;
		err = node->fMessage.SetUserRefCon((ULong) this);
		if (err != noErr)
			return err;
	}
	else
	{
		*nodep = fFreeNodes;
		fFreeNodes = fFreeNodes->fNext;
	}
	(*nodep)->fId = UniqueId();
	(*nodep)->fNext = nil;
	(*nodep)->fCallback = nil;
	return noErr;
}


// ROM 0x0025aa74 FreeNode__14TUSoundChannelFP9SoundNode
// Kept while there are fewer than two spares, deleted after.  An idle
// channel is no longer running or paused.
void
TUSoundChannel::FreeNode(SoundNode* node)
{
	long spares = 0;
	for (SoundNode* n = fFreeNodes; n != nil; n = n->fNext)
		spares++;
	if (spares > 1)
		delete node;
	else
	{
		node->fNext = fFreeNodes;
		fFreeNodes = node;
	}
	if (fBusyNodes == nil)
		fFlags &= ~(kSoundChannelRunning | kSoundChannelPaused);
}


// ROM 0x0025aaec FindNode__14TUSoundChannelFUl
SoundNode*
TUSoundChannel::FindNode(ULong id)
{
	SoundNode* node;
	for (node = fBusyNodes; node != nil; node = node->fNext)
		if (node->fId == id)
			break;
	return node;
}


// ROM 0x0025ab10 FindRefCon__14TUSoundChannelFUl
SoundNode*
TUSoundChannel::FindRefCon(ULong refCon)
{
	SoundNode* node;
	for (node = fBusyNodes; node != nil; node = node->fNext)
		if ((ULong) node->fRequest.fBlock.fRefCon == refCon)
			break;
	return node;
}


// ROM 0x0025ab34 UniqueId__14TUSoundChannelFv
ULong
TUSoundChannel::UniqueId(void)
{
	do
	{
		if (++fLastNodeId == 0)
			fLastNodeId = 1;
	} while (FindNode(fLastNodeId) != nil);
	return fLastNodeId;
}


// ROM 0x0025ab74 AbortBusy__14TUSoundChannelFv
// Everything scheduled given up: each message aborted and each callback
// told the block was cancelled.
void
TUSoundChannel::AbortBusy(void)
{
	SoundNode* node = fBusyNodes;
	while (node != nil)
	{
		node->fMessage.Abort();
		SoundNode* next = node->fNext;
		if (node->fCallback != nil)
			node->fCallback->Complete(&node->fRequest.fBlock, 1, kSndErrCancelled);
		FreeNode(node);
		node = next;
	}
	fBusyNodes = nil;
}
