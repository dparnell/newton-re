/*
	File:		recognition/Airus.cpp

	Contains:	The Airus dictionary engine's container (Airus.h): a
				dictionary made, its bytes grown and shifted, and the
				selector call everything else goes through.

				NOT YET RECONSTRUCTED: the walkers themselves - AL, AL16
				and AEnum - so CallAirusA answers "nothing happened" for
				every selector but the two that only clear the error.
*/

#include "Airus.h"
#include "OSErrors.h"

#include <string.h>


// ROM 0x0c100810 airusResult
long				airusResult = 0;

// (0x0c100814, which the ROM keeps no symbol for)
// The word every new dictionary carries in its parameter block.  The
// ROM's initialised copy of it holds 0x00028d78 - the address of a word
// of padding just before NewDictionary - so it is a tag the engine knows
// its own blocks by rather than a number that means anything.
//
// DEVIATION: that address is the ROM's, and means nothing here; the host
// keeps zero, which is what the block carries and what nothing outside
// the engine reads.
ULong				gAirusVersion = 0;

// ROM 0x0c10082c AE_Parms
AirusAParmBlock*	AE_Parms = nil;

// ROM 0x0c105af4, 0x0c105bf4
// The state area a walker works in.  There is one for the 8-bit walkers
// and one for the 16-bit ones, shared by every dictionary of that kind -
// which is what makes the engine one-at-a-time.
static UByte		gAirusScratch8[256];		// ROM 0x0c105af4
static UByte		gAirusScratch16[256];		// ROM 0x0c105bf4


// ROM 0x00029944 CheckDictPtrs__FP15AirusAParmBlock
// The block's pointers read again after the Handle may have moved, and
// the block made the one the walkers work on.  The end moves with the
// start, so that how much is in use does not change.
void
CheckDictPtrs(AirusAParmBlock* parms)
{
	Ptr now = *parms->fDataHandle;
	if (now != parms->fData)
	{
		parms->fDataEnd = parms->fDataEnd + (now - parms->fData);
		parms->fData = *parms->fDataHandle;
	}
	AE_Parms = parms;
}


// ROM 0x00028d7c NewDictionary
// An empty dictionary: a parameter block behind a Handle, and two bytes
// of data saying what it is - 'a', then the kind with the size of each
// word's attribute above it.  Nil, and airusResult -2, when there is no
// room for either.
Handle
NewDictionary(UByte type, long attributeSize)
{
	Handle handle = NewHandle(sizeof(AirusAParmBlock));
	if (handle == nil)
	{
		airusResult = kAirusNoMemory;
		return nil;
	}
	AirusAParmBlock* parms = (AirusAParmBlock*) *handle;
	parms->fVersion = gAirusVersion;
	parms->fWord = ((type & 7) == kAirusKindEnum16 || (type & 7) == kAirusKindAL16)
					? gAirusScratch16 : gAirusScratch8;
	parms->fGrowBy = 100;
	parms->fField04 = -1;
	parms->fField3c = 1;
	parms->fField38 = 0;
	parms->fSelf = handle;
	parms->fField4c = 1;
	parms->fField44 = 0;
	parms->fAttributeSize = attributeSize;
	parms->fAttribute = 0;
	parms->fField48 = 0;
	parms->fSize = 2;

	Handle data = NewHandle(2);
	if (data == nil)
	{
		DisposHandle(handle);
		airusResult = kAirusNoMemory;
		return nil;
	}
	parms = (AirusAParmBlock*) *handle;
	parms->fDataHandle = data;
	parms->fData = *data;
	parms->fData[0] = (char) kAirusSignature;
	parms->fData[1] = (char) (type | (parms->fAttributeSize << 4));
	parms->fDataEnd = parms->fData + parms->fSize;
	airusResult = 0;
	return handle;
}


// ROM 0x0002998c ExpandDict__FUl
// Room made for that many more bytes, a growBy at a time; ==> 0, or 2
// when the Handle would not grow.  The answer is left in the block as
// well, which is where the walkers look for it.
long
ExpandDict(long extra)
{
	long err = 0;
	long needed = (AE_Parms->fDataEnd - AE_Parms->fData) + extra;
	while (AE_Parms->fSize < needed)
	{
		SetHandleSize(AE_Parms->fDataHandle, AE_Parms->fSize + AE_Parms->fGrowBy);
		if (MemError() != noErr)
		{
			err = kAirusExpandFailed;
			break;
		}
		CheckDictPtrs(AE_Parms);
		AE_Parms->fSize += AE_Parms->fGrowBy;
	}
	AE_Parms->fResult = err;
	return err;
}


// ROM 0x00028ec4 SlideUp__FUlT1
// The bytes from that offset on moved down over count of them, and the
// end brought in by as much: what is left when something is taken out.
void
SlideUp(long offset, long count)
{
	if (count == 0)
		return;
	Ptr at = AE_Parms->fData + offset;
	long n = AE_Parms->fDataEnd - at;
	if (n > 0)
		BlockMove(at, at - count, n);
	AE_Parms->fDataEnd -= count;
}


// ROM 0x00028f18 SlideDown__FUlT1
// ... and up, the end pushed out by as much: the room something new goes
// into.  The caller has made sure there is room (ExpandDict).
void
SlideDown(long offset, long count)
{
	if (count == 0)
		return;
	Ptr at = AE_Parms->fData + offset;
	long n = AE_Parms->fDataEnd - at;
	if (n > 0)
		BlockMove(at, at + count, n);
	AE_Parms->fDataEnd += count;
}


// ROM 0x0002a178 GetDictBytes__FUli
// Count bytes of the dictionary from that offset, as one big-endian
// number; nothing (0) for a count of none.
ULong
GetDictBytes(long offset, long count)
{
	ULong value = 0;
	while (count > 0)
	{
		UByte byte = (UByte) AE_Parms->fData[offset];
		if (count == 4)
			value |= (ULong) byte << 24;
		else if (count == 3)
			value |= (ULong) byte << 16;
		else if (count == 2)
			value |= (ULong) byte << 8;
		else if (count == 1)
			value |= byte;
		count--;
		offset++;
	}
	return value;
}


/*------------------------------------------------------------------------------
	R e a d i n g   a   n o d e

	The trie is a run of nodes.  Each begins with its character and a byte
	of flags; the flags' top two bits say how many more bytes the offset to
	the next sibling takes, bit 5 that the node has no children, and bit 4
	that an attribute follows.  The children come next, one after another,
	and the sibling after the last of them - which is what the offset
	skips over.
------------------------------------------------------------------------------*/

// The characters of the dictionary being walked.  The ROM writes this
// test out wherever it needs it rather than calling anything.
long
AirusCharSize(void)
{
	long kind = (UByte) (*AE_Parms->fDataHandle)[1] & 7;
	return (kind == kAirusKindEnum16 || kind == kAirusKindAL16) ? 2 : 1;
}


// ROM 0x0002b5a4 RPByteSize__FUl
// How many bytes past the flags the node's sibling offset takes: none
// for the two small classes (it is in the flags themselves), one for the
// next and three for the largest.
long
RPByteSize(long node)
{
	long size = ((UByte) AE_Parms->fData[node + AirusCharSize()] & kAirusSizeMask) >> 6;
	if (size == 0 || size == 1)
		return 0;
	if (size == 2)
		return 1;
	if (size == 3)
		return 3;
	return 0;
}


// ROM 0x0002b608 SkipNode__FUl
// The offset just past the node's character, flags and sibling offset -
// where its attribute lies, when it has one, and its first child when it
// does not.
long
SkipNode(long node)
{
	return node + AirusCharSize() + 1 + RPByteSize(node);
}


// ROM 0x0002b6b4 GetAttr__FUl
// The attribute lying at that offset, as many bytes as this dictionary
// gives each word.  A dictionary that gives none answers 0x80 when it is
// of the plain enumerated kind and nothing otherwise.
ULong
GetAttr(long offset)
{
	if (AE_Parms->fAttributeSize > 0)
		return GetDictBytes(offset, AE_Parms->fAttributeSize);
	return ((UByte) (*AE_Parms->fDataHandle)[1] & 7) == kAirusKindEnum ? 0x80 : 0;
}


// ROM 0x00028fa0 FollowLeft__FUl
// The offset of the node's first child: past its own bytes, and past its
// attribute when it has one.
long
FollowLeft(long node)
{
	UByte flags = (UByte) AE_Parms->fData[node + AirusCharSize()];
	if ((flags & kAirusHasAttribute) != 0 && AE_Parms->fAttributeSize > 0)
		return SkipNode(node) + AE_Parms->fAttributeSize;
	return SkipNode(node);
}


// ROM 0x00029318 GetSymbol__FUl
// The node's character.
ULong
GetSymbol(long node)
{
	return GetDictBytes(node, AirusCharSize()) & 0xffff;
}


/*------------------------------------------------------------------------------
	L o o k i n g   a   w o r d   u p
------------------------------------------------------------------------------*/

// ROM 0x0002b048 AE8_Verify__FP15AirusAParmBlock
// The characters in the block's word buffer followed down the trie, from
// the root or from the node a previous call stopped at (fNode), up to
// and including the character fIndex names.  What it finds is left in the block: fNode the node it
// ended at, fIndex how far it got, fAttribute the attribute of the word
// when it has one, fSymbol the character of the only child when there is
// only one, and fResult one of:
//
//   kAirusNoMatch (3)         no word begins that way
//   kAirusLeaf (2)            the word ends there and the node has no children
//   kAirusPrefixWithAttr (1)  the word leads on, and carries an attribute
//   kAirusPrefix (0)          the word leads on, and carries none
long
AE8_Verify(AirusAParmBlock* parms)
{
	CheckDictPtrs(parms);
	long node = AE_Parms->fNode;
	if (node == 0)
		node = 2;						// the root, just past the two bytes that say what this is
	Ptr p = parms->fData + node;
	UByte flags = (UByte) p[1];
	const UByte* word = parms->fWord;
	long attrSize = parms->fAttributeSize;
	long limit = AE_Parms->fIndex;		// the index of the last character to match
	AE_Parms->fSymbol = (ULong) -1;

	Boolean fresh = AE_Parms->fNode == 0;
	if (fresh)
	{
		AE_Parms->fIndex = 0;
		if (AE_Parms->fDataEnd - AE_Parms->fData == 2)
		{
			// nothing has been put in this dictionary yet
			AE_Parms->fResult = kAirusNoMatch;
			return kAirusNoMatch;
		}
	}
	UByte ch = 0;
	for (;;)
	{
		if (!fresh)
		{
			// down to the first child of the node the last call stopped at
			if ((flags & kAirusNoChildren) != 0)
			{
				AE_Parms->fResult = kAirusNoMatch;
				return kAirusNoMatch;
			}
			long size = (flags & kAirusSizeMask) >> 6;
			p += kAirusNodeSize[size];
			p += (flags & kAirusHasAttribute) != 0 ? attrSize : 0;
			flags = (UByte) p[1];
		}
		fresh = false;
		ch = word[AE_Parms->fIndex];

		// along the children until the character is found, or passed
		while ((UByte) p[0] < ch && (flags & kAirusSizeMask) != 0)
		{
			long size = (flags & kAirusSizeMask) >> 6;
			Ptr next = p + kAirusNodeSize[size];
			next += (flags & kAirusHasAttribute) != 0 ? attrSize : 0;
			ULong delta;
			if (size == 3)
				delta = ((((ULong) (flags & 0x0f) << 8 | (UByte) p[2]) << 8
						| (UByte) p[3]) << 8) | (UByte) p[4];
			else
				delta = (((ULong) flags << 8) | (UByte) p[size]) & kAirusRPMask[size];
			p = next + delta;
			flags = (UByte) p[1];
		}
		if ((UByte) p[0] != ch)
		{
			AE_Parms->fResult = kAirusNoMatch;
			return kAirusNoMatch;
		}
		node = p - AE_Parms->fData;
		AE_Parms->fNode = node;
		long index = AE_Parms->fIndex;
		AE_Parms->fIndex = index + 1;
		if (index >= limit)
			break;
	}

	// every character matched: what is at the end of them
	long charSize = AirusCharSize();
	Boolean hasAttribute = ((UByte) AE_Parms->fData[node + charSize] & kAirusHasAttribute) != 0;
	if (hasAttribute)
		AE_Parms->fAttribute = GetAttr(SkipNode(node));
	if (((UByte) AE_Parms->fData[node + charSize] & kAirusNoChildren) != 0)
	{
		AE_Parms->fResult = kAirusLeaf;
		return kAirusLeaf;
	}
	// when the only way on is one character, say which
	long child = FollowLeft(node);
	if ((((UByte) AE_Parms->fData[child + charSize] & kAirusSizeMask) >> 6) == 0)
		AE_Parms->fSymbol = GetSymbol(child);
	long result = hasAttribute ? kAirusPrefixWithAttr : kAirusPrefix;
	AE_Parms->fResult = result;
	return result;
}


// ROM 0x0002b584 AEnum_Verify__FP15AirusAParmBlock
// AE8 or AE16, by the characters the dictionary is written in.
long
AEnum_Verify(AirusAParmBlock* parms)
{
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	if (kind == kAirusKindEnum16 || kind == kAirusKindAL16)
	{
		// NOT YET RECONSTRUCTED: AE16_Verify 0x0002b2cc - the same walk over
		// two-byte characters
		parms->fResult = kAirusNoMatch;
		return kAirusNoMatch;
	}
	return AE8_Verify(parms);
}

/*------------------------------------------------------------------------------
	T h e   d i s p a t c h e r

	Every call into the engine names a dictionary and one of ten things to
	do with it.  The dictionary's own second byte says which family of
	walkers answers, and whether its Handle has to be locked down first.
------------------------------------------------------------------------------*/

// ROM 0x0002d574 CallAirusANoLock
void
CallAirusANoLock(Handle dictionary, long selector)
{
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	switch (kind)
	{
	case kAirusKindAL:
		// NOT YET RECONSTRUCTED: AL_Shell 0x0002b758
		break;
	case kAirusKindAL16:
		// NOT YET RECONSTRUCTED: AL16_Shell 0x0002b774
		break;
	case kAirusKindEnum:
	case kAirusKindEnum16:
	case kAirusKindEnumRAM:
		switch (selector)
		{
		case kAirusVerify:
			AEnum_Verify(parms);
			break;
		case kAirusStartA:
		case kAirusExitA:
			// the two that only say nothing has gone wrong yet
			// (AEnum_StartA 0x0002a148, AEnum_ExitA 0x0002a160)
			parms->fResult = 0;
			break;
		default:
			// NOT YET RECONSTRUCTED: the rest of the AEnum walkers -
			// AddWord 0x00029b10, DeleteWord 0x00029e3c, FirstLast
			// 0x0002a1f4, NextPrevious 0x0002a244, ChangeAttribute
			// 0x0002a7cc, NextSet 0x0002afd0, NextSet9 0x0002af18
			break;
		}
		break;
	}
}


// ROM 0x0002d41c CallAirusA
// The same, with the Handle moved up and locked while the walker runs
// when the dictionary's kind byte asks for it.
void
CallAirusA(Handle dictionary, long selector)
{
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	Boolean locked = ((UByte) (*parms->fDataHandle)[1] & kAirusLockedBit) != 0;
	if (locked)
	{
		MoveHHi(dictionary);
		HLock(dictionary);
	}
	CallAirusANoLock(dictionary, selector);
	parms = (AirusAParmBlock*) *dictionary;
	if (((UByte) (*parms->fDataHandle)[1] & kAirusLockedBit) != 0)
		HUnlock(dictionary);
}
