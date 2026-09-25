/*
	File:		recognition/RosRecognizer.h

	Contains:	`TRosRecognizer`, the ROM's own handwriting engine as the
				recognition system sees it.

				It is an implementation of the `TWRecognizer` protocol
				(recognition/WRecDomain.h), the same socket
				`TInkOnlyRecognizer` sits in, and it is thin: strokes
				come down from the word domain, are turned into arrays
				of `FPoint` and handed to the Rosetta engine
				(recognition/Rosetta.h); the engine calls back through
				`RosRecCheckWords` with the words it read, and those are
				put on the unit as interpretations.  Everything hard is
				below the seam.

				Two things it does that are its own.  A word the engine
				breaks differently from the way the strokes were grouped
				is *regrouped* here: `RosRecCheckWords` is told how many
				of the group's strokes the words cover, and if that is
				fewer than the group has it splits the group in two,
				gives the words to the first part and starts a new group
				from the rest.  And an *ink* group - strokes the writer
				is drawing rather than writing - is gathered here
				(`GroupInkStroke`) into the recogniser's own array of up
				to eighty strokes, which is handed back whole through
				`EndInkStrokeGroup` when the drawing ends.

	`AreaInfoConfigure` reads a recognition configuration into the
	engine's area block.  The host installs this recogniser
	(`RegisterRosettaWRec`), so writing is read by the ROM's own engine.

	Reconstructed from the MP2x00 US ROM (0x001b5bac-0x001b7120); each
	function cites its origin.
*/

#ifndef __ROSRECOGNIZER_H
#define __ROSRECOGNIZER_H

#ifndef __WRECDOMAIN_H
#include "WRecDomain.h"
#endif

// The most ink strokes one drawing may gather before the recogniser
// hands them over whether the writer has finished or not.  (The ROM's
// object is 0x164 bytes and the array starts at +0x24.)
const long	kRosMaxInkStrokes	= 80;

// Registers the engine with the protocol registry, so that
// `TWRecognizer::New("TRosRecognizer")` finds it.  The ROM's
// `RegisterWRec` is this.
void	RegisterRosettaWRec(void);									// ROM 0x001b6b7c RegisterRosettaWRec__Fv

// The one that exists: the engine's callback needs to find it, and
// there is only ever one.
class TRosRecognizer;
extern TRosRecognizer*	gRosRecognizer;								// ROM 0x0c10194c gRosRecognizer

#endif	/* __ROSRECOGNIZER_H */
