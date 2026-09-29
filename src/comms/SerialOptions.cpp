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
#include "HALOptions.h"

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


// ROM 0x0037772c SCC1
const SCCChip	SCC1 = (SCCChip) "1";


// ROM 0x001dd988 __ct__19TCMOSerialHWChipLocFv
// 'schp: the chip by its location (the external port, by default).
TCMOSerialHWChipLoc::TCMOSerialHWChipLoc()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialHWChipLoc);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialHWChipLoc));
	fHWLoc = kHWLocExternalSerial;
	fService = 0;
}


// ROM 0x001ddad0 __ct__20TCMOSerialMiscConfigFv
// 'smsc: the input's send-for-interrupt delay (20 ms), DMA and transceiver
// choices.
TCMOSerialMiscConfig::TCMOSerialMiscConfig()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialMiscConfig);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialMiscConfig));
	inputDelay = 0x11ff8;			// (20 ms)
	disableInputDMA = false;
	disableOutputDMA = false;
	txdOffUntilSend = false;
	txdOnIfGPiOn = false;
	txdOnIfHSKiOn = false;
}


// ROM 0x001ddb40 __ct__16TCMOBreakFramingFv
TCMOBreakFraming::TCMOBreakFraming()
	: TOption(kOptionType)
{
	SetLabel(kCMOBreakFraming);
	SetLength(OPTION_DATA_LENGTH(TCMOBreakFraming));
	fBreakOnTime = 0;
	fBreakOffTime = 0;
	fUseHighSpeedClock = false;
	fRepeatCount = 0;
}


// ROM 0x001ddc48 __ct__22TCMOSerialEventEnablesFv
TCMOSerialEventEnables::TCMOSerialEventEnables()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialEventEnables);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialEventEnables));
	serEventEnables = 0;
	carrierDetectDownTime = 0;
}


// ROM 0x001ddca0 __ct__17TCMOSerialIOStatsFv
TCMOSerialIOStats::TCMOSerialIOStats()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialIOStats);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialIOStats));
	parityErrCount = 0;
	framingErrCount = 0;
	softOverrunCount = 0;
	hardOverrunCount = 0;
	fGPiState = 0;
	fHSKiState = 0;
	fExternalClockDetect = false;
}


// ROM 0x001ddd0c __ct__20TCMOFlowControlParmsFv
// XON/XOFF, neither kind of flow control on.
TCMOFlowControlParms::TCMOFlowControlParms()
	: TOption(kOptionType)
{
	SetLength(OPTION_DATA_LENGTH(TCMOFlowControlParms));
	xonChar = 0x11;
	xoffChar = 0x13;
	useSoftFlowControl = false;
	useHardFlowControl = false;
	hardFlowBlocked = false;
	softFlowBlocked = false;
}


// ROM 0x001ddd70 __ct__25TCMOInputFlowControlParmsFv
TCMOInputFlowControlParms::TCMOInputFlowControlParms()
{
	SetLabel(kCMOInputFlowControlParms);
}


// ROM 0x001dddb0 __ct__26TCMOOutputFlowControlParmsFv
TCMOOutputFlowControlParms::TCMOOutputFlowControlParms()
{
	SetLabel(kCMOOutputFlowControlParms);
}


// ROM 0x001de228 __ct__18TCMOSerialHardwareFv
// 'scc : side A of the first SCC.
TCMOSerialHardware::TCMOSerialHardware()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialHardware);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialHardware));
	fSCCSide = sideA;
	fSCCChip = SCC1;
	fSCCService = 0;
}


// ROM 0x001de554 __ct__17TCMOSerialBuffersFv
// 'sbuf: 512 bytes each way, 8 markers.
TCMOSerialBuffers::TCMOSerialBuffers()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialBuffers);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialBuffers));
	fSendSize = 0x200;
	fRecvSize = 0x200;
	fRecvMarkers = 8;
}


// ROM 0x001de5b4 __ct__17TCMOSerialIOParmsFv
// 'siop: 9600 bps, 8 data bits, no parity, one stop bit.
TCMOSerialIOParms::TCMOSerialIOParms()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialIOParms);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialIOParms));
	fStopBits = 0;
	fParity = 0;
	fDataBits = 8;
	fSpeed = 9600;
}


// ROM 0x001de61c __ct__17TCMOSerialBitRateFv
TCMOSerialBitRate::TCMOSerialBitRate()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialBitRate);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialBitRate));
	fBitsPerSecond = 9600;
}


// ROM 0x001de6c8 __ct__20TCMOSerialHalfDuplexFv
TCMOSerialHalfDuplex::TCMOSerialHalfDuplex()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialHalfDuplex);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialHalfDuplex));
	fHalfDuplex = false;
}


// ROM 0x001ddba0 __ct__20TCMOSerialDTRControlFv
// 'sdtr: DTR asserted.
TCMOSerialDTRControl::TCMOSerialDTRControl()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialDTRControl);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialDTRControl));
	fAssertDTR = true;
}


// ROM 0x000e6088 __ct__22THMOHiSpeedClockOptionFv
// 'hclk: the chip's high-speed clock used.
THMOHiSpeedClockOption::THMOHiSpeedClockOption()
	: TOption(kOptionType)
{
	SetLabel(kHMOHiSpeedClockOption);
	SetLength(OPTION_DATA_LENGTH(THMOHiSpeedClockOption));
	fUseHiSpeedClock = true;
}
