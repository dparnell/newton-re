/*
	File:		recognition/CursiveReader.h

	Contains:	ParaGraph's cursive reader: a word's trace read into
				words.

				`GCTryToRecognize` is where one word of the writing is
				read: its trace made (`GCWDGetTrace`), the base line the
				segmenter found handed to the engine
				(`GCFillBaseLineParameters` -> `SetRCB`, which works out
				the ink box and where the base line and the middle of the
				letters are, and how sure it is of each), the recogniser's
				data locked (`GCLockRecognitionData`: the letter table,
				the trigrams, the vocabularies and the lexical database),
				then the reading itself in three layers - the digit and
				number reader over "chunks" (`Chunk*`, when the field
				allows numbers), the low level (`low_level`: the trace cut
				into xrs, the elements a letter is matched on) and the xr
				reader (`xrw_algs`: the xrs matched against the letter
				table's prototypes, words out of the vocabularies) - and
				what it read written into the word's descriptor
				(`GCWDWriteRecResults`).

				NOT YET RECONSTRUCTED: the three layers of the reading
				(about 420KB of the ROM, docs/next-steps.md has the plan):
				on the host the low level answers failure, so every word
				is marked 0x200 (the low level failed) and the writing is
				kept as ink.

	Reconstructed from the MP2x00 US ROM (0x000d635c-0x000d6ad4,
	0x000d6e84-0x000d7030, 0x00168a9c-0x00168aec, 0x00168fe8-0x00169058,
	0x0019f2ac-0x0019f644, 0x0019f878-0x0019f918, 0x0021c120-0x0021c16c);
	each function cites its origin.
*/

#ifndef __CURSIVEREADER_H
#define __CURSIVEREADER_H

#ifndef __WORDSEGMENT_H
#include "WordSegment.h"
#endif
#ifndef __XRREADER_H
#include "XrReader.h"
#endif

struct rc_type;
struct RcHandlesType;
struct GCWordDescrType;
struct GCGroupParmStruct;
struct TrigramHeader;

// What SetRCB is given (0x18 bytes in the ROM): flags (1 the segmenter's
// line, 2 the word before's base line, 4 a fixed base line, 8 no new
// line, 0x10 no base line before), the trace and the three base lines.
struct RCB_inpdata_type
{
	UShort			fFlags;			// +00
	short			fCount;			// +02
	PS_point_type*	fTrace;			// +04
	short			fPrev[4];		// +08  the word before's: height, base, how sure of each
	short			fLine[2];		// +10  the segmenter's line: height and middle
	short			fFixed[2];		// +14  (rc +0xf4, +0xf6)
};

// The xrs of a word: how many, room for how many, and the elements
// (0x18 bytes each).
struct xrdata_type
{
	long			fLength;		// +00
	long			fSize;			// +04
	void*			fElements;		// +08
};

// The word graph the xr reader builds is XrReader.h's RWG_type.

void	GCFillLearningHandle(Handle* learning, UShort flags, rc_type* rc, PS_point_type* trace, short points, xrdata_type* xr, struct RWG_type* rwg, struct rec_w_type* readings, short count, void* ortl, ULong ortlSize);	// ROM 0x000d6ad4 GCFillLearningHandle__FPUlUsP7rc_typeP13PS_point_typesP11xrdata_typeP8RWG_typeP10rec_w_typeT5PvUl
long	GCTryToRecognize(PS_point_type* trace, GCWordDescrType* word, rc_type* rc, GCGroupParmStruct* parm);	// ROM 0x000d635c GCTryToRecognize__FP13PS_point_typeP15GCWordDescrTypeP7rc_typeP17GCGroupParmStruct - ==> 0, -6 nothing to read, -7 no memory, -8 the low level failed, -9 the xr reader failed
long	GCLockRecognitionData(rc_type* rc, RcHandlesType* saved);				// ROM 0x000d67f4 GCLockRecognitionData__FPvP13RcHandlesType - ==> 1, 0 for a failure (everything let go again)
void	GCUnlockRecognitionData(rc_type* rc, RcHandlesType* saved);				// ROM 0x000d6914 GCUnlockRecognitionData__FPvP13RcHandlesType
void	GCFreeRwgMem(RWG_type* rwg);											// ROM 0x000d6ad0 GCFreeRwgMem__FP8RWG_type - (nothing)
long	GCFillBaseLineParameters(short lineHeight, short baseLine, short newLine, short prev0, short prev1, short prev2, short prev3, rc_type* rc, PS_point_type* trace);	// ROM 0x000d6e84 GCFillBaseLineParameters__FsN61P7rc_typeP13PS_point_type
// The ink box, and the base line the engine starts from, worked out into
// the stroka_data at rc +0xd8.  ==> 0.
long	SetRCB(RCB_inpdata_type* inp, UByte* stroka);							// ROM 0x0019f2ac SetRCB__FP16RCB_inpdata_typeP11stroka_data
long	GetInkBox(PS_point_type* trace, long n, UByte* rect);					// ROM 0x0019f50c GetInkBox__FP13PS_point_typeiP5_RECT - big-endian left, top, right, bottom; ==> 0, 1 (all nought) for too few points
long	GetAvePos(PS_point_type* trace, long n);								// ROM 0x0019f5e0 GetAvePos__FP13PS_point_typei - the mean y of the points not pen-ups
long	SetMultiWordMarksDash(xrdata_type* xr);							// ROM 0x0019e4a8 SetMultiWordMarksDash__FP11xrdata_type - ==> whether a colon was found between breaks
long	SetMultiWordMarksWS(long limit, xrdata_type* xr, rc_type* rc);		// ROM 0x0019e520 SetMultiWordMarksWS__FiP11xrdata_typeP7rc_type - ==> whether a doubtful gap's break was marked
long	AllocXrdata(xrdata_type* xr, long size);								// ROM 0x0019f878 AllocXrdata__FP11xrdata_typei - ==> 0, 1 for a failure (120 at most)
void	FreeXrdata(xrdata_type* xr);											// ROM 0x0019f8e0 FreeXrdata__FP11xrdata_type
long	LockLexicalDB(void* chain);												// ROM 0x00168a9c LockLexicalDB__FUl
long	UnlockLexicalDB(void* chain);											// ROM 0x00168ac4 UnlockLexicalDB__FUl
long	LockVocabularies(void* vocs);											// ROM 0x00168fe8 LockVocabularies__FPv - ==> 0, 1 for nil
long	UnlockVocabularies(void* vocs);											// ROM 0x00169020 UnlockVocabularies__FPv
long	triads_lock(TrigramHeader* header);										// ROM 0x0021c120 triads_lock__FPv - ==> 0, 1 for a failure
Boolean	TracingCursive(void);													// host: NEWTON_TRACE_CURSIVE is set

#endif	/* __CURSIVEREADER_H */
