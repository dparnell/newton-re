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
	afresh by each pass), so the cache fields stay nought; DrText is
	Text.cpp's layout, drawing a glyph at a time.  NOT YET RECONSTRUCTED:
	the operations other than drawing (width, bounds, CharToPoint,
	PointToChar, TextArrow, UpdateLayoutState), and text drawn at a scale
	other than 1.0 (DrText draws at full size whatever the scales).

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
void			DrText(TextObjectRef text, Fixed hScale, Fixed vScale);	// (Text.cpp) the drawing
void			DoPutText(TextObjectRef text, Fixed hScale, Fixed vScale);	// (PicRecord.cpp) the recording

#endif /* __TEXTOBJECT_H */
