/*
	File:		user/UserMonitor.cpp

	Contains:	TUMonitor (UserMonitor.h), the handle on a kernel monitor.
				Destroying a monitor we made first suspends it (a call with
				kSuspendMonitor), so no caller is inside when it goes.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "UserMonitor.h"
#include "UserGlobals.h"
#include "os600/ObjectMessage.h"


// ROM 0x00257544 __ct__9TUMonitorFUl
TUMonitor::TUMonitor(TObjectId id)
	: TUObject(id)
{
}


// ROM 0x00257644 __dt__9TUMonitorFv
TUMonitor::~TUMonitor()
{
	DestroyObject();
}


// ROM 0x0025757c Init__9TUMonitorFPFPvUlT1_vUlPvT2UcT2T5
long
TUMonitor::Init(MonitorProcPtr monitorProc, ULong stackSize, void* monitorObject, TObjectId environmentId, Boolean faultMonitor, ULong name, Boolean rebootProtected)
{
	ObjectMessage msg;
	msg.fMonitor.fProc = monitorProc;
	msg.fMonitor.fStackSize = stackSize;
	msg.fMonitor.fMonitorObject = monitorObject;
	msg.fMonitor.fEnvironmentId = environmentId;
	msg.fMonitor.fFaultMonitor = faultMonitor;
	msg.fMonitor.fRebootProtected = rebootProtected;
	msg.fMonitor.fName = name;
	return MakeObject(kObjectMonitor, &msg, kObjectMessage_MonitorSize);
}


// ROM 0x002575c8 CopyObject__9TUMonitorFUl
void
TUMonitor::CopyObject(TObjectId id)
{
	if (fId == id)
		return;
	DestroyObject();
	fObjectCreatedByUs = false;
	fId = id;
}


// ROM 0x00257600 CopyObject__9TUMonitorFRC9TUMonitor
void
TUMonitor::CopyObject(const TUMonitor& copy)
{
	CopyObject(copy.fId);
}


// ROM 0x00257608 DestroyObject__9TUMonitorFv
void
TUMonitor::DestroyObject()
{
	if (fId == 0 || !fObjectCreatedByUs)
		return;
	MonitorDispatchSWI(fId, kSuspendMonitor, nil);
	TUObject::DestroyObject();
}
