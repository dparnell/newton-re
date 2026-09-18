/*
	File:		Environment.cpp

	Contains:	TEnvironment and the environment system calls.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Environment.h"
#include "Domain.h"
#include "MemArchManager.h"
#include "ObjectTable.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "OSErrors.h"


// ROM 0x000aeffc Init__12TEnvironmentFPv
NewtonErr
TEnvironment::Init(void* heap)
{
	fHeap = heap;
	fStackDomainId = 0;
	fHeapDomainId = 0;
	fRefCount = 0;
	fRemoved = false;
	gTheMemArchManager->AddEnvironment(this);
	return noErr;
}


// ROM 0x000af244 __dt__12TEnvironmentFv
TEnvironment::~TEnvironment()
{
	gTheMemArchManager->RemoveEnvironment(this);
}


// ROM 0x000af3fc Add__12TEnvironmentFP8TKDomainUcN22
NewtonErr
TEnvironment::Add(TKDomain* domain, Boolean isManager, Boolean isStack, Boolean isHeap)
{
	if (isManager)
		fDomainAccess = AddManagerToDCR(fDomainAccess, domain->fNumber);
	else
		fDomainAccess = AddClientToDCR(fDomainAccess, domain->fNumber);
	if (isStack)
		fStackDomainId = domain->fId;
	if (isHeap)
		fHeapDomainId = domain->fId;
	return noErr;
}


// ROM 0x000af460 Remove__12TEnvironmentFP8TKDomain
NewtonErr
TEnvironment::Remove(TKDomain* domain)
{
	fDomainAccess = RemoveFromDCR(fDomainAccess, domain->fNumber);
	return noErr;
}


// ROM 0x000af488 IncrRefCount__12TEnvironmentFv
void
TEnvironment::IncrRefCount()
{
	fRefCount++;
}


// ROM 0x000af498 DecrRefCount__12TEnvironmentFv
// When the last user of a removed environment lets go it disowns itself
// (so the object table will scavenge it) and asks to be deleted.
Boolean
TEnvironment::DecrRefCount()
{
	if (--fRefCount == 0 && fRemoved)
	{
		fOwnerId = 0;
		return true;
	}
	return false;
}


// ROM 0x000af4cc HasDomain__12TEnvironmentFP8TKDomainPUcT2
void
TEnvironment::HasDomain(TKDomain* domain, Boolean* outHasDomain, Boolean* outIsManager)
{
	switch ((fDomainAccess >> ((domain->fNumber & 0x7f) << 1)) & 3)
	{
	case 0:
	case 2:
		*outHasDomain = false;
		*outIsManager = false;
		break;
	case 1:
		*outHasDomain = true;
		*outIsManager = false;
		break;
	case 3:
		*outHasDomain = true;
		*outIsManager = true;
		break;
	}
}


/* -------------------------------------------------------------------------------
	System calls
------------------------------------------------------------------------------- */

static TEnvironment*
EnvironmentFromId(TObjectId id)
{
	return ObjectType(id) == kEnvironmentType ? (TEnvironment*) gObjectTable->Get(id) : nil;
}


static TKDomain*
DomainFromId(TObjectId id)
{
	return ObjectType(id) == kDomainType ? (TKDomain*) gObjectTable->Get(id) : nil;
}


// ROM 0x000d99b4 SetEnvironment__FUlPUl
// GenericSWI 0x23: switch the current task to another environment.  The old
// one's refcount drops but it is not freed here even if that was its last use.
NewtonErr
SetEnvironment(TObjectId newEnvId, TObjectId* outOldEnvId)
{
	TEnvironment* env = EnvironmentFromId(newEnvId);
	if (env == nil)
		return kError_Bad_ObjectId;
	gCurrentTask->fEnvironment->DecrRefCount();
	*outOldEnvId = gCurrentTask->fEnvironment->fId;
	gCurrentTask->fEnvironment = env;
	env->IncrRefCount();
	return noErr;
}


// ROM 0x000d9a78 GetEnvironment__FPUl
// GenericSWI 0x24.
NewtonErr
GetEnvironment(TObjectId* outEnvId)
{
	*outEnvId = gCurrentTask->fEnvironment->fId;
	return noErr;
}


// ROM 0x000d933c AddDomainToEnvironment__FUlN21
// GenericSWI 0x25.
NewtonErr
AddDomainToEnvironment(TObjectId envId, TObjectId domainId, ULong flags)
{
	TEnvironment* env = EnvironmentFromId(envId);
	TKDomain* domain = DomainFromId(domainId);
	if (env == nil || domain == nil)
		return kError_Bad_Parameters;
	return env->Add(domain, (flags & kEnvDomain_IsManager) != 0, (flags & kEnvDomain_IsStack) != 0, (flags & kEnvDomain_IsHeap) != 0);
}


// ROM 0x000d9444 RemoveDomainFromEnvironment__FUlT1
// GenericSWI 0x26.
NewtonErr
RemoveDomainFromEnvironment(TObjectId envId, TObjectId domainId)
{
	TEnvironment* env = EnvironmentFromId(envId);
	TKDomain* domain = DomainFromId(domainId);
	if (env == nil || domain == nil)
		return kError_Bad_Parameters;
	return env->Remove(domain);
}


// ROM 0x000d9258 EnvironmentHasDomain__FUlT1PUcT3
// GenericSWI 0x27.
NewtonErr
EnvironmentHasDomain(TObjectId envId, TObjectId domainId, Boolean* outHasDomain, Boolean* outIsManager)
{
	TEnvironment* env = EnvironmentFromId(envId);
	if (env == nil)
		return kError_Bad_ObjectId;
	TKDomain* domain = DomainFromId(domainId);
	if (domain == nil)
		return kError_Bad_ObjectId;
	env->HasDomain(domain, outHasDomain, outIsManager);
	return noErr;
}
