/*
	File:		Domain.cpp

	Contains:	TKDomain, the domain access control word helpers and the fault
				monitor table.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Domain.h"
#include "OSErrors.h"
#include "hal/Atomic.h"


SFaultMonitorEntry gFaultMonitorTable[kNumberOfDomains];


/* -------------------------------------------------------------------------------
	Domain access control words
------------------------------------------------------------------------------- */

static inline ULong
DomainBits(long domainNumber, ULong bits)
{
	return bits << ((domainNumber & 0x7f) << 1);
}


// ROM 0x0009d758 DefaultDCR__Fv
// Client access to domains 0 and 1.
ULong
DefaultDCR()
{
	return 5;
}


// ROM 0x0009d77c AddClientToDCR__FUll
ULong
AddClientToDCR(ULong dcr, long domainNumber)
{
	if (domainNumber < 0)
		return dcr;
	return dcr | DomainBits(domainNumber, 1);
}


// ROM 0x0009d794 AddManagerToDCR__FUll
ULong
AddManagerToDCR(ULong dcr, long domainNumber)
{
	if (domainNumber < 0)
		return dcr;
	return dcr | DomainBits(domainNumber, 3);
}


// ROM 0x0009d760 RemoveFromDCR__FUll
ULong
RemoveFromDCR(ULong dcr, long domainNumber)
{
	if (domainNumber < 0)
		return dcr;
	return dcr & ~DomainBits(domainNumber, 3);
}


// ROM 0x0009d7ac GetSpecificDomainFromDCR__FRUll
long
GetSpecificDomainFromDCR(ULong& dcr, long domainNumber)
{
	ULong bits = DomainBits(domainNumber, 3);
	if (dcr & bits)
		return kNoDomainNumber;
	dcr |= bits;
	return domainNumber;
}


// ROM 0x0009d7d4 NextAvailDomainInDCR__FRUl
// Domain 15 is never handed out.
long
NextAvailDomainInDCR(ULong& dcr)
{
	for (ULong n = 0; n < kNumberOfDomains - 1; n++)
	{
		ULong bits = DomainBits(n, 3);
		if ((dcr & bits) == 0)
		{
			dcr |= bits;
			return n;
		}
	}
	return kNoDomainNumber;
}


/* -------------------------------------------------------------------------------
	Fault monitor table
------------------------------------------------------------------------------- */

// ROM 0x0009d814 RegisterFaultMonitor__FUlN21
void
RegisterFaultMonitor(ULong domainNumber, TObjectId domainId, TObjectId monitorId)
{
	EnterAtomic();
	gFaultMonitorTable[domainNumber].fDomainId = domainId;
	if (monitorId != 0)
		gFaultMonitorTable[domainNumber].fMonitorId = monitorId;
	ExitAtomic();
}


// ROM 0x0009d850 DeregisterFaultMonitorByDomainNumber__FUl
void
DeregisterFaultMonitorByDomainNumber(ULong domainNumber)
{
	EnterAtomic();
	gFaultMonitorTable[domainNumber].fDomainId = 0;
	gFaultMonitorTable[domainNumber].fMonitorId = 0;
	ExitAtomic();
}


/* -------------------------------------------------------------------------------
	TKDomain
------------------------------------------------------------------------------- */

// ROM 0x000b0714 __ct__8TKDomainFv
TKDomain::TKDomain()
{
	fBase = 0;
	fSize = 0;
	fFaultMonitorId = 0;
	fNumber = kNoDomainNumber;
	fNext = nil;
}


// ROM 0x000b0380 SetFaultMonitor__8TKDomainFUl
NewtonErr
TKDomain::SetFaultMonitor(TObjectId monitorId)
{
	fFaultMonitorId = monitorId;
	RegisterFaultMonitor(fNumber, fId, monitorId);
	return noErr;
}


// ROM 0x000b03a8 Intersects__8TKDomainFUlT1
Boolean
TKDomain::Intersects(VAddr base, VAddr end)
{
	return fBase < end && base < fBase + fSize;
}
