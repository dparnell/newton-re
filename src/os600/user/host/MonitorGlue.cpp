/*
	File:		user/host/MonitorGlue.cpp

	Contains:	Host stand-ins for the user-mode entry points in MonitorGlue.h.
				The kernel stores their addresses in a task's saved pc; nothing on
				the host resumes a task yet, so reaching one is a test error.
*/

#include "MonitorGlue.h"

#include <stdio.h>
#include <stdlib.h>

extern "C" void
MonitorEntryGlue(void)
{
	fprintf(stderr, "MonitorEntryGlue reached on the host\n");
	abort();
}

extern "C" void
TaskKillSelf(void)
{
	fprintf(stderr, "TaskKillSelf reached on the host\n");
	abort();
}

extern "C" void
Throw(char* name, void* data, void (*destructor)(void*))
{
	fprintf(stderr, "Throw(%s) reached on the host\n", name ? name : "");
	abort();
}
