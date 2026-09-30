/*
	File:		packages/tests/test_Units.cpp

	Contains:	Host tests for the units (Units.h): two frames parts built
				here in the MessagePad's layout - one exporting a unit
				'client 1.2 of two objects, one importing 'client 1.0 and
				referring to both through magic pointers - imported as the
				frame part handler imports a package's part, and their
				unit tables installed and removed in both orders; the
				import refs resolved, turned back into bad magic pointers
				when the export goes, and resolved again when it comes
				back.  Also the ROM extension's export count, which
				InitObjects has InitRExMagicPointerTables set.  Uses the
				ROM image for the symbols the parts refer to.
*/

#include "Units.h"
#include "FramePartHandler.h"
#include "Interpreter.h"
#include "SortedList.h"
#include "FramesPart.h"
#include "ObjectAreaImport.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "RSSymbols.h"
#include "ByteOrder.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>		// (not <vector>: libc++'s locale support finds intl/Locale.h)

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


/*------------------------------------------------------------------------------
	A   f r a m e s   p a r t   i n   t h e   M e s s a g e P a d ' s   l a y o u t
------------------------------------------------------------------------------*/

// the ROM's symbols, by their ROM refs (RSSymbolTable.cpp)
const ULong32 kROMSymArray = 0x003b0039;
const ULong32 kROMSymString = 0x003c13a5;
const ULong32 kROMSymName = 0x003bf1a9;
const ULong32 kROMSymMajor = 0x00557af1;
const ULong32 kROMSymMinor = 0x00557b09;
const ULong32 kROMSymObjects = 0x00535991;
const ULong32 kROMSymClient = 0x00557ab9;
const ULong32 kROMSymExportTable = 0x00559489;
const ULong32 kROMSymImportTable = 0x00557b4d;
const ULong32 kROMSymX = 0x003c2a75;
const ULong32 kROMSymY = 0x003c2b05;

inline ULong32 ARMInt(long n)				{ return (ULong32) (n << 2); }
inline ULong32 ARMMagic(ULong32 value)		{ return (value << 2) | 3; }
const ULong32 kARMNil = 2;

// a part's bytes
struct PartBytes
{
	unsigned char	fData[1024];
	long			fSize;

	PartBytes() : fSize(0) { }
	void			push_back(unsigned char c)	{ fData[fSize++] = c; }
	long			size() const				{ return fSize; }
	const unsigned char* data() const			{ return fData; }
	unsigned char&	operator[](long i)			{ return fData[i]; }
};

class PartBuilder
{
public:
	PartBuilder(ULong32 base) : fBase(base) { }

	// an object: its header, the GC word, the class and the rest; ==> its ref
	ULong32 Object(ULong32 flags, ULong32 klass, std::initializer_list<ULong32> words)
	{
		ULong32 at = (ULong32) fBytes.size();
		ULong32 size = 12 + 4 * (ULong32) words.size();
		Word((size << 8) | flags);
		Word(0);
		Word(klass);
		for (ULong32 w : words)
			Word(w);
		return fBase + at + 1;
	}
	// the same, with a first word before the rest (a map's supermap)
	ULong32 ObjectOf(ULong32 flags, ULong32 klass, ULong32 first, std::initializer_list<ULong32> words)
	{
		ULong32 at = (ULong32) fBytes.size();
		ULong32 size = 16 + 4 * (ULong32) words.size();
		Word((size << 8) | flags);
		Word(0);
		Word(klass);
		Word(first);
		for (ULong32 w : words)
			Word(w);
		return fBase + at + 1;
	}
	ULong32 Array(std::initializer_list<ULong32> slots)	{ return Object(kObjSlotted, kROMSymArray, slots); }
	ULong32 Frame(std::initializer_list<ULong32> tags, std::initializer_list<ULong32> values)
	{
		ULong32 mapRef = ObjectOf(kObjSlotted, ARMInt(0), kARMNil, tags);		// (no supermap)
		return Object(kObjSlotted | kObjFrame, mapRef, values);
	}
	ULong32 String(const char* s)
	{
		ULong32 at = (ULong32) fBytes.size();
		ULong32 length = (ULong32) (strlen(s) + 1) * 2;
		ULong32 size = 12 + length;
		Word((size << 8) | 0);
		Word(0);
		Word(kROMSymString);
		for (const char* p = s; ; p++)
		{
			fBytes.push_back(0);
			fBytes.push_back((unsigned char) *p);
			if (*p == 0)
				break;
		}
		while (fBytes.size() % 4 != 0)
			fBytes.push_back(0);
		return fBase + at + 1;
	}
	// a slot of an object already made (its first word after the class is slot 0)
	void SetSlot(ULong32 ref, long slot, ULong32 value)
	{
		ULong32 at = ref - 1 - fBase + 12 + 4 * (ULong32) slot;
		fBytes[at] = (unsigned char) (value >> 24);
		fBytes[at + 1] = (unsigned char) (value >> 16);
		fBytes[at + 2] = (unsigned char) (value >> 8);
		fBytes[at + 3] = (unsigned char) value;
	}

	PartBytes		fBytes;
	ULong32						fBase;

private:
	void Word(ULong32 w)
	{
		fBytes.push_back((unsigned char) (w >> 24));
		fBytes.push_back((unsigned char) (w >> 16));
		fBytes.push_back((unsigned char) (w >> 8));
		fBytes.push_back((unsigned char) w);
	}
};


// The exporter: [ {_ExportTable: [ {name: 'client, major: 1, minor: 2,
// objects: ["hello", 42]} ]} ]
static PartBytes
ExporterPart(ULong32 base)
{
	PartBuilder b(base);
	ULong32 top = b.Array({ kARMNil });
	ULong32 objects = b.Array({ b.String("hello"), ARMInt(42) });
	ULong32 unit = b.Frame({ kROMSymName, kROMSymMajor, kROMSymMinor, kROMSymObjects },
						   { kROMSymClient, ARMInt(1), ARMInt(2), objects });
	ULong32 table = b.Array({ unit });
	ULong32 frame = b.Frame({ kROMSymExportTable }, { table });
	b.SetSlot(top, 0, frame);
	return b.fBytes;
}


// The importer: [ {_ImportTable: [ {name: 'client, major: 1, minor:
// wanted} ], x: @(2 << 12 | 0), y: @(2 << 12 | 1)} ] - x and y are the
// unit's first and second objects
static PartBytes
ImporterPart(ULong32 base, long wantedMinor)
{
	PartBuilder b(base);
	ULong32 top = b.Array({ kARMNil });
	ULong32 wanted = b.Frame({ kROMSymName, kROMSymMajor, kROMSymMinor }, { kROMSymClient, ARMInt(1), ARMInt(wantedMinor) });
	ULong32 table = b.Array({ wanted });
	ULong32 frame = b.Frame({ kROMSymImportTable, kROMSymX, kROMSymY }, { table, ARMMagic(2 << 12 | 0), ARMMagic(2 << 12 | 1) });
	b.SetSlot(top, 0, frame);
	return b.fBytes;
}


// A part imported and its unit tables installed as TFramePartHandler::Install
// does for a part outside the ROM.
static TImportedObjectArea*
Install(const PartBytes& bytes, ULong32 base)
{
	TImportedObjectArea* area = ImportFramesPart(bytes.data(), (ULong) bytes.size(), base);
	EXPECT(area != nil);
	if (area == nil)
		return nil;
	RefVar frame(FramePartToplevelFrame(area->fArea));
	EXPECT(IsFrame(frame));
	RefVar exports(GetFrameSlotRef(frame, RSSYM_exporttable));
	if (NOTNIL(exports))
		InstallExportTables(exports, area->fArea);
	RefVar imports(GetFrameSlotRef(frame, RSSYM_importtable));
	if (NOTNIL(imports))
	{
		RegisterUnitArea(area);
		InstallImportTable((ULong) area, imports, area->fArea, area->fAreaEnd - area->fArea);
		FlushPackageCache((ULong) area);
	}
	return area;
}


// ==> the dead imports
static Ref
Remove(TImportedObjectArea* area)
{
	RefVar dead(RemoveExportTables(area->fArea));
	RemoveImportTable(area->fArea);
	UnregisterUnitArea(area);
	return dead;
}


static Boolean
IsString(Ref r, const char* s)
{
	if (!ISPTR(r) || !IsString(RefVar(r)))
		return false;
	char buf[64];
	UniChar* u = (UniChar*) BinaryData(r);
	long i = 0;
	for ( ; u[i] != 0 && i < 63; i++)
		buf[i] = (char) u[i];
	buf[i] = 0;
	return strcmp(buf, s) == 0;
}


// an import ref left unresolved: a magic pointer with its top bit set
static Boolean
IsUnresolved(Ref r)
{
	return RTAG(r) == kTagMagicPtr && ((ULong32) r & 0x80000000) != 0;
}


static void
TestUnits(void)
{
	const ULong32 kExporterBase = 0x04000000, kImporterBase = 0x05000000;
	PartBytes exporterBytes = ExporterPart(kExporterBase);
	PartBytes importerBytes = ImporterPart(kImporterBase, 0);

	// the exporter first: the importer's refs resolved when it arrives
	TImportedObjectArea* exporter = Install(exporterBytes, kExporterBase);
	EXPECT(gMPExportList != nil && gMPExportList->GetArraySize() >= 1);
	MPExportItem* item = nil;
	for (ArrayIndex i = 0; i < gMPExportList->GetArraySize(); i++)
	{
		MPExportItem* e = (MPExportItem*) gMPExportList->At(i);
		if (strcmp(e->fName, "client") == 0)
			item = e;
	}
	EXPECT(item != nil && item->fMajor == 1 && item->fMinor == 2 && item->fCount == 2 && item->fClients == 0 && !item->fAllocated);
	TImportedObjectArea* importer = Install(importerBytes, kImporterBase);
	RefVar frame(FramePartToplevelFrame(importer->fArea));
	EXPECT(IsString(GetFrameSlotRef(frame, RSSYMx), "hello"));
	EXPECT(GetFrameSlotRef(frame, RSSYMy) == MAKEINT(42));
	EXPECT(item->fClients == 1);
	EXPECT(gMPPendingImports == nil);

	// the exporter goes: the importer is reported and its refs no longer resolve
	RefVar dead(Remove(exporter));
	EXPECT(IsArray(dead) && Length(dead) == 1);
	if (Length(dead) == 1)
	{
		RefVar d(GetArraySlotRef(dead, 0));
		EXPECT(EQRef(GetFrameSlotRef(d, RSSYMname), RSSYMclient));
		EXPECT(GetFrameSlotRef(d, RSSYMmajor) == MAKEINT(1) && GetFrameSlotRef(d, RSSYMminor) == MAKEINT(0));
		EXPECT(GetFrameSlotRef(d, RSSYMclient) == (Ref) frame);
	}
	EXPECT(IsUnresolved(GetFrameSlotRef(frame, RSSYMx)) && IsUnresolved(GetFrameSlotRef(frame, RSSYMy)));
	EXPECT(gMPPendingImports != nil && gMPPendingImports->fKind == 0 && gMPPendingImports->fNext == nil
		&& strcmp(gMPPendingImports->fName, "client") == 0);
	RemoveFramesPart(exporter);

	// it comes back: the pending import fulfilled, the refs resolved again
	exporter = Install(exporterBytes, kExporterBase);
	EXPECT(gMPPendingImports == nil);
	EXPECT(IsString(GetFrameSlotRef(frame, RSSYMx), "hello") && GetFrameSlotRef(frame, RSSYMy) == MAKEINT(42));

	// the importer goes, then the exporter: nothing dead to report
	dead = Remove(importer);
	EXPECT(Length(dead) == 0);
	RemoveFramesPart(importer);
	dead = Remove(exporter);
	EXPECT(Length(dead) == 0);
	RemoveFramesPart(exporter);

	// the importer first: pending until the exporter arrives
	importer = Install(importerBytes, kImporterBase);
	frame = FramePartToplevelFrame(importer->fArea);
	EXPECT(IsUnresolved(GetFrameSlotRef(frame, RSSYMx)));
	EXPECT(gMPPendingImports != nil);
	// (the exporter may be given the importer's old addresses: RemoveFramesPart
	// must have cleared the find-offset cache, or the exporter's frame
	// answers the old importer map's slots)
	exporter = Install(exporterBytes, kExporterBase);
	EXPECT(gMPPendingImports == nil);
	EXPECT(IsString(GetFrameSlotRef(frame, RSSYMx), "hello"));
	Remove(importer);
	RemoveFramesPart(importer);

	// a newer minor version than the export's is not satisfied by it
	PartBytes newerBytes = ImporterPart(kImporterBase, 3);
	importer = Install(newerBytes, kImporterBase);
	frame = FramePartToplevelFrame(importer->fArea);
	EXPECT(IsUnresolved(GetFrameSlotRef(frame, RSSYMy)));
	EXPECT(gMPPendingImports != nil && gMPPendingImports->fMinor == 3);
	Remove(importer);
	EXPECT(gMPPendingImports == nil);			// RemovePendingImports took it
	RemoveFramesPart(importer);
	Remove(exporter);
	RemoveFramesPart(exporter);
}


// ResolveImportRef on its own: tables 0 and 1 left alone, a ref in no
// import table's part marked
static void
TestResolve(void)
{
	Ref rom = MAKEMAGICPTR(0 << 12 | 5);
	void* cache = nil;
	ResolveImportRef(&rom, &cache);
	EXPECT(rom == MAKEMAGICPTR(5));
	Ref global = MAKEMAGICPTR(1 << 12 | 2);
	ResolveImportRef(&global, &cache);
	EXPECT(global == MAKEMAGICPTR(1 << 12 | 2));
	Ref stray = MAKEMAGICPTR(2 << 12 | 0);
	ResolveImportRef(&stray, &cache);
	EXPECT(IsUnresolved(stray) && cache == nil);
}


/*------------------------------------------------------------------------------
	T h e   ' c o m m   p a r t   h a n d l e r
------------------------------------------------------------------------------*/

// stand-ins for the globals the handler calls
static long gRegistered = 0, gRegisteredLength = 0, gUnregistered = 0, gInstalled = 0, gRemoved = 0;

static Ref
StubRegCommConfigArray(RefArg /*rcvr*/, RefArg configurations)
{
	gRegistered++;
	gRegisteredLength = Length(configurations);
	return NILREF;
}

static Ref
StubUnRegCommConfigArray(RefArg /*rcvr*/, RefArg /*configurations*/)
{
	gUnregistered++;
	return NILREF;
}

static Ref
StubInstallPart(RefArg /*rcvr*/, RefArg /*installInfo*/)
{
	gInstalled++;
	return MAKEINT(7);
}

static Ref
StubRemovePart(RefArg /*rcvr*/, RefArg /*removeInfo*/, RefArg /*cookie*/)
{
	gRemoved++;
	return NILREF;
}

// the handler with the remove object Install would have made for it
class TTestCommPartHandler : public TCommPartHandler
{
public:
	void	Prime()
	{
		fRemoveObject = new FramePartRemoveObject;
		fRemoveObject->fObject = new RefStruct(NILREF);
		fRemoveObject->fData = nil;
		fRemoveObject->fArea = nil;
	}
	Ref		SavedObject()	{ return *fRemoveObject->fObject; }
};


static void
TestCommPartHandler(void)
{
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RSSYMregcommconfigarray, RefVar(MakeCFunction((void*) StubRegCommConfigArray, 1, nil)));
	SetFrameSlot(functions, RSSYMunregcommconfigarray, RefVar(MakeCFunction((void*) StubUnRegCommConfigArray, 1, nil)));
	SetFrameSlot(functions, RSSYMinstallpart, RefVar(MakeCFunction((void*) StubInstallPart, 1, nil)));
	SetFrameSlot(functions, RSSYMremovepart, RefVar(MakeCFunction((void*) StubRemovePart, 2, nil)));

	// (never destroyed: a part handler unregisters from the package
	// manager, which is not running here)
	TTestCommPartHandler& handler = *new TTestCommPartHandler;
	handler.Prime();
	RefVar frame(AllocateFrame());
	RefVar configurations(MakeArray(2));
	SetFrameSlot(frame, RSSYMconfigurations, configurations);
	ExtendedPartInfo info;
	memset(&info, 0, sizeof(info));
	SourceType source = { kFixedMemory, kNoDevice, 0, 0 };
	PartId partId = { 1, 0 };

	// installed: the configurations registered, then the part as an 'auto part
	EXPECT(handler.InstallFrame(frame, partId, source, &info) == noErr);
	EXPECT(gRegistered == 1 && gRegisteredLength == 2 && gInstalled == 1);
	// removed: the configurations looked for in the remove object, which has
	// none (the ROM's bug) - so never unregistered - and the part removed
	RefVar saved(handler.SavedObject());
	EXPECT(IsFrame(saved) && ISNIL(GetFrameSlotRef(saved, RSSYMconfigurations)));
	EXPECT(handler.RemoveFrame(saved, partId, 'comm') == noErr);
	EXPECT(gUnregistered == 0 && gRemoved == 1);
	// a remove object that did have them would unregister them
	SetFrameSlot(saved, RSSYMconfigurations, configurations);
	EXPECT(handler.RemoveFrame(saved, partId, 'comm') == noErr);
	EXPECT(gUnregistered == 1 && gRemoved == 2);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_Units: cannot read %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	// InitObjects ran InitRExMagicPointerTables: the extension's 166 exports
	EXPECT(gMagicPointerTableCounts[2] == 166 && gMagicPointerTables[2] != nil);

	TestResolve();
	TestUnits();
	TestCommPartHandler();
	if (failures == 0)
		printf("test_Units: all passed\n");
	return failures != 0;
}
