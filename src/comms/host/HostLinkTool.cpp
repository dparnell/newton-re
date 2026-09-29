/*
	File:		comms/host/HostLinkTool.cpp

	Contains:	THostLinkTool (HostLinkTool.h).

	Host code for the NIE's link controller (no ROM counterpart).
*/

#include "HostLinkTool.h"
#include "Options.h"
#include "CommErrors.h"


THostLinkTool::THostLinkTool(ULong serviceId)
	: TCommTool(serviceId)
{ }


THostLinkTool::~THostLinkTool()
{ }


ULong
THostLinkTool::GetSizeOf()
{
	return sizeof(THostLinkTool);
}


UChar*
THostLinkTool::GetToolName()
{
	return (UChar*) "Host link";
}


NewtonErr
THostLinkTool::OpenStart(TOptionArray* options)
{
	return noErr;
}


// 'iclc: the link is up - its result long (the second) nought; anything
// else the link or a service configures is taken.
ULong
THostLinkTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	if (label == kLinkActivateOption && theOption->Length() >= 8)
	{
		UByte* data = (UByte*) (theOption + 1);
		data[4] = data[5] = data[6] = data[7] = 0;
	}
	return opSuccess;
}


void THostLinkTool::PutBytes(CBufferList*)						{ PutComplete(kCommErrNotSupported, 0); }
void THostLinkTool::PutFramedBytes(CBufferList*, Boolean)		{ PutComplete(kCommErrNotSupported, 0); }
void THostLinkTool::KillPut()									{ KillPutComplete(noErr); }
void THostLinkTool::GetBytes(CBufferList*)						{ GetComplete(kCommErrNotSupported); }
void THostLinkTool::GetFramedBytes(CBufferList*)				{ GetComplete(kCommErrNotSupported); }
void THostLinkTool::KillGet()									{ KillGetComplete(noErr); }
