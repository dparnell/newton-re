/*
	File:		ink/Ink.h

	Contains:	Ink: the strokes the pen leaves, kept as they were written.

				Three classes of binary hold it.  'ink and 'ink2 are raw
				ink - a sketch, or a word nobody has claimed - and
				'inkWord is an ink word, the ink of one word standing in
				a paragraph's text for the character 0xf701 (a rich
				string keeps its bytes among the text, see
				frames/RichString.h).  All three carry the same
				compressed stroke data; an ink word carries eight bytes
				of information about the word after it, which is what
				lets it be laid out and scaled among real characters
				without being expanded first.

				Those eight bytes are a PackedInkWordInfo - the word's
				width, the height above and below the baseline, the
				x-height, the pen size, the type face and the scale it
				was last drawn at, squeezed into two words.
				ExpandPackedInkWordInfo opens them out into an
				InkWordInfo, working out the font size the x-height
				comes to, the pen width that size wants, and the four
				measurements scaled by the scale - which are what a line
				of text is laid out from.

	The stroke compression underneath is ink/CICCodec.h, reached
	through the codec seam in ink/InkCodec.h; the glue between it and
	the strokes the pen leaves is ink/InkStrokes.cpp, and ink as a
	shape frame is ink/InkShapes.h.

	NOT YET RECONSTRUCTED: the ink word's glyph (TInkWordGlyph, an ink
	word drawn in a line of text) and the live inker (TLiveInker, the
	ink that follows the pen).

	Reconstructed from the MP2x00 US ROM (0x000dbebc-0x000dc314,
	0x0013ffb8-0x00140310, 0x00140940); each function cites its origin.
*/

#ifndef __INK_H
#define __INK_H

#ifndef __OBJECTS_H
#include "Stroke.h"
#include "objects.h"
#endif
#ifndef __RICHSTRING_H
#include "RichString.h"		// IsInkWord
#endif

// Whether the object is ink, and of which kind.  Ink is a binary of one
// of three classes: 'ink (the old raw ink), 'ink2 (raw ink) and
// 'inkWord (the ink of a single word, which carries its measurements).
Boolean	IsInk(RefArg obj);						// ROM 0x000dbf78 IsInk__FRC6RefVar
Boolean	IsRawInk(RefArg obj);					// ROM 0x000dc02c IsRawInk__FRC6RefVar
Boolean	IsOldRawInk(RefArg obj);				// ROM 0x000dc0c0 IsOldRawInk__FRC6RefVar
// IsInkWord (ROM 0x000dc120) is declared in frames/RichString.h: the
// frames layer needs it too, because an ink word is what a rich
// string keeps among its text.


// The eight bytes at the end of an 'inkWord binary.
//
//	word 0:	31..22	the word's width
//			21..12	how far it rises above the baseline
//			11..2	how far it falls below it
//			 1..0	the pen size, less one (1 to 4)
//	word 1:	31..22	the x-height
//			21..6	the scale, a 16.16 Fixed to eight fractional bits
//			 5..0	the type face, as GetRawFace packs it
struct PackedInkWordInfo
{
	ULong	fWord0;			// +0x00
	ULong	fWord1;			// +0x04
};


// The same, opened out, with what follows from it.  The first seven
// fields are what was packed; the rest are worked out from them.
struct InkWordInfo
{
	ULong	fWidth;				// +0x00
	ULong	fAscent;			// +0x04  above the baseline
	ULong	fDescent;			// +0x08  below it
	ULong	fXHeight;			// +0x0c
	ULong	fFace;				// +0x10  the QuickDraw face
	Fixed	fScale;				// +0x14
	ULong	fPenSize;			// +0x18  1 to 4
	long	fFontSize;			// +0x1c  the font size the x-height comes to
	long	fScaledFontSize;	// +0x20  that size at this scale
	long	fPenWidth;			// +0x24  the pen that size wants
	long	fScaledWidth;		// +0x28  the width at this scale, and the pen
	long	fScaledHeight;		// +0x2c  the height the same way
	long	fScaledAscent;		// +0x30
	long	fScaledXHeight;		// +0x34
	long	fScaledDescent;		// +0x38
};							// 0x3c bytes


// A type face between the form QuickDraw uses (bold 1, italic 2,
// underline 4, outline 8, superscript 0x80, subscript 0x100) and the
// six bits an ink word has room for.
ULong	GetQDFace(ULong raw);					// ROM 0x0013ffb8 GetQDFace__FUl
ULong	GetRawFace(ULong face);					// ROM 0x0013ffc8 GetRawFace__FUl

// The font size an x-height comes to (seven fours of it), and the pen
// width a font size wants.
long	GetInkWordFontSize(ULong xHeight);		// ROM 0x0014003c GetInkWordFontSize__FUl
long	GetStdInkWordPenWidth(ULong fontSize);	// ROM 0x00140068 GetStdInkWordPenWidth__FUl

void	PackInkWordInfo(PackedInkWordInfo* packed, ULong width, ULong ascent, ULong descent,
						ULong xHeight, Fixed scale, ULong face, ULong penSize);	// ROM 0x0013ffd8 PackInkWordInfo__FP17PackedInkWordInfoUlN32lN22
void	ExpandPackedInkWordInfo(const PackedInkWordInfo* packed, InkWordInfo* info);	// ROM 0x001400ac ExpandPackedInkWordInfo__FP17PackedInkWordInfoP11InkWordInfo

// An ink word's eight bytes, read and written where they are: the last
// eight of the binary.
void	GetPackedInkWordInfo(RefArg ink, PackedInkWordInfo* packed);		// ROM 0x0014022c GetPackedInkWordInfo__FRC6RefVarP17PackedInkWordInfo
void	SetPackedInkWordInfo(RefArg ink, const PackedInkWordInfo* packed);	// ROM 0x0014028c SetPackedInkWordInfo__FRC6RefVarP17PackedInkWordInfo
void	GetInkWordInfo(RefArg ink, InkWordInfo* info);					// ROM 0x001402ec GetInkWordInfo__FRC6RefVarP11InkWordInfo

// An ink word's font size, pen size, scale and face changed.  Each
// answers the ink word it was given.
Ref		SetInkWordFontSize(RefArg ink, ULong size);		// ROM 0x000dc180 SetInkWordFontSize__FRC6RefVarUl
Ref		SetInkWordPenSize(RefArg ink, ULong size);		// ROM 0x000dc1f4 SetInkWordPenSize__FRC6RefVarUl
Ref		SetInkWordScale(RefArg ink, long scale);		// ROM 0x000dc24c SetInkWordScale__FRC6RefVarl
Ref		SetInkWordFontFace(RefArg ink, ULong face);		// ROM 0x000dbebc SetInkWordFontFace__FRC6RefVarUl

// The strokes the pen left packed into a binary, and read back out of
// one.  A block of ink is 'ink2, or 'inkWord with the word's
// measurements after it; InkExpand answers a list of strokes ended by a
// nil, made with NewPtr.
class TStroke;
// (host) An ink object's packed strokes and how many bytes of them
// there are - the eight an ink word carries its measurements in are not
// part of them.  ==> nil when the object is not ink at all.
const void*	InkData(RefArg ink, long* outSize);

Ref			InkCompress(TStroke** strokes, Boolean asWord);			// ROM 0x00140b78 InkCompress__FPP7TStrokeUc
TStroke**	InkExpand(RefArg ink, ULong group, long x, long y);		// ROM 0x00140c98 InkExpand__FRC6RefVarUllT3

// The box a list of strokes covers, and the strokes moved.
void		UnionBounds(TStroke** strokes, Rect* rect);				// ROM 0x001a36bc UnionBounds__FPP7TStrokeP5TRect
void		OffsetStrokes(TStroke** strokes, long dx, long dy);		// ROM 0x001a3750 OffsetStrokes__FPP7TStrokelT2
void		InkBounds(TStroke** strokes, Rect* rect);				// ROM 0x001a3728 InkBounds__FPP7TStrokeP5TRect

// A word's strokes brought down to a size a line of text can hold.
void		ScaleStrokesForInkWord(TStroke** strokes, Rect* rect);	// ROM 0x00140318 ScaleStrokesForInkWord__FPP7TStrokeP5TRect

// The strokes made into ink, and where they ended up.  Both move the
// strokes to the origin first, so the ink is kept where it was drawn
// rather than where it is to go; both throw if there is no memory.
Ref			TStrokesToInk(TStroke** strokes, Rect* outRect);			// ROM 0x00140608 TStrokesToInk__FPP7TStrokeP5TRect
Ref			TStrokesToInkWord(TStroke** strokes, Rect* outRect);	// ROM 0x001404f0 TStrokesToInkWord__FPP7TStrokeP5TRect

// Ink drawn into the current port, its top-left at (x, y).  The scale
// is applied to the ink's own coordinates before the offset - a whole
// one draws it the size it was written - and `pen` is the width the
// lines are drawn with.
//
// `useInker` says the ink is wholly inside the clip, which lets the ROM
// draw it with the live inker's own line drawer instead of QuickDraw's
// (NOT YET: it is drawn the slow way either way).
// The two that take the ink itself take it as the ROM's do: a pointer
// to the packed strokes, which is not always a binary object - a word
// handed over by the recogniser is a block outside the object heap.
// (`size` is this reconstruction's: the ROM's decoder reads the length
// out of the block, and the seam here is given it instead.)
void		InkDraw(RefArg ink, ULong pen, long x, long y, Boolean useInker);	// ROM 0x00140cd0 InkDraw__FRC6RefVarUllT3Uc
void		InkDrawScaled(const void* data, long size, ULong pen, Fixed x, Fixed y,
						  Fixed scaleX, Fixed scaleY, Boolean useInker);	// ROM 0x00153884 GenericCSDraw__FP14CSStrokeHeaderUllN33Uc

// Ink drawn stretched from the box it was made in into another one,
// which is how a sketch that has been resized is drawn.  The size it was
// made at is given as a width and a height rather than a box, because
// what it is stretched onto is a 16.16 rectangle.
void		InkDrawInFRect(const void* data, long size, ULong pen, Fixed width, Fixed height,
							const FRect* to, Boolean useInker);		// ROM 0x001544bc CSDrawInRect__FP14CSStrokeHeaderUllT3P5FRectUc
void		InkDrawInRect(RefArg ink, ULong pen, const Rect* from, const Rect* to, Boolean useInker);	// ROM 0x00140d14 InkDrawInRect__FRC6RefVarUlP4RectT3Uc


// A shape frame that draws a list of strokes as ink: the strokes packed
// and hung off the frame's `ink` slot, the frame's bounds where they
// were.  The word's one is sized by the word's own measurements rather
// than by the box the strokes came out of.
Ref			MakeInkPoly(TStroke** strokes);							// ROM 0x001a31bc MakeInkPoly__FPP7TStroke
Ref			MakeInkWordPoly(TStroke** strokes);						// ROM 0x001a3250 MakeInkWordPoly__FPP7TStroke


// What a word of strokes measures, for the eight bytes an ink word
// carries.  The ascent and the x-height come from where the recogniser
// says the word sits (FindBaseline); the scale and the pen from the
// user's preferences.
void		GetPackedInkWordInfoFromStrokes(TStroke** strokes, PackedInkWordInfo* packed);	// ROM 0x00140a4c GetPackedInkWordInfoFromStrokes__FPP7TStrokeP17PackedInkWordInfo


// The character an ink word stands as in a paragraph's text (the ROM's
// IsInkChar also takes 0xf700 and 0xf702, which are raw ink).
const UniChar kInkWordChar = 0xf701;


// How far outside the strokes themselves the ink reaches.
const long kInkSlop = 2;


// ROM 0x001a3448 DisposeTStrokes__FPP7TStroke



// The x-height brought back to something believable.
void	AdjustInkWordXHeight(RefArg ink, Boolean forNumbers);	// ROM 0x00140940 AdjustInkWordXHeight__FRC6RefVarUc

#endif	/* __INK_H */
