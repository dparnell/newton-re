/*
	File:		Domain.h

	Contains:	TKDomain, a kernel domain (kDomainType): a 1 MB-aligned range of
				virtual address space that maps onto one of the ARM MMU's 16
				protection domains and has a fault monitor.  Tasks see domains
				through their environment (TEnvironment), whose domain access
				control word says which domains a task may touch as a client or
				as a manager.

				The DCR helpers operate on such a word: two bits per domain, 01 =
				client, 11 = manager (the ARM DACR encoding).  The default word
				(DefaultDCR) grants client access to domains 0 and 1 - the
				kernel's own.

				A domain's number indexes the fault-monitor table the abort
				handler consults (RegisterFaultMonitor); the table itself has no
				name in the symbol table.

				User side: TUDomain (UserDomain.h).

	Reconstructed from:	TKDomain 0x000b0238-0x000b0758, DCR helpers 0x0009d758-0x0009d7d4,
				RegisterFaultMonitor 0x0009d814, DeregisterFaultMonitorByDomainNumber
				0x0009d850
*/

#ifndef __DOMAIN_H
#define __DOMAIN_H

#ifndef __KERNELOBJECT_H
#include "KernelObject.h"
#endif

const long kNoDomainNumber = -1;
const ULong kNumberOfDomains = 16;		// the ARM MMU has 16; NextAvailDomainInDCR hands out 0-14

// domain access control words
ULong	DefaultDCR();
ULong	AddClientToDCR(ULong dcr, long domainNumber);
ULong	AddManagerToDCR(ULong dcr, long domainNumber);
ULong	RemoveFromDCR(ULong dcr, long domainNumber);
long	GetSpecificDomainFromDCR(ULong& dcr, long domainNumber);	// claim that number; kNoDomainNumber if taken
long	NextAvailDomainInDCR(ULong& dcr);							// claim the lowest free number

// the abort handler's table: which monitor handles faults in each domain
struct SFaultMonitorEntry
{
	TObjectId		fMonitorId;			// +0x00  the fault monitor; 0 = none
	TObjectId		fDomainId;			// +0x04  the domain registered under this number
};
extern SFaultMonitorEntry gFaultMonitorTable[kNumberOfDomains];	// 0x0c1030b8 (unnamed in the ROM)

void	RegisterFaultMonitor(ULong domainNumber, TObjectId domainId, TObjectId monitorId);	// monitorId 0 keeps the current one
void	DeregisterFaultMonitorByDomainNumber(ULong domainNumber);


// ROM size 0x24
class TKDomain : public TKernelObject
{
	public:
						TKDomain();
						~TKDomain();
		NewtonErr		Init(TObjectId faultMonitorId, VAddr base, ULong size);							// next free domain number
		NewtonErr		InitWithDomainNumber(TObjectId faultMonitorId, VAddr base, ULong size, long domainNumber);
		NewtonErr		SetFaultMonitor(TObjectId monitorId);
		Boolean			Intersects(VAddr base, VAddr end);		// [base, end) overlaps this domain?

		TObjectId		fFaultMonitorId;	// +0x10
		VAddr			fBase;				// +0x14  1 MB aligned
		ULong			fSize;				// +0x18  a multiple of 1 MB
		long			fNumber;			// +0x1c  the MMU domain number, kNoDomainNumber until added
		TKDomain*		fNext;				// +0x20  TMemArchManager's list of domains
};

#endif	/* __DOMAIN_H */
