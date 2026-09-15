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
