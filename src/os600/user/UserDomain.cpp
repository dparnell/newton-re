/*
	File:		user/UserDomain.cpp

	Contains:	TUDomain (UserDomain.h), the handle on a kernel domain.  Base()
				and Size() are declared by the DDK but have no code in the ROM.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "UserDomain.h"
#include "UserGlobals.h"
#include "UserMonitor.h"
#include "os600/ObjectMessage.h"


// ROM 0x00258918 Init__8TUDomainFUlN21
long
TUDomain::Init(TObjectId monitor, VAddr base, ULong size)
{
	ObjectMessage msg;
	msg.fDomain.fMonitorId = monitor;
	msg.fDomain.fBase = base;
	msg.fDomain.fSize = size;
	return MakeObject(kObjectDomain, &msg, kObjectMessage_DomainSize);
}


// ROM 0x00259384 SetFaultMonitor__8TUDomainFUl
long
TUDomain::SetFaultMonitor(TObjectId monitor)
{
	ObjectMessage msg;
	msg.fSize = kObjectMessage_SetFaultMonitorSize;
	msg.fDomain.fMonitorId = fId;		// +0x0c: the domain
	msg.fDomain.fBase = monitor;		// +0x10: the monitor
	return MonitorDispatchSWI(*gUObjectMgrMonitor, kObjectMgr_SetFaultMonitor, &msg);
}
