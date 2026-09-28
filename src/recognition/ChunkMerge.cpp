/*
	File:		ChunkMerge.cpp

	Contains:	The digit reader's readings merged into the word's: after
				the processor has read a number (ChunkProcessor) and the
				configuration was narrowed to it (ChunkModifyRC), the xrs
				the low level made are cut down to the strokes that are not
				digits (ChunkPatchXrdata) so that the xr reader reads only
				those; afterwards the digits and whatever the xr reader read
				are put together in writing order as the first reading
				(ChunkSortAnswers), and that is looked up in the lexical
				database, confusable characters tried in turn, and read
				again as a date where a '1' may have been a '/'
				(ChunkCorrectByLexDB).  See Chunk.h.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x002a4a34-0x002a6404, 0x002a6650-0x002a6b50 and
				0x002a7078-0x002a7168); each function cites its origin.
				All of it is from the disassembly - the decompiler takes
				the sort's registers for code pointers.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// IsAlpha, HWRStr*, HWRMemory*
#include "XrReader.h"		// lex_data_type, fw_buf_type, GF_LexDbSymbolSet, GetWordAttributeAndID
#include "CursiveReader.h"	// xrdata_type, AllocXrdata, FreeXrdata
#include "LowLevel.h"		// xrd_el_type
#include <string.h>

extern const short	kChunkMonthDays[14];	// ChunkTables.cpp: the days in each month, by month (0 and 13 nought)

// the ROM C library's ctype bit 0x20 (0x0038053c): the ten digits and
// nothing else
static inline bool
IsDigitChar(UByte c)
{
	return c >= '0' && c <= '9';
}

// a big-endian halfword, signed and unsigned
static inline long	BEShort(const UByte* p)			{ return (short) ((p[0] << 8) | p[1]); }
static inline void	SetBEShort(UByte* p, long v)	{ p[0] = (UByte) (v >> 8); p[1] = (UByte) v; }

// a reading's halfwords after its weight: +0x4a and +0x4c, big-endian as
// the ROM writes them (rec_w_type's fX4A)
static inline void	SetX4A(rec_w_type* r, long v)	{ SetBEShort(&r->fX4A[0], v); }
static inline void	SetX4C(rec_w_type* r, long v)	{ SetBEShort(&r->fX4A[2], v); }

static inline bool	IsBreak(UByte type)				{ return type >= 1 && type <= 5; }


#pragma mark - the xrs cut down to what is not a digit

// ROM 0x002a6680 ChunkPatchXrdata__FPv
// With a number found, the xrs the low level made for the xr reader: when
// the writing is nothing but a number, two breaks (the first xr made a
// break, with height 7) and nothing else, so the xr reader has nothing to
// read; otherwise, for each run of strokes that is not digits (the
// processor's point pairs), the xrs lying in it or across its ends, each
// run ended by a break - the first xr again - carrying the height, shift
// and orientation of the xr after the last one taken (a 0x34 xr before it
// keeps an orientation of nought, a 0x36 or 0x3a its own), so that the
// runs read as words with the digits gone.  The xrs next to a break are
// marked (attribute 0x80), and a lone 0x36/0x3a between two breaks
// becomes a 0x3a.
void
ChunkPatchXrdata(void* ctx)
{
	ChunkCtx* c = (ChunkCtx*) ctx;
	xrdata_type saved = { 0, 0, nil };		// (ROM: from 0x0037ae04, nought)
	if (c == nil || c->fNumbers == 0)
		return;
	const int32_t* pairs = (const int32_t*) c->fData;
	xrdata_type* xr = c->fXr;
	xrd_el_type* elements = (xrd_el_type*) xr->fElements;
	xrd_el_type first = elements[0];
	long length = xr->fLength;
	if (c->fNumbersOnly != 0)
	{
		memset(elements, 0, 3 * sizeof(xrd_el_type));
		first.type = 1;
		first.height = 7;
		elements[0] = first;
		elements[1] = first;
		return;						// (the length is left as it was)
	}
	AllocXrdata(&saved, length);
	if (saved.fElements == nil)
		return;
	xrd_el_type* old = (xrd_el_type*) saved.fElements;
	for (long i = 0; i < length; i++)
		old[i] = elements[i];
	saved.fLength = length;
	memset(elements, 0, length * sizeof(xrd_el_type));
	elements[0] = first;
	long made = 1;
	long last = length - 1;			// (the last xr is never looked at)
	for (long run = 0; run < c->f04; run++)
	{
		long from = pairs[run * 2];
		long to = pairs[run * 2 + 1];
		long taken = -1;
		for (long j = 1; j < last; j++)
		{
			long beg = XrGetH(old[j].begpoint);
			long end = XrGetH(old[j].endpoint);
			bool in = (from <= beg && end <= to)
				   || (beg < from && end <= to && from < end)
				   || (from <= beg && beg < to && to < end);
			if (!in)
				continue;
			if (IsBreak(old[j].type))
			{
				if (elements[made - 1].type == 1)
					continue;		// (two breaks together: the second left out)
				elements[made] = old[j];
			}
			else
				elements[made] = old[j];
			made++;
			taken = j;
		}
		xrd_el_type* e = &elements[made];
		if (!IsBreak(elements[made - 1].type))
		{
			*e = first;
			if (taken > -1 && taken < last)
			{
				UByte before = elements[made - 1].type;
				UByte orient;
				if (before == 0x34)
					orient = 0;
				else if (before == 0x36 || before == 0x3a)
					orient = elements[made - 1].orient;
				else
				{
					e->height = old[taken + 1].height;
					e->shift = old[taken + 1].shift;
					orient = old[taken + 1].orient;
				}
				e->orient = orient;
				e->link = 6;
			}
			made++;
		}
		else
		{
			elements[made - 1] = first;
			if (taken > -1 && taken < last)
			{
				elements[made - 1].height = old[taken + 1].height;
				elements[made - 1].shift = old[taken + 1].shift;
				elements[made - 1].orient = old[taken + 1].orient;
				elements[made - 1].link = old[taken + 1].link;
			}
		}
	}
	if (made == 1)
	{
		elements[1] = first;
		made = 2;
	}
	xr->fLength = made;
	for (long i = 0; i < made; i++)
	{
		if (elements[i].type == 1)
		{
			if (i > 0)
				elements[i - 1].attrib |= 0x80;
			if (i < made - 1)
				elements[i + 1].attrib |= 0x80;
		}
	}
	if (made == 3 && (elements[1].type == 0x3a || elements[1].type == 0x36))
	{
		elements[0] = first;
		elements[1].type = 0x3a;
		elements[1].shift = 0;
		elements[2].shift = 0;
		elements[2].link = 6;
		elements[2].orient = elements[1].orient;
		elements[2].height = 0;
	}
	FreeXrdata(&saved);
}


#pragma mark - the readings put in writing order

// ROM 0x002a4a34 (unnamed) - the box and the trace points of each letter
// of the first reading, out of the xrs its letters were read from (the
// counts at +0x30, from the second xr on; the breaks left out): per letter
// twelve bytes of big-endian halfwords - top, right, left and bottom
// divided by the processor's scale, then the first and last point.  The
// box starts at (30000, 30000, 0, 0) and the points at (30000, 0).  ==> 0
// when a letter's count is nought, 1 otherwise.
static long
ChunkLetterBoxes(rec_w_type* readings, xrdata_type* xr, long scale, UByte* boxes)
{
	const UByte* word = readings[0].fWord;
	const xrd_el_type* elements = (const xrd_el_type*) xr->fElements;
	long answer = 1;
	long at = 1;
	for (long letter = 0; word[letter] != 0; letter++)
	{
		long count = readings[0].fX30[letter];
		if (count == 0)
			return 0;
		long until = at + count;
		long top = 30000, bottom = 0, left = 30000, right = 0;
		long firstPoint = 30000, lastPoint = 0;
		for (; at < until; at++)
		{
			const xrd_el_type* e = &elements[at];
			if (IsBreak(e->type))
				continue;
			long v = XrGetH(&e->box[kXrTop]);
			if (v < top)
				top = v;
			v = XrGetH(&e->box[kXrBottom]);
			if (v > bottom)
				bottom = v;
			v = XrGetH(&e->box[kXrLeft]);
			if (v < left)
				left = v;
			v = XrGetH(&e->box[kXrRight]);
			if (v > right)
				right = v;
			v = XrGetH(e->begpoint);
			if (v < firstPoint)
				firstPoint = v;
			if (v < firstPoint)				// (the ROM asks twice)
				firstPoint = v;
			v = XrGetH(e->endpoint);
			if (v > lastPoint)
				lastPoint = v;
		}
		UByte* b = boxes + letter * 0xc;
		SetBEShort(b + 2, right / scale);
		SetBEShort(b + 0, top / scale);
		SetBEShort(b + 4, left / scale);
		SetBEShort(b + 6, bottom / scale);
		SetBEShort(b + 8, firstPoint);
		SetBEShort(b + 10, lastPoint);
	}
	return answer;
}


// The middle of a digit's box across (the ROM's (left + right) >> 1).
static inline long
MidX(const tagNumBox* b)
{
	return (b->fLeft + b->fRight) >> 1;
}


// ROM 0x002a4c04 (unnamed) - the number's characters and the xr reader's
// put together as the first reading.  When the writing was not a number
// alone, each letter of the first reading is (a letter other than x read
// as a digit: an o as a 0, anything else as the first of the other
// readings' characters there that is not a letter or a space - the first
// reading as it was kept as the second) put among the digits where its
// box's middle falls (a comma or full stop by its left side; one to the
// right of them all that does not lie beyond the last is left out), with
// no more than 23 characters in all.  Then a '(' or ')' pair written the
// wrong way round is turned about when it lies before the first; a lone
// '«' and a '>' (or two '>'s, or a ')' and a '>') after it become one
// '»', and a lone '»' and a '<' before it one '«'; with over four
// characters of which the second to fourth are digits, "(ddd1", "(ddd/",
// "(ddd7" get a ')' at the end, "1ddd)" and "/ddd)" a '(' at the start and
// "/ddd/" both.  The first reading is then the characters (weight 100)
// and, when the letters were read as digits or an 8 may be an '&', the
// second the alternatives (weight 90).  "d)" or "dd)" with the first digit
// not 0 is a list item (fListItem).
static void
ChunkSortNumbers(ChunkCtx* c)
{
	tagNumBox* nb = (tagNumBox*) c->fData2;
	rec_w_type* r = c->fReadings;
	UByte* w = (UByte*) r;			// (the readings as the ROM addresses them: +0x50 is the second's word)
	xrdata_type* xr = c->fXr;
	UByte* boxes = nil;
	long count = 0;
	for (long i = 0; i < 0x18; i++)
	{
		if (nb[i].fChar == 0)
			break;
		count++;
	}
	if (c->fNumbersOnly == 0)
	{
		boxes = (UByte*) HWRMemoryAlloc(0x120);
		if (boxes == nil)
			return;
		memset(boxes, 0, 0x120);
		ChunkLetterBoxes(r, xr, c->f10, boxes);
		// the first reading as it was.  (ROM: 0x18 bytes on the stack, a
		// 0x19th read only for a word with no nought in its 0x18 bytes, which
		// is the byte after them - the host has it nought.)
		UByte as[0x19];
		as[0x18] = 0;
		for (long i = 0; i < 0x18; i++)
		{
			as[i] = w[i];
			if (w[i] == 0)
				break;
			if (IsAlpha(w[i]) == 0 || w[i] == 'x' || w[i] == 'X')
				continue;
			if (w[i] == 'o' || w[i] == 'O')
			{
				w[i] = '0';
				c->fAlternative = 1;
				continue;
			}
			for (long k = 1; k < 5; k++)
			{
				if (w[k * 0x50] == 0)
					break;
				UByte other = w[k * 0x50 + i];
				if (other == ' ' || IsAlpha(other) != 0)
					continue;
				w[i] = other;
				c->fAlternative = 1;
				break;
			}
		}
		if (c->fAlternative != 0)
		{
			for (long i = 0; i <= 0x18; i++)
			{
				w[0x50 + i] = as[i];
				if (as[i] == 0)
					break;
			}
		}
		for (long i = 0; i < 0x18; i++)
		{
			UByte ch = w[i];
			if (ch == 0)
				break;
			if (ch == ' ')
				continue;
			const UByte* box = boxes + i * 0xc;
			long at;
			if (count == 0)
				at = 0;
			else
			{
				if (ch == ',' || ch == '.')
				{
					long left = BEShort(box + 4);
					if (left < nb[0].fLeft)
						at = 0;
					else
					{
						for (at = 1; at < count; at++)
							if (left > nb[at - 1].fLeft && left < nb[at].fLeft)
								break;
						if (at == count)
						{
							if (left <= nb[count - 1].fLeft)
								continue;
							at = count;
						}
					}
				}
				else
				{
					long mid = (BEShort(box + 4) + BEShort(box + 2)) >> 1;
					if (mid < MidX(&nb[0]))
						at = 0;
					else
					{
						for (at = 1; at < count; at++)
							if (mid > MidX(&nb[at - 1]) && mid < MidX(&nb[at]))
								break;
						if (at == count)
						{
							if (mid <= MidX(&nb[count - 1]))
								continue;
							at = count;
						}
					}
				}
				if (count == 0x17)
					break;
			}
			for (long k = count; k >= at; k--)
				nb[k + 1] = nb[k];
			nb[at].fChar = w[i];
			if (c->fAlternative != 0)
				nb[at].fAlt = w[0x50 + i];
			nb[at].fTop = (int32_t) BEShort(box + 0);
			nb[at].fRight = (int32_t) BEShort(box + 2);
			nb[at].fLeft = (int32_t) BEShort(box + 4);
			nb[at].fBottom = (int32_t) BEShort(box + 6);
			nb[at].fFirstPoint[0] = box[8];
			nb[at].fFirstPoint[1] = box[9];
			nb[at].fLastPoint[0] = box[10];
			nb[at].fLastPoint[1] = box[11];
			nb[at].f14 = 1;
			nb[at].fHeight[0] = 0;
			nb[at].fHeight[1] = 0;
			count++;
		}
	}

	// a '(' ')' written the wrong way round, before the first character
	long lastIndex = count - 1;
	for (long i = 0; i < lastIndex; i++)
	{
		long open;
		if (nb[i].fChar == '(')
		{
			if (nb[i + 1].fChar != ')')
				continue;
			open = i;
		}
		else if (nb[i].fChar == ')' && nb[i + 1].fChar == '(')
			open = i + 1;
		else
			continue;
		if (nb[open].fLeft < nb[0].fLeft)
		{
			tagNumBox moved = nb[open];
			for (long k = open - 1; k >= 0; k--)
				nb[k + 1] = nb[k];
			nb[0] = moved;
		}
		break;
	}

	// guillemets
	long opens = 0, closes = 0, lesses = 0, greaters = 0;
	long lastOpen = 0, lastClose = 0, lastLess = 0, lastGreater = 0;
	long removed = -1;
	for (long i = 0; i < count; i++)
	{
		UByte ch = nb[i].fChar;
		if (ch == 0xc7)				// '«' (Mac Roman)
		{
			opens++;
			lastOpen = i;
		}
		else if (ch == 0xc8)		// '»'
		{
			closes++;
			lastClose = i;
		}
		else if (ch == '<')
		{
			lesses++;
			lastLess = i;
		}
		else if (ch == '>')
		{
			greaters++;
			lastGreater = i;
		}
	}
	if (count > 0)
	{
		if (opens == 1 && closes == 0)
		{
			if (greaters > 0 && lastOpen + 1 < lastGreater)
			{
				UByte before = nb[lastGreater - 1].fChar;
				if (before == '>' || before == ')')
				{
					removed = lastGreater - 1;
					nb[lastGreater].fChar = 0xc8;
				}
				else if (lastIndex > lastGreater)
				{
					UByte after = nb[lastGreater + 1].fChar;
					if (after == '>' || after == ')')
					{
						removed = lastGreater;
						nb[lastGreater + 1].fChar = 0xc8;
					}
				}
			}
		}
		else if (closes == 1 && opens == 0)
		{
			if (lesses > 0 && lastClose - 1 > lastLess)
			{
				bool done = false;
				if (lastLess > 0)
				{
					UByte before = nb[lastLess - 1].fChar;
					if (before == '<' || before == '(')
					{
						removed = lastLess;
						nb[lastLess - 1].fChar = 0xc7;
						done = true;
					}
				}
				if (!done)
				{
					UByte after = nb[lastLess + 1].fChar;
					if (after == '<' || after == '(')
					{
						removed = lastLess + 1;
						nb[lastLess].fChar = 0xc7;
					}
				}
			}
		}
		if (removed > -1)
		{
			for (long k = removed; k < count; k++)
				nb[k] = nb[k + 1];
			count = lastIndex;
		}
	}

	// brackets round a four-digit run
	if (count > 4 && IsDigitChar(nb[1].fChar) && IsDigitChar(nb[2].fChar) && IsDigitChar(nb[3].fChar))
	{
		UByte head = nb[0].fChar;
		UByte tail = nb[4].fChar;
		if (head == '(' && (tail == '1' || tail == '/' || tail == '7'))
			nb[4].fChar = ')';
		else if (tail == ')' && (head == '1' || head == '/'))
			nb[0].fChar = '(';
		else if (head == '/' && tail == '/')
		{
			nb[0].fChar = '(';
			nb[4].fChar = ')';
		}
	}

	// the readings
	if (c->fNumbersOnly != 0)
	{
		for (long i = 0; i < count; i++)
		{
			w[i] = nb[i].fChar;
			w[0x18 + i] = 0;
			w[0x30 + i] = 0;
			w[0x68 + i] = 0;
			w[0x80 + i] = 0;
			if (c->fAmpersand != 0)
			{
				UByte alt = nb[i].fAlt;
				w[0x50 + i] = alt != 0 ? alt : w[i];
				// (the third reading's +0x18 and +0x30 cleared - its
				// variants, not the second's, which were cleared above)
				w[0xb8 + i] = 0;
				w[0xd0 + i] = 0;
			}
		}
		w[0x50 + count] = 0;
		c->fAlternative = c->fAmpersand;
	}
	else
	{
		for (long i = 0; i < count; i++)
		{
			w[i] = nb[i].fChar;
			w[0x18 + i] = 0;
			w[0x30 + i] = 0;
			w[0x68 + i] = 0;
			w[0x80 + i] = 0;
			if (c->fAlternative != 0)
			{
				w[0x50 + i] = nb[i].f14 == 0 ? nb[i].fChar : nb[i].fAlt;
				w[0xb8 + i] = 0;
				w[0xd0 + i] = 0;
			}
		}
	}
	w[count] = 0;
	r[0].fWeight = 100;
	if (c->fAlternative != 0)
	{
		w[0x50 + count] = 0;
		r[1].fWeight = 90;
	}
	if (count < 4 && count > 1 && w[count - 1] == ')' && IsDigitChar(w[0]) && w[0] != '0'
		&& (count == 2 || (count == 3 && IsDigitChar(w[1]))))
		c->fListItem = 1;
	if (boxes != nil)
		HWRMemoryFree((Ptr) boxes);
}


// ROM 0x002a6650 ChunkSortAnswers__FPv
// With a number found, the readings put together in writing order
// (above).  ==> 1, or 0 with no number.
long
ChunkSortAnswers(void* ctx)
{
	ChunkCtx* c = (ChunkCtx*) ctx;
	if (c == nil || c->fNumbers == 0)
		return 0;
	ChunkSortNumbers(c);
	return 1;
}


#pragma mark - the lexical database

// ROM 0x002a7078 (unnamed) - a dictionary walk made ready for the
// configuration's lexical database (rc +0x74, when rc +0x08 asks for it,
// bit 8): the word empty, the chain at +0x9c, and the four kinds of
// dictionary the configuration uses marked (+0x18 vocabularies, +0x20 the
// database, +0x28 trigrams, +0x30 the character set).  The rest of the walk
// is left as it was.  ==> 1, or 0 with no lexical database.
static long
ChunkLexBegin(rc_type* rc, lex_data_type* lex)
{
	long flags = (short) RCGetH(rc, 0x08);
	if (rc->fChain == nil || (flags & 8) == 0)
		return 0;
	lex->reverse = 0;
	*(int32_t*) &lex->f04[8] = (int32_t) flags;			// (+0x0c)
	lex->wlen = 0;
	lex->f50 = 0;
	lex->word[0] = 0;
	memset(lex->f4c, 0, sizeof(lex->f4c));
	lex->vocChain = nil;
	lex->trigrams = nil;
	lex->lexChain = (TDictChain*) rc->fChain;
	if ((flags & 1) != 0)
		lex->vocStatus = 1;
	if ((flags & 8) != 0)
		lex->lexStatus = 1;
	if ((flags & 4) != 0)
		lex->f28[0] = 1;									// (+0x28)
	if ((flags & 2) != 0)
		lex->f28[8] = 1;									// (+0x30)
	return 1;
}


// ROM 0x002a70ec (unnamed) - the walk moved on over one entry of what may
// follow (sym, status, attribute, dictionaries, state): the database's
// state taken from it, the symbol put at position n-1 of the word and the
// word made n long.
static void
ChunkLexTake(lex_data_type* lex, const fw_buf_type* entry, long n)
{
	lex->f04[0x10] = entry->sym;							// (+0x14)
	lex->flags = 2;
	lex->lexStatus = entry->status;
	lex->f21 = entry->attr;
	lex->lexMask = entry->dmask;
	lex->lexState = entry->state;
	if (n > 0)
		lex->word[n - 1] = (char) entry->sym;
	lex->word[n] = 0;
	lex->wlen = n;
}


// ROM 0x002a713c (unnamed) - where the walk is, as an entry ChunkLexTake
// takes (the first eight bytes of one).
static void
ChunkLexSave(const lex_data_type* lex, fw_buf_type* entry)
{
	entry->sym = lex->f04[0x10];
	entry->status = lex->lexStatus;
	entry->attr = lex->f21;
	entry->dmask = lex->lexMask;
	entry->state = lex->lexState;
}


// What ChunkCorrectByLexDB goes back to (ROM 0x10 bytes, 32 of them, a
// stack growing down from the last).
struct ChunkLexChoice
{
	int32_t		fPosition;			// +00  the character
	int32_t		fChoice;			// +04  which of its alternatives was taken
	uint32_t	fState;				// +08  the database's state before it
	UByte		fStatus;			// +0c
	UByte		fMask;				// +0d
	UByte		f0E[2];
};

// ROM 0x002a5574 (unnamed) - a choice kept on the stack (and the stack
// moved on whether or not there was room for it).
static void
ChunkLexPush(long position, long choice, const fw_buf_type* where, ChunkLexChoice* stack, long* top)
{
	long at = *top;
	if (at >= 0)
	{
		stack[at].fPosition = (int32_t) position;
		stack[at].fChoice = (int32_t) choice;
		stack[at].fState = where->state;
		stack[at].fStatus = where->status;
		stack[at].fMask = where->dmask;
	}
	*top = *top - 1;
}


// ROM 0x002a55bc (unnamed) - the last choice kept taken back: its position
// and choice, and the state, status and dictionaries into `where` (whose
// symbol and attribute are left as they were).  ==> 1, 0 with none left.
static long
ChunkLexPop(long* position, long* choice, fw_buf_type* where, const ChunkLexChoice* stack, long* top)
{
	long at = *top + 1;
	*top = at;
	if (at > 0x1f || at < 0)
		return 0;
	*position = stack[at].fPosition;
	*choice = stack[at].fChoice;
	where->state = stack[at].fState;
	where->status = stack[at].fStatus;
	where->dmask = stack[at].fMask;
	return 1;
}


// ROM 0x002a5414 (unnamed) - what the character at position i of the first
// reading may have been instead, itself first: 7 ")", 1 "/()", j ")",
// C "(1", Z "2", c "(1", r "2", z "2", ( "1/", ) "/7", / "1()", . ",-",
// , ".", ' "-"; any other character only itself.  ==> into `out`.
static void
ChunkAlternatives(ChunkCtx* c, long i, char* out)
{
	UByte ch = c->fReadings[0].fWord[i];
	const char* s;
	switch (ch)
	{
	case '7':	s = "7)"; break;
	case '1':	s = "1/()"; break;
	case 'j':	s = "j)"; break;
	case 'C':	s = "C(1"; break;
	case 'Z':	s = "Z2"; break;
	case 'c':	s = "c(1"; break;
	case 'r':	s = "r2"; break;
	case 'z':	s = "z2"; break;
	case '(':	s = "(1/"; break;
	case ')':	s = ")/7"; break;
	case '/':	s = "/1()"; break;
	case '.':	s = ".,-"; break;
	case ',':	s = ",."; break;
	case '\'':	s = "'-"; break;
	default:
		out[0] = (char) ch;
		out[1] = 0;
		return;
	}
	HWRStrCpy(out, s);
}


// The days in month m (1-12).  (The ROM reads its table on the stack at
// sp + 2m whatever m is; a month outside 0..13 is only ever looked up where
// the answer no longer matters, so the host answers nought there.)
static inline long
MonthDays(long m)
{
	return (m >= 0 && m < 14) ? kChunkMonthDays[m] : 0;
}


// ROM 0x002a5620 ChunkCorrectByLexDB__FPv
// With a number found (and not a list item's "1)"), the first reading
// checked against the configuration's lexical database, a character at a
// time: each position's alternatives (ChunkAlternatives) tried in order
// against what the database says may follow, going back to the last
// position that had another to try when none fits, until the reading is a
// whole word of the database (status 3 or 4) - then it is that word
// (weight 100, its id and attribute at +0x4a/+0x4c) and the reading as it
// was the second (weight 50) - or there is nothing left to try (weight 99,
// id -3).  A reading ending in a comma, or a configuration with no lexical
// database (id -3, nothing more), is left alone.  The alternative the sort
// made comes after them (weight 50, or 40 when there is already a second).
// Then a reading as long as the number that is a date with a '/' read as
// a '1' - d1d, d1dd/dd1d, dd1dd, d1d1dd, d1dd1dd/dd1d1dd, dd1dd1dd, the
// month 1-12, the day within the month (February 29), the year 0-99 - gets
// the date as well: in place of the first reading, which moves down (each
// one's weight less ten), for the long forms, as the second for the short
// ones; where it could be read either way round, the heights of the second
// and third characters decide.  Finally an x (the first of one) with
// something before it gets a space in front of it.
void
ChunkCorrectByLexDB(void* ctx)
{
	ChunkCtx* c = (ChunkCtx*) ctx;
	long xAt = -1;
	if (c == nil || c->fNumbers == 0 || c->fListItem != 0)
		return;
	rc_type* rc = c->fRC;
	rec_w_type* r = c->fReadings;
	UByte* w = r[0].fWord;
	UByte* rb = (UByte*) r;
	// (ROM: the walk is on the stack and ChunkLexBegin sets only part of
	// it; the rest is whatever was there, which the database walk does not
	// read - the host has it nought)
	lex_data_type lex;
	memset(&lex, 0, sizeof(lex));
	if (ChunkLexBegin(rc, &lex) == 0)
	{
		SetX4A(&r[0], -3);
		SetX4C(&r[0], 0);
		r[1].fWord[0] = 0;
		r[1].fWeight = 0;
		return;
	}
	if (w[0] != 0)
	{
		for (long i = 1; i < 0x18; i++)
		{
			if (w[i] == 0)
			{
				if (w[i - 1] == ',')
					return;
				break;
			}
		}
	}
	UByte* block = (UByte*) HWRMemoryAlloc(0xe30);
	if (block == nil)
		return;
	memset(block, 0, 0xe30);
	ChunkLexChoice* stack = (ChunkLexChoice*) block;
	char* as = (char*) block + 0x200;			// the first reading as it was
	char* alternative = (char*) block + 0x218;	// the sort's alternative
	fw_buf_type* next = (fw_buf_type*) (block + 0x230);
	HWRStrCpy(as, (const char*) w);
	if (c->fAlternative != 0)
		HWRStrCpy(alternative, (const char*) r[1].fWord);
	long top = 0x1f;
	long status = 2;
	long position = 0;
	long found = 0;
	long choice = 0;
	char choices[0x10];
	long nChoices;
	fw_buf_type where;
	memset(&where, 0, sizeof(where));
	for (;;)
	{
		GF_LexDbSymbolSet(&lex, next);
		ChunkAlternatives(c, position, choices);
		nChoices = HWRStrLen(choices);
		if (nChoices == 0)
			break;
		found = 0;
		for (long a = choice; choices[a] != 0 && found == 0; a++)
		{
			for (long e = 0; e < 0x100; e++)
			{
				if (next[e].sym == 0)
					break;
				if (next[e].sym == (UByte) choices[a])
				{
					if (nChoices > 1)
					{
						ChunkLexSave(&lex, &where);
						ChunkLexPush(position, a, &where, stack, &top);
					}
					status = next[e].status;
					ChunkLexTake(&lex, &next[e], position + 1);
					found = 1;
					break;
				}
			}
		}
		if (found != 0 && (w[position + 1] != 0 || status == 3 || status == 4))
		{
			choice = 0;
			position++;
			continue;
		}
		if (ChunkLexPop(&position, &choice, &where, stack, &top) == 0)
			break;
		ChunkLexTake(&lex, &where, position);
		choice++;
	}
	if (found != 0 && (status == 3 || status == 4))
	{
		while (ChunkLexPop(&position, &choice, &where, stack, &top) != 0)
		{
			ChunkAlternatives(c, position, choices);
			w[position] = (UByte) choices[choice];
		}
		r[0].fWeight = 100;
		lex.flags = 2;
		lex.lexMask = 0;
		long id, attr;
		if (GetWordAttributeAndID(&lex, &id, &attr) == 0)
		{
			SetX4A(&r[0], id);
			SetX4C(&r[0], attr);
		}
		else
		{
			SetX4A(&r[0], -3);
			SetX4C(&r[0], 0);
		}
		if (HWRStrCmp((const char*) w, as) != 0)
		{
			HWRStrCpy((char*) r[1].fWord, as);
			r[1].fWeight = 50;
			SetX4A(&r[1], -3);
		}
		else
		{
			r[1].fWord[0] = 0;
			r[1].fWeight = 0;
			SetX4A(&r[1], 0);
		}
		SetX4C(&r[1], 0);
		r[2].fWord[0] = 0;
		r[2].fWeight = 0;
	}
	else
	{
		r[0].fWeight = 99;
		SetX4A(&r[0], -3);
		SetX4C(&r[0], 0);
		r[1].fWord[0] = 0;
		r[1].fWeight = 0;
		SetX4A(&r[1], 0);
	}
	if (c->fAlternative != 0)
	{
		if (r[1].fWord[0] == 0)
		{
			HWRStrCpy((char*) r[1].fWord, alternative);
			r[1].fWeight = 50;
			SetX4A(&r[1], -3);
			r[2].fWord[0] = 0;
			r[2].fWeight = 0;
		}
		else
		{
			HWRStrCpy((char*) r[2].fWord, alternative);
			r[2].fWeight = 40;
			SetX4A(&r[2], -3);
			r[3].fWord[0] = 0;
			r[3].fWeight = 0;
		}
	}

	// a date with a '/' read as a '1'
	const tagNumBox* nb = (const tagNumBox*) c->fData2;
	long digits = 0;
	for (long i = 0; i < 0x18; i++)
	{
		if (nb[i].fChar == 0)
			break;
		digits++;
	}
	long length = 0;
	for (long i = 0; i < 0x18; i++)
	{
		if (w[i] == 0)
			break;
		length++;
	}
	ULong slashes = 0;				// which characters become '/': 2 the second, 4 the third, 8 the fourth, 0x10 the fifth, 0x20 the sixth
	bool asSecond = false;			// the date as the second reading (the short forms) rather than the first
	bool dm = false, md = false;	// (the two ways a form may be read)
	if (digits == 0 || length == 0 || digits != length)
		goto theX;
	switch (length - 3)
	{
	case 0:							// d1d
		if (!IsDigitChar(w[0]) || !IsDigitChar(w[2]) || w[1] == '/' || w[1] != '1')
			goto theX;
		slashes |= 2;
		if (w[0] - '0' < 1 || w[0] - '0' > 9 || w[2] - '0' < 1 || w[2] - '0' > 9)
			goto theX;
		asSecond = true;
		break;
	case 1:							// d1dd (month d, day dd) or dd1d (month dd, day d)
	{
		UByte c1 = w[1], c2 = w[2];
		if (c1 == '/')
			goto theX;
		if (c1 == '1')
			dm = true;
		if (c2 == '1')
			md = true;
		if (dm)
		{
			long m = w[0] - '0';
			if (m < 1 || m > 9)
				dm = false;
			long d = w[3] + c2 * 10 - 0x210;
			if (d < 1 || MonthDays(m) < d)
				dm = false;
			if (!IsDigitChar(w[0]) || !IsDigitChar(c2) || !IsDigitChar(w[3]))
				dm = false;
		}
		if (md)
		{
			long m = c1 + w[0] * 10 - 0x210;
			if (m < 1 || m > 0xc)
				md = false;
			long d = w[3] - '0';
			if (d < 1 || MonthDays(m) < d)
				md = false;
			if (!IsDigitChar(w[0]) || !IsDigitChar(c1) || !IsDigitChar(w[3]))
				md = false;
		}
		if (dm)
		{
			if (md)
			{
				if (c1 == '1')
				{
					if (c2 == '1')
					{
						if (BEShort(nb[1].fHeight) <= BEShort(nb[2].fHeight))
							dm = false;
						else
							md = false;
					}
					else if (c2 == '/')
						dm = false;
				}
			}
			if (dm)
				slashes |= 2;
		}
		if (md)
			slashes |= 4;
		if (!dm && !md)
			slashes = 0;
		asSecond = true;
		break;
	}
	case 2:							// dd1dd
	{
		if (!IsDigitChar(w[0]) || !IsDigitChar(w[1]) || !IsDigitChar(w[3]) || !IsDigitChar(w[4])
			|| w[2] == '/' || w[2] != '1')
			goto theX;
		slashes |= 4;
		long m = w[1] + w[0] * 10 - 0x210;
		if (m < 1 || m > 0xc)
			goto theX;
		long d = w[4] + w[3] * 10 - 0x210;
		if (d < 1 || MonthDays(m) < d)
			goto theX;
		asSecond = true;
		break;
	}
	case 3:							// d1d1dd
	{
		if (!IsDigitChar(w[0]) || !IsDigitChar(w[2]) || !IsDigitChar(w[4]) || !IsDigitChar(w[5]))
			goto theX;
		if (w[1] == '/')
		{
			if (w[3] == '/')
				goto theX;
		}
		else if (w[1] == '1')
			slashes |= 2;
		else
			goto theX;
		if (w[3] == '1')
			slashes |= 8;
		if ((slashes & 0xa) == 0)
			goto theX;
		if (w[0] - '0' < 1 || w[0] - '0' > 9 || w[2] - '0' < 1 || w[2] - '0' > 9)
			goto theX;
		long y = w[5] + w[4] * 10 - 0x210;
		if (y < 0 || y > 0x63)
			goto theX;
		break;
	}
	case 4:							// d1dd1dd (month d) or dd1d1dd (month dd)
	{
		if (w[4] == '/')
		{
			if (w[1] == '/' || w[2] == '/')
				goto theX;
		}
		else if (w[4] == '1')
			slashes |= 0x10;
		else
			goto theX;
		if (slashes != 0)
		{
			if (w[1] == '/')
				dm = true;
			if (w[2] == '/')
				md = true;
		}
		UByte c1 = w[1], c2 = w[2];
		if (c1 == '1')
			dm = true;
		if (c2 == '1')
			md = true;
		if (dm)
		{
			long m = w[0] - '0';
			if (m < 1 || m > 9)
				dm = false;
			long d = w[3] + c2 * 10 - 0x210;
			if (d < 1 || MonthDays(m) < d)
				dm = false;
			long y = w[6] + w[5] * 10 - 0x210;
			if (y < 0 || y > 0x63)
				dm = false;
			if (!IsDigitChar(w[0]) || !IsDigitChar(c2) || !IsDigitChar(w[3])
				|| !IsDigitChar(w[5]) || !IsDigitChar(w[6]))
				dm = false;
		}
		if (md)
		{
			long m = c1 + w[0] * 10 - 0x210;
			if (m < 1 || m > 0xc)
				md = false;
			long d = w[3] - '0';
			if (d < 1 || MonthDays(m) < d)
				md = false;
			long y = w[6] + w[5] * 10 - 0x210;
			if (y < 0 || y > 0x63)
				md = false;
			if (!IsDigitChar(w[0]) || !IsDigitChar(c1) || !IsDigitChar(w[3])
				|| !IsDigitChar(w[5]) || !IsDigitChar(w[6]))
				md = false;
		}
		if (dm)
		{
			bool keep = true;
			if (md)
			{
				if (c1 == '/')
				{
					if (c2 == '/')
						keep = false;
					else if (c2 == '1')
						md = false;
				}
				else if (c1 == '1')
				{
					if (c2 == '1')
					{
						if (BEShort(nb[1].fHeight) <= BEShort(nb[2].fHeight))
							keep = false;
						else
							md = false;
					}
					else if (c2 == '/')
						keep = false;
				}
			}
			if (keep)
				slashes |= 2;
			else
				dm = false;
		}
		if (md)
			slashes |= 4;
		if (!dm && !md)
			goto theX;
		break;
	}
	case 5:							// dd1dd1dd
	{
		if (!IsDigitChar(w[0]) || !IsDigitChar(w[1]) || !IsDigitChar(w[3]) || !IsDigitChar(w[4])
			|| !IsDigitChar(w[6]) || !IsDigitChar(w[7]))
			goto theX;
		if (w[2] == '/')
		{
			if (w[5] == '/')
				goto theX;
		}
		else if (w[2] == '1')
			slashes |= 4;
		else
			goto theX;
		if (w[5] == '1')
			slashes |= 0x20;
		if ((slashes & 0x24) == 0)
			goto theX;
		long m = w[1] + w[0] * 10 - 0x210;
		if (m < 1 || m > 0xc)
			goto theX;
		long d = w[4] + w[3] * 10 - 0x210;
		if (d < 1 || MonthDays(m) < d)
			goto theX;
		long y = w[7] + w[6] * 10 - 0x210;
		if (y < 0 || y > 0x63)
			goto theX;
		break;
	}
	default:
		goto theX;
	}
	if (slashes != 0)
	{
		long n = 0;
		for (n = 0; n < 5; n++)
			if (r[n].fWord[0] == 0)
				break;
		if (n < 4)
		{
			for (long k = n; k > 0; k--)
			{
				r[k] = r[k - 1];
				r[k].fWeight = (short) ((UShort) r[k].fWeight - 10);
			}
		}
		r[n + 1].fWord[0] = 0;
		r[n + 1].fWeight = 0;
		if (!asSecond)
		{
			if ((slashes & 2) != 0)
				w[1] = '/';
			if ((slashes & 8) != 0)
				w[3] = '/';
			if ((slashes & 4) != 0)
				w[2] = '/';
			if ((slashes & 0x10) != 0)
				w[4] = '/';
			if ((slashes & 0x20) != 0)
				w[5] = '/';
		}
		else
		{
			if ((slashes & 2) != 0)
				rb[0x51] = '/';
			if ((slashes & 4) != 0)
				rb[0x52] = '/';
		}
	}

theX:
	// an x with something before it (and no other x) gets a space before it
	for (long i = 0; i < 0x18; i++)
	{
		UByte ch = w[i];
		if (ch == 0)
			break;
		if (ch == 'x' || ch == 'X')
		{
			if (xAt != -1)
				goto done;
			xAt = i;
		}
	}
	if (xAt > 0)
	{
		if (xAt <= 0x16)
			for (long i = 0x16; i >= xAt; i--)
				w[i + 1] = w[i];
		w[xAt] = ' ';
	}
done:
	HWRMemoryFree((Ptr) block);
}
