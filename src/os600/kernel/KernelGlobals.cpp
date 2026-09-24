/*
	File:		KernelGlobals.cpp

	Contains:	Kernel-wide global variables (see KernelGlobals.h).
*/

#include "KernelGlobals.h"
#include "VirtualMemory.h"

TObjectTable*	gObjectTable = nil;
TObjectTable*	gTheMemArchObjTbl = nil;
TTask*			gCurrentTask = nil;
ULong			gNextGlobalUniqueId = 0x100;		// as in the ROM image: ids below (0x100 << 4) are never handed out
Boolean			gWrappedGlobalUniqueId = false;

TScheduler*		gKernelScheduler = nil;
TTask*			gIdleTask = nil;
ULong			gTaskPriority = 0;
Boolean			gSchedule = false;
Boolean			gScheduleRequested = false;
ULong			gHoldScheduleLevel = 0;
Boolean			gWantSchedulerToRun = false;
Boolean			gSchedulerRunning = false;
InterruptObject* gSchedulerIntObj = nil;

TTimerEngine*	gTimerEngine = nil;
TDoubleQContainer* gTimerDeferred = nil;
Boolean			gWantDeferred = false;
ULong			gTimerInterruptCount = 0;

TDoubleQContainer* gCopyTasks = nil;
TDoubleQContainer* gDeferredSends = nil;
Boolean			gCopyDone = false;
TPort*			gNullPort = nil;
TUObject*		gNameServer = nil;
TMonitor*		gTheObjectManagerMonitor = nil;

TObjectId		gCurrentMonitorId = 0;
TDoubleQContainer* gBlockedOnMemory = nil;
ULong			gRebootProtectCount = 0;
Boolean			gWantReboot = false;
SGlobalsThatLiveAcrossReboot gGlobalsThatLiveAcrossReboot;

// ROM 0x0c104f50 gCollectCPUStats - whether the four power counters in
// those globals are kept up to date (EnablePowerStats sets it).
// DEVIATION: nothing on the host counts them.
ULong gCollectCPUStats = 0;

TObjectId		gCurrentTaskId = 0;
void*			gCurrentGlobals = nil;
TTask*			gCurrentMemCountTask = nil;
ULong			gPtrsUsed = 0;
ULong			gHandlesUsed = 0;
ULong			gSavedPtrsUsed = 0;
ULong			gSavedHandlesUsed = 0;
ULong			gNumberOfTaskSwaps = 0;
Boolean			gCountTaskTime = false;
TTask*			gCurrentTimedTask = nil;
Int64			gTaskTimeStart = { 0, 0 };
Int64			gLastTaskEndTime = { 0, 0 };
Int64			gFirstTaskEndTime = { 0, 0 };
Boolean			gTaskEndTimeInvalid = true;
Int64			gDeadTaskTime = { 0, 0 };
ULong			gFIQInterruptOverHead = 0;
ULong			gIRQInterruptOverHead = 0;
ULong			gFIQAccumulatedIntOverHead = 0;
ULong			gIRQAccumulatedIntOverHead = 0;

Boolean			gOSIsRunning = false;
Boolean			gTaskDestroyed = false;
ULong			gMonitorTaskPriority = kKernelTaskPriority;	// 20 in the ROM image
