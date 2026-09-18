/*
	File:		frames/SortTables.cpp

	Contains:	The collation tables (SortTables.h): a sorting table read,
				two strings walked through it, the five registered tables,
				and the comparison everything that orders text goes through.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "SortTables.h"
#include "RichString.h"			// kInkChar
#include "ByteOrder.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "NativeFunctions.h"
#include "objects.h"


TSortTables		gSortTables;		// ROM 0x0c1048c8


/*----------------------------------------------------------------------
	TSortingTable - the binary the collation is read out of.
----------------------------------------------------------------------*/

// The table is a persistent format (it comes out of a ROM object or off a
// store), so its halfwords are big-endian on every host; the ROM read them
// with unaligned word loads, which comes to the same thing.
UShort
TSortingTable::Half(const void* p, long offset)
{
	return GetBigEndianHalf((const unsigned char*) p + offset);
}


// ROM 0x00256428 CalcSize__13TSortingTableCFv
// How long the binary is: the header, the projection entries of every
// range, the single characters, the ligatures and the lowest-sort
// halfwords.
long
TSortingTable::CalcSize(void) const
{
	long size = kSortTableHeaderSize
			  + NumSingles() * kSingleEntrySize
			  + NumLigatures() * kLigatureEntrySize
			  + NumLowest() * (long) sizeof(UShort);
	for (short i = 0; i < NumRanges(); i++)
		size += (RangeLast(i) - RangeFirst(i) + 1) * kProjectionEntrySize;
	return size;
}


// ROM 0x00256270 GetProjectionEntry__13TSortingTableCFUs
// What the character sorts as, or nil when the table has never heard of
// it.  Below 0x80 the first range is indexed straight (which is the same
// answer the loop below would give, the ROM's built-in table starting its
// first range at 0); above it each range is tried in turn, and then the
// single characters are binary searched.
const void*
TSortingTable::GetProjectionEntry(UniChar c) const
{
	if (c < 0x80)
		return Bytes() + kSortTableHeaderSize + c * kProjectionEntrySize;

	long offset = kSortTableHeaderSize;
	for (short i = 0; i < NumRanges(); i++)
	{
		UShort first = RangeFirst(i);
		UShort last = RangeLast(i);
		if (c >= first && c <= last)
			return Bytes() + offset + (c - first) * kProjectionEntrySize;
		offset += (last - first + 1) * kProjectionEntrySize;
	}

	long low = 0;
	long high = NumSingles() - 1;
	while (low <= high)
	{
		long mid = low + (high - low) / 2;
		UShort code = Half(offset + mid * kSingleEntrySize);
		if (code < c)
			low = mid + 1;
		else if (code > c)
			high = mid - 1;
		else
			return Bytes() + offset + mid * kSingleEntrySize + 2;
	}
	return nil;
}


// ROM 0x0025635c GetLigatureEntry__13TSortingTableCFUs
// The two characters the ligature really is.  The ROM does not count the
// entries as it goes: a character whose projection said it was a ligature
// is in the table, and one that is not runs off the end.
const void*
TSortingTable::GetLigatureEntry(UniChar c) const
{
	const unsigned char* entry = Bytes() + kSortTableHeaderSize + LigatureOffset();
	while (Half(entry, 0) != c)
		entry += kLigatureEntrySize;
	return entry;
}


// ROM 0x00256384 ConvertTextToLowestSort__13TSortingTableCFPUsl
// Each character replaced by the least one that sorts the same, so that
// two keys which collate equally are equal byte for byte - which is what
// lets a soup index compare its keys without the tables.  A character the
// table does not know, or whose projection is past the lowest-sort table
// and is not a ligature, is left alone.
void
TSortingTable::ConvertTextToLowestSort(UniChar* text, long count) const
{
	for (long i = 0; i < count; i++, text++)
	{
		const void* entry = GetProjectionEntry(*text);
		if (entry == nil)
			continue;
		UShort projection = Projection(entry);
		UShort lowest;
		if ((long) projection < NumLowest())
			lowest = Half(kSortTableHeaderSize + LowestOffset() + projection * (long) sizeof(UShort));
		else if (projection == kLigatureProjection)
			lowest = LigatureLowest(GetLigatureEntry(*text));
		else
			continue;
		*text = lowest;
	}
}


/*----------------------------------------------------------------------
	TStringToSort - one of the two strings being compared.
----------------------------------------------------------------------*/

// ROM 0x002560a0 __ct__13TStringToSortFPCUslPC13TSortingTable
// DEVIATION: the ROM leaves fChar and fSecondOrderChar as whatever was on
// the stack.  Fetch always writes fChar before anyone reads it, but
// fSecondOrderChar is only written by Project, and Project returns without
// writing it when the character has no projection entry at all - so a ROM
// comparison whose first difference is at an unknown character reads the
// stack.  That is undefined on the host, so both start at 0.
TStringToSort::TStringToSort(const UniChar* text, long length, const TSortingTable* table)
{
	fTable = table;
	fText = text;
	fLength = length;
	fOriginalLength = length;
	fChar = 0;
	fPending = 0;
	fSecondOrderChar = 0;
	fWasLigature = false;
	fIgnorable = false;
}


// ROM 0x002560f4 Fetch__13TStringToSortFv
// The next character: a ligature's second character if one is waiting,
// else the next one of the text.
Boolean
TStringToSort::Fetch(void)
{
	fIgnorable = false;
	if (fPending != 0)
	{
		fChar = fPending;
		fPending = 0;
		return true;
	}
	if (fLength == 0)
		return false;
	fChar = *fText++;
	fLength--;
	return true;
}


// ROM 0x0025615c Project__13TStringToSortFc
// The character's primary weight.  A ligature is taken apart first - its
// first character becomes the current one and its second waits for the
// next Fetch - and the loop goes round in case that one is a ligature too.
// afterDifference says the two strings have already differed somewhere, so
// the character that decides the second order has been recorded already.
UShort
TStringToSort::Project(Boolean afterDifference)
{
	for (;;)
	{
		const void* entry = fTable->GetProjectionEntry(fChar);
		if (entry == nil)
			return fChar;
		UShort projection = TSortingTable::Projection(entry);
		if (projection != kLigatureProjection)
		{
			fIgnorable = projection == 0;
			if (!afterDifference)
				fSecondOrderChar = fChar;
			return projection;
		}
		const void* ligature = fTable->GetLigatureEntry(fChar);
		fChar = TSortingTable::LigatureFirst(ligature);
		fPending = TSortingTable::LigatureSecond(ligature);
		if (!afterDifference)
			fWasLigature = true;
	}
}


// ROM 0x00256230 SecondOrderProject__13TStringToSortCFv
// The second-order weight of the character the strings first differed at -
// what tells a capital from a small letter and an accented one from a
// plain one.
UShort
TStringToSort::SecondOrderProject(void) const
{
	const void* entry = fTable->GetProjectionEntry(fSecondOrderChar);
	return entry == nil ? fSecondOrderChar : TSortingTable::SecondOrder(entry);
}


// ROM 0x00255fd0 CalcSecondOrderResult__FRC13TStringToSortT1
// The tie broken: the second-order weights, and failing those the string
// that had a ligature in it comes second.
int
CalcSecondOrderResult(const TStringToSort& a, const TStringToSort& b)
{
	UShort pa = a.SecondOrderProject();
	UShort pb = b.SecondOrderProject();
	if (pa != pb)
		return pa > pb ? 1 : -1;
	return (int) a.fWasLigature - (int) b.fWasLigature;
}


/*----------------------------------------------------------------------
	TSortTables - the registered tables.
----------------------------------------------------------------------*/

// ROM 0x002564a8 GetTableEntry__11TSortTablesCFl
// The slot holding the table with this id.  It is a const method, but
// Subscribe and Unsubscribe write through what it answers.
SortTableEntry*
TSortTables::GetTableEntry(long id) const
{
	SortTableEntry* entry = (SortTableEntry*) fEntries;
	for (long i = 0; i < kMaxSortTables; i++, entry++)
		if (entry->fTable != nil && entry->fTable->Id() == id)
			return entry;
	return nil;
}


// ROM 0x002564dc GetSortTable__11TSortTablesCFlPl
// The table with this id, and how big it is.  BUG (the ROM's): when the id
// is the default one and there is no default table - which is how the
// system starts - a caller that wants the size reads through a nil table.
// Everything in the ROM asks with no size, so it never happens there.
const TSortingTable*
TSortTables::GetSortTable(long id, long* size) const
{
	if (fDefaultId == id)
	{
		if (size != nil)
			*size = fDefaultTable->CalcSize();
		return fDefaultTable;
	}
	SortTableEntry* entry = GetTableEntry(id);
	if (entry != nil)
	{
		if (size != nil)
			*size = entry->fTable->CalcSize();
		return entry->fTable;
	}
	return nil;
}


// ROM 0x0025659c AddSortTable__11TSortTablesFPC13TSortingTableUc
// A table registered in the first free slot, with one user.  ==> false
// when a table with that id is already there.  owned: the table is a
// pointer of ours, to be disposed of when the last user has gone.
Boolean
TSortTables::AddSortTable(const TSortingTable* table, Boolean owned)
{
	if (GetSortTable(table->Id(), nil) != nil)
		return false;
	SortTableEntry* entry = fEntries;
	for (long i = 0; i < kMaxSortTables; i++, entry++)
		if (entry->fTable == nil)
		{
			entry->fTable = table;
			entry->fOwned = owned;
			entry->fUsers = 1;
			return true;
		}
	Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	return true;
}


// ROM 0x00256630 Subscribe__11TSortTablesFl
void
TSortTables::Subscribe(long id)
{
	SortTableEntry* entry = GetTableEntry(id);
	if (entry != nil)
		entry->fUsers++;
}


// ROM 0x00256654 Unsubscribe__11TSortTablesFl
// The last user gone: the slot is freed, and the table with it if it was
// ours.
void
TSortTables::Unsubscribe(long id)
{
	SortTableEntry* entry = GetTableEntry(id);
	if (entry == nil)
		return;
	if (--entry->fUsers == 0)
	{
		if (entry->fOwned)
			DisposPtr((Ptr) entry->fTable);
		entry->fTable = nil;
	}
}


// ROM 0x00256698 SetDefaultTableId__11TSortTablesFl
// ==> false when there is no such table (0, meaning none at all, is
// always taken).
Boolean
TSortTables::SetDefaultTableId(long id)
{
	const TSortingTable* table = GetSortTable(id, nil);
	Boolean ok = table != nil || id == 0;
	if (ok)
	{
		fDefaultId = id;
		fDefaultTable = table;
	}
	return ok;
}


/*----------------------------------------------------------------------
	Comparing.
----------------------------------------------------------------------*/

// ROM 0x00255bf8 OldCompareText__FPCUslT1T2UcPFlT1Pv_lPv
// The compare the Newton used before there were sorting tables, and what
// it falls back to when there is none: character by character, folded to
// upper case without diacriticals through the Mac Roman character class
// tables unless an exact compare was asked for.
//
// BUG (the ROM's): the folded characters are compared as bytes, so two
// characters that have no Mac Roman form (both fold to 0x1a and are left
// as themselves) compare on their low bytes alone - U+0100 and U+0200
// come out equal.
int
OldCompareText(const UniChar* a, long aLength, const UniChar* b, long bLength,
			   Boolean exact, CompareInkProcPtr compareInk, void* refCon)
{
	long n = aLength < bLength ? aLength : bLength;
	long total = n;
	while (n >= 1)
	{
		ULong ca = *a++;
		ULong cb = *b++;
		int result;
		if (compareInk != nil && (ca == kInkChar || cb == kInkChar))
		{
			if (ca != kInkChar)
				return -1;
			if (cb != kInkChar)
				return 1;
			// both are ink: the caller says which comes first.  The ROM
			// hands it the same offset twice, the two strings being walked
			// in step.
			result = (int) compareInk(total - n, total - n, refCon);
		}
		else if (!exact)
		{
			if (gUnicodeInited)
			{
				ULong fa = ca, fb = cb;
				unsigned char ma = ca < 0x80 ? (unsigned char) ca : (unsigned char) A_CONST_CHAR((UniChar) ca);
				unsigned char mb = cb < 0x80 ? (unsigned char) cb : (unsigned char) A_CONST_CHAR((UniChar) cb);
				if (ma != 0x1a)
					fa = (ULong) (ma + (unsigned char) gUpperNoMarkList[gCharClass[ma]]);
				if (mb != 0x1a)
					fb = (ULong) (mb + (unsigned char) gUpperNoMarkList[gCharClass[mb]]);
				result = (int) (fa & 0xff) - (int) (fb & 0xff);
			}
			else
				// DEVIATION: the ROM reads the class tables through nil
				// before InitUnicode has installed them; the host folds
				// with Unicode.h's own upper case, as UppercaseText does.
				result = (int) UToUpper((UniChar) ca) - (int) UToUpper((UniChar) cb);
		}
		else
			result = (int) ca - (int) cb;
		if (result != 0)
			return result;
		n--;
	}
	return (int) (aLength - bLength);
}


// ROM 0x00255d6c CompareUnicodeText__FPCUslT1T2PC13TSortingTableUcPFlT1Pv_lPv
// The collation: both strings walked a character at a time, each projected
// through the sorting table, the first difference in the primary weights
// deciding it.  A character that projects to nothing is skipped and the
// other string's character kept for the next turn.  If the primaries never
// differ, an exact compare then asks the second order - the cases and the
// diacriticals of the character they first differed at.
//
// table 1 means the default table (kDefaultSortTable); with no table at
// all the old byte compare answers instead.
int
CompareUnicodeText(const UniChar* a, long aLength, const UniChar* b, long bLength,
				   const TSortingTable* table, Boolean exact,
				   CompareInkProcPtr compareInk, void* refCon)
{
	if (table == kDefaultSortTable)
		table = gSortTables.fDefaultTable;
	if (table == nil)
		return OldCompareText(a, aLength, b, bLength, exact, compareInk, refCon);
	if (aLength == 0 || bLength == 0)
		return (int) (aLength - bLength);

	TStringToSort sa(a, aLength, table);
	TStringToSort sb(b, bLength, table);
	Boolean afterDifference = false;
	Boolean keepA = false;
	Boolean keepB = false;
	Boolean gotA, gotB;
	for (;;)
	{
		if (keepA) { gotA = true; keepA = false; } else gotA = sa.Fetch();
		if (keepB) { gotB = true; keepB = false; } else gotB = sb.Fetch();
		if (!gotA || !gotB)
			break;

		if (compareInk != nil && (sa.fChar == kInkChar || sb.fChar == kInkChar))
		{
			if (sa.fChar != kInkChar)
				return -1;
			if (sb.fChar != kInkChar)
				return 1;
			int result = (int) compareInk(sa.fOriginalLength - sa.fLength - 1,
										  sb.fOriginalLength - sb.fLength - 1, refCon);
			if (result != 0)
				return result;
			continue;
		}

		if (sa.fChar != sb.fChar)
		{
			UShort pa = sa.Project(afterDifference);
			UShort pb = sb.Project(afterDifference);
			if (pa != pb)
			{
				if (!sa.fIgnorable && !sb.fIgnorable)
					return pa > pb ? 1 : -1;
				// the ignorable one is fetched again next turn; the other
				// is kept as it is
				keepA = !sa.fIgnorable;
				keepB = !sb.fIgnorable;
			}
			afterDifference = true;
		}
	}

	if (!gotA && !gotB)
	{
		// the same length, and the primaries all matched
		if (!afterDifference || !exact)
			return 0;
	}
	else
	{
		if (!afterDifference)
			return gotA ? 1 : -1;
		// one of them has text left: if all that is left projects to
		// nothing the two collate the same after all
		TStringToSort& rest = gotA ? sa : sb;
		do
		{
			rest.Project(afterDifference);
			if (!rest.fIgnorable)
				return gotA ? 1 : -1;
		} while (rest.Fetch());
		if (!exact)
			return 0;
	}
	return CalcSecondOrderResult(sa, sb);
}


// ROM 0x00255750 CompareStringNoCase__FPUsT1
int
CompareStringNoCase(const UniChar* a, const UniChar* b)
{
	long bLength = Ustrlen(b);
	long aLength = Ustrlen(a);
	return CompareUnicodeText(a, aLength, b, bLength, kDefaultSortTable, false, nil, nil);
}


// ROM 0x002557a4 CompareTextNoCase__FPUslT1T2
int
CompareTextNoCase(const UniChar* a, long aLength, const UniChar* b, long bLength)
{
	return CompareUnicodeText(a, aLength, b, bLength, kDefaultSortTable, false, nil, nil);
}


/*----------------------------------------------------------------------
	What a script may ask about the sorting.
----------------------------------------------------------------------*/

// ROM 0x002566e0 FGetSortID__FRC6RefVarT1
// The default table's id, and nil when there is none.  The argument is a
// store, whose own table would be answered instead: NOT YET RECONSTRUCTED
// (StoreGetDirSortTable's tables kept on the store), and the ROM answers
// nil for it too.
Ref
FGetSortID(RefArg /*rcvr*/, RefArg store)
{
	if (gSortTables.fDefaultId != 0 && ISNIL(store))
		return MAKEINT(gSortTables.fDefaultId);
	return NILREF;
}


// ROM 0x00256710 FSetSortID__FRC6RefVarT1
// The default table chosen by id; nil means none at all.  ==> true, or
// nil when the id is not a number or names no registered table.
Ref
FSetSortID(RefArg /*rcvr*/, RefArg value)
{
	long id;
	if (ISNIL(value))
		id = 0;
	else if (!ISINT(value))
		return NILREF;
	else
		id = RINT(value);
	if (gSortTables.fDefaultId != id && !gSortTables.SetDefaultTableId(id))
		return NILREF;
	return TRUEREF;
}


void
RegisterSortTableNatives(void)
{
	RegisterNativeFunction("FGetSortID__FRC6RefVarT1", (void*) FGetSortID, 1);
	RegisterNativeFunction("FSetSortID__FRC6RefVarT1", (void*) FSetSortID, 1);
}
