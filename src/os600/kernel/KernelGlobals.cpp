/*
	File:		KernelGlobals.cpp

	Contains:	Kernel-wide global variables (see KernelGlobals.h).
*/

#include "KernelGlobals.h"

TObjectTable*	gObjectTable = nil;
TObjectTable*	gTheMemArchObjTbl = nil;
TTask*			gCurrentTask = nil;
ULong			gNextGlobalUniqueId = 0;
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
