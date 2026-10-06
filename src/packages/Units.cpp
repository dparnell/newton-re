/*
	File:		packages/Units.cpp

	Contains:	Units: export and import tables, the pending imports and the
				unit natives (Units.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Units.h"
#include "SortedList.h"
#include "ListIterator.h"
#include "ROMPackages.h"
#include "ROMExtension.h"
#include "FramesPart.h"
#include "ObjectAreaImport.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "ByteOrder.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "LargeObjects.h"
#include "LargeBinaries.h"
#include "host/RomBugs.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>

CSortedList*		gMPExportList = nil;
CSortedList*		gMPImportList = nil;
MPPendingImport*	gMPPendingImports = nil;

// InitMagicPointerTables (frames/Objects.cpp) calls InitRExMagicPointerTables
// through a hook, the frames library sitting below this one; it is set
// before anything runs.
static struct SetRExMagicPointerTablesHook
{
	SetRExMagicPointerTablesHook()	{ gInitRExMagicPointerTables = InitRExMagicPointerTables; }
} gSetRExMagicPointerTablesHook;

// the MessagePad's parts start in the ROM below this address; anything
// above it is a package's (TFramePartHandler::Install)
static const ULong	kMaxROMPartAddress = 0x037fffff;


/*------------------------------------------------------------------------------
	T h e   l i s t s '   o r d e r s
------------------------------------------------------------------------------*/

// ROM 0x000cf868 TestItem__25CMPExportListNameComparerCFPCv
// By name, ignoring the case of ASCII letters.
CompareResult
CMPExportListNameComparer::TestItem(const void* criteria) const
{
	const unsigned char* item = (const unsigned char*) ((const MPExportItem*) fItem)->fName;
	const unsigned char* other = (const unsigned char*) ((const MPExportItem*) criteria)->fName;
	for ( ; ; )
	{
		unsigned int a = *item;
		unsigned int b = *other;
		if (a == 0)
			return b != 0 ? kItemLessThanCriteria : kItemEqualCriteria;
		if (b == 0)
			return kItemGreaterThanCriteria;
		if (a > 0x60 && a < 0x7b)
			a -= 0x20;
		if (b > 0x60 && b < 0x7b)
			b -= 0x20;
		item++;
		other++;
		if (a != b)
			return (int) a < (int) b ? kItemLessThanCriteria : kItemGreaterThanCriteria;
	}
}


// ROM 0x000cf878 TestItem__27CMPImportListSourceComparerCFPCv
// By the part's address.
CompareResult
CMPImportListSourceComparer::TestItem(const void* criteria) const
{
	ULong mine = (ULong) ((const MPImportItem*) fItem)->fSource;
	ULong other = (ULong) ((const MPImportItem*) criteria)->fSource;
	if (other > mine)
		return kItemLessThanCriteria;
	return mine == other ? kItemEqualCriteria : kItemGreaterThanCriteria;
}


// ROM 0x000cf89c TestItem__25CMPImportListSourceTesterCFPCv
// The item being tested for is the part's address itself.
CompareResult
CMPImportListSourceTester::TestItem(const void* criteria) const
{
	ULong mine = (ULong) fItem;
	ULong other = (ULong) ((const MPImportItem*) criteria)->fSource;
	if (other > mine)
		return kItemLessThanCriteria;
	return mine == other ? kItemEqualCriteria : kItemGreaterThanCriteria;
}


/*------------------------------------------------------------------------------
	P e n d i n g   i m p o r t s
------------------------------------------------------------------------------*/

// ROM 0x000cf8bc Fulfill__16PkgPendingImportFP12MPExportItem
// The import slot filled in and the importing package's pages flushed, so
// that its refs are relocated against the export.  (The caller counts the
// client.)
void
PkgPendingImport::Fulfill(MPExportItem* item)
{
	AllocateExportTable(item);
	MPImportItem* import = (MPImportItem*) fImport;
	import->fExports[fIndex] = item;
	FlushPackageCache(import->fPackage);
}


// ROM 0x000cf8fc Match__16PkgPendingImportFPv
Boolean
PkgPendingImport::Match(void* source)
{
	return ((MPImportItem*) fImport)->fSource == source;
}


// ROM 0x000cf914 Fulfill__16RExPendingImportFP12MPExportItem
// The ROM extension's import table filled in from the export's objects:
// the record's count of them.
void
RExPendingImport::Fulfill(MPExportItem* item)
{
	long count = (long) GetBigEndianWord((const UByte*) fImport + 0x0C);
	memcpy(fTable, item->fTable, count * sizeof(Ref));
}


// ROM 0x000cf92c Match__16RExPendingImportFPv
// A ROM extension's import belongs to no part.
Boolean
RExPendingImport::Match(void* /*source*/)
{
	return false;
}


// ROM 0x000cf934 RegisterPendingImport__FP12MPImportItemlPcN22
// A package's import slot waiting for its unit, put at the front of the
// pending imports.
// ROM BUG (fixed): the test for the allocation's failing tests the import
// item instead of the new object - a nil item throws out-of-memory, and a
// failed allocation is written through.  The fix: the new object is
// tested.
void
RegisterPendingImport(MPImportItem* item, long index, const char* name, long major, long minor)
{
	PkgPendingImport* pending = new PkgPendingImport;
	if (RomBugFixed() ? pending == nil : item == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	pending->fKind = 0;
	pending->fName = name;
	pending->fMajor = major;
	pending->fMinor = minor;
	pending->fImport = item;
	pending->fIndex = index;
	pending->fNext = gMPPendingImports;
	gMPPendingImports = pending;
}


// ROM 0x000cf9c8 RegisterPendingImport__FP9RExImportPlPclT4
// A ROM extension's import waiting for its unit.  ROM BUG (fixed) as
// above: the record is tested rather than the new object, and the fix
// tests the new object.
void
RegisterPendingImport(const void* rexImport, Ref* table, const char* name, long major, long minor)
{
	RExPendingImport* pending = new RExPendingImport;
	if (RomBugFixed() ? pending == nil : rexImport == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	pending->fKind = 1;
	pending->fName = name;
	pending->fMajor = major;
	pending->fMinor = minor;
	pending->fImport = (void*) rexImport;
	pending->fTable = table;
	pending->fNext = gMPPendingImports;
	gMPPendingImports = pending;
}


// ROM 0x000cfa5c RemovePendingImports__FPv
// Every pending import of the part forgotten.
void
RemovePendingImports(void* source)
{
	MPPendingImport* previous = nil;
	MPPendingImport* pending = gMPPendingImports;
	while (pending != nil)
	{
		if (pending->Match(source))
		{
			MPPendingImport* doomed = pending;
			pending = pending->fNext;
			delete doomed;
			if (previous == nil)
				gMPPendingImports = pending;
			else
				previous->fNext = pending;
		}
		else
		{
			previous = pending;
			pending = pending->fNext;
		}
	}
}


// host: the pending imports of one import item forgotten (the fix of the
// ROM BUG in InstallImportTable below; RemovePendingImports goes by part,
// which would take an earlier installation's too).
static void
RemovePendingImportsOf(MPImportItem* import)
{
	MPPendingImport* previous = nil;
	MPPendingImport* pending = gMPPendingImports;
	while (pending != nil)
	{
		if (pending->fKind == 0 && pending->fImport == import)
		{
			MPPendingImport* doomed = pending;
			pending = pending->fNext;
			delete doomed;
			if (previous == nil)
				gMPPendingImports = pending;
			else
				previous->fNext = pending;
		}
		else
		{
			previous = pending;
			pending = pending->fNext;
		}
	}
}


// ROM 0x000cfb48 FulfillPendingImports__FP12MPExportIteml
// Every pending import the export satisfies - of the given import table,
// or of any when that is nil, and every ROM extension's - fulfilled: the
// same name (ignoring case) and major version, a minor version no newer
// than the export's.
void
FulfillPendingImports(MPExportItem* item, Ref importTable)
{
	MPPendingImport* previous = nil;
	MPPendingImport* pending = gMPPendingImports;
	while (pending != nil)
	{
		if ((importTable == NILREF || pending->fKind != 0 || ((MPImportItem*) pending->fImport)->fImportTable == importTable)
		 && symcmp(item->fName, (char*) pending->fName) == 0
		 && item->fMajor == pending->fMajor
		 && item->fMinor >= pending->fMinor)
		{
			MPPendingImport* fulfilled = pending;
			pending = pending->fNext;
			item->fClients++;
			fulfilled->Fulfill(item);
			delete fulfilled;
			if (previous == nil)
				gMPPendingImports = pending;
			else
				previous->fNext = pending;
		}
		else
		{
			previous = pending;
			pending = pending->fNext;
		}
	}
}


/*------------------------------------------------------------------------------
	E x p o r t   t a b l e s
------------------------------------------------------------------------------*/

// ROM 0x000d1704 IsInRDMSpace__FUl
// DEVIATION: the host has no ROM domain - no package is mapped into its
// virtual memory - so nothing is in its space.
Boolean
IsInRDMSpace(ULong /*address*/)
{
	return false;
}


// ROM 0x000cfc1c AllocateExportTable__FP12MPExportItem
// An export whose objects lie in the ROM domain (a package on a store,
// which may be paged out) given a copy of its table in memory.
void
AllocateExportTable(MPExportItem* item)
{
	if (item->fAllocated)
		return;
	if (!IsInRDMSpace((ULong) item->fOriginalTable))
		return;
	Ref* copy = (Ref*) malloc(item->fCount * sizeof(Ref));
	if (copy == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	BlockMove(item->fOriginalTable, copy, item->fCount * sizeof(Ref));
	item->fTable = copy;
	item->fAllocated = true;
}


// ROM 0x000cfc9c FreeExportTable__FP12MPExportItem
void
FreeExportTable(MPExportItem* item)
{
	if (!item->fAllocated)
		return;
	free(item->fTable);
	item->fTable = item->fOriginalTable;
	item->fAllocated = false;
}


// ROM 0x000d0f48 InitMPTableRegistry__Fv
// The export list (by name) and the import list (by part).
void
InitMPTableRegistry(void)
{
	CMPExportListNameComparer* byName = new CMPExportListNameComparer;
	if (byName == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	gMPExportList = new CSortedList(byName);
	if (gMPExportList == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	CMPImportListSourceComparer* bySource = new CMPImportListSourceComparer;
	if (bySource == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	gMPImportList = new CSortedList(bySource);
	if (gMPImportList == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
}


// ROM 0x000cfcd4 InstallExportTables__FRC6RefVarPv
// Each unit of the part's _ExportTable recorded, and any pending import
// it satisfies fulfilled.
void
InstallExportTables(RefArg exportTable, void* source)
{
	if (gMPExportList == nil)
		InitMPTableRegistry();
	long count = Length(exportTable);
	RefVar unit;
	for (long i = 0; i < count; i++)
	{
		unit = GetArraySlotRef(exportTable, i);
		const char* name = SymbolName(GetFrameSlotRef(unit, RSSYMname));
		MPExportItem* item = (MPExportItem*) malloc(offsetof(MPExportItem, fName) + strlen(name) + 1);	// DEVIATION: host field sizes
		if (item == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		newton_try
		{
			strcpy(item->fName, name);
			item->fSource = source;
			item->fMajor = RINT(GetFrameSlotRef(unit, RSSYMmajor));
			item->fMinor = RINT(GetFrameSlotRef(unit, RSSYMminor));
			item->fClients = 0;
			Ref objects = GetFrameSlotRef(unit, RSSYMobjects);
			item->fCount = Length(objects);
			item->fTable = Slots(objects);
			item->fOriginalTable = item->fTable;
			item->fAllocated = false;
			if (gMPExportList->Insert(item) != noErr)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			FulfillPendingImports(item, NILREF);
		}
		cleanup
		{
			free(item);
		}
		end_try;
	}
}


// ROM 0x000cfef8 RemoveExportTables__FPv
// Every unit the part exports taken away: each import slot that used it
// made a pending import again (and its package's pages flushed, so the
// refs through it are no longer resolved), the importers that are still
// there reported.  ==> an array of canonicalDeadImport frames, {name,
// major, minor, client: the importing part's frame}.
// ROM BUG (fixed): the export item itself is never freed, only taken off
// the list.  The fix: it is freed once it is off the list (nothing points
// at it by then - every import slot that did is pending again).
Ref
RemoveExportTables(void* source)
{
	RefVar deadImports(AllocateArray(RSSYMarray, 0));
	if (gMPExportList == nil)
		return deadImports;
	RefVar wanted;
	RefVar dead;
	RefVar value;
	CListIterator iter(gMPExportList);
	for (MPExportItem* item = (MPExportItem*) iter.FirstItem(); iter.More(); item = (MPExportItem*) iter.NextItem())
	{
		if (item->fSource != source)
			continue;
		long imports = gMPImportList->GetArraySize();
		for (long i = 0; i < imports; i++)
		{
			MPImportItem* import = (MPImportItem*) gMPImportList->At(i);
			for (long slot = 0; slot < import->fCount; slot++)
			{
				if (import->fExports[slot] != item)
					continue;
				import->fExports[slot] = nil;
				item->fClients--;
				wanted = GetArraySlotRef(import->fImportTable, slot);
				Ref client = FramePartToplevelFrame(import->fSource);
				if (client != NILREF)
				{
					dead = Clone(Rcanonicaldeadimport);
					value = GetFrameSlotRef(wanted, RSSYMname);
					value = EnsureInternal(value);
					SetFrameSlot(dead, RSSYMname, value);
					value = GetFrameSlotRef(wanted, RSSYMmajor);
					SetFrameSlot(dead, RSSYMmajor, value);
					value = GetFrameSlotRef(wanted, RSSYMminor);
					SetFrameSlot(dead, RSSYMminor, value);
					value = client;
					SetFrameSlot(dead, RSSYMclient, value);
					AddArraySlot(deadImports, dead);
				}
				const char* name = SymbolName(GetFrameSlotRef(wanted, RSSYMname));
				Long major = RINT(GetFrameSlotRef(wanted, RSSYMmajor));
				Long minor = RINT(GetFrameSlotRef(wanted, RSSYMminor));
				RegisterPendingImport(import, slot, name, major, minor);
				FlushPackageCache(import->fPackage);
			}
		}
		FreeExportTable(item);
		gMPExportList->RemoveElementsAt(iter.CurrentIndex(), 1);
		if (RomBugFixed())
			free(item);
	}
	return deadImports;
}


/*------------------------------------------------------------------------------
	I m p o r t   t a b l e s
------------------------------------------------------------------------------*/

// ROM 0x000d02a0 InstallImportTable__FUlRC6RefVarPvl
// What the part imports recorded: each unit's slot pointed at the export
// of the same name (ignoring case) and major version with the highest
// minor version at least the one asked for, or made a pending import
// when there is none.
// ROM BUG (fixed): the item is put on the import list with InsertUnique,
// and a part already there (which can only be this part installed twice)
// throws out-of-memory; the pending imports registered meanwhile are left
// pointing at the item the handler frees.  The fix: whatever throws, what
// the item did is undone before it is freed - its pending imports
// forgotten and each export it took one client the fewer (its table copy
// freed at none); the part installed twice is still refused.
void
InstallImportTable(ULong package, RefArg importTable, void* source, long size)
{
	if (gMPExportList == nil)
		InitMPTableRegistry();
	long count = Length(importTable);
	MPImportItem* import = (MPImportItem*) malloc(offsetof(MPImportItem, fExports) + count * sizeof(MPExportItem*));	// DEVIATION: host field sizes
	if (import == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	import->fPackage = package;
	import->fSource = source;
	import->fEnd = (char*) source + size;
	import->fImportTable = importTable;
	import->fCount = count;
	if (RomBugFixed())
	{
		for (long slot = 0; slot < count; slot++)
			import->fExports[slot] = nil;
	}
	newton_try
	{
		RefVar wanted;
		for (long slot = 0; slot < count; slot++)
		{
			wanted = GetArraySlotRef(importTable, slot);
			const char* name = SymbolName(GetFrameSlotRef(wanted, RSSYMname));
			Long major = RINT(GetFrameSlotRef(wanted, RSSYMmajor));
			Long minor = RINT(GetFrameSlotRef(wanted, RSSYMminor));
			MPExportItem* best = nil;
			long bestMinor = -1;
			long exports = gMPExportList->GetArraySize();
			for (long i = 0; i < exports; i++)
			{
				MPExportItem* item = (MPExportItem*) gMPExportList->At(i);
				if (symcmp(item->fName, (char*) name) == 0 && item->fMajor == major
				 && bestMinor < item->fMinor && minor <= item->fMinor)
				{
					best = item;
					bestMinor = item->fMinor;
				}
			}
			if (best == nil)
			{
				import->fExports[slot] = nil;
				RegisterPendingImport(import, slot, name, major, minor);
			}
			else
			{
				if (best->fClients == 0)
					AllocateExportTable(best);
				import->fExports[slot] = best;
				best->fClients++;
			}
		}
		if (!gMPImportList->InsertUnique(import))
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
	cleanup
	{
		if (RomBugFixed())
		{
			RemovePendingImportsOf(import);
			for (long slot = 0; slot < count; slot++)
			{
				MPExportItem* item = import->fExports[slot];
				if (item != nil && --item->fClients == 0)
					FreeExportTable(item);
			}
		}
		free(import);
	}
	end_try;
}


// ROM 0x000d0588 RemoveImportTable__FPv
// What the part imports forgotten: its pending imports, and each export
// it used one client the fewer (its copy of the table freed at none).
// ROM BUG (fixed): the import item is taken off the list but never freed.
// The fix: it is freed once it is off the list (its pending imports went
// first).
void
RemoveImportTable(void* source)
{
	RemovePendingImports(source);
	if (gMPImportList == nil)
		return;
	CMPImportListSourceTester tester;
	tester.SetTestItem(source);
	ArrayIndex index = 0;
	MPImportItem* import;
	while ((import = (MPImportItem*) gMPImportList->Search(&tester, index)) != nil)
	{
		for (long slot = 0; slot < import->fCount; slot++)
		{
			MPExportItem* item = import->fExports[slot];
			if (item != nil)
			{
				if (--item->fClients == 0)
					FreeExportTable(item);
			}
		}
		gMPImportList->RemoveElementsAt(index, 1);
		if (RomBugFixed())
			free(import);
	}
}


// ROM 0x000d0664 ResolveImportRef__FPlPPv
// An import ref resolved where it lies: a magic pointer whose table is 2
// or more names the part's import slot (table - 2) and the object's index
// in the unit ((ref >> 2) & 0xfff).  The import table is the one whose
// part the ref lies in (found unless *importItem already says).  A ref
// that cannot be resolved - no import table, no unit yet, no such object
// - has its top bit set, a magic pointer no table has, which throws
// kNSErrBadMagicPointer when it is used.  Tables 0 and 1 are the ROM's
// and are left alone.
void
ResolveImportRef(Ref* ref, void** importItem)
{
	ULong32 word = (ULong32) *ref;
	ULong32 table = word >> 14;
	if (table < 2)
		return;
	long slot = (long) table - 2;
	MPImportItem* import = (MPImportItem*) *importItem;
	if (import == nil && gMPImportList != nil)
	{
		long count = gMPImportList->GetArraySize();
		for (long i = 0; i < count; i++)
		{
			MPImportItem* candidate = (MPImportItem*) gMPImportList->At(i);
			if ((void*) ref >= candidate->fSource && (void*) ref < candidate->fEnd)
			{
				import = candidate;
				break;
			}
		}
	}
	if (import != nil)
	{
		*importItem = import;
		if (slot < import->fCount)
		{
			MPExportItem* item = import->fExports[slot];
			ULong32 index = (word >> 2) & 0xfff;
			if (item != nil && (long) index < item->fCount)
			{
				*ref = item->fTable[index];
				return;
			}
		}
	}
	*ref = (Ref) (Long) (int) (word | 0x80000000);		// (as the importer widens a word: sign-extended)
}


/*------------------------------------------------------------------------------
	T h e   R O M   d o m a i n ' s   p a g e s   (host)
------------------------------------------------------------------------------*/

// the imported parts whose imports FlushPackageCache relocates
struct UnitArea
{
	TImportedObjectArea*	fArea;
	UnitArea*				fNext;
};
static UnitArea*	gUnitAreas = nil;


void
RegisterUnitArea(TImportedObjectArea* area)
{
	UnitArea* entry = new UnitArea;
	entry->fArea = area;
	entry->fNext = gUnitAreas;
	gUnitAreas = entry;
}


void
UnregisterUnitArea(TImportedObjectArea* area)
{
	for (UnitArea** link = &gUnitAreas; *link != nil; link = &(*link)->fNext)
	{
		if ((*link)->fArea == area)
		{
			UnitArea* doomed = *link;
			*link = doomed->fNext;
			delete doomed;
			return;
		}
	}
}


// host: the import refs of an imported part resolved (DEVIATION, Units.h):
// the half of RelocateFramesInPage (0x000d1b50) that concerns them, over
// every object at once - an object's class, and a slotted object's slots,
// each word that was a magic pointer in the package put back as it was
// there and handed to ResolveImportRef (whose import item is looked for
// afresh for each ref, as the ROM's is).
void
RelocateImportRefs(TImportedObjectArea* area)
{
	if (area == nil || area->fBytes == nil)
		return;
	for (long i = 0; i < area->fCount; i++)
	{
		ObjHeader* o = area->fObjects[i].fObject;
		const UByte* source = area->fBytes + (area->fObjects[i].fAddress - area->fBase);
		ULong32 header = GetBigEndianWord(source);
		ULong32 size = header >> 8;
		long words = 1;								// the class
		if ((header & kObjSlotted) != 0)
			words = (long) ((size - kARMObjHeaderSize) / kARMWord);
		for (long j = 0; j < words; j++)
		{
			ULong32 word = GetBigEndianWord(source + kARMObjHeaderSize + j * kARMWord);
			if ((word & 3) != kTagMagicPtr)
				continue;
			Ref* slot = &ObjSlots(o)[j];
			*slot = (Ref) (Long) (int) word;
			void* importItem = nil;
			ResolveImportRef(slot, &importItem);
		}
	}
}


// ROM 0x000cfaec FlushPackageCache__FUl
// The package's pages thrown away by the ROM domain manager (and the
// instruction cache cleared), so that they are relocated - their imports
// resolved - again when they are next touched.  Nothing for 0.
// DEVIATION: the host's "package" is an imported part's area (Units.h),
// whose import refs are resolved again at once.
void
FlushPackageCache(ULong package)
{
	if (package == 0)
		return;
	for (UnitArea* entry = gUnitAreas; entry != nil; entry = entry->fNext)
	{
		if ((ULong) entry->fArea == package)
			RelocateImportRefs(entry->fArea);
	}
	ICacheClear();
}


/*------------------------------------------------------------------------------
	T h e   R O M   e x t e n s i o n s '   t a b l e s
------------------------------------------------------------------------------*/

// ROM 0x000d1038 InitRExMagicPointerTables__Fv
// For each ROM extension: how many objects it exports (its 'fexp' entry,
// magic pointer table 2 + 2 x its id) and, when it imports ('fimp': a
// count of objects, then records of {major, minor, -, count, name}, each
// padded to a word), a table of that many refs (magic pointer table 3 +
// 2 x its id), nil until each record's unit is exported and fills in its
// run of it.
// DEVIATION: the host keeps a REx's export table as refs of its own,
// filled in as each part of the extension is imported
// (FramePartHandler.cpp's TranslateROMExports); here it is made, nil.
// The records' words are big-endian, as the ROM image has them.
void
InitRExMagicPointerTables(void)
{
	for (ULong id = 0; id < kMaxROMExtensions; id++)
	{
		ULong size = 0x3ff;
		VAddr exports = GetRExConfigEntry(id, 'fexp', &size);
		long count = exports != 0 ? (long) (size >> 2) : 0;
		long which = 2 + 2 * (long) id;
		if (which + 1 >= kMagicPointerTables)
			break;
		if (count != 0 && (gMagicPointerTables[which] == nil || gMagicPointerTableCounts[which] != count))
		{
			Ref* entries = new Ref[count];
			for (long i = 0; i < count; i++)
				entries[i] = NILREF;
			delete[] gMagicPointerTables[which];
			gMagicPointerTables[which] = entries;
		}
		gMagicPointerTableCounts[which] = count;
		VAddr imports = GetRExConfigEntry(id, 'fimp', &size);
		if (imports == 0 || size == 0)
			continue;
		const UByte* record = (const UByte*) imports;
		const UByte* end = record + size;
		long objects = (long) GetBigEndianWord(record);
		record += 4;
		Ref* table = new Ref[objects];
		if (table == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		gMagicPointerTableCounts[which + 1] = objects;
		gMagicPointerTables[which + 1] = table;
		for (long i = 0; i < objects; i++)
			table[i] = NILREF;
		while (record < end)
		{
			const char* name = (const char*) record + 0x10;
			RegisterPendingImport(record, table, name, (long) GetBigEndianWord(record), (long) GetBigEndianWord(record + 4));
			const UByte* next = record + ((strlen(name) + 0x14) & ~3);
			table += (long) GetBigEndianWord(record + 0x0C);
			record = next;
		}
	}
}


/*------------------------------------------------------------------------------
	N a t i v e s
------------------------------------------------------------------------------*/

// The store entry of the package an import came from: the ROM hands the
// package's address to GetEntryFromLargeObjectVAddr, which answers the
// large binary (the package's pkgRef) mapped there - nil for a package
// that is not on a store.  DEVIATION: the host's import names the part's
// imported area (see TFramePartHandler::Install), so the address is that
// of the bytes it was imported from, taken back to the start of the large
// object they lie in.
static Ref
PackageEntryOf(ULong package)
{
	if (package == 0)
		return NILREF;
	ULong base;
	if (VAddrToBase(&base, (ULong) ((TImportedObjectArea*) package)->fBytes) != noErr)
		return NILREF;
	return GetEntryFromLargeObjectVAddr(base);
}


// ROM 0x000d0758 FCurrentExports
// [{name, major, minor, refCount, exportTable: the objects array}, ...]
static Ref
FCurrentExports(RefArg /*rcvr*/)
{
	long count = gMPExportList != nil ? gMPExportList->GetArraySize() : 0;
	RefVar result(AllocateArray(RSSYMarray, count));
	RefVar entry;
	RefVar value;
	for (long i = 0; i < count; i++)
	{
		MPExportItem* item = (MPExportItem*) gMPExportList->At(i);
		entry = Clone(Rcanonicalcurrentexport);
		value = Intern(item->fName);
		SetFrameSlot(entry, RSSYMname, value);
		value = MAKEINT(item->fMajor);
		SetFrameSlot(entry, RSSYMmajor, value);
		value = MAKEINT(item->fMinor);
		SetFrameSlot(entry, RSSYMminor, value);
		value = MAKEINT(item->fClients);
		SetFrameSlot(entry, RSSYMrefcount, value);
		// (the ROM makes the array's ref from its slots: slot 0 - 0xc + 1)
		value = MAKEPTR((char*) item->fOriginalTable - (sizeof(ObjHeader) + sizeof(Ref)));
		SetFrameSlot(entry, RSSYMexporttable, value);
		SetArraySlotRef(result, i, entry);
	}
	return result;
}


// ROM 0x000d092c FCurrentImports
// [{importTable, client: the package's store entry}, ...]
static Ref
FCurrentImports(RefArg /*rcvr*/)
{
	long count = gMPImportList != nil ? gMPImportList->GetArraySize() : 0;
	RefVar result(AllocateArray(RSSYMarray, count));
	RefVar entry;
	RefVar value;
	for (long i = 0; i < count; i++)
	{
		MPImportItem* import = (MPImportItem*) gMPImportList->At(i);
		entry = Clone(Rcanonicalcurrentimport);
		value = import->fImportTable;
		SetFrameSlot(entry, RSSYMimporttable, value);
		value = PackageEntryOf(import->fPackage);
		SetFrameSlot(entry, RSSYMclient, value);
		SetArraySlotRef(result, i, entry);
	}
	return result;
}


// ROM 0x000d0a48 FPendingImports
// [{name (a string), major, minor, client (a package's)}, ...]
static Ref
FPendingImports(RefArg /*rcvr*/)
{
	RefVar result(AllocateArray(RSSYMarray, 0));
	RefVar entry;
	RefVar value;
	for (MPPendingImport* pending = gMPPendingImports; pending != nil; pending = pending->fNext)
	{
		entry = Clone(Rcanonicalpendingimport);
		value = MakeString(pending->fName);
		SetFrameSlot(entry, RSSYMname, value);
		value = MAKEINT(pending->fMajor);
		SetFrameSlot(entry, RSSYMmajor, value);
		value = MAKEINT(pending->fMinor);
		SetFrameSlot(entry, RSSYMminor, value);
		if (pending->fKind == 0)
		{
			value = PackageEntryOf(((MPImportItem*) pending->fImport)->fPackage);
			SetFrameSlot(entry, RSSYMclient, value);
		}
		AddArraySlot(result, entry);
	}
	return result;
}


// ROM 0x000d0bbc FFlushImports
// Every importing package's pages flushed.
static Ref
FFlushImports(RefArg /*rcvr*/)
{
	if (gMPImportList != nil)
	{
		long count = gMPImportList->GetArraySize();
		for (long i = 0; i < count; i++)
			FlushPackageCache(((MPImportItem*) gMPImportList->At(i))->fPackage);
	}
	return NILREF;
}


// ROM 0x000d0c14 FGetExportTableClients
// The parts that import the unit whose objects array is given:
// [{name, major, minor, client: the importing part's frame}, ...]
static Ref
FGetExportTableClients(RefArg /*rcvr*/, RefArg objects)
{
	Ref* table = Slots(objects);
	RefVar result(AllocateArray(RSSYMarray, 0));
	RefVar wanted;
	RefVar entry;
	RefVar client;
	RefVar value;
	CListIterator iter(gMPExportList);
	for (MPExportItem* item = (MPExportItem*) iter.FirstItem(); iter.More(); item = (MPExportItem*) iter.NextItem())
	{
		if (item->fOriginalTable != table)
			continue;
		long imports = gMPImportList->GetArraySize();
		for (long i = 0; i < imports; i++)
		{
			MPImportItem* import = (MPImportItem*) gMPImportList->At(i);
			for (long slot = 0; slot < import->fCount; slot++)
			{
				if (import->fExports[slot] != item)
					continue;
				client = FramePartToplevelFrame(import->fSource);
				if (ISNIL(client))
					continue;
				wanted = GetArraySlotRef(import->fImportTable, slot);
				entry = Clone(Rcanonicalexporttableclient);
				value = GetFrameSlotRef(wanted, RSSYMname);
				SetFrameSlot(entry, RSSYMname, value);
				value = GetFrameSlotRef(wanted, RSSYMmajor);
				SetFrameSlot(entry, RSSYMmajor, value);
				value = GetFrameSlotRef(wanted, RSSYMminor);
				SetFrameSlot(entry, RSSYMminor, value);
				SetFrameSlot(entry, RSSYMclient, client);
				AddArraySlot(result, entry);
			}
		}
	}
	return result;
}


// ROM 0x000d0eb8 FFulfillImportTable
// The pending imports of the import table given (every one, for nil)
// offered every export.  ==> true, nil when nothing exports yet.
static Ref
FFulfillImportTable(RefArg /*rcvr*/, RefArg importTable)
{
	if (gMPExportList == nil)
		return NILREF;
	CListIterator iter(gMPExportList);
	for (MPExportItem* item = (MPExportItem*) iter.FirstItem(); iter.More(); item = (MPExportItem*) iter.NextItem())
		FulfillPendingImports(item, importTable);
	return TRUEREF;
}


void
RegisterPackageUnitNatives(void)
{
	RegisterNativeFunction("FCurrentExports", (void*) FCurrentExports, 0);
	RegisterNativeFunction("FCurrentImports", (void*) FCurrentImports, 0);
	RegisterNativeFunction("FPendingImports", (void*) FPendingImports, 0);
	RegisterNativeFunction("FFlushImports", (void*) FFlushImports, 0);
	RegisterNativeFunction("FGetExportTableClients", (void*) FGetExportTableClients, 1);
	RegisterNativeFunction("FFulfillImportTable", (void*) FFulfillImportTable, 1);
}
