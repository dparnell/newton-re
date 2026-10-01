/*
	File:		GenericSWI.cpp

	Contains:	GenericSWIHandler.

	Reconstructed from the MP2x00 US ROM.  Selectors whose kernel routine is
	not reconstructed yet are marked NOT YET RECONSTRUCTED and return
	kError_Call_Not_Implemented (the ROM's own answer for a selector it has
	no case for is kGenericSWI_UnknownSelector, -1).
*/

#include "GenericSWI.h"
#include "KernelGlobals.h"
#include "KernelObjects.h"
#include "ObjectTable.h"
#include "Task.h"
#include "TaskSwitch.h"
#include "Scheduler.h"
#include "Semaphore.h"
#include "Port.h"
#include "Environment.h"
#include "Reboot.h"
#include "MemObjManager.h"
#include "RealTimeClock.h"
#include "OSErrors.h"
#include "CompMath.h"
#include "hal/Atomic.h"
#include "hal/Timer.h"
#include "hal/Power.h"
#include "UserPersistent.h"


// GenericSWI 3: the global time, or a task's accumulated run time - for the
// running task including the current stretch, less interrupt overhead - in
// the caller's r1 (lo) and r2 (hi).
static long
GetTaskTimeSelector(ULong taskId)
{
	Int64 time = { 0, 0 };
	long err = noErr;
	if (taskId == 0)
		GetClock(&time);
	else
	{
		TTask* task = ObjectType(taskId) == kTaskType ? (TTask*) gObjectTable->Get(taskId) : nil;
		if (task == nil)
			err = kError_Task_Does_Not_Exist;
		else if (task == gCurrentTask)
		{
			GetClock(&time);
			CompSub(&gTaskTimeStart, &time);
			Int64 overhead = { 0, gIRQInterruptOverHead + gFIQInterruptOverHead };
			CompSub(&overhead, &time);
			CompAdd(&task->fTaskTime, &time);
		}
		else
			time = task->fTaskTime;
	}
	gCurrentTask->fRegister[1] = time.lo;
	gCurrentTask->fRegister[2] = time.hi;
	return err;
}


// ROM 0x001925c4 PowerOffSystemKernelGlue__Fv
// GenericSWI 0x44: the platform driver turns the machine off; it returns
// when something has turned it on again.
static void
PowerOffSystemKernelGlue(void)
{
	PlatformPowerOffSystem();
}


// ROM 0x000d8a64 GenericSWIHandler
long
GenericSWIHandler(ULong selector, ULong p1, ULong p2, ULong p3, ULong p4)
{
	TRegister* r = gCurrentTask->fRegister;
	long result = kError_Bad_Parameters;
	switch (selector)
	{
	case kGeneric_SetGlobals:
		gCurrentTask->fGlobals = (void*) (uintptr_t) p1;
		return noErr;
	case kGeneric_GiveObject:
		return GiveObject(p1, p2);
	case kGeneric_AcceptObject:
		return AcceptObject(p1);
	case kGeneric_GetTaskTime:
		return GetTaskTimeSelector(p1);
	case kGeneric_ResetAccountTime:
		return ResetAccountTimeKernelGlue();
	case kGeneric_GetNextTaskId:
		{
			TObjectId id;
			result = GetNextTaskIdKernelGlue(p1, &id);
			if (result == noErr)
				r[1] = id;
			return result;
		}
	case kGeneric_ReleaseBlockedOnMemory:
		{
			TTask* task;
			while ((task = (TTask*) gBlockedOnMemory->Remove()) != nil)
				ScheduleTask(task);
			return noErr;
		}
	case kGeneric_YieldToTask:
		{
			TTask* task = ObjectType(p1) == kTaskType ? (TTask*) gObjectTable->Get(p1) : nil;
			if (task == nil)
				return kError_Bad_Object;
			gKernelScheduler->fPreferredTask = task;
			WantSchedule();
			return noErr;
		}
	case kGeneric_Reboot:
		return Reboot((NewtonErr) p1, p2, (Boolean) p3);
	case kGeneric_Restart:
		Restart();
		return noErr;
	case kGeneric_SetEnvironment:
		{
			TObjectId old;
			result = SetEnvironment(p1, &old);
			if (result == noErr)
				r[1] = old;
			return result;
		}
	case kGeneric_GetEnvironment:
		{
			TObjectId id;
			result = GetEnvironment(&id);
			r[1] = id;
			return result;
		}
	case kGeneric_AddDomainToEnvironment:
		return AddDomainToEnvironment(p1, p2, p3);
	case kGeneric_RemoveDomainFromEnvironment:
		return RemoveDomainFromEnvironment(p1, p2);
	case kGeneric_EnvironmentHasDomain:
		{
			Boolean has = false, isManager = false;
			result = EnvironmentHasDomain(p1, p2, &has, &isManager);
			r[1] = has;
			r[2] = isManager;
			return result;
		}
	case kGeneric_SemGroupSetRefCon:
		return SemGroupSetRefCon(p1, (void*) (uintptr_t) p2);
	case kGeneric_SemGroupGetRefCon:
		{
			void* refCon;
			result = SemGroupGetRefCon(p1, &refCon);
			r[1] = (TRegister) refCon;
			return result;
		}
	case kGeneric_SetBequeathId:
		gCurrentTask->SetBequeathId(p1);
		return noErr;
	case kGeneric_PortReset:
		PortResetKernelGlue(p1, p2, p3);
		return (long) r[0];
	case kGeneric_GetMemObjInfo:
		return PrimGetMemObjInfo();
	case kGeneric_RealTimeClockDispatch:
		return RealTimeClockDispatch();
	case kGeneric_GetTaskStackInfo:
		{
			TTask* task = gCurrentTask;
			if (p1 != 0)
				task = ObjectType(p1) == kTaskType ? (TTask*) gObjectTable->Get(p1) : nil;
			if (task == nil)
				return kError_Bad_Parameters;
			r[1] = task->fStackTop;
			r[2] = task->fStackBase;
			return noErr;
		}

	case kGeneric_Unused4:
	case kGeneric_Unused15:
	case kGeneric_Unused16:
	case kGeneric_Unused24:
	case kGeneric_Unused25:
		return kGenericSWI_UnknownSelector;

	case kGeneric_ForgetPhysMapping:
	case kGeneric_ForgetPermMapping:
	case kGeneric_ForgetMapping:
	case kGeneric_RememberPhysMapping:
	case kGeneric_RememberPermMapping:
	case kGeneric_RememberMapping:
	case kGeneric_AddPageTable:
	case kGeneric_RemovePageTable:
	case kGeneric_ReleasePage:
	case kGeneric_CopyPhysPg:
	case kGeneric_InvalidatePhys:
	case kGeneric_PhysSize:
	case kGeneric_PhysBase:
	case kGeneric_PhysAlign:
	case kGeneric_PhysReadOnly:
	case kGeneric_AddPgPAndPermWithPageTable:
	case kGeneric_SetDomainRange:
	case kGeneric_ClearDomainRange:
	case kGeneric_VToP:
	case kGeneric_RemovePMappings:
	case kGeneric_MakePhysInaccessible:
	case kGeneric_MakePhysAccessible:
	case kGeneric_ForgetPhysMapping2:
	case kGeneric_RememberPhysMapping2:
	case kGeneric_ChangeVirtualMapping:
	case kGeneric_ReleasePageTable:
	case kGeneric_CleanPageInIandDCache:
	case kGeneric_CleanDCandFlushIC:
	case kGeneric_CleanPageInDC:
	case kGeneric_CleanRangeInDC:
	case kGeneric_CopyPagesAfterStackCollided:
		// NOT YET RECONSTRUCTED: the memory system (page tables, TPhys,
		// caches, the stack manager) - 0x000d9adc cases 7-0x22, 0x2a, 0x31,
		// 0x39-0x3f, 0x46-0x4a
		return kError_Call_Not_Implemented;
	case kGeneric_RegisterDelayedFunction:
	case kGeneric_RemoveDelayedFunction:
		// NOT YET RECONSTRUCTED: PrimRegisterDelayedFunction / PrimRemoveDelayedFunction
		return kError_Call_Not_Implemented;
	case kGeneric_GetPatchInfo:
		return PrimGetPatchInfo();
	case kGeneric_SetTabletCalibrationData:
		PrimSetTabletCalibrationData();			// (SetTabletCalibrationDataSWI, in supervisor mode)
		return noErr;
	case kGeneric_GetTabletCalibrationData:
		PrimGetTabletCalibrationData();			// (GetTabletCalibrationDataSWI, in supervisor mode)
		return noErr;
	case kGeneric_GetNetworkPersistentInfo:
	case kGeneric_ResetRebootReason:
	case kGeneric_ClearFIQAtomic:
	case kGeneric_RegisterLoadedCodeWithDebugger:
	case kGeneric_DeregisterLoadedCodeWithDebugger:
	case kGeneric_InformDebuggerMemoryReloaded:
	case kGeneric_BackupPatch:
	case kGeneric_RExConfigEntry:
	case kGeneric_ReadGlobalsWord:
	case kGeneric_LastRExConfigEntry:
	case kGeneric_RegisterPackageWithDebugger:
	case kGeneric_PauseSystem:
		// NOT YET RECONSTRUCTED: platform and debugger services - 0x000d9adc
		// cases 0x2b, 0x2d, 0x30, 0x32, 0x35-0x38, 0x3b, 0x40-0x42, 0x45
		return kError_Call_Not_Implemented;
	case kGeneric_PowerOffSystem:
		PowerOffSystemKernelGlue();
		return noErr;
	default:
		return kGenericSWI_UnknownSelector;
	}
}
