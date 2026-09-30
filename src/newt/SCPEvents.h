/*
	File:		newt/SCPEvents.h

	Contains:	What the newt world is told about devices plugged into the
				serial port.

				TSCPEvent ('newt' 'idle' 'scp!') is the docking loader's
				(comms/SCPLoader.h): a package it stored on the internal
				store for a device, to be registered (1), or the package
				of the device before it, to be removed (2) - which
				HandleSCPEvent does through the NewtonScript
				RegisterNewPackage and RemovePackage.  TInterConnectEvent
				('ic  ') is the interconnect port's handler's (TICHandler,
				NOT YET: the port's pin read as something plugged in or
				taken out), which HandleInterConnect passes on to the
				Connection application's AutoDock.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __NEWT_SCPEVENTS_H
#define __NEWT_SCPEVENTS_H

#ifndef __AEVENTS_H
#include "AEvents.h"
#endif

#define kNewtSCPEvent			'scp!'
#define kNewtInterConnectEvent	'ic  '

// what the loader says of a package
enum
{
	kSCPPackageLoaded = 1,		// register it
	kSCPPackageRemoved = 2		// the device before's: remove it
};

class TSCPEvent : public TAEvent		// 0x14 bytes
{
public:
					TSCPEvent();
					TSCPEvent(ULong what, ULong id);

	ULong			fType;				// +0x08  'scp!'
	ULong			fWhat;				// +0x0c
	ULong			fId;				// +0x10  the package's store object
};

class TInterConnectEvent : public TAEvent
{
public:
	ULong			fType;				// +0x08  'ic  '
	ULong			fState;				// +0x0c  1: something is plugged in
};

void	HandleSCPEvent(TSCPEvent* event);
void	HandleInterConnect(TInterConnectEvent* event);

#endif	/* __NEWT_SCPEVENTS_H */
