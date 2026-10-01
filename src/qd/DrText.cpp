/*
	File:		qd/DrText.cpp

	Contains:	Text drawn: StdText's drawing (DrText), which hands the
				laid-out text over a style run at a time to DrTextChunk.

				DrTextChunk composes a run the way the ROM does, as one
				block at the strike's own size: the glyphs ORed into a
				one-bit slab as wide as the run and as tall as the strike
				(the descent counted twice over, or the underline's reach),
				then the synthesised faces worked on the slab as a whole -
				bold smeared right a pixel at a time, italic sheared row by
				row from the bottom, the underline drawn with gaps where the
				descenders cross it, outline and shadow smeared right and
				down and the original XORed out of the middle - and gray
				text masked (MakeGrayText); the slab is then StretchBits'd
				onto the port under the mode, into the rectangle it comes to
				at the font engine's scale.  A strike drawn at its own size
				in srcOr with none of the faces, onto a rectangle of the
				port, skips the slab and ORs the glyphs straight into the
				port's bits.  A slab of more than 8000 bytes is drawn as two
				halves of the run, recursively.

	The slab's words are big-endian, as the ARM's memory holds them
	(the pixels' order in the byte, which is what StretchBits reads);
	the host works on them through GetBigEndianWord/PutBigEndianWord
	(DEVIATION: the ROM's loads and stores), and the ROM's byte loops
	that OR a glyph in - a byte of a glyph row shifted into place and
	masked at each end - are written a pixel at a time, which sets the
	same pixels.

	Reconstructed from the MP2x00 US ROM (0x0035c530-0x0035de30); each
	function cites its origin.
*/

#include "Text.h"
#include "TextObject.h"
#include "TextLayout.h"
#include "Fonts.h"
#include "Draw.h"
#include "Ports.h"
#include "Regions.h"
#include "Screen.h"
#include "FixedMath.h"
#include "ByteOrder.h"
#include "Frames.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


// what DrText hands DrTextChunk (the ROM's DrTextInfo, on DrText's stack)
struct DrTextInfo
{
	long			fMode;			// +0x00  the options' transfer mode, srcOr without options
	Fixed			fHScale;		// +0x04
	Fixed			fVScale;		// +0x08
	Fixed			fX;				// +0x0c  the pen: each chunk moves it on by its advance
	Fixed			fY;				// +0x10
	GrafPort*		fPort;			// +0x14
	StyleRecord*	fStyle;			// +0x18  the chunk's
	long			fAngle;			// +0x1c  the options' +0x0c (DrTextChunk does not look at it)
};


// (host) A word of the slab, laid out as the ARM's memory.
static inline ULong32
LW(const UByte* p)
{
	return GetBigEndianWord(p);
}

static inline void
SW(UByte* p, ULong32 v)
{
	PutBigEndianWord(p, v);
}

// (host) The ARM's shift left by a register: 32 or more shifts it all out.
static inline ULong32
LSL(ULong32 v, ULong32 n)
{
	n &= 0xff;
	return n >= 32 ? 0 : v << n;
}


// (host) A slab rectangle's coordinate taken to the scale about the
// origin, as the ROM does it: a 32-bit multiply, rounded, to a short.
static inline short
ScaleCoord(long n, Fixed scale)
{
	return (short) ((Long32) ((ULong32) n * (ULong32) scale + 0x8000) >> 16);
}

// The slab's rectangle as it is drawn at the font engine's scale: taken
// about the run's origin.
static void
ScaleSlabRect(Rect* r, long x0, long y0, Fixed scaleX, Fixed scaleY)
{
	OffsetRect(r, (short) -x0, (short) -y0);
	r->top = ScaleCoord(r->top, scaleY);
	r->left = ScaleCoord(r->left, scaleX);
	r->bottom = ScaleCoord(r->bottom, scaleY);
	r->right = ScaleCoord(r->right, scaleX);
	OffsetRect(r, (short) x0, (short) y0);
}


// ROM 0x0035dcd0 MakeGrayText__FP8PixelMapP8GrafPort
// A slab of text in a pattern made ready for the port: on a one-bit port
// every other pixel knocked out in a checkerboard (0xaa on the slab's
// first row, 0x55 on the next, ...); on a two- or four-bit port the slab
// made a map of that depth, every set pixel the gray of the foreground
// pattern's first pixel - in a new block, which the caller gives back.
// Other depths are left as they are.
static void
MakeGrayText(PixelMap* slab, GrafPort* port)
{
	long height = slab->bounds.bottom - slab->bounds.top;
	long rowBytes = slab->rowBytes;
	long size = rowBytes * height;
	UByte* bits = (UByte*) slab->baseAddr;
	long depth = port->portBits.pixMapFlags & 0xff;
	if (depth == 1)
	{
		ULong32 mask = 0xaaaaaaaa;
		for ( ; height > 0; height--)
		{
			for (long n = rowBytes >> 2; n > 0; n--)
			{
				SW(bits, LW(bits) & mask);
				bits += 4;
			}
			mask = ~mask;
		}
		return;
	}
	if (depth == 2)
	{
		UByte* gray = (UByte*) QDNewTempPtr(size * 2);
		if (gray == nil)
			return;
		UByte first = *(const UByte*) GetPixelMapBits(*port->fgPat);
		// a nibble of one-bit pixels as a byte of four two-bit ones
		UByte both = (UByte) ((first & 0xc0) | ((first & 0xc0) >> 2));	// the gray in pixels 0 and 1
		UByte low = (UByte) (both >> 4);								// in pixels 2 and 3
		UByte all = (UByte) (both | low);
		UByte table[16];
		table[0] = 0;
		table[1] = low & 3;
		table[2] = low & 0xc;
		table[3] = low;
		table[4] = both & 0x30;
		table[5] = all & 0x33;
		table[6] = all & 0x3c;
		table[7] = (both & 0x3f) | low;
		table[8] = both & 0xc0;
		table[9] = all & 0xc3;
		table[10] = all & 0xcc;
		table[11] = (both & 0xcf) | low;
		table[12] = both;
		table[13] = both | (low & 0xf3);
		table[14] = both | (low & 0xfc);
		table[15] = all;
		UByte* to = gray;
		for ( ; size != 0; size--)
		{
			to[0] = table[(*bits & 0xf0) >> 4];
			to[1] = table[*bits & 0xf];
			to += 2;
			bits++;
		}
		slab->baseAddr = (Ptr) gray;
		slab->rowBytes = (short) (rowBytes * 2);
		slab->pixMapFlags = (slab->pixMapFlags & 0xffffff00) | 2;
		return;
	}
	if (depth != 4)
		return;
	UByte* gray = (UByte*) QDNewTempPtr(size * 4);
	if (gray == nil)
		return;
	UByte first = *(const UByte*) GetPixelMapBits(*port->fgPat);
	// two one-bit pixels as a byte of two four-bit ones
	UByte table[4];
	table[0] = 0;
	table[1] = (UByte) ((first & 0xf0) >> 4);
	table[2] = (UByte) (first & 0xf0);
	table[3] = (UByte) (table[2] | table[1]);
	UByte* to = gray;
	for ( ; size != 0; size--)
	{
		to[0] = table[(*bits & 0xc0) >> 6];
		to[1] = table[(*bits & 0x30) >> 4];
		to[2] = table[(*bits & 0xc) >> 2];
		to[3] = table[*bits & 3];
		to += 4;
		bits++;
	}
	slab->baseAddr = (Ptr) gray;
	slab->rowBytes = (short) (rowBytes * 4);
	slab->pixMapFlags = (slab->pixMapFlags & 0xffffff00) | 4;
}


// ROM 0x0035c788 DrTextChunk__FP10DrTextInfolPUsPl
// A run of characters in one style drawn at the pen, the pen moved on by
// the run's advance (at the font engine's scale).  The header says how.
//
// ROM BUGS, kept: a chunk too big for a slab is drawn as two halves, and a
// single character too big is not drawn at all (the pen is left where it
// was, so what follows overlaps it); on the way to either the port's
// foreground pattern is left as the style's pattern (the recursion saves
// the pattern it set as the one to put back), as it is when the slab
// cannot be had; and a glyph that starts at or past the right of the
// port's rectangle ends the direct drawing of the run, even though a
// glyph after it (a negative advance) might come back into it.
static void
DrTextChunk(DrTextInfo* dti, long count, const UniChar* chars, const Fixed* advances)
{
	if (count < 1)
		return;
	GrafPort* port = dti->fPort;
	Fixed sum = 0;
	for (long i = 0; i < count; i++)
		sum += advances[i];
	FontEngineInfo info;
	if (OpenFont(&port->portBits, dti->fStyle, dti->fHScale, dti->fVScale, &info) == 3)
	{
		// (host: no font leaves the rest of the engine's info unset - the
		//  run is passed over, the pen moved on by its advance)
		dti->fX += sum;
		return;
	}
	long* adjust = info.fStyleAdjust;
	long x0 = (short) ((ULong32) (dti->fX + 0x8000) >> 16);
	long y0 = (short) ((ULong32) (dti->fY + 0x8000) >> 16);

	// the slab: the run's advance and the strike's bearings, italic's lean
	// either way, bold's smear, the outline's spread; down to the descent
	// again or the underline's reach
	long top = y0 - info.fMaxBeforeBL;
	long left = x0 + info.fMinOriginSB + ((info.fMinAfterBL * adjust[1]) >> 4);
	long right = (adjust[0] - info.fWidthAdjust) + (((info.fMaxBeforeBL * adjust[1]) >> 4) - info.fMinAdvanceSB);
	if (adjust[5] != 0)
		right += (adjust[5] < 4) ? adjust[5] + 1 : 4;
	right = x0 + (sum >> 16) + right;
	long reach = -(adjust[2] + adjust[4] * 2 + 1);
	if (info.fMinAfterBL <= reach)
		reach = info.fMinAfterBL;
	long bottom = (y0 - info.fMinAfterBL) - reach;
	Rect slabRect;
	slabRect.top = (short) top;
	slabRect.left = (short) left;
	slabRect.bottom = (short) bottom;
	slabRect.right = (short) right;
	Rect dstRect = slabRect;
	Boolean scaled = info.fScaleX != 0x10000 || info.fScaleY != 0x10000;
	if (scaled)
	{
		sum = FixedMultiply(sum, info.fScaleX);
		ScaleSlabRect(&dstRect, x0, y0, info.fScaleX, info.fScaleY);
	}
	dti->fX += sum;

	PixelMap portBits = port->portBits;
	Rect clip;
	if (!RSect(&clip, 4, &(*port->visRgn)->rgnBBox, &(*port->clipRgn)->rgnBBox, &portBits.bounds, &dstRect))
	{
		CloseFont(&info);
		return;
	}
	long clipTop = -info.fMaxBeforeBL;			// the rows a glyph may cover, from the baseline
	long clipBottom = -info.fMinAfterBL;
	PatternHandle savedPattern = nil;
	Boolean gray = false;
	if (dti->fStyle->fFontPattern != 0)
	{
		PatternHandle pattern = (PatternHandle) RefToAddress(dti->fStyle->fFontPattern);
		if (BlackOrWhitePat(pattern) != 1)
		{
			savedPattern = GetFgPattern();
			SetFgPattern(pattern);
			gray = true;
		}
	}

	// straight into the port: a strike at its own size, srcOr, no faces,
	// the port's regions rectangles
	Boolean direct = false;
	if (adjust[0] == 0 && adjust[1] == 0 && dti->fMode == 1 && adjust[4] == 0 && adjust[5] == 0
	 && !scaled && !gray && info.fReserved38 == 0 && (*port->clipRgn)->rgnSize == kRectRgnSize)
	{
		RgnPtr vis = *port->visRgn;
		long trimmed = 0;
		if (vis->rgnSize != kRectRgnSize)
			trimmed = TrimRect(&vis, &clip);
		if (trimmed < 0)
		{
			CloseFont(&info);
			return;
		}
		if (trimmed == 0)
		{
			direct = true;
			clipTop = clip.top - y0;
			clipBottom = clip.bottom - y0;
			QDStartDrawing(&portBits, &clip);
		}
	}
	Rect drawn = clip;
	// (host) NEWTON_TRACE_DRTEXT: each chunk, where its slab goes and how
	static const bool traceDrText = getenv("NEWTON_TRACE_DRTEXT") != nullptr;		// (read once: every text chunk asked)
	if (traceDrText)
		fprintf(stderr, "DrTextChunk n=%ld x0=%ld y0=%ld slab=(%d,%d,%d,%d) dst=(%d,%d,%d,%d) clip=(%d,%d,%d,%d) direct=%d adj=%ld,%ld,%ld,%ld,%ld,%ld mode=%ld c0=%x sum=%x\n", count, x0, y0, slabRect.left, slabRect.top, slabRect.right, slabRect.bottom, dstRect.left, dstRect.top, dstRect.right, dstRect.bottom, clip.left, clip.top, clip.right, clip.bottom, direct, adjust[0], adjust[1], adjust[2], adjust[3], adjust[4], adjust[5], dti->fMode, chars[0], sum);
	long depth = portBits.pixMapFlags & 0xff;
	UByte* bits;								// where row `top` starts
	long rowBytes;
	long origin;								// the column the rows start at
	long rowWords = 0;
	long total = 0;								// the slab's words
	UByte* block = nil;
	if (direct)
	{
		rowBytes = portBits.rowBytes;
		bits = (UByte*) GetPixelMapBits(&portBits) + rowBytes * (top - portBits.bounds.top);
		origin = portBits.bounds.left;
	}
	else
	{
		origin = ((left - portBits.bounds.left) & ~31) + portBits.bounds.left;
		rowWords = (long) ((ULong32) (right - origin) >> 5) + 2;
		rowBytes = rowWords * 4;
		total = rowWords * (bottom - top);
		long need = total * 4;
		if (adjust[5] != 0)
			need += rowWords * 0x10;
		if (need + 0x80 > 8000)
		{
			CloseFont(&info);
			dti->fX -= sum;
			long half = count >> 1;
			if (half != 0)
			{
				DrTextChunk(dti, half, chars, advances);
				DrTextChunk(dti, count - half, chars + half, advances + half);
			}
			return;
		}
		long size = total * 4 + 8;
		block = (UByte*) QDNewTempPtr(size);
		if (block == nil)
		{
			// (host: the font closed, where the ROM leaves it open)
			CloseFont(&info);
			return;
		}
		memset(block, 0, size);
		bits = block + 4;
	}

	// the glyphs ORed in, each at the pen (rounded) and its bearing
	Fixed pen = (Fixed) (((ULong32) (x0 - origin) << 16) | 0x8000);
	long clipLeft = clip.left - origin;
	long clipRight = clip.right - origin;
	for (long i = 0; i < count; i++)
	{
		info.fGetGlyph(chars[i], 0, &info);
		long gx = info.fGlyphBearingX + (pen >> 16);
		long gxEnd = info.fGlyphWidth + gx;
		pen += advances[i];
		long skip = 0;
		if (direct)
		{
			if (gx < clipLeft)
			{
				if (gxEnd <= clipLeft)
					continue;
				skip = clipLeft - gx;
				gx += skip;
				if (clipRight < gxEnd)
					gxEnd = clipRight;
			}
			else if (clipRight < gxEnd)
			{
				if (clipRight <= gx)
					break;
				gxEnd = clipRight;
			}
		}
		long rows = info.fGlyphHeight - 1;
		long firstRow = 0;
		long above = info.fGlyphBearingY + clipTop;
		if (above > 0)
		{
			firstRow = above;
			rows -= above;
		}
		long below = (info.fGlyphHeight - info.fGlyphBearingY) - clipBottom;
		if (below > 0)
			rows -= below;
		if (rows < 0)
			continue;
		if (direct && depth != 1 && depth != 2 && depth != 4)
			continue;							// (the ROM has no loop for deeper maps)
		const UByte* glyph = info.fGlyphBits;
		if (glyph == nil)
			continue;
		UByte* row = bits + rowBytes * (info.fMaxBeforeBL - info.fGlyphBearingY + firstRow);
		long start = gx - skip;					// the glyph's own first column
		for ( ; rows >= 0; rows--, firstRow++, row += rowBytes)
		{
			const UByte* from = glyph + firstRow * info.fGlyphRowBytes;
			for (long c = gx; c < gxEnd; c++)
			{
				long p = c - start;
				if (!(from[p >> 3] & (0x80 >> (p & 7))))
					continue;
				if (direct && depth != 1)
				{
					long bit = c * depth;
					row[bit >> 3] |= (UByte) (((1 << depth) - 1) << (8 - depth - (bit & 7)));
				}
				else
					row[c >> 3] |= (UByte) (0x80 >> (c & 7));
			}
		}
	}
	if (direct)
	{
		QDStopDrawing(&portBits, &drawn);
		CloseFont(&info);
		return;
	}

	// bold: the whole slab smeared a pixel right, once for each
	for (long n = 0; n < adjust[0]; n++)
	{
		ULong32 carry = 0;
		UByte* p = bits;
		for (long k = total; k >= 0; k--, p += 4)
		{
			ULong32 v = LW(p);
			SW(p, v | (v >> 1) | carry);
			carry = v << 31;
		}
	}

	// italic: each row above the bottom one moved right by another
	// italic sixteenths of a pixel, working up from the bottom
	if (adjust[1] != 0)
	{
		long to = total * 4 - rowBytes;			// byte offsets from bits
		long lean = 0;
		long rows = bottom - top;
		while (--rows > 0)
		{
			lean += adjust[1];
			ULong32 shift = (lean >> 4) & 0x1f;
			long from = to - 4 * (1 + (lean >> 9));
			for (long n = rowWords; n > 0; n--)
			{
				to -= 4;
				// (host: a word before the block reads as nought, where the
				//  ROM reads whatever is in front of it)
				ULong32 before = (from - 4 >= -4) ? LW(bits + from - 4) : 0;
				ULong32 here = (from >= -4) ? LW(bits + from) : 0;
				SW(bits + to, LSL(before, 32 - shift) | (here >> shift));
				from -= 4;
			}
		}
	}

	// underline: adjust[4] rows adjust[2] below the baseline, broken
	// where the ink around them (the rows either side, widened a pixel
	// each way) would touch it
	if (adjust[4] != 0)
	{
		UByte* gaps = (UByte*) QDNewTempPtr(rowBytes + 4);
		if (gaps != nil)
		{
			memset(gaps, 0, rowBytes + 4);
			UByte* line = bits + rowBytes * (info.fMaxBeforeBL + adjust[2]);
			long margin = adjust[4] >> 1;
			if (margin == 0)
				margin = 1;
			long rows = adjust[4] + margin * 2;
			UByte* a = line - rowBytes * margin;
			UByte* b = a + rowBytes;
			UByte* c = b;
			if (rows > 2)
				c = b + rowBytes;
			ULong32 carry = 0;
			for (long k = 0; k < rowWords; k++)
			{
				ULong32 v = LW(a + k * 4) | LW(b + k * 4) | LW(c + k * 4);
				SW(gaps + k * 4, v | (v >> 1) | carry);
				carry = v << 31;
			}
			for (rows -= 3; rows > 0; rows -= 2)
			{
				a = c + rowBytes;
				b = a;
				if (rows > 1)
				{
					c = a + rowBytes;
					b = c;
				}
				carry = 0;
				for (long k = 0; k < rowWords; k++)
				{
					ULong32 v = LW(a + k * 4) | LW(b + k * 4);
					SW(gaps + k * 4, v | (v >> 1) | carry | LW(gaps + k * 4));
					carry = v << 31;
				}
			}
			carry = 0;
			for (long k = rowWords - 1; k >= 0; k--)
			{
				ULong32 v = LW(gaps + k * 4);
				SW(gaps + k * 4, ~(v | (v << 1) | carry));
				carry = v >> 31;
			}
			for (long n = adjust[4]; n > 0; n--, line += rowBytes)
				for (long k = 0; k < rowWords; k++)
					SW(line + k * 4, LW(gaps + k * 4) | LW(line + k * 4));
			QDDisposeTempPtr(gaps);
		}
	}

	PixelMap slab;
	memset(&slab, 0, sizeof(slab));
	slab.baseAddr = (Ptr) bits;
	slab.rowBytes = (short) rowBytes;
	SetRect(&slab.bounds, (short) origin, (short) top, (short) right, (short) bottom);
	slab.pixMapFlags = (info.fReserved38 & 0x80000000) ? 0x41000001 : 0x40000001;
	Region wide;
	wide.rgnSize = kRectRgnSize;
	wide.filler = 0;
	wide.rgnBBox = portBits.bounds;
	RgnPtr widePtr = &wide;

	// outline and shadow: the slab smeared right and down (one pixel more
	// for the shadow) into a block four rows taller, and the original,
	// a pixel right and a row down, XORed out of it
	if (adjust[5] != 0)
	{
		long extra = total * 4 + rowBytes * 4;
		UByte* outlined = (UByte*) QDNewTempPtr(extra + 8);
		if (outlined != nil)
		{
			memset(outlined, 0, extra + 8);
			UByte* base = outlined + 4;
			UByte* end = base + extra;
			slab.baseAddr = (Ptr) base;
			memmove(base, bits, total * 4);
			long times = adjust[5] & 3;
			do
			{
				ULong32 carry = 0;
				UByte* p = base;
				for (long k = total; k >= 0; k--, p += 4)
				{
					ULong32 v = LW(p);
					SW(p, v | (v >> 1) | carry);
					carry = v << 31;
				}
			} while (--times >= 0);
			// ROM BUG, kept: the slab's first word is never ORed down into
			// the row below it (the loop stops a word short of the start)
			times = adjust[5] & 3;
			do
			{
				UByte* to = end;
				UByte* from = end - rowBytes;
				UByte* next;
				do
				{
					next = from - 4;
					SW(to, LW(from) | LW(to));
					to -= 4;
					from = next;
				} while (base < next);
			} while (--times >= 0);
			ULong32 carry = 0;
			UByte* to = base + rowBytes;
			for (long k = 0; k < total; k++, to += 4)
			{
				ULong32 v = LW(bits + k * 4);
				SW(to, (carry | (v >> 1)) ^ LW(to));
				carry = v << 31;
			}
			slab.bounds.bottom += 4;
			Rect srcRect = slabRect;
			srcRect.bottom += 4;
			Rect toRect = slabRect;
			toRect.bottom += 4;
			if (scaled)
				ScaleSlabRect(&toRect, x0, y0, info.fScaleX, info.fScaleY);
			if (gray)
				MakeGrayText(&slab, port);
			StretchBits(&slab, &portBits, &srcRect, &toRect, dti->fMode, port->visRgn, port->clipRgn, &widePtr);
			if (gray)
			{
				// ROM BUGS: the slab's bits are given back whatever
				// MakeGrayText did - on a one-bit port that is the middle
				// of the block given back next (host: given back only when
				// it is a block of its own) - and the first slab is not
				// given back at all, a leak kept
				Ptr grayBits = GetPixelMapBits(&slab);
				if (grayBits != (Ptr) base)
					QDDisposeTempPtr(grayBits);
				QDDisposeTempPtr(outlined);
				CloseFont(&info);
				SetFgPattern(savedPattern);
			}
			else
			{
				QDDisposeTempPtr(outlined);
				CloseFont(&info);
				QDDisposeTempPtr(block);
			}
			return;
		}
	}
	if (gray)
		MakeGrayText(&slab, port);
	StretchBits(&slab, &portBits, &slabRect, &dstRect, dti->fMode, port->visRgn, port->clipRgn, &widePtr);
	if (gray)
	{
		SetFgPattern(savedPattern);
		// ROM BUG: on a port deeper than four bits MakeGrayText made no
		// block of its own, and the ROM gives back the middle of the slab
		// (host: only a block of its own is given back)
		if ((port->portBits.pixMapFlags & 0xff) > 1 && GetPixelMapBits(&slab) != (Ptr) bits)
			QDDisposeTempPtr(GetPixelMapBits(&slab));
	}
	CloseFont(&info);
	QDDisposeTempPtr(block);
}


// ROM 0x0035c530 DrText__FlN21
// The text object drawn at the scales: laid out (the fonts opened at the
// size times the scale - a strike of that size if there is one, else the
// nearest stretched to it), then drawn a style run at a time from its
// location and the justification's start, in the options' transfer mode
// (srcOr without options).
//
// An object flagged 0x10000 (SetTextObjField's field 8) with options is
// squeezed into their width: the clip region's box widened by a
// sixteenth either side for the drawing, and while the text is wider
// than the width a pixel taken off each character's advance but the
// first's in turn.  (Host: the ROM takes it off the object's cached
// widths, so it lasts; the host lays the text out afresh each time and
// squeezes it afresh.)
void
DrText(TextObjectRef text, Fixed hScale, Fixed vScale)
{
	GrafPort* port = GetCurrentPort();
	if (port->pnVis < 0)
		return;
	if (TextObj(text)->fLength == 0)
		return;
	if (!UpdateLayoutState(text, 2, hScale, vScale))
		return;
	TextObject* obj = TextObj(text);
	TextLayout layout;
	Fixed start;
	if (!HostLayOut(obj, &layout, &start))
		return;
	obj = TextObj(text);
	long count = layout.fCount;
	if (count == 0)
	{
		HostDoneLayOut(&layout);
		return;
	}
	Boolean squeezed = false;
	short savedLeft = 0;
	short savedRight = 0;
	if ((obj->fFlags & 0x10000) != 0 && obj->fOptions != nil)
	{
		Fixed width = obj->fOptions->fWidth;
		squeezed = true;
		Rect* box = &(*port->clipRgn)->rgnBBox;
		savedLeft = box->left;
		savedRight = box->right;
		short l = (short) (savedLeft - (savedLeft >> 4));
		if (l >= 0)
			box->left = l;
		short r = (short) (savedRight + (savedRight >> 4));
		if (r > 0)
			box->right = r;
		Fixed advance = LayoutAdvance(obj, &layout, count);
		if (advance > width)
		{
			Fixed excess = advance - width;
			long index = 1;
			long left = count - 1;
			while (excess > 0)
			{
				// (host: a single character stops here, where the ROM
				//  walks on past the widths)
				if (index >= count)
					break;
				layout.fAdvances[index] -= 0x10000;
				excess -= 0x10000;
				index++;
				if (--left == 0)
				{
					left = count - 1;
					index = 1;
				}
			}
		}
	}
	DrTextInfo info;
	info.fMode = (obj->fOptions != nil) ? (obj->fOptions->fTransferMode & 7) : srcOr;
	info.fHScale = hScale;
	info.fVScale = vScale;
	info.fX = obj->fLocation.x + start;
	info.fY = obj->fLocation.y;
	info.fPort = port;
	info.fAngle = (obj->fOptions != nil) ? obj->fOptions->fReserved : 0;
	TTextObjectChars characters(obj);
	const UniChar* chars = characters.fChars;
	StyleRecord** styles = obj->fStyles;
	const short* runLengths = obj->fRunLengths;
	long length = obj->fLength;
	long done = 0;
	long run = 0;
	while (done < length)
	{
		long chunk = (runLengths != nil) ? runLengths[run] : length;
		if (chunk > length - done)
			chunk = length - done;
		if (chunk > count - done)
			chunk = count - done;
		if (chunk <= 0)
			break;
		info.fStyle = styles[run];
		DrTextChunk(&info, chunk, chars + done, layout.fAdvances + done);
		done += chunk;
		run++;
	}
	if (squeezed)
	{
		Rect* box = &(*port->clipRgn)->rgnBBox;
		box->left = savedLeft;
		box->right = savedRight;
	}
	HostDoneLayOut(&layout);
}
