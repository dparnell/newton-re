/*
	File:		XrAnswers.cpp

	Contains:	The cursive reader's answers: the word graph `xrw_algs`
				made turned into the readings (`rec_w_type`) a word
				descriptor keeps (see XrReader.h).

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	A list-of-answers graph (type 1, or 4 once the answers have been
	evaluated) is a run of symbols per answer with a type 3 or 4 symbol
	between them; each answer becomes one reading - its letters, the
	graph's score for it and its dictionary attribute - and the readings
	are sorted best first, the graph put into the same order, the scores
	brought down to the 0..100 a unit carries, and the readings too far
	below the best (rc +0x1a) or below the field's floor (rc +0x16)
	dropped.

	Reconstructed from the MP2x00 US ROM (0x0019f644-0x0019f874,
	0x00337fa4-0x00338d34); each function cites its origin.
*/

#include "XrReader.h"
#include "XrDomains.h"
#include "CursiveReader.h"
#include "LowLevel.h"
#include "Chunk.h"			// ChunkCtx, tagNumBox: a number's words

#include <string.h>

static inline short	RCSigned(rc_type* rc, ULong offset)	{ return (short) RCGetH(rc, offset); }

// the ARM's __rt_sdiv: the quotient truncated towards nought (a divisor
// of nought is never passed here)
static inline long	SDiv(long dividend, long divisor)	{ return dividend / divisor; }


#pragma mark - the readings

// ROM 0x00338158 MakeRecWordsFromWordGraph__FP8RWG_typeP10rec_w_typei
// The readings of a list-of-answers graph, one per answer (at most ten):
// each answer's letters (at most 23), the graph's score for it (its
// first symbol's weight) and its first symbol's +0x0d, and the attribute
// of its last symbol.  With a scale (nought: none) the score is worked
// out again from what each letter added less what each was charged
// (+0x0b, +0x0c) and the answer's penalty, times a thousand over the
// scale, plus ten times the answer's +0x08.
void
MakeRecWordsFromWordGraph(RWG_type* rwg, rec_w_type* readings, long scale)
{
	readings[0].fWord[0] = 0;
	readings[0].fX4A[0] = 0xff;
	readings[0].fX4A[1] = 0xff;
	RWS_type* rws = rwg->rws;
	if (rws == nil)
		return;
	// (the ROM's code has a flag it sets to one on entry and tests twice:
	// with it clear a letter's +0x0b would count half and the score would
	// add three times a field of the reading's +0x4c; both are dead)
	long i = 0;
	long r = 0;
	long len = 0;
	long sum = 0;
	long pen = 0;
	long bonus = 0;
	while (i < rwg->size && r < 10)
	{
		RWS_type* e = &rws[i];
		Boolean atEnd = false;
		if (e->type == 1)
		{
			if (len < 0x17)
			{
				if (len == 0)
				{
					if (r < 9)
						readings[r + 1].fWord[0] = 0;
					sum = 0;
					pen = e->f07;
					bonus = (SByte) e->f08;
					readings[r].fWeight = (short) e->weight;
					readings[r].fX4A[2] = 0;
					readings[r].fX4A[3] = e->f0d;
				}
				long attr = (SByte) e->attr;
				readings[r].fX4A[0] = (UByte) (attr >> 8);
				readings[r].fX4A[1] = (UByte) attr;
				sum = (short) (((SByte) e->letWeight - (SByte) e->f0b) - (SByte) e->f0c + sum);
				readings[r].fWord[len] = rws[i].sym;
				readings[r].fX30[len] = 0;
				readings[r].fWord[len + 1] = 0;
				len = (short) (len + 1);
				atEnd = e->type == 3 || e->type == 4;
			}
		}
		else
			atEnd = e->type == 3 || e->type == 4;
		if (!atEnd)
			atEnd = rwg->size - 1 == i;
		if (atEnd && scale != 0)
		{
			long weight = (short) SDiv((sum - pen) * 1000, scale);
			weight = (UShort) weight + bonus * 10;
			readings[r].fWeight = (short) weight;
		}
		if (e->type == 3 || e->type == 4)
		{
			r = (short) (r + 1);
			len = 0;
		}
		i = (short) (i + 1);
	}
}


#pragma mark - the readings of a graph of alternatives

// A graph that is not a list of answers is one answer in which some
// letters are groups of alternatives: a type 2 symbol opens a group, a
// type 4 one comes between two alternatives and a type 3 one closes it;
// an alternative is one symbol, or two in a row (a letter the reader saw
// as two).  Everything else is a letter of its own.  Its readings are the
// paths through it: the first takes each group's best alternative, and
// each after it changes one letter of an earlier path to the next
// alternative down, choosing the change that loses least.

static inline Boolean	IsTwoSymbols(const RWS_type* rws, long e)	{ return rws[e + 1].type == 1; }
// the alternative after the one starting at e (past e's symbols and the type 4 between)
static inline long		NextAlternative(const RWS_type* rws, long e)	{ return e + (IsTwoSymbols(rws, e) ? 3 : 2); }


// ROM 0x00338acc FillRecWordsElement__FP10rec_w_typeP8RWS_typesN23
// The symbol e put into a reading as its letter at pos, with the variant
// it was read as and the xrs it spans - unless the reader read it as a
// different letter (another case is the same letter), when the variant and
// span are left out; a symbol read in another case has its variant's top
// bit set.
// ROM BUG: a letter read as a different one clears the variant and span of
// the reading's *first* letter (+0x18, +0x30), not of the letter at pos.
void
FillRecWordsElement(rec_w_type* readings, RWS_type* rws, short reading, short pos, short e)
{
	rec_w_type* rw = &readings[reading];
	rw->fWord[pos] = rws[e].sym;
	if (ToLower(rws[e].sym) != ToLower(rws[e].realSym))
	{
		rw->fVariants[0] = 0;
		rw->fX30[0] = 0;
		return;
	}
	rw->fVariants[pos] = rws[e].var;
	rw->fX30[pos] = rws[e].xrLen;
	if (rws[e].sym != rws[e].realSym)
		rw->fVariants[pos] |= 0x80;
}


// ROM 0x00338b6c MakeNewPath__FP8RWS_typePA24_UcUsN23PsT6
// The letter of path `from` best changed to its next alternative down so
// as to make a path not yet among the first `count`: the one whose change
// loses least score (ties to the first).  Only a letter that is a group's
// alternative with another after it may change.  ==> whether there was
// one; *pos the letter, *loss what it loses.
Boolean
MakeNewPath(RWS_type* rws, UByte (*paths)[24], UShort groups, UShort count, UShort from, short* pos, short* loss)
{
	*pos = -1;
	*loss = 30000;
	for (short i = 0; i < (short) groups; i = (short) (i + 1))
	{
		short e = paths[from][i];
		if (e == 0)
			continue;
		if (rws[e - 1].type != 2 && rws[e - 1].type != 4)
			continue;
		short alt;
		if (IsTwoSymbols(rws, e))
		{
			if (rws[e + 2].type == 3)
				continue;
			alt = (short) (e + 3);
		}
		else
		{
			if (rws[e + 1].type == 3)
				continue;
			alt = (short) (e + 2);
		}
		// is the changed path one there is already?
		Boolean known = false;
		for (short j = 0; j < (short) count && !known; j = (short) (j + 1))
		{
			Boolean same = true;
			for (short k = 0; k < (short) groups; k = (short) (k + 1))
			{
				if (k != i ? paths[from][k] != paths[j][k] : paths[j][k] != alt)
				{
					same = false;
					break;
				}
			}
			known = same;
		}
		if (known)
			continue;
		long diff = (long) rws[e].weight - (long) rws[alt].weight;
		if (diff < *loss)
		{
			*loss = (short) diff;
			*pos = i;
		}
	}
	return *pos != -1;
}


// A path's score: the mean of its letters' (DEVIATION: with no letters the
// ROM divides by nought, which traps on the ARM; the host answers nought).
static inline short		PathScore(UShort sum, UShort groups)	{ return (short) (groups != 0 ? (ULong) sum / groups : 0); }


// ROM 0x003383c0 MakeRecWordsFromGraph__FP8RWS_typeUsP10rec_w_typePUcPA13_15RWG_PPD_el_type
// The readings of a graph of alternatives (above), at most ten, into
// `readings`; `paths` (ten of twenty-four bytes) keeps each reading's
// choice per letter.  Each group's alternatives are first put best first
// in the graph itself, and the two symbols of an alternative made of two
// given the mean of their scores.
void
MakeRecWordsFromGraph(RWS_type* rws, UShort size, rec_w_type* readings, UByte* pathBytes, void* ppd)
{
	(void) ppd;
	if (readings == nil || pathBytes == nil)
		return;
	UByte (*paths)[24] = (UByte (*)[24]) pathBytes;
	UByte first[0x18];
	UByte count[0x18];
	memset(first, 0, sizeof(first));
	memset(count, 0, sizeof(count));	// (the ROM leaves these as the stack had them; each is set before it is read)
	UShort groups = 0;
	Boolean inAlternatives = false;
	for (UShort i = 0; i < size; i++)
	{
		if (groups >= 0x18)
			break;		// DEVIATION: the ROM goes on writing past its 24-byte tables (a word has fewer letters)
		switch (rws[i].type)
		{
		case 1:
			if (first[groups] == 0)
			{
				first[groups] = i;
				count[groups] = 0;
			}
			if (inAlternatives)
			{
				if (rws[i - 1].type == 1)
					break;						// the second symbol of an alternative of two
				count[groups]++;
				if (rws[i + 1].type == 1)
				{
					rws[i].weight = (UByte) (((long) rws[i].weight + rws[i + 1].weight) / 2);
					rws[i + 1].weight = rws[i].weight;
				}
			}
			else
			{
				count[groups]++;
				groups++;
			}
			break;
		case 2:
			inAlternatives = true;
			break;
		case 3:
			inAlternatives = false;
			groups++;
			break;
		default:
			break;
		}
	}
	// each group's alternatives sorted best first (a bubble sort, the
	// alternatives moved about in the graph with the type 4 symbols
	// between them kept in place)
	for (UShort g = 0; g < groups; g++)
	{
		Boolean sorted;
		do
		{
			sorted = true;
			short pos = first[g];
			for (UShort j = 0; (long) count[g] - 1 > (long) j; j++)
			{
				Boolean two = IsTwoSymbols(rws, pos);
				short next = (short) (pos + (two ? 3 : 2));
				if (rws[pos].weight < rws[next].weight)
				{
					Boolean nextTwo = IsTwoSymbols(rws, next);
					RWS_type old[5];
					memcpy(old, &rws[pos], (two && nextTwo ? 5 : two || nextTwo ? 4 : 3) * sizeof(RWS_type));
					if (two && nextTwo)
					{
						rws[pos] = old[3];
						rws[pos + 1] = old[4];
						rws[pos + 3] = old[0];
						rws[pos + 4] = old[1];
					}
					else if (two)
					{
						rws[pos] = old[3];
						rws[pos + 1] = old[2];
						rws[pos + 2] = old[0];
						rws[pos + 3] = old[1];
						next = (short) (next - 1);
					}
					else if (nextTwo)
					{
						rws[pos] = old[2];
						rws[pos + 1] = old[3];
						rws[pos + 2] = old[1];
						rws[pos + 3] = old[0];
						next = (short) (next + 1);
					}
					else
					{
						rws[pos] = old[2];
						rws[pos + 2] = old[0];
					}
					sorted = false;
				}
				pos = next;
			}
		} while (!sorted);
	}
	// the first reading: every group's best
	UShort sum = 0;
	short letter = 0;
	for (UShort g = 0; g < groups; g++, letter = (short) (letter + 1))
	{
		short e = first[g];
		paths[0][g] = (UByte) e;
		sum = (UShort) (sum + rws[e].weight);
		FillRecWordsElement(readings, rws, 0, letter, e);
		if (e > 0 && (rws[e - 1].type == 2 || rws[e - 1].type == 4) && IsTwoSymbols(rws, e))
		{
			letter = (short) (letter + 1);
			FillRecWordsElement(readings, rws, 0, letter, (short) (e + 1));
		}
	}
	readings[0].fWord[letter] = 0;
	readings[0].fWeight = PathScore(sum, groups);
	readings[1].fWord[0] = 0;
	// then each reading after it, the best change of an earlier one
	UShort made = 1;
	Boolean done;
	do
	{
		done = true;
		short bestFrom = -1;
		short bestSum = 0;
		short bestPos = 0;
		for (UShort j = 0; j < made; j++)
		{
			short pos, loss;
			if (!MakeNewPath(rws, paths, groups, made, j, &pos, &loss))
				continue;
			short total = 0;
			for (UShort g = 0; g < groups; g++)
			{
				UShort e = paths[j][g];
				if (g == (UShort) pos)
					e = (UShort) NextAlternative(rws, e);
				total = (short) (total + rws[e].weight);
			}
			if (total > bestSum)
			{
				bestSum = total;
				bestFrom = (short) j;
				bestPos = pos;
				done = false;
			}
		}
		if (bestFrom == -1)
			break;
		UShort pathSum = 0;
		letter = 0;
		for (UShort g = 0; g < groups; g++, letter = (short) (letter + 1))
		{
			UByte e = g == (UShort) bestPos ? (UByte) NextAlternative(rws, paths[bestFrom][g]) : paths[bestFrom][g];
			paths[made][g] = e;
			pathSum = (UShort) (pathSum + rws[e].weight);
			FillRecWordsElement(readings, rws, made, letter, e);
			if (e > 0 && (rws[e - 1].type == 2 || rws[e - 1].type == 4) && IsTwoSymbols(rws, e))
			{
				letter = (short) (letter + 1);
				FillRecWordsElement(readings, rws, made, letter, (short) (e + 1));
			}
		}
		readings[made].fWord[letter] = 0;
		readings[made].fWeight = PathScore(pathSum, groups);
		if (made + 1 < 10)
			readings[made + 1].fWord[0] = 0;
		made++;
	} while (made != 10 && !done);
}


// ROM 0x00337fa4 MergeTwoRecWordsSets__FP10rec_w_typeT1
// Two sets of ten readings merged into the first, best score first (ties
// to the first set), a reading whose word is already there left out.  A
// reading scored below nought is never taken.
// DEVIATION: the ROM runs on past a set's tenth reading when so many are
// left out that one set is used up (reading whatever follows it); the host
// takes a set's eleventh reading as empty.  The ROM copies the ten back
// whether or not they were filled, which on the host is what memset left.
void
MergeTwoRecWordsSets(rec_w_type* into, rec_w_type* other)
{
	rec_w_type merged[10];
	memset(merged, 0, sizeof(merged));
	rec_w_type* sets[2] = { into, other };
	short next[2] = { 0, 0 };
	for (short n = 0; n < 10; n = (short) (n + 1))
	{
		merged[n].fWord[0] = 0;
		long best = -1;
		short fromSet = 0;
		for (short s = 0; s < 2; s = (short) (s + 1))
		{
			if (next[s] >= 10)
				continue;
			rec_w_type* rw = &sets[s][next[s]];
			if (rw->fWord[0] != 0 && rw->fWeight > best)
			{
				best = rw->fWeight;
				fromSet = s;
			}
		}
		if (best == -1)
			break;
		rec_w_type* rw = &sets[fromSet][next[fromSet]];
		Boolean known = false;
		for (short k = 0; k < n; k = (short) (k + 1))
			if (HWRStrCmp((const char*) merged[k].fWord, (const char*) rw->fWord) == 0)
			{
				known = true;
				break;
			}
		if (known)
			n = (short) (n - 1);
		else
			merged[n] = *rw;
		next[fromSet]++;
	}
	memcpy(into, merged, sizeof(merged));
}


// ROM 0x0019f644 MakeAndCombRecWordsFromWordGraph__FP8RWG_typeP7rc_typeP11xrdata_typeP10rec_w_type
// The readings made of a list-of-answers graph (scored against ten per
// xr, less ten), sorted best first with the graph put into the same
// order, their scores brought into 0..100, and those that are too far
// below the best (rc +0x1a) or below the floor (rc +0x16) dropped - the
// first of them and all after it.
void
MakeAndCombRecWordsFromWordGraph(RWG_type* rwg, rc_type* rc, xrdata_type* xr, rec_w_type* readings)
{
	long scale = 0;
	if (rwg->type == 1 || rwg->type == 4)
	{
		scale = xr->fLength * 10 - 10;
		if (scale < 0)
			scale = 0;
		MakeRecWordsFromWordGraph(rwg, readings, scale);
		fill_RW_aliases(readings, rwg);
	}
	int order[10];
	for (long i = 0; i < 10; i++)
		order[i] = (int) i;
	Boolean sorted;
	do
	{
		sorted = true;
		for (long i = 1; i < 10; i++)
		{
			if (readings[i].fWord[0] == 0)
				break;
			if (readings[i - 1].fWeight < readings[i].fWeight)
			{
				rec_w_type swap = readings[i];
				readings[i] = readings[i - 1];
				readings[i - 1] = swap;
				int n = order[i];
				order[i] = order[i - 1];
				order[i - 1] = n;
				sorted = false;
			}
		}
	} while (!sorted);
	SortGraph(&order, rwg);
	if (0 < scale)
	{
		for (long i = 0; i < 10; i++)
		{
			if (readings[i].fWord[0] == 0)
				break;
			readings[i].fWeight = (short) (readings[i].fWeight / 10);
			if (100 < readings[i].fWeight)
				readings[i].fWeight = 100;
			else if (readings[i].fWeight < 0)
				readings[i].fWeight = 0;
		}
	}
	for (long i = 0; ; )
	{
		if (readings[i].fWord[0] == 0)
		{
			readings[i].fWeight = 0;
			return;
		}
		if (readings[i].fWeight < readings[0].fWeight - RCSigned(rc, 0x1a)
		 || readings[i].fWeight < RCSigned(rc, 0x16))
		{
			readings[i].fWord[0] = 0;
			readings[i].fWeight = 0;
			return;
		}
		if (9 < ++i)
			return;
	}
}


#pragma mark - which strokes each word of a reading is

/*------------------------------------------------------------------------------
	A reading of several words (the engine read a space inside the ink it
	was given) is split into them.  The split information
	(RecwordSplitInfoType, 0x5b bytes and one more per stroke) is, in the
	ROM's bytes -
		+00  three bytes a reading for the first five: a bit for each
		     letter a word ends at
		+0f  how many words
		+10  twelve bytes a reading: each word's dictionary attribute (0xfd
		     where the graph cut without a space)
		+4c  twelve bytes: how many strokes each word has
		+58  the strokes, word by word (numbered from one)
	Each letter's xrs are traced back to the stretches of the trace they
	were made from (connect_trajectory_and_answers), and each stretch to
	the strokes it runs through (AddStrokesOfSymbol); a stroke no letter
	claimed goes to the word whose middle is nearest its own
	(AttachLostStrokeToWord).
------------------------------------------------------------------------------*/

static inline short	TraceY(const PS_point_type* trace, long i)	{ return trace[i].y; }


// ROM 0x002d48cc (unnamed)
// A stretch grown to take in an xr's points.
static void
GrowPartOfLetter(Part_of_letter* part, xrd_el_type* xr)
{
	short beg = XrGetH(xr->begpoint);
	if (beg < XrGetH(part->beg))
		XrSetH(part->beg, beg);
	short end = XrGetH(xr->endpoint);
	if (end > XrGetH(part->end))
		XrSetH(part->end, end);
}


static inline Boolean
IsCrossingMark(UByte type)
{
	return type == 0x34 || type == 0x36 || type == 0x3a || type == 0x3b;
}


// ROM 0x002d4aac connect_trajectory_and_letter__FP11xrd_el_typesT2PsP14Part_of_letter
// The stretches of the trace one letter's xrs (from..to) were made from,
// at most four: a run of xrs between breaks is one stretch, grown to
// take in each one's points; a crossing mark (0x34, 0x36, 0x3a, 0x3b) is
// a stretch of its own - unless one just like it came earlier in the
// letter - and one that does not follow another is swapped with the
// stretch before it.  ==> 0, 1 for more than four.
long
connect_trajectory_and_letter(xrd_el_type* xr, short from, short to, short* count, Part_of_letter* parts)
{
	Part_of_letter* part = nil;
	long n = 0;
	Boolean fresh = true;
	for (long i = from; i <= to; i = (short) (i + 1))
	{
		xrd_el_type* e = &xr[i];
		if (X_IsBreak(e) || e->type == 5)
		{
			if (!fresh)
				fresh = true;
			continue;
		}
		UByte type = e->type;
		if (IsCrossingMark(type))
		{
			if (from < i)
			{
				long j;
				for (j = from; j < i; j++)
					if (xr[j].type == type
					 && XrGetH(xr[j].begpoint) == XrGetH(e->begpoint)
					 && XrGetH(xr[j].endpoint) == XrGetH(e->endpoint))
						break;
				if (j < i)
					continue;
			}
			if (!fresh && IsCrossingMark(e[-1].type))
				fresh = true;
			long k = n;
			n = (short) (n + 1);
			part = &parts[k];
			if (4 < n)
				return 1;
			XrSetH(part->beg, (UShort) XrGetH(e->begpoint));
			XrSetH(part->end, (UShort) XrGetH(e->endpoint));
			if (!fresh)
			{
				Part_of_letter swap = *part;
				*part = parts[n - 2];
				parts[n - 2] = swap;
			}
		}
		else if (fresh)
		{
			long k = n;
			n = (short) (n + 1);
			part = &parts[k];
			if (4 < n)
				return 1;
			XrSetH(part->beg, (UShort) XrGetH(e->begpoint));
			XrSetH(part->end, (UShort) XrGetH(e->endpoint));
			fresh = false;
		}
		else
			GrowPartOfLetter(part, e);
	}
	*count = (short) n;
	return 0;
}


// ROM 0x002d4908 connect_trajectory_and_answers__FP11xrd_el_typeP10rec_w_typeP13Osokin_output
// The stretches of the trace each letter of the reading was made from
// (each letter's xrs following the last one's, as many as fX30 says).
// ==> 0; 1 when the reading has no xr counts, a letter has more than
// four stretches, there are more than 0x5b in all, or a stretch runs
// backwards - with nothing kept.
long
connect_trajectory_and_answers(xrd_el_type* xr, rec_w_type* reading, Osokin_output* out)
{
	if (reading->fX30[0] == 0)
		return 1;
	out->parts = (Part_of_letter*) HWRMemoryAlloc(0x60 * sizeof(Part_of_letter));
	if (out->parts != nil)
	{
		Part_of_letter* parts = out->parts;
		memset(parts, 0, 0x60 * sizeof(Part_of_letter));
		memset(out->count, 0, 0x18);
		long last = 0;
		long total = 0;
		long k = 0;
		Boolean failed = false;
		while (reading->fWord[k] != 0)
		{
			short first = (short) (last + 1);
			last = (short) (reading->fX30[k] + first - 1);
			short count;
			if (0x5c <= total || connect_trajectory_and_letter(xr, first, (short) last, &count, parts) != 0)
			{
				failed = true;
				break;
			}
			out->count[k] = (UByte) count;
			parts += count;
			total = (short) (total + count);
			k = (short) (k + 1);
		}
		if (!failed)
		{
			long p = 0;
			for (long l = 0; reading->fWord[l] != 0 && !failed; l = (short) (l + 1))
			{
				for (long j = 0; j < out->count[l]; j = (short) (j + 1))
				{
					if (XrGetH(out->parts[p].beg) > XrGetH(out->parts[p].end))
					{
						failed = true;
						break;
					}
					p = (short) (p + 1);
				}
			}
			if (!failed)
				return 0;
		}
		if (out->parts != nil)
			HWRMemoryFree((Ptr) out->parts);
	}
	out->parts = nil;
	return 1;
}


// ROM 0x0019ee54 GetStrokeNumber__FiP7rc_type
// The stroke (numbered from one) the trace's point is in: the pen-ups
// up to it; nought for a pen-up or a point outside the trace.
long
GetStrokeNumber(long point, rc_type* rc)
{
	const PS_point_type* trace = (const PS_point_type*) rc->fTrace;
	long n = 0;
	if (0 < point && point < (short) RCGetH(rc, 0x96) && 0 <= TraceY(trace, point))
		for (long i = 0; i <= point; i++)
			if (TraceY(trace, i) < 0)
				n++;
	return n;
}


// ROM 0x0019f0ec GetBegEndOfStroke__FiP7rc_typePiT3
// Where a stroke (numbered from one) starts and ends in the trace.
void
GetBegEndOfStroke(long stroke, rc_type* rc, long* beg, long* end)
{
	const PS_point_type* trace = (const PS_point_type*) rc->fTrace;
	long n = (short) RCGetH(rc, 0x96);
	long s = 1;
	*beg = 0;
	*end = 0;
	long i = 1;
	while (i < n && s != stroke)
	{
		if (TraceY(trace, i) < 0)
			s++;
		i++;
	}
	*beg = i;
	long e;
	do
	{
		e = i;
		i = e + 1;
	} while (0 <= TraceY(trace, i));
	*end = e;
}


// ROM 0x0019ed6c AddStrokesOfSymbol__FiN31P7rc_typeP20RecwordSplitInfoType
// The strokes a stretch of the trace (from..to) runs through added to
// word `word`'s (whose strokes follow the `before` of the words before
// it).  ==> 1, or 0 when a stroke belongs to an earlier word already.
long
AddStrokesOfSymbol(long from, long to, long before, long word, rc_type* rc, UByte* split)
{
	const PS_point_type* trace = (const PS_point_type*) rc->fTrace;
	UByte* counts = split + word;
	long count = counts[0x4c];
	for (long p = from; p <= to + 1; p++)
	{
		if (!(TraceY(trace, p) < 0 || to + 1 == p))
			continue;
		long stroke = GetStrokeNumber(p - 1, rc);
		if (stroke == 0)
			continue;
		long j = 0;
		Boolean seen = false;
		for ( ; j < before + count; j++)
		{
			if (split[0x58 + j] == stroke)
			{
				if (j < before)
					return 0;
				seen = true;
				break;
			}
		}
		if (seen)
			continue;
		count++;
		counts[0x4c] = (UByte) count;
		split[0x58 + j] = (UByte) stroke;
	}
	return 1;
}


// ROM 0x0019eeb4 AttachLostStrokeToWord__FiP7rc_typeP20RecwordSplitInfoType
// A stroke no word claimed given to the word whose box's middle is
// nearest the stroke's, after that word's strokes.
void
AttachLostStrokeToWord(long stroke, rc_type* rc, UByte* split)
{
	PS_point_type* trace = (PS_point_type*) rc->fTrace;
	long at = -1;
	long bestWord = 0;
	long bestDistance = 0x7fff;
	long beg, end;
	_RECT box;
	GetBegEndOfStroke(stroke, rc, &beg, &end);
	GetBoxFromTrace(trace, beg, end, &box);
	long middle = (box.right + box.left) >> 1;
	long first = 0;
	for (long w = 0; w < split[0xf]; w++)
	{
		_RECT wordBox = { 0x7fff, 0x7fff, 0, 0 };
		long count = split[0x4c + w];
		for (long s = first; s < first + count; s++)
		{
			long b, e;
			_RECT r;
			GetBegEndOfStroke(split[0x58 + s], rc, &b, &e);
			GetBoxFromTrace(trace, b, e, &r);
			if (wordBox.left > r.left)
				wordBox.left = r.left;
			if (wordBox.right < r.right)
				wordBox.right = r.right;
			if (wordBox.top > r.top)
				wordBox.top = r.top;
			if (wordBox.bottom < r.bottom)
				wordBox.bottom = r.bottom;
		}
		long distance = HWRAbs(((wordBox.right + wordBox.left) >> 1) - middle);
		if (distance < bestDistance)
		{
			bestWord = w;
			bestDistance = distance;
			at = first + count;
		}
		first += count;
	}
	if (at != -1)
	{
		// (the ROM's memcpy copies from the top down when the two overlap,
		// so this is a memmove)
		memmove(split + at + 0x59, split + at + 0x58, first - at);
		split[at + 0x58] = (UByte) stroke;
		split[0x4c + bestWord]++;
	}
}


// ROM 0x0019ec18 FillSplitInfoFromRWG__FP11xrdata_typeP8RWG_typeP20RecwordSplitInfoType
// The words of the other readings, from the graph: each answer's
// spaces, and each place its letters' xrs end at a letter's end the low
// level marked (attrib 4), are where a word ends.
void
FillSplitInfoFromRWG(xrdata_type* xr, RWG_type* rwg, UByte* split)
{
	RWS_type* rws = rwg->rws;
	xrd_el_type* e = (xrd_el_type*) xr->fElements;
	long answer = 0;
	long words = 0;
	long start = 0;
	for (long i = 0; i < rwg->size; i++)
	{
		RWS_type* s = &rws[i];
		if (s->type == 1)
		{
			if (answer == 0)
			{
				if (s->sym == ' ')
				{
					split[words + 0x10] = rws[i - 1].attr;
					words++;
				}
			}
			else if (0 < answer)
			{
				long x = s->xrStart;
				long last = x + s->xrLen - 1;
				while (x <= last && (e[x].attrib & 4) == 0)
					x++;
				// ROM BUG, kept: what follows the symbol is asked for by its
				// sym (the symbol's first byte) where its type (+2) was
				// meant, so "not at the end of the answer" is nearly always
				// true
				if (x == last && rws[i + 1].sym != 3 && rws[i + 1].sym != 4)
				{
					long bit = i - start;
					split[answer * 3 + bit / 8] |= (UByte) (1 << (bit % 8));
					split[answer * 0xc + words + 0x10] = rws[i + 1].sym == ' ' ? rws[i - 1].attr : 0xfd;
					words++;
				}
			}
		}
		else if (s->type == 2)
			start = i + 1;
		else if (s->type == 3 || s->type == 4)
		{
			split[answer * 0xc + words + 0x10] = rws[i - 1].attr;
			words = 0;
			answer++;
			start = i + 1;
		}
	}
}


// ROM 0x0019e6ec FillRecwordSplitInfo__FP11xrdata_typeP7rc_typeP8RWG_typeP10rec_w_typePv
// The split information for the readings (above), when the best has
// spaces in it: which strokes each of its words is and where the others'
// words end.  ==> the block (the caller frees it); nil for no reading, no
// memory, or the reading's letters not traced back to the strokes - and
// then the low level's letter ends are cleared.
UByte*
FillRecwordSplitInfo(xrdata_type* xr, rc_type* rc, RWG_type* rwg, rec_w_type* readings, void* chunk)
{
	xrd_el_type* elements = (xrd_el_type*) xr->fElements;
	const PS_point_type* trace = (const PS_point_type*) rc->fTrace;
	long points = (short) RCGetH(rc, 0x96);
	UByte ends[12];
	UByte* split = nil;
	if (readings[0].fWord[0] != 0)
	{
		long words = 1;
		for (char* s = HWRStrChr((char*) readings[0].fWord, ' '); s != nil; s = HWRStrChr(s + 1, ' '))
		{
			ends[words - 1] = (UByte) (s - (char*) readings[0].fWord - 1);
			words++;
		}
		ends[words - 1] = (UByte) (HWRStrLen((char*) readings[0].fWord) - 1);
		long strokes = 0;
		for (long i = 1; i < points; i++)
			if (TraceY(trace, i) < 0)
				strokes++;
		ULong size = strokes + 0x5b;
		split = (UByte*) HWRMemoryAlloc(size);
		if (split != nil)
		{
			memset(split, 0, size);
			split[0xf] = (UByte) words;
			if (split[0xf] == 1)
				return split;
			long claimed;
			if (chunk != nil && ((ChunkCtx*) chunk)->fNumbers != 0)
			{
				// a number read by the digit reader: its characters are the
				// digit reader's, one box each (ChunkSortAnswers), so each word
				// takes the strokes of its characters' boxes, and every
				// reading's word ends are marked (and its +0x4a's low byte
				// kept for each word)
				const tagNumBox* nb = (const tagNumBox*) ((ChunkCtx*) chunk)->fData2;
				claimed = 0;
				long next = 0;
				for (long w = 1; w <= words; w++)
				{
					long end = ends[w - 1];
					for (long r = 0; r < 5 && readings[r].fWord[0] != 0; r++)
					{
						split[r * 3 + (end >> 3)] |= (UByte) (1 << (end & 7));
						split[0xf + r * 0xc + w] = readings[r].fX4A[1];
					}
					for (; next <= end; next++)
						if (AddStrokesOfSymbol(XrGetH(nb[next].fFirstPoint), XrGetH(nb[next].fLastPoint), claimed, w - 1, rc, split) == 0)
							goto failed;
					next = end + 1;
					claimed += split[0x4c + w - 1];
				}
				if (xr->fLength != 0)
					elements[xr->fLength - 1].attrib |= 4;
			}
			else
			{
				Osokin_output letters;
				if (connect_trajectory_and_answers(elements, &readings[0], &letters) != 0)
					goto failed;
				long next = 0;
				claimed = 0;
				long part = 0;
				long xrs = 0;
				for (long w = 1; w <= words; w++)
				{
					long end = ends[w - 1];
					split[end >> 3] |= (UByte) (1 << (end & 7));
					for (long l = next; l <= end; l++)
					{
						long until = part + letters.count[l];
						for (long p = part; p < until; p++)
							if (AddStrokesOfSymbol(XrGetH(letters.parts[p].beg), XrGetH(letters.parts[p].end), claimed, w - 1, rc, split) == 0)
								goto failed;		// ROM BUG, kept: the stretches (letters.parts) are not freed
						part += letters.count[l];
						xrs += readings[0].fX30[l];
					}
					elements[xrs].attrib |= 4;
					next = end + 2;
					claimed += split[0x4c + w - 1];
				}
				HWRMemoryFree((Ptr) letters.parts);
				FillSplitInfoFromRWG(xr, rwg, split);
			}
			if (claimed != strokes)
			{
				for (long s = 1; s <= strokes; s++)
				{
					long j;
					for (j = 0; j < claimed; j++)
						if (split[0x58 + j] == s)
							break;
					if (j == claimed)
						AttachLostStrokeToWord(s, rc, split);
				}
			}
			return split;
		}
	}
	goto cleared;
failed:
	if (split != nil)
		HWRMemoryFree((Ptr) split);
cleared:
	for (long i = 0; i < xr->fLength && i < 0x78; i++)
		elements[i].attrib &= ~4;
	return nil;
}
