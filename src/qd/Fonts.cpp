/*
	File:		qd/Fonts.cpp

	Contains:	Fonts and text styles: the 'sfnt' font engine and the font
				manager over the font family frames.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The 'sfnt' data is read a byte at a time here (the ROM reads its
	big-endian halfwords and words in place).
*/

#include "Fonts.h"
#include "FixedMath.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Unicode.h"
#include "RichString.h"		// IsInkWord
#include "OSErrors.h"
#include <string.h>

const unsigned char kStyleTable[0x1c] = {
	0x50, 0x50, 0x00, 0x01, 0x01, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x05, 0x01, 0x01,
	0x05, 0x02, 0x02, 0x00, 0x00, 0xff, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00
};

const ULong kTagCmap = 0x636d6170;		// 'cmap'
const ULong kTagHead = 0x68656164;		// 'head'
const ULong kTagHhea = 0x68686561;		// 'hhea'
const ULong kTagHmtx = 0x686d7478;		// 'hmtx'
const ULong kTagHsty = 0x68737479;		// 'hsty'
const ULong kTagBloc = 0x626c6f63;		// 'bloc'
const ULong kTagBdat = 0x62646174;		// 'bdat'

const long kStrikeEntrySize = 0x30;		// a bitmapSizeTable
const long kOpenedAsIs = 0;				// SFNTOpenFont's answers
const long kOpenedScaled = 2;
const long kNoFont = 3;


static inline ULong	Get8(const char* p)		{ return (unsigned char) p[0]; }
static inline long	GetS8(const char* p)	{ return (signed char) p[0]; }
static inline ULong	Get16(const char* p)	{ return (Get8(p) << 8) | Get8(p + 1); }
static inline long	GetS16(const char* p)	{ return (short) Get16(p); }
static inline ULong	Get32(const char* p)	{ return (Get16(p) << 16) | Get16(p + 2); }

// (ToFixed and RoundFixed are Ports.h's: a whole number shifted into a
// Fixed, and a Fixed rounded back, both wrapping as the ARM's do.  A font
// with a negative descent, and a StyleRecord that nobody filled in - see
// CreateTextStyleRecord - are what make that matter here.)


/*------------------------------------------------------------------------------
	T h e   ' s f n t '   e n g i n e
------------------------------------------------------------------------------*/

// ROM 0x000adf28 EngineInitSFNT__Fv
static void
EngineInitSFNT(void)
{ }


// ROM 0x000aeba0 FindFontTable__FP16sfnt_OffsetTableUl
// The table with the tag in the 'sfnt' directory; nil for none.
const char*
FindFontTable(const char* sfnt, ULong tag)
{
	long count = Get16(sfnt + 4);
	const char* entry = sfnt + 12;
	for (long i = 0; i < count; i++, entry += 16)
		if (Get32(entry) == tag)
			return sfnt + Get32(entry + 8);
	return nil;
}


// ROM 0x000ae5b4 MapFormat0__FlPv
// A byte-indexed cmap: characters under 256.
long
MapFormat0(long ch, const void* cmap)
{
	if (ch < 0x100)
		return Get8((const char*) cmap + 6 + ch);
	return 0;
}


// ROM 0x000ae5cc MapFormat2__FlPv
// (The ROM has no format 2 mapping: nothing.)
long
MapFormat2(long /*ch*/, const void* /*cmap*/)
{
	return 0;
}


// ROM 0x000ae5d4 MapFormat4__FlPv
// The segment-mapped cmap: the segment whose end is at or past the
// character (the ROM's binary search then linear scan), the glyph from
// its delta or its glyph array.
long
MapFormat4(long ch, const void* cmap)
{
	const char* table = (const char*) cmap;
	long segCountX2 = Get16(table + 6);
	long segCount = segCountX2 >> 1;
	const char* endCodes = table + 14;
	const char* startCodes = endCodes + segCountX2 + 2;
	const char* deltas = startCodes + segCountX2;
	const char* rangeOffsets = deltas + segCountX2;
	ULong code = ch & 0xffff;
	for (long i = 0; i < segCount; i++)
	{
		if (Get16(endCodes + i * 2) < code)
			continue;
		if (Get16(startCodes + i * 2) > code)
			return 0;
		long rangeOffset = Get16(rangeOffsets + i * 2);
		if (rangeOffset == 0)
			return (Get16(deltas + i * 2) + code) & 0xffff;
		const char* glyphAt = rangeOffsets + i * 2 + rangeOffset + (code - Get16(startCodes + i * 2)) * 2;
		long glyph = Get16(glyphAt);
		if (glyph == 0)
			return 0;
		return (glyph + Get16(deltas + i * 2)) & 0xffff;
	}
	return 0;
}


// ROM 0x000ae7f0 MapFormat4Patched__FlPv
// (A font frame with the patched slot: the same mapping.)
static long
MapFormat4Patched(long ch, const void* cmap)
{
	return MapFormat4(ch, cmap);
}


// ROM 0x000ae7c0 MapFormat6__FlPv
// The trimmed table: a run of codes from firstCode.
long
MapFormat6(long ch, const void* cmap)
{
	const char* table = (const char*) cmap;
	long first = Get16(table + 6);
	long count = Get16(table + 8);
	if (ch < first || ch >= first + count)
		return 0;
	return Get16(table + 10 + (ch - first) * 2);
}


// ROM 0x000aebec LocateEntry__FlP14sfnt_blocTable
// The one-bit strike whose size is nearest the wanted size.
const char*
LocateEntry(Fixed size, const char* bloc)
{
	long wanted = (short) ((size + 0x8000) >> 16);
	long count = Get32(bloc + 4);
	const char* entry = bloc + 8;
	const char* best = nil;
	long bestDistance = 0x10000;
	for (long i = 0; i < count; i++, entry += kStrikeEntrySize)
	{
		if (Get8(entry + 0x2e) != 1)
			continue;
		long distance = wanted - (long) Get8(entry + 0x2c);
		if (distance == 0)
			return entry;
		if (distance < 0)
			distance = -distance;
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = entry;
		}
	}
	return best;
}


// ROM 0x000ae958 SFNTGetGlyphInfo__FlT1Pv
// The glyph's data found through the strike's index subtables (the
// missing glyph, 0, when the glyph is not in the strike) and its advance
// - the small or big metrics' - with the faces' extra width, as 16.16.
static void
SFNTGetGlyphInfo(long ch, long glyph, FontEngineInfo* info)
{
	if (ch != 0)
		glyph = info->fMap(ch, info->fCmap);
	const char* strike = info->fStrike;
	for (;;)
	{
		if ((long) Get16(strike + 0x28) <= glyph && glyph <= (long) Get16(strike + 0x2a))
		{
			for (const char* entry = info->fIndexSubTables; (long) Get16(entry) <= glyph; entry += 8)
			{
				if (glyph > (long) Get16(entry + 2))
					continue;
				long index = glyph - Get16(entry);
				const char* subTable = info->fIndexSubTables + Get32(entry + 4);
				long offset = 0;
				switch (Get16(subTable))
				{
				case 1:		offset = Get32(subTable + 8 + index * 4);		break;
				case 2:		offset = index * Get32(subTable + 8);			break;
				case 3:		offset = Get16(subTable + 8 + index * 2);		break;
				}
				const char* data = info->fBdat + Get32(subTable + 4) + offset;
				long imageFormat = Get16(subTable + 2);
				long advance = 0;
				if (imageFormat == 1 || imageFormat == 6)
					advance = Get8(data + 4);
				info->fGlyphAdvance = ToFixed(info->fWidthAdjust + advance);
				info->fIndexSubTable = subTable;
				info->fGlyphData = data;
				return;
			}
		}
		if (glyph == 0)
		{
			info->fGlyphAdvance = 0;
			info->fIndexSubTable = nil;
			info->fGlyphData = nil;
			return;
		}
		glyph = 0;
	}
}


// ROM 0x000aea58 SFNTGetGlyph__FlT1Pv
// The glyph's metrics and bitmap: the small (format 1) or big (format 6)
// metrics before byte-aligned rows; a superscript's or subscript's
// bearing shifted.
static void
SFNTGetGlyph(long ch, long glyph, FontEngineInfo* info)
{
	SFNTGetGlyphInfo(ch, glyph, info);
	const char* data = info->fGlyphData;
	if (data != nil)
	{
		long imageFormat = Get16(info->fIndexSubTable + 2);
		if (imageFormat == 1 || imageFormat == 6)
		{
			info->fGlyphHeight = Get8(data);
			info->fGlyphWidth = Get8(data + 1);
			info->fGlyphBearingX = GetS8(data + 2);
			info->fGlyphBearingY = GetS8(data + 3);
			info->fGlyphBits = (const unsigned char*) data + (imageFormat == 1 ? 5 : 8);
		}
	}
	else
	{
		info->fGlyphHeight = 0;
		info->fGlyphWidth = 0;
		info->fGlyphBearingX = 0;
		info->fGlyphBearingY = 0;
		info->fGlyphBits = nil;
	}
	info->fGlyphRowBytes = (info->fGlyphWidth + 7) >> 3;
	if (info->fBaselineShift != 0)
		info->fGlyphBearingY += info->fBaselineShift;
}


// ROM 0x000ae52c SFNTGetWidthsInfo__FlT1Pv
// A widths font's advance: the 'hmtx' width (the last for glyphs beyond
// the metrics) plus the faces' extra, scaled.
static void
SFNTGetWidthsInfo(long ch, long glyph, FontEngineInfo* info)
{
	if (ch != 0)
		glyph = info->fMap(ch, info->fCmap);
	if (glyph >= info->fNumHMetrics)
		glyph = info->fNumHMetrics - 1;
	info->fGlyphAdvance = (Fixed) ((Get16(info->fHmtx + glyph * 4) + info->fWidthsAdjust) * info->fWidthsScale);
}


// ROM 0x000ae584 SFNTGetWidthsGlyph__FlT1Pv
// A widths font has no bitmaps.
static void
SFNTGetWidthsGlyph(long ch, long glyph, FontEngineInfo* info)
{
	SFNTGetWidthsInfo(ch, glyph, info);
	info->fGlyphBearingX = 0;
	info->fGlyphBearingY = 0;
	info->fGlyphHeight = 0;
	info->fGlyphWidth = 0;
	info->fGlyphBits = nil;
}


// ROM 0x000ae388 SetupWidthsFont__FlP16sfnt_OffsetTableP14FontEngineInfoPl
// A font without strikes (a printer face): the metrics from 'hhea' scaled
// from the 'head' units per em to the size; the faces the font itself has
// ('head' macStyle) are taken off the ones to synthesise, and 'hsty' says
// how much wider each remaining face makes a glyph.  ==> the size.
static long
SetupWidthsFont(Fixed size, const char* sfnt, FontEngineInfo* info, long* face)
{
	const char* hhea = FindFontTable(sfnt, kTagHhea);
	info->fNumHMetrics = Get16(hhea + 0x22);
	info->fHmtx = FindFontTable(sfnt, kTagHmtx);
	const char* head = FindFontTable(sfnt, kTagHead);
	Fixed scale = FixedDivide(size, ToFixed(Get16(head + 0x12)));
	info->fWidthsScale = scale;
	long remaining = *face & ~Get16(head + 0x2c);
	const char* hsty = FindFontTable(sfnt, kTagHsty);
	long extra = 0;
	if (remaining == 0)
		extra = GetS16(hsty + 4);
	else
	{
		const char* p = hsty + 6;
		for (long bits = remaining; bits != 0; bits >>= 1, p += 2)
			if (bits & 1)
				extra += GetS16(p);
		*face = 0;
	}
	info->fWidthsAdjust = extra;
	info->fGetGlyphInfo = SFNTGetWidthsInfo;
	info->fGetGlyph = SFNTGetWidthsGlyph;
	info->fAscent = (short) ((scale * GetS16(hhea + 4) + 0x8000) >> 16);
	info->fDescent = -(short) ((scale * GetS16(hhea + 6) + 0x8000) >> 16);
	info->fLeading = (short) ((scale * GetS16(hhea + 8) + 0x8000) >> 16);
	info->fWidMax = (short) ((scale * Get16(hhea + 10) + 0x8000) >> 16);
	info->fReserved10 = 0;
	info->fReserved14 = 0;
	info->fMinOriginSB = 0;
	info->fMinAdvanceSB = 0;
	info->fMaxBeforeBL = 0;
	info->fMinAfterBL = 0;
	return size;
}


// ROM 0x000ae274 ChooseStrike__FlRC6RefVarPl
// The family's 'sfnt' for the face: boldItalicData for bold italic,
// italicData for italic (or a bold italic the family lacks), boldData
// for bold, else plainData; faceUsed says which of bold and italic the
// data has.
Ref
ChooseStrike(long face, RefArg fontFamily, long* faceUsed)
{
	RefVar data;
	*faceUsed = 0;
	long which = face & 3;
	if (which == 3)
	{
		data = GetFrameSlotRef(fontFamily, RSSYMbolditalicdata);
		if ((Ref) data != NILREF)
		{
			*faceUsed = 3;
			return data;
		}
		which = 2;
	}
	if (which == 2)
	{
		data = GetFrameSlotRef(fontFamily, RSSYMitalicdata);
		if ((Ref) data != NILREF)
		{
			*faceUsed = 2;
			return data;
		}
	}
	else if (which == 1)
	{
		data = GetFrameSlotRef(fontFamily, RSSYMbolddata);
		if ((Ref) data != NILREF)
		{
			*faceUsed = 1;
			return data;
		}
	}
	return GetFrameSlotRef(fontFamily, RSSYMplaindata);
}


// ROM 0x000aec68 FindSFNT__FlRC6RefVarP14FontEngineInfoPl
// The family's data for the face opened: the 'cmap' subtable for the
// family's encoding (its platform id) and its mapping, the one-bit strike
// nearest the size (its line metrics) or the widths.  ==> the strike's
// size in 16.16 (0 when there is no font); face keeps the bits the data
// does not have (bold, italic, underline, outline, superscript, subscript).
long
FindSFNT(Fixed size, RefArg fontFamily, FontEngineInfo* info, long* face)
{
	long wanted = *face;
	long encoding = RINT(GetFrameSlotRef(fontFamily, RSSYMencoding));
	long faceUsed;
	RefVar data(ChooseStrike(wanted, fontFamily, &faceUsed));
	*info->fFontData = data;
	*face = wanted & ~faceUsed & 0x18f;
	if ((Ref) data == NILREF)
		return 0;
	LockRef(data);
	const char* sfnt = BinaryData(data);
	const char* cmap = FindFontTable(sfnt, kTagCmap);
	if (cmap == nil)
	{
		UnlockRef(data);
		return 0;
	}
	const char* subTable = nil;
	long count = Get16(cmap + 2);
	for (long i = 0; i < count; i++)
		if ((long) Get16(cmap + 4 + i * 8) == encoding)
			subTable = cmap + Get32(cmap + 8 + i * 8);
	if (subTable == nil)
	{
		UnlockRef(data);
		return 0;
	}
	info->fSfnt = sfnt;
	info->fCmap = subTable;
	switch (Get16(subTable))
	{
	case 0:		info->fMap = MapFormat0;		break;
	case 2:		info->fMap = MapFormat2;		break;
	case 4:		info->fMap = FrameHasSlotRef(fontFamily, Intern((char*) "badFontMap")) ? MapFormat4Patched : MapFormat4;	break;
	case 6:		info->fMap = MapFormat6;		break;
	}
	const char* bloc = FindFontTable(sfnt, kTagBloc);
	if (bloc == nil)
		return SetupWidthsFont(size, sfnt, info, face);
	const char* strike = LocateEntry(size, bloc);
	if (strike == nil)
	{
		UnlockRef(data);
		return 0;
	}
	info->fStrike = strike;
	info->fIndexSubTables = bloc + Get32(strike);
	info->fBdat = FindFontTable(sfnt, kTagBdat);
	info->fGetGlyphInfo = SFNTGetGlyphInfo;
	info->fGetGlyph = SFNTGetGlyph;
	info->fAscent = GetS8(strike + 0x10);
	info->fDescent = -GetS8(strike + 0x11);
	info->fLeading = 0;
	info->fWidMax = Get8(strike + 0x12);
	info->fReserved10 = 0;
	info->fReserved14 = 0;
	info->fMinOriginSB = GetS8(strike + 0x16);
	info->fMinAdvanceSB = GetS8(strike + 0x17);
	info->fMaxBeforeBL = GetS8(strike + 0x18);
	info->fMinAfterBL = GetS8(strike + 0x19);
	return (long) Get8(strike + 0x2c) << 16;
}


// ROM 0x000aef10 IsSizeAvailable__FlRC6RefVarT1
// Whether the family has a strike of exactly the size for the face.
static Boolean
IsSizeAvailable(Fixed size, RefArg fontFamily, long face)
{
	long faceUsed;
	RefVar data(ChooseStrike(face, fontFamily, &faceUsed));
	if ((Ref) data == NILREF)
		return false;
	const char* sfnt = BinaryData(data);
	const char* bloc = FindFontTable(sfnt, kTagBloc);
	if (bloc == nil)
		return false;
	long wanted = (short) ((size + 0x8000) >> 16);
	long count = Get32(bloc + 4);
	for (long i = 0; i < count; i++)
		if ((long) Get8(bloc + 8 + i * kStrikeEntrySize + 0x2c) == wanted)
			return true;
	return false;
}


// ROM 0x000ae820 SFNTReopenFont__FPv
// (The ROM refreshes the info from its cache copy and re-locks the data;
// with no cache the info is what SFNTOpenFont left.)
static long
SFNTReopenFont(FontEngineInfo* info)
{
	return info->fScaling;
}


// ROM 0x000aeb24 SFNTCloseFont__FPv
static void
SFNTCloseFont(FontEngineInfo* info)
{
	if ((Ref) *info->fFontData != NILREF)
		UnlockRef(*info->fFontData);
}


// ROM 0x002e2724 UpdateStyleTable__FlT1
// The style table scaled to the size a font is being drawn at.  A face
// that QuickDraw synthesises - bold, italic, underline, outline, shadow
// - is made out of fixed numbers of pixels (how far the bold smear
// goes, how thick the underline is), and those numbers are right for a
// font drawn as it is.  A font drawn scaled has to have them scaled
// with it, or a big letter gets a hairline underline.
//
// Only some of the table's bytes are measurements: the rest are which
// adjustment a face touches, and those are left alone.  The horizontal
// ones go by the horizontal scale, the vertical ones by the vertical,
// and the two that are neither (the outline's spread) by the mean of
// the two.
//
// (BUG, kept: the bytes are read unsigned, so the one entry that is a
//  negative adjustment - 0xff, minus one - comes out of the scaling as
//  255 times the scale rather than minus the scale.  It only matters
//  for a font that is being scaled at all.)
//
// DEVIATION: the ROM keeps the scaled table in the open font's own
// entry of the global font array (gGlobalFontArray, 56 bytes an entry),
// which the reconstruction's font cache is NOT YET.  One scaled table
// is kept here and remade whenever the scale changes, which comes to
// the same thing while one font is opened at a time.
const unsigned char*
UpdateStyleTable(Fixed xScale, Fixed yScale)
{
	if (xScale == ToFixed(1) && yScale == ToFixed(1))
		return kStyleTable;

	static unsigned char	sScaled[sizeof(kStyleTable)];
	static Fixed			sScaledX = 0;
	static Fixed			sScaledY = 0;
	if (sScaledX == xScale && sScaledY == yScale)
		return sScaled;
	sScaledX = xScale;
	sScaledY = yScale;

	Fixed mean = (xScale + yScale) >> 1;
	memcpy(sScaled, kStyleTable, sizeof(sScaled));
	// which byte goes by which scale
	static const unsigned char kAcross[] = { 3, 4, 12, 13, 18, 19, 21, 22 };
	static const unsigned char kDown[] = { 23, 25 };
	static const unsigned char kMean[] = { 15, 16, 24 };
	for (ULong i = 0; i < sizeof(kAcross); i++)
		sScaled[kAcross[i]] = (unsigned char)
			(((ULong) xScale * sScaled[kAcross[i]] + 0x8000) >> 16);
	for (ULong i = 0; i < sizeof(kDown); i++)
		sScaled[kDown[i]] = (unsigned char)
			(((ULong) yScale * sScaled[kDown[i]] + 0x8000) >> 16);
	for (ULong i = 0; i < sizeof(kMean); i++)
		sScaled[kMean[i]] = (unsigned char)
			(((ULong) mean * sScaled[kMean[i]] + 0x8000) >> 16);
	return sScaled;
}


// ROM 0x000adf2c SFNTOpenFont__FP8PixelMapP11StyleRecordRC6RefVarlT4P14FontEngineInfo
// The info filled for the style in the family: the size scaled by
// xScale (a superscript or subscript at four fifths of it, raised or
// lowered by three eighths of the ascent), the strike found, the scale
// from the strike's size to the wanted one (1.0 when they agree, or
// when the map is a screen and the scales agree, or the reduced size
// exists as a strike), and the style table's adjustments for the faces
// the data lacks.  ==> 0 as is, 2 scaled, 3 no font.
long
SFNTOpenFont(PixelMap* /*pm*/, StyleRecord* style, RefArg fontFamily, Fixed xScale, Fixed yScale, FontEngineInfo* info)
{
	long face = style->fFontFace;
	Fixed styleSize = style->fFontSize;
	Fixed size = FixedMultiply(styleSize, xScale);
	Fixed wanted = size;
	Boolean superscript = (face & kSuperscriptFace) != 0;
	Boolean subscript = (face & kSubscriptFace) != 0;
	Boolean available = false;
	if (superscript || subscript)
	{
		styleSize = FixedMultiply(styleSize, 0xcccd);
		wanted = FixedMultiply(size, 0xcccd);
		available = IsSizeAvailable(size, fontFamily, face);
	}
	long strikeSize = FindSFNT(wanted, fontFamily, info, &face);
	if (strikeSize == 0)
		return kNoFont;
	GrafPort* port = GetCurrentPort();
	Fixed ratio;
	long result;
	if (((port->portBits.pixMapFlags & kPixMapDeviceType) == 0 && strikeSize == wanted && xScale == yScale) || available)
	{
		info->fScaleX = 0x10000;
		info->fScaleY = 0x10000;
		ratio = 0x10000;
		result = kOpenedAsIs;
	}
	else
	{
		ratio = FixedDivide(strikeSize, styleSize);
		info->fScaleX = FixedDivide(xScale, ratio);
		info->fScaleY = FixedDivide(yScale, ratio);
		result = kOpenedScaled;
	}
	info->fScaling = result;
	if (!superscript && !subscript)
		info->fBaselineShift = 0;
	else
	{
		long shift = (info->fAscent * 3) >> 3;
		if (subscript)
			shift = -shift;
		info->fAscent += shift;
		info->fDescent -= shift;
		info->fMaxBeforeBL += shift;
		info->fBaselineShift = shift;
		info->fMinAfterBL += shift;
		face &= ~(kSuperscriptFace | kSubscriptFace);
	}
	memset(info->fStyleAdjust, 0, sizeof(info->fStyleAdjust));
	info->fWidthAdjust = 0;
	info->fReserved38 = 0;
	if (face != 0)
	{
		// the table scaled to the size the font is drawn at, so that a
		// big letter does not get a hairline underline
		const unsigned char* table = UpdateStyleTable(xScale, yScale);
		const unsigned char* row = table + 2;
		for (long bits = face; bits != 0; bits >>= 1, row += 3)
			if (bits & 1)
			{
				info->fStyleAdjust[row[0]] += (signed char) row[1];
				info->fWidthAdjust += (signed char) row[2];
			}
		if (face & kUnderlineFace)
		{
			info->fStyleAdjust[2] = table[0x17] - info->fBaselineShift;
			info->fStyleAdjust[3] = table[0x18];
			info->fStyleAdjust[4] = table[0x19];
		}
	}
	info->fReopen = SFNTReopenFont;
	info->fClose = SFNTCloseFont;
	info->fCached = nil;
	return result;
}


/*------------------------------------------------------------------------------
	T h e   f o n t   m a n a g e r
------------------------------------------------------------------------------*/

// ROM 0x002e2074 InitFonts__Fv
void
InitFonts(void)
{
	EngineInitSFNT();
}


// ROM 0x002e2048 FlushFontCache__Fv
// The four entries of the open-font cache (gFontGlobals, 0x38 bytes each)
// marked empty, so that no family a removed font part took away is still
// used.  The host keeps no cache (OpenFont, above), so there is nothing to
// empty.
void
FlushFontCache(void)
{
}


// ROM 0x002e2070 LoadFontTable__Fv
// Empty in the ROM too (InitFontLoader calls it last).
void
LoadFontTable(void)
{
}


// the ROM's font list (the magic pointer the packed font spec indexes)
Ref
GetROMFontList(void)
{
	return Rromfontlist;
}


// a family frame by symbol through vars.fonts
Ref
GetFontFamily(RefArg familySymbol)
{
	RefVar fonts(GetFrameSlotRef(RefVar(gVarFrame), RSSYMfonts));
	if ((Ref) fonts == NILREF)
		return NILREF;
	return GetProtoVariable(fonts, familySymbol, nil);
}


// ROM 0x002e20e0 SearchFont__FlPUs
// The family in vars.fonts with the Mac font id (0: any) or the name;
// the system font when none matches.
Ref
SearchFont(long macFontID, const UniChar* name)
{
	RefVar fonts(GetFrameSlotRef(RefVar(gVarFrame), RSSYMfonts));
	RefVar found;
	RefVar family;
	TObjectIterator iter(fonts, true);
	for (; !iter.Done(); iter.Next())
	{
		family = iter.Value();
		if (!IsFrame(family))
			continue;
		if (macFontID != 0)
		{
			Ref id = GetFrameSlotRef(family, RSSYMmacfontid);
			if (ISINT(id) && RINT(id) == macFontID)
			{
				found = family;
				break;
			}
		}
		if (name != nil)
		{
			RefVar familyName(GetFrameSlotRef(family, RSSYMname));
			if ((Ref) familyName != NILREF && IsString(familyName) && Ustrcmp(name, GetCString(familyName)) == 0)
			{
				found = family;
				break;
			}
		}
	}
	if ((Ref) found == NILREF)
		found = GetProtoVariable(fonts, RefVar(Rsystemfont), nil);
	return found;
}


// The ink font's opener and its measurements, registered from outside
// (Fonts.h says why).
FontInkOpenProc	gInkOpenFont = nil;
FontInkParmsProc	gInkFontParms = nil;
FontColorPatternProc	gFontColorPattern = nil;
FontInkSetParmsProc	gInkSetFontParms = nil;


// ROM 0x002e229c OpenFont__FP8PixelMapP11StyleRecordlT3P14FontEngineInfo
// The info for the style (the system font when it names none): the
// family's 'sfnt' opened at the scales.
//
// A style whose "family" is an ink word - or an integer, which is the
// address of one kept outside the object heap - is not a font at all,
// and goes to the ink opener instead: that is how a word of writing is
// laid out and drawn among real characters.
//
// NOT YET RECONSTRUCTED: the four-entry cache of open fonts (each call
// opens afresh), the PostScript printer's font substitution.
long
OpenFont(PixelMap* pm, StyleRecord* style, Fixed xScale, Fixed yScale, FontEngineInfo* info)
{
	RefVar family(style->fFontFamily);
	if ((Ref) family == NILREF)
		family = GetFontFamily(RefVar(Rsystemfont));
	if (IsInkWord(family) || ISINT(family))
	{
		if (gInkOpenFont == nil)
		{
			info->fScaling = kNoFont;
			return kNoFont;
		}
		memset(info, 0, sizeof(FontEngineInfo));
		info->fFontData = new RefStruct;
		long inked = gInkOpenFont(pm, style, family, xScale, yScale, info);
		if (inked == kNoFont)
		{
			delete info->fFontData;
			info->fFontData = nil;
			info->fScaling = kNoFont;
		}
		return inked;
	}
	if (!IsFrame(family))
	{
		info->fScaling = kNoFont;
		return kNoFont;
	}
	memset(info, 0, sizeof(FontEngineInfo));
	info->fFontData = new RefStruct;
	long result = SFNTOpenFont(pm, style, family, xScale, yScale, info);
	if (result == kNoFont)
	{
		delete info->fFontData;
		info->fFontData = nil;
		info->fScaling = kNoFont;
	}
	return result;
}


// Host: an open font closed (what the ROM's info->fClose does, and the
// data the host allocated freed).
void
CloseFont(FontEngineInfo* info)
{
	if (info->fClose != nil)
		info->fClose(info);
	delete info->fFontData;
	info->fFontData = nil;
	info->fClose = nil;
}


// ROM 0x002e1f04 GetStyleFontInfo__FP11StyleRecordP8FontInfo
// The style's ascent, descent, leading and widest glyph, scaled when the
// strike is not the style's size.
void
GetStyleFontInfo(StyleRecord* style, FontInfo* fontInfo)
{
	FontEngineInfo info;
	long result = OpenFont(&GetCurrentPort()->portBits, style, 0x10000, 0x10000, &info);
	if (result == kNoFont)
	{
		fontInfo->ascent = fontInfo->descent = fontInfo->widMax = fontInfo->leading = 0;
		return;
	}
	if (info.fScaleY == 0x10000)
	{
		fontInfo->ascent = info.fAscent;
		fontInfo->descent = info.fDescent;
		fontInfo->leading = info.fLeading;
	}
	else
	{
		fontInfo->ascent = (short) RoundFixed(FixedMultiply(ToFixed(info.fAscent), info.fScaleY));
		fontInfo->descent = (short) RoundFixed(FixedMultiply(ToFixed(info.fDescent), info.fScaleY));
		fontInfo->leading = (short) RoundFixed(FixedMultiply(ToFixed(info.fLeading), info.fScaleY));
	}
	fontInfo->widMax = info.fWidMax;
	if (info.fScaleX != 0x10000)
		fontInfo->widMax = (short) RoundFixed(FixedMultiply(ToFixed(info.fWidMax), info.fScaleX));
	CloseFont(&info);
}


// ROM 0x002e1e60 FontRefToCharSize__FRC6RefVar
// The character size a font spec implies: 1 for a family with an
// encoding, 2 otherwise.
long
FontRefToCharSize(RefArg font)
{
	Ref ref = font;
	if (!ISINT(ref) && ref != NILREF && IsFrame(font))
	{
		Ref encoding = GetFrameSlotRef(font, RSSYMencoding);
		if (ISINT(encoding) && RINT(encoding) != 0)
			return 1;
	}
	return 2;
}


/*------------------------------------------------------------------------------
	S t y l e s
------------------------------------------------------------------------------*/

// ROM 0x0035a54c MakeSimpleStyle__FRC6RefVarlT2
void
MakeSimpleStyle(StyleRecord* style, RefArg fontFamily, long size, long face)
{
	style->fFontFamily = (Ref) fontFamily;
	style->fFontSize = (Fixed) size;
	style->fFontFace = face;
	style->fFontPattern = 0;
	style->fTransferMode = 0;
	style->fReserved14 = 0;
	style->fReserved18 = 0;
	style->fPattern = nil;
}


// ROM 0x00179e68 FamilyNumToSym__Fl
// The packed family numbers' symbols.
Ref
FamilyNumToSym(long family)
{
	switch (family)
	{
	case 0:		return RSSYMespy;
	case 1:		return RSSYMnewyork;
	case 2:		return RSSYMgeneva;
	case 3:		return RSSYMhandwriting;
	default:	return NILREF;
	}
}


// ROM 0x0017aca8 GetFontSize__FRC6RefVar
// The size of a packed font spec, or a font frame's size slot (an
// integer, else type.ref.frame).  An ink word is laid out at the size
// its own glyph answers; anything else is 0.
long
GetFontSize(RefArg fontSpec)
{
	if (ISINT(fontSpec))
		return PackedFontSize(RVALUE(fontSpec));
	if (!IsFrame(fontSpec))
	{
		long size = 0;
		long face = 0;
		if (gInkFontParms != nil && gInkFontParms(fontSpec, &size, &face))
			return size;
		return 0;
	}
	RefVar size(GetFrameSlotRef(fontSpec, RSSYMsize));
	if (!ISINT(size))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, size);
	return RVALUE(size);
}


// ROM 0x0017bbc4 GetFontFace__FRC6RefVar
// The face likewise (the face slot, and the word's own face for ink).
long
GetFontFace(RefArg fontSpec)
{
	if (ISINT(fontSpec))
		return PackedFontFace(RVALUE(fontSpec));
	if (!IsFrame(fontSpec))
	{
		long size = 0;
		long face = 0;
		if (gInkFontParms != nil && gInkFontParms(fontSpec, &size, &face))
			return face;
		return 0;
	}
	RefVar face(GetFrameSlotRef(fontSpec, RSSYMface));
	if (!ISINT(face))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, face);
	return RVALUE(face);
}


// ROM 0x0017cda4 GetFontFamilySym__FRC6RefVar
// The family symbol: a packed spec's number's, a font frame's family
// slot; nil for anything else.
Ref
GetFontFamilySym(RefArg fontSpec)
{
	if (ISINT(fontSpec))
		return FamilyNumToSym(PackedFontFamily(RVALUE(fontSpec)));
	if (!IsFrame(fontSpec))
		return NILREF;
	return GetFrameSlotRef(fontSpec, RSSYMfamily);
}


// ROM 0x00179d90 FamilySymToNum__FRC6RefVar
// The four built-in families' numbers; nil for a family that has none,
// which is what makes a font spec stay a frame rather than being packed
// into an integer.
Ref
FamilySymToNum(RefArg family)
{
	if (EQRef(family, RSSYMespy))			return MAKEINT(0);
	if (EQRef(family, RSSYMnewyork))		return MAKEINT(1);
	if (EQRef(family, RSSYMgeneva))			return MAKEINT(2);
	if (EQRef(family, RSSYMhandwriting))	return MAKEINT(3);
	return NILREF;
}


// ROM 0x0017c7f0 GetFontFamilyNum__FRC6RefVar
// The family number of a font spec: a packed spec's low ten bits, a font
// frame's family symbol looked up; nil for anything else (an ink word
// included - it has no family).
Ref
GetFontFamilyNum(RefArg fontSpec)
{
	if (ISINT(fontSpec))
		return MAKEINT(PackedFontFamily(RVALUE(fontSpec)));
	if (!IsFrame(fontSpec))
		return NILREF;
	return FamilySymToNum(RefVar(GetFrameSlotRef(fontSpec, RSSYMfamily)));
}


// ROM 0x0017a364 MakeCompactFont__FRC6RefVarlT2
// A font spec out of a family, a size and a face.  A family that has a
// number packs into one integer - family in the low ten bits, size in the
// next ten, face above that - and one that has not becomes a
// canonicalFontSpec frame with the three slots instead.
//
// The ROM masks none of the three when it packs them, so a size or face
// beyond ten bits runs into the field above it; that is the machine's
// own arithmetic and it is kept here.
Ref
MakeCompactFont(RefArg family, long size, long face)
{
	RefVar number(family);
	if (!ISINT(number))
		number = FamilySymToNum(family);
	if (ISINT(number))
		return MAKEINT(RVALUE(number) | (size << 10) | (face << 20));

	RefVar spec(Clone(RefVar(Rcanonicalfontspec)));
	SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(size)));
	SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(face)));
	RefVar symbol(family);
	if (ISINT(symbol))
		// (unreachable: an integer family was packed above.  The ROM
		//  passes the Ref rather than its value here, so even if it were
		//  reached it would answer nil for every family.)
		symbol = FamilyNumToSym((long) (Ref) symbol);
	SetFrameSlot(spec, RSSYMfamily, symbol);
	return spec;
}


// ROM 0x00179358 IntFontToFontParms__FRC6RefVar
// A packed font spec opened out into a canonicalFontSpec frame, which is
// the form the style slip and ChangeStylesOfRange work in.  A part that
// reads as nothing is left out: a size of 0, a face of 0x20000000, and a
// family of 0.
//
// GetFontFace cannot answer 0x20000000 - it masks the packed face to ten
// bits and answers 0 for anything that is not a spec at all - so that
// test never fires; it is a sentinel from some earlier arrangement.
//
// (Leaving out family 0 means Espy - the system font, and family number
// 0 - never reaches the frame, so a spec of that family comes back
// without one.  The ROM does this, and the slot then falls back to the
// view's own font.)
Ref
IntFontToFontParms(RefArg font)
{
	RefVar parms(Clone(RefVar(Rcanonicalfontspec)));
	long size = GetFontSize(font);
	if (size != 0)
		SetFrameSlot(parms, RSSYMsize, RefVar(MAKEINT(size)));
	long face = GetFontFace(font);
	if (face != 0x20000000)
		SetFrameSlot(parms, RSSYMface, RefVar(MAKEINT(face)));
	RefVar family(GetFontFamilyNum(font));
	if (ISINT(family) && RVALUE(family) != 0)
		SetFrameSlot(parms, RSSYMfamily, family);
	return parms;
}


// ROM 0x0017d164 SetFontParms__FRC6RefVarT1
// A font spec with the parts of `parms` (a frame with any of family,
// size and face) put into it; what it does not name is taken from the
// spec as it stands.  ==> the new spec.
//
// An ink word is restyled rather than replaced - it carries its own
// measurements - which is the ink area's business, so it goes out
// through the hook InitializeInkFont installs (gInkSetFontParms; the
// same arrangement as gInkFontParms above).
Ref
SetFontParms(RefArg fontSpec, RefArg parms)
{
	Ref inkResult = NILREF;
	if (gInkSetFontParms != nil && gInkSetFontParms(fontSpec, parms, &inkResult))
		return inkResult;

	RefVar family(GetFrameSlotRef(parms, RSSYMfamily));
	if (ISNIL(family))
		family = GetFontFamilySym(fontSpec);
	RefVar size(GetFrameSlotRef(parms, RSSYMsize));
	long theSize = ISNIL(size) ? GetFontSize(fontSpec) : RINT(size);
	RefVar face(GetFrameSlotRef(parms, RSSYMface));
	long theFace = ISNIL(face) ? GetFontFace(fontSpec) : RINT(face);
	return MakeCompactFont(family, theSize, theFace);
}


// the family frame for a packed font spec's family index (the ROM font
// list; the user's font, the system font, beyond it)
static Ref
PackedFontFamilyFrame(long font)
{
	RefVar list(GetROMFontList());
	long index = PackedFontFamily(font);
	if (index < Length(list))
		return GetArraySlotRef(list, index);
	return NILREF;
}


// ROM 0x002618b8 CreateTextStyleRecord__FRC6RefVarP11StyleRecord
// A style from a font spec: a packed integer (the ROM font list index,
// size and face), or an ink word, or a frame {family, size, face, color}
// (the family a symbol through vars.fonts, the color a pattern).  No
// family means the user's preference (userFont), else the system font.
//
// An ink word is not a font at all: the word itself goes where the
// family would, and OpenFont hands it to the ink font engine, which
// makes a font of one glyph out of it.  The size and face are the
// word's own.
//
// ROM BUG, kept: a spec that is none of those - a nil `styles` slot, for
// one, which is what TParagraphView::GetInterLineSpacing hands over for a
// paragraph that has no style runs - leaves the size and the face as the
// caller left them, and every caller builds its StyleRecord on the stack.
// The family is put right afterwards (it is a RefStruct, so it starts
// nil), but the size is whatever was underneath, and the font engine then
// scales a strike to it.  Nothing crashes on the machine, because the ARM
// is content to shift and multiply nonsense; on the host the arithmetic is
// written so that it wraps in the same way rather than trapping.
void
CreateTextStyleRecord(RefArg fontSpec, StyleRecord* style)
{
	style->fFontPattern = 0;
	style->fPattern = nil;
	long inkSize = 0;
	long inkFace = 0;
	Ref spec = fontSpec;
	if (ISINT(spec))
	{
		long font = RINT(spec);
		style->fFontFamily = PackedFontFamilyFrame(font);
		style->fFontSize = ToFixed(PackedFontSize(font));
		style->fFontFace = PackedFontFace(font);
	}
	else if (gInkFontParms != nil && gInkFontParms(fontSpec, &inkSize, &inkFace))
	{
		// (the ROM reads the word's own measurements here - its scaled
		//  font size and its face - which is what the seam answers)
		style->fFontFamily = fontSpec;
		style->fFontSize = ToFixed(inkSize);
		style->fFontFace = inkFace;
		style->fTransferMode = 0;
		style->fReserved14 = 0;
		style->fReserved18 = 0;
		return;
	}
	else if (IsFrame(fontSpec))
	{
		style->fFontFamily = GetFontFamily(RefVar(GetFrameSlotRef(fontSpec, RSSYMfamily)));
		style->fFontSize = ToFixed(RINT(GetFrameSlotRef(fontSpec, RSSYMsize)));
		style->fFontFace = RINT(GetFrameSlotRef(fontSpec, RSSYMface));
		RefVar color(GetFrameSlotRef(fontSpec, RSSYMcolor));
		PatternHandle pattern;
		if ((Ref) color != NILREF && gFontColorPattern != nil && gFontColorPattern(color, &pattern))
		{
			style->fPattern = pattern;
			style->fFontPattern = AddressToRef(pattern);
		}
	}
	if ((Ref) style->fFontFamily == NILREF)
	{
		RefVar userFont(GetFrameSlotRef(RefVar(GetFrameSlotRef(RefVar(gVarFrame), RSSYMuserconfiguration)), RSSYMuserfont));
		if (ISINT((Ref) userFont))
		{
			long index = PackedFontFamily(RINT(userFont));
			RefVar list(GetROMFontList());
			if (index >= Length(list))
				index = 1;
			style->fFontFamily = GetArraySlotRef(list, index);
		}
		else if (IsFrame(userFont))
			style->fFontFamily = GetFontFamily(RefVar(GetFrameSlotRef(userFont, RSSYMfamily)));
	}
	style->fTransferMode = 0;
	style->fReserved14 = 0;
	style->fReserved18 = 0;
}


// ROM 0x00359be8 CopyStyle__FP11StyleRecord
// (The ROM copies a style into the font cache, re-making its pattern;
// the host keeps no cache.)
void
CopyStyle(StyleRecord* /*style*/)
{ }


// ROM 0x00359cb8 EqualStyle__FP11StyleRecordT1
Boolean
EqualStyle(const StyleRecord* a, const StyleRecord* b)
{
	return (Ref) a->fFontFamily == (Ref) b->fFontFamily && a->fFontSize == b->fFontSize && a->fFontFace == b->fFontFace
		&& a->fFontPattern == b->fFontPattern && a->fTransferMode == b->fTransferMode;
}


// Host: the pattern CreateTextStyleRecord made disposed.
void
DisposeStyleRecord(StyleRecord* style)
{
	if (style->fPattern != nil)
	{
		DisposePattern(style->fPattern);
		style->fPattern = nil;
	}
}
