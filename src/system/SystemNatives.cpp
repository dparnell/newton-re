/*
	File:		system/SystemNatives.cpp

	Contains:	The machine's own NewtonScript functions (SystemNatives.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SystemNatives.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NewtonGestalt.h"
#include "ROMConstants.h"
#include "Frames.h"
#include "hal/System.h"
#include "hal/Power.h"
#include "ByteOrder.h"
#include "OSErrors.h"
#include "Screen.h"
#include "Keyboard.h"
#include "Locale.h"


// ROM 0x0020171c FGetSerialNumber
// The machine's serial number as an eight-byte binary of class
// 'serialNumber; nil when the serial number ROM cannot be read
// (hal/System.h's GetSystemSerialNumber - the host has a number of its
// own, see hal/host/System.cpp).
//
// DEVIATION: the ROM stores the two words straight into the binary,
// which on the Newton puts them there big-endian; a script reads them
// back with ExtractLong, which is big-endian everywhere, so on a
// little-endian host they have to be written that way explicitly.  The
// second word is what the internal store is signed with, so getting this
// wrong is what makes the machine say its store's signature has been
// altered.
Ref
FGetSerialNumber(RefArg /*rcvr*/)
{
	ULong serialNumber[2];
	if (GetSystemSerialNumber(serialNumber) != noErr)
		return NILREF;
	RefVar binary(AllocateBinary(RefVar(RSSYMserialnumber), 2 * sizeof(ULong32)));
	char* data = BinaryData(binary);
	PutBigEndianWord(data, serialNumber[0]);
	PutBigEndianWord(data + sizeof(ULong32), serialNumber[1]);
	return binary;
}


/*------------------------------------------------------------------------------
	G e s t a l t

	Gestalt(selector) is how a script asks the machine about itself.  Each
	selector the system answers has a parameter block of its own
	(NewtonGestalt.h) and a canonical frame in the ROM to clone and fill in;
	a selector the machine cannot answer gives nil rather than an error.
------------------------------------------------------------------------------*/

// ROM 0x00201e0c FGestalt
// Gestalt(selector): a frame describing that part of the machine, or nil.
// The selectors are kGestalt_Base + 1 up; an array asks the extended
// gestalts instead (ExtendedGestalt 0x00201bfc, NOT YET RECONSTRUCTED).
// kGestalt_SoundInfo and kGestalt_PCMCIAInfo answer nil: the ROM has a case
// for each and neither does anything.
Ref
FGestalt(RefArg /*rcvr*/, RefArg selector)
{
	RefVar result;
	TUGestalt gestalt;
	if (!ISINT(selector))
	{
		// NOT YET RECONSTRUCTED: ExtendedGestalt, the array form
		return NILREF;
	}
	switch (RINT(selector))
	{
	case kGestalt_Version:
	case kGestalt_NewtonScriptVersion:
		{
			TGestaltVersion version;
			if (gestalt.Gestalt(RINT(selector), &version, sizeof(version)) != noErr)
				break;
			result = Clone(RefVar(Rcanonicalgestaltversion));
			SetFrameSlot(result, RSSYMversion, RefVar(MAKEINT(version.fVersion)));
		}
		break;

	case kGestalt_SystemInfo:
		{
			TGestaltSystemInfo info;
			if (gestalt.Gestalt(kGestalt_SystemInfo, &info, sizeof(info)) != noErr)
				break;
			result = Clone(RefVar(Rcanonicalgestaltsysteminfo));
			SetFrameSlot(result, RSSYMmanufacturer, RefVar(MAKEINT(info.fManufacturer)));
			SetFrameSlot(result, RSSYMmachinetype, RefVar(MAKEINT(info.fMachineType)));
			SetFrameSlot(result, RSSYMromstage, RefVar(MAKEINT(info.fROMStage)));
			SetFrameSlot(result, RSSYMromversion, RefVar(MAKEINT(info.fROMVersion)));
			SetFrameSlot(result, RSSYMramsize, RefVar(MAKEINT(info.fRAMSize)));
			SetFrameSlot(result, RSSYMscreenwidth, RefVar(MAKEINT(info.fScreenWidth)));
			SetFrameSlot(result, RSSYMscreenheight, RefVar(MAKEINT(info.fScreenHeight)));
			SetFrameSlot(result, RSSYMscreenresolutionx, RefVar(MAKEINT(info.fScreenResolution.h)));
			SetFrameSlot(result, RSSYMscreenresolutiony, RefVar(MAKEINT(info.fScreenResolution.v)));
			SetFrameSlot(result, RSSYMscreendepth, RefVar(MAKEINT(info.fScreenDepth)));
			SetFrameSlot(result, RSSYMpatchversion, RefVar(MAKEINT(info.fPatchVersion)));
			// the tablet resolutions are Fixed, rounded to the nearest whole
			SetFrameSlot(result, RSSYMtabletresolutionx, RefVar(MAKEINT((short) ((info.fTabletResX + 0x8000) >> 16))));
			SetFrameSlot(result, RSSYMtabletresolutiony, RefVar(MAKEINT((short) ((info.fTabletResY + 0x8000) >> 16))));
			// The ROM sets manufactureDate to 60 divided by a word of stack it
			// never wrote - the slot the version selector's answer would have
			// gone in, had this been that call - so the date is whatever was
			// left there, and on the machine it is whatever it is.  A host
			// cannot read an uninitialised word, so the slot is left as the
			// canonical frame has it and this comment stands in its place.
			if (info.fCpuType == 3)
				SetFrameSlot(result, RSSYMcputype, RSSYMstrongarm);
			else if (info.fCpuType == 2)
				SetFrameSlot(result, RSSYMcputype, RSSYMarm710a);
			else if (info.fCpuType == 1)
				SetFrameSlot(result, RSSYMcputype, RSSYMarm610a);
			SetFrameSlot(result, RSSYMcpuspeed, RefVar(MakeReal((double) info.fCpuSpeed / 65536.0)));
			// NOT YET RECONSTRUCTED: romVersionString (VersionString 0x00146cb8)
		}
		break;

	case kGestalt_RebootInfo:
		{
			TGestaltRebootInfo info;
			if (gestalt.Gestalt(kGestalt_RebootInfo, &info, sizeof(info)) != noErr)
				break;
			result = Clone(RefVar(Rcanonicalgestaltrebootinfo));
			SetFrameSlot(result, RSSYMrebootreason, RefVar(MAKEINT(info.fRebootReason)));
			SetFrameSlot(result, RSSYMrebootcount, RefVar(MAKEINT(info.fRebootCount)));
		}
		break;

	case kGestalt_PatchInfo:
		{
			TGestaltPatchInfo info;
			if (gestalt.Gestalt(kGestalt_PatchInfo, &info, sizeof(info)) != noErr)
				break;
			result = Clone(RefVar(Rcanonicalgestaltpatchinfo));
			SetFrameSlot(result, RSSYMftotalpatchpagecount, RefVar(MAKEINT(info.fTotalPatchPageCount)));
			RefVar patches(MakeArray(5));
			for (long i = 0; i < 5; i++)
			{
				RefVar patch(Clone(RefVar(Rcanonicalgestaltpatchinfoarrayelement)));
				SetFrameSlot(patch, RSSYMfpatchchecksum, RefVar(MAKEINT(info.fPatch[i].fPatchCheckSum)));
				SetFrameSlot(patch, RSSYMfpatchversion, RefVar(MAKEINT(info.fPatch[i].fPatchVersion)));
				SetFrameSlot(patch, RSSYMfpatchpagecount, RefVar(MAKEINT(info.fPatch[i].fPatchPageCount)));
				SetFrameSlot(patch, RSSYMfpatchfirstpageindex, RefVar(MAKEINT(info.fPatch[i].fPatchFirstPageIndex)));
				SetArraySlotRef(patches, i, patch);
			}
			SetFrameSlot(result, RSSYMfpatch, patches);
		}
		break;

	case kGestalt_SoundInfo:
	case kGestalt_PCMCIAInfo:
		break;					// (the ROM has a case for each and does nothing)

	case kGestalt_RexInfo:
		{
			TGestaltRexInfo info;
			if (gestalt.Gestalt(kGestalt_RexInfo, &info, sizeof(info)) != noErr)
				break;
			result = MakeArray(4);
			for (long i = 0; i < 4; i++)
			{
				RefVar rex(Clone(RefVar(Rcanonicalgestaltrexinfoarrayelement)));
				SetFrameSlot(rex, RSSYMsignaturea, RefVar(MAKEINT(info.fRex[i].signatureA)));
				SetFrameSlot(rex, RSSYMsignatureb, RefVar(MAKEINT(info.fRex[i].signatureB)));
				SetFrameSlot(rex, RSSYMchecksum, RefVar(MAKEINT(info.fRex[i].checksum)));
				SetFrameSlot(rex, RSSYMheaderversion, RefVar(MAKEINT(info.fRex[i].headerVersion)));
				SetFrameSlot(rex, RSSYMmanufacturer, RefVar(MAKEINT(info.fRex[i].manufacturer)));
				SetFrameSlot(rex, RSSYMversion, RefVar(MAKEINT(info.fRex[i].version)));
				SetFrameSlot(rex, RSSYMlength, RefVar(MAKEINT(info.fRex[i].length)));
				SetFrameSlot(rex, RSSYMid, RefVar(MAKEINT(info.fRex[i].id)));
				SetFrameSlot(rex, RSSYMstart, RefVar(MAKEINT(info.fRex[i].start)));
				SetFrameSlot(rex, RSSYMcount, RefVar(MAKEINT(info.fRex[i].count)));
				SetArraySlotRef(result, i, rex);
			}
		}
		break;
	}
	return result;
}


/*------------------------------------------------------------------------------
	T h e   b a t t e r i e s
------------------------------------------------------------------------------*/

// ROM 0x002037bc GetBatteryStatus__FlP16PowerPlantStatusUc
// The power manager asked for a battery's status: a 'newt/'pg&e RPC,
// command 4 for the reading it keeps and 5 for a fresh one, whose reply
// carries the 0x34-byte PowerPlantStatus.
//
// DEVIATION: the power manager is NOT YET RECONSTRUCTED and a host has
// no batteries, so the reading comes from hal/Power.h instead and the
// `raw` argument makes no difference.
static NewtonErr
GetBatteryStatus(long which, PowerPlantStatus* status, Boolean /*raw*/)
{
	if (status == nil)
		return kError_Bad_Parameters;
	return GetPowerPlantStatus(which, status);
}


// a Fixed reading as a real
static Ref
FixedReal(Fixed value)
{
	return MakeReal((double) value / 65536.0);
}


// ROM 0x00203db8 FBatteryStatus
// BatteryStatus(which): a frame describing that battery and the power
// coming in, or an empty frame when the power manager will not say.  A
// reading of -1 means the machine cannot tell, and its slot is left as
// the canonical frame has it - nil.
Ref
FBatteryStatus(RefArg /*rcvr*/, RefArg which)
{
	RefVar result(AllocateFrame());
	PowerPlantStatus status;
	if (GetBatteryStatus(RINT(which), &status, false) != noErr)
		return result;
	result = Clone(RefVar(Rcanonicalbatterystatus));
	switch (status.fBatteryType)
	{
	case kBatteryAlkaline:	SetFrameSlot(result, RSSYMbatterytype, RSSYMalkaline);	break;
	case kBatteryNiCd:		SetFrameSlot(result, RSSYMbatterytype, RSSYMnicd);		break;
	case kBatteryNiMH:		SetFrameSlot(result, RSSYMbatterytype, RSSYMnimh);		break;
	case kBatteryLithium:	SetFrameSlot(result, RSSYMbatterytype, RSSYMlithium);	break;
	case -1:				break;													// (not known)
	default:				SetFrameSlot(result, RSSYMbatterytype, RefVar(MAKEINT(status.fBatteryType)));	break;
	}
	if (status.fBatteryVoltage != -1)
		SetFrameSlot(result, RSSYMbatteryvoltage, RefVar(FixedReal(status.fBatteryVoltage)));
	if (status.fBatteryCapacity != -1)
		SetFrameSlot(result, RSSYMbatterycapacity, RefVar(MAKEINT(status.fBatteryCapacity)));
	if (status.fBatteryLow != -1)
		SetFrameSlot(result, RSSYMbatterylow, RefVar(MAKEINT(status.fBatteryLow)));
	if (status.fBatteryDead != -1)
		SetFrameSlot(result, RSSYMbatterydead, RefVar(MAKEINT(status.fBatteryDead)));
	if (status.fBatteryCurrent != -1)
		SetFrameSlot(result, RSSYMbatterycurrent, RefVar(FixedReal(status.fBatteryCurrent)));
	if (status.fChargeCurrent != -1)
		SetFrameSlot(result, RSSYMchargecurrent, RefVar(FixedReal(status.fChargeCurrent)));
	if (status.fACPower == 0)
		SetFrameSlot(result, RSSYMacpower, RSSYMno);
	if (status.fACPower == 1)
		SetFrameSlot(result, RSSYMacpower, RSSYMyes);
	if (status.fACVoltage != -1)
		SetFrameSlot(result, RSSYMacvoltage, RefVar(FixedReal(status.fACVoltage)));
	switch (status.fChargeState)
	{
	case kChargeDischarging:		SetFrameSlot(result, RSSYMchargestate, RSSYMdischarging);			break;
	case kChargeTrickle:			SetFrameSlot(result, RSSYMchargestate, RSSYMtricklecharging);		break;
	case kChargeFast:				SetFrameSlot(result, RSSYMchargestate, RSSYMfastcharging);			break;
	case kChargeFullyCharged:		SetFrameSlot(result, RSSYMchargestate, RSSYMfullycharged);			break;
	case kChargePreliminary:		SetFrameSlot(result, RSSYMchargestate, RSSYMpreliminarycharge);		break;
	case kChargeTrickleContinuous:	SetFrameSlot(result, RSSYMchargestate, RSSYMtricklechargecontinuous);	break;
	case kChargeDeepToast:			SetFrameSlot(result, RSSYMchargestate, RSSYMdeeptoast);				break;
	case -1:						break;
	default:						SetFrameSlot(result, RSSYMchargestate, RefVar(MAKEINT(status.fChargeState)));	break;
	}
	if (status.fChargeRate != -1)
		SetFrameSlot(result, RSSYMchargerate, RefVar(FixedReal(status.fChargeRate)));
	if (status.fAmbientTemp != -1)
		SetFrameSlot(result, RSSYMambienttemp, RefVar(FixedReal(status.fAmbientTemp)));
	if (status.fBatteryTemp != -1)
		SetFrameSlot(result, RSSYMbatterytemp, RefVar(FixedReal(status.fBatteryTemp)));
	return result;
}

// ROM 0x00203510 FBatteryCount
// BatteryCount(): how many batteries the machine has, 0 when the power
// manager will not say (a 'newt/'pg&e RPC of its own).
//
// DEVIATION: the power manager is NOT YET RECONSTRUCTED; the count comes
// from hal/Power.h with the rest of the readings.
static Ref
FBatteryCount(RefArg /*rcvr*/)
{
	return MAKEINT(GetPowerPlantCount());
}

// ROM 0x002018f8 SleepUntilNextWakeup__Fv
// The machine put to sleep until something wakes it: the backlight off,
// the power cycled (the hard keymap cleared if it came back), the
// contrast set from the preference again and the power event handed on.
// ==> what woke it, as one of the kWoke... reasons.
//
// NOT YET RECONSTRUCTED: FBackLight 0x00201a3c, which turns the
// backlight off first.  The sleep itself is hal/Power.h's CyclePower,
// and on a host it does not sleep at all.
long
SleepUntilNextWakeup(void)
{
	// NOT YET RECONSTRUCTED: FBackLight(nil, nil)
	ULong event = CyclePower();
	if (event != 0)
		ClearHardKeymap();		// a key held down through the sleep is not a keypress
	RefVar contrast(GetPreference(RSSYMlcdcontrast));
	FSetLCDContrast(RefVar(NILREF), contrast);
	return TranslatePowerEvent(event);
}


// ROM 0x002019a0 FMinimumBatteryCheck
// The machine held until there is enough power to go on: while it is not
// on the mains and the battery has fallen to the level called dead, it
// sleeps.  ==> true when it had to sleep at all, nil when it did not.
// A reading that cannot be taken is simply asked for again.
Ref
FMinimumBatteryCheck(RefArg /*rcvr*/)
{
	Boolean slept = false;
	for (;;)
	{
		PowerPlantStatus status;
		while (GetBatteryStatus(0, &status, false) != noErr)
			;
		if (status.fACPower == 1 || status.fBatteryDead < status.fBatteryCapacity)
			break;
		slept = true;
		SleepUntilNextWakeup();
	}
	return MAKEBOOLEAN(slept);
}

void
RegisterSystemNatives(void)
{
	RegisterNativeFunction("FGetSerialNumber", (void*) FGetSerialNumber, 0);
	RegisterNativeFunction("FGestalt", (void*) FGestalt, 1);
	RegisterNativeFunction("FBatteryStatus", (void*) FBatteryStatus, 1);
	RegisterNativeFunction("FMinimumBatteryCheck", (void*) FMinimumBatteryCheck, 0);
	RegisterNativeFunction("FBatteryCount", (void*) FBatteryCount, 0);
}
