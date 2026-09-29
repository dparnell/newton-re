/*
	File:		packages/EndpointPackages.cpp

	Contains:	store:SuckPackageFromEndPoint - a package read off a
				NewtonScript endpoint (a serial link, a network connection)
				straight onto the store, through an endpoint pipe.

	The endpoint and its pipe are the communications area's
	(comms/NewScriptEndpoint.h, comms/EndpointPipe.h); what is read is
	stored as store:SuckPackageFromBinary stores a binary's
	(StorePackages.h's AllocatePackage).

	Reconstructed from the MP2x00 US ROM; the function cites its origin.
*/

#include "StorePackages.h"
#include "NewScriptEndpoint.h"
#include "EndpointPipe.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"


// ROM 0x00321b6c FSuckPackageFromEnpoint
// store:SuckPackageFromEndPoint(endpoint, parameters): the package the
// endpoint sends, read through a pipe of 0x800-byte reads and stored on the
// store (the parameters as SuckPackageFromBinary takes them: a callback, how
// often it is called, whether to activate).  ==> the package's frame; nil
// when the endpoint has no endpoint of its own behind it.  An exception
// while reading is thrown again as evt.ex.fr, once the pipe is done with.
Ref
FSuckPackageFromEnpoint(RefArg rcvr, RefArg endpoint, RefArg parameters)
{
	RefVar result;
	void* error = nil;
	TEndpoint* ep = GetClientEndpoint(endpoint);
	if (ep != nil)
	{
		TEndpointPipe pipe;
		newton_try
		{
			pipe.Init(ep, 0x800, 0, 0, false, nil);
			result = AllocatePackage(&pipe, rcvr, parameters);
		}
		newton_catch_all
		{
			error = CurrentException()->data;
		}
		end_try;
	}
	if (error != nil)
		Throw(exFrames, error, nil);
	return result;
}


void
RegisterEndpointPackageNatives(void)
{
	RegisterNativeFunction("FSuckPackageFromEnpoint", (void*) FSuckPackageFromEnpoint, 2);
}
