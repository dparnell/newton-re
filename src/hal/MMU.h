/*
	File:		hal/MMU.h

	Contains:	The memory-management unit as the kernel drives it.  So far only
				the domain access control word (ARM DACR, CP15 register 3), which
				the SWI exit path loads for the task about to run; page tables
				and domains' primary-table entries follow with the memory system.

	ROM:		SWIBoot exit path 0x003a43e4 (mcr p15,0,r0,c3,c3,0)
*/

#ifndef __HAL_MMU_H
#define __HAL_MMU_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#ifndef __KERNELTYPES_H
#include "KernelTypes.h"
#endif

extern "C" {
void	SetDomainAccessControl(ULong access);		// two bits per domain: 01 client, 11 manager (see os600/kernel/Domain.h)

// the primary (level 1) page table: mark a 1 MB-aligned range as belonging to
// a domain, or clear it (ROM: PrimSetDomainRange / PrimClearDomainRange, the
// kernel bodies behind GenericSWI 0x21/0x22; InitDomainPrimaryTable 0x00165630
// and ClearDomainPrimaryTable 0x0016562c are the names TKDomain uses)
void	SetDomainRange(ULong base, ULong size, ULong domainNumber);
void	ClearDomainRange(ULong base, ULong size);
}

// Map one 1 MB section of virtual space onto physical memory, without
// the jump table's help (the internal flash's windows are made this
// way: TNewInternalFlash::AlignAndMapVMRange).
void	AddNewSecPNJT(VAddr virtualAddr, PAddr physicalAddr, ULong domain, Perm perm, UChar cacheable);	// ROM 0x0015a5dc AddNewSecPNJT__FUlN214PermUc

// Where a virtual address that code outside the kernel was handed - one
// of the windows AddNewSecPNJT made - is in memory.  On the machine
// itself the MMU does this and the answer is the address; a port whose
// memory is not the Newton's own answers where it keeps those bytes
// (the host: hal/host/Host.h's HostRegisterPhysicalMemory), or nil for
// an address nothing is mapped at.
Ptr		VirtualAddressToPointer(VAddr virtualAddr);

#endif	/* __HAL_MMU_H */
