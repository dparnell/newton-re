/*
	File:		sound/SoundSettings.h

	Contains:	The volume, as a script sees it.

				The Newton has five volume settings, 0 to 4, and the sound
				server works in decibels - 16.16 fixed point, with
				0x80000000 standing for silence.  VolumeToDecibels and
				DecibelsToVolume are the table between them; the
				NewtonScript functions are thin wrappers over those and the
				global sound channel, handing decibels to and from a script
				as reals.

				The ROM's NewtonScript boot ends by reading the user
				configuration's soundVolumeDb and calling SetSystemVolume,
				which is why this is here.

	Reconstructed from the MP2x00 US ROM (0x001e6194-0x001e72e0); each
	function cites its origin.
*/

#ifndef __SOUNDSETTINGS_H
#define __SOUNDSETTINGS_H

#include "objects.h"

// what VolumeToDecibels answers for a volume of 0: no sound at all
const long kSilenceDecibels = (long) 0x80000000;

// The volume settings 0-4 as decibels, 16.16 fixed; anything above 4 is
// full volume and anything below 0 is silence.
long	VolumeToDecibels(long volume);
long	DecibelsToVolume(long decibels);

Ref		FVolumeToDecibels(RefArg rcvr, RefArg volume);
Ref		FDecibelsToVolume(RefArg rcvr, RefArg decibels);
Ref		FGetVolume(RefArg rcvr);
Ref		FSetVolume(RefArg rcvr, RefArg volume);
Ref		FGetSystemVolume(RefArg rcvr);
Ref		FSetSystemVolume(RefArg rcvr, RefArg decibels);
Ref		FConvertToSoundFrame(RefArg rcvr, RefArg obj);
Ref		FSoundPlayEnabled(RefArg rcvr, RefArg sound);
Ref		FPlaySoundIrregardless(RefArg rcvr, RefArg sound);
Ref		FPlaySoundSync(RefArg rcvr, RefArg sound);
Ref		FPlaySound(RefArg rcvr, RefArg sound);
Ref		FPlaySoundEffect(RefArg rcvr, RefArg sound, RefArg volume, RefArg kind);

// the two ROM sounds the pen makes, which go by the penSoundEffects
// preference rather than actionSoundEffects (the ROM compares the magic
// pointers)
enum { kClickSoundMagicPtr = 51, kPlonkSoundMagicPtr = 110 };

void	RegisterSoundNatives(void);

#endif	/* __SOUNDSETTINGS_H */
