/*
	File:		Environment.h

	Contains:	TEnvironment (kEnvironmentType): the set of domains a task may
				touch, as a domain access control word (see Domain.h), plus which
				of them holds the task's heap and which its stack.  Every task
				runs in an environment (TTask::fEnvironment); the environment is
				reference counted by the tasks using it and freed when the last
				one lets go after it has been removed from gTheMemArchManager.

				The environment system calls (GenericSWI 0x23-0x27) are here
				too.  In the ROM each is one function that acts directly in
				supervisor mode and issues the SWI otherwise; these are the
				kernel-mode bodies, the user side's wrappers follow with the
				syscall layer.

				User side: TUEnvironment (UserTasks.h), SetEnvironment & co.

	Reconstructed from:	TEnvironment 0x000b01f4-0x000b06c4, EnvironmentHasDomain
				0x000da2d0, AddDomainToEnvironment 0x000da3b4,
				RemoveDomainFromEnvironment 0x000da4bc, SetEnvironment 0x000daa2c,
				GetEnvironment 0x000daaf0; ObjectAlloc 0x0014a768 (size, no constructor)
*/

#ifndef __ENVIRONMENT_H
#define __ENVIRONMENT_H

#ifndef __KERNELOBJECT_H
#include "KernelObject.h"
#endif

class TKDomain;

// AddDomainToEnvironment flags
enum
{
	kEnvDomain_IsStack		= 1,
	kEnvDomain_IsHeap		= 2,
	kEnvDomain_IsManager	= 4
};


// ROM size 0x2c; no constructor - Init and TMemArchManager::AddEnvironment
// set every field
class TEnvironment : public TKernelObject
{
	public:
		NewtonErr		Init(void* unknown);
						~TEnvironment();

		NewtonErr		Add(TKDomain* domain, Boolean isManager, Boolean isHeap, Boolean isStack);
		NewtonErr		Remove(TKDomain* domain);
		void			IncrRefCount();
		Boolean			DecrRefCount();					// true: no users left and removed - delete it
		void			HasDomain(TKDomain* domain, Boolean* outHasDomain, Boolean* outIsManager);

		ULong			fDomainAccess;		// +0x10  domain access control word (loaded into the DACR on a task switch)
		void*			fUnknown14;			// +0x14  Init's argument (TUEnvironment::Init(void*)); use not yet traced
		TObjectId		fHeapDomainId;		// +0x18
		TObjectId		fStackDomainId;		// +0x1c
		long			fRefCount;			// +0x20  tasks running in it (SetEnvironment, TTask::Init/~TTask)
		Boolean			fRemoved;			// +0x24  taken out of gTheMemArchManager: free when fRefCount reaches 0
		TEnvironment*	fNext;				// +0x28  TMemArchManager's list
};


// system calls (GenericSWI 0x23-0x27)
NewtonErr	SetEnvironment(TObjectId newEnvId, TObjectId* outOldEnvId);
NewtonErr	GetEnvironment(TObjectId* outEnvId);
NewtonErr	AddDomainToEnvironment(TObjectId envId, TObjectId domainId, ULong flags);	// kEnvDomain_*
NewtonErr	RemoveDomainFromEnvironment(TObjectId envId, TObjectId domainId);
NewtonErr	EnvironmentHasDomain(TObjectId envId, TObjectId domainId, Boolean* outHasDomain, Boolean* outIsManager);

#endif	/* __ENVIRONMENT_H */
