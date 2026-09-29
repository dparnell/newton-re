/*
	File:		user/UserConfigServer.cpp

	Contains:	TUConfigServer (ddk/HALOptions.h), the name server used to
				say which piece of hardware a communications service runs
				on.  It adds no state to TUNameServer: a service is named
				by four characters rather than a string, so every call
				writes those four bytes into a five-byte buffer and asks
				the name server about that.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "HALOptions.h"
#include "ByteOrder.h"


// ROM 0x000e5fdc ULongStrToCStr__14TUConfigServerFUlPc
// The four characters of a name as a C string.
//
// DEVIATION: the ROM stores the word straight into the buffer
// (*(ULong*)nameStr = name), which on the Newton puts the characters in
// the order they were written - it is big-endian - and wants the buffer
// word aligned, which its callers' are and a host's char[5] on the stack
// is not.  The bytes are written one at a time, most significant first:
// the same result on the Newton, and the right one on a host of either
// byte order.
void
TUConfigServer::ULongStrToCStr(ULong name, char* nameStr)
{
	PutBigEndianWord(nameStr, (unsigned int) name);
	nameStr[4] = '\0';
}


// ROM 0x000e5fec GetDefaultHWLoc__14TUConfigServerFUlPUlT2
// A service's default hardware location: its "DefHWLoc" configuration.
NewtonErr
TUConfigServer::GetDefaultHWLoc(ULong serviceID, ULong* hwLocIDPtr, ULong* flagsPtr)
{
	return GetDefaultConfig(serviceID, (char*) "DefHWLoc", hwLocIDPtr, flagsPtr);
}


// ROM 0x000e623c SetDefaultHWLoc__14TUConfigServerFUlN21
NewtonErr
TUConfigServer::SetDefaultHWLoc(ULong serviceID, ULong hwLocID, ULong flags)
{
	return SetDefaultConfig(serviceID, (char*) "DefHWLoc", hwLocID, flags);
}


// ROM 0x000e61e8 GetDefaultConfig__14TUConfigServerFUlPcPUlT3
// The configuration registered for a service; flagsPtr may be nil, and
// the ROM still asks for the flags into a place of its own.
NewtonErr
TUConfigServer::GetDefaultConfig(ULong serviceID, char* configType, ULong* configIDPtr, ULong* flagsPtr)
{
	char name[5];
	ULongStrToCStr(serviceID, name);
	ULong flags;
	if (flagsPtr == nil)
		flagsPtr = &flags;
	return Lookup(name, configType, configIDPtr, flagsPtr);
}


// ROM 0x000e6268 SetDefaultConfig__14TUConfigServerFUlPcN21
// The service's configuration set: whatever was registered is taken away
// first, and a configuration of nought only takes it away.
NewtonErr
TUConfigServer::SetDefaultConfig(ULong serviceID, char* configType, ULong configID, ULong flags)
{
	char name[5];
	ULongStrToCStr(serviceID, name);
	NewtonErr err = UnRegisterName(name, configType);
	if (configID != 0)
		err = RegisterName(name, configType, configID, flags);
	return err;
}


// ROM 0x000e62cc RegisterULongName__14TUConfigServerFUlPcN21
NewtonErr
TUConfigServer::RegisterULongName(ULong name, char* type, ULong thing, ULong spec)
{
	char nameStr[5];
	ULongStrToCStr(name, nameStr);
	return RegisterName(nameStr, type, thing, spec);
}


// ROM 0x000e6314 UnRegisterULongName__14TUConfigServerFUlPc
NewtonErr
TUConfigServer::UnRegisterULongName(ULong name, char* type)
{
	char nameStr[5];
	ULongStrToCStr(name, nameStr);
	return UnRegisterName(nameStr, type);
}


// TUConfigServer::LookupULongName - the DDK declares it, and this ROM
// has no such function: nothing called it, so the linker left it out.
// It is here because the header promises it, and because the other two
// halves of the pair are.
NewtonErr
TUConfigServer::LookupULongName(ULong name, char* type, ULong* thing, ULong* spec)
{
	char nameStr[5];
	ULongStrToCStr(name, nameStr);
	return Lookup(nameStr, type, thing, spec);
}
