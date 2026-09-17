/*
	File:		frames/UnicodeTables.cpp

	Contains:	InitUnicode: the ROM's character tables installed.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "UnicodeTables.h"
#include "Unicode.h"
#include "Objects.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "NewtonMemory.h"
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


// ROM 0x002558ac InstallBuiltInEncodings__Fv
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


// ROM 0x00254b80 InitUnicode__Fv
// The encodings installed, then the character class and case tables and
// the ASCII break table (Rasciibreak, the magic pointer @6) stored; the
// Unicode globals set.  NOT YET RECONSTRUCTED: the sort tables
// (gSortTables from the 'sortTables array, TSortTables::AddSortTable).
// Host: nothing without the ROM's objects (Runicode nil).
void
InitUnicode(void)
{
	if (ISNIL(Runicode) || gUnicodeInited)
		return;
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
