/*
	File:		views/FontNatives.cpp

	Contains:	The NewtonScript functions a script uses to read and change
				the way text looks: the parts of a font spec, and the style
				of a range of a paragraph.

				This is what the Styles slip is written in.  A font spec is
				either an integer with the family, size and face packed into
				it or a frame with those three slots (qd/Fonts.h), and these
				take one apart, put one together, and set one over a range
				of a paragraph's text.

				The ROM keeps them among the view natives
				(0x001ecb98-0x001ef5e8) because that is where the views'
				own functions are; the font arithmetic under them is
				QuickDraw's.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ParagraphView.h"
#include "ViewFlags.h"
#include "RootView.h"
#include "Fonts.h"
#include "Ink.h"
#include "RichString.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "Locale.h"		// GetPreference


/*------------------------------------------------------------------------------
	T h e   p a r t s   o f   a   f o n t   s p e c
------------------------------------------------------------------------------*/

// ROM 0x001ed980 FGetFontFamilyNum
// GetFontFamilyNum(spec): the family number of a font spec - the low ten
// bits of a packed one, the number of a font frame's family symbol - or
// nil when it has no number.
static Ref
FGetFontFamilyNum(RefArg /*rcvr*/, RefArg fontSpec)
{
	return GetFontFamilyNum(fontSpec);
}


// ROM 0x001ecbac FGetFontFace
// GetFontFace(spec): the face bits.  A font *frame* that does not name a
// face - through its protos as well as its own slots - answers nil
// rather than 0, so that a script can tell "plain" from "not said".
// Anything else (a packed integer, an ink word) answers what the font
// accessor makes of it.
static Ref
FGetFontFace(RefArg /*rcvr*/, RefArg fontSpec)
{
	if (IsFrame(fontSpec) && ISNIL(GetProtoVariable(fontSpec, RSSYMface, nil)))
		return NILREF;
	return MAKEINT(GetFontFace(fontSpec));
}


// ROM 0x001eda40 FMakeCompactFont
// MakeCompactFont(family, size, face): the three packed into one spec.
static Ref
FMakeCompactFont(RefArg /*rcvr*/, RefArg family, RefArg size, RefArg face)
{
	long theFace = RINT(face);
	long theSize = RINT(size);
	return MakeCompactFont(family, theSize, theFace);
}


// ROM 0x001eda34 FSetFontFamily
// SetFontFamily(spec, family): the spec with that family, its size and
// face kept.  An ink word has no family, so it comes back untouched.
static Ref
FSetFontFamily(RefArg /*rcvr*/, RefArg fontSpec, RefArg family)
{
	if (IsInkWord(fontSpec))
		return fontSpec;
	long face = GetFontFace(fontSpec);
	long size = GetFontSize(fontSpec);
	return MakeCompactFont(family, size, face);
}


// ROM 0x001ed994 FSetFontSize
// SetFontSize(spec, size): likewise for the size - and an ink word is
// re-measured at it rather than replaced.
Ref
FSetFontSize(RefArg /*rcvr*/, RefArg fontSpec, RefArg size)
{
	ULong theSize = (ULong) RINT(size);
	if (IsInkWord(fontSpec))
		return SetInkWordFontSize(fontSpec, theSize);
	long face = GetFontFace(fontSpec);
	return MakeCompactFont(RefVar(GetFontFamilySym(fontSpec)), (long) theSize, face);
}


// ROM 0x001ed9cc FSetFontFace
// SetFontFace(spec, face): and for the face.
static Ref
FSetFontFace(RefArg /*rcvr*/, RefArg fontSpec, RefArg face)
{
	ULong theFace = (ULong) RINT(face);
	if (IsInkWord(fontSpec))
		return SetInkWordFontFace(fontSpec, theFace);
	long size = GetFontSize(fontSpec);
	return MakeCompactFont(RefVar(GetFontFamilySym(fontSpec)), size, (long) theFace);
}


// ROM 0x001ed988 FSetFontParms
// SetFontParms(spec, parms): all three at once, out of a frame which may
// name any of them; what it does not name stays as it was.
static Ref
FSetFontParms(RefArg /*rcvr*/, RefArg fontSpec, RefArg parms)
{
	return SetFontParms(fontSpec, parms);
}


// ROM 0x001edc6c FGetDefaultFont__FRC6RefVarT1
// GetDefaultFont(view): the font a view lays its text out in.  Its own
// viewFont through the protos answers first; failing that, a *read-only*
// view looks the variable up in full (so a label or a title picks up a
// viewFont set anywhere above it in the context chain), and an editable
// one falls back to the user's font preference instead.
static Ref
FGetDefaultFont(RefArg /*rcvr*/, RefArg view)
{
	TView* theView = FailGetView(view);
	RefVar font(theView->GetProto(RSSYMviewfont));
	if (ISNIL(font))
	{
		if ((theView->fFlags & vReadOnly) != 0)
			font = theView->GetVar(RSSYMviewfont);
		else
			font = GetPreference(RSSYMuserfont);
	}
	return font;
}


/*------------------------------------------------------------------------------
	T h e   s t y l e   o f   a   r a n g e
------------------------------------------------------------------------------*/

// ROM 0x001eeba8 FChangeStylesOfRange
// view:ChangeStylesOfRange(start, length, style, redraw) - the Styles
// slip's verb.  The receiver is the view; only a paragraph answers it
// here.
//
// NOT YET RECONSTRUCTED: the TXView arm (class 108, the text engine's
// own view), which turns the range into a TXOffsetRange and calls
// ChangeRangeRuns.
Ref
FChangeStylesOfRange(RefArg rcvr, RefArg start, RefArg length, RefArg style, RefArg redraw)
{
	TView* view = FailGetView(rcvr);
	if (view->DerivedFrom(clParagraphView))
	{
		Boolean draw = NOTNIL(redraw);
		long count = RINT(length);
		long offset = RINT(start);
		((TParagraphView*) view)->ChangeStylesOfRange(offset, count, style, draw);
	}
	else if (view->DerivedFrom(108))
		;		// NOT YET RECONSTRUCTED: TXView::ChangeRangeRuns
	else
		ThrowMsg("bad view for changeStylesOfRange");
	return NILREF;
}


// ROM 0x001eed3c FGetInsertionStyle
// GetInsertionStyle(): the style the next character typed would have.
// For a paragraph it is the style at the caret; for an edit view it is
// the `styles` global the recogniser left there; and failing both it is
// the user's font preference.
static Ref
FGetInsertionStyle(RefArg /*rcvr*/)
{
	RefVar style;
	TView* view = gRootView != nil ? gRootView->fCaretView : nil;
	if (view != nil)
	{
		if (view->DerivedFrom(clParagraphView))
			style = ((TParagraphView*) view)->GetStyleForInsertion(gRootView->fCaretOffset, false, false);
		else if (view->DerivedFrom(clEditView))
			style = GetFrameSlotRef(RefVar(gVarFrame), RSSYMstyles);
	}
	if (ISNIL(style))
		style = GetPreference(RSSYMuserfont);
	return style;
}


// ROM 0x001eee28 FGetRangeText
// GetRangeText(view, start, end) - the text of that range, which each
// kind of view answers for itself.
static Ref
FGetRangeText(RefArg /*rcvr*/, RefArg view, RefArg start, RefArg end)
{
	TView* theView = FailGetView(view);
	long to = RINT(end);
	long from = RINT(start);
	return theView->GetRangeText(from, to);
}


// ROM 0x001ef414 FExtractTextRange
// view:ExtractTextRange(start, length) - the plain characters of that
// range of a paragraph, without the styles GetRangeText would carry.
Ref
FExtractTextRange(RefArg rcvr, RefArg start, RefArg length)
{
	TParagraphView* view = FailGetParagraphView(rcvr);
	long count = RINT(length);
	long offset = RINT(start);
	return view->ExtractTextRange((ULong) offset, (ULong) count);
}


// ROM 0x001ef5b8 FGetTextFlags
// GetTextFlags(view): what the view says of the text it holds (0 for
// something that is not a view at all).
static Ref
FGetTextFlags(RefArg /*rcvr*/, RefArg view)
{
	TView* theView = GetView(view);
	return MAKEINT(theView != nil ? theView->TextFlags() : 0);
}


// ROM 0x001127e8 FMungeStyles
// styles:MungeStyles(fontSpec) - every run of a styles array given the
// size of that spec, the rest of each run's style left as it was.  A
// run whose style is an ink word is skipped: a word of writing has its
// own size, and changing it is the ink area's business.  A styles
// argument that is not an array comes back as it is.
static Ref
FMungeStyles(RefArg /*rcvr*/, RefArg styles, RefArg fontSpec)
{
	if (!IsArray(styles))
		return styles;
	RefVar munged(Clone(styles));
	long count = Length(munged);
	long size = GetFontSize(fontSpec);
	for (long i = 1; i < count; i += 2)
	{
		RefVar one(GetArraySlotRef(munged, i));
		if (IsInkWord(one))
			continue;
		SetArraySlot(munged, i, RefVar(FSetFontSize(RefVar(NILREF), one, RefVar(MAKEINT(size)))));
	}
	return munged;
}


void
RegisterFontNatives(void)
{
	RegisterNativeFunction("FGetFontFamilyNum", (void*) FGetFontFamilyNum, 1);
	RegisterNativeFunction("FGetFontFace", (void*) FGetFontFace, 1);
	RegisterNativeFunction("FMakeCompactFont", (void*) FMakeCompactFont, 3);
	RegisterNativeFunction("FSetFontFamily", (void*) FSetFontFamily, 2);
	RegisterNativeFunction("FSetFontSize", (void*) FSetFontSize, 2);
	RegisterNativeFunction("FSetFontFace", (void*) FSetFontFace, 2);
	RegisterNativeFunction("FSetFontParms", (void*) FSetFontParms, 2);
	RegisterNativeFunction("FGetDefaultFont__FRC6RefVarT1", (void*) FGetDefaultFont, 1);
	RegisterNativeFunction("FChangeStylesOfRange", (void*) FChangeStylesOfRange, 4);
	RegisterNativeFunction("FGetInsertionStyle", (void*) FGetInsertionStyle, 0);
	RegisterNativeFunction("FGetRangeText", (void*) FGetRangeText, 3);
	RegisterNativeFunction("FGetTextFlags", (void*) FGetTextFlags, 1);
	RegisterNativeFunction("FMungeStyles", (void*) FMungeStyles, 2);
	RegisterNativeFunction("FExtractTextRange", (void*) FExtractTextRange, 2);
}
