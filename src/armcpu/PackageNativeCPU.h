/*
	File:		armcpu/PackageNativeCPU.h

	Contains:	The fallback for packages' native functions: their ARM
				code run by TARMCPU (ARMCPU.h) over a 32-bit world the
				adapter builds for each call, its calls into the ROM - the
				public jump table at 0x01800000 - answered by host
				functions.

				InstallPackageNativeCPU makes it the fallback of
				frames/PackageNatives.h (SetPackageNativeFallback): a
				package native with no host re-expression registered runs
				here.  The design - Refs as 32-bit handles, RefHandles in
				the ARM world, object data mapped on demand, the ROM image
				at 0 - is docs/armcpu/README.md.

				NEWTON_TRACE_ARMCPU=1 prints every call out of the ARM code
				and what it answered.
*/

#ifndef __PACKAGENATIVECPU_H
#define __PACKAGENATIVECPU_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

// the fallback installed (frames/PackageNatives.h)
void		InstallPackageNativeCPU(void);

// the fallback itself (the PackageNativeFallback signature)
Ref			RunPackageNativeOnCPU(RefArg code, ULong offset, RefArg rcvr, long numArgs, const RefVar* const* args);

// how many entry points of the public jump table the adapter answers, and
// whether it answers one (by mangled name)
long		PackageNativeCPUEntryCount(void);
Boolean		PackageNativeCPUAnswers(const char* name);

#endif	/* __PACKAGENATIVECPU_H */
