/*
	File:		sound/SoundSettings.cpp

	Contains:	The volume as a script sees it (SoundSettings.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SoundSettings.h"
#include "SoundChannel.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"
#include "Locale.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Frames.h"


// decibels are 16.16 fixed point on the wire and reals in a script
static const double	kDecibelScale = 65536.0;


// ROM 0x001e6194 VolumeToDecibels__Fl
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


// ROM 0x001e6b3c DecibelsToVolume__Fl
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


// ROM 0x001e61fc FVolumeToDecibels
Ref
FVolumeToDecibels(RefArg /*rcvr*/, RefArg volume)
{
	return MakeReal(VolumeToDecibels(RINT(volume)) / kDecibelScale);
}


// ROM 0x001e701c FDecibelsToVolume
Ref
FDecibelsToVolume(RefArg /*rcvr*/, RefArg decibels)
{
	return MAKEINT(DecibelsToVolume((long) (CoerceToDouble(decibels) * kDecibelScale)));
}


// ROM 0x001e71e0 FGetVolume
Ref
FGetVolume(RefArg /*rcvr*/)
{
	return MAKEINT(DecibelsToVolume(GlobalSoundChannel()->GetVolume()));
}


// ROM 0x001e7200 FSetVolume
// nil is volume 0, silence.  ==> nil.
Ref
FSetVolume(RefArg /*rcvr*/, RefArg volume)
{
	long setting = ISNIL(volume) ? 0 : RINT(volume);
	GlobalSoundChannel()->SetVolume(VolumeToDecibels(setting));
	return NILREF;
}


// ROM 0x001e7250 FGetSystemVolume
Ref
FGetSystemVolume(RefArg /*rcvr*/)
{
	return MakeReal(GlobalSoundChannel()->GetVolume() / kDecibelScale);
}


// ROM 0x001e7288 FSetSystemVolume
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


// ROM 0x000d2034 ConvertToSoundFrame__FRC6RefVar (the native is 0x001e870c)
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


// ROM 0x001e72e0 FSoundPlayEnabled
// Whether this sound may be heard: the two click sounds the pen makes go
// by the penSoundEffects preference, everything else by
// actionSoundEffects.
Ref
FSoundPlayEnabled(RefArg /*rcvr*/, RefArg sound)
{
	RefVar preference;
	if (EQRef(sound, MAKEMAGICPTR(kClickSoundMagicPtr)) || EQRef(sound, MAKEMAGICPTR(kPlonkSoundMagicPtr)))
		preference = GetPreference(RefVar(RSSYMpensoundeffects));
	else
		preference = GetPreference(RefVar(RSSYMactionsoundeffects));
	return MAKEBOOLEAN(NOTNIL(preference));
}


// ROM 0x001e6248 FPlaySoundIrregardless
// The sound played whatever the preferences say: whatever is playing is
// stopped, this is scheduled in its place and started without waiting.
// ==> nil.
Ref
FPlaySoundIrregardless(RefArg /*rcvr*/, RefArg sound)
{
	if (ISNIL(sound))
		return NILREF;
	TUSoundChannel* channel = GlobalSoundChannel();
	if (channel == nil)
		return NILREF;
	NewtonErr err = channel->Stop(nil);
	if (err == noErr)
		err = channel->Schedule(sound);
	if (err == noErr)
		err = channel->Start(1);
	if (err != noErr)
		Throw(exFrames, (void*) (Long) err, nil);
	return NILREF;
}


// ROM 0x001e64cc FPlaySoundSync
// The same, but Start waits for the end.  ==> true.
Ref
FPlaySoundSync(RefArg /*rcvr*/, RefArg sound)
{
	if (ISNIL(sound))
		return TRUEREF;
	TUSoundChannel* channel = GlobalSoundChannel();
	if (channel == nil)
		return TRUEREF;
	NewtonErr err = channel->Stop(nil);
	if (err == noErr)
		err = channel->Schedule(sound);
	if (err == noErr)
		err = channel->Start(0);
	if (err != noErr)
		Throw(exFrames, (void*) (Long) err, nil);
	return TRUEREF;
}


// ROM 0x001e649c FPlaySound__FRC6RefVarT1
// The sound played if the preferences allow it.
Ref
FPlaySound(RefArg rcvr, RefArg sound)
{
	if (NOTNIL(FSoundPlayEnabled(rcvr, sound)))
		FPlaySoundIrregardless(rcvr, sound);
	return NILREF;
}


// ROM 0x001e62fc FPlaySoundEffect
// PlaySoundEffect(sound, volume, kind): the sound turned into a frame, at
// the volume given if there is one, played when the preference for its
// kind allows it - 'pen, 'alarm and 'action have preferences, and a kind
// that is none of those plays regardless.
Ref
FPlaySoundEffect(RefArg rcvr, RefArg sound, RefArg volume, RefArg kind)
{
	if (ISNIL(sound))
		return NILREF;
	RefVar frame(FConvertToSoundFrame(rcvr, sound));
	RefVar toPlay(frame);
	if (NOTNIL(volume))
	{
		toPlay = Clone(frame);
		SetFrameSlot(toPlay, RSSYMvolume, volume);
	}
	Boolean allowed = true;
	if (EQRef(kind, RSSYMpen))
		allowed = NOTNIL(GetPreference(RefVar(RSSYMpensoundeffects)));
	else if (EQRef(kind, RSSYMalarm))
		allowed = NOTNIL(GetPreference(RefVar(RSSYMalarmsoundeffects)));
	else if (EQRef(kind, RSSYMaction))
		allowed = NOTNIL(GetPreference(RefVar(RSSYMactionsoundeffects)));
	if (!allowed)
		return NILREF;
	return FPlaySoundIrregardless(rcvr, toPlay);
}


// ROM 0x001e6578 FClicker
// clicker() - the noise the pen makes when the caret moves, and the one
// place in the machine where a sound is a *tune*.  `vars._clickSong` is an
// array the clicks are taken from in turn - the ROM's own is _clickSong -
// and `vars._curClick` remembers how far through it the machine is, so
// successive clicks are successive notes and wrap round at the end.
//
// An entry is either a sound frame to play or an integer indexing the
// ROM's `clicks` array.  Nothing happens at all when the pen sound
// effects preference is off.  ==> nil.
static Ref
FClicker(RefArg /*rcvr*/)
{
	if (ISNIL(GetPreference(RefVar(RSSYMpensoundeffects))))
		return NILREF;
	RefVar current(GetFrameSlotRef(gVarFrame, RSSYM_curclick));
	long at = ISINT(current) ? RINT(current) : 0;
	RefVar song(GetFrameSlotRef(gVarFrame, RSSYM_clicksong));
	if (ISNIL(song))
		song = R_clicksong;
	long next = at + 1;
	if ((ULong) next >= (ULong) Length(song))
		next = 0;
	RefVar click(GetArraySlotRef(song, next));
	if (ISINT(click))
		FPlaySoundIrregardless(RefVar(NILREF), RefVar(GetArraySlotRef(RefVar(Rclicks), RINT(click))));
	else
		FPlaySoundIrregardless(RefVar(NILREF), click);
	RefVar globals(gVarFrame);
	SetFrameSlot(globals, RSSYM_curclick, RefVar(MAKEINT(next)));
	return NILREF;
}


void
RegisterSoundNatives(void)
{
	RegisterNativeFunction("FSoundPlayEnabled", (void*) FSoundPlayEnabled, 1);
	RegisterNativeFunction("FPlaySoundIrregardless", (void*) FPlaySoundIrregardless, 1);
	RegisterNativeFunction("FClicker", (void*) FClicker, 0);
	RegisterNativeFunction("FPlaySoundSync", (void*) FPlaySoundSync, 1);
	RegisterNativeFunction("FPlaySound__FRC6RefVarT1", (void*) FPlaySound, 1);
	RegisterNativeFunction("FPlaySoundEffect", (void*) FPlaySoundEffect, 3);
	RegisterNativeFunction("FConvertToSoundFrame", (void*) FConvertToSoundFrame, 1);
	RegisterNativeFunction("FVolumeToDecibels", (void*) FVolumeToDecibels, 1);
	RegisterNativeFunction("FDecibelsToVolume", (void*) FDecibelsToVolume, 1);
	RegisterNativeFunction("FGetVolume", (void*) FGetVolume, 0);
	RegisterNativeFunction("FSetVolume", (void*) FSetVolume, 1);
	RegisterNativeFunction("FGetSystemVolume", (void*) FGetSystemVolume, 0);
	RegisterNativeFunction("FSetSystemVolume", (void*) FSetSystemVolume, 1);
}
