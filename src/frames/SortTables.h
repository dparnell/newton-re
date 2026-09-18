/*
	File:		frames/SortTables.h

	Contains:	The collation tables - how the Newton decides which of two
				pieces of text comes first.

				A sorting table (TSortingTable) is a binary object: a
				0x44-byte header and then, for every character the table
				knows, a four-byte projection entry saying what that
				character sorts as (its primary weight) and how it differs
				from the others that sort the same (its second-order weight,
				which is what tells 'a' from 'A' and from 'a-acute').  The
				characters are reached either through ranges - the ASCII
				block and one more for Latin-1, each a plain array indexed by
				the character - or, above them, through a sorted table of
				single characters that is binary searched.  A projection of
				0xffff means the character is really two (the ligature table
				says which, so that a sharp s sorts as "ss"), and a
				projection of 0 means the character is ignored altogether.
				The last part is the "lowest sort" table, which maps a
				primary weight back to the least character having it: the
				soup indexes store that instead of the text so a key compares
				with a memcmp.

				The tables themselves live in the ROM (the 'unicode frame's
				sortTables array - frames/UnicodeTables.cpp installs them)
				or on a store, and are registered in gSortTables, which holds
				five of them and remembers which is the default.  A table is
				named by its id, a small number: the ROM's built-in table is
				1, and 0 means none at all, in which case CompareUnicodeText
				falls back to OldCompareText - the case-folded byte compare
				the Newton used before there were sorting tables.

				The table is a persistent format, so every halfword in it is
				big-endian whatever the host is; the accessors below read it
				that way rather than casting a struct over it.

	Reconstructed from the MP2x00 US ROM (0x00257688-0x00258698); each
	function cites its origin.
*/

#ifndef __SORTTABLES_H
#define __SORTTABLES_H

#include "objects.h"
#include "Unicode.h"


/*----------------------------------------------------------------------
	A sorting table.

	It has no members: it is the binary's bytes, and every field is read
	out of them by offset, as the ROM does.
----------------------------------------------------------------------*/

enum {
	kSortTableHeaderSize	= 0x44,		// the header, and where the data starts
	kMaxSortRanges			= 6,		// what fits between +0x08 and +0x20
	kProjectionEntrySize	= 4,		// {primary, secondOrder}
	kSingleEntrySize		= 6,		// {character, primary, secondOrder}
	kLigatureEntrySize		= 8,		// {character, first, second, lowest}
	kLigatureProjection		= 0xffff	// "the character is really two"
};

class TSortingTable
{
public:
	// the header
	short			Id(void) const					{ return Short(0x00); }
	short			NumRanges(void) const			{ return Short(0x06); }
	UShort			RangeFirst(long i) const		{ return Half(0x08 + i * 4); }
	UShort			RangeLast(long i) const			{ return Half(0x0a + i * 4); }
	short			NumSingles(void) const			{ return Short(0x20); }
	short			LigatureOffset(void) const		{ return Short(0x24); }	// from the header's end
	short			NumLigatures(void) const		{ return Short(0x26); }
	short			LowestOffset(void) const		{ return Short(0x28); }	// from the header's end
	short			NumLowest(void) const			{ return Short(0x2a); }

	long			CalcSize(void) const;
	const void*		GetProjectionEntry(UniChar c) const;
	const void*		GetLigatureEntry(UniChar c) const;
	void			ConvertTextToLowestSort(UniChar* text, long count) const;

	// the halfwords of a projection entry (GetProjectionEntry's answer)
	static UShort	Projection(const void* entry)	{ return Half(entry, 0); }
	static UShort	SecondOrder(const void* entry)	{ return Half(entry, 2); }

	// ... and of a ligature entry (GetLigatureEntry's answer)
	static UShort	LigatureFirst(const void* e)	{ return Half(e, 2); }
	static UShort	LigatureSecond(const void* e)	{ return Half(e, 4); }
	static UShort	LigatureLowest(const void* e)	{ return Half(e, 6); }

private:
	const unsigned char*	Bytes(void) const		{ return (const unsigned char*) this; }
	UShort			Half(long offset) const			{ return Half(this, offset); }
	short			Short(long offset) const		{ return (short) Half(this, offset); }
	static UShort	Half(const void* p, long offset);
};


/*----------------------------------------------------------------------
	One of the two strings a comparison walks.

	Fetch takes the next character (a ligature's second character first,
	if one is waiting), Project turns it into its primary weight - taking
	a ligature apart as it goes - and SecondOrderProject answers the
	second-order weight of the character the two strings first differed at.
----------------------------------------------------------------------*/

class TStringToSort
{
public:
					TStringToSort(const UniChar* text, long length, const TSortingTable* table);

	Boolean			Fetch(void);					// ==> false at the end of the text
	UShort			Project(Boolean afterDifference);
	UShort			SecondOrderProject(void) const;

	const TSortingTable*	fTable;				// +0x00
	const UniChar*			fText;				// +0x04  what is left to read
	long					fLength;			// +0x08  how much of it
	long					fOriginalLength;	// +0x0c  so the offset can be worked out
	UniChar					fChar;				// +0x10  the character being sorted
	UniChar					fPending;			// +0x12  a ligature's second character
	UniChar					fSecondOrderChar;	// +0x14  the character at the first difference
	Boolean					fWasLigature;		// +0x16  a ligature was taken apart
	Boolean					fIgnorable;			// +0x17  the character projects to nothing
};													// 0x18 bytes


/*----------------------------------------------------------------------
	The five registered tables, and which one is the default.
----------------------------------------------------------------------*/

enum { kMaxSortTables = 5 };

struct SortTableEntry				// 0x0c bytes
{
	const TSortingTable*	fTable;		// +0x00  nil: the slot is free
	Boolean					fOwned;		// +0x04  ours to dispose of
	long					fUsers;		// +0x08  how many indexes want it
};

class TSortTables
{
public:
	// const, but the answer is written through (Subscribe) - as in the ROM
	SortTableEntry*			GetTableEntry(long id) const;
	const TSortingTable*	GetSortTable(long id, long* size) const;
	Boolean					AddSortTable(const TSortingTable* table, Boolean owned);
	void					Subscribe(long id);
	void					Unsubscribe(long id);
	Boolean					SetDefaultTableId(long id);

	SortTableEntry			fEntries[kMaxSortTables];	// +0x00
	long					fDefaultId;					// +0x3c
	const TSortingTable*	fDefaultTable;				// +0x40
};													// 0x44 bytes

extern TSortTables	gSortTables;		// ROM 0x0c1048c8


/*----------------------------------------------------------------------
	Comparing.
----------------------------------------------------------------------*/

// The table argument of CompareUnicodeText: 1 is not a table but "the
// default one", which is how everything but the soup indexes asks.
#define kDefaultSortTable	((const TSortingTable*) 1)

// A rich string's ink words cannot be collated, so the caller may hand in
// a function to compare two of them.  The ROM's declaration of it (the
// mangled name) says the second argument is a character pointer, but both
// callers pass the offset of the second string's character, a long; the
// offsets are what the function is given.
typedef long (*CompareInkProcPtr)(long aOffset, long bOffset, void* refCon);

// < 0, 0 or > 0.  exact compares the second order as well - the cases and
// the diacriticals - when the primaries are all equal.
int		CompareUnicodeText(const UniChar* a, long aLength, const UniChar* b, long bLength,
						   const TSortingTable* table, Boolean exact,
						   CompareInkProcPtr compareInk, void* refCon);
int		OldCompareText(const UniChar* a, long aLength, const UniChar* b, long bLength,
					   Boolean exact, CompareInkProcPtr compareInk, void* refCon);
int		CalcSecondOrderResult(const TStringToSort& a, const TStringToSort& b);

int		CompareStringNoCase(const UniChar* a, const UniChar* b);
int		CompareTextNoCase(const UniChar* a, long aLength, const UniChar* b, long bLength);

// GetSortID/SetSortID
Ref		FGetSortID(RefArg rcvr, RefArg store);
Ref		FSetSortID(RefArg rcvr, RefArg value);
void	RegisterSortTableNatives(void);

#endif	/* __SORTTABLES_H */
