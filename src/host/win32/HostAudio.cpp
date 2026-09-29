/*
	File:		host/win32/HostAudio.cpp

	Contains:	The host's loudspeaker over waveOut (HostAudio.h).

	A ring of sixteen wave headers, each with a buffer of its own: a play
	takes the next header the device has finished with (or never had),
	copies the samples in and queues it.  The server's buffers are at most
	0x750 samples and there are never more than two outstanding, so the
	ring is never short; if it were, the samples would be dropped rather
	than waited for.
*/

#include "HostAudio.h"

#ifdef _WIN32

#include <windows.h>
#include <mmsystem.h>
#include <string.h>

static const int	kHeaders = 16;
static const long	kHeaderSamples = 4096;

static HWAVEOUT		gDevice = NULL;
static WAVEHDR		gHeaders[kHeaders];
static short		gBuffers[kHeaders][kHeaderSamples];
static int			gNext = 0;


bool
HostAudioOpen(long sampleRate)
{
	if (gDevice != NULL)
		return true;
	WAVEFORMATEX format;
	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 1;
	format.nSamplesPerSec = (DWORD) sampleRate;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 2;
	format.nAvgBytesPerSec = (DWORD) sampleRate * 2;
	if (waveOutOpen(&gDevice, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
	{
		gDevice = NULL;
		return false;
	}
	memset(gHeaders, 0, sizeof(gHeaders));
	gNext = 0;
	return true;
}


void
HostAudioPlay(const short* samples, long count)
{
	if (gDevice == NULL || count <= 0)
		return;
	if (count > kHeaderSamples)
		count = kHeaderSamples;
	WAVEHDR* header = &gHeaders[gNext];
	if ((header->dwFlags & WHDR_PREPARED) != 0)
	{
		if ((header->dwFlags & WHDR_DONE) == 0)
			return;					// the ring is full: dropped
		waveOutUnprepareHeader(gDevice, header, sizeof(WAVEHDR));
	}
	memcpy(gBuffers[gNext], samples, count * sizeof(short));
	memset(header, 0, sizeof(WAVEHDR));
	header->lpData = (LPSTR) gBuffers[gNext];
	header->dwBufferLength = (DWORD) (count * sizeof(short));
	if (waveOutPrepareHeader(gDevice, header, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
		waveOutWrite(gDevice, header, sizeof(WAVEHDR));
	gNext = (gNext + 1) % kHeaders;
}


void
HostAudioClose(void)
{
	if (gDevice == NULL)
		return;
	for (int i = 0; i < kHeaders; i++)
		while ((gHeaders[i].dwFlags & WHDR_PREPARED) != 0 && (gHeaders[i].dwFlags & WHDR_DONE) == 0)
			Sleep(10);
	waveOutReset(gDevice);
	for (int i = 0; i < kHeaders; i++)
		if ((gHeaders[i].dwFlags & WHDR_PREPARED) != 0)
			waveOutUnprepareHeader(gDevice, &gHeaders[i], sizeof(WAVEHDR));
	waveOutClose(gDevice);
	gDevice = NULL;
}

/*------------------------------------------------------------------------------
	The microphone: eight buffers of a tenth of a second kept queued on the
	waveIn device; a record takes the samples of the buffers the device has
	filled, in order, into a ring, and hands out the oldest.
------------------------------------------------------------------------------*/

static const int	kInHeaders = 8;
static const long	kInHeaderSamples = 2160;
static const long	kInRing = 1 << 16;

static HWAVEIN		gInDevice = NULL;
static WAVEHDR		gInHeaders[kInHeaders];
static short		gInBuffers[kInHeaders][kInHeaderSamples];
static int			gInNext = 0;			// the header the device fills next
static short		gInRing[kInRing];
static long			gInHead = 0;			// the ring's oldest sample
static long			gInTail = 0;			// where the next goes


bool
HostMicrophoneOpen(long sampleRate)
{
	if (gInDevice != NULL)
		return true;
	WAVEFORMATEX format;
	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 1;
	format.nSamplesPerSec = (DWORD) sampleRate;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 2;
	format.nAvgBytesPerSec = (DWORD) sampleRate * 2;
	if (waveInOpen(&gInDevice, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
	{
		gInDevice = NULL;
		return false;
	}
	memset(gInHeaders, 0, sizeof(gInHeaders));
	for (int i = 0; i < kInHeaders; i++)
	{
		gInHeaders[i].lpData = (LPSTR) gInBuffers[i];
		gInHeaders[i].dwBufferLength = kInHeaderSamples * sizeof(short);
		waveInPrepareHeader(gInDevice, &gInHeaders[i], sizeof(WAVEHDR));
		waveInAddBuffer(gInDevice, &gInHeaders[i], sizeof(WAVEHDR));
	}
	gInNext = 0;
	gInHead = gInTail = 0;
	waveInStart(gInDevice);
	return true;
}


void
HostMicrophoneRecord(short* samples, long count)
{
	if (gInDevice != NULL)
	{
		// the buffers the device has filled, in order, into the ring and
		// back to the device
		while ((gInHeaders[gInNext].dwFlags & WHDR_DONE) != 0)
		{
			WAVEHDR* header = &gInHeaders[gInNext];
			long n = (long) (header->dwBytesRecorded / sizeof(short));
			for (long i = 0; i < n; i++)
			{
				gInRing[gInTail] = gInBuffers[gInNext][i];
				gInTail = (gInTail + 1) & (kInRing - 1);
				if (gInTail == gInHead)
					gInHead = (gInHead + 1) & (kInRing - 1);		// the oldest overwritten
			}
			header->dwFlags &= ~WHDR_DONE;
			header->dwBytesRecorded = 0;
			waveInAddBuffer(gInDevice, header, sizeof(WAVEHDR));
			gInNext = (gInNext + 1) % kInHeaders;
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
	if (gInDevice == NULL)
		return;
	waveInReset(gInDevice);
	for (int i = 0; i < kInHeaders; i++)
		waveInUnprepareHeader(gInDevice, &gInHeaders[i], sizeof(WAVEHDR));
	waveInClose(gInDevice);
	gInDevice = NULL;
}

#else

bool	HostMicrophoneOpen(long)				{ return false; }
void	HostMicrophoneRecord(short* samples, long count)	{ for (long i = 0; i < count; i++) samples[i] = 0; }
void	HostMicrophoneClose(void)				{ }

bool	HostAudioOpen(long)					{ return false; }
void	HostAudioPlay(const short*, long)	{ }
void	HostAudioClose(void)				{ }

#endif
