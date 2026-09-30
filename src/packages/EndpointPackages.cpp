/*
	File:		packages/EndpointPackages.cpp

	Contains:	store:SuckPackageFromEndPoint - a package read off a
				NewtonScript endpoint (a serial link, a network connection)
				straight onto the store, through an endpoint pipe; and a
				package loaded off an endpoint pipe (LoadPackage(TEndpointPipe*
				...), PackageLoader.h), with the endpoint pipe's two calls the
				loader and the 'pipe' world make through PartPipe.h's hooks.

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
#include "PackageLoader.h"
#include "PartPipe.h"


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


/*------------------------------------------------------------------------------
	A   p a c k a g e   o f f   a n   e n d p o i n t
------------------------------------------------------------------------------*/

// ROM 0x0015d74c __ct__14TPackageLoaderFP13TEndpointPipe10SourceType
// The loader of a stream an endpoint pipe reads.
TPackageLoader::TPackageLoader(TEndpointPipe* pipe, SourceType type)
{
	fSourceType = type;
	fPipe = pipe;
	fIsEndpoint = true;
	fHandler = nil;
	fBuffer = nil;
	fBufferPtr = nil;			// (the ROM leaves +0x00 alone)
	fPackageId = 0;				// (and these)
	fForDispatchOnly = false;
	fPatchInstalled = false;
}


// ROM 0x0015d5a0 LoadPackage__FP13TEndpointPipePUlUc
// A package read off an endpoint pipe, as a removable stream.
// DEVIATION: the ROM's source type has its device number and id from the
// stack; the host's are nought (as LoadPackage(CPipe* ...)'s).
NewtonErr
LoadPackage(TEndpointPipe* pipe, ULong* packageId, Boolean /*willRemove*/)
{
	SourceType type = { kRemovableStream, kNoDevice, 0, 0 };
	TPackageLoader loader(pipe, type);
	return cPackageLoad(loader, packageId);
}


// (host) PartPipe.h's hooks: the loader's pipe is the endpoint pipe it was
// made with
static void
EndpointPipeAddToAppWorld(CPipe* pipe)
{
	((TEndpointPipe*) pipe)->AddToAppWorld();
}

static void
EndpointPipeRemoveFromAppWorld(CPipe* pipe)
{
	((TEndpointPipe*) pipe)->RemoveFromAppWorld();
}


void
RegisterEndpointPackageNatives(void)
{
	gEndpointPipeHooks.fAddToAppWorld = EndpointPipeAddToAppWorld;
	gEndpointPipeHooks.fRemoveFromAppWorld = EndpointPipeRemoveFromAppWorld;
	RegisterNativeFunction("FSuckPackageFromEnpoint", (void*) FSuckPackageFromEnpoint, 2);
}
