/*
	File:		sound/SoundChannel.h

	Contains:	TUSoundChannel, the client side of the sound server
				(sound/SoundServer.h), and TFrameSoundChannel, the channel
				the NewtonScript sound functions play sound frames through.

				A task that wants to make a sound opens a channel - two of
				the server's channels in fact, an output channel and a
				decompressor feeding it - and talks to the server with
				immediate 'newt/'usnd events: start, pause, stop, set the
				volume.  A block of sound is scheduled with an asynchronous
				request (a SoundNode: the request, the reply and the
				message that carries them); the server answers it when the
				block has been played, and the answer comes back through
				the task's app world to AECompletionProc, which hands it to
				the block's TUSoundCallback.

				TFrameSoundChannel turns a sound frame into a SoundBlock
				(Convert: the samples locked, the coding, the rate, the
				start, count, loops and volume) and back again when it has
				been played (TFrameSoundCallback: the samples unlocked, the
				frame's callback method called with the state and error).
				GlobalSoundChannel is the one PlaySound and its relations
				use, made and opened the first time.

	NOT YET RECONSTRUCTED: coded sound (a frame whose sndFrameType is
	'codec: the codec is opened, but the server's codec channel does not
	decompress yet - TCodecChannel::InitNode).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include <stdint.h>

#ifndef __SOUNDCHANNEL_H
#define __SOUNDCHANNEL_H

#include "AEventHandler.h"
#include "UserPorts.h"
#include "NewtErrors.h"
#include "objects.h"
#include "SoundCodec.h"


// the sound server's events are 'newt'/'usnd'
const AEEventID	kSoundEventId = 0x75736e64;			// 'usnd'

// ERRBASE_SOUND - 10: what the ROM answers for a channel that is not open
const NewtonErr kSoundErrNotOpen = ERRBASE_SOUND - 10;

// the immediate commands the settings send (the rest are SoundServer.h's)
enum {
	kSoundSetVolume			= 0x0f,
	kSoundSetInputGain		= 0x10,
	kSoundSetOutputDevice	= 0x12,
	kSoundGetVolume			= 0x15
};

// An immediate command's reply: a TAEvent, the channel (the id an open
// answers), the error and a value (0x14 bytes; a pause or stop answers a
// TUSoundNodeReply, 0x20).
struct TUSoundReply					// 0x14 bytes
{
	TAEvent		fEvent;				// +0x00
	ULong		fChannel;			// +0x08
	long		fError;				// +0x0c
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
	int32_t		fOutputDevices;			// +0x10  the outputs there are: 1 the speaker, 8 line-out (the Sound preferences' "Play using" picker)
};

// DEVIATION: the ROM's sound driver registers the block above at boot;
// the host's driver (hal/host/HostSoundDriver.h) does not, so the host
// registers one of its own with what the reconstruction already knows
// (sound/SoundSettings.h): five settings, 0 to 4, spanning the 18.0618
// decibels between silence-but-audible and full, and a server that is
// not asked the volume.  Without it the Extras drawer, which asks for
// this to size its volume slider, gets nothing and cannot open.
void	RegisterHostVolumeInfo(void);

// fFlags (+0x14)
enum {
	kSoundChannelOutput			= 0x01,		// opened for output
	kSoundChannelInput			= 0x02,		// opened for input
	kSoundChannelRunning		= 0x04,		// started without waiting
	kSoundChannelPaused			= 0x08,
	kSoundChannelAskForVolume	= 0x80		// the gestalt says the server answers GetVolume
};


struct SoundBlock;
struct TUSoundNodeRequest;
struct TUSoundNodeReply;


// What a scheduled block's end is told: Complete(block, state, error).
class TUSoundCallback
{
public:
					TUSoundCallback();								// ROM 0x0025abec __ct__15TUSoundCallbackFv
	virtual			~TUSoundCallback();								// ROM 0x0025ac20 __dt__15TUSoundCallbackFv
	virtual void	Complete(SoundBlock* block, int state, long error) = 0;
};

// ...by calling a function.
typedef void (*SoundCallbackProc)(SoundBlock* block, int state, long error);

class TUSoundCallbackProc : public TUSoundCallback
{
public:
					TUSoundCallbackProc();							// ROM 0x0025ac38 __ct__19TUSoundCallbackProcFv
	virtual			~TUSoundCallbackProc();							// ROM 0x0025acfc __dt__19TUSoundCallbackProcFv
	void			SetCallback(SoundCallbackProc proc);			// ROM 0x0025ad3c SetCallback__19TUSoundCallbackProcFPFP10SoundBlockil_v
	virtual void	Complete(SoundBlock* block, int state, long error);	// ROM 0x0025ad44 Complete__19TUSoundCallbackProcFP10SoundBlockil

	SoundCallbackProc	fProc;			// +0x04
};


struct SoundNode;


class TUSoundChannel : public TAEventHandler
{
public:
					TUSoundChannel();
	virtual			~TUSoundChannel();

	NewtonErr		Open(int input, int device);
	NewtonErr		Close(void);

	// the settings the channel keeps, and tells the server about
	long			SetVolume(long decibels);		// ==> what the volume ended up as
	long			GetVolume(void);
	void			SetInputGain(long gain);
	long			GetInputGain(void);
	void			SetOutputDevice(long device);

	// playing
	NewtonErr		Schedule(SoundBlock* block, TUSoundCallback* callback);
	NewtonErr		Cancel(ULong refCon);
	NewtonErr		Start(int async);				// 0: answer when everything has been played
	NewtonErr		Pause(SoundBlock* block, long* samplesPlayed);
	NewtonErr		Stop(SoundBlock* block, long* samplesPlayed);

	// one immediate message to the sound server
	NewtonErr		SendImmediate(ULong command, ULong channelId, ULong value,
								  TUSoundReply* reply, ULong replySize);

	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);

	NewtonErr		MakeNode(SoundNode** node);
	void			FreeNode(SoundNode* node);
	SoundNode*		FindNode(ULong id);
	SoundNode*		FindRefCon(ULong refCon);
	ULong			UniqueId(void);
	void			AbortBusy(void);

	ULong			fFlags;				// +0x14
	ULong			fChannelId;			// +0x18  the output (or input) channel; 0 until open
	ULong			fCodecChannelId;	// +0x1c  the decompressor (or compressor) feeding it
	ULong			fLastNodeId;		// +0x20
	SoundNode*		fBusyNodes;			// +0x24  scheduled and not yet answered
	SoundNode*		fFreeNodes;			// +0x28  one kept for the next schedule
	long			fVolume;			// +0x2c  decibels, 16.16 fixed (0x7fffffff: never set)
	long			fInputGain;			// +0x30
	ULong			fUnknown34;			// +0x34
	long			fOutputDevice;		// +0x38
};										// 0x3c bytes


// the sound server's port (0: no server - the host with no sound driver)
extern TObjectId		gSndPort;			// ROM 0x0c101b10 gSndPort
// the channel the NewtonScript sound functions use (FrameSoundChannel.h's
// GlobalSoundChannel makes it)
extern TUSoundChannel*	gSoundChannel;		// ROM 0x0c101b08 gSoundChannel

// Everything playing stopped (a package going: its sounds are its bytes).
void	StopFrameSound(void);

#endif	/* __SOUNDCHANNEL_H */
