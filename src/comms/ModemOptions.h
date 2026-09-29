/*
	File:		comms/ModemOptions.h

	Contains:	TCMOModemDialing ('mdo '), how a modem dials: the speaker,
				the dial tone and busy detection, tone or pulse dialing,
				manual dialing, the speaker's volume, the waits (for the
				carrier, before dialing blind, for a comma) and the rings
				before answering, the country and whether the connection is
				cellular - and SetDialingOptionsFromPrefs, which fills one
				in from the user's modem preferences.  The old script
				endpoint makes one from a script's 'mdo ' option frame
				(comms/ScriptEndpoint.h); the modem tool that reads it is
				NOT YET.

				The field names are ours, their order the ROM's (offsets
				noted).

	Reconstructed from the MP2x00 US ROM (0x0011fa40, 0x00149f40); each
	function cites its origin.
*/

#ifndef __COMMS_MODEMOPTIONS_H
#define __COMMS_MODEMOPTIONS_H

#ifndef __OPTIONARRAY_H
#include "OptionArray.h"
#endif

#define kCMOModemDialing		'mdo '

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

void		SetDialingOptionsFromPrefs(TCMOModemDialing* option);

#endif	/* __COMMS_MODEMOPTIONS_H */
