/*
	File:		packages/PackageLoader.h

	Contains:	TPackageLoader: a package loaded from a pipe (a stream
				source) rather than a block of memory.

				The caller's world forks (so that it goes on taking events
				while the manager works), takes gPackageSemaphore and puts
				up a TPackageLoaderEventHandler; then, for a stream, makes a
				0x100-byte ring buffer, shares its memory, and starts the
				'pipe' world (TPipeApp, PartPipe.h) that fills it from the
				pipe on the manager's request.  The TPkBeginLoadEvent it
				sends the manager names the ring buffer's shared memory and
				the 'pipe' world's port as its source; the manager reads the
				package through a CPartPipe over it and, when it has read the
				whole package, closes the 'pipe' world.  Done gives back the
				ring buffer and the handler and lets go of the semaphore; a
				package that put a system patch in reboots the machine
				(cPackageLoad).

				A package in memory is loaded without a TPackageLoader
				(LoadPackage(Ptr ...) and InstallPackage, PackageManager.h);
				the loader's memory branch is there all the same.

				The frames parts of a streamed package are read by their
				handler as one flattened object (NSOF: TFramePartHandler's
				Expand), not in the object layout a package in memory has -
				which is how the ROM's own streamed packages are made.

				NOT YET RECONSTRUCTED: an endpoint as the source
				(TEndpointPipe, LoadPackage(TEndpointPipe* ...) - the comms
				area).

	Reconstructed from the MP2x00 US ROM (0x0015d3b0-0x0015dc00,
	0x001829c8-0x00182a2c); each function cites its origin.
*/

#ifndef __PACKAGELOADER_H
#define __PACKAGELOADER_H

#ifndef __PACKAGEMANAGER_H
#include "PackageManager.h"
#endif

class CPipe;
class CRingBuffer;


// Put up in the loading world for the duration: a 'pkbl, 'pkrm or 'pkbu
// event reaching it ends the loop the world is in.
class TPackageLoaderEventHandler : public TAEventHandler
{
public:
	virtual	Boolean	AETestEvent(TAEvent* event);		// ROM 0x001829c8 AETestEvent__26TPackageLoaderEventHandlerFP7TAEvent
	virtual	void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);		// ROM 0x001829fc AEHandlerProc__26TPackageLoaderEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual	void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x00182a14 AECompletionProc__26TPackageLoaderEventHandlerFP10TUMsgTokenPUlP7TAEvent
};


class TPackageLoader		// 0x24 bytes in the ROM
{
public:
					TPackageLoader(char* buffer, SourceType type);		// ROM 0x0015d3b0 __ct__14TPackageLoaderFPc10SourceType
					TPackageLoader(CPipe* pipe, SourceType type);		// ROM 0x0015d404 __ct__14TPackageLoaderFP5CPipe10SourceType
					~TPackageLoader();									// ROM 0x0015d7a4 __dt__14TPackageLoaderFv

	void			Reset(void);										// ROM 0x0015d7b0 Reset__14TPackageLoaderFv - nothing
	NewtonErr		Load(void);											// ROM 0x0015d7b4 Load__14TPackageLoaderFv
	void			Done(UChar* forDispatchOnly, UChar* patchInstalled);	// ROM 0x0015db0c Done__14TPackageLoaderFPUcT1

	char*			fBufferPtr;			// +0x00  a memory source
	CPipe*			fPipe;				// +0x04  a stream source
	SourceType		fSourceType;		// +0x08
	UChar			fIsEndpoint;		// +0x10  fPipe is a TEndpointPipe
	CRingBuffer*	fBuffer;			// +0x14  the ring buffer the 'pipe' world fills
	ULong			fPackageId;			// +0x18  the manager's answer
	TPackageLoaderEventHandler*	fHandler;	// +0x1c
	UChar			fForDispatchOnly;	// +0x20
	UChar			fPatchInstalled;	// +0x21
};


NewtonErr	cPackageLoad(TPackageLoader& loader, ULong* packageId);		// ROM 0x0015d458 cPackageLoad__FR14TPackageLoaderPUl
NewtonErr	LoadPackage(CPipe* pipe, ULong* packageId, Boolean willRemove);	// ROM 0x0015d4e0 LoadPackage__FP5CPipePUlUc - a removable stream
NewtonErr	LoadPackage(CPipe* pipe, SourceType type, ULong* packageId);	// ROM 0x0015d5fc LoadPackage__FP5CPipe10SourceTypePUl - kError_Bad_Parameters for a memory type

#endif	/* __PACKAGELOADER_H */
