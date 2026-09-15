/*
	File:		hal/host/MMU.cpp

	Contains:	The MMU on a host: there is none; the domain access word is
				recorded so tests and the runtime can see what the kernel asked for.
*/

#include "hal/MMU.h"

ULong	gHostDomainAccess = 0;

extern "C" void
SetDomainAccessControl(ULong access)
{
	gHostDomainAccess = access;
}

extern "C" void
SetDomainRange(ULong /*base*/, ULong /*size*/, ULong /*domainNumber*/)
{
}

extern "C" void
ClearDomainRange(ULong /*base*/, ULong /*size*/)
{
}
