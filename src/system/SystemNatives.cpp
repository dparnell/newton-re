/*
	File:		system/SystemNatives.cpp

	Contains:	The machine's own NewtonScript functions (SystemNatives.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SystemNatives.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "hal/System.h"
#include "OSErrors.h"


// ROM 0x0020171c FGetSerialNumber
// The machine's serial number as an eight-byte binary of class
// 'serialNumber; nil when the serial number ROM cannot be read
// (hal/System.h's GetSystemSerialNumber - the host has a number of its
// own, see hal/host/System.cpp).
Ref
FGetSerialNumber(RefArg /*rcvr*/)
{
	RefVar binary(AllocateBinary(RefVar(RSSYMserialnumber), 2 * sizeof(ULong)));
	ULong* serialNumber = (ULong*) BinaryData(binary);
	if (GetSystemSerialNumber(serialNumber) != noErr)
		return NILREF;
	return binary;
}


void
RegisterSystemNatives(void)
{
	RegisterNativeFunction("FGetSerialNumber", (void*) FGetSerialNumber, 0);
}
