/*
	File:		ink/InkShapes.h

	Contains:	Ink as a shape frame, which is how the rest of the system
				passes a piece of writing about: a frame of the ink verb
				(MakeInkPoly, MakeInkWordPoly in Ink.h) whose viewBounds
				say where it is and whose `ink` slot holds the strokes.

				GetPolyAsTStrokes takes such a frame apart again, and
				SplitInkAt and MergeInk are the two editing operations
				over it - a word cut in two at a caret, and two words put
				together - both of which expand the ink to strokes, work
				on those and pack them up again.  AddInk hands a shape to
				the editor view it is to become a child of.

				GetInkAt and NextInkIndex are the paragraph's side: the
				ink words in a run of styled text, found by walking the
				string for the 0xf701 characters that stand for them.

				RegisterInkNatives binds the NewtonScript functions.

	Reconstructed from the MP2x00 US ROM (0x001a015c-0x001a3a90); each
	function cites its origin.
*/

#ifndef __INKSHAPES_H
#define __INKSHAPES_H

#include "Ink.h"

class TView;
class TParagraphView;
class TStroke;

// The strokes of an ink shape, in the coordinates its viewBounds put it
// at; a list ended by a nil, given back with DisposeTStrokes.
TStroke**	GetPolyAsTStrokes(RefArg form, ULong group);	// ROM 0x001a3340 GetPolyAsTStrokes__FRC6RefVarUl

// An ink shape made a child of an edit view.
void		AddInk(TView* view, RefArg form);				// ROM 0x001a2b70 AddInk__FP5TViewRC6RefVar

// An ink word cut in two at x (a view coordinate), with slop pixels of
// leeway either side: ==> an array of the two shapes, or nil when it
// cannot be cut there - a stroke that crosses the gap, or nothing left
// on one side.
Ref			SplitInkAt(RefArg form, long x, long slop);		// ROM 0x001a2c60 SplitInkAt__FRC6RefVarlT2

// Two ink words put together, the second moved up against the first.
Ref			MergeInk(RefArg first, RefArg second);			// ROM 0x001a2fc4 MergeInk__FRC6RefVarT1

// The next ink word in a paragraph's text after an offset: the index of
// the 0xf701 character standing for it, or -1 when there is none.
long		NextInkIndex(RefArg para, long index);			// ROM 0x001a163c NextInkIndex__FRC6RefVarl

// The ink word at an offset in a paragraph, as a shape frame placed
// where the paragraph draws it; nil when there is no ink there.
Ref			GetInkAt(TParagraphView* para, long offset);	// ROM 0x001a170c GetInkAt__FP14TParagraphViewl

// A bundle of strokes packed up as ink - a word, a sketch, or the ink
// word a paragraph keeps in its text.  The bundle remembers the word it
// was made into, so asking twice costs nothing.
Ref			StrokeBundleToInkWord(RefArg bundle);			// ROM 0x001406bc StrokeBundleToInkWord__FRC6RefVar
Ref			CompressStrokes(RefArg bundle);					// ROM 0x001a2090 CompressStrokes__FRC6RefVar
Ref			CompressStrokesToInk(RefArg bundle);			// ROM 0x001a2100 CompressStrokesToInk__FRC6RefVar

// ... and the other way: an ink shape back into a bundle of strokes.
Ref			ExpandInk(RefArg form, long format);			// ROM 0x001a2344 ExpandInk__FRC6RefVarl

Boolean		PolyContainsInk(RefArg form);					// ROM 0x001a15c8 PolyContainsInk__FRC6RefVar - a shape frame with writing in it
Boolean		ParaContainsInk(RefArg para);					// ROM 0x001a15f4 ParaContainsInk__FRC6RefVar - a paragraph with an ink word in its styles

void		RegisterInkNatives(void);

#endif	/* __INKSHAPES_H */
