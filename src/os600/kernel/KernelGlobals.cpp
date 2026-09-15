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
TUObject*		gNullPort = nil;
TUObject*		gNameServer = nil;
TMonitor*		gTheObjectManagerMonitor = nil;

TObjectId		gCurrentMonitorId = 0;
TDoubleQContainer* gBlockedOnMemory = nil;
ULong			gRebootProtectCount = 0;
Boolean			gWantReboot = false;
SGlobalsThatLiveAcrossReboot gGlobalsThatLiveAcrossReboot;

Boolean			gTaskDestroyed = false;
ULong			gMonitorTaskPriority = kKernelTaskPriority;	// 20 in the ROM image
