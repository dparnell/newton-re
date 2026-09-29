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

#include <string.h>
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


// ROM 0x000e6190 __ct__19THMOSerIRLinkConfigFv
// 'irlk: the IR port's mode - Sharp's ASK, no flags (the status and the
// sensitivity are left as they are).
THMOSerIRLinkConfig::THMOSerIRLinkConfig()
	: TOption(kOptionType)
{
	SetLabel(kHMOSerIRLinkConfig);
	SetLength(OPTION_DATA_LENGTH(THMOSerIRLinkConfig));
	fIRLinkMode = kSerIRLink_SharpIR;
	fConfigFlags = kSerIRLinkCfg_Default;
}


// ROM 0x001de71c __ct__24TCMOSerialBytesAvailableFv
TCMOSerialBytesAvailable::TCMOSerialBytesAvailable()
	: TOption(kOptionType)
{
	SetLabel(kCMOSerialBytesAvailable);
	SetLength(OPTION_DATA_LENGTH(TCMOSerialBytesAvailable));
	fBytesAvailable = 0;
}


/*------------------------------------------------------------------------------
	The slow IR options (Sharp IR, comms/SharpIRTool.h)
------------------------------------------------------------------------------*/

// ROM 0x001de0a8 __ct__22TCMOSlowIRProtocolTypeFv
// 'irpt: negotiating, 9600 bps.
TCMOSlowIRProtocolType::TCMOSlowIRProtocolType()
	: TOption(kOptionType)
{
	SetLabel(kCMOSlowIRProtocolType);
	SetLength(OPTION_DATA_LENGTH(TCMOSlowIRProtocolType));
	protocol = irUsingNegotiateIR;
	options = irUsing9600;
}


// ROM 0x001de104 __ct__15TCMOSlowIRStatsFv
TCMOSlowIRStats::TCMOSlowIRStats()
	: TOption(kOptionType)
{
	SetLabel(kCMOSlowIRStats);
	SetLength(OPTION_DATA_LENGTH(TCMOSlowIRStats));
	dataPacketsIn = 0;
	dataPacketsOut = 0;
	dataRetries = 0;
	checkSumErrs = 0;
	falseStarts = 0;
	serialErrs = 0;
	protocolErrs = 0;
}


// ROM 0x001de170 __ct__15TCMOSlowIRSniffFv
TCMOSlowIRSniff::TCMOSlowIRSniff()
	: TOption(kOptionType)
{
	SetLabel(kCMOSlowIRSniff);
	SetLength(OPTION_DATA_LENGTH(TCMOSlowIRSniff));
	sniffEnable = true;
}


// ROM 0x001de1c4 __ct__17TCMOSlowIRBitBangFv
// A bit a millisecond, once.  (The DDK says bit banging defaults on; the
// ROM's default is off.)
TCMOSlowIRBitBang::TCMOSlowIRBitBang()
	: TOption(kOptionType)
{
	SetLabel(kCMOSlowIRBitBang);
	SetLength(OPTION_DATA_LENGTH(TCMOSlowIRBitBang));
	bitTime = 1000;
	count = 1;
	enableBitBangIR = false;
}


// ROM 0x001de294 __ct__17TCMOSlowIRConnectFv
TCMOSlowIRConnect::TCMOSlowIRConnect()
	: TOption(kOptionType)
{
	SetLabel(kCMOSlowIRConnect);
	SetLength(OPTION_DATA_LENGTH(TCMOSlowIRConnect));
	connectOptions = 0;
}


/*------------------------------------------------------------------------------
	The IrDA options (SerialOptions.h)
------------------------------------------------------------------------------*/

// ROM 0x001de2e8 __ct__17TCMOIrDADiscoveryFv
// Eight slots; a PDA; any peer; look for other traffic first.
TCMOIrDADiscovery::TCMOIrDADiscovery()
	: TOption(kOptionType)
{
	SetLabel(kCMOIrDADiscovery);
	SetLength(OPTION_DATA_LENGTH(TCMOIrDADiscovery));
	fProbeSlots = 8;
	fMyServiceHints = 2;
	fPeerServiceHints = 0xFFFFFFFF;
	fPeerDevAddr = 0;
	fMediaBusyCheck = 1;
}


// ROM 0x001de35c __ct__22TCMOIrDAReceiveBuffersFv
TCMOIrDAReceiveBuffers::TCMOIrDAReceiveBuffers()
	: TOption(kOptionType)
{
	SetLabel(kCMOIrDAReceiveBuffers);
	SetLength(OPTION_DATA_LENGTH(TCMOIrDAReceiveBuffers));
	fSize = 0x200;
	fCount = 1;
}


// ROM 0x001de3b8 __ct__22TCMOIrDALinkDisconnectFv
TCMOIrDALinkDisconnect::TCMOIrDALinkDisconnect()
	: TOption(kOptionType)
{
	SetLabel(kCMOIrDALinkDisconnect);
	SetLength(OPTION_DATA_LENGTH(TCMOIrDALinkDisconnect));
	fTimeout = 40;
}


// ROM 0x001de40c __ct__22TCMOIrDAConnectionInfoFv
// Both class names "X" - the peer's at the fourth byte, not after the
// first's terminator as the DDK's comment has it.
TCMOIrDAConnectionInfo::TCMOIrDAConnectionInfo()
	: TOption(kOptionType)
{
	SetLabel(kCMOIrDAConnectionInfo);
	SetLength(OPTION_DATA_LENGTH(TCMOIrDAConnectionInfo));
	fMyLSAPId = 0;
	fPeerLSAPId = 0;
	fMyNameLength = 1;
	fPeerNameLength = 1;
	fClassNames[0] = 'X';
	fClassNames[1] = 0;
	fClassNames[4] = 'X';
	fClassNames[5] = 0;
}


// ROM 0x001de488 __ct__23TCMOIrDAConnectUserDataFv
TCMOIrDAConnectUserData::TCMOIrDAConnectUserData()
	: TOption(kOptionType)
{
	SetLabel(kCMOIrDAConnectUserData);
	SetLength(OPTION_DATA_LENGTH(TCMOIrDAConnectUserData));
	fDataLength = 0;
}


// ROM 0x001de4dc __ct__23TCMOIrDAConnectAttrNameFv
// "IrDA:IrLMP:LsapSel", its length 18 (and its terminator copied too).
TCMOIrDAConnectAttrName::TCMOIrDAConnectAttrName()
	: TOption(kOptionType)
{
	SetLabel(kCMOIrDAConnectAttrName);
	SetLength(OPTION_DATA_LENGTH(TCMOIrDAConnectAttrName));
	fNameLength = 18;
	memcpy(fName, "IrDA:IrLMP:LsapSel", 19);
}
