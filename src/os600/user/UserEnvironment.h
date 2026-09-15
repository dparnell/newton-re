/*
	File:		UserEnvironment.h

	Contains:	TUEnvironment, the handle on a kernel environment (kEnvironmentType).
				The DDK does not ship this header (only UserTasks.h's environment
				functions), but the class is in the ROM and the boot uses it; its
				shape follows TUDomain's.

	Reconstructed from:	TUEnvironment 0x0025748c-0x002574f4
*/

#ifndef __USERENVIRONMENT_H
#define __USERENVIRONMENT_H

#ifndef __USEROBJECTS_H
#include "UserObjects.h"
#endif

class TUEnvironment : public TUObject
{
	public:
					TUEnvironment() : TUObject((TObjectId) 0) {}
					TUEnvironment(TObjectId id) : TUObject(id) {}
		void		operator=(TObjectId id) { CopyObject(id); }
		void		operator=(const TUEnvironment& copy) { CopyObject(copy); }

		long		Init(void* heap);						// a new environment with this default heap
		long		Add(TObjectId domainId, Boolean isManager = false, Boolean isStack = false, Boolean isHeap = false);
		long		Remove(TObjectId domainId);
		long		HasDomain(TObjectId domainId, Boolean* outHasDomain, Boolean* outIsManager);
};

#endif	/* __USERENVIRONMENT_H */
