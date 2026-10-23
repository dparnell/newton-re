/*
	File:		host/coreaudio/HostAudio.cpp

	Contains:	The host's loudspeaker and microphone on macOS (HostAudio.h):
				AudioToolbox's audio queues.  The Windows ones are
				host/win32/HostAudio.cpp (waveOut, waveIn), the Linux ones
				host/alsa/HostAudio.cpp, and all three answer the same six
				calls.

	The loudspeaker is an output queue that runs from HostAudioOpen to
	HostAudioClose, its few buffers refilled on the queue's own thread from
	a ring the sound server's task writes into.  HostAudioPlay must come
	back at once, so what will not fit in the ring - a fifth of a second,
	as ALSA's buffer is - is dropped rather than waited for, and what the
	queue finds the ring short of is silence.  A DMA buffer lasts as long
	on the system clock as on the device, so in practice the ring neither
	fills nor runs dry.

	The microphone is an input queue that fills a ring of its own, which
	HostMicrophoneRecord takes the oldest samples from (silence for what
	has not arrived, so it never waits).  It is started by the first record
	rather than by HostMicrophoneOpen: macOS asks the user for the
	microphone the first time a program listens, and newton opens the
	microphone at every start - it should ask when the Newton records, not
	then.  HostMicrophoneOpen answers whether there is an input device.
*/

#include "HostAudio.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <mutex>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static const int	kQueueBuffers = 3;			// the queue's buffers, each a fortieth of a second
static const long	kRingSize = 1 << 16;		// (a power of two: more than a fifth of a second at any rate asked)


/*------------------------------------------------------------------------------
	A ring of samples, one writer and one reader on different threads.
------------------------------------------------------------------------------*/

struct SampleRing
{
	short				fSamples[kRingSize];
	std::atomic<long>	fHead{0};				// the oldest sample (the reader's)
	std::atomic<long>	fTail{0};				// where the next goes (the writer's)
	long				fLimit = kRingSize - 1;	// the most it holds

	long	Count(void) const	{ return (fTail.load() - fHead.load()) & (kRingSize - 1); }
	void	Clear(void)			{ fHead.store(fTail.load()); }

	// as many of the samples as there is room for (==> how many)
	long
	Put(const short* samples, long count)
	{
		long room = fLimit - Count();
		if (count > room)
			count = room;
		long tail = fTail.load();
		for (long i = 0; i < count; i++)
			fSamples[(tail + i) & (kRingSize - 1)] = samples[i];
		fTail.store((tail + count) & (kRingSize - 1));
		return count;
	}

	// count samples, silence for what is not there
	void
	Take(short* samples, long count)
	{
		long have = Count();
		long head = fHead.load();
		long i = 0;
		for (; i < count && i < have; i++)
			samples[i] = fSamples[(head + i) & (kRingSize - 1)];
		fHead.store((head + i) & (kRingSize - 1));
		for (; i < count; i++)
			samples[i] = 0;
	}
};


static AudioStreamBasicDescription
Format(long sampleRate)
{
	AudioStreamBasicDescription format;
	memset(&format, 0, sizeof(format));
	format.mSampleRate = (Float64) sampleRate;
	format.mFormatID = kAudioFormatLinearPCM;
	format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;	// the host's own byte order
	format.mBitsPerChannel = 16;
	format.mChannelsPerFrame = 1;
	format.mBytesPerFrame = 2;
	format.mFramesPerPacket = 1;
	format.mBytesPerPacket = 2;
	return format;
}


/*------------------------------------------------------------------------------
	The loudspeaker
------------------------------------------------------------------------------*/

static AudioQueueRef	gOut = nullptr;
static SampleRing		gOutRing;
static long				gOutBufferSamples = 0;
static std::atomic<bool>	gOutClosing(false);


// The queue wants a buffer filled: from the ring (silence where it is short)
static void
OutputCallback(void*, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
	gOutRing.Take((short*) buffer->mAudioData, gOutBufferSamples);
	buffer->mAudioDataByteSize = (UInt32) (gOutBufferSamples * sizeof(short));
	if (!gOutClosing.load())
		AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}


bool
HostAudioOpen(long sampleRate)
{
	if (gOut != nullptr)
		return true;
	AudioStreamBasicDescription format = Format(sampleRate);
	if (AudioQueueNewOutput(&format, OutputCallback, nullptr, nullptr, nullptr, 0, &gOut) != noErr)
	{
		gOut = nullptr;
		return false;
	}
	gOutClosing.store(false);
	gOutRing.Clear();
	gOutRing.fLimit = sampleRate / 5;		// a fifth of a second
	if (gOutRing.fLimit > kRingSize - 1)
		gOutRing.fLimit = kRingSize - 1;
	gOutBufferSamples = sampleRate / 40;
	for (int i = 0; i < kQueueBuffers; i++)
	{
		AudioQueueBufferRef buffer;
		if (AudioQueueAllocateBuffer(gOut, (UInt32) (gOutBufferSamples * sizeof(short)), &buffer) == noErr)
			OutputCallback(nullptr, gOut, buffer);		// (silence to start with)
	}
	if (AudioQueueStart(gOut, nullptr) != noErr)
	{
		AudioQueueDispose(gOut, true);
		gOut = nullptr;
		return false;
	}
	return true;
}


void
HostAudioPlay(const short* samples, long count)
{
	if (gOut == nullptr || count <= 0)
		return;
	gOutRing.Put(samples, count);		// (what does not fit is dropped, as ALSA's full buffer does)
}


void
HostAudioClose(void)
{
	if (gOut == nullptr)
		return;
	// what is queued let finish: the ring played out (a fifth of a second
	// at most), then the queue's buffers
	for (int i = 0; i < 50 && gOutRing.Count() > 0; i++)
		usleep(10 * 1000);
	gOutClosing.store(true);
	AudioQueueStop(gOut, false);
	AudioQueueDispose(gOut, false);
	gOut = nullptr;
}


/*------------------------------------------------------------------------------
	The microphone
------------------------------------------------------------------------------*/

static std::mutex		gInLock;				// (the open, the first record and the close)
static AudioQueueRef	gIn = nullptr;
static long				gInRate = 0;			// HostMicrophoneOpen's (0: closed)
static bool				gInFailed = false;		// the queue could not be started: no more tries
static SampleRing		gInRing;
static std::atomic<bool>	gInClosing(false);


static void
InputCallback(void*, AudioQueueRef queue, AudioQueueBufferRef buffer, const AudioTimeStamp*, UInt32, const AudioStreamPacketDescription*)
{
	const short* samples = (const short*) buffer->mAudioData;
	long count = (long) (buffer->mAudioDataByteSize / sizeof(short));
	gInRing.Put(samples, count);		// (full - three seconds not taken - the rest is dropped: only the reader moves the head)
	if (!gInClosing.load())
		AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}


// Whether there is an input device to record from
static bool
HasInputDevice(void)
{
	// (element 0 is the main one: kAudioObjectPropertyElementMain, which
	// macOS 11 - the oldest newton runs on - calls ...ElementMaster)
	AudioObjectPropertyAddress address = { kAudioHardwarePropertyDefaultInputDevice,
										   kAudioObjectPropertyScopeGlobal, 0 };
	AudioDeviceID device = kAudioObjectUnknown;
	UInt32 size = sizeof(device);
	return AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) == noErr
		&& device != kAudioObjectUnknown;
}


bool
HostMicrophoneOpen(long sampleRate)
{
	std::lock_guard<std::mutex> lock(gInLock);
	if (gInRate != 0)
		return true;
	if (!HasInputDevice())
		return false;
	gInRate = sampleRate;
	gInFailed = false;
	return true;
}


// The input queue started, the first time the Newton records
static void
StartInput(void)
{
	AudioStreamBasicDescription format = Format(gInRate);
	if (AudioQueueNewInput(&format, InputCallback, nullptr, nullptr, nullptr, 0, &gIn) != noErr)
	{
		gIn = nullptr;
		gInFailed = true;
		fprintf(stderr, "[host] the microphone could not be opened; recording silence\n");
		return;
	}
	gInClosing.store(false);
	gInRing.Clear();
	gInRing.fLimit = kRingSize - 1;
	long bufferSamples = gInRate / 40;
	for (int i = 0; i < kQueueBuffers; i++)
	{
		AudioQueueBufferRef buffer;
		if (AudioQueueAllocateBuffer(gIn, (UInt32) (bufferSamples * sizeof(short)), &buffer) == noErr)
			AudioQueueEnqueueBuffer(gIn, buffer, 0, nullptr);
	}
	if (AudioQueueStart(gIn, nullptr) != noErr)
	{
		AudioQueueDispose(gIn, true);
		gIn = nullptr;
		gInFailed = true;
		fprintf(stderr, "[host] the microphone could not be started (not allowed?); recording silence\n");
	}
}


void
HostMicrophoneRecord(short* samples, long count)
{
	{
		std::lock_guard<std::mutex> lock(gInLock);
		if (gIn == nullptr && gInRate != 0 && !gInFailed)
			StartInput();
	}
	gInRing.Take(samples, count);
}


void
HostMicrophoneClose(void)
{
	std::lock_guard<std::mutex> lock(gInLock);
	if (gIn != nullptr)
	{
		gInClosing.store(true);
		AudioQueueStop(gIn, true);
		AudioQueueDispose(gIn, true);
		gIn = nullptr;
	}
	gInRate = 0;
}
