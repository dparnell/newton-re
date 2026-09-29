/*
	File:		comms/host/HostServices.cpp

	Contains:	THostInetService (HostServices.h).

	Host code for the NIE's services (no ROM counterpart).
*/

#include "HostServices.h"
#include "HostTCPTool.h"
#include "HostDNSTool.h"
#include "CommManager.h"
#include "NewtErrors.h"


PROTOCOL_IMPL_SOURCE_MACRO(THostInetService)
PROTOCOL_CLASSINFO(THostInetService, "TCMService", "serv\0inet\0\0", 0, 0, nil)


THostInetService*
THostInetService::New()
{
	return this;
}


void
THostInetService::Delete()
{
}


// The tool's task started (the object here is the parent's copy, thrown
// away once the task has its own) and opened with the endpoint's options;
// the open's reply reaches the comm manager, which calls DoneStarting.
NewtonErr
THostInetService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	THostTCPTool tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}


// What the open answered.
NewtonErr
THostInetService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}


PROTOCOL_IMPL_SOURCE_MACRO(THostDNSService)
PROTOCOL_CLASSINFO(THostDNSService, "TCMService", "serv\0dnst\0\0", 0, 0, nil)

THostDNSService*
THostDNSService::New()
{
	return this;
}

void
THostDNSService::Delete()
{
}

// As the inet service's, over the DNS tool.
NewtonErr
THostDNSService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	THostDNSTool tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}

NewtonErr
THostDNSService::DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo)
{
	return ((TCommToolReply*) event)->fResult;
}


void
RegisterHostCommServices(void)
{
	THostInetService::ClassInfo()->Register();
	THostDNSService::ClassInfo()->Register();
}
