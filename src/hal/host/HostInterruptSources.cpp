/*
	File:		hal/host/HostInterruptSources.cpp

	Contains:	The host's interrupt-source registry - see
				HostInterruptSources.h.
*/

#include "hal/host/HostInterruptSources.h"
#include "CompMath.h"

struct HostInterruptSource
{
	HostInterruptDeadlineProc	fDeadline;
	HostInterruptDeliverProc	fDeliver;
};

const int					kMaxSources = 8;
static HostInterruptSource	gSources[kMaxSources];
static int					gSourceCount = 0;


Boolean
HostRegisterInterruptSource(HostInterruptDeadlineProc deadline, HostInterruptDeliverProc deliver)
{
	for (int i = 0; i < gSourceCount; i++)
		if (gSources[i].fDeadline == deadline && gSources[i].fDeliver == deliver)
			return true;
	if (gSourceCount == kMaxSources)
		return false;
	gSources[gSourceCount].fDeadline = deadline;
	gSources[gSourceCount].fDeliver = deliver;
	gSourceCount++;
	return true;
}


void
HostUnregisterInterruptSource(HostInterruptDeadlineProc deadline, HostInterruptDeliverProc deliver)
{
	for (int i = 0; i < gSourceCount; i++)
		if (gSources[i].fDeadline == deadline && gSources[i].fDeliver == deliver)
		{
			gSources[i] = gSources[--gSourceCount];
			return;
		}
}


void
HostDeliverInterruptSources(const Int64* now)
{
	for (int i = 0; i < gSourceCount; i++)
	{
		Int64 when;
		if (gSources[i].fDeadline(&when) && CompCompare(now, &when) >= 0)
			gSources[i].fDeliver();
	}
}


Boolean
HostInterruptSourcesDeadline(Int64* deadline)
{
	Boolean have = false;
	for (int i = 0; i < gSourceCount; i++)
	{
		Int64 when;
		if (gSources[i].fDeadline(&when) && (!have || CompCompare(&when, deadline) < 0))
		{
			*deadline = when;
			have = true;
		}
	}
	return have;
}
