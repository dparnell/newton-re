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
#ifndef __OBJECTS_H
#include "objects.h"
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

// what airusResult may hold
const long	kAirusNoSuchDictionary	= -12;	// the chain is shorter than the position asked for
const long	kAirusAlreadyThere		= 4;	// the word was in the dictionary already
const long	kAirusEmptyWord			= 5;
const long	kAirusIsPrefix			= 1;	// after VerifyString: the beginning of other words, and not one itself
const long	kAirusIsPrefixAndWord	= 2;	// ... and one as well
const long	kAirusIsWord			= 3;	// a word, with nothing going on from it
const long	kAirusNotAWord			= -6;
const long	kAirusBadDictionary		= -3;	// the bytes are not a dictionary
const long	kAirusEmptyDictionary	= -9;	// FirstCompletion: there are no words at all
const long	kAirusNoMoreWords		= -10;	// NextCompletion: that was the last
const long	kAirusDictionaryFull	= -15;	// the dictionary frame's `limit` was reached (AddWordWithCount)

// airusResult, and what ExpandDict leaves in the block
const long	kAirusNoMemory		= -2;
const long	kAirusExpandFailed	= 2;

// What AEnum_NextSet calls for each character that may follow a node:
// the context it was given, the character, the node it stands for (its
// offset, with its "carries an attribute" and "has no children" flags in
// the top two bits) and the attribute it carries.
typedef void (*AirusNextSetProc)(void* context, ULong character, ULong node, ULong attribute);

// 0x58 bytes, always reached through a Handle.  The fields named by their
// offset are the ones the walkers use and this reconstruction has not
// needed yet; they are set as the ROM sets them so that a dictionary this
// makes is the one the ROM's own code would have made.
struct AirusAParmBlock
{
	ULong		fVersion;			// +0x00  the engine's version word (gAirusVersion)
	long		fDictID;			// +0x04  which dictionary it is (InitDictionaries puts it there; NewDictionary leaves -1)
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
	Handle		fCurrent;			// +0x40  the dictionary of the chain the walk is on (NewDictionary sets it to the block's own Handle)
	Handle		fNext;				// +0x44  the next dictionary of the chain (nil: none)
	ULong		fField48;			// +0x48  0 - what VerifyString hands back beside the attribute
	long		fField4c;			// +0x4c  1
	void*		fWalkContext;		// +0x50  what NextSet hands its callback
	AirusNextSetProc fWalkProc;		// +0x54  ... and the callback itself
};

extern long				airusResult;	// ROM 0x0c100810 airusResult - what the last call left
extern ULong			gAirusVersion;	// (0x0c100814, which the ROM keeps no symbol for) the word every new dictionary carries
extern AirusAParmBlock*	AE_Parms;		// ROM 0x0c10082c AE_Parms - the block being worked on

// the container
Handle	NewDictionary(UByte type, long attributeSize);		// ROM 0x00028d7c NewDictionary - an empty one; nil, with airusResult, when there is no room
// A dictionary opened over bytes that already exist - the ones built
// into the ROM, which are read where they lie rather than copied.
Handle	BuildDictionaryFromHandle(Handle data);				// ROM 0x0002d0b0 BuildDictionaryFromHandle
Handle	BuildDictionaryFromPtr(void* data, Size size);		// ROM 0x0002d624 BuildDictionaryFromPtr
// ... and over the bytes of a binary object, which is how a dictionary
// that came off a store, out of a package or out of a locale bundle is
// opened.
Handle	ReadRefDictionary(RefArg binary);					// ROM 0x0002d6a0 ReadRefDictionary__FRC6RefVar
// The dictionary given back: its data and then the block itself.
void	DisposDictionary(Handle* dictionary);				// ROM 0x0002d6f4 DisposDictionary
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

// writing a node
void	PutDictBytes(long offset, long count, ULong value);	// ROM 0x000298b0 PutDictBytes__FUliT1
long	Ashortstrlen(const UniChar* s);						// ROM 0x0002e6fc Ashortstrlen__FPUs - twice the characters in it
UniChar*	CopyBufferHack(UByte* bytes, UniChar* chars, long back);	// ROM 0x00029a20 CopyBufferHack__FPUcPUsi
long	RPNibbleSize(long node);							// ROM 0x0002af64 RPNibbleSize__FUl
ULong	GetRP(long node);									// ROM 0x0002900c GetRP__FUl - the offset to its sibling
void	SetRPFlags(long node, long size);					// ROM 0x0002b654 SetRPFlags__FUli
long	FollowRight(long node);								// ROM 0x000290c4 FollowRight__FUl - the offset of its sibling
long	ClearRP(long node);									// ROM 0x00029158 ClearRP__FUl
long	PutRP(long node, ULong offset);						// ROM 0x0002919c PutRP__FUlT1 - ==> how much the data grew
long	PutAttr(long offset);								// ROM 0x0002b708 PutAttr__FUl
long	ClearAttr(long offset);								// ROM 0x00028f6c ClearAttr__FUl

// walking
long	AE8_Verify(AirusAParmBlock* parms);					// ROM 0x0002b048 AE8_Verify__FP15AirusAParmBlock
long	AEnum_Verify(AirusAParmBlock* parms);				// ROM 0x0002b584 AEnum_Verify__FP15AirusAParmBlock - AE8 or AE16 by the dictionary's kind
long	AEnum_AddWord(AirusAParmBlock* parms);				// ROM 0x00029b10 AEnum_AddWord__FP15AirusAParmBlock
long	AEnum_DeleteWord(AirusAParmBlock* parms);			// ROM 0x00029e3c AEnum_DeleteWord__FP15AirusAParmBlock				// ROM 0x00029b10 AEnum_AddWord__FP15AirusAParmBlock
Handle	PositionToHandle(Handle dictionary, ULong position);	// ROM 0x0002d658 PositionToHandle
void	AddWord(Handle dictionary, ULong position, UByte* word, ULong attribute);	// ROM 0x0002c48c AddWord__FPP15AirusAParmBlockUlPUcT2				// ROM 0x0002b584 AEnum_Verify__FP15AirusAParmBlock - AE8 or AE16 by the dictionary's kind
// ... and one taken out; airusResult 0 it went, 4 it was not there, 5
// the word was empty, -2 the Handle could not be resized.
void	DeleteWord(Handle dictionary, UByte* word);			// ROM 0x0002c56c DeleteWord
// How many bytes of attribute each word carries (the oldest writable
// kind carries one without declaring it).
long	AttributeLength(Handle dictionary);					// ROM 0x0002d37c AttributeLength
// ... and that attribute changed; airusResult 0 it was, -7 the
// dictionary carries none, -6 the word is not in it.
void	ChangeAttribute(Handle dictionary, UByte* word, ULong attribute);	// ROM 0x0002d3b8 ChangeAttribute

// the data, big-endian as it lies
ULong	GetDictBytes(long offset, long count);				// ROM 0x0002a178 GetDictBytes__FUli

// the engine's own string handling
long	Astrlen(const char* s);								// ROM 0x0002e6e0 Astrlen__FPc
void	Astrcpy(char* dest, const char* src);				// ROM 0x0002e738 Astrcpy__FPcT1
char*	Astrchr(char* str, char c);							// ROM 0x0002e7a8 Astrchr__FPcc
void	Ashortstrcpy(UniChar* dest, const UniChar* src);	// ROM 0x0002e764 Ashortstrcpy__FPUsT1

// the way in
Boolean	HasActualOrImpliedAtr(Handle dictionary);			// ROM 0x0002c770 HasActualOrImpliedAtr__FPP15AirusAParmBlock
void	NewVerifyReset(Handle dictionary, ULong position, long node, const UByte* word);	// ROM 0x0002c6a8 NewVerifyReset
void	VerifyStart(Handle dictionary);						// ROM 0x0002c760 VerifyStart__FPP15AirusAParmBlock
void	VerifyString(Handle dictionary, const void* word, void** terminal, ULong** attribute, ULong* extra);	// ROM 0x0002cd20 VerifyString
// The words of a dictionary in alphabetical order: the first beginning
// with a prefix, and the one after a word.
void	FirstCompletion(Handle dictionary, const void* prefix, void* word, ULong** attribute, ULong* extra);	// ROM 0x0002cf0c FirstCompletion
void	NextCompletion(Handle dictionary, const void* prefix, void* word, const void* last, ULong** attribute, ULong* extra);	// ROM 0x0002d224 NextCompletion
long	AEnum_FirstLast(AirusAParmBlock* parms);			// ROM 0x0002a1f4 AEnum_FirstLast__FP15AirusAParmBlock
long	AEnum_NextPrevious(AirusAParmBlock* parms);			// ROM 0x0002a244 AEnum_NextPrevious__FP15AirusAParmBlock

// the dispatcher
// The ROM's own lexicons: read-only tries whose nodes stand for a set
// of characters rather than for one.  ==> the block's result.
long	AirusAL(ULong selector, AirusAParmBlock* parms);		// ROM 0x0002bdf4 AirusAL__FUlP15AirusAParmBlock
long	AirusAL16(ULong selector, AirusAParmBlock* parms);	// ROM 0x0002b790 AirusAL16__FUlP15AirusAParmBlock
void	AL_Verify(AirusAParmBlock* parms);					// ROM 0x0002bf78 AL_Verify__FP15AirusAParmBlock
void	AL16_Verify(AirusAParmBlock* parms);				// ROM 0x0002b918 AL16_Verify__FP15AirusAParmBlock
// A string with its repeated characters taken out.
void	AL_FilterString(char* str);							// ROM 0x0002c170 AL_FilterString__FPc

// What may come next: one row of the trie.
void	AE8_NextSet9(AirusAParmBlock* parms);				// ROM 0x0002a9f4 AE8_NextSet9__FP15AirusAParmBlock
void	AE8_NextSetCB(void* context, ULong character, ULong node, ULong attribute);	// ROM 0x0002af38 AE8_NextSetCB__FUlN31
// The characters that may follow the block's node, written into its word
// buffer and terminated.
void	AEnum_NextSet(AirusAParmBlock* parms);				// ROM 0x0002afd0 AEnum_NextSet__FP15AirusAParmBlock
// The attribute of a word already there written over where it lies.
void	AEnum_ChangeAttribute(AirusAParmBlock* parms);		// ROM 0x0002a7cc AEnum_ChangeAttribute__FP15AirusAParmBlock

// Walking a whole dictionary.  The callback is given the word, the
// attribute stored with it, the one character that could follow it (0
// when there is not exactly one) and how many words have been reached;
// it answers whether to go on.
typedef Boolean (*DictWalkProc)(UByte* word, ULong attribute, UByte terminal,
								long count, void* context);

// What the walk carries with it (the ROM's own, 0x50 bytes: the word
// buffer is the rest of it).
struct DictWalkBlock
{
	Handle			fDictionary;	// +0x00
	long			fCount;			// +0x04  how many words have been reached
	DictWalkProc	fProc;			// +0x08
	void*			fContext;		// +0x0c
	UByte			fWord[64];		// +0x10  the word the walk is standing on
};

Boolean	A8_PrefixCompletions(DictWalkBlock* block);			// ROM 0x0002d73c A8_PrefixCompletions__FP13DictWalkBlock
Boolean	A8_WalkNextChars(DictWalkBlock* block, long length);	// ROM 0x0002d890 A8_WalkNextChars__FP13DictWalkBlockUl
// ==> how many words were reached.  A nil callback only counts.
long	WalkDictionary(Handle dictionary, const UByte* prefix, DictWalkProc proc, void* context);	// ROM 0x0002e0f0 WalkDictionary__FPP15AirusAParmBlockPUcPFPUcUlUcT2Pv_UcPv

// A word and everything that goes on from it taken out at once.
void	DeletePrefix(Handle dictionary, UByte* word);		// ROM 0x0002c60c DeletePrefix

void	CallAirusA(Handle dictionary, long selector);		// ROM 0x0002d41c CallAirusA - the Handle locked first when the dictionary asks for it
void	CallAirusANoLock(Handle dictionary, long selector);	// ROM 0x0002d574 CallAirusANoLock

#endif	/* __AIRUS_H */
