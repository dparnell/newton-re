/*
	File:		comms/ModemToolOptions.cpp

	Contains:	The modem tool's options - ModemOptions.h.

	Reconstructed from the MP2x00 US ROM (0x0011f384-0x0011fad4,
	0x00206520-0x00206848); each function cites its origin.
*/

#include "ModemOptions.h"

#include <stddef.h>
#include <string.h>

#define OPTION_DATA_LENGTH(cls)	(sizeof(cls) - sizeof(TOption))

// DEVIATION (pointer size): what a profile's fields take before its
// strings - 0x1c in the ROM, the host's here
#define kProfileFieldsLength	(offsetof(TCMOModemProfile, fStrings) - sizeof(TOption))


// ROM 0x0011f384 __ct__15TCMOModemECTypeFv
TCMOModemECType::TCMOModemECType()
	: TOption(kOptionType)
{
	SetLabel(kCMOModemECType);
	SetLength(OPTION_DATA_LENGTH(TCMOModemECType));
	fType = 7;
}


// ROM 0x0011f3d8 __ct__21TCMOModemConnectSpeedFv
TCMOModemConnectSpeed::TCMOModemConnectSpeed()
	: TOption(kOptionType)
{
	SetLabel(kCMOModemConnectSpeed);
	SetLength(OPTION_DATA_LENGTH(TCMOModemConnectSpeed));
	fSpeed = 0;
}


// ROM 0x0011f42c __ct__20TCMOModemPrefs_Ver_1Fv
TCMOModemPrefs_Ver_1::TCMOModemPrefs_Ver_1()
	: TOption(kOptionType)
{
	SetLabel(kCMOModemPrefs);
	SetLength(OPTION_DATA_LENGTH(TCMOModemPrefs_Ver_1));
	fDirectConnect = false;
	fCheckResponse = true;
	fUnknownRefused = false;
	fDropOnCarrierLoss = true;
	fField10 = false;
	fConfigureModem = true;
	fSendDialPrefs = true;
	fHangUpAtDisconnect = true;
	fNavigatorOpen = false;
	fField15 = false;
	fDirectSpeed = 19200;
	fCarrierDownSlow = 3;
	fCarrierDownFast = 15;
}


// ROM 0x0011f4c0 __ct__14TCMOModemPrefsFv
TCMOModemPrefs::TCMOModemPrefs()
	: TOption(kOptionType)
{
	SetLabel(kCMOModemPrefs);
	SetLength(OPTION_DATA_LENGTH(TCMOModemPrefs));
	fDirectConnect = false;
	fCheckResponse = true;
	fUnknownRefused = false;
	fDropOnCarrierLoss = true;
	fField10 = false;
	fConfigureModem = true;
	fSendDialPrefs = true;
	fHangUpAtDisconnect = true;
	fNavigatorOpen = false;
	fField15 = false;
	fDirectSpeed = 19200;
	fCarrierDownSlow = 3;
	fCarrierDownFast = 15;
	fStripDashes = true;
}


// ROM 0x0011f558 __ct__16TCMOModemProfileFUl
// (The object must have room for stringsSize bytes of strings behind it -
// ModemProfileSize.)
TCMOModemProfile::TCMOModemProfile(ULong stringsSize)
	: TOption(kOptionType)
{
	SetDefault(stringsSize);
}


// ROM 0x0011f5a0 SetDefault__16TCMOModemProfileFUl
// No cellular, MNP 10 or pass-through; every kind of error correction;
// spoken to at 19200 bps, answers waited for 2 seconds, commands of 40
// characters at most, 25 ms between an answer and the next command.
void
TCMOModemProfile::SetDefault(ULong stringsSize)
{
	Reset();
	SetAsOption(kCMOModemProfile);
	fStringsSize = stringsSize;
	SetLength(kProfileFieldsLength + stringsSize);
	fCellularSupported = false;
	fMNP10 = false;
	fField0E = false;
	fPassThrough = true;
	fECTypes = 0xff;
	fSerialSpeed = 19200;
	fCommandTimeout = 2000;
	fMaxCommandLength = 40;
	fCommandDelay = 25;
}


// ROM 0x0011f618 GetModemString__16TCMOModemProfileFl
// The index'th of the six strings (nil past the sixth).
UChar*
TCMOModemProfile::GetModemString(Long index)
{
	if (index > 5)
		return nil;
	UChar* s = fStrings;
	for (Long i = 0; i < index; i++)
		s += strlen((char*) s) + 1;
	return s;
}


// ROM 0x0011f668 SetModemStrings__16TCMOModemProfileFPCUcN51
// The six strings copied in one after another, as many as there is room
// for (the first that does not fit and all after it left out).
void
TCMOModemProfile::SetModemStrings(const UChar* id, const UChar* noEC, const UChar* ecOnly, const UChar* ecFallBack,
								  const UChar* cellular, const UChar* direct)
{
	UChar* s = fStrings;
	Long room = Length() - kProfileFieldsLength;
	const UChar* strings[6] = { id, noEC, ecOnly, ecFallBack, cellular, direct };
	for (int i = 0; i < 6; i++)
	{
		Long size = strlen((const char*) strings[i]) + 1;
		if (size > room)
			return;
		strcpy((char*) s, (const char*) strings[i]);
		room -= size;
		s += size;
	}
}


// ROM 0x0011f780 __ct__24TCMOModemFaxCapabilitiesFv
TCMOModemFaxCapabilities::TCMOModemFaxCapabilities()
	: TOptionExtended(kOptionType)
{
	SetLabel(kCMOModemFaxCapabilities);
	SetLength(OPTION_DATA_LENGTH(TCMOModemFaxCapabilities));
	fServiceClasses = kModemFaxClass0 | kModemFaxClass1 | kModemFaxClass2;
	fTransmitDataMods = 0x1ffe;
	fTransmitHDLCMods = 1;
	fReceiveDataMods = 0x1ffe;
	fReceiveHDLCMods = 1;
}


// ROM 0x0011f7f8 __ct__23TCMOModemFaxEnabledCapsFv
TCMOModemFaxEnabledCaps::TCMOModemFaxEnabledCaps()
	: TOptionExtended(kOptionType)
{
	SetLabel(kCMOModemFaxEnabledCaps);
	SetLength(OPTION_DATA_LENGTH(TCMOModemFaxEnabledCaps));
	fServiceClasses = kModemFaxClass0 | kModemFaxClass1 | kModemFaxClass2;
	fTransmitDataMods = 0x1ffe;
	fTransmitHDLCMods = 1;
	fReceiveDataMods = 0x1ffe;
	fReceiveHDLCMods = 1;
}


// ROM 0x0011f870 __ct__28TCMOModemFaxClassesSupportedFv
TCMOModemFaxClassesSupported::TCMOModemFaxClassesSupported()
	: TOptionExtended(kOptionType)
{
	SetLabel(kCMOModemFaxClassesSupported);
	SetLength(OPTION_DATA_LENGTH(TCMOModemFaxClassesSupported));
	fClasses = 0;
}


// ROM 0x0011f8c4 __ct__17TCMOModemFaxClassFv
TCMOModemFaxClass::TCMOModemFaxClass()
	: TOptionExtended(kOptionType)
{
	SetLabel(kCMOModemFaxClass);
	SetLength(OPTION_DATA_LENGTH(TCMOModemFaxClass));
	fClass = kModemFaxClass0 | kModemFaxClass1 | kModemFaxClass2;
}


// ROM 0x0011f918 __ct__21TCMOModemFaxClass1CapFv
TCMOModemFaxClass1Cap::TCMOModemFaxClass1Cap()
	: TOptionExtended(kOptionType)
{
	SetLabel(kCMOModemFaxClass1Cap);
	SetLength(OPTION_DATA_LENGTH(TCMOModemFaxClass1Cap));
	fTransmitDataMods = 0x1ffe;
	fTransmitHDLCMods = 1;
	fReceiveDataMods = 0x1ffe;
	fReceiveHDLCMods = 1;
}


// ROM 0x0011f984 __ct__21TCMOModemVoiceSupportFv
TCMOModemVoiceSupport::TCMOModemVoiceSupport()
	: TOption(kOptionType)
{
	SetLabel(kCMOModemVoiceSupport);
	SetLength(OPTION_DATA_LENGTH(TCMOModemVoiceSupport));
	fSupportsVoice = false;
}


// ROM 0x0011f9d8 __ct__20TCMOModemConnectTypeFv
TCMOModemConnectType::TCMOModemConnectType()
	: TOption(kOptionType)
{
	SetLabel(kCMOModemConnectType);
	SetLength(OPTION_DATA_LENGTH(TCMOModemConnectType));
	fData = true;
	fFax = false;
	fVoice = false;
	fField0F = false;
	fAlreadyConnected = false;
}


// ROM 0x0011fa40 __ct__16TCMOModemDialingFv
// The speaker on, dial tone and busy detected, tone dialing, the speaker at
// '2', 55 seconds for the carrier, 4 before dialing blind, 1 for a comma,
// answering after 2 rings, country 1.
TCMOModemDialing::TCMOModemDialing()
	: TOption(kOptionType)
{
	SetLabel(kCMOModemDialing);
	SetLength(OPTION_DATA_LENGTH(TCMOModemDialing));
	fSpeakerOn = 1;
	fDetectDialTone = 1;
	fDetectBusy = 1;
	fDTMFToneDialing = 1;
	fManualDial = 0;
	fSpeakerVolume = '2';
	fWaitForCarrier = 55;
	fWaitBeforeBlindDial = 4;
	fCommaDelay = 1;
	fRingToAnswerAfter = 2;
	fCountryCode = 1;
	fCellular = false;
}


// ROM 0x0020652c __ct__15TCMOTAPISpeakerFv
TCMOTAPISpeaker::TCMOTAPISpeaker()
	: TOption(kOptionType)
{
	SetLabel(kCMOTAPISpeaker);
	SetLength(OPTION_DATA_LENGTH(TCMOTAPISpeaker));
	fSpeakerOn = true;
}


// ROM 0x00206580 __ct__21TCMOHandsetManagementFv
TCMOHandsetManagement::TCMOHandsetManagement()
	: TOption(kOptionType)
{
	SetLabel(kCMOHandsetManagement);
	SetLength(OPTION_DATA_LENGTH(TCMOHandsetManagement));
	fManaged = true;
}


// ROM 0x002067e8 __ct__15TCMOTAPIServiceFv
TCMOTAPIService::TCMOTAPIService()
	: TOption(kOptionType)
{
	SetLabel(kCMOTAPIService);
	SetLength(OPTION_DATA_LENGTH(TCMOTAPIService));
	fActive = false;
	fActive2 = false;
	fField0E = false;
	fField0F = true;
}
