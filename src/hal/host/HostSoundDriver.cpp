/*
	File:		hal/host/HostSoundDriver.cpp

	Contains:	PMainSoundDriver: the host's sound output (HostSoundDriver.h).
*/

#include "HostSoundDriver.h"
#include "HostInterruptSources.h"
#include "CompMath.h"
#include "hal/Timer.h"
#include "SampleWords.h"
#include <stdlib.h>
#include <string.h>

PROTOCOL_IMPL_SOURCE_MACRO(PMainSoundDriver)
PROTOCOL_CLASSINFO(PMainSoundDriver, "PSoundDriver", "SoundOutput\0\0SoundInput\0\0", 0, 0, nil)

const long	kHostSoundRate = 21600;

static PMainSoundDriver*		gHostSoundDriver = nil;
static const HostSoundBackend*	gHostSoundBackend = nil;

static short*	gCaptured = nil;
static long		gCapturedCount = 0;
static long		gCapturedRoom = 0;


/*------------------------------------------------------------------------------
	The null backend: the samples kept.
------------------------------------------------------------------------------*/

static void
CapturePlay(const short* samples, long count)
{
	if (gCapturedCount + count > gCapturedRoom)
	{
		long room = gCapturedRoom * 2;
		if (room < gCapturedCount + count)
			room = gCapturedCount + count + 0x4000;
		short* p = (short*) realloc(gCaptured, room * sizeof(short));
		if (p == NULL)
			return;
		gCaptured = p;
		gCapturedRoom = room;
	}
	memcpy(gCaptured + gCapturedCount, samples, count * sizeof(short));
	gCapturedCount += count;
}

static const short*	gSource = nil;
static long			gSourceCount = 0;
static long			gSourceAt = 0;

static void
SourceRecord(short* samples, long count)
{
	for (long i = 0; i < count; i++)
		samples[i] = (gSourceAt < gSourceCount) ? gSource[gSourceAt++] : 0;
}


void
HostSoundSetSource(const short* samples, long count)
{
	gSource = samples;
	gSourceCount = count;
	gSourceAt = 0;
}


static const HostSoundBackend	gNullBackend = { CapturePlay, SourceRecord };


const short*
HostSoundCaptured(long* count)
{
	*count = gCapturedCount;
	return gCaptured;
}


void
HostSoundClearCapture(void)
{
	gCapturedCount = 0;
}


/*------------------------------------------------------------------------------
	The interrupt: a buffer played to its end.
------------------------------------------------------------------------------*/

static Boolean
SoundDeadline(Int64* when)
{
	PMainSoundDriver* driver = gHostSoundDriver;
	if (driver == nil || !driver->fPlaying)
		return false;
	*when = driver->fEnd;
	return true;
}


// The buffer that was playing done with, the next one started, and the
// server told (it refills the one just played and schedules it behind).
static void
SoundDeliver(void)
{
	PMainSoundDriver* driver = gHostSoundDriver;
	if (driver == nil || !driver->fPlaying)
		return;
	driver->fPlaying = false;
	driver->fQueue[0] = driver->fQueue[1];
	driver->fQueueSize[0] = driver->fQueueSize[1];
	driver->fQueue[1] = -1;
	driver->fQueued--;
	driver->StartPlaying();
	driver->OutputIntHandlerDispatcher();
}


// The input's: a buffer filled.
static Boolean
RecordDeadline(Int64* when)
{
	PMainSoundDriver* driver = gHostSoundDriver;
	if (driver == nil || !driver->fRecording)
		return false;
	*when = driver->fInEnd;
	return true;
}


static void
RecordDeliver(void)
{
	PMainSoundDriver* driver = gHostSoundDriver;
	if (driver == nil || !driver->fRecording)
		return;
	driver->fRecording = false;
	driver->fInQueue[0] = driver->fInQueue[1];
	driver->fInQueueSize[0] = driver->fInQueueSize[1];
	driver->fInQueue[1] = -1;
	driver->fInQueued--;
	driver->StartRecording();
	driver->InputIntHandlerDispatcher();
}


static void
RegisterHostSoundDriver(void)
{
	PMainSoundDriver::ClassInfo()->Register();
	HostRegisterInterruptSource(SoundDeadline, SoundDeliver);
	HostRegisterInterruptSource(RecordDeadline, RecordDeliver);
}


void
HostInstallSoundDriver(const HostSoundBackend* backend)
{
	gHostSoundBackend = (backend != nil) ? backend : &gNullBackend;
	gHostRegisterSoundDriver = RegisterHostSoundDriver;
}


/*------------------------------------------------------------------------------
	P M a i n S o u n d D r i v e r
------------------------------------------------------------------------------*/

PMainSoundDriver*
PMainSoundDriver::New()
{
	fOutputProc = nil;
	fOutputRefCon = nil;
	fInputProc = nil;
	fInputRefCon = nil;
	fBuffer[0] = fBuffer[1] = 0;
	fBufferSize[0] = fBufferSize[1] = 0;
	fQueue[0] = fQueue[1] = -1;
	fQueueSize[0] = fQueueSize[1] = 0;
	fQueued = 0;
	fRunning = false;
	fPowered = false;
	fPlaying = false;
	fEnd.hi = 0;
	fEnd.lo = 0;
	fVolume = 0;
	fInBuffer[0] = fInBuffer[1] = 0;
	fInBufferSize[0] = fInBufferSize[1] = 0;
	fInQueue[0] = fInQueue[1] = -1;
	fInQueueSize[0] = fInQueueSize[1] = 0;
	fInQueued = 0;
	fInRunning = false;
	fInPowered = false;
	fRecording = false;
	fInEnd.hi = 0;
	fInEnd.lo = 0;
	fInGain = 0x80;
	fPlay = nil;
	fPlaySize = 0;
	if (gHostSoundBackend == nil)
		gHostSoundBackend = &gNullBackend;
	gHostSoundDriver = this;
	return this;
}


void
PMainSoundDriver::Delete()
{
	if (gHostSoundDriver == this)
		gHostSoundDriver = nil;
}


NewtonErr
PMainSoundDriver::SetSoundHardwareInfo(const TSoundDriverInfo* /*info*/)
{
	return noErr;
}


// As PCirrusSoundDriver answers at its top rate.
NewtonErr
PMainSoundDriver::GetSoundHardwareInfo(TSoundDriverInfo* info)
{
	info->fUnknown00 = 1;
	info->fUnknown04 = 1;
	info->fUnknown08 = 1;
	info->fSampleRate = kHostSoundRate << 16;
	info->fFormat = 6;					// 16-bit linear
	info->fSampleBits = 16;
	info->fUnknown18 = 1;
	return noErr;
}


NewtonErr
PMainSoundDriver::SetOutputBuffers(VAddr buffer1, ULong size1, VAddr buffer2, ULong size2)
{
	fBuffer[0] = buffer1;
	fBufferSize[0] = size1;
	fBuffer[1] = buffer2;
	fBufferSize[1] = size2;
	return noErr;
}


NewtonErr
PMainSoundDriver::SetInputBuffers(VAddr buffer1, ULong size1, VAddr buffer2, ULong size2)
{
	fInBuffer[0] = buffer1;
	fInBufferSize[0] = size1;
	fInBuffer[1] = buffer2;
	fInBufferSize[1] = size2;
	return noErr;
}


// A buffer put behind whatever is playing; an empty one says there is
// nothing more, which stops the output once the queue has run dry.
NewtonErr
PMainSoundDriver::ScheduleOutputBuffer(ULong which, ULong size)
{
	if (size == 0)
	{
		if (fRunning && !fPlaying && fQueued == 0)
			StopOutput();
		return noErr;
	}
	if (fQueued == 2)
		fQueued = 1;					// (the hardware's next-buffer register overwritten)
	fQueue[fQueued] = which & 1;
	fQueueSize[fQueued] = size;
	fQueued++;
	if (fRunning && !fPlaying)
		StartPlaying();
	return noErr;
}


// A buffer to fill, behind the one filling.
NewtonErr
PMainSoundDriver::ScheduleInputBuffer(ULong which, ULong size)
{
	if (size == 0)
		return noErr;
	if (fInQueued == 2)
		fInQueued = 1;
	fInQueue[fInQueued] = which & 1;
	fInQueueSize[fInQueued] = size;
	fInQueued++;
	if (fInRunning && !fRecording)
		StartRecording();
	return noErr;
}


// The buffer at the head of the queue filled, as the hardware would fill
// it over the time it takes: the samples are taken from the backend now,
// and the interrupt falls due when the last of them would have arrived.
void
PMainSoundDriver::StartRecording(void)
{
	if (fInQueued == 0)
		return;
	long which = fInQueue[0];
	long samples = fInQueueSize[0] / 2;
	short* buffer = (short*) fInBuffer[which];
	// (the backend's samples are the host's shorts; the buffer's are
	// big-endian, as the hardware's are - SampleWords.h)
	if (gHostSoundBackend->record != nil)
	{
		gHostSoundBackend->record(buffer, samples);
		for (long i = 0; i < samples; i++)
			PutSampleAt(buffer, i, buffer[i]);
	}
	else
		memset(buffer, 0, samples * sizeof(short));
	GetClock(&fInEnd);
	Int64 length = { 0, (ULong) (((long long) samples * kSeconds) / kHostSoundRate) };
	CompAdd(&length, &fInEnd);
	fRecording = true;
}


void
PMainSoundDriver::StartPlaying(void)
{
	if (fQueued == 0)
		return;
	long which = fQueue[0];
	long samples = fQueueSize[0] / 2;
	// (the buffer's samples are big-endian, as the hardware's are; the
	// backend is handed the host's shorts - SampleWords.h)
	if (samples > fPlaySize)
	{
		free(fPlay);
		fPlay = (short*) malloc(samples * sizeof(short));
		fPlaySize = (fPlay != nil) ? samples : 0;
	}
	if (fPlay != nil)
	{
		for (long i = 0; i < samples; i++)
			fPlay[i] = GetSampleAt((const void*) fBuffer[which], i);
		gHostSoundBackend->play(fPlay, samples);
	}
	GetClock(&fEnd);
	Int64 length = { 0, (ULong) (((long long) samples * kSeconds) / kHostSoundRate) };
	CompAdd(&length, &fEnd);
	fPlaying = true;
}


void		PMainSoundDriver::PowerOutputOn(long)	{ fPowered = true; }
void		PMainSoundDriver::PowerOutputOff(void)	{ fPowered = false; }
void		PMainSoundDriver::PowerInputOn(long)	{ fInPowered = true; }
void		PMainSoundDriver::PowerInputOff(void)	{ fInPowered = false; }


// The first buffer started; answers 1 - "schedule the other one too" - when
// only one is queued, so the server fills the second before the first ends
// and the output never waits on it.
NewtonErr
PMainSoundDriver::StartOutput(void)
{
	fRunning = true;
	if (!fPlaying)
		StartPlaying();
	return (fQueued < 2) ? 1 : noErr;
}


// The input started on the buffers scheduled.
NewtonErr
PMainSoundDriver::StartInput(void)
{
	fInRunning = true;
	if (!fRecording)
		StartRecording();
	return noErr;
}


// As PCirrusSoundDriver's: a running output stopped takes one last
// interrupt, which is how the server learns it has stopped.
NewtonErr
PMainSoundDriver::StopOutput(void)
{
	Boolean wasRunning = fRunning;
	fRunning = false;
	fPlaying = false;
	fQueued = 0;
	fQueue[0] = fQueue[1] = -1;
	if (!wasRunning)
		return noErr;
	long result = OutputIntHandler();
	if (fOutputProc != nil)
		fOutputProc(fOutputRefCon);
	return result;
}


NewtonErr
PMainSoundDriver::StopInput(void)
{
	fInRunning = false;
	fRecording = false;
	fInQueued = 0;
	fInQueue[0] = fInQueue[1] = -1;
	return noErr;
}
Boolean		PMainSoundDriver::OutputIsEnabled(void)		{ return fRunning; }
Boolean		PMainSoundDriver::InputIsEnabled(void)		{ return fInRunning; }
Boolean		PMainSoundDriver::OutputIsRunning(void)		{ return fRunning; }
Boolean		PMainSoundDriver::InputIsRunning(void)		{ return fInRunning; }
VAddr		PMainSoundDriver::CurrentOutputPtr(void)	{ return fPlaying ? fBuffer[fQueue[0]] : 0; }
VAddr		PMainSoundDriver::CurrentInputPtr(void)		{ return 0; }
void		PMainSoundDriver::OutputVolume(long decibels)	{ fVolume = decibels; }
long		PMainSoundDriver::OutputVolume(void)		{ return fVolume; }
void		PMainSoundDriver::InputVolume(long gain)	{ fInGain = gain; }
long		PMainSoundDriver::InputVolume(void)			{ return fInGain; }
void		PMainSoundDriver::EnableExtSoundSource(long)	{ }
void		PMainSoundDriver::DisableExtSoundSource(long)	{ }
long		PMainSoundDriver::OutputIntHandler(void)	{ return noErr; }
long		PMainSoundDriver::InputIntHandler(void)		{ return noErr; }
