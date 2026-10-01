/*
	File:		user/UserPersistent.h

	Contains:	The calls that read and write what the kernel keeps across a
				warm restart (gGlobalsThatLiveAcrossReboot): the tablet's
				calibration, the installed patch's version and size, and
				the RAM the machine has.  Each is dual-mode - a task asks
				through GenericSWI with its arguments in the request block
				at the bottom of its globals (os600/TaskGlobals.h), the
				kernel (or a caller already in supervisor mode) works on the
				globals themselves.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __USERPERSISTENT_H
#define __USERPERSISTENT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

// the request block: the five words at the bottom of the task's globals
struct PersistentInfoRequest
{
	ULong		fWord[5];		// +0x00 .. +0x10
};

PersistentInfoRequest*	PersistentInfoRequestBlock(void);

// the tablet's calibration, marked kTabletCalibrationValid ('G00D') when set
const ULong kTabletCalibrationValid = 0x47303044;

NewtonErr	SetTabletCalibrationData(long xScale, long xOffset, long yScale, long yOffset);	// ROM 0x000d967c SetTabletCalibrationData__FlN31
NewtonErr	SetTabletCalibrationDataSWI(void);			// ROM 0x000d9704 SetTabletCalibrationDataSWI__Fv
NewtonErr	GetTabletCalibrationDataSWI(void);			// ROM 0x000d9774 GetTabletCalibrationDataSWI__Fv

NewtonErr	GetPatchInfo(ULong* version, ULong* size);	// ROM 0x0011e078 GetPatchInfo__FPUlT1
NewtonErr	PrimGetPatchInfo(void);						// ROM 0x0011e0f4 PrimGetPatchInfo__Fv

// the supervisor halves, which the kernel's cases call (host)
void		PrimSetTabletCalibrationData(void);
void		PrimGetTabletCalibrationData(void);
void		SuperGetPatchInfo(ULong* version, ULong* size);

ULong		InternalRAMInfo(ULong which, ULong alignment);	// ROM 0x0011e318 InternalRAMInfo

#endif	/* __USERPERSISTENT_H */
