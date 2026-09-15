/*
	File:		KernelObjects.h

	Contains:	Registering kernel objects and passing ownership between tasks.
*/

#ifndef __KERNELOBJECTS_H
#define __KERNELOBJECTS_H

#ifndef __KERNELOBJECT_H
#include "KernelObject.h"
#endif

NewtonErr	RegisterObject(TKernelObject* object, KernelTypes type, TObjectId owner, TObjectId* outId);
NewtonErr	GiveObject(TObjectId id, TObjectId assignToTaskId);		// GenericSWI 1
NewtonErr	AcceptObject(TObjectId id);								// GenericSWI 2

#endif	/* __KERNELOBJECTS_H */
