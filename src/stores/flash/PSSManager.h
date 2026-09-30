/*
	File:		stores/flash/PSSManager.h

	Contains:	The PSS manager's internal half (PSSManager.cpp): the
				internal store made on the internal flash at boot.
*/

#ifndef __PSSMANAGER_H
#define __PSSMANAGER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

ULong		InternalStoreInfo(int which);						// ROM 0x0011e250 InternalStoreInfo
NewtonErr	MapInternalFlashWindows(void);						// (part of InitCGlobals, ROM 0x000453b4)
NewtonErr	InitPSSManager(ULong environment, ULong);			// ROM 0x001553cc InitPSSManager__FUlT1

#endif	/* __PSSMANAGER_H */
