/*
	File:		recognition/UnistrokeRecognizer.h

	Contains:	`TUnistrokeRecognizer` ('UNIS'), the host's Graffiti-style
				engine: one stroke, one character, typed at the caret as
				soon as it is written, as on a Palm.

				A host engine (WordEngines.h), chosen with the "Unistroke"
				button of the Handwriting Recognition slip.  Each stroke is
				a unit of its own, closed at once (no waiting for the pen
				to rest), read by the classifier (Unistroke.h: Graffiti's
				alphabet - docs/recognition/unistroke-card.svg) and
				labelled as typing (kHostTypedLabel), so that the
				recogniser over it posts the character at the caret as the
				keyboard would (THostWRecRecognizer, WordEngines.cpp): a
				letter or digit, a space (a line drawn left to right),
				backspace (right to left) and return (a line down to the
				left) all do what those keys do.  The caps shift (a line
				drawn up) makes the next letter a capital, twice locks
				capitals, and a third time unlocks them.

				Graffiti read 0/O, 1/I and 5/S by where they were written
				(its letter and number areas); here a field that takes
				numbers but no letters reads them as digits, and any other
				as letters.  A stroke not read well enough is left as ink.

	NOT YET: punctuation (Graffiti's punctuation shift is a tap, which the
	Newton takes as a click before any recogniser sees it), the symbol and
	extended shifts, and accented letters.

	Not reconstructed: the ROM has no such engine.
*/

#ifndef __UNISTROKERECOGNIZER_H
#define __UNISTROKERECOGNIZER_H

#include "WRecDomain.h"

const ULong kUnistrokeType = 'UNIS';

// Registers the engine under kHostWordEngineInterface.  Does nothing
// when there is no protocol registry.
void	RegisterUnistrokeRecognizer(void);

#endif	/* __UNISTROKERECOGNIZER_H */
