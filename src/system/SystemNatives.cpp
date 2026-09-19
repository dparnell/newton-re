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
#include "OSErrors.h"


// ROM 0x0020171c FGetSerialNumber
// The machine's serial number as an eight-byte binary of class
// 'serialNumber; nil when the serial number ROM cannot be read
// (hal/System.h's GetSystemSerialNumber - the host has a number of its
// own, see hal/host/System.cpp).
Ref
FGetSerialNumber(RefArg /*rcvr*/)
{
	RefVar binary(AllocateBinary(RefVar(RSSYMserialnumber), 2 * sizeof(ULong)));
	ULong* serialNumber = (ULong*) BinaryData(binary);
	if (GetSystemSerialNumber(serialNumber) != noErr)
		return NILREF;
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


void
RegisterSystemNatives(void)
{
	RegisterNativeFunction("FGetSerialNumber", (void*) FGetSerialNumber, 0);
	RegisterNativeFunction("FGestalt", (void*) FGestalt, 1);
}
