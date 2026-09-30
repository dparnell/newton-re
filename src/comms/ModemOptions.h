/*
	File:		comms/ModemOptions.h

	Contains:	The modem tool's options (comms/ModemTool.h) - how a modem
				dials ('mdo ', TCMOModemDialing), what it is and what it is
				told to do at each step ('mpro', TCMOModemProfile: its
				speeds, delays and six strings), the user's preferences
				('mpre'), what the connection is for ('mcto'), the error
				correction wanted ('mecp'), the speed it connected at
				('mspd'), voice ('mvso'), the fax capabilities and class
				('mfax', 'mfec', 'mfsq', 'mfsc', 'mf1c') and the telephone
				API's ('taps', 'tasp', 'hsmn') - and SetDialingOptionsFromPrefs,
				which fills a TCMOModemDialing in from the user's modem
				preferences.

				Their declarations are not in the DDK: the field names are
				ours, their order the ROM's (offsets noted).  The fax
				capabilities are bit sets: a class is 1 (data), 2 (Class
				1), 4 (Class 2), 8 (Class 2.0); a modulation 1 (V.21
				channel 2, 300 bps), 2 and 4 (V.27ter 2400 and 4800), 8
				(V.29 7200), 0x10 and 0x20 (V.17 7200, long and short
				training), 0x40 (V.29 9600), 0x80 and 0x100 (V.17 9600),
				0x200 and 0x400 (V.17 12000), 0x800 and 0x1000 (V.17
				14400) - TClassOneModem::C1GetCapExtractResult.

	Reconstructed from the MP2x00 US ROM (0x0011f384-0x0011fad4,
	0x00206520-0x00206848, 0x00149f40); each function cites its origin.
*/

#ifndef __COMMS_MODEMOPTIONS_H
#define __COMMS_MODEMOPTIONS_H

#ifndef __OPTIONARRAY_H
#include "OptionArray.h"
#endif

#define kCMOModemDialing				'mdo '
#define kCMOModemProfile				'mpro'
#define kCMOModemPrefs					'mpre'
#define kCMOModemConnectType			'mcto'
#define kCMOModemECType					'mecp'
#define kCMOModemConnectSpeed			'mspd'
#define kCMOModemVoiceSupport			'mvso'
#define kCMOModemFaxCapabilities		'mfax'
#define kCMOModemFaxEnabledCaps			'mfec'
#define kCMOModemFaxClassesSupported	'mfsq'
#define kCMOModemFaxClass				'mfsc'
#define kCMOModemFaxClass1Cap			'mf1c'
#define kCMOTAPIService					'taps'
#define kCMOTAPISpeaker					'tasp'
#define kCMOHandsetManagement			'hsmn'

// the fax classes (TCMOModemFaxClass's fClass, the capabilities' classes)
#define kModemFaxClass0					0x00000001		// data
#define kModemFaxClass1					0x00000002
#define kModemFaxClass2					0x00000004
#define kModemFaxClass20				0x00000008		// Class 2.0

// 'mdo ': how to dial
class TCMOModemDialing : public TOption
{
public:
					TCMOModemDialing();

	UByte			fSpeakerOn;				// +0x0c
	UByte			fDetectDialTone;		// +0x0d
	UByte			fDetectBusy;			// +0x0e
	UByte			fDTMFToneDialing;		// +0x0f
	UByte			fManualDial;			// +0x10
	UByte			fSpeakerVolume;			// +0x11  a character: '1' to '3'
	UByte			fWaitForCarrier;		// +0x12  seconds
	UByte			fWaitBeforeBlindDial;	// +0x13  seconds
	UByte			fCommaDelay;			// +0x14  seconds
	UByte			fRingToAnswerAfter;		// +0x15
	Long			fCountryCode;			// +0x18
	Boolean			fCellular;				// +0x1c
};

// 'mpro': the modem - its speeds and delays, and six strings one after
// another behind the fields: its identity, and the command setting it up
// for a connection without error correction, with it only, with it falling
// back to none, for a cellular connection, and directly
class TCMOModemProfile : public TOption
{
public:
					TCMOModemProfile(ULong stringsSize);

	void			SetDefault(ULong stringsSize);
	UChar*			GetModemString(Long index);
	void			SetModemStrings(const UChar* id, const UChar* noEC, const UChar* ecOnly, const UChar* ecFallBack,
									const UChar* cellular, const UChar* direct);

	Boolean			fCellularSupported;		// +0x0c  (the cellular string is not empty)
	Boolean			fMNP10;					// +0x0d  MNP 10 supported
	Boolean			fField0E;				// +0x0e
	Boolean			fPassThrough;			// +0x0f  its own error correction may be used over a direct connection
	ULong			fECTypes;				// +0x10  the error correction it has ('mecp' bits; 0xff)
	ULong			fSerialSpeed;			// +0x14  the speed it is spoken to at (19200)
	ULong			fCommandTimeout;		// +0x18  milliseconds a command's answer is waited for (2000)
	ULong			fMaxCommandLength;		// +0x1c  (40)
	ULong			fCommandDelay;			// +0x20  milliseconds between the last answer and the next command (25)
	ULong			fStringsSize;			// +0x24
	UChar			fStrings[1];			// +0x28  ... six C strings
};

// the size of a profile whose strings take stringsSize bytes (host)
#define ModemProfileSize(stringsSize)	(offsetof(TCMOModemProfile, fStrings) + (stringsSize))

// 'mpre': the modem's preferences
class TCMOModemPrefs : public TOption
{
public:
					TCMOModemPrefs();

	Boolean			fDirectConnect;			// +0x0c  (0) a direct connection, at fDirectSpeed, without error correction
	Boolean			fCheckResponse;			// +0x0d  (1) the modem must answer "are you there"
	Boolean			fUnknownRefused;		// +0x0e  (0) a modem it does not know is refused
	Boolean			fDropOnCarrierLoss;		// +0x0f  (1) the connection is dropped when the carrier goes
	Boolean			fField10;				// +0x10
	Boolean			fConfigureModem;		// +0x11  (1) the modem's configuration string is sent
	Boolean			fSendDialPrefs;			// +0x12  (1) the dialing preferences are sent
	Boolean			fHangUpAtDisconnect;	// +0x13  (1)
	Boolean			fNavigatorOpen;			// +0x14  set by the navigator's endpoint: the line is the navigator's, left as it is
	Boolean			fField15;				// +0x15
	ULong			fDirectSpeed;			// +0x18  (19200) a direct connection's speed
	ULong			fCarrierDownSlow;		// +0x1c  (3) seconds the carrier may drop at 2400 bps and below
	ULong			fCarrierDownFast;		// +0x20  (15) and above
	Boolean			fStripDashes;			// +0x24  (1) the dashes are taken out of a number dialed
};

// 'mpre' as 1.x wrote it, without fStripDashes
class TCMOModemPrefs_Ver_1 : public TOption
{
public:
					TCMOModemPrefs_Ver_1();

	Boolean			fDirectConnect;			// +0x0c
	Boolean			fCheckResponse;			// +0x0d
	Boolean			fUnknownRefused;		// +0x0e
	Boolean			fDropOnCarrierLoss;		// +0x0f
	Boolean			fField10;				// +0x10
	Boolean			fConfigureModem;		// +0x11
	Boolean			fSendDialPrefs;			// +0x12
	Boolean			fHangUpAtDisconnect;	// +0x13
	Boolean			fNavigatorOpen;			// +0x14
	Boolean			fField15;				// +0x15
	ULong			fDirectSpeed;			// +0x18
	ULong			fCarrierDownSlow;		// +0x1c
	ULong			fCarrierDownFast;		// +0x20
};

// 'mcto': what the connection is for
class TCMOModemConnectType : public TOption
{
public:
					TCMOModemConnectType();

	Boolean			fVoice;					// +0x0c
	Boolean			fFax;					// +0x0d
	Boolean			fData;					// +0x0e  (1)
	Boolean			fField0F;				// +0x0f
	Boolean			fAlreadyConnected;		// +0x10  the call is up (answered by hand): go on line
};

// 'mecp': the error correction wanted (bits: 1 MNP allowed, 2/4/8 V.42
// kinds, 0x10 MNP required; 7 by default)
class TCMOModemECType : public TOption
{
public:
					TCMOModemECType();

	ULong			fType;					// +0x0c
};

// 'mspd': the speed the modem connected at
class TCMOModemConnectSpeed : public TOption
{
public:
					TCMOModemConnectSpeed();

	ULong			fSpeed;					// +0x0c
};

// 'mvso': voice
class TCMOModemVoiceSupport : public TOption
{
public:
					TCMOModemVoiceSupport();

	Boolean			fSupportsVoice;			// +0x0c
};

// 'mfax' and 'mfec': the fax classes and modulations the modem has, and
// those that may be used
class TCMOModemFaxCapabilities : public TOptionExtended
{
public:
					TCMOModemFaxCapabilities();

	ULong			fServiceClasses;		// +0x14  (7)
	ULong			fTransmitDataMods;		// +0x18  +FTM (0x1ffe)
	ULong			fTransmitHDLCMods;		// +0x1c  +FTH (1)
	ULong			fReceiveDataMods;		// +0x20  +FRM (0x1ffe)
	ULong			fReceiveHDLCMods;		// +0x24  +FRH (1)
};

class TCMOModemFaxEnabledCaps : public TOptionExtended
{
public:
					TCMOModemFaxEnabledCaps();

	ULong			fServiceClasses;		// +0x14
	ULong			fTransmitDataMods;		// +0x18
	ULong			fTransmitHDLCMods;		// +0x1c
	ULong			fReceiveDataMods;		// +0x20
	ULong			fReceiveHDLCMods;		// +0x24
};

// 'mfsq': the fax classes the modem has
class TCMOModemFaxClassesSupported : public TOptionExtended
{
public:
					TCMOModemFaxClassesSupported();

	ULong			fClasses;				// +0x14
};

// 'mfsc': the fax class the modem is put in
class TCMOModemFaxClass : public TOptionExtended
{
public:
					TCMOModemFaxClass();

	ULong			fClass;					// +0x14  kModemFaxClass... (7)
};

// 'mf1c': the Class 1 modulations the modem has
class TCMOModemFaxClass1Cap : public TOptionExtended
{
public:
					TCMOModemFaxClass1Cap();

	ULong			fTransmitDataMods;		// +0x14
	ULong			fTransmitHDLCMods;		// +0x18
	ULong			fReceiveDataMods;		// +0x1c
	ULong			fReceiveHDLCMods;		// +0x20
};

// 'taps': the telephone API's service
class TCMOTAPIService : public TOption
{
public:
					TCMOTAPIService();

	Boolean			fActive;				// +0x0c
	Boolean			fActive2;				// +0x0d  (both must be set)
	Boolean			fField0E;				// +0x0e
	Boolean			fField0F;				// +0x0f  (1)
};

// 'tasp': the telephone API's speaker
class TCMOTAPISpeaker : public TOption
{
public:
					TCMOTAPISpeaker();

	Boolean			fSpeakerOn;				// +0x0c  (1)
};

// 'hsmn': the handset
class TCMOHandsetManagement : public TOption
{
public:
					TCMOHandsetManagement();

	Boolean			fManaged;				// +0x0c  (1)
};

void		SetDialingOptionsFromPrefs(TCMOModemDialing* option);

#endif	/* __COMMS_MODEMOPTIONS_H */
