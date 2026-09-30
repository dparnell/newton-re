/*
	File:		ink/Ink.cpp

	Contains:	Ink: what it is, and what an ink word says about itself.
				See Ink.h.
*/

#include "Ink.h"
#include "objects.h"
#include "RSSymbols.h"
#include "RichString.h"		// IsInkWord, which the rich string needs too
#include "FixedMath.h"
#include "Ports.h"			// RoundFixed


/*------------------------------------------------------------------------------
	W h a t   i s   i n k
------------------------------------------------------------------------------*/

// ROM 0x000dbf78 IsInk__FRC6RefVar
// Any of the three: raw ink of either kind, or an ink word.
Boolean
IsInk(RefArg obj)
{
	if (!IsBinary(obj))
		return false;
	RefVar cls(ClassOf(obj));
	return EQRef(cls, RSSYMink) || EQRef(cls, RSSYMinkword) || EQRef(cls, RSSYMink2);
}


// ROM 0x000dc02c IsRawInk__FRC6RefVar
// Ink that is not a word: a sketch, or writing nobody has claimed.
Boolean
IsRawInk(RefArg obj)
{
	if (!IsBinary(obj))
		return false;
	RefVar cls(ClassOf(obj));
	return EQRef(cls, RSSYMink) || EQRef(cls, RSSYMink2);
}


// ROM 0x000dc0c0 IsOldRawInk__FRC6RefVar
// The older of the two raw kinds.
Boolean
IsOldRawInk(RefArg obj)
{
	if (!IsBinary(obj))
		return false;
	return EQRef(RefVar(ClassOf(obj)), RSSYMink) != 0;
}


/*------------------------------------------------------------------------------
	W h a t   a n   i n k   w o r d   s a y s
------------------------------------------------------------------------------*/

// ROM 0x0013ffb8 GetQDFace__FUl
// The six bits an ink word keeps a face in, opened out into
// QuickDraw's: the four ordinary bits stay where they are and the two
// script bits (superscript and subscript) go back up to 0x80 and 0x100.
ULong
GetQDFace(ULong raw)
{
	return (raw & 0xf) | ((raw & 0x30) << 3);
}


// ROM 0x0013ffc8 GetRawFace__FUl
// And back again.
ULong
GetRawFace(ULong face)
{
	return (face & 0xf) | ((face & 0x180) >> 3);
}


// ROM 0x0014003c GetInkWordFontSize__FUl
// The font size an x-height comes to: seven quarters of it.
long
GetInkWordFontSize(ULong xHeight)
{
	return RoundFixed(FixedMultiply(ToFixed((long) xHeight), 0x1c000));		// x 1.75
}


// ROM 0x00140068 GetStdInkWordPenWidth__FUl
// The pen a font size wants: one pixel up to ten, and then two more
// than forty divided by the size - so it thins out as the writing
// grows, and never goes below two.
long
GetStdInkWordPenWidth(ULong fontSize)
{
	if (fontSize <= 10)
		return 1;
	return (long) (40 / fontSize) + 2;
}


// ROM 0x0013ffd8 PackInkWordInfo__FP17PackedInkWordInfoUlN32lN22
// The seven measurements squeezed into two words (see Ink.h).
void
PackInkWordInfo(PackedInkWordInfo* packed, ULong width, ULong ascent, ULong descent,
				ULong xHeight, Fixed scale, ULong face, ULong penSize)
{
	packed->SetWord0((width << 22) | (ascent << 12) | (descent << 2) | (penSize - 1));
	packed->SetWord1((xHeight << 22) | ((((ULong) scale) & 0xffff00) >> 2) | GetRawFace(face));
}


// ROM 0x001400ac ExpandPackedInkWordInfo__FP17PackedInkWordInfoP11InkWordInfo
// The two words opened out, and everything that follows from them: the
// font size the x-height comes to, the pen width that size wants, and
// the measurements at the word's own scale - the width, the height and
// the ascent each with the pen added, because the ink is drawn with a
// pen of that width and spills half of it either side.
void
ExpandPackedInkWordInfo(const PackedInkWordInfo* packed, InkWordInfo* info)
{
	ULong word0 = packed->Word0();
	ULong word1 = packed->Word1();
	info->fWidth = word0 >> 22;
	info->fAscent = (word0 >> 12) & 0x3ff;
	info->fDescent = (word0 >> 2) & 0x3ff;
	info->fPenSize = (word0 & 3) + 1;
	info->fXHeight = word1 >> 22;
	info->fScale = (Fixed) ((word1 & 0x3fffc0) << 2);
	info->fFace = GetQDFace(word1 & 0x3f);

	info->fFontSize = GetInkWordFontSize(info->fXHeight);
	info->fScaledFontSize = RoundFixed(FixedMultiply(ToFixed(info->fFontSize), info->fScale));
	info->fPenWidth = GetStdInkWordPenWidth((ULong) info->fScaledFontSize);
	info->fScaledWidth = info->fPenWidth + RoundFixed(FixedMultiply(ToFixed((long) info->fWidth), info->fScale));
	info->fScaledHeight = info->fPenWidth
						+ RoundFixed(FixedMultiply(ToFixed((long) (info->fAscent + info->fDescent)), info->fScale));
	info->fScaledAscent = info->fPenWidth + RoundFixed(FixedMultiply(ToFixed((long) info->fAscent), info->fScale));
	info->fScaledXHeight = RoundFixed(FixedMultiply(ToFixed((long) info->fXHeight), info->fScale));
	info->fScaledDescent = RoundFixed(FixedMultiply(ToFixed((long) info->fDescent), info->fScale));
}


// ROM 0x0014022c GetPackedInkWordInfo__FRC6RefVarP17PackedInkWordInfo
// The last eight bytes of the binary.
void
GetPackedInkWordInfo(RefArg ink, PackedInkWordInfo* packed)
{
	const char* data = (const char*) BinaryData(ink);
	BlockMove(data + Length(ink) - (long) sizeof(PackedInkWordInfo), packed, sizeof(PackedInkWordInfo));
}


// ROM 0x0014028c SetPackedInkWordInfo__FRC6RefVarP17PackedInkWordInfo
void
SetPackedInkWordInfo(RefArg ink, const PackedInkWordInfo* packed)
{
	char* data = (char*) BinaryData(ink);
	BlockMove(packed, data + Length(ink) - (long) sizeof(PackedInkWordInfo), sizeof(PackedInkWordInfo));
}


// ROM 0x001402ec GetInkWordInfo__FRC6RefVarP11InkWordInfo
void
GetInkWordInfo(RefArg ink, InkWordInfo* info)
{
	PackedInkWordInfo packed;
	GetPackedInkWordInfo(ink, &packed);
	ExpandPackedInkWordInfo(&packed, info);
}


/*------------------------------------------------------------------------------
	C h a n g i n g   o n e
------------------------------------------------------------------------------*/

// ROM 0x000dbd0c SetInkWordFontParms__FRC6RefVarT1
// An ink word restyled from a font spec frame - the same frame a
// paragraph's style run is written with, so that changing the style of a
// run of text changes the writing in it too.  What the spec does not
// say, the word keeps.
//
//   scale     a percentage of the word's own size
//   size      a point size, which comes to a scale against the size the
//             word's x-height makes it
//   face      the type face, packed down to the six bits an ink word
//             has room for
//   penSize   how thick the pen is
//
// `scale` wins over `size` when both are there.  ==> the ink word.
Ref
SetInkWordFontParms(RefArg ink, RefArg fontSpec)
{
	InkWordInfo info;
	GetInkWordInfo(ink, &info);

	RefVar slot(GetFrameSlot(fontSpec, RSSYMscale));
	Fixed scale;
	if (NOTNIL(slot))
		scale = FixedDivide(ToFixed(RINT(slot)), ToFixed(100));
	else
	{
		slot = GetFrameSlot(fontSpec, RSSYMsize);
		if (NOTNIL(slot))
			scale = FixedDivide(ToFixed(RINT(slot)), ToFixed(info.fFontSize));
		else
			scale = info.fScale;
	}

	slot = GetFrameSlot(fontSpec, RSSYMface);
	ULong face = NOTNIL(slot) ? GetRawFace((ULong) RINT(slot)) : info.fFace;

	slot = GetFrameSlot(fontSpec, RSSYMpensize);
	ULong pen = NOTNIL(slot) ? (ULong) RINT(slot) : info.fPenSize;

	PackedInkWordInfo packed;
	PackInkWordInfo(&packed, info.fWidth, info.fAscent, info.fDescent,
					info.fXHeight, scale, face, pen);
	SetPackedInkWordInfo(ink, &packed);
	return ink;
}


// ROM 0x000dc180 SetInkWordFontSize__FRC6RefVarUl
// The word drawn at a font size: the scale is the size asked for over
// the size the x-height comes to, so the ink is stretched to fit the
// text around it rather than redrawn.
Ref
SetInkWordFontSize(RefArg ink, ULong size)
{
	InkWordInfo info;
	GetInkWordInfo(ink, &info);
	Fixed scale = FixedDivide(ToFixed((long) size), ToFixed(info.fFontSize));
	PackedInkWordInfo packed;
	PackInkWordInfo(&packed, info.fWidth, info.fAscent, info.fDescent, info.fXHeight,
					scale, info.fFace, info.fPenSize);
	SetPackedInkWordInfo(ink, &packed);
	return ink;
}


// ROM 0x000dc1f4 SetInkWordPenSize__FRC6RefVarUl
Ref
SetInkWordPenSize(RefArg ink, ULong size)
{
	PackedInkWordInfo packed;
	GetPackedInkWordInfo(ink, &packed);
	packed.SetWord0((packed.Word0() & ~(ULong) 3) | ((size - 1) & 3));
	SetPackedInkWordInfo(ink, &packed);
	return ink;
}


// ROM 0x000dc24c SetInkWordScale__FRC6RefVarl
Ref
SetInkWordScale(RefArg ink, long scale)
{
	PackedInkWordInfo packed;
	GetPackedInkWordInfo(ink, &packed);
	packed.SetWord1((packed.Word1() & 0xffc0003f) | ((((ULong) scale) & 0xffff00) >> 2));
	SetPackedInkWordInfo(ink, &packed);
	return ink;
}


// ROM 0x000dbebc SetInkWordFontFace__FRC6RefVarUl
Ref
SetInkWordFontFace(RefArg ink, ULong face)
{
	PackedInkWordInfo packed;
	GetPackedInkWordInfo(ink, &packed);
	packed.SetWord1((packed.Word1() & ~(ULong) 0x3f) | GetRawFace(face));
	SetPackedInkWordInfo(ink, &packed);
	return ink;
}


// ROM 0x00140940 AdjustInkWordXHeight__FRC6RefVarUc
// The x-height the recogniser measured brought back to something a line
// of text can be laid out from, because it is what the font size - and
// so everything else - is worked out from.
//
// For a word of letters (forNumbers false) the x-height is only
// mistrusted when the word is more than four times as tall as it is
// wide: a tall narrow scribble, where the writing has no waist to
// speak of, and two fifths of the ascent is a better guess.
//
// For a view that expects numbers the test is the other way about: a
// word whose x-height is more than three fifths of its ascent and which
// falls less than a fifth of its height below the baseline is one
// where the recogniser has found no ascenders or descenders to measure
// against - digits have none - and its x-height is too big.  Fifty-five
// hundredths of the ascent goes in instead.
void
AdjustInkWordXHeight(RefArg ink, Boolean forNumbers)
{
	PackedInkWordInfo packed;
	GetPackedInkWordInfo(ink, &packed);
	ULong width = packed.Word0() >> 22;
	ULong ascent = (packed.Word0() >> 12) & 0x3ff;
	ULong descent = (packed.Word0() >> 2) & 0x3ff;
	ULong xHeight = packed.Word1() >> 22;
	ULong wanted = xHeight;
	Fixed factor = 0;
	if (!forNumbers)
	{
		if (width != 0
			&& FixedDivide(ToFixed((long) (ascent + descent)), ToFixed((long) width)) > 0x40000)
			factor = 0x6666;			// two fifths
	}
	else if (FixedDivide(ToFixed((long) xHeight), ToFixed((long) ascent)) > 0x999a		// three fifths
			 && FixedDivide(ToFixed((long) descent), ToFixed((long) (ascent + descent))) < 0x3333)	// a fifth
		factor = 0x8ccd;				// fifty-five hundredths
	if (factor != 0)
		wanted = (ULong) RoundFixed(FixedMultiply(factor, ToFixed((long) ascent)));
	if (wanted != xHeight)
	{
		packed.SetWord1((packed.Word1() & 0x3fffff) | (wanted << 22));
		SetPackedInkWordInfo(ink, &packed);
	}
}
