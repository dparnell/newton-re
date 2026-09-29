/*
	File:		comms/host/HostLinkTool.h

	Contains:	THostLinkTool, the comm tool behind the host's 'ictl
				service: the Newton Internet Enabler's connection control
				endpoint (the CCE - inetenbl.pkg's link state machine
				instantiates a protoBasicEndpoint whose 'sid ' option names
				"ictl", and configures the NIE's stack through it).

				DEVIATION (owner's decision - docs/comms/README.md): the NIE's
				TInetCCEService (inetenbl.pkg part 5) controls its own native
				TCP/IP stack - the link modules, the interfaces' addresses,
				the stack's activation; the host's network is the host's,
				always up, so every configuration the link asks for is taken
				and answered with success.

				What the link state machine sends (read with tools/newton-rom/
				analysis/pkgns.py; docs/comms/README.md, "The link"):

				Instantiate  the 'sid ' option ("ictl", then a long the
				             state machine reads back as the process port id)
				'iclc        "activate the link" (ActivateCCE, 0x4c25d): the
				             link id, a long result the tool writes (0), and
				             three longs; its completion (0x4c59d) fails the
				             link on an option result or on that long
				anything else  the link entry's inetAlmostFinalOptions and
				             inetFinalOptions, and whatever a service sends
				             through the CCE: taken

				The events the NIE's CCE event handler (0x480e1) looks for
				(eventCode 1: the link dropped, 2 and 3: failures) are never
				sent: the host's link does not drop.

	Host code for the NIE's link controller (no ROM counterpart).
*/

#ifndef __COMMS_HOSTLINKTOOL_H
#define __COMMS_HOSTLINKTOOL_H

#ifndef __COMMS_COMMTOOLS_H
#include "CommTools.h"
#endif

#define kLinkServiceId				'ictl'
#define kLinkActivateOption			'iclc'

class THostLinkTool : public TCommTool
{
public:
						THostLinkTool(ULong serviceId);
	virtual				~THostLinkTool();

	virtual ULong		GetSizeOf();
	virtual UChar*		GetToolName();
	virtual NewtonErr	OpenStart(TOptionArray* options);
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);

	// (no data flows through the CCE)
	virtual void		PutBytes(CBufferList* clientBuffer);
	virtual void		PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame);
	virtual void		KillPut();
	virtual void		GetBytes(CBufferList* clientBuffer);
	virtual void		GetFramedBytes(CBufferList* clientBuffer);
	virtual void		KillGet();
};

#endif
