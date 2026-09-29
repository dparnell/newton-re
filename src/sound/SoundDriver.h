/*
	File:		sound/SoundDriver.h

	Contains:	PSoundDriver, the protocol the sound server drives the
				sound hardware through - the seam between the operating
				system and the machine.  The ROM's implementation is
				PCirrusSoundDriver (the MessagePad's Cirrus codec and the
				Voyager DMA channels, 0x00059980-0x0005abf0); a host
				provides its own (hal/host/HostSoundDriver.h), registered
				under the name the server asks for first, PMainSoundDriver.

				The server hands the driver two output buffers of 0xea0
				bytes (SetOutputBuffers) and then, over and over, says which
				of them to play next and how much of it
				(ScheduleOutputBuffer); when the hardware has played a
				buffer it interrupts, and the driver's OutputIntHandler runs
				the server's callback (SoundOutputIH) through
				OutputIntHandlerDispatcher.  The hardware's own format -
				16-bit linear samples at 21600, 14400 or 7200 a second on
				the MP2x00 - is what GetSoundHardwareInfo answers.

	The interface follows the ROM's dispatch table
	(tools/newton-rom/analysis/classinfo.py --name PCirrusSoundDriver);
	TSoundDriverInfo's field names are ours, read off
	PCirrusSoundDriver::GetSoundHardwareInfo 0x0005a8b0 and TDMAChannel's
	constructor 0x001e3804.
*/

#ifndef __SOUNDDRIVER_H
#define __SOUNDDRIVER_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

#ifndef __NEWTERRORS_H
#include "NewtErrors.h"
#endif


// What the hardware is.  (The first three words are 1 on the MP2x00; what
// they say is not known - nothing the server does reads them.)
struct TSoundDriverInfo
{
	ULong		fUnknown00;			// +0x00
	ULong		fUnknown04;			// +0x04
	ULong		fUnknown08;			// +0x08
	Fixed		fSampleRate;		// +0x0c  samples a second, 16.16 (0x54600000: 21600)
	ULong		fFormat;			// +0x10  kSoundFormatLinear16
	ULong		fSampleBits;		// +0x14  16
	ULong		fUnknown18;			// +0x18
};

// the driver's interrupt callbacks: the server's SoundOutputIH/SoundInputIH
typedef long (*SoundCallbackProcPtr)(void* refCon);


PROTOCOL PSoundDriver : public TProtocol
{
public:
	static PSoundDriver*	New(const char* implementation);	// ROM 0x003890a0 New__12PSoundDriverSFPc
	void			Delete();									// ROM 0x003890cc Delete__12PSoundDriverFv

	VIRTUAL NewtonErr	SetSoundHardwareInfo(const TSoundDriverInfo* info) ENDVIRTUAL;	// ROM 0x003890e8
	VIRTUAL NewtonErr	GetSoundHardwareInfo(TSoundDriverInfo* info) ENDVIRTUAL;			// ROM 0x003890f4
	VIRTUAL NewtonErr	SetOutputBuffers(VAddr buffer1, ULong size1, VAddr buffer2, ULong size2) ENDVIRTUAL;	// ROM 0x00389100
	VIRTUAL NewtonErr	SetInputBuffers(VAddr buffer1, ULong size1, VAddr buffer2, ULong size2) ENDVIRTUAL;	// ROM 0x0038910c
	// which: buffer 0 or 1; size: the bytes of it to play
	VIRTUAL NewtonErr	ScheduleOutputBuffer(ULong which, ULong size) ENDVIRTUAL;			// ROM 0x00389118
	VIRTUAL NewtonErr	ScheduleInputBuffer(ULong which, ULong size) ENDVIRTUAL;			// ROM 0x00389124
	VIRTUAL void		PowerOutputOn(long device) ENDVIRTUAL;								// ROM 0x00389130
	VIRTUAL void		PowerOutputOff(void) ENDVIRTUAL;									// ROM 0x0038913c
	VIRTUAL void		PowerInputOn(long device) ENDVIRTUAL;								// ROM 0x00389148
	VIRTUAL void		PowerInputOff(void) ENDVIRTUAL;										// ROM 0x00389154
	VIRTUAL NewtonErr	StartOutput(void) ENDVIRTUAL;										// ROM 0x00389160
	VIRTUAL NewtonErr	StartInput(void) ENDVIRTUAL;										// ROM 0x0038916c
	VIRTUAL NewtonErr	StopOutput(void) ENDVIRTUAL;										// ROM 0x00389178
	VIRTUAL NewtonErr	StopInput(void) ENDVIRTUAL;											// ROM 0x00389184
	VIRTUAL Boolean		OutputIsEnabled(void) ENDVIRTUAL;									// ROM 0x00389190
	VIRTUAL Boolean		InputIsEnabled(void) ENDVIRTUAL;									// ROM 0x0038919c
	VIRTUAL Boolean		OutputIsRunning(void) ENDVIRTUAL;									// ROM 0x003891a8
	VIRTUAL Boolean		InputIsRunning(void) ENDVIRTUAL;									// ROM 0x003891b4
	VIRTUAL VAddr		CurrentOutputPtr(void) ENDVIRTUAL;									// ROM 0x003891c0
	VIRTUAL VAddr		CurrentInputPtr(void) ENDVIRTUAL;									// ROM 0x003891cc
	// the volume in 16.16 decibels
	VIRTUAL void		OutputVolume(long decibels) ENDVIRTUAL;								// ROM 0x003891d8
	VIRTUAL long		OutputVolume(void) ENDVIRTUAL;										// ROM 0x003891e4
	VIRTUAL void		InputVolume(long gain) ENDVIRTUAL;									// ROM 0x003891f0
	VIRTUAL long		InputVolume(void) ENDVIRTUAL;										// ROM 0x003891fc
	VIRTUAL void		EnableExtSoundSource(long source) ENDVIRTUAL;						// ROM 0x00389208
	VIRTUAL void		DisableExtSoundSource(long source) ENDVIRTUAL;						// ROM 0x00389214
	VIRTUAL long		OutputIntHandler(void) ENDVIRTUAL;									// ROM 0x00389220
	VIRTUAL long		InputIntHandler(void) ENDVIRTUAL;									// ROM 0x0038922c

	// the interrupt, as the hardware takes it: the driver's handler and
	// then the server's callback
	long			OutputIntHandlerDispatcher(void);			// ROM 0x001e60fc OutputIntHandlerDispatcher__12PSoundDriverFv
	long			InputIntHandlerDispatcher(void);			// ROM 0x001e6130 InputIntHandlerDispatcher__12PSoundDriverFv
	void			SetOutputCallbackProc(SoundCallbackProcPtr proc, void* refCon);	// ROM 0x001e6164 SetOutputCallbackProc__12PSoundDriverFPFPv_lPv
	void			SetInputCallbackProc(SoundCallbackProcPtr proc, void* refCon);	// ROM 0x001e6170 SetInputCallbackProc__12PSoundDriverFPFPv_lPv

	SoundCallbackProcPtr	fOutputProc;		// +0x10
	void*					fOutputRefCon;		// +0x14
	SoundCallbackProcPtr	fInputProc;			// +0x18
	void*					fInputRefCon;		// +0x1c
};

extern PSoundDriver*	gSndDriver;			// ROM 0x0c101b14 gSndDriver

// ROM 0x001e617c RegisterSoundHardwareDriver__Fv - the machine's driver put
// in the protocol registry.  DEVIATION: the ROM registers
// PCirrusSoundDriver, which programs the MessagePad's codec; a host
// registers its own driver through this hook (nil: none registered, and
// the server finds no driver and does not start).
extern void	(*gHostRegisterSoundDriver)(void);
void	RegisterSoundHardwareDriver(void);

#endif	/* __SOUNDDRIVER_H */
