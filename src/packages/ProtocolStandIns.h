/*
	File:		packages/ProtocolStandIns.h

	Contains:	Host implementations standing in for packages' protocol
				parts.

				A package's protocol part (kind kProtocol: a driver, a comms
				tool, a card handler) is ARM code whose class info the ROM's
				package manager registers with the protocol registry
				(TPackageEventHandler::InstallPart).  The host cannot run it
				there, so a host re-expression of a part (the Newton Internet
				Enabler's, by the owner's decision - thirdparty/nie) is
				registered here under the part's implementation and interface
				names beforehand, and the package manager registers it in
				the part's place when the part is installed and deregisters
				it when the part is removed - as the ROM does the part's own.
				The stand-in's class info is the host's own, never freed.

				DEVIATION: the ROM has no such table; a part with no stand-in
				is still reported and not registered (PackageManager.cpp).
*/

#ifndef __PROTOCOLSTANDINS_H
#define __PROTOCOLSTANDINS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TClassInfo;

// info stands in for a package's part implementing its interface under its
// implementation's name (from info itself)
void				RegisterProtocolStandIn(const TClassInfo* info);

// ==> the stand-in for a part implementing interface as implementation, or nil
const TClassInfo*	ProtocolStandInFor(const char* implementation, const char* interface);

// ==> whether this class info is a stand-in (the host's own: not a copy of a part)
Boolean				IsProtocolStandIn(const void* classInfo);

// The fallback for a part with no stand-in: its ARM code run on the host's
// ARM interpreter (armcpu/ARMProtocols.h), which makes and registers a
// class info for it (a stand-in from then on).  part is the part's bytes,
// package where the package lies (nil: unknown) and partOffset the part's
// place in it.  ==> the class info, nil when there is no fallback or it
// cannot take the part.
typedef const TClassInfo*	(*ARMProtocolPartLoader)(const void* part, ULong size, const void* package, ULong partOffset);
void				SetARMProtocolPartLoader(ARMProtocolPartLoader loader);
const TClassInfo*	LoadARMProtocolPartFallback(const void* part, ULong size, const void* package, ULong partOffset);

#endif	/* __PROTOCOLSTANDINS_H */
