/*
	File:		recognition/RecConfig.cpp

	Contains:	The recognition configuration (RecConfig.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RecConfig.h"
#include "View.h"
#include "ViewFlags.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NativeFunctions.h"


// The text-ish field kinds: the recognition bits that say a view takes
// words, as opposed to strokes, gestures, shapes or a caps preference.
// TextOrInkWordsEnabled and BuildRCView both test for them.
enum
{
	kTextFieldFlags = vCharsAllowed | vNumbersAllowed | vLettersAllowed
					| vPunctuationAllowed | vMathAllowed | vPhoneField
					| vDateField | vTimeField | vAddressField | vNameField
					| vCustomDictionaries
};


// ROM 0x001a28a8 GetRecognitionView__FP5TView
// Which view's recognition settings apply.  An edit view is written on
// directly and answers for itself; a paragraph is on one, so the edit
// view it belongs to answers for it - its parent when that is the
// editor, its parent's parent when a container gathers it with others.
// Anything else answers for itself.
TView*
GetRecognitionView(TView* view)
{
	if (view->DerivedFrom(clEditView))
		return view;
	if (!view->DerivedFrom(clParagraphView))
		return view;
	if (view->fParent->DerivedFrom(clEditView))
		return view->fParent;
	if (view->fParent->fParent->DerivedFrom(clEditView))
		return view->fParent->fParent;
	return view;
}


// ROM 0x0013f9c0 CountCustomDictionaries__FP5TView
// How many dictionaries the view names in its `dictionaries` slot - one
// when it names a single dictionary by number, its length when it names
// an array of them.
//
// A view that allows everything has none, whatever it says: custom
// dictionaries belong to the fields that allow only some things, and the
// test is written so that vAnythingAllowed rules them out before the
// vCustomDictionaries bit is even looked at.
long
CountCustomDictionaries(TView* view)
{
	long count = 0;
	ULong flags = view->fFlags & vRecognitionAllowed;
	if ((flags & vAnythingAllowed) != vAnythingAllowed
		&& (flags & vCustomDictionaries) != 0)
	{
		RefVar dictionaries(view->GetVar(RSSYMdictionaries));
		if (NOTNIL(dictionaries))
			count = ISINT(dictionaries) ? 1 : Length(dictionaries);
	}
	return count;
}


// ROM 0x0019d1f8 BuildInputMask__FRC6RefVarUlUc
// The word the recognisers are driven by, built out of the handwriting
// settings the configuration answers: the cursive options for words,
// letters and numbers, punctuation when one of those is on, and shapes.
// `force` reads the settings whether or not the configuration allows
// them, which is what a view with no configuration of its own wants.
//
// The punctuation option is only asked for when words, letters or
// numbers are on, so punctuation alone never reaches the recognisers.
ULong
BuildInputMask(RefArg config, ULong mask, Boolean force)
{
	Boolean cursive = false;
	if (force || NOTNIL(GetProtoVariable(config, RSSYMallowtextrecognition, nil)))
	{
		if (NOTNIL(GetVariable(config, RSSYMdotextrecognition, nil, 0)))
		{
			if (NOTNIL(GetVariable(config, RSSYMwordscursiveoption, nil, 0)))
			{
				cursive = true;
				mask |= 0x1000;
			}
			if (NOTNIL(GetVariable(config, RSSYMletterscursiveoption, nil, 0)))
			{
				cursive = true;
				mask |= 0x6000;
			}
			Boolean numbers = NOTNIL(GetVariable(config, RSSYMnumberscursiveoption, nil, 0));
			if (numbers)
				mask |= 0x100000 | 0xc2000;
			if ((numbers || cursive)
				&& NOTNIL(GetVariable(config, RSSYMpunctuationcursiveoption, nil, 0)))
				mask |= 0x8000;
		}
	}
	if (force || NOTNIL(GetProtoVariable(config, RSSYMallowshaperecognition, nil)))
	{
		if (NOTNIL(GetVariable(config, RSSYMdoshaperecognition, nil, 0)))
			mask |= 0x10000;
	}
	return mask;
}


// ROM 0x00035298 PrepRecConfig__FP5TViewRC6RefVar
// A configuration made ready to be read through.  Rather than copying
// anything, it builds a prototype chain: protoRecConfig cloned, with the
// configuration itself as its _proto and what it is to be read against
// as its _parent - the user's configuration, or the view's own
// `_recogSettings` expanded by the NewtonScript `ExpandSettings` with the
// user's configuration behind *it*.  A preference changed afterwards is
// therefore seen straight away through every configuration built from it.
//
// A configuration that already has a _parent has been through here, and
// is given back as it is.
Ref
PrepRecConfig(TView* view, RefArg config)
{
	if (FrameHasSlotRef(config, RSSYM_parent))
		return config;
	RefVar prepped(Clone(RefVar(Rprotorecconfig)));
	SetFrameSlot(prepped, RSSYM_proto, config);
	RefVar userConfig(GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration));
	RefVar settings;
	if (view != nil)
		settings = view->GetVar(RSSYM_recogsettings);
	if (ISNIL(settings))
		SetFrameSlot(prepped, RSSYM_parent, userConfig);
	else
	{
		settings = NSCallGlobalFn(RefVar(RSSYMexpandsettings), settings);
		SetFrameSlot(settings, RSSYM_proto, userConfig);
		SetFrameSlot(prepped, RSSYM_parent, settings);
	}
	return prepped;
}


// ROM 0x00034ff0 BuildRCView__FP5TViewUl
// The configuration for a view that has none of its own.  A view that
// allows everything is written on with the user's own handwriting
// preferences (rcPrefsConfig) and gets an inputMask built from them;
// a view that allows only some things starts from rcNoRecog and keeps
// the mask its own flags make, so that a date field reads dates and
// nothing else.  Either way its custom dictionaries go in as well.
Ref
BuildRCView(TView* view, ULong flags)
{
	RefVar config;
	RefVar userConfig(GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration));
	Boolean anything = (flags & vAnythingAllowed) == vAnythingAllowed;
	config = anything ? Rrcprefsconfig : Rrcnorecog;
	config = PrepRecConfig(view, config);
	if (anything)
		flags = BuildInputMask(config, vClickable | vGesturesAllowed, true);
	SetFrameSlot(config, RSSYMinputmask, RefVar(MAKEINT(flags)));
	RefVar dictionaries;
	if (view != nil && CountCustomDictionaries(view) != 0)
		dictionaries = view->GetVar(RSSYMdictionaries);
	if (NOTNIL(dictionaries))
		SetFrameSlot(config, RSSYMdictionaries, dictionaries);
	return config;
}


// ROM 0x0003517c BuildRCProto__FP5TViewRC6RefVar
// A configuration the view carries itself, prepared and given an
// inputMask.  It keeps the mask it already has unless it has none, or
// unless it asks - with a `buildInputMask` slot - for one to be built
// from the settings; the mask then starts at its `baseInputMask`, or at
// the clicks and gestures every view takes.
Ref
BuildRCProto(TView* view, RefArg config)
{
	RefVar prepped(PrepRecConfig(view, config));
	RefVar mask(GetProtoVariable(prepped, RSSYMinputmask, nil));
	if (ISNIL(mask) || NOTNIL(GetProtoVariable(prepped, RSSYMbuildinputmask, nil)))
	{
		ULong base = vClickable | vGesturesAllowed;
		RefVar baseMask(GetProtoVariable(prepped, RSSYMbaseinputmask, nil));
		if (NOTNIL(baseMask))
			base = (ULong) RINT(baseMask);
		mask = MAKEINT(BuildInputMask(prepped, base, false));
	}
	SetFrameSlot(prepped, RSSYMinputmask, mask);
	return prepped;
}


// ROM 0x00034c94 InkTextEnabled__FP5TViewUlRC6RefVar
// Whether the view is one whose writing is kept as ink words: a view
// that allows only some things, whose configuration asks for ink word
// recognition.  A view that allows everything never does - ink words
// there are the page's business, not a field's.
Boolean
InkTextEnabled(TView* view, ULong flags, RefArg config)
{
	if (view == nil || (flags & vAnythingAllowed) == vAnythingAllowed)
		return false;
	return NOTNIL(GetProtoVariable(config, RSSYMdoinkwordrecognition, nil));
}


// ROM 0x00034cec BuildRecConfigForDeferred__FP5TViewUl
Ref
BuildRecConfigForDeferred(TView* view, ULong flags)
{
	RefVar config(NILREF);
	if (flags != vAnythingAllowed)
	{
		if (view != nil)
			config = GetProtoVariable(RefVar(view->fContext), RSSYMrecconfig, nil);
		if (ISNIL(config) || InkTextEnabled(view, flags, config))
			config = BuildRCView(view, flags);
		else
			config = PrepRecConfig(view, config);
		SetFrameSlot(config, RSSYMinputmask, RefVar(MAKEINT(flags & 0xfffef1ff)));
		SetFrameSlot(config, RSSYMallowtextrecognition, RefVar(TRUEREF));
		SetFrameSlot(config, RSSYMdotextrecognition, RefVar(TRUEREF));
		SetFrameSlot(config, RSSYMdoinkwordrecognition, RefVar(NILREF));
		SetFrameSlot(config, RSSYMspeedcursiveoption, RefVar(MAKEINT(2)));
		SetFrameSlot(config, RSSYMletterspacecursiveoption, RefVar(NILREF));
	}
	return config;
}


// ROM 0x00034eac BuildInkOrTextConfig__FRC6RefVarP5TViewUl
// A configuration the view carries itself.  When it is an ink word or
// plain text configuration that allows text recognition, the view's own
// flags become the inputMask as they are - there is nothing to build,
// because what may be written is exactly what the field says.  Anything
// else goes the long way through BuildRCProto.
Ref
BuildInkOrTextConfig(RefArg config, TView* view, ULong flags)
{
	RefVar theConfig(config);
	if (InkTextEnabled(view, flags, theConfig))
	{
		theConfig = PrepRecConfig(view, theConfig);
		if (NOTNIL(GetVariable(theConfig, RSSYMallowtextrecognition, nil, 0))
			&& NOTNIL(GetVariable(theConfig, RSSYMdotextrecognition, nil, 0)))
		{
			SetFrameSlot(theConfig, RSSYMinputmask, RefVar(MAKEINT(flags)));
			if (CountCustomDictionaries(view) != 0)
			{
				RefVar dictionaries(view->GetVar(RSSYMdictionaries));
				if (NOTNIL(dictionaries))
					SetFrameSlot(theConfig, RSSYMdictionaries, dictionaries);
			}
			return theConfig;
		}
	}
	return BuildRCProto(view, theConfig);
}


// ROM 0x00034b70 BuildRecConfig__FP5TViewUl
// The configuration a view's writing is recognised under.  A view that
// allows everything is read against `vars.userConfiguration.testConfig` -
// which is what the handwriting test screens set and is nil in ordinary
// use - and a view that allows only some things against its own
// `recConfig`; with neither, BuildRCView makes one out of the flags and
// the user's preferences.
Ref
BuildRecConfig(TView* view, ULong flags)
{
	RefVar config;
	if ((flags & vAnythingAllowed) == vAnythingAllowed)
	{
		RefVar userConfig(GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration));
		config = GetFrameSlotRef(userConfig, RSSYMtestconfig);
	}
	else if (view != nil)
		config = GetProtoVariable(view->fContext, RSSYMrecconfig, nil);
	else
		return BuildRCView(view, flags);
	if (ISNIL(config))
		return BuildRCView(view, flags);
	return BuildInkOrTextConfig(config, view, flags);
}


// ROM 0x000362c8 UsesLetters__FP5TView
// Whether the view is one whose recognition configuration says it is
// read a letter at a time (`rcSingleLetters`) rather than a word at a
// time.  A character written over another in such a view is read as
// itself rather than being made to fit a word.
Boolean
UsesLetters(TView* view)
{
	RefVar config(view->GetVar(RSSYMrecconfig));
	if (ISNIL(config))
		return false;
	return NOTNIL(RefVar(GetVariable(config, RSSYMrcsingleletters, nil, 0)));
}

/*------------------------------------------------------------------------------
	W h a t   a   v i e w   a l l o w s

	Two questions a script asks before it hands a view some writing: may
	it keep raw ink, and may it keep a word of writing that was not read.
	Both are answered out of the view's recognition configuration, and a
	view whose recognition flags are every bit of the writing mask is
	taken to allow both without the configuration being built at all.
------------------------------------------------------------------------------*/

// the recognition flags of viewFlags: every kind of writing allowed
const ULong kAllRecognitionFlags = 0x01fffe00;


// ROM 0x001a293c ViewAllowsInk__FP5TView
// Whether the view keeps raw ink - its configuration's `_proto` says so
// by *not* having a `doRawInkRecognition` slot, or by having one that is
// not nil.  (A slot that is there and nil is the only way to say no.)
Boolean
ViewAllowsInk(TView* view)
{
	TView* recognition = GetRecognitionView(view);
	if ((recognition->fFlags & kAllRecognitionFlags) == kAllRecognitionFlags)
		return true;
	RefVar config(BuildRecConfig(recognition, recognition->fFlags & (kAllRecognitionFlags | 0x100)));
	RefVar proto(GetFrameSlotRef(config, RSSYM_proto));
	return !FrameHasSlot(proto, RSSYMdorawinkrecognition)
		   || NOTNIL(GetFrameSlotRef(proto, RSSYMdorawinkrecognition));
}


// ROM 0x001a29ec ViewAllowsInkWords__FP5TView
// ... and whether it keeps a word of writing that was not read, which
// the configuration says plainly (through the protos this time).
Boolean
ViewAllowsInkWords(TView* view)
{
	TView* recognition = GetRecognitionView(view);
	if ((recognition->fFlags & kAllRecognitionFlags) == kAllRecognitionFlags)
		return true;
	RefVar config(BuildRecConfig(recognition, recognition->fFlags & (kAllRecognitionFlags | 0x100)));
	return NOTNIL(GetProtoVariable(config, RSSYMdoinkwordrecognition, nil));
}


// ROM 0x001a0028 FViewAllowsInk
static Ref
FViewAllowsInk(RefArg /*rcvr*/, RefArg view)
{
	return MAKEBOOLEAN(ViewAllowsInk(FailGetView(view)));
}


// ROM 0x001a0124 FViewAllowsInkWords
static Ref
FViewAllowsInkWords(RefArg /*rcvr*/, RefArg view)
{
	return MAKEBOOLEAN(ViewAllowsInkWords(FailGetView(view)));
}


// ROM 0x001ef5f0 FViewWorksWithCombCorrector
// ViewWorksWithCombCorrector(view): whether the comb corrector - the one
// that shows a word a letter at a time in boxes - can be used on that
// view.  A paragraph or an edit view can; nothing else can.
static Ref
FViewWorksWithCombCorrector(RefArg /*rcvr*/, RefArg view)
{
	TView* theView = GetView(view);
	if (theView == nil)
		return NILREF;
	return MAKEBOOLEAN(theView->DerivedFrom(clParagraphView) || theView->DerivedFrom(clEditView));
}


void
RegisterRecConfigNatives(void)
{
	RegisterNativeFunction("FViewAllowsInk", (void*) FViewAllowsInk, 1);
	RegisterNativeFunction("FViewAllowsInkWords", (void*) FViewAllowsInkWords, 1);
	RegisterNativeFunction("FViewWorksWithCombCorrector", (void*) FViewWorksWithCombCorrector, 1);
}
