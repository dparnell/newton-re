/*
	File:		recognition/Airus.cpp

	Contains:	The Airus dictionary engine's container (Airus.h): a
				dictionary made, its bytes grown and shifted, and the
				selector call everything else goes through.

				NOT YET RECONSTRUCTED: the walkers themselves - AL, AL16
				and the two lexicon walkers, AL and AL16, which look a
				word up in the dictionaries built into the ROM.
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


// ROM 0x0002d0b0 BuildDictionaryFromHandle
// A dictionary opened over bytes that already exist - the ones built
// into the ROM.  It is `NewDictionary` the other way round: rather than
// making an empty trie to write into, it takes a finished one and fills
// in the block that describes it, reading the attribute size and the
// kind out of the second header byte.
//
// The bytes are not copied and not owned: a ROM dictionary is read where
// it lies.  ==> the block's Handle, or nil with `airusResult` saying
// why.
Handle
BuildDictionaryFromHandle(Handle data)
{
	Handle handle = NewHandle(sizeof(AirusAParmBlock));
	if (handle == nil)
	{
		airusResult = kAirusNoMemory;
		return nil;
	}
	AirusAParmBlock* parms = (AirusAParmBlock*) *handle;
	parms->fVersion = gAirusVersion;
	parms->fGrowBy = 100;
	parms->fDictID = 0;
	parms->fField3c = 1;
	parms->fField38 = 0;
	parms->fCurrent = handle;
	parms->fNext = nil;
	parms->fField4c = 1;
	parms->fDataHandle = data;
	parms->fData = *data;
	long size = GetHandleSize(data);
	parms->fSize = size;

	long kind = size >= 2 ? ((UByte) parms->fData[1] & 7) : 0;
	if (size < 2 || (UByte) parms->fData[0] != kAirusSignature
		|| !(kind == kAirusKindAL || kind == kAirusKindAL16
			 || kind == kAirusKindEnum || kind == kAirusKindEnum16
			 || kind == kAirusKindEnumRAM || kind == 6))
	{
		// (kind 6 is accepted here and nowhere else; no dictionary in
		//  this ROM is one)
		DisposHandle(handle);
		airusResult = kAirusBadDictionary;
		return nil;
	}

	parms->fDataEnd = parms->fData + size;
	parms->fAttributeSize = (UByte) parms->fData[1] >> 4;
	parms->fAttribute = 0;
	parms->fField48 = 0;
	parms->fVersion = gAirusVersion;
	parms->fWord = (kind == kAirusKindEnum16 || kind == kAirusKindAL16)
				   ? gAirusScratch16 : gAirusScratch8;
	airusResult = 0;
	return handle;
}


// ROM 0x0002d624 BuildDictionaryFromPtr
// The same over bytes that are not in a Handle at all, which is how the
// ROM's own dictionaries are opened: a fake Handle is made over them
// first.
Handle
BuildDictionaryFromPtr(void* data, Size size)
{
	Handle fake = NewFakeHandle(data, size);
	if (fake == nil)
	{
		airusResult = kAirusNoMemory;
		return nil;
	}
	return BuildDictionaryFromHandle(fake);
}


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
	parms->fDictID = -1;
	parms->fField3c = 1;
	parms->fField38 = 0;
	parms->fCurrent = handle;
	parms->fField4c = 1;
	parms->fNext = nil;
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
	W r i t i n g   a   n o d e

	The sibling offset - the ROM calls it the RP - is kept in nibbles: one
	in the flag byte's low half, and the rest in the bytes after it.  Its
	size class says how many: one nibble, three, or seven.  Changing it
	means making room or taking it away, which moves everything after this
	node along, and is why adding a word ends by fixing up the offsets that
	pointed over the place it grew.
------------------------------------------------------------------------------*/

// ROM 0x000298b0 PutDictBytes__FUliT1
// Count bytes of that value written big-endian at the offset.
void
PutDictBytes(long offset, long count, ULong value)
{
	while (count > 0)
	{
		char byte;
		if (count == 4)
			byte = (char) (value >> 24);
		else if (count == 3)
			byte = (char) (value >> 16);
		else if (count == 2)
			byte = (char) (value >> 8);
		else
			byte = (char) value;
		AE_Parms->fData[offset] = byte;
		count--;
		offset++;
	}
}


// ROM 0x0002e6fc Ashortstrlen__FPUs
// The bytes a UniChar string takes, not counting its terminator - twice
// the characters in it.
long
Ashortstrlen(const UniChar* s)
{
	const UniChar* p = s;
	while (*p++ != 0)
		;
	return ((p - s) * 2) - 2;
}


// ROM 0x00029a20 CopyBufferHack__FPUcPUsi
// The word moved between the bytes the caller handed in and the UniChars
// the adding works in.  A 16-bit dictionary's word is already UniChars,
// so nothing is copied and the buffer it was in is the answer.
UniChar*
CopyBufferHack(UByte* bytes, UniChar* chars, long back)
{
	long kind = (UByte) (*AE_Parms->fDataHandle)[1] & 7;
	if (kind == kAirusKindEnum16 || kind == kAirusKindAL16)
		return back == 0 ? (UniChar*) bytes : chars;
	if (back == 0)
	{
		long length = (long) strlen((const char*) bytes);
		long i = 0;
		for (; i < length; i++)
			chars[i] = bytes[i];
		chars[i] = 0;
		return chars + i;
	}
	long length = Ashortstrlen(chars) / 2;
	long i = 0;
	for (; i < length; i++)
		bytes[i] = (UByte) chars[i];
	bytes[i] = 0;
	return (UniChar*) (bytes + i);
}


// ROM 0x0002af64 RPNibbleSize__FUl
// The nibbles the node's sibling offset takes, by its size class.
long
RPNibbleSize(long node)
{
	long size = (UByte) AE_Parms->fData[node + AirusCharSize()] >> 6;
	if (size == 0)
		return 0;
	if (size == 1)
		return 1;
	if (size == 2)
		return 3;
	if (size == 3)
		return 7;
	return 0;
}


// ROM 0x0002900c GetRP__FUl
// The node's sibling offset: the low nibble of its flags at the top, and
// the bytes after them below.
ULong
GetRP(long node)
{
	long nibbles = RPNibbleSize(node);
	if (nibbles <= 0)
		return 0;
	ULong value = GetDictBytes(node + AirusCharSize(), 1);
	nibbles--;
	value = (value & 0x0f) << (nibbles * 4);
	if (nibbles > 0)
		value |= GetDictBytes(node + AirusCharSize() + 1, nibbles / 2);
	return value;
}


// ROM 0x0002b654 SetRPFlags__FUli
// The node's size class, the rest of its flags left as they are.
void
SetRPFlags(long node, long size)
{
	UByte flags = (UByte) AE_Parms->fData[node + AirusCharSize()];
	flags = (UByte) ((flags & 0x3f) | ((size & 3) << 6));
	AE_Parms->fData[node + AirusCharSize()] = (char) flags;
}


// ROM 0x000290c4 FollowRight__FUl
// The offset of the node's sibling: past its own bytes, past its
// attribute when it has one, and on by its sibling offset - which is
// what carries it over all of its children.
long
FollowRight(long node)
{
	UByte flags = (UByte) AE_Parms->fData[node + AirusCharSize()];
	if ((flags & kAirusHasAttribute) != 0 && AE_Parms->fAttributeSize != 0)
		return SkipNode(node) + AE_Parms->fAttributeSize + GetRP(node);
	return SkipNode(node) + GetRP(node);
}


// ROM 0x00029158 ClearRP__FUl
// The node's sibling offset taken away; ==> the bytes that went.
long
ClearRP(long node)
{
	long size = RPByteSize(node);
	if (size > 0)
		SlideUp(SkipNode(node), size);
	SetRPFlags(node, 0);
	return size;
}


// ROM 0x0002919c PutRP__FUlT1
// The node given that sibling offset, in as few nibbles as will hold it,
// the bytes after the flags made to fit; ==> how much the data grew (or
// shrank, as a negative).
//
// The ROM has no case for an offset of 0x10000000 or more: it uses
// whatever was left in the registers.  A dictionary that big cannot say
// where its siblings are, so the largest class is taken here and the
// offset written as far as it goes - which is the nearest thing to what
// the ROM does that can be written down.
long
PutRP(long node, ULong offset)
{
	long was = RPByteSize(node);
	long nibbles = 7;
	long size = 3;
	if ((offset & ~0x0fUL) == 0)
	{
		nibbles = 1;
		size = 1;
	}
	else if ((long) offset >> 12 == 0)
	{
		nibbles = 3;
		size = 2;
	}
	long grew = (nibbles / 2) - was;
	if (grew > 0)
		SlideDown(node + AirusCharSize() + 1, grew);
	else if (grew < 0)
		SlideUp(SkipNode(node), -grew);
	SetRPFlags(node, size);
	nibbles--;
	ULong top = (offset >> (nibbles * 4)) & 0x0f;
	ULong flags = GetDictBytes(node + AirusCharSize(), 1);
	PutDictBytes(node + AirusCharSize(), 1, (flags & 0xf0) | top);
	if (nibbles > 0)
		PutDictBytes(node + AirusCharSize() + 1, nibbles / 2, offset);
	return grew;
}


// ROM 0x0002b708 PutAttr__FUl
// Room made at the offset for this dictionary's attribute, and the one
// in the block written there; ==> the bytes it took.
long
PutAttr(long offset)
{
	long size = AE_Parms->fAttributeSize;
	if (size > 0)
	{
		SlideDown(offset, size);
		PutDictBytes(offset, size, AE_Parms->fAttribute);
	}
	return size;
}


// ROM 0x00028f6c ClearAttr__FUl
// ... and taken away again; ==> the bytes that went.
long
ClearAttr(long offset)
{
	long size = AE_Parms->fAttributeSize;
	if (size > 0)
		SlideUp(offset + size, size);
	return size;
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
	A d d i n g   a   w o r d

	The word is followed down the trie as far as it already goes, and what
	is left of it written in as a row of nodes.  Making room for them moves
	everything after the place they went, so every sibling offset that
	reached over that place has to be made longer - which may itself need
	more bytes, which moves things again.  The nodes whose offsets those
	are were put on a stack on the way down.
------------------------------------------------------------------------------*/

// ROM 0x0c105df4, 0x0c100830
// The nodes whose sibling offsets will have to be fixed after the word
// goes in, and where the top of that stack is: 0x3f is empty, and it
// grows downwards.
static ULong	gAirusFixups[0x3f];
static long		gAirusFixupTop = 0x3f;


// ROM 0x00029630 FindInsertionPoint__FPPUsPUl
// The word followed down the trie for as long as it is already there.
// What is left of it is left in `word`, and where the rest of it goes in
// `node`; the nodes passed on the way whose sibling offsets reach over
// that place are put on the fixup stack.  ==> how much the data grew,
// which is not nothing when a sibling offset had to be written to reach
// the new row.
static long
FindInsertionPoint(UniChar** word, long* node)
{
	if (AE_Parms->fDataEnd - AE_Parms->fData == 2)
	{
		// nothing in it yet: the first word goes straight after the header
		*node = 2;
		return 0;
	}
	long at = 2;
	long grew = 0;
	*node = 0;
	for (;;)
	{
		UniChar ch = **word;
		if (ch == 0 || *node != 0)
			return grew;
		long charSize = AirusCharSize();
		ULong symbol = GetSymbol(at);
		if (ch < symbol)
		{
			// the new node goes in front of this one, which keeps its place
			*node = at;
			gAirusFixups[--gAirusFixupTop] = at;
		}
		else if (ch == symbol)
		{
			(*word)++;
			if (((UByte) AE_Parms->fData[at + charSize] >> 6) != 0)
				gAirusFixups[--gAirusFixupTop] = at;
			if (**word == 0)
			{
				// the whole word was there already
				*node = at;
				if (((UByte) AE_Parms->fData[at + charSize] >> 6) != 0 && gAirusFixupTop < 0x3f)
					gAirusFixupTop++;		// nothing grew, so that one needs nothing
			}
			else if (((UByte) AE_Parms->fData[at + charSize] & kAirusNoChildren) != 0)
			{
				// a word that ends here until now: it grows children
				AE_Parms->fData[at + charSize] = (char) ((UByte) AE_Parms->fData[at + charSize] & ~kAirusNoChildren);
				*node = FollowLeft(at);
			}
			else
				at = FollowLeft(at);
		}
		else if (((UByte) AE_Parms->fData[at + charSize] >> 6) == 0)
		{
			// past the last of this row: the new node goes after everything
			// the row leads to, and this one is given the offset to reach it
			long after = gAirusFixupTop == 0x3f ? 0 : (long) gAirusFixups[gAirusFixupTop];
			after = after == 0 ? (long) (AE_Parms->fDataEnd - AE_Parms->fData) : FollowRight(after);
			*node = after;
			grew += PutRP(at, (ULong) (*node - FollowLeft(at)));
			*node = *node + grew;
		}
		else
			at = FollowRight(at);
	}
}


// ROM 0x00029360 PutWord__FUlPUs
// What is left of the word written in at that offset, one node per
// character, each the only one of its row; the last of them carries the
// attribute and has no children.  ==> the bytes it took.
static long
PutWord(long node, const UniChar* word)
{
	long grew = 0;
	long charSize = AirusCharSize();
	long nodeSize = charSize + 1;
	SlideDown(node, nodeSize * (Ashortstrlen(word) / 2));
	while (*word != 0)
	{
		PutDictBytes(node, charSize, *word);
		PutDictBytes(node + charSize, 1, 0);
		node += nodeSize;
		grew += nodeSize;
		word++;
	}
	long last = node - nodeSize;
	AE_Parms->fData[last + charSize] = (char) ((UByte) AE_Parms->fData[last + charSize] | kAirusHasAttribute);
	AE_Parms->fData[last + charSize] = (char) ((UByte) AE_Parms->fData[last + charSize] | kAirusNoChildren);
	return PutAttr(SkipNode(last)) + grew;
}


// ROM 0x000294a8 FixupPointers__FUl9Operation
// The sibling offsets that reached over the place the word went, made to
// reach over it still.  Each may take more bytes than it did, which
// moves everything after it again, so what has grown is carried along
// the stack.
//
// NOT YET RECONSTRUCTED: the other operation, which is what deleting a
// word asks for.
static void
FixupPointers(long grew, long operation)
{
	if (grew == 0)
		return;
	if (operation != 0)
		return;
	while (gAirusFixupTop != 0x3f)
	{
		long node = gAirusFixupTop < 0x3f ? (long) gAirusFixups[gAirusFixupTop++] : 0;
		long charSize = AirusCharSize();
		ULong value;
		if (((UByte) AE_Parms->fData[node + charSize] >> 6) == 0)
		{
			// it had no sibling before: the one it has now lies just past
			// what went in
			ULong attribute = ((UByte) AE_Parms->fData[node + charSize] & kAirusHasAttribute) != 0
							? (ULong) AE_Parms->fAttributeSize : 0;
			value = (ULong) (grew - (charSize + 1)) - attribute;
		}
		else
			value = GetRP(node) + grew;
		grew += PutRP(node, value);
	}
}


// ROM 0x00029b10 AEnum_AddWord__FP15AirusAParmBlock
// The word in the block's buffer put into the dictionary.  A word that
// is already there is left alone and its attribute read back instead,
// unless it had none, in which case it is given the one in the block.
// The block's fResult says which: 0 it went in, 1 it was already there.
long
AEnum_AddWord(AirusAParmBlock* parms)
{
	CheckDictPtrs(parms);
	UniChar buffer[256];
	CopyBufferHack(AE_Parms->fWord, buffer, 0);
	UniChar* word = buffer;
	long characters = Ashortstrlen(word) / 2;
	long charSize = AirusCharSize();
	long result = ExpandDict((charSize + 1) * characters + AE_Parms->fAttributeSize + 100);
	if (result == 0)
	{
		gAirusFixupTop = 0x3f;
		long node = 0;
		long grew = FindInsertionPoint(&word, &node);
		if (*word == 0)
		{
			// every character of it was there already
			char* flags = &AE_Parms->fData[node + charSize];
			if (((UByte) *flags & kAirusHasAttribute) == 0)
			{
				*flags = (char) ((UByte) *flags | kAirusHasAttribute);
				grew += PutAttr(SkipNode(node));
				result = 0;
			}
			else
			{
				AE_Parms->fAttribute = GetAttr(SkipNode(node));
				result = 1;
			}
		}
		else
		{
			grew += PutWord(node, word);
			result = 0;
		}
		if (gAirusFixupTop != 0x3f)
			FixupPointers(grew, 0);
	}
	AE_Parms->fResult = result;
	return result;
}


// ROM 0x0002d658 PositionToHandle
// The nth dictionary of a chain, counting from the one handed in.  Nil,
// and airusResult -12, when the chain is shorter than that.
Handle
PositionToHandle(Handle dictionary, ULong position)
{
	for (ULong i = 0; i < position; i++)
	{
		dictionary = ((AirusAParmBlock*) *dictionary)->fNext;
		if (dictionary == nil)
		{
			airusResult = kAirusNoSuchDictionary;
			return nil;
		}
	}
	airusResult = 0;
	return dictionary;
}


// ROM 0x0002c48c AddWord__FPP15AirusAParmBlockUlPUcT2
// A word added to the nth dictionary of the chain, with the attribute to
// give it.  airusResult afterwards: 0 it went in, 4 it was already
// there, 5 the word was empty, -2 there was no room.
void
AddWord(Handle dictionary, ULong position, UByte* word, ULong attribute)
{
	Handle h = PositionToHandle(dictionary, position);
	if (airusResult != 0)
		return;
	AirusAParmBlock* parms = (AirusAParmBlock*) *h;
	parms->fWord = word;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	Boolean empty = (kind == kAirusKindEnum16 || kind == kAirusKindAL16)
				   ? *(const UniChar*) word == 0 : word[0] == 0;
	if (empty)
	{
		airusResult = kAirusEmptyWord;
		return;
	}
	if (parms->fAttributeSize == 1)
		parms->fAttribute = attribute & 0xff;
	else if (parms->fAttributeSize == 2 || parms->fAttributeSize == 4)
		parms->fAttribute = attribute;
	parms->fField48 = 0;
	CallAirusA(h, kAirusAddWord);
	long result = ((AirusAParmBlock*) *h)->fResult;
	if (result == 0)
		airusResult = 0;
	else
		airusResult = result == 1 ? kAirusAlreadyThere : kAirusNoMemory;
}

/*------------------------------------------------------------------------------
	T h e   w a y   i n

	A caller does not touch the block: it asks VerifyStart to put the
	dictionaries of a chain back to their beginning, and then VerifyString
	for a whole word at once.  What comes back is airusResult, and
	pointers to the character that would come next and to the word's
	attribute - both into the engine's own globals, and nil when there is
	none.
------------------------------------------------------------------------------*/

// ROM 0x0c100818, 0x0c10081c, 0x0c100824, 0x0c100828
// Where VerifyString leaves what it found, so that it can hand back a
// pointer to it.
static UByte	gAirusTerminal8 = 0;
static UniChar	gAirusTerminal16 = 0;
static ULong	gAirusVerifyAttribute = 0;
static ULong	gAirusVerifyExtra = 0;


// ROM 0x0002e6e0 Astrlen__FPc
// The engine's own string handling, over the bytes and the UniChars it
// keeps words in.
long
Astrlen(const char* s)
{
	return (long) strlen(s);
}


// ROM 0x0002e738 Astrcpy__FPcT1
void
Astrcpy(char* dest, const char* src)
{
	strcpy(dest, src);
}


// ROM 0x0002e764 Ashortstrcpy__FPUsT1
void
Ashortstrcpy(UniChar* dest, const UniChar* src)
{
	while ((*dest++ = *src++) != 0)
		;
}


// ROM 0x0002c770 HasActualOrImpliedAtr__FPP15AirusAParmBlock
// Whether a word of this dictionary carries an attribute at all: one it
// keeps, or the 0x80 the plain enumerated kind is taken to mean.
Boolean
HasActualOrImpliedAtr(Handle dictionary)
{
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	if (parms->fAttributeSize != 0)
		return true;
	return ((UByte) (*parms->fDataHandle)[1] & 7) == kAirusKindEnum;
}


// ROM 0x0002c6a8 NewVerifyReset
// Every dictionary of the chain put back to its beginning, and the one
// at that position made the one the walk is on.  A word given here is
// the one it will start from.
void
NewVerifyReset(Handle dictionary, ULong position, long node, const UByte* word)
{
	Handle at = PositionToHandle(dictionary, position);
	if (airusResult != 0)
		return;
	Handle p = dictionary;
	do
	{
		AirusAParmBlock* parms = (AirusAParmBlock*) *p;
		parms->fIndex = 0;
		parms->fResult = 0;
		parms->fWord = gAirusScratch8;
		parms->fCurrent = at;
		if (p == at)
		{
			parms->fNode = node;
			if (word != nil && word[0] != 0)
			{
				Astrcpy((char*) gAirusScratch8, (const char*) word);
				parms->fIndex = Astrlen((const char*) word);
			}
		}
		else
			parms->fNode = 0;
		p = parms->fNext;
	}
	while (p != nil);
}


// ROM 0x0002c760 VerifyStart__FPP15AirusAParmBlock
// The chain put back to the beginning, with nothing looked at yet.
void
VerifyStart(Handle dictionary)
{
	NewVerifyReset(dictionary, 0, 0, nil);
}


// ROM 0x0002cd20 VerifyString
// A whole word looked up.  airusResult afterwards: 1 the word is the
// beginning of others and is not one itself, 2 it is the beginning of
// others and is one as well, 3 it is a word and nothing goes on from it,
// -6 nothing begins that way.  `terminal` is given the character that
// would come next when there is only one, and `attribute` the word's
// attribute; either is nil when there is none.
//
// The ROM's callers hand in four arguments where it takes five, so its
// last out-parameter is whatever was in the register.  Nothing here
// passes anything but nil for it.
void
VerifyString(Handle dictionary, const void* word, void** terminal, ULong** attribute, ULong* extra)
{
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	Boolean wide = kind == kAirusKindEnum16 || kind == kAirusKindAL16;
	ULong* foundAttribute = &gAirusVerifyAttribute;
	ULong found = gAirusVerifyExtra;
	void* foundTerminal;
	long length;
	if (!wide)
	{
		Astrcpy((char*) gAirusScratch8, (const char*) word);
		parms->fWord = gAirusScratch8;
		length = Astrlen((const char*) gAirusScratch8);
		foundTerminal = &gAirusTerminal8;
	}
	else
	{
		Ashortstrcpy((UniChar*) gAirusScratch16, (const UniChar*) word);
		parms->fWord = gAirusScratch16;
		length = Ashortstrlen((const UniChar*) gAirusScratch16) / 2;
		foundTerminal = &gAirusTerminal16;
	}
	parms->fIndex = length - 1;
	parms->fNode = 0;
	CallAirusA(dictionary, kAirusVerify);
	parms = (AirusAParmBlock*) *dictionary;
	long symbol = (long) parms->fSymbol;
	if (symbol == -1)
		foundTerminal = nil;
	else if (!wide)
		gAirusTerminal8 = (UByte) symbol;
	else
		gAirusTerminal16 = (UniChar) symbol;
	switch (parms->fResult)
	{
	case kAirusPrefix:
		foundAttribute = nil;
		airusResult = kAirusIsPrefix;
		found = 0;
		break;
	case kAirusPrefixWithAttr:
	case kAirusLeaf:
		airusResult = parms->fResult == kAirusPrefixWithAttr ? kAirusIsPrefixAndWord : kAirusIsWord;
		if (!HasActualOrImpliedAtr(dictionary))
			foundAttribute = nil;
		else
			gAirusVerifyAttribute = parms->fAttribute;
		found = HasActualOrImpliedAtr(dictionary) ? parms->fField48 : 0;
		break;
	case kAirusNoMatch:
		foundAttribute = nil;
		airusResult = kAirusNotAWord;
		found = 0;
		break;
	}
	if (terminal != nil)
		*terminal = foundTerminal;
	if (attribute != nil)
		*attribute = foundAttribute;
	if (extra != nil)
		*extra = found;
}

/*------------------------------------------------------------------------------
	T h e   d i s p a t c h e r

	Every call into the engine names a dictionary and one of ten things to
	do with it.  The dictionary's own second byte says which family of
	walkers answers, and whether its Handle has to be locked down first.
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	T h e   R O M ' s   o w n   l e x i c o n s   ( A L ,   A L 1 6 )
------------------------------------------------------------------------------*/

// The dictionaries built into the ROM are not the ones the machine
// writes.  They are read-only tries in a different shape, and they have
// their own walker - two of them, one for eight-bit characters and one
// for sixteen.
//
// A node is:
//
//     +0  the offset of its character set, sixteen bits
//     +2  flags: 1 it carries an attribute, 2 it has no children,
//         4 it is the last of its siblings
//     +3  the offset of its first child, sixteen bits - absent when the
//         node has no children, so a leaf is three bytes and any other
//         node five, plus the dictionary's attribute size
//
// The character set is what makes this shape worth having: a node does
// not stand for one character but for a *set* of them, all of which lead
// to the same place.  Matching a character means looking for it in that
// set; the sets are shared, which is where the saving is.
//
// The walk itself is the same both ways: match a character in the node's
// set, step to the child, and go on until the word runs out or nothing
// matches.  What comes back is one of the four answers `VerifyString`
// hands on - a prefix, a prefix that is also a word, a whole word, or
// nothing - and, when the word can only go on one way, the single
// character it must go on with.

// ROM 0x0002c440 (unnamed) - AL_Prep
// The block made current and its pointers re-read, the Handle having
// possibly moved since the last call.  (`AL16_Prep` 0x0002bda8 is the
// same function again; the two walkers keep their own copy of it and
// their own current block.)
static void
AL_Prep(AirusAParmBlock* parms)
{
	AE_Parms = parms;
	Ptr now = *parms->fDataHandle;
	if (now < parms->fData)
		parms->fDataEnd = parms->fDataEnd - (parms->fData - now);
	else
		parms->fDataEnd = parms->fDataEnd + (now - parms->fData);
	parms->fData = *parms->fDataHandle;
}


// ROM 0x0002c3f8 (unnamed) - the offset of a node's character set
static long
AL_SymbolOffset(long node)
{
	const UByte* data = (const UByte*) AE_Parms->fData;
	return (data[node] << 8) | data[node + 1];
}


// ROM 0x0002c41c (unnamed) - the offset of a node's first child
static long
AL_FollowLeft(long node)
{
	const UByte* data = (const UByte*) AE_Parms->fData;
	return (data[node + 3] << 8) | data[node + 4];
}


// The bytes a node takes: three for a leaf, five for anything else,
// plus the dictionary's attribute.
static long
AL_NodeSize(UByte flags)
{
	return ((flags & 2) != 0 ? 3 : 5) + AE_Parms->fAttributeSize;
}


// ROM 0x0002be40 AL_GetAttribute__FUl
// The attribute lying after the node, as many bytes of it as the
// dictionary says, most significant first.
static void
AL_GetAttribute(long node)
{
	AE_Parms->fAttribute = 0;
	const UByte* data = (const UByte*) AE_Parms->fData;
	long at = node + ((data[node + 2] & 2) == 0 ? 5 : 3);
	switch (AE_Parms->fAttributeSize)
	{
	case 1:
		AE_Parms->fAttribute = data[at];
		break;
	case 2:
		AE_Parms->fAttribute = ((ULong) data[at] << 8) | data[at + 1];
		break;
	case 4:
		AE_Parms->fAttribute = ((ULong) data[at] << 24) | ((ULong) data[at + 1] << 16)
							   | ((ULong) data[at + 2] << 8) | data[at + 3];
		break;
	default:
		break;
	}
}


// ROM 0x0002bf28 AL_GetAttribute2__FUl
// What lies after the node's character set, which is what the caller
// gets back beside the attribute.
static void
AL_GetAttribute2(long node)
{
	const char* set = AE_Parms->fData + AL_SymbolOffset(node);
	AE_Parms->fField48 = (ULong) (uintptr_t) (set + Astrlen(set) + 1);
}


// ROM 0x0002bf78 AL_Verify__FP15AirusAParmBlock
// The word walked into the lexicon as far as it goes.
//
// `fNode` says where to start: nought is the root (the two header bytes
// are skipped), and anything else carries on from a node a previous call
// left.  `fIndex` is how far along the word to go; it comes back as how
// far the walk actually got, and `fNode` as the node it ended on.
void
AL_Verify(AirusAParmBlock* parms)
{
	ULong only = 0;			// the one character the word can go on with
	AL_Prep(parms);
	long node = AE_Parms->fNode;
	if (node == 0)
		node = 2;			// past the two header bytes
	long want = AE_Parms->fIndex;
	long result = kAirusNoMatch;
	AE_Parms->fSymbol = 0xffffffff;

	Boolean descend = AE_Parms->fNode != 0;
	if (!descend)
	{
		AE_Parms->fIndex = 0;
		if (AE_Parms->fDataEnd - AE_Parms->fData == 2)
		{
			// nothing in it but the header
			AE_Parms->fResult = kAirusNoMatch;
			return;
		}
	}

	long child = 0;
	Boolean tail = false;
	for (;;)
	{
		if (descend)
		{
			// the node matched: on to its children
			if ((((const UByte*) AE_Parms->fData)[node + 2] & 2) != 0)
			{
				result = kAirusNoMatch;
				break;
			}
			node = AL_FollowLeft(node);
			descend = false;
		}

		char c = ((const char*) AE_Parms->fWord)[AE_Parms->fIndex];
		Boolean matched = false;
		for (;;)
		{
			const char* set = AE_Parms->fData + AL_SymbolOffset(node);
			for (const char* p = set; *p != 0; p++)
			{
				if (c == *p)
				{
					matched = true;
					break;
				}
			}
			if (matched)
				break;
			UByte flags = ((const UByte*) AE_Parms->fData)[node + 2];
			if ((flags & 4) != 0)
				break;			// the last sibling: no word begins that way
			node = node + AL_NodeSize(flags);
		}
		if (!matched)
		{
			result = kAirusNoMatch;
			break;
		}

		AE_Parms->fNode = node;
		AL_GetAttribute(node);
		AL_GetAttribute2(node);
		long was = AE_Parms->fIndex;
		AE_Parms->fIndex = was + 1;
		if (was < want)
		{
			descend = true;
			continue;
		}
		// the word is used up
		UByte flags = ((const UByte*) AE_Parms->fData)[node + 2];
		result = (flags & 1) != 0 ? kAirusPrefixWithAttr : kAirusPrefix;
		if ((flags & 2) != 0)
		{
			result = kAirusLeaf;	// nothing goes on from here
			break;
		}
		child = AL_FollowLeft(node);
		tail = true;
		break;
	}

	// when everything that could follow is the same single character,
	// that character is handed back: it is what the corrector offers
	while (tail)
	{
		const UByte* data = (const UByte*) AE_Parms->fData;
		long at = AL_SymbolOffset(child);
		if (data[at + 1] != 0)
			break;				// more than one character here
		ULong c = data[at];
		if (only != 0 && c != only)
			break;				// and they are not all the same
		only = c;
		UByte flags = data[child + 2];
		if ((flags & 4) != 0)
		{
			AE_Parms->fSymbol = only;
			break;
		}
		child = child + AL_NodeSize(flags);
	}
	AE_Parms->fResult = result;
}


// ROM 0x0002e7a8 Astrchr__FPcc
// The first of that character in the string, or nil.
char*
Astrchr(char* str, char c)
{
	for (; *str != c && *str != 0; str++)
		;
	return *str == c ? str : nil;
}


// ROM 0x0002c170 AL_FilterString__FPc
// A string with its repeated characters taken out, which is what a
// character set is: each character once, in the order it first appeared.
void
AL_FilterString(char* str)
{
	char seen[256];
	long length = Astrlen(str);
	seen[0] = 0;
	long kept = 0;
	for (long i = 0; i < length; i++)
	{
		if (Astrchr(seen, str[i]) == nil)
		{
			seen[kept++] = str[i];
			seen[kept] = 0;
		}
	}
	Astrcpy(str, seen);
}


// ROM 0x0002bdf4 AirusAL__FUlP15AirusAParmBlock
// The eight-bit lexicon's three operations.  (Anything else is not
// something a read-only dictionary can do.)
long
AirusAL(ULong selector, AirusAParmBlock* parms)
{
	if (selector == kAirusVerify)
		AL_Verify(parms);
	// NOT YET RECONSTRUCTED: AL_NextSet 0x0002c214 and AL_NextSet9
	// 0x0002c268, which walk the set of characters that may follow -
	// what the corrector's completions are built from.
	return parms->fResult;
}


/*------------------------------------------------------------------------------
	T h e   s i x t e e n - b i t   o n e
------------------------------------------------------------------------------*/

// The same walk again over sixteen-bit characters.  The ROM keeps the
// two apart rather than parameterising them, down to its own copy of the
// preparation and its own current block, so they are kept apart here.

// ROM 0x0002bda8 AL16_Prep__FP15AirusAParmBlock
static void
AL16_Prep(AirusAParmBlock* parms)
{
	Ptr now = *parms->fDataHandle;
	if (now < parms->fData)
		parms->fDataEnd = parms->fDataEnd - (parms->fData - now);
	else
		parms->fDataEnd = parms->fDataEnd + (now - parms->fData);
	AE_Parms = parms;
	parms->fData = *parms->fDataHandle;
}


// ROM 0x0002bd60 AL16_ClassOffset__FUl
static long
AL16_ClassOffset(long node)
{
	const UByte* data = (const UByte*) AE_Parms->fData;
	return (data[node] << 8) | data[node + 1];
}


// ROM 0x0002bd84 AL16_LBNode__FUl
static long
AL16_LBNode(long node)
{
	const UByte* data = (const UByte*) AE_Parms->fData;
	return (data[node + 3] << 8) | data[node + 4];
}


// The characters of a sixteen-bit node's set, big-endian in the data.
static ULong
AL16_Symbol(long at)
{
	const UByte* data = (const UByte*) AE_Parms->fData;
	return ((ULong) data[at] << 8) | data[at + 1];
}


// ROM 0x0002b7dc AL16_GetAttribute__FUl
static void
AL16_GetAttribute(long node)
{
	AL_GetAttribute(node);		// (the ROM writes the same code out twice)
}


// ROM 0x0002b8c4 AL16_GetAttribute2__FUl
static void
AL16_GetAttribute2(long node)
{
	long at = AL16_ClassOffset(node);
	while (AL16_Symbol(at) != 0)
		at += 2;
	AE_Parms->fField48 = (ULong) (uintptr_t) (AE_Parms->fData + at + 2);
}


// ROM 0x0002b918 AL16_Verify__FP15AirusAParmBlock
// The sixteen-bit walk.  It is `AL_Verify` again with two-byte
// characters, and with one difference that is not deliberate.
//
// ROM BUG, kept: the tail - the loop that works out the single character
// a word can only go on with - tests the "last sibling" flag the wrong
// way round.  Where the eight-bit walker stops at the last sibling and
// hands the character back, this one stops at every sibling *but* the
// last, and on the last one steps past the end of the list and carries
// on reading whatever lies there.  It leaves the loop only when those
// bytes happen to disagree, and the character it hands back is the one
// before the end rather than the one after it.
void
AL16_Verify(AirusAParmBlock* parms)
{
	ULong only = 0;
	AL16_Prep(parms);
	long node = AE_Parms->fNode;
	if (node == 0)
		node = 2;
	long want = AE_Parms->fIndex;
	long result = kAirusNoMatch;
	AE_Parms->fSymbol = 0xffffffff;

	Boolean descend = AE_Parms->fNode != 0;
	if (!descend)
	{
		AE_Parms->fIndex = 0;
		if (AE_Parms->fDataEnd - AE_Parms->fData == 2)
		{
			AE_Parms->fResult = kAirusNoMatch;
			return;
		}
	}

	long child = 0;
	Boolean tail = false;
	for (;;)
	{
		if (descend)
		{
			if ((((const UByte*) AE_Parms->fData)[node + 2] & 2) != 0)
			{
				result = kAirusNoMatch;
				break;
			}
			node = AL16_LBNode(node);
			descend = false;
		}

		ULong c = ((const UniChar*) AE_Parms->fWord)[AE_Parms->fIndex];
		Boolean matched = false;
		for (;;)
		{
			long at = AL16_ClassOffset(node);
			for (ULong s = AL16_Symbol(at); s != 0; at += 2, s = AL16_Symbol(at))
			{
				if (c == s)
				{
					matched = true;
					break;
				}
			}
			if (matched)
				break;
			UByte flags = ((const UByte*) AE_Parms->fData)[node + 2];
			if ((flags & 4) != 0)
				break;
			node = node + AL_NodeSize(flags);
		}
		if (!matched)
		{
			result = kAirusNoMatch;
			break;
		}

		AE_Parms->fNode = node;
		AL16_GetAttribute(node);
		AL16_GetAttribute2(node);
		AE_Parms->fIndex = AE_Parms->fIndex + 1;
		if (AE_Parms->fIndex <= want)
		{
			descend = true;
			continue;
		}
		UByte flags = ((const UByte*) AE_Parms->fData)[node + 2];
		result = (flags & 1) != 0 ? kAirusPrefixWithAttr : kAirusPrefix;
		if ((flags & 2) != 0)
		{
			result = kAirusLeaf;
			break;
		}
		child = AL16_LBNode(node);
		tail = true;
		break;
	}

	while (tail)
	{
		const UByte* data = (const UByte*) AE_Parms->fData;
		long at = AL16_ClassOffset(child);
		if (data[at + 2] != 0)
			break;
		ULong c = AL16_Symbol(at);
		if (only != 0 && c != only)
			break;
		only = c;
		UByte flags = data[child + 2];
		// (the inverted test - see above)
		if ((flags & 4) == 0)
		{
			AE_Parms->fSymbol = only;
			break;
		}
		child = child + AL_NodeSize(flags);
	}
	AE_Parms->fResult = result;
}


// ROM 0x0002b790 AirusAL16__FUlP15AirusAParmBlock
long
AirusAL16(ULong selector, AirusAParmBlock* parms)
{
	if (selector == kAirusVerify)
		AL16_Verify(parms);
	// NOT YET RECONSTRUCTED: AL16_NextSet 0x0002bba4 and AL16_NextSet9
	// 0x0002bbe4.
	return parms->fResult;
}


// ROM 0x0002d574 CallAirusANoLock
void
CallAirusANoLock(Handle dictionary, long selector)
{
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	switch (kind)
	{
	case kAirusKindAL:
		AirusAL(selector, parms);		// (AL_Shell 0x0002b758)
		break;
	case kAirusKindAL16:
		AirusAL16(selector, parms);		// (AL16_Shell 0x0002b774)
		break;
	case kAirusKindEnum:
	case kAirusKindEnum16:
	case kAirusKindEnumRAM:
		switch (selector)
		{
		case kAirusAddWord:
			AEnum_AddWord(parms);
			break;
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
