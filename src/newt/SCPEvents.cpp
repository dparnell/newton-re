/*
	File:		newt/SCPEvents.cpp

	Contains:	The serial port's device events (SCPEvents.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SCPEvents.h"
#include "Interpreter.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "Soups.h"
#include "StorePackages.h"
#include "LargeObjects.h"
#include "LargeBinaries.h"


// ROM 0x0030bc00 __ct__9TSCPEventFv
TSCPEvent::TSCPEvent()
{
	fAEventClass = 'newt';
	fAEventID = 'idle';
	fType = kNewtSCPEvent;
	fWhat = 0;
	fId = 0;
}


// ROM 0x0030bc64 __ct__9TSCPEventFUlT1
TSCPEvent::TSCPEvent(ULong what, ULong id)
{
	fAEventClass = 'newt';
	fAEventID = 'idle';
	fType = kNewtSCPEvent;
	fWhat = what;
	fId = id;
}


// ROM 0x0030db60 HandleSCPEvent__FP9TSCPEvent
// A package the loader stored on the internal store registered as one
// the user installed (RegisterNewPackage(package, store, true)); or the
// entry of the one it replaces handed to RemovePackage.
void
HandleSCPEvent(TSCPEvent* event)
{
	if (event->fWhat == kSCPPackageLoaded)
	{
		TStore* store = GetInternalStore();
		RefVar package(WrapPackage(event->fId, store));
		if (package != NILREF)
		{
			RefVar yes(TRUEREF);
			RefVar storeFrame(ToObject(store));
			NSCallGlobalFn(RSSYMregisternewpackage, package, storeFrame, yes);
		}
	}
	else
	{
		TStore* store = GetInternalStore();
		ULong address;
		if (StoreToVAddr(&address, store, event->fId) == noErr)
		{
			RefVar entry(GetEntryFromLargeObjectVAddr(address));
			NSCallGlobalFn(RSSYMremovepackage, entry);
		}
	}
}


// ROM 0x0030dae8 HandleInterConnect__FP18TInterConnectEvent
// Something plugged into the interconnect port (AutoDock('Connect, true))
// or taken out (AutoDock('Disconnect, nil)).  (Nothing sends it yet: the port's
// handler, TICHandler, is NOT YET.)
void
HandleInterConnect(TInterConnectEvent* event)
{
	if (event->fState == 1)
	{
		RefVar yes(TRUEREF);
		NSCallGlobalFn(RSSYMautodock, RSSYMconnect, yes);
	}
	else
	{
		RefVar nothing(NILREF);
		NSCallGlobalFn(RSSYMautodock, RSSYMdisconnect, nothing);
	}
}
