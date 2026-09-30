/*
	File:		frames/UnicodeTables.cpp

	Contains:	InitUnicode: the ROM's character tables installed.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "UnicodeTables.h"
#include "Unicode.h"
#include "objects.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "NewtonMemory.h"
#include "SortTables.h"
#include <string.h>

static Ref	GetUnicodeSlot(const char* name)
{
	return GetFrameSlotRef(RefVar(Runicode), RefVar(Intern((char*) name)));
}


// a binary's bytes in memory of their own: the ROM points into its
// objects, which never move; the host's heap compacts (DEVIATION)
static void*
CopyBinary(RefArg binary)
{
	long size = Length(binary);
	void* copy = NewPtr(size);
	memcpy(copy, BinaryData(binary), size);
	return copy;
}


// ROM 0x002577e4 InstallBuiltInEncodings__Fv
// Each frame of the 'unicode frame's charEncodings ({encodingID,
// mapFromUnicode, mapToUnicode}) read by GetMappingInfo into two
// TEncodingMaps and installed with its converters.
void
InstallBuiltInEncodings(void)
{
	RefVar encodings(GetUnicodeSlot("charEncodings"));
	long count = Length(encodings);
	for (long i = 0; i < count; i++)
	{
		RefVar encoding(GetArraySlotRef(encodings, i));
		UShort id = (UShort) RINT(GetFrameSlotRef(encoding, RefVar(Intern((char*) "encodingID"))));
		TEncodingMap* fromMap = (TEncodingMap*) NewPtrClear(sizeof(TEncodingMap));
		TEncodingMap* toMap = (TEncodingMap*) NewPtrClear(sizeof(TEncodingMap));
		void* fromUnicode;
		void* toUnicode;
		GetMappingInfo(CopyBinary(RefVar(GetFrameSlotRef(encoding, RefVar(Intern((char*) "mapFromUnicode"))))), fromMap, &fromUnicode);
		GetMappingInfo(CopyBinary(RefVar(GetFrameSlotRef(encoding, RefVar(Intern((char*) "mapToUnicode"))))), toMap, &toUnicode);
		InstallCharEncoding(id, fromMap, toMap, (ConvertFromUnicodeProcPtr) fromUnicode, (ConvertToUnicodeProcPtr) toUnicode);
	}
}


// ROM 0x00256acc InitUnicode__Fv
// The encodings installed, then the character class and case tables and
// the ASCII break table (Rasciibreak, the magic pointer @6) stored; the
// Unicode globals set.  The ROM's sorting tables - the 'sortTables array
// of binaries, one per table - are registered first, before anything can
// ask to collate.  Host: nothing without the ROM's objects (Runicode nil).
void
InitUnicode(void)
{
	if (ISNIL(Runicode) || gUnicodeInited)
		return;
	RefVar tables(Rsorttables);
	if (NOTNIL(tables))
	{
		long count = Length(tables);
		for (long i = 0; i < count; i++)
			// DEVIATION: the ROM's tables are its own objects, which never
			// move, so it registers them where they lie and does not own
			// them; the host's heap compacts, so each is copied out and
			// the copy is ours.
			gSortTables.AddSortTable((const TSortingTable*) CopyBinary(RefVar(GetArraySlotRef(tables, i))), true);
	}
	InstallBuiltInEncodings();
	const unsigned char* breakTable = nil;
	RefVar breaks(Rasciibreak);
	if (NOTNIL(breaks) && IsBinary(breaks))
		breakTable = (const unsigned char*) CopyBinary(breaks);
	InstallCharTables((const unsigned char*) CopyBinary(RefVar(GetUnicodeSlot("charClass"))),
					  (const unsigned char*) CopyBinary(RefVar(GetUnicodeSlot("typelist"))),
					  (const signed char*) CopyBinary(RefVar(GetUnicodeSlot("upperList"))),
					  (const signed char*) CopyBinary(RefVar(GetUnicodeSlot("lowerList"))),
					  (const signed char*) CopyBinary(RefVar(GetUnicodeSlot("upperNoMarkList"))),
					  (const signed char*) CopyBinary(RefVar(GetUnicodeSlot("noMarkList"))),
					  breakTable);
}
