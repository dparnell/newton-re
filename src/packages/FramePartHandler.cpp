/*
	File:		packages/FramePartHandler.cpp

	Contains:	The frames part handlers (FramePartHandler.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "FramePartHandler.h"
#include "ROMPackages.h"
#include "PackageIterator.h"
#include "FramesPart.h"
#include "Units.h"
#include "ROMImport.h"
#include "LargeObjects.h"		// ROMDomainUserRequest (kRDMObjectAt)
#include "ROMExtension.h"
#include "ObjectStreamer.h"
#include "Pipes.h"
#include "BufferSegment.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <stdio.h>
#include <string.h>

extern const ExceptionName exPipeException;


/*------------------------------------------------------------------------------
	T h e   h o s t ' s   i m p o r t

	DEVIATION (FramePartHandler.h): a part's objects are imported into a
	host object area before its frame can be looked at.
------------------------------------------------------------------------------*/

// Whether the package at package has a part at back bytes from its start.
static Boolean
HasPartAt(const UByte* package, ULong back)
{
	if (!IsPackageHeader(package, back))
		return false;
	TPrivatePackageIterator iter;
	if (iter.Init((void*) package) != noErr)
		return false;
	for (ULong i = 0; i < iter.NumberOfParts(); i++)
		if (iter.GetPartDataOffset(i) == back)
			return true;
	return false;
}


// The package a part in memory belongs to.  A package on a store is the
// large object the domain manager has mapped round the part (a big
// package's later parts lie far beyond its directory: Newton Internet
// Enabler's ninth part is 0x35960 bytes in); anything else - the ROM's own
// packages, one loaded from memory - is looked for backwards from the part
// a word at a time, as far as the ROM image's start for a part in it, else
// 0x20000 bytes.  ==> the package, and the part's offset in it; nil when no
// package there says it has a part at that address.
static const UByte*
PackageContaining(const UByte* part, ULong* partOffset)
{
	RDMParams params;
	memset(&params, 0, sizeof(params));
	params.fAddress = (ULong) part;
	if (ROMDomainUserRequest(kRDMObjectAt, &params) == noErr)
	{
		const UByte* package = (const UByte*) params.fAddress;
		ULong back = (ULong) (part - package);
		if (back >= kPackageDirectorySize && HasPartAt(package, back))
		{
			*partOffset = back;
			return package;
		}
	}
	ULong limit = 0x20000;
	const void* region = nil;
	if (ROMAddressOf(part, nil, &region))
		limit = (ULong) (part - (const UByte*) region);
	for (ULong back = kPackageDirectorySize; back <= limit; back += kARMWord)
	{
		if (HasPartAt(part - back, back))
		{
			*partOffset = back;
			return part - back;
		}
	}
	return nil;
}


// The table a ROM extension's exports live in: 2 for the first extension,
// 4 for the second, and so on (ResolveMagicPtr).  It is made, empty, the
// first time a part of the extension is imported, and the entries that
// point into each part are filled in as the part is.  An entry whose part
// has not been imported (a streamed one) stays nil and answers
// kNSErrBadMagicPointer if anything asks for it, as a missing entry does
// on the Newton.
static void
TranslateROMExports(ULong32 partAddress, ULong partSize, const TImportedObjectArea* area)
{
	for (ULong rexId = 0; rexId < kMaxROMExtensions; rexId++)
	{
		ULong size = 0;
		VAddr table = GetRExConfigEntry(rexId, 'fexp', &size);
		long which = 2 + 2 * (long) rexId;
		if (table == 0 || size < kARMWord || which >= kMagicPointerTables)
			continue;
		long count = (long) (size / kARMWord);
		if (gMagicPointerTables[which] == nil || gMagicPointerTableCounts[which] != count)
		{
			Ref* entries = new Ref[count];
			if (entries == nil)
				continue;
			for (long i = 0; i < count; i++)
				entries[i] = NILREF;
			delete[] gMagicPointerTables[which];
			gMagicPointerTables[which] = entries;
			gMagicPointerTableCounts[which] = count;
		}
		for (long i = 0; i < count; i++)
		{
			ULong32 ref = GetBigEndianWord((const UByte*) table + i * kARMWord);
			if (ref >= partAddress && ref < partAddress + partSize)
				gMagicPointerTables[which][i] = area->TranslateRef(ref);
		}
	}
}


// A frames part in memory imported.  ==> the area, nil when its bytes are
// not a run of objects (reported: the part is not installed, and nothing
// else would say so); *inROMImage whether the part is one of the ROM's.
TImportedObjectArea*
ImportPackagePart(Ptr data, PartInfo* info, Boolean* inROMImage)
{
	ULong partOffset = 0;
	const UByte* package = PackageContaining((const UByte*) data, &partOffset);
	// a version 0 package packs its objects to eight bytes rather than
	// four, with a fill pattern in the gaps
	long align = 4;
	if (package != nil && ((const PackageDirectory*) package)->fSignature[7] == '0')
		align = 8;
	// (a part in the ROM - the image, or the extension an object file
	// carries - at its ROM address)
	ULong address = 0;
	Boolean inROM = ROMAddressOf(data, &address, nil);
	*inROMImage = inROM;
	ULong32 refBase = inROM ? (ULong32) address : (ULong32) partOffset;
	TImportedObjectArea* area = ImportFramesPart(data, info->size, refBase, align);
	if (area == nil)
	{
		fprintf(stderr, "[packages] a frames part ('%c%c%c%c) is not a run of objects\n",
				(char) (info->type >> 24), (char) (info->type >> 16), (char) (info->type >> 8), (char) info->type);
		fflush(stderr);
		return nil;
	}
	if (inROM)
		TranslateROMExports(refBase, info->size, area);
	return area;
}


// DEVIATION (part of the one above: the host imports a part's objects
// before it can use them, where the ROM reads them where they lie): a part
// may be asked about before it is installed - the Extras drawer's
// HandleNewPackage reads GetPkgRefInfo(pkgRef).parts as soon as a package
// is stored, before it is activated - so an area imported for that is kept
// and used again when the part is installed, rather than imported twice
// (which would leave the drawer's entries pointing at a copy the package's
// removal never takes away).
TImportedObjectArea*
FindOrImportPackagePart(Ptr data, PartInfo* info, Boolean* inROMImage)
{
	TImportedObjectArea* area = FindFramesPart((const void*) data);
	if (area == nil)
		return ImportPackagePart(data, info, inROMImage);
	*inROMImage = ROMAddressOf(data, nil, nil);
	return area;
}


/*------------------------------------------------------------------------------
	T F r a m e P a r t H a n d l e r
------------------------------------------------------------------------------*/

// A part that would not install: its area removed - unless it was
// imported before, only to be looked at (GetPkgRefInfo), in which case it
// stays provisional: the package is still there to be looked at, and its
// bytes, and refs into them, would still be good on the MessagePad.
static void
GiveBackPart(TImportedObjectArea* area, Boolean lookedAt)
{
	if (lookedAt)
		SetFramesPartProvisional(area, true);
	else
		RemoveFramesPart(area);
}


// ROM 0x000d118c Install__17TFramePartHandlerFRC6PartId10SourceTypeP8PartInfo
// The part's top-level frame found and handed to InstallFrame, with a
// remove object made for it; kError_Bad_Package when there is no frame.
// A part in memory is used where it lies unless it is NSOF ("streamed"),
// which is read out of it: its _ExportTable's units are recorded, and a
// part outside the ROM (at an address above 0x037fffff) has its
// _ImportTable installed and its package's pages flushed, so that its
// import refs are resolved (Units.h).
// ROM BUG: when InstallFrame fails the units the part exports are not
// taken back, and the export list keeps pointing into a part that is
// about to go.
// DEVIATION: the part the unit tables name is its imported area's first
// object, and its package that area (Units.h).
// A streamed source's part is one flattened object, read by Copy (Expand).
NewtonErr
TFramePartHandler::Install(const PartId& partId, SourceType sourceType, PartInfo* partInfo)
{
	RefVar frame;
	NewtonErr err = noErr;
	Ptr data = nil;
	TImportedObjectArea* area = nil;
	Boolean lookedAt = false;
	if (!IsMemory(sourceType))
	{
		Ref ref = NILREF;
		err = Copy(&ref);
		frame = ref;
	}
	else
	{
		data = GetSourcePtr();
		if (partInfo->compressed && strcmp(partInfo->compressor, "streamed") == 0)
		{
			CBufferSegment segment;
			segment.Init(GetSourcePtr(), partInfo->size, false, 0, -1);
			MemoryPipe pipe;
			pipe.Init(&segment, nil, false);
			Ref ref = NILREF;
			err = Expand(&ref, &pipe, partInfo);
			frame = ref;
		}
		else
		{
			Boolean inROM = false;
			lookedAt = FindFramesPart((const void*) data) != nil;
			area = FindOrImportPackagePart(data, partInfo, &inROM);
			if (area == nil)
				return kError_Bad_Package;
			SetFramesPartProvisional(area, false);		// (the part's now: see below)
			frame = FramePartToplevelFrame(area->fArea);
			if (ISNIL(frame))
			{
				GiveBackPart(area, lookedAt);
				return kError_Bad_Package;
			}
			void* source = area->fArea;
			RefVar exports(GetFrameSlotRef(frame, RSSYM_exporttable));
			if (NOTNIL(exports))
				InstallExportTables(exports, source);
			if (!inROM)
			{
				RefVar imports(GetFrameSlotRef(frame, RSSYM_importtable));
				if (NOTNIL(imports))
				{
					ULong package = (ULong) area;
					RegisterUnitArea(area);
					InstallImportTable(package, imports, source, area->fAreaEnd - area->fArea);
					FlushPackageCache(package);
				}
			}
		}
	}
	if (ISNIL(frame) || !IsFrame(frame))
	{
		if (area != nil)
			GiveBackPart(area, lookedAt);
		return kError_Bad_Package;
	}
	if (err != noErr)
	{
		if (area != nil)
			GiveBackPart(area, lookedAt);
		return err;
	}
	fRemoveObject = new FramePartRemoveObject;
	if (fRemoveObject == nil)
	{
		if (area != nil)
			GiveBackPart(area, lookedAt);
		return MemError();
	}
	fRemoveObject->fObject = new RefStruct(NILREF);
	fRemoveObject->fData = data;
	fRemoveObject->fArea = area;
	err = InstallFrame(frame, partId, sourceType, partInfo);
	if (err == noErr)
		SetRemoveObjPtr((RemoveObjPtr) fRemoveObject);
	else
	{
		delete fRemoveObject->fObject;
		delete fRemoveObject;
		fRemoveObject = nil;
		if (area != nil)
			GiveBackPart(area, lookedAt);
	}
	return err;
}


// ROM 0x000d14c8 Remove__17TFramePartHandlerFRC6PartIdUll
// The part taken out: its unit tables removed, its remove object handed
// to RemoveFrame, the objects no longer referred to collected; any units
// whose importers have now lost them reported (ReportDeadUnitImports).
NewtonErr
TFramePartHandler::Remove(const PartId& partId, PartType partType, RemoveObjPtr removePtr)
{
	FramePartRemoveObject* removeObject = (FramePartRemoveObject*) removePtr;
	RefVar removed;
	RefVar deadImports;
	TImportedObjectArea* area = removeObject->fArea;
	if (removeObject->fData != nil)
	{
		// (the part the unit tables name: on the host its area's first object)
		void* source = area != nil ? (void*) area->fArea : (void*) removeObject->fData;
		deadImports = RemoveExportTables(source);
		RemoveImportTable(source);
	}
	removed = *removeObject->fObject;
	if (removeObject != nil)
	{
		delete removeObject->fObject;
		delete removeObject;
	}
	NewtonErr err = RemoveFrame(removed, partId, partType);
	GC();
	ICacheClear();
	// DEVIATION: the imported objects go with the part; refs to them left in
	// the heap are declawed, as the ROM declaws a removed package's range
	if (area != nil)
	{
		removed = NILREF;
		UnregisterUnitArea(area);
		RemoveFramesPart(area);
	}
	if (NOTNIL(deadImports) && Length(deadImports) > 0)
	{
		newton_try
		{
			if (FrameHasSlot(RefVar(gFunctionFrame), RSSYMreportdeadunitimports))
				NSCallGlobalFn(RSSYMreportdeadunitimports, deadImports);
		}
		newton_catch_all
		{ }
		end_try;
	}
	return err;
}


// ROM 0x000d15f8 SetFrameRemoveObject__17TFramePartHandlerFRC6RefVar
NewtonErr
TFramePartHandler::SetFrameRemoveObject(RefArg removeObject)
{
	*fRemoveObject->fObject = removeObject;
	return noErr;
}


// ROM 0x000d1628 Expand__17TFramePartHandlerFPvP5CPipeP8PartInfo
// A streamed part's object read (NSOF) into *data.  ==> a pipe
// exception's error; any other exception is passed on.
NewtonErr
TFramePartHandler::Expand(void* data, CPipe* pipe, PartInfo* /*info*/)
{
	NewtonErr err = noErr;
	TObjectReader reader(*pipe);
	newton_try
	{
		*(Ref*) data = reader.Read();
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}


/*------------------------------------------------------------------------------
	I n s t a l l P a r t   a n d   R e m o v e P a r t
------------------------------------------------------------------------------*/

// ROM 0x000cb68c InstallPart__FRC6RefVarT1RC6PartId10SourceTypeP8PartInfoT1
// The part described (canonicalFramePartInstallInfo) and handed to the
// global InstallPart; its frame, its packageStyle and InstallPart's
// answer (the remove cookie) kept in the remove object.
NewtonErr
InstallPart(RefArg partType, RefArg partFrame, const PartId& partId, SourceType type, PartInfo* info, RefArg removeObject)
{
	NewtonErr err = noErr;
	newton_try
	{
		RefVar style;
		if (type.format == kFixedMemory)
		{
			if (type.deviceKind == kNoDevice)
				style = RSSYMhighrom;
		}
		else if (type.format == kRemovableMemory)
		{
			if (type.deviceKind == kStoreDevice)
				style = RSSYM1_2Ex;
			else if (type.deviceKind == kStoreDeviceV2)
				style = RSSYMvbo;
		}
		RefVar installInfo(Clone(RefVar(Rcanonicalframepartinstallinfo)));
		SetFrameSlot(installInfo, RSSYMparttype, partType);
		SetFrameSlot(installInfo, RSSYMpartframe, partFrame);
		SetFrameSlot(installInfo, RSSYMpackageid, RefVar(MAKEINT(partId.packageId)));
		SetFrameSlot(installInfo, RSSYMpackagename, RefVar(MakeString(((ExtendedPartInfo*) info)->packageName)));
		SetFrameSlot(installInfo, RSSYMpartindex, RefVar(MAKEINT(partId.partIndex)));
		SetFrameSlot(installInfo, RSSYMsize, RefVar(MAKEINT(info->sizeInMemory)));
		SetFrameSlot(installInfo, RSSYMpackagetype, RefVar(MAKEINT(type.format)));
		SetFrameSlot(installInfo, RSSYMdevicekind, RefVar(MAKEINT(type.deviceKind)));
		SetFrameSlot(installInfo, RSSYMdevicenumber, RefVar(MAKEINT(type.deviceNumber)));
		SetFrameSlot(installInfo, RSSYMpackagestyle, style);
		RefVar args(MakeArray(1));
		SetArraySlotRef(args, 0, installInfo);
		RefVar fn(GetFrameSlotRef(RefVar(gFunctionFrame), RSSYMinstallpart));
		RefVar cookie(DoBlock(fn, args));
		SetFrameSlot(removeObject, RSSYMpartframe, partFrame);
		SetFrameSlot(removeObject, RSSYMpackagestyle, style);
		SetFrameSlot(removeObject, RSSYMremovecookie, cookie);
	}
	newton_catch(exFrames)
	{
		err = FramesException(CurrentException());
	}
	end_try;
	return err;
}


// ROM 0x000cba20 RemovePart__FRC6RefVarRC6PartIdT1
// The part described (canonicalFramePartRemoveInfo, out of the remove
// object) and handed to the global RemovePart with its cookie; an
// evt.ex.fr is swallowed.
NewtonErr
RemovePart(RefArg partType, const PartId& partId, RefArg removeObject)
{
	newton_try
	{
		RefVar removeInfo(Clone(RefVar(Rcanonicalframepartremoveinfo)));
		SetFrameSlot(removeInfo, RSSYMparttype, partType);
		SetFrameSlot(removeInfo, RSSYMpartframe, RefVar(GetFrameSlotRef(removeObject, RSSYMpartframe)));
		SetFrameSlot(removeInfo, RSSYMpackageid, RefVar(MAKEINT(partId.packageId)));
		SetFrameSlot(removeInfo, RSSYMpartindex, RefVar(MAKEINT(partId.partIndex)));
		SetFrameSlot(removeInfo, RSSYMpackagestyle, RefVar(GetFrameSlotRef(removeObject, RSSYMpackagestyle)));
		RefVar args(MakeArray(2));
		SetArraySlotRef(args, 0, removeInfo);
		SetArraySlotRef(args, 1, GetFrameSlotRef(removeObject, RSSYMremovecookie));
		RefVar fn(GetFrameSlotRef(RefVar(gFunctionFrame), RSSYMremovepart));
		DoBlock(fn, args);
	}
	newton_catch(exFrames)
	{
		FramesException(CurrentException());
	}
	end_try;
	return noErr;
}


/*------------------------------------------------------------------------------
	T F o r m P a r t H a n d l e r   a n d
	T A u t o S c r i p t P a r t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x000cbc74 InstallFrame__16TFormPartHandlerFRC6RefVarRC6PartId10SourceTypeP8PartInfo
NewtonErr
TFormPartHandler::InstallFrame(RefArg frame, const PartId& partId, SourceType sourceType, PartInfo* partInfo)
{
	RefVar removeObject(Clone(RefVar(Rcanonicalframepartsavedobject)));
	SetFrameRemoveObject(removeObject);
	return InstallPart(RSSYMform, frame, partId, sourceType, partInfo, removeObject);
}


// ROM 0x000cbd00 RemoveFrame__16TFormPartHandlerFRC6RefVarRC6PartIdUl
// (RemovePart, written out again in the ROM)
NewtonErr
TFormPartHandler::RemoveFrame(RefArg removeObject, const PartId& partId, PartType /*partType*/)
{
	return RemovePart(RSSYMform, partId, removeObject);
}


// ROM 0x000cbd18 GetBackupInfo__16TFormPartHandlerFRC6PartIdUllP8PartInfoT2PUc
// ROM BUG: says nothing about whether the part needs a backup - the
// caller's flag is left as it was.
NewtonErr
TFormPartHandler::GetBackupInfo(const PartId& /*partId*/, PartType /*partType*/, RemoveObjPtr /*removePtr*/, PartInfo* /*partInfo*/,
								ULong /*lastBackupDate*/, Boolean* /*needsBackup*/)
{
	return noErr;
}


// ROM 0x000cbd20 Backup__16TFormPartHandlerFRC6PartIdlP5CPipe
NewtonErr
TFormPartHandler::Backup(const PartId& /*partId*/, RemoveObjPtr /*removePtr*/, CPipe* /*pipe*/)
{
	return noErr;
}


// ROM 0x000cbd28 InstallFrame__22TAutoScriptPartHandlerFRC6RefVarRC6PartId10SourceTypeP8PartInfo
NewtonErr
TAutoScriptPartHandler::InstallFrame(RefArg frame, const PartId& partId, SourceType sourceType, PartInfo* partInfo)
{
	RefVar removeObject(Clone(RefVar(Rcanonicalframepartsavedobject)));
	SetFrameRemoveObject(removeObject);
	return InstallPart(RSSYMauto, frame, partId, sourceType, partInfo, removeObject);
}


// ROM 0x000cbdb4 RemoveFrame__22TAutoScriptPartHandlerFRC6RefVarRC6PartIdUl
NewtonErr
TAutoScriptPartHandler::RemoveFrame(RefArg removeObject, const PartId& partId, PartType /*partType*/)
{
	return RemovePart(RSSYMauto, partId, removeObject);
}


/*------------------------------------------------------------------------------
	T C o m m P a r t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x0013a5d8 InstallFrame__16TCommPartHandlerFRC6RefVarRC6PartId10SourceTypeP8PartInfo
// The part's `configurations`, if it has some, registered with the global
// RegCommConfigArray (an evt.ex.fr becomes the answer), then the part
// installed as an 'auto part is.
// ROM BUG: what the 'auto part's installation answers is thrown away -
// only the configurations' error (none, usually) is answered.
NewtonErr
TCommPartHandler::InstallFrame(RefArg frame, const PartId& partId, SourceType sourceType, PartInfo* partInfo)
{
	NewtonErr err = noErr;
	RefVar configurations(GetFrameSlotRef(frame, RSSYMconfigurations));
	if (NOTNIL(configurations))
	{
		RefVar args(MakeArray(1));
		SetArraySlotRef(args, 0, configurations);
		newton_try
		{
			RefVar fn(GetFrameSlotRef(RefVar(gFunctionFrame), RSSYMregcommconfigarray));
			DoBlock(fn, args);
		}
		newton_catch(exFrames)
		{
			err = FramesException(CurrentException());
		}
		end_try;
	}
	TAutoScriptPartHandler::InstallFrame(frame, partId, sourceType, partInfo);
	return err;
}


// ROM 0x0013a760 RemoveFrame__16TCommPartHandlerFRC6RefVarRC6PartIdUl
// The configurations unregistered (the global UnRegCommConfigArray), then
// the part removed as an 'auto part is.
// ROM BUG: the configurations are looked for in the remove object - a
// canonicalFramePartSavedObject, {partFrame, packageStyle, removeCookie} -
// rather than in the part's frame, so they are never unregistered.  And
// as in InstallFrame, the 'auto part's answer is thrown away.
NewtonErr
TCommPartHandler::RemoveFrame(RefArg removeObject, const PartId& partId, PartType partType)
{
	NewtonErr err = noErr;
	RefVar configurations(GetFrameSlotRef(removeObject, RSSYMconfigurations));
	if (NOTNIL(configurations))
	{
		RefVar args(MakeArray(1));
		SetArraySlotRef(args, 0, configurations);
		newton_try
		{
			RefVar fn(GetFrameSlotRef(RefVar(gFunctionFrame), RSSYMunregcommconfigarray));
			DoBlock(fn, args);
		}
		newton_catch(exFrames)
		{
			err = FramesException(CurrentException());
		}
		end_try;
	}
	TAutoScriptPartHandler::RemoveFrame(removeObject, partId, partType);
	return err;
}
