/*
	File:		user/UserPersistent.cpp

	Contains:	The tablet calibration, patch and RAM calls over what the
				kernel keeps across a warm restart (UserPersistent.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "UserPersistent.h"
#include "UserGlobals.h"
#include "KernelGlobals.h"
#include "VirtualMemory.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "os600/TaskGlobals.h"
#include "os600/GenericSWISelectors.h"
#include "hal/System.h"


PersistentInfoRequest*
PersistentInfoRequestBlock(void)
{
	return (PersistentInfoRequest*) ((char*) gCurrentGlobals - kTaskGlobalsSize);
}


/*------------------------------------------------------------------------------
	T h e   t a b l e t ' s   c a l i b r a t i o n
------------------------------------------------------------------------------*/

// ROM 0x000d967c SetTabletCalibrationData__FlN31
// The calibration the inker answered given the kernel too, marked valid, so
// that the tablet starts calibrated after a warm restart.
NewtonErr
SetTabletCalibrationData(long xScale, long xOffset, long yScale, long yOffset)
{
	PersistentInfoRequest* request = PersistentInfoRequestBlock();
	request->fWord[0] = kTabletCalibrationValid;
	request->fWord[1] = (ULong) xScale;
	request->fWord[2] = (ULong) xOffset;
	request->fWord[3] = (ULong) yScale;
	request->fWord[4] = (ULong) yOffset;
	return SetTabletCalibrationDataSWI();
}


// The supervisor halves of the dual-mode calls below.  (Host: the ROM's
// kernel case calls the dual-mode function itself, which finds itself in
// supervisor mode; a host system call does not run in supervisor mode -
// IsSuperMode is an interrupt handler's - so the kernel's case calls these.)
void
PrimSetTabletCalibrationData(void)
{
	PersistentInfoRequest* request = PersistentInfoRequestBlock();
	gGlobalsThatLiveAcrossReboot.fTabletValid = (long) request->fWord[0];
	gGlobalsThatLiveAcrossReboot.fTabletXScale = (long) request->fWord[1];
	gGlobalsThatLiveAcrossReboot.fTabletXOffset = (long) request->fWord[2];
	gGlobalsThatLiveAcrossReboot.fTabletYScale = (long) request->fWord[3];
	gGlobalsThatLiveAcrossReboot.fTabletYOffset = (long) request->fWord[4];
}


void
PrimGetTabletCalibrationData(void)
{
	PersistentInfoRequest* request = PersistentInfoRequestBlock();
	request->fWord[0] = (ULong) gGlobalsThatLiveAcrossReboot.fTabletValid;
	request->fWord[1] = (ULong) gGlobalsThatLiveAcrossReboot.fTabletXScale;
	request->fWord[2] = (ULong) gGlobalsThatLiveAcrossReboot.fTabletXOffset;
	request->fWord[3] = (ULong) gGlobalsThatLiveAcrossReboot.fTabletYScale;
	request->fWord[4] = (ULong) gGlobalsThatLiveAcrossReboot.fTabletYOffset;
}


// ROM 0x000d9704 SetTabletCalibrationDataSWI__Fv
// The request block's five words into the kernel's copy (valid, x scale, x
// offset, y scale, y offset); from user mode by system call 0x33.
NewtonErr
SetTabletCalibrationDataSWI(void)
{
	if (!IsSuperMode())
		GenericSWI(kGeneric_SetTabletCalibrationData);
	else
		PrimSetTabletCalibrationData();
	return noErr;
}


// ROM 0x000d9774 GetTabletCalibrationDataSWI__Fv
// The kernel's copy into the request block; from user mode by system call
// 0x34.
NewtonErr
GetTabletCalibrationDataSWI(void)
{
	if (!IsSuperMode())
		GenericSWI(kGeneric_GetTabletCalibrationData);
	else
		PrimGetTabletCalibrationData();
	return noErr;
}


/*------------------------------------------------------------------------------
	P a t c h e s
------------------------------------------------------------------------------*/

// GetPatchInfo's supervisor half (host: see PrimSetTabletCalibrationData).
void
SuperGetPatchInfo(ULong* version, ULong* size)
{
	*version = gGlobalsThatLiveAcrossReboot.fPatchArray[0].fPatchVersion;
	*size = gGlobalsThatLiveAcrossReboot.fPatchArray[0].fPatchPageCount << 12;
}


// ROM 0x0011e078 GetPatchInfo__FPUlT1
// The first patch's version and its size in bytes (its pages).  From user
// mode by system call 0x2f with selector 1 in the request block, the
// answers coming back in its next two words.
NewtonErr
GetPatchInfo(ULong* version, ULong* size)
{
	NewtonErr err = noErr;
	if (IsSuperMode())
		SuperGetPatchInfo(version, size);
	else
	{
		PersistentInfoRequest* request = PersistentInfoRequestBlock();
		request->fWord[0] = 1;
		err = (NewtonErr) GenericSWI(kGeneric_GetPatchInfo);
		*version = request->fWord[1];
		*size = request->fWord[2];
	}
	return err;
}


// ROM 0x0011e0f4 PrimGetPatchInfo__Fv
// The kernel's side of system call 0x2f: selector 0 registers a patch (its
// four arguments in the next words), 1 answers GetPatchInfo into the block.
// NOT YET RECONSTRUCTED: RegisterPatch (0x0011dffc), a system patch mapped
// over the ROM's pages - the host has no page tables to patch, and answers
// kError_Call_Not_Implemented.
NewtonErr
PrimGetPatchInfo(void)
{
	PersistentInfoRequest* request = PersistentInfoRequestBlock();
	switch (request->fWord[0])
	{
	case 0:
		return kError_Call_Not_Implemented;
	case 1:
		SuperGetPatchInfo(&request->fWord[1], &request->fWord[2]);	// (GetPatchInfo, in supervisor mode)
		return noErr;
	}
	return kError_Bad_Parameters;
}


/*------------------------------------------------------------------------------
	R A M
------------------------------------------------------------------------------*/

// ROM 0x0011e318 InternalRAMInfo
// which = -1: the RAM the system has, less what the internal store was cut
// out of (TRAMTable::GetRAMSize() - InternalStoreInfo(1)).  Any other: that
// entry of the last extension's 'ralc' RAM allocation table, its share
// (per 1024) of that RAM rounded up to the alignment and held between the
// entry's minimum and maximum.
// DEVIATION: the host's RAM is its heap and its store a file, so the first
// is SystemRAMSize's answer (memory/MemoryManager.cpp).  NOT YET
// RECONSTRUCTED: the 'ralc' entries (GetLastRExConfigEntry is the packages
// layer's here, above this one), which answer 0.
ULong
InternalRAMInfo(ULong which, ULong /*alignment*/)
{
	ULong ram = (ULong) SystemRAMSize();
	if (which == 0xFFFFFFFF)
		return ram;
	return 0;
}
