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
// imported from, so each address is taken as an offset into it; with no
// image imported the table stays empty and every dictionary of the ROM
// comes back as nothing, which is the same as a machine whose lexicons
// could not be built.
void
InitROMDictionaryData(void)
{
	ULong size = 0;
	const UByte* base = (const UByte*) ROMImageBase(&size);
	for (long i = 0; i < kROMDictionaryCount; i++)
	{
		ULong at = gROMDictionaryTable[i].fAddress;
		gROMDictionaryData[i] = (base != nil && at != 0 && at < size) ? base + at : nil;
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
