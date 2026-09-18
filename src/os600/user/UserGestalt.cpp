/*
	File:		user/UserGestalt.cpp

	Contains:	TUGestalt (NewtonGestalt.h): the machine-information query.
				A selector the system answers goes to the name server as a
				TGestaltRequest; selectors others register (RegisterGestalt)
				are names in the name server, "<selector in hex>" of type
				"GSLT", whose thing is the parameter block's address and spec
				its size, copied straight out of the registrant's memory.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "NewtonGestalt.h"
#include "NameServer.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "UserGlobals.h"
#include "OSErrors.h"

#include <stdio.h>

static char kGestaltNameType[] = "GSLT";

// the selectors the system itself answers (kGestalt_Base + 1 ...)
static inline Boolean
IsSystemSelector(GestaltSelector selector)
{
	return selector > kGestalt_Base && selector <= kGestalt_Extended_Base - 1;
}


// ROM 0x00131780 __ct__9TUGestaltFv
TUGestalt::TUGestalt()
{
	fGestaltPort.CopyObject(GetPortSWI(kGetNameServerPort));
}


// ROM 0x001317cc Gestalt__9TUGestaltFUlPvT1
NewtonErr
TUGestalt::Gestalt(GestaltSelector selector, void* paramBlock, ULong paramSize)
{
	ULong size = paramSize;
	return Gestalt(selector, paramBlock, &size);
}


// ROM 0x001317ec Gestalt__9TUGestaltFUlPvPUl
// A registered selector's block is copied (as much of it as fits; the
// registrant's memory may be gone, so a bus error is caught and is the
// result); paramSize ends up as the registered size.  Otherwise the name
// server answers (paramSize is left alone).
NewtonErr
TUGestalt::Gestalt(GestaltSelector selector, void* paramBlock, ULong* paramSize)
{
	TUNameServer nameServer;
	char name[12];
	ULong block = 0, blockSize = 0;
	sprintf(name, "%lx", (unsigned long) selector);
	nameServer.Lookup(name, kGestaltNameType, &block, &blockSize);
	if (block == 0)
	{
		TGestaltRequest request;
		request.fSelector = selector;
		ULong returnSize;
		return fGestaltPort.SendRPC(&returnSize, &request, sizeof(request), paramBlock, *paramSize);
	}
	if (*paramSize > blockSize)
		*paramSize = blockSize;
	NewtonErr err = noErr;
	newton_try
	{
		BlockMove((void*) (uintptr_t) block, paramBlock, *paramSize);
	}
	newton_catch(exAbort)
	{
		err = (NewtonErr) (intptr_t) CurrentException()->data;
	}
	newton_catch(exBusError)
	{
		err = (NewtonErr) (intptr_t) CurrentException()->data;
	}
	end_try;
	*paramSize = blockSize;
	return err;
}


// ROM 0x00131994 RegisterGestalt__9TUGestaltFUlPvT1
// A selector outside the system's range, not yet registered.
NewtonErr
TUGestalt::RegisterGestalt(GestaltSelector selector, void* paramBlock, ULong paramSize)
{
	TUNameServer nameServer;
	char name[12];
	ULong block = 0, blockSize = 0;
	sprintf(name, "%lx", (unsigned long) selector);
	nameServer.Lookup(name, kGestaltNameType, &block, &blockSize);
	if (block == 0 && !IsSystemSelector(selector))
		return nameServer.RegisterName(name, kGestaltNameType, (ULong) (uintptr_t) paramBlock, paramSize);
	return kError_Already_Registered;
}


// ROM 0x00131a6c ReplaceGestalt__9TUGestaltFUlPvT1
// Re-registers; a system selector that is not registered cannot be.
NewtonErr
TUGestalt::ReplaceGestalt(GestaltSelector selector, void* paramBlock, ULong paramSize)
{
	TUNameServer nameServer;
	char name[12];
	ULong block = 0, blockSize = 0;
	sprintf(name, "%lx", (unsigned long) selector);
	nameServer.Lookup(name, kGestaltNameType, &block, &blockSize);
	if (block == 0)
	{
		if (IsSystemSelector(selector))
			return kError_Bad_Parameters;
	}
	else
		nameServer.UnRegisterName(name, kGestaltNameType);
	return nameServer.RegisterName(name, kGestaltNameType, (ULong) (uintptr_t) paramBlock, paramSize);
}
