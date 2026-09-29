/*
	File:		text/TXNewtTextRun.cpp

	Contains:	The text run: characters in a font (TXNewtTextRun.h).

	Reconstructed from the MP2x00 US ROM (0x0023f648-0x0024054c); each
	function cites its origin.
*/

#include "TXNewtTextRun.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "FixedMath.h"
#include "Fonts.h"
#include "Text.h"
#include "TextObject.h"
#include "Ports.h"
#include "Locale.h"

#include <string.h>


// ROM 0x0c104ddc: the locale's 'lineBreakTable, looked up by the first run
// made and kept from then on.  (Host: held where the collector sees it,
// and left nil on a host with no locale, as Text.cpp's TextBox does -
// FindWordBreaks then does without.)
static RefStruct*	gTXLineBreakTable = nil;


// ROM 0x00240158 __ct__20TXNewtFontFamilyInfoFRC6RefVar
TXNewtFontFamilyInfo::TXNewtFontFamilyInfo(RefArg family)
{
	fFamily = family;
}


// ROM 0x002401c0 __dt__20TXNewtFontFamilyInfoFv
TXNewtFontFamilyInfo::~TXNewtFontFamilyInfo()
{ }


// ROM 0x0023f648 __ct__13TXNewtTextRunFv
// A run in the user's font.
TXNewtTextRun::TXNewtTextRun()
	: fSize(0), fFace(0), fDescent(0), fLeading(0)
{
	if (gTXLineBreakTable == nil)
	{
		gTXLineBreakTable = new RefStruct;
		if (NOTNIL(IntlResources()))
			*gTXLineBreakTable = GetLocaleSlot(RSSYMlinebreaktable);
	}
	RefVar font(GetPreference(RSSYMuserfont));
	SetNSObject(font);
	fAscent = -1;
}


// ROM 0x0023f6fc CreateNew__13TXNewtTextRunCFv
TXAttrObject*
TXNewtTextRun::CreateNew(void) const
{
	return new TXNewtTextRun;
}


// ROM 0x0023fe50 GetClassId__13TXNewtTextRunCFv
long
TXNewtTextRun::GetClassId(void) const
{
	return kTXTextRunClassId;
}


// ROM 0x0024037c IsTextRun__13TXNewtTextRunCFv
Boolean
TXNewtTextRun::IsTextRun(void) const
{
	return true;
}


// ROM 0x00240384 Assign__13TXNewtTextRunFPC12TXAttrObject
// The font and the height worked out for it.
void
TXNewtTextRun::Assign(const TXAttrObject* other)
{
	TXRun::Assign(other);
	const TXNewtTextRun* run = (const TXNewtTextRun*) other;
	fFamily = run->fFamily;
	fSize = run->fSize;
	fFace = run->fFace;
	fAscent = run->fAscent;
	fDescent = run->fDescent;
	fLeading = run->fLeading;
}


// ROM 0x0023f850 IsEqual__13TXNewtTextRunCFPC12TXAttrObject
Boolean
TXNewtTextRun::IsEqual(const TXAttrObject* other) const
{
	if (!TXAttrObject::IsEqual(other))
		return false;
	if (other == this)
		return true;
	const TXNewtTextRun* run = (const TXNewtTextRun*) other;
	return EQRef(fFamily, run->fFamily) && fSize == run->fSize && fFace == run->fFace;
}


// ROM 0x002403d8 GetAttributeValue__13TXNewtTextRunCFUlPv
// A 'font' value is a TXNewtFontFamilyInfo the caller made: the family
// goes into it.
Boolean
TXNewtTextRun::GetAttributeValue(TXAttrTag tag, void* value) const
{
	if (tag == kTXAttrFace)
		*(long*) value = fFace;
	else if (tag == kTXAttrFont)
	{
		(*(TXNewtFontFamilyInfo**) value)->fFamily = fFamily;
		return true;
	}
	else if (tag == kTXAttrSize)
		*(long*) value = fSize;
	else
		return false;
	return true;
}


// ROM 0x002404d8 SetAttributeValue__13TXNewtTextRunFUlPCv
// (The height is thrown away whatever the tag.)
void
TXNewtTextRun::SetAttributeValue(TXAttrTag tag, const void* value)
{
	fAscent = -1;
	if (tag == kTXAttrFace)
		fFace = *(const long*) value;
	else if (tag == kTXAttrFont)
		fFamily = (*(TXNewtFontFamilyInfo* const*) value)->fFamily;
	else if (tag == kTXAttrSize)
		fSize = *(const long*) value;
}


// ROM 0x00240434 GetAttributesValues__13TXNewtTextRunFP12TXAttrValues
void
TXNewtTextRun::GetAttributesValues(TXAttrValues* values)
{
	TXNewtFontFamilyInfo* family = new TXNewtFontFamilyInfo(fFamily);
	values->Add(kTXAttrFont, &family, sizeof(family), true);
	values->Add(kTXAttrSize, &fSize, sizeof(fSize), false);
	values->Add(kTXAttrFace, &fFace, sizeof(fFace), false);
	TXAttrObject::GetAttributesValues(values);
}


// ROM 0x0023f7b0 GetCommonAttrValue__13TXNewtTextRunCFUlPv
// Whether this run agrees with the value.  The faces agree about the bits
// they share, which the value is narrowed to; two plain faces agree.
Boolean
TXNewtTextRun::GetCommonAttrValue(TXAttrTag tag, void* value) const
{
	if (tag == kTXAttrFace)
	{
		ULong* faces = (ULong*) value;
		if ((ULong) fFace == 0 && *faces == 0)
			return true;
		ULong common = (ULong) fFace & *faces;
		if (common != 0)
			*faces = common;
		return common != 0;
	}
	if (tag == kTXAttrFont)
		return EQRef(fFamily, (*(TXNewtFontFamilyInfo**) value)->fFamily) != 0;
	if (tag == kTXAttrSize)
		return fSize == *(long*) value;
	return false;
}


// ROM 0x0023f8c4 GetAttributeFlags__13TXNewtTextRunCFUl
unsigned long
TXNewtTextRun::GetAttributeFlags(TXAttrTag tag) const
{
	unsigned long flags = 0;
	if (tag == kTXAttrFont || tag == kTXAttrSize || tag == kTXAttrFace)
		flags = 3;
	return TXAttrObject::GetAttributeFlags(tag) | flags;
}


// ROM 0x0023f704 UpdateAttribute__13TXNewtTextRunFUlPCvl
void
TXNewtTextRun::UpdateAttribute(TXAttrTag tag, const void* value, long how)
{
	fAscent = -1;
	if (tag == kTXAttrFace && (how & 0x0c) != 0)
	{
		long faces = fFace;
		if (how & 4)
			AddFace(*(const long*) value, &faces);
		else
			RemoveFace(*(const long*) value, &faces);
		fFace = faces;
		return;
	}
	TXAttrObject::UpdateAttribute(tag, value, how);
}


// ROM 0x0024053c AddFace__13TXNewtTextRunFlPl
void
TXNewtTextRun::AddFace(long face, long* faces)
{
	*faces |= face;
}


// ROM 0x0024054c RemoveFace__13TXNewtTextRunCFlPl
void
TXNewtTextRun::RemoveFace(long face, long* faces) const
{
	*faces &= ~face;
}


// ROM 0x00240104 GetNSObject__13TXNewtTextRunCFv
Ref
TXNewtTextRun::GetNSObject(void) const
{
	return MakeCompactFont(fFamily, fSize, fFace);
}


// ROM 0x00240114 SetNSObject__13TXNewtTextRunFRC6RefVar
// The three numbers out of a font spec.  (The height is not thrown away.)
void
TXNewtTextRun::SetNSObject(RefArg obj)
{
	fFamily = GetFontFamilySym(obj);
	fSize = GetFontSize(obj);
	fFace = GetFontFace(obj);
}


// ROM 0x0023f908 GetNewtStyleRecord__13TXNewtTextRunFP11StyleRecord
// The family a ROM font's number stands for, or the symbol looked up in
// vars.fonts.  The pattern word is left an integer nought (not nil), as
// the ROM leaves it.
void
TXNewtTextRun::GetNewtStyleRecord(StyleRecord* style)
{
	Ref family = fFamily;
	if (ISINT(family))
		style->fFontFamily = GetArraySlotRef(RefVar(GetROMFontList()), RINT(family));
	else
	{
		RefVar fonts(GetFrameSlotRef(RefVar(gVarFrame), RSSYMfonts));
		style->fFontFamily = GetProtoVariable(fonts, fFamily, nil);
	}
	style->fFontSize = ToFixed(fSize);
	style->fFontFace = fFace;
	style->fFontPattern = 0;
	style->fTransferMode = 0;
	style->fReserved14 = 0;
	style->fReserved18 = 0;
}


// ROM 0x0023f9f4 GetHeightInfo__13TXNewtTextRunFPiN21
void
TXNewtTextRun::GetHeightInfo(int* ascent, int* descent, int* leading)
{
	if (fAscent == -1)
	{
		StyleRecord style;
		GetNewtStyleRecord(&style);
		FontInfo info;
		GetStyleFontInfo(&style, &info);
		fAscent = info.ascent;
		fDescent = info.descent;
		fLeading = info.leading;
		if (style.fPattern != nil)
			DisposePattern(style.fPattern);
	}
	*ascent = fAscent;
	*descent = fDescent;
	*leading = fLeading;
}


// The options a hit-test lays the run out with: none, unless the line is
// fully justified, when the run is stretched over its width and the extra.
static TextOptions*
JustifiedOptions(const TXLineRunDisplayInfo& info, TextOptions* options)
{
	if (info.fJustifyExtra == 0)
		return nil;
	memset(options, 0, sizeof(TextOptions));
	options->fJustification = 0x10000;
	options->fWidth = info.fWidth + info.fJustifyExtra;
	return options;
}


// ROM 0x0023faa4 Draw__13TXNewtTextRunFRC20TXLineRunDisplayInfolRC4Recti
// Drawn in `or` mode from x, on the baseline `baseline` pixels below the
// line's top.  The options' +0x14 word is 9, which the ROM's DoTextOnce
// takes to mark the text object (its flag 0x40000) and keep the options.
void
TXNewtTextRun::Draw(const TXLineRunDisplayInfo& info, Fixed x, const Rect& line, int baseline)
{
	StyleRecord style;
	GetNewtStyleRecord(&style);
	TextOptions options;
	options.fAlignment = 0;
	options.fReserved = 0;
	options.fTransferMode = 1;			// srcOr
	options.fFittedWidth = 9;
	options.fReserved2 = 0;
	if (info.fJustifyExtra == 0)
	{
		options.fJustification = 0;
		options.fWidth = info.fWidth;
	}
	else
	{
		options.fJustification = 0x10000;
		options.fWidth = info.fWidth + info.fJustifyExtra;
	}
	FPoint where;
	where.x = x;
	where.y = ToFixed(line.top + baseline);
	StyleRecord* styles[1] = { &style };
	DrawTextOnce(info.fText, info.fLength, styles, nil, where, &options, nil);
	if (style.fPattern != nil)
		DisposePattern(style.fPattern);
}


// ROM 0x0023fb94 MeasureWidth__13TXNewtTextRunFRC20TXLineRunDisplayInfo
// The run's advance.  The options' +0x14 word is 10, which the ROM's
// DoTextOnce takes to mean no options at all - the rest of them are left
// as the stack had them.  (Host: nought, which measures the same.)
Fixed
TXNewtTextRun::MeasureWidth(const TXLineRunDisplayInfo& info)
{
	StyleRecord style;
	GetNewtStyleRecord(&style);
	TextOptions options;
	memset(&options, 0, sizeof(options));
	options.fFittedWidth = 10;
	TextBoundsInfo bounds;
	FPoint origin = { 0, 0 };
	StyleRecord* styles[1] = { &style };
	MeasureTextOnce(info.fText, info.fLength, styles, nil, origin, &options, &bounds);
	if (style.fPattern != nil)
		DisposePattern(style.fPattern);
	return bounds.fWidth;
}


// ROM 0x0023fc34 PixelToChar__13TXNewtTextRunFRC20TXLineRunDisplayInfolP13TXOffsetRange
// The character boundary nearest `pixel` (from the run's start), as an
// empty range; a boundary after the first character is at the end of the
// character before it.
void
TXNewtTextRun::PixelToChar(const TXLineRunDisplayInfo& info, Fixed pixel, TXOffsetRange* range)
{
	StyleRecord style;
	GetNewtStyleRecord(&style);
	TextOptions justified;
	TextOptions* options = JustifiedOptions(info, &justified);
	StyleRecord* styles[1] = { &style };
	FPoint origin = { 0, 0 };
	TextObjectRef text = NewText(info.fText, info.fLength, styles, nil, origin, options);
	FPoint point = { pixel, 0 };
	long offset = PointToChar(text, point);
	DisposeText(text);
	range->Set(offset, offset, offset != 0, offset != 0);
	if (style.fPattern != nil)
		DisposePattern(style.fPattern);
}


// ROM 0x0023fd58 CharToPixel__13TXNewtTextRunFRC20TXLineRunDisplayInfol
Fixed
TXNewtTextRun::CharToPixel(const TXLineRunDisplayInfo& info, long offset)
{
	StyleRecord style;
	GetNewtStyleRecord(&style);
	TextOptions justified;
	TextOptions* options = JustifiedOptions(info, &justified);
	StyleRecord* styles[1] = { &style };
	FPoint origin = { 0, 0 };
	TextObjectRef text = NewText(info.fText, info.fLength, styles, nil, origin, options);
	FPoint point;
	CharToPoint(text, offset, &point);
	DisposeText(text);
	if (style.fPattern != nil)
		DisposePattern(style.fPattern);
	return point.x;
}


// ROM 0x0023fe5c FullJustifPortion__13TXNewtTextRunFRC20TXLineRunDisplayInfo
// How much a fully justified line may stretch this run: a thirty-second of
// the point size for every space in it.
Fixed
TXNewtTextRun::FullJustifPortion(const TXLineRunDisplayInfo& info)
{
	ULong spaces = 0;
	for (long i = 0; i < info.fLength; i++)
		if (info.fText[i] == 0x20)
			spaces += 0x1000;
	Fixed portion = (Fixed) (spaces << 4) >> 5;
	return FixedMultiply((Fixed) ((ULong) fSize << 16), portion);
}


// ROM 0x0023feb0 VisibleLen__13TXNewtTextRunFPCUsl
// The count less the spaces, line feeds and returns at the end.
long
TXNewtTextRun::VisibleLen(const UniChar* text, long count)
{
	if (count == 0)
		return 0;
	const UniChar* p = text + count;
	do
	{
		UniChar c = *--p;
		if (c != 0x20 && c != 0x0a && c != 0x0d)
			return count;
		count--;
	} while (count > 0);
	return count;
}


// ROM 0x0023fef4 LineBreak__13TXNewtTextRunFPCUslT2PlUcT4
// How much of text[start, count) goes on a line with `*width` left.  All
// of it: the room it took comes off `*width` and ==> 2.  Otherwise the
// line is cut at the word the fitted length ends in (FindWordBreaks over
// the locale's break table) and `*width` becomes nought: after that word
// when it is spaces, else in front of it (==> 0) - unless the word starts
// the text and the line may cut a word, when the fitted length stands (at
// least one character, when the run starts the text) and ==> 1.
//
// ROM QUIRK, kept: a word starting the text on a line that may not cut
// one answers a length of -start, not nought - which is only nought when
// the run starts the text.
long
TXNewtTextRun::LineBreak(const UniChar* text, long count, long start, Fixed* width, Boolean mayCutWord, long* length)
{
	StyleRecord style;
	GetNewtStyleRecord(&style);
	TextOptions options;
	options.fJustification = 0;
	options.fAlignment = 0;
	options.fWidth = *width;
	options.fReserved = 0;
	options.fTransferMode = 0;
	options.fFittedWidth = 0;
	options.fReserved2 = 0;
	StyleRecord* styles[1] = { &style };
	FPoint origin = { 0, 0 };
	long rest = count - start;
	TextObjectRef obj = NewText(text + start, rest, styles, nil, origin, &options);
	GetTextObjField(obj, kTextObjFittedLength, length);
	DisposeText(obj);
	long result;
	if (*length == rest)
	{
		*width -= options.fFittedWidth;
		result = 2;
	}
	else
	{
		*width = 0;
		ULong wordStart, wordEnd;
		RefVar breakTable(gTXLineBreakTable != nil ? (Ref) *gTXLineBreakTable : NILREF);
		FindWordBreaks(text, count, *length + start, true, breakTable, &wordStart, &wordEnd);
		if (text[wordStart] == 0x20)
		{
			*length = wordEnd - start;
			result = 0;
		}
		else if (wordStart == 0 && mayCutWord)
		{
			if (start == 0 && *length == 0)
				*length = 1;
			result = 1;
		}
		else
		{
			*length = wordStart - start;
			result = 0;
		}
	}
	if (style.fPattern != nil)
		DisposePattern(style.fPattern);
	return result;
}


// ROM 0x00240208 TXGetRunAttrValues__FRC6RefVar
TXAttrValues*
TXGetRunAttrValues(RefArg fontSpec)
{
	TXAttrValues* values = new TXAttrValues;
	if (values == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);	// (-10007)
	RefVar slot(GetFontFamilySym(fontSpec));
	if (NOTNIL(slot))
	{
		TXNewtFontFamilyInfo* family = new TXNewtFontFamilyInfo(slot);
		values->Add(kTXAttrFont, &family, sizeof(family), true);
	}
	slot = GetFrameSlotRef(fontSpec, RSSYMsize);
	if (ISINT(slot))
	{
		long size = RINT(slot);
		values->Add(kTXAttrSize, &size, sizeof(size), false);
	}
	slot = GetFrameSlotRef(fontSpec, RSSYMface);
	if (ISINT(slot))
	{
		long face = RINT(slot);
		values->Add(kTXAttrFace, &face, sizeof(face), false);
	}
	return values;
}
