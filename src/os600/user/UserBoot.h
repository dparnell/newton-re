/*
	File:		UserBoot.h

	Contains:	The user side's part of booting.  UserInit (called by OsBoot)
				makes the handles on the object manager monitor and the null port;
				UserBoot is the body of the first task ('user'): it readies the
				user-level memory system, marks the OS running, and spawns the
				kernel services task ('ksrv', InitialKSRVTask), which starts the
				name server, the package manager and the first application world.

				A host build lets the program running the OS supply what
				InitialKSRVTask should start (gHostKernelServicesTask) while the
				services it starts on the MessagePad are not reconstructed.

	Reconstructed from:	UserInit 0x002574fc, UserBoot 0x002d1860, InitialKSRVTask 0x002d1954
*/

#ifndef __USERBOOT_H
#define __USERBOOT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

void	UserInit();
void	InitDomainsAndEnvironments();
NewtonErr	BuildDomainsAndHeaps(TObjectId kernelEnvId);
NewtonErr	BuildEnvironments();
void	UserBoot();
long	InitialKSRVTask();

// host: what the 'ksrv' task runs after the (not yet reconstructed) services;
// nil to run nothing
extern void (*gHostKernelServicesTask)();

#endif	/* __USERBOOT_H */
