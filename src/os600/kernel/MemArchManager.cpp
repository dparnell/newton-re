/*
	File:		MemArchManager.cpp

	Contains:	TMemArchManager.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "MemArchManager.h"
#include "Domain.h"
#include "Environment.h"
#include "OSErrors.h"


TMemArchManager*	gTheMemArchManager = nil;


// ROM 0x000b03d0 __ct__15TMemArchManagerFv
TMemArchManager::TMemArchManager()
{
	fEnvironments = nil;
	fDomains = nil;
	fDomainsInUse = DefaultDCR();
}


// ROM 0x000b0410 AddEnvironment__15TMemArchManagerFP12TEnvironment
// A new environment starts with the default access word.
void
TMemArchManager::AddEnvironment(TEnvironment* env)
{
	env->fDomainAccess = DefaultDCR();
	env->fNext = fEnvironments;
	fEnvironments = env;
}


// ROM 0x000b0478 RemoveEnvironment__15TMemArchManagerFP12TEnvironment
// Unlinks the environment and marks it removed, so that the last
// DecrRefCount reports it can be deleted.
// DEVIATION: the ROM's loop never advances to the next environment (nor
// tracks the previous one), so it spins forever unless the list is empty;
// evidently never reached in practice.  Written as intended.
void
TMemArchManager::RemoveEnvironment(TEnvironment* env)
{
	TEnvironment* prev = nil;
	for (TEnvironment* e = fEnvironments; e != nil; prev = e, e = e->fNext)
	{
		if (e == env)
		{
			if (prev != nil)
				prev->fNext = e->fNext;
			else
				fEnvironments = e->fNext;
			e->fRemoved = true;
			e->fNext = nil;
			return;
		}
	}
}


// ROM 0x000b04c0 AddDomainWithDomainNumber__15TMemArchManagerFP8TKDomainl
NewtonErr
TMemArchManager::AddDomainWithDomainNumber(TKDomain* domain, long domainNumber)
{
	domain->fNumber = GetSpecificDomainFromDCR(fDomainsInUse, domainNumber);
	if (domain->fNumber < 0)
		return kError_Bad_Parameters;
	domain->fNext = fDomains;
	fDomains = domain;
	return noErr;
}


// ROM 0x000b0508 AddDomain__15TMemArchManagerFP8TKDomain
NewtonErr
TMemArchManager::AddDomain(TKDomain* domain)
{
	domain->fNumber = NextAvailDomainInDCR(fDomainsInUse);
	if (domain->fNumber < 0)
		return kError_Out_Of_Domains;
	domain->fNext = fDomains;
	fDomains = domain;
	return noErr;
}


// ROM 0x000b054c RemoveDomain__15TMemArchManagerFP8TKDomain
// Unlinks the domain; its number is not returned to fDomainsInUse.
void
TMemArchManager::RemoveDomain(TKDomain* domain)
{
	TKDomain* prev = nil;
	for (TKDomain* d = fDomains; d != nil; prev = d, d = d->fNext)
	{
		if (d == domain)
		{
			if (prev == nil)
				fDomains = d->fNext;
			else
				prev->fNext = d->fNext;
			d->fNumber = kNoDomainNumber;
			d->fNext = nil;
			return;
		}
	}
}


// ROM 0x000b05a4 DomainRangeIsFree__15TMemArchManagerFUlT1
// end is the last address of the range (TKDomain::Init passes base + size - 1).
Boolean
TMemArchManager::DomainRangeIsFree(VAddr base, VAddr end)
{
	for (TKDomain* d = fDomains; d != nil; d = d->fNext)
		if (d->Intersects(base, end))
			return false;
	return true;
}
