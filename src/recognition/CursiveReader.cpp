/*
	File:		recognition/CursiveReader.cpp

	Contains:	ParaGraph's cursive reader (CursiveReader.h).

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "CursiveReader.h"
#include "WordDescriptors.h"
#include "InkGroups.h"
#include "XrDomains.h"
#include "ParaGraph.h"
#include "Dictionaries.h"
#include "LowLevel.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// HOST ONLY: with NEWTON_TRACE_CURSIVE set in the environment, each word
// the cursive reader is given (its strokes and points) and what came of
// it is printed on stderr.
static Boolean
TracingCursive(void)
{
	static int tracing = -1;
	if (tracing < 0)
		tracing = getenv("NEWTON_TRACE_CURSIVE") != nil ? 1 : 0;
	return tracing != 0;
}

static inline short	GetBE(const UByte* p)			{ return (short) ((p[0] << 8) | p[1]); }
static inline void	SetBE(UByte* p, long v)			{ p[0] = (UByte) (v >> 8); p[1] = (UByte) v; }


#pragma mark - reading a word

// ROM 0x000d635c GCTryToRecognize__FP13PS_point_typeP15GCWordDescrTypeP7rc_typeP17GCGroupParmStruct
// One word read.  With rc +0xb4 set only the word's first two points and
// its last are read (and its info cleared).
long
GCTryToRecognize(PS_point_type* trace, GCWordDescrType* word, rc_type* rc, GCGroupParmStruct* parm)
{
	RWG_type rwg;
	memset(&rwg, 0, sizeof(rwg));
	Handle learning = nil;
	void* chunk = nil;
	void* ortl = nil;
	rec_w_type* readings = nil;
	void* split = nil;
	Boolean locked = false;
	xrdata_type xr;
	xr.fElements = nil;
	RcHandlesType saved;
	PS_point_type* wordTrace = nil;
	short nPoints = 0;
	ULong copied = 0;
	PS_point_type three[3];
	long err;
	if (trace != nil && word != nil && rc != nil && word->fRecResults == nil)
	{
		err = GCWDGetTrace(trace, word, parm->fStrokes, &wordTrace, &nPoints, &copied);
		if (err != 0)
			goto done;
		if (wordTrace != nil && 2 < nPoints)
		{
			rc->fWordInfo = &word->fInfo;
			short n;
			PS_point_type* points;
			if (RCGetH(rc, 0xb4) == 0)
			{
				n = nPoints;
				points = wordTrace;
			}
			else
			{
				three[0] = wordTrace[0];
				three[1] = wordTrace[1];
				three[2] = wordTrace[nPoints - 1];
				n = 3;
				memset(&word->fInfo, 0, sizeof(word->fInfo));
				points = three;
			}
			if (AllocXrdata(&xr, 0x78) == 0
			 && (readings = (rec_w_type*) HWRMemoryAlloc(10 * sizeof(rec_w_type))) != nil)
			{
				memset(readings, 0, 10 * sizeof(rec_w_type));
				RCSetH(rc, 0x96, n);
				rc->fTrace = points;
				err = GCWDFillBaseLineParameters(word, rc, points);
				if (err != 0)
					goto done;
				if (GCLockRecognitionData(rc, &saved) != 0)
				{
					locked = true;
					// NOT YET RECONSTRUCTED: with rc +0xb6 set (the field
					// allows numbers) the digit and number reader goes
					// first - ChunkAllocCtx, ChunkProcessor, and when it
					// found numbers ChunkModifyRC (0x002a5620-0x002a70c0);
					// without it the chunk stays nil, as it does in a
					// field that has no numbers
					if (low_level(points, &xr, rc) != 0)
					{
						err = -8;
						goto done;
					}
					if (TracingCursive())
					{
						fprintf(stderr, "[cursive] low_level: %ld xrs:", xr.fLength);
						xrd_el_type* e = (xrd_el_type*) xr.fElements;
						for (long i = 0; i < xr.fLength; i++)
							fprintf(stderr, " %02x/%d", e[i].type, e[i].height);
						fprintf(stderr, "\n");
					}
					// NOT YET RECONSTRUCTED: the rest - ChunkWriteParamCtx,
					// ChunkPatchXrdata, and for more than two xrs
					// SetMultiWordMarksWS/Dash and xrw_algs (0x00362f08,
					// the xrs read into words: -9 when it fails),
					// EvaluateAndSortAnswers,
					// MakeAndCombRecWordsFromWordGraph, ORCreateLearnInfo;
					// then ChunkRestoreRC, ChunkSortAnswers,
					// ChunkCorrectByLexDB, FillRecwordSplitInfo and
					// GCFillLearningHandle.  The host stops here with
					// xrw_algs' failure.
					err = -9;
					goto done;
				}
			}
			err = -7;
			goto done;
		}
	}
	err = -6;
done:
	if (TracingCursive() && word != nil)
	{
		int extras = 0;
		while (extras < 8 && word->fExtra[extras] != 0)
			extras++;
		fprintf(stderr, "[cursive] word of strokes %d-%d (+%d more), %d points: %ld\n",
				word->fFirst, word->fLast, extras, nPoints, err);
	}
	rc->fWordInfo = nil;
	GCWDWriteRecResults(word, rc, readings, learning, err, split, 0);
	if (split != nil)
		HWRMemoryFree((Ptr) split);
	// NOT YET RECONSTRUCTED: ORLArrayDelete(&ortl), ChunkRestoreRC(chunk,
	// rc) and ChunkCleanUp(&chunk) - with nothing made (ortl and chunk nil)
	// there is nothing for them to do
	(void) ortl;
	(void) chunk;
	GCFreeRwgMem(&rwg);
	if (err != 0 && learning != nil)
	{
		HWRMemoryFreeHandle(learning);
		learning = nil;
	}
	if (locked)
		GCUnlockRecognitionData(rc, &saved);
	if (readings != nil)
		HWRMemoryFree((Ptr) readings);
	FreeXrdata(&xr);
	if (wordTrace != nil && copied != 0)
		HWRMemoryFree((Ptr) wordTrace);
	return err;
}


// ROM 0x000d67f4 GCLockRecognitionData__FPvP13RcHandlesType
// The letter table and the orthographic database, the trigrams, the
// vocabularies and the lexical database locked, their handles kept.
long
GCLockRecognitionData(rc_type* rc, RcHandlesType* saved)
{
	long failures = 0;
	if (rc == nil || saved == nil || GCLockDTEAndLearningData(rc, saved) == 0)
		return 0;
	saved->fTrigrams = rc->fTrigrams;
	for (short i = 0; i < 15; i++)
		saved->fVocs[i] = rc->fVocs[i];
	if (rc->fTrigrams != nil)
	{
		TrigramHeader* header = (TrigramHeader*) HWRMemoryLockHandle(rc->fTrigrams);
		rc->fTrigrams = (Handle) header;
		if (header == nil || triads_lock(header) != 0)
			failures = 1;
	}
	for (short i = 0; i < 15; i++)
	{
		if (rc->fVocs[i] != nil)
		{
			Ptr vocs = HWRMemoryLockHandle(rc->fVocs[i]);
			rc->fVocs[i] = (Handle) vocs;
			if (vocs == nil || LockVocabularies(vocs) != 0)
				failures++;
		}
	}
	if (rc->fChain != nil && LockLexicalDB(rc->fChain) == 0)
		failures++;
	if (failures != 0)
	{
		GCUnlockRecognitionData(rc, saved);
		return 0;
	}
	return 1;
}


// ROM 0x000d6914 GCUnlockRecognitionData__FPvP13RcHandlesType
void
GCUnlockRecognitionData(rc_type* rc, RcHandlesType* saved)
{
	if (rc == nil || saved == nil)
		return;
	GCUnlockDTEAndLearningData(rc, saved);
	UnlockLexicalDB(rc->fChain);
	if (rc->fTrigrams != nil)
		triads_unlock((TrigramHeader*) rc->fTrigrams);
	for (short i = 0; i < 15; i++)
		if (rc->fVocs[i] != nil)
			UnlockVocabularies(rc->fVocs[i]);
	rc->fTrigrams = saved->fTrigrams;
	for (short i = 0; i < 15; i++)
		rc->fVocs[i] = saved->fVocs[i];
	if (rc->fTrigrams != nil)
		HWRMemoryUnlockHandle(rc->fTrigrams);
	for (short i = 0; i < 15; i++)
		if (rc->fVocs[i] != nil)
			HWRMemoryUnlockHandle(rc->fVocs[i]);
}


// ROM 0x000d6ad0 GCFreeRwgMem__FP8RWG_type
void
GCFreeRwgMem(RWG_type* rwg)
{
}


// ROM 0x000d6e84 GCFillBaseLineParameters__FsN61P7rc_typeP13PS_point_type
// What is known of the word's base line: the word before's (when its
// height is positive), the segmenter's line (when its height is; with
// no new line flag 8 is cleared) and, with rc +0x90 bit 0, the fixed one
// the field asks for.  ==> 0, -1.
long
GCFillBaseLineParameters(short lineHeight, short baseLine, short newLine, short prev0, short prev1, short prev2, short prev3, rc_type* rc, PS_point_type* trace)
{
	if (trace == nil || rc == nil)
		return -1;
	RCB_inpdata_type inp;
	memset(&inp, 0, sizeof(inp));		// (the ROM leaves the unused halfwords as they were on the stack)
	inp.fTrace = trace;
	inp.fCount = (short) RCGetH(rc, 0x96);
	inp.fFlags = 0x18;
	if (0 < prev0)
	{
		inp.fPrev[0] = prev0;
		inp.fPrev[1] = prev1;
		inp.fPrev[2] = prev2;
		inp.fPrev[3] = prev3;
		inp.fFlags |= 2;
		inp.fFlags &= ~0x10;
	}
	if (0 < lineHeight)
	{
		inp.fLine[0] = lineHeight;
		inp.fLine[1] = baseLine;
		inp.fFlags |= 1;
		if (newLine == 0)
			inp.fFlags &= ~8;
	}
	if ((RCGetH(rc, 0x90) & 1) != 0)
	{
		inp.fFixed[0] = (short) RCGetH(rc, 0xf4);
		inp.fFixed[1] = (short) RCGetH(rc, 0xf6);
		inp.fFlags |= 4;
	}
	if (SetRCB(&inp, RCByte(rc, 0xd8)) == 0)
		return 0;
	return -1;
}


// ROM 0x0019f2ac SetRCB__FP16RCB_inpdata_typeP11stroka_data
// The stroka_data: the ink box (+0), then at +0xa the height and middle
// of the letters the engine is to assume and how sure (out of 100) it
// is of each - from the segmenter's line (50 each, the middle only when
// no new line), else the word before's (its middle dropped when it is
// further from this word's mean y than its height), and a fixed base
// line overriding both, pulled up inside the ink when the ink stops
// short of it.
long
SetRCB(RCB_inpdata_type* inp, UByte* stroka)
{
	long height = 0, middle = 0, sureHeight = 0, sureMiddle = 0;
	memset(stroka + 0xa, 0, 8);
	GetInkBox(inp->fTrace, inp->fCount, stroka);
	UShort flags = inp->fFlags;
	if ((flags & 1) != 0)
	{
		height = inp->fLine[0];
		middle = inp->fLine[1];
		sureHeight = 0x32;
		if ((flags & 8) == 0)
			sureMiddle = 0x32;
	}
	if ((flags & 2) != 0 && (flags & 0x10) == 0)
	{
		height = inp->fPrev[0];
		sureHeight = inp->fPrev[2];
		if ((flags & 1) == 0)
		{
			middle = inp->fPrev[1];
			sureMiddle = inp->fPrev[3];
			long ave = GetAvePos(inp->fTrace, inp->fCount);
			if (height < HWRAbs(ave - (middle - height / 2)))
				sureMiddle = 0;
		}
		else if ((flags & 8) == 0)
		{
			middle = inp->fPrev[1];
			sureMiddle = inp->fPrev[3];
		}
	}
	if ((inp->fFlags & 4) != 0)
	{
		height = inp->fFixed[0];
		middle = inp->fFixed[1];
		sureHeight = 100;
		sureMiddle = 100;
		long bottom = GetBE(stroka + 6);
		if (0 < bottom)
		{
			long top = GetBE(stroka + 2);
			long half = height / 2;
			if (half < bottom - top && bottom < middle)
			{
				long d = middle - bottom;
				if (half < middle - bottom)
					d = half;
				middle -= d;
				if (middle - height < top)
					height = height - (top - (middle - height));
			}
		}
	}
	SetBE(stroka + 0xa, height);
	SetBE(stroka + 0xc, middle);
	SetBE(stroka + 0xe, sureHeight);
	SetBE(stroka + 0x10, sureMiddle);
	return 0;
}


// ROM 0x0019f50c GetInkBox__FP13PS_point_typeiP5_RECT
long
GetInkBox(PS_point_type* trace, long n, UByte* rect)
{
	if (trace != nil && 2 < n)
	{
		long left = 0x7d00, top = 0x7d00, right = 0, bottom = 0;
		for (long i = 0; i < n; i++, trace++)
		{
			long y = trace->y;
			if (-1 < y)
			{
				long x = trace->x;
				if (x < left)
					left = x;
				if (right < x)
					right = x;
				if (y < top)
					top = y;
				if (bottom < y)
					bottom = y;
			}
		}
		SetBE(rect + 0, left);
		SetBE(rect + 2, top);
		SetBE(rect + 4, right);
		SetBE(rect + 6, bottom);
		return 0;
	}
	memset(rect, 0, 8);
	return 1;
}


// ROM 0x0019f5e0 GetAvePos__FP13PS_point_typei
long
GetAvePos(PS_point_type* trace, long n)
{
	if (trace != nil && 2 < n)
	{
		long sum = 0, count = 0;
		for (long i = 0; i < n; i++)
		{
			if (-1 < trace[i].y)
			{
				sum += trace[i].y;
				count++;
			}
		}
		if (count != 0)
			return sum / count;
	}
	return 0;
}


// ROM 0x0019f878 AllocXrdata__FP11xrdata_typei
long
AllocXrdata(xrdata_type* xr, long size)
{
	if (xr != nil && size != 0 && size < 0x79)
	{
		xr->fElements = HWRMemoryAlloc(size * 0x18);
		if (xr->fElements != nil)
		{
			xr->fSize = size;
			xr->fLength = 0;
			memset(xr->fElements, 0, size * 0x18);
			return 0;
		}
	}
	return 1;
}


// ROM 0x0019f8e0 FreeXrdata__FP11xrdata_type
void
FreeXrdata(xrdata_type* xr)
{
	if (xr != nil && xr->fElements != nil)
	{
		HWRMemoryFree((Ptr) xr->fElements);
		xr->fElements = nil;
		xr->fLength = 0;
		xr->fSize = 0;
	}
}


#pragma mark - the vocabularies

// ROM 0x00168a9c LockLexicalDB__FUl
long
LockLexicalDB(void* chain)
{
	if (chain != nil)
	{
		((TDictChain*) chain)->LockChain();
		return 1;
	}
	return 0;
}


// ROM 0x00168ac4 UnlockLexicalDB__FUl
long
UnlockLexicalDB(void* chain)
{
	if (chain != nil)
	{
		((TDictChain*) chain)->UnlockChain();
		return 1;
	}
	return 0;
}


// ROM 0x00168fe8 LockVocabularies__FPv
// The first dictionary chain of each of the two lists locked.
long
LockVocabularies(void* vocs)
{
	if (vocs == nil)
		return 1;
	VocAdders* adders = (VocAdders*) vocs;
	if (adders->fMain[0] != nil)
		((TDictChain*) adders->fMain[0])->LockChain();
	if (adders->fAux[0] != nil)
		((TDictChain*) adders->fAux[0])->LockChain();
	return 0;
}


// ROM 0x00169020 UnlockVocabularies__FPv
long
UnlockVocabularies(void* vocs)
{
	if (vocs == nil)
		return 0;
	VocAdders* adders = (VocAdders*) vocs;
	if (adders->fMain[0] != nil)
		((TDictChain*) adders->fMain[0])->UnlockChain();
	if (adders->fAux[0] != nil)
		((TDictChain*) adders->fAux[0])->UnlockChain();
	return 1;
}


// ROM 0x0021c120 triads_lock__FPv
// A trigram table replaced from RAM locked.
long
triads_lock(TrigramHeader* header)
{
	if (header == nil)
		return 1;
	if (header->fTrigram == nil && header->fRAMTrigram != 0)
	{
		header->fTrigram = (UByte*) LockRamParaData(header->fRAMTrigram);
		if (header->fTrigram == nil)
			return 1;
	}
	return 0;
}
