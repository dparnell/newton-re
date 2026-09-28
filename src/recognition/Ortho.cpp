/*
	File:		Ortho.cpp

	Contains:	The cursive reader's orthographic learning: the learn array
				a word's letters are tied to the trace with, and the
				training that walks it.  See Ortho.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM (from the
				disassembly - the decompiler loses OrtoEntries' stack).
*/

#include "Ortho.h"
#include "ParaGraph.h"
#include "XrReader.h"
#include "LowLevel.h"
#include "CursiveReader.h"
#include "WordSegment.h"
#include "XrDomains.h"
#include "ByteOrder.h"
#include <string.h>

// the learn array's fields (big-endian, as the ROM writes them a byte at
// a time)
static inline long		OrtoH(Ptr block, long at)			{ return GetBigEndianHalf(block + at); }
static inline void		OrtoSetH(Ptr block, long at, long v)	{ PutBigEndianHalf(block + at, (unsigned short) v); }
static inline UByte*	OrtoEntry(Ptr block, long i)		{ return (UByte*) block + kOrtoEntry + i * 4; }

// DEVIATION: the ROM keeps a pointer to the parts at +0x14; the host
// works it out from +0x04, as OrtoTraining does before it uses it
static inline Part_of_letter*
OrtoParts(Ptr block)
{
	return (Part_of_letter*) (block + GetBigEndianWord(block + kOrtoPartsOff));
}


/*------------------------------------------------------------------------------
	T h e   l e a r n   a r r a y
------------------------------------------------------------------------------*/

// ROM 0x00147548 OrtoCalcSize__FsT1
// The header, the entries and the parts (with none asked for, four an
// entry).
long
OrtoCalcSize(short entries, short parts)
{
	long n = parts;
	if (n <= 0)
		n = (short) (entries * 4);
	return (entries - 1) * 4 + n * 4 + 0x1c;
}


// ROM 0x0014757c OrtoGetmem__FsT1
// A learn array with room for so many entries and parts, empty.  (+0x0c
// is left as the allocation found it: OrtoEntries sets it.)
Ptr
OrtoGetmem(short entries, short parts)
{
	short size = (short) OrtoCalcSize(entries, parts);
	Ptr block = HWRMemoryAlloc(size);
	if (block == nil)
		return nil;
	PutBigEndianWord(block + kOrtoSize, size);
	OrtoSetH(block, kOrtoMaxEntries, entries);
	OrtoSetH(block, kOrtoMaxParts, parts);
	OrtoSetH(block, kOrtoEntries, 0);
	OrtoSetH(block, kOrtoParts, 0);
	PutBigEndianWord(block + kOrtoPartsOff, ((short) OrtoH(block, kOrtoMaxEntries) - 1) * 4 + 0x1c);
	PutBigEndianWord(block + kOrtoPartsPtr, 0);		// DEVIATION: see OrtoParts
	return block;
}


// ROM 0x00147610 OrtoResize__FP16_LEARN_ARRAY_tag
// A block just big enough for the entries and parts this one holds, a
// copy of them, and this one freed.  ==> the new block, nil (and this
// one freed) when there is no room.
Ptr
OrtoResize(Ptr block)
{
	if (block == nil)
		return nil;
	Ptr sized = OrtoGetmem((short) OrtoH(block, kOrtoEntries), (short) OrtoH(block, kOrtoParts));
	if (sized == nil)
	{
		OrtoDelete(block);
		return nil;
	}
	OrtoSetH(sized, kOrtoEntries, OrtoH(sized, kOrtoMaxEntries));
	OrtoSetH(sized, kOrtoParts, OrtoH(sized, kOrtoMaxParts));
	OrtoSetH(sized, kOrtoGroups, OrtoH(block, kOrtoGroups));
	memcpy(sized + kOrtoEntry, block + kOrtoEntry, (short) OrtoH(sized, kOrtoEntries) * 4);
	memcpy(OrtoParts(sized), OrtoParts(block), (short) OrtoH(sized, kOrtoParts) * 4);
	OrtoDelete(block);
	return sized;
}


// ROM 0x001476d4 OrtoFasten__FP16_LEARN_ARRAY_tagUcN22
// An entry added: the graph symbol and the first and last xr it was read
// from (the parts, once OrtoEntries has found them), no character yet.
long
OrtoFasten(Ptr block, UByte symbol, UByte first, UByte last)
{
	long n = (short) OrtoH(block, kOrtoEntries);
	if (n >= (short) OrtoH(block, kOrtoMaxEntries))
		return 0;
	UByte* e = OrtoEntry(block, n);
	e[3] = 0;
	e[2] = symbol;
	e[0] = first;
	e[1] = last;
	OrtoSetH(block, kOrtoEntries, OrtoH(block, kOrtoEntries) + 1);
	return 1;
}


// ROM 0x00147734 OrtoEntries__FsP16_LEARN_ARRAY_tagP8RWG_typeP11xrdata_type
// The learn array filled from the word graph.  First every symbol of the
// graph becomes an entry: a letter or digit the xrs it was read from
// (with flags bit 0, every one of them up to the last), anything else
// (the marks between alternatives) an empty one - counting the runs of
// symbols as it goes.  Then for each entry not yet done the stretches of
// the trace those xrs were written with are found
// (connect_trajectory_and_letter, at most four), the ones that are only a
// crossing's (0x34) taken out and the rest sorted (RemovePointAndSort),
// they are added to the parts, and every entry read from the same xrs
// is pointed at them and given its symbol's character.  ==> 1, 0 when the
// parts run out of room.
long
OrtoEntries(short flags, Ptr block, RWG_type* rwg, xrdata_type* xr)
{
	RWS_type* rws;
	long symbols;
	if (rwg == nil)
	{
		rws = nil;
		symbols = 0;
	}
	else
	{
		rws = rwg->rws;
		symbols = (short) rwg->size;
	}
	long result = 0;
	xrd_el_type* elements = (xrd_el_type*) xr->fElements;
	Part_of_letter* parts = (Part_of_letter*) HWRMemoryAlloc(0x10);
	if (parts == nil)
		return result;
	long all = flags & 1;
	UByte lastXr = 0;
	if (all)
		lastXr = (UByte) ((short) xr->fLength - 1);
	Boolean newRun = true;
	short runs = 0;
	for (short i = 0; i < symbols; i++)
	{
		RWS_type* s = &rws[i];
		UByte first = s->xrStart;
		if (s->type != 1)
		{
			newRun = true;
			OrtoFasten(block, (UByte) i, 1, 0);
			if (s->type == 3)
				break;
			continue;
		}
		if (newRun)
		{
			newRun = false;
			runs++;
		}
		if (IsAlnum(s->sym))
		{
			UByte last = all ? lastXr : (UByte) (s->xrLen + first - 1);
			if (last < 0x78 && last >= first)
				OrtoFasten(block, (UByte) i, first, last);
		}
	}
	OrtoSetH(block, kOrtoGroups, runs);
	for (short i = 0; i < (short) OrtoH(block, kOrtoEntries); i++)
	{
		UByte* e = OrtoEntry(block, i);
		if (e[3] != 0)
			continue;
		UByte first = e[0];
		UByte last = e[1];
		UByte partFirst = 1;
		UByte partLast = 0;
		if (first > last)
			continue;
		short count;
		if (connect_trajectory_and_letter(elements, first, last, &count, parts) == 0 && count != 0)
		{
			RemovePointAndSort(xr, first, last, parts, &count);
			long have = (short) OrtoH(block, kOrtoParts);
			long total = have + count;
			if (total >= (short) OrtoH(block, kOrtoMaxParts))
				goto out;
			partFirst = (UByte) have;
			partLast = (UByte) (partFirst + count - 1);
			OrtoSetH(block, kOrtoParts, total);
			memcpy(OrtoParts(block) + partFirst, parts, count * 4);
		}
		for (short j = i; j < (short) OrtoH(block, kOrtoEntries); j++)
		{
			UByte* f = OrtoEntry(block, j);
			if (f[3] == 0 && f[0] == first && f[1] == last)
			{
				UByte symbol = f[2];
				f[0] = partFirst;
				f[1] = partLast;
				if (rws[symbol].type == 1)
					f[3] = rws[symbol].sym;
			}
		}
	}
	result = 1;
out:
	HWRMemoryFree((Ptr) parts);
	return result;
}


// ROM 0x00147a50 LearnPartsCopy__FP13PS_point_typeT1P14Part_of_letters
// A letter's own points out of the trace, stretch by stretch, each
// stretch ended with a pen-up (as is the start), the pen-ups of the
// trace itself left out.  ==> how many points were written, 0 when the
// 256 of the buffer run out.
long
LearnPartsCopy(const PS_point_type* trace, PS_point_type* out, const Part_of_letter* parts, short count)
{
	if (trace == nil || out == nil || parts == nil)
		return 0;
	if (count <= 0)
		return 0;
	out[0].x = 0;
	out[0].y = -1;
	short n = 1;
	for (short i = 0; i < count; i++)
	{
		Boolean any = false;
		for (short p = XrGetH(parts[i].beg); p <= XrGetH(parts[i].end); p++)
		{
			if (trace[p].y < 0)
				continue;
			out[n] = trace[p];
			if (++n == 0x100)
				return 0;
			any = true;
		}
		if (any)
		{
			out[n].x = 0;
			out[n].y = -1;
			if (++n == 0x100)
				return 0;
		}
	}
	out[n].x = 0;
	out[n].y = -1;
	return (short) (n + 1);
}


// ROM 0x00147b88 RemovePointAndSort__FP11xrdata_typesT2P14Part_of_letterPs
// The stretches of a letter tidied.  When there are more than one, the
// first crossing (0x34) among the letter's inner xrs - the one next to a
// break at either end left out - has every stretch that holds it taken
// out, and then the stretches are sorted by where they start.
// ROM QUIRK kept: after a stretch is taken out the one moved into its
// place is not looked at.
void
RemovePointAndSort(xrdata_type* xr, short from, short to, Part_of_letter* parts, short* count)
{
	xrd_el_type* el = (xrd_el_type*) xr->fElements;
	short n = *count;
	if (n <= 1)
		return;
	if (el != nil)
	{
		for (short i = from + 1; i < to; i++)
		{
			if (i == from + 1 && el[from].type == 1)
				continue;
			if (i == to - 1 && el[to].type == 1)
				continue;
			if (el[i].type != 0x34)
				continue;
			short beg = XrGetH(el[i].begpoint);
			short end = XrGetH(el[i].endpoint);
			for (short k = 0; k < n && k < *count; k++)
			{
				if (XrGetH(parts[k].beg) <= beg && end <= XrGetH(parts[k].end))
				{
					*count = *count - 1;
					memmove(&parts[k], &parts[k + 1], (*count - k) * 4);
				}
			}
			break;
		}
	}
	for (short j = *count - 2; j >= 0; j--)
		for (short k = j; k >= 0; k--)
			if (XrGetH(parts[k].beg) > XrGetH(parts[k + 1].beg))
			{
				Part_of_letter t = parts[k];
				parts[k] = parts[k + 1];
				parts[k + 1] = t;
			}
}


// ROM 0x00147d70 ORTraining__FPvP13PS_point_typeP10rec_w_typeT1
// The word the writer settled on trained into the database, from the
// word's learn array ('ORTL') and its trace.
void
ORTraining(void* db, const PS_point_type* trace, const rec_w_type* word, void* ortl)
{
	if (db == nil || trace == nil || word == nil || ortl == nil)
		return;
	if (word->fWord[0] == 0)
		return;
	OrtoTraining((Ptr) ortl, db, (const char*) word->fWord, trace);
}


// ROM 0x00147da4 ORLArrayDelete__FPPv
void
ORLArrayDelete(void** ortl)
{
	if (ortl == nil || *ortl == nil)
		return;
	*ortl = OrtoDelete((Ptr) *ortl);
}


// ROM 0x00147dd4 OrtoCreate__FsP8RWG_typeP11xrdata_type
// A word's learn array, as big as it needs to be.  ==> nil for a graph
// with nothing in it, or no room.
Ptr
OrtoCreate(short flags, RWG_type* rwg, xrdata_type* xr)
{
	if (rwg == nil || rwg->rws == nil || xr == nil)
		return nil;
	Ptr block = OrtoGetmem(0x80, 0x80);
	if (block == nil)
		return nil;
	if (OrtoEntries(flags, block, rwg, xr) == 0)
	{
		OrtoDelete(block);
		return nil;
	}
	return OrtoResize(block);
}


// ROM 0x00147e58 OrtoDelete__FP16_LEARN_ARRAY_tag
Ptr
OrtoDelete(Ptr block)
{
	if (block != nil)
		HWRMemoryFree(block);
	return nil;
}


// ROM 0x00147e74 OrtoTraining__FP16_LEARN_ARRAY_tagPvPcP13PS_point_type
// The first run of entries that spells the word (from an entry that
// starts a run, the letters and digits of the word matched against the
// entries' characters in lower case, anything else in the word skipped
// but still compared) trained letter by letter: each letter's points,
// when there are more than three, go to TrainTrajectory.  ==> 1 once the
// buffer was had, 0 otherwise.
long
OrtoTraining(Ptr block, void* db, const char* word, const PS_point_type* trace)
{
	if (block == nil || db == nil || word == nil || trace == nil)
		return 0;
	PS_point_type* points = (PS_point_type*) HWRMemoryAlloc(0x400);
	if (points == nil)
		return 0;
	PutBigEndianWord(block + kOrtoPartsOff, ((short) OrtoH(block, kOrtoMaxEntries) - 1) * 4 + 0x1c);
	for (short i = 0; i < (short) OrtoH(block, kOrtoEntries); i++)
	{
		UByte* e = OrtoEntry(block, i);
		if (e[3] == 0)
			continue;
		if (i != 0 && e[-1] != 0)
			continue;
		UByte start = (UByte) i;
		UByte n = 0;
		UByte skipped = 0;
		if (start <= (short) OrtoH(block, kOrtoEntries))
		{
			for ( ; ; )
			{
				UByte c1 = (start + n == (short) OrtoH(block, kOrtoEntries))
						 ? 0 : (UByte) ToLower(OrtoEntry(block, start + n)[3]);
				UByte c2 = (UByte) ToLower((UByte) word[n + skipped]);
				if (IsAlnum(c2))
					n++;
				else
					skipped++;
				if (c1 != c2)
					goto next;
				if (c1 == 0)
					break;
				if (start + n > (short) OrtoH(block, kOrtoEntries))
					break;
			}
		}
		for (short k = start; k < start + n; k++)
		{
			UByte* f = OrtoEntry(block, k);
			UByte sym = (UByte) ToLower(f[3]);
			long got = LearnPartsCopy(trace, points, OrtoParts(block) + f[0], (short) (f[1] - f[0] + 1));
			if (got > 3)
				TrainTrajectory(points, db, sym);
		}
		break;
	next:
		;
	}
	HWRMemoryFree((Ptr) points);
	return 1;
}


// ROM 0x00148054 OrtoSize__FP16_LEARN_ARRAY_tag
ULong
OrtoSize(Ptr block)
{
	return block == nil ? 0 : GetBigEndianWord(block + kOrtoSize);
}


// ROM 0x00148078 ORCreateLearnInfo__FP11xrdata_typeP8RWG_typePPvPUl
// The word's learn array (every letter to the xrs it was read from, not
// the whole word's) and its size, for its training data.
void
ORCreateLearnInfo(xrdata_type* xr, RWG_type* rwg, void** ortl, ULong* size)
{
	if (ortl == nil || xr == nil || rwg == nil)
		return;
	*ortl = OrtoCreate(2, rwg, xr);
	if (size != nil)
		*size = OrtoSize((Ptr) *ortl);
}
