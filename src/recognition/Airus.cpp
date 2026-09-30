/*
	File:		recognition/Airus.cpp

	Contains:	The Airus dictionary engine's container (Airus.h): a
				dictionary made, its bytes grown and shifted, and the
				selector call everything else goes through; and the
				walkers - the enumerated dictionaries' (AEnum_*, AE8_*)
				and the ROM lexicons' (AL_*, AL16_Verify).

				NOT YET RECONSTRUCTED: the sixteen-bit walks other than
				AL16_Verify (AE16_Verify, AE16_NextSet9/AE16_NextSetCB,
				AL16_NextSet, AL16_NextSet9) - no dictionary in this ROM
				is sixteen-bit, so nothing reaches them.
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


// ROM 0x0002d6a0 ReadRefDictionary__FRC6RefVar
// A dictionary over the bytes of a binary object.  Nothing is copied:
// the object itself is the dictionary's data, which is why the frames
// heap must not move it while the dictionary is open.
Handle
ReadRefDictionary(RefArg binary)
{
	return BuildDictionaryFromPtr((void*) BinaryData(binary), (Size) Length(binary));
}


// ROM 0x0002d6f4 DisposDictionary
// The data handle and the block, and the caller's pointer cleared.
void
DisposDictionary(Handle* dictionary)
{
	if (*dictionary != nil)
	{
		DisposeHandle(((AirusAParmBlock*) **dictionary)->fDataHandle);
		DisposeHandle(*dictionary);
		*dictionary = nil;
	}
	airusResult = 0;
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
// Deleting asks for the other operation: the offsets shrink instead,
// and a node that had no sibling is left alone - what was taken out was
// under it, not after it.
static void
FixupPointers(long grew, long operation)
{
	if (grew == 0)
		return;
	if (operation == 1)
	{
		while (gAirusFixupTop != 0x3f)
		{
			long node = gAirusFixupTop < 0x3f ? (long) gAirusFixups[gAirusFixupTop++] : 0;
			long charSize = AirusCharSize();
			if (((UByte) AE_Parms->fData[node + charSize] >> 6) != 0)
				grew -= PutRP(node, GetRP(node) - grew);
		}
		return;
	}
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


// ROM 0x00029c88 (unnamed) - FindDeletionPoint
// The word followed down the trie, as far as it goes.  What comes back
// is the node the word ends at (`node`), the sibling in front of it
// (`previous`), and where the bytes after it begin (`after`); the
// ancestors are left on the fixup stack.  ==> 1 when the word is not in
// the dictionary at all.
static long
FindDeletionPoint(const UniChar* word, ULong* previous, ULong* node, long* after)
{
	long at = 2;
	*node = 2;
	*previous = 2;
	*after = (long) (AE_Parms->fDataEnd - AE_Parms->fData);
	for (;;)
	{
		if (*word == 0)
			return 0;
		long charSize = AirusCharSize();
		if (GetSymbol(at) != *word)
		{
			do
			{
				charSize = AirusCharSize();
				if (((UByte) AE_Parms->fData[at + charSize] >> 6) == 0)
					return 1;			// the row ran out: it is not here
				*previous = (ULong) at;
				at = FollowRight(at);
				*node = (ULong) at;
			}
			while (GetSymbol(at) != *word);
		}
		gAirusFixups[--gAirusFixupTop] = (ULong) at;
		word++;

		charSize = AirusCharSize();
		// The block's result going in is the mode: 0 take this word out,
		// anything else take the whole prefix out (DeletePrefix sets it
		// to 1).  Where the bytes after the word begin differs between
		// the two.
		if (AE_Parms->fResult == 0)
			*after = FollowLeft(at);
		else
			*after = ((UByte) AE_Parms->fData[at + charSize] >> 6) == 0
					 ? *after : FollowRight(at);

		charSize = AirusCharSize();
		UByte flags = (UByte) AE_Parms->fData[at + charSize];
		if ((flags & kAirusNoChildren) != 0 && *word != 0)
			return 1;					// it stops short of the word
		at = FollowLeft(at);
	}
}


// ROM 0x00029e3c AEnum_DeleteWord__FP15AirusAParmBlock
// The word in the block's buffer taken out of the dictionary.
//
// A word that other words go on from keeps its nodes and only loses its
// attribute - it stops being a word without stopping being a path.  A
// word nothing goes on from has its nodes taken out as well, back up the
// trie as far as the first ancestor that is a word itself or has another
// child; the bytes between are slid away and every sibling offset that
// reached over them is shortened.  The Handle is given back a growth
// unit at a time when the data has shrunk enough to spare one.
//
// The block's fResult going in is the mode - 0 for one word,
// anything else (which is what `DeletePrefix` sets) for the word and
// everything under it - and afterwards it says what happened: 0 it
// went, 1 it was not there, 2 the Handle could not be resized.
//
// (BUG, kept: a word whose last node carries no attribute - a path that
//  is not a word - returns without setting fResult, which DeleteWord had
//  already set to 0, so deleting a word that is not there is reported as
//  success.)
long
AEnum_DeleteWord(AirusAParmBlock* parms)
{
	long result;
	CheckDictPtrs(parms);
	if (AE_Parms->fDataEnd - AE_Parms->fData == 2)
	{
		result = 1;						// nothing in it
		AE_Parms->fResult = result;
		return result;
	}

	UniChar buffer[256];
	CopyBufferHack(AE_Parms->fWord, buffer, 0);
	gAirusFixupTop = 0x3f;
	ULong previous, node;
	long after;
	if (FindDeletionPoint(buffer, &previous, &node, &after) != 0)
	{
		result = 1;
		AE_Parms->fResult = result;
		return result;
	}

	long at = gAirusFixupTop < 0x3f ? (long) gAirusFixups[gAirusFixupTop++] : 0;
	long removed = 0;
	Boolean wasLast = false;
	long charSize = AirusCharSize();
	Boolean removing = AE_Parms->fResult != 0;
	if (!removing)
	{
		UByte flags = (UByte) AE_Parms->fData[at + charSize];
		if ((flags & kAirusHasAttribute) == 0)
			return AE_Parms->fResult;	// not a word: nothing to take out
		if ((flags & kAirusNoChildren) != 0)
			removing = true;
		else
		{
			// words go on from it: only the attribute goes
			AE_Parms->fData[at + charSize] =
				(char) ((UByte) AE_Parms->fData[at + charSize] & ~kAirusHasAttribute);
			long shrank = ClearAttr(SkipNode(at));
			FixupPointers(shrank, 1);
		}
	}

	if (removing)
	{
		Boolean done = false;
		for (;;)
		{
			charSize = AirusCharSize();
			if (((UByte) AE_Parms->fData[at + charSize] >> 6) != 0)
			{
				// it has a sibling, so its row stays: only it goes
				removed = after - at;
				done = true;
				break;
			}
			ULong parent = gAirusFixupTop == 0x3f ? 0 : gAirusFixups[gAirusFixupTop];
			if (parent < node)
				at = 0;
			else if (gAirusFixupTop < 0x3f)
				at = (long) gAirusFixups[gAirusFixupTop++];
			else
				at = 0;
			if (at == 0)
			{
				// back at the top: the whole row from the node goes
				removed = after - (long) node;
				wasLast = true;
				done = true;
				break;
			}
			charSize = AirusCharSize();
			if (((UByte) AE_Parms->fData[at + charSize] & kAirusHasAttribute) != 0)
				break;					// an ancestor that is a word itself
		}
		if (!done)
		{
			// that ancestor keeps its node and loses its children
			removed = after - FollowLeft(at);
			charSize = AirusCharSize();
			AE_Parms->fData[at + charSize] =
				(char) ((UByte) AE_Parms->fData[at + charSize] | kAirusNoChildren);
			gAirusFixups[--gAirusFixupTop] = (ULong) at;
		}

		SlideUp(after, removed);
		if (AE_Parms->fDataEnd - AE_Parms->fData != 2)
		{
			if (wasLast)
				removed += ClearRP((long) previous);
			FixupPointers(removed, 1);
		}
		// the room the data no longer needs given back, a growth unit at
		// a time
		while ((ULong) AE_Parms->fSize > (ULong) AE_Parms->fGrowBy
			   && (ULong) (AE_Parms->fSize - AE_Parms->fGrowBy)
				  >= (ULong) (AE_Parms->fDataEnd - AE_Parms->fData))
		{
			SetHandleSize(AE_Parms->fDataHandle, AE_Parms->fSize - AE_Parms->fGrowBy);
			if (MemError() != noErr)
			{
				result = 2;
				AE_Parms->fResult = result;
				return result;
			}
			CheckDictPtrs(AE_Parms);
			AE_Parms->fSize -= AE_Parms->fGrowBy;
		}
	}

	result = 0;
	AE_Parms->fResult = result;
	return result;
}


// The flags byte of the node at that offset (after its character).
static inline UByte
NodeFlags(long node)
{
	return (UByte) AE_Parms->fData[node + AirusCharSize()];
}


// ROM 0x0002a1f4 AEnum_FirstLast__FP15AirusAParmBlock
// The first (fResult 0) or last (1) word at or below the block's node;
// an empty dictionary has none (1).  The answer is left in fResult.
long
AEnum_FirstLast(AirusAParmBlock* parms)
{
	long result;
	if (parms->fNode == 0 && parms->fDataEnd - parms->fData == 2)
		result = 1;
	else
	{
		parms->fResult += 2;
		result = AEnum_NextPrevious(parms);
	}
	AE_Parms->fResult = result;
	return result;
}


// ROM 0x0002a244 AEnum_NextPrevious__FP15AirusAParmBlock
// A walk of the trie in alphabetical order from the word in the block's
// buffer (the part after fIndex, the prefix before it staying as it is):
// fResult 0 the next word, 1 the previous one, 2 the first word under the
// node, 3 the last.  The word found is written over the buffer from
// fIndex on and its attribute put in fAttribute.  ==> 0, or 1 when there
// is no such word (also left in fResult).
//
// Forwards: down the word, noting the last node that had a sibling to
// its right; the next word is the first one below the word's own node
// when it has children, else the first one below that sibling.
// Backwards: down the word, noting the last word-ending node above it
// and the last left sibling along the way; the previous word is the last
// one below that sibling when it lies later in the data than that
// ancestor, else the ancestor's own word.  The ROM writes this with goto
// - the four cases share their tails and jump into each other's loops -
// and so does this, with its registers' names (uN a node, pN a position
// in the word).
long
AEnum_NextPrevious(AirusAParmBlock* parms)
{
	CheckDictPtrs(parms);
	ULong u10 = 0;
	ULong u11 = 0;
	UByte* p8 = AE_Parms->fWord + AE_Parms->fIndex;
	UByte* p9;
	UByte* r10 = nil;
	ULong u2 = (ULong) AE_Parms->fNode;
	ULong u3 = u2 == 0 ? 2 : u2;
	long result;
	long direction = AE_Parms->fResult;

	if (direction == 0)
	{
		// next
		p9 = p8;
		if (u2 == 0)
			goto next_find;
		u11 = u3;
		while (*p9 != 0)
		{
			u3 = FollowLeft(u11);
next_find:
			while (GetSymbol(u3) != *p9)
				u3 = FollowRight(u3);
			if ((NodeFlags(u3) >> 6) != 0)
			{
				p8 = p9;
				u10 = u3;
			}
			p9++;
			u11 = u3;
		}
		if ((NodeFlags(u11) & kAirusNoChildren) == 0)
		{
			do
			{
				u11 = FollowLeft(u11);
				*p9 = (UByte) GetSymbol(u11);
				p8 = p9 + 1;
				p9 = p8;
			}
			while ((NodeFlags(u11) & kAirusHasAttribute) == 0);
		}
		else
		{
			if (u10 == 0)
			{
				result = 1;
				goto done;
			}
			u11 = FollowRight(u10);
			p9 = p8;
			for (;;)
			{
				*p9 = (UByte) GetSymbol(u11);
				p8 = p9 + 1;
				if ((NodeFlags(u11) & kAirusHasAttribute) != 0)
					break;
				u11 = SkipNode(u11);
				p9 = p8;
			}
		}
		goto terminate;
	}

	if (direction == 1)
	{
		// previous
		p9 = p8;
		if (u2 == 0)
			goto previous_find;
		u2 = u3;
		if (*p8 != 0)
		{
			do
			{
				if ((NodeFlags(u2) & kAirusHasAttribute) != 0)
				{
					r10 = p8;
					u11 = u2;
				}
				u3 = FollowLeft(u2);
previous_find:
				{
					ULong symbol = GetSymbol(u3);
					u2 = u3;
					u3 = u10;
					if (symbol != *p8)
					{
						do
						{
							u3 = u2;
							u2 = FollowRight(u3);
							p9 = p8;
						}
						while (GetSymbol(u2) != *p8);
					}
				}
				p8++;
				u10 = u3;
			}
			while (*p8 != 0);
			if (u3 != 0 || u11 != 0)
			{
				if (u11 <= u3)
				{
					*p9 = (UByte) GetSymbol(u3);
					p8 = p9 + 1;
					goto last_below;
				}
				goto end_at_ancestor;
			}
		}
		result = 1;
		goto done;
	}

	if (direction == 2)
	{
		// first
		p9 = p8;
		if (u2 == 0)
		{
			*p8 = (UByte) GetSymbol(u3);
			p9 = p8 + 1;
		}
		u11 = u3;
		p8 = p9;
		while ((NodeFlags(u11) & kAirusHasAttribute) == 0)
		{
			u11 = SkipNode(u11);
			*p8++ = (UByte) GetSymbol(u11);
		}
		goto terminate;
	}

	if (direction == 3)
	{
		// last
		if (u2 == 0)
			goto last_siblings;
last_below:
		u11 = u3;
		while ((NodeFlags(u11) & kAirusNoChildren) == 0)
		{
			u3 = FollowLeft(u11);
last_siblings:
			while ((NodeFlags(u3) >> 6) != 0)
				u3 = FollowRight(u3);
			*p8++ = (UByte) GetSymbol(u3);
			u11 = u3;
		}
		goto terminate;
	}

end_at_ancestor:
	*r10 = 0;
	goto attribute;

terminate:
	*p8 = 0;

attribute:
	if (AE_Parms->fAttributeSize != 0)
		u11 = SkipNode(u11);
	switch (AE_Parms->fAttributeSize)
	{
	case 0:
		if (((UByte) AE_Parms->fData[1] & 7) == kAirusKindEnum)
			AE_Parms->fAttribute = 0x80;
		break;
	case 1:
		AE_Parms->fAttribute = (UByte) AE_Parms->fData[u11];
		break;
	case 2:
		AE_Parms->fAttribute = ((ULong) (UByte) AE_Parms->fData[u11] << 8) | (UByte) AE_Parms->fData[u11 + 1];
		break;
	case 4:
		AE_Parms->fAttribute = ((ULong) (UByte) AE_Parms->fData[u11] << 24)
							 | ((ULong) (UByte) AE_Parms->fData[u11 + 1] << 16)
							 | ((ULong) (UByte) AE_Parms->fData[u11 + 2] << 8)
							 | (UByte) AE_Parms->fData[u11 + 3];
		break;
	}
	result = 0;

done:
	AE_Parms->fResult = result;
	return result;
}


// ROM 0x0002a7cc AEnum_ChangeAttribute__FP15AirusAParmBlock
// The attribute of a word already in the dictionary written over where
// it lies.  The word is walked from the root a character at a time -
// across the siblings of a row to find the character, then down to that
// node's children for the next - and the new attribute replaces the old
// one in place, so nothing moves and the dictionary does not grow.
//
// The block's result: 0 it was changed, 1 the dictionary carries no
// attributes at all, 2 the word is not in it (or carries none).
void
AEnum_ChangeAttribute(AirusAParmBlock* parms)
{
	CheckDictPtrs(parms);
	if (AE_Parms->fAttributeSize == 0)
	{
		AE_Parms->fResult = 1;
		return;
	}
	// a dictionary of two bytes is the header and nothing else
	if (AE_Parms->fDataEnd - AE_Parms->fData != 2)
	{
		const UByte* word = AE_Parms->fWord;
		long node;
		if (*word == 0)
			// the empty word: whatever node the caller left in the block
			node = AE_Parms->fNode;
		else
		{
			node = 2;							// the first node, past the header
			for (;;)
			{
				while (GetSymbol(node) != *word)
				{
					// the row is over, so no word begins this way
					if (((UByte) AE_Parms->fData[node + AirusCharSize()] & kAirusSizeMask) == 0)
					{
						AE_Parms->fResult = 2;
						return;
					}
					node = FollowRight(node);
				}
				word++;
				if (*word == 0)
					break;
				// the character matched and there is more of the word
				if (((UByte) AE_Parms->fData[node + AirusCharSize()] & kAirusNoChildren) != 0)
				{
					AE_Parms->fResult = 2;
					return;
				}
				node = FollowLeft(node);
			}
		}

		if (((UByte) AE_Parms->fData[node + AirusCharSize()] & kAirusHasAttribute) != 0)
		{
			long offset = SkipNode(node);
			ULong attribute = AE_Parms->fAttribute;
			// BUG (the ROM's): it writes the bytes out by hand for the
			// sizes 1, 2 and 4 and has no arm for 3, so a dictionary
			// with a three-byte attribute is left as it was - and the
			// call still says it worked.  (PutDictBytes, which every
			// other writer goes through, handles all four.)
			if (AE_Parms->fAttributeSize == 1)
				AE_Parms->fData[offset] = (char) attribute;
			else if (AE_Parms->fAttributeSize == 2)
			{
				AE_Parms->fData[offset] = (char) (attribute >> 8);
				AE_Parms->fData[offset + 1] = (char) attribute;
			}
			else if (AE_Parms->fAttributeSize == 4)
			{
				AE_Parms->fData[offset] = (char) (attribute >> 24);
				AE_Parms->fData[offset + 1] = (char) (attribute >> 16);
				AE_Parms->fData[offset + 2] = (char) (attribute >> 8);
				AE_Parms->fData[offset + 3] = (char) attribute;
			}
			AE_Parms->fResult = 0;
			return;
		}
	}
	AE_Parms->fResult = 2;
}


// ROM 0x0002d37c AttributeLength
// How many bytes of attribute each word of a dictionary carries.  A
// dictionary that declares none but is of the oldest writable kind
// carries one all the same, which is what the kind meant before the
// size was written down.
long
AttributeLength(Handle dictionary)
{
	airusResult = 0;
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	long size = parms->fAttributeSize;
	if (size != 0)
		return size;
	if (((UByte) (*parms->fDataHandle)[1] & 7) == kAirusKindEnum)
		return 1;
	return 0;
}


// ROM 0x0002d3b8 ChangeAttribute
// The way in: the word and the new attribute put in the block and the
// walker run.  airusResult afterwards is 0 it was changed, -7 the
// dictionary carries no attributes, -6 the word is not in it; anything
// else the walker may answer leaves airusResult as it was.
void
ChangeAttribute(Handle dictionary, UByte* word, ULong attribute)
{
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	parms->fWord = word;
	parms = (AirusAParmBlock*) *dictionary;
	parms->fAttribute = attribute;
	CallAirusA(dictionary, kAirusChangeAttribute);

	parms = (AirusAParmBlock*) *dictionary;
	long result = parms->fResult;
	if (result == 0)
		airusResult = 0;
	else if (result == 1)
		airusResult = -7;
	else if (result == 2)
		airusResult = kAirusNotAWord;
}


// ROM 0x0002c56c DeleteWord
// A word taken out of a dictionary.  airusResult afterwards: 0 it went,
// 4 it was not there, 5 the word was empty, -2 the Handle could not be
// resized.
void
DeleteWord(Handle dictionary, UByte* word)
{
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	parms->fResult = 0;
	parms->fWord = word;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	Boolean empty = (kind == kAirusKindEnum16 || kind == kAirusKindAL16)
				   ? *(const UniChar*) word == 0 : word[0] == 0;
	if (empty)
	{
		airusResult = kAirusEmptyWord;
		return;
	}
	CallAirusA(dictionary, kAirusDeleteWord);
	long result = ((AirusAParmBlock*) *dictionary)->fResult;
	if (result != 0)
	{
		airusResult = result == 1 ? kAirusAlreadyThere : kAirusNoMemory;
		return;
	}
	airusResult = 0;
}


/*------------------------------------------------------------------------------
	W h a t   m a y   c o m e   n e x t

	The other way of reading a dictionary: not "is this a word" but "what
	characters may follow what I have so far".  That is one row of the
	trie, and it is what walking a whole dictionary is built on.
------------------------------------------------------------------------------*/

// ROM 0x0002a9f4 AE8_NextSet9__FP15AirusAParmBlock
// Every child of the block's node offered to the block's callback, in the
// order they lie - which is sorted, so the characters come out sorted.
// A node of 0 means the root, and the row walked is the top one.
//
// (BUG, kept: the attribute handed to the callback is assembled from its
//  bytes low one first, where `PutAttr` writes it and `GetAttr` reads it
//  high one first.  A one-byte attribute - which is what every dictionary
//  the machine writes has - is the same either way, so nobody ever saw
//  it; a two- or four-byte one comes out of here byte-reversed.)
//
// The ROM writes the loop out twice, once for a dictionary whose words
// carry no attribute at all and once for the rest; the two differ only in
// whether the attribute is read, and the step to the next sibling is the
// same either way, so it is one loop here.
void
AE8_NextSet9(AirusAParmBlock* parms)
{
	CheckDictPtrs(parms);
	long node = AE_Parms->fNode;
	long at = node == 0 ? 2 : node;
	long result = 1;
	if (AE_Parms->fWalkProc != nil)
	{
		long attributeSize = AE_Parms->fAttributeSize;
		void* context = AE_Parms->fWalkContext;
		if (AE_Parms->fDataEnd - AE_Parms->fData == 2)
		{
			AE_Parms->fResult = 1;		// nothing in it
			return;
		}
		if (node != 0)
		{
			if (((UByte) AE_Parms->fData[at + 1] & kAirusNoChildren) != 0)
			{
				AE_Parms->fResult = 1;	// nothing goes on from it
				return;
			}
			at = FollowLeft(at);
		}
		Boolean implied = ((UByte) (*AE_Parms->fDataHandle)[1] & 7) == kAirusKindEnum;
		for (;;)
		{
			UByte flags = (UByte) AE_Parms->fData[at + 1];
			ULong attribute = 0;
			if ((attributeSize != 0 || implied)
				&& (flags & kAirusHasAttribute) != 0 && attributeSize > 0)
			{
				const UByte* bytes = (const UByte*) AE_Parms->fData + SkipNode(at);
				for (long i = 0; i < attributeSize; i++)
					attribute |= (ULong) bytes[i] << (i * 8);
			}
			AE_Parms->fWalkProc(context, (ULong) (UByte) AE_Parms->fData[at],
								(ULong) at | ((ULong) ((flags >> 4) & 3) << 30), attribute);
			if ((flags & 0xc0) == 0)
				break;
			at = FollowRight(at);
		}
		result = 0;
	}
	AE_Parms->fResult = result;
}


// ROM 0x0002af18 AEnum_NextSet9__FP15AirusAParmBlock
// The walk of what may follow, by the dictionary's kind.  NOT YET
// RECONSTRUCTED: AE16_NextSet9 (a sixteen-bit dictionary answers
// nothing to follow).
void
AEnum_NextSet9(AirusAParmBlock* parms)
{
	UByte kind = (UByte) (*parms->fDataHandle)[1] & 7;
	if (kind == kAirusKindEnum16 || kind == kAirusKindAL16)
		parms->fResult = 1;
	else
		AE8_NextSet9(parms);
}


// ROM 0x0002af38 AE8_NextSetCB__FUlN31
// The callback `AEnum_NextSet` uses: the characters written out one after
// another into the caller's buffer, through a pointer the caller keeps.
void
AE8_NextSetCB(void* context, ULong character, ULong /*node*/, ULong /*attribute*/)
{
	UByte** where = (UByte**) context;
	*(*where)++ = (UByte) character;
}


// ROM 0x0002afd0 AEnum_NextSet__FP15AirusAParmBlock
// The set of characters that may follow the block's node, written into
// the block's word buffer and terminated.
//
// NOT YET RECONSTRUCTED: the sixteen-bit walk (AE16_NextSet9 0x0002af18,
// AE16_NextSetCB), which is what a dictionary of UniChars would need; the
// ones the machine writes are all eight-bit.
void
AEnum_NextSet(AirusAParmBlock* parms)
{
	UByte* out = parms->fWord;
	parms->fWalkContext = &out;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	if (kind == kAirusKindEnum16 || kind == kAirusKindAL16)
	{
		parms->fWalkProc = nil;			// AE16_NextSetCB
		parms->fResult = 1;				// AE16_NextSet9(parms)
		*out++ = 0;
	}
	else
	{
		parms->fWalkProc = AE8_NextSetCB;
		AE8_NextSet9(parms);
	}
	*out = 0;
}


/*------------------------------------------------------------------------------
	W a l k i n g   a   w h o l e   d i c t i o n a r y

	Every word of it, or every word under a prefix, handed to a callback
	in order.  It is a recursion over the two questions above: what the
	word so far is (Verify), and what may follow it (NextSet).
------------------------------------------------------------------------------*/

// ROM 0x0002d73c A8_PrefixCompletions__FP13DictWalkBlock
// The word the block holds looked up, and the walk carried on from what
// comes back: a prefix of other words is followed on down, a word is
// reported, and a word that is also a prefix is both.
//
// ==> false when the callback asked to stop.
Boolean
A8_PrefixCompletions(DictWalkBlock* block)
{
	Handle dictionary = block->fDictionary;
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	UByte* word = block->fWord;
	parms->fWord = word;
	CallAirusA(dictionary, kAirusVerify);
	parms = (AirusAParmBlock*) *dictionary;
	long result = parms->fResult;
	if (result == kAirusPrefix)
		return A8_WalkNextChars(block, parms->fIndex);
	if (result != kAirusPrefixWithAttr && result != kAirusLeaf)
		return true;				// nothing begins that way

	block->fCount++;
	ULong attribute = parms->fAttribute;
	UByte terminal = parms->fField48 != 0 ? *(const UByte*) parms->fField48 : 0;
	word[parms->fIndex] = 0;
	if (result == kAirusPrefixWithAttr)
	{
		if (block->fProc != nil
			&& !block->fProc(word, attribute, terminal, block->fCount, block->fContext))
			return false;
		return A8_WalkNextChars(block, ((AirusAParmBlock*) *dictionary)->fIndex);
	}
	if (block->fProc == nil)
		return true;
	return block->fProc(word, attribute, terminal, block->fCount, block->fContext);
}


// ROM 0x0002d890 A8_WalkNextChars__FP13DictWalkBlockUl
// Each character that may follow the word so far tried in turn.  When
// only one may - which is what `fSymbol` says - it is taken straight;
// otherwise the whole set is asked for and walked.
Boolean
A8_WalkNextChars(DictWalkBlock* block, long length)
{
	Handle dictionary = block->fDictionary;
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	UByte* word = block->fWord;
	long symbol = (long) parms->fSymbol;
	if (symbol == -1)
	{
		UByte set[256];
		long node = parms->fNode;
		strncpy((char*) set, (const char*) word, (size_t) length);
		parms->fIndex = length - 1;
		parms->fWord = set;
		CallAirusA(dictionary, kAirusNextSet);
		parms = (AirusAParmBlock*) *dictionary;
		if (parms->fResult == 0)
		{
			for (long i = 0; set[i] != 0; i++)
			{
				word[length] = set[i];
				word[length + 1] = 0;
				parms = (AirusAParmBlock*) *dictionary;
				parms->fNode = node;
				parms->fIndex = length;
				if (!A8_PrefixCompletions(block))
					return false;
			}
		}
	}
	else
	{
		word[length] = (UByte) symbol;
		word[length + 1] = 0;
		parms->fIndex = length;
		if (!A8_PrefixCompletions(block))
			return false;
	}
	return true;
}


// ROM 0x0002e0f0 WalkDictionary__FPP15AirusAParmBlockPUcPFPUcUlUcT2Pv_UcPv
// Every word of a dictionary, or every word that begins with a prefix,
// handed to the callback in order.  A nil callback walks it all the same
// and only counts.  ==> how many words were reached.
long
WalkDictionary(Handle dictionary, const UByte* prefix, DictWalkProc proc, void* context)
{
	DictWalkBlock block;
	block.fDictionary = dictionary;
	block.fCount = 0;
	block.fProc = proc;
	block.fContext = context;
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	if (prefix != nil)
	{
		long length = (long) strlen((const char*) prefix);
		strcpy((char*) block.fWord, (const char*) prefix);
		if (length != 0)
		{
			parms->fNode = 0;
			parms->fIndex = length - 1;
			A8_PrefixCompletions(&block);
			return block.fCount;
		}
	}
	else
		block.fWord[0] = 0;
	parms->fNode = 0;
	parms->fSymbol = (ULong) -1;
	A8_WalkNextChars(&block, 0);
	return block.fCount;
}


// ROM 0x0002c60c DeletePrefix
// A word and everything that goes on from it taken out at once.  It is
// `DeleteWord` with the block's result set to 1 rather than 0 going in,
// which is the flag the walker reads as "take the whole row out" rather
// than "take this word out".
void
DeletePrefix(Handle dictionary, UByte* word)
{
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	parms->fResult = 1;
	parms->fWord = word;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	Boolean empty = (kind == kAirusKindEnum16 || kind == kAirusKindAL16)
				   ? *(const UniChar*) word == 0 : word[0] == 0;
	if (empty)
	{
		airusResult = kAirusEmptyWord;
		return;
	}
	CallAirusA(dictionary, kAirusDeleteWord);
	airusResult = 0;
	long result = ((AirusAParmBlock*) *dictionary)->fResult;
	if (result != 0)
		airusResult = result == 1 ? kAirusAlreadyThere : kAirusNoMemory;
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
static ULong	gAirusVerifyPosition = 0;		// ROM 0x0c100820: the chain position of the dictionary a VerifyCharacter matched in
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


// ROM 0x0002c7ac VerifyCharacter__FPP15AirusAParmBlockPUcPPUcPPUlT4UcT3
// The word walked on from where the last walk left off (VerifyStart
// begins one): `word` appended to what has been matched so far, and every
// dictionary of the chain from the one the walk is on asked in turn - all
// of them when `allDicts`, else only until one takes it.  airusResult
// gathers what they say: 1 the characters begin a word, 2 they begin one
// and are one, 3 they are a word and nothing goes on, -6 nothing begins
// that way.  The first dictionary to take it becomes the one the walk is
// on; `position` is given its place in the chain, `attribute` the first
// attribute found (nil for none) and `extra` what goes with it;
// `terminal` the one character that may come next, when every dictionary
// that took it agrees (nil otherwise).
void
VerifyCharacter(Handle dictionary, UByte* word, UByte** terminal, ULong** position, ULong** attribute, Boolean allDicts, ULong* extra)
{
	Handle p = ((AirusAParmBlock*) *dictionary)->fCurrent;
	AirusAParmBlock* parms = (AirusAParmBlock*) *p;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	Boolean wide = kind == kAirusKindEnum16 || kind == kAirusKindAL16;
	// ROM QUIRK: the ROM keeps the two terminals in two registers, r6 for an
	// 8-bit dictionary and r8 for a 16-bit one, loading only the one it
	// needs - but it asks r6 whether to go on looking for a terminal, and
	// clears only r6 when none was found.  For a 16-bit dictionary r6 is
	// whatever the caller left in it (not reproducible; taken as non-nil
	// here), so the 16-bit terminal is answered even when none was set.
	UByte* terminal8 = wide ? nil : &gAirusTerminal8;
	UniChar* terminal16 = wide ? &gAirusTerminal16 : nil;
	Boolean lookForTerminal = true;
	ULong* foundPosition = &gAirusVerifyPosition;
	ULong* foundAttribute = &gAirusVerifyAttribute;
	ULong found = gAirusVerifyExtra;
	long startIndex = parms->fIndex;
	ULong count = (ULong) parms->fField3c;
	Boolean attributeFound = false, taken = false, terminalSet = false, positionSet = false;
	long wordLength, totalLength;
	UByte* buffer;
	if (!wide)
	{
		wordLength = Astrlen((const char*) word);
		Astrcpy((char*) gAirusScratch8 + startIndex, (const char*) word);
		totalLength = Astrlen((const char*) gAirusScratch8);
		buffer = gAirusScratch8;
	}
	else
	{
		wordLength = Ashortstrlen((const UniChar*) word) / 2;
		Ashortstrcpy((UniChar*) gAirusScratch16 + startIndex, (const UniChar*) word);
		totalLength = Ashortstrlen((const UniChar*) gAirusScratch16) / 2;
		buffer = gAirusScratch16;
	}
	((AirusAParmBlock*) *p)->fWord = buffer;
	ULong at = (ULong) ((AirusAParmBlock*) *p)->fField38;
	airusResult = 0;
	gAirusVerifyPosition = at;
	if (at < count)
	{
		do
		{
			parms = (AirusAParmBlock*) *p;
			Boolean walk = true;
			if (parms->fIndex != startIndex)
			{
				// a dictionary that has fallen behind: walked over the
				// whole word from its start, or - asking all of them -
				// passed over as matching nothing
				if (!allDicts)
				{
					if (totalLength > 1 && parms->fNode == 0)
						parms->fIndex = totalLength - 1;
				}
				else
					walk = false;
			}
			else if (wordLength > 1 && parms->fNode == 0)
				parms->fIndex = wordLength - 1;
			Boolean took = false;
			if (walk)
			{
				CallAirusA(p, kAirusVerify);
				switch (((AirusAParmBlock*) *p)->fResult)
				{
				case kAirusPrefix:
					if (airusResult == 0 || airusResult == kAirusNotAWord)
						airusResult = kAirusIsPrefix;
					else if (airusResult == kAirusIsWord)
						airusResult = kAirusIsPrefixAndWord;
					took = true;
					break;
				case kAirusPrefixWithAttr:
					if (airusResult == 0 || airusResult == kAirusIsPrefix || airusResult == kAirusIsWord || airusResult == kAirusNotAWord)
						airusResult = kAirusIsPrefixAndWord;
					took = true;
					break;
				case kAirusLeaf:
					if (airusResult == 0 || airusResult == kAirusNotAWord)
						airusResult = kAirusIsWord;
					else if (airusResult == kAirusIsPrefix)
						airusResult = kAirusIsPrefixAndWord;
					took = true;
					break;
				case kAirusNoMatch:
					walk = false;
					break;
				default:
					break;
				}
			}
			if (!walk && airusResult == 0)
				airusResult = kAirusNotAWord;
			if (took)
			{
				AirusAParmBlock* q = (AirusAParmBlock*) *p;
				// a whole word (with or without more after it) is what
				// sets the position
				if (q->fResult != kAirusPrefix && !positionSet)
				{
					positionSet = true;
					gAirusVerifyPosition = (ULong) q->fField38;
					if (!attributeFound && HasActualOrImpliedAtr(p))
					{
						attributeFound = true;
						gAirusVerifyAttribute = q->fAttribute;
						found = q->fField48;
					}
				}
				if (!taken)
				{
					taken = true;
					((AirusAParmBlock*) *dictionary)->fCurrent = p;
					if (!attributeFound && HasActualOrImpliedAtr(p))
					{
						attributeFound = true;
						gAirusVerifyAttribute = q->fAttribute;
						found = q->fField48;
					}
				}
				if (lookForTerminal)
				{
					ULong symbol = q->fSymbol;
					if (symbol == 0xffffffff)
					{
						if (q->fResult != kAirusLeaf)
						{
							if (!wide) { terminal8 = nil; lookForTerminal = false; }
							else terminal16 = nil;
						}
					}
					else if (terminalSet)
					{
						if (!wide)
						{
							if ((symbol & 0xff) != *terminal8) { terminal8 = nil; lookForTerminal = false; }
						}
						else if (terminal16 != nil && (UniChar) symbol != *terminal16)
							terminal16 = nil;
						// (the ROM reads address 0 here once the 16-bit
						// terminal has been dropped)
					}
					else
					{
						terminalSet = true;
						if (!wide)
							*terminal8 = (UByte) symbol;
						else
							gAirusTerminal16 = (UniChar) symbol;
					}
				}
			}
			if (!allDicts && taken)
				break;
			p = ((AirusAParmBlock*) *p)->fNext;
			at++;
		}
		while (at < count);
		if (!positionSet)
			foundPosition = nil;
	}
	else
		foundPosition = nil;
	if (!terminalSet)
		terminal8 = nil;
	if (!attributeFound)
	{
		foundAttribute = nil;
		found = 0;
	}
	if (position != nil)
		*position = foundPosition;
	if (terminal != nil)
		*terminal = wide ? (UByte*) terminal16 : terminal8;
	if (attribute != nil)
		*attribute = foundAttribute;
	if (extra != nil)
		*extra = found;
}


// ROM 0x0002cce8 VerifyWord__FPP15AirusAParmBlockPUcPPUcPPUlT4UcT3
// VerifyCharacter under another name.
void
VerifyWord(Handle dictionary, UByte* word, UByte** terminal, ULong** position, ULong** attribute, Boolean allDicts, ULong* extra)
{
	VerifyCharacter(dictionary, word, terminal, position, attribute, allDicts, extra);
}


// ROM 0x0002cf0c FirstCompletion
// The first word of the dictionary that begins with the prefix, written
// into `word`.  airusResult afterwards: 0 there was one, -6 nothing
// begins with the prefix, -9 the dictionary is empty.  `attribute` is
// given the word's attribute (nil when there is none) and `extra` what
// VerifyString would hand back beside it.
void
FirstCompletion(Handle dictionary, const void* prefix, void* word, ULong** attribute, ULong* extra)
{
	ULong* foundAttribute = &gAirusVerifyAttribute;
	ULong found = gAirusVerifyExtra;
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	Boolean wide = kind == kAirusKindEnum16 || kind == kAirusKindAL16;
	Boolean empty = wide ? *(const UniChar*) prefix == 0 : *(const UByte*) prefix == 0;
	if (!empty)
	{
		VerifyString(dictionary, prefix, nil, nil, nil);
		if (airusResult < 0)
		{
			airusResult = kAirusNotAWord;
			foundAttribute = nil;
			goto out;
		}
	}
	else
		((AirusAParmBlock*) *dictionary)->fNode = 0;
	parms = (AirusAParmBlock*) *dictionary;
	if (parms->fDataEnd - parms->fData == 2)
	{
		airusResult = kAirusEmptyDictionary;
		foundAttribute = nil;
		goto out;
	}
	{
		long length;
		if (wide)
		{
			Ashortstrcpy((UniChar*) word, (const UniChar*) prefix);
			length = Ashortstrlen((const UniChar*) word) / 2;
		}
		else
		{
			Astrcpy((char*) word, (const char*) prefix);
			length = Astrlen((const char*) word);
		}
		parms = (AirusAParmBlock*) *dictionary;
		parms->fIndex = length;
		parms->fResult = 0;
		parms->fWord = (UByte*) word;
		CallAirusA(dictionary, kAirusFirstLast);
		if (!HasActualOrImpliedAtr(dictionary))
			foundAttribute = nil;
		else
			gAirusVerifyAttribute = ((AirusAParmBlock*) *dictionary)->fAttribute;
		found = HasActualOrImpliedAtr(dictionary) ? ((AirusAParmBlock*) *dictionary)->fField48 : 0;
		airusResult = 0;
	}
out:
	if (attribute != nil)
		*attribute = foundAttribute;
	if (extra != nil)
		*extra = found;
}


// ROM 0x0002d224 NextCompletion
// The word after `last` among those beginning with the prefix, written
// into `word`.  airusResult afterwards: 0 there was one, -10 there are no
// more, -6 `last` was empty.
void
NextCompletion(Handle dictionary, const void* prefix, void* word, const void* last, ULong** attribute, ULong* extra)
{
	ULong* foundAttribute = &gAirusVerifyAttribute;
	ULong found;
	AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
	long kind = (UByte) (*parms->fDataHandle)[1] & 7;
	Boolean wide = kind == kAirusKindEnum16 || kind == kAirusKindAL16;
	Boolean empty = wide ? *(const UniChar*) last == 0 : *(const UByte*) last == 0;
	if (empty)
	{
		foundAttribute = nil;
		airusResult = kAirusNotAWord;
		found = gAirusVerifyExtra;
	}
	else
	{
		long length;
		if (wide)
		{
			Ashortstrcpy((UniChar*) word, (const UniChar*) last);
			length = Ashortstrlen((const UniChar*) prefix) / 2;
		}
		else
		{
			Astrcpy((char*) word, (const char*) last);
			length = Astrlen((const char*) prefix);
		}
		parms = (AirusAParmBlock*) *dictionary;
		parms->fIndex = length;
		parms->fResult = 0;
		parms->fWord = (UByte*) word;
		CallAirusA(dictionary, kAirusNextPrevious);
		if (!HasActualOrImpliedAtr(dictionary))
			foundAttribute = nil;
		else
			gAirusVerifyAttribute = ((AirusAParmBlock*) *dictionary)->fAttribute;
		found = HasActualOrImpliedAtr(dictionary) ? ((AirusAParmBlock*) *dictionary)->fField48 : 0;
		long result = ((AirusAParmBlock*) *dictionary)->fResult;
		if (result == 0)
			airusResult = 0;
		else if (result == 1)
		{
			foundAttribute = nil;
			airusResult = kAirusNoMoreWords;
		}
	}
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
	else if (selector == kAirusNextSet)
		AL_NextSet(parms);
	else if (selector == kAirusNextSet9)
		AL_NextSet9(parms);
	return parms->fResult;
}


// ROM 0x0002c1e8 AL_NextSetCB__FUlN31
// The callback AL_NextSet uses: a child's character set (a string, in a
// lexicon) copied out after the ones before it, through a pointer the
// caller keeps - no terminator, the next one going straight after.
void
AL_NextSetCB(void* context, ULong set, ULong /*node*/, ULong /*attribute*/)
{
	UByte** where = (UByte**) context;
	UByte* out = *where;
	for (const UByte* s = (const UByte*) (uintptr_t) set; *s != 0; s++)
		*out++ = *s;
	*where = out;
}


// ROM 0x0002c214 AL_NextSet__FP15AirusAParmBlock
// The characters that may follow the node reached, as one string in the
// block's word buffer: every child's set run together, terminated, and
// each character kept once (AL_FilterString).  ==> AL_NextSet9's answer.
long
AL_NextSet(AirusAParmBlock* parms)
{
	UByte* out = parms->fWord;
	parms->fWalkContext = &out;
	parms->fWalkProc = AL_NextSetCB;
	long result = AL_NextSet9(parms);
	*out = 0;
	AL_FilterString((char*) parms->fWord);
	return result;
}


// ROM 0x0002c268 AL_NextSet9__FP15AirusAParmBlock
// What may follow the node reached (nought: the root): each child handed
// to the block's walk callback - the child's character set, which in a
// lexicon is a string rather than one character, the node with its flags
// in the top two bits (bit 30 an attribute, bit 31 no children) and its
// attribute.  ==> 0, 1 (the block's result too) for nothing to follow.
long
AL_NextSet9(AirusAParmBlock* parms)
{
	AL_Prep(parms);
	long node = AE_Parms->fNode;
	if (node == 0)
		node = 2;
	AE_Parms->fResult = 0;
	const UByte* data = (const UByte*) AE_Parms->fData;
	if (AE_Parms->fDataEnd != AE_Parms->fData + 2 && (data[node + 2] & 2) == 0)
	{
		if (AE_Parms->fNode != 0)
			node = AL_FollowLeft(node);
		for (;;)
		{
			data = (const UByte*) AE_Parms->fData;
			ULong attribute = 0;
			if ((data[node + 2] & 1) != 0)
			{
				AL_GetAttribute(node);
				attribute = AE_Parms->fAttribute;
			}
			long set = (UShort) AL_SymbolOffset(node);
			AE_Parms->fWalkProc(AE_Parms->fWalkContext, (ULong) (uintptr_t) (AE_Parms->fData + set),
								(ULong) node | ((ULong) data[node + 2] << 30), attribute);
			data = (const UByte*) AE_Parms->fData;
			if ((data[node + 2] & 4) != 0)
				break;
			node = AE_Parms->fAttributeSize + node + ((data[node + 2] & 2) == 0 ? 5 : 3);
		}
		return 0;
	}
	AE_Parms->fResult = 1;
	return 1;
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
		case kAirusDeleteWord:
			AEnum_DeleteWord(parms);
			break;
		case kAirusNextSet:
			AEnum_NextSet(parms);
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
		case kAirusChangeAttribute:
			AEnum_ChangeAttribute(parms);
			break;
		case kAirusFirstLast:
			AEnum_FirstLast(parms);
			break;
		case kAirusNextPrevious:
			AEnum_NextPrevious(parms);
			break;
		case kAirusNextSet9:
			AEnum_NextSet9(parms);
			break;
		default:
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
