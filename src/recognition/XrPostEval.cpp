/*
	File:		XrPostEval.cpp

	Contains:	The cursive reader's answers scored again: each letter of
				the word graph by the letter table's rules and the side
				reasoning, and the answers' weights moved by what that
				comes to.  See XrPost.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.
*/

#include "XrPost.h"
#include "XrDomains.h"
#include "CursiveReader.h"
#include "LowLevel.h"
#include "ParaGraph.h"
#include "WordSegment.h"
#include <string.h>
#include <stdio.h>

static inline xrd_el_type*	XrAt(xrdata_type* xr, long i)	{ return (xrd_el_type*) xr->fElements + i; }

// the ARM's 32-bit division (__rt_sdiv): a/b, rounded towards nought
static inline long	SDiv(long a, long b)	{ return (int32_t) a / (int32_t) b; }

// the side-reasoning tables (XR_TO_LETTERS, LETTERS_TO_XR): three strings,
// a penalty and the band of heights the xrs must be in
struct SideTable
{
	const char*	s0;					// +00  the xrs (X2L) or the letters (L2X)
	const char*	s1;					// +04
	const char*	s2;					// +08  (nil for none)
	short		penalty;			// +0c
	UByte		minHeight;			// +0e
	UByte		maxHeight;			// +0f
};

static SideTable
MakeSideTable(const char* const* strings, const unsigned char* tail)
{
	SideTable t = { strings[0], strings[1], strings[2], (short) ((tail[0] << 8) | tail[1]), tail[2], tail[3] };
	return t;
}


/*------------------------------------------------------------------------------
	S i d e   r e a s o n i n g
------------------------------------------------------------------------------*/

// ROM 0x0033695c EvaluateXrToLetters__FPcP11xrdata_typeP13XR_TO_LETTERS
// A word whose xrs include a mark of the table's (at a height in its
// band) but none of whose letters is one that makes such a mark - or
// with two or more such marks more than letters - is charged the
// table's penalty.
static long
EvaluateXrToLetters(const UByte* word, xrdata_type* xr, const SideTable* t)
{
	long result = 0;
	long marks = 0;
	if (t->s0[0] == 0)
		return 0;
	const xrd_el_type* base = (const xrd_el_type*) xr->fElements;
	for (long i = 0; t->s0[i] != 0; i++)
	{
		if (base[0].type == 0)
			continue;
		for (const xrd_el_type* el = base; el->type != 0; el++)
			if ((UByte) t->s0[i] == el->type && t->minHeight <= el->height && el->height <= t->maxHeight)
				marks++;
	}
	if (marks > 0)
	{
		long letters = 0;
		for (long i = 0; i < 0x18 && word[i] != 0; i++)
			if (HWRStrChr(t->s1, OSToRec(word[i])) != nil)
				letters++;
		if (t->s2 != nil)
			for (long i = 0; i < 0x18 && word[i] != 0; i++)
				if (HWRStrChr(t->s2, OSToRec(word[i])) != nil)
					letters++;
		if (letters == 0 || marks - letters > 1)
			result = (short) -t->penalty;
	}
	return result;
}


// ROM 0x003368f4 EvaluateWordUsingSideReasoning__FPcP11xrdata_type
// ROM QUIRK: the second table's answer is worked out and thrown away.
long
EvaluateWordUsingSideReasoning(const UByte* word, xrdata_type* xr)
{
	SideTable t0 = MakeSideTable(kXrToLetters0, kXrToLetters0Tail);
	SideTable t1 = MakeSideTable(kXrToLetters1, kXrToLetters1Tail);
	long result = EvaluateXrToLetters(word, xr, &t0);
	EvaluateXrToLetters(word, xr, &t1);
	return result;
}


// ROM 0x00336aa4 (unnamed) - LetterReadsXrOf
// Whether one of the xrs the symbol's prototype read (not skipped) is of
// one of the types.
static long
LetterReadsXrOf(short sym, RWG_PPD_type* ppd, xrdata_type* xr, const char* types)
{
	const xrd_el_type* base = (const xrd_el_type*) xr->fElements;
	for (long j = 0; ppd[sym].el[j][1] != 0; j = (short) (j + 1))
	{
		if (ppd[sym].el[j][1] != 3)
			continue;
		long k = FindXrIndex(ppd, sym, j);
		if (k < 0)
			continue;
		for (long i = 0; types[i] != 0; i = (short) (i + 1))
			if ((UByte) types[i] == base[k].type)
				return 1;
	}
	return 0;
}


// ROM 0x00336ce8 FindMinMaxXrIndex__FPsPA13_15RWG_PPD_el_typeN21
// The first and last xrs the symbols (-1 ending them) were read from.
static long
FindMinMaxXrIndex(short* syms, RWG_PPD_type* ppd, short* lo, short* hi)
{
	*lo = 0x7fff;
	*hi = -1;
	for (long s = 0; syms[s] != -1; s = (short) (s + 1))
		for (long j = 0; ppd[syms[s]].el[j][1] != 0; j = (short) (j + 1))
		{
			long k = FindXrIndex(ppd, syms[s], j);
			if (k < 0)
				continue;
			if (*hi < k)
				*hi = (short) k;
			if (k < *lo)
				*lo = (short) k;
		}
	return *lo != 0x7fff && *hi != -1;
}


// ROM 0x00336e0c BoxesXOverlapOK__FP11xrdata_typeP13LETTERS_TO_XRP5_RECTsPsT5
// Whether the i'th xr (at a height in the table's band) lies over the
// box: its middle left of the box's right, or near enough the box's
// middle across for how far it is from it up and down.
static long
BoxesXOverlapOK(xrdata_type* xr, const SideTable* t, _RECT* box, short i, short* x, short* y)
{
	if (i >= 0x78)
		return 0;
	xrd_el_type* el = XrAt(xr, i);
	if (el->type == 0 || el->height < t->minHeight || el->height > t->maxHeight)
		return 0;
	_RECT r;
	GetTraceBox(x, y, XrGetH(el->begpoint), XrGetH(el->endpoint), &r);
	long mid = (r.right + r.left) >> 1;
	if (box->right > mid)
		return 1;
	long across = HWRAbs(mid - ((box->right + box->left) >> 1));
	long down = HWRAbs(((r.top + r.bottom) >> 1) - ((box->top + box->bottom) >> 1));
	if (across >= down >> 1)
		return 0;
	return 1;
}


// ROM 0x00336f14 SmthAboveOrBelowAtNeededHeight__FP12_POST_PARAMSsP13LETTERS_TO_XRPs
// Whether the xr after the letter's last (i) - or the one after that -
// is over the letter at the table's height, or a break after a top no
// lower than the letter's own top.
static long
SmthAboveOrBelowAtNeededHeight(POST_PARAMS* pp, short i, const SideTable* t, short*)
{
	xrd_el_type* base = (xrd_el_type*) pp->xr->fElements;
	UByte type = base[i].type;
	if (type == 0x36 || type == 0x34 || type == 0x3a || type == 0x3b)
	{
		if (i <= 0)
			return 0;
		UByte before = base[i - 1].type;
		if (before == 0x36 || before == 0x34 || before == 0x3a || before == 0x3b)
			return 0;
	}
	if (i >= 0x76)
		return 0;
	xrd_el_type* el = base + i;
	if (el[1].type == 0 || el[2].type == 0)
		return 0;
	_RECT box;
	int err;
	if (FindXrLetterBox(0, pp, &box, 1, &err) == 0)
		return 0;
	if (i > 0 && X_IsBreak(el))
	{
		UByte before = el[-1].type;
		if ((before == 0x12 || before == 0x10 || before == 0xf || before == 0x23)
		 && XrGetH(el[-1].box + kXrTop) <= box.top)
			return 1;
	}
	if (BoxesXOverlapOK(pp->xr, t, &box, (short) (i + 1), pp->x, pp->y)
	 || BoxesXOverlapOK(pp->xr, t, &box, (short) (i + 2), pp->x, pp->y))
		return 1;
	return 0;
}


// ROM 0x00337078 EvaluateLettersToXr__FUcP12_POST_PARAMSP13LETTERS_TO_XR
// A letter of the table's (an i, a j, a colon...) that needs a mark
// (the second string) and has none among its xrs, its neighbours' or the
// one before them, nor anything over it, is charged the table's penalty
// - less, for a letter of more than three xrs.
static long
EvaluateLettersToXr(UByte c, POST_PARAMS* pp, const SideTable* t)
{
	short result = 0;
	xrd_el_type* base = (xrd_el_type*) pp->xr->fElements;
	if (HWRStrChr(t->s0, c) == nil)
		return 0;
	if (LetterReadsXrOf(pp->cur, pp->ppd, pp->xr, t->s2) != 0)
		return 0;
	short syms[2] = { pp->cur, -1 };
	short lo, hi, oLo, oHi;
	if (!FindMinMaxXrIndex(syms, pp->ppd, &lo, &hi))
		return 0;
	short last = hi;
	long n = hi - lo + 1;
	if (pp->noPrev == 0 && FindMinMaxXrIndex(pp->prevSyms, pp->ppd, &oLo, &oHi))
	{
		if (oLo < lo)
			lo = oLo;
		if (oHi > hi)
			hi = oHi;
	}
	if (pp->noNext == 0 && FindMinMaxXrIndex(pp->nextSyms, pp->ppd, &oLo, &oHi))
	{
		if (oLo < lo)
			lo = oLo;
		if (oHi > hi)
			hi = oHi;
	}
	if (lo > hi || lo < 0 || hi > 0x77)
		return 0;
	if (lo > 0)
		lo = (short) (lo - 1);
	for (short i = lo; i <= hi; i = (short) (i + 1))
		for (short k = 0; t->s1[k] != 0; k = (short) (k + 1))
			if ((UByte) t->s1[k] == base[i].type && t->minHeight <= base[i].height && base[i].height <= t->maxHeight)
				return result;
	if (pp->noNext == 0 && SmthAboveOrBelowAtNeededHeight(pp, last, t, &result) != 0)
		return result;
	result = (short) (result - (UShort) t->penalty);
	if (n > 3)
		result = (short) SDiv(-(n / 2 - result * 4), n);
	return result;
}


// ROM 0x00336934 EvaluateLetterUsingSideReasoning__FUcP12_POST_PARAMS
long
EvaluateLetterUsingSideReasoning(UByte c, POST_PARAMS* pp)
{
	SideTable t = MakeSideTable(kLettersToXr, kLettersToXrTail);
	return (short) EvaluateLettersToXr(c, pp, &t);
}


// ROM 0x00337398 GetCorr__FPUci
// The n'th nibble.
static long
GetCorr(const UByte* p, long n)
{
	if ((n % 2) == 0)
		return p[n / 2] >> 4;
	return p[n / 2] & 0xf;
}


// ROM 0x00087f68 GetVarOfChar__FUcT1P8xrp_typePv
// The variant's prototype elements (0x4c bytes each) copied out, a clear
// one after them.  ==> how many.
static long
GetVarOfChar(UByte sym, UByte var, UByte* out, DTIHeader* dti)
{
	dte_sym_header_type* d;
	long v = GetSymDescriptor(OSToRec(sym), var, &d, dti);
	if (v < 0)
		return 0;
	UByte* el = d + 0x54;
	for (long i = 0; i < v && i < 16; i++)
		el += d[4 + i] * 0x4c;
	long n = d[4 + v];
	memcpy(out, el, n * 0x4c);
	memset(out + n * 0x4c, 0, 0x4c);
	return n;
}


// ROM 0x003373c8 EvaluateMissingCross__FP12_POST_PARAMSiT2
// The crosses and dots (0x36, 0x3a, 0x3b) the letters of an answer
// expect against those the word has: twice the difference, at most six,
// as a penalty.
long
EvaluateMissingCross(POST_PARAMS* pp, long from, long count)
{
	long strong = 0, expected = 0, found = 0, pen = 0;
	RWS_type* rws = pp->rws;
	RWG_PPD_type* ppd = pp->ppd;
	xrd_el_type* base = (xrd_el_type*) pp->xr->fElements;
	UByte prototype[988];
	long end = from + count;
	if (from >= end)
		return 0;
	for (long s = from; s < end; s++)
	{
		if (rws[s].type != 1)
			continue;
		long n = GetVarOfChar(rws[s].realSym, rws[s].var, prototype, (DTIHeader*) pp->rc->fDTI);
		for (long j = 0; j < n; j++)
		{
			UByte* el = prototype + j * 0x4c;
			if (el[0] == 0x36 || el[0] == 0x3a || el[0] == 0x3b)
			{
				expected++;
				if (el[3] > 2)
					strong++;
			}
			else if ((GetCorr(el + 4, 0x36) > 3 || GetCorr(el + 4, 0x3a) > 3 || GetCorr(el + 4, 0x3b) > 3)
					 && ppd[s].el[j][1] == 3)
			{
				UByte t = base[ppd[s].el[j][0]].type;
				if (t == 0x36 || t == 0x3a || t == 0x3b)
					expected++;
			}
		}
	}
	if (expected != 0)
	{
		for (long i = 0; i < pp->xr->fLength; i++)
		{
			UByte t = base[i].type;
			if (t == 0x36 || t == 0x3a || t == 0x3b)
				found++;
		}
		if (found < strong)
			pen = strong - found;
		else if (expected < found)
			pen = found - expected;
		pen *= 2;
		if (pen > 6)
			pen = 6;
	}
	return -pen;
}


// ROM 0x0033b920 CalculateBoxes_Side_Result__FsT1P12_POST_PARAMS
// A letter (not a mark, nor t or f) whose box does not stand clear to
// the right of the letter before's: -4 when it is wider than two thirds
// of its height and sits over it, -1 when only a third, nought otherwise.
long
CalculateBoxes_Side_Result(short c, short prev, POST_PARAMS* pp)
{
	static const char kSkip[] = "'\";:,.tTfF";
	long result = 0;
	_RECT box, before;
	int err;
	if (HWRStrChr(kSkip, c & 0xff) != nil)
		return 0;
	if (prev != -1 && HWRStrChr(kSkip, prev & 0xff) != nil)
		return 0;
	if (FindXrLetterBox(0, pp, &box, 1, &err) == 0)
		return 0;
	if (FindXrLetterBox(-1, pp, &before, 1, &err) == 0)
		return 0;
	if (xHardOverlapRect(&box, &before, 0) == 0
	 && (((before.right + before.left) >> 1) - ((before.right - before.left + 2) >> 2)) <= ((box.right + box.left) >> 1))
		return 0;
	if (pp->rwg->type == 1)
	{
		short syms[2] = { pp->cur, -1 };
		if (TooManyStrongElems(pp->ppd, pp->xr, syms) && TooManyStrongElems(pp->ppd, pp->xr, pp->prevSyms))
			return 0;
	}
	long height = box.bottom - box.top;
	long width = box.right - box.left;
	if (SDiv(1 + height * 2, 3) <= width && xHardOverlapRect(&box, &before, 1) != 0)
		return -4;
	if (SDiv(1 + (before.right - before.left) * 2, 3) + before.left >= (box.right + box.left) >> 1)
		return -4;
	if (SDiv(height + 1, 3) <= width)
		result = -1;
	return result;
}


// NOT YET RECONSTRUCTED: CheckDiacriticsDirections (0x0007c9a0) - the directions of the strokes over letters that
// carry diacritical marks (AnalyseDiacriticsDirection 0x0007c130,
// CurvFromSquare, LengthOfTraj), which EvaluateCharQuality asks only for
// a field whose language (rc +6) has bit 2 or 3 set - a French or German
// letter set; the U.S. ROM's letter sets have neither, so it is never
// reached from here.  Answers nought (nothing to charge).
long
CheckDiacriticsDirections(POST_PARAMS*, rec_w_type*, xrdata_type*, short, short* result)
{
	*result = 0;
	return 0;
}


/*------------------------------------------------------------------------------
	T h e   l e t t e r s
------------------------------------------------------------------------------*/

// the last symbol of each alternative of a group, going back from `from`
// to the group's start, into the list (at most ten)
static void
CollectBack(RWS_type* rws, long from, short* list)
{
	long n = 0;
	for (long i = from; rws[i].type != 2; i = (short) (i - 1))
		if (rws[i].type == 1 && rws[i + 1].type != 1)
		{
			if (n > 9)
				break;
			list[n] = (short) i;
			n = (short) (n + 1);
		}
}

// the first symbol of each alternative of a group, going on from `from`
// to the group's end
static void
CollectOn(RWS_type* rws, long from, short* list)
{
	long n = 0;
	for (long i = from; rws[i].type != 3; i = (short) (i + 1))
		if (rws[i].type == 1 && rws[i - 1].type != 1)
		{
			if (n > 9)
				break;
			list[n] = (short) i;
			n = (short) (n + 1);
		}
}


// ROM 0x00335fe0 EvaluateCharQuality__FP12_POST_PARAMS
// One letter of the graph (pp->cur) scored by its rules: its own, and
// the ones for the letter before and after it; four times their weighed
// sum over the number of queues, plus four times each side reasoning's.
// A space is nought; a semicolon inside a one-answer graph's word -400.
long
EvaluateCharQuality(POST_PARAMS* pp)
{
	RWG_type* rwg = pp->rwg;
	RWS_type* rws = pp->rws;
	long cur = pp->cur;
	short prevList[11], nextList[11];
	pp->queues = 0;
	pp->prevSyms = prevList;
	pp->nextSyms = nextList;
	for (long i = 0; i < 11; i++)
		prevList[i] = nextList[i] = -1;
	short prevChar = -1, nextChar = -1;
	long prevVar = -1, nextVar = -1;
	long noPrev, noNext;

	// the letter before
	if (cur == 0)
		noPrev = 1;
	else
	{
		UByte t = rws[cur - 1].type;
		if (rwg->type == 1)
		{
			if (t != 1)
				noPrev = 1;
			else
			{
				noPrev = 0;
				if (OSToRec(rws[cur - 1].sym) != 1)
				{
					prevChar = OSToRec(rws[cur - 1].realSym);
					prevVar = rws[cur - 1].var;
				}
				prevList[0] = (short) (cur - 1);
			}
		}
		else if (t == 1 || t == 3)
		{
			noPrev = 0;
			if (t == 1)
			{
				prevChar = OSToRec(rws[cur - 1].realSym);
				prevVar = rws[cur - 1].var;
				prevList[0] = (short) (cur - 1);
			}
			else
				CollectBack(rws, (short) (cur - 1), prevList);
		}
		else
		{
			long i = cur;
			while (i > 0 && rws[i].type != 2)
				i = (short) (i - 1);
			if (i == 0)
				noPrev = 1;
			else
			{
				noPrev = 0;
				if (rws[i - 1].type == 1)
				{
					prevChar = OSToRec(rws[i - 1].realSym);
					prevVar = rws[i - 1].var;
					prevList[0] = (short) (i - 1);
				}
				else
					CollectBack(rws, (short) (i - 1), prevList);
			}
		}
	}

	// the letter after
	long next = cur + 1;
	if (rwg->size <= next)
		noNext = 1;
	else
	{
		UByte t = rws[cur + 1].type;
		if (rwg->type == 1)
		{
			if (t != 1)
				noNext = 1;
			else
			{
				if (OSToRec(rws[cur + 1].sym) != 1)
				{
					nextChar = OSToRec(rws[cur + 1].realSym);
					nextVar = rws[cur + 1].var;
				}
				noNext = 0;
				nextList[0] = (short) next;
			}
		}
		else if (t == 1 || t == 2)
		{
			noNext = 0;
			if (t == 2)
				CollectOn(rws, (short) next, nextList);
			else
			{
				nextChar = OSToRec(rws[cur + 1].realSym);
				nextVar = rws[cur + 1].var;
				nextList[0] = (short) next;
			}
		}
		else
		{
			long i = cur;
			while (i < rwg->size - 1 && rws[i].type != 3)
				i = (short) (i + 1);
			if (rwg->size - 1 == i)
				noNext = 1;
			else
			{
				noNext = 0;
				if (rws[i + 1].type != 1)
					CollectOn(rws, (short) (i + 1), nextList);
				else
				{
					nextChar = OSToRec(rws[i + 1].realSym);
					nextVar = rws[i + 1].var;
					nextList[0] = (short) (i + 1);
				}
			}
		}
	}

	short c = OSToRec(rws[cur].realSym);
	short var = rws[cur].var;
	if (c == ';')
	{
		if (rwg->type != 1 && noNext == 0)
			return -400;
	}
	else if (c == ' ')
		return 0;
	if (prevChar == ' ')
	{
		prevChar = -1;
		noPrev = 1;
		prevVar = -1;
		prevList[0] = -1;
	}
	if (nextChar == ' ')
	{
		nextChar = -1;
		noNext = 1;
		nextVar = -1;
		nextList[0] = -1;
	}

	// the rules
	const UByte* main = ((PDFHeader*) pp->pdf)->fSection0;
	const UByte* own = nil;
	const UByte* before = nil;
	const UByte* after = nil;
	ULong queues = 0;
	if (PDFGetRule(main, c, var, -1, -1, &own))
	{
		queues = (UShort) XrGetH(own);
		if ((pp->flags & 1) == 0)
			own = nil;
	}
	else
		own = nil;
	if (prevChar != -1)
	{
		if (PDFGetRule(main, c, var, prevChar, (short) prevVar, &before))
		{
			queues += (UShort) XrGetH(before);
			if ((pp->flags & 2) == 0)
				before = nil;
		}
		else
			before = nil;
	}
	if (nextChar != -1)
	{
		if (PDFGetRule(main, c, var, nextChar, (short) nextVar, &after))
		{
			queues += (UShort) XrGetH(after);
			if ((pp->flags & 2) == 0)
				after = nil;
		}
		else
			after = nil;
	}
	long floor = queues << 4;
	ResetChangePPDLetterInfo(pp);
	pp->noPrev = noPrev;
	pp->noNext = noNext;
	pp->curChar = (UByte) c;
	pp->prevChar = (UByte) prevChar;
	pp->nextChar = (UByte) nextChar;
	long result = 0;
	if (own != nil)
	{
		pp->isPrev = 0;
		pp->isNext = 0;
		result = CalculateGroupResult(pp, own, floor);
	}
	if (before != nil)
	{
		pp->isNext = 0;
		pp->isPrev = 1;
		result += CalculateGroupResult(pp, before, floor);
	}
	if (after != nil)
	{
		pp->isPrev = 0;
		pp->isNext = 1;
		result += CalculateGroupResult(pp, after, floor);
	}
	long rules = result;
	if (queues != 0)
		result = SDiv(result * 4, (long) queues);
	long side = 0, boxes = 0;
	if ((pp->flags & 4) != 0 && (RCGetH(pp->rc, 0x90) & 0x800) == 0)
		result += (side = EvaluateLetterUsingSideReasoning(OSToRec(rws[cur].realSym), pp)) * 4;
	if ((pp->flags & 1) != 0 && (RCGetH(pp->rc, 6) & 0xc) != 0)
	{
		short diacritics = 0;
		if (CheckDiacriticsDirections(pp, nil, pp->xr, 0, &diacritics) != 0)
			result -= diacritics * 4;
	}
	if ((pp->flags & 4) != 0)
		result += (boxes = CalculateBoxes_Side_Result(c, prevChar, pp)) * 4;
	if (TracingCursive())
		fprintf(stderr, "[cursive] post: '%c' after '%c', before '%c': rules %ld over %lu queues (%s%s%s), side %ld, boxes %ld\n",
				c, prevChar == -1 ? '-' : prevChar, nextChar == -1 ? '-' : nextChar, rules, queues,
				own != nil ? "own " : "", before != nil ? "before " : "", after != nil ? "after" : "", side, boxes);
	ApplyChangePPDLetterInfo(pp);
	return result;
}


/*------------------------------------------------------------------------------
	T h e   a n s w e r s
------------------------------------------------------------------------------*/

// ROM 0x002af750 IsDidigitInStr__FPc
static long
IsDidigitInStr(const UByte* s)
{
	for (; *s != 0; s++)
		if (HWRStrChr("0123456789", OSToRec(*s)) != nil)
			return 1;
	return 0;
}


// ROM 0x002af390 FindAverageNumberBoxes__FP10rec_w_typeP11xrdata_typeP5_RECTPUlT4
// The mean top and bottom of the digits of a reading (over the xrs each
// was read from, leaving out breaks), and how much each varies (the mean
// of the squares of their differences from the mean, over at most
// twenty).  ==> whether there were any.
static long
FindAverageNumberBoxes(rec_w_type* reading, xrdata_type* xr, _RECT* box, ULong* topVar, ULong* bottomVar)
{
	static const char kBreaks[] = "\001\002\003\004\005";
	xrd_el_type* base = (xrd_el_type*) xr->fElements;
	box->top = 0;
	box->bottom = 0;
	*topVar = 0;
	*bottomVar = 0;
	long sumTop = 0, sumBottom = 0, n = 0, at = 1, i = 0, ok = 0;
	long tops[20], bottoms[20];
	if (reading->fWord[0] == 0)
		return 0;
	do
	{
		ok = 1;
		long len = (short) reading->fX30[i];
		if (len == 0)
		{
			ok = 0;
			break;
		}
		if ((__ctype[reading->fWord[i]] & 0x20) != 0)
		{
			long top = 0x7fff, bottom = 0;
			long end = (short) (at + len);
			for (long k = at; k < end; k = (short) (k + 1))
			{
				if (HWRStrChr(kBreaks, base[k].type) != nil)
					continue;
				if (XrGetH(base[k].box + kXrTop) < top)
					top = XrGetH(base[k].box + kXrTop);
				if (bottom < XrGetH(base[k].box + kXrBottom))
					bottom = XrGetH(base[k].box + kXrBottom);
			}
			if (top != 0x7fff && bottom != 0)
			{
				sumTop += top;
				sumBottom += bottom;
				if (n < 20)
				{
					tops[n] = top;
					bottoms[n] = bottom;
				}
				n = (short) (n + 1);
			}
		}
		at = (short) (at + len);
		i = (short) (i + 1);
	} while (reading->fWord[i] != 0);
	if (n == 0)
		return 0;
	long meanTop = SDiv(sumTop, n);
	long meanBottom = SDiv(sumBottom, n);
	box->top = (short) meanTop;
	box->bottom = (short) meanBottom;
	ULong sqTop = 0, sqBottom = 0;
	for (long k = 0; k < n && k < 20; k = (short) (k + 1))
	{
		long d = tops[k] - meanTop;
		sqTop += (uint32_t) (d * d);
		d = bottoms[k] - meanBottom;
		sqBottom += (uint32_t) (d * d);
	}
	if (n < 20)
		*topVar = (uint32_t) sqTop / (uint32_t) n;
	else
	{
		*topVar = (uint32_t) sqTop / 20u;
		n = 20;
	}
	*bottomVar = (uint32_t) sqBottom / (uint32_t) n;
	return ok;
}


// ROM 0x002af648 CheckDigitsLine__FP10rec_w_typeP11xrdata_typePs
// A reading with digits whose tops or bottoms wander (more than 0.12 of
// the digits' height, as a root of the mean square): the penalty, 1..6,
// in result.  ==> whether there is one.
long
CheckDigitsLine(rec_w_type* reading, xrdata_type* xr, short* result)
{
	*result = 0;
	_RECT box;
	ULong topVar, bottomVar;
	if (IsDidigitInStr(reading->fWord) != 1 || FindAverageNumberBoxes(reading, xr, &box, &topVar, &bottomVar) != 1)
		return 0;
	long h = HWRAbs(box.bottom - box.top);
	if (h == 0)
		h = 1;
	topVar = (uint32_t) (topVar * 10000) / (uint32_t) (h * h);
	bottomVar = (uint32_t) (bottomVar * 10000) / (uint32_t) (h * h);
	ULong v = topVar;
	if (topVar < 0x91)
		v = bottomVar;
	if (v <= 0x90)
		return 0;
	v = bottomVar;
	if (bottomVar < topVar)
		v = topVar;
	ULong pen = HWRMathILSqrt((long) v) - 10;
	if (pen < 7)
		*result = (short) pen;
	else
		*result = 6;
	return 1;
}




// ROM 0x00337624 EvaluateAnswers__FP12_POST_PARAMSP10rec_w_typeP13POST_CONTROLSPUi
// Each letter of each answer scored (EvaluateCharQuality) and its f0b
// moved by a quarter of the score scaled by how many xrs it spans, the
// answers' weights by the sum (and, with flag 8, by the word's side
// reasoning and missing crosses, which also go into each letter's f08);
// then the readings' aliases made again and a reading with digits that
// do not keep to a line charged for it.  Unless the best answer is
// already sure enough (above the controls' first number and ahead of
// the second by more than their second), or the field turns it off
// (rc +0xaf).  A graph of type 4 is scored as a list with flags 0xe, and
// a list is left as type 4.  ==> 1 when anything was scored.
long
EvaluateAnswers(POST_PARAMS* pp, rec_w_type* readings, const UByte* controls, ULong* multi)
{
	long result = 0;
	RWG_PPD_type* ppd = nil;
	Ptr alist = nil;
	long evaluate = 1;
	rc_type* rc = pp->rc;
	RWG_type* rwg = pp->rwg;
	xrdata_type* xr = pp->xr;
	intptr_t stack[15];
	pp->stack = stack;
	pp->stackBytes = 0x3c;
	pp->x = nil;
	*multi = 0;
	if (((PDFHeader*) pp->pdf)->fSection0 == nil)
		goto done;
	if (readings != nil && rwg->type == 1)
	{
		Boolean digit = false;
		for (long i = 0; i < 0x18 && readings[0].fWord[i] != 0; i++)
			if ((__ctype[readings[0].fWord[i]] & 0x20) != 0)
			{
				digit = true;
				break;
			}
		short attr = (short) ((readings[0].fX4A[0] << 8) | readings[0].fX4A[1]);
		short sure = (short) XrGetH(controls);
		short ahead = (short) XrGetH(controls + 2);
		if (attr >= 0 && !digit
		 && (readings[0].fWeight < sure || ahead < readings[0].fWeight - readings[1].fWeight))
			evaluate = 0;
	}
	if (*RCByte(rc, 0xaf) == 0)
		evaluate = 0;
	if (TracingCursive())
		fprintf(stderr, "[cursive] post: graph type %d, %s (rc +0xaf %d, +0x100 %d %d; weights %d %d, attr %d)\n", rwg->type,
				evaluate ? "scoring the letters" : "not scored", *RCByte(rc, 0xaf), (short) XrGetH(controls), (short) XrGetH(controls + 2),
				readings != nil ? readings[0].fWeight : 0, readings != nil ? readings[1].fWeight : 0,
				readings != nil ? (short) ((readings[0].fX4A[0] << 8) | readings[0].fX4A[1]) : 0);
	if (rwg->type == 4)
	{
		rwg->type = 1;
		pp->flags = 0xe;
	}
	{
		RWS_type* rws = rwg->rws;
		if (rws == nil)
			goto done;
		pp->rws = rws;
		ppd = rwg->ppd;
		if (ppd != nil)
		{
			pp->ppd = ppd;
			if (rwg->size == 0
			 || (rwg->type != 1 && readings != nil && (alist = HWRMemoryAlloc(0xf0)) == nil))
			{
				result = 0;
				goto done;
			}
			if (evaluate != 0)
			{
				short* xy = (short*) HWRMemoryAlloc(pp->nPoints * 4);
				pp->x = xy;
				if (xy != nil)
				{
					pp->y = xy + pp->nPoints;
					trace_to_xy(pp->x, pp->y, pp->nPoints, pp->trace);
					if (rwg->type != 1)
						*multi = 1;
					long i = 0;
					Boolean inAlternatives = false;
					long answer = 0;
					while (i < rwg->size)
					{
						long letters = 0;
						if (rwg->type == 1)
						{
							if (rws[i].type == 2 || rws[i].type == 4)
								i = (short) (i + 1);
							if (rwg->size <= i)
								break;
							while (rws[i + letters].type == 1)
							{
								letters = (short) (letters + 1);
								if (i + letters >= rwg->size)
									break;
							}
							if (letters == 0)
								break;
						}
						long start = i;
						if (rwg->type == 1)
						{
							long total = 0;
							for (long m = 0; m < letters; m = (short) (m + 1))
							{
								pp->cur = (short) i;
								long q = (short) EvaluateCharQuality(pp);
								long quarter = (short) (q / 4);
								total = (short) (total + quarter);
								long move = -quarter;
								if (quarter < -0x7f || (quarter != -0x20 && 0x1f < move))
									move = 0x20;
								move = move * rws[i].xrLen * 10;
								move = move / 32;
								if (move == 0)
								{
									if (quarter < 0)
										move = 1;
									else if (quarter > 0)
										move = -1;
								}
								move += (SByte) rws[i].f0b;
								if (move >= 0x80)
									move = 0x7f;
								else if (move < -0x7f)
									move = -0x7f;
								rws[i].f0b = (UByte) move;
								if (TracingCursive())
									fprintf(stderr, "[cursive] post: '%c' (variant %d) scores %ld, f0b %d\n", rws[i].realSym, rws[i].var, q, (SByte) rws[i].f0b);
								i = (short) (i + 1);
							}
							if (readings != nil)
							{
								if ((pp->flags & 8) != 0)
								{
									long side = (short) EvaluateWordUsingSideReasoning(readings[answer].fWord, xr);
									long both = (short) (EvaluateMissingCross(pp, start, letters) + side);
									for (long j = start; j < start + letters; j++)
										rws[j].f08 = (UByte) both;
									total = (short) (total + both);
								}
								total += readings[answer].fWeight;
								if (total < 1)
									readings[answer].fWeight = 1;
								else
									readings[answer].fWeight = (short) total;
							}
							answer = (short) (answer + 1);
						}
						else
						{
							UByte t = rws[i].type;
							if (t == 2)
								inAlternatives = true;
							else if (t == 3)
								inAlternatives = false;
							else if (t == 1)
							{
								pp->cur = (short) i;
								long q = EvaluateCharQuality(pp);
								long scaled = (int32_t) ((uint32_t) q << 18) >> 16;
								long quarter = (short) (scaled / 4);
								SByte step = (SByte) (scaled / 4);
								if (quarter + (short) rws[i].weight < 1)
									rws[i].weight = 1;
								else
									rws[i].weight = (UByte) ((SByte) rws[i].weight + step);
								if (inAlternatives)
								{
									long j = -1;
									if (rws[i - 1].type == 1)
										j = (short) (i - 1);
									if (rws[i + 1].type == 1)
										j = (short) (i + 1);
									if (j != -1)
									{
										if (quarter + (short) rws[j].weight < 1)
											rws[j].weight = 1;
										else
											rws[j].weight = (UByte) ((SByte) rws[j].weight + step);
									}
								}
							}
							i = (short) (i + 1);
						}
					}
					result = 1;
				}
			}
		}
		if (rwg->type == 1)
		{
			if (readings != nil)
				fill_RW_aliases(readings, rwg);
		}
		else if (readings != nil)
		{
			// NOT YET RECONSTRUCTED: MakeRecWordsFromGraph (0x003383c0) and
			// MergeTwoRecWordsSets - the readings of a graph that is not a
			// list made from it twice (as it is, and with the digits' and
			// + = %'s weights lowered by a hundred) and merged, then the
			// word's side reasoning added to each; such a graph's readings are
			// left as they were
		}
		if (evaluate != 0 && readings != nil)
		{
			UShort kinds = RCGetH(rc, 0x08);
			Boolean check = (kinds & 1) != 0;
			if (!check && (kinds & 2) != 0)
				check = (RCGetH(rc, 0x02) & 1) != 0;
			if (check)
			{
				for (long r = 0; r < 10 && readings[r].fWord[0] != 0; r = (short) (r + 1))
				{
					short pen;
					if (CheckDigitsLine(&readings[r], xr, &pen) == 1)
					{
						readings[r].fWeight = (short) (readings[r].fWeight - pen);
						result = 1;
					}
				}
			}
		}
	}
done:
	if (pp->x != nil)
	{
		HWRMemoryFree((Ptr) pp->x);
		pp->x = nil;
	}
	if (alist != nil)
		HWRMemoryFree(alist);
	if (rwg->type == 1)
		rwg->type = 4;
	return result;
}


// ROM 0x00337ee8 EvaluateAndSortAnswers__FP10rec_w_typeP7rc_typeP11xrdata_typeP8RWG_type
// The graph's answers made into readings (unscaled) and scored again.
// DEVIATION: the parameters are sized by the host (sizeof).
void
EvaluateAndSortAnswers(rec_w_type* readings, rc_type* rc, xrdata_type* xr, RWG_type* rwg)
{
	POST_PARAMS pp;
	memset(&pp, 0, sizeof(pp));
	pp.pdf = (PDFHeader*) ((DTIHeader*) rc->fDTI)->fPDFPtr;
	pp.rc = rc;
	pp.xr = xr;
	pp.trace = (PS_point_type*) rc->fTrace;
	pp.nPoints = (short) RCGetH(rc, 0x96);
	pp.flags = readings == nil ? 3 : 0xf;
	pp.competeOn = 1;
	pp.defineEnds = 0;
	pp.rwg = rwg;
	if (rwg->type == 1 || rwg->type == 4)
		MakeRecWordsFromWordGraph(rwg, readings, 0);
	else
		readings[0].fWord[0] = 0;
	ULong multi;
	EvaluateAnswers(&pp, readings, RCByte(rc, 0x100), &multi);
}
