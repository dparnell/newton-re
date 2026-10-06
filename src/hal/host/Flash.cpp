/*
	File:		hal/host/Flash.cpp

	Contains:	The bank control register and the internal flash's
				programming voltage (hal/Flash.h) on a host.  The host's
				flash bank (HostFlash.h) is a 32-bit bank whatever the
				register says, so the register is only remembered; there is
				no programming voltage to switch.
*/

#include "hal/Flash.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "host/RomBugs.h"

ULong	gHostBankControlRegister = 0;		// the register at 0x0F241000
ULong	gHostInternalVppCount = 0;			// the ROM's gInternalVppCount

static TBankControlRegister	gBankControlRegister;		// ROM 0x0c1008cc (unnamed)


// ROM 0x0003b308 GetBankControlRegister__20TBankControlRegisterSFv
TBankControlRegister*
TBankControlRegister::GetBankControlRegister(void)
{
	if (gBankControlRegister.fMade == 0)
		gBankControlRegister.fMade = 1;
	return &gBankControlRegister;
}


// ROM 0x0003b298 ConfigureFlashBankDataSize__20TBankControlRegisterF11eMemoryLane
// Bits 8-10 of the register: 0 a 32-bit bus, 2 and 3 the high and low
// halves, 4 and 5 the top byte and the second byte.  The two bytes of the
// low half have no setting of their own.
//
// ROM BUG (fixed): a lane set with no setting answers 0x293b
// (kError_Flash_Bad_Lanes), kError_Flash_Erase_Failed without its sign, so
// a caller testing for an error (< 0) takes it for success.  The fix
// answers the signed error, kError_Flash_Erase_Failed.
NewtonErr
TBankControlRegister::ConfigureFlashBankDataSize(eMemoryLane lanes)
{
	ULong value;
	if (lanes == kAllLanes)
		value = 0;
	else if (lanes == 0xFF00)
		value = 0x500;
	else if (lanes == kLowHalfLanes)
		value = 0x300;
	else if (lanes == 0xFF000000)
		value = 0x400;
	else if (lanes == kHighHalfLanes)
		value = 0x200;
	else if (RomBugFixed())
		return kError_Flash_Erase_Failed;
	else
		return kError_Flash_Bad_Lanes;
	SetBankControlRegister(value, 0x700);
	return noErr;
}


// ROM 0x0003b324 SetBankControlRegister__20TBankControlRegisterFUlT1
// DEVIATION: the register is hardware; the host keeps the value it would hold.
ULong
TBankControlRegister::SetBankControlRegister(ULong value, ULong mask)
{
	gHostBankControlRegister = value | (gHostBankControlRegister & ~mask & 0x7FF);
	return gHostBankControlRegister;
}


// ROM 0x00050740 InternalVppOn__Fv
// DEVIATION: the ROM powers the flash's Vpp supply up (IOPowerOn 0x1d or
// 0x1e) and tells the card server, under gInternalPowerSemaphore; a host's
// flash needs no programming voltage, so only the count is kept.
void
InternalVppOn(void)
{
	gHostInternalVppCount++;
}


// ROM 0x0005086c InternalVppIdleOff__FUc
// DEVIATION: as InternalVppOn - the ROM counts gInternalVppOffCountdown
// down each time the card server's idler asks and powers the supply off
// (IOPowerOff 0x1d or 0x1e) at the end, answering whether it is still on;
// a host's flash has no supply, so it is never on.
Boolean
InternalVppIdleOff(Boolean /*force_off*/)
{
	return false;
}


// ROM 0x00050808 InternalVppOff__Fv
// DEVIATION: as InternalVppOn - the ROM starts gInternalVppOffCountdown
// (0x1c20000) when the count comes back to nought.
void
InternalVppOff(void)
{
	if (gHostInternalVppCount != 0)
		gHostInternalVppCount--;
}
