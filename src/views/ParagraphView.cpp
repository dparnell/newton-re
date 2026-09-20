/*
	File:		views/ParagraphView.cpp

	Contains:	TParagraphView: a view of styled text, display only.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ParagraphView.h"
#include "Hilites.h"
#include "OSErrors.h"
#include "StyleRuns.h"
#include "Unicode.h"
#include "RootView.h"
#include "Application.h"
#include "Commands.h"
#include "Animate.h"
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
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "Locale.h"
#include "NewtonExceptions.h"
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
// The bounds set (TView); the text flags made known when they are not
// yet; the lines laid out again when the size changed, moved along when
// the view only moved.
void
TParagraphView::SetBounds(const Rect& bounds)
{
	if (fTextFlags == -1)
		fTextFlags = (long) GetInputViewTextFlags((ULong) TextFlags(), fFlags);
	TView::SetBounds(bounds);
	if (fLines == nil)
		return;
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
			CreateTextStyleRecord(spec, fRunStyles[i]);
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
		CreateTextStyleRecord(spec, fRunStyles[0]);
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
// bounds; the last line's height the line height.  The lines are moved
// down by the vertical text bits when the text is shorter than the
// bounds: centred, or to the bottom.  NOT YET RECONSTRUCTED: the ROM's
// LineLoop (tabs, the text objects it makes for every run of a line, the
// parents' bounds narrowing the lines, the empty last line after a final
// carriage return).
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
	while (pos < length)
	{
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
		LineInfo& line = fLines[fLineCount];
		line.fStart = pos;
		line.fEnd = pos + fitted;
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
		const UniChar* next = SkipUpToTwoSpacesAndCR(text + pos + fitted, text + length);
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
// and MakeParagraphStylesSlot); RemoveCorrectionInfo, which belongs to
// the corrector; and the vCalculateBounds paragraph whose text has just
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
		// NOT YET: RemoveCorrectionInfo(this)
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
// caret wants): the line found, the text up to the offset measured for
// the left, the line's top and baseline for the top and bottom; without
// lines (no text) the view's top-left in the default style's height.
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
	rich.ReleasePtr();
	bounds->left = line.fBounds.left + width;
	bounds->right = bounds->left;
	bounds->top = line.fBounds.top;
	bounds->bottom = line.fBounds.top + line.fAscent;		// the baseline
}


// ROM 0x00171ad4 OffsetToCaret__14TParagraphViewFlP5TRect
// Where the caret goes for the offset: the character's box (OffsetToBounds)
// - past the last line's end the caret stays at that end (an offset on a
// trailing return goes to the next line's start, NOT YET) - its left a
// pixel in, kept inside the view's sides and its bottom (the baseline)
// inside the view unless it calculates its bounds; the rect is 2 wide.
// Empty when the offset is outside the cached range.
void
TParagraphView::OffsetToCaret(long offset, Rect* caret)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	long textLength = TextLength();
	if (offset < 0 || offset > textLength)
	{
		SetEmptyRect(caret);
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
	long end = line.fEnd;
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


// host: the region covering the characters a hilite selects, in the view's
// coordinates - the union, over the lines the hilite touches, of the box
// from the first selected character's left edge to the last's (or the
// line's right when the selection runs on past it).  ==> whether it is
// non-empty.
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
		long selEnd = end < line.fEnd ? end : line.fEnd;
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


// ROM 0x0016a49c MakeHilite__14TParagraphViewFlT1Uc
// The characters between the offsets selected: clamped to the text, and
// unioned with the existing selection (which is removed first) so a drag
// extends it; an empty range with caretOnEmpty just moves the caret.
// Else a hilite is added (aeAddHilite, a `{start, end}` frame - DEVIATION:
// the ROM makes a C++ TParagraphHilite that carries the selected text and
// its area region) and the key view set to the range, so the caret is off
// (a selection) and DrawHilites paints it.
void
TParagraphView::MakeHilite(long start, long end, Boolean caretOnEmpty)
{
	if (fLines == nil || !fCachesValid)
		CreateAllCaches();
	long length = TextLength();
	if (end > length)
		end = length;
	if (start < 0)
		start = 0;
	if (start > length)
		start = length;
	if (end < start)
		end = start;
	TParagraphHilite* first = HiliteOf(RefVar(FirstHilite()));
	if (first != nil)
	{
		long s0 = first->fStart;
		long e0 = first->fEnd;
		if (s0 < start)
			start = s0;
		if (e0 > end)
			end = e0;
		RemoveAllHilites();
	}
	if (end == start && caretOnEmpty)
	{
		gRootView->SetKeyView(this, start, 0, false);
		return;
	}
	TParagraphHilite* hilite = new TParagraphHilite(start, end);
	if (hilite == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	SetupArea(hilite);
	RefVar cmd(MakeCommand(aeAddHilite, this, 0x8000000));
	CommandSetFrameParameter(cmd, RefVar(AddressToRef(hilite)));
	gApplication->DispatchCommand(cmd);
	gRootView->SetKeyView(this, start, end - start, false);
}


// ROM 0x00179464 ChangeStylesOfRange__14TParagraphViewFlT1RC6RefVarUc
// The characters from start for length given a style: the writeable
// styles array gets the spec over the range (SetStyleOfRange, the equal
// neighbours merged), and the range is laid out again.  DEVIATION: the
// ROM merges the spec into each run (a font, a face toggled, a size) and
// posts it as an undoable command; the reconstruction sets the spec over
// the range directly (no per-run merge, no undo).
void
TParagraphView::ChangeStylesOfRange(long start, long length, RefArg style, Boolean redraw)
{
	if (length <= 0)
		return;
	RefVar spec(style);
	if (ISNIL(spec))
		spec = GetPreference(RSSYMuserfont);
	RefVar styles(GetWriteableTextStylesArray());
	SetStyleOfRange(styles, spec, start, start + length);
	CompactStyleRuns(styles);
	if (redraw)
	{
		ClearAllCaches();
		RangeChanged(start, length, length, RSSYMstyles);
	}
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
static long
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
static long
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


// host: whether the point falls within the selected text (the ROM TView::PointInHilite 0x0026051c iterates the hilites, asking each Encloses)
// Whether the point falls within the selected text: it is tested against
// each hilite's region (built by SelectionRegion).  DEVIATION: the ROM
// asks each C++ TParagraphHilite's Encloses in the view's local
// coordinates; the host builds the region from the frame.
Boolean
TParagraphView::PointInHilite(Point& pt)
{
	HiliteLoop loop(this);
	while (loop.Next())
	{
		TParagraphHilite* hilite = (TParagraphHilite*) loop.fCurrent;
		if (hilite == nil)
			continue;
		SetupArea(hilite);
		if (hilite->Encloses(pt))
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
	for (long offset = info.fStart; offset < info.fEnd; offset++)
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

	// NOT YET: CheckAndDoSplitInk, which a caret over ink splits instead
	InsertStyledText((ULong) offset, chars, (ULong) count, RefVar(NILREF), RefVar(NILREF), 0, 0, !typed);
	// the caret goes where the insertion was, but only when more than one
	// space or any line break went in
	if (typed && (spaces > 1 || breaks > 0))
		gRootView->SetKeyView(this, offset, 0, false);
	if (allocated != nil)
		delete[] allocated;
	return 1;
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
// down joins the paragraph to the one after; the caret with a tail (3) is
// as many spaces as the tail is wide, or as many line breaks as it is
// tall; the open one (5) is line breaks unless the view is one line only;
// the flat one (6), which only a one-line view takes, is a single space
// when both its arms are short.
//
// NOT YET RECONSTRUCTED: CheckAndDoJoin (0x00175964), which joins this
// paragraph to the next, and InsertVerticalSpace (0x001764c4), which
// splits a line - both want the ROM's text objects (the Finder).  The
// gestures that ask for them answer 0.
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
			return 0;			// NOT YET: CheckAndDoJoin(armA, point, armB)
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
		return 0;				// NOT YET: InsertVerticalSpace(point, height)

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
//
// NOT YET RECONSTRUCTED: ScrubWords (0x001740c4) and the ScrubCharacter
// (0x00174808) under it, which need the word boundaries
// (PointToWordBoundary, PointToWord) and the ROM's text objects; a scrub
// over part of a line therefore does nothing yet.
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
			// NOT YET RECONSTRUCTED: the correction info of the range (ExtractRange)
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
	// NOT YET RECONSTRUCTED: OffsetCorrectionInfo, the correctInfo inserted
	(void) correctInfo;
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
		if (!ScrubHilite(bounds))
			return HandleScrub(bounds, -1, unit, true) != 0;
		// the selection went: the scrub's own ink comes off and the hole
		// it left puffs away
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
		// a second tap can be a double tap (word select) instead
		fTapped = true;
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		fTapPoint = unit->Stroke()->FirstPoint();
		gRootView->AddIdler(this, gDoubleTapInterval * 16 + 80, 2);
		CommandSetResult(cmd, 1);
		return true;
	}
	if (id == aeDoubleTap)
	{
		// the pending single tap cancelled; the word under the tap selected
		fTapped = false;
		TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
		Point pt = unit->Stroke()->FirstPoint();
		if (PtInRect(pt, &viewBounds) && SelectWordAt(pt))
			CommandSetResult(cmd, 1);
		else
			HandleTap(pt);		// no word there: just the caret
		return true;
	}
	return TView::RealDoCommand(cmd);
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
	long runs = RunsOfRange(fRunStyles, fRunLengths, fRunCount, line.fStart, line.fEnd - line.fStart, lineStyles, lineLengths, &firstRun);
	TextOptions options = fTextOptions;
	FPoint where;
	where.x = ToFixed(viewBounds.left);
	where.y = ToFixed(line.fBounds.top + line.fAscent);
	if (line.fEnd > line.fStart)
		DoTextOnce(text + line.fStart, line.fEnd - line.fStart, lineStyles, lineLengths, where, &options, nil, true);
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
