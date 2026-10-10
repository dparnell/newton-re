/*
	File:		qd/HostFontEngine.cpp

	Contains:	Fonts the host draws - HostFonts.h.  DEVIATION throughout:
				the ROM has the 'sfnt' engine and the ink engine only.

	The engine fills a FontEngineInfo as SFNTOpenFont does - the line
	metrics, the procedures, the synthesised faces' adjustments - from a
	THostFace opened at the size wanted.  Its faces are kept open in a
	small cache of their own (eight, each with the glyphs asked of it)
	rather than in the ROM's four-entry cache, whose entries keep the
	'sfnt' engine's pointers as offsets into the font's binary.
*/

#include "HostFonts.h"
#include "FixedMath.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Unicode.h"
#include "Interpreter.h"
#include "NewtonExceptions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const long kOpenedAsIs = 0;			// (Fonts.cpp's answers)
const long kOpenedScaled = 2;
const long kNoFont = 3;

static THostFontProvider*	gHostFontProvider = nil;


static Ref
Sym(const char* name)
{
	return Intern((char*) name);
}


/*------------------------------------------------------------------------------
	T h e   f a c e   c a c h e
------------------------------------------------------------------------------*/

const long kFamilyNameSize = 64;
const long kGlyphSlots = 256;			// a face's glyphs, by the character's low byte
const long kFaceEntries = 8;

struct HostGlyphSlot
{
	bool			fUsed;
	UniChar			fChar;
	long			fWidth;
	long			fHeight;
	long			fBearingX;
	long			fBearingY;
	Fixed			fAdvance;
	long			fRowBytes;
	unsigned char*	fBits;				// one bit a pixel, cut to what the slab has room for
};

struct HostFaceEntry
{
	UniChar			fFamily[kFamilyNameSize];
	long			fFace;				// the host's face: plain, bold, italic or bold italic
	long			fPixelsX;
	long			fPixelsY;
	THostFace*		fHostFace;			// nil: an empty entry
	long			fUsers;				// infos open on it
	ULong			fLastUse;
	// the bounds every glyph is kept within (FontEngineInfo's fMaxBeforeBL ...)
	long			fMaxBeforeBL;
	long			fMinAfterBL;
	long			fMinOriginSB;
	long			fMinAdvanceSB;
	HostFaceMetrics	fMetrics;
	// the system font, for the characters the face lacks (opened when
	// first wanted; fFallbackTried once it has been)
	bool			fFallbackTried;
	FontEngineInfo*	fFallback;
	HostGlyphSlot	fGlyphs[kGlyphSlots];
};

static HostFaceEntry*	gFaces[kFaceEntries];
static ULong			gFaceClock = 0;


static void
CloseFallback(HostFaceEntry* entry)
{
	FontEngineInfo* fb = entry->fFallback;
	if (fb == nil)
		return;
	if (fb->fClose != nil)
		fb->fClose(fb);
	delete fb->fFontData;
	if (fb->fCached != nil)
	{
		delete fb->fCached->fFontData;
		delete fb->fCached;
	}
	delete fb;
	entry->fFallback = nil;
}


static void
EmptyEntry(HostFaceEntry* entry)
{
	for (long i = 0; i < kGlyphSlots; i++)
	{
		free(entry->fGlyphs[i].fBits);
		entry->fGlyphs[i].fBits = nil;
		entry->fGlyphs[i].fUsed = false;
	}
	CloseFallback(entry);
	entry->fFallbackTried = false;
	delete entry->fHostFace;
	entry->fHostFace = nil;
}


void
FlushHostFontCache(void)
{
	for (long i = 0; i < kFaceEntries; i++)
		if (gFaces[i] != nil)
		{
			if (gFaces[i]->fUsers > 0)
				continue;					// (given back when it is closed: see HostCloseFont)
			EmptyEntry(gFaces[i]);
			delete gFaces[i];
			gFaces[i] = nil;
		}
}


// The entry for the face at the size: one open already, or a new one in
// an empty entry, or in the one least recently used that nobody has open
// (a new entry beyond the cache when every one is open, given back when
// it is closed).
static HostFaceEntry*
FindFace(const UniChar* family, long face, long pixelsX, long pixelsY)
{
	for (long i = 0; i < kFaceEntries; i++)
	{
		HostFaceEntry* entry = gFaces[i];
		if (entry != nil && entry->fHostFace != nil && entry->fFace == face && entry->fPixelsX == pixelsX
		 && entry->fPixelsY == pixelsY && Ustrcmp(entry->fFamily, family) == 0)
		{
			entry->fLastUse = ++gFaceClock;
			return entry;
		}
	}
	// where it goes: an empty entry, else the least recently used that
	// nobody has open, else (-1) outside the cache
	long into = -1;
	for (long i = 0; i < kFaceEntries && into < 0; i++)
		if (gFaces[i] == nil)
			into = i;
	if (into < 0)
		for (long i = 0; i < kFaceEntries; i++)
			if (gFaces[i]->fUsers == 0 && (into < 0 || gFaces[i]->fLastUse < gFaces[into]->fLastUse))
				into = i;
	THostFace* hostFace = gHostFontProvider->OpenFace(family, face, pixelsX, pixelsY);
	if (hostFace == nil)
		return nil;
	HostFaceEntry* entry;
	if (into < 0)
		entry = (HostFaceEntry*) calloc(1, sizeof(HostFaceEntry));
	else if (gFaces[into] == nil)
		entry = gFaces[into] = (HostFaceEntry*) calloc(1, sizeof(HostFaceEntry));
	else
	{
		entry = gFaces[into];
		EmptyEntry(entry);
	}
	if (entry == nil)
	{
		delete hostFace;
		return nil;
	}
	long n = Ustrlen(family);
	if (n > kFamilyNameSize - 1)
		n = kFamilyNameSize - 1;
	memcpy(entry->fFamily, family, n * sizeof(UniChar));
	entry->fFamily[n] = 0;
	entry->fFace = face;
	entry->fPixelsX = pixelsX;
	entry->fPixelsY = pixelsY;
	entry->fHostFace = hostFace;
	entry->fUsers = 0;
	entry->fLastUse = ++gFaceClock;
	hostFace->GetMetrics(&entry->fMetrics);
	// the bounds the slab is made to (DrText.cpp's DrTextChunk): above and
	// below the baseline, as far as the face says any glyph goes - kept to
	// twice the size - and to the left as far, and to the right a quarter
	// of the size past a glyph's advance; a glyph is cut to them
	const HostFaceMetrics& m = entry->fMetrics;
	long limit = pixelsY * 2;
	long above = m.top > m.ascent ? m.top : m.ascent;
	long below = m.bottom < -m.descent ? m.bottom : -m.descent;
	entry->fMaxBeforeBL = above > limit ? limit : above;
	entry->fMinAfterBL = below < -limit ? -limit : below;
	long left = m.left < 0 ? m.left : 0;
	entry->fMinOriginSB = left < -pixelsX ? -pixelsX : left;
	entry->fMinAdvanceSB = -(pixelsX / 4 + 1);
	return entry;
}


// The system font at the face's size, for the characters the face lacks
// (its own bold when the host's face is bold; the faces synthesised are
// worked on the whole slab anyway).  Opened through the 'sfnt' engine
// with an info and a cached copy of its own, so that the ROM's cache is
// not touched.
static FontEngineInfo*
Fallback(HostFaceEntry* entry)
{
	if (entry->fFallbackTried)
		return entry->fFallback;
	entry->fFallbackTried = true;
	RefVar family(GetFontFamily(RefVar(Rsystemfont)));
	if (!IsFrame(family))
		return nil;
	FontEngineInfo* fb = new FontEngineInfo;
	memset(fb, 0, sizeof(FontEngineInfo));
	fb->fFontData = new RefStruct;
	fb->fCached = new FontEngineInfo;
	memset(fb->fCached, 0, sizeof(FontEngineInfo));
	fb->fCached->fFontData = new RefStruct;
	entry->fFallback = fb;
	StyleRecord style;
	style.fFontFamily = family;
	style.fFontSize = ToFixed(entry->fPixelsY);
	style.fFontFace = entry->fFace & kBoldFace;
	if (SFNTOpenFont(nil, &style, family, ToFixed(1), ToFixed(1), fb) == kNoFont)
	{
		fb->fClose = nil;
		CloseFallback(entry);
	}
	return entry->fFallback;
}


// A glyph's bits kept in the slot, one bit a pixel, cut to the entry's
// bounds: no further left of the pen than fMinOriginSB, no further right
// of its advance than -fMinAdvanceSB, nothing above fMaxBeforeBL or
// below fMinAfterBL (DrTextChunk clips the rows itself; the columns it
// trusts the font for).
static void
KeepGlyph(HostFaceEntry* entry, HostGlyphSlot* slot, long width, long height, long bearingX, long bearingY,
		  Fixed advance, long depth, long rowBytes, const unsigned char* bits)
{
	slot->fAdvance = advance;
	long firstCol = 0;
	if (bearingX < entry->fMinOriginSB)
		firstCol = entry->fMinOriginSB - bearingX;
	long lastCol = width;								// (past the last)
	long rightmost = (advance >> 16) - entry->fMinAdvanceSB;
	if (bearingX + lastCol > rightmost)
		lastCol = rightmost - bearingX;
	long firstRow = 0;
	if (bearingY > entry->fMaxBeforeBL)
		firstRow = bearingY - entry->fMaxBeforeBL;
	long lastRow = height;
	if (bearingY - lastRow < entry->fMinAfterBL)
		lastRow = bearingY - entry->fMinAfterBL;
	if (bits == nil || lastCol <= firstCol || lastRow <= firstRow)
	{
		slot->fWidth = slot->fHeight = 0;
		slot->fBearingX = slot->fBearingY = 0;
		slot->fRowBytes = 0;
		slot->fBits = nil;
		return;
	}
	long w = lastCol - firstCol;
	long h = lastRow - firstRow;
	long outRowBytes = (w + 7) >> 3;
	unsigned char* out = (unsigned char*) calloc((size_t) (outRowBytes * h), 1);
	if (out == nil)
	{
		slot->fWidth = slot->fHeight = 0;
		slot->fBits = nil;
		return;
	}
	for (long y = 0; y < h; y++)
	{
		const unsigned char* from = bits + (firstRow + y) * rowBytes;
		unsigned char* to = out + y * outRowBytes;
		for (long x = 0; x < w; x++)
		{
			long p = firstCol + x;
			bool on = (depth == 8) ? from[p] >= 0x80 : (from[p >> 3] & (0x80 >> (p & 7))) != 0;
			if (on)
				to[x >> 3] |= (unsigned char) (0x80 >> (x & 7));
		}
	}
	slot->fWidth = w;
	slot->fHeight = h;
	slot->fBearingX = bearingX + firstCol;
	slot->fBearingY = bearingY - firstRow;
	slot->fRowBytes = outRowBytes;
	slot->fBits = out;
}


// The character's glyph, from the cache or the host (or the system font).
static HostGlyphSlot*
GlyphSlot(HostFaceEntry* entry, UniChar ch)
{
	HostGlyphSlot* slot = &entry->fGlyphs[ch & (kGlyphSlots - 1)];
	if (slot->fUsed && slot->fChar == ch)
		return slot;
	free(slot->fBits);
	slot->fBits = nil;
	slot->fUsed = true;
	slot->fChar = ch;
	HostGlyph glyph;
	memset(&glyph, 0, sizeof(glyph));
	if (entry->fHostFace->GetGlyph(ch, 1, &glyph))
	{
		KeepGlyph(entry, slot, glyph.width, glyph.height, glyph.bearingX, glyph.bearingY,
				  glyph.advance, glyph.depth, glyph.rowBytes, glyph.bits);
		return slot;
	}
	FontEngineInfo* fb = Fallback(entry);
	if (fb != nil)
	{
		fb->fGetGlyph(ch, 0, fb);
		KeepGlyph(entry, slot, fb->fGlyphWidth, fb->fGlyphHeight, fb->fGlyphBearingX, fb->fGlyphBearingY,
				  fb->fGlyphAdvance, 1, fb->fGlyphRowBytes, fb->fGlyphBits);
		return slot;
	}
	KeepGlyph(entry, slot, 0, 0, 0, 0, 0, 1, 0, nil);
	return slot;
}


/*------------------------------------------------------------------------------
	T h e   e n g i n e ' s   p r o c e d u r e s
------------------------------------------------------------------------------*/

// The character is its own glyph: the host maps it.
static long
HostMap(long ch, const void* /*cmap*/)
{
	return ch;
}


// The advance, with the synthesised faces' extra width (ch 0: the glyph,
// which is the character, as given).
static void
HostGetGlyphInfo(long ch, long glyph, FontEngineInfo* info)
{
	HostFaceEntry* entry = (HostFaceEntry*) info->fHostFace;
	HostGlyphSlot* slot = GlyphSlot(entry, (UniChar) (ch != 0 ? ch : glyph));
	info->fGlyphAdvance = slot->fAdvance + ToFixed(info->fWidthAdjust);
}


// ... and the glyph's metrics and bits; a superscript's or subscript's
// bearing shifted, as SFNTGetGlyph shifts it.
static void
HostGetGlyph(long ch, long glyph, FontEngineInfo* info)
{
	HostFaceEntry* entry = (HostFaceEntry*) info->fHostFace;
	HostGlyphSlot* slot = GlyphSlot(entry, (UniChar) (ch != 0 ? ch : glyph));
	info->fGlyphAdvance = slot->fAdvance + ToFixed(info->fWidthAdjust);
	info->fGlyphHeight = slot->fHeight;
	info->fGlyphWidth = slot->fWidth;
	info->fGlyphBearingX = slot->fBearingX;
	info->fGlyphBearingY = slot->fBearingY + info->fBaselineShift;
	info->fGlyphBits = slot->fBits;
	info->fGlyphRowBytes = slot->fRowBytes;
}


static void
HostCloseFont(FontEngineInfo* info)
{
	HostFaceEntry* entry = (HostFaceEntry*) info->fHostFace;
	info->fHostFace = nil;
	if (entry == nil)
		return;
	entry->fUsers--;
	bool cached = false;
	for (long i = 0; i < kFaceEntries; i++)
		if (gFaces[i] == entry)
			cached = true;
	if (!cached && entry->fUsers <= 0)
	{
		EmptyEntry(entry);
		free(entry);
	}
}


/*------------------------------------------------------------------------------
	O p e n i n g
------------------------------------------------------------------------------*/

void
SetHostFontProvider(THostFontProvider* provider)
{
	FlushHostFontCache();
	gHostFontProvider = provider;
}


THostFontProvider*
HostFontProvider(void)
{
	return gHostFontProvider;
}


Boolean
IsHostFontFamily(RefArg family)
{
	return IsFrame(family) && IsString(RefVar(GetFrameSlotRef(family, Sym("hostFont"))));
}


// The size in pixels a 16.16 size comes to (at least one; no more than
// the packed font spec's ten bits hold).
static long
Pixels(Fixed size)
{
	long pixels = RoundFixed(size);
	if (pixels < 1)
		pixels = 1;
	if (pixels > 1023)
		pixels = 1023;
	return pixels;
}


// As SFNTOpenFont: the face the host has nearest the one wanted, as
// ChooseStrike picks the data - bold italic, else italic (for bold
// italic too), else bold, else plain - and failing that whichever it
// has; the face is opened at the size times the scale (a superscript
// or subscript at four fifths of it, raised or lowered by three eighths
// of the ascent), or at the size and scaled when the two scales differ;
// the style table's adjustments for the faces the host's face lacks.
long
HostOpenFont(PixelMap* /*pm*/, StyleRecord* style, RefArg family, Fixed xScale, Fixed yScale, FontEngineInfo* info)
{
	if (gHostFontProvider == nil)
		return kNoFont;
	RefVar name(GetFrameSlotRef(family, Sym("hostFont")));
	if (!IsString(name))
		return kNoFont;
	UniChar familyName[kFamilyNameSize];
	{
		const UniChar* s = GetCString(name);
		long n = Ustrlen(s);
		if (n > kFamilyNameSize - 1)
			n = kFamilyNameSize - 1;
		memcpy(familyName, s, n * sizeof(UniChar));
		familyName[n] = 0;
	}
	long faces = gHostFontProvider->Faces(familyName);
	if (faces == 0)
		return kNoFont;							// (not on this host: the system font)

	long face = style->fFontFace;
	long which = face & 3;
	long used = -1;
	if (which == 3)
	{
		if (faces & (1 << 3))
			used = 3;
		else
			which = 2;
	}
	if (used < 0 && which == 2 && (faces & (1 << 2)))
		used = 2;
	else if (used < 0 && which == 1 && (faces & (1 << 1)))
		used = 1;
	if (used < 0)
	{
		used = 0;
		while (used < 3 && !(faces & (1 << used)))
			used++;
	}
	face = face & ~used & 0x18f;				// (FindSFNT: what is left to synthesise)

	Boolean superscript = (face & kSuperscriptFace) != 0;
	Boolean subscript = (face & kSubscriptFace) != 0;
	Fixed styleSize = style->fFontSize;
	if (superscript || subscript)
		styleSize = FixedMultiply(styleSize, 0xcccd);
	long pixelsX, pixelsY;
	long result;
	if (xScale == yScale)
	{
		pixelsX = pixelsY = Pixels(FixedMultiply(styleSize, xScale));
		info->fScaleX = ToFixed(1);
		info->fScaleY = ToFixed(1);
		result = kOpenedAsIs;
	}
	else
	{
		pixelsX = pixelsY = Pixels(styleSize);
		info->fScaleX = xScale;
		info->fScaleY = yScale;
		result = kOpenedScaled;
	}
	HostFaceEntry* entry = FindFace(familyName, used, pixelsX, pixelsY);
	if (entry == nil)
		return kNoFont;
	entry->fUsers++;
	info->fHostFace = entry;
	info->fScaling = result;
	const HostFaceMetrics& m = entry->fMetrics;
	info->fAscent = m.ascent;
	info->fDescent = m.descent;
	info->fLeading = m.leading;
	info->fWidMax = m.widMax;
	info->fReserved10 = 0;
	info->fReserved14 = 0;
	info->fMinOriginSB = entry->fMinOriginSB;
	info->fMinAdvanceSB = entry->fMinAdvanceSB;
	info->fMaxBeforeBL = entry->fMaxBeforeBL;
	info->fMinAfterBL = entry->fMinAfterBL;
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
	info->fMap = HostMap;
	info->fGetGlyphInfo = HostGetGlyphInfo;
	info->fGetGlyph = HostGetGlyph;
	info->fReopen = nil;						// (never in the ROM's cache)
	info->fClose = HostCloseFont;
	return result;
}


/*------------------------------------------------------------------------------
	T h e   f a m i l i e s   i n   v a r s . f o n t s
------------------------------------------------------------------------------*/

// the symbols of the families added (an array kept in a RefStruct)
static RefStruct*	gAdded = nil;


// A host family's symbol: its name (letters, digits, spaces and a few
// others, as a symbol may have them) and ".host", so that no ROM or
// package family is taken for it.  nil: a name a symbol cannot carry.
static Ref
FamilySymbol(const UniChar* name)
{
	char text[kFamilyNameSize + 8];
	long n = 0;
	for (const UniChar* s = name; *s != 0; s++)
	{
		if (*s < 0x20 || *s > 0x7e || *s == '|' || *s == '\\' || n >= kFamilyNameSize - 1)
			return NILREF;
		text[n++] = (char) *s;
	}
	if (n == 0)
		return NILREF;
	strcpy(text + n, ".host");
	return Intern(text);
}


Ref
MakeHostFontFamily(const UniChar* name)
{
	RefVar sym(FamilySymbol(name));
	if (ISNIL(sym))
		return NILREF;
	RefVar family(AllocateFrame());
	SetFrameSlot(family, RSSYMname, RefVar(MakeString(name)));
	SetFrameSlot(family, RSSYMencoding, RefVar(MAKEINT(0)));
	SetFrameSlot(family, RSSYMmacfontid, RefVar(MAKEINT(0)));
	SetFrameSlot(family, RSSYMscreensym, sym);
	SetFrameSlot(family, Sym("hostFont"), RefVar(MakeString(name)));
	static const long kSizes[] = { 9, 10, 12, 14, 18, 24 };
	RefVar sizes(MakeArray(sizeof(kSizes) / sizeof(kSizes[0])));
	for (ArrayIndex i = 0; i < sizeof(kSizes) / sizeof(kSizes[0]); i++)
		SetArraySlot(sizes, i, RefVar(MAKEINT(kSizes[i])));
	SetFrameSlot(family, Sym("userSizes"), sizes);
	SetFrameSlot(family, Sym("usable"), RefVar(TRUEREF));
	return family;
}


// One family added (if the host has it and it is not there already).
static bool
AddFamily(RefArg fonts, const UniChar* name)
{
	if (!gHostFontProvider->HasFamily(name) || gHostFontProvider->Faces(name) == 0)
		return false;
	RefVar family(MakeHostFontFamily(name));
	if (ISNIL(family))
		return false;
	RefVar sym(GetFrameSlotRef(family, RSSYMscreensym));
	if (NOTNIL(GetFrameSlotRef(fonts, sym)))
		return false;
	SetFrameSlot(fonts, sym, family);
	AddArraySlot(RefVar(*gAdded), sym);
	return true;
}


// a UTF-8 name (no more than kFamilyNameSize - 1 characters) as UniChars
static void
FromUTF8(const char* s, long length, UniChar* out)
{
	long n = 0;
	for (long i = 0; i < length && n < kFamilyNameSize - 1; )
	{
		unsigned char c = (unsigned char) s[i];
		ULong u;
		long extra;
		if (c < 0x80)		{ u = c; extra = 0; }
		else if (c < 0xe0)	{ u = c & 0x1f; extra = 1; }
		else if (c < 0xf0)	{ u = c & 0x0f; extra = 2; }
		else				{ u = c & 0x07; extra = 3; }
		i++;
		for (long k = 0; k < extra && i < length; k++, i++)
			u = (u << 6) | ((unsigned char) s[i] & 0x3f);
		out[n++] = (UniChar) (u > 0xffff ? 0xfffd : u);
	}
	out[n] = 0;
}


long
AddHostFontFamilies(const char* names)
{
	if (gHostFontProvider == nil)
		return 0;
	RefVar fonts(GetFrameSlotRef(RefVar(gVarFrame), RSSYMfonts));
	if (!IsFrame(fonts))
		return 0;
	if (gAdded == nil)
		gAdded = new RefStruct;
	if (!IsArray(RefVar(*gAdded)))
		*gAdded = MakeArray(0);
	long added = 0;
	UniChar name[kFamilyNameSize];
	if (names != nil && strcmp(names, "*") == 0)
	{
		long count = gHostFontProvider->CountFamilies();
		for (long i = 0; i < count; i++)
			if (gHostFontProvider->GetFamilyName(i, name, kFamilyNameSize) && AddFamily(fonts, name))
				added++;
	}
	else if (names != nil)
	{
		for (const char* s = names; *s != 0; )
		{
			const char* end = strchr(s, ',');
			long length = end != nil ? (long) (end - s) : (long) strlen(s);
			while (length > 0 && *s == ' ')
				s++, length--;
			while (length > 0 && s[length - 1] == ' ')
				length--;
			FromUTF8(s, length, name);
			if (name[0] != 0 && AddFamily(fonts, name))
				added++;
			if (end == nil)
				break;
			s = end + 1;
		}
	}
	else
	{
		const char* const* defaults = gHostFontProvider->DefaultFamilies();
		for (long i = 0; defaults != nil && defaults[i] != nil; i++)
		{
			FromUTF8(defaults[i], (long) strlen(defaults[i]), name);
			if (AddFamily(fonts, name))
				added++;
		}
	}
	return added;
}


void
RemoveHostFontFamilies(void)
{
	if (gAdded == nil || !IsArray(RefVar(*gAdded)))
		return;
	RefVar fonts(GetFrameSlotRef(RefVar(gVarFrame), RSSYMfonts));
	RefVar list(*gAdded);
	if (IsFrame(fonts))
		for (ArrayIndex i = 0, count = Length(list); i < count; i++)
			RemoveSlot(fonts, RefVar(GetArraySlotRef(list, i)));
	*gAdded = NILREF;
	FlushFontCache();
	FlushHostFontCache();
}


Boolean
HostFontFamiliesAdded(void)
{
	return gAdded != nil && IsArray(RefVar(*gAdded)) && Length(RefVar(*gAdded)) > 0;
}


/*------------------------------------------------------------------------------
	N e w t o n S c r i p t
------------------------------------------------------------------------------*/

// HostFontFamilies(): the names of the host's families (those a family
// symbol can name), nil when it draws none
static Ref
FHostFontFamilies(RefArg /*rcvr*/)
{
	if (gHostFontProvider == nil)
		return NILREF;
	long count = gHostFontProvider->CountFamilies();
	RefVar list(MakeArray(0));
	UniChar name[kFamilyNameSize];
	for (long i = 0; i < count; i++)
		if (gHostFontProvider->GetFamilyName(i, name, kFamilyNameSize) && NOTNIL(FamilySymbol(name)))
			AddArraySlot(list, RefVar(MakeString(name)));
	return list;
}


// HostFontsAdded(): the symbols of the host's families in vars.fonts
static Ref
FHostFontsAdded(RefArg /*rcvr*/)
{
	if (gAdded == nil || !IsArray(RefVar(*gAdded)))
		return MakeArray(0);
	return Clone(RefVar(*gAdded));
}


void
RegisterHostFontNatives(void)
{
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Sym("HostFontFamilies")), RefVar(MakeCFunction((void*) FHostFontFamilies, 0, nil)));
	SetFrameSlot(functions, RefVar(Sym("HostFontsAdded")), RefVar(MakeCFunction((void*) FHostFontsAdded, 0, nil)));
}
