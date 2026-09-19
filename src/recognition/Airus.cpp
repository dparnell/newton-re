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


// ROM 0x0002e6e0 Astrlen__FPc, 0x0002e738 Astrcpy__FPcT1, 0x0002e764 Ashortstrcpy__FPUsT1
// The engine's own string handling, over the bytes and the UniChars it
// keeps words in.
long
Astrlen(const char* s)
{
	return (long) strlen(s);
}


void
Astrcpy(char* dest, const char* src)
{
	strcpy(dest, src);
}


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
