/*
	File:		sound/SoundSettings.cpp

	Contains:	The volume as a script sees it (SoundSettings.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "SoundSettings.h"
#include "SoundChannel.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Frames.h"


// decibels are 16.16 fixed point on the wire and reals in a script
static const double	kDecibelScale = 65536.0;


// ROM 0x001e85ac VolumeToDecibels__Fl
// The five settings' levels, read out of the ROM's own words: an eighth,
// a half and a square root of a half of full amplitude for 1, 2 and 3,
// full for 4 and silence for 0.
long
VolumeToDecibels(long volume)
{
	if (volume < 0)
		return kSilenceDecibels;
	if (volume > 4)
		return 0;
	switch (volume)
	{
	case 0:		return kSilenceDecibels;
	case 1:		return (long) 0xffedf02e;	// -18.0618 dB
	case 2:		return (long) 0xfff9faba;	//  -6.0206 dB
	case 3:		return (long) 0xfffcfd5d;	//  -3.0103 dB
	}
	return 0;									// 4: full
}


// ROM 0x001e8f54 DecibelsToVolume__Fl
// The way back: the setting whose level the decibels reach.  The ROM
// compares against VolumeToDecibels(2) and VolumeToDecibels(3) built up
// out of immediate constants, so a level exactly at a setting's own
// answers that setting.
long
DecibelsToVolume(long decibels)
{
	if (decibels == kSilenceDecibels)
		return 0;
	if (decibels < (long) 0xfff9faba)		// below volume 2's level
		return 1;
	if (decibels < (long) 0xfffcfd5d)		// below volume 3's level
		return 2;
	return decibels < 0 ? 3 : 4;
}


// ROM 0x001e8614 FVolumeToDecibels
Ref
FVolumeToDecibels(RefArg /*rcvr*/, RefArg volume)
{
	return MakeReal(VolumeToDecibels(RINT(volume)) / kDecibelScale);
}


// ROM 0x001e9434 FDecibelsToVolume
Ref
FDecibelsToVolume(RefArg /*rcvr*/, RefArg decibels)
{
	return MAKEINT(DecibelsToVolume((long) (CoerceToDouble(decibels) * kDecibelScale)));
}


// ROM 0x001e95f8 FGetVolume
Ref
FGetVolume(RefArg /*rcvr*/)
{
	return MAKEINT(DecibelsToVolume(GlobalSoundChannel()->GetVolume()));
}


// ROM 0x001e9618 FSetVolume
// nil is volume 0, silence.  ==> nil.
Ref
FSetVolume(RefArg /*rcvr*/, RefArg volume)
{
	long setting = ISNIL(volume) ? 0 : RINT(volume);
	GlobalSoundChannel()->SetVolume(VolumeToDecibels(setting));
	return NILREF;
}


// ROM 0x001e9668 FGetSystemVolume
Ref
FGetSystemVolume(RefArg /*rcvr*/)
{
	return MakeReal(GlobalSoundChannel()->GetVolume() / kDecibelScale);
}


// ROM 0x001e96a0 FSetSystemVolume
// ==> the decibels the channel settled on, which is what was asked for
// unless the sound server had something to say about it.
Ref
FSetSystemVolume(RefArg /*rcvr*/, RefArg decibels)
{
	long fixed = (long) (CoerceToDouble(decibels) * kDecibelScale);
	return MakeReal(GlobalSoundChannel()->SetVolume(fixed) / kDecibelScale);
}


// the sound frame a string or a binary is made into: the ROM's own
// numbers, read out of the instructions that build it
enum {
	kSoundFrameBufferSize		= 5000,
	kSoundFrameBufferCount		= 4,
	kSoundFrameCompressionType	= 6,
	kSoundFrameDataType			= 16,
	kSoundFrameSamplingRate		= 21600
};


// ROM 0x000d3188 ConvertToSoundFrame__FRC6RefVar (the native is 0x001e870c)
// What a script hands to a sound function turned into something the sound
// server can play: a string is spoken, so it becomes a codec frame for
// TMacintalkCodec with the text as its samples; a binary is coded sound,
// and its class names the codec that knows how to read it.  Anything else
// - a sound frame already, most of all - is answered as it stands.
//
// NOT YET RECONSTRUCTED: FStripInk, which the ROM runs the string through
// first; a string of this reconstruction carries no ink (frames/RichString.h),
// so a clone of it is what stripping would give.
Ref
FConvertToSoundFrame(RefArg /*rcvr*/, RefArg obj)
{
	RefVar sound(obj);
	if (IsString(sound))
	{
		RefVar frame(AllocateFrame());
		SetFrameSlot(frame, RSSYM_proto, RefVar(Rprotosoundframe));
		SetFrameSlot(frame, RSSYMsndframetype, RSSYMcodec);
		SetFrameSlot(frame, RSSYMcodecname, RefVar(MakeString("TMacintalkCodec")));
		SetFrameSlot(frame, RSSYMsamples, RefVar(Clone(sound)));
		SetFrameSlot(frame, RSSYMbuffersize, RefVar(MAKEINT(kSoundFrameBufferSize)));
		SetFrameSlot(frame, RSSYMbuffercount, RefVar(MAKEINT(kSoundFrameBufferCount)));
		SetFrameSlot(frame, RSSYMcompressiontype, RefVar(MAKEINT(kSoundFrameCompressionType)));
		SetFrameSlot(frame, RSSYMdatatype, RefVar(MAKEINT(kSoundFrameDataType)));
		SetFrameSlot(frame, RSSYMsamplingrate, RefVar(MAKEINT(kSoundFrameSamplingRate)));
		sound = frame;
	}
	if (IsBinary(sound))
	{
		RefVar frame(AllocateFrame());
		SetFrameSlot(frame, RSSYM_proto, RefVar(Rprotosoundframe));
		SetFrameSlot(frame, RSSYMsndframetype, RSSYMcodec);
		SetFrameSlot(frame, RSSYMcodecname, RefVar(SPrintObject(RefVar(ClassOf(sound)))));
		SetFrameSlot(frame, RSSYMsamples, sound);
		SetFrameSlot(frame, RSSYMbuffersize, RefVar(MAKEINT(kSoundFrameBufferSize)));
		SetFrameSlot(frame, RSSYMbuffercount, RefVar(MAKEINT(kSoundFrameBufferCount)));
		SetFrameSlot(frame, RSSYMcompressiontype, RefVar(MAKEINT(kSoundFrameCompressionType)));
		SetFrameSlot(frame, RSSYMdatatype, RefVar(MAKEINT(kSoundFrameDataType)));
		SetFrameSlot(frame, RSSYMsamplingrate, RefVar(MAKEINT(kSoundFrameSamplingRate)));
		sound = frame;
	}
	return sound;
}


void
RegisterSoundNatives(void)
{
	RegisterNativeFunction("FConvertToSoundFrame", (void*) FConvertToSoundFrame, 1);
	RegisterNativeFunction("FVolumeToDecibels", (void*) FVolumeToDecibels, 1);
	RegisterNativeFunction("FDecibelsToVolume", (void*) FDecibelsToVolume, 1);
	RegisterNativeFunction("FGetVolume", (void*) FGetVolume, 0);
	RegisterNativeFunction("FSetVolume", (void*) FSetVolume, 1);
	RegisterNativeFunction("FGetSystemVolume", (void*) FGetSystemVolume, 0);
	RegisterNativeFunction("FSetSystemVolume", (void*) FSetSystemVolume, 1);
}
