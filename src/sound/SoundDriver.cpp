/*
	File:		sound/SoundDriver.cpp

	Contains:	PSoundDriver's own members - the interrupt dispatchers the
				hardware's handler is reached through, and the callbacks
				the sound server hangs on them.

	The implementations are the machine's (the ROM's PCirrusSoundDriver; a
	host's hal/host/HostSoundDriver.h).
*/

#include "SoundDriver.h"


PSoundDriver*	gSndDriver = nil;					// ROM 0x0c101b14 gSndDriver

void			(*gHostRegisterSoundDriver)(void) = nil;


/*------------------------------------------------------------------------------
	P S o u n d D r i v e r
------------------------------------------------------------------------------*/

// ROM 0x003890a0 New__12PSoundDriverSFPc
PSoundDriver*
PSoundDriver::New(const char* implementation)
{
	PSoundDriver* p = (PSoundDriver*) AllocInstanceByName("PSoundDriver", implementation);
	return p != nil ? (PSoundDriver*) p->GlueNew() : nil;
}


// ROM 0x003890cc Delete__12PSoundDriverFv
void
PSoundDriver::Delete()
{
	GlueDelete();
}


// ROM 0x001e60fc OutputIntHandlerDispatcher__12PSoundDriverFv
// The driver's handler, then the server's callback; ==> the handler's
// answer (the callback's is dropped).
long
PSoundDriver::OutputIntHandlerDispatcher(void)
{
	long result = OutputIntHandler();
	if (fOutputProc != nil)
		fOutputProc(fOutputRefCon);
	return result;
}


// ROM 0x001e6130 InputIntHandlerDispatcher__12PSoundDriverFv
long
PSoundDriver::InputIntHandlerDispatcher(void)
{
	long result = InputIntHandler();
	if (fInputProc != nil)
		fInputProc(fInputRefCon);
	return result;
}


// ROM 0x001e6164 SetOutputCallbackProc__12PSoundDriverFPFPv_lPv
void
PSoundDriver::SetOutputCallbackProc(SoundCallbackProcPtr proc, void* refCon)
{
	fOutputProc = proc;
	fOutputRefCon = refCon;
}


// ROM 0x001e6170 SetInputCallbackProc__12PSoundDriverFPFPv_lPv
void
PSoundDriver::SetInputCallbackProc(SoundCallbackProcPtr proc, void* refCon)
{
	fInputProc = proc;
	fInputRefCon = refCon;
}


// ROM 0x001e617c RegisterSoundHardwareDriver__Fv
// DEVIATION: PCirrusSoundDriver::ClassInfo()->Register() in the ROM; the
// host's driver is registered by the hook a host program sets.
void
RegisterSoundHardwareDriver(void)
{
	if (gHostRegisterSoundDriver != nil)
		gHostRegisterSoundDriver();
}
