/*
	File:		sound/FrameSoundChannel.cpp

	Contains:	TFrameSoundChannel, GlobalSoundChannel and the
				protoSoundChannel natives (FrameSoundChannel.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "FrameSoundChannel.h"
#include "SoundServer.h"
#include "SoundSettings.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"
#include "NativeFunctions.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "Frames.h"
#include "Unicode.h"
#include "Locale.h"
#include "ByteOrder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void
ThrowSoundErr(NewtonErr err)
{
	Throw(exFrames, (void*) (Long) err, nil);
}


// ROM 0x001e5bec ConvertCodecBlock__FP10SoundBlockP10CodecBlock
void
ConvertCodecBlock(SoundBlock* from, CodecBlock* to)
{
	to->fError = 0;
	to->fBuffer = from->fData;
	to->fSize = from->fCount;
	to->fSampleBits = from->fSampleBits;
	to->fFormat = from->fFormat;
	to->fSampleRate = from->fSampleRate;
	to->fRefCon = (ULong) from->fRefCon;
}


// ROM 0x001e5c28 ConvertCodecBlock__FP10CodecBlockP10SoundBlock
// (nothing when the codec left an error in the block)
void
ConvertCodecBlock(CodecBlock* from, SoundBlock* to)
{
	if (from->fError < 0)
		return;
	to->fData = from->fBuffer;
	to->fCount = from->fSize;
	to->fSampleBits = from->fSampleBits;
	to->fFormat = from->fFormat;
	to->fSampleRate = from->fSampleRate;
	to->fRefCon = (void*) from->fRefCon;
}


/*------------------------------------------------------------------------------
	T F r a m e S o u n d C a l l b a c k
------------------------------------------------------------------------------*/

// ROM 0x000d1ea0 __ct__19TFrameSoundCallbackFv
TFrameSoundCallback::TFrameSoundCallback()
{
	fChannel = nil;
}


// ROM 0x000d1ee8 __dt__19TFrameSoundCallbackFv
TFrameSoundCallback::~TFrameSoundCallback()
{ }


// The frame a block's refCon holds (a heap-held Ref, Convert's).
static RefStruct*
BlockFrame(SoundBlock* block)
{
	return (RefStruct*) block->fRefCon;
}


// ROM 0x000d24bc Complete__19TFrameSoundCallbackFP10SoundBlockil
// A frame played: its samples unlocked, the block's hold on it let go,
// its codec deleted, and its callback method - if it has one - sent the
// state and error, any exception out of it caught and dropped.
void
TFrameSoundCallback::Complete(SoundBlock* block, int state, long error)
{
	RefVar frame(BlockFrame(block) != nil ? (Ref) *BlockFrame(block) : NILREF);
	RefStruct* holder = BlockFrame(block);
	if (holder != nil)
		delete holder;
	UnlockRef(GetProtoVariable(frame, RSSYMsamples, nil));
	fChannel->DeleteCodec(block);
	newton_try
	{
		RefVar callback(GetProtoVariable(frame, RSSYMcallback, nil));
		if (NOTNIL(callback))
			// (the ROM's NSSendProto, which the reconstruction has with one
			// argument only; a sound frame has no _parent, so NSSend finds
			// the same method through its _proto)
			NSSend(frame, RSSYMcallback, RefVar(MAKEINT(state)), RefVar(MAKEINT(error)));
	}
	newton_catch_all
	{ }
	end_try;
}


/*------------------------------------------------------------------------------
	T F r a m e S o u n d C h a n n e l
------------------------------------------------------------------------------*/

// ROM 0x000d25e0 __ct__18TFrameSoundChannelFv
TFrameSoundChannel::TFrameSoundChannel()
{
	fCodec = nil;
	memset(&fCodecBlock, 0, sizeof(fCodecBlock));
	fCodecInited = false;
	fCallback.fChannel = this;
}


// ROM 0x000d2648 __dt__18TFrameSoundChannelFv
TFrameSoundChannel::~TFrameSoundChannel()
{
	Close();
}


// ROM 0x000d269c Open__18TFrameSoundChannelFiT1
// Opened, and the codec the channel was given the name of made.
NewtonErr
TFrameSoundChannel::Open(int input, int device)
{
	NewtonErr err = TUSoundChannel::Open(input, device);
	if (IsString(fCodecName))
	{
		char name[128];
		ConvertFromUnicode(GetCString(fCodecName), name, kMacRomanEncoding, 0x7fffffff);
		fCodec = (TSoundCodec*) NewByName("TSoundCodec", name);
	}
	return err;
}


// ROM 0x000d270c Close__18TFrameSoundChannelFv
NewtonErr
TFrameSoundChannel::Close(void)
{
	NewtonErr err = TUSoundChannel::Close();
	if (fCodec != nil)
	{
		SafeCodecDelete(fCodec);
		fCodec = nil;
		fCodecInited = false;
	}
	return err;
}


// ROM 0x000d2744 Schedule__18TFrameSoundChannelFRC6RefVar
// The frame converted (Convert throws when it cannot be) and scheduled,
// this channel's callback to hear when it has been played.
//
// (A codec frame whose codec cannot be made - the ROM has no
// TMacintalkCodec, the speech a string becomes - has no codec in its
// block: OpenCodec's
// error is not looked at, and the coded bytes are played as samples, as
// the ROM's would be.  NEWTON_TRACE_SOUND says so.)
//
// DEVIATION: with no sound server the block is let go at once
// (TUSoundChannel::Schedule drops it).
NewtonErr
TFrameSoundChannel::Schedule(RefArg sound)
{
	SoundBlock block;
	Convert(sound, &block);
	if (block.fCodec == nil && getenv("NEWTON_TRACE_SOUND") != NULL
	 && EQRef(RefVar(GetProtoVariable(RefVar(*BlockFrame(&block)), RSSYMsndframetype, nil)), RSSYMcodec))
	{
		RefVar name(GetProtoVariable(RefVar(*BlockFrame(&block)), RSSYMcodecname, nil));
		char codec[64] = "?";
		if (IsString(name))
			ConvertFromUnicode(GetCString(name), codec, kMacRomanEncoding, sizeof(codec) - 1);
		fprintf(stderr, "sound: no codec %s: the coded bytes are played as they are\n", codec);
	}
	NewtonErr err = TUSoundChannel::Schedule(&block, &fCallback);
	if (gSndPort == 0)
		fCallback.Complete(&block, 1, err);
	return err;
}


// ROM 0x000d2774 Convert__18TFrameSoundChannelFRC6RefVarP10SoundBlock
// A sound (frame) as a SoundBlock; throws evt.ex.fr on what cannot be
// played.
void
TFrameSoundChannel::Convert(RefArg sound, SoundBlock* block)
{
	NewtonErr err = kSndErrBadFormat;
	RefVar value;
	RefVar frame(FConvertToSoundFrame(RefVar(NILREF), sound));
	memset(block, 0, sizeof(*block));
	block->fRefCon = nil;
	value = GetProtoVariable(frame, RSSYMsndframetype, nil);
	block->fCodec = nil;
	Boolean simple = EQRef(value, RSSYMsimplesound);
	if (EQRef(value, RSSYMcodec))
	{
		OpenCodec(frame, block);			// (its error is not looked at)
		simple = true;
	}
	if (simple)
	{
		value = GetProtoVariable(frame, RSSYMcompressiontype, nil);
		if (ISNIL(value))
			block->fFormat = kSoundFormatStd8;
		else
		{
			block->fFormat = RINT(value);
			if (block->fFormat != kSoundFormatStd8 && block->fFormat != kSoundFormatLinear16 && block->fFormat != kSoundFormatMuLaw)
				ThrowSoundErr(kSndErrBadFormat);
		}
		value = GetProtoVariable(frame, RSSYMdatatype, nil);
		long bits = 0;
		if (ISNIL(value))
			bits = (block->fFormat == kSoundFormatStd8) ? 8 : 16;
		else
		{
			long type = RINT(value);
			if (type == 8 || type == 1)
				bits = 8;
			else if (type == 16 || type == 2)
				bits = 16;
		}
		if (bits != 0)
		{
			block->fSampleBits = bits;
			value = GetProtoVariable(frame, RSSYMsamples, nil);
			LockRef(value);
			block->fData = BinaryData(value);
			if (block->fCodec == nil)
				block->fCount = Length(value) / (block->fSampleBits / 8);
			else
				block->fCount = Length(value);
			value = GetProtoVariable(frame, RSSYMlength, nil);
			if (ISINT(value))
				block->fCount = RVALUE(value);
			value = GetProtoVariable(frame, RSSYMsamplingrate, nil);
			Boolean rateOk = true;
			if (IsReal(value))
				block->fSampleRate = (Fixed) (CDouble(value) * 65536.0);
			else if (ISINT(value))
				block->fSampleRate = RVALUE(value) << 16;
			else if (IsBinary(value))
				block->fSampleRate = (Fixed) (int32_t) GetBigEndianWord(BinaryData(value));
			else
				block->fSampleRate = 0x560a6e85;		// 22026.43 a second (and not checked)
			if ((IsReal(value) || ISINT(value) || IsBinary(value)) && block->fSampleRate < 1)
				rateOk = false;
			if (rateOk)
			{
				block->fRefCon = new RefStruct(frame);
				err = MemError();
				if (err == noErr)
				{
					value = GetProtoVariable(frame, RSSYMstart, nil);
					block->fStart = ISNIL(value) ? 0 : RINT(value);
					value = GetProtoVariable(frame, RSSYMcount, nil);
					block->fPlayCount = ISNIL(value) ? block->fCount : RINT(value);
					value = GetProtoVariable(frame, RSSYMloops, nil);
					block->fLoops = ISNIL(value) ? 0 : RINT(value);
					value = GetProtoVariable(frame, RSSYMvolume, nil);
					if (ISINT(value))
						block->fVolume = VolumeToDecibels(RVALUE(value));
					else if (IsReal(value))
						block->fVolume = (long) (CDouble(value) * 65536.0);
					else
						block->fVolume = 0x7fffffff;
					err = InitCodec(block);
					if (err == noErr)
						return;
				}
			}
		}
	}
	// what was done undone, and the error thrown
	// (NEWTON_TRACE_SOUND: which frame, and what was made of it)
	if (getenv("NEWTON_TRACE_SOUND") != NULL)
	{
		RefVar type(GetProtoVariable(frame, RSSYMsndframetype, nil));
		fprintf(stderr, "sound: cannot play a sound frame (%ld): sndFrameType %s, %ld bits, format %ld, rate %#lx, %ld samples\n", (long) err,
				IsSymbol(type) ? SymbolName(type) : "-",
				(long) block->fSampleBits, (long) block->fFormat, (long) block->fSampleRate, (long) block->fCount);
	}
	DeleteCodec(block);
	if (block->fRefCon != nil)
	{
		RefVar held(*BlockFrame(block));
		UnlockRef(GetProtoVariable(held, RSSYMsamples, nil));
		delete BlockFrame(block);
		block->fRefCon = nil;
	}
	ThrowSoundErr(err);
}


// ROM 0x000d2d04 OpenCodec__18TFrameSoundChannelFRC6RefVarP10SoundBlock
// A codec frame's codec: the channel's own when the frame names it,
// otherwise a new one by name; the buffer size and count the codec is
// to work in.
NewtonErr
TFrameSoundChannel::OpenCodec(RefArg frame, SoundBlock* block)
{
	RefVar value(GetProtoVariable(frame, RSSYMbuffersize, nil));
	block->fUnknown2C = ISINT(value) ? RVALUE(value) : 0;
	value = GetProtoVariable(frame, RSSYMbuffercount, nil);
	block->fUnknown30 = ISINT(value) ? RVALUE(value) : 0;
	value = GetProtoVariable(frame, RSSYMcodecname, nil);
	if (!IsString(value))
		return -0xbd12;			// (-48402, which the ROM answers here)
	char name[128];
	ConvertFromUnicode(GetCString(value), name, kMacRomanEncoding, 0x7fffffff);
	if (IsString(fCodecName) && NOTNIL(FStrEqual(RefVar(NILREF), value, fCodecName)))
	{
		block->fCodec = fCodec;
		return noErr;
	}
	block->fCodec = (TSoundCodec*) NewByName("TSoundCodec", name);
	if (block->fCodec == nil)
	{
		NewtonErr err = MemError();
		if (err == noErr)
			err = kSndErrBadFormat;
		return err;
	}
	return noErr;
}


// ROM 0x000d1f28 InitCodec__18TFrameSoundChannelFP10SoundBlock
// A block's codec set up on it - the channel's own only the first time,
// its setup kept and reused after - and the block changed to what the
// codec will produce.
NewtonErr
TFrameSoundChannel::InitCodec(SoundBlock* block)
{
	if (block->fCodec == nil)
		return noErr;
	Boolean own = (block->fCodec == fCodec);
	CodecBlock codecBlock;
	ConvertCodecBlock(block, &codecBlock);
	if (!own || !fCodecInited)
	{
		NewtonErr err = SafeCodecInit((TSoundCodec*) block->fCodec, &codecBlock);
		if (err != noErr)
			return err;
		if (own)
		{
			fCodecBlock = codecBlock;
			fCodecInited = true;
		}
	}
	else
	{
		codecBlock.fSampleBits = fCodecBlock.fSampleBits;
		codecBlock.fFormat = fCodecBlock.fFormat;
		codecBlock.fSampleRate = fCodecBlock.fSampleRate;
	}
	ConvertCodecBlock(&codecBlock, block);
	return noErr;
}


// ROM 0x000d1ff4 DeleteCodec__18TFrameSoundChannelFP10SoundBlock
// A block's codec deleted, unless it is the channel's own.
NewtonErr
TFrameSoundChannel::DeleteCodec(SoundBlock* block)
{
	if (fCodec != block->fCodec)
		SafeCodecDelete((TSoundCodec*) block->fCodec);
	block->fCodec = nil;
	return noErr;
}


// ROM 0x001e70b4 GlobalSoundChannel__Fv
// The channel the NewtonScript sound functions play through, made and
// opened for output on vars.userConfiguration.outputDevice the first time
// one of them is called; throws evt.ex.fr if it cannot be.
//
// DEVIATION: with no sound server (gSndPort 0: a host with no sound
// driver) the channel is made and not opened - the ROM's Open would fail
// and throw - so it keeps the volume it is told and plays nothing.
TFrameSoundChannel*
GlobalSoundChannel(void)
{
	if (gSoundChannel == nil)
	{
		TFrameSoundChannel* channel = new TFrameSoundChannel;
		gSoundChannel = channel;
		if (MemError() != noErr)
			ThrowSoundErr(MemError());
		if (gSndPort != 0)
		{
			long device = 0;
			RefVar config(GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration));
			RefVar value(GetProtoVariable(config, RSSYMoutputdevice, nil));
			if (ISINT(value))
				device = RVALUE(value);
			NewtonErr err = channel->Open(0, device);
			if (err != noErr)
			{
				delete channel;
				gSoundChannel = nil;
				ThrowSoundErr(err);
			}
		}
	}
	return (TFrameSoundChannel*) gSoundChannel;
}


/*------------------------------------------------------------------------------
	p r o t o S o u n d C h a n n e l
	The channel a script opens is kept in its _channel slot, as an address.
------------------------------------------------------------------------------*/

// ROM 0x001e676c LocalSoundChannel__FRC6RefVar
static TFrameSoundChannel*
LocalSoundChannel(RefArg rcvr)
{
	RefVar slot(GetFrameSlotRef(rcvr, RSSYM_channel));
	TFrameSoundChannel* channel = nil;
	if (ISNIL(slot) || (channel = (TFrameSoundChannel*) RefToAddress(slot)) == nil)
		ThrowSoundErr(kSndErrNoChannel);
	return channel;
}


// ROM 0x001e67e8 FSoundOpen
// Open(): a channel for output (or input, when the frame's direction is
// 'record) on the frame's device, or the user configuration's; an input
// channel gets the frame's gain, or the configuration's.
static Ref
FSoundOpen(RefArg rcvr)
{
	TFrameSoundChannel* channel = new TFrameSoundChannel;
	if (MemError() != noErr)
		ThrowSoundErr(MemError());
	long device = 0;
	Boolean input = EQRef(RefVar(GetProtoVariable(rcvr, RSSYMdirection, nil)), RSSYMrecord);
	RefVar value(GetProtoVariable(rcvr, input ? RSSYMinputdevice : RSSYMoutputdevice, nil));
	if (ISINT(value))
		device = RVALUE(value);
	else
	{
		value = GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration);
		value = GetProtoVariable(value, input ? RSSYMinputdevice : RSSYMoutputdevice, nil);
		if (ISINT(value))
			device = RVALUE(value);
	}
	NewtonErr err = channel->Open(input, device);
	if (err != noErr)
		ThrowSoundErr(err);		// ROM QUIRK kept: the channel is not deleted
	if (input)
	{
		long gain = 0x80;
		value = GetProtoVariable(rcvr, RSSYMinputgain, nil);
		if (ISINT(value))
			gain = RVALUE(value);
		else
		{
			value = GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration);
			value = GetProtoVariable(value, RSSYMinputgain, nil);
			if (ISINT(value))
				gain = RVALUE(value);
		}
		channel->SetInputGain(gain);
	}
	SetFrameSlot(rcvr, RSSYM_channel, RefVar(AddressToRef(channel)));
	return NILREF;
}


// ROM 0x001e6a70 FSoundClose
static Ref
FSoundClose(RefArg rcvr)
{
	TFrameSoundChannel* channel = LocalSoundChannel(rcvr);
	NewtonErr err = channel->Close();
	if (err != noErr)
		ThrowSoundErr(err);
	delete channel;
	SetFrameSlot(rcvr, RSSYM_channel, RefVar(NILREF));
	return NILREF;
}


// ROM 0x001e6af8 FSoundSchedule
static Ref
FSoundSchedule(RefArg rcvr, RefArg sound)
{
	NewtonErr err = LocalSoundChannel(rcvr)->Schedule(sound);
	if (err != noErr)
		ThrowSoundErr(err);
	return NILREF;
}


// ROM 0x001e6b80 FSoundStart
// Start(async): nil waits for everything scheduled to have been played -
// which an input channel cannot do.
static Ref
FSoundStart(RefArg rcvr, RefArg async)
{
	TFrameSoundChannel* channel = LocalSoundChannel(rcvr);
	Boolean input = EQRef(RefVar(GetProtoVariable(rcvr, RSSYMdirection, nil)), RSSYMrecord);
	if (input && ISNIL(async))
		ThrowSoundErr(ERRBASE_SOUND);
	NewtonErr err = channel->Start(NOTNIL(async));
	if (err != noErr)
		ThrowSoundErr(err);
	return NILREF;
}


// A pause or stop's answer: {sound: the frame, index: the sample it got to}.
static Ref
PlayedFrame(SoundBlock* block, long samples, RefArg sound)
{
	RefVar result(AllocateFrame());
	SetFrameSlot(result, RSSYMsound, sound);
	SetFrameSlot(result, RSSYMindex, RefVar(MAKEINT(samples)));
	return result;
}


// ROM 0x001e6c48 FSoundStop
// Stop(): paused first, when it was not, so where it got to is known; ==>
// {sound, index}, or nil when nothing was playing.
static Ref
FSoundStop(RefArg rcvr)
{
	TFrameSoundChannel* channel = LocalSoundChannel(rcvr);
	RefVar result;
	RefVar sound;
	SoundBlock block;
	long samples;
	if ((channel->fFlags & kSoundChannelPaused) == 0)
	{
		NewtonErr err = channel->Pause(&block, &samples);
		if (err != noErr)
			ThrowSoundErr(err);
		if (samples >= 0)
			sound = (Ref) *BlockFrame(&block);
	}
	NewtonErr err = channel->Stop(&block, &samples);
	if (err != noErr)
		ThrowSoundErr(err);
	if (samples >= 0)
		result = PlayedFrame(&block, samples, sound);
	return result;
}


// ROM 0x001e6d7c FSoundPause
// Pause(): ==> {sound, index}, or nil.
static Ref
FSoundPause(RefArg rcvr)
{
	TFrameSoundChannel* channel = LocalSoundChannel(rcvr);
	RefVar result;
	SoundBlock block;
	long samples;
	NewtonErr err = channel->Pause(&block, &samples);
	if (err != noErr)
		ThrowSoundErr(err);
	if (samples >= 0)
		result = PlayedFrame(&block, samples, RefVar((Ref) *BlockFrame(&block)));
	return result;
}


// ROM 0x001e6e60 FSoundIsActive
static Ref
FSoundIsActive(RefArg rcvr)
{
	return MAKEBOOLEAN((LocalSoundChannel(rcvr)->fFlags & kSoundChannelRunning) != 0);
}


// ROM 0x001e6e84 FSoundIsPaused
static Ref
FSoundIsPaused(RefArg rcvr)
{
	return MAKEBOOLEAN((LocalSoundChannel(rcvr)->fFlags & kSoundChannelPaused) != 0);
}


// ROM 0x001e6ea8 FSetChannelVolume
// SetVolume(volume): 0 to 4, nil for the system's.
static Ref
FSetChannelVolume(RefArg rcvr, RefArg volume)
{
	TFrameSoundChannel* channel = LocalSoundChannel(rcvr);
	channel->SetVolume(ISNIL(volume) ? 0x7fffffff : VolumeToDecibels(RINT(volume)));
	return NILREF;
}


// ROM 0x001e6f00 FGetChannelVolume
static Ref
FGetChannelVolume(RefArg rcvr)
{
	long decibels = LocalSoundChannel(rcvr)->GetVolume();
	if (decibels == 0x7fffffff)
		return NILREF;
	return MAKEINT(DecibelsToVolume(decibels));
}


// ROM 0x001e6f8c FSetChannelInputGain
// SetInputGain(gain): 0 to 255; not an integer, the preference's.
static Ref
FSetChannelInputGain(RefArg rcvr, RefArg gain)
{
	RefVar value(gain);
	long setting = 0x80;
	if (!ISINT(value))
		value = GetPreference(RefVar(RSSYMinputgain));
	if (ISINT(value))
	{
		setting = RVALUE(value);
		if (setting < 0)
			setting = 0;
		else if (setting > 0xff)
			setting = 0xff;
	}
	LocalSoundChannel(rcvr)->SetInputGain(setting);
	return NILREF;
}


// ROM 0x001e7050 FGetChannelInputGain
static Ref
FGetChannelInputGain(RefArg rcvr)
{
	return MAKEINT(LocalSoundChannel(rcvr)->GetInputGain());
}


void
RegisterSoundChannelNatives(void)
{
	RegisterNativeFunction("FSoundOpen", (void*) FSoundOpen, 0);
	RegisterNativeFunction("FSoundClose", (void*) FSoundClose, 0);
	RegisterNativeFunction("FSoundSchedule", (void*) FSoundSchedule, 1);
	RegisterNativeFunction("FSoundStart", (void*) FSoundStart, 1);
	RegisterNativeFunction("FSoundStop", (void*) FSoundStop, 0);
	RegisterNativeFunction("FSoundPause", (void*) FSoundPause, 0);
	RegisterNativeFunction("FSoundIsActive", (void*) FSoundIsActive, 0);
	RegisterNativeFunction("FSoundIsPaused", (void*) FSoundIsPaused, 0);
	RegisterNativeFunction("FSetChannelVolume", (void*) FSetChannelVolume, 1);
	RegisterNativeFunction("FGetChannelVolume", (void*) FGetChannelVolume, 0);
	RegisterNativeFunction("FSetChannelInputGain", (void*) FSetChannelInputGain, 1);
	RegisterNativeFunction("FGetChannelInputGain", (void*) FGetChannelInputGain, 0);
}
