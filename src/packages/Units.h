/*
	File:		packages/Units.h

	Contains:	Units: the objects one package lends another.  A frames part
				(NTK's partFrame) may carry an `_ExportTable` - an array of
				unit frames {name: 'someUnit, major, minor, objects: [...]}
				- and an `_ImportTable` - an array of {name, major, minor}
				naming the units it uses.  An imported object is written in
				the importer as a magic pointer whose table is 2 + the unit's
				slot in the import table and whose index is the object's in
				the unit's `objects` array (ResolveImportRef).

				InstallExportTables records each unit a part exports in
				gMPExportList (sorted by name); InstallImportTable records
				what a part imports in gMPImportList (sorted by the part's
				address), each slot pointing at the best export there is -
				the same name and major version and the highest minor version
				at least the one asked for - or, when there is none yet, a
				pending import (gMPPendingImports) that the next matching
				export fulfils.  RemoveExportTables takes a part's units away
				again, turning each importer's slot back into a pending
				import and answering the importers that lost one
				(canonicalDeadImport frames, which the frame part handler
				hands to ReportDeadUnitImports); RemoveImportTable forgets a
				part's imports.  The ROM's own parts export units too (five
				of them in this ROM); only a part outside the ROM has its
				imports installed.

				On the MessagePad a package lies in the ROM domain's virtual
				memory and its import refs are resolved as each page of it
				is relocated (RelocateFramesInPage over ResolveImportRef);
				FlushPackageCache has the ROM domain manager throw the pages
				away so that they are relocated - and the imports resolved -
				again the next time they are touched.

				DEVIATION: the host has no ROM domain.  A part's objects are
				imported into a host area of their own before the unit tables
				see them (FramePartHandler.h), so the "part" a unit table
				names is the area's first object and its extent the area's;
				a part's import refs are resolved all at once, over the host
				objects' words, by RelocateImportRefs (the import-ref half of
				RelocateFramesInPage), which reads the untouched refs out of
				the package's own bytes each time; an import table's package
				is that area rather than a ROM domain base (0 for a package
				outside the domain on the MessagePad, whose refs are then never
				relocated again), and FlushPackageCache relocates the areas
				of the imports that name it.  The ROM keeps an import slot as
				a pointer to the export's (count, table) pair at item + 4; the
				host keeps the item.

	Reconstructed from the MP2x00 US ROM (0x000cf868-0x000d1038 and
	InitRExMagicPointerTables 0x000d1038); each function cites its origin.
*/

#ifndef __UNITS_H
#define __UNITS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __ITEMCOMPARER_H
#include "ItemComparer.h"
#endif

class CSortedList;
class TImportedObjectArea;

// A unit a part exports (malloc'd with its name after it; the ROM's
// layout, the host's field sizes)
struct MPExportItem
{
	void*		fSource;			// +00 the part that exports it
	long		fCount;				// +04 how many objects
	Ref*		fTable;				// +08 the objects (a copy of them when fAllocated)
	Boolean		fAllocated;			// +0C the table is a malloc'd copy
	Ref*		fOriginalTable;		// +10 the objects array's slots, where the part has them
	long		fMajor;				// +14
	long		fMinor;				// +18
	long		fClients;			// +1C how many import slots point at it
	char		fName[1];			// +20 the unit's name, NUL-terminated
};

// What a part imports
struct MPImportItem
{
	ULong			fPackage;		// +00 the package (the ROM domain's base; DEVIATION on the host: its area)
	void*			fSource;		// +04 the part, from here
	void*			fEnd;			// +08 to here
	Ref				fImportTable;	// +0C the part's _ImportTable
	long			fCount;			// +10 its length
	MPExportItem*	fExports[1];	// +14 one per unit, nil while it is pending
};

// A unit asked for that nothing exports yet.  (The ROM has no name for
// the base the two kinds share; the layout is theirs, 0x20 bytes.)
class MPPendingImport
{
public:
	virtual void	Fulfill(MPExportItem* item) = 0;
	virtual Boolean	Match(void* source) = 0;

	MPPendingImport*	fNext;		// +04
	long			fKind;			// +08 0: a package's import slot, 1: a ROM extension's
	const char*		fName;			// +0C
	long			fMajor;			// +10
	long			fMinor;			// +14
	void*			fImport;		// +18 the MPImportItem, or the ROM extension's import record
	union
	{
		long		fIndex;			// +1C the import slot (a package's)
		Ref*		fTable;			// +1C the table to fill in (a ROM extension's)
	};
};

class PkgPendingImport : public MPPendingImport
{
public:
	virtual void	Fulfill(MPExportItem* item);
	virtual Boolean	Match(void* source);
};

class RExPendingImport : public MPPendingImport
{
public:
	virtual void	Fulfill(MPExportItem* item);
	virtual Boolean	Match(void* source);
};

// the lists' orders
class CMPExportListNameComparer : public CItemComparer
{
public:
	virtual CompareResult	TestItem(const void* criteria) const;
};

class CMPImportListSourceComparer : public CItemComparer
{
public:
	virtual CompareResult	TestItem(const void* criteria) const;
};

class CMPImportListSourceTester : public CItemComparer
{
public:
	virtual CompareResult	TestItem(const void* criteria) const;
};

extern CSortedList*		gMPExportList;			// ROM 0x0c100e00
extern CSortedList*		gMPImportList;			// ROM 0x0c100e04
extern MPPendingImport*	gMPPendingImports;		// ROM 0x0c100e08

void		InitMPTableRegistry(void);																// ROM 0x000d0f48
void		InitRExMagicPointerTables(void);														// ROM 0x000d1038
void		InstallExportTables(RefArg exportTable, void* source);									// ROM 0x000cfcd4
Ref			RemoveExportTables(void* source);														// ROM 0x000cfef8
void		InstallImportTable(ULong package, RefArg importTable, void* source, long size);			// ROM 0x000d02a0
void		RemoveImportTable(void* source);														// ROM 0x000d0588
void		ResolveImportRef(Ref* ref, void** importItem);											// ROM 0x000d0664
void		FlushPackageCache(ULong package);														// ROM 0x000cfaec
void		RegisterPendingImport(MPImportItem* item, long index, const char* name, long major, long minor);		// ROM 0x000cf934
void		RegisterPendingImport(const void* rexImport, Ref* table, const char* name, long major, long minor);	// ROM 0x000cf9c8
void		RemovePendingImports(void* source);														// ROM 0x000cfa5c
void		FulfillPendingImports(MPExportItem* item, Ref importTable);								// ROM 0x000cfb48
void		AllocateExportTable(MPExportItem* item);												// ROM 0x000cfc1c
void		FreeExportTable(MPExportItem* item);													// ROM 0x000cfc9c
Boolean		IsInRDMSpace(ULong address);															// ROM 0x000d1704

// host: the import-ref half of RelocateFramesInPage over an imported part
// (DEVIATION, above), and the areas whose imports FlushPackageCache
// relocates
void		RelocateImportRefs(TImportedObjectArea* area);
void		RegisterUnitArea(TImportedObjectArea* area);
void		UnregisterUnitArea(TImportedObjectArea* area);

void		RegisterPackageUnitNatives(void);			// CurrentExports, CurrentImports, PendingImports, FlushImports, GetExportTableClients, FulfillImportTable

#endif	/* __UNITS_H */
