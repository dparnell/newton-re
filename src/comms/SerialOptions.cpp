/*
	File:		comms/SerialOptions.cpp

	Contains:	The serial options (the DDK's SerialOptions.h): the options a
				serial tool and a serial chip are configured and asked
				through.

				DEVIATION (pointer size): an option's length is its data's
				size on the host (comms/Options.h, OPTION_DATA_LENGTH); the
				ROM's is its ARM layout's.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Options.h"
#include "SerialOptions.h"

#define OPTION_DATA_LENGTH(cls)	(sizeof(cls) - sizeof(TOption))


// ROM 0x001dd9e8 __ct__18TCMOSerialChipSpecFv
// 'sers: which chip - its location, what it can do, whether it is free.
TCMOSerialChipSpec::TCMOSerialChipSpec()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialChipSpec);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialChipSpec));
	fHWLoc = 0;
	fSerFeatures = 0;
	fSerOutSupported = 0;
	fSerInSupported = 0;
	fParitySupport = 0;
	fDataStopBitSupport = 0;
	fUARTType = 0;
	fChipNotInUse = true;
	fReserved2 = 0;
	fReserved3 = 0;
	fCIS_ManFID = 0;
	fCIS_ManFIDInfo = 0;
}
