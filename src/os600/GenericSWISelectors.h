/*
	File:		GenericSWISelectors.h

	Contains:	The selectors of GenericSWI (SWI 5), shared by the user side that
				issues them and the kernel's GenericSWIHandler that dispatches
				them.  docs/os600/swi-table.md lists each with its kernel routine.

	Reconstructed from:	GenericSWIHandler 0x000d9adc and the user-side wrappers
*/

#ifndef __GENERICSWISELECTORS_H
#define __GENERICSWISELECTORS_H

// the selectors, as the ROM's user-side wrappers use them
enum GenericSWISelectors
{
	kGeneric_SetGlobals = 0,			// gCurrentTask->fGlobals = p1
	kGeneric_GiveObject,				// (id, toTaskId)
	kGeneric_AcceptObject,				// (id)
	kGeneric_GetTaskTime,				// (taskId or 0 for the global time) -> r1 = lo, r2 = hi
	kGeneric_Unused4,
	kGeneric_ResetAccountTime,
	kGeneric_GetNextTaskId,				// (afterId) -> r1
	kGeneric_ForgetPhysMapping,			// 7: the physical-memory mapping calls (memory system)
	kGeneric_ForgetPermMapping,
	kGeneric_ForgetMapping,
	kGeneric_RememberPhysMapping,
	kGeneric_RememberPermMapping,
	kGeneric_RememberMapping,
	kGeneric_AddPageTable,
	kGeneric_RemovePageTable,
	kGeneric_Unused15,
	kGeneric_Unused16,
	kGeneric_ReleasePage,				// 0x11
	kGeneric_CopyPhysPg,
	kGeneric_InvalidatePhys,
	kGeneric_PhysSize,
	kGeneric_PhysBase,
	kGeneric_PhysAlign,
	kGeneric_PhysReadOnly,
	kGeneric_Unused24,
	kGeneric_Unused25,
	kGeneric_ReleaseBlockedOnMemory,	// 0x1a: every task parked on gBlockedOnMemory runs again
	kGeneric_YieldToTask,				// 0x1b: (taskId) make it the preferred task and reschedule
	kGeneric_Reboot,					// 0x1c: (error, rebootType, safe)
	kGeneric_Restart,					// 0x1d
	kGeneric_RegisterDelayedFunction,	// 0x1e
	kGeneric_RemoveDelayedFunction,		// 0x1f
	kGeneric_AddPgPAndPermWithPageTable,// 0x20
	kGeneric_SetDomainRange,			// 0x21
	kGeneric_ClearDomainRange,			// 0x22
	kGeneric_SetEnvironment,			// 0x23: (envId) -> r1 = old
	kGeneric_GetEnvironment,			// 0x24: -> r1
	kGeneric_AddDomainToEnvironment,	// 0x25: (envId, domainId, flags)
	kGeneric_RemoveDomainFromEnvironment,// 0x26
	kGeneric_EnvironmentHasDomain,		// 0x27: (envId, domainId) -> r1 = has, r2 = isManager
	kGeneric_SemGroupSetRefCon,			// 0x28
	kGeneric_SemGroupGetRefCon,			// 0x29: -> r1
	kGeneric_VToP,						// 0x2a
	kGeneric_RealTimeClockDispatch,		// 0x2b
	kGeneric_GetMemObjInfo,				// 0x2c
	kGeneric_GetNetworkPersistentInfo,	// 0x2d
	kGeneric_SetBequeathId,				// 0x2e: (taskId)
	kGeneric_GetPatchInfo,				// 0x2f
	kGeneric_ResetRebootReason,			// 0x30
	kGeneric_RemovePMappings,			// 0x31
	kGeneric_ClearFIQAtomic,			// 0x32
	kGeneric_SetTabletCalibrationData,	// 0x33
	kGeneric_GetTabletCalibrationData,	// 0x34
	kGeneric_RegisterLoadedCodeWithDebugger,	// 0x35
	kGeneric_DeregisterLoadedCodeWithDebugger,	// 0x36
	kGeneric_InformDebuggerMemoryReloaded,		// 0x37
	kGeneric_BackupPatch,				// 0x38
	kGeneric_MakePhysInaccessible,		// 0x39
	kGeneric_MakePhysAccessible,		// 0x3a
	kGeneric_RExConfigEntry,			// 0x3b
	kGeneric_ForgetPhysMapping2,		// 0x3c
	kGeneric_RememberPhysMapping2,		// 0x3d
	kGeneric_ChangeVirtualMapping,		// 0x3e
	kGeneric_ReleasePageTable,			// 0x3f
	kGeneric_ReadGlobalsWord,			// 0x40: (index) -> a word of the 0x0c103778 table
	kGeneric_LastRExConfigEntry,		// 0x41
	kGeneric_RegisterPackageWithDebugger,	// 0x42
	kGeneric_PortReset,					// 0x43: (portId, senderFlags, receiverFlags)
	kGeneric_PowerOffSystem,			// 0x44
	kGeneric_PauseSystem,				// 0x45
	kGeneric_CleanPageInIandDCache,		// 0x46
	kGeneric_CleanDCandFlushIC,			// 0x47
	kGeneric_CleanPageInDC,				// 0x48
	kGeneric_CleanRangeInDC,			// 0x49
	kGeneric_CopyPagesAfterStackCollided,	// 0x4a
	kGeneric_GetTaskStackInfo			// 0x4b: (taskId or 0) -> r1 = stack top, r2 = stack base
};

#endif	/* __GENERICSWISELECTORS_H */
