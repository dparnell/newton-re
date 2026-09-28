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
#ifndef __UNIT_H
#include "Unit.h"
#endif
#ifndef __WORDSEGMENT_H
#include "WordSegment.h"
#endif
#ifndef __WORDUNIT_H
#include "WordUnit.h"
#endif

class TStrXrUnit;
class TXrWordDomain;
struct dInfoRec;
struct GCWordDescrType;
struct GCRecResults;
struct GCGroupParmStruct;

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
	UByte		fBC0[0x38];			// ROM +0xc0..0xf7  (+0xd8 the stroka_data SetRCB writes: the ink box and four base-line halfwords, +0xea..0xf0 the base line read, +0xf4/+0xf6 a fixed base line)
	void*		fTrace;				// +0xf8  the trace being read (GCTryToRecognize)
	UByte		fBFC[0x0c];			// ROM +0xfc..0x107
	void*		fWordInfo;			// +0x108  the word descriptor's ws_word_info_type while it is read
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

// What GCLockRecognitionData keeps of the block to put back: the
// handles its locked pointers replace (0x48 bytes in the ROM).
struct RcHandlesType
{
	void*		fDTI;				// +0x00
	Handle		fTrigrams;			// +0x04
	Handle		fVocs[15];			// +0x08
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
struct STRXRPARAM;
void	SetStrXrRC(ULong command, STRXRPARAM* param);			// ROM 0x000651e4 SetStrXrRC__FUlP10STRXRPARAM - one word of a configuration's strxrCommands
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
	UByte		fX4A[6];			// +0x4a
};
static_assert(sizeof(rec_w_type) == 0x50, "a rec_w_type is 0x50 bytes, as in the ROM");

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

long	LHAddEntry(Handle* h, ULong id1, ULong id2, ULong id3, void* data, ULong size);	// ROM 0x001059b4 LHAddEntry__FPUlUlN22PvT2 - ==> 0, -2 no data, -4 already there, -1 no memory
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

// The strokes-to-xrs domain's parameter block (0x58 bytes in the ROM).
struct STRXRPARAM
{
	UShort		fFlags;				// +0x00  what the field's writing is made of: 1 letters, 2 no letters, 0x10/0x20 words/digits, 0x400 cursive letter style (or 0x40 in the field type), 0x0800 kept, 0x8000 punctuation
	UShort		fFlags2;			// +0x02  1 not phone numbers only, 2 phone numbers
	Handle		fDTI;				// +0x04  the letter table (selector 1)
	ULong		fFieldType;			// +0x08  the recogniser's field flags (as XRWR's)
	ULong		fControl;			// +0x0c  low 16 bits the writer's letter spacing, 0x10000 read only at the end (no pre-grouping), from bit 17 how many words to wait for (lineAtATime)
	long		fGeom[7];			// +0x10  the base line geometry (0x2000b/c): +0x18 the base line, +0x20 its height ...
	long		fGrid[4];			// +0x2c  the boxes to write in (0x2000d/e): two sizes, two gaps
	ULong		fBoxHit;			// +0x3c  the box the last stroke hit (x in the low half, y in the high)
	UShort		fLetterStyle;		// +0x40  (from the letter set)
	UShort		fLanguage;			// +0x42  1 English, 8 international
	UShort		fField44;			// +0x44
	UShort		fField46;
	ULong		fField48;			// +0x48  (numbers and phone fields, when fField4C)
	ULong		fField4C;			// +0x4c
	short		fPrevBase[4];		// +0x50  the base line of the word before (0x20042): top, base, and two more
};

// Strokes to xrs: the writing grouped into words by ParaGraph's word
// segmenter (on line) or by the boxes it is written in, and each word
// read (CallGroupAndClassify, over the GC layer: InkGroups.h,
// WordDescriptors.h, CursiveReader.h) - the words it reads handed on as
// new units of its own type, which the xrs-to-words domain turns into
// word units.
class TStrXrDomain : public TDomain
{
public:
	static TStrXrDomain*	Make(TController* controller);		// ROM 0x0021fc7c Make__12TStrXrDomainSFP11TController
	void				IStrXrDomain(TController* controller);	// ROM 0x0021fcc4 IStrXrDomain__12TStrXrDomainFP11TController

	virtual void		Dispose(void);							// ROM 0x00220210 Dispose__12TStrXrDomainFv (nothing)
	virtual void		Classify(TUnit* unit);					// ROM 0x00220214 Classify__12TStrXrDomainFP5TUnit - a unit not yet read (0x8000000) read
	virtual void		Reclassify(TUnit* unit);				// ROM 0x0022024c Reclassify__12TStrXrDomainFP5TUnit - everything it read thrown away and read again
	virtual long		Group(TUnit* unit, dInfoRec* info);		// ROM 0x002202e0 Group__12TStrXrDomainFP5TUnitP8dInfoRec
	virtual long		PreGroup(TUnit* unit);					// ROM 0x00220344 PreGroup__12TStrXrDomainFP5TUnit
	virtual long		DomainParameter(ULong selector, ULong result, ULong info);	// ROM 0x0022037c DomainParameter__12TStrXrDomainFUlN21
	virtual Boolean		SetParameters(Handle params);			// ROM 0x00220a4c SetParameters__12TStrXrDomainFPPc

	void				ReclassifyStrXr(TStrXrUnit* unit);		// ROM 0x0021fd74 ReclassifyStrXr__12TStrXrDomainFP10TStrXrUnit
	void				ClassifyStrXr(TStrXrUnit* unit);		// ROM 0x00220ba0 ClassifyStrXr__12TStrXrDomainFP10TStrXrUnit
	void				StartWord(TStrokeUnit* stroke);			// ROM 0x0021fd94 StartWord__12TStrXrDomainFP11TStrokeUnit - a new word of one stroke
	long				GroupOnLineSegmentation(TUnit* stroke);	// ROM 0x0021fe10 GroupOnLineSegmentation__12TStrXrDomainFP5TUnit
	Boolean				GroupBoxedSegmentation(TUnit* stroke);	// ROM 0x0021fe18 GroupBoxedSegmentation__12TStrXrDomainFP5TUnit
	Boolean				AddStrokeToBoxedWord(TStrokeUnit* stroke);	// ROM 0x0021fe60 AddStrokeToBoxedWord__12TStrXrDomainFP11TStrokeUnit - whether the stroke is in the box the last was
	ULong				BoxHit(TStrokeUnit* stroke);			// ROM 0x0021ff30 BoxHit__12TStrXrDomainFP11TStrokeUnit - the box the stroke's middle is in (x in the low half, y in the high; 0xffff for no boxes that way)

	// the parameters the area last gave it (SetParameters: a copy of the
	// STRXRPARAM, at the ROM's offsets)
	short				fFlags;			// +0x24
	short				fFlags2;		// +0x26
	Handle				fDTI;			// +0x28
	ULong				fField2C;		// +0x2c
	ULong				fFieldType;		// +0x30
	ULong				fControl;		// +0x34
	long				fGeom[7];		// +0x38
	long				fGrid[4];		// +0x54  the box origin and size (x, y, width, height)
	ULong				fBoxHit;		// +0x64
	short				fLetterStyle;	// +0x68
	short				fLanguage;		// +0x6a
	short				fField6C;		// +0x6c
	ULong				fField70;		// +0x70  (rc +0xb6: read numbers first)
	ULong				fField74;		// +0x74
	short				fPrevBase[4];	// +0x78
};

// A strokes-to-xrs unit: the strokes of a piece of writing (its subs),
// the words found in them (the word descriptors and the segmenter's
// state), and - once one word has been read - that word's readings and
// where it lies (0xb0 bytes in the ROM).
class TStrXrUnit : public TSIUnit
{
public:
	static TStrXrUnit*	Make(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x00220bcc Make__10TStrXrUnitSFP7TDomainUlP6TArray
	long				IStrXrUnit(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x00220c44 IStrXrUnit__10TStrXrUnitFP7TDomainUlP6TArray

	virtual void		Dump(TMsg* msg);						// ROM 0x00220dc0 Dump__10TStrXrUnitFP4TMsg
	virtual long		SizeInBytes(void);						// ROM 0x00220e08 SizeInBytes__10TStrXrUnitFv
	virtual void		IDispose(void);							// ROM 0x00220d2c IDispose__10TStrXrUnitFv

	long				fLeft;			// +0x40  the word's ink, in tablet units (eighths of a pixel)
	long				fRight;			// +0x44
	long				fBase;			// +0x48  the base line the engine found
	long				fBase2;			// +0x4c
	long				fHeight;		// +0x50
	long				fHeight2;		// +0x54
	long				fField58;		// +0x58
	long				fField5C;		// +0x5c
	long				fField60;		// +0x60
	short				fLineHeight;	// +0x64  what the segmenter said of its line
	short				fBaseLine;		// +0x66
	short				fNewLine;		// +0x68
	short				fPrevBase[4];	// +0x6a
	short				fWordCount;		// +0x72  the readings
	rec_w_type*			fWords;			// +0x74
	Handle				fLearning;		// +0x78
	Handle				fDescriptors;	// +0x7c  the word descriptors (GCNewRecSegment)
	Handle				fGRes;			// +0x80  the segmenter's state
	short				fNumStrokes;	// +0x84  strokes given to the segmenter
	short				fNext;			// +0x86
	UByte				fStrokes[32];	// +0x88  the strokes to segment (a bit each)
	short				fJoinX;			// +0xa8  a word after a dash: where the line it continues ended
	short				fJoinY;			// +0xaa
	UByte				fMerged;		// +0xac
	UByte				fActive;		// +0xad  still collecting strokes (1), or one word read (0)
	UByte				fRereading;		// +0xae
};


// A word the cursive reader read (ROM 100 bytes): its readings as
// interpretations, the STXR unit it was made of as its sub, the ink's
// box and base line in tablet units, and what the learning is given.
class TXrWordUnit : public TStdWordUnit
{
public:
	static TXrWordUnit*	Make(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0024fb04 Make__11TXrWordUnitSFP7TDomainUlP6TArray
	long				IXrWordUnit(TDomain* domain, ULong kind, TArray* areas);	// ROM 0x0024fb7c IXrWordUnit__11TXrWordUnitFP7TDomainUlP6TArray

	virtual void		IDispose(void);							// ROM 0x0024fbb0 IDispose__11TXrWordUnitFv - the training data given back first
	virtual void		GetWordBase(FPoint* left, FPoint* right, ULong index);	// ROM 0x0024fbe4 GetWordBase__11TXrWordUnitFP6FPointT1Ul - the base line the engine found, in pixels
	virtual long		GetWordSlant(ULong index);				// ROM 0x0024fc58 GetWordSlant__11TXrWordUnitFUl
	virtual long		GetWordSize(ULong index);				// ROM 0x0024fc64 GetWordSize__11TXrWordUnitFUl - the mean height of the two ends, in pixels
	virtual Handle		GetTrainingData(long index);			// ROM 0x0024fcdc GetTrainingData__11TXrWordUnitFl - a copy of it
	virtual void		DisposeTrainingData(Handle data);		// ROM 0x0024fd48 DisposeTrainingData__11TXrWordUnitFPPc

	long				fLeft;			// +0x3c  (the STXR unit's +0x40..+0x60)
	long				fRight;			// +0x40
	long				fBase;			// +0x44
	long				fBase2;			// +0x48
	long				fHeight;		// +0x4c
	long				fHeight2;		// +0x50
	long				fSlant;			// +0x54
	long				fField58;		// +0x58
	long				fField5C;		// +0x5c
	Handle				fLearning;		// +0x60  the training data (GCFillLearningHandle)
};

// Xrs to words: an STXR unit (a word the cursive reader has read) made
// into a word unit whose interpretations are its readings.
class TXrWordDomain : public TDomain
{
public:
	static TXrWordDomain*	Make(TController* controller);		// ROM 0x0024dff4 Make__13TXrWordDomainSFP11TController
	void				IXrWordDomain(TController* controller);	// ROM 0x0024e03c IXrWordDomain__13TXrWordDomainFP11TController

	virtual void		Dispose(void);							// ROM 0x0024ea2c Dispose__13TXrWordDomainFv (nothing)
	virtual void		Classify(TUnit* unit);					// ROM 0x0024ea30 Classify__13TXrWordDomainFP5TUnit
	virtual void		Reclassify(TUnit* unit);				// ROM 0x0024eab4 Reclassify__13TXrWordDomainFP5TUnit
	virtual long		Group(TUnit* unit, dInfoRec* info);		// ROM 0x0024eb28 Group__13TXrWordDomainFP5TUnitP8dInfoRec
	void				ClassifyXrWord(TXrWordUnit* unit);		// ROM 0x0024e09c ClassifyXrWord__13TXrWordDomainFP11TXrWordUnit
	void				TakeReadings(TXrWordUnit* unit);		// (the body ClassifyXrWord and Reclassify share)

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

// The GC layer between the domain and the engine.
long	GCPregroupAndGroup(TStrXrDomain* domain, TStrokeUnit* stroke, ULong pregroup);	// ROM 0x000d39c8 GCPregroupAndGroup__FP12TStrXrDomainP11TStrokeUnitUi
ULong	CallGroupAndClassify(TStrXrDomain* domain, TStrXrUnit* unit, TStrokeUnit* stroke, ULong classify, ULong pregroup, ULong lineAtATime);	// ROM 0x000d3b0c CallGroupAndClassify__FP12TStrXrDomainP10TStrXrUnitP11TStrokeUnitUiN24
long	GCReleaseRecResults(TStrXrDomain* domain, TStrXrUnit* unit, TStrokeUnit* stroke, UByte* strokes, short base, ULong classify, ULong* released);	// ROM 0x000d4400 GCReleaseRecResults__FP12TStrXrDomainP10TStrXrUnitP11TStrokeUnitPUcsUiPUi
long	WriteRecResults(TStrXrDomain* domain, TStrXrUnit* unit, TStrokeUnit* stroke, TStrXrUnit** last, GCWordDescrType* word, short base, ULong classify, ULong* released);	// ROM 0x000d4540 WriteRecResults__FP12TStrXrDomainP10TStrXrUnitP11TStrokeUnitPP10TStrXrUnitP15GCWordDescrTypesUiPUi
long	GCWriteRW(TStrXrUnit* unit, GCRecResults* results, long part);	// ROM 0x000d4b6c GCWriteRW__FP10TStrXrUnitP12GCRecResultsi
long	GCFillRecParmStruct(TStrXrDomain* domain, TUnit* unit, rc_type* rc);	// ROM 0x000d4f00 GCFillRecParmStruct__FP12TStrXrDomainP5TUnitP7rc_type
long	GCClearChains(TStrXrDomain* domain, TUnit* unit);			// ROM 0x000d4ff0 GCClearChains__FP12TStrXrDomainP5TUnit
void	GCAllocRecTrace(TStrXrUnit* unit, TStrokeUnit* stroke, UByte* strokes, short base, PS_point_type** trace, short* nPoints);	// ROM 0x000d502c GCAllocRecTrace__FP10TStrXrUnitP11TStrokeUnitPUcsPP13PS_point_typePs
long	GCGetUnitRealStrokeIndex(TStrXrUnit* unit, short index);	// ROM 0x000d51d4 GCGetUnitRealStrokeIndex__FP10TStrXrUnits
long	GroupAndClassifyStrokes(PS_point_type* trace, short nPoints, rc_type* rc, GCGroupParmStruct* parm, Handle* descriptors, ULong pregroup, ULong lineAtATime, ULong final, ULong* classified);	// ROM 0x000d5240 GroupAndClassifyStrokes__FP13PS_point_typesP7rc_typeP17GCGroupParmStructPUlUiN26PUi
long	GCClassifyStrokes(GCWordDescrType* words, PS_point_type* trace, rc_type* rc, GCGroupParmStruct* parm, ULong* classified);	// ROM 0x000d546c GCClassifyStrokes__FP15GCWordDescrTypeP13PS_point_typeP7rc_typeP17GCGroupParmStructPUi
void	WritePrevBaseLineToStrXrDomain(TStrXrDomain* domain, TStrXrUnit* unit);	// ROM 0x00065d48 WritePrevBaseLineToStrXrDomain__FP12TStrXrDomainP10TStrXrUnit
long	SetStrXrFieldType(ULong type, STRXRPARAM* param);			// ROM 0x00220080 SetStrXrFieldType__FUlP10STRXRPARAM - ==> 0, -1 for no kind of field at all
void	GetTraceFromStrXrUnit(TStrXrUnit* unit, PS_point_type** trace, short* nPoints);	// ROM 0x000651cc GetTraceFromStrXrUnit__FP10TStrXrUnitPP13PS_point_typePs - the unit's strokes as one trace (made, the caller frees it)
void	SetUpChains(TXrWordDomain* domain, TUnit* unit);			// ROM 0x0024e650 SetUpChains__FP13TXrWordDomainP5TUnit - the area's dictionary chains given to the word domain
void	AdjustRecParmStruct(TXrWordDomain* domain, rc_type* rc);	// ROM 0x0024e77c AdjustRecParmStruct__FP13TXrWordDomainP7rc_type

#endif	/* __XRDOMAINS_H */
