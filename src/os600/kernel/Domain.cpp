/*
	File:		Domain.cpp

	Contains:	TKDomain, the domain access control word helpers and the fault
				monitor table.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Domain.h"
#include "OSErrors.h"
#include "hal/Atomic.h"
#include "hal/MMU.h"
#include "MemArchManager.h"


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


// The range must be 1 MB aligned in base and size and overlap no other
// domain; kError_Ill_Formed_Domain otherwise.  base + size - 1 is what the
// manager's overlap check takes.
static Boolean
RangeIsWellFormed(VAddr base, ULong size)
{
	VAddr end = base + size - 1;
	if (base > end)
		return false;
	if ((base & 0xfffff) != 0 || (size & 0xfffff) != 0)		// the ROM tests base << 12 == 0
		return false;
	return gTheMemArchManager->DomainRangeIsFree(base, end);
}


// ROM 0x000b02d4 Init__8TKDomainFUlN21
// A domain on the next free number: its primary page table entries are set
// up and its fault monitor registered.
NewtonErr
TKDomain::Init(TObjectId faultMonitorId, VAddr base, ULong size)
{
	fBase = base;
	fSize = size;
	if (!RangeIsWellFormed(base, size))
		return kError_Ill_Formed_Domain;
	NewtonErr err = gTheMemArchManager->AddDomain(this);
	SetDomainRange((ULong) base, size, fNumber);		// InitDomainPrimaryTable
	SetFaultMonitor(faultMonitorId);
	return err;
}


// ROM 0x000b0238 InitWithDomainNumber__8TKDomainFUlN31
// The same on a given number (the kernel's own domain); the page table is
// left as the boot set it up.
NewtonErr
TKDomain::InitWithDomainNumber(TObjectId faultMonitorId, VAddr base, ULong size, long domainNumber)
{
	fBase = base;
	fSize = size;
	if (!RangeIsWellFormed(base, size))
		return kError_Ill_Formed_Domain;
	NewtonErr err = gTheMemArchManager->AddDomainWithDomainNumber(this, domainNumber);
	SetFaultMonitor(faultMonitorId);
	return err;
}


// ROM 0x000b0758 __dt__8TKDomainFv
// DEVIATION: the ROM deregisters the fault monitor for fNumber even when
// the domain never got a number (-1), writing two words before the table;
// a domain without a number has nothing registered, so it is skipped.
TKDomain::~TKDomain()
{
	if (fNumber != kNoDomainNumber)
		DeregisterFaultMonitorByDomainNumber(fNumber);
	ClearDomainRange((ULong) fBase, fSize);			// ClearDomainPrimaryTable
	gTheMemArchManager->RemoveDomain(this);
}
