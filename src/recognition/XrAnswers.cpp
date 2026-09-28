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
	0x00338158-0x003383c0); each function cites its origin.
*/

#include "XrReader.h"
#include "XrDomains.h"
#include "CursiveReader.h"
#include "LowLevel.h"

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
			if (chunk != nil)
			{
				// NOT YET RECONSTRUCTED: the digit reader's words (the chunk's
				// +0x14 and +0x08, 0x0019e9e8-0x0019eb64) - the chunk is
				// always nil while the digit reader is not reconstructed
				claimed = 0;
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
