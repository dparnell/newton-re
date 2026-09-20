/*
	File:		packages/ROMPackages.cpp

	Contains:	The ROM extension's packages found and installed
				(ROMPackages.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "NativeFunctions.h"
#include "ROMPackages.h"
#include "Store.h"
#include "Soups.h"
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
/*------------------------------------------------------------------------------
	T h e   f r a m e   e x p o r t   t a b l e

	A package built by NTK refers to the objects of the system, and to the
	objects of the other packages built with it, through magic pointers: a
	ref whose value is a table number and an index into it
	(ObjectHeap.h's ResolveMagicPtr - table 0 is the ROM's own, table 1 the
	global variables and the built-in functions, the even tables from 2 up
	the ROM extensions' export tables).  A ROM extension carries its table
	as its `fexp` configuration entry: a flat array of refs, one per
	exported object, pointing into the extension's own packages.

	On the Newton those refs are addresses of objects that are simply
	there, and ResolveMagicPtr reads the entry as it lies.  DEVIATION: the
	host imports each part into an object area of its own
	(frames/FramesPart.h), so the addresses in the table mean nothing until
	the part they point into has been imported - the table is copied into
	gMagicPointerTables with each entry translated, and an entry whose part
	has not been imported (a streamed one, which is NOT YET) is left nil
	and answers kNSErrBadMagicPointer if anything asks for it, as a missing
	entry does on the Newton.
------------------------------------------------------------------------------*/

// the table a ROM extension's exports live in: 2 for the first extension,
// 4 for the second, and so on (ResolveMagicPtr)
static long
ExportTableFor(ULong rexId)
{
	return 2 + 2 * (long) rexId;
}


// The extension's export table made, empty, before its parts are imported.
static void
MakeExportTable(ULong rexId)
{
	ULong size = 0;
	VAddr table = GetRExConfigEntry(rexId, 'fexp', &size);
	long which = ExportTableFor(rexId);
	if (table == 0 || size < kARMWord || which >= kMagicPointerTables)
		return;
	long count = (long) (size / kARMWord);
	Ref* entries = new Ref[count];
	if (entries == nil)
		return;
	for (long i = 0; i < count; i++)
		entries[i] = NILREF;
	delete[] gMagicPointerTables[which];
	gMagicPointerTables[which] = entries;
	gMagicPointerTableCounts[which] = count;
}


// The entries of the extension's export table that point into the part
// just imported, translated to the host's refs.
static void
TranslateExports(ULong rexId, const unsigned char* rom, ULong partAddress, ULong partSize,
				 const TImportedObjectArea* area)
{
	ULong size = 0;
	VAddr table = GetRExConfigEntry(rexId, 'fexp', &size);
	long which = ExportTableFor(rexId);
	if (table == 0 || which >= kMagicPointerTables || gMagicPointerTables[which] == nil)
		return;
	long count = gMagicPointerTableCounts[which];
	for (long i = 0; i < count; i++)
	{
		ULong32 ref = GetBigEndianWord(rom + (ULong) table + i * kARMWord);
		if (ref < partAddress || ref >= partAddress + partSize)
			continue;
		gMagicPointerTables[which][i] = area->TranslateRef(ref);
	}
}


// a package's own store, the part type the ROM's TPackageStorePartHandler
// is registered for
const PartType kStorePartType = 'soup';


// ROM 0x001601dc Install__24TPackageStorePartHandlerFRC6PartId10SourceTypeP8PartInfo
// A 'soup part - a store built into the package, read-only, holding the
// data an application ships with (the WorldData package's cities and
// countries, which the Setup assistant's city picker queries) - mounted:
// a TPackageStore over the part's bytes where they lie, made into a store
// frame and added to gPackageStores, which is what GetPackageStore looks
// through.
//
// DEVIATION: the part handlers are NOT YET RECONSTRUCTED, so this is
// called from the loop below rather than by the package manager handing
// the part to TPackageStorePartHandler.  The ROM removes the store again
// if MakeStoreObject throws anything but an evt.ex; here a throw is
// reported and the store let go, as the frames parts are.
static void
InstallStorePart(TPackageIterator& iter, ULong index, const PartInfo& part, ULong packageId)
{
	TStore* store = TStore::New("TPackageStore");
	if (store == nil)
	{
		Say(packageId, index, "no memory for its store");
		return;
	}
	void* data = (void*) ((char*) iter.fPackage + iter.GetPartDataOffset(index));
	if (store->Init(data, part.sizeInMemory, 0, 0, 0, nil) != noErr)
	{
		Say(packageId, index, "its store would not mount");
		store->Delete();
		return;
	}
	newton_try
	{
		RefVar storeObject(MakeStoreObject(store));
		RefVar stores(gPackageStores);
		AddArraySlot(stores, storeObject);
	}
	newton_catch_all
	{
		fprintf(stderr, "[packages] the store part %lu of package %lu threw %s while mounting\n",
				(unsigned long) index, (unsigned long) packageId, CurrentException()->name);
		fflush(stderr);
		store->Delete();
	}
	end_try;
}

static void	RememberInstalledPackage(void* bytes, ULong packageId);


static void
InstallPackage(ULong rexId, const unsigned char* rom, ULong packageAddress, ULong packageId)
{
	TPackageIterator iter((void*) (rom + packageAddress));
	if (iter.Init() != noErr)
		return;
	RememberInstalledPackage((void*) (rom + packageAddress), packageId);
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
		if (part.kind == kRaw && part.type == kStorePartType)
		{
			InstallStorePart(iter, i, part, packageId);
			continue;
		}
		if (part.kind != kFrames)
			continue;								// the protocols are somebody else's
		RefVar partType(PartTypeSymbol(part.type));
		if (ISNIL(partType))
		{
			Say(packageId, i, "no handler for its part type");
			continue;
		}
		ULong offset = iter.GetPartDataOffset(i);
		// the part's refs take its first byte to be at its ROM address.
		// A version 0 package packs its objects to eight bytes rather than
		// four, with a fill pattern in the gaps, so the walk has to be
		// told which kind it is reading.
		long align = iter.PackageFormatVersion() == 0 ? 8 : 4;
		TImportedObjectArea* area = ImportFramesPart(rom + packageAddress + offset, part.size,
													packageAddress + offset, align);
		if (area == nil)
		{
			// (a part whose bytes are a NSOF stream rather than a run of
			//  objects would land here too; TFramePartHandler::Expand
			//  reads those with a TObjectReader, which is NOT YET)
			Say(packageId, i, "its bytes are not a run of objects");
			continue;
		}
		TranslateExports(rexId, rom, packageAddress + offset, part.size, area);
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


// The host's note of the packages it installed, which stands in for the
// package manager's list (ROMPackages.h).  The bytes stay where they
// are - they are the ROM extension's own - so only the address and the
// id it was installed under are kept.
struct HostInstalledPackage
{
	void*	fBytes;
	ULong	fPackageId;
};
const long kMaxInstalledPackages = 64;
static HostInstalledPackage	gInstalledPackages[kMaxInstalledPackages];
static long					gInstalledPackageCount = 0;

static void
RememberInstalledPackage(void* bytes, ULong packageId)
{
	if (gInstalledPackageCount >= kMaxInstalledPackages)
		return;
	gInstalledPackages[gInstalledPackageCount].fBytes = bytes;
	gInstalledPackages[gInstalledPackageCount].fPackageId = packageId;
	gInstalledPackageCount++;
}


long
InstalledPackageCount(void)
{
	return gInstalledPackageCount;
}


void*
InstalledPackageAt(long index, ULong* packageId)
{
	if (index < 0 || index >= gInstalledPackageCount)
		return nil;
	if (packageId != nil)
		*packageId = gInstalledPackages[index].fPackageId;
	return gInstalledPackages[index].fBytes;
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
		MakeExportTable(rexId);
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
			InstallPackage(rexId, rom, at, packageId++);
			at = (at + size + 3) & ~3u;
		}
	}
}

// ROM 0x001fb630 IteratorToPackageFrame__FP11TPMIterator
// One package as a script sees it: a clone of the ROM's
// canonicalTPMIteratorPackageFrame with what the package says about
// itself put into it.  The ROM reads those facts off a TPMIterator, whose
// fields the package manager filled in when it installed the package;
// they are the same facts the package's own directory carries, which is
// where the host reads them.
//
// NOT YET RECONSTRUCTED: IdToStore, which adds the `store` and `pssid`
// slots for a package that lives on a store.  These are in the ROM, so
// they are on no store, and the ROM leaves both slots out for those too.
static Ref
IteratorToPackageFrame(TPackageIterator& iter, ULong packageId)
{
	RefVar frame(Clone(RefVar(Rcanonicaltpmiteratorpackageframe)));
	// the ROM answers the package manager's id for the package; the host's
	// is the one it installed the package's parts under
	SetFrameSlot(frame, RSSYMid, RefVar(MAKEINT(packageId)));
	SetFrameSlot(frame, RSSYMsize, RefVar(MAKEINT(iter.PackageSize())));
	const UniChar* name = iter.PackageName();
	SetFrameSlot(frame, RSSYMtitle, name != nil ? RefVar(MakeString(name)) : RefVar(NILREF));
	SetFrameSlot(frame, RSSYMversion, RefVar(MAKEINT(iter.GetVersion())));
	SetFrameSlot(frame, RSSYMtimestamp, RefVar(MAKEINT(iter.CreationDate())));
	SetFrameSlot(frame, RSSYMcopyprotection, RefVar(MAKEBOOLEAN(iter.CopyProtected())));
	return frame;
}


// ROM 0x001fbaf8 FGetPackages__FRC6RefVar
// GetPackages(): an array with a frame for every package installed - what
// the setup assistant asks for when it has finished with the signature.
//
// DEVIATION: the ROM forks the application world first (TForkWorld::Fork,
// throwing "couldn't fork it over" when it cannot) so that the walk
// happens away from the caller, and then walks the package manager's
// TPMIterator.  Neither the fork nor the package manager is reconstructed;
// the host walks the packages it installed itself, in the order it
// installed them.
Ref
FGetPackages(RefArg /*rcvr*/)
{
	RefVar packages(MakeArray(0));
	for (long i = 0; i < InstalledPackageCount(); i++)
	{
		ULong packageId = 0;
		void* bytes = InstalledPackageAt(i, &packageId);
		TPackageIterator iter(bytes);
		if (iter.Init() != noErr)
			continue;
		AddArraySlot(packages, RefVar(IteratorToPackageFrame(iter, packageId)));
	}
	return packages;
}


void
RegisterPackageNatives(void)
{
	RegisterNativeFunction("FGetPackages__FRC6RefVar", (void*) FGetPackages, 0);
}
