/*
	File:		LowExchange.cpp

	Contains:	The cursive reader's low level: exchange, which writes the
				special elements AnalyzeLowData leaves as the xrs the reader
				reads.  See LowLevel.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	exchange walks the special elements once.  Each element's code (lk_begin
	and the passes after it gave it one) becomes an xr type - an upper or
	lower extremum by the height band it is in (the attr's 0x30), a stroke's
	start or end, an arc, an angle, a crossing, a dot or dash - with the
	element's height band, its extent and its point (hotpoint) in the
	filtered trace; AssignInputPenaltyAndStrict gives it the penalty the
	reader starts it at, GetLinkBetweenThisAndNextXr how the trace goes on
	to the next (a stick, an arc of a certain bend either way, an S or a Z),
	and MarkXrAsLastInLetter whether a letter usually ends there.  A break
	(type 1) stands at each end.  Then the points are mapped back to the
	original trace (buffer 2), each xr's box is measured over it, a break's
	neighbours are marked, check_xrdata puts in the crossing a gap in the
	writing stands for (PutZintoXrd), and FillXrFeatures adds the features
	the reader compares against its prototypes.

	The xrs' halfwords are big-endian bytes as the ROM writes them; the
	ROM copies GetBoxFromTrace's box into them with unaligned loads whose
	low half is the halfword *before* the address loaded (so a load at the
	top's address yields the left) - the order that comes out is left, top,
	right, bottom, which is what is written here.

	Nearly all of it was read from the disassembly: the decompiler loses
	the byte stores' halves and the unset registers.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LowLevel.h"
#include "ParaGraph.h"
#include "CursiveReader.h"

#include <string.h>


static inline Boolean
IsBreakCode(UByte c)
{
	return c == 0x12 || c == 1 || c == 0x13 || c == 0x14;
}


// ROM 0x00305c68 X_IsBreak__FP11xrd_el_type
Boolean
X_IsBreak(xrd_el_type* xr)
{
	UByte t = xr->type;
	return t == 3 || t == 4 || t == 2 || t == 1;
}


// ROM 0x00305bfc IsStrongElem__FP9SPEC_TYPE
// Whether an element is a firm one: not a dash or dot (0xd, 0x10), an
// arc (0xe, 0x11, 0x28, 0x29) or a movement (0x23..0x27).
Boolean
IsStrongElem(SPEC_TYPE* elem)
{
	switch (elem->code)
	{
	case 0xd: case 0xe: case 0x10: case 0x11:
	case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29:
		return false;
	}
	return true;
}


// ROM 0x002c854c AssignInputPenaltyAndStrict__FP9SPEC_TYPEP11xrd_el_type
// The penalty the reader starts the xr at: an arc's or a movement's own
// (its other byte - an arc with a second point takes that point's low
// byte, ROM QUIRK), none for a firm end, otherwise the type's penalty
// plus the height band's, six more for a dash or dot or a crossing where
// it should not be, and up to eight more (one for an arc) for how far its
// height band is from its firm neighbours', at most 0x12.
long
AssignInputPenaltyAndStrict(SPEC_TYPE* elem, xrd_el_type* xr)
{
	UByte height = xr->height;
	UByte code = elem->code;
	UByte pen;
	if (code == 0x28 || code == 0x29)
	{
		xr->penalty = elem->other;
		return 0;
	}
	if ((code == 0xe || code == 0x11) && elem->ipoint1 != -2)
	{
		xr->penalty = (UByte) elem->ipoint1;
		return 0;
	}
	if (code == 0x23 || code == 0x24)
	{
		xr->penalty = elem->other;
		return 0;
	}
	if (code == 0x14)
	{
		if ((elem->other & 0xa) != 0)
		{
			xr->penalty = 0;
			return 0;
		}
	}
	else if (code == 0xd)
	{
		if ((elem->other & 8) != 0)
		{
			xr->penalty = 2;
			return 0;
		}
	}
	else if ((code == 3 || code == 7 || code == 0xa || code == 9 || code == 0xc || code == 0xb)
		  && (elem->other & 8) != 0)
	{
		xr->penalty = 0;
		return 0;
	}

	pen = 0;
	if (xr->type < 0x40 && height < 0x10)
	{
		xr->penalty = penlDefX[xr->type];
		code = elem->code;
		if (code == 0x10)
			return 0;
		if (code == 0xe || code == 0x11 || code == 0x28 || code == 0x29)
			goto neighbours;
		pen = xr->penalty + penlDefH[height];
	}
	xr->penalty = pen;
	code = elem->code;
	if (code == 0xd)
	{
		UByte other = elem->other;
		if ((other & 2) == 0)
		{
			if ((other & 4) == 0 || elem->ipoint0 == 0)
				return 0;
			if (elem->ipoint1 != 0)
				return 0;
		}
		xr->penalty += 6;
	}
	else if (code == 4)
	{
		if ((elem->attr & 0xf) > 2)
			goto neighbours;
		xr->penalty += 6;
	}
	else if (code == 6)
	{
		if ((elem->attr & 0xf) < 0xc)
			goto neighbours;
		xr->penalty += 6;
	}
	code = elem->code;
	if (IsBreakCode(code) || code == 0xd || code == 0x10)
		return 0;

neighbours:
	{
		SPEC_TYPE* prev = elem->prev;
		UByte pc = prev->code;
		if (pc == 0)
			return 0;
		while (prev != nil && (pc = prev->code) != 0 && (pc == 0xd || pc == 0x10))
			prev = prev->prev;
		SPEC_TYPE* next = elem->next;
		while (next != nil && (next->code == 0xd || next->code == 0x10))
			next = next->next;
		if (prev == nil || next == nil || (pc = prev->code) == 0)
			return 0;
		if (IsBreakCode(pc) && IsBreakCode(next->code))
			return 0;
		short diff = 0;
		UByte add = 0;
		if (!IsBreakCode(pc))
			diff = (short) HWRAbs((elem->attr & 0xf) - (prev->attr & 0xf));
		if (!IsBreakCode(next->code))
			diff = (short) (HWRAbs((elem->attr & 0xf) - (next->attr & 0xf)) + diff);
		if (diff >= 0xc)
			add = 8;
		else if (diff >= 6)
			add = 4;
		code = elem->code;
		if ((code == 0xe || code == 0x11 || code == 0x28 || code == 0x29) && add >= 1)
			add = 1;
		xr->penalty += add;
		if (xr->penalty > 0x12)
			xr->penalty = 0x12;
	}
	return 0;
}


// ROM 0x002c8be0 MarkXrAsLastInLetter__FP11xrd_el_typeP8low_typeP9SPEC_TYPE
// Whether a letter usually ends at the element: the xr is marked last in
// its letter (attrib 1) at a break, at a dash or dot after a break, at a
// lower extremum followed by a stroke's end, and in the other cases the
// shapes of the letters it may end suggest.  ==> 0.
long
MarkXrAsLastInLetter(xrd_el_type* xr, low_type* low, SPEC_TYPE* elem)
{
	SPEC_TYPE* head = low->fSpecl;
	SPEC_TYPE* next = elem->next;
	SPEC_TYPE* prev = elem->prev;
	UByte code = elem->code;
	UByte mark = elem->mark;
	if (elem == head || IsBreakCode(code))
		goto last;
	if (prev == head || next == nil || IsBreakCode(next->code))
		return 0;
	{
		UByte nc = next->code;
		UByte pc = prev->code;
		if (IsBreakCode(pc))
		{
			if (code == 0x10 || (code == 0xd && (elem->other & 4) == 0))
			{
				if (next != nil)
					goto last;
			}
			if (pc != 0x13)
				return 0;
			SPEC_TYPE* nn = next->next;
			if (nn != nil)
			{
				UByte c = nn->code;
				if (!IsBreakCode(c))
				{
					if (code == 3 || code == 7 || code == 0xa || code == 9 || code == 0xc || code == 0xb)
						goto last;
					return 0;
				}
			}
			if (code == 3 || code == 9)
				goto last;
			return 0;
		}
		SPEC_TYPE* nn = next->next;
		if (nn == nil || IsBreakCode(nn->code))
			return 0;
		UByte nnc = nn->code;
		Boolean shape;
		if (code == 6 || code == 0x1e || code == 0x1f || code == 0x1c || code == 0x1b || code == 0x17 || code == 0x18)
			shape = true;
		else if (code == 8)
			shape = (elem->attr & 0x30) == 0x20;
		else
			shape = (code == 0x22);
		if (!shape)
		{
			if (mark == 5 || code == 0x29 || code == 0x10)
				shape = true;
			else if (code == 0xd)
				shape = (elem->other & 4) == 0;
			else
				shape = (code == 7 || code == 0xb || code == 0xc);
		}
		if (shape)
		{
			Boolean keep = false;
			if (code != 6 && !(code == 0x22 && (elem->attr & 0x30) == 0x10))
				goto last;
			if (nc == 2 || nc == 0x21 || nc == 3)
			{
				SPEC_TYPE* s = nn;
				while (s != nil && !IsBreakCode(s->code) && !IsStrongElem(s))
					s = s->next;
				if (s != nil)
				{
					SPEC_TYPE* sn = s->next;
					if ((sn == nil || IsBreakCode(sn->code))
					 && (s->code == 7 || s->code == 0xb || s->code == 0xc || s->code == 0x1c || s->code == 0x19))
					{
						_RECT box;
						GetTraceBox(low->fX, low->fY, next->iBeg, s->iEnd, &box);
						// ROM QUIRK: twice the middle of the box's height set
						// against the middle of its width
						if (((box.top + box.bottom) >> 1) * 2 < ((box.right + box.left) >> 1))
							keep = true;
					}
				}
			}
			if (!keep)
				goto last;
			return 0;
		}
		if (nc == 0x15 || nc == 0x16 || nc == 4 || nc == 0x1d || nc == 0x20 || nc == 0xf || nc == 0x28 || nc == 0xd || nc == 0x10)
			goto last;
		if (pc == 0x22 && (prev->attr & 0x30) == 0x20)
			goto last;
		if (nnc == 0x22 && (nn->attr & 0x30) == 0x20)
			goto last;
		if (pc == 4)
			goto last;
		if (code == 2 || (code == 3 && (mark == 1 || mark == 9)))
		{
			if ((elem->attr & 0x30) == 0x10)
			{
				if (pc == 0xe)
					xr[-1].attrib |= 1;
				if (next->code == 0x11)
					goto last;
				return 0;
			}
		}
		else if (code == 3)
			return 0;
		if (code == 8 || (code == 7 && (mark == 1 || mark == 9)))
		{
			if ((elem->attr & 0x30) != 0x20)
				return 0;
			if (pc == 0xe)
				xr[-1].attrib |= 1;
			if (next->code == 0x11)
				goto last;
		}
		return 0;
	}
last:
	xr->attrib |= 1;
	return 0;
}


// ROM 0x000fe55c GetMovementLink__FUc
long
GetMovementLink(UByte code)
{
	if (code == 0x23)
		return 0xc;
	if (code == 0x24)
		return 0xf;
	if (code == 0x25)
		return 0xb;
	return code == 0x26;
}


// ROM 0x000fe594 GetCurveLink__FsUi
// An arc's link by how much it bends (hundredths of its chord) and which
// way: 5..1 one way, 7..0xb the other.
long
GetCurveLink(short crook, ULong right)
{
	if (crook < 10)
		return right ? 7 : 5;
	if (crook < 0xf)
		return right ? 8 : 4;
	if (crook < 0x14)
		return right ? 9 : 3;
	if (crook < 0x1e)
		return right ? 0xa : 2;
	return right ? 0xb : 1;
}


// ROM 0x000fe60c CalculateStickOrArc__FP8SDB_TYPE
// A side too small beside the other (under a quarter of it, and under
// 10) forgotten; then a nearly straight piece (bend under 5, or under 20
// either way) is a stick (6), a piece that bends one way an arc
// (GetCurveLink), and one that bends both ways 0 - an S or a Z.
long
CalculateStickOrArc(SDB_TYPE* sdb)
{
	long a = sdb->distA;
	long b = sdb->distB;
	if (a > b * 4 && b < 10)
	{
		b = 0;
		sdb->distB = 0;
	}
	else if (b > a * 4 && a < 10)
	{
		a = 0;
		sdb->distA = 0;
	}
	long crook = sdb->crook;
	if (crook < 5 || (crook < 0x14 && a != 0 && b != 0))
		return 6;
	if (sdb->distA == 0)
		return GetCurveLink(crook, 0);
	if (sdb->distB != 0)
		return 0;
	return GetCurveLink(crook, 1);
}


// ROM 0x000fe6d8 CalculateLinkLikeSZ__FP8SDB_TYPEi
// A piece bending both ways: an S (0xc, 0xd) or a Z (0xe, 0xf) by which
// side's furthest point comes first, the larger number for a strong bend.
// ROM QUIRK: dy (which way the piece goes up or down) is tested and both
// branches do the same.
long
CalculateLinkLikeSZ(SDB_TYPE* sdb, long dy)
{
	long crook = sdb->crook;
	long iA = sdb->iA;
	long iB = sdb->iB;
	if (dy < 0)
	{
		if (iA > iB)
			return (crook > 0x19) ? 0xc : 0xd;
		return (crook > 0x19) ? 0xf : 0xe;
	}
	if (iA > iB)
		return (crook > 0x19) ? 0xc : 0xd;
	return (crook > 0x19) ? 0xf : 0xe;
}


static inline Boolean
IsLinkEndCode(SPEC_TYPE* e)
{
	UByte c = e->code;
	return c == 4 || c == 6 || c == 2 || c == 8 || c == 0x21 || c == 0x22
		|| ((c == 3 || c == 7) && e->mark == 6)
		|| c == 0xa || c == 9 || c == 0xc || c == 0xb;
}


// ROM 0x000fe754 CalculateLinkWithoutSDS__FP8low_typeP9SPEC_TYPET2
// How the trace goes from one element to the next, from the piece
// between their points: a stick (6) when the piece is short, otherwise
// as iMostFarDoubleSide describes it - the bend on the side whose furthest
// point is sharper kept when the chord crosses the piece.
long
CalculateLinkWithoutSDS(low_type* low, SPEC_TYPE* elem, SPEC_TYPE* next)
{
	short* x = low->fX;
	short* y = low->fY;
	_SDS_TYPE sds;
	short px, py;
	SDB_TYPE* sdb = (SDB_TYPE*) &sds.chord;
	UByte code = elem->code;
	if (IsLinkEndCode(elem) && IsLinkEndCode(next)
	 && ((elem->attr & 0x30) == (next->attr & 0x30) || code == 3 || code == 7 || next->code == 3 || next->code == 7))
	{
		sds.iBeg = elem->ipoint0;
		sds.iEnd = next->ipoint0;
	}
	else
	{
		short i = elem->ipoint0;
		if (i == -2 || i == 0 || code == 0x27)
			sds.iBeg = (short) ((elem->iBeg + elem->iEnd) >> 1);
		else
			sds.iBeg = i;
		i = next->ipoint0;
		if (i == -2 || i == 0 || next->code == 0x27)
			sds.iEnd = (short) ((next->iBeg + next->iEnd) >> 1);
		else
			sds.iEnd = i;
	}
	if (sds.iEnd - sds.iBeg <= 8)
		return 6;
	long dy = y[sds.iEnd] - y[sds.iBeg];
	iMostFarDoubleSide(x, y, &sds, &px, &py, 0);
	if (sdb->distB != 0 && sdb->distA != 0
	 && (((sds.mark << 8) | sds.attr) == 0x81))
	{
		long lo = sds.iBeg + 1;
		long hi = sds.iEnd - 1;
		if (sdb->iA <= lo || sdb->iA >= hi)
			sdb->distA = 0;
		else if (sdb->iB <= lo || sdb->iB >= hi)
			sdb->distB = 0;
		else
		{
			long ca = cos_vect(sdb->iA - 2, sdb->iA + 2, sds.iBeg, sds.iEnd, x, y);
			long cb = cos_vect(sdb->iB - 2, sdb->iB + 2, sds.iBeg, sds.iEnd, x, y);
			if (ca <= cb)
				sdb->distA = 0;
			else
				sdb->distB = 0;
		}
	}
	long link = CalculateStickOrArc(sdb);
	if (link == 0)
		link = CalculateLinkLikeSZ(sdb, dy);
	return link;
}


static inline Boolean
IsArcCode(UByte c)
{
	return c == 0xe || c == 0x11 || c == 0x28 || c == 0x29;
}

static inline Boolean
IsMovementCode(UByte c)
{
	return c == 0x24 || c == 0x23 || c == 0x26 || c == 0x25;
}

static inline Boolean
Overlap(SPEC_TYPE* a, SPEC_TYPE* b)
{
	return a->iEnd >= b->iBeg && b->iEnd >= a->iBeg;
}


// ROM 0x000fe384 GetLinkBetweenThisAndNextXr__FP8low_typeP9SPEC_TYPEP11xrd_el_type
// The xr's link to the next firm element (dashes and dots passed over,
// and arcs that overlap it): a movement's own, 6 at a break or a dash,
// otherwise CalculateLinkWithoutSDS's.  ==> the link.
long
GetLinkBetweenThisAndNextXr(low_type* low, SPEC_TYPE* elem, xrd_el_type* xr)
{
	long link;
	SPEC_TYPE* next = elem->next;
	while (next != nil && (next->code == 0xd || next->code == 0x10))
		next = next->next;
	UByte code = elem->code;
	if (IsBreakCode(code) || next == nil || IsBreakCode(next->code) || code == 0xd || code == 0x10)
		goto stick;
	if (IsArcCode(code) && Overlap(elem, next))
	{
		UByte nc = next->code;
		if (!(nc == 0x24 || nc == 0x23 || nc == 0x26 || nc == 0x25 || nc == 0xf))
			goto stick;
	}
	if (IsMovementCode(code))
	{
		link = GetMovementLink(code);
		goto done;
	}
	if (code == 0xf)
		goto stick;
	while (next != nil && IsArcCode(next->code) && Overlap(elem, next))
		next = next->next;
	if (next == nil || next->code == 0xf)
		goto stick;
	if (!IsArcCode(code))
	{
		SPEC_TYPE* e = next;
		while (e != nil && IsArcCode(e->code))
			e = e->next;
		if (e != nil)
		{
			link = GetMovementLink(e->code);
			if (link != 0)
				goto done;
		}
	}
	else if (IsMovementCode(next->code))
		next = next->next;
	// DEVIATION: an arc followed by a movement that ends the list would
	// have the ROM read the element at address nought; there is always a
	// break after the last element, so it never does
	if (next == nil)
		goto stick;
	link = CalculateLinkWithoutSDS(low, elem, next);
	goto done;
stick:
	link = 6;
done:
	xr->link = (UByte) link;
	return (short) link;
}


// The first xr: a break at the start of the trace, from point 1 to the
// start of the stroke the first firm element is in.
static void
StartBreak(low_type* low, xrd_el_type* xr, short* x, short* y)
{
	SPEC_TYPE* head = low->fSpecl;
	xr->type = 1;
	xr->attrib = 0;
	xr->penalty = 5;
	xr->height = 7;
	xr->link = 6;
	MarkXrAsLastInLetter(xr, low, head);
	XrSetH(xr->hotpoint, 1);
	XrSetH(xr->begpoint, 1);
	SPEC_TYPE* e = head->next;
	if (e != nil)
	{
		while (e->code == 0xd || e->code == 0x10)
		{
			SPEC_TYPE* n = e->next;
			if (n == nil || IsBreakCode(n->code))
				break;
			e = n;
		}
	}
	if (e == nil)
		XrSetH(xr->endpoint, 1);
	else
	{
		long end = (UShort) e->iBeg;
		for (;;)
		{
			XrSetH(xr->endpoint, end);
			end = XrGetH(xr->endpoint);
			if (end <= 0 || y[end - 1] == -1)
				break;
			end = end - 1;
		}
	}
	long end = XrGetH(xr->endpoint);
	XrSetH(xr->box + kXrLeft, (x[1] < x[end]) ? x[1] : x[end]);
	XrSetH(xr->box + kXrTop, (y[1] >= y[end]) ? y[end] : y[1]);
	XrSetH(xr->box + kXrRight, (x[1] > x[end]) ? x[1] : x[end]);
	XrSetH(xr->box + kXrBottom, (y[1] > y[end]) ? y[1] : y[end]);
}


// ROM 0x002c7a00 exchange__FP8low_typeP11xrdata_type
// See the file's comment.  ==> 0.
long
exchange(low_type* low, xrdata_type* xrdata)
{
	SPEC_TYPE* head = low->fSpecl;
	PS_point_type* trace = low->fTrace;
	xrd_el_type* xr0 = (xrd_el_type*) xrdata->fElements;
	short* x = low->fX;
	short* y = low->fY;
	short* map = low->fBuffers[2].ptr;
	xrd_el_type* xr = xr0;
	// what the last element walked came to - its xr type, or its code
	// when it was passed over.  DEVIATION: the ROM keeps it in a register
	// it does not set when the list holds only its first break; the host
	// starts it at nought (the list always holds more)
	UByte last = 0;
	UByte type = 0;

	StartBreak(low, xr, x, y);
	short count = 1;
	xr++;
	SPEC_TYPE* e = head->next;
	if (e == nil)
		goto closeBreak;
	if (e->code == 1 || e->code == 0x12 || e->code == 0x13)
		e = e->next;
	for ( ; e != nil; e = e->next)
	{
		UByte code = e->code;
		last = code;
		UByte attr = e->attr;
		UByte band = attr & 0x30;
		short hot = 0;
		XrSetH(xr->hotpoint, 0);
		switch (code)
		{
		default:
		case 0: case 5:
			continue;
		case 1:		type = (e->next != nil) ? 3 : 1; goto write;
		case 0x12:	type = (e->next != nil) ? 2 : 1; goto write;
		case 0x13:	type = (e->next != nil) ? 4 : 1; goto write;
		case 0x14:	type = 1; goto write;
		case 2:		type = (band == 0x10) ? 7 : 0xe; goto hotAt0;
		case 3:
			if (e->mark == 0x10)
				type = 0xb;
			else if (e->mark == 0x20)
				type = 0x12;
			else if (band != 0x10)
				type = 0xd;
			else if (e->mark == 9)
				type = 0xc;
			// the element after it in the array: a crossing's partner
			else if (e->mark == 6 && ((e + 1)->attr & 0x30) == 0x20)
				type = 0xc;
			else
				type = 6;
			goto hotAt0;
		case 7:
			if (e->mark == 0x10)
				type = 0x1f;
			else if (e->mark == 0x20)
				type = 0x18;
			else if (band != 0x20)
				type = 0x1a;
			else
				type = (e->mark == 9) ? 0x19 : 0x13;
			goto hotAt0;
		case 4:		type = 0x28; goto hotAt0;
		case 6:		type = 0x2b; goto hotAt0;
		case 8:		type = (band == 0x10) ? 0x1b : 0x14; goto hotAt0;
		case 9:		type = (band == 0x10) ? 9 : 0x10; goto hotAt0;
		case 0xa:	type = (band == 0x10) ? 0xa : 0x11; goto hotAt0;
		case 0xb:	type = (band == 0x10) ? 0x1d : 0x16; goto hotAt0;
		case 0xc:	type = (band == 0x10) ? 0x1e : 0x17; goto hotAt0;
		case 0xe:	type = 0x2d; goto hotAt0;
		case 0x11:	type = 0x30; goto hotAt0;
		case 0xd:	type = (e->other & 4) ? 0x36 : 0x3a; goto write;
		case 0xf:	type = 0x35; goto write;
		case 0x10:	type = (e->other & 2) ? 0x3b : 0x34; goto write;
		case 0x1d:	type = 0x29; goto hotAt0IfAny;
		case 0x1e:	type = 0x2a; goto hotAt0IfAny;
		case 0x21:	type = (band == 0x10) ? 8 : 0xf; goto hotAt0IfAny;
		case 0x22:	type = (band == 0x10) ? 0x1c : 0x15; goto hotAt0IfAny;
		case 0x15:	type = 0x20; goto hotAt1IfAny;
		case 0x18:	type = 0x23; goto hotAt1IfAny;
		case 0x19:	type = 0x24; goto hotAt1IfAny;
		case 0x1c:	type = 0x27; goto hotAt1IfAny;
		case 0x16:	type = 0x21; goto hotAt1UnlessCrossing;
		case 0x17:	type = 0x22; goto hotAt1UnlessCrossing;
		case 0x1a:	type = 0x25; goto hotAt1UnlessCrossing;
		case 0x1b:	type = 0x26; goto hotAt1UnlessCrossing;
		case 0x1f:	type = 0x2c; goto write;
		case 0x20:	type = 0x31; goto write;
		case 0x23:	type = 0x32; goto write;
		case 0x24:	type = 0x33; goto write;
		case 0x25:	type = 0x2f; goto write;
		case 0x26:	type = 0x2e; goto write;
		case 0x27:	type = 0x39; goto write;
		case 0x28:	type = 0x3c; goto write;
		case 0x29:	type = 0x3d; goto write;
		}
	hotAt0:
		// ROM QUIRK: no check for an element without a point (-2), whose
		// hotpoint is then the map's word before its start
		hot = map[e->ipoint0];
		goto setHot;
	hotAt0IfAny:
		hot = (e->ipoint0 == -2) ? 0 : map[e->ipoint0];
		goto setHot;
	hotAt1UnlessCrossing:
		if (e->mark == 6)
		{
			hot = 0;
			goto setHot;
		}
		// fall through
	hotAt1IfAny:
		hot = (e->ipoint1 == -2) ? 0 : map[e->ipoint1];
	setHot:
		XrSetH(xr->hotpoint, hot);
	write:
		last = type;
		xr->type = type;
		xr->height = attr & 0xf;
		xr->attrib = 0;
		AssignInputPenaltyAndStrict(e, xr);
		if (e->code == 1 && (e->other & 4) != 0)
			xr->attrib |= 2;
		XrSetH(xr->begpoint, e->iBeg);
		XrSetH(xr->endpoint, e->iEnd);
		GetLinkBetweenThisAndNextXr(low, e, xr);
		MarkXrAsLastInLetter(xr, low, e);
		if (XrGetH(xr->hotpoint) == 0 && !IsBreakCode(e->code))
			XrSetH(xr->hotpoint, map[(e->iBeg + e->iEnd) >> 1]);
		count++;
		xr++;
		if (count > 0x75)
			break;
	}
	if (last == 1)
	{
		// ended on a stroke's end with nothing after it: that break takes
		// the end of the xr before it as its point
		if (count > 2)
		{
			short v = XrGetH(xr[-2].endpoint);
			XrSetH(xr[-1].begpoint, v);
			XrSetH(xr[-1].endpoint, v);
			XrSetH(xr[-1].hotpoint, map[v]);
		}
	}
	else
	{
	closeBreak:
		xr->type = 1;
		xr->height = 7;
		xr->link = 6;
		xr->orient = 6;
		xr->penalty = 5;
		xr->attrib = 0;
		MarkXrAsLastInLetter(xr, low, head);
		short v = XrGetH(xr[-1].endpoint);
		XrSetH(xr->begpoint, v);
		XrSetH(xr->endpoint, v);
		XrSetH(xr->hotpoint, map[v]);
		XrSetH(xr->box + kXrLeft, x[v]);
		XrSetH(xr->box + kXrTop, y[v]);
		XrSetH(xr->box + kXrRight, x[v]);
		XrSetH(xr->box + kXrBottom, y[v]);
		xr++;
	}
	memset(xr, 0, sizeof(xrd_el_type));

	// the xrs either side of a break marked; a firm extremum after one
	// with a crossing after it (and a dash or dot between) marks the
	// crossing too when it is no lower
	for (short i = 0; i < kXrMaxElements && xr0[i].type != 0; i++)
	{
		xrd_el_type* p = &xr0[i];
		if (!X_IsBreak(p))
			continue;
		if (i > 0)
			p[-1].attrib |= 0x80;
		p[1].attrib |= 0x80;
		UByte t = p[1].type;
		if (!(t == 0xb || t == 9 || t == 0x11 || t == 0x2c || t == 0x29))
			continue;
		UByte t2 = p[2].type;
		if (!(t2 == 0x14 || (t2 == 0x2d && p[3].type == 0x14)))
			continue;
		short k = (t2 == 0x2d) ? i + 3 : i + 2;
		long d = (long) xr0[k].height - (long) p[1].height;
		if (d < 0)
			continue;
		if (i == 0 || d <= 3)
			xr0[k].attrib |= 0x80;
	}

	// the points mapped back to the original trace, an xr of one point
	// widened by one either way (by its type) unless a pen-up is there,
	// and its box measured
	for (short i = 0; i < kXrMaxElements && xr0[i].type != 0; i++)
	{
		xrd_el_type* p = &xr0[i];
		XrSetH(p->begpoint, (UShort) map[XrGetH(p->begpoint)]);
		XrSetH(p->endpoint, (UShort) map[XrGetH(p->endpoint)]);
		short b = XrGetH(p->begpoint);
		short en = XrGetH(p->endpoint);
		if (!X_IsBreak(p) && b == en)
		{
			if (trace[b - 1].y == -1)
			{
				if (trace[b + 1].y != -1)
					XrSetH(p->endpoint, en + 1);
			}
			else if (trace[b + 1].y == -1)
				XrSetH(p->begpoint, b - 1);
			else switch (p->type)
			{
			case 9: case 0xb: case 0x11: case 0x16: case 0x1e: case 0x1f:
				XrSetH(p->endpoint, en + 1);
				break;
			case 0xa: case 0x10: case 0x12: case 0x17: case 0x18: case 0x1d:
				XrSetH(p->begpoint, b - 1);
				break;
			case 0xc: case 0x19:
				if (X_IsBreak(p - 1))
					XrSetH(p->endpoint, (UShort) XrGetH(p->endpoint) + 1);
				break;
			}
		}
		_RECT box;
		GetBoxFromTrace(trace, XrGetH(p->begpoint), XrGetH(p->endpoint), &box);
		XrSetH(p->box + kXrLeft, box.left);
		XrSetH(p->box + kXrTop, box.top);
		XrSetH(p->box + kXrRight, box.right);
		XrSetH(p->box + kXrBottom, box.bottom);
	}
	check_xrdata(xr0, low);
	short n = 0;
	while (n < kXrMaxElements && xr0[n].type != 0)
		n++;
	xrdata->fLength = n;
	FillXrFeatures(xrdata, low);
	return 0;
}


// ROM 0x002c8a90 PutZintoXrd__FP8low_typeP11xrd_el_typeN22UcsPs
// A crossing (type 5) put in at cur, spanning from prev's end to cur's
// start: the xrs from cur on moved up one (dst is cur + 1).  ==> whether
// the room is used up.
Boolean
PutZintoXrd(low_type* low, xrd_el_type* dst, xrd_el_type* prev, xrd_el_type* cur, UByte penalty, short i, short* count)
{
	long a = XrGetH(prev->endpoint);
	long b = XrGetH(cur->begpoint);
	PS_point_type* trace = low->fTrace;
	if (a > b)
	{
		long t = a;
		a = b;
		b = t;
	}
	// (the ROM's memcpy copies from the top down when the source is below
	// the destination, as here)
	memmove(dst, cur, (*count - i + 1) * sizeof(xrd_el_type));
	cur->type = 5;
	cur->height = 7;
	cur->link = 6;
	cur->orient = 6;
	cur->penalty = penalty;
	cur->attrib = 0;
	MarkXrAsLastInLetter(cur, low, low->fSpecl);
	XrSetH(cur->begpoint, a);
	XrSetH(cur->hotpoint, a);
	XrSetH(cur->endpoint, b);
	_RECT box;
	GetBoxFromTrace(trace, a, b, &box);
	XrSetH(cur->box + kXrLeft, box.left);
	XrSetH(cur->box + kXrTop, box.top);
	XrSetH(cur->box + kXrRight, box.right);
	XrSetH(cur->box + kXrBottom, box.bottom);
	*count = (short) ((UShort) *count + 1);
	return *count >= 0x77;
}


// ROM 0x002c8874 check_xrdata__FP11xrd_el_typeP8low_type
// A crossing put in where the writing leaves a gap in a letter: an
// extremum (6 or 7) two after a 0x15 arc, not before a break, reaching
// more than a quarter of the box of the three xrs before it further right
// and with its top within a third of that box's height of the box's top.
// ==> 0, 1 when the xrs already fill the room.
long
check_xrdata(xrd_el_type* xr, low_type* low)
{
	PS_point_type* trace = low->fTrace;
	short count = 0;
	while (count < kXrMaxElements && xr[count].type != 0)
		count++;
	if (count >= 0x76)
		return 1;
	for (short i = 4; i < count && count < kXrMaxElements; i++)
	{
		xrd_el_type* cur = &xr[i];
		if (cur[-2].type != 0x15 || !(cur->type == 6 || cur->type == 7) || X_IsBreak(cur + 1))
			continue;
		_RECT box;
		GetBoxFromTrace(trace, XrGetH(cur[-3].begpoint), XrGetH(cur[-1].endpoint), &box);
		if (box.left == 0x7fff || box.right == 0 || box.top == 0x7fff || box.bottom == 0)
			continue;
		short width = box.right - box.left;
		short height = box.bottom - box.top;
		short beyond = (short) ((UShort) XrGetH(cur->box + kXrRight) - box.right);
		if (beyond <= width / 4)
			continue;
		long top = XrGetH(cur->box + kXrTop);
		long third = height / 3;
		if (top <= (short) (box.top - third) || top >= (short) (third + box.top))
			continue;
		if (PutZintoXrd(low, cur + 1, cur - 1, cur, 5, i, &count))
			break;
	}
	return 0;
}
