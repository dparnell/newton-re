/*
	File:		recognition/RecConfig.h

	Contains:	The recognition configuration: the frame that says what the
				recogniser should make of what is written on a view, built
				out of the view's own viewFlags, the user's handwriting
				preferences and whatever the view's context says for
				itself.

				It is what stands between a viewFlags word and the
				recognisers.  A view's recognition bits (vAnythingAllowed
				and the field kinds under it) name what may be written on
				it; `vars.userConfiguration` holds what the Handwriting
				Recognition preferences were set to; and a view may carry a
				`recConfig` or `_recogSettings` of its own.  BuildRecConfig
				puts the three together into one frame, whose `inputMask`
				is the word the recognisers are actually driven by, and
				which the areas (Areas.h) are then made from.

				The frames are built as prototype chains rather than
				copied: a configuration is `protoRecConfig` cloned, with
				what it was built from as its _proto and the user's
				configuration (or the view's expanded settings) as its
				_parent, so a preference changed afterwards is seen
				through it.

	Reconstructed from the MP2x00 US ROM (0x00034b70-0x000353ec,
	0x0013f9c0, 0x0019d1f8, 0x001a28a8); each function cites its origin.
*/

#ifndef __RECCONFIG_H
#define __RECCONFIG_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "objects.h"

class TView;

// The view whose recognition settings a view is written under: an edit
// view answers for itself, and a paragraph answers for the edit view it
// is on (its parent, or its parent's parent when a container gathers it
// with others).  Anything else answers for itself.
TView*	GetRecognitionView(TView* view);					// ROM 0x001a28a8 GetRecognitionView__FP5TView

// The configuration frame for a view with those recognition flags.
Ref		BuildRecConfig(TView* view, ULong flags);			// ROM 0x00034b70 BuildRecConfig__FP5TViewUl

// A configuration made ready to be read through: protoRecConfig cloned,
// with `config` as its _proto and the user's configuration - or the
// view's own expanded `_recogSettings` - as its _parent.  A configuration
// that already has a _parent is given back as it is.
Ref		PrepRecConfig(TView* view, RefArg config);			// ROM 0x00035298 PrepRecConfig__FP5TViewRC6RefVar

// The configuration a view gets when it has none of its own: the user's
// preferences (rcPrefsConfig) when anything may be written on it, and
// rcNoRecog when only some things may, with an inputMask built to match
// and the view's custom dictionaries added.
Ref		BuildRCView(TView* view, ULong flags);				// ROM 0x00034ff0 BuildRCView__FP5TViewUl

// A configuration of the view's own prepared and given an inputMask -
// built from its baseInputMask when it asks for one to be built.
Ref		BuildRCProto(TView* view, RefArg config);			// ROM 0x0003517c BuildRCProto__FP5TViewRC6RefVar

// A configuration of the view's own, taking the short way when it is for
// ink words or plain text: the flags become the inputMask as they are.
Ref		BuildInkOrTextConfig(RefArg config, TView* view, ULong flags);	// ROM 0x00034eac BuildInkOrTextConfig__FRC6RefVarP5TViewUl

// Whether the view is one that takes ink words - a view that does not
// allow everything, whose configuration asks for ink word recognition.
Boolean	InkTextEnabled(TView* view, ULong flags, RefArg config);	// ROM 0x00034c94 InkTextEnabled__FP5TViewUlRC6RefVar

// The recognisers' input mask: the bits of the handwriting settings the
// configuration turns on, added to `mask`.  `force` asks for the text and
// shape settings to be read whether or not the configuration says they
// are allowed.
ULong	BuildInputMask(RefArg config, ULong mask, Boolean force);	// ROM 0x0019d1f8 BuildInputMask__FRC6RefVarUlUc

// How many custom dictionaries the view names.  A view that allows
// everything has none - custom dictionaries are for the fields that
// allow only some things.
long	CountCustomDictionaries(TView* view);				// ROM 0x0013f9c0 CountCustomDictionaries__FP5TView

// Whether the view is read a letter at a time rather than a word at a
// time (`rcSingleLetters`).
Boolean	UsesLetters(TView* view);							// ROM 0x000362c8 UsesLetters__FP5TView

#endif	/* __RECCONFIG_H */
