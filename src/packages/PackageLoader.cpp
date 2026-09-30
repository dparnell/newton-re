/*
	File:		packages/PackageLoader.cpp

	Contains:	A package loaded from a pipe - see PackageLoader.h.

	Reconstructed from the MP2x00 US ROM (0x0015d3b0-0x0015dc00,
	0x001829c8-0x00182a2c); each function cites its origin.
*/

#include "PackageLoader.h"
#include "PartPipe.h"
#include "RingBuffer.h"
#include "PackageEvents.h"
#include "PartHandlers.h"			// PackageManagerPortId
#include "NameServer.h"
#include "UserTasks.h"
#include "UserPorts.h"
#include "Reboot.h"
#include "OSErrors.h"


/*------------------------------------------------------------------------------
	T P a c k a g e L o a d e r E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x001829c8 AETestEvent__26TPackageLoaderEventHandlerFP7TAEvent
// A begin-load, remove or backup event.
Boolean
TPackageLoaderEventHandler::AETestEvent(TAEvent* event)
{
	ULong code = ((TPkBaseEvent*) event)->fEventCode;
	return code == kPkBeginLoadEvent || code == kPkRemoveEvent || code == kPkBackupEvent;
}


// ROM 0x001829fc AEHandlerProc__26TPackageLoaderEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The loop the world is in ended (the ROM sets its state's fDone, which is
// what AETerminateLoop does).
void
TPackageLoaderEventHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{
	((TAppWorld*) GetGlobals())->AETerminateLoop();
}


// ROM 0x00182a14 AECompletionProc__26TPackageLoaderEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TPackageLoaderEventHandler::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{
	((TAppWorld*) GetGlobals())->AETerminateLoop();
}


/*------------------------------------------------------------------------------
	T P a c k a g e L o a d e r
------------------------------------------------------------------------------*/

// ROM 0x0015d3b0 __ct__14TPackageLoaderFPc10SourceType
TPackageLoader::TPackageLoader(char* buffer, SourceType type)
{
	fIsEndpoint = false;
	fSourceType = type;
	fHandler = nil;
	fBufferPtr = buffer;
	fBuffer = nil;
	fPipe = nil;				// (the ROM leaves +0x04 alone)
	fPackageId = 0;
	fForDispatchOnly = false;
	fPatchInstalled = false;
}


// ROM 0x0015d404 __ct__14TPackageLoaderFP5CPipe10SourceType
TPackageLoader::TPackageLoader(CPipe* pipe, SourceType type)
{
	fSourceType = type;
	fPipe = pipe;
	fIsEndpoint = false;
	fHandler = nil;
	fBuffer = nil;
	fBufferPtr = nil;			// (the ROM leaves +0x00 alone)
	fPackageId = 0;
	fForDispatchOnly = false;
	fPatchInstalled = false;
}


// ROM 0x0015d7a4 __dt__14TPackageLoaderFv
TPackageLoader::~TPackageLoader()
{ }


// ROM 0x0015d7b0 Reset__14TPackageLoaderFv
void
TPackageLoader::Reset(void)
{ }


// ROM 0x0015d7b4 Load__14TPackageLoaderFv
// The package sent to the manager, on a stream through a ring buffer the
// 'pipe' world fills (PackageLoader.h).  gPackageSemaphore is taken here and
// let go in Done, whatever happens between.  ==> the manager's answer, or
// the first error on the way.
// ROM BUG kept: a ring buffer that cannot be made answers noErr - and no
// package is loaded.
// ROM BUG kept: the handler is initialised without looking whether it could
// be made.
NewtonErr
TPackageLoader::Load(void)
{
	TForkWorld* world = (TForkWorld*) GetGlobals();
	world->ReleaseMutex();
	gPackageSemaphore->Acquire(kWaitOnBlock);
	world->AcquireMutex();
	fForDispatchOnly = false;
	fPatchInstalled = false;
	fHandler = new TPackageLoaderEventHandler;
	NewtonErr err = fHandler->Init(kPackageEventId, kNewtEventClass);
	if (err != noErr)
		return err;

	PartSource source;
	if (!IsMemory(fSourceType))
	{
		fBuffer = new CRingBuffer;
		if (fBuffer == nil)
			return noErr;
		err = fBuffer->Init(0x100);
		if (err != noErr)
			return err;
		fBuffer->MakeShared(0);
		PipeInfo info;
		info.fPipe = fPipe;
		info.fUnused04 = 0;		// (the ROM's is whatever was on the stack)
		info.fBuffer = fBuffer;
		info.fIsEndpoint = false;
		// an endpoint let go of by this world for the 'pipe' world to read
		// in; the error an exPipeException carried is the answer
		if (fIsEndpoint)
		{
			err = CallEndpointPipeHook(gEndpointPipeHooks.fRemoveFromAppWorld, fPipe);
			if (err != noErr)
				return err;
		}
		TPipeApp app(info, fIsEndpoint);
		err = app.Init('pipe', true, 6000);
		if (err != noErr)
			return err;
		TUNameServer nameServer;
		ULong portId = 0, spec = 0;
		err = nameServer.Lookup((char*) "pipe", (char*) "TUPort", &portId, &spec);
		if (err != noErr)
			return err;
		source.stream.bufferId = (TObjectId) fBuffer->fSharedMem;
		source.stream.messagePortId = (TObjectId) portId;
	}
	else
	{
		source.stream.bufferId = (TObjectId) fBufferPtr;
		source.stream.messagePortId = 0;	// (the ROM's is whatever was on the stack)
	}

	TAppWorld* appWorld = (TAppWorld*) GetGlobals();
	TPkBeginLoadEvent event(fSourceType, source, *appWorld->GetMyPort(), *appWorld->GetMyPort(), true);
	TUPort port(PackageManagerPortId());
	world->ReleaseMutex();
	ULong replySize;
	err = port.SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
	world->AcquireMutex();
	if (err == noErr)
		err = event.fEventError;
	fPackageId = event.fPackageId;
	if (err == noErr)
	{
		fForDispatchOnly = event.fForDispatchOnly;
		fPatchInstalled = event.fPatchInstalled;
	}
	return err;
}


// ROM 0x0015db0c Done__14TPackageLoaderFPUcT1
// What the manager said (dispatch only, a patch went in); the endpoint
// given back to its world, the handler and the ring buffer gone,
// gPackageSemaphore let go.
void
TPackageLoader::Done(UChar* forDispatchOnly, UChar* patchInstalled)
{
	if (forDispatchOnly != nil)
		*forDispatchOnly = fForDispatchOnly;
	if (patchInstalled != nil)
		*patchInstalled = fPatchInstalled;
	// the endpoint taken back into this world (an exPipeException dropped)
	if (!IsMemory(fSourceType) && fIsEndpoint)
		CallEndpointPipeHook(gEndpointPipeHooks.fAddToAppWorld, fPipe);
	if (fHandler != nil)
		delete fHandler;
	if (fBuffer != nil)
		delete fBuffer;
	fHandler = nil;
	fBuffer = nil;
	gPackageSemaphore->Release();
}


/*------------------------------------------------------------------------------
	T h e   c a l l s
------------------------------------------------------------------------------*/

// ROM 0x0015d458 cPackageLoad__FR14TPackageLoaderPUl
// The world forked, the package loaded and the loader done with; a package
// that put a system patch in reboots the machine.  ==> the load's error.
NewtonErr
cPackageLoad(TPackageLoader& loader, ULong* packageId)
{
	NewtonErr err = ((TForkWorld*) GetGlobals())->Fork(nil);
	if (err == noErr)
	{
		err = loader.Load();
		*packageId = loader.fPackageId;
		UChar forDispatchOnly = false;
		UChar patchInstalled = false;
		loader.Done(&forDispatchOnly, &patchInstalled);
		if (patchInstalled)
			Reboot(kError_New_System_Software, 0, false);
	}
	return err;
}


// ROM 0x0015d4e0 LoadPackage__FP5CPipePUlUc
// A package read from a pipe, as a removable stream.
// DEVIATION: the ROM's source type has its device number and id from the
// stack; the host's are nought.
NewtonErr
LoadPackage(CPipe* pipe, ULong* packageId, Boolean /*willRemove*/)
{
	SourceType type = { kRemovableStream, kNoDevice, 0, 0 };
	TPackageLoader loader(pipe, type);
	return cPackageLoad(loader, packageId);
}


// ROM 0x0015d5fc LoadPackage__FP5CPipe10SourceTypePUl
// A package read from a pipe; a memory type is not for a pipe.
NewtonErr
LoadPackage(CPipe* pipe, SourceType type, ULong* packageId)
{
	if (IsMemory(type))
		return kError_Bad_Parameters;
	TPackageLoader loader(pipe, type);
	return cPackageLoad(loader, packageId);
}
