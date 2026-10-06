/*
	File:		recognition/Unistroke.h

	Contains:	The unistroke engine's classifier: one stroke, one
				character, read from its shape and the direction it was
				drawn in, after the alphabet of Palm's Graffiti (the
				original, "Graffiti 1") that so many people already know.

				Each character is a template - a path in a unit square,
				drawn the way the Graffiti reference card draws it.  A
				stroke is resampled to evenly spaced points, centred and
				scaled the way the template it is compared with was, and
				its distance from it is the mean distance between
				corresponding points; the nearest templates are the
				answers.  A template marked straight (I, X, the space,
				backspace, return and shift strokes) is compared with the
				stroke scaled the same in both directions, so that a short
				line keeps its direction; any other with the stroke
				stretched to the template's square, so that a tall O and a
				wide one are both an O.

				Direction matters: the space is a line drawn left to
				right and backspace the same line drawn right to left, as
				in Graffiti.

				Letters and digits share shapes that Graffiti told apart
				by where they were written (its letter and number areas):
				0 and O, 1 and I, 5 and S.  The caller says which it
				wants; the other comes back as the next answer.

	Not reconstructed: the ROM has no such engine (docs/recognition/engines.md).
*/

#ifndef __UNISTROKE_H
#define __UNISTROKE_H

#include "Newton.h"

// what a stroke can be besides a character
enum
{
	kUnistrokeNone		= 0,		// nothing near enough
	kUnistrokeSpace		= ' ',
	kUnistrokeBackspace	= 0x08,
	kUnistrokeReturn	= 0x0D,
	kUnistrokeShift		= 0x0E		// caps shift (twice: caps lock)
};

struct UnistrokeMatch
{
	UniChar		fChar;			// lower case for a letter
	long		fScore;			// the distance x 1000: 0 is a perfect match
};

// Which shapes are read first where they are shared.
enum UnistrokeMode
{
	kUnistrokeLetters,			// 0/O, 1/I, 5/S read as letters
	kUnistrokeDigits			// ... as digits
};

// The answers for a stroke of `count` points (x, y pairs; y down, any
// units), nearest first, at most `max` of them, one per character.
// ==> how many; 0 for a stroke too small to read (a dot).
long	UnistrokeClassify(const double* xy, long count, UnistrokeMode mode, UnistrokeMatch* out, long max);

// A score below this is a character read with confidence.
const long kUnistrokeGoodScore = 160;

// the template drawn as a stroke of `count` points into `xy`, in a
// `size` square at the origin (for tests and demos).  ==> the points
// written, 0 for no such character.
long	UnistrokeTemplatePath(UniChar ch, Boolean digit, double size, double* xy, long count);

#endif	/* __UNISTROKE_H */
