/*
	File:		armcpu/ARMProtocols.h

	Contains:	Packages' protocol parts run on the ARM interpreter.

				A protocol part (a driver, a store, a card handler) is ARM
				code that starts with the ROM's TClassInfo table (fifteen
				words: self-relative offsets to its names and its dispatch
				table, ARM branches to its sizeof, alloc, free, New and
				Delete; packages/ROMClassInfo.h).  The host cannot register
				that table, so when no host stand-in is registered for a part
				(packages/ProtocolStandIns.h), its code is copied into the
				ARM world (ARMWorld.h: a region, relocated as the ROM's
				TSimpleCRelocator relocates it) and a host TClassInfo is made
				for it, registered in its place:

				- an instance the host makes (NewByName from host code) is a
				  *proxy*: a host object of a class written for the part's
				  interface (RegisterARMProxyKind) that answers each method
				  by calling the ARM instance's dispatch table slot - an ARM
				  instance made beside it, as the ROM's TClassInfo::MakeAt
				  makes one (fRuntime 0, fRealThis itself, fBTable its
				  dispatch table, fMonitorId 0) and as its New() and
				  Delete() say;
				- an instance the ARM code makes (AllocInstanceByName,
				  NewByName from the ARM code) is the ARM instance alone, and
				  its methods are called ARM to ARM through the table;
				- the arguments a proxy hands the ARM code are marshalled
				  by the proxy: integers as they are, a host object the ARM
				  code reads fields of as a *mirror* in the ARM heap in the
				  ROM's layout, one whose methods it calls through the public
				  jump table as a handle the glue maps back
				  (ARMMirrorFor/ARMHostOf).

				Monitor parts are NOT YET (none of the fixtures' is one).

	docs/armcpu/README.md, "Protocol parts".
*/

#ifndef __ARMPROTOCOLS_H
#define __ARMPROTOCOLS_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

#include <stdint.h>

// the loader: made the packages' fallback for a protocol part with no
// stand-in (ProtocolStandIns.h's SetARMProtocolPartLoader)
void		InstallARMProtocols(void);

// A part's code and where its package lies (for the relocation; nil when
// unknown), made a registered class.  ==> its host class info, or nil.
const TClassInfo*	LoadARMProtocolPart(const void* part, unsigned long size, const void* package, unsigned long partOffset);

// What an interface's proxies are made with: placement construction of
// the proxy class and its size.
typedef TProtocol*	(*ARMProxyMakeAt)(void* at);
typedef size_t		(*ARMProxySize)(void);
void		RegisterARMProxyKind(const char* interface, ARMProxyMakeAt makeAt, ARMProxySize size);

// A proxy's ARM instance, and a call of its dispatch table's slot (slot 2
// is New, 3 Delete, 4 the interface's first method): the instance first,
// then the arguments.  ==> r0.
uint32_t	ARMInstanceOf(const TProtocol* proxy);
uint32_t	ARMCallSlot(const TProtocol* proxy, int slot, const uint32_t* args = nil, int count = 0);
// the same for an ARM instance (made by the ARM code)
uint32_t	ARMCallInstanceSlot(uint32_t instance, int slot, const uint32_t* args = nil, int count = 0);

// Host objects as the ARM code sees them: a mirror is a block of the ARM
// heap standing for the host object (its bytes the caller's to fill in the
// ROM's layout); the same host object has the same mirror (made once, the
// size given then).  ARMHostOf answers the host object a mirror stands for
// (nil for none) - how the glue for its methods finds it.
uint32_t	ARMMirrorFor(const void* host, uint32_t size, uint32_t kind);
void*		ARMHostOf(uint32_t mirror, uint32_t kind);
void		ARMForgetMirror(const void* host);

// an ARM address made a copy of a C string (in the ARM heap; the caller's to free)
uint32_t	ARMCString(const char* s);

// The user-side OS objects ARM code makes (ARMKernelGlue.cpp): the host
// event handler standing for an ARM TAEventHandler (its AETestEvent,
// AEHandlerProc, AECompletionProc and IdleProc call the ARM object's own
// virtual functions), nil for none.
class TAEventHandler;
TAEventHandler*	ARMEventHandlerOf(uint32_t arm);

#endif	/* __ARMPROTOCOLS_H */
