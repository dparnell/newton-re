/*
	File:		hal/host/System.cpp

	Contains:	Whole-machine control for a host build: a reset is recorded (and
				reported) rather than performed, so tests can observe it.
*/

#include "hal/System.h"
#include "OSErrors.h"

#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

ULong	gHostResetCount = 0;
void	(*gHostResetHook)(void) = nil;
Boolean	gHostPoweredOff = false;

extern "C" NewtonErr
Reset(void)
{
	gHostResetCount++;
	fprintf(stderr, "[host] machine reset requested\n");
	if (gHostResetHook != nil)
		gHostResetHook();
	return 0;
}

extern "C" void
DisableAllInterrupts(void)
{
}

extern "C" void
IOPowerOffAll(void)
{
	gHostPoweredOff = true;
	fprintf(stderr, "[host] power off requested\n");
}

// ROM 0x00394410 IsSuperMode
// The CPSR's mode bits are neither user (0x10) nor 26-bit user (0) - so
// true in SVC, IRQ and FIQ mode alike.
// DEVIATION: the host has no CPSR.  A task's code - and the kernel glue a
// system-call stub calls on its behalf - takes the user-mode path, so the
// dual-mode ROM routines exercise the stubs; but an interrupt handler, which
// the task runtime runs at a safe point on whichever task's thread holds
// the baton (HostDeliverInterrupts), is in IRQ mode on the MessagePad and
// must take the supervisor path.  Answering false there made a dual-mode
// call from a handler a system call - GetGlobalTime from the serial tool's
// receive interrupt (TSerTool::IHRequest -> TimeFromNow) was the one seen -
// whose glue and exit path wrote the *interrupted* task's saved registers
// (its r0-r2: the results of the system call it was itself in the middle
// of) and could even switch tasks from inside the handler.
long gHostInterruptLevel = 0;

extern "C" Boolean
IsSuperMode(void)
{
	return gHostInterruptLevel > 0;
}

// DEVIATION: the host answers what an MP2x00 measures - a StrongARM at
// 162 MHz - rather than reading a coprocessor and timing a loop.
extern "C" ULong
LowLevelGetCPUType(void)
{
	return 3;
}

extern "C" Fixed
GetCPUClockSpeed(void)
{
	return 0xa22f1b;
}

// A MessagePad 2100 has 4 MB of DRAM.
extern "C" ULong
GetRamSize(void)
{
	return 4 * 1024 * 1024;
}

// ROM 0x001dd720 GetSystemSerialNumber__16TSerialNumberROMFPUl
// The MessagePad reads its number off a one-wire ROM chip
// (TSerialNumberROM, whose Init reads the eight bytes: a family code, the
// number and a CRC) and hands back the middle of it - the first word bits
// 8-23 of the chip's first word, the second the rest.
//
// DEVIATION: a host has no such chip, and the ROM answers -10074 when the
// chip has not been read - which would leave every store unsigned.  The
// host therefore has a number of its own, fixed so that a store formatted
// by one run is recognised by the next, and obviously not a real Newton's.
extern "C" NewtonErr
GetSystemSerialNumber(ULong serialNumber[2])
{
	// the second word is what the internal store is signed with
	// (MakeStoreObject), and a script reads it back with ExtractLong, which
	// throws on anything that is not a 30-bit NewtonScript integer - so the
	// top two bits stay clear, as they do on a machine whose number comes
	// off the chip
	serialNumber[0] = 0x00004e65;		// 'Ne'
	serialNumber[1] = 0x0077746f;		// 'wto'
	return noErr;
}


// The host runs everything as a user-mode task.
extern "C" ULong
GetCPUMode(void)
{
	return 0x10;
}

// The host thread's stack, from the operating system.
extern "C" void
GetStackBounds(const void** low, const void** high)
{
#ifdef _WIN32
	ULONG_PTR lowLimit, highLimit;
	GetCurrentThreadStackLimits(&lowLimit, &highLimit);
	*low = (const void*) lowLimit;
	*high = (const void*) highLimit;
#elif defined(__APPLE__)
	// (macOS answers the stack's top - its high address - and its size)
	pthread_t self = pthread_self();
	char* top = (char*) pthread_get_stackaddr_np(self);
	*high = top;
	*low = top - pthread_get_stacksize_np(self);
#else
	pthread_attr_t attr;
	void* base = nil;
	size_t size = 0;
	if (pthread_getattr_np(pthread_self(), &attr) == 0)
	{
		pthread_attr_getstack(&attr, &base, &size);
		pthread_attr_destroy(&attr);
	}
	*low = base;
	*high = (const char*) base + size;
#endif
}
