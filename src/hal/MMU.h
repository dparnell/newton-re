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

extern "C" {
void	SetDomainAccessControl(ULong access);		// two bits per domain: 01 client, 11 manager (see os600/kernel/Domain.h)
}

#endif	/* __HAL_MMU_H */
