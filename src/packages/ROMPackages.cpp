/*
	File:		packages/ROMPackages.cpp

	Contains:	The ROM extension's packages found and installed
				(ROMPackages.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ROMPackages.h"
#include "ROMExtension.h"
#include "PackageIterator.h"
#include "FramesPart.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Frames.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "ByteOrder.h"
#include "ROMExtension.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <stdio.h>
#include <string.h>


// ROM 0x0011ef10 GetRExConfigEntry
// The ROM does a generic system call (0x3b) and the kernel answers out of
// the extension headers the loader found at boot.
//
// DEVIATION: the host does not boot from the ROM, so the kernel has no
// such table (os600/kernel/GenericSWI.cpp answers
// kError_Call_Not_Implemented for that selector).  The extensions are
// found in the ROM's own bytes instead, which the host keeps for exactly
// this (frames/ROMImport.h's ROMImageBase); a RExBlock header is looked
// for on word boundaries, as the signature's comment in
// ddk/ROMExtension.h says it is meant to be.
VAddr
GetRExConfigEntry(ULong rexId, ULong tag, ULong* size)
{
	if (size != nil)
		*size = 0;
	ULong imageSize = 0;
	const unsigned char* rom = (const unsigned char*) ROMImageBase(&imageSize);
	if (rom == nil)
		return 0;
	for (ULong at = 0; at + sizeof(RExHeader) <= imageSize; at += kARMWord)
	{
		if (GetBigEndianWord(rom + at) != kRExSignatureA || GetBigEndianWord(rom + at + 4) != kRExSignatureB)
			continue;
		if (GetBigEndianWord(rom + at + 0x1c) != rexId)		// the extension's id
			continue;
		ULong count = GetBigEndianWord(rom + at + 0x24);
		ULong table = at + 0x28;
		for (ULong i = 0; i < count; i++)
		{
			ULong entry = table + i * 3 * kARMWord;
			if (entry + 3 * kARMWord > imageSize)
				break;
			if (GetBigEndianWord(rom + entry) != tag)
				continue;
			if (size != nil)
				*size = GetBigEndianWord(rom + entry + 2 * kARMWord);
			// the offset is from the extension's start
			return (VAddr) (at + GetBigEndianWord(rom + entry + kARMWord));
		}
	}
	return 0;
}


// ROM 0x0011eeec GetPackageList
VAddr
GetPackageList(ULong rexId)
{
	ULong size = 0;
	return GetRExConfigEntry(rexId, 'pkgl', &size);
}


// ROM 0x000cb68c InstallPart__FRC6RefVarT1RC6PartId10SourceTypeP8PartInfoT1
// The part described to the rest of the system: a clone of
// canonicalFramePartInstallInfo filled in and handed to the NewtonScript
// InstallPart, which is what actually registers the application, puts it
// in the Extras drawer and runs its InstallScript.  Its answer is the
// cookie that removes the part again.
//
// The package style is 'HighROM for a package built into the ROM, which is
// the only kind here (the ROM also knows '1.x and 'vbo for the two store
// formats).  NOT YET RECONSTRUCTED: the PartId and SourceType the ROM
// carries through, which name the package manager's copy of the package -
// there is no package manager here, so the caller passes what it knows.
Ref
InstallPart(RefArg partType, RefArg partFrame, RefArg packageName,
			ULong packageId, ULong partIndex, ULong size, ULong packageType)
{
	RefVar info(Clone(RefVar(Rcanonicalframepartinstallinfo)));
	SetFrameSlot(info, RSSYMparttype, partType);
	SetFrameSlot(info, RSSYMpartframe, partFrame);
	SetFrameSlot(info, RSSYMpackageid, RefVar(MAKEINT(packageId)));
	SetFrameSlot(info, RSSYMpackagename, packageName);
	SetFrameSlot(info, RSSYMpartindex, RefVar(MAKEINT(partIndex)));
	SetFrameSlot(info, RSSYMsize, RefVar(MAKEINT(size)));
	SetFrameSlot(info, RSSYMpackagetype, RefVar(MAKEINT(packageType)));
	SetFrameSlot(info, RSSYMdevicekind, RefVar(MAKEINT(0)));
	SetFrameSlot(info, RSSYMdevicenumber, RefVar(MAKEINT(0)));
	SetFrameSlot(info, RSSYMpackagestyle, RSSYMhighrom);

	RefVar args(MakeArray(1));
	SetArraySlotRef(args, 0, info);
	RefVar fn(GetFrameSlotRef(RefVar(gFunctionFrame), RSSYMinstallpart));
	if (ISNIL(fn))
	{
		fprintf(stderr, "[packages] no InstallPart in the function frame\n");
		fflush(stderr);
		return NILREF;
	}
	return DoBlock(fn, args);
}


// the part types the ROM's frames part handlers take
static Ref
PartTypeSymbol(ULong type)
{
	switch (type)
	{
	case 'form':	return RSSYMform;
	case 'auto':	return RSSYMauto;
	case 'book':	return RSSYMbook;
	case 'dict':	return RSSYMdict;
	case 'soup':	return RSSYMsoup;
	default:		return NILREF;
	}
}


// A part that cannot be installed is worth saying so about: it leaves the
// system quietly short of an application, and there is no other sign.
static void
Say(ULong packageId, ULong partIndex, const char* why)
{
	fprintf(stderr, "[packages] package %lu part %lu not installed: %s\n",
			(unsigned long) packageId, (unsigned long) partIndex, why);
	fflush(stderr);
}


// one package's frames parts installed
static void
InstallPackage(const unsigned char* rom, ULong packageAddress, ULong packageId)
{
	TPackageIterator iter((void*) (rom + packageAddress));
	if (iter.Init() != noErr)
		return;
	RefVar name;
	const UniChar* packageName = iter.PackageName();
	if (packageName != nil)
		name = MakeString(packageName);
	ULong parts = iter.NumberOfParts();
	for (ULong i = 0; i < parts; i++)
	{
		PartInfo part;
		memset(&part, 0, sizeof(part));
		iter.GetPartInfo(i, &part);
		if (part.kind != kFrames)
			continue;								// the protocols and the raw parts are somebody else's
		RefVar partType(PartTypeSymbol(part.type));
		if (ISNIL(partType))
		{
			Say(packageId, i, "no handler for its part type");
			continue;
		}
		ULong offset = iter.GetPartDataOffset(i);
		// the part's refs take its first byte to be at its ROM address
		TImportedObjectArea* area = ImportFramesPart(rom + packageAddress + offset, part.size,
													packageAddress + offset);
		if (area == nil)
		{
			// a streamed part is NSOF, which TFramePartHandler::Expand
			// reads with a TObjectReader: NOT YET RECONSTRUCTED
			Say(packageId, i, "its bytes are not a run of objects");
			continue;
		}
		RefVar frame(FramePartToplevelFrame(area->fArea));
		if (ISNIL(frame))
		{
			Say(packageId, i, "no top-level frame");
			RemoveFramesPart(area);
			continue;
		}
		newton_try
		{
			InstallPart(partType, frame, name, packageId, i, part.size, 0);
		}
		newton_catch_all
		{
			fprintf(stderr, "[packages] %s part %lu of package %lu threw %s while installing\n",
					SymbolName(partType), (unsigned long) i, (unsigned long) packageId,
					CurrentException()->name);
			fflush(stderr);
		}
		end_try;
	}
}


// ROM 0x000e7040 LoadHighROMFramesPackages__Fv
// The ROM calls LoadHighROMPackages, which walks the package list of each
// of the four extensions and sends the package manager a
// TPkBeginLoadEvent for every package in it, stopping at the first one
// that answers something other than "loaded" or "already there".
//
// DEVIATION: the package manager, its event handler and the part handlers
// are NOT YET RECONSTRUCTED, so there is nobody to send to.  The host
// walks the same package lists itself and installs each frames part
// directly: the part's objects imported (frames/FramesPart.h - the device
// uses them where they lie, the host cannot), its top-level frame taken,
// and InstallPart run over it, which is the point at which the ROM's own
// NewtonScript takes over and is the same on either.  A part that throws
// is reported and the next one tried, where the ROM would stop.
void
LoadHighROMFramesPackages(void)
{
	ULong imageSize = 0;
	const unsigned char* rom = (const unsigned char*) ROMImageBase(&imageSize);
	if (rom == nil)
		return;
	for (ULong rexId = 0; rexId < kMaxROMExtensions; rexId++)
	{
		ULong listSize = 0;
		VAddr list = GetRExConfigEntry(rexId, 'pkgl', &listSize);
		if (list == 0)
			continue;
		ULong at = (ULong) list;
		ULong end = at + listSize;
		if (end > imageSize)
			end = imageSize;
		ULong packageId = 0;
		while (at + sizeof(PackageDirectory) <= end)
		{
			if (!IsPackageHeader(rom + at, end - at))
			{
				at += kARMWord;
				continue;
			}
			ULong size = GetBigEndianWord(rom + at + 0x1c);
			if (size < sizeof(PackageDirectory) || at + size > end)
				break;
			InstallPackage(rom, at, packageId++);
			at = (at + size + 3) & ~3u;
		}
	}
}
