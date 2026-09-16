/*
	File:		views/ParagraphView.cpp

	Contains:	TParagraphView: a view of styled text, display only.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "ParagraphView.h"
#include "StyleRuns.h"
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
