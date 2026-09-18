/*
	File:		system/ConfigServer.cpp

	Contains:	TNSConfigServer and CSInstantiate (ConfigServer.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "ConfigServer.h"
#include "Frames.h"
#include "NativeFunctions.h"
#include "Interpreter.h"
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


// ROM 0x0013c7f4 GetConfig__15TNSConfigServerFPl
// The configuration registered for this service, as a string of its four
// characters; the error comes back separately, and "there is none"
// (kError_Not_Registered, which the name server answers for a name it
// does not know) is not one - it simply has no configuration yet.
Ref
TNSConfigServer::GetConfig(NewtonErr* err)
{
	ULong configID = 0;
	*err = GetDefaultConfig(fServiceType, fName, &configID, nil);
	if (*err == kError_Not_Registered)
	{
		*err = noErr;
		return NILREF;
	}
	char name[5];
	ULongStrToCStr(configID, name);
	return MakeString(name);
}


// ROM 0x0013c888 SetConfig__15TNSConfigServerFRC6RefVar
// The service's configuration set from a string of four characters; nil
// takes the registration away, and "there was none to take away" is not an
// error.
NewtonErr
TNSConfigServer::SetConfig(RefArg config)
{
	ULong configID = 0;
	if (NOTNIL(config))
		ConvertFromUnicode(GetCString(config), &configID, kMacRomanEncoding, sizeof(configID));
	NewtonErr err = SetDefaultConfig(fServiceType, fName, configID, 0);
	// nothing was registered under that name: that is only an error when
	// there was meant to be something to replace
	if (err == kError_Not_Registered && configID == 0)
		err = noErr;
	return err;
}


// ROM 0x000ad564 GetClient__FRC6RefVar
// The C++ object a protoConfigServer frame carries in its ciPrivate slot.
TNSConfigServer*
GetClient(RefArg rcvr)
{
	if (ISNIL(rcvr))
		return nil;
	RefVar object(GetVariable(rcvr, RSSYMciprivate, nil, 0));
	if (ISNIL(object))
		return nil;
	return (TNSConfigServer*) RefToAddress(object);
}


// ROM 0x0013bba4 CSGetDefaultConfig
Ref
CSGetDefaultConfig(RefArg rcvr)
{
	TNSConfigServer* server = GetClient(rcvr);
	if (server == nil)
		Throw((ExceptionName) "evt.ex.comm", (void*) (Long) -1, nil);
	NewtonErr err = noErr;
	RefVar config(server->GetConfig(&err));
	if (err != noErr)
		Throw((ExceptionName) "evt.ex.comm", (void*) (Long) err, nil);
	return config;
}


// ROM 0x0013bc4c CSSetDefaultConfig
Ref
CSSetDefaultConfig(RefArg rcvr, RefArg config)
{
	TNSConfigServer* server = GetClient(rcvr);
	NewtonErr err = server == nil ? (NewtonErr) -1 : server->SetConfig(config);
	if (err != noErr)
		Throw((ExceptionName) "evt.ex.comm", (void*) (Long) err, nil);
	return NILREF;
}


// ROM 0x0013c9a4 CSDispose
// protoConfigServer:Dispose(): the C++ object let go and the frame's
// ciPrivate slot emptied.
Ref
CSDispose(RefArg rcvr)
{
	TNSConfigServer* server = GetClient(rcvr);
	if (server != nil)
		delete server;
	SetFrameSlot(rcvr, RSSYMciprivate, RefVar(NILREF));
	return NILREF;
}


void
RegisterConfigServerNatives(void)
{
	RegisterNativeFunction("CSDispose", (void*) CSDispose, 0);
	RegisterNativeFunction("CSInstantiate", (void*) CSInstantiate, 2);
	RegisterNativeFunction("CSGetDefaultConfig", (void*) CSGetDefaultConfig, 0);
	RegisterNativeFunction("CSSetDefaultConfig", (void*) CSSetDefaultConfig, 1);
}
