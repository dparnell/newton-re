/*
	File:		XrMatrix.cpp

	Contains:	The cursive reader's xr matching matrix (see XrMatrix.h).

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.
*/

#include "XrMatrix.h"
#include "LowLevel.h"
#include "CursiveReader.h"
#include "XrDomains.h"
#include "toolbox/ByteOrder.h"

#include <string.h>

// DEVIATION: the host carves a trace block on eight-byte boundaries,
// where the ROM rounds to four, so that the pointers in what it carves
// are aligned.
enum { kTraceAlign = 8, kTraceBlockSize = 0xff0 };

static inline short	RCSigned(rc_type* rc, ULong offset)	{ return (short) RCGetH(rc, offset); }


// ROM 0x003606e4 xrmatr_alloc__FP7rc_typeP11xrdata_typePP9xrcm_type
// The matrix for the word's xrs: its lines, and with rc +0x0a bit 0 a
// block of 0x208 bytes for each break in which what each letter read
// from it is remembered.  DEVIATION: the header is sizeof(xrcm_type)
// rather than the ROM's 0x2c8 (and the lines start after it rather than
// at 0x2cc).
long
xrmatr_alloc(rc_type* rc, xrdata_type* xr, xrcm_type** matrix)
{
	xrd_el_type* elements = (xrd_el_type*) xr->fElements;
	long n;
	long breaks = 0;
	for (n = 0; n < xr->fLength; n++)
		if (XrcmIsBreak(elements[n].type))
			breaks++;
	long lineLen = (n - n % 4) + 8;
	long size = lineLen * 0x2e + (long) sizeof(xrcm_type);
	if ((RCSigned(rc, 0x0a) & 1) != 0)
		size += breaks * 0x208;
	size += 8;
	xrcm_type* x = (xrcm_type*) HWRMemoryAlloc(size);
	if (x == nil)
		return 1;
	// ROM QUIRK: the block is cleared a halfword's worth at most - a
	// word of many breaks has more than 0xffff bytes, and the rest is
	// left as the allocator gave it
	memset(x, 0, size & 0xffff);
	x->size = size;
	UByte* p = (UByte*) x + sizeof(xrcm_type);
	x->inpLine = (short*) p;
	p += lineLen * 2;
	x->outLine = (short*) p;
	p += lineLen * 2;
	for (long v = 0; v < 0x10; v++)
	{
		x->varOut[v] = (short*) p;
		p += lineLen * 2;
	}
	x->wwcLine = (short*) p;
	p += lineLen * 2;
	x->xrinp = (xrinp_type*) p;
	if ((RCSigned(rc, 0x0a) & 1) != 0)
	{
		x->capsBuf = p + lineLen * 8;
		x->capsSize = breaks * 0x208;
	}
	x->caps = RCSigned(rc, 0x1e);
	x->caseFlags = RCSigned(rc, 0x1e);
	x->rcFlags = RCSigned(rc, 0x0a);
	x->style = RCSigned(rc, 0x04);
	x->lang = RCSigned(rc, 0x06);
	x->flags |= (RCSigned(rc, 0x24) != 0) | 2;
	x->wwc = RCSigned(rc, 0x1c);
	x->nXrs = n;
	x->dti = (DTIHeader*) rc->fDTI;
	// DEVIATION: with no letter table the ROM reads the word at 0xa4 of
	// whatever is mapped at nought (it never happens on the machine, whose
	// word domain always has the table loaded); the host has none
	x->learnInfo = x->dti != nil ? x->dti->fLearnInfoPtr : nil;
	for (long i = 0; i < n; i++)
		memcpy(&x->xrinp[i], &elements[i], sizeof(xrinp_type));
	SetWWCLine(x->wwc, x);
	x->f1dc = n * 10 - 10;
	change_direction(0, x);
	*matrix = x;
	return 0;
}


// ROM 0x00360908 xrmatr_dealloc__FPP9xrcm_type
void
xrmatr_dealloc(xrcm_type** matrix)
{
	if (*matrix != nil)
	{
		HWRMemoryFree((Ptr) *matrix);
		*matrix = nil;
	}
}


// ROM 0x00360938 SetInpLineByValue__FiN21P9xrcm_type
// The input line from `first` for `count` positions: `value` there, and
// each position after it the xrs' penalties less (4 more at a position
// that is no break, with rc +0x0a bit 1); nought before and after.
long
SetInpLineByValue(long value, long first, long count, xrcm_type* x)
{
	short* inp = x->inpLine;
	if (first > 0)
		inp[first - 1] = 0;
	inp[first] = (short) value;
	long acc = 0;
	long i = first;
	long limit = first + count;
	for (;;)
	{
		i++;
		if (!(i < limit && i < x->nXrs))
			break;
		acc += x->xrinp[i].penalty;
		inp[i] = (short) (value - acc);
		if ((x->rcFlags & 2) != 0 && x->brk[i] == 0)
			inp[i] = (short) (inp[i] - 4);
	}
	inp[i] = 0;
	x->inpSt = first;
	x->inpEnd = i;
	return 0;
}


// ROM 0x003609f4 SetOutLine__FPsiT2P9xrcm_type
long
SetOutLine(short* line, long first, long count, xrcm_type* x)
{
	short* out = x->outLine + first;
	for (long i = 0; i < count; i++)
		out[i] = line[i];
	return 0;
}


// ROM 0x00360a34 MergeWithOutLine__FPsiT2P9xrcm_type
// A line (the other case's) merged into the out line, the better of the
// two at each position; traced, the letter's trace records which case
// won each position.
long
MergeWithOutLine(short* line, long first, long count, xrcm_type* x)
{
	long outSt = x->outSt;
	long outEnd = x->outEnd;
	long end = first + count;
	short* out = x->outLine;
	long lo = first < outSt ? first : outSt;
	long hi = outEnd < end ? end : outEnd;
	if (x->nXrs < hi)
		hi = x->nXrs;
	if ((x->flags & 4) == 0)
	{
		if (x->fwd == 0)
			return 0;
		for (long i = lo; i < hi; i++)
		{
			if (first <= i)
			{
				if (i < outSt || (i < end && outEnd <= i))
					out[i] = line[i - first];
				else if (i < end && line[i - first] >= out[i])
					out[i] = line[i - first];
			}
		}
	}
	else
	{
		XrTraceLetter* t = x->tletter;
		if (x->fwd == 0)
		{
			for (long i = lo; i < hi; i++)
				t->caseOfPos[i] = 1;
			return 0;
		}
		t->start = lo;
		t->end = hi;
		for (long i = lo; i < hi; i++)
		{
			UByte which;
			if (i < first)
				which = 1;
			else if (i < outSt || (i < end && outEnd <= i))
			{
				out[i] = line[i - first];
				which = 0;
			}
			else if (end <= i || line[i - first] < out[i])
				which = 1;
			else
			{
				out[i] = line[i - first];
				which = 0;
			}
			t->caseOfPos[i] = which;
		}
	}
	x->outSt = lo;
	x->outEnd = hi;
	return 0;
}


// ROM 0x00360bdc GetOutLine__FPsiT2P9xrcm_type
void
GetOutLine(short* line, long first, long count, xrcm_type* x)
{
	short* out = x->outLine + first;
	long outSt = x->outSt;
	long outEnd = x->outEnd;
	for (long i = first; i < first + count; i++, out++, line++)
		*line = (i < outSt || outEnd <= i) ? 0 : *out;
}


// ROM 0x00360c50 TraceAlloc__FiP9xrcm_type
// The trace for a word of `letters` letters: one block of 0xff0 bytes to
// carve, with the letters' out lines after it.
long
TraceAlloc(long letters, xrcm_type* x)
{
	XrTrace* t = (XrTrace*) HWRMemoryAlloc(letters * x->nXrs * 2 + 0x1000);
	if (t == nil)
		return 1;
	memset(t, 0, sizeof(XrTrace));
	t->blocks[0] = (UByte*) t;
	t->nBlocks = 0;
	t->used = (sizeof(XrTrace) + kTraceAlign - 1) & ~(kTraceAlign - 1);
	t->cur = (UByte*) t;
	t->nLines = 0;
	t->lines = (short*) ((UByte*) t + kTraceBlockSize);
	x->trace = t;
	return 0;
}


// ROM 0x00360cc8 TraceAddAlloc__FP9xrcm_type
long
TraceAddAlloc(xrcm_type* x)
{
	XrTrace* t = x->trace;
	UByte* block;
	if (t != nil && t->nBlocks < 9 && (block = (UByte*) HWRMemoryAlloc(kTraceBlockSize)) != nil)
	{
		t->nBlocks++;
		t->blocks[t->nBlocks] = block;
		t->cur = block;
		t->used = 0;
		return 0;
	}
	return 1;
}


// ROM 0x00360d24 TraceDealloc__FP9xrcm_type
long
TraceDealloc(xrcm_type* x)
{
	XrTrace* t = nil;
	if (x != nil)
		t = x->trace;
	if (x != nil && t != nil)
	{
		for (long i = t->nBlocks; ; i--)
		{
			if (i < 0)
			{
				x->trace = nil;
				return 0;
			}
			UByte* block = t->blocks[i];
			if (block == nil)
				break;
			HWRMemoryFree((Ptr) block);
		}
	}
	return 1;
}


// ROM 0x00360d78 CreateLayout__FP9xrcm_type
// The trace walked back from the word's end: for each letter, last to
// first, the case and variant that won the position it ends at, and from
// the variant's last prototype back to its first the steps each cell was
// reached by.
long
CreateLayout(xrcm_type* x)
{
	XrLayout* layout = nil;
	if (x->trace != nil)
	{
		if (x->layout != nil)
			HWRMemoryFree((Ptr) x->layout->self);
		x->layout = nil;
		long len = HWRStrLen(x->word);
		long size = len * sizeof(XrLayoutLetter) + sizeof(XrLayout);
		layout = (XrLayout*) HWRMemoryAlloc(size);
		if (layout == nil)
			return 1;
		memset(layout, 0, sizeof(XrLayout));
		long used = sizeof(XrLayout);
		for (long i = 0; i < len; i++)
		{
			layout->letters[i] = (XrLayoutLetter*) ((UByte*) layout + used);
			memset(layout->letters[i], 0, sizeof(XrLayoutLetter));
			used += sizeof(XrLayoutLetter);
		}
		layout->self = layout;
		layout->size = size;
		layout->used = used;
		long pos = x->wordEnd;
		if (pos < 1)
			pos = x->outEnd - 1;
		for (;;)
		{
			len--;
			if (len < 0)
			{
				x->layout = layout;
				return 0;
			}
			XrLayoutLetter* l = layout->letters[len];
			XrTraceLetter* tl = x->trace->letters[len];
			if (pos < tl->start || tl->end <= pos)
				break;
			UByte which = tl->caseOfPos[pos];
			XrTraceSym* ts = tl->syms[which];
			l->sym = tl->sym[which];
			l->end = (UByte) pos;
			if (pos < ts->start || ts->end <= pos)
				break;
			UByte var = ts->varOfPos[pos];
			XrTraceVar* tv;
			if (var > 0xf || (tv = ts->vars[var]) == nil)
				break;
			l->var = var;
			long n = x->nXrs;
			long count = 0;
			long xi = tv->len - 1;
			while (count < n + 0xc && xi >= 0)
			{
				XrTraceXr* txr = tv->xrs[xi];
				long st = txr[2];
				UByte how;
				if (pos < st || st + txr[3] <= pos || (how = txr[4 + (pos - st)]) > 3)
					goto fail;
				l->steps[count][0] = (UByte) pos;
				l->steps[count][1] = (UByte) xi;
				l->steps[count][2] = how;
				if (how == 1)
					pos--;
				else if (how == 2)
					xi--;
				else if (how == 3)
				{
					xi--;
					pos--;
				}
				count++;
			}
			l->count = (UByte) count;
			l->start = (UByte) pos;
			// the steps were found last first: turned round
			long c = count & 0xff;
			for (long k = 0; k < (long) ((ULong) c >> 1); k++)
			{
				c--;
				UByte step[4];
				memcpy(step, l->steps[c], 4);
				memcpy(l->steps[c], l->steps[k], 4);
				memcpy(l->steps[k], step, 4);
			}
		}
	}
fail:
	if (layout != nil)
		HWRMemoryFree((Ptr) layout);
	return 1;
}


// ROM 0x00360fd8 change_direction__FiP9xrcm_type
// The matrix turned to read the xrs forwards (0) or backwards (1; 2 or
// more: the other way from now): the xrs between the first and the last
// reversed, the remembered reads forgotten, and the breaks numbered in
// the new order.
long
change_direction(long dir, xrcm_type* x)
{
	ULong old = x->dir;
	if (1 < dir)
		dir = old ^ 1;
	x->dir = dir;
	if ((ULong) x->dir != old)
	{
		long k = x->nXrs - 1;
		long half = k / 2;
		for (long j = 1; j <= half; j++, k--)
		{
			xrinp_type t = x->xrinp[k];
			x->xrinp[k] = x->xrinp[j];
			x->xrinp[j] = t;
		}
		if (x->capsBuf != nil)
			memset(x->capsBuf, 0, x->capsSize);
	}
	UByte b = 1;
	if (x->dir == 0)
	{
		x->fwd = 1;
		x->merge = 1;
		for (long i = 1; i <= x->nXrs - 1; i++)
			x->brk[i] = XrcmIsBreak(x->xrinp[i].type) ? b++ : 0;
	}
	else
	{
		x->fwd = 0;
		x->merge = 0;
		long n = x->nXrs;
		for (long i = 2; i <= n - 1; i++)
			x->brk[i - 1] = XrcmIsBreak(x->xrinp[i].type) ? b++ : 0;
		x->brk[n - 1] = b;
	}
	SetWWCLine(x->wwc, x);
	return 0;
}


// ROM 0x00361190 FreeLayout__FP9xrcm_type
void
FreeLayout(xrcm_type* x)
{
	XrLayout* layout = nil;
	if (x != nil)
		layout = x->layout;
	if (x != nil && layout != nil)
	{
		HWRMemoryFree((Ptr) layout->self);
		x->layout = nil;
	}
}


// ROM 0x003611c4 TDwordAdvance__FiP9xrcm_type
// `size` bytes carved out of the trace's current block, a new block
// taken when it is full.  ==> nil when there is no room.
void*
TDwordAdvance(long size, xrcm_type* x)
{
	long used = x->trace->used;
	long next = ((used + size - 1) & ~(kTraceAlign - 1)) + kTraceAlign;
	if (kTraceBlockSize - 1 < next)
	{
		if (TraceAddAlloc(x) != 0)
			return nil;
		used = x->trace->used;
		next = ((used + size - 1) & ~(kTraceAlign - 1)) + kTraceAlign;
		if (!(next < kTraceBlockSize))
			return nil;
	}
	x->trace->used = next;
	return x->trace->cur + used;
}


// ROM 0x00361248 SetWWCLine__FiP9xrcm_type
// What reading up to each position costs a word: 40 less `wwc` per xr.
long
SetWWCLine(long wwc, xrcm_type* x)
{
	for (long i = 0; i < x->nXrs; i++)
		x->wwcLine[i] = (short) (i * (0x28 - wwc));
	x->wwcValue = wwc;
	return 0;
}


// ROM 0x00361294 FillLetterWeights__FPsP9xrcm_type
// What each letter of the word added to the score: its out line's value
// where its layout says it ended, less 100, less the letter before's.
long
FillLetterWeights(short* lines, xrcm_type* x)
{
	if (lines == nil || x == nil || x->layout == nil)
		return 1;
	long offset = 0;
	long before = 0;
	for (long i = 0; i < 0x18; i++)
	{
		if (x->word[i] == 0)
			return 0;
		XrLayoutLetter* l = x->layout->letters[i];
		long value = lines[offset + l->steps[l->count - 1][0]] - 100;
		x->weights[i] = (short) (value - before);
		offset += x->nXrs;
		before = value;
	}
	return 0;
}


// ROM 0x0036132c CountWord__FPUciT2P9xrcm_type
// A word counted against the xrs letter by letter, from the input line
// already set: each letter's out line cut down to the stretch within 50
// of its best position (and 30 after it) becomes the next letter's input
// line.  `caps` says which cases a letter may be read in (1/4 as
// written, 2/8 the upper case of a lower-case letter, 0x10 the lower of
// an upper - the first bits for the word's first letter, the second for
// the rest); flag 4 traces the reading and makes the layout.  ==> 0, 1.
long
CountWord(const UByte* word, long caps, long flags, xrcm_type* x)
{
	ULong savedFlags = x->flags;
	x->flags = flags;
	long len = HWRStrLen((const char*) word);
	long traced = 0;
	if (len < 0x18)
	{
		HWRStrCpy(x->word, (const char*) word);
		traced = flags & 4;
		if (traced == 0 || TraceAlloc(len, x) == 0)
		{
			x->bestPos = x->pos;
			for (long i = 0; i < len; i++)
			{
				x->letter = x->word[i];
				x->caseFlags = 5;
				x->pos = x->bestPos;
				x->varMask = x->varMasks[i];
				if (IsAlpha(x->letter))
				{
					long asWritten;
					if ((i == 0 && x->dir == 0) || (i == len - 1 && x->dir != 0))
					{
						if ((caps & 2) != 0)
							x->caseFlags |= 0xa;
						if ((caps & 0x10) != 0)
							x->caseFlags |= 0x15;
						asWritten = caps & 1;
					}
					else
					{
						if ((caps & 8) != 0)
							x->caseFlags |= 0xa;
						if ((caps & 0x10) != 0)
							x->caseFlags |= 0x15;
						asWritten = caps & 4;
					}
					if (asWritten == 0)
						x->caseFlags &= ~5;
				}
				if (traced != 0)
				{
					XrTraceLetter* t = (XrTraceLetter*) TDwordAdvance(sizeof(XrTraceLetter), x);
					if (t == nil)
						goto fail;
					memset(t, 0, sizeof(XrTraceLetter));
					x->trace->letters[i] = t;
					x->letterIdx = i;
					x->tletter = t;
				}
				if (CountLetter(x) != 0)
					goto fail;
				if (traced != 0)
				{
					memcpy(x->trace->lines + x->trace->nLines, x->outLine, x->nXrs * 2);
					x->trace->nLines += x->nXrs;
				}
				short* out = x->outLine;
				long lowest = out[x->bestPos] - 0x32;
				long first = x->outSt;
				while (first < x->bestPos - 2 && out[first] < lowest)
					first++;
				long last = x->outEnd - 1;
				while (x->bestPos + 2 < last && out[last] < lowest + 0x14)
					last--;
				SetInpLine(&out[first], first, last + 1 - first, x);
			}
			if (traced != 0)
			{
				if (CreateLayout(x) != 0)
					goto fail;
				FillLetterWeights(x->trace->lines, x);
				TraceDealloc(x);
			}
			x->flags = savedFlags;
			return 0;
		}
	}
fail:
	x->flags = savedFlags;
	TraceDealloc(x);
	return 1;
}


// ROM 0x00361610 CountLetter__FP9xrcm_type
// One letter counted, in the case it is written in and, when the case
// flags allow, the other: the better of the two kept (forwards the two
// lines are merged, the trace saying which won where), and how much
// better it was.
long
CountLetter(xrcm_type* x)
{
	ULong caseFlags = x->caseFlags;
	ULong savedFlags = x->flags;
	UByte ch = x->letter;
	UByte other = 0;
	x->flags = savedFlags & ~0x28;
	UByte first = ch;
	if (IsAlpha(ch))
	{
		if ((caseFlags & 4) == 0)
			first = 0;
		if (IsLower(ch))
		{
			if ((caseFlags & 8) != 0)
				other = (UByte) ToUpper(ch);
		}
		else if ((caseFlags & 0x10) != 0)
			other = (UByte) ToLower(ch);
		if ((first == 0 || other == 0) && x->learnInfo != nil && (x->flags & 0x10) == 0)
			x->flags |= 8;
	}
	UByte second = other;
	if (first == 0)
	{
		second = 0;
		first = other;
	}
	if (first != 0 && second != 0)
		x->flags |= 0x40;
	x->sym = first;
	x->bestSym = first;
	x->symDiff = 0;
	if ((x->flags & 4) != 0)
	{
		XrTraceSym* ts = (XrTraceSym*) TDwordAdvance(sizeof(XrTraceSym), x);
		if (ts == nil)
			goto fail;
		memset(ts, 0, sizeof(XrTraceSym));
		x->tsym = ts;
		x->tletter->syms[0] = ts;
		x->tletter->sym[0] = first;
		x->tsym->sym = first;
		x->caseIdx = 0;
	}
	if (CountSym(x) != 0)
		goto fail;
	if ((x->flags & 4) != 0)
	{
		x->tletter->start = x->tsym->start;
		x->tletter->end = x->tsym->end;
	}
	if (second != 0)
	{
		x->sym = second;
		if (IsLower(second))
			x->flags |= 0x20;
		long bestVal = x->bestVal;
		long bestPos = x->bestPos;
		long bestScore = x->bestScore;
		long endVal = x->endVal;
		long outSt = x->outSt;
		long outEnd = x->outEnd;
		short line[kXrcmMaxXrs];
		UByte varOfPos[kXrcmMaxXrs];
		GetOutLine(line, outSt, outEnd - outSt, x);
		memcpy(varOfPos, x->varOfPos, sizeof(varOfPos));
		if ((x->flags & 4) != 0)
		{
			XrTraceSym* ts = (XrTraceSym*) TDwordAdvance(sizeof(XrTraceSym), x);
			if (ts == nil)
				goto fail;
			memset(ts, 0, sizeof(XrTraceSym));
			x->tsym = ts;
			x->tletter->syms[1] = ts;
			x->tletter->sym[1] = second;
			x->tsym->sym = second;
			x->caseIdx = 1;
		}
		if (CountSym(x) != 0)
			goto fail;
		long score = x->bestScore;
		long diff;
		if (bestScore < score)
		{
			x->bestSym = second;
			diff = score - bestScore;
		}
		else
			diff = bestScore - score;
		diff++;
		if (0xff < diff)
			diff = 0xff;
		x->symDiff = (UByte) diff;
		if (x->endVal < endVal)
			x->endVal = endVal;
		if (score <= bestScore || x->merge == 0)
		{
			// the first case's line put back (backwards, even when the
			// second read better: only forwards are the two merged)
			x->bestVal = bestVal;
			x->bestScore = bestScore;
			x->outSt = outSt;
			x->outEnd = outEnd;
			x->bestPos = bestPos;
			SetOutLine(line, outSt, outEnd - outSt, x);
			memcpy(x->varOfPos, varOfPos, sizeof(varOfPos));
		}
		if (x->merge != 0)
			MergeWithOutLine(line, outSt, outEnd - outSt, x);
	}
	x->flags = savedFlags;
	return 0;

fail:
	x->flags = savedFlags;
	return 1;
}


// ROM 0x00361934 CountSym__FP9xrcm_type
// A letter in one case counted: each of its variants the writer's style
// allows through CountVar, and the variants merged.  With the matrix's
// memory of breaks, a letter already read from the same break is not
// read again - its best end and how much it added are remembered and the
// out line drawn from them.  ==> 0, 1 for a letter the table does not
// have or no memory.
long
CountSym(xrcm_type* x)
{
	long cached = 0;
	DTIHeader* dti = x->dti;
	ULong sym = OSToRec(x->sym) & 0xff;
	if (sym < 0x20 || 0xa2 <= sym)
		return 1;
	if (x->capsBuf != nil && (x->flags & 0x44) == 0)
	{
		cached = 2;
		UByte b = x->brk[x->pos];
		if (x->capsBuf[b * 0x208 + sym * 4 - 0x7f] != 0)
			cached = 1;
		if (b == 0)
			cached = 0;
	}
	if ((cached & 1) == 0)
	{
		// DEVIATION: no letter table is no such letter (the ROM would read
		// its offsets from low memory)
		if (dti == nil)
			return 1;
		UByte* descriptor = nil;
		UByte* table = dti->fRAMDTEMainPtr;
		ULong offset = 0;
		if (table != nil)
			offset = GetBigEndianWord(table + sym * 4);
		if (table == nil || offset == 0 || (descriptor = table + offset) == nil)
		{
			table = dti->fDTEMain;
			if (table != nil)
				offset = GetBigEndianWord(table + sym * 4);
			if (table == nil || offset == 0)
				return 1;
			descriptor = table + offset;
		}
		if (descriptor == nil || descriptor[0] == 0)
			return 1;
		x->symDescr = descriptor;
		if ((x->flags & 1) == 0)
			x->vexes = descriptor + 0x14;
		else
			x->vexes = x->learnInfo + sym * 0x10 - 0x200;
		UByte* xrps = descriptor + 0x54;
		for (long v = 0; v < 0x10; v++)
		{
			UByte len = descriptor[4 + v];
			Boolean use = len != 0 && v < descriptor[0];
			ULong flags = x->flags;
			if ((flags & 2) != 0 && (x->vexes[v] & 7) == 7)
				use = false;
			UByte groups = descriptor[0x24 + v];
			if ((groups & ((x->style << 4) & 0xff)) == 0)
				use = false;
			if ((flags & 0x20) != 0 && (groups & 1) != 0)
				use = false;
			if (((short) x->varMask & (1 << v)) != 0)
				use = false;
			if ((flags & 8) != 0)
			{
				long bit = v & 7;
				if ((x->learnInfo[0x820 + ((v + (long) sym * 0x10 - 0x200) >> 3)] & (1 << bit)) != 0)
					use = false;
			}
			if (use)
			{
				x->varXrps = xrps;
				x->varLen = len;
				x->inp = x->inpLine;
				x->out = x->varOut[v];
				if ((flags & 4) != 0)
				{
					XrTraceVar* tv = (XrTraceVar*) TDwordAdvance(sizeof(XrTraceVar), x);
					if (tv == nil)
						return 1;
					memset(tv, 0, sizeof(XrTraceVar));
					x->tvar = tv;
					x->tsym->vars[v] = tv;
					tv->sym = x->sym;
					tv->var = (UByte) v;
					tv->len = len;
					x->varIdx = v;
				}
				if (CountVar(x) != 0)
					return 1;
				x->varRes[v][0] = (UByte) x->varSt;
				x->varRes[v][1] = (UByte) x->varEnd;
			}
			else
			{
				x->varRes[v][1] = 0;
				x->varRes[v][0] = 0;
			}
			xrps += len * kXrpSize;
		}
		if (MergeVarResults(x) != 0)
			return 1;
	}
	if (cached != 0)
	{
		long last = x->nXrs - 1;
		long pos = x->pos;
		UByte b = x->brk[pos];
		if (cached == 1)
		{
			UByte* entry = x->capsBuf + b * 0x208 + sym * 4;
			long end = entry[-0x7f];
			long value = x->inpLine[pos] + (signed char) entry[-0x80];
			short* out = x->outLine;
			out[end] = (short) value;
			if (1 < end)
				out[end - 2] = (short) (value - 0x14);
			if (0 < end)
				out[end - 1] = (short) (value - 0xa);
			if (end < last)
				out[end + 1] = (short) (value - 0xa);
			if (end <= last)
				out[end + 2] = (short) (value - 0x14);
			long st = end - 2;
			if (st <= 0)
				st = 0;
			x->outSt = st;
			long en = end + 2;
			if (last < en)
				en = last + 1;
			x->outEnd = en;
			x->bestPos = end;
			x->bestVal = value;
			x->bestScore = value * 4 - x->wwcLine[end];
			x->endVal = (end == last) ? value : 0;
		}
		else if (cached == 2)
		{
			long bestPos = x->bestPos;
			long d = x->outLine[bestPos] - x->inpLine[pos];
			ULong bb = x->brk[bestPos];
			if (bb == b && d >= 0)
				bb = 0;
			if (b < bb || d < -0x14)
			{
				if (d < -0x7f)
					d = -0x7f;
				else if (0x7f < d)
					d = 0x7f;
				UByte* entry = x->capsBuf + b * 0x208 + sym * 4;
				entry[-0x7f] = (UByte) bestPos;
				entry[-0x80] = (UByte) d;
			}
		}
	}
	return 0;
}


// ROM 0x00361d90 CountVar__FP9xrcm_type
// One variant counted: its prototypes one column after another (last to
// first when reading backwards), each column's line the next one's
// input; the range read grows by one position a prototype and its start
// moves on once past half the variant.
long
CountVar(xrcm_type* x)
{
	long pos = x->pos;
	if (pos < 1)
		pos = 1;
	long len = x->varLen;
	long limit = x->inpEnd + len;
	if (x->nXrs < x->inpEnd + len)
		limit = x->nXrs;
	long step;
	UByte* xrp;
	if (x->dir == 0)
	{
		step = 1;
		xrp = x->varXrps;
	}
	else
	{
		step = -1;
		xrp = x->varXrps + len * kXrpSize - kXrpSize;
	}
	x->xrp = xrp;
	for (long k = 0; k < len; k++)
	{
		long add = (len >> 1) < k ? k - (len >> 1) : 0;
		x->st = x->inpSt + add;
		if (pos - 1 < x->inpSt + add)
			x->st = pos - 1;
		long end = x->inpEnd + k + 1;
		x->end = end;
		if (limit < end)
			x->end = limit;
		if ((x->flags & 4) == 0)
			CountXrAsm(x);
		else
		{
			long e = x->end;
			long s = x->st;
			XrTraceXr* t = (XrTraceXr*) TDwordAdvance(0x7c - (0x78 - (e - s)), x);
			if (t == nil)
				return 1;
			x->txr = t;
			x->tvar->xrs[k] = t;
			t[0] = x->sym;
			t[1] = x->xrp[0];
			t[2] = (UByte) s;
			t[3] = (UByte) (e - s);
			TCountXrAsm(x);
		}
		x->inp = x->out;
		x->xrp += step * kXrpSize;
	}
	x->varSt = x->st;
	x->varEnd = x->end;
	return 0;
}


// ROM 0x00361ef8 MergeVarResults__FP9xrcm_type
// The variants' lines merged into the letter's out line, each charged
// twice its vex, the variant that won each position noted; then the
// best position - the greatest four times the value less the length
// charge - and the value at the last xr.
long
MergeVarResults(xrcm_type* x)
{
	UByte* vexes = x->vexes;
	ULong lo = 0x78;
	ULong hi = 0;
	long count = 0;
	for (long v = 0; v < 0x10; v++)
	{
		ULong end = x->varRes[v][1];
		if (end != 0)
		{
			if (x->varRes[v][0] < lo)
				lo = x->varRes[v][0];
			if (hi < end)
				hi = end;
			count++;
		}
	}
	if (count == 0)
	{
		x->endVal = 0;
		x->bestVal = 0;
		x->bestPos = 0;
		x->bestScore = 0;
		x->outEnd = 0;
		x->outSt = 0;
		return 0;
	}
	short* out = x->outLine;
	for (ULong i = lo; (long) i < (long) hi; i++)
		out[i] = 0;
	if ((x->flags & 4) != 0)
	{
		x->tsym->start = (UByte) lo;
		x->tsym->end = (UByte) hi;
		x->tsym->nVars = (UByte) count;
	}
	for (long v = 0; v < 0x10; v++)
	{
		ULong st = x->varRes[v][0];
		ULong end = x->varRes[v][1];
		if (end == 0)
			continue;
		short* line = x->varOut[v];
		long vex = vexes[v] & 7;
		for (ULong i = st; (long) i < (long) end; i++)
		{
			long value = line[i] - vex * 2;
			if (out[i] < value)
			{
				out[i] = (short) value;
				x->varOfPos[i] = (UByte) v;
				if ((x->flags & 4) != 0)
					x->tsym->varOfPos[i] = (UByte) v;
			}
		}
	}
	if ((x->rcFlags & 2) != 0)
	{
		for (ULong i = lo; (long) i < (long) hi; i++)
			if (x->brk[i] == 0)
				out[i] = (short) (out[i] - 4);
	}
	x->outSt = lo;
	x->outEnd = hi;
	if (lo == 0)
		lo = 2;
	long best = 0;
	ULong bestPos = lo;
	for (ULong i = lo; (long) i < (long) hi; i++)
	{
		long score = out[i] * 4 - x->wwcLine[i];
		if (best <= score)
		{
			bestPos = i;
			best = score;
		}
	}
	x->bestPos = bestPos;
	x->bestScore = best;
	x->bestVal = out[bestPos];
	x->endVal = ((ULong) x->nXrs == hi) ? out[hi - 1] : 0;
	return 0;
}


// ROM 0x003621e4 SetInitialLine__FiP9xrcm_type
// The input line for a word's start: 100 at the first xr, falling by the
// xrs' penalties for `count` more.
void
SetInitialLine(long count, xrcm_type* x)
{
	SetInpLineByValue(100, 0, count, x);
	x->pos = 0;
}


// ROM 0x00362214 SetInpLine__FPsiT2P9xrcm_type
void
SetInpLine(short* line, long first, long count, xrcm_type* x)
{
	short* inp = x->inpLine;
	if (first > 0)
		inp[first - 1] = 0;
	long limit = first + count;
	long i = first;
	while (i < limit && i < x->nXrs)
		inp[i++] = *line++;
	inp[i] = 0;
	x->inpSt = first;
	x->inpEnd = i;
}


/*------------------------------------------------------------------------------
	T h e   i n n e r   l o o p s

	Hand-written assembly in the ROM; transcribed register by register.
	A prototype's tables are nibbles, two to a byte, the high nibble the
	even index; the prototype's first word, rotated right by eight, gives
	its byte 3 (what skipping the prototype costs) in its top byte and its
	byte 2 in its bottom one.
------------------------------------------------------------------------------*/

static inline long
Nibble(const UByte* table, ULong index)
{
	UByte b = table[index >> 1];
	return (index & 1) == 0 ? b >> 4 : b & 0xf;
}


// What the prototype gives the xr, or -1 when it does not read it at all
// (a prototype that only reads xrs next to a break, or an xr of a type
// the prototype gives nothing).
static inline long
XrMatch(const UByte* xrp, const xrinp_type* in)
{
	if ((in->attrib & 0x80) == 0 && (xrp[2] & 0x80) != 0)
		return -1;
	long type = Nibble(xrp + 0x04, in->type);
	if (type == 0)
		return -1;
	return type
		 + Nibble(xrp + 0x24, in->height)
		 + Nibble(xrp + 0x2c, in->shift)
		 + Nibble(xrp + 0x3c, in->orient)
		 + Nibble(xrp + 0x34, in->link);
}


// ROM 0x0038cd38 CountXrAsm
// One column: each position the best of the xr skipped (the position
// before in this column less the xr's penalty), the prototype skipped
// (the input line less the prototype's penalty) and the diagonal (the
// input line's position before, less 50, plus the match) - a tie going
// to the diagonal; nought after the last.
void
CountXrAsm(xrcm_type* x)
{
	const short* inp = x->inp;
	short* out = x->out;
	const xrinp_type* in = x->xrinp;
	const UByte* xrp = x->xrp;
	long skipXrp = xrp[3];
	long prevInp = 0;
	long prevOut = 0;
	long p = x->st;
	for (; p < x->end; p++)
	{
		long diag = prevInp - 0x32;
		prevInp = inp[p];
		long best = prevOut - in[p].penalty;
		if (best < prevInp - skipXrp)
			best = prevInp - skipXrp;
		long m = XrMatch(xrp, &in[p]);
		if (m >= 0)
			diag += m;
		if (best <= diag)
			best = diag;
		prevOut = best;
		out[p] = (short) best;
	}
	out[p] = 0;
}


// ROM 0x003ad244 TCountXrAsm
// CountXrAsm with the way each cell was reached written to the trace -
// 1 the xr skipped, 2 the prototype skipped, 3 the diagonal - a tie
// going to the earlier of the three rather than to the diagonal.
void
TCountXrAsm(xrcm_type* x)
{
	const short* inp = x->inp;
	short* out = x->out;
	const xrinp_type* in = x->xrinp;
	const UByte* xrp = x->xrp;
	UByte* how = x->txr + 4;
	long skipXrp = xrp[3];
	long prevInp = 0;
	long prevOut = 0;
	long p = x->st;
	for (; p < x->end; p++)
	{
		long diag = prevInp - 0x32;
		prevInp = inp[p];
		long skip = prevInp - skipXrp;
		long best = prevOut - in[p].penalty;
		long m = XrMatch(xrp, &in[p]);
		if (m >= 0)
			diag += m;
		UByte h;
		if (skip > best)
		{
			best = skip;
			h = 2;
		}
		else
			h = 1;
		if (diag > best)
		{
			best = diag;
			h = 3;
		}
		prevOut = best;
		out[p] = (short) best;
		*how++ = h;
	}
	out[p] = 0;
}
