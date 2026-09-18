// The collation tables (frames/SortTables.h) over the ones the ROM
// carries: the table read (its ranges, its single characters, its
// ligatures and its lowest-sort table), the comparison that walks two
// strings through it, and the registry that holds it.  The MP2x00 US
// ROM has two of them, ids 1 and 7; everything below is about id 1,
// the default.
//
// The table is installed by InitUnicode, which InitObjects runs once the
// ROM's objects are there; before that there is no table and the compare
// falls back to OldCompareText, so both are exercised.

#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "SortTables.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// a UniChar string from an ASCII one, so the tests read
static UniChar	buf[8][64];
static long		next = 0;

static const UniChar*
U(const char* s)
{
	UniChar* p = buf[next++ & 7];
	long i = 0;
	for (; s[i] != 0; i++)
		p[i] = (unsigned char) s[i];
	p[i] = 0;
	return p;
}

static int
Collate(const UniChar* a, const UniChar* b, const TSortingTable* table, Boolean exact)
{
	return CompareUnicodeText(a, Ustrlen(a), b, Ustrlen(b), table, exact, nil, nil);
}


int
main()
{
	InitHostStandaloneHeap();

	// ---- with no table at all: the old case-folding byte compare -------
	EXPECT(gSortTables.fDefaultTable == nil && gSortTables.fDefaultId == 0);
	EXPECT(Collate(U("apple"), U("APPLE"), kDefaultSortTable, false) == 0);
	EXPECT(Collate(U("apple"), U("APPLE"), kDefaultSortTable, true) == 'a' - 'A');
	// the characters' own difference, not just its sign
	EXPECT(Collate(U("ape"), U("apple"), kDefaultSortTable, false) == 'E' - 'P');
	EXPECT(Collate(U("ap"), U("apple"), kDefaultSortTable, false) == -3);

	NewtonErr err = ImportROMObjectsFromFile(NEWTON_ROM_IMAGE);
	if (err != noErr)
	{
		printf("test_SortTables: cannot import %s (%ld)\n", NEWTON_ROM_IMAGE, (long) err);
		return 1;
	}
	gObjectHeapSize = 0x80000;
	InitObjects();

	// ---- the ROM's table registered by InitUnicode ---------------------
	EXPECT(Length(RefVar(Rsorttables)) == 2);
	SortTableEntry* entry = gSortTables.GetTableEntry(1);
	EXPECT(entry != nil && entry->fUsers == 1);
	if (entry == nil)
		return 1;
	const TSortingTable* table = entry->fTable;
	EXPECT(gSortTables.GetSortTable(1, nil) == table);
	EXPECT(gSortTables.GetSortTable(2, nil) == nil);
	// the ROM's other table, of the same shape and ten bytes shorter
	long otherSize = 0;
	EXPECT(gSortTables.GetSortTable(7, &otherSize) != nil && otherSize == 1474);

	// ---- the table's shape ---------------------------------------------
	EXPECT(table->Id() == 1);
	EXPECT(table->NumRanges() == 2);
	EXPECT(table->RangeFirst(0) == 0x0000 && table->RangeLast(0) == 0x007f);
	EXPECT(table->RangeFirst(1) == 0x00a0 && table->RangeLast(1) == 0x00ff);
	EXPECT(table->NumSingles() == 47 && table->NumLigatures() == 7 && table->NumLowest() == 91);
	// the header, both ranges' entries, the singles, the ligatures and the
	// lowest-sort halfwords: 12 bytes short of the binary it lives in
	EXPECT(table->CalcSize() == 1484);
	long size = 0;
	EXPECT(gSortTables.GetSortTable(1, &size) == table && size == 1484);

	// ---- what a character projects to -----------------------------------
	// the ASCII range, indexed straight
	const void* a = table->GetProjectionEntry('a');
	const void* A = table->GetProjectionEntry('A');
	EXPECT(TSortingTable::Projection(a) == 'A' && TSortingTable::Projection(A) == 'A');
	EXPECT(TSortingTable::SecondOrder(a) == 7 && TSortingTable::SecondOrder(A) == 0);
	// the Latin-1 range: a-grave sorts as an A too, with its own second order
	EXPECT(TSortingTable::Projection(table->GetProjectionEntry(0x00e0)) == 'A');
	EXPECT(TSortingTable::SecondOrder(table->GetProjectionEntry(0x00e0)) == 9);
	// above them, the singles, binary searched: a dotless i sorts as an I
	EXPECT(TSortingTable::Projection(table->GetProjectionEntry(0x0131)) == 'I');
	EXPECT(TSortingTable::SecondOrder(table->GetProjectionEntry(0x0131)) == 6);
	// and a character in neither is unknown
	EXPECT(table->GetProjectionEntry(0x2000) == nil);
	// a ligature says so, and the ligature table says what it really is
	EXPECT(TSortingTable::Projection(table->GetProjectionEntry(0x00df)) == kLigatureProjection);
	const void* sharpS = table->GetLigatureEntry(0x00df);
	EXPECT(TSortingTable::LigatureFirst(sharpS) == 's' && TSortingTable::LigatureSecond(sharpS) == 's');
	const void* ae = table->GetLigatureEntry(0x00c6);
	EXPECT(TSortingTable::LigatureFirst(ae) == 'A' && TSortingTable::LigatureSecond(ae) == 'E');
	EXPECT(TSortingTable::LigatureLowest(ae) == 0x00c6);
	// a few characters are ignored altogether
	EXPECT(TSortingTable::Projection(table->GetProjectionEntry(0x00ad)) == 0);
	EXPECT(TSortingTable::Projection(table->GetProjectionEntry(' ')) == ' ');

	// ---- collating through it -------------------------------------------
	// the ROM's compare answers the sign of the first difference, not its size
	EXPECT(Collate(U("ape"), U("apple"), table, false) == -1);
	EXPECT(Collate(U("zoo"), U("apple"), table, false) == 1);
	EXPECT(Collate(U("apple"), U("apple"), table, true) == 0);
	// case and diacriticals are the second order: equal unless exact
	EXPECT(Collate(U("apple"), U("APPLE"), table, false) == 0);
	EXPECT(Collate(U("apple"), U("APPLE"), table, true) == 1);
	EXPECT(Collate(U("APPLE"), U("apple"), table, true) == -1);
	// a shorter string that is a prefix comes first
	EXPECT(Collate(U("ap"), U("apple"), table, false) == -1);
	EXPECT(Collate(U("apple"), U("ap"), table, false) == 1);
	// an empty string is answered before a table is even looked at
	EXPECT(Collate(U(""), U("apple"), table, false) == -5);
	// an ignorable character is skipped on either side
	UniChar soft[4] = { 'a', 0x00ad, 'b', 0 };
	EXPECT(Collate(soft, U("ab"), table, false) == 0);
	EXPECT(Collate(U("ab"), soft, table, false) == 0);
	// ... including one hanging off the end, but only once the two have
	// differed somewhere: with nothing to decide yet the ROM answers on the
	// lengths alone and never looks at what is left over
	UniChar trailing[3] = { 'a', 0x00ad, 0 };
	EXPECT(Collate(trailing, U("a"), table, false) == 1);
	UniChar differed[4] = { 'a', 'B', 0x00ad, 0 };
	EXPECT(Collate(differed, U("ab"), table, false) == 0);
	EXPECT(Collate(differed, U("ab"), table, true) == -1);		// B against b
	// a ligature collates as the two characters it stands for, and only an
	// exact compare notices that it was one
	UniChar strasse[7] = { 's', 't', 'r', 'a', 0x00df, 'e', 0 };
	EXPECT(Collate(strasse, U("strasse"), table, false) == 0);
	EXPECT(Collate(strasse, U("strasse"), table, true) == 1);
	EXPECT(Collate(strasse, U("stratte"), table, false) == -1);
	// a-grave and a sort the same until the second order is asked for
	UniChar agrave[6] = { 0x00e0, 'p', 'p', 'l', 'e', 0 };
	EXPECT(Collate(agrave, U("apple"), table, false) == 0);
	EXPECT(Collate(agrave, U("apple"), table, true) == 1);		// 9 against 7
	EXPECT(Collate(agrave, U("APPLE"), table, true) == 1);		// 9 against 0

	// ---- the lowest sort order, which is what an index stores ------------
	UniChar lowest[6];
	Ustrcpy(lowest, U("Apple"));
	table->ConvertTextToLowestSort(lowest, 5);
	EXPECT(Ustrcmp(lowest, U("APPLE")) == 0);
	UniChar lowest2[6] = { 0x00e0, 'p', 'p', 'l', 'e', 0 };
	table->ConvertTextToLowestSort(lowest2, 5);
	EXPECT(Ustrcmp(lowest2, U("APPLE")) == 0);
	// a character the table does not know is left alone
	UniChar unknown[3] = { 0x2000, 'a', 0 };
	table->ConvertTextToLowestSort(unknown, 2);
	EXPECT(unknown[0] == 0x2000 && unknown[1] == 'A');

	// ---- the registry ----------------------------------------------------
	EXPECT(!gSortTables.AddSortTable(table, false));		// id 1 is taken
	gSortTables.Subscribe(1);
	EXPECT(entry->fUsers == 2);
	gSortTables.Unsubscribe(1);
	EXPECT(entry->fUsers == 1 && entry->fTable == table);
	EXPECT(!gSortTables.SetDefaultTableId(2));				// no such table
	EXPECT(gSortTables.fDefaultId == 0 && gSortTables.fDefaultTable == nil);
	EXPECT(gSortTables.SetDefaultTableId(1));
	EXPECT(gSortTables.fDefaultId == 1 && gSortTables.fDefaultTable == table);
	// and now the default table is what kDefaultSortTable means
	EXPECT(Collate(U("ape"), U("apple"), kDefaultSortTable, false) == -1);
	EXPECT(CompareStringNoCase(U("apple"), U("APPLE")) == 0);
	EXPECT(CompareTextNoCase(U("zoo"), 3, U("apple"), 5) == 1);
	// 0 means none at all, and is always taken
	EXPECT(gSortTables.SetDefaultTableId(0));
	EXPECT(gSortTables.fDefaultTable == nil);
	EXPECT(Collate(U("ape"), U("apple"), kDefaultSortTable, false) == 'E' - 'P');

	// ---- the NewtonScript side --------------------------------------------
	EXPECT(ISNIL(FGetSortID(RefVar(NILREF), RefVar(NILREF))));
	EXPECT(NOTNIL(FSetSortID(RefVar(NILREF), RefVar(MAKEINT(1)))));
	EXPECT(RINT(FGetSortID(RefVar(NILREF), RefVar(NILREF))) == 1);
	EXPECT(ISNIL(FSetSortID(RefVar(NILREF), RefVar(MAKEINT(2)))));
	EXPECT(ISNIL(FSetSortID(RefVar(NILREF), RefVar(MakeString("one")))));
	EXPECT(NOTNIL(FSetSortID(RefVar(NILREF), RefVar(NILREF))));
	EXPECT(gSortTables.fDefaultId == 0);

	printf("test_SortTables: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
