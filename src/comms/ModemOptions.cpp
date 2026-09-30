/*
	File:		comms/ModemOptions.cpp

	Contains:	SetDialingOptionsFromPrefs - ModemOptions.h.  (The options
				themselves are ModemToolOptions.cpp's, with the tool.)

	Reconstructed from the MP2x00 US ROM (0x00149f40); each function cites
	its origin.
*/

#include "ModemOptions.h"
#include "Locale.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "RSSymbols.h"

// an integer preference, or the ROM's error for anything else
static long
IntPreference(RefArg value)
{
	return RINT(value);
}


// ROM 0x00149f40 SetDialingOptionsFromPrefs__FP16TCMOModemDialing
// The option (required) filled in from the modem preferences: the speaker
// by its volume, manual and blind dialing, tone or pulse, the three waits,
// and the country's dialing code (10 for Canada, else its GetCountryEntry
// frame's countryCode).
void
SetDialingOptionsFromPrefs(TCMOModemDialing* option)
{
	option->SetOpCode(opSetRequired);
	RefVar pref(GetPreference(RefVar(RSSYMmodemsoundvolume)));
	long volume = IntPreference(pref);
	option->fSpeakerOn = volume > 0;
	if (volume > 0)
	{
		if (volume == 1)
			option->fSpeakerVolume = '1';
		else if (volume == 2)
			option->fSpeakerVolume = '2';
		else if (volume == 3)
			option->fSpeakerVolume = '3';
	}
	RefVar manual(GetPreference(RefVar(RSSYMmanualdialing)));
	option->fManualDial = NOTNIL(manual);
	pref = GetPreference(RefVar(RSSYMblinddialing));
	option->fDetectDialTone = ISNIL(pref) && ISNIL(manual);
	option->fDetectBusy = ISNIL(manual);
	pref = GetPreference(RefVar(RSSYMdialing));
	option->fDTMFToneDialing = EQRef(RSSYMtouchtone, pref);
	pref = GetPreference(RefVar(RSSYMcarrierdelay));
	if (NOTNIL(pref))
		option->fWaitForCarrier = IntPreference(pref);
	pref = GetPreference(RefVar(RSSYMblinddialdelay));
	if (NOTNIL(pref))
		option->fWaitBeforeBlindDial = IntPreference(pref);
	pref = GetPreference(RefVar(RSSYMcommadelay));
	if (NOTNIL(pref))
		option->fCommaDelay = IntPreference(pref);
	pref = GetPreference(RefVar(RSSYMcurrentcountry));
	if (NOTNIL(pref))
	{
		if (EQRef(pref, RSSYMcanada))
			option->fCountryCode = 10;
		else
		{
			RefVar args(MakeArray(1));
			SetArraySlotRef(args, 0, pref);
			RefVar functions(gFunctionFrame);
			RefVar fn(GetFrameSlotRef(functions, RSSYMgetcountryentry));
			RefVar entry(DoBlock(fn, args));
			pref = GetArraySlotRef(entry, 0);
			if (NOTNIL(pref))
				option->fCountryCode = RINT(GetFrameSlotRef(pref, RSSYMcountrycode));
		}
	}
	pref = GetPreference(RefVar(RSSYMcellularconnection));
	option->fCellular = NOTNIL(pref);
}
