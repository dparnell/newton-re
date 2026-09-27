/*
	File:		recognition/InkGroups.cpp

	Contains:	Strokes nobody read grouped into words of ink
				(InkGroups.h).

	Reconstructed from the MP2x00 US ROM (0x000ea554-0x000eb360,
	0x000d55c4-0x000d5cc0, 0x000d83a0-0x000d84f8, 0x0006583c-0x00065b2c);
	each function cites its origin.
*/

#include <stdio.h>
#include "InkGroups.h"
#include "ParaGraph.h"
#include "Stroke.h"
#include "StrokeQueue.h"		// gTabScale
#include "Unit.h"
#include "Controller.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "Locale.h"				// GetPreference
#include "FixedMath.h"
#include <string.h>
#include <stddef.h>

static inline Boolean	BitIsSet(const UByte* bits, ULong i)	{ return ((bits[(i & 0xff) >> 3] >> (7 - (i & 7))) & 1) != 0; }
static inline void		SetBit(UByte* bits, ULong i)			{ bits[(i & 0xff) >> 3] |= (UByte) (1 << (7 - (i & 7))); }
static inline void		ClearBit(UByte* bits, ULong i)			{ bits[(i & 0xff) >> 3] &= (UByte) ~(1 << (7 - (i & 7))); }

// DEVIATION: the group's size from sizeof (the ROM's 0x34-byte header
// and four-byte stroke pointers)
static inline ULong		GroupSize(ULong capacity)				{ return offsetof(GroupDataStruct, fUnits) + capacity * sizeof(TStrokeUnit*); }


#pragma mark - the trace

// ROM 0x0006583c GetTraceFromStrokes__FPP7TStrokePP13PS_point_typePsT3
void
GetTraceFromStrokes(TStroke** strokes, PS_point_type** trace, short* nStrokes, short* nPoints)
{
	*trace = nil;
	NewGetTraceFromStrokes(strokes, trace, nStrokes, nPoints);
}


// ROM 0x00065848 NewGetTraceFromStrokes__FPP7TStrokePP13PS_point_typePsT3
// The strokes' points into a trace (made when *trace is nil), each
// stroke after a pen-up: in eighths of a pixel (the tablet's scale),
// then moved so that nothing is left of or above nought.  ROM QUIRK: a
// point whose x is nought is not moved.
void
NewGetTraceFromStrokes(TStroke** strokes, PS_point_type** trace, short* nStrokes, short* nPoints)
{
	*nPoints = 0;
	*nStrokes = 0;
	PS_point_type* points = *trace;
	if (points == nil)
	{
		if (strokes[0] == nil)
			return;
		long total = 0, last = 0;
		for (long k = 0; strokes[k] != nil; k++)
		{
			last = strokes[k]->Count() + total;
			total = last + 1;
		}
		if (total == 0)
			return;
		points = (PS_point_type*) HWRMemoryAlloc((last + 2) * sizeof(PS_point_type));
		if (points == nil)
		{
			*trace = nil;
			return;
		}
		*trace = points;
	}
	long minX = 0, minY = 0;
	points[0].x = 0;
	points[0].y = -1;
	long k = 0;
	long at = 1;
	for (TStroke* stroke = strokes[0]; stroke != nil; stroke = strokes[k])
	{
		long count = stroke->Count();
		*nPoints = (short) (*nPoints + count + 1);
		SamplePt* sample = stroke->GetPoint(0);
		for (long j = 0; j < count; j++, sample++)
		{
			short x;
			Fixed y;
			if (gTabScale.x == 0x80000)
			{
				x = (short) ((ULong) (uint32_t) (SampleX(sample) * 8 + 0x8000) >> 16);
				y = SampleY(sample) * 8;
			}
			else
			{
				x = (short) ((ULong) (uint32_t) (FixedMultiply(SampleX(sample), gTabScale.x) + 0x8000) >> 16);
				y = FixedMultiply(SampleY(sample), gTabScale.y);
			}
			y += 0x8000;
			short sy = (short) ((uint32_t) y >> 16);
			if (x < minX)
				minX = x;
			if (sy < minY)
				minY = sy;
			points[at].x = x;
			points[at].y = sy;
			at++;
		}
		points[at].x = 0;
		points[at].y = -1;
		k++;
		at++;
	}
	*nStrokes = (short) k;
	if (k != 0)
		*nPoints = (short) (*nPoints + 1);
	if (minX != 0 || minY != 0)
	{
		for (long j = 0; j < *nPoints; j++)
		{
			if (points[j].x != 0 && points[j].y != -1)
			{
				points[j].x = (short) (points[j].x - minX);
				points[j].y = (short) (points[j].y - minY);
			}
		}
	}
}


#pragma mark - the GC layer

// ROM 0x000d55c4 GCGroupStrokes__FP15GCWordDescrTypeP13PS_point_typesUiT4P17GCGroupParmStruct
// The strokes the parameters list (those after the ones already given)
// given to the word segmenter one at a time, out of the trace; with
// `final` and none to give, the segmenter told the writing is over.
// ==> 0, -1 for nothing to do, -7 for no memory.
long
GCGroupStrokes(GCWordDescrType* words, PS_point_type* trace, short nPoints, ULong final, ULong param5, GCGroupParmStruct* parm)
{
	Boolean all = false;
	ws_control_type* control = nil;
	ws_results_type* results = nil;
	long first, last;
	long err;
	if (trace == nil || nPoints < 3 || parm == nil)
		return -1;
	if (words != nil)
	{
		// NOT YET RECONSTRUCTED: the cursive recogniser's word
		// descriptors (GCWriteNewGroupResults and the rest)
		return -1;
	}
	parm->fInProgress = 0;
	{
		ULong from = (ULong) parm->fNumStrokes;
		ULong end = from;
		while ((long) end < 0x100 && BitIsSet(parm->fStrokes, end))
			end++;
		if (from == end)
		{
			if (final == 0)
				return -1;
			first = -1;
			last = -2;
		}
		else
		{
			first = GCGetRealStrokeIndex(parm->fStrokes, (short) parm->fNumStrokes);
			last = GCGetRealStrokeIndex(parm->fStrokes, (short) (end - 1));
			if (first < 0 || last < 0)
				return -1;
			if (0xf9 < (long) end)
				all = true;
		}
		err = GCResizeAndLockGResHandle(&parm->fGRes, &control, &results, end + 1);
	}
	if (err == 0)
	{
		control->fNumPoints = 0;
		control->fFlags = 0;
		control->fWordDistLevel = parm->fSpacing + 1;
		control->fLineDist = 0;
		control->fDefLineHeight = 0x50;
		control->fMode = 3;
		control->fSureLevel = parm->fSureLevel;
		if (final == 0 || -1 < first)
		{
			for ( ; first <= last; first++)
			{
				PS_point_type* stroke;
				short count;
				GCGetStrokeFromTrace(trace, nPoints, (short) first, &stroke, &count);
				control->fNumPoints = count - 2;
				if (first == last)
				{
					if (final != 0 || all)
						control->fFlags |= 1;
					if (param5 != 0)
						control->fFlags |= 2;
				}
				parm->fNumStrokes++;
				if (WordStrokes(stroke + 1, control, results) != 0)
				{
					err = -7;
					goto done;
				}
			}
			if (final == 0 && parm->fNumStrokes == 0xfa)
				GCTryToRemoveLastWords(results, parm->fStrokes, words, 0xf9, &parm->fNext);
		}
		else
		{
			// the end of the writing
			control->fNumPoints = 0;
			control->fFlags |= 3;
			PS_point_type none = { 0, 0 };
			if (WordStrokes(&none, control, results) != 0)
			{
				err = -7;
				goto done;
			}
		}
		err = 0;
	}
done:
	parm->fInProgress = (err == -4);
	if (control != nil)
		GCUnlockGResHandle(parm->fGRes);
	return err;
}


// ROM 0x000d5994 GCResizeAndLockGResHandle__FPUlPPvT2Ul
// The segmenter's block locked, made (or made bigger, ten words at a
// time, the old one copied and freed) to hold `words` words; the control
// and results records answered.  ==> 0, -6 for no block and none asked
// for, -7 for no memory (ROM QUIRK: the old block is left locked).
long
GCResizeAndLockGResHandle(Handle* gres, ws_control_type** control, ws_results_type** results, ULong words)
{
	ULong old = 0;
	GResBlock* block = nil;
	if (control != nil)
		*control = nil;
	if (results != nil)
		*results = nil;
	if (gres == nil)
		return -6;
	if (words == 0 && *gres == nil)
		return -6;
	if (*gres != nil)
	{
		block = (GResBlock*) HWRMemoryLockHandle(*gres);
		if (block == nil)
			return -7;
		old = block->fCapacity;
	}
	if (old < words)
	{
		ULong capacity = (words / 10) * 10 + 10;
		// DEVIATION: sized from sizeof (the ROM's header is 0x224 bytes)
		ULong size = offsetof(GResBlock, fWords) + capacity * sizeof(ws_word_type);
		Handle h = HWRMemoryAllocHandle(size);
		GResBlock* bigger;
		if (h == nil || (bigger = (GResBlock*) HWRMemoryLockHandle(h)) == nil)
			return -7;
		ULong clear = size;
		if (old != 0)
		{
			memcpy(bigger, block, size - (capacity - old) * sizeof(ws_word_type));
			HWRMemoryUnlockHandle(*gres);
			HWRMemoryFreeHandle(*gres);
			clear = (capacity - old) * sizeof(ws_word_type);
		}
		memset((char*) bigger + (size - clear), 0, clear);
		*gres = h;
		bigger->fCapacity = capacity;
		block = bigger;
	}
	if (control != nil)
		*control = &block->fControl;
	if (results != nil)
	{
		*results = &block->fResults;
		block->fResults.fWords = block->fWords;
	}
	return 0;
}


// ROM 0x000d5af0 GCUnlockGResHandle__FUl
long
GCUnlockGResHandle(Handle gres)
{
	if (gres != nil)
		HWRMemoryUnlockHandle(gres);
	return 0;
}


// ROM 0x000d5b0c GCDisposeGResHandle__FPUl
// The segmenter's block freed (the segmenter told to throw its state
// away first).
long
GCDisposeGResHandle(Handle* gres)
{
	if (gres == nil)
		return -6;
	if (*gres == nil)
		return 0;
	ws_control_type* control;
	long err = GCResizeAndLockGResHandle(gres, &control, nil, 0);
	if (err == 0)
	{
		if (control->fMem != nil)
		{
			control->fNumPoints = 0;
			control->fFlags = 0x80;
			WordStrokes(nil, control, nil);
		}
		HWRMemoryUnlockHandle(*gres);
		HWRMemoryFreeHandle(*gres);
		*gres = nil;
	}
	return err;
}


// ROM 0x000d5cc0 GCTryToRemoveLastWords__FP15ws_results_typePUcP15GCWordDescrTypesPs
// With 250 strokes segmented, the last words taken back so that their
// strokes are segmented again with what follows.  ==> 0, or -1 for
// nothing to do - which is always so without word descriptors, the way
// the ink grouping calls it.
long
GCTryToRemoveLastWords(ws_results_type* results, UByte* strokes, GCWordDescrType* words, short last, short* next)
{
	if (results == nil || words == nil || next == nil || strokes == nil)
		return -1;
	// NOT YET RECONSTRUCTED: the rest, which needs the cursive
	// recogniser's word descriptors
	return -1;
}


// ROM 0x000d83a0 GCGetRealStrokeIndex__FPUcs
// How many strokes the list has before the index; -1 for no list or an
// index outside 0-255.
long
GCGetRealStrokeIndex(UByte* strokes, short index)
{
	if (strokes != nil && -1 < index && index < 0x100)
	{
		short n = 0;
		for (long i = 0; i < index; i++)
			if (BitIsSet(strokes, i))
				n = (short) (n + 1);
		return n;
	}
	return -1;
}


// ROM 0x000d8418 GCGetNumOfStrokesInList__FPUc
long
GCGetNumOfStrokesInList(UByte* strokes)
{
	long n = GCGetRealStrokeIndex(strokes, 0xff);
	long lastOne = (strokes[0x1f] & 1) != 0 ? 1 : 0;
	return (short) (lastOne + n);
}


// ROM 0x000d844c GCGetStrokeFromTrace__FP13PS_point_typesT2PP13PS_point_typePs
// The index'th stroke of a trace: the pen-up before it, and how many
// points from there to the pen-up after it (both counted).
void
GCGetStrokeFromTrace(PS_point_type* trace, short nPoints, short index, PS_point_type** stroke, short* count)
{
	if (count != nil)
		*count = 0;
	if (stroke != nil)
		*stroke = nil;
	if (trace == nil || nPoints < 0)
		return;
	long k = 0, start = 0, i = 1;
	for ( ; i < nPoints; i++)
	{
		if (trace[i].y < 0)
		{
			if (k == index)
				break;
			k++;
			start = i;
		}
	}
	if (count != nil)
		*count = (short) ((i - start) + 1);
	if (stroke != nil)
		*stroke = trace + start;
}


#pragma mark - the IG layer

// ROM 0x000ea554 IGGroupAndCompressStrokes__FUlP11TStrokeUnitT1UcPPPc
// A stroke added to the group of expired strokes (or, with none, the
// group looked at again), the strokes still to be placed given to the
// segmenter, and the words it has settled compressed into ink
// (IGCompressStrokes); the group then made again of what is left.  With
// `final` everything is settled, and the group thrown away after.
// ==> whether any strokes were compressed.
ULong
IGGroupAndCompressStrokes(ULong strokeWorld, TStrokeUnit* unit, ULong spacing, UChar final, Handle* group)
{
	Boolean failed = false;
	long lineMode = 4;
	PS_point_type* trace = nil;
	Handle newGroup = nil;
	GroupDataStruct* newData = nil;
	GroupDataStruct* data;
	ULong compressed = 0;
	long err;
	ULong count;
	long base, start;
	Boolean all;
	GCGroupParmStruct parm;
	if (GetPreference(RSSYMlineatatime) == NILREF)
		lineMode = 0;
	if (*group == nil)
	{
		IGNewGroupData(group);
		if (*group == nil)
		{
			err = -1;
			goto disposed;
		}
	}
	data = IGLockGroupData(*group);
	count = IGGetNumOfSrokes(data);
	if (unit != nil)
	{
		err = IGAddSroke(group, &data, unit);
		if (err != 0)
			goto unlock;
		if (GCGetRealStrokeIndex(data->fStrokes, 0xff) != 0 || (data->fStrokes[0x1f] & 1) != 0)
			count = data->fMarked;
		count++;
		for (ULong i = data->fMarked; i < count; i++)
			SetBit(data->fStrokes, i);
		data->fMarked = count;
	}
	if (count < 1)
	{
		err = -2;
		goto unlock;
	}
	memset(&parm, 0, sizeof(parm));
	parm.fGRes = data->fGRes;
	parm.fNumStrokes = (short) data->fNumGrouped;
	memcpy(parm.fStrokes, data->fStrokes, 0x20);
	base = 0;
	start = data->fMarked;
	if (GCGetRealStrokeIndex(data->fStrokes, 0xff) != 0 || (data->fStrokes[0x1f] & 1) != 0)
		count = start;
	for ( ; ; )
	{
		parm.fLineAtATime = (short) lineMode;
		parm.fSpacing = (short) spacing;
		parm.fField03 = 1;
		parm.fField02 = 0;
		if ((ULong) (count + (long) parm.fNumStrokes - start) > 0xfa)
		{
			// more than the segmenter holds: those that fit
			all = true;
			for (ULong i = start - base; i < 0xfa; i++)
				SetBit(parm.fStrokes, i);
		}
		else
		{
			all = false;
			for ( ; (ULong) start < count; start++)
				SetBit(parm.fStrokes, start - base);
			start = count;
		}
		short nPoints;
		IGAllocTrace(data, parm.fStrokes, (short) base, &trace, &nPoints);
		if (trace == nil)
		{
			failed = true;
			break;
		}
		if (GCGroupStrokes(nil, trace, nPoints, (final && !all) ? 1 : 0, 0, &parm) != 0)
			failed = true;
		HWRMemoryFree((Ptr) trace);
		trace = nil;
		if (parm.fGRes == nil
		|| IGCompressStrokes(strokeWorld, *group, &data, &parm, (short) base, (final && !all) ? 1 : 0, &compressed) == -1)
		{
			failed = true;
			break;
		}
		if (failed)
			break;
		if (!all && !final && parm.fNumStrokes != 0xfa)
			break;
		base = (short) (parm.fNumStrokes + base);
		if (all || parm.fNumStrokes == 0xfa)
			start = base;
		GCDisposeGResHandle(&parm.fGRes);
		memset(&parm, 0, sizeof(parm));
		if (!all)
			break;
	}
	data->fGRes = parm.fGRes;
	if (compressed == 0)
	{
		if (unit != nil)
		{
			data->fGRes = parm.fGRes;
			data->fNumGrouped = parm.fNumStrokes;
			data->fMarked = start - base;
			memcpy(data->fStrokes, parm.fStrokes, 0x20);
		}
		else if (final)
		{
			err = -1;
			goto unlock;
		}
	}
	else
	{
		if (!(GCGetRealStrokeIndex(parm.fStrokes, 0xff) == 0 && (parm.fStrokes[0x1f] & 1) == 0 && (ULong) start == count))
		{
			// the strokes not yet compressed into a group of their own
			IGNewGroupData(&newGroup);
			if (newGroup == nil)
			{
				err = -1;
				goto unlock;
			}
			newData = IGLockGroupData(newGroup);
			ULong numOld = IGGetNumOfSrokes(data);
			ULong left = count - base;
			for (ULong i = 0; i < 0x100; i++)
			{
				if ((ULong) start == count)
				{
					if (!BitIsSet(parm.fStrokes, i))
						continue;
				}
				else if (left <= i)
					break;
				ULong index = IGGetRealStrokeIndex(data, i + base);
				if (numOld <= index)
					break;
				err = IGAddSroke(&newGroup, &newData, data->fUnits[index]);
				if (err != 0)
					goto unlock;
			}
			newData->fGRes = parm.fGRes;
			newData->fNumGrouped = parm.fNumStrokes;
			if ((ULong) start == count)
			{
				newData->fMarked = start - base;
				memcpy(newData->fStrokes, parm.fStrokes, 0x20);
			}
			else
			{
				ULong n = IGGetNumOfSrokes(newData);
				newData->fMarked = n;
				for (ULong i = 0; i < newData->fMarked; i++)
					SetBit(newData->fStrokes, i);
			}
			data->fGRes = nil;
		}
		IGUnlockGroupData(*group);
		IGDisposeGroupData(group);
		*group = newGroup;
		data = newData;
		newGroup = nil;
		newData = nil;
	}
	err = failed ? -1 : 0;
unlock:
	if (*group != nil && data != nil)
		IGUnlockGroupData(*group);
disposed:
	if (newGroup != nil)
	{
		if (newData != nil)
			IGUnlockGroupData(newGroup);
		IGDisposeGroupData(&newGroup);
	}
	if (final && *group != nil)
		IGDisposeGroupData(group);
	if (err == -1)
		gController->SignalMemoryError();
	return compressed & 0xff;
}


// ROM 0x000eabec IGCompressStrokes__FUlPPcPP15GroupDataStructP17GCGroupParmStructsUiPUi
// The settled words' strokes taken out of the group and handed to the
// stroke world as groups of ink (IGCompressGroup), at most forty strokes
// to a group: a word is settled when the segmenter has closed its line,
// when more words than the 'lineAtATime preference asks for are waiting,
// or at the end.  ROM QUIRKS: the count of words waiting goes down by
// one for each stroke rather than each word, and a word whose last
// stroke is not one still to be placed never hands the strokes gathered
// before it over.  ==> 0, -1 for no memory, -2 for nothing to do.
long
IGCompressStrokes(ULong strokeWorld, Handle group, GroupDataStruct** dataP, GCGroupParmStruct* parm, short base, ULong final, ULong* compressed)
{
	TStrokeUnit* local[20];
	TStrokeUnit** units = local;
	ws_results_type* results = nil;
	GroupDataStruct* data = *dataP;
	long err;
	*compressed = 0;
	if (group == nil || parm == nil)
		return -2;
	if (GCResizeAndLockGResHandle(&parm->fGRes, nil, &results, 0) != 0)
		return -1;
	if (data == nil)
		data = IGLockGroupData(group);
	{
		ws_word_type* words = results->fWords;
		ULong bufSize = IGGetCompressBufSize(strokeWorld);
		if (bufSize < 2)
			err = -1;
		else
		{
			Boolean seen = false;
			long waiting = 0;
			for (ULong w = 0; w < results->fNumWords; w++)
			{
				if (words[w].fWord == 0)
				{
					if (seen)
						break;
				}
				else
				{
					seen = true;
					if ((words[w].fFlags & 4) == 0)
						waiting++;
				}
			}
			seen = false;
			err = 0;
			for (ULong w = 0; w < results->fNumWords; w++)
			{
				ws_word_type* word = &words[w];
				if (word->fWord == 0)
				{
					if (seen)
						break;
					continue;
				}
				seen = true;
				UByte flags = word->fFlags;
				if ((flags & 4) != 0 || ((flags & 1) == 0 && parm->fLineAtATime >= waiting && final == 0))
					continue;
				long mine = 0;
				for (ULong j = 0; j < word->fCount; j++)
					if (BitIsSet(parm->fStrokes, results->fStrokes[word->fFirst + j]))
						mine++;
				if (mine == 0)
					continue;
				ULong size = mine + 1;
				if (bufSize <= (ULong) (mine + 1))
					size = bufSize;
				if (0x14 < size && (units = (TStrokeUnit**) HWRMemoryAlloc(size * sizeof(TStrokeUnit*))) == nil)
				{
					err = -1;
					goto done;
				}
				ULong n = 0;
				for (ULong j = 0; j < word->fCount; j++)
				{
					word->fFlags |= 4;
					waiting--;
					UByte s = results->fStrokes[word->fFirst + j];
					if (BitIsSet(parm->fStrokes, s))
					{
						ClearBit(parm->fStrokes, s);
						ULong index = IGGetRealStrokeIndex(data, s + base);
						units[n++] = data->fUnits[index];
						data->fUnits[index] = nil;
						if (size - 1 <= n || word->fCount - 1 == j)
						{
							units[n] = nil;
							IGUnlockGroupData(group);
							IGCompressGroup(strokeWorld, units);
							data = IGLockGroupData(group);
							n = 0;
							*compressed = 1;
						}
					}
				}
				if (units != local)
				{
					HWRMemoryFree((Ptr) units);
					units = local;
				}
			}
		}
	}
done:
	if (results != nil)
		GCUnlockGResHandle(parm->fGRes);
	if (*dataP == nil)
		IGUnlockGroupData(group);
	else
		*dataP = data;
	if (units != local && units != nil)
		HWRMemoryFree((Ptr) units);
	return err;
}


// ROM 0x000eafd4 IGAllocTrace__FP15GroupDataStructPUcsPP13PS_point_typePs
// The listed strokes of the group as a trace.
void
IGAllocTrace(GroupDataStruct* data, UByte* strokes, short base, PS_point_type** trace, short* nPoints)
{
	TStroke* local[20];
	TStroke** list = local;
	if (trace != nil && nPoints != nil)
	{
		*trace = nil;
		*nPoints = 0;
		if (data != nil && strokes != nil)
		{
			ULong count = IGGetNumOfSrokes(data);
			ULong n = 0;
			for (ULong i = 0; i < 0x100; i++)
				if (BitIsSet(strokes, i))
					n++;
			if (n != 0 && (n < 0x14 || (list = (TStroke**) HWRMemoryAlloc(n * sizeof(TStroke*) + sizeof(TStroke*))) != nil))
			{
				long k = 0;
				for (ULong i = 0; i < 0x100; i++)
				{
					if (BitIsSet(strokes, i))
					{
						ULong index = IGGetRealStrokeIndex(data, i + base);
						if (count <= index)
							goto done;
						list[k++] = data->fUnits[index]->fStroke;
					}
				}
				list[k] = nil;
				short nStrokes;
				NewGetTraceFromStrokes(list, trace, &nStrokes, nPoints);
			}
		}
	}
done:
	if (list != local && list != nil)
		HWRMemoryFree((Ptr) list);
}


// ROM 0x000eb120 IGNewGroupData__FPPPc
// An empty group with room for twenty strokes.  ==> 1, or 0 for no memory.
long
IGNewGroupData(Handle* group)
{
	*group = HWRMemoryAllocHandle(GroupSize(0x14));
	if (*group == nil)
		return 0;
	GroupDataStruct* data = (GroupDataStruct*) HWRMemoryLockHandle(*group);
	memset(data, 0, GroupSize(0x14));
	data->fCapacity = 0x14;
	HUnlock(*group);
	return 1;
}


// ROM 0x000eb16c IGDisposeGroupData__FPPPc
void
IGDisposeGroupData(Handle* group)
{
	if (*group == nil)
		return;
	GroupDataStruct* data = (GroupDataStruct*) HWRMemoryLockHandle(*group);
	GCDisposeGResHandle(&data->fGRes);
	HWRMemoryUnlockHandle(*group);
	HWRMemoryFreeHandle(*group);
	*group = nil;
}


// ROM 0x000eb1ac IGLockGroupData__FPPc
GroupDataStruct*
IGLockGroupData(Handle group)
{
	return (GroupDataStruct*) HWRMemoryLockHandle(group);
}


// ROM 0x000eb1b0 IGUnlockGroupData__FPPc
long
IGUnlockGroupData(Handle group)
{
	return HWRMemoryUnlockHandle(group);
}


// ROM 0x000eb1b4 IGGetStrokesQueue__FPvPPP11TStrokeUnitPUl
void
IGGetStrokesQueue(GroupDataStruct* data, TStrokeUnit*** units, ULong* count)
{
	if (data == nil)
	{
		*units = nil;
		*count = 0;
	}
	else
	{
		*units = data->fUnits;
		*count = IGGetNumOfSrokes(data);
	}
}


// ROM 0x000eb1e8 IGAddSroke__FPPPcPP15GroupDataStructP11TStrokeUnit
// A stroke unit added to the group (a new group twenty strokes bigger
// made when it is full).  ==> 0, -1 for no memory, -2 for no group.
long
IGAddSroke(Handle* group, GroupDataStruct** dataP, TStrokeUnit* unit)
{
	long err = 0;
	GroupDataStruct* data = *dataP;
	if (*group == nil)
		return -2;
	if (data == nil)
		data = IGLockGroupData(*group);
	GroupDataStruct* into = data;
	if (data->fCapacity <= data->fCount)
	{
		Handle bigger = HWRMemoryAllocHandle(GroupSize(data->fCapacity + 0x14));
		if (bigger == nil)
		{
			err = -1;
			goto done;
		}
		into = IGLockGroupData(bigger);
		memcpy(into, data, GroupSize(data->fCapacity));
		memset(&into->fUnits[data->fCapacity], 0, 0x14 * sizeof(TStrokeUnit*));
		into->fCapacity = data->fCapacity + 0x14;
		data->fGRes = nil;
		IGUnlockGroupData(*group);
		IGDisposeGroupData(group);
		*group = bigger;
	}
	into->fUnits[into->fCount] = unit;
	into->fCount++;
done:
	if (*group != nil)
	{
		if (*dataP == nil)
			IGUnlockGroupData(*group);
		else
			*dataP = into;
	}
	return err;
}


// ROM 0x000eb2f0 IGGetNumOfSrokes__FP15GroupDataStruct
ULong
IGGetNumOfSrokes(GroupDataStruct* data)
{
	return data == nil ? 0 : data->fCount;
}


// ROM 0x000eb300 IGGetRealStrokeIndex__FP15GroupDataStructUl
// Which of the group's strokes the index'th stroke given to the segmenter
// is: the index itself while none are listed, otherwise counted through
// the list (and past its end for 250 and more).
ULong
IGGetRealStrokeIndex(GroupDataStruct* data, ULong index)
{
	UByte* list = data->fStrokes;
	if (GCGetRealStrokeIndex(list, 0xff) == 0 && (data->fStrokes[0x1f] & 1) == 0)
		return index;
	if (0xf9 < index)
		return (GCGetNumOfStrokesInList(list) + index) - 0xfa;
	long k = (short) (index & 0xff);
	if (list != nil && k < 0x100)
	{
		short n = 0;
		for (long i = 0; i < k; i++)
			if (BitIsSet(list, i))
				n = (short) (n + 1);
		return n;
	}
	return (ULong) -1;
}
