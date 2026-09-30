/*
	File:		views/ParagraphView.cpp

	Contains:	TParagraphView: a view of styled text, display only.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ParagraphView.h"
#include "Polygons.h"
#include "Ink.h"
#include "InkShapes.h"
#include "Words.h"			// IsPunctSymbol
#include "WordInfo.h"		// kWordInfoIsInk
#include "CorrectInfo.h"
#include "WordList.h"		// TWordList, the try string
#include "Controller.h"		// AreStrokesAfterUnit
#include "RecConfig.h"		// UsesLetters
#include "EditView.h"		// ViewExpectsNumbers
#include "TextView.h"		// vjOneLineOnly
#include "InkFont.h"
#include "Hilites.h"
#include "DragDrop.h"
#include "ClipboardView.h"
#include "Transform.h"
#include "OSErrors.h"
#include "StyleRuns.h"
#include "Unicode.h"
#include "RootView.h"
#include "Application.h"
#include "Commands.h"
#include "Animate.h"
#include "NewtonTime.h"
#include "UnitPublic.h"
#include "Stroke.h"
#include "StrokeQueue.h"
#include "Keyboard.h"
#include "Rects.h"
#include "Regions.h"
#include "RegionVars.h"
#include "Draw.h"
#include "Ports.h"
#include "Fonts.h"
#include "RichString.h"
#include "REPTranslators.h"
#include "Areas.h"
#include "Recognizer.h"		// gRecognition: modal recognition
#include "Rerecognize.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "Locale.h"
#include "NewtonExceptions.h"
#include "Interpreter.h"
#include "NewtonMemory.h"
#include <string.h>


// host: the C++ hilite a Ref in the hilites array stands for.
static TParagraphHilite*
HiliteOf(RefArg hilite)
{
	return ISNIL(hilite) ? nil : (TParagraphHilite*) RefToAddress(hilite);
}


const UniChar kCR = 0x0d;
const UniChar kSP = 0x20;
const UniChar kEllipsisChar = 0x2026;		// the ROM's U_CONST_CHAR(0xc9): Mac Roman's ellipsis as Unicode

// ROM 0x00261d2c GetInputViewTextFlags__FUlT1
// The text flags of an input view: bits 14-16 (0x1c000) say what kind of
// text it takes; a view without them takes anything (0xc000), one that is
// read-only or takes no scripts (viewFlags 0x82) just the plain kind (0x4000).
ULong
GetInputViewTextFlags(ULong textFlags, ULong viewFlags)
{
	if ((textFlags & 0x1c000) == 0)
		textFlags |= ((viewFlags & 0x82) == 0) ? 0xc000 : 0x4000;
	return textFlags;
}


// ROM 0x000a2ffc TestLineOverlap__FP5TRectT1
// Where a line lies against a box: 0 above it, 1 within, 2 below - by its
// midline.
static long
TestLineOverlap(const Rect& box, const Rect& line)
{
	long mid = (line.top + line.bottom) / 2;
	if (mid < box.top)
		return 0;
	return mid <= box.bottom ? 1 : 2;
}


/*------------------------------------------------------------------------------
	T P a r a g r a p h V i e w
------------------------------------------------------------------------------*/

// ROM 0x0017e3ac ClassID__14TParagraphViewCFv
long
TParagraphView::ClassID(void) const
{
	return clParagraphView;
}


// ROM 0x0017e3b4 DerivedFrom__14TParagraphViewCFl
Boolean
TParagraphView::DerivedFrom(long id) const
{
	return id == clParagraphView || TDataView::DerivedFrom(id);
}


// ROM 0x0017edc0 Constructor__14TParagraphViewFRC6RefVarP5TView
// The text flags unknown (-1) until SetupDone.
void
TParagraphView::Constructor(RefArg context, TView* parent)
{
	fTextFlags = -1;
	fCaretOffset = 0;
	fSetupDone = false;
	fTapped = false;
	fTapPoint.h = fTapPoint.v = 0;
	TView::Constructor(context, parent);
}


// ROM 0x001805f4 __dt__14TParagraphViewFv
// The caches go (the hilites, style records, text objects and lines);
// NOT YET RECONSTRUCTED: the correction info, and vars.lastTextChanged /
// lastTextHiliteChanged cleared when they name this view.
TParagraphView::~TParagraphView()
{
	DisposeRuns();
	if (fLines != nil)
		DisposPtr((Ptr) fLines);
}


// ROM 0x0017f5d8 SetupDone__14TParagraphViewFv
// The view readied once its context is complete: the transfer mode
// (viewTransferMode, srcOr when none), the line spacing (viewLineSpacing),
// the text flags (the input view's from textFlags and viewFlags), the
// bounds cached, the default style's height as the line height, the
// locale's break tables; a rich text slot is split into text and styles
// (NOT YET RECONSTRUCTED: ink - a rich string's text is used as it is);
// then the styles are checked (CheckStyles NOT YET) and the caches built.
void
TParagraphView::SetupDone(void)
{
	RefVar mode(GetVar(RSSYMviewtransfermode));
	fTransferMode = ISNIL(mode) ? srcOr : RINT(mode);
	RefVar spacing(GetProto(RSSYMviewlinespacing));
	fLineSpacing = ISNIL(spacing) ? 0 : RINT(spacing);
	fTextFlags = (long) GetInputViewTextFlags((ULong) TextFlags(), fFlags);
	fCachedBounds = viewBounds;
	RefVar style(GetDefaultViewStyle());
	StyleRecord record;
	CreateTextStyleRecord(style, &record);
	FontInfo fontInfo;
	GetStyleFontInfo(&record, &fontInfo);
	fLineHeight = fontInfo.ascent + fontInfo.descent + fontInfo.leading;
	DisposeStyleRecord(&record);
	if (NOTNIL(IntlResources()))				// (the host without a locale: FindWordBreaks needs no table)
	{
		fWordBreakTable = GetLocaleSlot(RSSYMwordbreaktable);
		fLineBreakTable = GetLocaleSlot(RSSYMlinebreaktable);
	}
	fCachesValid = true;
	fCalculateBounds = (fFlags & vCalculateBounds) != 0;
	TView::SetupDone();
	CreateAllCaches();
	fSetupDone = true;
}


// ROM 0x0017e3e8 SetBounds__14TParagraphViewFRC5TRect
// The bounds (in the parent's contents coordinates) kept inside the
// parent's width - the right edge brought in to the parent's when it goes
// past it (or always, for a paragraph whose input flags have bit 0), the
// left out to the parent's, each only when that leaves more than ten
// pixels - unless the view's text flags have 0x800; then written to the
// data frame's viewBounds (the context's own slot removed when the data
// frame is another frame), set (TView) and the lines laid out again
// (FixupBBox).
//
// The text flags the 0x800 is looked for in are the paragraph's own
// fTextFlags, read through the unnamed accessor at vtable +0x20 - still
// -1, every bit set, before SetupDone, so a paragraph being built is not
// clamped.  The input flags are worked out for the test and not kept.
void
TParagraphView::SetBounds(const Rect& inBounds)
{
	Rect bounds = inBounds;
	Point origin = fParent->ContentsOrigin();
	short parentRight = (short) ((unsigned short) fParent->viewBounds.right - origin.h);
	ULong inputFlags = (ULong) fTextFlags;
	if (fTextFlags == -1)
		inputFlags = GetInputViewTextFlags((ULong) TextFlags(), fFlags);
	if (((ULong) fTextFlags & 0x800) == 0)
	{
		if (((inputFlags & 1) != 0 || parentRight < bounds.right) && bounds.left + 10 < parentRight)
			bounds.right = parentRight;
		short parentLeft = (short) ((unsigned short) fParent->viewBounds.left - origin.h);
		if (parentLeft > bounds.left && parentLeft + 10 < bounds.right)
			bounds.left = parentLeft;
	}
	RefVar boundsRef(ToObject(bounds));
	RefVar data(DataFrame());
	SetFrameSlot(data, RSSYMviewbounds, boundsRef);
	data = DataFrame();
	if (!EQRef(data, fContext))
		RemoveSlot(fContext, RSSYMviewbounds);
	TView::SetBounds(bounds);
	// host: the line cache holds the lines where they are drawn, so it is
	// moved with the view (or marked for laying out again when the size
	// changed) before FixupBBox looks at it
	if (fLines != nil)
	{
		if (viewBounds.right - viewBounds.left != fCachedBounds.right - fCachedBounds.left
		 || viewBounds.bottom - viewBounds.top != fCachedBounds.bottom - fCachedBounds.top)
			fCachesValid = false;
		else
		{
			Point delta;
			delta.h = (short) (viewBounds.left - fCachedBounds.left);
			delta.v = (short) (viewBounds.top - fCachedBounds.top);
			OffsetCachedBounds(delta);
		}
	}
	FixupBBox();
}


/*------------------------------------------------------------------------------
	T h e   s l o t s
------------------------------------------------------------------------------*/

// ROM 0x00181004 Text__14TParagraphViewFv
// The text slot as a string.
Ref
TParagraphView::Text(void)
{
	return GetValue(RSSYMtext, RSSYMstring);
}


// ROM 0x00181448 Styles__14TParagraphViewFv
// The styles slot (the runs), made to cover the text.
Ref
TParagraphView::Styles(void)
{
	RefVar styles(GetProto(RSSYMstyles));
	RefVar text(Text());
	CorrectAnyBadStyleRuns(styles, (Length(text) - 2) >> 1);
	return styles;
}


// ROM 0x00181104 GetStyles__14TParagraphViewFv
// The styles to draw with: the runs when there are several, the one
// style when there is a single run (the runs when it is an ink word), the
// default view style when there are none.
Ref
TParagraphView::GetStyles(void)
{
	RefVar styles(Styles());
	if (NOTNIL(styles))
	{
		long runs = Length(styles) / 2;
		if (runs == 1)
		{
			RefVar style(GetArraySlotRef(styles, 1));
			if (!IsInkWord(style))
				return style;
			return styles;
		}
		if (runs > 0)
			return styles;
	}
	return GetDefaultViewStyle();
}


// ROM 0x001814b4 Tabs__14TParagraphViewFv
// The tabs slot (a variable of the context).
Ref
TParagraphView::Tabs(void)
{
	return GetVar(RSSYMtabs);
}


// ROM 0x001789bc GetDefaultViewStyle__14TParagraphViewFv
// The view's font: viewFont from the protos, or - for a read-only view -
// from the parents too, else the user's font preference.
Ref
TParagraphView::GetDefaultViewStyle(void)
{
	RefVar font(GetProto(RSSYMviewfont));
	if (ISNIL(font))
	{
		if (fFlags & vReadOnly)
			font = GetVar(RSSYMviewfont);
		if (ISNIL(font))
			font = GetPreference(RSSYMuserfont);
	}
	return font;
}


// ROM 0x00169460 GetInterLineSpacing__14TParagraphViewFv
// The line spacing to lay the lines out with: viewLineSpacing when the
// view has one - for a single style only when the font fits it (the
// font's height between eight tenths of the spacing and three more than
// it), else 0: each line takes the height its fonts need.  NOT YET
// RECONSTRUCTED: the ROM's check of every run's font against the spacing
// when the styles are runs (the spacing is used).
long
TParagraphView::GetInterLineSpacing(void)
{
	if (fLineSpacing != 0)
		return fLineSpacing;
	RefVar spacing(GetVar(RSSYMviewlinespacing));
	if (ISNIL(spacing))
		return 0;
	long lineSpacing = RINT(spacing);
	if (lineSpacing <= 0)
		return 0;
	RefVar styles(GetStyles());
	if (IsArray(styles))
		return lineSpacing;
	StyleRecord record;
	CreateTextStyleRecord(styles, &record);
	FontInfo fontInfo;
	GetStyleFontInfo(&record, &fontInfo);
	DisposeStyleRecord(&record);
	long height = fontInfo.ascent + fontInfo.descent;
	if (height <= lineSpacing + 3 && (lineSpacing * 8) / 10 <= height)
		return lineSpacing;
	return 0;
}


// ROM 0x00179f08 IsFontFrame__FRC6RefVar
// A font frame names a family; anything else in a style slot is not one.
Boolean
IsFontFrame(RefArg fontSpec)
{
	return IsFrame(fontSpec) && FrameHasSlot(fontSpec, RSSYMfamily);
}


// ROM 0x00179f58 CreateParagraphStyleRecord__FRC6RefVarP11StyleRecordUlT1
// The style record a run of a paragraph is laid out and drawn with.
//
// A packed integer or a font frame is a font as usual, except that a
// view whose text flags have bit 3 takes the default font for every run
// whatever the run says.  Anything else that is not an ink word falls
// back on the default font too.
//
// An ink word is not a font at all.  The record keeps the word itself
// where the family would be - OpenFont sees it there and opens the word
// as a font of one glyph - and takes the size and face a glyph made for
// the word answers, with the top bit of the face set to say that the
// word has a pen of its own.  A view whose text flags have bit 4 wants
// its writing laid out at the text's size instead, and then the size
// and the face come from the default font and the top bit is left
// alone.
void
CreateParagraphStyleRecord(RefArg fontSpec, StyleRecord* style, ULong textFlags,
						   RefArg defaultFont)
{
	RefVar deflt(defaultFont);
	RefVar spec;
	if (ISNIL(deflt))
		deflt = GetPreference(RSSYMuserfont);
	if (ISINT(fontSpec) || IsFontFrame(fontSpec))
		spec = (textFlags & 8) != 0 ? (Ref) deflt : (Ref) fontSpec;
	else if (!IsInkWord(fontSpec))
		spec = deflt;
	else
	{
		// the word measured as it would be drawn at its own size
		TInkWordGlyph word(fontSpec, (ULong) -1, (ULong) -1);
		style->fFontFamily = fontSpec;
		if ((textFlags & 0x10) == 0)
		{
			style->fFontSize = ToFixed((long) word.fFontSize);
			style->fFontFace = (long) word.fFace;
			style->fFontFace |= (long) 0x80000000;
		}
		else
		{
			StyleRecord text;
			CreateTextStyleRecord(deflt, &text);
			style->fFontSize = text.fFontSize;
			style->fFontFace = text.fFontFace;
			DisposeStyleRecord(&text);
		}
		style->fFontPattern = 0;
		style->fTransferMode = 0;
		style->fReserved14 = 0;
		style->fReserved18 = 0;
		style->fPattern = nil;
		return;
	}
	if (NOTNIL(spec))
		CreateTextStyleRecord(spec, style);
}


/*------------------------------------------------------------------------------
	T h e   c a c h e s
------------------------------------------------------------------------------*/

// ROM 0x0017c9cc GrowLineInfoCache__FPPP8LineInfol
// The line cache grown to hold more lines (the ROM's cache is a
// null-terminated array of LineInfo pointers; the host's an array of
// records that doubles).
void
GrowLineInfoCache(LineInfo** cache, long* capacity)
{
	long newCapacity = *capacity == 0 ? 8 : *capacity * 2;
	LineInfo* lines = (LineInfo*) NewPtrClear(newCapacity * sizeof(LineInfo));
	if (lines == nil)
		OutOfMemory();
	if (*cache != nil)
	{
		memmove(lines, *cache, *capacity * sizeof(LineInfo));
		DisposPtr((Ptr) *cache);
	}
	*cache = lines;
	*capacity = newCapacity;
}


// The style runs as records for the layout: one record per run of the
// styles array (a single spec: one run over the whole text); the last
// run stretched to the text's end.
void
TParagraphView::LayoutRuns(RefArg styles, long textLength)
{
	DisposeRuns();
	fRunSpecs = styles;
	if (IsArray(styles) && Length(styles) >= 2)
	{
		long count = Length(styles) / 2;
		fRunStyles = (StyleRecord**) NewPtrClear(count * sizeof(StyleRecord*));
		fRunLengths = (short*) NewPtrClear(count * sizeof(short));
		long covered = 0;
		for (long i = 0; i < count; i++)
		{
			RefVar spec(GetArraySlotRef(styles, 2 * i + 1));
			long runLength = RINT(GetArraySlotRef(styles, 2 * i));
			if (i == count - 1 && covered + runLength < textLength)
				runLength = textLength - covered;
			fRunStyles[i] = new StyleRecord;
			CreateParagraphStyleRecord(spec, fRunStyles[i], (ULong) TextFlags(),
									   RefVar(GetDefaultViewStyle()));
			fRunLengths[i] = (short) runLength;
			covered += runLength;
			fRunCount = i + 1;
		}
	}
	else
	{
		RefVar spec(IsArray(styles) ? GetDefaultViewStyle() : (Ref) styles);
		fRunStyles = (StyleRecord**) NewPtrClear(sizeof(StyleRecord*));
		fRunLengths = (short*) NewPtrClear(sizeof(short));
		fRunStyles[0] = new StyleRecord;
		CreateParagraphStyleRecord(spec, fRunStyles[0], (ULong) TextFlags(),
								   RefVar(GetDefaultViewStyle()));
		fRunLengths[0] = (short) textLength;
		fRunCount = 1;
	}
}


void
TParagraphView::DisposeRuns(void)
{
	for (long i = 0; i < fRunCount; i++)
	{
		DisposeStyleRecord(fRunStyles[i]);
		delete fRunStyles[i];
	}
	if (fRunStyles != nil)
		DisposPtr((Ptr) fRunStyles);
	if (fRunLengths != nil)
		DisposPtr((Ptr) fRunLengths);
	fRunStyles = nil;
	fRunLengths = nil;
	fRunCount = 0;
	fRunSpecs = NILREF;
}


// the runs of a range of the text: the records and lengths from the run
// the start falls in (the last run covers whatever is left)
static long
RunsOfRange(StyleRecord** runStyles, const short* runLengths, long runCount, long start, long length, StyleRecord** styles, short* lengths, long* firstRun)
{
	long run = 0;
	long runStart = 0;
	while (run < runCount - 1 && runStart + runLengths[run] <= start)
	{
		runStart += runLengths[run];
		run++;
	}
	*firstRun = run;
	long count = 0;
	long done = 0;
	long offset = start - runStart;
	while (done < length && run < runCount)
	{
		long available = runLengths[run] - offset;
		if (run == runCount - 1 || available > length - done)
			available = length - done;
		if (available > 0)
		{
			styles[count] = runStyles[run];
			lengths[count] = (short) available;
			count++;
			done += available;
		}
		offset = 0;
		run++;
	}
	return count;
}


// ROM 0x0016bbd0 ClearAllCaches__14TParagraphViewFv
// The style records, text objects and lines forgotten; the line cache
// sized for the text (the ROM: a line per 32 characters, at most a tenth
// of the screen's height; the host's cache grows as needed).
void
TParagraphView::ClearAllCaches(void)
{
	DisposeRuns();
	fLineCount = 0;
	if (fLines == nil)
		GrowLineInfoCache(&fLines, &fLineCapacity);
}


// ROM 0x0016c25c RefillAllCaches__14TParagraphViewFv
// The caches cleared and filled again: the style records from the styles
// (CreateStyleRecordCache), then the lines.
void
TParagraphView::RefillAllCaches(void)
{
	ClearAllCaches();
	RefVar text(Text());
	long textLength = ISNIL(text) ? 0 : (Length(text) - 2) >> 1;
	LayoutRuns(RefVar(GetStyles()), textLength);
	FillAllCaches();
}


// ROM 0x0016baa8 CreateAllCaches__14TParagraphViewFv
// The caches made: the lines laid out, the hilites' areas set up again
// (NOT YET RECONSTRUCTED: the hilites), the bounds noted.
void
TParagraphView::CreateAllCaches(void)
{
	RefillAllCaches();
	fCachedBounds = viewBounds;
	fCachesValid = true;
}


// ROM 0x0016bc38 FillAllCaches__14TParagraphViewFPs
// The text wrapped into the bounds a line at a time: each line the text
// up to a carriage return (or the end) cut to what fits the width and
// back to a word boundary, the line the height its runs' fonts need
// (or the inter-line spacing), one below the other from the top; a line
// whose midline falls below the bottom is not kept (TestLineOverlap)
// unless the view calculates its bounds.  The lines' union is the text
// bounds; the last line's height the line height.  A final carriage
// return leaves an empty line behind it, which is where the caret goes
// when it is typed.  The lines are moved down by the vertical text bits
// when the text is shorter than the bounds: centred, or to the bottom.
// NOT YET RECONSTRUCTED: the ROM's LineLoop (tabs, the text objects it
// makes for every run of a line, the parents' bounds narrowing the
// lines).
void
TParagraphView::FillAllCaches(void)
{
	fLineCount = 0;
	SetEmptyRect(&fTextBounds);
	RefVar textRef(Text());
	if (ISNIL(textRef) || fRunCount == 0)
		return;
	TRichString rich(textRef);
	long length = rich.Length();
	const UniChar* text = rich.GrabPtr();
	// The lines run from the view's left edge to its right - except for a
	// view that sizes itself to its text (bit 2 of the text flags), which
	// runs to its *parent's* right edge instead (the ROM's LineLoop
	// constructor, 0x0010d9d0).  That is what lets a word typed or written
	// on a page grow to the right as it is added to: the paragraph starts
	// as wide as the first character and would otherwise wrap every
	// character after it onto a line of its own, because FixupBBox can
	// only grow the view to the width its lines came out.
	long left = viewBounds.left;
	long right = (TextFlags() & 4) != 0 && fParent != nil
			   ? fParent->viewBounds.right : viewBounds.right;
	long width = right - left;
	long height = viewBounds.bottom - viewBounds.top;
	memset(&fTextOptions, 0, sizeof(fTextOptions));
	fTextOptions.fAlignment = ConvertToQDFlush(fViewJustify & vjJustifyMask, &fTextOptions.fJustification);
	fTextOptions.fWidth = ToFixed(width);
	fTextOptions.fTransferMode = fTransferMode;
	long spacing = GetInterLineSpacing();
	StyleRecord** lineStyles = (StyleRecord**) NewPtrClear(fRunCount * sizeof(StyleRecord*));
	short* lineLengths = (short*) NewPtrClear(fRunCount * sizeof(short));
	FPoint origin;
	origin.x = 0;
	origin.y = 0;
	long y = 0;
	long pos = 0;
	// A final carriage return leaves an empty line behind it: the
	// caret goes on that line, and a view that sizes itself to its
	// text grows by it.  (The ROM's LineLoop hands the empty line out
	// like any other; here it is one more turn of the loop with
	// nothing to fit in it.)
	Boolean trailingLine = length > 0 && text[length - 1] == kCR;
	while (pos < length || trailingLine)
	{
		if (pos >= length)
			trailingLine = false;
		long lineEnd = pos;
		while (lineEnd < length && text[lineEnd] != kCR)
			lineEnd++;
		long firstRun;
		long runs = RunsOfRange(fRunStyles, fRunLengths, fRunCount, pos, lineEnd - pos, lineStyles, lineLengths, &firstRun);
		TextBoundsInfo bounds;
		TextOptions options = fTextOptions;
		long fitted = lineEnd - pos;
		if (fitted > 0)
		{
			fitted = DoTextOnce(text + pos, lineEnd - pos, lineStyles, lineLengths, origin, &options, &bounds, false);
			if (fitted < lineEnd - pos)
			{
				if (fitted > 0 && text[pos + fitted - 1] != kSP && text[pos + fitted] != kSP)
				{
					ULong wordStart, wordEnd;
					FindWordBreaks(text + pos, length - pos, fitted, true, fLineBreakTable, &wordStart, &wordEnd);
					if (wordStart != 0)
						fitted = wordStart;
				}
				if (fitted == 0)
					fitted = 1;			// a word wider than the line: a character at a time
				runs = RunsOfRange(fRunStyles, fRunLengths, fRunCount, pos, fitted, lineStyles, lineLengths, &firstRun);
				DoTextOnce(text + pos, fitted, lineStyles, lineLengths, origin, &options, &bounds, false);
			}
		}
		else
			bounds.fWidth = 0;
		// the line's height from its runs' fonts (the default style's for an empty line)
		long ascent = 0;
		long descent = 0;
		long leading = 0;
		for (long i = 0; i < (runs > 0 ? runs : 1); i++)
		{
			FontInfo fontInfo;
			GetStyleFontInfo(runs > 0 ? lineStyles[i] : fRunStyles[firstRun], &fontInfo);
			if (fontInfo.ascent > ascent)
				ascent = fontInfo.ascent;
			if (fontInfo.descent > descent)
				descent = fontInfo.descent;
			if (fontInfo.leading > leading)
				leading = fontInfo.leading;
		}
		long lineHeight = spacing != 0 ? spacing : ascent + descent + leading;
		if (fLineCount == fLineCapacity)
			GrowLineInfoCache(&fLines, &fLineCapacity);
		// The line takes the spaces and the return that end it with it:
		// fEnd is where the next line starts, so an offset at the end of
		// a line is on that line and not at the start of the next one.
		const UniChar* next = SkipUpToTwoSpacesAndCR(text + pos + fitted, text + length);
		LineInfo& line = fLines[fLineCount];
		line.fStart = pos;
		line.fEnd = (long) (next - text);
		line.fTextEnd = pos + fitted;
		line.fFirstObj = firstRun;
		line.fEndObj = firstRun + runs;
		line.fEndsWithSpace = fitted > 0 && text[pos + fitted - 1] == kSP;
		line.fAscent = ascent;
		line.fHeight = lineHeight;
		SetRect(&line.fBounds, 0, (short) y, (short) ((bounds.fWidth + 0x8000) >> 16), (short) (y + lineHeight));
		Rect box;
		SetRect(&box, 0, 0, (short) width, (short) height);
		if (!fCalculateBounds && TestLineOverlap(box, line.fBounds) == 2)
			break;
		fLineCount++;
		fLineHeight = lineHeight;
		y += lineHeight;
		pos = (long) (next - text);
	}
	rich.ReleasePtr();
	DisposPtr((Ptr) lineStyles);
	DisposPtr((Ptr) lineLengths);
	// the lines placed in the view: by the vertical text bits when there is room
	long dy = 0;
	if (y < height)
	{
		switch (fViewJustify & vjVMask)
		{
		case vjCenterV:	dy = (height - y) / 2;	break;
		case vjBottomV:	dy = height - y;		break;
		default:		break;
		}
	}
	for (long i = 0; i < fLineCount; i++)
	{
		Rect& box = fLines[i].fBounds;
		OffsetRect(&box, viewBounds.left, viewBounds.top + dy);
		if (i == 0)
			fTextBounds = box;
		else
			UnionRect(&fTextBounds, &box, &fTextBounds);
	}
}


// ROM 0x0016991c OffsetCachedBounds__14TParagraphViewFR6TPoint
// The cached lines moved with the view.
void
TParagraphView::OffsetCachedBounds(Point& delta)
{
	if (delta.h == 0 && delta.v == 0)
		return;
	for (long i = 0; i < fLineCount; i++)
		OffsetRect(&fLines[i].fBounds, delta.h, delta.v);
	OffsetRect(&fTextBounds, delta.h, delta.v);
	OffsetRect(&fCachedBounds, delta.h, delta.v);
}


/*------------------------------------------------------------------------------
	T h e   c a r e t
------------------------------------------------------------------------------*/

// the text's characters
long
TParagraphView::TextLength(void)
{
	RefVar text(Text());
	return ISNIL(text) ? 0 : (Length(text) - 2) / 2;
}


// ROM 0x0017efd8 SetCaretOffset__14TParagraphViewFPlT1
// The caret offset kept: -1 or past the text means its end; the length
// cut to what is left.
void
TParagraphView::SetCaretOffset(long* offset, long* length)
{
	long textLength = TextLength();
	if (*offset == -1 || *offset > textLength)
		*offset = textLength;
	if (*offset + *length > textLength)
		*length = textLength - *offset;
	fCaretOffset = *offset;
}


// ROM 0x0017f050 GetSelection__14TParagraphViewFv
// A paragraph caret info frame ({offset, length}): the first hilite's
// range, or the caret offset with no length.
Ref
TParagraphView::GetSelection(void)
{
	RefVar info(Clone(RefVar(Rcanonicalparacaretinfo)));
	TParagraphHilite* hilite = HiliteOf(RefVar(FirstHilite()));
	long offset = fCaretOffset;
	long length = 0;
	if (hilite != nil)
	{
		offset = hilite->fStart;
		length = hilite->fEnd - offset;
	}
	SetFrameSlot(info, RSSYMoffset, RefVar(MAKEINT(offset)));
	SetFrameSlot(info, RSSYMlength, RefVar(MAKEINT(length)));
	return info;
}


// ROM 0x0018081c SetValue__14TParagraphViewFRC6RefVarT1
// A slot of the paragraph set.  The four that change what is drawn -
// text, styles, viewFont and textFlags - are written into the data frame
// (or the view's own field) and then RangeChanged, which lays the lines
// out again; without that the line cache still describes the text that
// was there and the view draws the wrong number of characters from the
// new one.  New text also drops the styles, the correction info and the
// hilites, and puts the caret at the end when this view holds it.  The
// rest is TView's, with the recogniser's area cache purged for the three
// slots that can change what a view takes in writing (the ROM inlines
// TView::SetValue here).
//
// NOT YET RECONSTRUCTED: a rich string (ink) as the text - the ROM makes
// the text and style slots out of it (TRichString::MakeParagraphTextSlot
// and MakeParagraphStylesSlot); and the vCalculateBounds paragraph whose text has just
// become empty, which asks its parent to remove it (an aeRemoveData
// command) unless the parent's text flags say not to.
void
TParagraphView::SetValue(RefArg slot, RefArg value)
{
	long wasLength = (Length(RefVar(Text())) - 2) / 2;
	Boolean relayout = false;
	if (EQRef(slot, RSSYMtext))
	{
		RefVar text(value);
		if (IsRichString(value))
			text = value;			// NOT YET: the ink taken apart into text and styles
		else
			RemoveSlot(RefVar(DataFrame()), RefVar(RSSYMstyles));
		RemoveCorrectionInfo(this);
		RemoveAllHilites();
		SetFrameSlot(RefVar(DataFrame()), RefVar(RSSYMtext), text);
		if (gRootView->fCaretView == this)
			gRootView->SetKeyView(this, 99999, 0, false);
		relayout = true;
	}
	else if (EQRef(slot, RSSYMstyles) || EQRef(slot, RSSYMviewfont))
	{
		RemoveSlot(RefVar(DataFrame()), RefVar(RSSYMstyles));
		SetFrameSlot(RefVar(DataFrame()), slot, value);
		relayout = true;
	}
	else if (EQRef(slot, RSSYMtextflags))
	{
		fTextFlags = GetInputViewTextFlags(ISINT(value) ? RINT(value) : 0, fFlags);
		relayout = true;
	}
	if (relayout)
	{
		long nowLength = (Length(RefVar(Text())) - 2) / 2;
		RangeChanged(0, wasLength, nowLength, slot);
		return;
	}
	if (EQRef(slot, RSSYMviewflags) || EQRef(slot, RSSYMrecconfig) || EQRef(slot, RSSYMdictionaries))
		PurgeAreaCache();		// what this view takes in writing has changed
	TView::SetValue(slot, value);
}


// ROM 0x0017f178 SetSelection__14TParagraphViewFRC6RefVarPlT2
// The selection restored from a caret info frame's offset and length (nil
// length: 0): SetCaretOffset, then no length removes the hilites, a
// length re-hilites the range (MakeHilite).  A nil frame means no
// selection - the hilites removed and the offset and length 0.  NOT YET
// RECONSTRUCTED: the edit view's hilite-view redirection.
void
TParagraphView::SetSelection(RefArg selection, long* offset, long* length)
{
	if (ISNIL(selection))
	{
		*offset = 0;
		*length = 0;
		RemoveAllHilites();
		return;
	}
	*offset = RINT(GetProtoVariable(selection, RSSYMoffset, nil));
	Ref len = GetProtoVariable(selection, RSSYMlength, nil);
	*length = ISNIL(len) ? 0 : RINT(len);
	SetCaretOffset(offset, length);
	if (*length == 0)
		RemoveAllHilites();
	else
		MakeHilite(*offset, *offset + *length, false);
}


// ROM 0x0017f2d8 ActivateSelection__14TParagraphViewFUc
// TView's (the viewCaretActivateScript); deactivating (losing the caret)
// removes the selection - of the hilite view when there is one, else our
// own.
void
TParagraphView::ActivateSelection(Boolean on)
{
	TView::ActivateSelection(on);
	if (on)
		return;
	TView* hiliteView = GetHiliteView();
	if (hiliteView == nil)
		hiliteView = this;
	hiliteView->RemoveAllHilites();
}


// ROM 0x00174c7c FlushWordAtCaret__14TParagraphViewFv
// NOT YET RECONSTRUCTED: the word being typed at the caret handed to the
// recogniser's dictionaries (the auto-add words).
void
TParagraphView::FlushWordAtCaret(void)
{ }


// ROM 0x001786f8 FindLineContainingCharOffset__14TParagraphViewFl
// The line the offset is on: the first whose end is past it, the last
// for an offset at or past the text's end; -1 without lines.
long
TParagraphView::FindLineContainingCharOffset(long offset)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	if (fLineCount == 0)
		return -1;
	for (long i = 0; i < fLineCount; i++)
		if (offset < fLines[i].fEnd)
			return i;
	return fLineCount - 1;
}


// the width of a line's characters from its start to the offset (host:
// measured; the ROM's text objects know their character boxes)
static long
LineWidthTo(TParagraphView* view, const UniChar* text, const LineInfo& line, long offset, StyleRecord** runStyles, const short* runLengths, long runCount)
{
	if (offset <= line.fStart)
		return 0;
	StyleRecord** styles = (StyleRecord**) NewPtrClear(runCount * sizeof(StyleRecord*));
	short* lengths = (short*) NewPtrClear(runCount * sizeof(short));
	long firstRun;
	RunsOfRange(runStyles, runLengths, runCount, line.fStart, offset - line.fStart, styles, lengths, &firstRun);
	TextOptions options;
	memset(&options, 0, sizeof(options));
	FPoint where;
	where.x = 0;
	where.y = 0;
	TextBoundsInfo info;
	MeasureTextOnce(text + line.fStart, offset - line.fStart, styles, lengths, where, &options, &info);
	DisposPtr((Ptr) styles);
	DisposPtr((Ptr) lengths);
	(void) view;
	return (info.fRight - info.fLeft) >> 16;
}


// ROM 0x00177f20 OffsetToBounds__14TParagraphViewFlP5TRect
// The box of the character at the offset (its left edge is what the
// caret wants, its width what an ink word's box is worked out from):
// the line found, the text up to the offset measured for the left and
// up to the next character for the right, the line's top and baseline
// for the top and bottom; without lines (no text) the view's top-left
// in the default style's height.
void
TParagraphView::OffsetToBounds(long offset, Rect* bounds)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	long textLength = TextLength();
	if (offset < 0)
		offset = 0;
	else if (offset > textLength)
		offset = textLength;
	long index = FindLineContainingCharOffset(offset);
	if (index < 0 || fLineCount == 0)
	{
		bounds->left = viewBounds.left;
		bounds->top = viewBounds.top;
		bounds->right = bounds->left;
		long height = fLineHeight != 0 ? fLineHeight : (fLineSpacing != 0 ? fLineSpacing : 12);
		bounds->bottom = bounds->top + height;
		return;
	}
	const LineInfo& line = fLines[index];
	RefVar textRef(Text());
	TRichString rich(textRef);
	const UniChar* text = rich.GrabPtr();
	long width = LineWidthTo(this, text, line, offset, fRunStyles, fRunLengths, fRunCount);
	bounds->left = line.fBounds.left + width;
	// the character's own right edge, which is where the next one
	// starts.  (The ROM ends in CharBounds over the line's text runs,
	// which answers the character's box; here the width of one more
	// character is measured instead.  The last character of a line and
	// the end of the text have no character after them, and the box is
	// then empty - which is what a caret wants.)
	bounds->right = bounds->left;
	if (offset < line.fTextEnd)
		bounds->right = line.fBounds.left
						+ LineWidthTo(this, text, line, offset + 1, fRunStyles, fRunLengths, fRunCount);
	rich.ReleasePtr();
	bounds->top = line.fBounds.top;
	bounds->bottom = line.fBounds.top + line.fAscent;		// the baseline
}


// ROM 0x00171ad4 OffsetToCaret__14TParagraphViewFlP5TRect
// Where the caret goes for the offset: the character's box (OffsetToBounds)
// - past the last line's end the caret stays at that end - its left a
// pixel in, kept inside the view's sides and its bottom (the baseline)
// inside the view unless it calculates its bounds; the rect is 2 wide.
// Nowhere (top and bottom -32768) when the offset is outside the cached
// range.
void
TParagraphView::OffsetToCaret(long offset, Rect* caret)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	long textLength = TextLength();
	if (offset < 0 || offset > textLength)
	{
		caret->top = -32768;		// nowhere (left and right not touched)
		caret->bottom = -32768;
		return;
	}
	if (fLineCount > 0)
	{
		long lastEnd = fLines[fLineCount - 1].fEnd;
		if (offset > lastEnd)
			offset = lastEnd;
	}
	OffsetToBounds(offset, caret);
	long left = caret->left - 1;
	if (left < viewBounds.left)
		left = viewBounds.left;
	if (left > viewBounds.right - 3)
		left = viewBounds.right - 3;
	caret->left = (short) left;
	caret->right = (short) (left + 2);
	if (!fCalculateBounds && caret->bottom > viewBounds.bottom)
		caret->bottom = viewBounds.bottom;
}


// ROM 0x00177520 PointToOffset__14TParagraphViewFRC6TPoint10MarginSizeUcP5TRectPP8LineInfoPlPUc
// The character offset for a point: the line whose box holds the point's
// v (the last for a point below, the first for one above), and within it
// the character boundary nearest the point's h (host: the prefixes
// measured; the ROM walks its text objects).
long
TParagraphView::PointToOffset(const Point& pt)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	if (fLineCount == 0)
		return 0;
	long index = fLineCount - 1;
	for (long i = 0; i < fLineCount; i++)
		if (pt.v < fLines[i].fBounds.bottom)
		{
			index = i;
			break;
		}
	const LineInfo& line = fLines[index];
	RefVar textRef(Text());
	TRichString rich(textRef);
	const UniChar* text = rich.GrabPtr();
	long best = line.fStart;
	long bestDistance = 0x7fffffff;
	long end = line.fTextEnd;
	if (end > line.fStart && line.fEndsWithSpace)
		end--;
	for (long offset = line.fStart; offset <= end; offset++)
	{
		long x = line.fBounds.left + LineWidthTo(this, text, line, offset, fRunStyles, fRunLengths, fRunCount);
		long distance = x > pt.h ? x - pt.h : pt.h - x;
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = offset;
		}
	}
	rich.ReleasePtr();
	return best;
}


// ROM 0x001716c8 PointToCaret__14TParagraphViewFR6TPointP5TRectT2
// The caret rect for a tap: OffsetToCaret(0) for an empty text, else the
// caret at the character nearest the point (PointToOffset; the ROM's
// PointToWordBoundary, and a point below the paragraph puts the caret
// on a new line when the view calculates its bounds - NOT YET).
void
TParagraphView::PointToCaret(Point& pt, Rect* caret, Rect* /*bounds*/)
{
	if (TextLength() == 0)
	{
		OffsetToCaret(0, caret);
		return;
	}
	OffsetToCaret(PointToOffset(pt), caret);
}


/*------------------------------------------------------------------------------
	E d i t i n g
------------------------------------------------------------------------------*/

// ROM 0x001791a8 GetStyleAtOffset__14TParagraphViewFlPlT2
Ref
TParagraphView::GetStyleAtOffset(long offset, long* run, long* offsetInRun)
{
	RefVar styles(GetStyles());
	return ::GetStyleAtOffset(styles, offset, run, offsetInRun);
}


// ROM 0x00178210 GetInkRefAndBounds__14TParagraphViewFlP5TRect
// The ink word at an offset and the box it is drawn in.  An ink word is
// its own style - the 'inkWord binary sits in the styles array where a
// font frame would - so the style at the offset is the ink itself when
// there is ink there.
//
// The character's box comes out of OffsetToBounds, which is the line's
// box: the word may be shorter than the line is tall, so the top is
// moved down to the baseline less the word's own ascent and the bottom
// to a word's height below that.
Ref
TParagraphView::GetInkRefAndBounds(long offset, Rect* bounds)
{
	RefVar style(GetStyleAtOffset(offset, nil, nil));
	if (NOTNIL(style) && IsInkWord(style))
	{
		InkWordInfo info;
		GetInkWordInfo(style, &info);
		long line = FindLineContainingCharOffset(offset);
		OffsetToBounds(offset, bounds);
		bounds->top = (short) (fLines[line].fAscent + bounds->top - info.fScaledAscent);
		bounds->bottom = (short) (bounds->top + info.fScaledHeight);
	}
	return style;
}


// ROM 0x001726a4 ExtractTextRange__14TParagraphViewFUlT1
// The characters from `offset` for `length` as a plain string; the range
// is kept inside the text, so asking beyond the end gives what there is.
Ref
TParagraphView::ExtractTextRange(ULong offset, ULong length)
{
	RefVar text(Text());
	const UniChar* chars = GetCString(text);
	ULong have = (ULong) Ustrlen(chars);
	if (have < offset)
		offset = have;
	if (have < offset + length)
		length = have - offset;
	RefVar result(AllocateBinary(RSSYMstring, (long) length * (long) sizeof(UniChar) + 2));
	UniChar* out = (UniChar*) BinaryData(result);
	BlockMove(chars + offset, out, (long) length * (long) sizeof(UniChar));
	out[length] = 0;
	return result;
}


// ROM 0x00180248 GetRangeText__14TParagraphViewFlT1
// What a script gets when it asks a paragraph for a range of its text.
// A paragraph with no style runs has nothing to carry but the
// characters; one that has them may have writing among them, so the text
// and the styles of the range are put together into a rich string
// (frames/RichString.h), which carries the ink with the characters.
Ref
TParagraphView::GetRangeText(long offset, long length)
{
	RefVar text(ExtractTextRange((ULong) offset, (ULong) length));
	if (!IsArray(RefVar(Styles())))
		return text;
	// (the range is pinned again here, because the styles have to be
	//  taken over the same characters the text was)
	RefVar all(Text());
	long have = Ustrlen(GetCString(all));
	if (have < offset)
		offset = have;
	if (have < offset + length)
		length = have - offset;
	RefVar styles(GetStylesOfRange(offset, length, false));
	return FMakeRichString(RefVar(NILREF), text, styles);
}


// ROM 0x001791f8 GetStylesOfRange__14TParagraphViewFlT1Uc
Ref
TParagraphView::GetStylesOfRange(long offset, long length, Boolean clone)
{
	RefVar styles(GetStyles());
	return ::GetStylesOfRange(styles, offset, length, clone);
}


// ROM 0x00179248 GetWriteableTextStylesArray__14TParagraphViewFv
// The styles slot as a runs array of the data frame's own: made from the
// single spec (one run over the text) when it is not one yet.
Ref
TParagraphView::GetWriteableTextStylesArray(void)
{
	RefVar styles(Styles());
	if (ISNIL(styles) || Length(styles) == 0)
	{
		RefVar spec(GetStyles());
		styles = MakeArray(2);
		RefVar data(DataFrame());
		SetFrameSlot(data, RSSYMstyles, styles);
		SetArraySlotRef(styles, 0, MAKEINT(TextLength()));
		SetArraySlot(styles, 1, spec);
	}
	return styles;
}


// ROM 0x00178748 GetStyleForInsertion__14TParagraphViewFlUcT2
// The style text inserted at the offset gets: vars.nextStyle when set (a
// style picked for what comes next; cleared by useNextStyle) - else,
// with style runs and text, the style of the last character before the
// offset (skipping white space when asked; not an ink word) - else,
// for an empty vCalculateBounds paragraph, the defaultFontSpec proto
// variable or the userFont preference - else the default view style.
Ref
TParagraphView::GetStyleForInsertion(long offset, Boolean useNextStyle, Boolean skipWhiteSpace)
{
	RefVar styles(Styles());
	long textLength = TextLength();
	RefVar text(Text());
	long before = offset - 1;
	if (before < 0)
		before = 0;
	Boolean calculates = (fFlags & vCalculateBounds) != 0;
	RefVar style(GetFrameSlotRef(gVarFrame, RSSYMnextstyle));
	RefVar defaultSpec(GetProto(RSSYMdefaultfontspec));
	if (ISNIL(style))
	{
		if (IsArray(styles) && textLength != 0)
		{
			const UniChar* chars = (const UniChar*) BinaryData(text);
			for ( ; before >= 0; before--)
			{
				UniChar c = chars[before];
				if (c != 0xf701 && (!skipWhiteSpace || !IsWhiteSpace(c)))
				{
					long run;
					style = GetStyleAtOffset(before, &run, nil);
					break;
				}
			}
			if (ISNIL(style) && calculates)
				style = GetPreference(RSSYMuserfont);
		}
		else if (textLength == 0 && calculates)
		{
			style = defaultSpec;
			if (ISNIL(style))
				style = GetPreference(RSSYMuserfont);
		}
	}
	else if (useNextStyle)
		SetFrameSlot(RefVar(gVarFrame), RSSYMnextstyle, RefVar(NILREF));
	if (ISNIL(style))
		style = GetDefaultViewStyle();
	return style;
}


// ROM 0x00178ed4 AdjustStyles__14TParagraphViewFlN21RC6RefVarT1
// The style runs follow a replacement: without new styles (or with
// nothing inserted) the removed characters leave their runs and the
// inserted ones join the run at the offset; with new styles a longer
// replacement grows the run before the range (a shorter one shrinks it)
// and then the new runs are laid over the inserted range
// (SetStyleOfRange, from the offset plus styleOffset).  The runs are
// compacted; a paragraph that does not take styles (textFlags 0x100) is
// left with them, else a single run that is the view's font drops the
// styles slot (a vCalculateBounds paragraph takes it as its viewFont).
void
TParagraphView::AdjustStyles(long offset, long removed, long inserted, RefArg styles, long styleOffset)
{
	RefVar runs(GetProto(RSSYMstyles));
	if (NOTNIL(runs))
	{
		long n = removed > 0 ? inserted : removed;
		if (n < 1 || ISNIL(styles))
		{
			RunsDelete(runs, offset, removed);
			RunsInsert(runs, offset, inserted);
		}
		else
		{
			long delta = inserted - removed;
			if (delta > 0)
				RunsInsert(runs, offset + removed - 1, delta);
			else if (delta < 0)
				RunsDelete(runs, offset + inserted, -delta);
		}
	}
	if (NOTNIL(styles) && inserted > 0)
	{
		runs = GetWriteableTextStylesArray();
		long count = Length(styles) / 2;
		long end = offset + styleOffset + inserted;
		long start = offset + styleOffset;
		for (long i = 0; i < count; i++)
		{
			long runEnd = start + RINT(GetArraySlotRef(styles, i * 2));
			if (runEnd > end)
				runEnd = end;
			RefVar spec(GetArraySlotRef(styles, i * 2 + 1));
			SetStyleOfRange(runs, spec, start, runEnd);
			if (runEnd == end)
				break;
			start = runEnd;
		}
	}
	if (NOTNIL(runs))
	{
		CompactStyleRuns(runs);
		if ((TextFlags() & 0x100) == 0)
		{
			RefVar data(DataFrame());
			Boolean calculates = (fFlags & vCalculateBounds) != 0;
			if (Length(runs) == 2)
			{
				RefVar spec(GetArraySlotRef(runs, 1));
				if (!IsInkWord(spec))
				{
					if (calculates)
					{
						RemoveSlot(data, RSSYMstyles);
						SetFrameSlot(data, RSSYMviewfont, spec);
					}
					else if (EQRef(GetProto(RSSYMviewfont), spec))
						RemoveSlot(data, RSSYMstyles);
				}
				else if (calculates)
					RemoveSlot(data, RSSYMviewfont);
			}
			else if (calculates)
				RemoveSlot(data, RSSYMviewfont);
		}
	}
}


// ROM 0x0016a824 AdjustHilites__14TParagraphViewFlT1
// NOT YET RECONSTRUCTED: the hilites moved past a replacement.
void
TParagraphView::AdjustHilites(long /*offset*/, long /*delta*/)
{ }


// host: the region covering the characters a hilite selects - the union,
// over the lines the hilite touches, of the box from the first selected
// character's left edge to the last's (or the line's right when the
// selection runs on past it).  ==> whether it is non-empty.
//
// The lines are laid out in the port's coordinates, so the region is
// moved into the view's own at the end, which is where a hilite keeps
// its area and its bounding box (the ROM's TParagraphView::Area
// 0x0016a92c ends with the same OffsetRgn).  Everything that looks at a
// hilite - TView::RemoveHilite's dirty rectangle, GlobalHiliteBounds,
// TEditView::ScrubHilite - works in those coordinates, so the drawing is
// the one place that has to move it back.
Boolean
TParagraphView::SelectionRegion(long start, long end, RgnHandle rgn)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	SetEmptyRgn(rgn);
	if (end <= start)
		return false;
	for (long i = 0; i < fLineCount; i++)
	{
		const LineInfo& line = fLines[i];
		long selStart = start > line.fStart ? start : line.fStart;
		long selEnd = end < line.fTextEnd ? end : line.fTextEnd;
		if (selEnd <= selStart)
			continue;
		Rect leftBox;
		OffsetToBounds(selStart, &leftBox);
		long right;
		if (end >= line.fEnd)
			right = line.fBounds.right;
		else
		{
			Rect rightBox;
			OffsetToBounds(selEnd, &rightBox);
			right = rightBox.left;
		}
		Rect box;
		box.left = leftBox.left;
		box.top = line.fBounds.top;
		box.right = (short) right;
		box.bottom = line.fBounds.bottom;
		if (box.right > box.left)
		{
			TRegionVar lineRgn;
			RectRgn(lineRgn, &box);
			UnionRgn(rgn, lineRgn, rgn);
		}
	}
	OffsetRgn(rgn, -viewBounds.left, -viewBounds.top);
	return !EmptyRgn(rgn);
}


// ROM 0x0016aecc DrawHilites__14TParagraphViewFUc
// The hilited text inverted (the ROM fills each hilite's region into
// offscreen bits and XORs them onto the view - PostDraw 0x0016cc84; the
// host inverts the region over the current port directly, the same on one
// bit).  Nothing when the hilites are being suppressed (gDontDrawHilites)
// or a scaled draw is asked for.
void
TParagraphView::DrawHilites(Boolean scaled)
{
	if (scaled || gDontDrawHilites)
		return;
	HiliteLoop loop(this);
	while (loop.Next())
	{
		TParagraphHilite* hilite = (TParagraphHilite*) loop.fCurrent;
		if (hilite == nil)
			continue;
		SetupArea(hilite);
		TRegionVar rgn;
		hilite->Area(rgn);
		OffsetRgn(rgn, viewBounds.left, viewBounds.top);		// the area is the view's own; the port is drawn in
		if (!EmptyRgn(rgn))
			InvertRgn(rgn);
	}
}


// ROM 0x0016a744 SetupArea__14TParagraphViewFP16TParagraphHilite
// The region a hilite covers, worked out once and kept in it - the
// characters are laid out in lines, so only the paragraph can say - and
// the bounding box that goes with it.
void
TParagraphView::SetupArea(TParagraphHilite* hilite)
{
	if (hilite == nil || hilite->HasArea())
		return;
	TRegionVar rgn;
	SelectionRegion(hilite->fStart, hilite->fEnd, rgn);
	hilite->SetArea(rgn);
}


// ROM 0x00169b0c HandleHilite__14TParagraphViewFP11TUnitPubliclUc
// What a hilite stroke over a paragraph selects, in the order the four
// kinds are tried: the words it runs through (6), the whole paragraph
// (1), whole lines (2), or a range of characters (3).  A kind other than
// -1 asks only about that one.  ==> the kind taken, or 0.
long
TParagraphView::HandleHilite(TUnitPublic* unit, long kind, Boolean reallyDoIt)
{
	if (fLines == nil)
		CreateAllCaches();
	Rect box;
	unit->Bounds(&box);
	if (Overlaps(&viewBounds, &box))
	{
		if ((kind == 6 || kind == -1) && HiliteWords(unit, reallyDoIt))
			return 6;
		if ((kind == 1 || kind == -1) && HiliteParagraph(unit, reallyDoIt))
			return 1;
		if ((kind == 2 || kind == -1) && HiliteLines(unit, reallyDoIt))
			return 2;
		if ((kind == 3 || kind == -1) && HiliteRange(unit, reallyDoIt))
			return 3;
	}
	return 0;
}


// ROM 0x0016a400 HiliteWords__14TParagraphViewFP11TUnitPublicUc
// Kind 6, the words the stroke runs through - which the ROM does not do:
// the function is `mov r0,#0; mov pc,lr`, so nothing ever takes a 6 and
// the next kind is always tried.  (Kept as the ROM has it.)
Boolean
TParagraphView::HiliteWords(TUnitPublic* /*unit*/, Boolean /*reallyDoIt*/)
{
	return false;
}


// ROM 0x00169c0c HiliteParagraph__14TParagraphViewFP11TUnitPublicUc
// Kind 1, the whole paragraph: the stroke has to cover more than 60 per
// cent of the text's box.  A stroke at least twice as tall as it is
// wide, drawn between the view's left and right edges, is judged by its
// vertical extent alone - which is how a line drawn down the margin
// takes the paragraph.  An empty paragraph takes nothing.
Boolean
TParagraphView::HiliteParagraph(TUnitPublic* unit, Boolean reallyDoIt)
{
	if ((Length(RefVar(Text())) - 2) / 2 == 0)
		return false;
	Rect box;
	unit->Bounds(&box);
	Rect text = fCachedBounds;
	Rect mine = viewBounds;
	Boolean covered = CoveredBy(&text, &box) > 60;
	if (!covered)
	{
		if (box.bottom - box.top < 2 * (box.right - box.left)
			|| box.left < mine.left || box.right > mine.right)
			return false;
		Rect flat = box;
		flat.left = 0;
		flat.right = 1;
		Rect flatText = text;
		flatText.left = 0;
		flatText.right = 1;
		covered = CoveredBy(&flatText, &flat) > 60;
	}
	if (covered && reallyDoIt)
		HiliteAll();
	return covered;
}


// ROM 0x00169da0 HiliteLines__14TParagraphViewFP11TUnitPublicUc
// Kind 2, whole lines: a stroke at least twice as tall as it is wide
// that covers half of each of a run of lines takes them all.  It is
// judged by its vertical extent alone, so it does not matter where
// across the paragraph it was drawn; the run ends at the first line the
// stroke does not cover.
Boolean
TParagraphView::HiliteLines(TUnitPublic* unit, Boolean reallyDoIt)
{
	Rect box;
	unit->Bounds(&box);
	if (box.bottom - box.top < 2 * (box.right - box.left))
		return false;
	long start = -1;
	long end = -1;
	box.left = 0;
	box.right = 1;
	for (long i = 0; i < fLineCount; i++)
	{
		Rect line = Line(i).fBounds;
		line.left = 0;
		line.right = 1;
		if (CoveredBy(&line, &box) >= 50)
		{
			if (start == -1)
				start = Line(i).fStart;
			end = Line(i).fEnd;
		}
		else if (start != -1)
			break;
	}
	if (start == -1)
		return false;
	if (reallyDoIt)
		MakeHilite(start, end, true);
	return true;
}


// ROM 0x00169fbc FindFirstWordHitByHilite__14TParagraphViewFP6TPointl6TPointUc
// The first word boundary the hilite stroke's outline runs through,
// walking the polygon's points from one end - or, for `fromEnd`, from
// the other - and stepping a pixel at a time along any segment that
// moves more than one pixel across.  Each point is offset by the
// polygon's top left, which is what puts it in the view's coordinates.
// ==> the character offset, or -1.
long
TParagraphView::FindFirstWordHitByHilite(const Point* points, long count, Point offset, Boolean fromEnd)
{
	const Point* p = fromEnd ? points + count - 1 : points;
	Point cur;
	cur.h = (short) (p->h + offset.h);
	cur.v = (short) (p->v + offset.v);
	long found = -1;
	for (long i = 0; i < count; i++)
	{
		Point next;
		next.h = (short) (p->h + offset.h);
		next.v = (short) (p->v + offset.v);
		long dh = next.h - cur.h;
		if (dh > 1 || dh < -1)
		{
			long steps = dh < 0 ? -dh : dh;
			short hStep = (short) (dh > 0 ? 1 : -1);
			short vStep = (short) ((next.v - cur.v) / steps);
			Point walk;
			walk.h = (short) (cur.h + hStep);
			walk.v = (short) (cur.v + vStep);
			for (long j = 0; j < steps; j++)
			{
				found = PointToWordBoundary(walk, -50, nil);
				if (found >= 0)
					break;
				walk.v = (short) (walk.v + vStep);
				walk.h = (short) (walk.h + hStep);
			}
			if (found >= 0)
				return found;
		}
		found = PointToWordBoundary(next, -50, nil);
		if (found >= 0)
			return found;
		p = fromEnd ? p - 1 : p + 1;
		cur = next;
	}
	return found;
}


// ROM 0x00169ec4 HiliteRange__14TParagraphViewFP11TUnitPublicUc
// Kind 3, a range of characters: the stroke's rough outline is walked
// from each end for the first word boundary it runs through, and
// everything between the two is selected.  The two ends finding the same
// boundary - or either finding none - means the stroke crossed no text.
Boolean
TParagraphView::HiliteRange(TUnitPublic* unit, Boolean reallyDoIt)
{
	Rect box;
	unit->Bounds(&box);
	if (!Overlaps(&viewBounds, &box))
		return false;
	Handle shape = unit->RoughShape();
	if (shape == nil)
		return false;
	Polygon* poly = (Polygon*) *shape;
	Point* points = poly->polyPoints;
	long count = PolyPointCount(poly);
	Point offset;
	offset.v = poly->polyBBox.top;
	offset.h = poly->polyBBox.left;
	long first = FindFirstWordHitByHilite(points, count, offset, false);
	if (first < 0)
		return false;
	long last = FindFirstWordHitByHilite(points, count, offset, true);
	if (last < 0)
		first = last;
	if (first == last)
		return false;
	if (reallyDoIt)
	{
		long from = first;
		long to = last;
		if (from > to)
		{
			from = last;
			to = first;
		}
		MakeHilite(from, to, true);
	}
	return true;
}


// ROM 0x00169d8c HiliteAll__14TParagraphViewFv
// The whole of the text selected.  The ROM asks for characters 0 to
// 60000 and lets MakeHilite cut the end back to the text's length.
void
TParagraphView::HiliteAll(void)
{
	MakeHilite(0, 0xea60, true);
}


// ROM 0x0016a49c MakeHilite__14TParagraphViewFlT1Uc
// The characters between the offsets selected: clamped to the text (a
// start after the end is brought back to it), and unioned with the
// existing selection (which is removed first) so a drag extends it; an
// empty range with `interactive` just moves the caret.  Else a hilite is
// added through aeAddHilite, carried bare when `interactive` - and then
// the command's handler makes the range the key view, so the caret goes
// off and DrawHilites paints it - and wrapped in a frame's `hilite` slot
// when not (SetSelection putting back a saved selection, which leaves
// the key view alone).  Either way the change is time-stamped and the
// style palette brought up to date.  (DEVIATION: the ROM's
// TParagraphHilite also carries a copy of the selected text.)
void
TParagraphView::MakeHilite(long start, long end, Boolean interactive)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	long length = TextLength();
	if (end > length)
		end = length;
	if (start < 0)
		start = 0;
	if (end <= start)
		start = end;
	RefVar firstRef(FirstHilite());
	TParagraphHilite* first = HiliteOf(firstRef);
	if (first != nil)
	{
		if (first->fStart < start)
			start = first->fStart;
		if (first->fEnd > end)
			end = first->fEnd;
		RemoveHilite(firstRef);
	}
	if (end - start == 0 && interactive)
	{
		long at = start < 0 ? 0 : start;
		if (at > length)
			at = length;
		gRootView->SetKeyView(this, at, 0, false);
	}
	else
	{
		TParagraphHilite* hilite = new TParagraphHilite(start, end);
		if (hilite == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		RefVar cmd(MakeCommand(aeAddHilite, this, 0x8000000));
		if (!interactive)
		{
			RefVar wrapped(AllocateFrame());
			SetFrameSlot(wrapped, RSSYMhilite, RefVar(AddressToRef(hilite)));
			CommandSetFrameParameter(cmd, wrapped);
		}
		else
			CommandSetFrameParameter(cmd, RefVar(AddressToRef(hilite)));
		SetupArea(hilite);
		gApplication->DispatchCommand(cmd);
	}
	TimeStampHiliteChange(this);
	UpdateStylePalette();
}


// ROM 0x0016a408 TimeStampHiliteChange__FP5TView
// The selection has changed in the view: lastTextHiliteChanged is it,
// and lastTextChanged is cleared.
void
TimeStampHiliteChange(TView* view)
{
	SetFrameSlot(RefVar(gVarFrame), RSSYMlasttexthilitechanged, view->fContext);
	SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
}


// ROM 0x0017b108 UpdateStylePalette__Fv
// The style palette, if it is open, told to show the new selection's
// styles (its SyncButtons).
void
UpdateStylePalette(void)
{
	RefVar palette(gRootView->GetVar(RSSYMstylepalette));
	TView* view = GetView(palette);
	if (view != nil && (view->fFlags & vVisible) != 0)
		DoMessage(palette, RSSYMsyncbuttons, RefVar(NILREF));
}


// ROM 0x00179464 ChangeStylesOfRange__14TParagraphViewFlT1RC6RefVarUc
// The characters from `start` for `length` given a style.  This is what
// the Styles slip does to a selection, and it is done as a replacement
// of the range by itself: the styles of the range are taken, each run's
// spec is merged with the one asked for, and the text and the new styles
// go through the ordinary aeReplaceText command - so the change lands in
// the undo stack with everything else, and the caret and the correction
// information follow it.
//
// `style` may be:
//  - nil, which means the user's font preference;
//  - a packed font integer, opened out into a font-parameter frame;
//  - a frame with a `fontParms` slot, which is how the style slip sends
//    a face to be *changed* rather than set: its `command` slot is 1 to
//    add the face bits, 2 to take them away, and 3 to toggle - and a
//    toggle makes up its mind on the first run of the range, so the
//    whole selection ends up the same way round;
//  - any other frame, which is a set of font parameters as it stands.
void
TParagraphView::ChangeStylesOfRange(long start, long length, RefArg style, Boolean redraw)
{
	RefVar spec(style);
	if (ISNIL(spec))
		spec = GetPreference(RSSYMuserfont);
	RefVar fontParms;
	long command = 0;
	if (ISINT(spec))
		spec = IntFontToFontParms(spec);
	else if (FrameHasSlot(spec, RSSYMfontparms))
	{
		RefVar which(GetFrameSlotRef(spec, RSSYMcommand));
		if (ISINT(which))
			command = RVALUE(which);
		fontParms = GetFrameSlotRef(spec, RSSYMfontparms);
		spec = Clone(fontParms);
	}

	// the runs over the range, [length, style, length, style, ...],
	// each style put through the change
	RefVar styles(GetStylesOfRange(start, length, true));
	long runs = Length(styles) / 2;
	for (long i = 0; i < runs; i++)
	{
		long at = i * 2 + 1;
		RefVar one(GetArraySlotRef(styles, at));
		if (command != 0)
		{
			RefVar wanted(GetFrameSlotRef(fontParms, RSSYMface));
			if (NOTNIL(wanted))
			{
				long bits = RINT(wanted);
				long have = GetFontFace(one);
				if (i == 0 && command == 3)
					command = (have & bits) == bits ? 2 : 1;
				if (command == 1)
					bits = have | bits;
				else if (command == 2)
					bits = have & ~bits;
				SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(bits)));
			}
		}
		SetArraySlot(styles, at, RefVar(SetFontParms(one, spec)));
	}

	// the range replaced by itself
	RefVar text(AllocateBinary(RSSYMtext, length * (long) sizeof(UniChar)));
	RefVar oldText(Text());
	BlockMove(GetCString(oldText) + start, BinaryData(text),
			  length * (long) sizeof(UniChar));
	RemoveAllHilites();

	RefVar cmd(MakeCommand(aeReplaceText, this, fId));
	CommandSetText(cmd, text);
	RefVar params(AllocateArray(RSSYMarray, 7));
	SetArraySlot(params, 0, RefVar(MAKEINT(start)));
	SetArraySlot(params, 1, RefVar(MAKEINT(length)));
	SetArraySlot(params, 2, RefVar(MAKEINT(length)));
	SetArraySlot(params, 3, RefVar(MAKEINT(0)));
	SetArraySlot(params, 4, RefVar(MAKEINT(redraw)));
	SetArraySlot(params, 5, RefVar(MAKEINT(0)));
	SetArraySlot(params, 6, RefVar(MAKEINT(0)));
	SetFrameSlot(cmd, RSSYMparams, params);

	RefVar correctInfo(ExtractRange(RefVar(CorrectInfo()), this, start, length));
	RefVar frame(styles);
	if (NOTNIL(correctInfo))
	{
		frame = Clone(RefVar(Rcanonicalcorrectinfo));
		SetFrameSlot(frame, RSSYMstyles, styles);
		SetFrameSlot(frame, RSSYMcorrectinfo, correctInfo);
	}
	CommandSetFrameParameter(cmd, frame);

	Rect was = viewBounds;
	HiliteText(start, length, true);
	HandleReplaceText(cmd);
	// a paragraph that changed size inside a view which lays its children
	// out has to have the parent redrawn as well
	if (!EqualRect(&was, &viewBounds) && (fFlags & vCalculateBounds) != 0)
		fParent->Dirty(nil);
}


// ROM 0x00179a68 ChangeStyleOfSelection__14TParagraphViewFRC6RefVar
// The selected text (the first hilite's range) restyled.
void
TParagraphView::ChangeStyleOfSelection(RefArg style)
{
	TParagraphHilite* hilite = HiliteOf(RefVar(FirstHilite()));
	if (hilite == nil)
		return;
	ChangeStylesOfRange(hilite->fStart, hilite->fEnd - hilite->fStart, style, true);
}


static const UniChar kScanInkChar = 0xf701;		// the ink-word placeholder the word scan treats as its own kind (RichString.h's kInkChar is 0xf700)


// ROM 0x001a1250 ScanWordStart__FPUslT2
// The start of the word around offset: back while the characters are of
// the same kind (all ink or all not) and not white space, no further than
// limit.
long
ScanWordStart(const UniChar* text, long offset, long limit)
{
	Boolean startInk = text[offset] == kScanInkChar;
	while (offset >= limit)
	{
		UniChar c = text[offset];
		if (IsWhiteSpace(c) || (c == kScanInkChar) != startInk)
			break;
		offset--;
	}
	return offset + 1;
}


// ROM 0x001a1134 ScanWordEnd__FPUslT2
// The end of the word around offset (the character after it): forward
// while the characters are of the same kind and not white space, no
// further than limit.
long
ScanWordEnd(const UniChar* text, long offset, long limit)
{
	Boolean startInk = text[offset] == kScanInkChar;
	while (offset < limit)
	{
		UniChar c = text[offset];
		if (IsWhiteSpace(c) || (c == kScanInkChar) != startInk)
			break;
		offset++;
	}
	return offset;
}


// ROM 0x001a1398 ScanNextWord__FPUslT2
// The start of the next word: forward over white space, no further than
// limit (which it answers when there is no word left).
long
ScanNextWord(const UniChar* text, long offset, long limit)
{
	while (offset < limit && IsWhiteSpace(text[offset]))
		offset++;
	return offset;
}


// ROM 0x001a1484 ScanPrevWordEnd__FPUslT2
// The end of the word before offset (the character after it): back over
// white space, no further than limit.  -1 when there is nothing but
// white space back to the limit.
long
ScanPrevWordEnd(const UniChar* text, long offset, long limit)
{
	while (IsWhiteSpace(text[offset]) && limit < offset)
		offset--;
	if (offset == limit && IsWhiteSpace(text[offset]))
		return -1;
	return offset + 1;
}


// The word around a point selected (a double tap): the character under
// the point found (PointToOffset), the word scanned around it, and, when
// it is not empty, hilited.  ==> whether a word was selected.
Boolean
TParagraphView::SelectWordAt(Point pt)
{
	if (fFlags & (vReadOnly | vWriteProtected))
		return false;
	long offset = PointToOffset(pt);
	if (offset < 0)
		return false;
	long length = TextLength();
	if (offset >= length)
		return false;
	RefVar textRef(Text());
	if (ISNIL(textRef))
		return false;
	TRichString rich(textRef);
	const UniChar* text = rich.GrabPtr();
	long start = ScanWordStart(text, offset, 0);
	long end = ScanWordEnd(text, offset, length);
	rich.ReleasePtr();
	if (end <= start)
		return false;
	MakeHilite(start, end, true);
	return true;
}


// ROM 0x00177cbc FindWordOffset__14TParagraphViewF6TPointPlP6TPoint
// The word under a point: where it starts in the text, where its first
// character sits on the screen, and how long it is.  ==> 0 when there is
// no word there - past the end of a line, or on a space.
long
TParagraphView::FindWordOffset(Point pt, long* offset, Point* where)
{
	if (fLines == nil)
		CreateAllCaches();
	long start, end;
	// (the ROM also refuses a tab, which it knows from the text object
	//  the point is in; here the whole line is one run)
	if (!PointToWord(pt, &start, &end, nil))
		return 0;
	RefVar textRef(Text());
	if (ISNIL(textRef))
		return 0;
	TRichString rich(textRef);
	const UniChar* text = rich.GrabPtr();
	UniChar first = text[start];
	rich.ReleasePtr();
	if (first == ' ')
		return 0;
	// (the ROM asks OffsetInRunToBounds, which is the same box for a
	//  paragraph whose line is one run)
	Rect bounds;
	OffsetToBounds(start, &bounds);
	*offset = start;
	where->h = bounds.left;
	where->v = bounds.top;
	return end - start;
}


// ROM 0x00171344 HitsHilitedInkWord__FP5TView6TPoint
// Whether the point is on an ink word inside the view's selection - a
// word of writing that has been selected and tapped again, which is
// asking for it to be read rather than corrected.
Boolean
HitsHilitedInkWord(TView* view, Point pt)
{
	if (!view->DerivedFrom(clParagraphView))
		return false;
	TParagraphView* para = (TParagraphView*) view;
	RefVar hiliteRef(para->FirstHilite());
	if (ISNIL(hiliteRef))
		return false;
	TParagraphHilite* hilite = HiliteOf(hiliteRef);
	if (hilite == nil)
		return false;
	RefVar textRef(para->Text());
	TRichString rich(textRef);
	const UniChar* text = rich.GrabPtr();
	Boolean hit = false;
	for (long offset = hilite->fStart; offset < hilite->fEnd; offset++)
	{
		RefVar style(para->GetStyleAtOffset(offset, nil, nil));
		if (text[offset] != kInkWordChar || !IsInkWord(style))
			continue;
		Rect box, next;
		para->OffsetToBounds(offset, &box);
		para->OffsetToBounds(offset + 1, &next);
		box.right = next.left;
		if (PtInRect(pt, &box))
		{
			hit = true;
			break;
		}
	}
	rich.ReleasePtr();
	return hit;
}


// ROM 0x001752c4 HandleTap__14TParagraphViewFR6TPoint
// The caret placed at the tapped point: the selection removed, the
// character nearest the point found (PointToOffset; before the first line
// goes to the start, past the text to its end), the key view set there.
// NOT YET RECONSTRUCTED: FClicker (the tap sound).
void
TParagraphView::HandleTap(Point& pt)
{
	RemoveAllHilites();
	if (fFlags & (vReadOnly | vWriteProtected))
		return;
	long offset = PointToOffset(pt);
	if (offset < 0)
	{
		if (pt.v < fCachedBounds.top)
			offset = 0;
		else
			offset = TextLength();
	}
	gRootView->SetKeyView(this, offset, 0, true);
}


// ROM 0x0017e964 Idle__14TParagraphViewFl
// TView's idle; reason 2 is the deferred single tap - the caret placed at
// the point kept when the tap came, once the double-tap interval has
// passed with no second tap.  NOT YET RECONSTRUCTED: reason 1's expiry of
// the just-typed word runs (the ink recogniser).
long
TParagraphView::Idle(long reason)
{
	long delay = TView::Idle(reason);
	if (reason == 2 && fTapped)
	{
		HandleTap(fTapPoint);
		fTapped = false;
	}
	return delay;
}


// host: whether the point falls within the selected text (the ROM
// TView::PointInHilite 0x0026051c iterates the hilites, asking each
// Encloses).  The point comes in the port's coordinates and a hilite's
// area is the view's own, so it is moved first.
Boolean
TParagraphView::PointInHilite(Point& pt)
{
	Point local = pt;
	local.h = (short) (local.h - viewBounds.left);
	local.v = (short) (local.v - viewBounds.top);
	HiliteLoop loop(this);
	while (loop.Next())
	{
		TParagraphHilite* hilite = (TParagraphHilite*) loop.fCurrent;
		if (hilite == nil)
			continue;
		SetupArea(hilite);
		if (hilite->Encloses(local))
			return true;
	}
	return false;
}


// ROM 0x00180ce4 ProcessStyles__14TParagraphViewFUc
// The styles checked for ink words to recognise (CheckStyles; the
// recogniser then runs over the text).  NOT YET RECONSTRUCTED: ink -
// nothing to process.  ==> whether anything was.
Boolean
TParagraphView::ProcessStyles(Boolean /*redraw*/)
{
	return false;
}


// ROM 0x001815b8 FixupBBox__14TParagraphViewFv
// The lines laid out again; a paragraph that calculates its bounds takes
// the text's height (at least a line) - and, one line only, its width
// (at least 5 wide) - as its bounds, written to its viewBounds slot and
// the parent told (ChildBoundsChanged).  NOT YET: the hilites' areas
// remade (UpdateHiliteArea).
void
TParagraphView::FixupBBox(void)
{
	if (!fCachesValid)
		return;
	RefillAllCaches();
	if ((fFlags & vCalculateBounds) && fCalculateBounds)
	{
		Rect bounds = viewBounds;
		if (fViewJustify & vjOneLineOnly)
		{
			long right = bounds.left + 5;
			if (right < fTextBounds.right)
				right = fTextBounds.right;
			bounds.right = (short) right;
		}
		long bottom = bounds.top + fLineHeight;
		if (bottom < fTextBounds.bottom)
			bottom = fTextBounds.bottom;
		bounds.bottom = (short) bottom;
		if (TextFlags() & 4)
			bounds.right = fTextBounds.right;
		if (!EqualRect(&viewBounds, &bounds))
		{
			// the new bounds are in the parent's contents coordinates
			// before they are written, because that is what the view's
			// viewBounds slot holds; writing the global ones straight
			// through SetBounds would add the parent's scroll origin to
			// them a second time, and a paragraph on a scrolled page
			// would walk down it a line at a time as it was typed into
			Rect was = viewBounds;
			fCachesValid = false;
			Point origin = fParent->ContentsOrigin();
			OffsetRect(&bounds, -origin.h, -origin.v);
			WriteBounds(bounds);
			fParent->ChildBoundsChanged(this, was);
			fCachesValid = true;
		}
	}
}


// ROM 0x00180bd8 RangeChanged__14TParagraphViewFlN21RC6RefVar
// The text changed: the lines laid out again (FixupBBox); once set up
// the styles are processed (ink words recognised) and the view is told
// Changed(slot) ('text when none was given and the styles did change)
// - before that the view is only dirtied.
void
TParagraphView::RangeChanged(long /*offset*/, long /*removed*/, long /*inserted*/, RefArg slot)
{
	if (fCachesValid)
		FixupBBox();
	if (!fSetupDone)
	{
		Dirty(nil);
		return;
	}
	fSetupDone = false;
	Boolean changed = ProcessStyles(true);
	fSetupDone = true;
	RefVar what(slot);
	if (ISNIL(what))
	{
		if (!changed)
			return;
		what = RSSYMtext;
	}
	Changed(what);
}


/*------------------------------------------------------------------------------
	S c r u b b i n g
------------------------------------------------------------------------------*/

// The view the recogniser last put a word into, when it did so and
// where the word ended.  Nothing writes them yet - HandleWord is NOT
// YET - so the guard they serve in ScrubLines never fires.
TView*	gLastAddedWordView = nil;			// ROM 0x0c101714 gLastAddedWordView
ULong	gLastAddedWordAddTime = 0;			// ROM 0x0c101724 gLastAddedWordAddTime
long	gLastAddedWordEndOffset = 0;		// ROM 0x0c10172c gLastAddedWordEndOffset


// ROM 0x0016c63c GetLastAddedWordView__Fv
TView*
GetLastAddedWordView(void)
{
	return gLastAddedWordView;
}


// ROM 0x0017a310 ContainsOnlyWhiteSpace__FPUsUl
// Whether the first count characters are all white space; the string's
// end stops the walk early, so a count of -1 means to the end.
Boolean
ContainsOnlyWhiteSpace(const UniChar* text, ULong count)
{
	for (ULong i = 0; i < count && *text != 0; text++, i++)
		if (!IsWhiteSpace(*text))
			return false;
	return true;
}


// ROM 0x00174dbc DeleteHilitedTextOnly__14TParagraphViewFRC6RefVar
// The hilited characters removed and the caret left where they were.
void
TParagraphView::DeleteHilitedTextOnly(RefArg hilite)
{
	TParagraphHilite* h = (TParagraphHilite*) RefToAddress(hilite);
	long start = h->fStart;
	RemoveText(start, h->fEnd - start);
	gRootView->SetKeyView(this, start, 0, false);
}


// ROM 0x00173ea8 ScrubHilite__14TParagraphViewFRC5TRect
// A scrub that touches the selection takes the selection out, whatever
// else it covers.  The scrub's bounds are global and a paragraph hilite's
// area is kept in the view's own coordinates, so they are moved over by
// the view's top left first.  ==> whether there was a selection to delete.
Boolean
TParagraphView::ScrubHilite(const Rect& bounds)
{
	RefVar hilite(FirstHilite());
	if (NOTNIL(hilite))
	{
		Rect r = bounds;
		OffsetRect(&r, -viewBounds.left, -viewBounds.top);
		if (((THilite*) RefToAddress(hilite))->Overlaps(r))
		{
			DeleteHilitedTextOnly(hilite);
			return true;
		}
	}
	return false;
}


// ROM 0x0017d2a0 FindNearestWordBoundary__FRC6TPointlN22
// Which edge of a word the point is nearest, as a percentage of the
// word's width measured against the bias.  A positive bias asks how far
// across the word the point is and takes the right edge at that much or
// more; a negative one asks how far from the right edge it is and takes
// the right edge when it is within that much.
long
FindNearestWordBoundary(const Point& pt, long left, long right, long bias)
{
	if (left == right)
		return left;
	long span = right - left;
	if (bias >= 0)
		return ((pt.h - left) * 100) / span >= bias ? right : left;
	return ((right - pt.h) * 100) / span < -bias ? right : left;
}


// ROM 0x001776f0 PointToWord__14TParagraphViewFRC6TPointPlT210MarginSizePP8LineInfoT2PUc
// The word the point is in: the line it is on by its v alone (the ROM's
// MarginSize 1 widens each line's box by a thousand pixels either way, so
// only the v counts), the character nearest its h, and the word breaks
// round that character.
//
// (host: the ROM finds the text object under the point and asks
// FindWordBreaks over that object's own text, which is also how it knows
// the run and whether the character is a tab.  Here the whole line is one
// run and the breaks are scanned over the paragraph's text.)
Boolean
TParagraphView::PointToWord(const Point& pt, long* start, long* end, long* outLine)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	if (fLineCount == 0)
		return false;
	long index = fLineCount - 1;
	for (long i = 0; i < fLineCount; i++)
		if (pt.v < fLines[i].fBounds.bottom)
		{
			index = i;
			break;
		}
	long offset = PointToOffset(pt);
	long length = TextLength();
	if (offset >= length)
		offset = length - 1;
	if (offset < 0)
		return false;
	RefVar textRef(Text());
	if (ISNIL(textRef))
		return false;
	TRichString rich(textRef);
	const UniChar* text = rich.GrabPtr();
	long from = ScanWordStart(text, offset, 0);
	long to = ScanWordEnd(text, offset, length);
	rich.ReleasePtr();
	*start = from;
	*end = to;
	if (outLine != nil)
		*outLine = index;
	return true;
}


// ROM 0x00177dcc PointToWordBoundary__14TParagraphViewF6TPoint10MarginSizelPP8LineInfoPlPUc
// The end of the word at the point that the point is nearest, biased.
// ==> the character offset of that end, or -1 when there is no word
// there at all.
long
TParagraphView::PointToWordBoundary(const Point& pt, long bias, long* outLine)
{
	long start, end;
	if (!PointToWord(pt, &start, &end, outLine))
		return -1;
	Rect box;
	OffsetToBounds(start, &box);
	long left = box.left;
	OffsetToBounds(end, &box);
	long right = box.left;
	return FindNearestWordBoundary(pt, left, right, bias) == left ? start : end;
}


// ROM 0x00174808 ScrubCharacter__14TParagraphViewFP8LineInfolRC5TRectPl
// The single character of the line that the scrub is over, when there is
// one: the scrub and the character must contain one another
// horizontally, one way or the other.  ==> whether one was found, and
// its offset.
//
// (host: the ROM asks ReplaceCharacter, which walks the line's text
// objects with an empty replacement string just to find the character
// the rectangle picks out - the same containment test, over CharBounds.
// The text objects are NOT YET, so the line's characters are measured
// here instead.)
Boolean
TParagraphView::ScrubCharacter(long line, const Rect& bounds, long* outOffset)
{
	if (line < 0 || line >= fLineCount)
		return false;
	const LineInfo& info = fLines[line];
	for (long offset = info.fStart; offset < info.fTextEnd; offset++)
	{
		Rect box, next;
		OffsetToBounds(offset, &box);
		OffsetToBounds(offset + 1, &next);
		long left = box.left;
		long right = next.left;
		if (right <= left)
			continue;
		if ((bounds.left <= left && right <= bounds.right)
			|| (left <= bounds.left && bounds.right <= right))
		{
			*outOffset = offset;
			return true;
		}
	}
	return false;
}


// The width of a space in the style text put in at the offset would take
// (host: the ROM makes a paragraph style record out of
// GetStyleForInsertion and measures a space text object with it; the same
// style spec goes through MeasureTextOnce here).
static long
SpaceWidthAt(TParagraphView* view, long offset)
{
	RefVar spec(view->GetStyleForInsertion(offset, false, true));
	if (ISNIL(spec))
		spec = view->GetDefaultViewStyle();
	StyleRecord record;
	CreateTextStyleRecord(spec, &record);
	StyleRecord* styles[1];
	styles[0] = &record;
	short lengths[1];
	lengths[0] = 1;
	UniChar space = ' ';
	TextOptions options;
	memset(&options, 0, sizeof(options));
	FPoint where;
	where.x = 0;
	where.y = 0;
	TextBoundsInfo info;
	MeasureTextOnce(&space, 1, styles, lengths, where, &options, &info);
	DisposeStyleRecord(&record);
	return (info.fRight - info.fLeft) >> 16;
}


/*------------------------------------------------------------------------------
	T h e   c a r e t   g e s t u r e
------------------------------------------------------------------------------*/

// ROM 0x00178548 FindClosestBaseline__14TParagraphViewFs
// The line whose baseline is nearest the given v, or none when the point is
// further below the last line than one line's spacing (the spacing being
// the gap between the last two lines, or a third again the ascent when
// there is only one).
//
// (the ROM writes a line's baseline as its box's bottom less the field at
// +0x18 of its LineInfo, which is the descent below the baseline; this
// cache keeps the ascent instead, so the baseline is the box's top plus
// that.)
long
TParagraphView::FindClosestBaseline(short v)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	if (fLineCount == 0)
		return -1;
	const LineInfo& last = fLines[fLineCount - 1];
	long lastBaseline = last.fBounds.top + last.fAscent;
	long spacing;
	if (fLineCount < 2)
		spacing = (last.fAscent * 4) / 3;
	else
		spacing = lastBaseline - (fLines[fLineCount - 2].fBounds.top + fLines[fLineCount - 2].fAscent);
	long belowLast = v - (lastBaseline + spacing);
	if (belowLast < 0)
		belowLast = -belowLast;
	long fromLast = v - lastBaseline;
	if (fromLast < 0)
		fromLast = -fromLast;
	if (fromLast > belowLast)
		return -1;				// past the end of the text altogether
	long best = -1;
	long nearest = 10000;
	for (long i = 0; i < fLineCount; i++)
	{
		long distance = v - (fLines[i].fBounds.top + fLines[i].fAscent);
		if (distance < 0)
			distance = -distance;
		if (distance < nearest)
		{
			nearest = distance;
			best = i;
		}
	}
	return best;
}


// ROM 0x00175840 FindLineForWord__14TParagraphViewFRC5TRectl
// Which line something written in the box belongs to.  The box's middle
// is tried first when flag 1 is asked for, and taken when the line it
// lands on actually straddles the box; otherwise the box's top (flag 2)
// and bottom (flag 4) are tried and whichever baseline is nearer wins -
// the bottom only when the top is more than eight pixels out.
long
TParagraphView::FindLineForWord(const Rect& box, long flags)
{
	if ((flags & 1) != 0)
	{
		long middle = FindClosestBaseline((short) ((box.top + box.bottom) >> 1));
		if (middle >= 0)
		{
			long baseline = fLines[middle].fBounds.top + fLines[middle].fAscent;
			if (baseline > box.top && baseline < box.bottom)
				return middle;		// it straddles the box: that is the line
		}
	}
	long fromTop = 10000;
	long fromBottom = 10000;
	long atTop = -1;
	long atBottom = -1;
	if ((flags & 2) != 0)
	{
		atTop = FindClosestBaseline(box.top);
		if (atTop >= 0)
		{
			fromTop = (fLines[atTop].fBounds.top + fLines[atTop].fAscent) - box.top;
			if (fromTop < 0)
				fromTop = -fromTop;
		}
	}
	if ((flags & 4) != 0 && fromTop > 8)
	{
		atBottom = FindClosestBaseline(box.bottom);
		if (atBottom >= 0)
		{
			fromBottom = (fLines[atBottom].fBounds.top + fLines[atBottom].fAscent) - box.bottom;
			if (fromBottom < 0)
				fromBottom = -fromBottom;
		}
	}
	return fromTop < fromBottom ? atTop : atBottom;
}


// ROM 0x00175dac InsertHorizontalSpace__14TParagraphViewFR6TPointlT2Uc
// What a caret gesture actually does: spaces, or line breaks, put in at
// the point.
//
// A plain caret (width -1) is one space.  A caret with a tail is as many
// spaces as the tail is wide, measured against the width of a space in
// the style the text would be inserted in.  With no width at all it is
// line breaks instead: one for a caret with no height either, or as many
// as the height is worth in lines - and then the white space already at
// the point is stepped over first, so the break lands after it.
//
// (host: the ROM inserts through DoInsertItems, the same path a dropped
// item takes; InsertStyledText is the host's equivalent - it makes the
// same aeReplaceText command, with the same undo.  SaveInsertArea, which
// remembers where the recogniser put something, and CheckAndDoSplitInk
// are NOT YET: the insert-run list and ink.)
long
TParagraphView::InsertHorizontalSpace(Point& pt, long width, long height, Boolean typed)
{
	long line = FindClosestBaseline(pt.v);
	if (line < 0)
		return 0;
	long lineHeight = fLines[line].fAscent + fLines[line].fHeight;
	long offset = PointToOffset(pt);
	long spaces = width == -1 ? 1 : 0;
	long breaks = 0;
	Boolean stepOverWhite = false;
	if (width != -1)
	{
		breaks = height == -1 ? 1 : 0;
		if (height == -1)
			stepOverWhite = true;
		else if (width < 1)
		{
			if (height > 0)
			{
				breaks = (height + lineHeight / 2) / lineHeight + 1;
				if (breaks > 0)
					stepOverWhite = true;
			}
		}
		else
		{
			long space = SpaceWidthAt(this, offset);
			spaces = space > 0 ? width / space : 0;
		}
	}
	RefVar textRef(Text());
	TRichString rich(textRef);
	const UniChar* text = rich.GrabPtr();
	if (stepOverWhite)
	{
		long length = (long) Ustrlen(text);
		while ((IsTab(text[offset]) || IsSpace(text[offset])) && offset < length)
			offset++;
	}
	rich.ReleasePtr();

	// ROM bug kept: with neither a width worth a space nor a height worth a
	// line the ROM inserts the buffer it never filled in, which is whatever
	// was on the stack.  DEVIATION: the host cannot reproduce which bytes
	// those are, and putting arbitrary text into somebody's note is worse
	// than useless, so the buffer starts empty and nothing goes in.
	UniChar buffer[41];
	buffer[0] = 0;
	UniChar* chars = buffer;
	UniChar* allocated = nil;
	long count = 0;
	if (spaces >= 1 || breaks >= 1)
	{
		count = spaces >= 1 ? spaces : breaks;
		UniChar fill = spaces >= 1 ? ' ' : 0x0d;
		if (count > 40)
		{
			allocated = new UniChar[count + 1];
			if (allocated == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			chars = allocated;
		}
		for (long i = 0; i < count; i++)
			chars[i] = fill;
		chars[count] = 0;
	}

	// a caret that opens no space at all, drawn over a word of writing,
	// cuts the word in two instead
	if (count == 0 && CheckAndDoSplitInk(pt, offset))
	{
		if (allocated != nil)
			delete[] allocated;
		return 1;
	}
	InsertStyledText((ULong) offset, chars, (ULong) count, RefVar(NILREF), RefVar(NILREF), 0, 0, !typed);
	// the caret goes where the insertion was, but only when more than one
	// space or any line break went in
	if (typed && (spaces > 1 || breaks > 0))
		gRootView->SetKeyView(this, offset, 0, false);
	if (allocated != nil)
		delete[] allocated;
	return 1;
}



// ROM 0x00176208 CheckAndDoSplitInk__14TParagraphViewFR6TPointl
// A caret drawn over a word of writing cuts it in two rather than
// opening space in the text: the caret gesture's other answer, for a
// paragraph whose characters are ink.
//
// One of the two characters the caret's offset lies between has to be an
// ink word, and which of them it is the caret's own point decides: to
// the left of where the offset draws its caret the word before it is
// meant, to the right the word after.  (A caret drawn exactly on the
// boundary picks neither and nothing happens.)
//
// The word is then cut at the caret's x (`SplitInkAt` with eight pixels
// of slop, which is what lets a letter written across the cut stop it),
// each half brought back to the x-height this view writes in, and the
// two put in where the one was - through `DoInsertItems`, so the cut is
// one thing to undo.  The white space after the word goes with it when
// there is any, because the two halves are spaced apart by the insert
// itself.
//
// ==> whether the word was cut.
long
TParagraphView::CheckAndDoSplitInk(Point& pt, long offset)
{
	// (the ROM reads the characters out of a RefVar it lets go of at
	//  once and keeps the bare pointer; the object is held here, which
	//  comes to the same thing and does not go stale if the heap moves)
	RefVar object(Text());
	const UniChar* text = GetCString(object);
	long length = Ustrlen(text);
	Boolean inkBefore = offset > 0 && text[offset - 1] == kInkWordChar;
	Boolean inkAfter = offset < length && text[offset] == kInkWordChar;
	long removeLength = 1;
	if (!inkBefore && !inkAfter)
		return 0;

	Rect caret;
	OffsetToBounds(offset, &caret);
	long at = -1;
	if (inkBefore && pt.h < caret.left)
		at = offset - 1;
	else if (inkAfter && pt.h > caret.left)
		at = offset;

	// (the ROM asks this before it knows whether there is a word at all,
	//  so with none it reads the character at the offset for nothing)
	if (at + 1 < length && IsWhiteSpace(text[at + 1]))
		removeLength = 2;
	if (at < 0)
		return 0;

	RefVar form(GetInkAt(this, at));
	RefVar pieces(SplitInkAt(form, pt.h, 8));
	if (ISNIL(pieces))
		return 0;

	Boolean numbers = ViewExpectsNumbers(this);
	for (long i = 0; i < 2; i++)
	{
		RefVar piece(GetFrameSlot(RefVar(GetArraySlotRef(pieces, i)), RSSYMink));
		AdjustInkWordXHeight(piece, numbers);
		SetArraySlot(pieces, i, piece);
	}
	DoInsertItems(this, pieces, true, true, at, removeLength, false, RefVar(NILREF));
	return 1;
}


// ROM 0x001764c4 InsertVerticalSpace__14TParagraphViewFR6TPointl
// A caret drawn between two lines opens the space of a line between
// them: as many carriage returns as the caret's height is worth go in at
// the start of the line the caret points into, and the caret follows
// them.
//
// The line is the first whose midline is below the point, so a point
// anywhere in a line's top half picks that line.  Two things then
// disqualify it: the point more than a quarter of a line above the line
// before's baseline (the first line's top standing in for it - a caret
// drawn well clear of the text above is not aimed between the lines at
// all), and the point below the line's own midline, which the search
// has already ruled out.
//
// The count is the height rounded to lines (at least one), and one more
// unless there is already a carriage return at the insertion point or
// just before it - a break in the middle of a line costs a return to
// make, and one to keep the line that was there.  The caret then goes
// after the first of them, or at the insertion point itself when a
// return was already before it.
//
// (host: the ROM inserts through AddWord, the recogniser's path into a
// paragraph; InsertStyledText is the host's equivalent - the same
// aeReplaceText command with the same undo.  SaveInsertArea, which
// remembers where the recogniser put something, is NOT YET.)
long
TParagraphView::InsertVerticalSpace(Point& pt, long height)
{
	if (fLines == nil || fLineCount == 0)
		return 0;
	// the baseline of the line before the one being looked at; before the
	// first line, that line's own top
	long previous = fLines[0].fBounds.top;
	for (long i = 0; i < fLineCount; i++)
	{
		const LineInfo& line = fLines[i];
		long lineHeight = line.fBounds.bottom - line.fBounds.top;
		if (line.fBounds.top + lineHeight / 2 <= pt.v)
		{
			previous = line.fBounds.top + line.fAscent;
			continue;
		}
		if (previous - pt.v > lineHeight / 4)
			return 0;
		if (pt.v - line.fBounds.top > lineHeight / 2)
			return 0;			// (the search has already said otherwise)

		long offset = line.fStart;
		long count = 1;
		if (height != -1)
		{
			count = (height + lineHeight / 2) / lineHeight;
			if (count == 0)
				count = 1;
		}
		RefVar textRef(Text());
		TRichString rich(textRef);
		const UniChar* text = rich.GrabPtr();
		Boolean atBreak = text[offset] == kCR || (offset > 0 && text[offset - 1] == kCR);
		rich.ReleasePtr();
		if (!atBreak)
			count++;

		UniChar buffer[41];
		UniChar* chars = buffer;
		UniChar* allocated = nil;
		if (count > 40)
		{
			allocated = new UniChar[count + 1];
			if (allocated == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			chars = allocated;
		}
		for (long j = 0; j < count; j++)
			chars[j] = kCR;
		chars[count] = 0;
		InsertStyledText((ULong) offset, chars, (ULong) count, RefVar(NILREF), RefVar(NILREF), 0, 0, false);
		if (allocated != nil)
			delete[] allocated;

		// the caret after the first return that went in - unless there
		// was one before the insertion already, when it stays put
		long delta = 1;
		RefVar afterRef(Text());
		TRichString after(afterRef);
		const UniChar* newText = after.GrabPtr();
		if (offset > 1 && newText[offset - 1] == kCR)
			delta = 0;
		after.ReleasePtr();
		gRootView->SetKeyView(this, offset + delta, 0, false);
		// NOT YET: SaveInsertArea(fInsertRunList, offset, count)
		return 1;
	}
	return 0;
}


// the second of the three ink characters the ROM's IsInkChar (0x001fe914)
// takes - 0xf700, 0xf701 and 0xf702 - and the only one a join merges
const UniChar kJoinableInkChar = 0xf701;


/*------------------------------------------------------------------------------
	A   c h a r a c t e r   w r i t t e n   o v e r   a   c h a r a c t e r
------------------------------------------------------------------------------*/

// The writer can correct a word by writing one letter over another
// letter of it.  That is not an insertion: what has to happen is that
// the recogniser reads the new letter, the letter it was written over is
// replaced by it, and the *word* the letter belongs to gets a new set of
// readings - because a word whose third letter has just changed is a
// different word, and the corrector should offer alternatives for the
// new one.

// The last replacement, so that a writer correcting the same letter of
// the same word over and over is understood to be choosing between the
// readings rather than starting again.  The index is 100 - out of range
// of any word - when there is none.
long		gLastReplacedIndex = 100;			// ROM 0x0c101740
UniChar*	gLastReplacedWord = nil;			// ROM 0x0c101744
// The one-character string a replacement's answer is handed back as.
UniChar		gAlternateWord[4] = { 0, 0, 0, 0 };	// ROM 0x0c101738


// ROM 0x0017b1b8 DoReplaceSym__FP14TParagraphViewP7WordHitPUsRC6RefVar
// The letter under the writing replaced by what the recogniser read.
//
//   - the word around the hit is found (`FindWordBreaks` over the
//     paragraph's own word break table) and, with it, the correction
//     entry that covers it - made on the spot when there is none, and
//     widened to take in the neighbouring words when the "word" turns
//     out to be nothing but white space;
//   - the word's characters are copied into a buffer, and the character
//     the writing landed on is noted;
//   - the unit is asked for its readings again, and every reading that
//     is a *single character* is tried in that buffer's place: each one
//     makes a whole candidate word, which goes into the frame of
//     readings being built;
//   - and the lot goes in through `HandleInsertItems`, replacing the
//     word rather than the letter, so the correction information ends
//     up describing the new word.
//
// The unit's own score is only used as a floor when the writing was more
// than a letter or two (or the character being replaced is a space), so
// that a single letter written deliberately always wins.
//
// ==> whether anything was replaced; `out` is the character chosen.
//
// NOT YET RECONSTRUCTED: the branch for a view that is read a word at a
// time rather than a letter at a time (`!UsesLetters`).  There the ROM
// asks the engine to read the writing *again* as one character of a
// known height - `ReclassifyCharacter` 0x000348e4 over `MakeCharArea`
// and `TController::ClassifyInArea` - with the unit's readings saved and
// put back around it (`GetInterpretationsCopy` 0x0021f6a8 and its two
// companions).  Our engine reads nothing, so there would be nothing to
// ask; the readings the unit already has are used either way.
Boolean
DoReplaceSym(TParagraphView* para, WordHit* hit, UniChar* out, RefArg breakTable)
{
	if (gLastReplacedWord == nil)
	{
		gLastReplacedWord = new UniChar[1];
		gLastReplacedWord[0] = 0;
	}
	Boolean done = false;
	UniChar* buf = nil;
	long n = 0;
	long where = 0;

	long wordLength = Ustrlen(hit->fWord);
	if (hit->fUnit == nil)
	{
		// nothing was written: a deletion is a replacement too
		if (wordLength == 0)
			done = true;
	}
	else if (!AreStrokesAfterUnit(hit->fUnit->fUnit))
	{
		ULong textLength = (ULong) Ustrlen(hit->fText);
		ULong start = 0;
		ULong end = 0;
		FindWordBreaks(hit->fText, textLength, (ULong) hit->fIndexInRun, true,
					   breakTable, &start, &end);
		if (start < textLength)
		{
			long count = (long) (end - start);
			long at = hit->fIndex - (hit->fIndexInRun - (long) start);
			long minScore = 1000;
			RefVar item;
			RefVar info(FindWordInfo(para, at));
			if (ISNIL(info) && count != 0)
			{
				if (ContainsOnlyWhiteSpace(hit->fText + start, (ULong) count))
				{
					// the writing landed in a gap: the words on either
					// side of it are part of what is being corrected
					RefVar before(FindWordInfo(para, at - 1));
					if (NOTNIL(before))
					{
						long was = RINT(RefVar(GetFrameSlotRef(before, RSSYMstart)));
						long to = RINT(RefVar(GetFrameSlotRef(before, RSSYMstop)));
						at = was;
						count += to - was;
						start -= (ULong) (to - was);
					}
					RefVar after(FindWordInfo(para, at + count));
					if (NOTNIL(after))
					{
						long was = RINT(RefVar(GetFrameSlotRef(after, RSSYMstart)));
						long to = RINT(RefVar(GetFrameSlotRef(after, RSSYMstop)));
						count += to - was;
						end += (ULong) (to - was);
					}
				}
				info = MakeWordInfo(para, at, count);
				SetOffsetInfo(info, para, at, at + count, 0);
				SetWordInfoFlags(info, kWordInfoAutoAdded);
				AutoRemove(info);
			}
			if (NOTNIL(info))
			{
				// an empty frame for the readings to be gathered into
				item = MakeWordInfo(RefVar(NILREF));
				long was = RINT(RefVar(GetFrameSlotRef(info, RSSYMstart)));
				long to = RINT(RefVar(GetFrameSlotRef(info, RSSYMstop)));
				if (at >= was && at + count <= to)
				{
					// the entry covers more than the word breaks found:
					// the whole of it is what is being replaced
					start -= (ULong) (at - was);
					count = to - was;
					end = start + (ULong) count;
					at = was;
				}
			}

			n = (long) (end - start);
			buf = new UniChar[n + 1];
			Ustrncpy(buf, hit->fText + start, n);
			buf[n] = 0;
			where = hit->fIndexInRun - (long) start;
			UniChar was = buf[where];

			// how good a reading has to be: the unit's own score, but
			// only when the writing was more than a letter or two - a
			// single letter written over a letter is meant
			Handle read = hit->fUnit->Word();
			long chars = Ustrlen(*(UniChar**) read);
			if (chars > 2 || (was == U_CONST_CHAR(' ') && chars > 1))
				minScore = (long) hit->fUnit->WordScore();
			DisposHandle(read);

			// the writer correcting the same letter of the same word
			// again is choosing between its readings; anything else
			// starts the choice over
			if (where != gLastReplacedIndex || Ustrcmp(gLastReplacedWord, buf) != 0)
				ClearTryString();
			AddTryString(was);

			TWordList* words = hit->fUnit->MakeWordList(true, true);
			if (words != nil)
			{
				if (words->Score(0) < minScore)
				{
					UniChar best = 0;
					long bestScore = 1000;
					long bestLabel = 0;
					for (long i = 0; i < words->Count(); i++)
					{
						long score = 0;
						long label = 0;
						Handle reading = words->Ith(i, &score, &label);
						if (Ustrlen(*(UniChar**) reading) == 1)
						{
							UniChar c = (*(UniChar**) reading)[0];
							buf[where] = c;
							RefVar word(MakeString(buf, n));
							RefVar interp(MakeWordInterp(word, score, -1, label));
							if (best == 0 && c != was)
							{
								// the first reading that is not what is
								// already there is the answer
								best = c;
								out[0] = c;
								bestScore = score;
								bestLabel = label;
							}
							done = true;
							if (NOTNIL(item))
							{
								DeleteMatchingWord(item, word);
								InsertWordInterp(item, interp, -1);
							}
						}
						DisposHandle(reading);
					}
					if (minScore == 1000 && bestScore < 1000)
					{
						// nothing was holding the readings to a floor, so
						// the best of them goes to the front as well
						out[0] = best;
						buf[where] = best;
						RefVar word(MakeString(buf, n));
						RefVar interp(MakeWordInterp(word, bestScore, -1, bestLabel));
						done = true;
						if (NOTNIL(item))
						{
							DeleteMatchingWord(item, word);
							InsertWordInterp(item, interp, 0);
						}
					}
				}

				if (NOTNIL(item) && done)
				{
					// the word replaced, with its new readings: the
					// paragraph puts it in as an item like any other
					RefVar style(para->GetStyleAtOffset(at, nil, nil));
					if (IsInkWord(style))
						style = para->GetStyleForInsertion(at, false, false);
					RefVar spec(Clone(RefVar(Rstarterinsertspec)));
					SetFrameSlot(spec, RSSYMinsertitems, item);
					SetFrameSlot(spec, RSSYMaddspace, RefVar(NILREF));
					SetFrameSlot(spec, RSSYMinsertoffset, RefVar(MAKEINT(at)));
					SetFrameSlot(spec, RSSYMreplacechars, RefVar(MAKEINT(count)));
					SetFrameSlot(spec, RSSYMdefaultfontspec, style);
					SetFrameSlot(spec, RSSYMmovecaret, RefVar(NILREF));
					para->HandleInsertItems(spec);
				}
				delete words;
			}
		}
	}

	// what was replaced, for the next time round
	gLastReplacedIndex = 100;
	if (done && buf != nil)
	{
		if (gLastReplacedWord != nil)
			delete[] gLastReplacedWord;
		gLastReplacedWord = new UniChar[n + 1];
		if (gLastReplacedWord != nil)
		{
			buf[where] = out[0];
			Ustrncpy(gLastReplacedWord, buf, n);
			gLastReplacedWord[n] = 0;
			gLastReplacedIndex = where;
		}
	}
	if (buf != nil)
		delete[] buf;
	return done;
}


// (host: the ROM asks a text object which character an x falls on
// (CoordToChar) and which character *gap* it is nearest
// (CoordToInterCharGap 0x0017d614, over PointToChar).  The text objects
// are NOT YET, so the line's own characters are measured instead; the
// answers are relative to the line's first character, as the ROM's are
// to the run's.)
static long
CharAtCoord(TParagraphView* para, const LineInfo* line, long x)
{
	for (long i = line->fStart; i < line->fTextEnd; i++)
	{
		Rect box;
		para->OffsetToBounds(i, &box);
		if (x < box.right)
			return i - line->fStart;
	}
	return line->fTextEnd - line->fStart;
}


static long
GapAtCoord(TParagraphView* para, const LineInfo* line, long x)
{
	long at = CharAtCoord(para, line, x) + line->fStart;
	Rect box;
	para->OffsetToBounds(at, &box);
	if (x > (box.left + box.right) / 2)
		at++;
	return at - line->fStart;
}


// ROM 0x0017bc84 WordOverSpaces__FPUsClT2
// Whether the writing between two characters is over a run of spaces
// rather than over anything to correct: at least as many spaces as the
// writing is wide (and never fewer than three) counted forward from the
// first character, and - when that run covered the whole of it -
// backwards from the character before as well.
Boolean
WordOverSpaces(const UniChar* text, const long from, const long to)
{
	long span = to - from;
	long wanted = span < 4 ? 3 : span;
	long spaces = 0;
	const UniChar* at = text + from;
	const UniChar* forward = at;
	while (*forward == U_CONST_CHAR(' '))
	{
		forward++;
		spaces++;
		if (wanted <= spaces)
			return true;
	}
	if (span <= spaces)
	{
		while (--at >= text && *at == U_CONST_CHAR(' '))
		{
			spaces++;
			if (wanted <= spaces)
				return true;
		}
	}
	return false;
}


// ROM 0x00174e14 ReplaceCharacter__14TParagraphViewFPC8LineInfoClP6Finder
// A character written over a character of the text replaces it, which is
// the strongest claim a paragraph can make on a piece of writing: it
// answers 6, and `TEditView::HandleWord` stops asking anybody else.
//
// The writing has to fall on the line horizontally, and either (with a
// unit) cover no more than three characters and not be over a run of
// spaces, or (without one - the edit view's probe for what text a point
// is in) be no more than two character gaps wide.  The character it
// lands on is the one under the middle of its box, stepped back over any
// tabs and returns, and back one more when it is the character the line
// ends at.
//
// Whether that character is *replaced* or the writing goes before or
// after it is then worked out from the two boxes: writing that covers
// the character replaces it, writing clear of it on one side goes on
// that side, and writing that overlaps it is decided by which half of
// the character its middle is in.  A space is treated more carefully -
// writing over the last space of a line, with another space before it,
// is not a correction at all.
//
// `DoReplaceSym` does the rest.  What comes back is a character, which
// may not be the one the recogniser first read: when it differs, the
// finder's word is pointed at `gAlternateWord` so that the caller puts
// *that* character in.
//
// (host: the ROM asks the line's text objects for the boxes and for the
// character at a coordinate - GetTextObjBounds, GetTextObjField,
// CoordToChar, CoordToInterCharGap, CharBounds.  The text objects are
// NOT YET, so the line's own bounds and OffsetToBounds/PointToOffset
// answer instead, as they do for the rest of FindWordInRun.)
Boolean
TParagraphView::ReplaceCharacter(const LineInfo* line, const long run, Finder* finder)
{
	(void) run;
	Rect lineBox = line->fBounds;
	RefVar textRef(Text());
	const UniChar* text = GetCString(textRef);
	long runStart = line->fStart;

	// the writing's box, clipped to the line
	Rect box = finder->fBox;
	if (box.top < lineBox.top)
		box.top = lineBox.top;
	if (box.bottom > lineBox.bottom)
		box.bottom = lineBox.bottom;
	Point mid = MidPoint(box);
	if (box.right + 3 <= lineBox.left || lineBox.right + 3 < box.left)
		return false;

	if (finder->fUnit != nil)
	{
		// no more than three characters, and not over a run of spaces
		long right = CharAtCoord(this, line, box.right);
		long left = CharAtCoord(this, line, box.left);
		if (right - left > 3)
			return false;
		if (WordOverSpaces(text + runStart, left, right))
			return false;
	}
	else if (GapAtCoord(this, line, box.right) - GapAtCoord(this, line, box.left) > 2)
		return false;

	// the character under the middle of the writing, past the tabs and
	// returns, and not the one the line ends at
	long at = CharAtCoord(this, line, mid.h) + runStart;
	while (text[at] == U_CONST_CHAR('\t') || text[at] == 0x0d)
		at--;
	if (line->fEnd == at)
		at--;

	long replaced = 1;
	if (finder->fUnit != nil)
	{
		Rect charBox;
		OffsetToBounds(at, &charBox);
		UniChar c = text[at];
		if (c == U_CONST_CHAR(' ') || c == 0xca)
		{
			if (line->fEnd - 1 == at)
			{
				// the last space of the line: writing over it is only a
				// correction when there is something before it
				if (at - 1 < line->fStart || text[at - 1] == U_CONST_CHAR(' '))
					return false;
				replaced = 0;
			}
			else if ((box.left > charBox.left && box.right >= charBox.right)
					 || (box.left < charBox.left && box.right <= charBox.right))
				replaced = 1;		// the writing and the space cover one another
			else if (mid.h <= (charBox.left + charBox.right) / 2)
				replaced = 0;		// before it
			else
			{
				at++;				// after it
				replaced = 0;
			}
		}
		else if (at == runStart && mid.h < charBox.left)
			replaced = 0;			// before the line's first character
		else if (mid.h > charBox.right)
		{
			at++;
			replaced = 0;			// past the character
		}
	}

	WordHit hit;
	hit.fText = text + runStart;
	hit.fIndexInRun = at - runStart;
	hit.fIndex = at;
	hit.fReplaceLength = replaced;
	hit.fBaseline = lineBox.bottom;
	hit.fUnused14 = 0;
	hit.fUnit = finder->fUnit;
	hit.fWord = finder->fText;
	hit.fLine = (LineInfo*) line;
	hit.fReallyDoIt = finder->fReallyDoIt;

	UniChar chosen[2];
	chosen[0] = finder->fText[0];
	chosen[1] = 0;
	if (!DoReplaceSym(this, &hit, chosen, RefVar(fWordBreakTable)))
		return false;

	if (finder->fText[0] != chosen[0])
	{
		// the recogniser's second thoughts go in instead
		gAlternateWord[0] = chosen[0];
		gAlternateWord[1] = 0;
		finder->fText = gAlternateWord;
		finder->fLength = 1;
	}
	finder->fView = this;
	finder->fExact = true;
	finder->fOffset = at;
	finder->fReplaceLength = hit.fReplaceLength;
	if (hit.fReplaceLength == 1)
		gAddWordInfo = false;		// the word it belongs to is what went in
	if (finder->fOffset == line->fStart && finder->fOffset != 0
		&& text[finder->fOffset - 1] == 0x0d)
		finder->fNewLine = true;
	return true;
}


/*------------------------------------------------------------------------------
	W h e r e   t h e   w o r d   g o e s
------------------------------------------------------------------------------*/

// A `Finder` carries a word written on the page into the paragraph and
// comes back saying where in the text it belongs: the offset, how many
// characters it replaces, and whether it starts a new line.  The three
// functions below fill it in; `AddWord` is what acts on it.

// ROM 0x001733e4 MinWidthToIntuitTab__FPCUsRC5TRect
// How wide a gap has to be before it is taken for a tab rather than for
// a space: four times the average width of a character of the word that
// was written, and never less than 22 pixels.  The narrow letters count
// half, because a word of i's and l's is not as wide as its letter count
// suggests - and when that discount makes the average implausibly wide
// (over fifteen pixels) the plain letter count is used instead.
//
// FindTab answers nothing in this ROM, so the number is only ever used
// to ask AdjacentBoxes whether a gap is small enough to be a space.
long
MinWidthToIntuitTab(const UniChar* text, const Rect& box)
{
	long narrow = 0;
	long length = Ustrlen(text);
	for (long i = 0; i < length; i++)
		if (text[i] == U_CONST_CHAR('i') || text[i] == U_CONST_CHAR('l')
			|| text[i] == U_CONST_CHAR('I'))
			narrow++;
	long count = length - (narrow + 1) / 2;
	if (count == 0)
		count = 1;
	// (the box's width divided by the letters it holds; the ROM divides
	//  by the box and would trap on a box of no width at all)
	long width = (short) (box.right - box.left);
	long average = width / count;
	if (average > 15)
		average = width / length;
	average = average * 4;
	if (average < 0x17)
		average = 0x16;
	return average;
}


// ROM 0x00173cc4 NearTabStop__14TParagraphViewFl
// The paragraph's tab stop within ten pixels of x, or -1 when there is
// none.  ==> its index in the `tabs` array.
long
TParagraphView::NearTabStop(long x)
{
	RefVar tabs(Tabs());
	if (NOTNIL(tabs))
	{
		long count = Length(tabs);
		for (long i = 0; i < count; i++)
		{
			long at = RINT(RefVar(GetArraySlotRef(tabs, i))) - x;
			if (at < 0)
				at = -at;
			if (at < 10)
				return i;
		}
	}
	return -1;
}


// ROM 0x00173ea0 FindTab__14TParagraphViewFP6Finderl
// Which tab stop the word was written at.  The shipping ROM's answer is
// always "none": the whole tab-intuiting path - AddTabStop, the tab
// characters AddWord would put in front of the word - is dead code
// behind it, which is why writing in columns on a Newton gives spaces
// rather than tabs.  Kept as it is.
long
TParagraphView::FindTab(Finder* /*finder*/, long /*x*/)
{
	return 0;
}


// ROM 0x00173268 PreviousLineNeedsCR__14TParagraphViewFPUsT1
// Whether a carriage return is wanted before the word.  Another one the
// ROM answers no to out of hand.
Boolean
TParagraphView::PreviousLineNeedsCR(UniChar* /*text*/, UniChar* /*word*/)
{
	return false;
}


// ROM 0x00173668 FindWordInRun__14TParagraphViewFP6Finder
// Where a word written *over* the paragraph's own text belongs.  The line
// it was written on is the one nearest its box (`FindLineForWord` with
// the middle, the top and the bottom all tried), and then where on that
// line it falls decides:
//
//   left of the line      at the line's start
//   past the end of it    at the line's end
//   over the text         over a run of spaces, it replaces them; over a
//                         word, it goes before or after that word,
//                         whichever edge it was written nearer
//
// ==> whether the word belongs to a line at all.
//
// (host: the ROM walks the line's text objects - GetTextObjField,
// CharLeftEdge, CoordToChar - and asks TabBounds for a tab's box.  The
// text objects are NOT YET, so the line's characters are measured
// through OffsetToBounds and PointToOffset instead, and a tab is a
// character like any other.)
Boolean
TParagraphView::FindWordInRun(Finder* finder)
{
	long wordLeft = finder->fBox.left;
	long index = FindLineForWord(finder->fBox, 5);
	if (index < 0)
		return false;
	const LineInfo& line = fLines[index];
	Point pt;
	pt.h = (short) wordLeft;
	pt.v = (short) ((line.fBounds.top + line.fBounds.bottom) / 2);
	RefVar textRef(Text());
	const UniChar* text = GetCString(textRef);

	// a character written over a character of the text replaces it,
	// which is the strongest claim this paragraph can make
	if (finder->fUnit != nil && ReplaceCharacter(&line, index, finder))
		return true;

	if (wordLeft < line.fBounds.left)
	{
		// written out in the left margin: the word goes at the line's
		// start, and does not need a new line when the line already
		// begins one
		finder->fView = this;
		finder->fOffset = line.fStart;
		finder->fReplaceLength = 0;
		if (line.fStart != 0 && text[line.fStart - 1] == 0x0d)
			finder->fNewLine = true;
	}
	else if (wordLeft < line.fBounds.right)
	{
		// written over the line's own text
		long start = 0;
		long end = 0;
		long onLine = 0;
		PointToWord(pt, &start, &end, &onLine);
		Rect box;
		OffsetToBounds(start, &box);
		long leftEdge = box.left;
		OffsetToBounds(end, &box);
		long rightEdge = box.left;

		if (text[start] == U_CONST_CHAR(' ')
			&& (rightEdge >= finder->fBox.right || end == line.fEnd))
		{
			// written over a run of spaces: it takes their place, from
			// the character its left edge is over to the character its
			// right edge is over (or to the end of the line)
			long at = PointToOffset(pt);
			long to = end;
			if (end != line.fEnd)
			{
				Point right;
				right.v = pt.v;
				right.h = finder->fBox.right;
				to = PointToOffset(right) + 1;
			}
			finder->fView = this;
			finder->fOffset = at;
			finder->fReplaceLength = to - at;
			return true;
		}

		// before or after the word it was written over, whichever edge
		// it was written nearer
		if (wordLeft - leftEdge < rightEdge - wordLeft)
		{
			finder->fOffset = start;
			if (start != 0 && text[start - 1] == 0x0d)
				finder->fNewLine = true;
		}
		else
			finder->fOffset = end;
		finder->fView = this;
		finder->fReplaceLength = 0;
	}
	else
	{
		// written past the end of the line
		if (line.fStart - 1 < 0 || text[line.fStart - 1] == 0x0d)
			finder->fTab = FindTab(finder, line.fBounds.right);
		finder->fView = this;
		finder->fOffset = line.fEnd;
		finder->fReplaceLength = 0;
	}
	return true;
}


// ROM 0x001735e4 SetFinderBelowParagraph__14TParagraphViewFP6Finder
// The word goes at the very end of the text, on a line of its own - and
// it starts that new line unless the view holds one line only, or a word
// has already gone in somewhere (in which case the one before it has
// started the line already).
void
TParagraphView::SetFinderBelowParagraph(Finder* finder)
{
	finder->fOffset = TextLength();
	finder->fReplaceLength = 0;
	finder->fView = this;
	finder->fNewLine = (fViewJustify & vjOneLineOnly) == 0
					   && gLastAddedWordView == nil;
	finder->fTab = FindTab(finder, viewBounds.left);
}


// ROM 0x0017348c FindWordInParagraph__14TParagraphViewFP6Finder
// Where a word written on the page belongs in this paragraph.  It is
// over the text (`FindWordInRun`), or it carries on from the word that
// went in before - beside the last one this view took, or beside the end
// of its last line when it has taken none - in which case it goes at the
// end of the text; or it is on a line of its own below the paragraph.
void
TParagraphView::FindWordInParagraph(Finder* finder)
{
	if (FindWordInRun(finder))
		return;

	Rect last;
	BoundsOfLastLine(&last);
	Rect from;
	Point fromBase;
	if (GetLastAddedWordView() == this)
	{
		from = gLastAddedWordBox;
		fromBase = gLastAddedWordBase;
	}
	else
	{
		from = last;
		fromBase.v = last.bottom;		// the end of the last line
		fromBase.h = last.right;
	}

	if (!AdjacentBoxes(from, finder->fBox, fromBase, finder->fBase, 1000))
	{
		SetFinderBelowParagraph(finder);
		return;
	}

	// it carries on from what is already there
	finder->fOffset = TextLength();
	finder->fView = this;
	finder->fReplaceLength = 0;
	finder->fNewLine = false;
	if ((fViewJustify & 3) != vjCenterH && (fViewJustify & 3) != vjRightH)
	{
		// a gap wider than a word's worth of letters would be a tab
		long wide = MinWidthToIntuitTab(finder->fText, finder->fBox);
		if (!AdjacentBoxes(from, finder->fBox, fromBase, finder->fBase, wide))
		{
			finder->fTab = FindTab(finder, last.right);
			return;
		}
	}
	finder->fTab = 0;
}


// ROM 0x00172eb4 AddWord__14TParagraphViewFP6FinderPCUsUlRC6RefVarPl
// The word put into the text where the Finder says.  It is not inserted
// as it stands: the characters that have to go in front of it - the tabs
// it was written at, the carriage return that starts its line, the space
// that keeps it off the word before - are built up in a buffer in front
// of a copy of the word, and the whole lot goes in as one replacement.
// That is what `styleOffset` is for: it tells `InsertStyledText` how
// many of the characters going in are the run-up rather than the word,
// so the word's own styles land on the word.
//
// A view whose text flags say the text may not be rearranged (bit 1)
// skips all of it and puts the word in bare.
//
// ==> through `outOffset`, where the word itself landed.
void
TParagraphView::AddWord(Finder* finder, const UniChar* text, ULong length,
						RefArg info, long* outOffset)
{
	ULong styleOffset = 0;
	Boolean munge = (TextFlags() & 2) == 0;
	UniChar buffer[128];
	UniChar* allocated = nil;
	UniChar* word = nil;

	if (munge)
	{
		// seventeen characters of room in front of the word and four
		// after it
		UniChar* start = buffer;
		if (length + 0x15 >= 0x80)
		{
			allocated = new UniChar[length + 0x15];
			if (allocated == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			start = allocated;
		}
		word = start + 0x11;
		BlockMove(text, word, length * (long) sizeof(UniChar));
		UniChar* first = word;

		RefVar textRef(Text());
		const UniChar* all = GetCString(textRef);
		long offset = finder->fOffset;
		if (offset != 0)
		{
			// the tabs it was written at (FindTab answers none in this
			// ROM, so never)
			long tabs = finder->fTab;
			if (tabs > 0)
			{
				if (tabs > 11)
					tabs = 12;
				for (long i = 0; i < tabs; i++)
				{
					*--word = U_CONST_CHAR('\t');
					length++;
				}
			}
			if (finder->fNewLine)
			{
				// a return, but only when the character before is not a
				// break already and the word is going in at the very end
				if (!IsBreaker(all[offset - 1]) && all[offset] == 0)
				{
					*--word = 0x0d;
					length++;
				}
			}
			else if (IsBreaker(all[offset - 1]))
				// the word takes the place of the break before it
				finder->fOffset--;

			if (!finder->fExact)
			{
				// and a space between it and what is already there
				UniChar delimiter[6];
				GetAppendDelimiter(delimiter, all, word,
								   (ULong) finder->fOffset, length);
				long n = Ustrlen(delimiter);
				if (n > 0)
				{
					word -= n;
					BlockMove(delimiter, word, n * (long) sizeof(UniChar));
					length += n;
				}
			}
		}
		if (!finder->fExact)
		{
			// and one between it and whatever follows
			const UniChar* after = all + finder->fOffset + finder->fReplaceLength;
			if (after[0] != 0)
			{
				UniChar delimiter[6];
				GetAppendDelimiter(delimiter, word, after, length,
								   (ULong) Ustrlen(after));
				long n = Ustrlen(delimiter);
				if (n > 0)
				{
					BlockMove(delimiter, word + length,
							  n * (long) sizeof(UniChar));
					length += n;
				}
			}
		}
		styleOffset = (ULong) (first - word);
		// (NOT YET RECONSTRUCTED: AddTabStop 0x00173b34, which the ROM
		//  calls when the word was written at a tab stop.  FindTab
		//  answers none in this ROM, so fTab is always 0 and the call
		//  never happens.)
	}

	RefVar styles;
	RefVar correctInfo;
	if (NOTNIL(info))
	{
		styles = GetFrameSlot(info, RSSYMstyles);
		correctInfo = GetFrameSlot(info, RSSYMcorrectinfo);
	}
	InsertStyledText((ULong) finder->fOffset, munge ? word : text, length,
					 styles, correctInfo, styleOffset,
					 (ULong) finder->fReplaceLength, false);
	if (outOffset != nil)
		*outOffset = finder->fOffset + (long) styleOffset;
	if (allocated != nil)
		delete[] allocated;
}


// ROM 0x00172584 IsMidWordLetterInsertion__FP14TParagraphViewP11TUnitPublic
// Whether the writer is putting a single letter into the middle of a
// word at the caret - one character read, the caret in this paragraph
// with nothing selected, and letters on both sides of it.  A letter
// going in there wants no space around it.
Boolean
IsMidWordLetterInsertion(TParagraphView* para, TUnitPublic* unit)
{
	Handle word = unit->Word();
	long length = Ustrlen(*(UniChar**) word);
	DisposHandle(word);
	if (length != 1)
		return false;
	if (gRootView->fCaretView != (TView*) para || gRootView->fCaretLength != 0)
		return false;
	long textLength = para->TextLength();
	RefVar textRef(para->Text());
	const UniChar* text = GetCString(textRef);
	long caret = gRootView->fCaretOffset;
	if (caret == textLength || caret == 0)
		return false;
	return !IsWhiteSpace(text[caret - 1]) && !IsWhiteSpace(text[caret]);
}


// ROM 0x00172760 HandleWord__14TParagraphViewFPCUsUlRC5TRectRC6TPointN22RC6RefVarUcPlP11TUnitPublic
// How well this paragraph would take a word written on the page, and -
// when `reallyDoIt` says so - the word put in.  This is the answer the
// recogniser's `TEditView::HandleWord` collects from every child before
// it picks one, and it is also what `TextContainingPoint` asks with a
// single letter to find out what text a point is in.
//
// The score:
//
//   0  not at all
//   1  the word overlaps the paragraph
//   2  it is on the line below the paragraph's text
//   3  the last word to go in went into this view
//   4  it is over the paragraph's last line
//   5  the paragraph covers half the word's box or more
//   6  it replaces a character of the text exactly (NOT YET)
//
// A score below four only says how well the word fits; four and above
// are taken as certain, which is why they go on to place the word even
// when only asked.
//
// The room a word may fall in is the view's bounds with the margins
// added (ten pixels left, thirty right) and one more line's worth of
// slack on the right, worked out from how wide the word's letters are -
// an ink word standing for a hundred pixels' worth.
//
// Two words written one after another belong together even when the
// second falls outside the paragraph the first made: when the last word
// to go in went into this view and this one was written beside it or on
// the line under it, the word is treated as though it had been written
// just after the last one in the text - five pixels past the end of the
// last character, on that line.  More than a second between them, or a
// word written above or to the left of the paragraph, and the tie is
// forgotten.
long
TParagraphView::HandleWord(const UniChar* text, ULong length, const Rect& box,
						   const Point& pt, ULong startTime, ULong endTime,
						   RefArg info, Boolean reallyDoIt, long* outOffset,
						   TUnitPublic* unit)
{
	if (fLines == nil)
		CreateAllCaches();
	if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
		return 0;

	Rect room = viewBounds;
	long score = 0;
	if (pt.v > room.top)
	{
		if (CoveredBy(&box, &room) >= 0x32)
			score = 5;
		else if (WordOnLastLine(box))
			score = 4;
	}

	// how much slack the word gets past the right edge: six times the
	// width of one of its letters, an ink word standing for a hundred
	long slack;
	if (text[0] == kInkWordChar)
		slack = 100;
	else
	{
		// (an empty word is divided by as the ROM's __rt_udiv does it: it
		// throws evt.ex.div0 rather than trapping.  The recognisers never
		// hand one over - the cursive reader did until the digit reader's
		// merge, ChunkSortAnswers, was there to fill in a number's reading)
		if (length == 0)
			Throw(exDivideByZero, nil, nil);
		slack = (short) (6 * (((ULong) (short) (box.right - box.left)) / length));
	}
	AddMarginsToBounds(&room);
	room.right = (short) (room.right + slack);

	// the tie to the word that went in before: gone when there are no
	// times at all (nobody is writing - this is the edit view asking what
	// text a point is in), when a second has passed since that word's ink
	// ended, or when this word was written above the paragraph or to the
	// left of it
	if ((startTime == 0 && endTime == 0)
		|| (ULong) (startTime - gLastAddedWordInkEndTime) > 0x3c
		|| (gLastAddedWordView == this
			&& (box.right < room.left || box.bottom < room.top)))
	{
		gLastAddedWordView = nil;
		gLastAddedWordBox.top = -0x8000;
		gLastAddedWordBox.bottom = -0x8000;
		gLastAddedWordBase.v = -0x8000;
	}
	if (score == 0 && GetLastAddedWordView() == this)
		score = 3;

	// one of the word's edges has to fall in the room
	if ((box.left >= room.left && box.left < room.right)
		|| (box.right > room.left && box.right <= room.right))
	{
		if (score == 0)
		{
			if (ISNIL(RefVar(GetVar(RSSYMonelineparagraphs)))
				&& WordOnLineBelowParagraph(box, pt))
				score = 2;
			if (score == 0)
			{
				if (!Overlaps(&box, &room))
					return 0;
				score = 1;
			}
		}
	}
	if (score == 0)
		return 0;
	if (!reallyDoIt && score <= 3)
		return score;

	// a word written beside the last one, or on the line under it, is
	// placed as though it had been written just after it in the text
	Rect wordBox = box;
	Point wordPt = pt;
	if (reallyDoIt && gLastAddedWordView == this
		&& startTime < gLastAddedWordAddTime)
	{
		Boolean adjacent = AdjacentBoxes(gLastAddedWordBox, box,
										 gLastAddedWordBase, pt, 1000);
		Boolean above = !adjacent && BoxAboveBox(gLastAddedWordBox, box);
		if (adjacent || above)
		{
			if (gLastAddedWordEndOffset == 0)
				gLastAddedWordEndOffset = TextLength();
			Rect endBox;
			OffsetToBounds(gLastAddedWordEndOffset - 1, &endBox);
			long middle = endBox.top
						  + (((short) (endBox.bottom - endBox.top)) >> 1);
			if (adjacent || middle > gLastAddedWordBase.v)
			{
				short width = (short) (box.right - box.left);
				wordBox.left = (short) (endBox.right + 5);
				wordBox.right = (short) (wordBox.left + width);
				wordBox.top = endBox.top;
				wordBox.bottom = endBox.bottom;
				// (ROM bug: the new middle is *added* to the point's h
				//  rather than replacing it.  It is harmless - nothing
				//  reads this point's h again, only its v, which
				//  AdjacentBoxes compares - so it is kept.)
				wordPt.h = (short) (wordBox.left + (width >> 1) + wordPt.h);
				wordPt.v = wordBox.bottom;
			}
		}
	}

	Finder finder;
	finder.fBox = wordBox;
	finder.fBase = wordPt;
	finder.fText = text;
	finder.fLength = length;
	finder.fView = nil;
	finder.fOffset = 0;
	finder.fReplaceLength = 0;
	finder.fExact = false;
	finder.fNewLine = false;
	finder.fReallyDoIt = reallyDoIt;
	finder.fTab = 0;
	finder.fUnit = unit;

	if (score == 2)
		SetFinderBelowParagraph(&finder);
	else
	{
		FindWordInParagraph(&finder);
		if (finder.fExact)
			score = 6;
		if (!reallyDoIt)
			return score;
	}
	if (finder.fLength == 0)
		return score;

	long at = finder.fOffset;
	if (score != 6)
	{
		// the word goes in at the caret instead when the writer has said
		// writing may come from anywhere and the caret is here (or the
		// word is on the line below); a word that brings its own data
		// frame is always placed rather than inserted
		Boolean atCaret = gRootView->fCaretView != nil
						  && NOTNIL(RefVar(GetPreference(RSSYMremotewriting)))
						  && (gRootView->fCaretView == this || score == 2);
		if (unit == nil || !atCaret || NOTNIL(info))
			AddWord(&finder, finder.fText, finder.fLength, info, &at);
		else
		{
			RefVar spec(Clone(RefVar(Rstarterinsertspec)));
			SetFrameSlot(spec, RSSYMinsertitems, RefVar(unit->WordInfo()));
			SetFrameSlot(spec, RSSYMaddspace,
						 RefVar(MAKEBOOLEAN(!IsMidWordLetterInsertion(this, unit))));
			InsertItemsAtCaret(spec);
			gAddWordInfo = false;
			at = RINT(RefVar(GetFrameSlot(spec, RSSYMinsertoffset)));
			finder.fLength = (ULong) RINT(RefVar(GetFrameSlot(spec, RSSYMreplacechars)));
		}
	}
	if (outOffset != nil)
		*outOffset = at;
	SaveAddedUnitBounds(wordBox, wordPt, endTime);
	gLastAddedWordEndOffset = (long) finder.fLength + at;
	return score;
}


/*------------------------------------------------------------------------------
	T h i n g s   p u t   i n t o   a   p a r a g r a p h
------------------------------------------------------------------------------*/

// A recognised word, a dropped clipping, an ink word split off another -
// everything that arrives at a paragraph from outside arrives the same
// way: a frame saying what to put in and where, sent to the view as
// command 0x4d.  The paragraph works out the text and the style runs
// the whole lot comes to, and hands them to the same HandleReplaceText
// that typing goes through.
//
// The items are appended one after another, with a delimiter worked out
// between each pair - which is why "one" and "two" dropped together
// come out as "one two" but "one" and "," come out as "one,".

/*------------------------------------------------------------------------------
	W h e r e   a   w o r d   w a s   w r i t t e n
------------------------------------------------------------------------------*/

// Before a paragraph can say whether a word written on the page belongs
// to it, it has to work out where the word is in relation to its own
// text - and to the word that went in before, because two words written
// one after the other on the same line belong together even when the
// second one falls outside the paragraph the first one made.
//
// That is what the "last added word" globals are: the view a word last
// went into, the box it was written in, the middle of its base line, and
// the two times (when the ink ended, and when the word was put in).
// `SaveAddedUnitBounds` records them and the paragraph reads them back
// through the three accessors.

Rect	gLastAddedWordBox = { 0, 0, 0, 0 };	// ROM 0x0c101718 gLastAddedWordBox
Point	gLastAddedWordBase = { 0, 0 };		// ROM 0x0c101728 gLastAddedWordBase
ULong	gLastAddedWordInkEndTime = 0;		// ROM 0x0c101720 gLastAddedWordInkEndTime


// ROM 0x0016c64c GetLastAddedWordBox__Fv
Rect*
GetLastAddedWordBox(void)
{
	return &gLastAddedWordBox;
}


// ROM 0x00170094 GetLastAddedWordBase__Fv
Point*
GetLastAddedWordBase(void)
{
	return &gLastAddedWordBase;
}


// ROM 0x00172e68 SaveAddedUnitBounds__14TParagraphViewFRC5TRectRC6TPointUl
// A word has gone into this paragraph: where it was written, the middle
// of its base line, when its ink ended, and - from the clock - when it
// went in.
void
TParagraphView::SaveAddedUnitBounds(const Rect& box, const Point& base, ULong inkEndTime)
{
	gLastAddedWordBox = box;
	gLastAddedWordBase = base;
	gLastAddedWordView = this;
	gLastAddedWordInkEndTime = inkEndTime;
	gLastAddedWordAddTime = Ticks();
}


// ROM 0x00172048 AddMarginsToBounds__FP5TRect
// The room a paragraph allows around itself when it is asked whether a
// word belongs to it: ten pixels to the left of it and thirty to the
// right, because writing runs on past the right edge far more often than
// it starts before the left one.
void
AddMarginsToBounds(Rect* bounds)
{
	bounds->left = (short) (bounds->left - 10);
	bounds->right = (short) (bounds->right + 30);
}


// ROM 0x0017b010 AdjacentBoxes__FRC5TRectT1RC6TPointT3l
// Whether `next` was written beside `box`, on the same line: it starts no
// more than five pixels to the left of the box's right edge (so a little
// overlap still counts) and no further than `gap` to the right of it, and
// the two base lines are within nineteen pixels of each other.  A box
// whose top is -32768 is the empty one nothing is beside.
Boolean
AdjacentBoxes(const Rect& box, const Rect& next, const Point& base,
			  const Point& nextBase, long gap)
{
	if (box.top == -0x8000 || next.top == -0x8000)
		return false;
	if (box.right - 5 < next.left && next.left - box.right <= gap)
	{
		long dv = base.v - nextBase.v;
		if (dv < 0)
			dv = -dv;
		if (dv < 0x13)
			return true;
	}
	return false;
}


// ROM 0x0017b08c BoxAboveBox__FRC5TRectT1
// Whether `box` is on the line above `below`: its own middle is above the
// other's top, and the gap between them is less than the taller of the
// two plus ten - so a word written on the very next line belongs to the
// one before it, and one written half a page down does not.
Boolean
BoxAboveBox(const Rect& box, const Rect& below)
{
	if (box.top == -0x8000 || below.top == -0x8000)
		return false;
	long height = (short) (box.bottom - box.top);
	long other = (short) (below.bottom - below.top);
	long taller = height > other ? height : other;
	return box.bottom - height / 2 < below.top
		   && below.top - box.bottom < taller + 10;
}


// ROM 0x00172008 WordOnLastLine__14TParagraphViewFRC5TRect
// Whether the word was written over the paragraph's last line - the
// bottom of the view, one line tall.
Boolean
TParagraphView::WordOnLastLine(const Rect& box)
{
	Rect last = viewBounds;
	last.top = (short) (last.bottom - fLineHeight);
	return Overlaps(&last, &box);
}


// ROM 0x001721ac BoundsOfLastLine__14TParagraphViewFP5TRect
// The box of the paragraph's last line, or the whole view when it has no
// lines laid out.
void
TParagraphView::BoundsOfLastLine(Rect* bounds)
{
	*bounds = viewBounds;
	if (fLines != nil && fLineCount > 0)
		*bounds = fLines[fLineCount - 1].fBounds;
}


// ROM 0x0016ba24 OffsetPastVisible__14TParagraphViewFv
// The offset of the first character the laid-out lines did not reach -
// the end of the last line - or -1 when they hold all of the text (or
// there are none).  A paragraph lays out only the lines that fit its
// bounds, so this is where the text runs out of room; ReflowText cuts
// a paragraph there to carry the rest over to the next page.
long
TParagraphView::OffsetPastVisible(void)
{
	long count = fLines != nil ? fLineCount : 0;
	long offset = -1;
	if (count > 0)
	{
		long end = fLines[count - 1].fEnd;
		RefVar text(Text());
		if (end < (long) ((ULong) (Length(text) - 2) >> 1))
			offset = end;
	}
	return offset;
}


// ROM 0x0017207c WordOnLineBelowParagraph__14TParagraphViewFRC5TRectRC6TPoint
// Whether the word was written on the line *after* the paragraph's text:
// the strip below the laid-out bounds, a line tall (or as tall as the
// last word that went in, whichever is more) and with the margins added.
//
// A word further down than that still belongs here when it carries on
// from the word that last went into this view - either on the line under
// it (`BoxAboveBox`) or beside it with a thousand pixels of slack
// (`AdjacentBoxes`), which is what lets someone write a long line across
// a page and have it all end up in one paragraph.
Boolean
TParagraphView::WordOnLineBelowParagraph(const Rect& box, const Point& base)
{
	Rect below = fCachedBounds;
	below.top = below.bottom;
	long height = 0;
	if (GetLastAddedWordView() == this)
		height = (short) (gLastAddedWordBox.bottom - gLastAddedWordBox.top);
	if (fLineHeight > height)
		height = fLineHeight;
	below.bottom = (short) (below.bottom + height);
	AddMarginsToBounds(&below);

	if (box.top >= below.top && box.top < below.bottom)
		return true;
	if (box.top < fCachedBounds.bottom)
		return false;
	if (GetLastAddedWordView() != this)
		return false;
	if (BoxAboveBox(gLastAddedWordBox, box))
		return true;
	return AdjacentBoxes(gLastAddedWordBox, box, gLastAddedWordBase, base, 1000) != 0;
}


// ROM 0x000edc24 GetAppendDelimiter__FPUsPCUsT2CUlT4
// What goes between two pieces of text being joined: a space, or
// nothing.  Nothing when either side is empty, when there is white
// space at the join already, when the left ends in a hyphen or an open
// bracket, when the right starts with a hyphen, when the right is a
// single punctuation mark that is not an open bracket, or when the left
// ends in a punctuation mark standing on its own.
void
GetAppendDelimiter(UniChar* out, const UniChar* left, const UniChar* right,
				   const ULong leftLength, const ULong rightLength)
{
	out[0] = 0;
	if (leftLength == 0 || rightLength == 0)
		return;
	UniChar last = left[leftLength - 1];
	UniChar first = right[0];
	if (IsWhiteSpace(last) || IsWhiteSpace(first))
		return;
	// whether the left's last character is a word of its own
	Boolean alone;
	if (leftLength == 1)
		alone = true;
	else
		alone = IsWhiteSpace(left[leftLength - 2]) != 0;

	if (last == U_CONST_CHAR('-') || last == U_CONST_CHAR('('))
		return;
	if (first == U_CONST_CHAR('-'))
		return;
	if (rightLength == 1 && IsPunctSymbol(&first, 0) && first != U_CONST_CHAR('('))
		return;
	if (alone && IsPunctSymbol(&last, 0))
		return;
	out[0] = U_CONST_CHAR(' ');
	out[1] = 0;
}


// ROM 0x0016f954 (unnamed) - GrowInsertText
// Room for `add` more characters in the text being built, rounded up to
// a multiple of forty so that a long insert does not resize once per
// item.  `used` is advanced whether or not the binary grew.
static void
GrowInsertText(RefArg text, long* used, long add)
{
	long size = Length(text);
	long need = *used + add;
	if (need > size / 2)
		SetLength(text, (need + (0x28 - need % 0x28)) * (long) sizeof(UniChar));
	*used = need;
}


// ROM 0x0016f9b0 (unnamed) - GrowInsertStyles
// The same for the styles array, rounded up to a multiple of ten - five
// runs, each being a length and a style.
static void
GrowInsertStyles(RefArg styles, long* used, long add)
{
	long size = Length(styles);
	long need = *used + add;
	if (need > size)
		SetLength(styles, need + (10 - need % 10));
	*used = need;
}


// ROM 0x0016fa08 EqualStyles__FRC6RefVarT1
// Whether two style runs are the same style, which is what decides
// whether a new run is needed at all.  Two font frames are the same when
// their size, face and family are; anything else - a packed font
// integer, an ink word - only when it is the same object.
Boolean
EqualStyles(RefArg a, RefArg b)
{
	if (!IsFrame(a) || !IsFrame(b))
		return EQRef(a, b);
	if (!EQRef(RefVar(GetFrameSlot(a, RSSYMsize)), RefVar(GetFrameSlot(b, RSSYMsize))))
		return false;
	if (!EQRef(RefVar(GetFrameSlot(a, RSSYMface)), RefVar(GetFrameSlot(b, RSSYMface))))
		return false;
	return EQRef(RefVar(GetFrameSlot(a, RSSYMfamily)),
				 RefVar(GetFrameSlot(b, RSSYMfamily)));
}


// ROM 0x0016fba8 (unnamed) - AppendStyleRun
// Another run of `count` characters in `style`, merged into the last run
// when it is the same style.  The array is pairs: a length and a style.
static void
AppendStyleRun(RefArg styles, long* used, long count, RefArg style)
{
	if (*used != 0)
	{
		RefVar last(GetArraySlotRef(styles, *used - 1));
		if (EqualStyles(style, last))
		{
			long at = *used - 2;
			long length = RINT(RefVar(GetArraySlotRef(styles, at)));
			SetArraySlot(styles, at, RefVar(MAKEINT(length + count)));
			return;
		}
	}
	long at = *used;
	GrowInsertStyles(styles, used, 2);
	SetArraySlot(styles, at, RefVar(MAKEINT(count)));
	SetArraySlot(styles, at + 1, style);
}


// ROM 0x0016fcc8 (unnamed) - StyleBeforeIndex
// The style of the last run that is not an ink word, working backwards
// from `index` a run at a time.  An ink word is not a style anything
// else can be written in, so a run that follows one takes the style of
// whatever was being written before it.
static Ref
StyleBeforeIndex(RefArg styles, long index)
{
	if (Length(styles) == 0)
		return NILREF;
	RefVar found(NILREF);
	while (index >= 0)
	{
		RefVar style(GetArraySlotRef(styles, index));
		if (!IsInkWord(style))
		{
			found = style;
			break;
		}
		index -= 2;					// a run is a length and a style
	}
	return found;
}


// ROM 0x0016fd7c (unnamed) - AppendInsertItem
// One item appended to the text and styles being built, with the
// delimiter that belongs in front of it.
//
// The delimiter is worked out between the item and whatever comes before
// it: the paragraph's own text up to the insertion point for the first
// item (`lastText` with `lastLength`), and the text built so far for
// every item after that - which is why the caller drops `lastText` once
// the first item is in.  How long it came to is answered through
// `outDelimiter`, because the correction information has to know where
// the item really starts.
//
// An item that brings its own style runs has them copied in as they
// stand.  One that brings a single style - or none, in which case the
// last run's style is taken, and failing that the default - gets one run
// over the whole of it.
static void
AppendInsertItem(RefArg lastText, long lastLength, RefArg text, RefArg styles,
				 long* usedText, long* usedStyles, Boolean addSpace,
				 RefArg itemText, RefArg itemStyles, RefArg defaultStyle,
				 long* outDelimiter)
{
	if (!addSpace)
		*outDelimiter = 0;
	else
	{
		UniChar delimiter[6];
		if (NOTNIL(lastText))
			GetAppendDelimiter(delimiter, GetCString(lastText), GetCString(itemText),
							   (ULong) lastLength,
							   (ULong) Ustrlen(GetCString(itemText)));
		else
			GetAppendDelimiter(delimiter, GetCString(text), GetCString(itemText),
							   (ULong) Ustrlen(GetCString(text)),
							   (ULong) Ustrlen(GetCString(itemText)));
		*outDelimiter = Ustrlen(delimiter);
		if (*outDelimiter > 0)
		{
			long at = *usedText;
			GrowInsertText(text, usedText, *outDelimiter);
			BlockMove(delimiter, (UniChar*) BinaryData(text) + at,
					  *outDelimiter * (long) sizeof(UniChar));
			AppendStyleRun(styles, usedStyles, *outDelimiter, defaultStyle);
		}
	}

	// the item's own characters, without its terminator
	long count = Length(itemText) / (long) sizeof(UniChar) - 1;
	long at = *usedText;
	GrowInsertText(text, usedText, count);
	BlockMove(BinaryData(itemText), (UniChar*) BinaryData(text) + at,
			  count * (long) sizeof(UniChar));

	if (IsArray(itemStyles))
	{
		long runs = Length(itemStyles);
		long first = *usedStyles;
		GrowInsertStyles(styles, usedStyles, runs);
		ArrayMunger(styles, first, runs, itemStyles, 0, runs);
	}
	else
	{
		RefVar style(itemStyles);
		if (ISNIL(style))
			style = StyleBeforeIndex(styles, *usedStyles - 1);
		if (ISNIL(style))
			style = defaultStyle;
		AppendStyleRun(styles, usedStyles, count, style);
	}
}


// ROM 0x00170064 (unnamed) - InsertItemCount
// The items may be an array of them or just the one.
static long
InsertItemCount(RefArg items)
{
	return IsArray(items) ? Length(items) : 1;
}


// ROM 0x00170030 (unnamed) - InsertItemAt
static Ref
InsertItemAt(RefArg items, long index)
{
	return IsArray(items) ? GetArraySlotRef(items, index) : (Ref) items;
}


// ROM 0x0007623c NewCorrectInfo__Fv
// The frame the corrector keeps what it would need to put a word right
// in: a clone of protoCorrectInfo with an empty `info` array.
Ref
NewCorrectInfo(void)
{
	RefVar info(Clone(RefVar(Rprotocorrectinfo)));
	SetFrameSlot(info, RSSYMinfo, RefVar(MakeArray(0)));
	return info;
}


// (host) The one character an ink word stands as, as a string object -
// the ROM builds it by hand at each of the two places that need one.
static Ref
MakeInkWordCharacter(void)
{
	RefVar one(AllocateBinary(RSSYMstring, 2 * (long) sizeof(UniChar)));
	UniChar* chars = (UniChar*) BinaryData(one);
	chars[0] = kInkWordChar;
	chars[1] = 0;
	return one;
}


// ROM 0x001700a0 HandleInsertItems__14TParagraphViewFRC6RefVar
// The command a paragraph answers when something is to be put into it.
// The spec says what (`insertItems`, one item or an array of them),
// where (`insertOffset`, the caret's offset when there is none), how
// much to take out (`replaceChars`, the selection's length when there
// is none), and whether to space the items apart (`addSpace`), to make
// it undoable (`undoable`) and to move the caret after it (`moveCaret`).
// `defaultFontSpec` is the style anything that brings none is written
// in; with none, whatever the paragraph would use at that offset.
//
// The items are gathered into one text binary and one styles array -
// with a delimiter worked out between each pair, so that two words
// dropped together come out with a space between them but a word and a
// comma do not - and the lot is handed to the same HandleReplaceText
// that typing goes through, which is what gives it its undo.
//
// The kinds of item:
//
//   a string              put in as it is
//   an ink word           one 0xF701 character, the word as its style
//   a frame with `text`   its text, with its `styles`
//   a frame with `words`  what the recogniser made of a piece of
//                         writing: the first reading's `word`, unless
//                         the frame is flagged as ink, in which case
//                         its `ink` - or its `strokes`, packed - goes
//                         in as one 0xF701 character
//
// Anything else is passed over.  A word-info frame also leaves a note in
// the correction information - where it landed, whether it was ink and
// whether it carries training data - so that the corrector can offer
// alternatives for it later.
//
// A *rich* string - one with writing in it - comes apart first
// (TRichString::MakeParagraphTextSlot / MakeParagraphStylesSlot), so
// its writing arrives as style runs rather than being lost.
Boolean
TParagraphView::HandleInsertItems(RefArg spec)
{
	RefVar items(GetFrameSlot(spec, RSSYMinsertitems));
	RefVar text(AllocateBinary(RSSYMstring, 0));
	RefVar styles(AllocateArray(RSSYMstyles, 0));
	long usedText = 0;
	long usedStyles = 0;
	RefVar correctInfo;

	// a paragraph whose text flags have bit 1 never spaces its items
	Boolean addSpace = (TextFlags() & 2) == 0;
	if (addSpace && FrameHasSlot(spec, RSSYMaddspace))
		addSpace = NOTNIL(RefVar(GetFrameSlot(spec, RSSYMaddspace)));

	RefVar slot(GetFrameSlot(spec, RSSYMinsertoffset));
	long insertOffset = ISNIL(slot) ? fCaretOffset : RINT(slot);

	Boolean undoable = true;
	if (FrameHasSlot(spec, RSSYMundoable))
		undoable = NOTNIL(RefVar(GetFrameSlot(spec, RSSYMundoable)));
	Boolean moveCaret = true;
	if (FrameHasSlot(spec, RSSYMmovecaret))
		moveCaret = NOTNIL(RefVar(GetFrameSlot(spec, RSSYMmovecaret)));

	// how much goes out: what the spec says, or whatever is selected
	long replaceChars;
	slot = GetFrameSlot(spec, RSSYMreplacechars);
	if (NOTNIL(slot))
		replaceChars = RINT(slot);
	else
	{
		RefVar hilite(FirstHilite());
		if (ISNIL(hilite))
			replaceChars = 0;
		else
		{
			TParagraphHilite* range = (TParagraphHilite*) RefToAddress(hilite);
			replaceChars = range->fEnd - range->fStart;
		}
	}

	// the selection goes as soon as the insert is under way, unless
	// something is holding on to it
	if (!gRootView->GetPreserveHilites())
	{
		TView* owner = GetEnclosingEditView();
		if (owner == nil)
			owner = this;
		owner->RemoveAllHilites();
	}

	RefVar defaultStyle(GetFrameSlot(spec, RSSYMdefaultfontspec));
	if (ISNIL(defaultStyle))
		defaultStyle = GetStyleForInsertion(insertOffset, true, true);

	long count = InsertItemCount(items);
	// the text the first item's delimiter is measured against; dropped
	// once an item is in, so that the rest measure against what has been
	// built
	RefVar before(Text());
	long delimiter = 0;

	for (long i = 0; i < count; i++)
	{
		RefVar item(InsertItemAt(items, i));
		RefVar itemText;
		RefVar itemStyles;

		if (IsInstance(item, RSSYMstring))
		{
			if (!IsRichString(item))
				AppendInsertItem(before, insertOffset, text, styles,
								 &usedText, &usedStyles, addSpace,
								 item, itemStyles, defaultStyle, &delimiter);
			else
			{
				// a string with writing in it comes apart into the two
				// halves a paragraph keeps: the text with each word of
				// writing standing as one character, and the styles that
				// carry the words themselves
				TRichString rich(item);
				itemText = rich.MakeParagraphTextSlot();
				itemStyles = rich.MakeParagraphStylesSlot(defaultStyle);
				AppendInsertItem(before, insertOffset, text, styles,
								 &usedText, &usedStyles, addSpace,
								 itemText, itemStyles, defaultStyle, &delimiter);
			}
		}
		else if (IsInkWord(item))
		{
			itemText = MakeInkWordCharacter();
			itemStyles = AllocateArray(RSSYMstyles, 2);
			SetArraySlot(itemStyles, 0, RefVar(MAKEINT(1)));
			SetArraySlot(itemStyles, 1, item);
			AppendInsertItem(before, insertOffset, text, styles,
							 &usedText, &usedStyles, addSpace,
							 itemText, itemStyles, defaultStyle, &delimiter);
		}
		else if (IsFrame(item) && FrameHasSlot(item, RSSYMtext))
		{
			itemText = GetFrameSlot(item, RSSYMtext);
			itemStyles = GetFrameSlot(item, RSSYMstyles);
			AppendInsertItem(before, insertOffset, text, styles,
							 &usedText, &usedStyles, addSpace,
							 itemText, itemStyles, defaultStyle, &delimiter);
		}
		else if (IsFrame(item) && FrameHasSlot(item, RSSYMwords))
		{
			// what the recogniser made of a piece of writing
			long startedAt = usedText;
			RefVar scratch(GetFrameSlot(item, RSSYMwords));
			Boolean asInk = ISNIL(scratch);
			if (!asInk)
				asInk = (RINT(RefVar(GetFrameSlot(item, RSSYMflags)))
						 & kWordInfoIsInk) != 0;

			if (!asInk)
			{
				// the best reading
				scratch = GetArraySlotRef(scratch, 0);
				itemText = GetFrameSlot(scratch, RSSYMword);
				AppendInsertItem(before, insertOffset, text, styles,
								 &usedText, &usedStyles, addSpace,
								 itemText, itemStyles, defaultStyle, &delimiter);
			}
			else
			{
				// the writing itself: already packed, or a bundle of
				// strokes to pack
				scratch = GetFrameSlot(item, RSSYMink);
				if (ISNIL(scratch))
				{
					scratch = GetFrameSlot(item, RSSYMstrokes);
					if (NOTNIL(scratch))
						scratch = CompressStrokes(scratch);
				}
				if (NOTNIL(scratch))
				{
					if (!IsInkWord(scratch))
						scratch = GetFrameSlot(scratch, RSSYMink);
					itemText = MakeInkWordCharacter();
					itemStyles = AllocateArray(RSSYMstyles, 2);
					SetArraySlot(itemStyles, 0, RefVar(MAKEINT(1)));
					SetArraySlot(itemStyles, 1, scratch);
					AppendInsertItem(before, insertOffset, text, styles,
									 &usedText, &usedStyles, addSpace,
									 itemText, itemStyles, defaultStyle, &delimiter);
				}
			}

			// where it landed, and what the corrector would need to know
			// (a word that could not be put in at all still gets a note,
			//  an empty one)
			long length = NOTNIL(itemText) ? Ustrlen(GetCString(itemText)) : 0;
			SetFrameSlot(item, RSSYMstart, RefVar(MAKEINT(startedAt + delimiter)));
			SetFrameSlot(item, RSSYMstop,
						 RefVar(MAKEINT(startedAt + length + delimiter)));
			long noteFlags = IsInkWord(scratch) ? 10 : 2;
			if (NOTNIL(RefVar(GetFrameSlot(item, RSSYMunitdata))))
				noteFlags |= 1;
			SetFrameSlot(item, RSSYMflags, RefVar(MAKEINT(noteFlags)));
			if (ISNIL(correctInfo))
				correctInfo = NewCorrectInfo();
			AddArraySlot(RefVar(GetFrameSlot(correctInfo, RSSYMinfo)), item);
		}

		// the next item measures its delimiter against what has been
		// built rather than against the paragraph
		before = RefVar(NILREF);
	}

	// and a delimiter after the last item, when the paragraph goes on
	if (addSpace)
	{
		RefVar after(Text());
		long length = Ustrlen(GetCString(after));
		if (insertOffset + replaceChars < length
			&& !IsWhiteSpace(GetCString(after)[insertOffset + replaceChars]))
		{
			// (the ROM looks at the character after what is being
			//  replaced but measures the delimiter against the text from
			//  the insertion point, so a replacement is measured against
			//  the characters it is about to take out)
			UniChar trailing[6];
			GetAppendDelimiter(trailing, GetCString(text),
							   GetCString(after) + insertOffset,
							   (ULong) usedText, (ULong) (length - insertOffset));
			long n = Ustrlen(trailing);
			long at = usedText;
			GrowInsertText(text, &usedText, n);
			BlockMove(trailing, (UniChar*) BinaryData(text) + at,
					  n * (long) sizeof(UniChar));
			AppendStyleRun(styles, &usedStyles, n, defaultStyle);
		}
	}

	SetLength(text, usedText * (long) sizeof(UniChar));
	SetLength(styles, usedStyles);

	RefVar cmd(MakeCommand(aeReplaceText, this, fId));
	CommandSetText(cmd, text);
	RefVar params(AllocateArray(RSSYMarray, 7));
	SetArraySlot(params, 0, RefVar(MAKEINT(insertOffset)));
	SetArraySlot(params, 1, RefVar(MAKEINT(replaceChars)));
	SetArraySlot(params, 2, RefVar(MAKEINT(usedText)));
	SetArraySlot(params, 3, RefVar(MAKEINT(0)));
	SetArraySlot(params, 4, RefVar(MAKEINT(undoable)));
	SetArraySlot(params, 5, RefVar(MAKEINT(moveCaret)));
	SetArraySlot(params, 6, RefVar(MAKEINT(0)));
	SetFrameSlot(cmd, RSSYMparams, params);

	RefVar frame(styles);
	if (NOTNIL(correctInfo))
	{
		frame = Clone(RefVar(Rcanonicalstyles));
		SetFrameSlot(frame, RSSYMstyles, styles);
		SetFrameSlot(frame, RSSYMcorrectinfo, correctInfo);
	}
	CommandSetFrameParameter(cmd, frame);
	HandleReplaceText(cmd);

	// what actually went in, for whoever asked
	SetFrameSlot(spec, RSSYMinsertoffset, RefVar(MAKEINT(insertOffset)));
	SetFrameSlot(spec, RSSYMreplacechars, RefVar(MAKEINT(usedText)));
	return true;
}


// ROM 0x00170e90 (unnamed) - SendInsertCommand
// The spec sent to a view as command 0x4d.  ==> whether it took it.
static Boolean
SendInsertCommand(TView* view, ULong id, RefArg spec)
{
	if (view == nil)
		return false;
	RefVar cmd(Clone(RefVar(Rprotocommand)));
	SetFrameSlot(cmd, RSSYMid, RefVar(MAKEINT(id)));
	SetFrameSlot(cmd, RSSYMreceiver, RefVar(view->fContext));
	SetFrameSlot(cmd, RSSYMframeparameter, spec);
	return view->DoCommand(cmd);
}


// ROM 0x00170f7c DoInsertItems__FP5TViewRC6RefVarUcT3lT5T3T2
// Items put into a named view: the spec built out of the starter and
// sent on.  ==> the spec, which comes back saying what actually went in.
Ref
DoInsertItems(TView* view, RefArg items, Boolean addSpace, Boolean undoable,
			  long insertOffset, long replaceChars, Boolean moveCaret,
			  RefArg defaultFontSpec)
{
	RefVar spec(Clone(RefVar(Rstarterinsertspec)));
	SetFrameSlot(spec, RSSYMinsertitems, items);
	SetFrameSlot(spec, RSSYMaddspace, RefVar(MAKEBOOLEAN(addSpace)));
	SetFrameSlot(spec, RSSYMundoable, RefVar(MAKEBOOLEAN(undoable)));
	SetFrameSlot(spec, RSSYMinsertoffset, RefVar(MAKEINT(insertOffset)));
	SetFrameSlot(spec, RSSYMreplacechars, RefVar(MAKEINT(replaceChars)));
	SetFrameSlot(spec, RSSYMmovecaret, RefVar(MAKEBOOLEAN(moveCaret)));
	SetFrameSlot(spec, RSSYMdefaultfontspec, defaultFontSpec);
	SendInsertCommand(view, kInsertItemsCommand, spec);
	return spec;
}


// ROM 0x00171168 InsertItemsAtCaret__FRC6RefVar
// The same, put wherever the caret is.  Nothing takes it when there is
// no caret, or when the view the caret is in will not have it, and then
// the machine beeps.  ==> whether anything took it.
Boolean
InsertItemsAtCaret(RefArg spec)
{
	TView* caret = gRootView->fCaretView;
	Boolean taken = false;
	if (caret != nil)
		taken = SendInsertCommand(caret, kInsertItemsCommand, spec);
	if (!taken)
		gRootView->RunScript(RSSYMsysbeep, RefVar(MakeArray(0)), false, nil);
	return taken;
}


// ROM 0x00175964 CheckAndDoJoin__14TParagraphViewFR6TPointN21
// A caret drawn upside down across a line - the join gesture - closes up
// the white space between the two words its arms are over.
//
// Its two arms have to be within fifteen pixels of each other vertically,
// and the line is the one whose baseline is nearest the right-hand arm
// (FindLineForWord over a box from the caret's point to that arm).  Both
// arms then have to lie within half an ascent of that line's baseline,
// which is what keeps a caret drawn between two lines from joining
// either; and both are taken to the baseline before the characters under
// them are asked for, so a badly drawn caret still picks the characters
// its arms cross.
//
// From those two characters the gesture works outwards: an arm on white
// space steps back one (the character the space follows), and an arm on
// the last character steps forward one.  Then the first white space at or
// after the left character, as long as it comes before the right one, is
// the run that goes - however many spaces, tabs and returns follow it.
// So a join drawn over "one   two" takes all three spaces, and one drawn
// over a word takes nothing.
//
// (host: the ROM removes the run through DoInsertItems, the same path a
// dropped item takes; InsertStyledText is the host's equivalent - it
// makes the same aeReplaceText command, with the same undo.)
//
// Two ink words are joined instead of white space being closed up: when
// both characters are 0xf701 - the ink word character, and the only one
// of the three the ROM's IsInkChar takes that is joinable - the two
// words are merged into one, all the way down to the strokes and back,
// and that one replaces both of them and everything between.
long
TParagraphView::CheckAndDoJoin(Point& armA, Point& point, Point& armB)
{
	long arms = armA.v - armB.v;
	if (arms < 0)
		arms = -arms;
	if (arms > 15)
		return 0;
	Point left = armA.h < armB.h ? armA : armB;
	Point right = armA.h < armB.h ? armB : armA;

	Rect written;
	SetRect(&written, point.h, point.v, right.h, right.v);
	long index = FindLineForWord(written, 4);
	if (index < 0)
		return 0;
	const LineInfo& line = fLines[index];
	long half = line.fAscent / 2;
	long baseline = line.fBounds.top + line.fAscent;
	long fromLeft = baseline - left.v;
	long fromRight = baseline - right.v;
	// both arms within half an ascent of the baseline, below it and above
	if ((fromLeft > half ? fromLeft : fromRight) > half)
		return 0;
	if ((fromLeft >= -half ? fromRight : fromLeft) < -half)
		return 0;

	left.v = (short) baseline;
	right.v = (short) baseline;
	long start = PointToOffset(left);
	long end = PointToOffset(right);

	RefVar textRef(Text());
	TRichString rich(textRef);
	const UniChar* text = rich.GrabPtr();
	if (IsWhiteSpace(text[start]) && start != 0)
		start--;
	if (IsWhiteSpace(text[end]) && text[end] != 0)
		end++;
	long count = 0;
	Boolean join = false;
	if (text[start] == kJoinableInkChar && text[end] == kJoinableInkChar)
	{
		// Two words of writing joined into one.  Nothing but white space
		// may lie between them - a join drawn across a word in between
		// is not a join of the two outer ones - and then the two ink
		// words are merged, all the way down to the strokes and back,
		// and the one that comes out replaces both of them and
		// everything that was between.
		long after = start + 1;
		while (IsWhiteSpace(text[after]))
			after++;
		if (after != end)
		{
			rich.ReleasePtr();
			return 0;
		}
		RefVar merged(MergeInk(RefVar(GetInkAt(this, start)),
							   RefVar(GetInkAt(this, end))));
		RefVar ink(GetFrameSlot(merged, RSSYMink));
		AdjustInkWordXHeight(ink, ViewExpectsNumbers(this));
		rich.ReleasePtr();
		// (host: the ROM puts the word in through DoInsertItems; one ink
		//  word is one character with the word as its style run, which
		//  is what InsertStyledText takes)
		UniChar one = kInkWordChar;
		RefVar styles(AllocateArray(RSSYMstyles, 2));
		SetArraySlot(styles, 0, MAKEINT(1));
		SetArraySlot(styles, 1, ink);
		InsertStyledText((ULong) start, &one, 1, styles, RefVar(NILREF), 0,
						 (ULong) (end - start + 1), false);
		return 1;
	}
	else if (!IsWhiteSpace(text[start]) && !IsWhiteSpace(text[end]))
	{
		do
		{
			start++;
			if (start >= end)
				break;
		} while (!IsWhiteSpace(text[start]));
		if (start != end)
		{
			for (long i = start; IsWhiteSpace(text[i]); i++)
				count++;
			join = true;
		}
	}
	rich.ReleasePtr();
	if (!join)
		return 0;
	UniChar none = 0;
	InsertStyledText((ULong) start, &none, 0, RefVar(NILREF), RefVar(NILREF), 0, (ULong) count, false);
	return 1;
}


// ROM 0x0016a490 HiliteText__14TParagraphViewFlT1Uc
// A range selected by its start and length.  (The ROM's is three
// instructions that fall into MakeHilite, and they throw the flag they
// were given away: an empty range always moves the caret.)
void
TParagraphView::HiliteText(long start, long length, Boolean /*caretOnEmpty*/)
{
	MakeHilite(start, start + length, true);
}


// ROM 0x00176bd4 HandleLineGesture__14TParagraphViewFlR6TPointT2
// A line drawn up or down through the selected text changes its case:
// up (angle 0) makes it upper case, down (180) lower case.  A line
// gesture's angle is measured from the vertical - PtsToAngle divides dx
// by dy, so a line straight up is 0 and one straight down is 180 - and
// the editor also takes the two horizontal ones (90 and -90) and offers
// them to its other children.  Only a paragraph that has a selection
// takes one at all.
//
// A line drawn upwards is turned round first, so either way `from` is
// the end at the top and `to` the end at the bottom.  The box of the
// two has to touch the view (grown six pixels to the left), and then
// each selection in turn is asked whether the line spans it: the line
// must begin at or above the selection's top and end at or below its
// bottom, and be no taller than three selections plus the slack - fifty
// pixels at the least, and twelve more each way when the whole
// paragraph is selected, since a line drawn over everything need not be
// neat.
//
// What changes case is either the whole selection or just its first
// character: the first character alone when the line's middle is within
// six pixels of that character's box.  So a line drawn through the
// first letter of a selected word capitalises the letter, and one drawn
// through the middle of the word capitalises the word.  The text goes
// back in through the same replace command any other edit uses, with
// the styles it had, and the range is selected again afterwards.
//
// A hilite's bounding box is in the view's own coordinates, so the two
// ends and the middle are moved there before they are compared with it -
// and the middle is moved back when it is compared with the character
// box OffsetToBounds answers, which is the port's.  The ROM moves the
// caller's own points, which are the editor's, so the next view it
// offers the gesture to sees them already moved; that is ported as it
// stands, and so is the turning round of a line drawn upwards.
//
// (host: the ROM replaces the range through DoInsertItems, the same path
// a dropped item takes, carrying a canonicalTextAndStyles frame;
// InsertStyledText is this reconstruction's equivalent - the same
// aeReplaceText command with the same undo, and it takes the text and
// the styles as they are.)
long
TParagraphView::HandleLineGesture(long angle, Point& from, Point& to)
{
	// where the line's middle is, before either end is touched
	Point mid;
	mid.h = (short) ((from.h + to.h) / 2);
	mid.v = (short) ((from.v + to.v) / 2);
	RefVar first(FirstHilite());
	long slack = NOTNIL(first) && IsCompletelyHilited(first) ? 12 : 0;
	Rect grown = viewBounds;
	grown.left = (short) (grown.left - 6);
	if ((angle != 0 && angle != 180) || !Hilited())
		return 0;
	if (angle == 0)
	{
		Point swap = from;
		from = to;
		to = swap;
	}
	if (from.v >= to.v)
		return 0;
	Rect box;
	box.left = from.h < to.h ? from.h : to.h;
	box.right = from.h < to.h ? to.h : from.h;
	box.top = from.v < to.v ? from.v : to.v;
	box.bottom = from.v < to.v ? to.v : from.v;
	if (!Overlaps(&grown, &box))
		return 0;
	// into the view's own coordinates, where the hilites are
	OffsetRect(&box, -viewBounds.left, -viewBounds.top);
	mid.h = (short) (mid.h - viewBounds.left);
	mid.v = (short) (mid.v - viewBounds.top);
	from.h = (short) (from.h - viewBounds.left);
	from.v = (short) (from.v - viewBounds.top);
	to.h = (short) (to.h - viewBounds.left);
	to.v = (short) (to.v - viewBounds.top);

	long done = 0;
	HiliteLoop loop(this);
	while (loop.Next())
	{
		TParagraphHilite* hilite = (TParagraphHilite*) loop.fCurrent;
		if (hilite == nil)
			continue;
		SetupArea(hilite);		// (the ROM leaves this to the drawing, which has happened by the time a pen gesture arrives)
		Rect hbox = hilite->fBounds;
		hbox.left = (short) (hbox.left - 6);
		long reach = (hilite->fBounds.bottom - hilite->fBounds.top) * 3 + slack * 2;
		if (reach < 50)
			reach = 50;
		if (!Overlaps(&hbox, &box))
			continue;
		// the line has to cover the selection, and not by too much
		if (from.v > hilite->fBounds.top)
			continue;
		if (hilite->fBounds.bottom > to.v || to.v - from.v > reach)
			continue;

		long start = hilite->fStart;
		long length = hilite->fEnd - start;
		// the selection and the character after it (the ROM copies one
		// past the range, and terminates the copy itself)
		UniChar* chars = new UniChar[length + 1];
		RefVar textRef(Text());
		TRichString rich(textRef);
		BlockMove(rich.GrabPtr() + start, chars, (length + 1) * sizeof(UniChar));
		rich.ReleasePtr();
		long lead = 0;
		long copied = (long) Ustrlen(chars);
		while (IsWhiteSpace(chars[lead]) && lead < copied)
			lead++;
		// the whole selection, or just its first character when the
		// line's middle is over that character's box
		long count = length;
		Rect firstChar;
		OffsetToBounds(start + lead, &firstChar);
		long right = firstChar.right > firstChar.left + 6 ? firstChar.right : firstChar.left + 6;
		long midH = mid.h + viewBounds.left;		// the character box is the port's
		if (midH >= firstChar.left - 6 && right >= midH)
			count = lead + 1;
		chars[count] = 0;
		if (angle == 0)
			UppercaseText(chars, 0x7fffffff);
		else
			LowercaseText(chars, 0x7fffffff);
		RefVar styles(GetStylesOfRange(start, length, false));
		InsertStyledText((ULong) start, chars, (ULong) count, styles, RefVar(NILREF), 0, (ULong) count, false);
		HiliteText(start, length, true);
		delete[] chars;
		gRootView->fDirtyFlag = true;
		done = 1;
		break;
	}
	return done;
}


// ROM 0x001753b4 HandleCaret__14TParagraphViewFUllR6TPointN33
// A caret gesture offered to the paragraph.  Its point has to be within
// the view grown by the height of the caret's arms - a caret drawn just
// above or below a line still belongs to it - and an upside-down caret is
// placed by its arm instead of its point.  A caret pointing right has to
// be in the left margin: within ten pixels outside the view's left edge
// or twenty inside it.
//
// What it then does depends on the kind and the angle: the plain caret
// (2) pointing up is one space, pointing right a line break, pointing
// down closes up the space between two words; the caret with a tail (3) is
// as many spaces as the tail is wide, or as many line breaks as it is
// tall; the open one (5) is line breaks unless the view is one line only;
// the flat one (6), which only a one-line view takes, is a single space
// when both its arms are short.
//
// NOT YET RECONSTRUCTED: the ink half of CheckAndDoJoin, which joins two
// ink words rather than closing up the space between two of text.
long
TParagraphView::HandleCaret(ULong kind, long angle, Point& armA, Point& point,
							Point& armB, Point& tail)
{
	Rect box = viewBounds;
	long reach = point.v - armA.v;
	if (reach < 0)
		reach = -reach;
	InsetRect(&box, 0, -reach);
	short wasBottom = box.bottom;
	Point at = point;
	if (kind == 2)
	{
		if (angle == 180)
			at = armA;
	}
	else if (kind == 6)
	{
		box.right = (short) (box.right + 1000);
		box.bottom = wasBottom;
	}
	if (!PtInRect(at, &box))
		return 0;
	if ((kind == 2 || kind == 3) && angle == 90)
	{
		// it has to be in the left margin
		long outside = viewBounds.left - point.h;
		long limit = 10;
		if (outside < 11)
		{
			outside = point.h - viewBounds.left;
			limit = 20;
		}
		if (outside > limit)
			return 0;
	}

	long width = 0;
	long height = 0;
	Boolean typed = true;
	Boolean vertical = false;
	if (kind == 2)
	{
		if (angle == 0)
			width = -1;
		else if (angle == 90)
		{
			height = -1;
			vertical = true;
		}
		else if (angle == 180)
			return CheckAndDoJoin(armA, point, armB);
		else
			return 0;
	}
	else if (kind == 3)
	{
		if (angle == 0)
		{
			width = tail.h - point.h;
			if (width < 0)
				width = -width;
		}
		else
		{
			height = tail.v - point.v;
			if (height < 0)
				height = -height;
			vertical = angle == 90;
		}
	}
	else if (kind == 5)
	{
		if ((fViewJustify & vjOneLineOnly) != 0)
			return 0;
		height = tail.v - point.v;
		if (height < 0)
			height = -height;
		vertical = angle == 90;
	}
	else if (kind == 6)
	{
		if ((fViewJustify & vjOneLineOnly) == 0)
			return 0;
		long armOne = CheapDistance(point, armA);
		long armTwo = CheapDistance(point, armB);
		if (armTwo > 10 || armOne > 10)
			return 0;			// both arms have to be short
		width = -1;
		typed = false;
	}
	else
		return 0;

	if (vertical)
		return InsertVerticalSpace(point, height);

	// the line the caret was drawn over, from the box its point and its arm
	// make, and then the insertion at that line's top
	Rect written;
	SetRect(&written, armA.h, armA.v, point.h, armA.v);
	long line = FindLineForWord(written, 6);
	if (line < 0)
		return 0;
	Point where;
	where.v = (short) (fLines[line].fBounds.top + fLines[line].fAscent);
	where.h = point.h;
	return InsertHorizontalSpace(where, width, height, typed);
}


// ROM 0x001740c4 ScrubWords__14TParagraphViewFRC5TRectP11TUnitPublicUc
// The word or words the scrub crossed taken out.
//
// The line is the first whose top falls inside the scrub's vertical span
// (or, failing that, one whose top pixel row holds the whole scrub); the
// walk gives up once the scrub is above the line it has reached.  The
// scrub's left and right edges, taken at that line's middle, give two
// word boundaries - the left one biased towards the word's start, the
// right one towards its end - and between them is what would go.
//
// Whether it actually goes depends on how much of it the scrub covers.
// Two or more characters on one line have to be more than half spanned
// by the scrub; a range that came out empty asks ScrubCharacter for the
// one character under the scrub instead (which a scrub wider than five
// pixels may only have if it was drawn with seven corners or more - a
// real to-and-fro, not a flick), and failing that the word's own box
// must be sixty per cent under the scrub.  White space is held to ninety
// per cent, and a single space to the same five-pixels-or-seven-corners
// rule.
//
// ==> 2 (words) when it took something out, 5 when what was left is
// white space and the whole text went with it, 0 when nothing did.
long
TParagraphView::ScrubWords(const Rect& bounds, TUnitPublic* unit, Boolean reallyDoIt)
{
	// the scrub's vertical span, a pixel wide, to measure the lines against
	Rect scrubRows;
	SetRect(&scrubRows, 0, bounds.top, 1, bounds.bottom);
	long line = -1;
	for (long i = 0; i < fLineCount; i++)
	{
		const Rect& box = fLines[i].fBounds;
		// the line from its top down to its baseline, which is where the
		// glyphs are (the ROM writes it as the box's bottom less the field at
		// +0x18 of the LineInfo, its descent; this cache keeps the ascent, so
		// the baseline is the top plus that)
		Rect lineRows;
		SetRect(&lineRows, 0, box.top, 1, (short) (box.top + fLines[i].fAscent));
		if (Overlaps(&box, &bounds)
			&& (CoveredBy(&lineRows, &scrubRows) >= 50 || CoveredBy(&scrubRows, &lineRows) == 100))
		{
			line = i;
			break;
		}
		if (bounds.bottom < box.top)
			return 0;			// the scrub is above this line: there is no more to find
	}
	if (line < 0)
		return 0;

	const Rect& lineBox = fLines[line].fBounds;
	Point at;
	at.v = (short) (lineBox.top + (lineBox.bottom - lineBox.top) / 2);
	at.h = bounds.left;
	long lineA = -1;
	long start = PointToWordBoundary(at, -50, &lineA);
	if (start < 0)
		return 0;
	at.h = bounds.right;
	long lineB = -1;
	long end = PointToWordBoundary(at, 50, &lineB);
	if (end < 0)
		return 0;

	long width = bounds.right - bounds.left;
	Boolean scrubbed = false;
	Boolean tookCharacter = false;
	long character = 0;
	if (end - start > 1)
	{
		if (lineA != lineB)
			scrubbed = true;		// the two ends are not even on the same line
		else
		{
			Rect from, to;
			OffsetToBounds(start, &from);
			OffsetToBounds(end, &to);
			long span = to.left - from.left;
			if (span != 0 && (width * 100) / span > 50)
				scrubbed = true;
		}
	}
	if (!scrubbed)
	{
		if (ScrubCharacter(lineA, bounds, &character)
			|| (lineA != lineB && ScrubCharacter(lineB, bounds, &character)))
		{
			// a scrub wider than five pixels has to have been drawn as one:
			// seven corners or more, not a flick across a letter
			if (width > 5 && CountGesturePoints(unit) < 7)
				return 0;
			tookCharacter = true;
		}
		if (start == end)
		{
			at.h = bounds.left;
			PointToWord(at, &start, &end, nil);
		}
		else if (start > end)
		{
			long swap = start;
			start = end;
			end = swap;
		}
		if (tookCharacter)
		{
			scrubbed = true;
			start = character;
			end = character + 1;
		}
		else
		{
			Rect from, to;
			OffsetToBounds(start, &from);
			OffsetToBounds(end - 1, &to);
			Rect word, scrub;
			SetRect(&word, from.left, 0, to.right, 1);
			SetRect(&scrub, bounds.left, 0, bounds.right, 1);
			if (CoveredBy(&scrub, &word) >= 60)
				scrubbed = true;
		}
	}

	long count = end - start;
	if (!scrubbed)
		return 0;
	Boolean nothingLeft = false;
	{
		RefVar textRef(Text());
		TRichString rich(textRef);
		const UniChar* text = rich.GrabPtr();
		if (ContainsOnlyWhiteSpace(text + start, (ULong) count))
		{
			if (count == 1 && IsSpace(text[start]))
			{
				if (width > 5 && CountGesturePoints(unit) < 7)
					scrubbed = false;
			}
			else
			{
				Rect from, to;
				OffsetToBounds(start, &from);
				OffsetToBounds(end - 1, &to);
				long span = to.right - from.left;
				if (span <= 0)
					span = lineBox.right - from.left;	// it runs to the end of the line
				if (span != 0 && (width * 100) / span < 90)
					scrubbed = false;
			}
		}
		if (scrubbed && reallyDoIt)
			nothingLeft = ContainsOnlyWhiteSpace(text, (ULong) start)
						   && ContainsOnlyWhiteSpace(text + end, (ULong) -1);
		rich.ReleasePtr();
	}
	if (!scrubbed)
		return 0;
	if (GetLastAddedWordView() == this && end == gLastAddedWordEndOffset
		&& unit->StartTime() < gLastAddedWordAddTime)
		return 0;
	if (!reallyDoIt)
		return 2;
	if (nothingLeft)
	{
		if ((fFlags & vCalculateBounds) == 0)
			RemoveText(0, TextLength());
		return 5;
	}
	if (count > 0)
		RemoveText(start, count);
	return 2;
}


// ROM 0x001748b8 ScrubLines__14TParagraphViewFRC5TRectP11TUnitPublicUc
// The lines the scrub covers taken out.  A line counts when more than
// sixty per cent of its box lies under the scrub; an empty line has a box
// one pixel wide, so it is widened to the view before it is measured.
// The run of covered lines stops at the first line that is not covered
// once one has been.
//
// A single line that ends exactly where the recogniser last put a word,
// in this view, is left alone when the scrub was begun before that word
// was added: the pen was writing, not scrubbing.
//
// ==> 3 (lines) when it took something out, 5 when what is left is white
// space and the whole text went with it, 0 when no line was covered.
// `reallyDoIt` false asks the question without doing anything.
long
TParagraphView::ScrubLines(const Rect& bounds, TUnitPublic* unit, Boolean reallyDoIt)
{
	long count = 0;
	long first = -1;
	long last = 0;
	for (long i = 0; i < fLineCount; i++)
	{
		const LineInfo& line = fLines[i];
		Rect box = line.fBounds;
		if (box.right - box.left == 1)
			box.right = viewBounds.right;		// an empty line: the whole width of the view
		if (CoveredBy(&box, &bounds) < 61)
		{
			if (count > 0)
				break;
		}
		else
		{
			count++;
			if (first == -1)
				first = line.fStart;
			last = line.fEnd;
		}
	}
	if (count <= 0)
		return 0;
	if (count == 1 && GetLastAddedWordView() == this && last == gLastAddedWordEndOffset
		&& unit->StartTime() < gLastAddedWordAddTime)
		return 0;
	if (reallyDoIt)
	{
		RefVar textRef(Text());
		TRichString rich(textRef);
		const UniChar* text = rich.GrabPtr();
		Boolean nothingLeft = ContainsOnlyWhiteSpace(text, (ULong) first)
							  && ContainsOnlyWhiteSpace(text + last, (ULong) -1);
		rich.ReleasePtr();
		if (nothingLeft)
		{
			if ((fFlags & vCalculateBounds) == 0)
				RemoveText(0, TextLength());
			return 5;
		}
		if (last - first > 0)
			RemoveText(first, last - first);
	}
	return 3;
}


// ROM 0x00173fac HandleScrub__14TParagraphViewFRC5TRectlP11TUnitPublicUc
// What a scrub over the paragraph takes out.  A scrub covering more than
// seventy per cent of the whole paragraph (or any of a write-protected
// one) empties it; otherwise the lines it covers are tried, and then the
// words.  `kind` asks for one of those in particular - 5 the whole
// paragraph, 3 lines, 2 words - and -1 for whichever answers first, which
// is what a gesture asks.  ==> the kind that answered, 0 for none.
long
TParagraphView::HandleScrub(const Rect& bounds, long kind, TUnitPublic* unit, Boolean reallyDoIt)
{
	if (fLines == nil)
		CreateAllCaches();
	if (!Overlaps(&viewBounds, &bounds))
		return 0;
	if (kind == 5 || kind == -1)
	{
		long covered = CoveredBy(&fCachedBounds, &bounds);
		if (covered > 70 || ((fFlags & vWriteProtected) != 0 && covered != 0))
		{
			if (reallyDoIt)
				RemoveText(0, TextLength());
			return 5;
		}
	}
	if (kind == 3 || kind == -1)
	{
		long done = ScrubLines(bounds, unit, reallyDoIt);
		if (done != 0)
			return done;
	}
	if (kind == 2 || kind == -1)
	{
		long done = ScrubWords(bounds, unit, reallyDoIt);
		if (done != 0)
			return done;
	}
	return 0;
}


// ROM 0x0016ef00 HandleReplaceText__14TParagraphViewFRC6RefVar
// The aeReplaceText command carried out: nothing for a read-only or
// write-protected paragraph.  The index parameters are the offset, the
// characters removed, the characters inserted (the command's text), the
// styles' offset into them, whether to post the inverse for undo,
// whether the caret goes after the insertion, and the typed flag; the
// frame parameter holds the styles for the insertion (a runs array, or
// {styles, tabs, correctInfo}) - none means the style for insertion
// there (an ink word before it gets the style over the whole range).
// The offset and count are kept within the text; the inverse command
// (the removed text put back with its styles and tabs) is posted to the
// application's undo; the text is munged (a read-only string cloned
// into the data frame), the styles adjusted, the tabs slot dropped when
// no tab is left (or taken from the frame parameter); the caret follows
// the change (to the insertion's end, or moved past it) when this is the
// key view; RangeChanged lays the lines out again and the view (its
// parent for an undo) is dirtied - the old bounds too when they shrank.
// NOT YET RECONSTRUCTED: the correction info and insert areas
// (the recogniser's), the hilites.
void
TParagraphView::HandleReplaceText(RefArg cmd)
{
	if (fFlags & (vReadOnly | vWriteProtected))
		return;
	Rect oldBounds = viewBounds;
	Dirty(nil);
	long caret = fCaretOffset;
	long offset = CommandIndexParameter(cmd, 0);
	long removed = CommandIndexParameter(cmd, 1);
	long inserted = CommandIndexParameter(cmd, 2);
	long styleOffset = CommandIndexParameter(cmd, 3);
	Boolean caretAfter = CommandIndexParameter(cmd, 5) != 0;
	RefVar frame(CommandFrameParameter(cmd));
	RefVar styles(ExtractStylesArray(frame));
	RefVar tabs(ExtractTabStopsArray(frame));
	RefVar correctInfo(ExtractCorrectInfo(frame));
	RefVar myTabs(Tabs());
	Boolean removedHasTab = false;
	if (inserted != 0)
	{
		if (ISNIL(styles))
		{
			styles = AllocateArray(RSSYMarray, 2);
			SetArraySlotRef(styles, 0, MAKEINT(inserted));
			SetArraySlot(styles, 1, RefVar(GetStyleForInsertion(offset, true, true)));
			styleOffset = 0;
		}
		else if (styleOffset != 0 && offset != 0)
		{
			RefVar before(GetStyleAtOffset(offset - 1, nil, nil));
			if (IsInkWord(before))
			{
				RefVar lead(AllocateArray(RSSYMarray, 2));
				SetArraySlotRef(lead, 0, MAKEINT(styleOffset));
				SetArraySlot(lead, 1, RefVar(GetStyleForInsertion(offset, true, true)));
				ArrayMunger(styles, 0, 0, lead, 0, 2);
				styleOffset = 0;
			}
		}
	}
	long textLength = TextLength();
	if (offset > textLength)
		offset = textLength;
	if (offset + removed > textLength)
		removed = textLength - offset;
	RefVar undo;
	Boolean postUndo = CommandIndexParameter(cmd, 4) != 0;
	if (postUndo)
	{
		TResponder* receiver = this;
		Long parameter = kNoParameter;
		if (fFlags & vCalculateBounds)
		{
			receiver = fParent;
			parameter = fId;
		}
		undo = MakeCommand(aeReplaceText, receiver, parameter);
		RefVar text(Text());
		RefVar removedText(MakeString((const UniChar*) BinaryData(text) + offset, removed));
		CommandSetIndexParameter(undo, 0, offset);
		CommandSetIndexParameter(undo, 1, inserted);
		CommandSetIndexParameter(undo, 2, removed);
		CommandSetIndexParameter(undo, 3, 0);
		CommandSetIndexParameter(undo, 4, 1);
		CommandSetIndexParameter(undo, 5, caretAfter);
		CommandSetIndexParameter(undo, 6, CommandIndexParameter(cmd, 6));
		CommandSetText(undo, removedText);
		if (removed != 0)
		{
			RefVar myStyles(Styles());
			removedHasTab = Ustrchr((const UniChar*) BinaryData(removedText), 9) != nil;
			RefVar tabsToKeep;
			if (removedHasTab)
				tabsToKeep = myTabs;
			RefVar oldStyles;
			if (ISNIL(myStyles) || Length(myStyles) > 0)
				oldStyles = GetStylesOfRange(offset, removed, false);
			RefVar saved(SaveStylesAndTabStopsArrays(oldStyles, tabsToKeep));
			// the words about to go, kept with their alternatives and
			// rebased to the start of the range, so that undoing the
			// deletion puts them back where they were
			RefVar taken(ExtractRange(RefVar(CorrectInfo()), this,
									  offset, offset + removed));
			if (NOTNIL(taken))
			{
				OffsetCorrectionInfo(taken, this, 0, offset, 0);
				SetFrameSlot(saved, RSSYMcorrectinfo, taken);
			}
			CommandSetFrameParameter(undo, saved);
		}
	}
	RefVar newText;
	{
		RefVar insertedText(CommandText(cmd));
		RefVar text(Text());
		LockRefArg(insertedText);
		newText = Munger(text, offset * 2, removed * 2, BinaryData(insertedText), inserted * 2);
		UnlockRefArg(insertedText);
	}
	RefVar data(DataFrame());
	SetFrameSlot(data, RSSYMtext, newText);
	// NOT YET RECONSTRUCTED: the insert areas after a deletion/insertion (not for an undo)
	if (postUndo)
		gApplication->PostUndoCommand(undo);
	AdjustStyles(offset, removed, inserted, styles, styleOffset);
	AdjustHilites(offset, inserted - removed);
	// the machine's own list brought up to date with the change, and
	// then whatever the command brought with it - an undo's saved
	// words - moved to where they are going and put back on
	OffsetCorrectionInfo(this, offset, removed, inserted);
	if (NOTNIL(correctInfo))
	{
		OffsetCorrectionInfo(correctInfo, nil, 0, 0, offset);
		InsertRange(RefVar(CorrectInfo()), correctInfo, this);
	}
	if (!removedHasTab)
	{
		if (inserted != 0 && NOTNIL(tabs) && ISNIL(myTabs))
			SetFrameSlot(data, RSSYMtabs, tabs);
	}
	else if (Ustrchr((const UniChar*) BinaryData(newText), 9) == nil)
		RemoveSlot(data, RSSYMtabs);
	if (!caretAfter)
	{
		if (gRootView->fCaretView == this && gRootView->fCaretLength == 0)
		{
			long newCaret = -1;
			if (caret >= offset && caret <= offset + removed)
				newCaret = offset + inserted;
			else if (caret > offset + removed)
				newCaret = caret + inserted - removed;
			if (newCaret >= 0)
				gRootView->SetKeyView(this, newCaret, 0, false);
		}
	}
	else
		gRootView->SetKeyView(this, offset + inserted, 0, false);
	RangeChanged(offset, removed, inserted, RSSYMtext);
	TView* dirtied = IsUndoCommand(cmd) ? fParent : this;
	dirtied->Dirty(nil);
	if (viewBounds.right - viewBounds.left < oldBounds.right - oldBounds.left
	 || viewBounds.bottom - viewBounds.top < oldBounds.bottom - oldBounds.top)
		fParent->Dirty(&oldBounds);
}


// ROM 0x00178d5c MakeAndDoReplaceCommand__14TParagraphViewFUlPCUsT1RC6RefVarT4N21Uc
// An aeReplaceText command for the view (its id as the parameter) with
// the index parameters [offset, removeLength, length, styleOffset, 1, 1,
// typed], the styles (with the correct info, in a canonical correctInfo
// frame) as the frame parameter and the text as a string, dispatched.
void
TParagraphView::MakeAndDoReplaceCommand(ULong offset, const UniChar* text, ULong length, RefArg styles, RefArg correctInfo, ULong styleOffset, ULong removeLength, Boolean typed)
{
	RefVar cmd(MakeCommand(aeReplaceText, this, fId));
	CommandSetIndexParameter(cmd, 0, offset);
	CommandSetIndexParameter(cmd, 1, removeLength);
	CommandSetIndexParameter(cmd, 2, length);
	CommandSetIndexParameter(cmd, 3, styleOffset);
	CommandSetIndexParameter(cmd, 4, 1);
	CommandSetIndexParameter(cmd, 5, 1);
	CommandSetIndexParameter(cmd, 6, typed);
	RefVar frame(styles);
	if (NOTNIL(correctInfo))
	{
		frame = Clone(RefVar(Rcanonicalcorrectinfo));
		SetFrameSlot(frame, RSSYMstyles, styles);
		SetFrameSlot(frame, RSSYMcorrectinfo, correctInfo);
	}
	CommandSetFrameParameter(cmd, frame);
	CommandSetText(cmd, RefVar(MakeString(text, length)));
	gApplication->DispatchCommand(cmd);
}


// ROM 0x00178a3c InsertStyledText__14TParagraphViewFUlPCUsT1RC6RefVarT4N21Uc
// Text put in place of removeLength characters at the offset
// (MakeAndDoReplaceCommand); a deletion that inserts nothing (and is not
// a backspace) then drops the white space left at the end of the text.
void
TParagraphView::InsertStyledText(ULong offset, const UniChar* text, ULong length, RefArg styles, RefArg correctInfo, ULong styleOffset, ULong removeLength, Boolean typed)
{
	MakeAndDoReplaceCommand(offset, text, length, styles, correctInfo, styleOffset, removeLength, typed);
	if (removeLength == 0 || length != 0)
		return;
	if (text != nil && *text == 8)
		return;
	RefVar textRef(Text());
	const UniChar* chars = (const UniChar*) BinaryData(textRef);
	long textLength = Ustrlen(chars);
	long kept = textLength;
	while (kept > 0 && IsWhiteSpace(chars[kept - 1]))
		kept--;
	if (kept < textLength)
		MakeAndDoReplaceCommand(kept, nil, 0, RefVar(NILREF), RefVar(NILREF), 0, textLength - kept, false);
}


// ROM 0x001814c0 InsertInk__14TParagraphViewFUlRC6RefVarT1
// A bundle of strokes put into the text.  An ink word is one character -
// 0xf701 - whose style is the ink word binary itself, so the styles
// handed to the replace command are the pair (1 character, the ink).
void
TParagraphView::InsertInk(ULong offset, RefArg bundle, ULong removeLength)
{
	UniChar text[2];
	text[0] = kInkWordChar;
	text[1] = 0;
	RefVar styles(AllocateArray(RSSYMstyles, 2));
	RefVar ink(StrokeBundleToInkWord(bundle));
	SetArraySlot(styles, 0, RefVar(MAKEINT(1)));
	SetArraySlot(styles, 1, ink);
	MakeAndDoReplaceCommand(offset, text, 1, styles, RefVar(NILREF), 0, removeLength, false);
}


// ROM 0x00178b98 RemoveText__14TParagraphViewFUlT1
// A range removed, widened to take a space next to it: a range that is
// not itself bounded by spaces (and, for one character, a word character
// standing alone) takes the space (or two) after it, else the one (or
// two) before.
void
TParagraphView::RemoveText(ULong offset, ULong length)
{
	RefVar textRef(Text());
	const UniChar* chars = (const UniChar*) BinaryData(textRef);
	long end = offset + length;
	UniChar first = chars[offset];
	UniChar before = offset == 0 ? 0 : chars[offset - 1];
	UniChar after = chars[end];
	long start = offset;
	long stop = end;
	if (first != ' ' && chars[end - 1] != ' ')
	{
		Boolean widen = true;
		if (length == 1)
		{
			Boolean firstIsWord = IsAlphaNumeric(first) || first == 0xf701;
			Boolean beforeIsWord = IsAlphaNumeric(before) || before == 0xf701;
			Boolean afterIsWord = IsAlphaNumeric(after) || after == 0xf701;
			widen = firstIsWord && !beforeIsWord && !afterIsWord;		// a lone word character
		}
		if (widen)
		{
			if (after == ' ')
			{
				stop = end + 1;
				if (chars[stop] == ' ')
					stop = end + 2;
			}
			else if (before == ' ')
			{
				start = offset - 1;
				if (start != 0 && chars[start - 1] == ' ')
					start = offset - 2;
			}
		}
	}
	InsertStyledText(start, nil, 0, RefVar(NILREF), RefVar(NILREF), 0, stop - start, false);
}


// ROM 0x00177218 AddKeyToCurrUndo__14TParagraphViewFUsl
// A typed key joined to the last undo entry when that undoes typing
// here (an aeReplaceText for this view, typed, inserting nothing) that
// ends at the offset and covers fewer than ten characters: the entry
// grows by the character (shrinks for a backspace; goes when empty) and
// the key is carried out by a replace command of its own that posts no
// undo; ==> whether it was.
Boolean
TParagraphView::AddKeyToCurrUndo(UniChar ch, long offset)
{
	RefVar stack(gApplication->GetUndoStack(0));
	if (ISNIL(stack))
		return false;
	long last = Length(stack) - 1;
	if (last < 0)
		return false;
	RefVar entry(GetArraySlotRef(stack, last));
	if (CommandID(entry) != aeReplaceText || CommandParameter(entry) != fId
	 || CommandIndexParameter(entry, 6) == 0 || CommandIndexParameter(entry, 2) != 0)
		return false;
	long entryOffset = CommandIndexParameter(entry, 0);
	long entryLength = CommandIndexParameter(entry, 1);
	if (entryOffset + entryLength != offset || entryLength >= 10)
		return false;
	Boolean backspace = ch == 8;
	long newLength = backspace ? entryLength - 1 : entryLength + 1;
	if (newLength == 0)
		ArrayRemoveCount(stack, last, 1);
	else
		CommandSetIndexParameter(entry, 1, newLength);
	if (backspace)
		offset--;
	RefVar cmd(MakeCommand(aeReplaceText, this, fId));
	CommandSetIndexParameter(cmd, 0, offset);
	CommandSetIndexParameter(cmd, 1, backspace ? 1 : 0);
	CommandSetIndexParameter(cmd, 2, backspace ? 0 : 1);
	CommandSetIndexParameter(cmd, 3, 0);
	CommandSetIndexParameter(cmd, 4, 0);
	CommandSetIndexParameter(cmd, 5, 1);
	CommandSetIndexParameter(cmd, 6, 1);
	UniChar key = ch;
	CommandSetText(cmd, RefVar(MakeString(&key, 1)));
	gApplication->DispatchCommand(cmd);
	return true;
}


// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0x134c (aeInkWord)
// A word of writing nobody read, sent to the paragraph itself.  The
// view's own script gets it first; the command's unit becomes its word
// info in the command's correctInfo slot (and its strokes the frame
// parameter, when there is none); then the word goes in at the caret when
// the paragraph has a selection and the writing may be remote, and
// failing that where it was written (HandleInkWord).
Boolean
TParagraphView::InkWordCommand(RefArg cmd)
{
	if (TView::RealDoCommand(cmd))
		return true;
	if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
		return TView::RealDoCommand(cmd);
	ULong remote = SetRemoteForCorrector();
	RefVar param(GetFrameSlotRef(cmd, RSSYMparameter));
	if (NOTNIL(param))
	{
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		if (unit != nil)
		{
			SetFrameSlot(cmd, RSSYMcorrectinfo, RefVar(unit->WordInfo()));
			if (ISNIL(RefVar(CommandFrameParameter(cmd))))
				CommandSetFrameParameter(cmd, RefVar(unit->Strokes()));
		}
	}
	RefVar hilite(FirstHilite());
	if (NOTNIL(hilite) && (remote & 1) == 0)
	{
		RefVar spec(Clone(RefVar(Rstarterinsertspec)));
		RefVar ink(StrokeBundleToInkWord(RefVar(CommandFrameParameter(cmd))));
		AdjustInkWordXHeight(ink, ViewExpectsNumbers(this));
		SetFrameSlot(spec, RSSYMinsertitems, ink);
		if (InsertItemsAtCaret(spec))
		{
			CommandSetResult(cmd, 1);
			RestoreRemoteForCorrector(remote);
			return true;
		}
	}
	Boolean handled = false;
	if (HandleInkWord(cmd, true))
	{
		handled = true;
		CommandSetResult(cmd, 1);
	}
	RestoreRemoteForCorrector(remote);
	if (!handled)
		return TView::RealDoCommand(cmd);
	return true;
}


// ROM 0x001722a4 HandleInkWord__14TParagraphViewFRC6RefVarUc
// How well the paragraph would take a word of writing (the command's
// stroke bundle), or with `reallyDoIt` the word taken: offered to
// HandleWord as the one ink-word character, at the middle of where it was
// written across and its ascent down from the top, with the bundle's
// times.  Taken for real it is brought to the x-height the paragraph
// writes in and goes in with a style of its own (the ink word itself as
// the run's font); but when the caret is in this paragraph and the
// writing may not be remote it goes in at the caret instead (answering 5).
long
TParagraphView::HandleInkWord(RefArg cmd, Boolean reallyDoIt)
{
	UniChar ch = kInkWordChar;
	RefVar bundle(CommandFrameParameter(cmd));
	RefVar ink(StrokeBundleToInkWord(bundle));
	Rect box;
	FromObject(RefVar(GetFrameSlotRef(bundle, RSSYMbounds)), box);
	InkWordInfo info;
	GetInkWordInfo(ink, &info);
	Point pt;
	pt.h = (short) ((box.left + box.right) / 2);
	pt.v = (short) (box.top + info.fAscent);
	long start = 0, stop = 0;
	RefVar startTime(GetFrameSlotRef(bundle, RSSYMstarttime));
	if (NOTNIL(startTime))
	{
		start = RINT(startTime);
		stop = RINT(RefVar(GetFrameSlotRef(bundle, RSSYMendtime)));
	}
	RefVar props;
	if (reallyDoIt)
	{
		AdjustInkWordXHeight(ink, ViewExpectsNumbers(this));
		if (NOTNIL(GetPreference(RSSYMremotewriting)) && gRootView->fCaretView == this)
		{
			RefVar spec(Clone(RefVar(Rstarterinsertspec)));
			SetFrameSlot(spec, RSSYMinsertitems, ink);
			InsertItemsAtCaret(spec);
			return 5;
		}
		props = DeepClone(RefVar(Rstarterproperties));
		RefVar styles(GetFrameSlotRef(props, RSSYMstyles));
		SetArraySlotRef(styles, 0, MAKEINT(1));
		SetArraySlotRef(styles, 1, ink);
	}
	long offset = 0;
	return HandleWord(&ch, 1, box, pt, start, stop, props, reallyDoIt, &offset, nil);
}


// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0xbc (aeWord)
// A word the recogniser read, sent to the paragraph itself (a field on
// its own, with no page around it).  A field that holds one word only
// (viewJustify oneWordOnly) has its text replaced by the word and keeps
// the other readings in its alternateWords slot.  Otherwise the word
// goes in at the caret when the paragraph has a selection and the
// writing may be remote, and failing that where it was written
// (HandleWord), at the middle of the base line the recogniser found for
// it.  A word that went in is registered for the corrector.
Boolean
TParagraphView::WordCommand(RefArg cmd)
{
	if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
		return true;
	if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
		return TView::RealDoCommand(cmd);
	ULong remote = SetRemoteForCorrector();
	TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
	Rect box;
	unit->Bounds(&box);
	// the middle of the word's base line box, as it stands (its top is
	// -32768 when the base line is not known)
	Point pt;
	pt.h = (short) (unit->fWordBase.left + unit->fWordBase.right) >> 1;
	pt.v = (short) (unit->fWordBase.top + unit->fWordBase.bottom) >> 1;
	// ROM QUIRK: a point outside the word's box is taken to its left edge
	// or its bottom, whichever side it strayed from
	if (pt.h < box.left || pt.h > box.right)
		pt.h = box.left;
	if (pt.v < box.top || box.bottom < pt.v)
		pt.v = box.bottom;
	Handle word = unit->Word();
	HLock(word);
	UniChar* text = (UniChar*) *word;
	Boolean handled = false;
	if (text[0] != 0)
	{
		if ((fViewJustify & vjOneWordOnly) != 0)
		{
			SetFrameSlot(fContext, RSSYMalternatewords, RefVar(GetWordArray(unit)));
			ULong length = Ustrlen(text);
			RefVar current(Text());
			ULong had = (Length(current) - 2) >> 1;
			InsertStyledText(0, text, length, RefVar(NILREF), RefVar(NILREF), 0, had, false);
			RemoveCorrectionInfo(this);
			AddWordInfo(this, 0, length, unit);
			CommandSetResult(cmd, 1);
			RestoreRemoteForCorrector(remote);
			// ROM BUG: the word's handle is neither unlocked nor disposed
			// of on this path
			return true;
		}
		RefVar hilite(FirstHilite());
		if (NOTNIL(hilite) && (remote & 1) == 0)
		{
			RefVar spec(Clone(RefVar(Rstarterinsertspec)));
			SetFrameSlot(spec, RSSYMinsertitems, RefVar(unit->WordInfo()));
			if (InsertItemsAtCaret(spec))
			{
				CommandSetResult(cmd, 1);
				RestoreRemoteForCorrector(remote);
				// ROM BUG: the word's handle is kept here too
				return true;
			}
		}
		gAddWordInfo = true;
		ULong length = Ustrlen(text);
		long offset = 0;
		if (HandleWord(text, length, box, pt, unit->StartTime(), unit->EndTime(), RefVar(NILREF),
					   true, &offset, unit))
		{
			if (gAddWordInfo)
				AddWordInfo(this, offset, offset + length, unit);
			CommandSetResult(cmd, 1);
			handled = true;
		}
	}
	HUnlock(word);
	DisposHandle(word);
	RestoreRemoteForCorrector(remote);
	if (!handled)
		return TView::RealDoCommand(cmd);
	return true;
}


// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar
// The paragraph's commands.  A key down or repeat runs the key scripts
// and key commands (HandleKeyEvent); a key nobody took, for a paragraph
// that can be written, goes into the text: return (or the enter key, 3)
// with a default button (viewJustify's 0x1800000) sends _doDefaultButton;
// tab, unless the view calculates its bounds, moves to the next key
// view (NOT YET: NextKeyView); the arrows move the caret (NOT YET) ...
// a hilite is replaced (NOT YET: the hilites); white space flushes the
// word at the caret; backspace removes the character before the caret
// (a vCalculateBounds paragraph down to its last character asks its
// parent to remove it - NOT YET); a character KeyCanBeHandled goes in at
// the caret - both merged into the last undo when they can
// (AddKeyToCurrUndo).  A key up runs the key scripts (NOT YET: the empty
// paragraph's removal).  A key string is inserted at the caret (or over
// the hilite) and the hilites removed.  aeReplaceText is
// HandleReplaceText.  NOT YET RECONSTRUCTED: the pen commands (clicks,
// strokes, words, gestures, ink), the hilite and style commands, the
// clipboard; the rest is TView's.
Boolean
TParagraphView::RealDoCommand(RefArg cmd)
{
	ULong id = (ULong) CommandID(cmd);
	if (id == aeKeyDown || id == aeKeyRepeat)
	{
		Boolean isCommandKey = false;
		Boolean handled = HandleKeyEvent(cmd, id, &isCommandKey);
		if (handled || isCommandKey || (fFlags & (vReadOnly | vWriteProtected)))
			return true;
		ULong parameter = (ULong) CommandParameter(cmd);
		if (parameter & 0x20000000)
			return true;
		UniChar ch = (UniChar) KeyEventChar(parameter);
		if (ch == 3)
			ch = 0x0d;
		if (ch == 0x0d && (fViewJustify & 0x1800000))
		{
			SendKeyMessage(this, RSSYM_dodefaultbutton);
			return true;
		}
		if ((fFlags & vCalculateBounds) == 0 && ch == 9)
		{
			// tab moves along the key view chain: forward, or back when
			// the shift modifier is set (0x4000000) without the cancelling
			// one (0x1000000)
			long direction = ((parameter & 0x4000000) == 0 || (parameter & 0x1000000) != 0) ? 1 : -1;
			TView* next = NextKeyView(this, direction, 0);
			if (next != nil)
			{
				if (next->DerivedFrom(clParagraphView))
					((TParagraphView*) next)->MakeHilite(0, 999999, true);		// select the whole target
				else
					gRootView->SetKeyView(next, 999999, 0, false);
			}
			return true;
		}
		// a selection is collapsed by an arrow and replaced by a content key
		TParagraphHilite* selection = HiliteOf(RefVar(FirstHilite()));
		Boolean hasSelection = selection != nil;
		long hiliteStart = 0, hiliteEnd = 0;
		if (hasSelection)
		{
			hiliteStart = selection->fStart;
			hiliteEnd = selection->fEnd;
		}
		if (ch == 0x1c || ch == 0x1d)
		{
			long offset;
			if (hasSelection)
			{
				offset = (ch == 0x1c) ? hiliteStart : hiliteEnd;
				RemoveAllHilites();
			}
			else
				offset = fCaretOffset + (ch == 0x1c ? -1 : 1);
			long textLength = TextLength();
			if (offset < 0)
				offset = 0;
			if (offset > textLength)
				offset = textLength;
			gRootView->SetKeyView(this, offset, 0, false);
			return true;
		}
		// a content key over a selection replaces it in one edit
		if (hasSelection && ch != 0 && (ch == 8 || KeyCanBeHandled(ch)))
		{
			RemoveAllHilites();
			long removeLength = hiliteEnd - hiliteStart;
			if (ch == 8)
				InsertStyledText(hiliteStart, nil, 0, RefVar(NILREF), RefVar(NILREF), 0, removeLength, true);
			else
			{
				UniChar key = ch;
				InsertStyledText(hiliteStart, &key, 1, RefVar(NILREF), RefVar(NILREF), 0, removeLength, true);
			}
			return true;
		}
		if (ch == 0x1e || ch == 0x1f)
		{
			// NOT YET RECONSTRUCTED: the caret moved a line up or down (the ROM's OffsetInRunToBounds/PointToOffset)
			gRootView->SetKeyView(this, ch == 0x1f ? 999999 : 0, 0, false);
			return true;
		}
		if (IsWhiteSpace(ch))
			FlushWordAtCaret();
		if (ch == 0)
			return true;
		if (ch == 8)
		{
			if (fCaretOffset < 1)
				return true;
			long offset = --fCaretOffset;
			if (AddKeyToCurrUndo(ch, offset))
				return true;
			UniChar key = ch;
			InsertStyledText(fCaretOffset, &key, 0, RefVar(NILREF), RefVar(NILREF), 0, 1, true);
			return true;
		}
		if (!KeyCanBeHandled(ch))
			return true;
		long offset = fCaretOffset++;
		if (AddKeyToCurrUndo(ch, offset))
			return true;
		UniChar key = ch;
		InsertStyledText(offset, &key, 1, RefVar(NILREF), RefVar(NILREF), 0, 0, true);
		return true;
	}
	if (id == aeKeyUp)
	{
		if (HandleKeyEvent(cmd, id, nil))
			return true;
		return true;
	}
	if (id == aeKeyString)
	{
		if (HandleKeyEvent(cmd, id, nil))
			return true;
		if (fFlags & (vReadOnly | vWriteProtected))
			return true;
		RefVar str(CommandFrameParameter(cmd));
		LockRefArg(str);
		const UniChar* chars = (const UniChar*) BinaryData(str);
		long length = Ustrlen(chars);
		long offset = fCaretOffset;
		fCaretOffset += length;
		InsertStyledText(offset, chars, length, RefVar(NILREF), RefVar(NILREF), 0, 0, false);
		UnlockRefArg(str);
		RemoveAllHilites();
		return true;
	}
	if (id == aeReplaceText)
	{
		HandleReplaceText(cmd);
		return true;
	}
	if (id == aeScrub)
	{
		// (textFlags bit 0x2000: the view answers the pen itself first)
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		if ((fFlags & (vReadOnly | vCalculateBounds)) != 0)
			return TView::RealDoCommand(cmd);
		if ((fFlags & vWriteProtected) != 0)
		{
			CommandSetResult(cmd, 1);		// taken, and nothing done
			return true;
		}
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		Rect bounds;
		unit->Bounds(&bounds);
		Boolean done = ScrubHilite(bounds);
		if (!done)
		{
			// what the scrub would take out asked for first, and then
			// done for real
			long kind = HandleScrub(bounds, -1, unit, false);
			if (kind != 0)
			{
				HandleScrub(bounds, kind, unit, true);
				done = true;
			}
		}
		// nothing taken: the gesture script has it
		if (!done)
			return TView::RealDoCommand(cmd);
		// the selection or the text went: the scrub's own ink comes off,
		// the hole it left puffs away, and the command's result says the
		// scrub was taken (so its stroke is claimed, not read as writing)
		unit->Stroke()->InkOff(false);
		TAnimate effect;
		effect.SetupPoofEffect(this, bounds);
		effect.DoEffect(RefVar(Rpoof));
		CommandSetResult(cmd, 1);
		return true;
	}

	if (id == aeTap)
	{
		// defer placing the caret until the double-tap interval passes, so
		// a second tap can be a double tap (word select) instead; the tap
		// then goes on to the view's gesture script
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		fTapped = true;
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		fTapPoint = unit->Stroke()->FirstPoint();
		gRootView->AddIdler(this, gDoubleTapInterval * 16 + 80, 2);
		return TView::RealDoCommand(cmd);
	}
	if (id == aeCaret)
	{
		// the caret gesture: the selection goes, and a caret the text
		// can take (ValidTextEditCaret) is handed to HandleCaret with its
		// kind, angle and points
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
			return TView::RealDoCommand(cmd);
		RemoveAllHilites();
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		if (ValidTextEditCaret(unit))
		{
			ULong kind = unit->CaretType();
			long angle = unit->GestureAngle();
			Point point = unit->GesturePoint(0);
			Point armA = unit->GesturePoint(1);
			Point armB = unit->GesturePoint(2);
			Point tail;
			if (kind == 3 || kind == 5)
				tail = unit->GesturePoint(3);
			else
				tail.v = -32768;
			if (HandleCaret(kind, angle, point, armA, armB, tail))
			{
				CommandSetResult(cmd, 1);
				return true;
			}
		}
		return TView::RealDoCommand(cmd);
	}
	if (id == aeLine)
	{
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
			return TView::RealDoCommand(cmd);
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		if (ValidLineGesture(unit))
		{
			long angle = unit->GestureAngle();
			Point from = unit->GesturePoint(0);
			Point to = unit->GesturePoint(1);
			if (HandleLineGesture(angle, from, to))
			{
				CommandSetResult(cmd, 1);
				return true;
			}
		}
		return TView::RealDoCommand(cmd);
	}
	if (id == aeWord)
		return WordCommand(cmd);
	if (id == aeInkWord)
		return InkWordCommand(cmd);
	if (id == aeRecognizeInk)
		return RecognizeInkCommand(cmd);
	if (id == aeRecognizeRange && (fFlags & (vReadOnly | vWriteProtected)) == 0)
		return RecognizeRangeCommand(cmd);
	if (id == aeGesture2f)
	{
		// the hilite stroke over the paragraph: the kind of selection it
		// makes asked for, and made
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		long kind = HandleHilite(unit, -1, false);
		if (kind != 0)
		{
			HandleHilite(unit, kind, true);
			gRootView->SetHilitedView(this);
		}
		Dirty(nil);
		CommandSetResult(cmd, 1);
		return true;
	}
	if (id == aeAddHilite)
	{
		// the base adds the hilite; a hilite added bare (made by the pen,
		// not put back from a saved selection) makes its range the key
		// view - or the page, when this is one of several children the
		// page has selected - unless the page is carrying a hilite out
		// over its children
		TView::RealDoCommand(cmd);
		RefVar param(CommandFrameParameter(cmd));
		Boolean framed = IsFrame(param);
		if (framed)
			param = GetFrameSlotRef(param, RSSYMhilite);
		TParagraphHilite* hilite = (TParagraphHilite*) RefToAddress(param);
		Boolean hiliting = false;
		TView* editor = GetEnclosingEditView();
		if (editor != nil)
			hiliting = ((TEditView*) editor)->fHilitingChildren;
		if (!framed && !hiliting)
		{
			TView* keyView = this;
			long offset, length;
			TView* parent = fParent;
			if (parent != nil && parent->DerivedFrom(clEditView)
				&& ((TEditView*) parent)->HasHilitedChildren(2, nil))
			{
				length = 999;
				offset = 0;
				keyView = parent;
			}
			else
			{
				offset = hilite->fStart;
				length = hilite->fEnd - offset;
			}
			gRootView->SetKeyView(keyView, offset, length, false);
		}
		return true;
	}
	if (id == aeClick)
		return ClickCommand(cmd);
	if (id == aeScaleData)
		return ScaleCommand(cmd);
	if (id == aeToChildren)
	{
		// a page's children told to restyle: the selected text takes
		// the style (the frame parameter, or else the parameter as an
		// integer)
		if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
			return true;
		RefVar style(CommandFrameParameter(cmd));
		if (ISNIL(style))
			style = MAKEINT(CommandParameter(cmd));
		ChangeStyleOfSelection(style);
		return true;
	}
	if (id == aeDoubleTap)
	{
		// The second tap on a word: the corrector goes up over it.  The
		// pending single tap is cancelled first, so the caret is not
		// placed as well.  A tap on an ink word inside the selection
		// reads the whole selection again instead (command 0x1a), and a
		// tap on an ink word the corrector knows no readings for reads
		// that word again (command 0x19) - Rerecognize.h.
		if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
			return true;
		if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
			return TView::RealDoCommand(cmd);
		fTapped = false;

		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		Point pt = unit->Stroke()->FirstPoint();
		// a double tap off the paragraph is the scripts' business
		if (!PtInRect(pt, &viewBounds))
			return TView::RealDoCommand(cmd);
		{
			long offset = 0;
			Point where;
			long length = FindWordOffset(pt, &offset, &where);
			if (length != 0)
			{
				RefVar textRef(Text());
				TRichString rich(textRef);
				const UniChar* text = rich.GrabPtr();
				offset = ScanWordStart(text, offset, 0);
				length = ScanWordEnd(text, offset, Ustrlen(text)) - offset;
				rich.ReleasePtr();
			}
			if (length == 0)
			{
				// nothing to correct: the caret goes where the tap was and
				// the keypad is offered - unless the view works out its own
				// bounds, which is a paragraph of a page rather than a field,
				// and then the double tap goes to the scripts
				if ((fFlags & vCalculateBounds) != 0)
					return TView::RealDoCommand(cmd);
				HandleTap(pt);
				OpenKeypadFor(this);
			}
			else
			{
				Boolean handled = false;
				if (HitsHilitedInkWord(this, pt))
				{
					TParagraphHilite* hilite = HiliteOf(RefVar(FirstHilite()));
					RefVar again(MakeCommand(aeRecognizeRange, this, fId));
					SetFrameSlot(again, RSSYMstart, RefVar(MAKEINT(hilite->fStart)));
					SetFrameSlot(again, RSSYMstop, RefVar(MAKEINT(hilite->fEnd)));
					SetFrameSlot(again, RSSYMdohilite, RefVar(TRUEREF));
					SetFrameSlot(again, RSSYMrecconfig, RefVar(NILREF));
					gApplication->DispatchCommand(again);
					RemoveAllHilites();
					handled = true;
				}
				RefVar info(FindWordInfo(this, offset));
				if (!handled)
				{
					RefVar style(GetStyleAtOffset(offset, nil, nil));
					Boolean hasWords = false;
					if (NOTNIL(info) && NOTNIL(GetFrameSlotRef(info, RSSYMwords)))
						hasWords = Length(GetFrameSlotRef(info, RSSYMwords)) > 0;
					if (IsInkWord(style) && !hasWords)
					{
						HiliteText(offset, length, true);
						gRootView->Update(nil);
						RefVar again(MakeCommand(aeRecognizeInk, this, fId));
						SetFrameSlot(again, RSSYMstart, RefVar(MAKEINT(offset)));
						SetFrameSlot(again, RSSYMstop, RefVar(MAKEINT(length)));
						SetFrameSlot(again, RSSYMdohilite, RefVar(TRUEREF));
						SetFrameSlot(again, RSSYMrecconfig, RefVar(NILREF));
						gApplication->DispatchCommand(again);
						RemoveAllHilites();
						handled = true;
					}
				}
				if (handled)
				{
					CommandSetResult(cmd, 1);
					return true;
				}
				// a word the corrector already knows about is corrected
				// as a whole, however much of it was tapped
				if (NOTNIL(info))
				{
					long start = RINT(RefVar(GetFrameSlotRef(info, RSSYMstart)));
					long stop = RINT(RefVar(GetFrameSlotRef(info, RSSYMstop)));
					if (start <= offset && offset + length <= stop)
					{
						length = stop - start;
						offset = start;
					}
				}
				Rect bounds, end;
				OffsetToBounds(offset, &bounds);
				OffsetToBounds(offset + length, &end);
				bounds.right = end.left;
				RefVar textRef(Text());
				TRichString rich(textRef);
				UniChar* text = (UniChar*) rich.GrabPtr();
				Correct(this, text + offset, length, offset, bounds);
				rich.ReleasePtr();
			}
		}
		CommandSetResult(cmd, 1);
		return true;
	}
	if (id == kInsertItemsCommand && (fFlags & (vReadOnly | vWriteProtected)) == 0)
	{
		// something put into the paragraph from outside.  A paragraph
		// that will not take it - or one that may not be written on -
		// lets the view's own scripts have the command instead.
		if (HandleInsertItems(RefVar(CommandFrameParameter(cmd))))
			return true;
	}
	return TView::RealDoCommand(cmd);
}


// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0xc04 (command 0x19)
// The ink word at the command's start read again (RerecognizeWord) in
// an area of its own, with recognition made modal and the controller's
// state put aside meanwhile; the arrow drawn over it when asked and the
// paragraph is visible.  With no configuration in the command, the one
// its recognition view's flags give for reading writing again
// (BuildRecConfigForDeferred).  The view's recognition flags are put
// back afterwards (flag 0x1000 is cleared while it reads).
Boolean
TParagraphView::RecognizeInkCommand(RefArg cmd)
{
	Rect none = { 0, 0, 0, 0 };
	UChar failed = false;
	gRecognition.EnableModalRecognition(none);
	ControllerState* state = SaveRecognitionState(gController, &failed);
	if (!failed)
	{
		ULong flags = fFlags & 0x01ffff00;
		ClearFlags(0x1000);
		if (NOTNIL(GetFrameSlotRef(cmd, RSSYMdohilite)) && (fFlags & vVisible) != 0)
		{
			long start = RINT(GetFrameSlotRef(cmd, RSSYMstart));
			Rect bounds, next;
			OffsetToBounds(start, &bounds);
			OffsetToBounds(start + 1, &next);
			bounds.right = next.left;
			DrawCheckmark(bounds);
		}
		RefVar config(GetFrameSlotRef(cmd, RSSYMrecconfig));
		if (ISNIL(config))
		{
			TView* view = GetRecognitionView(this);
			config = BuildRecConfigForDeferred(view, view == this ? flags : (view->fFlags & 0x01ffff00));
		}
		TRecArea* area = MakeRerecognizeArea(gController, config);
		RerecognizeWord(this, cmd, area);
		if (area != nil)
			area->Dispose();
		SetFlags(flags);
	}
	RestoreRecognitionState(gController, state);
	gRecognition.DisableModalRecognition();
	CommandSetResult(cmd, 1);
	return true;
}


// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0x358 (command 0x1a)
// Every ink word between the command's start and stop found first - with
// the box it is drawn in when the line cache has it (the arrow is drawn
// there when asked and the paragraph is visible) - and then each read
// again with a command 0x19 of its own, later offsets moved by how much
// longer or shorter each replacement was.  The command's stop is left
// saying where the range ends now, and the paragraph told the range
// changed once, at the end (its styles not processed meanwhile).
Boolean
TParagraphView::RecognizeRangeCommand(RefArg cmd)
{
	struct InkWordEntry
	{
		long	fStart;
		long	fEnd;
		Rect	fBounds;
	};
	RefVar style(NILREF);
	long start = RINT(GetFrameSlotRef(cmd, RSSYMstart));
	long stop = RINT(GetFrameSlotRef(cmd, RSSYMstop));
	Boolean hilite = NOTNIL(GetFrameSlotRef(cmd, RSSYMdohilite)) && VisibleDeep();
	RefVar config(GetFrameSlotRef(cmd, RSSYMrecconfig));
	long firstStart = start;
	long oldLength = stop - start;
	InkWordEntry* entries = nil;
	long count = 0;
	{
		RefVar textRef(Text());
		TRichString rich(textRef);
		const UniChar* text = rich.GrabPtr();
		long cacheStart, cacheLength;
		GetCachedRange(&cacheStart, &cacheLength);
		long cacheLast = cacheStart + cacheLength - 1;
		// (the ROM's box is one stack slot, so a word outside the cache
		// keeps the left and right of the one before)
		Rect bounds = { 0, 0, 0, 0 };
		for (ULong i = start; i < (ULong) stop; i++)
		{
			style = GetStyleAtOffset(i, nil, nil);
			if (text[i] == kInkWordChar && IsInkWord(style))
			{
				if ((long) i < cacheStart || (long) i > cacheLast)
				{
					bounds.top = -32768;
					bounds.bottom = -32768;
				}
				else
				{
					Rect next;
					OffsetToBounds(i, &bounds);
					OffsetToBounds(i + 1, &next);
					bounds.right = next.left;
				}
				InkWordEntry* more = (InkWordEntry*) realloc(entries, (count + 1) * sizeof(InkWordEntry));
				if (more == nil)
					break;
				entries = more;
				entries[count].fStart = i;
				entries[count].fEnd = i + 1;
				entries[count].fBounds = bounds;
				count++;
			}
		}
		rich.ReleasePtr();
	}
	Boolean setupDone = fSetupDone;
	if (setupDone)
		fSetupDone = false;
	long moved = 0;
	for (long k = 0; k < count; k++)
	{
		long wordStart = entries[k].fStart + moved;
		long wordLength = entries[k].fEnd - entries[k].fStart;
		if (hilite && entries[k].fBounds.top != -32768)
			DrawCheckmark(entries[k].fBounds);
		RefVar again(MakeCommand(aeRecognizeInk, this, fId));
		SetFrameSlot(again, RSSYMstart, RefVar(MAKEINT(wordStart)));
		SetFrameSlot(again, RSSYMstop, RefVar(MAKEINT(wordLength)));
		SetFrameSlot(again, RSSYMdohilite, RefVar(NILREF));
		SetFrameSlot(again, RSSYMrecconfig, config);
		gApplication->DispatchCommand(again);
		moved += RINT(GetFrameSlotRef(again, RSSYMstop)) - wordLength;
	}
	free(entries);
	long newStop = stop + moved;
	SetFrameSlot(cmd, RSSYMstop, RefVar(MAKEINT(newStop)));
	if (setupDone)
		fSetupDone = true;
	RangeChanged(firstStart, oldLength, newStop - firstStart, RSSYMtext);
	CommandSetResult(cmd, 1);
	return true;
}


// ROM 0x001690b4 GetCachedRange__14TParagraphViewFPlT1
void
TParagraphView::GetCachedRange(long* start, long* length)
{
	long count = 0;
	if (fLines == nil || fLineCount == 0)
		*start = 0;
	else
	{
		*start = fLines[0].fStart;
		count = fLines[fLineCount - 1].fEnd - fLines[0].fStart;
	}
	*length = count;
}


/*------------------------------------------------------------------------------
	D r a w i n g
------------------------------------------------------------------------------*/

// one line drawn from its baseline, its runs in their styles, the
// ellipsis after it when asked
void
TParagraphView::DrawLine(const UniChar* text, const LineInfo& line, Boolean ellipsis)
{
	StyleRecord** lineStyles = (StyleRecord**) NewPtrClear(fRunCount * sizeof(StyleRecord*));
	short* lineLengths = (short*) NewPtrClear(fRunCount * sizeof(short));
	long firstRun;
	long runs = RunsOfRange(fRunStyles, fRunLengths, fRunCount, line.fStart, line.fTextEnd - line.fStart, lineStyles, lineLengths, &firstRun);
	TextOptions options = fTextOptions;
	FPoint where;
	where.x = ToFixed(viewBounds.left);
	where.y = ToFixed(line.fBounds.top + line.fAscent);
	if (line.fTextEnd > line.fStart)
		DoTextOnce(text + line.fStart, line.fTextEnd - line.fStart, lineStyles, lineLengths, where, &options, nil, true);
	if (ellipsis)
	{
		// the ellipsis in the style the text would be inserted in at the line's end, after what fit
		UniChar dots = kEllipsisChar;
		StyleRecord* style = runs > 0 ? lineStyles[runs - 1] : fRunStyles[fRunCount - 1];
		TextOptions dotOptions;
		memset(&dotOptions, 0, sizeof(dotOptions));
		dotOptions.fTransferMode = fTransferMode;
		FPoint at;
		long textWidth = line.fBounds.right - line.fBounds.left;
		long slack = (viewBounds.right - viewBounds.left) - textWidth;
		long left = viewBounds.left;
		if (options.fAlignment == 0x10000)
			left += slack;
		else if (options.fAlignment == 0x8000)
			left += slack / 2;
		at.x = ToFixed(left + textWidth);
		at.y = where.y;
		DoTextOnce(&dots, 1, &style, nil, at, &dotOptions, nil, true);
	}
	DisposPtr((Ptr) lineStyles);
	DisposPtr((Ptr) lineLengths);
}


// ROM 0x0016911c RealDraw__14TParagraphViewFR5TRect
// The lines drawn (laid out first when they are not cached, moved along
// when the view has moved since); an ellipsis after the last line when
// the text goes on past it and the view does not calculate its bounds.
// NOT YET RECONSTRUCTED: the hilites and caret drawn with the text.
void
TParagraphView::RealDraw(Rect& /*bounds*/)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	else
	{
		Point delta;
		delta.h = (short) (viewBounds.left - fCachedBounds.left);
		delta.v = (short) (viewBounds.top - fCachedBounds.top);
		OffsetCachedBounds(delta);
	}
	if (fLineCount == 0)
		return;
	RefVar textRef(Text());
	if (ISNIL(textRef))
		return;
	TRichString rich(textRef);
	long length = rich.Length();
	const UniChar* text = rich.GrabPtr();
	for (long i = 0; i < fLineCount; i++)
	{
		const LineInfo& line = fLines[i];
		Boolean ellipsis = i == fLineCount - 1 && !fCalculateBounds && line.fEnd < length;
		DrawLine(text, line, ellipsis);
	}
	rich.ReleasePtr();
	// the selection over the text (the ROM does this in PostDraw)
	DrawHilites(false);
}

// ROM 0x000a2e24 GetJustificationOfDroppedText__FRC6RefVar
// What a piece of text that has been written or dropped asks for in the
// way of justification: the low two bits of its viewJustify.  It only
// counts when the view it came from works out its own bounds
// (vCalculateBounds), because a view with bounds of its own has already
// been justified inside them.  ==> 0 for nothing asked, the two bits
// when they are asked for, and 4 for a justification that is there but
// is not the caller's business.
long
GetJustificationOfDroppedText(RefArg info)
{
	RefVar justify(GetProtoVariable(info, RSSYMviewjustify, nil));
	if (ISNIL(justify))
		return 0;
	long bits = RINT(justify) & 3;
	if (bits == 0)
		return 0;
	RefVar flags(GetProtoVariable(info, RSSYMviewflags, nil));
	if (NOTNIL(flags) && (RINT(flags) & vCalculateBounds) != 0)
		return bits;
	return 4;
}


// ROM 0x0017a4d8 MakeParagraphForm__FPUslRC5TRectRC6RefVarUc
// The context frame a new paragraph is built from: a clone of the ROM's
// starterParagraph with the bounds and the text put into it, and then
// whatever `info` - the style of the word that is going in - has to say.
// The styles, the tabs and the correction information are carried over;
// the text flags only when they differ from protoParagraph's, and the
// justification only when the text asked for one.
//
// A style array of exactly two whose second element is not an ink word
// is one font for the whole paragraph, so it becomes the viewFont and
// the styles go; with no styles at all the font is the user's, and that
// is only written down when it differs from protoParagraph's.
Ref
MakeParagraphForm(UniChar* text, long length, const Rect& bounds, RefArg info, Boolean /*flag*/)
{
	RefVar form(Clone(RefVar(Rstarterparagraph)));
	SetFrameSlot(form, RSSYMviewbounds, RefVar(ToObject(bounds)));
	SetFrameSlot(form, RSSYMtext, RefVar(MakeString(text, length)));
	RefVar styles;
	RefVar font(GetPreference(RSSYMuserfont));
	if (NOTNIL(info))
	{
		long justify = GetJustificationOfDroppedText(info);
		if (justify != 0 && justify != 4)
			SetFrameSlot(form, RSSYMviewjustify, RefVar(MAKEINT(justify)));
		RefVar slot(GetProtoVariable(info, RSSYMstyles, nil));
		if (NOTNIL(slot))
		{
			styles = slot;
			SetFrameSlot(form, RSSYMstyles, RefVar(Clone(slot)));
		}
		else
		{
			slot = GetProtoVariable(info, RSSYMviewfont, nil);
			if (NOTNIL(slot))
				font = slot;
		}
		slot = GetProtoVariable(info, RSSYMtabs, nil);
		if (NOTNIL(slot))
			SetFrameSlot(form, RSSYMtabs, RefVar(Clone(slot)));
		slot = GetProtoVariable(info, RSSYMtextflags, nil);
		if (NOTNIL(slot))
		{
			RefVar standard(GetProtoVariable(RefVar(Rprotoparagraph), RSSYMtextflags, nil));
			if (RINT(standard) != RINT(slot))
				SetFrameSlot(form, RSSYMtextflags, slot);
		}
		slot = GetProtoVariable(info, RSSYMcorrectinfo, nil);
		if (NOTNIL(slot))
			SetFrameSlot(form, RSSYMcorrectinfo, slot);
	}
	if (NOTNIL(styles))
	{
		if (Length(styles) == 2)
		{
			RefVar only(GetArraySlotRef(styles, 1));
			if (!IsInkWord(only))
			{
				RemoveSlot(form, RSSYMstyles);
				SetFrameSlot(form, RSSYMviewfont, only);
			}
		}
	}
	else
	{
		RefVar standard(GetProtoVariable(RefVar(Rprotoparagraph), RSSYMviewfont, nil));
		if (!EQRef(font, standard))
			SetFrameSlot(form, RSSYMviewfont, RefVar(Clone(font)));
	}
	return form;
}


// ROM 0x00171e8c CaretRelativeToVisibleRect__14TParagraphViewFRC5TRect
// Which way the caret has gone out of `visible`, which is what tells a
// scrolling view where it has to scroll to.  0 it has not (and 0 for a
// view that does not hold the caret at all), 1 it is inside, 2 below, 3
// above, 4 left, 5 right.
//
// Three questions in order, and the cheapest first.  The line cache is
// asked by character offset: a caret past the last cached line's end is
// below, one before the first cached line's start is above, and no
// measuring is needed to say so.  With no lines cached the view's own
// bounds are compared with the rectangle, which answers for a paragraph
// wholly off the edge.  Only when neither has put the caret out is the
// root view asked where it actually is - and a caret that has never been
// placed (-32768) answers 0.
//
// (The ROM's line cache is a nil-terminated array of LineInfo pointers
// counted by CacheLength 0x0017c8b0; the reconstruction keeps the same
// records in a flat array with a count, so the two ends of it are
// fLines[0] and fLines[fLineCount - 1].)
long
TParagraphView::CaretRelativeToVisibleRect(const Rect& visible)
{
	long where = 0;
	if (gRootView->fCaretView != this)
		return 0;
	long lines = fLineCount;
	if (lines > 0)
	{
		if (fLines[lines - 1].fEnd < fCaretOffset)
			where = 2;
		else if (fLines[0].fStart > fCaretOffset)
			where = 3;
	}
	else
	{
		Rect box = viewBounds;
		if (box.bottom < visible.top)
			where = 3;
		else if (visible.bottom < box.top)
			where = 2;
		else if (box.right < visible.left)
			where = 4;
		else if (box.left > visible.right)
			where = 5;
	}
	if (where != 0)
		return where;
	Point caret;
	gRootView->GetCaretPoint(&caret);
	if (caret.v == -32768)			// there is no caret to be out of anything
		return 0;
	if (caret.v < visible.top)
		where = 3;
	else if (visible.bottom < caret.v)
		where = 2;
	else if (caret.h < visible.left)
		where = 4;
	else
		where = caret.h > visible.right ? 5 : 1;
	return where;
}


// ROM 0x001ee218 FailGetParagraphView__FRC6RefVar
TParagraphView*
FailGetParagraphView(RefArg context)
{
	TView* view = FailGetView(context);
	if (!view->DerivedFrom(clParagraphView))
		ThrowMsg((char*) "not a paragraph view");
	return (TParagraphView*) view;
}


/*------------------------------------------------------------------------------
	S e l e c t i o n s   a n d   d r a g   a n d   d r o p

	A paragraph's selection is a range of its characters.  Dragged, the
	selected text goes as a 'text item whose data is a paragraph's frame
	of its own - the characters, their styles, the tabs, the correction
	information, and viewBounds in the paragraph's own coordinates - and
	text dropped on a paragraph is put in where it was let go, as a word
	written there would be (HandleWord, and failing that below the last
	line).  A paragraph does not take a drop on its own selection.
------------------------------------------------------------------------------*/

// ROM 0x0017ed50 IsCompletelyHilited__14TParagraphViewFRC6RefVar
// Whether a hilite takes in all the text.
Boolean
TParagraphView::IsCompletelyHilited(RefArg hilite)
{
	TParagraphHilite* h = (TParagraphHilite*) RefToAddress(hilite);
	RefVar text(Text());
	ULong length = (Length(text) - 2) >> 1;
	return h->fStart == 0 && h->fEnd >= (long) length;
}


// ROM 0x0017ea80 ClickOptions__14TParagraphViewFv
// What a click on the selection may do: always drag it (bit 0), and
// resize it too (bit 1) when the paragraph sizes itself and the whole of
// it is selected - resizing a selection resizes the paragraph.
long
TParagraphView::ClickOptions(void)
{
	RefVar hilite(FirstHilite());
	if (ISNIL(hilite) || (fFlags & vCalculateBounds) == 0 || !IsCompletelyHilited(hilite))
		return 1;
	return 3;
}


// ROM 0x0017edcc RemoveHilite__14TParagraphViewFRC6RefVar
// The selection gone, and the style palette brought up to date with it.
void
TParagraphView::RemoveHilite(RefArg hilite)
{
	TView::RemoveHilite(hilite);
	UpdateStylePalette();
}


// ROM 0x00174aac ROMDeleteHilited__14TParagraphViewFRC6RefVar
// The selected text deleted.  A hilite that is not a range of the text
// is left alone; one in text that may not be changed is only taken away.
// A paragraph that sizes itself and loses all its text - or everything
// but white space - goes altogether, with an undoable aeRemoveData to
// the page, unless the page keeps empty paragraphs (text flag 0x80).
void
TParagraphView::ROMDeleteHilited(RefArg hilite)
{
	TParagraphHilite* h = (TParagraphHilite*) RefToAddress(hilite);
	RefVar textRef(Text());
	long length = (long) ((Length(textRef) - 2) >> 1);
	long start = h->fStart;
	if (start < 0 || start > length)
		return;
	long end = h->fEnd;
	if (end < 0 || end > length || start >= end)
		return;
	if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
	{
		RemoveHilite(hilite);
		return;
	}
	if ((fFlags & vCalculateBounds) != 0)
	{
		const UniChar* text = GetCString(textRef);
		if (IsCompletelyHilited(hilite)
			|| (ContainsOnlyWhiteSpace(text, h->fStart) && ContainsOnlyWhiteSpace(text + h->fEnd, (ULong) -1)))
		{
			TView* editor = GetEnclosingEditView();
			if (editor != nil && (editor->TextFlags() & 0x80) != 0)
				DeleteHilitedTextOnly(hilite);
			else
				gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, fParent, fId)));
			return;
		}
	}
	DeleteHilitedTextOnly(hilite);
}


// ROM 0x0017eaf8 DeleteHilited__14TParagraphViewFRC6RefVar
// The selected text deleted (ROMDeleteHilited); a hilite the view's
// hilites do not hold is taken away afterwards as well.
void
TParagraphView::DeleteHilited(RefArg hilite)
{
	if (!Hilited())
		return;
	Boolean remove = true;
	RefVar hilites(Hilites());
	if (NOTNIL(hilites) && NOTNIL(FSetContains(RefVar(), hilites, hilite)))
		remove = false;
	ROMDeleteHilited(hilite);
	if (remove)
		RemoveHilite(hilite);
}


// ROM 0x001782e8 FindLineContainingPoint__14TParagraphViewFP6TPoint10MarginSize
// The line a point is on: of the lines whose box (widened by a thousand
// pixels each way for margins 1 and 2; raised by half its height and
// widened by ten for margin 3) holds the point, the one whose baseline is
// nearest, with the point's h brought inside its box.  Margin 2 is for a
// drop: a point above or below the paragraph is taken to the first
// line's top left or the last line's bottom right and answers that line,
// and a point between lines that none holds is brought to the nearer end.
// ==> the line's index, -1 for none.
//
// The ROM measures the distance to the line's bottom less the second of
// the two heights it keeps, which is its baseline; this cache keeps the
// baseline as the top and the ascent.
long
TParagraphView::FindLineContainingPoint(Point* pt, long margin)
{
	if (fLines == nil || fLineCount == 0)
		return -1;
	if (margin == 2)
	{
		if (pt->v < viewBounds.top)
		{
			pt->v = fLines[0].fBounds.top;
			pt->h = fLines[0].fBounds.left;
			return 0;
		}
		if (pt->v >= viewBounds.bottom)
		{
			pt->v = fLines[fLineCount - 1].fBounds.bottom;
			pt->h = fLines[fLineCount - 1].fBounds.right;
			return fLineCount - 1;
		}
	}
	long best = -1;
	long bestDistance = 10000;
	Rect bestBox;
	for (long i = 0; i < fLineCount; i++)
	{
		Rect box = fLines[i].fBounds;
		Rect work = box;
		if (margin == 1 || margin == 2)
			InsetRect(&work, -1000, 0);
		else if (margin == 3)
		{
			work.top = (short) (work.top - (short) (box.bottom - box.top) / 2);
			InsetRect(&work, -10, 0);
		}
		if (PtInRect(*pt, &work))
		{
			long distance = pt->v - (fLines[i].fBounds.top + fLines[i].fAscent);
			if (distance < 0)
				distance = -distance;
			if (distance < bestDistance)
			{
				bestDistance = distance;
				best = i;
				bestBox = box;
			}
		}
	}
	if (best >= 0)
	{
		if (pt->h < bestBox.left)
			pt->h = bestBox.left;
		if (pt->h > bestBox.right - 1)
			pt->h = (short) (bestBox.right - 1);
		return best;
	}
	if (margin == 2)
	{
		short top = fLines[0].fBounds.top;
		if (top > pt->v)
			pt->v = top;
		else
		{
			short bottom = fLines[fLineCount - 1].fBounds.bottom;
			if (bottom < pt->v)
				pt->v = bottom;
		}
	}
	return -1;
}


// ROM 0x00177c5c PointOverText__14TParagraphViewFR6TPointP6TPoint
// Whether a point is on a line of the text as it stands (margin 1: any
// distance to the side of it), and where on the line it would go.
Boolean
TParagraphView::PointOverText(Point& pt, Point* onLine)
{
	Point at = pt;
	long line = FindLineContainingPoint(&at, 1);
	if (onLine != nil)
		*onLine = at;
	return line >= 0 && pt.h == at.h && pt.v == at.v;
}


// ROM 0x0016b1b8 PointOverHilitedText__14TParagraphViewFR6TPoint
// Where a point is in relation to the selection: 0 not over it, 1 over
// it, 2 over it where the selection runs to the end of the text, 3 below
// the last line of such a selection (on the line below the paragraph, or
// below the line it ends on) - which is where a drop would add to the
// selected text rather than land in it.
//
// (host: PointToOffset is this reconstruction's nearest character, which
//  is never -1, where the ROM's answers -1 for a point on no line)
long
TParagraphView::PointOverHilitedText(Point& pt)
{
	if (!Hilited())
		return 0;
	Rect bounds = viewBounds;
	AddMarginsToBounds(&bounds);
	RefVar hilite(FirstHilite());
	TParagraphHilite* h = (TParagraphHilite*) RefToAddress(hilite);
	RefVar text(Text());
	Boolean atEnd = h->fEnd == (long) ((Length(text) - 2) >> 1);
	long result = 0;
	if (PtInRect(pt, &bounds))
	{
		long offset = PointToOffset(pt);
		if (offset < 0)
			return 0;
		if (h->fStart <= offset && offset <= h->fEnd)
		{
			result = 1;
			long first = FindLineContainingCharOffset(h->fStart);
			long last = FindLineContainingCharOffset(h->fEnd);
			if (first == last && first >= 0 && pt.h > fLines[first].fBounds.right)
				result = 0;
			else if (atEnd && first >= 0)
				result = fLines[first].fBounds.bottom < pt.v ? 3 : 2;
		}
	}
	else
	{
		Rect box;
		box.top = pt.v;
		box.left = pt.h;
		box.bottom = (short) (pt.v + 1);
		box.right = (short) (pt.h + 1);
		if (atEnd)
		{
			Point base;
			base.v = box.bottom;
			base.h = box.left;
			if (WordOnLineBelowParagraph(box, base))
				result = 3;
		}
	}
	return result;
}


// ROM 0x001811b0 GetRangeProperties__14TParagraphViewFlT1
// A frame describing a stretch of the text as a paragraph of its own
// would need it: the justification, font, styles of the stretch, tabs
// (a copy), text flags, how far the first line's text sits below its
// top ('offset), and the correction information of the stretch, moved
// to start at nought.  The text itself is the caller's to add.
//
// NOT YET: 'offset - the ROM's line cache keeps two heights this one
// does not, and the offset is the line's height less the two.
Ref
TParagraphView::GetRangeProperties(long start, long end)
{
	RefVar props(AllocateFrame());
	RefVar value(GetCacheProto(9));
	if (NOTNIL(value))
		SetFrameSlot(props, RSSYMviewjustify, value);
	value = GetProto(RSSYMviewfont);
	if (NOTNIL(value))
		SetFrameSlot(props, RSSYMviewfont, value);
	value = Styles();
	if (NOTNIL(value) && Length(value) > 0)
	{
		value = GetStylesOfRange(start, end - start, false);
		SetFrameSlot(props, RSSYMstyles, value);
	}
	value = GetProto(RSSYMtabs);
	if (NOTNIL(value))
		SetFrameSlot(props, RSSYMtabs, RefVar(Clone(value)));
	value = GetProto(RSSYMtextflags);
	if (NOTNIL(value))
		SetFrameSlot(props, RSSYMtextflags, value);
	value = ExtractRange(RefVar(CorrectInfo()), this, start, end);
	if (NOTNIL(value))
	{
		OffsetCorrectionInfo(value, this, 0, start, 0);
		SetFrameSlot(props, RSSYMcorrectinfo, value);
	}
	return props;
}


// ROM 0x0017f320 AddDragInfo__14TParagraphViewFP9TDragInfo
// The script's items, or else one 'text item whose drag ref is the
// paragraph's context.
Boolean
TParagraphView::AddDragInfo(TDragInfo* dragInfo)
{
	if (TView::AddDragInfo(dragInfo))
		return true;
	dragInfo->AddDragItem(RefVar(RSSYMtext), RefVar(fContext), RefVar());
	return true;
}


// ROM 0x0017f378 GetSupportedDropTypes__14TParagraphViewFRC6TPoint
Ref
TParagraphView::GetSupportedDropTypes(const Point& pt)
{
	RefVar types(TView::GetSupportedDropTypes(pt));
	if (ISNIL(types))
	{
		types = MakeArray(2);
		SetArraySlot(types, 0, RefVar(RSSYMtext));
		SetArraySlot(types, 1, RefVar(RSSYMink));
	}
	return types;
}


// ROM 0x0017f3f4 GetDropData__14TParagraphViewFRC6RefVarT1
// The script's data, or else the selected text as a paragraph frame
// (GetRangeProperties, less the justification) whose viewBounds are
// the selection's in the paragraph's own coordinates - all the text and
// the bounds it was laid out in when nothing is selected.
Ref
TParagraphView::GetDropData(RefArg dragType, RefArg dragRef)
{
	RefVar data(TView::GetDropData(dragType, dragRef));
	if (NOTNIL(data))
		return data;
	RefVar hilite(FirstHilite());
	long start, end;
	Rect box;
	if (NOTNIL(hilite))
	{
		TParagraphHilite* h = (TParagraphHilite*) RefToAddress(hilite);
		start = h->fStart;
		end = h->fEnd;
		box.top = box.bottom = -32768;
		GlobalHiliteBounds(&box);
	}
	else
	{
		start = 0;
		RefVar text(Text());
		end = (long) ((Length(text) - 2) >> 1);
		box = fCachedBounds;
	}
	OffsetRect(&box, -viewBounds.left, -viewBounds.top);
	data = GetRangeProperties(start, end);
	RemoveSlot(data, RSSYMviewjustify);
	SetFrameSlot(data, RSSYMtext, RefVar(ExtractTextRange(start, end - start)));
	SetFrameSlot(data, RSSYMviewbounds, RefVar(ToObject(box)));
	return data;
}


// ROM 0x0017fc20 Drop__14TParagraphViewFRC6RefVarT1P6TPoint
// Text dropped on the paragraph, when its script does not take it: put
// in where it was let go as a word written there would be (HandleWord,
// with the dropped frame as its properties - a rich string's ink kept as
// its styles), failing that below the last line (AddWord).  The drop
// point comes back as where the text now ends.  A drop on the selection
// itself is refused.
//
// NOT YET RECONSTRUCTED: ink dropped on a paragraph, which the ROM puts in
// as an ink word - InkConvert turning the ink into one is the CIC
// library's ConverterRun (0x00280980 ConvertData), not reconstructed.
Boolean
TParagraphView::Drop(RefArg dropType, RefArg dropData, Point* dropPt)
{
	if (TView::Drop(dropType, dropData, dropPt))
		return true;
	long over = PointOverHilitedText(*dropPt);
	if (over == 1 || over == 2)
		return false;
	if (!EQRef(dropType, RSSYMtext))
		return false;
	RefVar text(GetFrameSlotRef(dropData, RSSYMtext));
	if (IsRichString(text))
	{
		TRichString rich(text);
		text = rich.MakeParagraphTextSlot();
		if (!FrameHasSlot(dropData, RSSYMstyles))
			SetFrameSlot(dropData, RSSYMstyles, RefVar(rich.MakeParagraphStylesSlot(RefVar(GetDefaultViewStyle()))));
	}
	const UniChar* chars = GetCString(text);
	ULong length = Ustrlen(chars);
	RefVar props(dropData);
	fSetupDone = false;
	Rect box;
	box.top = dropPt->v;
	box.left = dropPt->h;
	box.bottom = (short) (dropPt->v + 1);
	box.right = (short) (dropPt->h + length);
	long offset = 0;
	if (!HandleWord(chars, length, box, *dropPt, 0, 0, props, true, &offset, nil))
	{
		Finder finder;
		SetFinderBelowParagraph(&finder);
		finder.fNewLine = false;
		AddWord(&finder, GetCString(text), length, props, &offset);
	}
	OffsetToBounds(offset + length, &box);
	dropPt->h = box.left;
	dropPt->v = (short) (box.bottom - 1);
	return true;
}


// ROM 0x0017fff8 DropMove__14TParagraphViewFRC6RefVarRC6TPointT2Uc
// Selected text dragged within its own paragraph: dropped where it was
// let go (brought onto a line), and then, unless it was a copy, taken
// out from where it was.
Boolean
TParagraphView::DropMove(RefArg dragRef, const Point& delta, const Point& dropPt, Boolean copy)
{
	if (TView::DropMove(dragRef, delta, dropPt, copy))
		return true;
	RefVar data(GetDropData(RefVar(RSSYMtext), dragRef));
	Point pt = dropPt;
	FindLineContainingPoint(&pt, 2);
	if (!Drop(RefVar(RSSYMtext), data, &pt))
		return false;
	if (!copy)
		DropRemove(dragRef);
	return true;
}


// ROM 0x00180164 DropRemove__14TParagraphViewFRC6RefVar
// The dragged text taken out: the selection deleted.
Boolean
TParagraphView::DropRemove(RefArg dragRef)
{
	if (TView::DropRemove(dragRef))
		return true;
	if (NOTNIL(FirstHilite()))
		DeleteHilited(RefVar(FirstHilite()));
	return true;
}


// ROM 0x001800e4 DropDone__14TParagraphViewFv
// The drop over: the styles processed again over the whole text (Drop
// holds them off while it works) and the selection taken away.
Boolean
TParagraphView::DropDone(void)
{
	fSetupDone = true;
	RefVar text(Text());
	long length = (long) ((Length(text) - 2) >> 1);
	RangeChanged(0, length, length, RefVar(RSSYMtext));
	RemoveAllHilites();
	TView::DropDone();
	return true;
}


// ROM 0x001801d0 DragFeedback__14TParagraphViewFRC9TDragInfoRC6TPointUc
// Where the text would go shown as a caret inverted at the point, unless
// the script shows it.  Inverting twice takes it away again.
Boolean
TParagraphView::DragFeedback(const TDragInfo& dragInfo, const Point& pt, Boolean show)
{
	if (TView::DragFeedback(dragInfo, pt, show))
		return true;
	Point at = pt;
	Rect caret;
	PointToCaret(at, &caret, nil);
	if (caret.top == -32768)
		return false;
	InvertRect(&caret);
	return true;
}


/*------------------------------------------------------------------------------
	C l i c k s   o n   t h e   s e l e c t i o n ,   a n d   r e s i z i n g

	The pen pressed on the selected text drags it (HiliteClick); pressed
	on a clipping's label, it drags the clipping (IconClick).  A paragraph
	that is wholly selected can be resized by the page's gray border
	(TEditView::TrackScale), which draws it scaled as the pen moves
	(DrawScaledData) and then sends it aeScaleData; a paragraph resized so
	stops fitting its width or height to its text.
------------------------------------------------------------------------------*/

// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0x187c (aeClick)
// A clipping's label is picked up; anything else is a press on the
// selection, which may be the start of dragging it.  Neither taken, the
// click goes to the scripts (a second time, when the paragraph takes
// gestures itself and its script declined it the first time).
Boolean
TParagraphView::ClickCommand(RefArg cmd)
{
	if ((TextFlags() & 0x2000) != 0 && TView::RealDoCommand(cmd))
		return true;
	TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
	if (((fFlags & vClipboard) != 0 && IconClick(unit->Stroke()))
		|| HiliteClick(unit->Stroke()))
	{
		CommandSetResult(cmd, 1);
		return true;
	}
	return TView::RealDoCommand(cmd);
}


// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0x2464 (aeScaleData)
// The paragraph scaled as the rectangle in params 0-1 maps onto the one
// in 2-3 (TEditView::TrackScale sends the selection's bounds and the new
// ones).  A paragraph the size of the first rectangle is taken to be it:
// its bounds are moved onto it before scaling and back again after.  One
// resized so no longer fits itself to its text (text flags 1 and 4 go,
// in its data frame too); its new bounds are written in its parent's
// coordinates, the parent is told, and an undo that scales it back is
// posted.
Boolean
TParagraphView::ScaleCommand(RefArg cmd)
{
	if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
		return TView::RealDoCommand(cmd);
	Rect src, dst;
	CommandIndexRect(cmd, 0, &src);
	CommandIndexRect(cmd, 2, &dst);
	Rect r = viewBounds;
	Rect old = viewBounds;
	Boolean sameSize = false;
	Point by;
	if ((short) (old.right - old.left) == (short) (src.right - src.left)
		&& (short) (old.bottom - old.top) == (short) (src.bottom - src.top))
	{
		sameSize = true;
		by.h = (short) (src.left - old.left);
		by.v = (short) (src.top - old.top);
		OffsetRect(&r, by.h, by.v);
	}
	TTransform transform;
	transform.Setup(&src, &dst, false);
	::Scale(&r, transform);
	if (sameSize)
		OffsetRect(&r, -by.h, -by.v);
	if ((TextFlags() & 1) != 0 || (TextFlags() & 4) != 0)
	{
		fTextFlags &= ~5;
		SetFrameSlot(RefVar(DataFrame()), RSSYMtextflags, RefVar(MAKEINT(TextFlags() & ~5)));
	}
	Point origin = fParent->ContentsOrigin();
	OffsetRect(&r, -origin.h, -origin.v);
	fCachesValid = false;
	WriteBounds(r);
	fCachesValid = true;
	FixupBBox();
	Rect now = viewBounds;
	fParent->ChildBoundsChanged(this, old);
	RefVar undo(MakeCommand(aeScaleData, this, kNoParameter));
	CommandSetIndexRect(undo, 0, now);
	CommandSetIndexRect(undo, 2, old);
	gApplication->PostUndoCommand(undo);
	fParent->Dirty(nil);
	return true;
}


// ROM 0x0016afe4 DrawScaledData__14TParagraphViewFRC5TRectT1P5TRect
// The paragraph drawn as a resize from `src` to `dst` would leave it,
// while the pen is still resizing: its bounds scaled and written for the
// moment (not fitting itself to its text meanwhile), the text laid out
// and drawn in them, and everything put back.  `bounds` comes back as
// where it was drawn.
void
TParagraphView::DrawScaledData(const Rect& src, const Rect& dst, Rect* bounds)
{
	Rect r = viewBounds;
	Rect saved = viewBounds;
	TTransform transform;
	transform.Setup(&src, &dst, false);
	::Scale(&r, transform);
	Boolean fitWidth = (TextFlags() & 1) != 0;
	Boolean fitHeight = (TextFlags() & 4) != 0;
	if (fitWidth)
		fTextFlags &= ~1;
	if (fitHeight)
		fTextFlags &= ~4;
	Point origin = fParent->ContentsOrigin();
	OffsetRect(&r, -origin.h, -origin.v);
	fCachesValid = false;
	WriteBounds(r);
	fCachesValid = true;
	FixupBBox();
	Rect drawn = viewBounds;
	*bounds = drawn;
	RealDraw(drawn);
	fCachesValid = false;
	OffsetRect(&saved, -origin.h, -origin.v);
	WriteBounds(saved);
	fCachesValid = true;
	if (fitWidth)
		fTextFlags |= 1;
	if (fitHeight)
		fTextFlags |= 4;
}


// ROM 0x00181418 GetProperties__14TParagraphViewFRC6RefVar
// The properties of a selection's text as a paragraph of its own would
// need them - GetRangeProperties over the hilite's range, which the ROM
// writes out a second time word for word.
Ref
TParagraphView::GetProperties(RefArg hilite)
{
	TParagraphHilite* h = (TParagraphHilite*) RefToAddress(hilite);
	return GetRangeProperties(h->fStart, h->fEnd);
}


// ROM 0x0017ebb4 AddHilited__14TParagraphViewFRC6RefVarP9TEditView
// The selected text made a paragraph of its own on the page, selected
// whole: where the selection is drawn (in the page's coordinates), or,
// for the whole paragraph, where the paragraph is.
//
// (host: the hilite's +0x14, the ROM's pointer to the selected text, is
//  NOT YET, so the text is found from its offsets)
TView*
TParagraphView::AddHilited(RefArg hilite, TEditView* editor)
{
	TParagraphHilite* h = (TParagraphHilite*) RefToAddress(hilite);
	Rect r;
	if (!IsCompletelyHilited(hilite))
	{
		r = h->fBounds;
		Point origin = LocalOrigin();
		OffsetRect(&r, origin.h, origin.v);
	}
	else
	{
		r = viewBounds;
		Point origin = ContentsOrigin();
		OffsetRect(&r, -origin.h, -origin.v);
	}
	RefVar props(GetProperties(hilite));
	RefVar text(Text());
	RefVar form(MakeParagraphForm((UniChar*) GetCString(text) + h->fStart, h->fEnd - h->fStart, r, props, false));
	TParagraphView* view = (TParagraphView*) editor->AddForm(form);
	if (h != nil)
		view->MakeHilite(0, h->fEnd - h->fStart, true);
	return view;
}


// ROM 0x0c101760 (unnamed) - when a paragraph's selection was last pressed
ULong	gLastParagraphClick = 0;


// ROM 0x0017aefc LengthSansTabsAndCRs__FPUsPUc
// How long the text would be with each run of tabs and returns made one
// space - a run at the very end counting nothing - and whether it has
// any.
long
LengthSansTabsAndCRs(const UniChar* text, Boolean* found)
{
	long length = 0;
	Boolean inRun = false;
	*found = false;
	for ( ; *text != 0; text++)
	{
		if (*text == 0x09 || *text == 0x0D)
		{
			inRun = true;
			*found = true;
		}
		else
		{
			if (inRun)
			{
				length++;
				inRun = false;
			}
			length++;
		}
	}
	return length;
}


// ROM 0x0017ad6c RemoveTabsAndCRs__FPUsRC6RefVar
// The text with each run of tabs and returns made one space - a run at
// the end dropped - and the style runs shortened to match; nil when it
// has none (or there is no memory for the copy).  The copy is the
// caller's to delete[].
UniChar*
RemoveTabsAndCRs(const UniChar* text, RefArg styles)
{
	Boolean found = false;
	long length = LengthSansTabsAndCRs(text, &found);
	if (!found)
		return nil;
	UniChar* result = new UniChar[length + 1];
	if (result == nil)
		return nil;
	UniChar* out = result;
	long run = 0;
	for ( ; *text != 0; text++)
	{
		if (*text == 0x09 || *text == 0x0D)
			run++;
		else
		{
			if (run > 0)
			{
				*out++ = ' ';
				if (NOTNIL(styles))
					RunsDelete(styles, out - result, run - 1);
				run = 0;
			}
			*out++ = *text;
		}
	}
	if (run > 0 && NOTNIL(styles))
		RunsDelete(styles, out - result, run);
	*out = 0;
	return result;
}


// ROM 0x0017e83c CleanupData__14TParagraphViewFv
// After a resize has joined paragraphs into this one: its tabs and
// returns made single spaces, since the text now flows in one paragraph.
void
TParagraphView::CleanupData(void)
{
	RefVar styles(Styles());
	RefVar newStyles;
	if (NOTNIL(styles))
		newStyles = Clone(styles);
	RefVar text(Text());
	UniChar* plain = RemoveTabsAndCRs(GetCString(text), newStyles);
	if (plain != nil)
	{
		RefVar current(Text());
		ULong had = (Length(current) - 2) >> 1;
		InsertStyledText(0, plain, Ustrlen(plain), newStyles, RefVar(), 0, had, false);
		delete[] plain;
	}
}


// ROM 0x0017ede4 HiliteClick__14TParagraphViewFP13TStrokePublic
// The pen pressed on the selected text: the text dragged as a 'text item
// (whose drag ref is this paragraph).  It is a copy when the paragraph
// had just been tapped, or when two presses come within 80 ticks of one
// another.  ==> whether it was dragged.
Boolean
TParagraphView::HiliteClick(TStrokePublic* stroke)
{
	Point pt = stroke->FirstPoint();
	if (!Hilited() || (ClickOptions() & 1) == 0 || !PointInHilite(pt))
		return false;
	Rect bounds;
	bounds.top = bounds.bottom = -32768;
	GlobalHiliteBounds(&bounds);
	Rect pinned;
	pinned.top = pinned.bottom = -32768;
	GlobalHilitePinnedBounds(&pinned);
	ULong now = Ticks();
	ULong since = now - gLastParagraphClick;
	gLastParagraphClick = now;
	Boolean copy = fTapped || (since > 0 && since < 80);
	fTapped = false;
	TDragInfo dragInfo(RefVar(RSSYMtext), RefVar(AddressToRef(this)), RefVar());
	return DragAndDrop(stroke, bounds, &pinned, &pinned, copy, dragInfo, nil) != 0;
}


// ROM 0x0017efa8 IconClick__14TParagraphViewFP13TStrokePublic
// The label of a clipping picked up: the clipping it belongs to dragged.
Boolean
TParagraphView::IconClick(TStrokePublic* stroke)
{
	return ((TClipboard*) gRootView->GetClipboard(this))->DragFromClipboard(stroke);
}

/*------------------------------------------------------------------------------
	T h e   b a s e l i n e s

	The ROM keeps four halfwords at +0xa0 as the lines are laid out: the
	first line's baseline and ascent, the last line's baseline and
	descent.  DEVIATION: the host's line cache has no such block (+0xa0 is
	the lines' union here), so they are read off the first and last
	LineInfo - the baseline its top plus its ascent, the descent what the
	line's height leaves below it.
------------------------------------------------------------------------------*/

// the metrics the ROM keeps at +0xa0..+0xa6 (see above)
static long
FirstLineBaseline(const TParagraphView* view)
{
	const LineInfo& line = view->Line(0);
	return (short) (line.fBounds.top + line.fAscent);
}

static long
FirstLineAscent(const TParagraphView* view)
{
	return (short) view->Line(0).fAscent;
}

static long
LastLineBaseline(const TParagraphView* view)
{
	const LineInfo& line = view->Line(view->LineCount() - 1);
	return (short) (line.fBounds.top + line.fAscent);
}

static long
LastLineDescent(const TParagraphView* view)
{
	const LineInfo& line = view->Line(view->LineCount() - 1);
	return (short) (line.fHeight - line.fAscent);
}


// ROM 0x0017a0fc GetParagraphStyleRecordMetrics__FP11StyleRecordPlN32
void
GetParagraphStyleRecordMetrics(StyleRecord* style, long* ascent, long* descent, long* lineAscent, long* lineDescent)
{
	FontInfo fontInfo;
	GetStyleFontInfo(style, &fontInfo);
	*ascent = fontInfo.ascent;
	*descent = fontInfo.descent;
	if (lineAscent == nil)
		return;
	if (!IsInkWord(style->fFontFamily))
	{
		*lineAscent = fontInfo.ascent;
		*lineDescent = fontInfo.descent;
		return;
	}
	ULong size = (ULong) (long) (short) ((ULong) (style->fFontSize + 0x8000) >> 16);
	if (size < 13)
	{
		*lineAscent = 14;
		*lineDescent = 5;
	}
	else if (size <= 39)
	{
		*lineAscent = 17;
		*lineDescent = 5;
	}
	else
	{
		ULong steps = (size - 22) / 18;
		*lineAscent = steps * 17 + 17;
		*lineDescent = steps * 5 + 5;
	}
}


// ROM 0x001693fc GetRequestedLineSpacing__14TParagraphViewFv
long
TParagraphView::GetRequestedLineSpacing(void)
{
	if (fLineSpacing != 0)
		return fLineSpacing;
	RefVar spacing(GetVar(RSSYMviewlinespacing));
	return ISNIL(spacing) ? 0 : RINT(spacing);
}


// the font of the style an insertion at the offset would take (the empty
// paragraph's baselines are worked out of it)
static void
InsertionFontInfo(TParagraphView* view, long offset, FontInfo* fontInfo)
{
	RefVar style(view->GetStyleForInsertion(offset, false, false));
	StyleRecord record;
	CreateParagraphStyleRecord(style, &record, (ULong) view->fTextFlags, RefVar(view->GetDefaultViewStyle()));
	GetStyleFontInfo(&record, fontInfo);
	DisposeStyleRecord(&record);
}


// ROM 0x0016b77c GetFirstBaseline__14TParagraphViewFv
// The first line's baseline; an empty paragraph's is the insertion
// style's ascent below its top.
long
TParagraphView::GetFirstBaseline(void)
{
	if (fLines == nil)
		CreateAllCaches();
	Rect bounds = viewBounds;
	if (TextLength() == 0 || fLineCount == 0)
	{
		FontInfo fontInfo;
		InsertionFontInfo(this, 0, &fontInfo);
		return bounds.top + (short) fontInfo.ascent;
	}
	return FirstLineBaseline(this);
}


// ROM 0x0016b8a8 GetLastBaseline__14TParagraphViewFv
// The last line's baseline - a line further down when the text ends in a
// return, the empty line after it being where the caret would go.
long
TParagraphView::GetLastBaseline(void)
{
	if (fLines == nil)
		CreateAllCaches();
	Rect bounds = viewBounds;
	long length = TextLength();
	if (length == 0 || fLineCount == 0)
	{
		FontInfo fontInfo;
		InsertionFontInfo(this, 0, &fontInfo);
		return bounds.top + (short) fontInfo.ascent;
	}
	long baseline = LastLineBaseline(this);
	RefVar text(Text());
	const UniChar* chars = (const UniChar*) BinaryData(text);
	if (chars[length - 1] == kCR)
		baseline += (short) fLineHeight;
	return baseline;
}


// ROM 0x0016b454 GetNextBaseline__14TParagraphViewFP14TParagraphView
// Where the first baseline of the paragraph after this one goes.  With
// line spacing on both, the spacing below the last baseline.  Without,
// the last line's descent and the next paragraph's first ascent below it
// - unless this one asks for a spacing the next one's font fits (at
// least eight more than its ascent, and eight tenths of it no more than
// five more), when that is the gap; an empty next paragraph is measured
// by the style it would type in.  With no next paragraph, or when this
// one is empty, the insertion style's ascent below this one's *bottom*
// and a line's spacing below that.
long
TParagraphView::GetNextBaseline(TParagraphView* next)
{
	Rect bounds = viewBounds;
	long spacing = (short) GetInterLineSpacing();
	long nextSpacing = next != nil ? (short) next->GetInterLineSpacing() : 0;
	if (fLines == nil)
		CreateAllCaches();
	long length = TextLength();
	long baseline;
	if (next == nil || length == 0 || fLineCount == 0)
	{
		FontInfo fontInfo;
		InsertionFontInfo(this, length, &fontInfo);
		baseline = (short) (bounds.bottom + fontInfo.ascent);
		if (spacing < 1)
			spacing = (short) (fontInfo.descent + fontInfo.ascent + fontInfo.leading);
	}
	else
	{
		baseline = (short) GetLastBaseline();
		if (spacing <= 0 || nextSpacing <= 0)
		{
			if (next->fLines == nil)
				next->CreateAllCaches();
			if (next->TextLength() == 0 || next->fLineCount == 0)
			{
				RefVar style(next->GetStyleForInsertion(0, false, false));
				StyleRecord record;
				CreateParagraphStyleRecord(style, &record, (ULong) next->fTextFlags, RefVar(next->GetDefaultViewStyle()));
				long ascent, descent, lineAscent, lineDescent;
				GetParagraphStyleRecordMetrics(&record, &ascent, &descent, &lineAscent, &lineDescent);
				DisposeStyleRecord(&record);
				return (short) (LastLineDescent(this) + baseline + lineAscent);
			}
			long nextAscent = FirstLineAscent(next);
			spacing = (short) GetRequestedLineSpacing();
			if (spacing < 1 || spacing < nextAscent + 8 || nextAscent + 5 < (spacing * 8) / 10)
				return baseline + LastLineDescent(this) + nextAscent;
		}
	}
	return baseline + spacing;
}


// ROM 0x0016b750 AdjustBoundsForFirstBaseline__14TParagraphViewFl
void
TParagraphView::AdjustBoundsForFirstBaseline(long baseline)
{
	long delta = (short) (baseline - GetFirstBaseline());
	viewBounds.top = (short) (viewBounds.top + delta);
	viewBounds.bottom = (short) (viewBounds.bottom + delta);
}
