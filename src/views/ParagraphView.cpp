/*
	File:		views/ParagraphView.cpp

	Contains:	TParagraphView: a view of styled text, display only.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "ParagraphView.h"
#include "StyleRuns.h"
#include "Unicode.h"
#include "RootView.h"
#include "Application.h"
#include "Commands.h"
#include "Keyboard.h"
#include "Rects.h"
#include "Fonts.h"
#include "RichString.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "Locale.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include <string.h>

const UniChar kCR = 0x0d;
const UniChar kSP = 0x20;
const UniChar kEllipsisChar = 0x2026;		// the ROM's U_CONST_CHAR(0xc9): Mac Roman's ellipsis as Unicode

// ROM 0x0025fdf4 GetInputViewTextFlags__FUlT1
// The text flags of an input view: bits 14-16 (0x1c000) say what kind of
// text it takes; a view without them takes anything (0xc000), one that is
// read-only or takes no scripts (viewFlags 0x82) just the plain kind (0x4000).
static ULong
GetInputViewTextFlags(ULong textFlags, ULong viewFlags)
{
	if ((textFlags & 0x1c000) == 0)
		textFlags |= ((viewFlags & 0x82) == 0) ? 0xc000 : 0x4000;
	return textFlags;
}


// ROM 0x000a41fc TestLineOverlap__FP5TRectT1
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

// ROM 0x001803dc ClassID__14TParagraphViewCFv
long
TParagraphView::ClassID(void) const
{
	return clParagraphView;
}


// ROM 0x001803e4 DerivedFrom__14TParagraphViewCFl
Boolean
TParagraphView::DerivedFrom(long id) const
{
	return id == clParagraphView || TDataView::DerivedFrom(id);
}


// ROM 0x00180df0 Constructor__14TParagraphViewFRC6RefVarP5TView
// The text flags unknown (-1) until SetupDone.
void
TParagraphView::Constructor(RefArg context, TView* parent)
{
	fTextFlags = -1;
	fCaretOffset = 0;
	fSetupDone = false;
	TView::Constructor(context, parent);
}


// ROM 0x00182624 __dt__14TParagraphViewFv
// The caches go (the hilites, style records, text objects and lines);
// NOT YET RECONSTRUCTED: the correction info, and vars.lastTextChanged /
// lastTextHiliteChanged cleared when they name this view.
TParagraphView::~TParagraphView()
{
	DisposeRuns();
	if (fLines != nil)
		DisposPtr((Ptr) fLines);
}


// ROM 0x00181608 SetupDone__14TParagraphViewFv
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


// ROM 0x00180418 SetBounds__14TParagraphViewFRC5TRect
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

// ROM 0x00183034 Text__14TParagraphViewFv
// The text slot as a string.
Ref
TParagraphView::Text(void)
{
	return GetValue(RSSYMtext, RSSYMstring);
}


// ROM 0x00183478 Styles__14TParagraphViewFv
// The styles slot (the runs), made to cover the text.
Ref
TParagraphView::Styles(void)
{
	RefVar styles(GetProto(RSSYMstyles));
	RefVar text(Text());
	CorrectAnyBadStyleRuns(styles, (Length(text) - 2) >> 1);
	return styles;
}


// ROM 0x00183134 GetStyles__14TParagraphViewFv
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


// ROM 0x001834e4 Tabs__14TParagraphViewFv
// The tabs slot (a variable of the context).
Ref
TParagraphView::Tabs(void)
{
	return GetVar(RSSYMtabs);
}


// ROM 0x0017a9ec GetDefaultViewStyle__14TParagraphViewFv
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


// ROM 0x0016b490 GetInterLineSpacing__14TParagraphViewFv
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

// ROM 0x0017e9fc GrowLineInfoCache__FPPP8LineInfol
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


// ROM 0x0016dc00 ClearAllCaches__14TParagraphViewFv
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


// ROM 0x0016e28c RefillAllCaches__14TParagraphViewFv
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


// ROM 0x0016dad8 CreateAllCaches__14TParagraphViewFv
// The caches made: the lines laid out, the hilites' areas set up again
// (NOT YET RECONSTRUCTED: the hilites), the bounds noted.
void
TParagraphView::CreateAllCaches(void)
{
	RefillAllCaches();
	fCachedBounds = viewBounds;
	fCachesValid = true;
}


// ROM 0x0016dc68 FillAllCaches__14TParagraphViewFPs
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
	long width = viewBounds.right - viewBounds.left;
	long height = viewBounds.bottom - viewBounds.top;
	memset(&fTextOptions, 0, sizeof(fTextOptions));
	fTextOptions.fAlignment = ConvertToQDFlush(fViewJustify & vjJustifyMask, &fTextOptions.fJustification);
	fTextOptions.fWidth = (Fixed) width << 16;
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


// ROM 0x0016b94c OffsetCachedBounds__14TParagraphViewFR6TPoint
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


// ROM 0x00181008 SetCaretOffset__14TParagraphViewFPlT1
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


// ROM 0x00181080 GetSelection__14TParagraphViewFv
// A paragraph caret info frame ({offset, length}): the first hilite's
// range, or the caret offset with no length.
Ref
TParagraphView::GetSelection(void)
{
	RefVar info(Clone(RefVar(Rcanonicalparacaretinfo)));
	// NOT YET RECONSTRUCTED: FirstHilite's range (the hilites)
	SetFrameSlot(info, RSSYMoffset, RefVar(MAKEINT(fCaretOffset)));
	SetFrameSlot(info, RSSYMlength, RefVar(MAKEINT(0)));
	return info;
}


// ROM 0x001811a8 SetSelection__14TParagraphViewFRC6RefVarPlT2
// The selection from a caret info frame's offset and length (nil length:
// 0) through SetCaretOffset; a nil frame means no selection - the
// hilites removed (RemoveAllHilites) and the offset and length 0.
void
TParagraphView::SetSelection(RefArg selection, long* offset, long* length)
{
	if (NOTNIL(selection))
	{
		*offset = RINT(GetProtoVariable(selection, RSSYMoffset, nil));
		Ref len = GetProtoVariable(selection, RSSYMlength, nil);
		*length = ISNIL(len) ? 0 : RINT(len);
		SetCaretOffset(offset, length);
		return;
	}
	*offset = 0;
	*length = 0;
	RemoveAllHilites();
}


// ROM 0x00181308 ActivateSelection__14TParagraphViewFUc
// TView's (the viewCaretActivateScript); losing the caret also asks the
// hilite view (GetHiliteView - NOT YET: the ROM goes on to its hilites).
void
TParagraphView::ActivateSelection(Boolean on)
{
	TView::ActivateSelection(on);
	if (!on)
		GetHiliteView();
}


// ROM 0x00176cac FlushWordAtCaret__14TParagraphViewFv
// NOT YET RECONSTRUCTED: the word being typed at the caret handed to the
// recogniser's dictionaries (the auto-add words).
void
TParagraphView::FlushWordAtCaret(void)
{ }


// ROM 0x0017a728 FindLineContainingCharOffset__14TParagraphViewFl
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


// ROM 0x00179f50 OffsetToBounds__14TParagraphViewFlP5TRect
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


// ROM 0x00173b04 OffsetToCaret__14TParagraphViewFlP5TRect
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


// ROM 0x00179550 PointToOffset__14TParagraphViewFRC6TPoint10MarginSizeUcP5TRectPP8LineInfoPlPUc
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


// ROM 0x001736f8 PointToCaret__14TParagraphViewFR6TPointP5TRectT2
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

// ROM 0x0017b1d8 GetStyleAtOffset__14TParagraphViewFlPlT2
Ref
TParagraphView::GetStyleAtOffset(long offset, long* run, long* offsetInRun)
{
	RefVar styles(GetStyles());
	return ::GetStyleAtOffset(styles, offset, run, offsetInRun);
}


// ROM 0x0017b228 GetStylesOfRange__14TParagraphViewFlT1Uc
Ref
TParagraphView::GetStylesOfRange(long offset, long length, Boolean clone)
{
	RefVar styles(GetStyles());
	return ::GetStylesOfRange(styles, offset, length, clone);
}


// ROM 0x0017b278 GetWriteableTextStylesArray__14TParagraphViewFv
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


// ROM 0x0017a778 GetStyleForInsertion__14TParagraphViewFlUcT2
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


// ROM 0x0017af04 AdjustStyles__14TParagraphViewFlN21RC6RefVarT1
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


// ROM 0x0016c854 AdjustHilites__14TParagraphViewFlT1
// NOT YET RECONSTRUCTED: the hilites moved past a replacement.
void
TParagraphView::AdjustHilites(long /*offset*/, long /*delta*/)
{ }


// ROM 0x00182d14 ProcessStyles__14TParagraphViewFUc
// The styles checked for ink words to recognise (CheckStyles; the
// recogniser then runs over the text).  NOT YET RECONSTRUCTED: ink -
// nothing to process.  ==> whether anything was.
Boolean
TParagraphView::ProcessStyles(Boolean /*redraw*/)
{
	return false;
}


// ROM 0x001835e8 FixupBBox__14TParagraphViewFv
// The lines laid out again; a paragraph that calculates its bounds takes
// the text's height (at least a line) - and, one line only, its width
// (at least 5 wide) - as its bounds (SetBounds; the ROM writes the bounds
// and tells the parent ChildBoundsChanged).  NOT YET: the hilites' areas
// remade.
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
			fCachesValid = false;
			SetBounds(bounds);
			fCachesValid = true;
		}
	}
}


// ROM 0x00182c08 RangeChanged__14TParagraphViewFlN21RC6RefVar
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


// ROM 0x00170f30 HandleReplaceText__14TParagraphViewFRC6RefVar
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


// ROM 0x0017ad8c MakeAndDoReplaceCommand__14TParagraphViewFUlPCUsT1RC6RefVarT4N21Uc
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


// ROM 0x0017aa6c InsertStyledText__14TParagraphViewFUlPCUsT1RC6RefVarT4N21Uc
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


// ROM 0x0017abc8 RemoveText__14TParagraphViewFUlT1
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


// ROM 0x00179248 AddKeyToCurrUndo__14TParagraphViewFUsl
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


// ROM 0x0016e688 RealDoCommand__14TParagraphViewFRC6RefVar
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
			// NOT YET RECONSTRUCTED: NextKeyView (the tab chain)
			return true;
		}
		// NOT YET RECONSTRUCTED: FirstHilite - a selection replaced by the key
		if (ch == 0x1c || ch == 0x1d)
		{
			long offset = fCaretOffset;
			if (ch == 0x1c)
				offset--;
			else
				offset++;
			long textLength = TextLength();
			if (offset < 0)
				offset = 0;
			if (offset > textLength)
				offset = textLength;
			gRootView->SetKeyView(this, offset, 0, false);
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
	where.x = (Fixed) viewBounds.left << 16;
	where.y = (Fixed) (line.fBounds.top + line.fAscent) << 16;
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
		at.x = (Fixed) (left + textWidth) << 16;
		at.y = where.y;
		DoTextOnce(&dots, 1, &style, nil, at, &dotOptions, nil, true);
	}
	DisposPtr((Ptr) lineStyles);
	DisposPtr((Ptr) lineLengths);
}


// ROM 0x0016b14c RealDraw__14TParagraphViewFR5TRect
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
}
