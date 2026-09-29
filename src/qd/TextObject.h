/*
	File:		qd/TextObject.h

	Contains:	Text objects: the ROM's unit of text drawing.  A text
				object is a 0x50-byte handle (NewText) holding a run of
				characters, its length, the styles and their run lengths,
				where it goes and the layout options, a word of flags, and
				the caches the layout keeps (the glyph widths, the bounds).
				The flags' second byte is the operation the port's text
				proc is asked for - nought to draw, 0x100 the width, 0x200
				the bounds - so a single proc (QDProcs' textProc, or
				StdText) answers every question asked of text, which is how
				a printer port or a picture being recorded sees it all.
				DrawTextObj asks for the drawing at full size; CallDrawText
				at the scales given (a picture played into a rectangle of
				another size).  StdText's drawing records the text into an
				open picture (DoPutText, PicRecord.h) and draws it (DrText).

				DrawTextOnce/MeasureTextOnce (Text.h) make one on the stack
				for a single use, flagged as not allocated so DisposeText
				only throws the caches away.

	DEVIATION: the host keeps no caches (the glyph widths are worked out
	afresh by each pass), so the cache fields stay nought.  NOT YET
	RECONSTRUCTED: the layout's three numbers (0x400), TextArrow
	(0x2000) and text at an angle (the options' +0x0c) in DoPointToChar.
	The drawing (DrText.cpp: each style run composed into a slab and
	stretched onto the port), the fitted length (0x100), the bounds
	(0x200: CalcTextBounds), CharToPoint (0x800) and PointToChar
	(0x1000) are here, over the host's layout (TextLayout.h) worked out
	afresh for each question.

	Reconstructed from the MP2x00 US ROM (0x0035a60c, 0x0035b07c,
	0x0035b624, 0x0035bfc4, 0x0035dc94, 0x0035df74); each function cites
	its origin.
*/

#ifndef __TEXTOBJECT_H
#define __TEXTOBJECT_H

#include "Text.h"

// the flags word
enum
{
	kTextObjAllocated	= 0x80000000,	// NewText made it: DisposeText frees it
	kTextObjTempCache1	= 0x40000000,	// the cache at +0x20 is a temporary block
	kTextObjTempCache2	= 0x20000000,	// the cache at +0x28 is
	kTextObjOpMask		= 0x0000ff00,	// the operation the text proc is asked for
	kTextObjOpDraw		= 0x00000000,
	kTextObjOpWidth		= 0x00000100,
	kTextObjOpBounds	= 0x00000200,
	kTextObjOpMetrics	= 0x00000400,	// the layout's three numbers (+0x2c)
	kTextObjOpCharToPoint	= 0x00000800,
	kTextObjOpPointToChar	= 0x00001000,
	kTextObjOpTextArrow	= 0x00002000,
	kTextObjLayoutMask	= 0x000000ff	// how far the layout has got
};

// The ROM's 0x50 bytes, the offsets its (host: pointers are wider, so the
// fields after the first are further along).
struct TextObject
{
	const void*		fText;			// +0x00  the characters
	long			fLength;		// +0x04  how many (cut to the ones that fit a width)
	StyleRecord**	fStyles;		// +0x08  a style per run
	const short*	fRunLengths;	// +0x0c  the runs' lengths; nil for one run
	FPoint			fLocation;		// +0x10  where it goes (x, y)
	TextOptions*	fOptions;		// +0x18
	ULong32			fFlags;			// +0x1c
	void*			fCache20;		// +0x20  (the glyph widths; host: none)
	long			fField24;		// +0x24
	void*			fCache28;		// +0x28  (host: none)
	Fixed			fBounds[3];		// +0x2c  the bounds pass's answer
	Fixed			fHScale;		// +0x38
	Fixed			fVScale;		// +0x3c
	void*			fCache40;		// +0x40  (host: none)
	long			fField44;		// +0x44
	long			fField48;		// +0x48
	void*			fResult;		// +0x4c  where an operation's answer goes
};

// a text object as the text procs are handed it: the handle as a word
typedef Long	TextObjectRef;

inline TextObject*	TextObj(TextObjectRef text)		{ return *(TextObject**) text; }

TextObjectRef	NewText(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint location, TextOptions* options);	// ROM 0x0035bfc4 NewText__FPvlPP11StyleRecordPs6FPointP11TextOptions
void			DisposeText(TextObjectRef text);							// ROM 0x0035dc94 DisposeText__Fl
void			InvalCachedTextInfo(TextObjectRef text);					// ROM 0x0035b624 InvalCachedTextInfo__Fl
void			DrawTextObj(TextObjectRef text);							// ROM 0x0035df74 DrawTextObj__Fl
void			CallDrawText(TextObjectRef text, Fixed hScale, Fixed vScale);	// ROM 0x0035a60c CallDrawText__FlN21
extern "C" void	StdText(TextObjectRef text, Fixed hScale, Fixed vScale);	// ROM 0x0035b07c StdText
void			DrText(TextObjectRef text, Fixed hScale, Fixed vScale);	// ROM 0x0035c530 DrText__FlN21 - the drawing (DrText.cpp)

// What a text object is asked (GetTextObjField).
enum TextObjectField
{
	kTextObjText = 0,			// the characters
	kTextObjFittedLength,		// how many fit the options' width (the width operation)
	kTextObjStyles,
	kTextObjRunLengths,
	kTextObjLocation,			// an FPoint, copied
	kTextObjOptions,
	kTextObjBounds,				// a TextBoundsInfo (the bounds operation)
	kTextObjMetrics				// the layout's three numbers (operation 0x400)
};

void			GetTextObjField(TextObjectRef text, TextObjectField field, void* result);	// ROM 0x0035df90 GetTextObjField__Fl15TextObjectFieldPv
Boolean			SetTextObjField(TextObjectRef text, TextObjectField field, void* value);	// ROM 0x0035e028 SetTextObjField__Fl15TextObjectFieldPv
void			CharToPoint(TextObjectRef text, long offset, FPoint* point);	// ROM 0x0035e100 CharToPoint__FlT1P6FPoint - where the character at `offset` starts
long			PointToChar(TextObjectRef text, FPoint point);			// ROM 0x00359d40 PointToChar__Fl6FPoint - the character boundary nearest the point
Boolean			UpdateLayoutState(TextObjectRef text, long level, Fixed hScale, Fixed vScale);	// ROM 0x0035c080 UpdateLayoutState__FlN31 - ==> whether the layout could be brought to the level
Boolean			RemapCharWidths(TextObjectRef text);						// ROM 0x0035bfbc RemapCharWidths__Fl
void			CalcTextBounds(TextObjectRef text, void* result, Fixed hScale, Fixed vScale);	// ROM 0x0035b3f8 CalcTextBounds__FlPvN21 - six Fixeds: start, advance x and y, ascent, descent, leading
void			CalcTextAdvance(TextObjectRef text, FPoint* advance, long count);	// ROM 0x0035b220 CalcTextAdvance__FlP6FPointT1 - the first `count` characters' advance
void			DoCharToPoint(TextObjectRef text, Fixed hScale, Fixed vScale);	// ROM 0x0035e13c DoCharToPoint__FlN21
void			DoPointToChar(TextObjectRef text, Fixed hScale, Fixed vScale);	// ROM 0x00359d80 DoPointToChar__FlN21

// The QuickDraw library's protocol face of them (the TQDLibraryDriver
// dispatch; the ROM's forward to the functions above).
class TQDLibraryDriver
{
public:
	static void		GetTextObjField(TextObjectRef text, int field, void* result);	// ROM 0x00195cb0 GetTextObjField__16TQDLibraryDriverFliPv
	static void		CharToPoint(TextObjectRef text, long offset, FPoint* point);	// ROM 0x00195cbc CharToPoint__16TQDLibraryDriverFlT1P6FPoint
	static long		PointToChar(TextObjectRef text, FPoint point);			// ROM 0x00195cc0 PointToChar__16TQDLibraryDriverFl6FPoint
};
void			DoPutText(TextObjectRef text, Fixed hScale, Fixed vScale);	// (PicRecord.cpp) the recording

#endif /* __TEXTOBJECT_H */
