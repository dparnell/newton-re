/*
	File:		user/UserEnvironment.cpp

	Contains:	TUEnvironment.  Init makes the environment through the object
				manager; the rest are the environment system calls (GenericSWI
				0x25-0x27), dual-mode in the ROM - here the user-mode side.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "UserEnvironment.h"
#include "UserGlobals.h"
#include "UserMonitor.h"
#include "os600/ObjectMessage.h"
#include "os600/GenericSWISelectors.h"


// ROM 0x0025748c Init__13TUEnvironmentFPv
long
TUEnvironment::Init(void* heap)
{
	ObjectMessage msg;
	msg.fEnvironment.fHeap = heap;
	return MakeObject(kObjectEnvironment, &msg, kObjectMessage_EnvironmentSize);
}


// ROM 0x002574b4 Add__13TUEnvironmentFUlUcN22
long
TUEnvironment::Add(TObjectId domainId, Boolean isManager, Boolean isStack, Boolean isHeap)
{
	return GenericSWI(kGeneric_AddDomainToEnvironment, fId, domainId, (isManager ? 4 : 0) | (isStack ? 2 : 0) | (isHeap ? 1 : 0));
}


// ROM 0x002574ec Remove__13TUEnvironmentFUl
long
TUEnvironment::Remove(TObjectId domainId)
{
	return GenericSWI(kGeneric_RemoveDomainFromEnvironment, fId, domainId);
}


// ROM 0x002574f4 HasDomain__13TUEnvironmentFUlPUcT2
long
TUEnvironment::HasDomain(TObjectId domainId, Boolean* outHasDomain, Boolean* outIsManager)
{
	return GenericWithReturnSWI(kGeneric_EnvironmentHasDomain, fId, domainId, 0, (ULong*) outHasDomain, (ULong*) outIsManager, nil);
}
