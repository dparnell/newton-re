/*
	File:		KernelObjects.h

	Contains:	Registering kernel objects and passing ownership between tasks.
*/

#ifndef __KERNELOBJECTS_H
#define __KERNELOBJECTS_H

#ifndef __KERNELOBJECT_H
#include "KernelObject.h"
#endif

class TSharedMem;

NewtonErr	RegisterObject(TKernelObject* object, KernelTypes type, TObjectId owner, TObjectId* outId);

// Resolving ids.  The "built-in" ids kBuiltInSMemMsgId (1), kBuiltInSMemId (2)
// and kBuiltInSMemMonitorFaultId (3) of SharedTypes.h stand for the current
// task's own message, its shared memory, and its monitor's message.
TObjectId	LocalToGlobalId(TObjectId id);
NewtonErr	ConvertIdToObj(KernelTypes type, TObjectId id, void* outObject);	// outObject: pointer to a pointer, may be nil
NewtonErr	ConvertMemOrMsgIdToObj(TObjectId id, TSharedMem** outObject);
NewtonErr	GiveObject(TObjectId id, TObjectId assignToTaskId);		// GenericSWI 1
NewtonErr	AcceptObject(TObjectId id);								// GenericSWI 2

#endif	/* __KERNELOBJECTS_H */
