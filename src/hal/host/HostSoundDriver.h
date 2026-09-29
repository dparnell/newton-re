/*
	File:		hal/host/HostSoundDriver.h

	Contains:	PMainSoundDriver: the host's sound hardware, the PSoundDriver
				(sound/SoundDriver.h) the sound server finds first.

				It has output and input ("SoundOutput", "SoundInput"), 16-bit linear samples at
				21600 a second - the MP2x00's top rate, so ROM sounds are
				resampled exactly as they were on the machine.  The server's
				two DMA buffers are played in the order they are scheduled;
				the end of each is an interrupt (hal/host/HostInterruptSources.h)
				falling due at the time the buffer would have finished
				playing - start + samples/rate on the system clock - and
				delivered, as the hardware's is, to the server's callback
				through OutputIntHandlerDispatcher.  On the controllable
				clock that makes playback deterministic.

				Where the samples go is a backend's business: the null
				backend (the default) keeps every buffer that was played, for
				a test to read (HostSoundCaptured); a backend that makes a
				noise (a Win32 waveOut one, NOT YET) is given each buffer as
				it starts.  A backend is called on the task that holds the
				baton and must not block; it never calls into the OS.

	Nothing in the ROM corresponds: its driver is PCirrusSoundDriver.
*/

#ifndef __HAL_HOST_SOUNDDRIVER_H
#define __HAL_HOST_SOUNDDRIVER_H

#ifndef __SOUNDDRIVER_H
#include "SoundDriver.h"
#endif

class PMainSoundDriver : public PSoundDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PMainSoundDriver);
	PMainSoundDriver*	New();
	void			Delete();

	NewtonErr	SetSoundHardwareInfo(const TSoundDriverInfo* info);
	NewtonErr	GetSoundHardwareInfo(TSoundDriverInfo* info);
	NewtonErr	SetOutputBuffers(VAddr buffer1, ULong size1, VAddr buffer2, ULong size2);
	NewtonErr	SetInputBuffers(VAddr buffer1, ULong size1, VAddr buffer2, ULong size2);
	NewtonErr	ScheduleOutputBuffer(ULong which, ULong size);
	NewtonErr	ScheduleInputBuffer(ULong which, ULong size);
	void		PowerOutputOn(long device);
	void		PowerOutputOff(void);
	void		PowerInputOn(long device);
	void		PowerInputOff(void);
	NewtonErr	StartOutput(void);
	NewtonErr	StartInput(void);
	NewtonErr	StopOutput(void);
	NewtonErr	StopInput(void);
	Boolean		OutputIsEnabled(void);
	Boolean		InputIsEnabled(void);
	Boolean		OutputIsRunning(void);
	Boolean		InputIsRunning(void);
	VAddr		CurrentOutputPtr(void);
	VAddr		CurrentInputPtr(void);
	void		OutputVolume(long decibels);
	long		OutputVolume(void);
	void		InputVolume(long gain);
	long		InputVolume(void);
	void		EnableExtSoundSource(long source);
	void		DisableExtSoundSource(long source);
	long		OutputIntHandler(void);
	long		InputIntHandler(void);

	void		StartPlaying(void);
	void		StartRecording(void);

	VAddr		fBuffer[2];
	ULong		fBufferSize[2];
	long		fQueue[2];			// the buffers scheduled, in order (-1: none)
	ULong		fQueueSize[2];		// and how many bytes of each
	long		fQueued;
	Boolean		fRunning;
	Boolean		fPowered;
	Boolean		fPlaying;			// fQueue[0] is playing, and ends at fEnd
	Int64		fEnd;
	long		fVolume;			// 16.16 decibels

	VAddr		fInBuffer[2];		// the input's, as the output's
	ULong		fInBufferSize[2];
	long		fInQueue[2];
	ULong		fInQueueSize[2];
	long		fInQueued;
	Boolean		fInRunning;
	Boolean		fInPowered;
	Boolean		fRecording;			// fInQueue[0] is filling, and is full at fInEnd
	Int64		fInEnd;
	long		fInGain;
	short*		fPlay;				// a buffer in the host's byte order, for the backend
	long		fPlaySize;
};


// Where the played samples go.  `play` is handed each buffer as it starts
// (16-bit samples in the host's byte order, 21600 a second: the driver
// swaps them from the big-endian the sound server keeps them in), and
// `record` fills a buffer the same way round.
struct HostSoundBackend
{
	void	(*play)(const short* samples, long count);
	void	(*record)(short* samples, long count);		// nil: silence
};

// The host program's side: the driver registered for InitializeSound to
// find (sets gHostRegisterSoundDriver - call before the OS boots), and the
// backend (nil: the null one).
void	HostInstallSoundDriver(const HostSoundBackend* backend);

// The null backend's capture: every sample played since the last clear.
const short*	HostSoundCaptured(long* count);
void			HostSoundClearCapture(void);

// The null backend's microphone: these samples, then silence (the array
// must outlast the recording).
void			HostSoundSetSource(const short* samples, long count);

extern const long	kHostSoundRate;		// 21600

#endif	/* __HAL_HOST_SOUNDDRIVER_H */
