/*
	File:		system/ConfigServer.cpp

	Contains:	TNSConfigServer and CSInstantiate (ConfigServer.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "ConfigServer.h"
#include "Frames.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"


TNSConfigServer::TNSConfigServer()
{
	fName = nil;
	fServiceType = 0;
}


// ROM 0x0013c7a8 __dt__15TNSConfigServerFv
TNSConfigServer::~TNSConfigServer()
{
	if (fName != nil)
	{
		DisposPtr((Ptr) fName);
		fName = nil;
	}
}


// ROM 0x0013c724 InitConfigServer__15TNSConfigServerFRC6RefVarT1
// The service type's four characters taken out of the string into the
// ULong the name server keys on, and the configuration's name kept as a
// C string of its own.
//
// DEVIATION: the ROM converts with ConvertUnicodeCharacters (0x00254770,
// NOT YET RECONSTRUCTED - a hand-rolled loop that also handles the
// multi-byte encodings); for encoding 1, Mac Roman, which is the only one
// asked for here, ConvertFromUnicode does the same work.
NewtonErr
TNSConfigServer::InitConfigServer(RefArg serviceType, RefArg configName)
{
	ConvertFromUnicode(GetCString(serviceType), &fServiceType, kMacRomanEncoding, sizeof(fServiceType));
	long length = Umbstrlen(GetCString(configName));
	fName = (char*) NewPtrClear(length + 1);
	if (fName == nil)
		return kError_No_Memory;
	ConvertFromUnicode(GetCString(configName), fName, kMacRomanEncoding, length + 1);
	return noErr;
}


// ROM 0x0013c8fc CSInstantiate
// protoConfigServer:Instantiate(serviceType, configName): the C++ object
// made and hung off the frame's ciPrivate slot.  A failure is an
// evt.ex.comm throw.
Ref
CSInstantiate(RefArg rcvr, RefArg serviceType, RefArg configName)
{
	NewtonErr err = kError_No_Memory;
	TNSConfigServer* server = new TNSConfigServer;
	if (server != nil)
	{
		SetFrameSlot(rcvr, RSSYMciprivate, RefVar(AddressToRef(server)));
		err = server->InitConfigServer(serviceType, configName);
		if (err == noErr)
			return NILREF;
	}
	Throw((ExceptionName) "evt.ex.comm", (void*) (Long) err, nil);
	return NILREF;
}


void
RegisterConfigServerNatives(void)
{
	RegisterNativeFunction("CSInstantiate", (void*) CSInstantiate, 2);
}
