/*
	File:		qd/TextLayout.h

	Contains:	How a text object is laid out on the host: each character's
				advance and the run it is in, as `MeasureGlyphWidths` and
				`JustifyText` (Text.cpp) work it out.  The ROM keeps the
				same numbers in caches hung off the text object (its +0x20
				and +0x28) and fills them in `UpdateLayoutState`; the host
				keeps no caches (a DEVIATION recorded in TextObject.h) and
				lays the text out afresh for each question, which is what
				the drawing, the bounds and the character/point questions
				(TextObject.cpp: DoCharToPoint, DoPointToChar) all share.
*/

#ifndef __TEXTLAYOUT_H
#define __TEXTLAYOUT_H

#include "Text.h"

struct GrafPort;

// the layout of a text: each character's advance and its run
struct TextLayout
{
	Fixed*	fAdvances;		// per character, 16.16 (the justification added)
	long*	fRuns;			// per character, the run it is in
	long	fCount;
	Fixed	fWidth;			// the advances' sum
	long	fSpaces;
};

// Every character's advance in its run's font; with a width to fit, the
// count that fits.  (Text.cpp)
long	MeasureGlyphWidths(const UniChar* chars, long length, StyleRecord** styles, const short* runLengths, TextOptions* options, TextLayout* layout, GrafPort* port, Fixed hScale = 0x10000, Fixed vScale = 0x10000);	// ROM 0x0035baa4 MeasureGlyphWidths__Fl
// The slack spread over the characters; ==> the offset of the text's
// start.  (Text.cpp)
Fixed	JustifyText(const UniChar* chars, long length, TextOptions* options, TextLayout* layout);	// ROM 0x0035b6d8 JustifyText__Fl

#endif	/* __TEXTLAYOUT_H */
