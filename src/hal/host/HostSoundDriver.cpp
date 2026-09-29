/*
	File:		hal/host/HostSoundDriver.cpp

	Contains:	PMainSoundDriver: the host's sound output (HostSoundDriver.h).
*/

#include "HostSoundDriver.h"
#include "HostInterruptSources.h"
#include "CompMath.h"
#include "hal/Timer.h"
#include <stdlib.h>
#include <string.h>

PROTOCOL_IMPL_SOURCE_MACRO(PMainSoundDriver)
PROTOCOL_CLASSINFO(PMainSoundDriver, "PSoundDriver", "SoundOutput\0\0", 0, 0, nil)

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

static const HostSoundBackend	gNullBackend = { CapturePlay };


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


static void
RegisterHostSoundDriver(void)
{
	PMainSoundDriver::ClassInfo()->Register();
	HostRegisterInterruptSource(SoundDeadline, SoundDeliver);
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
PMainSoundDriver::SetInputBuffers(VAddr, ULong, VAddr, ULong)
{
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


NewtonErr
PMainSoundDriver::ScheduleInputBuffer(ULong, ULong)
{
	return noErr;
}


void
PMainSoundDriver::StartPlaying(void)
{
	if (fQueued == 0)
		return;
	long which = fQueue[0];
	long samples = fQueueSize[0] / 2;
	gHostSoundBackend->play((const short*) fBuffer[which], samples);
	GetClock(&fEnd);
	Int64 length = { 0, (ULong) (((long long) samples * kSeconds) / kHostSoundRate) };
	CompAdd(&length, &fEnd);
	fPlaying = true;
}


void		PMainSoundDriver::PowerOutputOn(long)	{ fPowered = true; }
void		PMainSoundDriver::PowerOutputOff(void)	{ fPowered = false; }
void		PMainSoundDriver::PowerInputOn(long)	{ }
void		PMainSoundDriver::PowerInputOff(void)	{ }


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


NewtonErr
PMainSoundDriver::StartInput(void)
{
	return -1;
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


NewtonErr	PMainSoundDriver::StopInput(void)			{ return noErr; }
Boolean		PMainSoundDriver::OutputIsEnabled(void)		{ return fRunning; }
Boolean		PMainSoundDriver::InputIsEnabled(void)		{ return false; }
Boolean		PMainSoundDriver::OutputIsRunning(void)		{ return fRunning; }
Boolean		PMainSoundDriver::InputIsRunning(void)		{ return false; }
VAddr		PMainSoundDriver::CurrentOutputPtr(void)	{ return fPlaying ? fBuffer[fQueue[0]] : 0; }
VAddr		PMainSoundDriver::CurrentInputPtr(void)		{ return 0; }
void		PMainSoundDriver::OutputVolume(long decibels)	{ fVolume = decibels; }
long		PMainSoundDriver::OutputVolume(void)		{ return fVolume; }
void		PMainSoundDriver::InputVolume(long)			{ }
long		PMainSoundDriver::InputVolume(void)			{ return 0; }
void		PMainSoundDriver::EnableExtSoundSource(long)	{ }
void		PMainSoundDriver::DisableExtSoundSource(long)	{ }
long		PMainSoundDriver::OutputIntHandler(void)	{ return noErr; }
long		PMainSoundDriver::InputIntHandler(void)		{ return noErr; }
