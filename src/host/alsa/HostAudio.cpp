/*
	File:		host/alsa/HostAudio.cpp

	Contains:	The host's loudspeaker and microphone over ALSA (HostAudio.h),
				which is what a Linux host plays and records through.  The
				Windows ones are host/win32/HostAudio.cpp, over waveOut and
				waveIn, and answer the same six calls.

	Both devices are opened non-blocking, because HostAudioPlay is called
	from the sound server's own task as a DMA buffer starts and must come
	back at once: what will not fit in the device's buffer is dropped
	rather than waited for, as the Windows ring does when it is full.  A
	buffer lasts as long on the system clock as it does on the device, so
	in practice the queue neither runs far ahead nor dry.

	The microphone is read the other way about: whatever has arrived is
	taken into a ring whenever the driver asks for samples, and what has
	not arrived is silence, so a record never waits either.
*/

#include "HostAudio.h"

#include <alsa/asoundlib.h>
#include <string.h>

#define nil 0			// (no Newton headers here, as in the Windows one)

// the device's buffer: a fifth of a second, so a late buffer does not
// break the sound, worked in periods of a fortieth
static const unsigned int	kBufferMicroseconds = 200 * 1000;
static const unsigned int	kPeriodMicroseconds = 25 * 1000;


/*------------------------------------------------------------------------------
	A PCM opened for 16-bit mono at the rate, non-blocking.  ==> nil if there
	is no such device (no sound card, or it is another program's).
------------------------------------------------------------------------------*/

static snd_pcm_t*
OpenPCM(long sampleRate, snd_pcm_stream_t direction)
{
	snd_pcm_t* pcm = nil;
	if (snd_pcm_open(&pcm, "default", direction, SND_PCM_NONBLOCK) < 0)
		return nil;
	snd_pcm_hw_params_t* params = nil;
	snd_pcm_hw_params_malloc(&params);
	if (params == nil)
	{
		snd_pcm_close(pcm);
		return nil;
	}
	unsigned int rate = (unsigned int) sampleRate;
	unsigned int buffer = kBufferMicroseconds;
	unsigned int period = kPeriodMicroseconds;
	int err = snd_pcm_hw_params_any(pcm, params);
	if (err >= 0)
		err = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err >= 0)
		err = snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16);	// the host's own byte order
	if (err >= 0)
		err = snd_pcm_hw_params_set_channels(pcm, params, 1);
	if (err >= 0)
		err = snd_pcm_hw_params_set_rate_near(pcm, params, &rate, nil);
	if (err >= 0)
		err = snd_pcm_hw_params_set_buffer_time_near(pcm, params, &buffer, nil);
	if (err >= 0)
		err = snd_pcm_hw_params_set_period_time_near(pcm, params, &period, nil);
	if (err >= 0)
		err = snd_pcm_hw_params(pcm, params);
	snd_pcm_hw_params_free(params);
	if (err < 0)
	{
		snd_pcm_close(pcm);
		return nil;
	}
	snd_pcm_prepare(pcm);
	return pcm;
}


/*------------------------------------------------------------------------------
	The loudspeaker.
------------------------------------------------------------------------------*/

static snd_pcm_t*	gOut = nil;


bool
HostAudioOpen(long sampleRate)
{
	if (gOut != nil)
		return true;
	gOut = OpenPCM(sampleRate, SND_PCM_STREAM_PLAYBACK);
	return gOut != nil;
}


void
HostAudioPlay(const short* samples, long count)
{
	if (gOut == nil || count <= 0)
		return;
	// an underrun (the sound stopped while nothing was being played) leaves
	// the device needing to be prepared again before it will take anything
	snd_pcm_state_t state = snd_pcm_state(gOut);
	if (state == SND_PCM_STATE_XRUN || state == SND_PCM_STATE_SUSPENDED)
	{
		if (snd_pcm_recover(gOut, -EPIPE, 1) < 0)
			return;
	}
	snd_pcm_sframes_t wrote = snd_pcm_writei(gOut, samples, (snd_pcm_uframes_t) count);
	if (wrote == -EPIPE || wrote == -ESTRPIPE)
	{
		if (snd_pcm_recover(gOut, (int) wrote, 1) >= 0)
			snd_pcm_writei(gOut, samples, (snd_pcm_uframes_t) count);
	}
	// -EAGAIN, or a short write: the device's buffer is full and the rest
	// is dropped, which is what the Windows ring does when it is short
}


void
HostAudioClose(void)
{
	if (gOut == nil)
		return;
	snd_pcm_nonblock(gOut, 0);			// so that what is queued is let finish
	snd_pcm_drain(gOut);
	snd_pcm_close(gOut);
	gOut = nil;
}


/*------------------------------------------------------------------------------
	The microphone: what has arrived kept in a ring, the oldest handed out.
------------------------------------------------------------------------------*/

static const long	kInRing = 1 << 16;

static snd_pcm_t*	gIn = nil;
static short		gInRing[kInRing];
static long			gInHead = 0;			// the ring's oldest sample
static long			gInTail = 0;			// where the next goes


bool
HostMicrophoneOpen(long sampleRate)
{
	if (gIn != nil)
		return true;
	gIn = OpenPCM(sampleRate, SND_PCM_STREAM_CAPTURE);
	if (gIn == nil)
		return false;
	gInHead = gInTail = 0;
	snd_pcm_start(gIn);
	return true;
}


void
HostMicrophoneRecord(short* samples, long count)
{
	if (gIn != nil)
	{
		// everything the device has captured since last time into the ring
		for (;;)
		{
			short block[1024];
			snd_pcm_sframes_t read = snd_pcm_readi(gIn, block, sizeof(block) / sizeof(block[0]));
			if (read == -EPIPE || read == -ESTRPIPE)
			{
				if (snd_pcm_recover(gIn, (int) read, 1) < 0)
					break;
				continue;
			}
			if (read <= 0)
				break;
			for (snd_pcm_sframes_t i = 0; i < read; i++)
			{
				gInRing[gInTail] = block[i];
				gInTail = (gInTail + 1) & (kInRing - 1);
				if (gInTail == gInHead)
					gInHead = (gInHead + 1) & (kInRing - 1);		// the oldest overwritten
			}
		}
	}
	for (long i = 0; i < count; i++)
	{
		if (gInHead != gInTail)
		{
			samples[i] = gInRing[gInHead];
			gInHead = (gInHead + 1) & (kInRing - 1);
		}
		else
			samples[i] = 0;
	}
}


void
HostMicrophoneClose(void)
{
	if (gIn == nil)
		return;
	snd_pcm_close(gIn);
	gIn = nil;
}
