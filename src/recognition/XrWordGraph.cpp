/*
	File:		XrWordGraph.cpp

	Contains:	The cursive reader's xr reader: the entry point and the
				word graph (see XrReader.h).

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.
*/

#include "XrReader.h"
#include "LowLevel.h"
#include "CursiveReader.h"
#include "XrDomains.h"

#include <string.h>

static inline short	RCSigned(rc_type* rc, ULong offset)	{ return (short) RCGetH(rc, offset); }


// ROM 0x0036227c create_rwg_ppd__FP9xrcm_typeP7rc_typeP11xrdata_typeP8RWG_type
// Which xrs each symbol of the graph was read from: the graph cut where
// its symbols' xr segment changes (each symbol's byte 0x0e), and the
// xrs of each segment - with the word's first xr in front and, when the
// segment does not end at a break, the first xr again after it - read
// against the segment's symbols by create_rwg_ppd_node.  The second
// segment is read with the capitals of the first letter's bits (rc
// +0x1e: 4 -> 1, 8 -> 2).
long
create_rwg_ppd(xrcm_type* x, rc_type* rc, xrdata_type* xr, RWG_type* rwg)
{
	xrdata_type part = { 0, 0, nil };
	UByte starts[24];
	UByte xrStarts[24];
	UShort caps = RCGetH(rc, 0x1e);
	long n;
	RWS_type* rws = rwg->rws;
	if (rws != nil && rwg->ppd == nil && 2 < (n = xr->fLength))
	{
		rwg->ppd = (RWG_PPD_type*) HWRMemoryAlloc((rwg->size + 1) * sizeof(RWG_PPD_type));
		if (rwg->ppd != nil)
		{
			memset(rwg->ppd, 0, (rwg->size + 1) * sizeof(RWG_PPD_type));
			ULong prev = 0xffffffff;
			long k = 0;
			for (long i = 0; i < rwg->size; i++)
			{
				RWS_type* e = &rws[i];
				if (e->type == 1 && e->src != prev)
				{
					starts[k] = (UByte) i;
					xrStarts[k] = e->src;
					prev = e->src;
					k++;
				}
			}
			xrStarts[k] = (UByte) n;
			starts[k] = (UByte) rwg->size;
			AllocXrdata(&part, n + 1);
			if (part.fElements != nil)
			{
				if (1 < k)
					x = nil;
				xrd_el_type* whole = (xrd_el_type*) xr->fElements;
				xrd_el_type* el = (xrd_el_type*) part.fElements;
				memcpy(&el[0], &whole[0], sizeof(xrd_el_type));
				for (long j = 0; j < k; j++)
				{
					long len = xrStarts[j + 1] - xrStarts[j];
					part.fLength = len + 1;
					memcpy(&el[1], &whole[xrStarts[j]], len * sizeof(xrd_el_type));
					memset(&el[xrStarts[j + 1] - xrStarts[j] + 1], 0, sizeof(xrd_el_type));
					len = xrStarts[j + 1] - xrStarts[j];
					if (!XrcmIsBreak(el[len].type))
						memcpy(&el[len + 1], &whole[0], sizeof(xrd_el_type));
					if (j == 1)
					{
						UShort c = RCGetH(rc, 0x1e) & 0xfffc;
						RCSetH(rc, 0x1e, c);
						if ((RCGetH(rc, 0x1e) & 4) != 0)
							RCSetH(rc, 0x1e, RCGetH(rc, 0x1e) | 1);
						if ((RCGetH(rc, 0x1e) & 8) != 0)
							RCSetH(rc, 0x1e, RCGetH(rc, 0x1e) | 2);
					}
					if (rwg->type == 2)
						RCSetH(rc, 0x1e, 5);
					if (create_rwg_ppd_node(x, rc, xrStarts[j] - 1, &part, starts[j], starts[j + 1], rwg) != 0)
						goto fail;
				}
				FreeXrdata(&part);
				RCSetH(rc, 0x1e, caps);
				return 0;
			}
		}
	}
fail:
	FreeXrdata(&part);
	if (rwg->ppd != nil)
	{
		HWRMemoryFree((Ptr) rwg->ppd);
		rwg->ppd = nil;
	}
	RCSetH(rc, 0x1e, caps);
	return 1;
}


// ROM 0x003625c8 create_rwg_ppd_node__FP9xrcm_typeP7rc_typeiP11xrdata_typeN23P8RWG_type
// The symbols from `from` to `to` (a run of alternatives, each a string
// of symbols, 4 between them) each read against the xrs by a traced
// CountWord, and from its layout the xrs each letter read and how -
// consecutive steps on one prototype made one - into the ppd; then each
// symbol's first xr and how many it spans.
long
create_rwg_ppd_node(xrcm_type* x, rc_type* rc, long offset, xrdata_type* xr, long from, long to, RWG_type* rwg)
{
	xrcm_type* matrix = x;
	if (rwg->rws == nil || rwg->ppd == nil || (x == nil && xrmatr_alloc(rc, xr, &matrix) != 0))
		goto fail;
	{
		RWS_type* rws = rwg->rws;
		RWG_PPD_type* ppd = rwg->ppd;
		long i = from;
		if (rws[from].type == 2)
			i = from + 1;
		UByte off = (UByte) offset;
		long iterations = 0;
		long n;
		do
		{
			n = 0;
			while (i + n < to && rws[i + n].type == 1)
			{
				matrix->word[n] = rws[i + n].sym;
				n++;
				if (!(n < 0x18))
					break;
			}
			matrix->word[n] = 0;
			SetInitialLine(4, matrix);
			if (CountWord((const UByte*) matrix->word, matrix->caps, matrix->flags | 4, matrix) != 0
			 || matrix->layout == nil)
				goto fail;
			for (long j = 0; j < n; j++)
			{
				XrLayoutLetter* l = matrix->layout->letters[j];
				RWS_type* e = &rws[i + j];
				e->var = l->var;
				e->realSym = l->sym;
				e->letWeight = (UByte) matrix->weights[j];
				ULong prevXrp = 0xffffffff;
				Boolean same = false;
				// DEVIATION: a first step reached by skipping an xr (neither 2
				// nor 3) takes whatever the caller left in the register the
				// ROM keeps the type in; the host has nothing there
				UByte type = 0;
				long count = 0;
				UByte* p = ppd[i + j].el[0];
				for (long k = 0; k < l->count; k++)
				{
					UByte* step = l->steps[k];
					if (step[1] == prevXrp)
						same = true;
					else
					{
						if (step[2] == 3)
							type = 3;
						else if (step[2] == 2)
							type = same ? 1 : 2;
						p[count * 4] = (UByte) (step[0] + off);
						p[count * 4 + 1] = type;
						prevXrp = step[1];
						count++;
						same = false;
					}
				}
				ppd[i + j].el[count][1] = 0;
			}
			FreeLayout(matrix);
		} while (rws[i + n].type == 4
			  && (i = i + n + 1) < to
			  && i < rwg->size
			  && ++iterations < 10);
		// DEVIATION: the ROM tests a stack word it has not set for the first
		// symbol (whether the one before was no symbol); the host takes it
		// as set - the first symbol starts at the segment's first xr
		long afterBreak = 1;
		for (long p = from; p < to; p++)
		{
			RWS_type* e = &rws[p];
			if (e->type == 1)
			{
				ULong start = offset + 1;
				if (afterBreak == 0)
				{
					start = ppd[p].el[0][0];
					for (long k = 0; k < 0xc; k++)
					{
						UByte t = ppd[p].el[k][1];
						if (t == 0)
							break;
						if (t != 2)
						{
							start = ppd[p].el[k][0];
							break;
						}
					}
				}
				long k = 0;
				while (k < 0xc && ppd[p].el[k][1] != 0)
					k++;
				ULong last = k < 1 ? 0 : ppd[p].el[k - 1][0];
				e->xrStart = (UByte) start;
				e->xrLen = (long) (last - start) < 0 ? 0 : (UByte) (last - start + 1);
				afterBreak = 0;
			}
			else
				afterBreak = 1;
		}
		if (x == nil)
			xrmatr_dealloc(&matrix);
		return 0;
	}
fail:
	FreeLayout(matrix);
	if (x == nil)
		xrmatr_dealloc(&matrix);
	return 1;
}


// ROM 0x00362990 FreeRWGMem__FP8RWG_type
long
FreeRWGMem(RWG_type* rwg)
{
	if (rwg != nil)
	{
		if (rwg->rws != nil)
		{
			HWRMemoryFree((Ptr) rwg->rws);
			rwg->rws = nil;
		}
		if (rwg->ppd != nil)
		{
			HWRMemoryFree((Ptr) rwg->ppd);
			rwg->ppd = nil;
		}
	}
	return 0;
}


// ROM 0x003629d8 fill_RW_aliases__FPA10_10rec_w_typeP8RWG_type
// What the graph says of each symbol written into the readings: its
// variant (bit 7: read in the other case) and its letter weight.  A
// graph of one answer (type 2) fills the first reading and copies it to
// the others; a list of answers (1 or 4) one reading per answer.
long
fill_RW_aliases(rec_w_type* readings, RWG_type* rwg)
{
	if (rwg->type == 2)
	{
		RWS_type* rws = rwg->rws;
		if (rws == nil)
			return 1;
		long k = 0;
		Boolean start = true;
		for (long i = 0; i < rwg->size; i++)
		{
			RWS_type* e = &rws[i];
			if (e->type == 2)
				start = true;
			else if (e->type == 1 && start)
			{
				readings[0].fVariants[k] = e->var;
				readings[0].fX30[k] = e->xrLen;
				if (readings[0].fWord[k] != e->realSym)
					readings[0].fVariants[k] |= 0x80;
				k++;
				start = false;
			}
		}
		for (long r = 1; r < 10; r++)
		{
			if (readings[r].fWord[0] == 0)
				break;
			memcpy(readings[r].fX30, readings[0].fX30, 0x18);
			memcpy(readings[r].fVariants, readings[0].fVariants, 0x18);
		}
	}
	if (rwg->type == 1 || rwg->type == 4)
	{
		RWS_type* rws = rwg->rws;
		if (rws == nil)
			return 1;
		long i = 0;
		long r = 0;
		do
		{
			if (rwg->size <= i)
				break;
			if (rws[i].type == 1)
			{
				long k = 0;
				do
				{
					RWS_type* e = &rws[i];
					readings[r].fVariants[k] = e->var;
					readings[r].fX30[k] = e->xrLen;
					readings[r].fWord[k] = e->sym;
					// ROM BUG, kept: a letter read in another letter's case
					// clears the reading's first span rather than its own
					if (ToLower(readings[r].fWord[k]) != ToLower(e->realSym))
						readings[r].fX30[0] = 0;
					if (readings[r].fWord[k] != e->realSym)
						readings[r].fVariants[k] |= 0x80;
					k++;
					i++;
				} while (k < 0x18 && rws[i].type == 1);
				r++;
			}
			i++;
		} while (r < 10);
	}
	return 0;
}


// ROM 0x00362bf8 GetCMPAliases__FP11xrdata_typeP8RWG_typePcP7rc_type
// The graph of one fixed answer: the word, each letter a symbol, its
// xrs found by create_rwg_ppd.
long
GetCMPAliases(xrdata_type* xr, RWG_type* rwg, const char* word, rc_type* rc)
{
	rwg->rws = nil;
	rwg->ppd = nil;
	rwg->type = 1;
	rwg->size = HWRStrLen(word);
	RWS_type* rws = (RWS_type*) HWRMemoryAlloc((rwg->size + 1) * sizeof(RWS_type));
	rwg->rws = rws;
	if (rws != nil)
	{
		memset(rws, 0, (rwg->size + 1) * sizeof(RWS_type));
		for (long i = 0; i < rwg->size; i++)
		{
			rws[i].sym = word[i];
			rws[i].weight = 100;
			rws[i].src = 1;
			rws[i].type = 1;
		}
		if (create_rwg_ppd(nil, rc, xr, rwg) == 0 && rwg->ppd != nil)
			return 0;
		if (rwg->rws != nil)
		{
			HWRMemoryFree((Ptr) rwg->rws);
			rwg->rws = nil;
		}
	}
	if (rwg->ppd != nil)
	{
		HWRMemoryFree((Ptr) rwg->ppd);
		rwg->ppd = nil;
	}
	return 1;
}


// ROM 0x00362d00 SortGraph__FPA10_iP8RWG_type
// The graph's alternatives put into the order given (a list of their
// numbers), 4 between them.
long
SortGraph(int (*order)[10], RWG_type* rwg)
{
	RWS_type* rws = rwg->rws;
	RWG_PPD_type* ppd = rwg->ppd;
	if (rws == nil || ppd == nil)
		return 1;
	RWS_type* newRws = (RWS_type*) HWRMemoryAlloc(rwg->size * sizeof(RWS_type));
	RWG_PPD_type* newPpd = (RWG_PPD_type*) HWRMemoryAlloc(rwg->size * sizeof(RWG_PPD_type));
	if (newRws != nil && newPpd != nil)
	{
		long alternatives = 0;
		for (long i = 1; i < rwg->size; i++)
			if (rws[i].type != 1)
				alternatives++;
		long out = 1;
		for (long a = 0; a < alternatives; a++)
		{
			long start = 1;
			long seen = 0;
			for (long i = 1; i < rwg->size; i++)
			{
				if (rws[i].type != 1)
				{
					if ((*order)[a] == seen)
					{
						long len = i - start;
						memcpy(&newRws[out], &rws[start], len * sizeof(RWS_type));
						memcpy(&newPpd[out], &ppd[start], len * sizeof(RWG_PPD_type));
						len += out;
						memset(&newPpd[len], 0, sizeof(RWG_PPD_type));
						out = len + 1;
						newRws[len].type = 4;
						break;
					}
					seen++;
					start = i + 1;
				}
			}
		}
		newRws[0] = rws[0];
		newRws[out - 1] = rws[rwg->size - 1];
		rwg->size = out;
		rwg->rws = newRws;
		rwg->ppd = newPpd;
		HWRMemoryFree((Ptr) rws);
		HWRMemoryFree((Ptr) ppd);
		return 0;
	}
	if (newRws != nil)
		HWRMemoryFree((Ptr) newRws);
	if (newPpd != nil)
		HWRMemoryFree((Ptr) newPpd);
	return 1;
}


// ROM 0x00362f08 xrw_algs__FP11xrdata_typePA10_10rec_w_typeP8RWG_typeP7rc_type
// The word's xrs read into a graph: by the kind of field (rc +0x00; 3
// reads each xr as a location with the charset only), a fixed answer at
// rc +0xc0 compared against, anything else read by xrlv.  The four
// halfwords of the block it changes are put back.
long
xrw_algs(xrdata_type* xr, rec_w_type* readings, RWG_type* rwg, rc_type* rc)
{
	UShort h08 = RCGetH(rc, 0x08);
	UShort h0a = RCGetH(rc, 0x0a);
	UShort h0e = RCGetH(rc, 0x0e);
	UShort h1e = RCGetH(rc, 0x1e);
	readings[0].fWeight = 0;
	long kind = RCSigned(rc, 0x00);
	if (kind == 3)
	{
		RCSetH(rc, 0x0e, 1);
		RCSetH(rc, 0x08, 2);
	}
	if (*RCByte(rc, 0xc0) != 0)
		kind = 0x11;
	long result;
	if (kind == 1 || kind == 2 || kind == 3 || kind == 4 || kind == 5)
		result = xrlv(xr, rwg, rc);
	else if (kind == 0x11)
		result = GetCMPAliases(xr, rwg, (const char*) RCByte(rc, 0xc0), rc);
	else
		result = 1;
	RCSetH(rc, 0x0e, h0e);
	RCSetH(rc, 0x08, h08);
	RCSetH(rc, 0x0a, h0a);
	RCSetH(rc, 0x1e, h1e);
	return result;
}


// ROM 0x00363038 GetBaseBord__FP7rc_type
// The writing's slant, out of the ten pairs of bytes at rc +0x98 (a
// profile of the ink across its width): the mean of each pair weighed by
// its place, against the ink box's height and width.
long
GetBaseBord(rc_type* rc)
{
	long weighted = 0;
	long total = 0;
	long height = RCSigned(rc, 0xdc) - RCSigned(rc, 0xd8);
	long width = RCSigned(rc, 0xde) - RCSigned(rc, 0xda);
	for (long i = 0; i <= 9; i++)
	{
		long a = *RCByte(rc, 0x98 + i * 2);
		long b = *RCByte(rc, 0x98 + i * 2 + 1);
		long mean = (a + b) / 2;
		weighted += i * mean;
		total += mean;
	}
	if (height == 0)
		return 0;
	return (int) (3 * width * (weighted * 2 - total * 9)) / (int) (height * 110);
}


// ROM 0x003630c8 GetSymBox__FUciT2P11xrdata_typeP5_RECT
// The box of the xrs from `first` to before `end`, the links left out:
// the xrs that do not move with the writing's slant if there are two or
// more of them, else those with the ones that do; a punctuation mark's
// also takes in the movable ones.  With movable xrs other than dots and
// the letter one without a diacritical, the box is grown up and down
// towards them by at most a third of its height.  ==> 0, 1 for no xrs.
long
GetSymBox(UByte sym, long first, long end, xrdata_type* xr, _RECT* box)
{
	int punct = IsPunct(sym);
	long notDots = 0;
	if (end <= first)
		return 1;
	xrd_el_type* e = (xrd_el_type*) xr->fElements + first;
	long top = 32000, bottom = 0, left = 32000, right = 0;
	long mTop = 32000, mBottom = 0, mLeft = 32000, mRight = 0;
	long fixed = 0, movable = 0;
	long t, b, l, r;
	for (long i = first; i < end; i++, e++)
	{
		if (IsXrLink(e))
			continue;
		long eTop = XrGetH(e->box + kXrTop);
		long eBottom = XrGetH(e->box + kXrBottom);
		long eLeft = XrGetH(e->box + kXrLeft);
		long eRight = XrGetH(e->box + kXrRight);
		if (GetXrMovable(e) == 0)
		{
			if (eTop < top) top = eTop;
			if (bottom < eBottom) bottom = eBottom;
			if (eLeft < left) left = eLeft;
			if (right < eRight) right = eRight;
			fixed++;
		}
		else
		{
			if (eTop < mTop) mTop = eTop;
			if (mBottom < eBottom) mBottom = eBottom;
			if (eLeft < mLeft) mLeft = eLeft;
			if (mRight < eRight) mRight = eRight;
			if (e->type != 0x34)
				notDots++;
			movable++;
		}
	}
	t = top; b = bottom; l = left; r = right;
	if (1 < fixed)
		;
	else if (movable != 0)
	{
		if (fixed == 0)
		{
			t = mTop; b = mBottom; l = mLeft; r = mRight;
		}
		else
		{
			t = mTop <= top ? mTop : top;
			b = bottom <= mBottom ? mBottom : bottom;
			l = mLeft <= left ? mLeft : left;
			r = right < mRight ? mRight : right;
		}
	}
	else
	{
		// ROM QUIRK: one xr that does not move and none that do is taken as
		// no xrs at all - the box is the point where the word's first xr's
		// box starts
		xrd_el_type* e0 = (xrd_el_type*) xr->fElements;
		b = XrGetH(e0->box + kXrTop);
		l = XrGetH(e0->box + kXrLeft);
		t = b;
		r = l;
	}
	if (punct != 0 && movable != 0)
	{
		if (mTop <= t) t = mTop;
		if (b <= mBottom) b = mBottom;
		if (mLeft <= l) l = mLeft;
		if (r <= mRight) r = mRight;
	}
	box->top = (short) t;
	box->bottom = (short) b;
	box->left = (short) l;
	box->right = (short) r;
	if (notDots != 0 && movable != 0 && HWRStrChr((const char*) DiacriticsLetter, sym) == nil)
	{
		if (t < mTop)
			mTop = t;
		if (b <= mBottom)
			b = mBottom;
		long bt = box->top;
		if (mTop < bt)
		{
			long third = (box->bottom - bt) / 3;
			box->top = (short) (third < bt - mTop ? bt - third : mTop);
		}
		long bb = box->bottom;
		if (bb < b)
		{
			long third = (bb - box->top) / 3;
			box->bottom = (short) (third < b - bb ? third + bb : b);
		}
	}
	return 0;
}
