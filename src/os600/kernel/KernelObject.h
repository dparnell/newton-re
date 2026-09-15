/*
	File:		KernelObject.h

	Contains:	TKernelObject, the header every kernel object starts with
				(ports, tasks, monitors, semaphores, shared memory, domains,
				environments, physical pages).  The object table links objects
				through it and ownership is recorded in it.

				A TObjectId is the object's unique number shifted left by
				kTypeBits with its KernelTypes in the low bits (SharedTypes.h),
				so the type of any id is `id & kTypeMask`.

	Reconstructed from:	TObjectTable::Add 0x002f4c98, GiveObject 0x0014b16c,
				AcceptObject 0x0014b220, TObjectTable::Scavenge 0x002f477c
*/

#ifndef __KERNELOBJECT_H
#define __KERNELOBJECT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __SHAREDTYPES_H
#include "SharedTypes.h"
#endif

inline KernelTypes ObjectType(TObjectId id)	{ return (KernelTypes) (id & kTypeMask); }


// ROM size 0x10 (as a prefix of every kernel object)
class TKernelObject
{
	public:
		TObjectId		fId;				// +0x00  (unique << kTypeBits) | KernelTypes
		TKernelObject*	fNext;				// +0x04  next object in the same object-table bucket
		TObjectId		fOwnerId;			// +0x08  task that owns the object; an object owning
											//        itself (fOwnerId == fId) is never scavenged
		TObjectId		fAssignedOwnerId;	// +0x0c  task the object has been given to but which
											//        has not accepted it yet (TaskGiveObject/TaskAcceptObject)
};


// A scavenge proc decides whether an object whose owner has gone should be
// removed: it returns the destructor to run on it, or nil to keep it.
typedef void (*ObjectDestructorProcPtr)(TKernelObject* object);
typedef ObjectDestructorProcPtr (*ScavengeProcPtr)(TKernelObject* object, ULong unused);

#endif	/* __KERNELOBJECT_H */
