/*
	File:		host/HostPackages.cpp

	Contains:	Handing the running machine a package from the host: the
				queue, the `hostPackages` global and the sender.  See
				HostPackages.h.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HostPackages.h"
#include "PackageManager.h"
#include "NewtWorld.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "UserPorts.h"
#include "UserTasks.h"
#include "NameServer.h"
#include "NewtonMemory.h"
#include "RootView.h"
#include "StorePackages.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include <atomic>				// (<mutex> and <string> bring in libc++'s locale support, which intl/Locale.h shadows)

// the files not yet sent: a small ring of copied names, taken and put
// under a spin lock (the window's thread puts, the kernel services task
// takes; neither holds it for more than a copy)
const int					kQueueSize = 32;
static char*				gQueue[kQueueSize];
static int					gQueueHead = 0, gQueueTail = 0;
static std::atomic_flag		gQueueLock = ATOMIC_FLAG_INIT;
static std::atomic<bool>	gGlobalReady(false);


void
HostQueuePackageFile(const char* path)
{
	while (gQueueLock.test_and_set(std::memory_order_acquire))
		;
	int next = (gQueueTail + 1) % kQueueSize;
	if (next != gQueueHead)
	{
		gQueue[gQueueTail] = strdup(path);
		gQueueTail = next;
		path = nil;
	}
	gQueueLock.clear(std::memory_order_release);
	if (path != nil)
		fprintf(stderr, "[host] %s: too many packages waiting; not installed\n", path);
}


// the next file to send (the caller frees it), or nil
static char*
TakeQueuedFile(void)
{
	char* path = nil;
	while (gQueueLock.test_and_set(std::memory_order_acquire))
		;
	if (gQueueHead != gQueueTail)
	{
		path = gQueue[gQueueHead];
		gQueueHead = (gQueueHead + 1) % kQueueSize;
	}
	gQueueLock.clear(std::memory_order_release);
	return path;
}


extern "C" void
HostWindowFileDropped(const char* path)
{
	HostQueuePackageFile(path);
}


// hostPackages:Install(bytes): the package in the binary stored on the
// default store and activated, as a package arriving from the Newton
// Connection is (store:SuckPackageFromBinary, then the ROM's
// RegisterNewPackage: recorded in the store's "Packages" soup, so it is
// activated again whenever the store is mounted - with --store, at every
// boot after this one).  ==> its id, or the error.
static Ref
FHostInstallPackage(RefArg /*rcvr*/, RefArg bytes)
{
	if (!IsBinary(bytes))
		return MAKEINT(kError_Bad_Parameters);
	volatile NewtonErr err = noErr;
	RefVar pkgRef;
	newton_try
	{
		RefVar store(NSCallGlobalFn(RSSYMgetdefaultstore));
		RefVar none;
		pkgRef = FSuckPackageFromBinary(store, bytes, none);
	}
	newton_catch_all
	{
		err = (NewtonErr) (long) (Long) _info.exception.data;
	}
	end_try;
	if (err != noErr)
		return MAKEINT(err);
	ULong packageId = 0;
	if (ISNIL(pkgRef) || VAddrToId(&packageId, (ULong) BinaryData(pkgRef)) != noErr || packageId == 0)
		return MAKEINT(kError_Bad_Package);
	return MAKEINT((long) packageId);
}


void
HostInstallPackageGlobal(void)
{
	RefVar hostPackages(AllocateFrame());
	SetFrameSlot(hostPackages, RefVar(Intern((char*) "Install")), RefVar(MakeCFunction((void*) FHostInstallPackage, 1, nil)));
	SetFrameSlot(gRootView->fContext, RefVar(Intern((char*) "hostPackages")), hostPackages);	// (a 'scpt event's variable is looked for from the root view)
	gGlobalReady.store(true);
}


// the whole of a file in a malloc'd block (the caller frees it), or nil
static char*
ReadWholeFile(const char* path, long* size)
{
	FILE* f = fopen(path, "rb");
	if (f == nil)
		return nil;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* bytes = *size > 0 ? (char*) malloc(*size) : nil;
	if (bytes != nil && fread(bytes, 1, *size, f) != (size_t) *size)
	{
		free(bytes);
		bytes = nil;
	}
	fclose(f);
	return bytes;
}


void
HostSendQueuedPackages(void)
{
	if (!gGlobalReady.load() || !gNewtIsAliveAndWell)
		return;
	static TObjectId portId = 0;
	if (portId == 0)
	{
		TUNameServer nameServer;
		ULong spec = 0;
		if (nameServer.Lookup((char*) "newt", (char*) "TUPort", &portId, &spec) != noErr)
		{
			portId = 0;
			return;
		}
	}
	char* path;
	while ((path = TakeQueuedFile()) != nil)
	{
		long size = 0;
		char* bytes = ReadWholeFile(path, &size);
		if (bytes == nil)
		{
			fprintf(stderr, "[host] %s: cannot be read\n", path);
			free(path);
			continue;
		}
		TRunScriptEvent event("hostPackages", "Install");
		event.fData = bytes;
		event.fSize = size;
		TUPort newtPort(portId);
		ULong replySize = 0;
		long err = newtPort.SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
		if (err == noErr)
			err = event.fError;
		if (err == noErr && event.fResult < 0)
			err = event.fResult;
		if (err != noErr)
			fprintf(stderr, "[host] %s: not installed (error %ld)\n", path, err);
		else
			fprintf(stderr, "[host] %s: installed as package %ld\n", path, event.fResult);
		free(bytes);
		free(path);
	}
}
