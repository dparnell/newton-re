/*
	File:		frames/Symbols.cpp

	Contains:	Symbols: interning (Intern), the RAM symbol table (an array
				open-addressed by hash, enlarged, rehashed or shrunk as it
				fills) over the ROM's read-only one, comparison (EQRef and the
				ROM's ListEQ family, SymbolCompare, symcmp) and GCTWA, the
				collector's dropping of symbols nothing else refers to.

	Reconstructed from the MP2100 D ROM (0x0032d4a8-0x0032dd00 and the
	comparisons at 0x002f42d8-0x002f44c4); each function cites its origin.

	The ROM's symbol table (the array at 0x0053eba1, 32768 slots) is part of
	the ROM's object graph, which is not imported yet; the host stands in
	with a read-only symbol space holding the symbols the C++ code refers to
	(RSSymbols.h, generated from the ROM's RSSYM constants), as
	gROMSymbolTable.  Symbols there are unique, so EQRef compares two of them
	by identity as the ROM does for symbols below its RExBlock (0x006f2e9c).
*/

#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"

#include <ctype.h>
#include <string.h>

Ref		gSymbolTable = NILREF;
long	gSymbolTableSize = 0;
long	gSymbolTableHashShift = 0;
Ref*	gROMSymbolTable = nil;
long	gROMSymbolTableSize = 0;
long	gROMSymbolTableHashShift = 0;
long	gNumSymbols = 0;
long	gNumSlotsTaken = 0;

// the host's read-only symbol space (InitROMSymbols)
static char*	gROMSymbolSpaceStart = nil;
static char*	gROMSymbolSpaceEnd = nil;


/* -------------------------------------------------------------------------------
	Names and hashes
------------------------------------------------------------------------------- */

// ROM 0x0032d4a8 SymbolName__Fl
char*
SymbolName(Ref sym)
{
	return BinaryData(sym) + sizeof(ULong32);
}


// ROM 0x0032d4c0 SymbolHash__Fl
ULong
SymbolHash(Ref sym)
{
	return *(ULong32*) BinaryData(sym);
}


// ROM 0x0032dab8 SymbolHashFunction__FPc
// The sum of the upper-cased characters times the golden-ratio constant,
// in 32 bits (the hash is stored in every symbol, so it is the ROM's).
ULong32
SymbolHashFunction(const char* name)
{
	ULong32 sum = 0;
	for (const unsigned char* p = (const unsigned char*) name; *p != 0; p++)
		sum += (ULong32) toupper(*p);
	return sum * 0x9E3779B9u;
}


// ROM 0x0032db00 symcmp__FPcT1
// Case-insensitive (ASCII letters) comparison, as strcmp.
int
symcmp(char* s1, char* s2)
{
	const unsigned char* p1 = (const unsigned char*) s1;
	const unsigned char* p2 = (const unsigned char*) s2;
	for (;;)
	{
		unsigned c1 = *p1;
		unsigned c2 = *p2;
		if (c1 == 0)
			return (c2 != 0) ? -1 : 0;
		if (c2 == 0)
			return 1;
		if (c1 >= 'a' && c1 <= 'z')
			c1 -= 0x20;
		if (c2 >= 'a' && c2 <= 'z')
			c2 -= 0x20;
		p1++;
		p2++;
		if (c1 != c2)
			return (c1 < c2) ? -1 : 1;
	}
}


// ROM 0x0032d940 SymbolCompare__FlT1
// Order two symbols: by hash first, then by name.
int
SymbolCompare(Ref sym1, Ref sym2)
{
	if (sym1 == sym2)
		return 0;
	ULong32 hash1 = ObjSymbol(PTRVALUE(sym1))->fHash;
	ULong32 hash2 = ObjSymbol(PTRVALUE(sym2))->fHash;
	if (hash1 > hash2)
		return 1;
	if (hash1 < hash2)
		return -1;
	return symcmp(ObjSymbol(PTRVALUE(sym1))->fName, ObjSymbol(PTRVALUE(sym2))->fName);
}


// ROM 0x0032db70 SymbolCompareLexRef__FlT1
// Order two symbols by name (case-insensitively).
int
SymbolCompareLexRef(Ref sym1, Ref sym2)
{
	if (!IsSymbol(sym1))
	{
		RefVar value(sym1);
		ThrowExFramesWithBadValue(kNSErrNotASymbol, value);
	}
	else if (!IsSymbol(sym2))
	{
		RefVar value(sym2);
		ThrowExFramesWithBadValue(kNSErrNotASymbol, value);
	}
	return symcmp(SymbolName(sym1), SymbolName(sym2));
}


// ROM 0x0032d908 IsSymbol__Fl
int
IsSymbol(Ref obj)
{
	return ISPTR(obj) && ObjClass(NoFaultObjectPtr(obj)) == kSymbolClass;
}


// ROM 0x002f761c IsSymbol__FRC6RefVar
Boolean
IsSymbol(RefArg obj)
{
	return IsSymbol((Ref) obj) != 0;
}


// The ROM knows a symbol below its RExBlock (0x006f2e9c, the end of the
// ROM's symbols) is the only one of its name; here that is the ROM's
// object area read by ROMImport, or the host's small symbol space.
Boolean
InROMSymbolSpace(Ref r)
{
	return ((char*) r >= gROMSymbolSpaceStart && (char*) r < gROMSymbolSpaceEnd) || InROMObjectArea(r);
}


// ROM 0x0032da2c UnsafeSymbolEqual__FlT1Ul
// Two symbols with the same name, given the first's hash; refs already
// resolved to objects.
Boolean
UnsafeSymbolEqual(Ref sym1, Ref sym2, ULong32 hash)
{
	if (sym1 == kDeclawedRef)
		Throw(exFrames, (void*) kNSErrBadPackageRef, nil);
	if (sym1 == sym2)
		return true;
	if (InROMSymbolSpace(sym1) && InROMSymbolSpace(sym2))
		return false;
	if (ObjSymbol(PTRVALUE(sym1))->fHash != hash)
		return false;
	return symcmp(ObjSymbol(PTRVALUE(sym1))->fName, ObjSymbol(PTRVALUE(sym2))->fName) == 0;
}


/* -------------------------------------------------------------------------------
	EQ
	Two refs are EQ when they are the same, or point at the same object
	(forwarding followed), or are two symbols of the same name - unless both
	are ROM symbols, which are unique.
------------------------------------------------------------------------------- */

// the object behind a pointer ref for EQ: forwarding followed, no faulting
static inline ObjHeader*
EQObject(Ref r)
{
	if (RTAG(r) == kTagPointer)
	{
		ObjHeader* o = PTRVALUE(r);
		if ((ObjFlags(o) & kObjForward) != 0)
			o = PTRVALUE(ForwardReference(r));
		return o;
	}
	return NoFaultObjectPtr(r);
}


static inline Boolean
SameSymbol(ObjHeader* a, ObjHeader* b)
{
	return ObjClass(a) == kSymbolClass && ObjClass(b) == kSymbolClass
		&& ObjSymbol(a)->fHash == ObjSymbol(b)->fHash
		&& symcmp(ObjSymbol(a)->fName, ObjSymbol(b)->fName) == 0;
}


// ROM 0x002f42d8 EQ1__FlT1
// Two pointer refs.
static Boolean
EQ1(Ref a, Ref b)
{
	ObjHeader* oa = EQObject(a);
	ObjHeader* ob = EQObject(b);
	if (oa == ob)
		return true;
	if (!InROMSymbolSpace(a) || !InROMSymbolSpace(b))
		return SameSymbol(oa, ob);
	return false;
}


// ROM 0x002f43c8 EQRef__FlT1
int
EQRef(Ref a, Ref b)
{
	if (a == b)
		return true;
	if ((a & b & 1) == 0)
		return false;
	return EQ1(a, b);
}


// ROM 0x002f44c4 SetupListEQ__Fl
// For comparing one ref against many: its object, once.
static ObjHeader*
SetupListEQ(Ref r)
{
	if (!ISPTR(r))
		return nil;
	return EQObject(r);
}


// ROM 0x002f43e8 ListEQ1__FlT1Pc
static Boolean
ListEQ1(Ref a, Ref b, ObjHeader* ob)
{
	ObjHeader* oa = EQObject(a);
	if (oa == ob)
		return true;
	if (!InROMSymbolSpace(a) || !InROMSymbolSpace(b))
		return SameSymbol(oa, ob);
	return false;
}


// ROM 0x002f44a4 ListEQ__FlT1Pc
// EQRef(a, b) with b's object already found by SetupListEQ.
static Boolean
ListEQ(Ref a, Ref b, ObjHeader* ob)
{
	if (a == b)
		return true;
	if ((a & b & 1) == 0)
		return false;
	return ListEQ1(a, b, ob);
}


/* -------------------------------------------------------------------------------
	TPrecedentsVar
	The list of objects already seen by DeepClone/TotalClone: an array that
	grows by 16, searched with ListEQ.
------------------------------------------------------------------------------- */

// ROM 0x0032b270 __ct__14TPrecedentsVarFv
TPrecedentsVar::TPrecedentsVar()
	: fArray(AllocateArray(RSSYMarray, 16))
{
	fCount = 0;
}


// ROM 0x0032c980 Append__14TPrecedentsVarFRC6RefVar
void
TPrecedentsVar::Append(RefArg obj)
{
	long length = Length(fArray);
	if (fCount == length)
		SetLength(fArray, length + 16);
	SetArraySlotRef(fArray, fCount, obj);
	fCount++;
}


// ROM 0x0032ce14 Find__14TPrecedentsVarFRC6RefVar
long
TPrecedentsVar::Find(RefArg obj)
{
	if (fCount != 0)
	{
		Ref* slots = ObjArraySlots(OBJ(fArray));
		ObjHeader* o = SetupListEQ(obj);
		for (long i = 0; i < fCount; i++)
			if (ListEQ(slots[i], obj, o))
				return i;
	}
	return -1;
}


// ROM 0x0032ce90 Get__14TPrecedentsVarFl
Ref
TPrecedentsVar::Get(long index)
{
	return GetArraySlotRef(fArray, index);
}


/* -------------------------------------------------------------------------------
	The symbol tables
	Open addressing: the slot is the hash's top bits (hash >> shift), the
	probe step (hash & 7) * 2 + 1; an empty slot is NILREF, a deleted one an
	integer (GCTWA leaves 0).
------------------------------------------------------------------------------- */

// ROM 0x0032dc34 FindSymbol__FPllT2PcUlRl
// Whether a symbol of this name is in the table; *index is its slot, or the
// slot a new one would go in (the first deleted slot met, else the empty
// one).
Boolean
FindSymbol(Ref* table, long size, long hashShift, const char* name, ULong32 hash, long* index)
{
	if (size == 0)								// (the host's ROM table can be empty)
	{
		*index = 0;
		return false;
	}
	ULong32 start = hash >> hashShift;
	*index = start;
	long deleted = -1;
	Ref sym = table[start];
	if (sym != NILREF)
	{
		do {
			if (RTAG(sym) == kTagInteger)
				deleted = *index;
			else if (symcmp((char*) name, SymbolName(sym)) == 0)
				return true;
			long next = *index + (start & 7) * 2 + 1;
			*index = next;
			if (next >= size)
				*index = next - size;
			sym = table[*index];
		} while (sym != NILREF);
		if (deleted != -1)
			*index = deleted;
	}
	return false;
}


// ROM 0x0032dcd4 InternExistingSymbol__FRC6RefVar
// A symbol object into the RAM table (rehashing, or one made elsewhere).
void
InternExistingSymbol(RefArg sym)
{
	ULong32 hash = SymbolHash((Ref) sym);
	char* name = SymbolName(sym);
	long index;
	if (!FindSymbol(Slots(gSymbolTable), gSymbolTableSize, gSymbolTableHashShift, name, hash, &index))
		SetArraySlotRef(gSymbolTable, index, sym);
}


// a new RAM table of the given size, the old one's symbols re-entered
static void
RebuildSymbolTable(long size, long hashShift)
{
	RefVar oldTable(gSymbolTable);
	long oldSize = gSymbolTableSize;
	gSymbolTable = AllocateArray(RSSYMarray, size);
	gSymbolTableSize = size;
	gSymbolTableHashShift = hashShift;
	RefVar sym;
	for (long i = 0; i < oldSize; i++)
	{
		sym = GetArraySlotRef(oldTable, i);
		if (ISPTR(sym))
			InternExistingSymbol(sym);
	}
	gNumSlotsTaken = gNumSymbols;
}


// ROM 0x0032d57c EnlargeSymbolTable__Fv
void
EnlargeSymbolTable(void)
{
	RebuildSymbolTable(gSymbolTableSize * 2, gSymbolTableHashShift - 1);
}


// ROM 0x0032d598 RehashSymbolTable__Fv
// The same size again: drops the deleted slots.
void
RehashSymbolTable(void)
{
	RebuildSymbolTable(gSymbolTableSize, gSymbolTableHashShift);
}


// ROM 0x0032d634 AdjustSymbolTableSize__Fv
// The smallest power of two (at least 32) holding the symbols at half full.
void
AdjustSymbolTableSize(void)
{
	long shift = 27;
	long size = 32;
	while (size < gNumSymbols * 2)
	{
		size *= 2;
		shift--;
	}
	if (size != gSymbolTableSize)
		RebuildSymbolTable(size, shift);
}


// ROM 0x0032d674 Intern__FPc
// The symbol of this name: the ROM's, or the RAM table's, or a new one -
// which enlarges the table past 85% full, and afterwards shrinks it if it
// is under a quarter full or rehashes it when the deleted slots make it
// 85% taken.
Ref
Intern(char* name)
{
	ULong32 hash = SymbolHashFunction(name);
	long index;
	if (FindSymbol(gROMSymbolTable, gROMSymbolTableSize, gROMSymbolTableHashShift, name, hash, &index))
		return gROMSymbolTable[index];
	if (FindSymbol(Slots(gSymbolTable), gSymbolTableSize, gSymbolTableHashShift, name, hash, &index))
		return GetArraySlotRef(gSymbolTable, index);

	long length = strlen(name);
	RefVar sym(AllocateBinary(RefVar(kSymbolClass), length + 1 + sizeof(ULong32)));
	SymbolData* data = (SymbolData*) BinaryData(sym);
	data->fHash = hash;
	strcpy(data->fName, name);
	if (gNumSymbols > Length(gSymbolTable) * 85 / 100)
	{
		EnlargeSymbolTable();
		InternExistingSymbol(sym);
	}
	else
		SetArraySlotRef(gSymbolTable, index, sym);
	gNumSymbols++;
	gNumSlotsTaken++;
	if (gNumSymbols < gSymbolTableSize / 4)
		AdjustSymbolTableSize();
	else if (gNumSlotsTaken > gSymbolTableSize * 85 / 100)
		RehashSymbolTable();
	return sym;
}


// ROM 0x0032d810 Intern__FPUs
// NOT YET RECONSTRUCTED: ConvertFromUnicode (the Unicode encoders); the
// low bytes of the characters are taken.
Ref
Intern(UniChar* name)
{
	long length = 0;
	while (name[length] != 0)
		length++;
	char* ascii = new char[length + 1];
	for (long i = 0; i <= length; i++)
		ascii[i] = (char) name[i];
	RefVar sym(Intern(ascii));
	delete[] ascii;
	return sym;
}


// ROM 0x002f766c MakeSymbol__FPc
Ref
MakeSymbol(char* name)
{
	return Intern(name);
}


// ROM 0x0032d870 GCTWA__Fv
// Garbage-collect the RAM symbols nothing else marked: their slots become
// deleted (0), so that the table is not the reason they survive.
void
GCTWA(void)
{
	Ref* slots = ObjArraySlots(OBJ(gSymbolTable));
	for (long i = 0; i < gSymbolTableSize; i++)
	{
		Ref sym = slots[i];
		if (RTAG(sym) == kTagPointer && gHeap->InHeap(sym) && (ObjectFlags(sym) & kObjMarked) == 0)
		{
			slots[i] = 0;
			gNumSymbols--;
		}
	}
}


/* -------------------------------------------------------------------------------
	Start-up
------------------------------------------------------------------------------- */

// Host: the symbols the C++ code names (the ROM's RSSYM constants) as a
// read-only symbol space outside the object heap, and a table over them as
// gROMSymbolTable - the shape of the ROM's own symbol table, which arrives
// with the ROM object importer.
void
InitROMSymbols(void)
{
	// the space
	ULong space = 0;
	for (long i = 0; i < gRSSymbolCount; i++)
		space += AlignedSize(BinaryObjSize(strlen(gRSSymbolEntries[i].fName) + 1 + sizeof(ULong32)));
	gROMSymbolSpaceStart = (char*) AlignedSize((ULong) NewPtr(space + kObjAlign));
	gROMSymbolSpaceEnd = gROMSymbolSpaceStart + space;

	// the table: a power of two at most half full
	long size = 32;
	long shift = 27;
	while (size < gRSSymbolCount * 2)
	{
		size *= 2;
		shift--;
	}
	gROMSymbolTable = (Ref*) NewPtr(size * sizeof(Ref));
	for (long i = 0; i < size; i++)
		gROMSymbolTable[i] = NILREF;
	gROMSymbolTableSize = size;
	gROMSymbolTableHashShift = shift;

	char* p = gROMSymbolSpaceStart;
	for (long i = 0; i < gRSSymbolCount; i++)
	{
		const RSSymbolEntry& entry = gRSSymbolEntries[i];
		long length = strlen(entry.fName) + 1 + sizeof(ULong32);
		ObjHeader* o = (ObjHeader*) p;
		o->fSizeAndFlags = (BinaryObjSize(length) << kObjSizeShift) | kObjReadOnly;
		o->fGCStuff = 0;
		ObjClass(o) = kSymbolClass;
		ObjSymbol(o)->fHash = entry.fHash;
		strcpy(ObjSymbol(o)->fName, entry.fName);
		Ref sym = MAKEPTR(o);
		long index;
		if (!FindSymbol(gROMSymbolTable, size, shift, entry.fName, entry.fHash, &index))
			gROMSymbolTable[index] = sym;
		*entry.fRef = sym;
		p += AlignedSize(BinaryObjSize(length));
	}
}


// ROM 0x0032d97c InitSymbols__Fv
// The RAM table (128 slots, a GC root the collector treats specially) over
// the ROM's (the array at 0x0053eba1 - here the one ROMImport read, or
// InitROMSymbols' small stand-in when no ROM image was imported).
void
InitSymbols(void)
{
	gSymbolTableSize = 0x80;
	gSymbolTableHashShift = 0x19;
	gSymbolTable = AllocateArray(RefVar(NILREF), gSymbolTableSize);
	AddGCRoot(gSymbolTable);
	if (gROMSymbolTableRef == NILREF)
	{
		InitROMSymbols();
		return;
	}
	gROMSymbolTable = Slots(gROMSymbolTableRef);
	gROMSymbolTableSize = Length(gROMSymbolTableRef);
	gROMSymbolTableHashShift = 0x1f;
	for (long size = 2; size < gROMSymbolTableSize; size *= 2)
		gROMSymbolTableHashShift--;
}
