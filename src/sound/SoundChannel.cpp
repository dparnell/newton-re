/*
	File:		sound/SoundChannel.cpp

	Contains:	TUSoundChannel's settings and the global channel
				(SoundChannel.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "SoundChannel.h"
#include "NewtonGestalt.h"
#include "UserGlobals.h"
#include "OSErrors.h"
#include "NewtonMemory.h"

#include <string.h>


TObjectId		gSndPort = 0;			// ROM 0x0c101cec
TUSoundChannel*	gSoundChannel = nil;	// ROM 0x0c101ce4


// ROM 0x0025886c __ct__14TUSoundChannelFv
// Nothing scheduled, no channel open, the volume "never set"
// (0x7fffffff) and the input gain at 0x80; the gestalt says whether the
// server can be asked for the volume, which is what GetVolume goes on.
TUSoundChannel::TUSoundChannel()
{
	fFlags = 0;
	fChannelId = 0;
	fUnknown1C = 0;
	fUnknown20 = 0;
	fUnknown24 = 0;
	fUnknown28 = 0;
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


// ROM 0x002591c4 SendImmediate__14TUSoundChannelFUlN21P12TUSoundReplyT1
// One 'newt/'usnd event carrying {command, channel, value}, sent to the
// sound server's port and answered on the spot.
//
// NOT YET RECONSTRUCTED: TSoundServer, so gSndPort is never opened.  The
// ROM would send to port 0 and let the kernel answer; the host says so
// here instead, because this is reached from the NewtonScript boot before
// there is an OS to ask.  Every caller falls back on the value the
// channel keeps itself, which is why the volume a script sets is the
// volume it reads back.
NewtonErr
TUSoundChannel::SendImmediate(ULong command, ULong channelId, ULong value,
							  TUSoundReply* /*reply*/, ULong /*replySize*/)
{
	if (gSndPort == 0)
		return kError_Bad_ObjectId;
	(void) command; (void) channelId; (void) value;
	return kError_Bad_ObjectId;
}


// ROM 0x00258e34 SetVolume__14TUSoundChannelFl
// The volume in decibels, 16.16 fixed.  The channel keeps it whatever
// happens; an open channel - or the global one, which the sound functions
// all go through - also tells the server, and answers what the server made
// of it.
long
TUSoundChannel::SetVolume(long decibels)
{
	fVolume = decibels;
	if (fUnknown24 != 0 || this == gSoundChannel)
	{
		TUSoundReply reply;
		reply.fEvent.fAEventClass = kNewtEventClass;
		reply.fEvent.fAEventID = kSoundEventId;
		reply.fCommand = 0;
		reply.fChannelId = 0;
		reply.fValue = 0;
		if (SendImmediate(kSoundSetVolume, 0, decibels, &reply, sizeof(reply)) == noErr)
			decibels = reply.fValue;
	}
	return decibels;
}


// ROM 0x00258edc GetVolume__14TUSoundChannelFv
// The server is only asked when the gestalt said it would answer.
long
TUSoundChannel::GetVolume(void)
{
	long decibels = fVolume;
	if ((fUnknown24 != 0 || this == gSoundChannel) && (fFlags & kSoundChannelAskForVolume) != 0)
	{
		TUSoundReply reply;
		reply.fEvent.fAEventClass = kNewtEventClass;
		reply.fEvent.fAEventID = kSoundEventId;
		reply.fCommand = 0;
		reply.fChannelId = 0;
		reply.fValue = 0;
		if (SendImmediate(kSoundGetVolume, 0, 0, &reply, sizeof(reply)) == noErr)
			decibels = reply.fValue;
	}
	return decibels;
}


// ROM 0x00258f88 SetInputGain__14TUSoundChannelFl
void
TUSoundChannel::SetInputGain(long gain)
{
	fInputGain = gain;
	if (fChannelId == 0)
		return;
	TUSoundReply reply;
	reply.fEvent.fAEventClass = kNewtEventClass;
	reply.fEvent.fAEventID = kSoundEventId;
	reply.fCommand = 0;
	reply.fChannelId = 0;
	reply.fValue = 0;
	SendImmediate(kSoundSetInputGain, fChannelId, gain, &reply, sizeof(reply));
}


// ROM 0x00259004 GetInputGain__14TUSoundChannelFv
// The channel's own; the server is never asked.
long
TUSoundChannel::GetInputGain(void)
{
	return fInputGain;
}


// ROM 0x00259148 SetOutputDevice__14TUSoundChannelFl
void
TUSoundChannel::SetOutputDevice(long device)
{
	fOutputDevice = device;
	if (fChannelId == 0)
		return;
	TUSoundReply reply;
	reply.fEvent.fAEventClass = kNewtEventClass;
	reply.fEvent.fAEventID = kSoundEventId;
	reply.fCommand = 0;
	reply.fChannelId = 0;
	reply.fValue = 0;
	SendImmediate(kSoundSetOutputDevice, fChannelId, device, &reply, sizeof(reply));
}


// ROM 0x001e94cc GlobalSoundChannel__Fv
// The channel the NewtonScript sound functions play through, made the
// first time one of them is called.
//
// DEVIATION: the ROM makes a TFrameSoundChannel and opens it for output on
// vars.userConfiguration.outputDevice, throwing evt.ex.fr if that fails -
// and with no sound server it does fail, TUSoundChannel::Open answering
// ERRBASE_SOUND when gSndPort is 0.  TFrameSoundChannel and the server are
// NOT YET RECONSTRUCTED, so the host makes a plain, unopened channel: it
// keeps the volume it is told and answers it, and plays nothing.
TUSoundChannel*
GlobalSoundChannel(void)
{
	if (gSoundChannel == nil)
		gSoundChannel = new TUSoundChannel;
	return gSoundChannel;
}
