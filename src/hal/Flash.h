/*
	File:		hal/Flash.h

	Contains:	What the flash code asks of the hardware beneath the chips'
				drivers: the bank control register, which says how wide the
				internal flash bank's data bus is (all 32 bits, or one or two
				of its byte lanes presented as a narrower bus), and the
				programming voltage the internal flash needs switched on
				while it is written or erased.

				The flash itself - the ranges of chips, their drivers and the
				TFlash protocol - is stores/flash/Flash.h; where the chips'
				bytes are is hal/MMU.h's VirtualAddressToPointer.

	ROM:		TBankControlRegister 0x0003b298-0x0003b340,
				InternalVppOn 0x00050740, InternalVppOff 0x00050808
*/

#ifndef __HAL_FLASH_H
#define __HAL_FLASH_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

// A set of byte lanes of a 32-bit bus, as a mask of the bits they carry
// (the ROM's eMemoryLane): 0xFFFFFFFF all four, 0xFFFF and 0xFFFF0000 a
// 16-bit half, 0xFF .. 0xFF000000 one byte.
typedef ULong	eMemoryLane;

enum
{
	kAllLanes			= 0xFFFFFFFF,
	kLowHalfLanes		= 0x0000FFFF,
	kHighHalfLanes		= 0xFFFF0000
};

// What the bank control register answers a lane set it cannot make a bus
// of.  ROM BUG: 0x293b, a positive number - kError_Flash_Erase_Failed
// (-10555) without its sign - so a caller testing for an error (< 0) takes
// it for success.
enum
{
	kError_Flash_Bad_Lanes	= 0x293b
};


/*------------------------------------------------------------------------------
	T B a n k C o n t r o l R e g i s t e r
	The one register (0x0F241000) whose bits 8-10 choose the flash bank's
	data size.  The object is a token: GetBankControlRegister makes the one
	there is.
------------------------------------------------------------------------------*/

class TBankControlRegister
{
public:
	static TBankControlRegister*	GetBankControlRegister(void);		// ROM 0x0003b308 GetBankControlRegister__20TBankControlRegisterSFv

	NewtonErr	ConfigureFlashBankDataSize(eMemoryLane lanes);		// ROM 0x0003b298 ConfigureFlashBankDataSize__20TBankControlRegisterF11eMemoryLane
	ULong		SetBankControlRegister(ULong value, ULong mask);	// ROM 0x0003b324 SetBankControlRegister__20TBankControlRegisterFUlT1

	ULong		fMade;				// set once GetBankControlRegister has been asked
};


// The internal flash's programming voltage, counted: the first On turns
// it on, the last Off starts the countdown to turning it off.
void	InternalVppOn(void);		// ROM 0x00050740 InternalVppOn__Fv
void	InternalVppOff(void);		// ROM 0x00050808 InternalVppOff__Fv

#endif	/* __HAL_FLASH_H */
