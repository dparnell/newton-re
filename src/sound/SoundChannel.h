/*
	File:		sound/SoundChannel.h

	Contains:	TUSoundChannel, the client side of the sound server.

				A task that wants to make a sound opens a channel on the
				sound server's port and then talks to it with immediate
				'newt/'usnd events: schedule this block, start, pause, stop,
				set the volume.  The channel keeps the settings itself as
				well as telling the server, so it can answer them without a
				round trip - which is what makes it useful on a host that
				has no sound server at all.

				GlobalSoundChannel is the one the NewtonScript sound
				functions use (sound/SoundSettings.h); the ROM makes it on
				demand, as a TFrameSoundChannel (the subclass that plays
				NewtonScript sound frames) and opens it for output.

	NOT YET RECONSTRUCTED: nearly all of it.  The sound server
	(TSoundServer/TSoundChannel), the driver behind it, the scheduling
	(SoundBlock, SoundNode, Schedule/Start/Pause/Stop), the callbacks and
	TFrameSoundChannel are not here; what is here is the channel's own
	state - the volume, the input gain and the output device - and the
	message that would carry it to the server, because the boot's
	SetSystemVolume needs those and nothing else.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include <stdint.h>

#ifndef __SOUNDCHANNEL_H
#define __SOUNDCHANNEL_H

#include "AEventHandler.h"
#include "UserPorts.h"
#include "NewtErrors.h"
#include "objects.h"


// the sound server's events are 'newt'/'usnd'
const AEEventID	kSoundEventId = 0x75736e64;			// 'usnd'

// the commands SendImmediate carries (only the ones used here are named)
// ERRBASE_SOUND - 10: what the ROM answers for a channel that is not open
// or has nothing scheduled
const NewtonErr kSoundErrNotOpen = ERRBASE_SOUND - 10;

enum {
	kSoundSetVolume			= 0x0f,
	kSoundSetInputGain		= 0x10,
	kSoundSetOutputDevice	= 0x12,
	kSoundGetVolume			= 0x15
};

// The block a channel sends and the server answers in: a TAEvent and three
// longs.  The answer to a get is the last of them.
struct TUSoundReply					// 0x14 bytes
{
	TAEvent		fEvent;				// +0x00
	ULong		fCommand;			// +0x08
	ULong		fChannelId;			// +0x0c
	long		fValue;				// +0x10
};

// kGestalt_Ext_VolumeInfo's parameter block, which the ROM's sound
// driver registers (PCirrusSoundDriver::New 0x00059a9c) and both the
// sound channel and the Extras drawer read.  Its shape is the template
// the drawer asks for it with - ['struct, 'boolean, 'boolean, 'boolean,
// 'boolean, 'Real, 'long, 'long] - so four flag bytes, a double and two
// longs, twenty bytes in all.
struct TGestaltVolumeInfo			// 0x14 bytes
{
	UByte		fUnknown00;
	UByte		fUnknown01;
	Boolean		fServerAnswersVolume;	// +0x02  the server answers GetVolume
	UByte		fUnknown03;
	// +0x04  a double, held as two 32-bit words: a double of its own would
	// be eight-aligned here, where the ARM puts it on a four-byte
	// boundary, and ULong is pointer-sized in this reconstruction - a
	// parameter block has to be laid out in the widths the machine uses
	uint32_t	fDecibelRange[2];
	int32_t		fHighestSetting;		// +0x0c  the loudest setting's number
	int32_t		fUnknown10;				// +0x10
};

// DEVIATION: the ROM's sound driver registers the block above at boot;
// the host has no sound hardware and no driver, so it registers one of
// its own with what the reconstruction already knows (sound/
// SoundSettings.h): five settings, 0 to 4, spanning the 18.0618 decibels
// between silence-but-audible and full, and no sound server to answer
// the volume.  Without it the Extras drawer, which asks for this to size
// its volume slider, gets nothing and cannot open.
void	RegisterHostVolumeInfo(void);

// fFlags (+0x14)
enum {
	kSoundChannelOutput			= 0x01,		// opened for output
	kSoundChannelAskForVolume	= 0x80		// the gestalt says the server answers GetVolume
};


class TUSoundChannel : public TAEventHandler
{
public:
					TUSoundChannel();

	// the settings the channel keeps, and tells the server about
	long			SetVolume(long decibels);		// ==> what the volume ended up as
	long			GetVolume(void);
	void			SetInputGain(long gain);
	long			GetInputGain(void);
	void			SetOutputDevice(long device);

	// playing
	NewtonErr		Schedule(RefArg sound);
	NewtonErr		Start(int wait);
	NewtonErr		Stop(long* samplesPlayed);

	// one immediate message to the sound server
	NewtonErr		SendImmediate(ULong command, ULong channelId, ULong value,
								  TUSoundReply* reply, ULong replySize);

	ULong			fFlags;				// +0x14
	ULong			fChannelId;			// +0x18  0 until the channel is open
	ULong			fUnknown1C;			// +0x1c
	ULong			fUnknown20;			// +0x20
	ULong			fUnknown24;			// +0x24  not zero once the channel has been opened: Set/GetVolume then ask the server
	ULong			fUnknown28;			// +0x28
	long			fVolume;			// +0x2c  decibels, 16.16 fixed (0x7fffffff: never set)
	long			fInputGain;			// +0x30
	ULong			fUnknown34;			// +0x34
	long			fOutputDevice;		// +0x38
};										// 0x3c bytes


// the sound server's port (NOT YET RECONSTRUCTED: never opened)
extern TObjectId		gSndPort;			// ROM 0x0c101b10 gSndPort
extern TUSoundChannel*	gSoundChannel;		// ROM 0x0c101b08 gSoundChannel

TUSoundChannel*	GlobalSoundChannel(void);

#endif	/* __SOUNDCHANNEL_H */
