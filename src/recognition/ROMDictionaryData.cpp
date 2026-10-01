/*
	File:		recognition/ROMDictionaryData.cpp

	Contains:	The lexicons built into the ROM - ROMDictionaryData.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ROMDictionaryData.h"
#include "Airus.h"
#include "ROMImport.h"
#include "ByteOrder.h"


// ROM 0x0c106840 gROMDictionaryData
const UByte*	gROMDictionaryData[kROMDictionaryCount] = { nil };


// ROM 0x0019b0d4 InitROMDictionaryData__Fv
// The table of lexicons filled in.  The ROM writes 129 addresses into it
// one at a time, in no particular order and with a good many slots
// sharing the same empty dictionary; here the addresses are the generated
// table's and the loop does what the straight line did.
//
// DEVIATION: the ROM's addresses are the machine's own, where the ROM is
// simply there to be read.  The host reads the image the frames were
// imported from, or the lexicons an object file built from the ROM source
// tree carries (frames/ROMImport.h's ROMBytesAt), so each address is taken
// as the ROM address of those bytes; with neither, the table stays empty.
void
InitROMDictionaryData(void)
{
	for (long i = 0; i < kROMDictionaryCount; i++)
	{
		// DEVIATION: a lexicon an edit of romsrc/ made bigger than its room
		// lies elsewhere in the object file (ROMMovedAddress)
		ULong at = gROMDictionaryTable[i].fAddress;
		if (at != 0)
			at = ROMMovedAddress(at);
		const UByte* data = at == 0 ? nil : (const UByte*) ROMBytesAt(at, 4);
		if (data != nil && ROMBytesAt(at, 4 + GetBigEndianWord(data)) == nil)
			data = nil;
		gROMDictionaryData[i] = data;
	}
}


// ROM 0x0019b078 GetROMDictionaryData__FUlPUl
// The bytes of one lexicon.  The data begins with the size of what
// follows it, which is read and stepped over, so that what comes back is
// the trie itself.
const void*
GetROMDictionaryData(ULong id, ULong* size)
{
	const UByte* data = id < (ULong) kROMDictionaryCount ? gROMDictionaryData[id] : nil;
	if (data == nil)
	{
		*size = 0;
		return nil;
	}
	*size = GetBigEndianWord(data);
	return data + 4;
}


// ROM 0x0019b09c GetROMDictionary__FUl
// ... and a dictionary opened over them, answering to that id.
Handle
GetROMDictionary(ULong id)
{
	ULong size = 0;
	const void* data = GetROMDictionaryData(id, &size);
	Handle dictionary = BuildDictionaryFromPtr((void*) data, (Size) size);
	if (dictionary == nil)
		return nil;
	((AirusAParmBlock*) *dictionary)->fDictID = (long) id;
	return dictionary;
}
