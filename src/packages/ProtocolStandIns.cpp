/*
	File:		packages/ProtocolStandIns.cpp

	Contains:	The protocol stand-ins (ProtocolStandIns.h).

	Host-only: there is nothing of the ROM's here.
*/

#include "ProtocolStandIns.h"
#include "Protocols.h"

#include <string.h>


static const TClassInfo*	gStandIns[64];
static long					gStandInCount = 0;


void
RegisterProtocolStandIn(const TClassInfo* info)
{
	for (long i = 0; i < gStandInCount; i++)
		if (gStandIns[i] == info)
			return;
	if (gStandInCount < (long) (sizeof(gStandIns) / sizeof(gStandIns[0])))
		gStandIns[gStandInCount++] = info;
}


const TClassInfo*
ProtocolStandInFor(const char* implementation, const char* interface)
{
	for (long i = 0; i < gStandInCount; i++)
		if (strcmp(gStandIns[i]->ImplementationName(), implementation) == 0
		&&  strcmp(gStandIns[i]->InterfaceName(), interface) == 0)
			return gStandIns[i];
	return nil;
}


Boolean
IsProtocolStandIn(const void* classInfo)
{
	for (long i = 0; i < gStandInCount; i++)
		if ((const void*) gStandIns[i] == classInfo)
			return true;
	return false;
}


static ARMProtocolPartLoader	gARMLoader = nil;

void
SetARMProtocolPartLoader(ARMProtocolPartLoader loader)
{
	gARMLoader = loader;
}


const TClassInfo*
LoadARMProtocolPartFallback(const void* part, ULong size, const void* package, ULong partOffset)
{
	return gARMLoader != nil ? gARMLoader(part, size, package, partOffset) : nil;
}
