/*
	File:		sound/FrameSoundChannel.h

	Contains:	TFrameSoundChannel - a TUSoundChannel that plays NewtonScript
				sound frames - and the channel the sound functions share
				(GlobalSoundChannel), and the protoSoundChannel natives
				(RegisterSoundChannelNatives).

				A frame is turned into a SoundBlock by Convert: the
				samples binary locked and pointed at, its coding
				(compressionType: 0 offset-binary 8-bit, 1 mu-law, 6
				16-bit linear), its sample size (dataType), its rate
				(samplingRate: an integer, a real, or a binary holding a
				16.16 number), where to start, how many to play, how many
				times more, and the volume; the block's refCon holds the
				frame.  When the block has been played the frame's
				callback method is sent the state and the error
				(TFrameSoundCallback).

	Coded sound goes through the server's codec channel (SoundServer.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __FRAMESOUNDCHANNEL_H
#define __FRAMESOUNDCHANNEL_H

#include "SoundChannel.h"

class TFrameSoundChannel;

class TFrameSoundCallback : public TUSoundCallback
{
public:
					TFrameSoundCallback();					// ROM 0x000d1ea0 __ct__19TFrameSoundCallbackFv
	virtual			~TFrameSoundCallback();					// ROM 0x000d1ee8 __dt__19TFrameSoundCallbackFv
	virtual void	Complete(SoundBlock* block, int state, long error);

	TFrameSoundChannel*	fChannel;		// +0x04
};


class TFrameSoundChannel : public TUSoundChannel
{
public:
					TFrameSoundChannel();
	virtual			~TFrameSoundChannel();

	NewtonErr		Open(int input, int device);
	NewtonErr		Close(void);
	NewtonErr		Schedule(RefArg sound);
	void			Convert(RefArg sound, SoundBlock* block);
	NewtonErr		OpenCodec(RefArg frame, SoundBlock* block);
	NewtonErr		InitCodec(SoundBlock* block);
	NewtonErr		DeleteCodec(SoundBlock* block);

	TSoundCodec*		fCodec;			// +0x3c  the channel's own codec (fCodecName's)
	CodecBlock			fCodecBlock;	// +0x40  what it was set up with
	Boolean				fCodecInited;	// +0x5c
	RefStruct			fCodecName;		// +0x60
	TFrameSoundCallback	fCallback;		// +0x64
};										// 0x6c bytes


// ROM 0x001e70b4 GlobalSoundChannel__Fv
TFrameSoundChannel*	GlobalSoundChannel(void);

void	ConvertCodecBlock(SoundBlock* from, CodecBlock* to);		// ROM 0x001e5bec ConvertCodecBlock__FP10SoundBlockP10CodecBlock
void	ConvertCodecBlock(CodecBlock* from, SoundBlock* to);		// ROM 0x001e5c28 ConvertCodecBlock__FP10CodecBlockP10SoundBlock

void	RegisterSoundChannelNatives(void);

#endif	/* __FRAMESOUNDCHANNEL_H */
