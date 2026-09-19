/*
	File:		recognition/Airus.h

	Contains:	The Airus dictionary engine: the machine the Newton's word
				lists are looked up in and written to.

				A dictionary is a byte stream - a trie, walked a character
				at a time - kept in a Handle, with an `AirusAParmBlock`
				in front of it holding where the data is, how big it is and
				how far it has been filled.  The block lives behind a
				Handle of its own, and is what everything outside calls a
				dictionary.

				The first two bytes of the data say what it is: 'a', then
				a byte whose low three bits are the kind and whose high
				four are the size of the attribute each word carries.  The
				kind picks which of the three walkers the calls go to -
				`AL` (kind 1) and `AL16` (kind 2) for the lexicons built
				into the ROM, `AEnum` (kinds 3, 5 and 7) for the ones the
				machine writes, which is what the user's own words go in.
				Bit 3 of that byte says the Handle must be locked while
				the walker runs.

				Everything goes through one selector call, `CallAirusA`,
				and one global: `AE_Parms`, the block the walkers are
				working on.  The engine is not re-entrant, and neither is
				this.

	Reconstructed from the MP2x00 US ROM (0x00028d7c-0x0002e300); each
	function cites its origin.
*/

#ifndef __AIRUS_H
#define __AIRUS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __NEWTONMEMORY_H
#include "NewtonMemory.h"
#endif

// the kinds a dictionary's second byte may name (its low three bits)
enum
{
	kAirusKindAL			= 1,	// the ROM's 8-bit lexicon
	kAirusKindAL16			= 2,	// the ROM's 16-bit lexicon
	kAirusKindEnum			= 3,	// the walkers the machine writes with
	kAirusKindEnum16		= 5,
	kAirusKindEnumRAM		= 7		// what NewDictionary makes
};

const UByte	kAirusSignature		= 'a';		// the first byte of every dictionary
const UByte	kAirusLockedBit		= 0x08;		// the Handle is locked while a walker runs

// what CallAirusA is asked to do
enum
{
	kAirusStartA			= 0,
	kAirusExitA				= 1,
	kAirusVerify			= 2,
	kAirusAddWord			= 3,
	kAirusDeleteWord		= 4,
	kAirusFirstLast			= 5,
	kAirusNextPrevious		= 6,
	kAirusChangeAttribute	= 7,
	kAirusNextSet			= 8,
	kAirusNextSet9			= 9
};

// airusResult, and what ExpandDict leaves in the block
const long	kAirusNoMemory		= -2;
const long	kAirusExpandFailed	= 2;

// 0x58 bytes, always reached through a Handle.  The fields named by their
// offset are the ones the walkers use and this reconstruction has not
// needed yet; they are set as the ROM sets them so that a dictionary this
// makes is the one the ROM's own code would have made.
struct AirusAParmBlock
{
	ULong		fVersion;			// +0x00  the engine's version word (gAirusVersion)
	long		fField04;			// +0x04  -1
	Handle		fDataHandle;		// +0x08  the dictionary's bytes
	Ptr			fData;				// +0x0c  *fDataHandle, as it lies now
	Ptr			fDataEnd;			// +0x10  one past the last byte in use
	long		fSize;				// +0x14  the Handle's size
	long		fGrowBy;			// +0x18  how much ExpandDict adds at a time
	UByte*		fWord;				// +0x1c  the characters being looked at (one buffer per kind, shared)
	long		fIndex;				// +0x20  in: the index of the last character to match; out: how far it got
	ULong		fAttribute;			// +0x24  the attribute of the word that was found
	long		fNode;				// +0x28  the node reached, as an offset into the data (0: start at the root)
	long		fResult;			// +0x2c  what the walker found
	ULong		fSymbol;			// +0x30  the character after the word, when there is only one
	long		fAttributeSize;		// +0x34  bytes of attribute per word
	long		fField38;			// +0x38  0
	long		fField3c;			// +0x3c  1
	Handle		fSelf;				// +0x40  the block's own Handle
	long		fField44;			// +0x44  0
	long		fField48;			// +0x48  0
	long		fField4c;			// +0x4c  1
	long		fField50;
	long		fField54;
};

extern long				airusResult;	// ROM 0x0c100810 airusResult - what the last call left
extern ULong			gAirusVersion;	// (0x0c100814, which the ROM keeps no symbol for) the word every new dictionary carries
extern AirusAParmBlock*	AE_Parms;		// ROM 0x0c10082c AE_Parms - the block being worked on

// the container
Handle	NewDictionary(UByte type, long attributeSize);		// ROM 0x00028d7c NewDictionary - an empty one; nil, with airusResult, when there is no room
void	CheckDictPtrs(AirusAParmBlock* parms);				// ROM 0x00029944 CheckDictPtrs__FP15AirusAParmBlock - the pointers re-read after the Handle may have moved, and AE_Parms set
long	ExpandDict(long extra);								// ROM 0x0002998c ExpandDict__FUl - room made for that many more bytes; ==> 0, or 2 when there is none
void	SlideUp(long offset, long count);					// ROM 0x00028ec4 SlideUp__FUlT1 - the bytes from offset moved down over count of them
void	SlideDown(long offset, long count);					// ROM 0x00028f18 SlideDown__FUlT1 - ... and up, making room

// A node of the trie, in order:
//   the character  (one byte, or two in a 16-bit dictionary)
//   the flags      (one byte: bits 7-6 the size class of the sibling
//                   offset, bit 5 "no children", bit 4 "an attribute
//                   follows", bits 3-0 the top of the sibling offset)
//   the sibling offset (kAirusRPByteSize more bytes, by the size class)
//   the attribute  (fAttributeSize bytes, when bit 4 is set)
//   then the first child, and after all the children the sibling.
// The sibling offset is counted from the end of the attribute.
const UByte	kAirusSizeMask		= 0xc0;		// the size class of the sibling offset
const UByte	kAirusNoChildren	= 0x20;
const UByte	kAirusHasAttribute	= 0x10;

// what a walk of the trie found (the block's fResult)
enum
{
	kAirusPrefix			= 0,	// the word leads somewhere, and carries no attribute
	kAirusPrefixWithAttr	= 1,	// ... and carries one
	kAirusLeaf				= 2,	// the word is a whole one: the node it ends at has no children
	kAirusNoMatch			= 3		// no word begins that way
};

extern const unsigned int	kAirusRPMask[4];	// AirusTables.cpp, generated
extern const unsigned int	kAirusNodeSize[4];

// reading a node
long	AirusCharSize(void);								// the characters of the dictionary being walked: 2 in a 16-bit one, else 1 (the ROM writes the test out wherever it needs it)
long	RPByteSize(long node);								// ROM 0x0002b5a4 RPByteSize__FUl - the bytes the node's sibling offset takes
long	SkipNode(long node);								// ROM 0x0002b608 SkipNode__FUl - the offset just past its character, flags and sibling offset
ULong	GetAttr(long offset);								// ROM 0x0002b6b4 GetAttr__FUl - the attribute lying there
long	FollowLeft(long node);								// ROM 0x00028fa0 FollowLeft__FUl - the offset of its first child
ULong	GetSymbol(long node);								// ROM 0x00029318 GetSymbol__FUl - its character

// walking
long	AE8_Verify(AirusAParmBlock* parms);					// ROM 0x0002b048 AE8_Verify__FP15AirusAParmBlock
long	AEnum_Verify(AirusAParmBlock* parms);				// ROM 0x0002b584 AEnum_Verify__FP15AirusAParmBlock - AE8 or AE16 by the dictionary's kind

// the data, big-endian as it lies
ULong	GetDictBytes(long offset, long count);				// ROM 0x0002a178 GetDictBytes__FUli

// the dispatcher
void	CallAirusA(Handle dictionary, long selector);		// ROM 0x0002d41c CallAirusA - the Handle locked first when the dictionary asks for it
void	CallAirusANoLock(Handle dictionary, long selector);	// ROM 0x0002d574 CallAirusANoLock

#endif	/* __AIRUS_H */
