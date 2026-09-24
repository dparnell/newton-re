/*
	File:		ink/InkFont.cpp

	Contains:	An ink word standing in for a font - InkFont.h.
*/

#include "InkFont.h"
#include "InkCodec.h"
#include "Objects.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "RichString.h"		// IsInkWord
#include "RSSymbols.h"
#include "Ports.h"
#include "Rects.h"
#include "Regions.h"		// QDNewTempPtr
#include "Unit.h"			// FixRect
#include "FixedMath.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <string.h>


/*------------------------------------------------------------------------------
	I n k   k e p t   o u t s i d e   t h e   h e a p
------------------------------------------------------------------------------*/

// An ink word does not have to be a binary object.  The recogniser can
// hand one over as a plain block of memory, and its address travels in
// the style slot as an integer Ref: the block's first halfword is how
// long the whole of it is, the packed strokes follow, and the eight
// bytes of measurements are at the end.

// ROM 0x000dc2a4 GetInkWordAddrData__FRC6RefVar
// The strokes of such a word: past the length halfword.
void*
GetInkWordAddrData(RefArg ink)
{
	return (UByte*) RefToAddress(ink) + sizeof(UniChar);
}


// ROM 0x000dc2c4 GetInkWordAddrInfo__FRC6RefVarP11InkWordInfo
// ... and its measurements, six bytes back from the end of it (the two
// the length itself takes up are part of the length).
//
// (host: the length is read as the UniChar a rich string writes it as,
// which is the same thing on the ROM's big-endian ARM - see
// frames/RichString.cpp's MungeRange.)
void
GetInkWordAddrInfo(RefArg ink, InkWordInfo* info)
{
	const UByte* block = (const UByte*) RefToAddress(ink);
	long length = *(const UniChar*) block;
	PackedInkWordInfo packed;
	BlockMove(block + length - 6, &packed, sizeof(packed));
	ExpandPackedInkWordInfo(&packed, info);
}


// (host) The strokes of a word held either way, and how many bytes of
// them there are.  The ROM's drawing functions are handed the block and
// read the length out of it; the codec seam here is given the length.
static const void*
GlyphInkData(RefArg ink, long* outSize)
{
	if (!ISINT(ink))
		return InkData(ink, outSize);
	const UByte* block = (const UByte*) RefToAddress(ink);
	long length = *(const UniChar*) block;
	if (outSize != nil)
		*outSize = length - (long) sizeof(UniChar) - (long) sizeof(PackedInkWordInfo);
	return block + sizeof(UniChar);
}


/*------------------------------------------------------------------------------
	T I n k W o r d G l y p h
------------------------------------------------------------------------------*/

// ROM 0x000dc314 __ct__13TInkWordGlyphFRC6RefVarUlT2
// A word of writing made into a glyph for a size and a face.  A size or
// a face of -1 means the word's own, which ReadMetrics fills in.
TInkWordGlyph::TInkWordGlyph(RefArg ink, ULong fontSize, ULong face)
{
	fInk = new RefStruct;
	*fInk = ink;
	fFace = face;
	fFontSize = fontSize;
	ReadMetrics();
}


// ROM 0x000dc394 ReadMetrics__13TInkWordGlyphFv
// What the word measures drawn at the size and face it was made for.
//
// At the size the word was last drawn at, everything is already worked
// out and sits in the packed measurements.  At any other size the scale
// is what the wanted size is of the word's own, and the ascent, descent
// and width follow from it.  The pen is the standard one for the size,
// unless the top bit of the face asks for the word's own pen scaled
// instead - and it is never less than one.
//
// The slop is a tenth of the word's font size, at the scale in force,
// and the word is given twice as much room as that: it is how far
// outside the strokes the pen spills.
//
// A printer's dots are finer than the screen's, so the pen is halved
// when the port being drawn into is one.
void
TInkWordGlyph::ReadMetrics(void)
{
	if (ISINT((Ref) *fInk))
		GetInkWordAddrInfo(RefVar(*fInk), &fInfo);
	else
		GetInkWordInfo(RefVar(*fInk), &fInfo);
	if (fFontSize == (ULong) -1)
		fFontSize = (ULong) fInfo.fScaledFontSize;
	if (fFace == (ULong) -1)
		fFace = fInfo.fFace;
	// (the top bit of the face: the word keeps a pen of its own)
	Boolean ownPen = (fFace & 0x80000000) != 0;
	if ((long) fFontSize == fInfo.fScaledFontSize)
	{
		fScale = fInfo.fScale;
		fPen = ownPen ? (long) fInfo.fPenSize : fInfo.fPenWidth;
		fAscent = fInfo.fScaledAscent;
		fDescent = fInfo.fScaledDescent;
	}
	else
	{
		fScale = FixedDivide(ToFixed((long) fFontSize), ToFixed(fInfo.fFontSize));
		if (ownPen)
		{
			Fixed of = FixedDivide(ToFixed((long) fFontSize), ToFixed(fInfo.fScaledFontSize));
			fPen = (short) RoundFixed(FixedMultiply(ToFixed((long) fInfo.fPenSize), of));
			if (fPen < 1)
				fPen = 1;
		}
		else
			fPen = GetStdInkWordPenWidth(fFontSize);
		fAscent = (short) RoundFixed(FixedMultiply(ToFixed((long) fInfo.fAscent), fScale));
		fDescent = (short) RoundFixed(FixedMultiply(ToFixed((long) fInfo.fDescent), fScale));
	}
	fSlop = FixedMultiply(FixedDivide(ToFixed(fInfo.fFontSize), ToFixed(10)), fScale);
	fWidth = (short) RoundFixed(FixedMultiply(ToFixed((long) fInfo.fWidth), fScale)
								+ FixedMultiply(fSlop, ToFixed(2)));
	GrafPort* port;
	GetPort(&port);
	if ((port->portBits.pixMapFlags & (kPixMapDevDotPrint | kPixMapDevPSPrint)) != 0)
	{
		fPen = (long) ((ULong) fPen >> 1);
		if (fPen == 0)
			fPen = 1;
	}
}


// ROM 0x000dbf14 SetFontParms__13TInkWordGlyphFRC6RefVar
// The word restyled, and the glyph measured again for it.  The size and
// face go back to "the word's own", because the word has just been told
// what they are.  ==> the ink word.
Ref
TInkWordGlyph::SetFontParms(RefArg fontSpec)
{
	if (IsInkWord(*fInk))
	{
		*fInk = SetInkWordFontParms(*fInk, fontSpec);
		fFontSize = (ULong) -1;
		fFace = (ULong) -1;
		ReadMetrics();
	}
	return *fInk;
}


// ROM 0x000dc568 DrawAt__13TInkWordGlyphFUlT1
// The word drawn into the current port with the left end of its
// baseline at (x, y), which is where the text engine puts a glyph.
//
// The box it goes in is the word's own measurements about that point,
// less the slop at each side and the pen at the bottom and the right -
// the pen hangs outside the line it is drawn on, so the room it needs
// is taken off the far edges rather than added to them.
//
// When the clip is a plain rectangle that holds the whole of the box,
// nothing has to be clipped, and the ROM says so to the drawing: it then
// uses the live inker's own line drawer, which carries its pen with it,
// rather than QuickDraw's.  (That is NOT YET, so the flag is worked out
// and passed on and the lines are drawn the slow way.)
//
// NOT YET RECONSTRUCTED: the printing path - on a printer port the ROM
// makes real outlined paths of the strokes (CSMakePathsGroup,
// CSMakePathsGroupInRect) and frames each of them, so that a PostScript
// printer gets outlines rather than a bitmap.
void
TInkWordGlyph::DrawAt(ULong x, ULong y)
{
	long size = 0;
	const void* data = GlyphInkData(RefVar(*fInk), &size);

	GrafPort* port;
	GetPort(&port);
	Boolean printing = (port->portBits.pixMapFlags & (kPixMapDevDotPrint | kPixMapDevPSPrint)) != 0;
	Boolean scaled = (long) fFontSize != fInfo.fFontSize;

	Rect box;
	box.left = (short) ((long) x + RoundFixed(fSlop));
	box.right = (short) ((fWidth + (long) x) - RoundFixed(fSlop) - fPen);
	box.top = (short) ((long) y - fAscent);
	box.bottom = (short) ((fDescent + (long) y) - fPen);

	PenState pen;
	GetPenState(&pen);
	PenNormal();
	PenSize(fPen, fPen);
	if (!printing)
	{
		// (CheckPic, which asks whether a picture is being recorded, is
		//  NOT YET; nothing here records one)
		Boolean enclosed = false;
		GetPort(&port);
		RgnHandle clip = port->clipRgn;
		if ((*clip)->rgnSize == sizeof(Region) && port->picSave == nil)
			enclosed = Encloses(&(*clip)->rgnBBox, &box);
		if (scaled)
		{
			FRect dst;
			FixRect(&dst, &box);
			InkDrawInFRect(data, size, (ULong) fPen, ToFixed((long) fInfo.fWidth),
						   ToFixed((long) (fInfo.fAscent + fInfo.fDescent)), &dst, enclosed);
		}
		else
			InkDrawScaled(data, size, (ULong) fPen, ToFixed(box.left), ToFixed(box.top),
						  0x10000, 0x10000, enclosed);
	}
	SetPenState(&pen);
}


/*------------------------------------------------------------------------------
	T h e   f o n t   t h e   t e x t   e n g i n e   o p e n s
------------------------------------------------------------------------------*/

// ROM 0x000adcb4 InkReopenFont__FPv
// Nothing to do: the glyph holds everything it needs.
long
InkReopenFont(FontEngineInfo* info)
{
	return 0;
}


// ROM 0x000adcbc InkCharToGlyph__FlPv
// An ink font has one glyph, which is the word itself; a space is the
// one other character it answers, with a blank glyph of its own.
long
InkCharToGlyph(long ch, const void* unused)
{
	return ch == ' ' ? 0xffff : 0;
}


// ROM 0x000adce8 InkGetGlyphInfo__FlT1Pv
// How far the glyph advances the pen: the word's width, or a fifth of
// the line's height for the space.
void
InkGetGlyphInfo(long ch, long glyph, FontEngineInfo* info)
{
	if (ch != 0)
		glyph = InkCharToGlyph(ch, info->fCmap);
	if (glyph == 0xffff)
		info->fGlyphAdvance = FixedMultiply(ToFixed(info->fDescent + info->fAscent), 0x3333);
	else
		info->fGlyphAdvance = ToFixed(((TInkWordGlyph*) info->fCmap)->fWidth);
}


// ROM 0x000add48 InkGetGlyph__FlT1Pv
// The glyph itself: the word drawn into a bitmap of its own, which the
// text engine then blits like any other glyph.  The rows are made a
// whole word wide, as the blitter wants, and the block comes out of
// QuickDraw's temporary store so that it is given back when the font is
// closed.
void
InkGetGlyph(long ch, long glyph, FontEngineInfo* info)
{
	if (ch != 0)
		glyph = InkCharToGlyph(ch, info->fCmap);
	if (glyph == 0xffff)
	{
		// the space: nothing to draw
		info->fGlyphWidth = 0;
		info->fGlyphHeight = 0;
		info->fGlyphBearingX = 0;
		info->fGlyphBearingY = 0;
		info->fGlyphBits = nil;
		return;
	}
	TInkWordGlyph* word = (TInkWordGlyph*) info->fCmap;
	info->fGlyphWidth = word->fWidth;
	info->fGlyphHeight = word->fAscent + word->fDescent;
	info->fGlyphBearingX = 0;
	info->fGlyphBearingY = info->fAscent;
	if (info->fBaselineShift != 0)
		info->fGlyphBearingY = info->fAscent + info->fBaselineShift;

	long rowBytes = (long) (((ULong) info->fGlyphWidth + 0x1f) & ~0x1fUL) >> 3;
	long length = rowBytes * info->fGlyphHeight;
	// ROM BUG, kept: a block already here is dropped rather than given
	// back, so asking an open ink font for its glyph twice loses the
	// first one.  Nothing does: a run of an ink word is one character
	// long, and the font is closed after it.
	info->fInkGlyphBits = QDNewTempPtr(length);
	if (info->fInkGlyphBits == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	info->fInkGlyphSize = length;
	memset(info->fInkGlyphBits, 0, (size_t) length);

	PixelMap map;
	map.baseAddr = (Ptr) info->fInkGlyphBits;
	map.rowBytes = (short) rowBytes;
	SetRect(&map.bounds, 0, 0, info->fGlyphWidth, info->fGlyphHeight);
	map.pixMapFlags = kPixMapPtr | 1;
	map.deviceRes.v = 0;
	map.deviceRes.h = 0;
	map.grayTable = nil;

	GrafPort* saved;
	GetPort(&saved);
	GrafPort port;
	OpenPort(&port);
	SetPortBits(&map);
	port.portRect = map.bounds;
	RectRgn(port.visRgn, &map.bounds);
	word->DrawAt(0, (ULong) word->fAscent);
	SetPort(saved);
	ClosePort(&port);

	info->fGlyphBits = (const unsigned char*) info->fInkGlyphBits;
	info->fGlyphRowBytes = rowBytes;
}


// ROM 0x000aded8 InkCloseFont__FPv
// The bitmap and the glyph given back.
void
InkCloseFont(FontEngineInfo* info)
{
	if (info->fInkGlyphBits != nil)
	{
		QDDisposeTempPtr(info->fInkGlyphBits);
		info->fInkGlyphSize = 0;
		info->fInkGlyphBits = nil;
	}
	TInkWordGlyph* word = (TInkWordGlyph*) info->fCmap;
	if (word == nil)
		return;
	delete word;
	info->fCmap = nil;
}


// ROM 0x000ada30 InkOpenFont__FP8PixelMapP11StyleRecordRC6RefVarlT4P14FontEngineInfo
// The style's font is a word of writing rather than a font family: the
// engine is given an info whose one glyph is that word.
//
// The glyph object is kept in the `cmap` field, which an ink font has no
// use for otherwise, and the ink in the field the 'sfnt' would be locked
// in.  The rest is the same work a real font's opener does - the line
// metrics, the superscript and subscript shift (which also takes the
// size down to four fifths), and the synthesised faces' adjustments.
long
InkOpenFont(PixelMap* pm, StyleRecord* style, RefArg ink, Fixed xScale, Fixed yScale,
			FontEngineInfo* info)
{
	ULong face = (ULong) style->fFontFace;
	Fixed size = FixedMultiply(style->fFontSize, xScale);
	Boolean superscript = (face & kSuperscriptFace) != 0;
	Boolean subscript = (face & kSubscriptFace) != 0;
	if (superscript || subscript)
		size = FixedMultiply(size, 0xcccd);		// four fifths

	*info->fFontData = ink;
	info->fCmap = nil;
	// (the face keeps its top bit, which says the word has a pen of its own)
	TInkWordGlyph* word = new TInkWordGlyph(ink, (ULong) RoundFixed(size),
											face & 0x8000018f);
	ULong faceBits = face & 0x18f;
	info->fCmap = (const char*) word;
	if (word == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);

	info->fAscent = word->fAscent;
	info->fDescent = word->fDescent;
	info->fLeading = 0;
	info->fWidMax = 0;
	info->fReserved10 = 0;
	info->fReserved14 = 0;
	info->fMinOriginSB = 0;
	info->fMaxBeforeBL = word->fAscent;
	info->fMinAfterBL = -word->fDescent;
	info->fScaleX = 0x10000;
	info->fMinAdvanceSB = 0;
	info->fScaleY = xScale == yScale ? 0x10000 : FixedDivide(yScale, xScale);
	info->fScaling = 0;
	if (!superscript && !subscript)
		info->fBaselineShift = 0;
	else
	{
		long shift = info->fAscent * 3;
		if (shift < 0)
			shift += 7;			// (three eighths, rounded towards nought)
		shift >>= 3;
		if (subscript)
			shift = -shift;
		info->fAscent += shift;
		info->fDescent -= shift;
		info->fMaxBeforeBL += shift;
		info->fBaselineShift = shift;
		info->fMinAfterBL += shift;
		faceBits = face & 0xf;
	}
	memset(info->fStyleAdjust, 0, sizeof(info->fStyleAdjust));
	info->fWidthAdjust = 0;
	info->fReserved38 = 0;
	if (faceBits != 0)
	{
		// the table scaled to the size, as the ROM's own font opener
		// asks for it
		const unsigned char* table = UpdateStyleTable(xScale, yScale);
		const unsigned char* row = table + 2;
		for (ULong bits = faceBits; bits != 0; bits >>= 1, row += 3)
			if (bits & 1)
			{
				info->fStyleAdjust[row[0]] += (signed char) row[1];
				info->fWidthAdjust += (signed char) row[2];
			}
		if (faceBits & kUnderlineFace)
		{
			info->fStyleAdjust[2] = table[0x17] - info->fBaselineShift;
			info->fStyleAdjust[3] = table[0x18];
			info->fStyleAdjust[4] = table[0x19];
		}
	}
	info->fReopen = InkReopenFont;
	info->fMap = InkCharToGlyph;
	info->fGetGlyphInfo = InkGetGlyphInfo;
	info->fGetGlyph = InkGetGlyph;
	info->fClose = InkCloseFont;
	info->fInkGlyphBits = nil;
	info->fInkGlyphSize = 0;
	return 0;
}


// (host) The size and face an ink word is laid out with, which only a
// glyph made for it can answer - the other half of the seam in Fonts.h.
// ==> whether the spec was an ink word at all.
static Boolean
InkWordFontParms(RefArg fontSpec, long* outSize, long* outFace)
{
	if (!IsInkWord(fontSpec))
		return false;
	TInkWordGlyph word(fontSpec, (ULong) -1, (ULong) -1);
	if (outSize != nil)
		*outSize = (long) word.fFontSize;
	if (outFace != nil)
		*outFace = (long) word.fFace;
	return true;
}


// (host) An ink word restyled from a font-parameter frame, which is what
// SetFontParms does with a spec that turns out to be one: the word keeps
// its own measurements and is told the new size, face, scale and pen
// (Ink.h's SetInkWordFontParms), rather than being replaced by a packed
// spec.  ==> whether the spec was an ink word at all.
static Boolean
InkWordSetFontParms(RefArg fontSpec, RefArg parms, Ref* outSpec)
{
	if (!IsInkWord(fontSpec))
		return false;
	Ref result = SetInkWordFontParms(fontSpec, parms);
	if (outSpec != nil)
		*outSpec = result;
	return true;
}


// (host) What the font engine is handed - Fonts.h says why these are
// registered rather than called.
void
InitializeInkFont(void)
{
	gInkOpenFont = InkOpenFont;
	gInkFontParms = InkWordFontParms;
	gInkSetFontParms = InkWordSetFontParms;
}
