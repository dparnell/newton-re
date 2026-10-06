/*
	File:		recognition/UnistrokeRecognizer.h

	Contains:	`TUnistrokeRecognizer` ('UNIS'), the host's Graffiti-style
				engine: one letter per stroke, each stroke read on its own
				from its shape, the letters of a word gathered by the same
				grouping the ROM's engines use.

				A host engine (WordEngines.h), chosen with the
				"Unistroke" button of the Handwriting Recognition slip.

	NOT YET: the reading.  It gathers strokes into words and answers
	ink for every one of them, as `TInkOnlyRecognizer` does - which is the
	framework's stand-in until the alphabet is done
	(docs/recognition/engines.md).

	Reconstructed from nothing: the ROM has no such engine.
*/

#ifndef __UNISTROKERECOGNIZER_H
#define __UNISTROKERECOGNIZER_H

#include "WRecDomain.h"

const ULong kUnistrokeType = 'UNIS';

// Registers the engine under kHostWordEngineInterface.  Does nothing
// when there is no protocol registry.
void	RegisterUnistrokeRecognizer(void);

#endif	/* __UNISTROKERECOGNIZER_H */
