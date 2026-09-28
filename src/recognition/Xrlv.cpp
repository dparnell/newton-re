/*
	File:		Xrlv.cpp

	Contains:	The cursive reader's Viterbi over the xrs' locations (see
				XrReader.h).

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.
*/

#include "XrReader.h"
#include "LowLevel.h"
#include "CursiveReader.h"
#include "XrDomains.h"
#include "Dictionaries.h"

#include <string.h>

static inline short	RCSigned(rc_type* rc, ULong offset)	{ return (short) RCGetH(rc, offset); }

// The ARM library's division, __rt_sdiv(divisor, dividend).  DEVIATION:
// a nought divisor traps in the ROM (the machine's divide-by-zero
// exception); the host answers nought.
static inline long
RtSdiv(long divisor, long dividend)
{
	if (divisor == 0)
		return 0;
	return (int) dividend / (int) divisor;
}

static inline UByte*	CacheEntry(xrlv_data_type* d, long r)	{ return d->cache + r * kXrlvCacheEntry; }
static inline UShort	VarCHL(const xrlv_var_data_type* v)		{ return (UShort) ((v->chl[0] << 8) | v->chl[1]); }
static inline void		SetVarCHL(xrlv_var_data_type* v, ULong x)	{ v->chl[0] = (UByte) (x >> 8); v->chl[1] = (UByte) x; }
static inline XrlvPos*	PosAt(xrlv_data_type* d, long loc)		{ return d->pos[loc]; }


// ROM 0x0027ab60 xrlv__FP11xrdata_typeP8RWG_typeP7rc_type
// The word's xrs read along their locations: the first location given
// one reading of score 100, then from each location in turn - after its
// readings' letters have been checked against the base line - the
// readings grown into the locations ahead, the position blocks behind
// passed on to the locations further on.  The last location's readings
// sorted, the duplicates dropped, and made into the word graph.  The
// block's rc +0x0a bit 0 is cleared while it reads.  ==> 0, 1.
long
xrlv(xrdata_type* xr, RWG_type* rwg, rc_type* rc)
{
	UShort h0a = RCGetH(rc, 0x0a);
	UShort h1e = RCGetH(rc, 0x1e);
	UShort h02 = RCGetH(rc, 0x02);
	xrlv_data_type* d = nil;
	memset(rwg, 0, sizeof(RWG_type));
	RCSetH(rc, 0x0a, h0a & 0xfffe);
	if (XrlvAlloc(&d, xr, rc) == 0)
	{
		d->rc = rc;
		long beam = RCSigned(rc, 0x14);
		if (beam < 0)
			beam += 3;
		d->beam = beam >> 2;
		d->flags = RCSigned(rc, 0x08);
		d->charsets = RCSigned(rc, 0x02);
		d->caps = RCSigned(rc, 0x1e);
		// DEVIATION: no trigram header at all is no trigram table (the ROM
		// reads its word at 0x90 wherever nought points)
		TrigramHeader* trigrams = (TrigramHeader*) rc->fTrigrams;
		if (trigrams == nil || trigrams->fTrigram == nil)
			d->flags &= ~4;
		if (rc->fChain == nil)
			d->flags &= ~8;
		if ((d->flags & 4) != 0)
			d->lex.trigrams = trigrams->fTrigram;
		if ((d->flags & 8) != 0)
			d->lex.lexChain = (TDictChain*) rc->fChain;
		if (AssignDictionaries(0, 0, &d->lex, rc))
			d->flags &= ~1;
		XrlvGetCharset(d);
		ULong f = d->xrcm->flags;
		d->xrcm->flags = (f & ~2) | 0x10;
		d->f020 = d->xrcm->f1dc;
		*(int32_t*) &d->lex.f04[4] = 1;			// (lex +0x08)
		d->lex.alpha = rc->fAlphaCharset;
		d->lex.lPunct = rc->fLPunctCharset;
		d->lex.ePunct = rc->fEPunctCharset;
		memset(d->pos[0], 0, 0xdc);
		d->f024 = 100;
		d->pos[0]->best = 100;
		SetVarScore(&d->pos[0]->entries[0], 100);
		d->pos[0]->used = 1;
		for (long loc = 0; loc < d->nLocs; loc++)
		{
			XrlvCHLXrlvPos(loc, d);
			if (loc != 0 && d->width + loc - 1 < d->nLocs)
			{
				XrlvPos* block = d->pos[loc - 1];
				d->pos[d->width + loc - 1] = block;
				d->pos[loc - 1] = nil;
				block->best = 0;
				block->threshold = 0;
				block->slot = 0;
				block->total = 0;
				block->used = 0;
			}
			if (loc < d->nLocs - 1)
			{
				XrlvSortXrlvPos(loc, d);
				XrlvTrimXrlvPos(loc, d);
				XrlvGuessFutureGws(loc, d);
				XrlvDevelopPos(loc, d);
			}
		}
		XrlvFreeSomePos(d);
		// DEVIATION: the ROM calls the routine whose address is the word at
		// rc +0x104 when there is one (a hook to abandon the answers); the
		// host's block has no code addresses in it, and nothing sets one
		XrlvSortAns(d);
		XrlvCleanAns(d);
		if (XrlvCreateRWG(rwg, d) == 0)
		{
			RCSetH(rc, 0x0a, h0a);
			RCSetH(rc, 0x1e, h1e);
			RCSetH(rc, 0x02, h02);
			XrlvDealloc(&d);
			return 0;
		}
	}
	RCSetH(rc, 0x0a, h0a);
	RCSetH(rc, 0x1e, h1e);
	RCSetH(rc, 0x02, h02);
	XrlvDealloc(&d);
	return 1;
}


// ROM 0x0027af08 XrlvDevelopPos__FiP14xrlv_data_type
// Every reading at the location offered what may come next.  At the
// first location, and at a break wide enough for a word gap, a reading
// may also end its word here and start a new one (its word checked for
// capitals and closed, a space put in its text for a wide gap), from the
// vocabulary, the lexical database, the character set or leading
// punctuation; a reading may go on with ending punctuation, and a word
// that the vocabulary ended may go on as a word of the 0x81 dictionaries
// (a compound).
long
XrlvDevelopPos(long loc, xrlv_data_type* d)
{
	xrcm_type* x = d->xrcm;
	ULong caps = d->caps;
	UByte xi = d->locXr[loc];
	x->pos = xi;
	XrlvPos* P = d->pos[loc];
	SetInpLineByValue(100, xi, 3, x);
	x->pos = xi;
	long capFirst = 0;
	if ((caps & 2) != 0 && loc == 0)
		capFirst = 1;
	if ((caps & 8) != 0)
		capFirst = 1;
	UByte t = x->xrinp[d->locXr[loc]].type;
	if (t != 1 && t != 2)
		capFirst = 0;
	x->caseFlags = 5;
	ULong gap = ((xrd_el_type*) d->xr->fElements)[d->locXr[loc]].penalty;
	Boolean atBreak = loc == 0 || x->brk[d->locXr[loc]] != 0;
	long space = (atBreak && 7 < gap) ? 1 : 0;
	for (long i = 0; i < kXrlvCacheSyms; i++)
		CacheEntry(d, i)[3] = 0;
	long spacePen = gap * 3 - 0x1c;
	for (long j = 0; j < P->used; j++)
	{
		xrlv_var_data_type* E = &P->entries[d->order[j]];
		Boolean closes = E->status >= 3 && space != 0;
		xrlv_var_data_type v;
		if (loc != 0)
		{
			if (E->status < 4)
			{
				v = *E;
				v.loc = (UByte) loc;
				v.src = (UByte) j;
				XrlvDevelopCell(loc, capFirst, 0, &v, d);
			}
		}
		if (loc == 0 || closes)
		{
			v = *E;
			XrlvCheckDictCap(&v, d);
			XrlvApplyWordEndInfo(loc, &v, d);
			v.state = 0;
			v.status = 1;
			v.wlen = 0;
			v.flags = 0;
			v.cls = 0;
			v.count++;
			if (loc != 0 && 8 < gap)
			{
				v.word[v.len] = ' ';
				if (v.len < 0x16)
					v.len++;
			}
			long extra = v.count + spacePen - 1;
			if (extra < 0 || loc == 0)
				extra = 0;
			v.loc = (UByte) loc;
			v.src = (UByte) j;
			if ((d->flags & 1) != 0)
			{
				v.kind = 1;
				XrlvDevelopCell(loc, capFirst, extra, &v, d);
				v.kind = 0x41;
				XrlvDevelopCell(loc, capFirst, extra, &v, d);
			}
			if ((d->flags & 8) != 0)
			{
				v.kind = 2;
				XrlvDevelopCell(loc, capFirst, extra, &v, d);
			}
			if ((d->flags & 2) != 0)
			{
				Boolean charset = (E->kind & 8) != 0;
				ULong k = E->kind;
				if (charset)
					k = gap;
				if (!charset || k == 9)
				{
					v.kind = (d->flags & 4) == 0 ? 8 : 0xc;
					XrlvDevelopCell(loc, capFirst, extra, &v, d);
				}
			}
			if ((d->charsets & 8) != 0)
			{
				v.kind = 0x10;
				XrlvDevelopCell(loc, capFirst, extra, &v, d);
			}
		}
		if (loc != 0)
		{
			if (E->status == 1 && E->kind == 0x10)
			{
				v = *E;
				v.wlen = 0;
				v.flags = 0;
				v.loc = (UByte) loc;
				v.src = (UByte) j;
				if ((d->flags & 1) != 0)
				{
					v.kind = 1;
					XrlvDevelopCell(loc, capFirst, 0, &v, d);
					v.kind = 0x41;
					XrlvDevelopCell(loc, capFirst, 0, &v, d);
				}
				if ((d->flags & 8) != 0)
				{
					v.kind = 2;
					XrlvDevelopCell(loc, capFirst, 0, &v, d);
				}
			}
			if (2 < E->status && E->kind == 0x41)
			{
				v = *E;
				v.wlen = 0;
				v.flags = 1;
				v.status = 1;
				v.kind = 1;
				v.loc = (UByte) loc;
				v.src = (UByte) j;
				XrlvDevelopCell(loc, capFirst, 0, &v, d);
			}
		}
		if ((d->charsets & 0x10) != 0
		 && (loc == 0 || (2 < E->status && (E->kind == 1 || E->kind == 0x81 || E->kind == 2))))
		{
			v = *E;
			v.loc = (UByte) loc;
			v.src = (UByte) j;
			XrlvCheckDictCap(&v, d);
			XrlvApplyWordEndInfo(loc, &v, d);
			v.kind = 0x20;
			XrlvDevelopCell(loc, capFirst, 0, &v, d);
		}
		if (2 < E->status && E->kind == 1)
		{
			v = *E;
			v.wlen = 0;
			v.loc = (UByte) loc;
			v.src = (UByte) j;
			XrlvCheckDictCap(&v, d);
			v.status = 1;
			v.kind = 0x81;
			XrlvDevelopCell(loc, capFirst, 0, &v, d);
		}
	}
	return 0;
}


// ROM 0x0027b57c XrlvCleanAns__FP14xrlv_data_type
// An answer whose text an earlier one has already given is scored nought.
long
XrlvCleanAns(xrlv_data_type* d)
{
	XrlvPos* P = d->pos[d->nLocs - 1];
	long dropped = 0;
	for (long i = 1; i < P->used && dropped <= 4; i++)
	{
		for (long j = 0; j < i; j++)
		{
			if (HWRStrCmp(P->entries[d->ans[j].index].word, P->entries[d->ans[i].index].word) == 0)
			{
				d->ans[i].score = 0;
				break;
			}
			// ROM QUIRK: meant to count answers that are no duplicate, it
			// compares an index that never reaches the one it is compared to
			if (j == i)
				dropped++;
		}
	}
	return 0;
}


// ROM 0x0027b638 XrlvCreateRWG__FP8RWG_typeP14xrlv_data_type
// The word graph of the answers: up to five, in score order, as long as
// each scores at least rc +0x16 tens and within rc +0x1a tens of the
// best - each answer's letters a symbol each (with the xrs its letter
// was read from, its variant and what it added), a whole dictionary
// word's id kept with it, the answers bracketed by 2 and 3 with 4
// between them when there are more than one.  ==> 0, 1.
long
XrlvCreateRWG(RWG_type* rwg, xrlv_data_type* d)
{
	XrlvPos* P = d->pos[d->nLocs - 1];
	rc_type* rc = d->rc;
	XrlvAnswer* a = d->ans;
	long minScore = RCSigned(rc, 0x16);
	long count = 0;
	long thr = a->score - RCSigned(rc, 0x1a) * 10;
	lex_data_type* lex = &d->lex;
	char ids[8];
	long total = 0;
	for (long i = 0; i < P->used && count <= 4; i++, a++)
	{
		long s = a->score;
		if (s == 0)
			continue;
		if (s < minScore * 10 || s < thr)
			break;
		xrlv_var_data_type* E = &P->entries[a->index];
		long id;
		if (E->len == E->wlen
		 && (((E->kind & 1) != 0 && SetupVocHandle(lex, 1) == 0) || (E->kind & 2) != 0))
		{
			lex->flags = E->kind;
			HWRStrCpy(lex->word, E->word + (E->len - E->wlen));
			long attr;
			if (GetWordAttributeAndID(lex, &id, &attr) != 0)
			{
				lex->word[0] = (char) ToLower(lex->word[0]);
				if (GetWordAttributeAndID(lex, &id, &attr) != 0)
					id = -3;
			}
		}
		else
			id = -3;
		ids[count] = (char) id;
		if (0x17 < E->len)
			E->len = 0x17;
		if (E->word[E->len - 1] == ' ')
			E->len--;
		total += E->len;
		count++;
	}
	if (1 < count)
		total += count + 1;
	if (count != 0)
	{
		rwg->type = 1;
		rwg->size = total;
		long n = total + 1;
		rwg->rws = (RWS_type*) HWRMemoryAlloc(n * sizeof(RWS_type));
		if (rwg->rws == nil)
			goto freePpd;
		rwg->ppd = (RWG_PPD_type*) HWRMemoryAlloc(n * sizeof(RWG_PPD_type));
		if (rwg->ppd != nil)
		{
			RWS_type* p = rwg->rws;
			memset(p, 0, n * sizeof(RWS_type));
			memset(rwg->ppd, 0, n * sizeof(RWG_PPD_type));
			if (1 < count)
			{
				p[0].type = 2;
				p[total - 1].type = 3;
				p++;
			}
			long g = 1 < count ? 1 : 0;
			long k = 0;
			a = d->ans;
			for (; g < total && k < count; a++)
			{
				if (a->score == 0)
					continue;
				xrlv_var_data_type* E = &P->entries[a->index];
				long locs = 0;
				for (long j = 0; j < E->len && j < 0x17; j++)
				{
					p->type = 1;
					p->attr = (UByte) ids[k];
					p->letWeight = (UByte) E->letScore[j];
					p->f07 = a->pen;
					p->src = 1;
					UByte w = (UByte) (a->score / 10);
					p->weight = w > 100 ? 100 : w;
					p->sym = p->realSym = (UByte) E->word[j];
					UByte l = d->locXr[locs];
					p->xrStart = l + 1;
					p->xrLen = d->locXr[(E->letInfo[j] & 0xf) + locs] - l;
					p->var = E->letInfo[j] >> 4;
					p->f0b = 0;
					XrlvGetRwgSymAliases(g, rwg, d);
					locs += E->letInfo[j] & 0xf;
					g++;
					p++;
				}
				if (1 < count && count - 1 != k)
				{
					p->type = 4;
					g++;
					p++;
				}
				k++;
			}
			return 0;
		}
		if (rwg->rws != nil)
		{
			HWRMemoryFree((Ptr) rwg->rws);
			rwg->rws = nil;
		}
	}
freePpd:
	if (rwg->ppd != nil)
	{
		HWRMemoryFree((Ptr) rwg->ppd);
		rwg->ppd = nil;
	}
	return 1;
}


// ROM 0x0027bad0 XrlvSetLocations__FP14xrlv_data_typei
// The locations: the xrs the low level marked as the last of a letter
// (with `everyXr`, only the first and the last of them); and how many
// locations a letter may span - the most there are within seventeen xrs
// of any one.  ==> 0, 1 for fewer than two locations or three xrs.
long
XrlvSetLocations(xrlv_data_type* d, long everyXr)
{
	long n = d->xr->fLength;
	xrd_el_type* e = (xrd_el_type*) d->xr->fElements;
	long k = 0;
	long i;
	for (i = 0; i < n; i++, e++)
	{
		if ((e->attrib & 1) == 0)
			continue;
		if (everyXr != 0 && 0 < i && i < n - 1)
			continue;
		d->xrLoc[i] = (UByte) k;
		d->locXr[k] = (UByte) i;
		k++;
	}
	if (k < 2 || i < 3)
		return 1;
	long width = 0;
	for (long j = 0; j < k; j++)
	{
		long c = 0;
		long first = d->locXr[j] + 1;
		for (long p = first; p < n && p < first + 0x11; p++)
			if (d->xrLoc[p] != 0)
				c++;
		if (width < c)
			width = c;
	}
	d->width = width + 1;
	d->nLocs = k;
	return 0;
}


// ROM 0x0027bbc8 XrlvGetCharset__FP14xrlv_data_type
// The character set a reading may use outside the dictionaries, by the
// block's charsets: the letters (with their capitals when rc +0x1e asks
// for either), then the digits, ending punctuation, leading punctuation,
// the others and the math symbols, each once, at most 255 - each entry
// its group (0x10 letters, 0x20 digits, 0x50 punctuation, 0x40 others,
// 0x30 math) and what it costs (4, digits 3).  ==> how many.
long
XrlvGetCharset(xrlv_data_type* d)
{
	ULong sets = d->charsets;
	ULong caps = RCGetH(d->rc, 0x1e);
	fw_buf_type* buf = d->charset;
	long n = 0;
	fw_buf_type* p = buf;
	struct { ULong bit; const char* chars; UByte group; UByte pen; } others[5] =
	{
		{ 2, d->rc->fNumCharset, 0x20, 3 },
		{ 0x10, d->rc->fEPunctCharset, 0x50, 4 },
		{ 8, d->rc->fLPunctCharset, 0x50, 4 },
		{ 0x20, d->rc->fOtherCharset, 0x40, 4 },
		{ 4, d->rc->fMathCharset, 0x30, 4 }
	};
	if ((sets & 1) != 0)
	{
		const UByte* c = (const UByte*) d->rc->fAlphaCharset;
		for (; *c != 0 && n < 0xff; c++)
		{
			p->sym = *c;
			p->attr = 0x10;
			p->pen = 4;
			p->status = 3;
			n++;
			p++;
			if (0xff < n)
				goto done;
			if ((caps & 0xa) != 0 && IsLower(*c))
			{
				p->sym = (UByte) ToUpper(*c);
				p->attr = 0x10;
				p->pen = 4;
				p->status = 3;
				n++;
				p++;
				if (0xff < n)
					goto done;
			}
		}
	}
	for (long s = 0; s < 5; s++)
	{
		if ((sets & others[s].bit) == 0)
			continue;
		const UByte* c = (const UByte*) others[s].chars;
		for (; *c != 0 && n < 0xff; c++)
		{
			long i;
			for (i = 0; i < n; i++)
				if (buf[i].sym == *c)
					break;
			if (i < n)
				continue;
			p->sym = *c;
			p->attr = others[s].group;
			p->pen = others[s].pen;
			p->status = 3;
			n++;
			p++;
			if (0xff < n)
				goto done;
		}
	}
done:
	d->nCharset = n;
	return n;
}


// ROM 0x0027bf70 XrlvGetRwgSymAliases__FiP8RWG_typeP14xrlv_data_type
// The xrs the graph's symbol `i` was read from: an earlier symbol, the
// same letter over the same xrs, lends its own; otherwise the letter is
// read again over its xrs (XrlvGetSymAliases).  A letter of any variant
// (0xf) takes the one the matrix found.  ==> 0.
long
XrlvGetRwgSymAliases(long i, RWG_type* rwg, xrlv_data_type* d)
{
	RWS_type* rws = rwg->rws;
	RWG_PPD_type* ppd = rwg->ppd;
	RWS_type* e = &rws[i];
	UByte start = e->xrStart;
	ULong len = e->xrLen;
	UByte var = e->var;
	for (long j = 0; j < i; j++)
	{
		RWS_type* p = &rws[j];
		if (p->type == 1 && p->sym == e->sym && p->xrStart == start && p->xrLen == len)
		{
			memcpy(&ppd[i], &ppd[j], sizeof(RWG_PPD_type));
			e->var = rws[j].var;
			return 0;
		}
	}
	// DEVIATION: the ROM's buffer is whatever was on the stack, read as it
	// is when the letter could not be read again; the host's starts empty
	UByte aliases[16] = { 0 };
	XrlvGetSymAliases(e->sym, var, start - 1, start + len, aliases, d->xrcm);
	UByte* out = ppd[i].el[0];
	for (long k = 0; aliases[k] != 0 && k < 0xc; k++, out += 4)
	{
		out[0] = (UByte) ((aliases[k] & 0x3f) + start - 1);
		out[1] = aliases[k] >> 6;
	}
	if (var == 0xf)
		e->var = d->xrcm->varOfPos[len - 1];
	return 0;
}


// ROM 0x0027c0c8 XrlvAlloc__FPP14xrlv_data_typeP11xrdata_typeP7rc_type
// The Viterbi's state: the matrix, the trace's points as two arrays, the
// locations, and a position block for each location a letter may reach
// at once - rc +0x10 readings each (at most 100), fewer and fewer when
// the blocks will not all fit (five tries, each asking for the share that
// did fit).  ==> 0, non-zero for a failure.
long
XrlvAlloc(xrlv_data_type** pd, xrdata_type* xr, rc_type* rc)
{
	long total = 0;
	xrlv_data_type* d = (xrlv_data_type*) HWRMemoryAlloc(sizeof(xrlv_data_type));
	if (d != nil)
	{
		memset(d, 0, sizeof(xrlv_data_type));
		if (xrmatr_alloc(rc, xr, &d->xrcm) == 0)
		{
			d->xr = xr;
			long n = RCSigned(rc, 0x96);
			short* trace = (short*) HWRMemoryAlloc(n * 4);
			if (trace != nil)
			{
				d->traceX = trace;
				d->traceY = trace + n;
				PS_point_type* points = (PS_point_type*) rc->fTrace;
				for (long i = 0; i < n; i++)
				{
					d->traceX[i] = points[i].x;
					d->traceY[i] = points[i].y;
				}
				if (XrlvSetLocations(d, RCSigned(rc, 0x0e) == 1) == 0 && 1 < d->width)
				{
					long q = RCSigned(rc, 0x10);
					if (99 < q)
						q = 100;
					long pct = 100;
					d->nEntries = q;
					for (long tries = 0; tries < 5; tries++)
					{
						d->nEntries = RtSdiv(100, pct * d->nEntries);
						if (d->nEntries < 2)
							goto fail;
						d->posSize = 0x24 + d->nEntries * (long) sizeof(xrlv_var_data_type);
						pct = 100;
						total = 0;
						for (long i = 0; i < d->width; i++)
						{
							XrlvPos* p = (XrlvPos*) HWRMemoryAlloc(d->posSize);
							d->pos[i] = p;
							if (p == nil)
							{
								pct = RtSdiv(d->width, i * 100);
								break;
							}
							p->best = 0;
							p->threshold = 0;
							p->slot = 0;
							p->total = 0;
							p->used = 0;
							total += d->posSize;
						}
						if (pct == 100)
						{
							*pd = d;
							return 0;
						}
						for (long i = 0; i < d->width; i++)
						{
							if (d->pos[i] != nil)
							{
								HWRMemoryFree((Ptr) d->pos[i]);
								d->pos[i] = nil;
							}
						}
					}
					if (99 < pct)
					{
						*pd = d;
						return 0;
					}
				}
			}
		}
	}
fail:
	XrlvDealloc(&d);
	*pd = nil;
	return total + 1;
}


// ROM 0x0027c39c XrlvDealloc__FPP14xrlv_data_type
long
XrlvDealloc(xrlv_data_type** pd)
{
	xrlv_data_type* d = *pd;
	if (d != nil)
	{
		if (d->traceX != nil)
		{
			HWRMemoryFree((Ptr) d->traceX);
			d->traceX = nil;
			d->traceY = nil;
		}
		if (d->xrcm != nil)
			xrmatr_dealloc(&d->xrcm);
		for (long i = 0; i < 0x78; i++)
		{
			if (d->pos[i] != nil)
			{
				HWRMemoryFree((Ptr) d->pos[i]);
				d->pos[i] = nil;
			}
		}
		HWRMemoryFree((Ptr) d);
		*pd = nil;
	}
	return 0;
}


// ROM 0x0027c420 XrlvFreeSomePos__FP14xrlv_data_type
// Every position block but the last location's.
long
XrlvFreeSomePos(xrlv_data_type* d)
{
	if (d != nil)
	{
		for (long i = 0; i < d->nLocs - 1; i++)
		{
			if (d->pos[i] != nil)
			{
				HWRMemoryFree((Ptr) d->pos[i]);
				d->pos[i] = nil;
			}
		}
	}
	return 0;
}


// ROM 0x0027c480 XrlvCheckDictCap__FP18xrlv_var_data_typeP14xrlv_data_type
// A dictionary word that wants capitals (its attribute's top two bits: the
// whole word; bit 7 alone: its first letter) gets them - each lower-case
// letter made a capital, its variant marked any (0xf), and five taken off
// the score for a variant that may not stand for a capital.
long
XrlvCheckDictCap(xrlv_var_data_type* v, xrlv_data_type* d)
{
	long i = v->len - v->wlen;
	DTIHeader* dti = (DTIHeader*) d->rc->fDTI;
	if ((v->attr & 0xc0) == 0xc0)
	{
		for (; i < v->len; i++)
		{
			if (IsLower((UByte) v->word[i]))
			{
				if (GetVarRewcapAllow((UByte) v->word[i], v->letInfo[i] >> 4, dti) == 0)
				{
					SetVarScore(v, (UShort) VarScore(v) - 5);
					long s = v->letScore[i] - 5;
					if (s < -0x7f)
						s = -0x7f;
					v->letScore[i] = (signed char) s;
				}
				v->word[i] = (char) ToUpper((UByte) v->word[i]);
				v->letInfo[i] |= 0xf0;
			}
		}
	}
	else if ((v->attr & 0x80) != 0)
	{
		if (IsLower((UByte) v->word[i]))
		{
			if (GetVarRewcapAllow((UByte) v->word[i], v->letInfo[i] >> 4, dti) == 0)
			{
				SetVarScore(v, (UShort) VarScore(v) - 5);
				// ROM QUIRK: the first letter's penalty is booked against the
				// last letter
				long s = v->letScore[v->len - 1] - 5;
				if (s < -0x7f)
					s = -0x7f;
				v->letScore[v->len - 1] = (signed char) s;
			}
			v->word[i] = (char) ToUpper((UByte) v->word[i]);
			v->letInfo[i] |= 0xf0;
		}
	}
	return 0;
}


// ROM 0x0027c5ec XrlvApplyWordEndInfo__FiP18xrlv_var_data_typeP14xrlv_data_type
// A word that ends here: a one-letter word from no character set loses
// half the xrs its letter spans, and a word the dictionary gave an
// attribute (its low two bits) gains by it, more for a longer word.
long
XrlvApplyWordEndInfo(long loc, xrlv_var_data_type* v, xrlv_data_type* d)
{
	ULong len = v->len;
	if (len == 0)
		return 0;
	if ((v->kind & 8) == 0 && v->wlen == 1)
	{
		long p = ((long) (d->locXr[loc] - d->locXr[loc - (v->letInfo[len - 1] & 0xf)]) * 2 + 2) >> 2;
		SetVarScore(v, (UShort) VarScore(v) - p);
		long s = v->letScore[len - 1] - p;
		if (s < -0x7f)
			s = -0x7f;
		v->letScore[len - 1] = (signed char) s;
	}
	if ((v->flags & 1) == 0)
	{
		long a = v->attr & 3;
		long p = a + ((long) (v->wlen * a * 3) >> 3);
		SetVarScore(v, (UShort) VarScore(v) + p);
		long s = p + v->letScore[v->len - 1];
		if (0x7f < s)
			s = 0x7f;
		v->letScore[v->len - 1] = (signed char) s;
	}
	return 0;
}


// ROM 0x0027c6d8 XrlvDevelopCell__FiN21P18xrlv_var_data_typeP14xrlv_data_type
// One reading offered every symbol that may come next: each symbol
// counted from the location once (CountSym, remembered in the cache), and
// every location ahead that the symbol's out line reaches given a new
// reading if it beats that location's threshold - the reading's score
// plus what the letter read, less what it costs after the letter before
// (by their classes), what the xrs it skipped cost and what ending away
// from a break costs.  A full location replaces its weakest reading, or
// one that has fallen more than twice the beam below its best.
long
XrlvDevelopCell(long loc, long capFirst, long extra, xrlv_var_data_type* v, xrlv_data_type* d)
{
	long twoBeam = d->beam << 1;
	xrcm_type* x = d->xrcm;
	long xi = d->locXr[loc];
	long link = 0;
	xrd_el_type* elements = (xrd_el_type*) d->xr->fElements;
	for (long p = xi; p >= 0; p--)
	{
		if (IsXrLink(&elements[p]))
			link = 1;
		if (GetXrMovable(&elements[p]) == 0)
			break;
	}
	long n = XrlvGetNextSymbols(v, capFirst, d);
	for (long s = 0; s < n; s++)
	{
		fw_buf_type* fw = &d->syms[s];
		UByte c = fw->sym;
		UByte status = fw->status & 0xf;
		if (!IsLower(c) && c != '\'' && link == 0)
			continue;
		long r = OSToRec(c) - 0x20;
		if (r < 0 || 0x82 < r)
			continue;
		UByte* cache = CacheEntry(d, r);
		if ((cache[3] & 2) != 0)
			continue;
		if ((cache[3] & 1) == 0)
		{
			x->sym = c;
			if (CountSym(x) != 0)
				continue;
			cache[1] = (UByte) x->outSt;
			if (cache[1] == 0)
				cache[1] = 1;
			cache[2] = (UByte) x->outEnd;
			long k = 0;
			for (long p = cache[1]; p < cache[2]; p++, k++)
			{
				long value = x->outLine[p];
				cache[4 + k] = (UByte) (value < 0 ? 0 : value);
				cache[0x14 + k] = x->varOfPos[p];
			}
			cache[3] = 1;
		}
		long start = cache[1] < xi ? xi : cache[1];
		UByte attr = status < 3 ? 0 : fw->attr;
		long cls;
		if ((v->kind & 3) != 0)
			cls = 5;
		else if (IsLower(c))
			cls = 2;
		else if (IsUpper(c))
			cls = 3;
		else if ((__ctype[c] & 0x20) != 0)
			cls = 4;
		else if (c == '+' || c == '=' || c == '*' || c == '/' || c == '@' || c == '(' || c == ')')
			cls = 6;
		else
			cls = 1;
		long clsPen = kXrlvClassPenalty[v->cls * 7 + cls];
		long none = 1;
		for (long p = start; p < cache[2]; p++)
		{
			long l = d->xrLoc[p];
			if (l <= loc)
				continue;
			XrlvPos* P = d->pos[l];
			if (P == nil)
				break;
			long value = cache[4 + (p - cache[1])] - 100;
			if (VarScore(v) + value < P->threshold)
				continue;
			none = 0;
			long brk;
			if (x->brk[p] == 0)
				brk = (v->kind & 1) == 0 ? 4 : 1;
			else
				brk = 0;
			long penBrk = brk + extra;
			long penGap = clsPen + (((p - xi) * fw->pen + 2) >> 2);
			long delta = value - penGap - penBrk;
			if (delta < -0x7f)
				delta = -0x7f;
			else if (0x7f < delta)
				delta = 0x7f;
			long score = VarScore(v) + delta;
			if (score <= P->threshold)
				continue;
			xrlv_var_data_type* E = &P->entries[P->slot];
			*E = *v;
			E->sym = c;
			E->var = cache[0x14 + (p - cache[1])];
			E->cls = (UByte) cls;
			E->letValue = (UByte) value;
			SetVarScore(E, score);
			E->penGap = (UByte) penGap;
			E->penBrk = (UByte) penBrk;
			E->flags |= fw->status & 0x80;
			E->state = fw->state;
			E->attr = attr;
			E->dmask = fw->dmask;
			E->status = status;
			E->word[E->len] = (char) c;
			E->letScore[E->len] = (signed char) delta;
			E->letInfo[E->len] = (UByte) ((l - loc) | (E->var << 4));
			if (E->len < 0x16)
			{
				E->wlen++;
				E->len++;
			}
			if (P->best < score)
				P->best = score;
			long total = ++P->total;
			if (P->used < d->nEntries)
				P->used++;
			if (total < d->nEntries)
			{
				P->slot = total;
				P->threshold = 0;
			}
			else
			{
				long slot = 0;
				long lim = P->best - twoBeam;
				long least = VarScore(&P->entries[0]);
				for (long k = 0; k < P->used; k++)
				{
					long es = VarScore(&P->entries[k]);
					if (es < lim)
					{
						slot = k;
						break;
					}
					if (es < least)
					{
						least = es;
						slot = k;
					}
				}
				P->slot = slot;
				P->threshold = VarScore(&P->entries[slot]);
				if (P->threshold < lim)
					P->threshold = lim;
			}
		}
		if (none != 0)
			cache[3] |= 2;
	}
	return 0;
}


// ROM 0x0027cce4 XrlvSortXrlvPos__FiP14xrlv_data_type
// The location's readings in order of score, best first (d->order); the
// location's best lowered to the best there is.
long
XrlvSortXrlvPos(long loc, xrlv_data_type* d)
{
	XrlvPos* P = d->pos[loc];
	UByte* order = d->order;
	for (long i = 0; i < P->used; i++)
		order[i] = (UByte) i;
	Boolean sorted;
	do
	{
		sorted = true;
		if (P->used < 2)
			break;
		for (long i = 1; i < P->used; i++)
		{
			UByte a = order[i - 1];
			if (VarScore(&P->entries[a]) < VarScore(&P->entries[order[i]]))
			{
				order[i - 1] = order[i];
				order[i] = a;
				sorted = false;
			}
		}
	} while (!sorted);
	long top = VarScore(&P->entries[order[0]]);
	if (top < P->best)
		P->best = top;
	return 0;
}


// ROM 0x0027cdbc XrlvTrimXrlvPos__FiP14xrlv_data_type
// The location's readings cut to those within the beam of the best.  If
// the best character-set reading has fallen more than two thirds of the
// beam below, the character-set readings get back what their skipped xrs
// cost; the best of them is kept even when the cut would drop it.
// ==> the readings kept.
long
XrlvTrimXrlvPos(long loc, xrlv_data_type* d)
{
	long beam = d->beam;
	long third = RtSdiv(3, beam << 1);
	XrlvPos* P = d->pos[loc];
	UByte* order = d->order;
	long n = P->used;
	if (n < 2)
		return P->used;
	long bestIdx = 0;
	long bestCharset = 0;
	for (long i = 0; i < n; i++)
	{
		xrlv_var_data_type* E = &P->entries[order[i]];
		if ((E->kind & 8) != 0 && bestCharset < VarScore(E))
		{
			bestIdx = i;
			bestCharset = VarScore(E);
		}
	}
	if (bestCharset != 0)
	{
		long lim = P->best - third;
		if (lim > bestCharset)
		{
			for (long i = 0; i < P->used; i++)
			{
				xrlv_var_data_type* E = &P->entries[order[i]];
				if ((E->kind & 8) != 0)
				{
					long s = E->letScore[E->len - 1] + (signed char) E->penGap;
					if (0x7f < s)
						s = 0x7f;
					E->letScore[E->len - 1] = (signed char) s;
					SetVarScore(E, (UShort) VarScore(E) + E->penGap);
				}
			}
		}
	}
	long i = 0;
	if (0 < P->used)
	{
		long lim = P->best - beam;
		for (; i < P->used; i++)
		{
			if (lim > VarScore(&P->entries[order[i]]))
			{
				if (i <= bestIdx)
				{
					order[i] = order[bestIdx];
					i++;
				}
				break;
			}
		}
	}
	P->used = i;
	return P->used;
}


// ROM 0x0027cf48 XrlvGuessFutureGws__FiP14xrlv_data_type
// The locations ahead given a floor for their best - a quarter of the
// beam below the best behind them - and thresholds of a beam below that.
void
XrlvGuessFutureGws(long loc, xrlv_data_type* d)
{
	long b = d->beam;
	if (b < 0)
		b += 3;
	long quarter = b >> 2;
	long prevBest = 0;
	for (;; loc++)
	{
		XrlvPos* P = d->pos[loc];
		if (P == nil)
			return;
		if (d->nLocs <= loc)
			break;
		long t = prevBest - quarter;
		if (P->best < t)
			P->best = t;
		long best = P->best;
		if (P->threshold < best - d->beam)
			P->threshold = best - d->beam;
		if (0 < best)
			prevBest = best;
	}
}


// A letter's box moved by the writing's slant at its middle, and where on
// it the letter's variant says its body lies (the variant's position and
// size nibbles, in sixteenths of the box).
static long
SlantBox(UByte sym, long first, long end, xrlv_data_type* d, _RECT* box, UByte var, long inkLeft, long slant,
		 long* bodyTop, long* bodyBottom)
{
	if (GetSymBox(sym, first, end, d->xr, box) != 0)
		return 0;
	long dx = ((box->left + box->right) / 2 - inkLeft) * slant;
	if (dx < 0)
		dx += 0x7f;
	dx = (short) (dx >> 7);
	box->top = (short) (box->top - dx);
	box->bottom = (short) (box->bottom - dx);
	ULong posSize = GetVarPosSize(sym, var, (DTIHeader*) d->rc->fDTI);
	long h = box->bottom - box->top;
	if (bodyTop != nil)
	{
		long t = (long) ((posSize >> 4) & 0xf) * h;
		if (t < 0)
			t += 0xf;
		*bodyTop = (short) (box->top + (t >> 4));
		t = ((long) (posSize & 0xf) + 1) * h;
		if (t < 0)
			t += 0xf;
		*bodyBottom = (short) (box->top + (t >> 4));
	}
	return posSize;
}


// The size check against a letter before: the ratio of the two letters'
// heights against what their variants' size nibbles allow (the high
// nibble the smallest a letter is, the next the tallest, in parts of the
// cap height), eight at most; halved or cut to two thirds when the two
// boxes overlap little on the line.  (`overlapA` and `overlapB` are the
// boxes the ROM measures the overlap between - the letter's and the one
// before it, whichever letter before is being checked.)
static long
SizePenalty(ULong before, ULong now, long hBefore, long hNow, const _RECT& a, const _RECT& b, Boolean halveOnRatio)
{
	long beforeHi = (before >> 12) & 0xf;
	long beforeLo = (before >> 8) & 0xf;
	long nowHi = (now >> 12) & 0xf;
	long nowLo = (now >> 8) & 0xf;
	long hiRatio = RtSdiv(nowLo, beforeHi * 100);
	long loRatio = RtSdiv(nowHi, beforeLo * 100);
	long ratio = RtSdiv(hNow, hBefore * 100);
	if (ratio < 1)
		ratio = 1;
	long pen = 0;
	Boolean shape = nowLo >= 14 && beforeLo >= 14;
	if (ratio < hiRatio)
	{
		if (nowLo < 15 && 1 < beforeHi)
			pen = RtSdiv(ratio, hiRatio << 3) - 8;
	}
	else if (loRatio < ratio)
	{
		if (1 < nowHi && beforeLo < 15)
			pen = RtSdiv(loRatio, ratio << 3) - 8;
	}
	else
		return -1;			// (no penalty, and none of the clamping)
	if (shape)
	{
		long overlap = a.bottom - b.top;
		long other = b.bottom - a.top;
		if (other <= overlap)
			overlap = other;
		long big = hBefore <= hNow ? hNow : hBefore;
		if (overlap < big / 2)
		{
			long small = hNow <= hBefore ? hNow : hBefore;
			if (overlap < RtSdiv(3, small << 1))
				pen /= 2;
			else if (overlap < RtSdiv(5, small << 2))
				pen = RtSdiv(3, pen << 1);
		}
	}
	if (halveOnRatio)
	{
		if (ratio < hiRatio ? beforeHi * 3 < nowLo : nowHi * 3 < beforeLo)
			pen /= 2;
	}
	if (8 < pen)
		pen = 8;
	else if (pen < 0)
		pen = 0;
	return pen;
}


// ROM 0x0027cfb8 XrlvCHLXrlvPos__FiP14xrlv_data_type
// Each reading at the location has its last letter checked against the
// line, and charged for what looks wrong (eight at most each): whether it
// sits where the letter before does (their bodies, by their variants'
// position nibbles, should overlap), whether its size against the letter
// before and the one before that is what their variants allow, and
// whether its middle is where the line's middle should be (the base line
// found for the word, rc +0xe2..0xf0).  A single-letter reading is
// checked against the line itself as though a letter before sat on it.
// Every box is first moved by the writing's slant (GetBaseBord).
// ==> 0, 1 for nothing there.
long
XrlvCHLXrlvPos(long loc, xrlv_data_type* d)
{
	rc_type* rc = d->rc;
	long inkLeft = RCSigned(rc, 0xd8);
	XrlvPos* P = d->pos[loc];
	if (P->used == 0 || loc == 0)
		return 1;
	long slant = GetBaseBord(rc);
	long lineMid = 0;
	long lineH = 0;
	long base = 0;
	Boolean haveLine = false;
	if (RCSigned(rc, 0xea) != 0 && 0x46 <= RCSigned(rc, 0xf0))
	{
		lineH = RCSigned(rc, 0xea) << 1;
		base = RCSigned(rc, 0xec);
		haveLine = true;
	}
	else if (RCSigned(rc, 0xe2) != 0 && 0x46 <= RCSigned(rc, 0xe8))
	{
		lineH = RCSigned(rc, 0xe2) << 1;
		base = RCSigned(rc, 0xe4);
		haveLine = true;
	}
	if (haveLine)
	{
		long b10 = base * 10;
		if (b10 != 0)
		{
			long t = ((inkLeft + RCSigned(rc, 0xdc)) / 2 - inkLeft) * slant;
			if (t < 0)
				t += 0x7f;
			lineMid = b10 - (t >> 7);
		}
	}
	// DEVIATION: the box of the letter before is kept from one reading to
	// the next, as the ROM's stack slots are; where the ROM would read them
	// unset, the host has nought
	_RECT A = { 0, 0, 0, 0 }, B = { 0, 0, 0, 0 }, C = { 0, 0, 0, 0 };
	long exTop = 0, exBottom = 0, prevTop = 0, prevBottom = 0;
	for (long k = 0; k < P->used; k++)
	{
		xrlv_var_data_type* E = &P->entries[k];
		long total = 0;
		ULong prev = 0;			// the letter before's position and size
		ULong prev2 = 0;		// the one before that's
		ULong now = 0;
		now = SlantBox(E->sym, d->locXr[E->loc] + 1, d->locXr[loc] + 1, d, &A, E->var, inkLeft, slant, &exTop, &exBottom);
		long len = E->len;
		if (len <= 1)
		{
			if (RCSigned(rc, 0xe2) != 0 && RCSigned(rc, 0xe8) == 100 && RCSigned(rc, 0xe6) == 100)
			{
				B = A;
				B.bottom = (short) RCGetH(rc, 0xe4);
				B.top = (short) (B.bottom - RCGetH(rc, 0xe2));
				// ROM QUIRK: the letter before is taken to be of the size and
				// position a constant happens to say (0x2a5778: its high nibble
				// five, its next seven); only its non-nought-ness was meant
				prev = 0x2a5778;
				long bh = B.bottom - B.top;
				long t = 7 * bh;
				if (t < 0)
					t += 0xf;
				prevTop = (short) (B.top + (t >> 4));
				t = 9 * bh;
				if (t < 0)
					t += 0xf;
				prevBottom = (short) (B.top + (t >> 4));
			}
		}
		else
		{
			long i = len - 2;
			if (E->letInfo[i] == 0)
				i--;
			if (i >= 0)
			{
				long locB = E->loc - (E->letInfo[i] & 0xf);
				prev = SlantBox((UByte) E->word[i], d->locXr[locB] + 1, d->locXr[E->loc] + 1, d, &B,
								E->letInfo[i] >> 4, inkLeft, slant, &prevTop, &prevBottom);
			}
			if (i > 0)
			{
				long j = i - 1;
				if (E->letInfo[j] == 0)
					j--;
				if (j >= 0)
				{
					long locB = E->loc - (E->letInfo[i] & 0xf);
					long locC = locB - (E->letInfo[j] & 0xf);
					prev2 = SlantBox((UByte) E->word[j], d->locXr[locC] + 1, d->locXr[locB] + 1, d, &C,
									 E->letInfo[j] >> 4, inkLeft, slant, nil, nil);
				}
			}
		}
		if (now != 0)
		{
			// where it sits against the letter before
			if (prev != 0)
			{
				long ha = A.bottom - A.top;
				long hb = B.bottom - B.top;
				long mn = ha >= hb ? hb : ha;
				long mx = ha > hb ? ha : hb;
				long q = RtSdiv(3, mn + mx * 2 + 1);
				if (q < 0x14)
					q = 0x14;
				long p = 0;
				long diff = 0;
				if (prevTop > exBottom)
				{
					if (exBottom != A.bottom && prevTop != B.top)
						diff = prevTop - exBottom;
				}
				else if (exTop > prevBottom)
				{
					if (prevBottom != B.bottom && exTop != A.top)
						diff = exTop - prevBottom;
				}
				if (diff != 0)
				{
					p = RtSdiv(q, diff * 0x14);
					if (8 < p)
						p = 8;
					else if (p < 0)
						p = 0;
				}
				total = p;
				SetVarCHL(E, p);
			}
			// its size against the letter before
			if (prev != 0)
			{
				long hb = B.bottom - B.top;
				long ha = A.bottom - A.top;
				if (ha < 1)
					ha = 1;
				long p = SizePenalty(prev, now, hb, ha, A, B, true);
				if (p < 0)
					p = 0;
				total += p;
				SetVarCHL(E, VarCHL(E) | (p << 4));
			}
			// ... and against the one before that (ROM QUIRK: the overlap is
			// still measured against the letter just before)
			if (prev2 != 0)
			{
				long hc = C.bottom - C.top;
				long ha = A.bottom - A.top;
				if (ha < 1)
					ha = 1;
				long p = SizePenalty(prev2, now, hc, ha, A, B, false);
				if (p < 0)
					p = 0;
				long half = p / 2;
				total += half;
				ULong old = VarCHL(E);
				SetVarCHL(E, old | ((half + (long) (old >> 4)) << 4));
			}
			// its middle against the line's
			if (lineMid != 0)
			{
				long v = RtSdiv(lineH, (A.top + A.bottom) * 5 - lineMid) + 10;
				long hi = (now >> 20) & 0xf;
				long lo = (now >> 16) & 0xf;
				long p = 0;
				if (v < hi && hi != 0)
					p = (hi - v) * 2;
				if (lo < v && lo != 0xf)
					p = (v - lo) * 2;
				if (8 < p)
					p = 8;
				else if (p < 0)
					p = 0;
				total += p;
				SetVarCHL(E, VarCHL(E) | (p << 8));
			}
		}
		SetVarScore(E, (UShort) VarScore(E) - total);
		long s = E->letScore[E->len - 1] - total;
		if (s < -0x7f)
			s = -0x7f;
		E->letScore[E->len - 1] = (signed char) s;
	}
	return 0;
}


// ROM 0x0027dd00 XrlvGetNextSymbols__FP18xrlv_var_data_typeiP14xrlv_data_type
// What may follow the reading, by what allows its letters: the vocabulary
// (its next letters, and at the start of a word that may take a capital
// their capitals too), the lexical database (each two cheaper), the
// character set (each weighed by the trigram table against the two
// letters before, when there is one), or the punctuation that may start
// or end a word.  The last set asked for is remembered.  ==> how many
// (d->syms).
long
XrlvGetNextSymbols(xrlv_var_data_type* v, long capFirst, xrlv_data_type* d)
{
	lex_data_type* lex = &d->lex;
	UByte kind = v->kind;
	long count = 0;
	long result = 0;
	if (kind == 0x10)
	{
		if (d->nLPunct == 0)
		{
			long n = 0;
			for (long i = 0; i < 0x10; i++)
			{
				fw_buf_type* e = &d->lPunct[i];
				e->sym = (UByte) lex->lPunct[i];
				if (e->sym == 0)
					break;
				e->attr = 0;
				e->state = 0;
				e->pen = 2;
				e->status = 1;
				n++;
			}
			d->nLPunct = n;
		}
		d->syms = d->lPunct;
		return d->nLPunct;
	}
	fw_buf_type* buf = d->symBuf;
	if (kind < 0x11)
	{
		if (kind == 1)
			goto vocabulary;
		if (kind != 2)
		{
			if (kind != 8 && kind != 0xc)
				return 0;
			long n = d->nCharset;
			if (lex->trigrams != nil && (kind & 4) != 0)
			{
				uint32_t state = v->status == 1 ? 0 : v->state;
				fw_buf_type* e = d->charset;
				for (long i = 0; i < n; i++, e++)
				{
					long t = triads_mapping[e->sym] - 1;
					if (t >= 0)
					{
						if (0x27 < t)
							t = 0;
						UByte b = lex->trigrams[((state >> 8) & 0xff) * 400 + (state & 0xff) * 10 + (t >> 2)];
						e->state = (uint32_t) t | (state << 8);
						e->pen = (UByte) (5 - ((b >> ((t & 3) << 1)) & 3));
					}
					else if (e->attr == 0x30 || e->attr == 0x10 || e->attr == 0x40 || e->attr == 0x50)
						e->pen = 4;
					else if (e->attr == 0x20)
						e->pen = 3;
				}
			}
			d->syms = d->charset;
			return n;
		}
		// the lexical database
		lex->lexStatus = v->status;
		if (3 < v->status)
			goto done;
		if (d->lastKind == 2 && d->lastState == v->state && d->lastMask == v->dmask
		 && (result = d->lastCount) != 0)
			goto done;
		lex->flags = v->kind;
		lex->lexState = v->state;
		lex->lexStatus = v->status;
		lex->lexMask = v->dmask;
		lex->wlen = v->wlen;
		HWRStrCpy(lex->word, v->word + (v->len - v->wlen));
		result = GF_LexDbSymbolSet(lex, buf);
		SortSymBuf(result, buf);
		for (long i = 0; i < result; i++)
			buf[i].pen = (UByte) (buf[i].pen - 2);
		d->lastState = v->state;
		d->lastMask = v->dmask;
		d->lastKind = 2;
		d->lastCount = result;
		goto done;
	}
	if (kind == 0x20)
	{
		if (d->nEPunct == 0)
		{
			long n = 0;
			for (long i = 0; i < 0x10; i++)
			{
				fw_buf_type* e = &d->ePunct[i];
				e->sym = (UByte) lex->ePunct[i];
				if (e->sym == 0)
					break;
				e->attr = 0;
				e->state = 0;
				e->pen = 2;
				e->status = 3;
				n++;
			}
			d->nEPunct = n;
		}
		d->syms = d->ePunct;
		return d->nEPunct;
	}
	if (kind != 0x41 && kind != 0x81)
		return 0;
vocabulary:
	lex->vocStatus = v->status;
	if (3 < v->status)
		goto done;
	if (d->lastKind == v->kind && d->lastState == v->state && d->lastMask == v->dmask
	 && (result = d->lastCount) != 0)
		goto done;
	lex->flags = v->kind;
	lex->vocState = v->state;
	lex->vocStatus = v->status;
	lex->vocMask = v->dmask;
	lex->wlen = v->wlen;
	HWRStrCpy(lex->word, v->word + (v->len - v->wlen));
	if ((v->flags & 0x80) != 0)
		lex->word[0] = (char) ToLower((UByte) lex->word[0]);
	if (SetupVocHandle(lex, v->kind) == 0)
	{
		count = GF_VocSymbolSet(lex, buf);
		SortSymBuf(count, buf);
	}
	if (v->kind != 1)
		for (long i = 0; i < count; i++)
			buf[i].pen = 1;
	{
		fw_buf_type* p = &buf[count];
		result = count;
		for (long i = 0; v->status == 1 && capFirst != 0 && i < count; i++)
		{
			if (IsLower(buf[i].sym))
			{
				*p = buf[i];
				p->sym = (UByte) ToUpper(buf[i].sym);
				p->status |= 0x80;
				p++;
				result++;
			}
		}
	}
	d->lastState = v->state;
	d->lastMask = v->dmask;
	d->lastKind = v->kind;
	d->lastCount = result;
done:
	d->syms = buf;
	return result;
}


// ROM 0x0027e1e8 XrlvGetSymAliases__FUciN22PUcP9xrcm_type
// A letter (in one variant, or 0xf any) read again, traced, over the xrs
// from `first` to `last` alone: the xrs its layout's steps read - each
// step on a new prototype, with how it was reached in the top two bits
// (3 the diagonal, 2 an xr the prototype read, 1 the same after a step
// that stayed on the prototype), up to twelve and a nought after.
// ==> 0.
long
XrlvGetSymAliases(UByte sym, long var, long first, long last, UByte* aliases, xrcm_type* x)
{
	ULong savedFlags = x->flags;
	long savedN = x->nXrs;
	long savedCaps = x->caps;
	xrinp_type* savedXrinp = x->xrinp;
	x->flags = (savedFlags & ~2) | 0x10;
	x->xrinp = savedXrinp + first;
	x->nXrs = last - first + 1;
	x->wordEnd = last - first - 1;
	SetInpLineByValue(100, 0, 3, x);
	x->pos = 0;
	x->varMasks[0] = var == 0xf ? 0 : (short) ~(1 << var);
	x->word[0] = (char) sym;
	x->word[1] = 0;
	x->caps = 5;
	if (CountWord((const UByte*) x->word, 5, x->flags | 4, x) == 0 && x->layout != nil)
	{
		XrLayoutLetter* l = x->layout->letters[0];
		long k = 0;
		ULong prevXrp = 0xffffffff;
		Boolean same = false;
		// DEVIATION: a first step reached by skipping an xr takes whatever
		// the caller left in the ROM's register; the host has nothing
		UByte type = 0;
		if (l->count == 0)
			aliases[0] = 0;
		else
		{
			for (long s = 0; s < l->count; s++)
			{
				UByte* step = l->steps[s];
				if (step[1] == prevXrp)
					same = true;
				else
				{
					if (step[2] == 3)
						type = 3;
					else if (step[2] == 2)
						type = same ? 1 : 2;
					aliases[k] = (UByte) (step[0] | (type << 6));
					prevXrp = step[1];
					k++;
					same = false;
				}
			}
			if (k < 0xc)
				aliases[k] = 0;
		}
		FreeLayout(x);
	}
	x->flags = savedFlags;
	x->wordEnd = 0;
	x->varMask = 0;
	x->varMasks[0] = 0;
	x->xrinp = savedXrinp;
	x->nXrs = savedN;
	x->caps = savedCaps;
	return 0;
}


// ROM 0x0027e38c XrlvSortAns__FP14xrlv_data_type
// The last location's readings scored as answers: a dictionary word
// closed (its capitals, its end), a reading that is not a whole word
// charged twelve, the score over the word's length in thousandths (a
// dictionary word one more), at most 2000; then sorted best first.
long
XrlvSortAns(xrlv_data_type* d)
{
	XrlvPos* P = d->pos[d->nLocs - 1];
	XrlvAnswer* a = d->ans;
	for (long i = 0; i < P->used; i++, a++)
	{
		xrlv_var_data_type* E = &P->entries[i];
		if ((E->kind & 3) != 0)
		{
			XrlvCheckDictCap(E, d);
			XrlvApplyWordEndInfo(d->nLocs - 1, E, d);
		}
		long pen = E->status < 3 ? 0xc : 0;
		long s = RtSdiv(d->f020, (VarScore(E) - d->f024 - pen) * 1000);
		if ((E->kind & 3) != 0)
			s++;
		if (s < 0)
			s = 0;
		else if (2000 < s)
			s = 2000;
		a->score = (short) s;
		a->pen = (UByte) pen;
		a->index = (UByte) i;
	}
	Boolean sorted;
	do
	{
		sorted = true;
		if (P->used < 2)
			return 0;
		for (long i = 1; i < P->used; i++)
		{
			if (d->ans[i - 1].score < d->ans[i].score)
			{
				XrlvAnswer t = d->ans[i - 1];
				d->ans[i - 1] = d->ans[i];
				d->ans[i] = t;
				sorted = false;
			}
		}
	} while (!sorted);
	return 0;
}


// ROM 0x0027e4e8 SetupVocHandle__FP13lex_data_typei
long
SetupVocHandle(lex_data_type* lex, long kind)
{
	if (kind == 1 || kind == 0x41 || kind == 0x81)
	{
		lex->vocKind = kind;
		return 0;
	}
	return 1;
}
