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
#include "Text.h"			// TextBounds (ComputeParagraphHeight)
#include "StyleRuns.h"		// GetStylesOfRange (ExtractRichStringFromParaSlots)
#include "Unicode.h"		// Ustrlen
#include "ROMConstants.h"
#include <string.h>


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


// ROM 0x00179eb8 SetFontFamily__FRC6RefVarT1
// The spec with that family, its size and face kept.  An ink word has no
// family, so it comes back untouched.  A family named by a number (or a
// symbol that is one of the ROM's) makes a packed spec straight away -
// family, size << 10, face << 20 - and any other a copy of
// canonicalFontSpec with the three slots set.
Ref
SetFontFamily(RefArg fontSpec, RefArg family)
{
	if (IsInkWord(fontSpec))
		return fontSpec;
	long face = GetFontFace(fontSpec);
	long size = GetFontSize(fontSpec);
	RefVar number(family);
	if (!ISINT(family))
		number = FamilySymToNum(family);
	if (ISINT(number))
		return MAKEINT(RVALUE(number) | (size << 10) | (face << 20));
	RefVar spec(Clone(RefVar(Rcanonicalfontspec)));
	SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(size)));
	SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(face)));
	SetFrameSlot(spec, RSSYMfamily, RefVar(ISINT(family) ? FamilyNumToSym(RVALUE(family)) : (Ref) family));
	return spec;
}


// ROM 0x001eda34 FSetFontFamily
// SetFontFamily(spec, family).
static Ref
FSetFontFamily(RefArg /*rcvr*/, RefArg fontSpec, RefArg family)
{
	return SetFontFamily(fontSpec, family);
}


// ROM 0x0017d9f0 SetFontSize__FRC6RefVarl
// A font spec at another size: an ink word re-measured at it, anything
// else made again as a compact font of the same family and face.
Ref
SetFontSize(RefArg fontSpec, long size)
{
	if (IsInkWord(fontSpec))
		return SetInkWordFontSize(fontSpec, (ULong) size);
	long face = GetFontFace(fontSpec);
	return MakeCompactFont(RefVar(GetFontFamilySym(fontSpec)), size, face);
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


// ROM 0x0017e338 SetFontFace__FRC6RefVarl
// A font spec with another face: an ink word given it, anything else made
// again as a compact font of the same family and size.
Ref
SetFontFace(RefArg fontSpec, long face)
{
	if (IsInkWord(fontSpec))
		return SetInkWordFontFace(fontSpec, (ULong) face);
	long size = GetFontSize(fontSpec);
	return MakeCompactFont(RefVar(GetFontFamilySym(fontSpec)), size, face);
}


// ROM 0x001ed9cc FSetFontFace
// SetFontFace(spec, face): and for the face.
static Ref
FSetFontFace(RefArg /*rcvr*/, RefArg fontSpec, RefArg face)
{
	return SetFontFace(fontSpec, RINT(face));
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

// DEVIATION (library layering): the text engine's view (class 108,
// text/TXView.h) is above the views, so its arms of the two style natives
// are reached through functions it registers (RegisterTXViewNatives).
TXViewStylesHooks	gTXViewStylesHooks = { nil, nil };


// ROM 0x001eeba8 FChangeStylesOfRange
// view:ChangeStylesOfRange(start, length, style, redraw) - the Styles
// slip's verb: a paragraph's ChangeStylesOfRange, or for the text
// engine's view (class 108) TXView::ChangeRangeRuns over the range
// [start, start + length), not toggled.
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
	{
		long offset = RINT(start);
		long end = offset + RINT(length);
		if (gTXViewStylesHooks.fChangeRangeRuns != nil)
			gTXViewStylesHooks.fChangeRangeRuns(view, offset, end, style, NOTNIL(redraw));
	}
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


// ROM 0x001ef3b0 FExtractRangeAsRichString
// view:ExtractRangeAsRichString(start, length) - that range of a
// paragraph as a rich string (TParagraphView::ExtractRangeAsRichString).
static Ref
FExtractRangeAsRichString(RefArg rcvr, RefArg start, RefArg length)
{
	TParagraphView* view = FailGetParagraphView(rcvr);
	ULong count = (ULong) RINT(length);
	ULong offset = (ULong) RINT(start);
	return view->ExtractRangeAsRichString(offset, count);
}


// ROM 0x001efd88 FExtractRichStringFromParaSlots
// ExtractRichStringFromParaSlots(text, styles, start, length) - the same
// for a paragraph that is not open: the range cut out of the text and
// styles slots of its data.  The characters come out plain unless an ink
// word falls among the range's styles, which is when a rich string is
// needed to carry it.
static Ref
FExtractRichStringFromParaSlots(RefArg /*rcvr*/, RefArg text, RefArg styles, RefArg start, RefArg length)
{
	long count = RINT(length);
	ULong offset = (ULong) RINT(start);
	TRichString rich(text);
	ULong size = (ULong) rich.Length();
	if (size < offset)
		offset = size;
	if (size < offset + (ULong) count)
		count = (long) (size - offset);
	RefVar string(AllocateBinary(RSSYMstring, count * 2 + 2));
	memmove(BinaryData(string), (UniChar*) BinaryData(text) + offset, count * 2);
	((UniChar*) BinaryData(string))[count] = 0;
	if (!IsArray(styles) || Length(styles) < 1)
		return string;
	RefVar runs(GetStylesOfRange(styles, (long) offset, count, false));
	long slots = Length(runs);
	for (long slot = 1; slot < slots; slot += 2)
		if (IsInkWord(RefVar(GetArraySlotRef(runs, slot))))
			return MakeRichString(string, runs, false);
	return string;
}


// ROM 0x001ecfd0 FComputeParagraphHeight
// ComputeParagraphHeight(para, top, width) - how tall a paragraph of the
// frame's text in its viewFont would be when laid out that wide: the text
// fitted into a box of that width (TextBounds), never less than 50.
//
// The ROM builds the box on the stack as {top, 0, top, width}: its bottom
// is copied from its top through an unaligned load (the halfword before
// the one named), which is what makes the box empty in height so that
// TextBounds sizes it.
static Ref
FComputeParagraphHeight(RefArg /*rcvr*/, RefArg para, RefArg top, RefArg width)
{
	RefVar font(GetProtoVariable(para, RSSYMviewfont, nil));
	RefVar text(GetProtoVariable(para, RSSYMtext, nil));
	Rect box;
	box.top = (short) RINT(top);
	box.left = 0;
	box.bottom = box.top;
	box.right = (short) RINT(width);
	TRichString rich(text);
	TextBounds(rich, font, &box, 0);
	long height = (short) (box.bottom - box.top);
	if (height <= 50)
		height = 50;
	return MAKEINT(height);
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
	RegisterNativeFunction("FExtractRangeAsRichString", (void*) FExtractRangeAsRichString, 2);
	RegisterNativeFunction("FExtractRichStringFromParaSlots", (void*) FExtractRichStringFromParaSlots, 4);
	RegisterNativeFunction("FComputeParagraphHeight", (void*) FComputeParagraphHeight, 3);
}
