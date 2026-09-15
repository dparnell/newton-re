/*
	File:		user/Exceptions.cpp

	Contains:	The Newton exception system (NewtonExceptions.h): so far only the
				per-task initialisation TTask::Init needs.  Throw, the handler
				chain and the unwinding follow.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Newton.h"
#include "NewtonExceptions.h"


// ROM 0x000b12b4 InitializeExceptionGlobals
void
InitializeExceptionGlobals(ExceptionGlobals* globals)
{
	globals->firstCatch = nil;
}
