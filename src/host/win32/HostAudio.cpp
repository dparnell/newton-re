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

#else

bool	HostAudioOpen(long)					{ return false; }
void	HostAudioPlay(const short*, long)	{ }
void	HostAudioClose(void)				{ }

#endif
