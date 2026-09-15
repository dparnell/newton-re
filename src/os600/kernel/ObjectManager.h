/*
	File:		ObjectManager.h

	Contains:	TObjectManager, the monitor (gTheObjectManagerMonitor, priority
				gMonitorTaskPriority, name 'OBJM') through which user code makes,
				destroys and manipulates kernel objects.  A request is an
				ObjectMessage (os600/ObjectMessage.h) and the selector says what
				to do with it; every request ends with a scavenge of the object
				table if a task has been destroyed meanwhile.

				ObjectScavenger is the object table's scavenge proc: it names the
				destructor for each kind of object, and declines (deferring the
				removal) for a task inside a monitor or a monitor with a call in
				progress.

				Not yet reconstructed: the domain and physical-memory objects in
				ObjectAlloc (TKDomain::Init, TPhys need the MMU) and the external
				page tracker requests (TExtPageTrackerMgr).

	Reconstructed from:	TObjectManager 0x0014ad58-0x0014ad88, ObjectScavenger 0x0014a508,
				the Object* handlers 0x0014a5e0-0x0014b4ac
*/

#ifndef __OBJECTMANAGER_H
#define __OBJECTMANAGER_H

#ifndef __KERNELOBJECT_H
#include "KernelObject.h"
#endif
#ifndef __OBJECTMESSAGE_H
#include "os600/ObjectMessage.h"
#endif

class TTask;
class TMonitor;
class TEnvironment;


// ROM size 0x04
class TObjectManager
{
	public:
						TObjectManager();

		NewtonErr		MonitorProc(long selector, ObjectMessage* msg);		// the monitor's proc; result to the caller
		static void		MonitorProcGlue(void* manager, ULong selector, void* msg);

		TObjectId		fTaskToDelete;		// +0x00  a task that killed itself; removed at the next request
};

extern TObjectManager*	gTheObjectManager;		// 0x0c101790

// the request handlers; each checks the message size
NewtonErr	ObjectAlloc(ObjectMessage* msg, ULong size, TObjectId requesterId, TObjectId* outId);
NewtonErr	ObjectDestroy(ObjectMessage* msg, ULong size, TObjectId requesterId);	// requesterId 0: skip the ownership check
NewtonErr	ObjectStart(ObjectMessage* msg, ULong size);
NewtonErr	ObjectSuspend(ObjectMessage* msg, ULong size);
NewtonErr	ObjectGetRegister(ObjectMessage* msg, ULong size, ULong* outValue);
NewtonErr	ObjectSetRegister(ObjectMessage* msg, ULong size);
NewtonErr	GetObjectContent(ObjectMessage* msg, ULong size, ObjectMessage* reply);
NewtonErr	SetDomainFaultMonitor(ObjectMessage* msg, ULong size, ObjectMessage* reply);

ObjectDestructorProcPtr	ObjectScavenger(TKernelObject* object, ULong unused);
void		DeleteTask(TTask* task);

void		InitObjectManager(TEnvironment* environment);		// makes gTheObjectManager and its monitor, installs ObjectScavenger

#endif	/* __OBJECTMANAGER_H */
