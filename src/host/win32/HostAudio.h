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
				The microphone (HostMicrophone*) is the same over waveIn.
*/

#ifndef __HOSTAUDIO_H
#define __HOSTAUDIO_H

// (no Newton headers here: the Windows headers and they do not mix)
bool	HostAudioOpen(long sampleRate);						// the device opened for 16-bit mono at the rate; ==> whether it could be
void	HostAudioPlay(const short* samples, long count);	// the samples (host byte order) queued behind what is playing
void	HostAudioClose(void);								// what is queued let finish, and the device closed

// The host's microphone: 16-bit mono samples from the Windows waveIn
// device, captured all the time it is open into a ring of buffers.  The
// host sound driver's record backend (HostMicrophoneRecord) takes what has
// arrived; what has not arrived yet is silence, so it never waits.
bool	HostMicrophoneOpen(long sampleRate);				// ==> whether there is a microphone
void	HostMicrophoneRecord(short* samples, long count);	// the oldest samples not yet taken (host byte order)
void	HostMicrophoneClose(void);

#endif	/* __HOSTAUDIO_H */
