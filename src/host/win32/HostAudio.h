/*
	File:		host/win32/HostAudio.h

	Contains:	The host's loudspeaker: 16-bit mono samples handed to the
				Windows waveOut device, which plays them one after another
				as they are queued.  The host's sound driver
				(hal/host/HostSoundDriver.h) is given HostAudioPlay as its
				backend when newton runs with a window; it hands over each
				DMA buffer as the buffer starts, and a buffer lasts as long
				on the system clock as it does on the device, so the queue
				never runs far ahead or dry.  Playing copies the samples
				and returns at once - it never blocks, and never calls into
				the OS.  Only the Windows build has a device; elsewhere
				HostAudioOpen answers false and the null backend is used.
*/

#ifndef __HOSTAUDIO_H
#define __HOSTAUDIO_H

// (no Newton headers here: the Windows headers and they do not mix)
bool	HostAudioOpen(long sampleRate);						// the device opened for 16-bit mono at the rate; ==> whether it could be
void	HostAudioPlay(const short* samples, long count);	// the samples (host byte order) queued behind what is playing
void	HostAudioClose(void);								// what is queued let finish, and the device closed

#endif	/* __HOSTAUDIO_H */
