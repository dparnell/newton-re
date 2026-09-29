/*
	File:		system/SystemNatives.cpp

	Contains:	The machine's own NewtonScript functions (SystemNatives.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SystemNatives.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NewtonGestalt.h"
#include "Marshalling.h"
#include <stdlib.h>
#include "ROMConstants.h"
#include "Frames.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "SkiaHeap.h"
#include "NewtonMemory.h"
#include "hal/System.h"
#include "hal/Power.h"
#include "ByteOrder.h"
#include "OSErrors.h"
#include "Screen.h"
#include "Keyboard.h"
#include "RootView.h"
#include "Protocols.h"
#include "VirtualMemory.h"
#include "UserTasks.h"
#include "ClassInfoRegistry.h"
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

// The buffer the last extended gestalt wanted, remembered so that the
// next one of the same size does not have to be asked twice.
// (ROM 0x0c104c54, in gLastWakeupTime's neighbourhood.)
static long	gExtendedGestaltSize = 0;

// DEVIATION: the selectors a script registered (RegisterGestalt): their
// blocks were marshalled in the MessagePad's byte order (frames/
// MarshalOut.cpp writes the device's bytes), where a block the system's C
// code registers is in the host's; Gestalt's array form reads each in the
// order it was written.  On the MessagePad the two are the same.
static const long	kScriptGestaltsMax = 32;
static ULong		gScriptGestalts[kScriptGestaltsMax];
static long			gScriptGestaltCount = 0;

static Boolean
IsScriptGestalt(ULong selector)
{
	for (long i = 0; i < gScriptGestaltCount; i++)
		if (gScriptGestalts[i] == selector)
			return true;
	return false;
}


// ROM 0x00201bfc ExtendedGestalt
// Gestalt([selector, template, encoding]) - the array form, which is how
// a script asks for one of the extended gestalts and gets the parameter
// block back as NewtonScript values rather than a frame the ROM knows how
// to build.  The template says what is in the block (Marshalling.h), and
// the answer is an array of its fields.
//
// The selectors in the base range (0x01000001 to 0x02000000) are refused
// here: those are the ones FGestalt answers with a frame of its own.
//
// The buffer is guessed at the size the last call wanted; the system
// answers with the size it really needs, and the call is made again when
// that was more.
static Ref
ExtendedGestalt(RefArg args)
{
	RefVar result;
	TUGestalt gestalt;
	if (!ISINT(GetArraySlotRef(args, 0)))
		return NILREF;
	long selector = RINT(GetArraySlotRef(args, 0));
	if (!IsArray(RefVar(GetArraySlotRef(args, 1))) || !ISINT(GetArraySlotRef(args, 2)))
		return NILREF;
	if (selector > 0x01000000 && selector <= 0x02000000)
		return NILREF;					// FGestalt's own range

	long size = gExtendedGestaltSize;
	char* block = (char*) malloc(size);
	if (block == nil)
		return NILREF;
	ULong wanted = (ULong) size;
	NewtonErr err = gestalt.Gestalt(selector, block, &wanted);
	if ((long) wanted > gExtendedGestaltSize)
	{
		// it wants more than was guessed: ask again with room for it
		gExtendedGestaltSize = (long) wanted;
		free(block);
		block = (char*) malloc((size_t) wanted);
		if (block != nil)
			err = gestalt.Gestalt(selector, block, &wanted);
	}
	if (block != nil)
	{
		if (err == noErr)
		{
			long failed = 0;
			if (IsScriptGestalt((ULong) selector))
				result = ConstructReturnValueFromDevice(block, RefVar(GetArraySlotRef(args, 1)),
														&failed, (int) RINT(GetArraySlotRef(args, 2)));
			else
				result = ConstructReturnValue(block, RefVar(GetArraySlotRef(args, 1)),
											  &failed, (int) RINT(GetArraySlotRef(args, 2)));
		}
		free(block);
	}
	return err == noErr ? (Ref) result : NILREF;
}


// ROM 0x002028e4 UpdateGestalt
// What RegisterGestalt and ReplaceGestalt share: a selector of the
// script's own, its parameter block made out of values and the template
// saying what they are (Marshalling.h, as Gestalt's array form reads them
// back), registered - or put in place of what is there - with the gestalt
// server.  ==> true when it was, nil when the arguments were not an integer
// selector, two arrays and an integer encoding, or the block could not be
// made or registered.
//
// The size of the block is remembered in gExtendedGestaltSize when it is
// the largest yet, so that Gestalt's first guess at a buffer for it is big
// enough.
//
// ROM BUG, kept: the block MarshalArguments allocates is never freed - the
// gestalt server copies it, and the ROM leaves its own copy behind on
// every call.
static Ref
UpdateGestalt(RefArg selector, RefArg args, RefArg types, RefArg encoding, Boolean replace)
{
	TUGestalt gestalt;
	long err = 1;
	if (ISINT(selector) && ISINT(encoding) && IsArray(args) && IsArray(types))
	{
		ULong size;
		err = MarshalArgumentSize(args, types, &size, (int) RINT(encoding));
		if (err == noErr)
		{
			if ((long) size > gExtendedGestaltSize)
				gExtendedGestaltSize = (long) size;
			void* block;
			err = MarshalArguments(args, types, &block, (int) RINT(encoding));
			if (err == noErr)
			{
				if (!replace)
					err = gestalt.RegisterGestalt((GestaltSelector) RINT(selector), block, size);
				else
					err = gestalt.ReplaceGestalt((GestaltSelector) RINT(selector), block, size);
				if (err == noErr && !IsScriptGestalt((ULong) RINT(selector)) && gScriptGestaltCount < kScriptGestaltsMax)
					gScriptGestalts[gScriptGestaltCount++] = (ULong) RINT(selector);		// (host: its byte order, above)
			}
		}
	}
	return err == noErr ? TRUEREF : NILREF;
}


// ROM 0x00202a74 FRegisterGestalt
// RegisterGestalt(selector, values, template, encoding): a new gestalt.
static Ref
FRegisterGestalt(RefArg /*rcvr*/, RefArg selector, RefArg args, RefArg types, RefArg encoding)
{
	return UpdateGestalt(selector, args, types, encoding, false);
}


// ROM 0x00202aac FReplaceGestalt
// ReplaceGestalt(selector, values, template, encoding): one already there,
// given new values.
static Ref
FReplaceGestalt(RefArg /*rcvr*/, RefArg selector, RefArg args, RefArg types, RefArg encoding)
{
	return UpdateGestalt(selector, args, types, encoding, true);
}


// ROM 0x0030d0bc FBootSucceeded
// BootSucceeded(ok): what an automated boot test watches for - a file
// "bootResults" made when the boot went well, "bootFailure" when ok is nil,
// through the C library, which on a Newton with the debugger connected
// writes it on the desktop.  Then the REP's output goes back to stdout if
// the boot had it going through a translator of its own (gBootOut).
//
// The host has no gBootOut (frames/Printer.cpp: the boot's REP output is
// the host's own), so that half does nothing; the file is made in the
// working directory, which is the host's desktop.
static Ref
FBootSucceeded(RefArg /*rcvr*/, RefArg ok)
{
	FILE* f = fopen(ISNIL(ok) ? "bootFailure" : "bootResults", "w");
	if (f != nil)
		fclose(f);
	return NILREF;
}


// ROM 0x00201e0c FGestalt
// Gestalt(selector): a frame describing that part of the machine, or nil.
// The selectors are kGestalt_Base + 1 up; an array asks the extended
// gestalts instead (ExtendedGestalt, above).
// kGestalt_SoundInfo and kGestalt_PCMCIAInfo answer nil: the ROM has a case
// for each and neither does anything.
Ref
FGestalt(RefArg /*rcvr*/, RefArg selector)
{
	RefVar result;
	TUGestalt gestalt;
	if (!ISINT(selector))
		return ExtendedGestalt(selector);		// the array form
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


// ROM 0x002035d4 SetBatteryType__FlT1
// The power manager told what cells a battery holds: command 7 of the
// same RPC, whose reply is the error the manager made of it.
//
// DEVIATION: as above - hal/Power.h is asked instead.
static long
SetBatteryType(long which, long type)
{
	return SetPowerPlantBatteryType(which, type);
}


// a Fixed reading as a real
static Ref
FixedReal(Fixed value)
{
	return MakeReal((double) value / 65536.0);
}


// ROM 0x002038a0 BatteryStatusHelper__FlUc
// A frame describing that battery and the power coming in, or an empty
// frame when the power manager will not say.  A reading of -1 means the
// machine cannot tell, and its slot is left as the canonical frame has
// it - nil.  `raw` is what the two natives below differ by: it asks the
// power manager to take the reading again rather than answer the one it
// last took.
static Ref
BatteryStatusHelper(long which, Boolean raw)
{
	RefVar result(AllocateFrame());
	PowerPlantStatus status;
	if (GetBatteryStatus(which, &status, raw) != noErr)
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


// ROM 0x00203db8 FBatteryStatus
// BatteryStatus(which): the reading the power manager already has.
Ref
FBatteryStatus(RefArg /*rcvr*/, RefArg which)
{
	return BatteryStatusHelper(RINT(which), false);
}


// ROM 0x002017d4 FBatteryRawStatus
// BatteryRawStatus(which): the same frame, from a reading taken now.
static Ref
FBatteryRawStatus(RefArg /*rcvr*/, RefArg which)
{
	return BatteryStatusHelper(RINT(which), true);
}


// ROM 0x0c104c48 gLastBatteryLevel
// The capacity the main battery last read, so that a reading which
// cannot be taken answers the one before it rather than nothing.
static long	gLastBatteryLevel = 0;


// ROM 0x00201804 FBatteryLevel__FRC6RefVarT1
// BatteryLevel(what): one number out of the battery status, which is
// what a battery gauge watches rather than building a whole frame every
// time.  `what` picks it:
//
//	0	the main battery's capacity (and the level it last read is the
//		answer when the reading fails)
//	1	the second battery's capacity
//	2	the temperature
//	3	the main battery's capacity, without that fallback
//
// On the mains the main battery reads 100 whatever the cells say, which
// is what keeps the gauge full while the machine is plugged in.
//
// The temperature is answered as the Fixed the power manager sends, made
// an integer without being scaled - so it is degrees times 65536, not
// degrees.  (The ROM does this; its own scripts never ask for it.)
static Ref
FBatteryLevel(RefArg /*rcvr*/, RefArg what)
{
	RefVar result;
	Boolean wantCapacity = true;
	long which = RINT(what);
	if (which == 0)
		result = MAKEINT(gLastBatteryLevel);
	else if (which == 2)
	{
		wantCapacity = false;
		which = 0;
	}
	else if (which == 3)
		which = 0;

	PowerPlantStatus status;
	if (GetBatteryStatus(which, &status, false) != noErr)
		return result;
	if (which == 0)
		gLastBatteryLevel = status.fBatteryCapacity;

	long value;
	if (wantCapacity)
	{
		if (status.fACPower == 1 && which == 0)
			return MAKEINT(100);
		value = status.fBatteryCapacity;
	}
	else
	{
		value = status.fAmbientTemp;
		if (value == -1)
		{
			value = status.fBatteryTemp;
			if (value == -1)
				return result;
		}
	}
	return MAKEINT(value);
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


// ROM 0x00203684 FSetBatteryType
// SetBatteryType(which, type): what cells the machine is holding, as one
// of the symbols the status frame answers or as the number behind it;
// nil says it is not known.  ==> true when the power manager took it.
// A type which is neither a known symbol nor an integer is refused with
// nil rather than an error.
static Ref
FSetBatteryType(RefArg /*rcvr*/, RefArg which, RefArg type)
{
	long kind;
	if (IsSymbol(type))
	{
		if (EQRef(type, RSSYMalkaline))			kind = kBatteryAlkaline;
		else if (EQRef(type, RSSYMnicd))		kind = kBatteryNiCd;
		else if (EQRef(type, RSSYMnimh))		kind = kBatteryNiMH;
		else if (EQRef(type, RSSYMlithium))		kind = kBatteryLithium;
		else									return NILREF;
	}
	else if (ISNIL(type))
		kind = -1;
	else if (ISINT(type))
		kind = RINT(type);
	else
		return NILREF;
	return MAKEBOOLEAN(SetBatteryType(RINT(which), kind) == noErr);
}


/*------------------------------------------------------------------------------
	T h e   b a c k l i g h t
------------------------------------------------------------------------------*/

// ROM 0x00201a0c FBackLightStatus
// BackLightStatus(): true when the backlight is on.
static Ref
FBackLightStatus(RefArg /*rcvr*/)
{
	long on = 0;
	GetGrafInfo(kGrafInfoBacklight, &on);
	return MAKEBOOLEAN(on == 1);
}


// ROM 0x00201a3c FBackLight
// BackLight(on): the backlight switched, answering what it was before.
// Switching it on tickles the root view first (EventPause), so that the
// machine does not count as having been left alone the moment the light
// comes on and turn itself off again.
Ref
FBackLight(RefArg /*rcvr*/, RefArg on)
{
	long was = 0;
	GetGrafInfo(kGrafInfoBacklight, &was);
	if (ISNIL(on))
		SetGrafInfo(kGrafInfoBacklight, 0);
	else
	{
		NSSendRootMessage(RefVar(Intern((char*) "eventPause")), RefVar(TRUEREF));
		SetGrafInfo(kGrafInfoBacklight, 1);
	}
	return MAKEBOOLEAN(was == 1);
}


// ROM 0x002018f8 SleepUntilNextWakeup__Fv
// The machine put to sleep until something wakes it: the backlight off,
// the power cycled (the hard keymap cleared if it came back), the
// contrast set from the preference again and the power event handed on.
// ==> what woke it, as one of the kWoke... reasons.
//
// The sleep itself is hal/Power.h's CyclePower, and on a host it does
// not sleep at all.
long
SleepUntilNextWakeup(void)
{
	FBackLight(RefVar(NILREF), RefVar(NILREF));
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

/* -------------------------------------------------------------------------------
	What the memory looks like

	Three heaps answer for the machine's memory: the pointer heap and the
	handle heap the memory manager keeps (memory/SkiaHeap.h), and the
	frames heap the object system allocates in (frames/ObjectHeap.h).
	GetHeapStats reports all three, and what the system has left over
	besides.
------------------------------------------------------------------------------- */

// ROM 0x00202f08 GetActualHeapInfo__FPvPPvT2PlT4
// A heap walked a block at a time: where its first block starts, where
// its last one ends, how many blocks there are and how many bytes of
// them are free.
//
// A walk is only good while the heap holds still.  NextHeapBlock answers
// kMM_HeapSeedFailure when another task has allocated since the seed was
// taken, and then the whole walk starts again from nothing - which is
// why the counters are cleared inside the loop rather than before it.
static void
GetActualHeapInfo(Heap heap, void** start, void** end, long* blockCount, long* freeBytes)
{
	for (;;)
	{
		*blockCount = 0;
		*freeBytes = 0;
		long seed = HeapSeed(heap);
		Boolean first = true;
		void* block = nil;
		Size size = 0;
		TObjectId owner = 0;
		int type;
		while ((type = NextHeapBlock(heap, seed, block, &block, nil, nil, nil, &size, &owner)) != kMM_HeapSeedFailure)
		{
			if (type == kMM_HeapEndBlock)
				return;
			if (first)
			{
				*start = block;
				*end = block;
				first = false;
			}
			(*blockCount)++;
			*end = (char*) *end + size;
			if (type == kMM_HeapFreeBlock)
				*freeBytes += size;
		}
	}
}


// An address as the ROM hands one to a script: the low two bits masked
// off make it an integer Ref of the address divided by four, which is
// near enough for a number to print.  (A host pointer is wider than the
// Newton's, but a Ref here is pointer-sized, so it still fits.)
static Ref
AddressRef(const void* address)
{
	return (Ref) ((ULong) address & ~(ULong) 3);
}


// ROM 0x00202ff4 FGetHeapStats
// GetHeapStats(options): a frame of where each heap is, how big it is and
// how much of it is free.  `options` may say `garbageCollectFrames`, when
// the frames heap is collected before it is measured, and
// `includeSystemReleasable`, when the memory the system could give back
// counts as free.
//
// NOT YET RECONSTRUCTED: `includeSystemReleasable` (GetSystemReleasable
// 0x0014312c and ROMDomainManagerFreePageCount 0x0027f594), so the
// system's free space is what it has this moment, not what it could
// release.
static Ref
FGetHeapStats(RefArg /*rcvr*/, RefArg options)
{
	RefVar collect;
	RefVar releasable;
	if (NOTNIL(options))
	{
		collect = GetFrameSlotRef(options, RefVar(Intern((char*) "garbageCollectFrames")));
		releasable = GetFrameSlotRef(options, RefVar(Intern((char*) "includeSystemReleasable")));
	}

	void* start = nil;
	void* end = nil;
	long blockCount = 0;
	long freeBytes = 0;
	RefVar stats(AllocateFrame());

	GetActualHeapInfo(GetFixedHeap(GetHeap()), &start, &end, &blockCount, &freeBytes);
	SetFrameSlot(stats, RefVar(Intern((char*) "ptrHeapStart")), RefVar(AddressRef(start)));
	SetFrameSlot(stats, RefVar(Intern((char*) "ptrHeapSize")), RefVar(MAKEINT((char*) end - (char*) start)));
	SetFrameSlot(stats, RefVar(Intern((char*) "ptrFreeSize")), RefVar(MAKEINT(freeBytes)));

	GetActualHeapInfo(GetRelocHeap(GetHeap()), &start, &end, &blockCount, &freeBytes);
	SetFrameSlot(stats, RefVar(Intern((char*) "handleHeapStart")), RefVar(AddressRef(start)));
	SetFrameSlot(stats, RefVar(Intern((char*) "handleHeapSize")), RefVar(MAKEINT((char*) end - (char*) start)));
	SetFrameSlot(stats, RefVar(Intern((char*) "handleFreeSize")), RefVar(MAKEINT(freeBytes)));

	if (NOTNIL(collect))
		gHeap->GC();
	Ptr heapStart, heapLimit;
	HeapBounds(&heapStart, &heapLimit);
	ULong framesFree = 0;
	ULong largestFree = 0;
	gHeap->Statistics(&framesFree, &largestFree);
	SetFrameSlot(stats, RefVar(Intern((char*) "framesHeapStart")), RefVar(AddressRef(heapStart)));
	SetFrameSlot(stats, RefVar(Intern((char*) "framesHeapSize")), RefVar(MAKEINT(heapLimit - heapStart)));
	SetFrameSlot(stats, RefVar(Intern((char*) "framesFreeSize")), RefVar(MAKEINT((long) framesFree)));

	Size systemFree = TotalSystemFree();
	// (NOT YET: + GetSystemReleasable + ROMDomainManagerFreePageCount * 4096
	//  when `includeSystemReleasable` is asked for)
	SetFrameSlot(stats, RefVar(Intern((char*) "systemFreeSize")), RefVar(MAKEINT(systemFree)));
	return stats;
}


/*------------------------------------------------------------------------------
	T h e   p r o t o c o l   r e g i s t r y ,   f r o m   a   s c r i p t

	Protocols are the ROM's interface/implementation mechanism
	(`protocols/Protocols.h`); every implementation registers its class
	info, and these are how a script looks through that registry, makes
	an instance by name and destroys one again.  A class info and an
	instance both come back as a frame holding the pointer, cloned from
	`classInfoPrototype` and `protocolInstancePrototype`.

	DEVIATION: the ROM keeps the pointer as `(ULong) p & ~3`, which on a
	Newton is an integer Ref of the address divided by four and is read
	back by multiplying it again.  A host pointer does not fit in a
	thirty-bit integer, so AddressToRef/RefToAddress are used instead -
	the same pair every other C object a script holds goes through.
------------------------------------------------------------------------------*/

// ROM 0x00194aec WrapClassInfo__FPC10TClassInfo
// A class info as the frame a script holds it in; nil for none.
static Ref
WrapClassInfo(const TClassInfo* info)
{
	if (info == nil)
		return NILREF;
	RefVar wrapper(Clone(RefVar(Rclassinfoprototype)));
	SetFrameSlot(wrapper, RSSYM_classinfo, RefVar(AddressToRef((void*) info)));
	return wrapper;
}


// ROM 0x00194b5c WrapProtocolInstance__FP9TProtocol
// ... and an instance, whose `_parent` is its class info's frame, so
// that a script reading the instance can see what it is.
static Ref
WrapProtocolInstance(TProtocol* instance)
{
	if (instance == nil)
		return NILREF;
	RefVar wrapper(Clone(RefVar(Rprotocolinstanceprototype)));
	RefVar info(WrapClassInfo(instance->ClassInfo()));
	SetFrameSlot(wrapper, RSSYM_instance, RefVar(AddressToRef(instance)));
	SetFrameSlot(wrapper, RSSYM_parent, info);
	return wrapper;
}


// A name a script gives as the C string the registry wants: the object
// is locked while the pointer is held, because the frames heap moves.
class ProtocolName
{
public:
	ProtocolName(RefArg name)
	{
		if (NOTNIL(name))
		{
			fString = ASCIIString(name);
			LockRef(fString);
		}
	}
	~ProtocolName()
	{
		if (NOTNIL(fString))
			UnlockRef(fString);
	}
	const char* Get() const	{ return ISNIL(fString) ? nil : (const char*) BinaryData(fString); }

private:
	RefVar	fString;
};


// ROM 0x00194e54 FClassInfoByName
// ClassInfoByName(interface, implementation, capability): the class info
// the registry would satisfy that request with, as a frame; nil when
// nothing does.  Any of the three may be nil, which means "any".
static Ref
FClassInfoByName(RefArg /*rcvr*/, RefArg interface, RefArg implementation, RefArg capability)
{
	ProtocolName intf(interface);
	ProtocolName impl(implementation);
	ProtocolName cap(capability);
	return WrapClassInfo(gProtocolRegistry->Satisfy(intf.Get(), impl.Get(), cap.Get()));
}


// ROM 0x00195144 FClassInfoRegistrySeed
// ClassInfoRegistrySeed(): the registry's seed, which changes whenever
// something is registered or taken away - a walk that started before
// that is no longer good.  A seed too large to be an integer Ref reads
// as 0, which is the ROM's way of saying so.
static Ref
FClassInfoRegistrySeed(RefArg /*rcvr*/)
{
	long seed = gProtocolRegistry->Seed();
	if (seed != (Ref) MAKEINT(seed) >> 2)
		seed = 0;
	return MAKEINT(seed);
}


// ROM 0x00195174 FNextClassInfo
// ClassInfoRegistryNext(info, seed): the class info after that one, or
// the first when it is nil; nil at the end.  The seed is what
// ClassInfoRegistrySeed answered when the walk began.
static Ref
FNextClassInfo(RefArg /*rcvr*/, RefArg info, RefArg seed)
{
	const TClassInfo* next;
	if (ISNIL(info))
		next = gProtocolRegistry->First(0, nil);
	else
	{
		const TClassInfo* from =
			(const TClassInfo*) RefToAddress(GetVariable(info, RSSYM_classinfo, nil, 0));
		long since = ISNIL(seed) ? 0 : RINT(seed);
		next = gProtocolRegistry->Next(since, from, nil);
	}
	return WrapClassInfo(next);
}


// ROM 0x00194fd4 FNewByName
// NewByName(interface, implementation, capability): an instance of that
// protocol, as a frame; nil when the registry has nothing to make one
// from.
static Ref
FNewByName(RefArg /*rcvr*/, RefArg interface, RefArg implementation, RefArg capability)
{
	ProtocolName intf(interface);
	ProtocolName impl(implementation);
	ProtocolName cap(capability);
	return WrapProtocolInstance(NewByName(intf.Get(), impl.Get(), cap.Get()));
}


// ROM 0x00194c80 FDestroyProtocol
// instance:Destroy() - the instance destroyed through its own class
// info, and the frame's `_instance` slot emptied so that it cannot be
// used again.
static Ref
FDestroyProtocol(RefArg rcvr)
{
	RefVar held(GetVariable(rcvr, RSSYM_instance, nil, 0));
	if (NOTNIL(held))
	{
		TProtocol* instance = (TProtocol*) RefToAddress(held);
		if (instance != nil)
		{
			instance->ClassInfo()->Destroy(instance);
			SetFrameSlot(rcvr, RSSYM_instance, RefVar(NILREF));
		}
	}
	return NILREF;
}

/*------------------------------------------------------------------------------
	W h a t   t h e   m a c h i n e   h a s   b e e n   d o i n g

	Four counters live across a reboot (`SGlobalsThatLiveAcrossReboot`):
	how long the processor, the screen, the serial port and the sound
	have been on since the last cold boot.  The kernel only keeps them
	when `gCollectCPUStats` is set, which is what EnablePowerStats turns
	on.

	DEVIATION: nothing on the host counts them, so they stay at nought;
	the flag and the reset are kept because a script can set and read
	them, and because a port that does count them has somewhere to put
	the numbers.
------------------------------------------------------------------------------*/

// ROM 0x00202d8c FEnablePowerStats
// EnablePowerStats(on): whether the counters are kept.  ==> what it was
// set to.
static Ref
FEnablePowerStats(RefArg /*rcvr*/, RefArg on)
{
	gCollectCPUStats = NOTNIL(on) ? 1 : 0;
	return MAKEBOOLEAN(gCollectCPUStats != 0);
}


// ROM 0x00202db8 FGetPowerStats
// GetPowerStats(): the four counters and the time of the last cold
// boot, as a canonicalPowerStats frame.  The cold-boot time is the
// real-time clock's reading moved to the epoch a script counts in; the
// constant is the ROM's own.
static Ref
FGetPowerStats(RefArg /*rcvr*/)
{
	SGlobalsThatLiveAcrossReboot& g = gGlobalsThatLiveAcrossReboot;
	RefVar stats(Clone(RefVar(Rcanonicalpowerstats)));
	SetFrameSlot(stats, RSSYMtimeatcoldboot, RefVar(MAKEINT((long) (g.fTimeAtColdBoot + 0x58939444UL))));
	SetFrameSlot(stats, RSSYMprocessorofftime, RefVar(MAKEINT((long) g.fProcessorOnTime)));
	SetFrameSlot(stats, RSSYMscreenontime, RefVar(MAKEINT((long) g.fScreenOnTime)));
	SetFrameSlot(stats, RSSYMserialontime, RefVar(MAKEINT((long) g.fSerialOnTime)));
	SetFrameSlot(stats, RSSYMsoundontime, RefVar(MAKEINT((long) g.fSoundOnTime)));
	return stats;
}


// ROM 0x00202ee4 FResetPowerStats
// ResetPowerStats(): the four counters back to nought.  The cold-boot
// time is left as it was - it is not a counter.
static Ref
FResetPowerStats(RefArg /*rcvr*/)
{
	SGlobalsThatLiveAcrossReboot& g = gGlobalsThatLiveAcrossReboot;
	g.fProcessorOnTime = 0;
	g.fScreenOnTime = 0;
	g.fSerialOnTime = 0;
	g.fSoundOnTime = 0;
	return NILREF;
}


// ROM 0x00146af8 FReboot
// ReBoot(): the machine restarted, with no error and no reboot type,
// and not waiting for anything to finish.
static Ref
FReboot(RefArg /*rcvr*/)
{
	Reboot(noErr, 0, false);
	return NILREF;
}


// The class info a script holds, as a pointer out of its frame.
static const TClassInfo*
HeldClassInfo(RefArg wrapper)
{
	return (const TClassInfo*) RefToAddress(GetVariable(wrapper, RSSYM_classinfo, nil, 0));
}


// ROM 0x00195290 FProtocolInterfaceName
// info:InterfaceName() - the protocol this implements.
static Ref
FProtocolInterfaceName(RefArg rcvr)
{
	return MakeString(HeldClassInfo(rcvr)->InterfaceName());
}


// ROM 0x001952d0 FProtocolImplementationName
// info:ImplementationName() - what implements it.
static Ref
FProtocolImplementationName(RefArg rcvr)
{
	return MakeString(HeldClassInfo(rcvr)->ImplementationName());
}


// ROM 0x00195310 FProtocolSignature
// info:signature() - its capability list, as one string.
static Ref
FProtocolSignature(RefArg rcvr)
{
	return MakeString(HeldClassInfo(rcvr)->Signature());
}


// ROM 0x00194c00 FProtocolVersion
// info:version() - the implementation's version.
static Ref
FProtocolVersion(RefArg rcvr)
{
	return MAKEINT((long) HeldClassInfo(rcvr)->Version());
}


// ROM 0x00194d24 FGetCapability
// info:GetCapability(name) - what that capability of the implementation
// is set to, or nil when it has not got it.
static Ref
FGetCapability(RefArg rcvr, RefArg name)
{
	RefVar ascii(ASCIIString(name));
	LockRef(ascii);
	const char* value = HeldClassInfo(rcvr)->GetCapability(BinaryData(ascii));
	UnlockRef(ascii);
	return value != nil ? MakeString(value) : NILREF;
}


// ROM 0x00194dc4 FHasCapability
// info:HasCapability(name) - whether it has it at all.
static Ref
FHasCapability(RefArg rcvr, RefArg name)
{
	RefVar ascii(ASCIIString(name));
	LockRef(ascii);
	const char* value = HeldClassInfo(rcvr)->GetCapability(BinaryData(ascii));
	UnlockRef(ascii);
	return MAKEBOOLEAN(value != nil);
}


// ROM 0x000ce480 PrimCallProtocolFromFrames__FRC6RefVarN41
// A protocol instance's method called from NewtonScript: the arguments
// marshalled by their types (Marshalling.h; a value that will not marshal
// throws evt.ex.marshal.type), the method at dispatch slot selector + 1
// called with them - the instance in the first register, the arguments'
// words after it (CallProtocolMethodWithArgsOnStack 0x003ae508) - and its
// answer, a word, unmarshalled by the result type.
//
// NOT YET, and a DEVIATION where it can be done: the host's protocol
// methods are C++ virtual functions (protocols/Protocols.h), which cannot
// be called by their dispatch slot's number.  A monitor's can - its entry
// takes a selector, dispatch slot n being monitor selector n - 2 - so a
// monitor instance's method is called with up to four argument words, as
// its glue would pass them; any other instance throws
// kError_Call_Not_Implemented.
static Ref
PrimCallProtocolFromFrames(RefArg args, RefArg argTypes, TProtocol* instance, long selector, RefArg resultType)
{
	void* block = nil;
	long err = MarshalArguments(args, argTypes, &block, 2);
	if (err != noErr)
		Throw((ExceptionName) "evt.ex.marshal.type", (void*) (intptr_t) err, nil);
	ULong size = 0;
	MarshalArgumentSize(args, argTypes, &size, 2);
	ProtocolMonitorArgs callArgs;
	memset(&callArgs, 0, sizeof(callArgs));
	// (the block is the device's words: MarshalOut.cpp)
	for (ULong i = 0; i < 4 && (i + 1) * 4 <= size; i++)
		callArgs.fArg[i] = GetBigEndianWord((const UByte*) block + i * 4);
	free(block);
	if (instance == nil || instance->GetMonitorId() == 0 || selector + 1 < 2)
		Throw(exFrames, (void*) (intptr_t) kError_Call_Not_Implemented, nil);
	Long result = instance->MonitorCall((ULong) (selector + 1 - 2), &callArgs);
	UByte word[4];
	PutBigEndianWord(word, (ULong32) result);
	void* p = word;
	long failed = 0;
	return UnmarshalValue(&p, resultType, 1, &failed, 2);
}


// ROM 0x00195228 FDispatchProtocol
// instance:Dispatch(args, argTypes, selector, resultType) - one of the
// instance's methods called by its number (PrimCallProtocolFromFrames).
static Ref
FDispatchProtocol(RefArg rcvr, RefArg args, RefArg argTypes, RefArg selector, RefArg resultType)
{
	RefVar held(GetVariable(rcvr, RSSYM_instance, nil, 0));
	TProtocol* instance = ISNIL(held) ? nil : (TProtocol*) RefToAddress(held);
	return PrimCallProtocolFromFrames(args, argTypes, instance, RINT(selector), resultType);
}


// ROM 0x00194c40 FNewProtocol
// info:New() - an instance of that implementation, as the frame a
// script holds it in; nil when it cannot be made.
static Ref
FNewProtocol(RefArg rcvr)
{
	TProtocol* instance = HeldClassInfo(rcvr)->New();
	return WrapProtocolInstance(instance);
}


void
RegisterSystemNatives(void)
{
	RegisterNativeFunction("FGetSerialNumber", (void*) FGetSerialNumber, 0);
	RegisterNativeFunction("FGestalt", (void*) FGestalt, 1);
	RegisterNativeFunction("FDispatchProtocol", (void*) FDispatchProtocol, 4);
	RegisterNativeFunction("FRegisterGestalt", (void*) FRegisterGestalt, 4);
	RegisterNativeFunction("FReplaceGestalt", (void*) FReplaceGestalt, 4);
	RegisterNativeFunction("FBootSucceeded", (void*) FBootSucceeded, 1);
	RegisterNativeFunction("FBatteryStatus", (void*) FBatteryStatus, 1);
	RegisterNativeFunction("FBatteryRawStatus", (void*) FBatteryRawStatus, 1);
	RegisterNativeFunction("FBatteryLevel__FRC6RefVarT1", (void*) FBatteryLevel, 1);
	RegisterNativeFunction("FMinimumBatteryCheck", (void*) FMinimumBatteryCheck, 0);
	RegisterNativeFunction("FBatteryCount", (void*) FBatteryCount, 0);
	RegisterNativeFunction("FSetBatteryType", (void*) FSetBatteryType, 2);
	RegisterNativeFunction("FBackLightStatus", (void*) FBackLightStatus, 0);
	RegisterNativeFunction("FBackLight", (void*) FBackLight, 1);
	RegisterNativeFunction("FGetHeapStats", (void*) FGetHeapStats, 1);
	RegisterNativeFunction("FEnablePowerStats", (void*) FEnablePowerStats, 1);
	RegisterNativeFunction("FGetPowerStats", (void*) FGetPowerStats, 0);
	RegisterNativeFunction("FResetPowerStats", (void*) FResetPowerStats, 0);
	RegisterNativeFunction("FReboot", (void*) FReboot, 0);
	RegisterNativeFunction("FClassInfoByName", (void*) FClassInfoByName, 3);
	RegisterNativeFunction("FClassInfoRegistrySeed", (void*) FClassInfoRegistrySeed, 0);
	RegisterNativeFunction("FNextClassInfo", (void*) FNextClassInfo, 2);
	RegisterNativeFunction("FNewByName", (void*) FNewByName, 3);
	RegisterNativeFunction("FDestroyProtocol", (void*) FDestroyProtocol, 0);
	RegisterNativeFunction("FProtocolInterfaceName", (void*) FProtocolInterfaceName, 0);
	RegisterNativeFunction("FProtocolImplementationName", (void*) FProtocolImplementationName, 0);
	RegisterNativeFunction("FProtocolSignature", (void*) FProtocolSignature, 0);
	RegisterNativeFunction("FProtocolVersion", (void*) FProtocolVersion, 0);
	RegisterNativeFunction("FGetCapability", (void*) FGetCapability, 1);
	RegisterNativeFunction("FHasCapability", (void*) FHasCapability, 1);
	RegisterNativeFunction("FNewProtocol", (void*) FNewProtocol, 0);
}
