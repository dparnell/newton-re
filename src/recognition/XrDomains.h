/*
	File:		recognition/XrDomains.h

	Contains:	The cursive recogniser's two domains and the parameter
				blocks an area runs them with.

				'STXR' (`TStrXrDomain`, "Strokes to Xrs Domain") takes
				strokes and cuts them into xrs; 'XRWR' (`TXrWordDomain`,
				"Xrs to Word Domain") takes the xrs and reads words out of
				them.  `InstallWordRecognizer` (`WordRecognizer.h`) makes
				both at boot and hangs a recogniser off the second, asleep
				unless the writer's letter set is a cursive one.

				Everything else speaks to the engine through the word
				domain's `DomainParameter`, with the area's parameter
				block (an `XRWORDPARAM` in a handle) as the last argument.
				Its selectors:

				  0        how big the block is (0x160)
				  1        a new block filled in: the vocabularies, the
				           letter table and the letter set's learning info
				           loaded (InitializeParamStruct)
				  2        whether the selector in `result` is one it knows
				  3        everything the block holds let go
				  0x20001  the dictionary chains, read   0x20002  set
				  0x20005  the field type, read          0x20006  set
				  0x20007  the speed, read               0x20008  set
				  0x20009  a dictionary put back in the chain it was
				           taken out of by 0x2000a
				  0x20010  what the writer settled on learnt (XRWDoLearning)
				  0x20011  the third chain, read         0x20012  set
				  0x20014  whether a word is non-empty
				  0x20015  the learning info put back to the defaults
				  0x20016  a group of a letter's variants given a weight
				  0x20017  ... and read
				  0x20018  whether every character of a string has a
				           variant at all
				  0x20019/0x20020  the block marked for reloading / not
				  0x20021  the learning info's size    0x20022  its handle
				  0x20023/0x20024  learning off / on
				  0x20029  the two punctuation sets, set
				  0x20030  ... read                     0x20031  reset
				  0x20032  a single field changed (SetXrWordRC)
				  0x20033-0x20038  word training and orthographic
				           learning on and off
				  0x20039  the orthographic database's size
				  0x20040  ... its handle
				  0x20041  the letter set chosen
				  0x20046  the language (English or international)
				  0x20047  ... read

				The block is the engine's `rc_type` (0x10c bytes of the
				ROM) with the domain's own fields after it.  Its numbers
				are big-endian halfwords, which the ROM writes a byte at a
				time; the host keeps them the same way, in byte arrays at
				the ROM's offsets (`RCByte`), so that a field named by its
				offset - which is how `SetXrWordRC` is told which to
				change - is the same field.  DEVIATION: the words that hold
				pointers are host pointers, kept apart from the arrays;
				an offset that falls in one of them names nothing on the
				host.

	Reconstructed from the MP2x00 US ROM (0x00065dd4-0x000662d0,
	0x00087280-0x0008767c, 0x000d69dc-0x000d7074, 0x00105bb8-0x00105ca0,
	0x0021fc7c-0x0021fd74, 0x0024dff4-0x0024fb04); each function cites its
	origin.
*/

#ifndef __XRDOMAINS_H
#define __XRDOMAINS_H

#ifndef __DOMAIN_H
#include "Domain.h"
#endif
#ifndef __PARAGRAPH_H
#include "ParaGraph.h"
#endif

// The third dictionary chain and what goes with it (selectors
// 0x20011/0x20012).  DEVIATION: a host pointer - the ROM's is 12 bytes.
struct XrChainInfo
{
	void*		fChain;				// +0x00
	long		f04;				// +0x04
	UByte		f08;				// +0x08
};

// The engine's recognition parameters.
struct rc_type
{
	UByte		fB00[0x30];			// ROM +0x00  24 halfwords: +0 the kind of field, +2 and +8 flags, +4 the letter set's style, +6 the language, +0x10/+0x14 the speed's two numbers, +0x22 learning, +0x24 use the learning info
	void*		fDTI;				// +0x30  the DTE header's handle (a DTIHeader* while locked)
	Handle		fVocs[15];			// +0x34  the vocabularies
	Handle		fTrigrams;			// +0x70
	void*		fChain;				// +0x74  the third dictionary chain
	const char*	fAlphaCharset;		// +0x78
	const char*	fNumCharset;		// +0x7c
	const char*	fMathCharset;		// +0x80
	const char*	fLPunctCharset;		// +0x84
	const char*	fEPunctCharset;		// +0x88
	const char*	fOtherCharset;		// +0x8c
	UByte		fB90[0x2c];			// ROM +0x90..0xbb  halfwords and bytes (+0xb8: bit 3 orthographic learning)
	void*		fOrtho;				// +0xbc  the orthographic database's handle (a pointer while locked)
	UByte		fBC0[0x4c];			// ROM +0xc0..0x10b
};

// The word domain's parameter block.
struct XRWORDPARAM : public rc_type
{
	UByte		fB10C[0x0c];		// ROM +0x10c  a byte (the block to be reloaded), +0x110 the field type and +0x114 the speed (big-endian words)
	void*		fChain0;			// +0x118  the two dictionary chains
	void*		fChain1;			// +0x11c
	Handle		fRemovedDict;		// +0x120  a dictionary taken out of chain 0 (0x2000a)
	UByte		fB124[4];			// ROM +0x124  its position (a halfword)
	XrChainInfo	fChainInfo;			// +0x128
	UByte		fB134[0x2c];		// ROM +0x134..0x15f  the two punctuation sets (21 each) and two bytes
};

enum { kXRWORDPARAMSize = 0x160 };	// what DomainParameter 0 answers: the ROM's size

// The byte of the block at a ROM offset; nil for one that is part of a
// pointer on the host.
UByte*	RCByte(rc_type* rc, ULong offset);
UByte*	XRWByte(XRWORDPARAM* param, ULong offset);
inline UShort	RCGetH(rc_type* rc, ULong offset)				{ return (RCByte(rc, offset)[0] << 8) | RCByte(rc, offset)[1]; }
inline void		RCSetH(rc_type* rc, ULong offset, UShort v)	{ RCByte(rc, offset)[0] = v >> 8; RCByte(rc, offset)[1] = v; }
inline ULong	XRWGetW(XRWORDPARAM* p, ULong offset)			{ UByte* b = XRWByte(p, offset); return ((ULong) b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; }
inline void		XRWSetW(XRWORDPARAM* p, ULong offset, ULong v)	{ UByte* b = XRWByte(p, offset); b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = v; }

// What GCLockDTEAndLearningData keeps of the block to put back.  (The
// ROM's is 0x48 bytes, of which these two words are the ones used here.)
struct RcHandlesType
{
	void*		fDTI;				// +0x00
	void*		fOrtho;				// +0x44
};

// The DTE header and the orthographic database locked, and the block's
// words pointed at them.  ==> 1; 0 (everything let go again) when either
// could not be.
long	GCLockDTEAndLearningData(rc_type* rc, RcHandlesType* saved);	// ROM 0x000d69dc GCLockDTEAndLearningData__FPvP13RcHandlesType
void	GCUnlockDTEAndLearningData(rc_type* rc, RcHandlesType* saved);	// ROM 0x000d6a80 GCUnlockDTEAndLearningData__FPvP13RcHandlesType
// The character tables for the block's language, and with `setAlpha`
// its letters.
void	GCSetUpRecTableAndCharset(rc_type* rc, ULong setAlpha);	// ROM 0x000d7030 GCSetUpRecTableAndCharset__FP7rc_typeUi

// The kind of field (the recogniser's own flags: 1 words, 2 numbers,
// 4 upper-case, 8 punctuation, 0x10 phone, 0x20 single letters, 0x40
// ...) put into the block.  ==> 0, or -1 for none.
long	SetXrWordFieldType(ULong type, rc_type* rc);			// ROM 0x0024e394 SetXrWordFieldType__FUlP7rc_type
// The writer's speed (0-9, from the Handwriting Style slip) as the two
// numbers the engine reads.  ==> 0, or -1.
long	SetXrWordFieldSpeed(ULong speed, rc_type* rc, ULong type);	// ROM 0x0024e288 SetXrWordFieldSpeed__FUlP7rc_typeT1
ULong	PrintFieldType(ULong type);								// ROM 0x0024e64c PrintFieldType__FUl - (nothing: a debugging hook)

// A field of the block changed by a single word: bits 25-29 the
// operation (LongOperator), bits 16-23 which field, the low 16 bits the
// operand - or, with bit 24, the low 16 bits a byte offset into the
// block and bits 16-23 the operand.  A recognition configuration's
// `xrwCommands`/`strxrCommands` are arrays of these.
void	SetXrWordRC(ULong command, XRWORDPARAM* param);			// ROM 0x00065dd4 SetXrWordRC__FUlP11XRWORDPARAM
// *value = *value op operand: 0 =, 1 or, 2 and, 3 xor, 4 +, 5 -,
// 6 reverse -, 7 *, 8 /, 9 reverse /.
void	LongOperator(UByte op, long* value, long operand);		// ROM 0x0006555c LongOperator__FUcPll
void	RCShortOperator(UByte op, UByte* field, short operand);	// ROM 0x0006561c RCShortOperator__FUcPss
void	RCUShortOperator(UByte op, UByte* field, UShort operand);	// ROM 0x00065664 RCUShortOperator__FUcPUsUs
void	RCCharOperator(UByte op, UByte* field, char operand);	// ROM 0x000656ac RCCharOperator__FUcPcc
void	RCUCharOperator(UByte op, UByte* field, UByte operand);	// ROM 0x000656e4 RCUCharOperator__FUcPUcT1
void	RCBooleanOperator(UByte op, UByte* field, UByte operand);	// ROM 0x0006571c RCBooleanOperator__FUcPUcT1

/*------------------------------------------------------------------------------
	L e a r n i n g
------------------------------------------------------------------------------*/

// What the engine records of a word it read, in the training data a
// cursive unit keeps: the word, and for each letter which variant it was
// read as (bit 7: in the other case).  Written by the reading engine,
// which is NOT YET.
struct rec_w_type
{
	UByte		fWord[0x18];		// +0x00
	UByte		fVariants[0x18];	// +0x18
	UByte		fX30[0x18];			// +0x30
	short		fWeight;			// +0x48
};

// What TWordRecognizer::DoLearning hands selector 0x20010: the unit's
// training data (a handle of LH entries), the pen's trace, how many
// points and which of the unit's readings the writer settled on.
struct XrLearningRecord
{
	Handle		fData;				// +0x00
	void*		fTrace;				// +0x04
	long		fCount;				// +0x08
	long		fIndex;				// +0x0c
};

Ptr		LHLock(Handle h);										// ROM 0x00105bb8 LHLock__FUl
long	LHUnLock(Handle h);										// ROM 0x00105bc8 LHUnLock__FUl
// An entry of the training data found by its three ids ('****' for any).
// ==> 0, -2 for no data, -3 for no such entry.
long	LHFindEntry(void* data, ULong id1, ULong id2, ULong id3, void** entry, ULong* size);	// ROM 0x00105bec LHFindEntry__FPvUlN22PPvPUl

// The learning info moved towards how the writer writes: the variants of
// a word's letters that were used have their counters set back, the
// others counted up, and the vexes moved by the counters.
long	FlyLearn(rc_type* rc, const rec_w_type* word);			// ROM 0x00087280 FlyLearn__FP7rc_typePC10rec_w_type
long	FLUpdateCounters(rc_type* rc, const rec_w_type* word);	// ROM 0x00087410 FLUpdateCounters__FP7rc_typePC10rec_w_type
long	FLUpdateStates(rc_type* rc, const rec_w_type* word);		// ROM 0x0008751c FLUpdateStates__FP7rc_typePC10rec_w_type
long	GetMinGroupVex(UByte c, UByte variant, rc_type* rc);	// ROM 0x0008767c GetMinGroupVex__FUcT1P7rc_type
long	XRWDoLearning(ULong record, XRWORDPARAM* param);		// ROM 0x0024e8a8 XRWDoLearning__FUlP11XRWORDPARAM

/*------------------------------------------------------------------------------
	T h e   d o m a i n s
------------------------------------------------------------------------------*/

enum
{
	kStrXrDomainType = 'STXR',
	kXrWordDomainType = 'XRWR'
};

// Strokes to xrs.  NOT YET RECONSTRUCTED beyond its making: the cutting
// of strokes into xrs (Classify, Group, PreGroup, Reclassify and the
// boxed and on-line segmentation) and its parameters (DomainParameter,
// SetParameters, SetStrXrFieldType, SetStrXrRC) belong to the reading
// engine.
class TStrXrDomain : public TDomain
{
public:
	static TStrXrDomain*	Make(TController* controller);		// ROM 0x0021fc7c Make__12TStrXrDomainSFP11TController
	void				IStrXrDomain(TController* controller);	// ROM 0x0021fcc4 IStrXrDomain__12TStrXrDomainFP11TController

	UByte				fB24[0x5c];		// ROM +0x24..0x7f  the parameters an area last gave it
};

// Xrs to words.  NOT YET RECONSTRUCTED: the reading itself (Classify,
// ClassifyXrWord, Reclassify, Group).
class TXrWordDomain : public TDomain
{
public:
	static TXrWordDomain*	Make(TController* controller);		// ROM 0x0024dff4 Make__13TXrWordDomainSFP11TController
	void				IXrWordDomain(TController* controller);	// ROM 0x0024e03c IXrWordDomain__13TXrWordDomainFP11TController

	virtual long		DomainParameter(ULong selector, ULong result, ULong info);	// ROM 0x0024ebfc DomainParameter__13TXrWordDomainFUlN21
	virtual Boolean		SetParameters(Handle params);			// ROM 0x0024f79c SetParameters__13TXrWordDomainFPPc
	virtual void		ConfigureSubDomain(TRecArea* area);		// ROM 0x0024f6f4 ConfigureSubDomain__13TXrWordDomainFP8TRecArea

	long				InitializeParamStruct(XRWORDPARAM* param);	// ROM 0x0024f84c InitializeParamStruct__13TXrWordDomainFP11XRWORDPARAM

	ULong				f24;			// +0x24
	rc_type				fRC;			// +0x28  the parameters of the area last read in
	UByte				f134;			// +0x134
	ULong				fFieldType;		// +0x138
	ULong				fSpeed;			// +0x13c
	void*				fChain0;		// +0x140
	void*				fChain1;		// +0x144
	Handle				fRemovedDict;	// +0x148
	UShort				fRemovedPosition;	// +0x14c
	XrChainInfo			fChainInfo;		// +0x150
	UByte				f15C;			// +0x15c
	UByte				f15D;			// +0x15d
};

#endif	/* __XRDOMAINS_H */
