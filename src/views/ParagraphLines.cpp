/*
	File:		views/ParagraphLines.cpp

	Contains:	A paragraph's line layout: LineLoop, the caches of its
				lines and text objects and style records, and the text
				object questions the rest of the paragraph asks.  See
				ParagraphLines.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ParagraphLines.h"
#include "ParagraphView.h"
#include "RootView.h"
#include "Fonts.h"
#include "Rects.h"
#include "Screen.h"			// screenHeight
#include "Unicode.h"
#include "InkShapes.h"		// IsInkWord
#include "Ink.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "ROMConstants.h"
#include <string.h>

static const long kDefaultTabWidth = 48;	// FindNextTabStop, SkipLeadingTabs: the stops after the last tab
static const UniChar kTabChar = 0x09;
static const UniChar kReturnChar = 0x0d;
static const UniChar kSpaceChar = 0x20;


/*------------------------------------------------------------------------------
	T h e   t e x t
------------------------------------------------------------------------------*/

// ROM 0x0017cfdc TextRefScanner__FPvlN22PPv
// A paragraph text object's characters: -2 fetches the view's text and
// locks it, -1 unlocks it again; any other offset answers the characters
// from the object's offset in the text on.  ==> count, whatever is asked.
long
TextRefScanner(void* refCon, long offset, long count, long /*charSize*/, void** chars)
{
	TextRef* ref = (TextRef*) refCon;
	if (offset == -2)
	{
		ref->fText = ref->fView->Text();
		LockRef(ref->fText);
	}
	else if (offset == -1)
	{
		UnlockRef(ref->fText);
		ref->fText = NILREF;
	}
	else
		*chars = GetCString(ref->fText) + ref->fOffset + offset;
	return count;
}


// ROM 0x0010d87c InitializeTextOptions__FP11TextOptionsUlsT2
// The options a paragraph's text objects are laid out with: the alignment
// and justification for the text bits of viewJustify, the width, the
// transfer mode, and the characters read through the TextRef.
void
InitializeTextOptions(TextOptions* options, ULong justify, short width, ULong transferMode)
{
	options->fAlignment = ConvertToQDFlush(justify, &options->fJustification);
	options->fWidth = ToFixed(width);
	options->fReserved = 0;
	options->fTransferMode = (long) transferMode;
	options->fFittedWidth = 0;
	options->fScanner = TextRefScanner;
}


/*------------------------------------------------------------------------------
	T h e   c a c h e s
------------------------------------------------------------------------------*/

// The room a cache starts with: a line (or a text object) per 32
// characters, at most a tenth of the screen's height.
static long
InitialCacheSize(long textLength)
{
	long most = screenHeight / 10;
	if (textLength < 0)
		textLength += 0x1f;
	long wanted = (textLength >> 5) + 1;
	return wanted < most ? wanted : most;
}


// ROM 0x0017c7c0 InitializeCache__FPvl
void
InitializeCache(void* cache, long count)
{
	Long* word = (Long*) cache;
	for (long i = 0; i < count; i++)
		*word++ = 0;
	word[0] = 0;
	word[1] = -1;
}


// ROM 0x0017c888 CacheMaxLength__FPv
long
CacheMaxLength(void* cache)
{
	Long* word = (Long*) cache;
	long count = 0;
	while (*word != -1)
	{
		word++;
		count++;
	}
	return count - 1;
}


// ROM 0x0017c8b0 CacheLength__FPv
long
CacheLength(void* cache)
{
	if (cache == nil)
		return 0;
	Long* word = (Long*) cache;
	long count = 0;
	while (*word++ != 0)
		count++;
	return count;
}


// a cache's words, the room and the two ends (the ROM's operator new)
static Long*
NewCache(long count)
{
	Long* cache = (Long*) NewPtr((count + 2) * sizeof(Long));
	if (cache == nil)
		OutOfMemory();
	return cache;
}


// ROM 0x0017c58c InitializeTextObjectCache__FPPllP11TextOptions
// The text objects thrown away and the cache emptied - made afresh when
// it has less room than the text wants.
void
InitializeTextObjectCache(TextObjectRef** cache, long textLength, TextOptions* options)
{
	long count = InitialCacheSize(textLength);
	if (*cache != nil)
	{
		DestroyTextObjectCacheContents(*cache, options);
		if (count <= CacheMaxLength(*cache))
			return;
		DisposPtr((Ptr) *cache);
	}
	*cache = (TextObjectRef*) NewCache(count);
	(*cache)[0] = 0;
	InitializeCache(*cache, count);
}


// ROM 0x0017c64c GrowTextObjectCache__FPPll
void
GrowTextObjectCache(TextObjectRef** cache, long more)
{
	long room = CacheMaxLength(*cache);
	TextObjectRef* grown = (TextObjectRef*) NewPtr((room + more + 2) * sizeof(TextObjectRef));
	if (grown == nil)
		OutOfMemory();
	memmove(grown, *cache, room * sizeof(TextObjectRef));
	InitializeCache(grown + room, more);
	DisposPtr((Ptr) *cache);
	*cache = grown;
}


// ROM 0x0017c6d0 DestroyTextObjectCache__FPPlP11TextOptions
void
DestroyTextObjectCache(TextObjectRef** cache, TextOptions* options)
{
	if (*cache == nil)
		return;
	DestroyTextObjectCacheContents(*cache, options);
	DisposPtr((Ptr) *cache);
	*cache = nil;
}


// ROM 0x0017c704 DestroyTextObjectCacheContents__FPlP11TextOptions
// Each text object from this one on disposed of with what it owns: its
// TextRef, its copy of the run lengths, and options of its own (a text
// object after a tab has a width of its own).
void
DestroyTextObjectCacheContents(TextObjectRef* cache, TextOptions* options)
{
	if (cache == nil || *cache == 0)
		return;
	do
	{
		TextRef* ref;
		GetTextObjField(*cache, kTextObjText, &ref);
		if (ref != nil)
			delete ref;
		const short* lengths;
		GetTextObjField(*cache, kTextObjRunLengths, &lengths);
		if (lengths != nil)
			DisposPtr((Ptr) lengths);
		TextOptions* own;
		GetTextObjField(*cache, kTextObjOptions, &own);
		if (own != options)
			delete own;
		DisposeText(*cache);
		*cache++ = 0;
	} while (*cache != 0);
}


// ROM 0x0017c8e4 InitializeLineInfoCache__FPPP8LineInfol
// The lines thrown away and the cache emptied - made afresh when it has
// less room than the text wants.  (ClearAllCaches does the same inline.)
void
InitializeLineInfoCache(LineInfo*** cache, long textLength)
{
	long count = InitialCacheSize(textLength);
	if (*cache != nil)
	{
		if (count <= CacheMaxLength(*cache))
		{
			for (LineInfo** line = *cache; *line != nil; line++)
			{
				delete *line;
				*line = nil;
			}
			return;
		}
		DestroyLineInfoCache(cache);
	}
	*cache = (LineInfo**) NewCache(count);
	(*cache)[0] = nil;
	InitializeCache(*cache, count);
}


// ROM 0x0017c9cc GrowLineInfoCache__FPPP8LineInfol
void
GrowLineInfoCache(LineInfo*** cache, long more)
{
	long room = CacheMaxLength(*cache);
	LineInfo** grown = (LineInfo**) NewPtr((room + more + 2) * sizeof(LineInfo*));
	if (grown == nil)
		OutOfMemory();
	memmove(grown, *cache, room * sizeof(LineInfo*));
	InitializeCache(grown + room, more);
	DisposPtr((Ptr) *cache);
	*cache = grown;
}


// ROM 0x0017ca50 DestroyLineInfoCache__FPPP8LineInfo
void
DestroyLineInfoCache(LineInfo*** cache)
{
	if (*cache == nil)
		return;
	for (LineInfo** line = *cache; *line != nil; line++)
		delete *line;
	DisposPtr((Ptr) *cache);
	*cache = nil;
}


/*------------------------------------------------------------------------------
	T h e   s t y l e   r u n s
------------------------------------------------------------------------------*/

// ROM 0x0017caa0 UpdateStyleRunLengths__FPPsPPP11StyleRecordT2UlUc
// The runs walked until they cover `count` characters; the run that does
// is cut by what was taken (and passed over when that empties it).
//
// DEVIATION: when the runs end before they cover the count the ROM goes
// on with whatever its caller left in two registers - the run it would
// cut and how much to cut it by.  The host lengthens the last run to
// cover the count instead, which is what a paragraph's styles always do
// (Styles() corrects them to the text's length).
short*
UpdateStyleRunLengths(short** runLengths, StyleRecord*** styles, StyleRecord*** lastStyle, ULong count, Boolean copy)
{
	short* run = *runLengths;
	if (run == nil)
	{
		*lastStyle = *styles;
		return nil;
	}
	short* lengths = nil;
	ULong covered = 0;
	long runs = 0;
	StyleRecord** style = *styles;
	short taken;
	for ( ; ; )
	{
		if (*style == nil)
		{
			// (host: see above)
			if (runs == 0)
			{
				*lastStyle = style;
				return nil;
			}
			run--;
			style--;
			*run = (short) (*run + (count - covered));
			covered = count;
			taken = (short) (*run - (covered - count));
			*lastStyle = style;
			break;
		}
		covered += *run;
		runs++;
		if (count <= covered)
		{
			taken = (short) (*run - (covered - count));
			*lastStyle = style;
			break;
		}
		run++;
		style++;
	}
	if (copy)
	{
		lengths = (short*) NewPtr(runs * sizeof(short));
		if (lengths == nil)
			OutOfMemory();
		short* from = *runLengths;
		for (long i = 0; i < runs - 1; i++)
			lengths[i] = *from++;
		lengths[runs - 1] = taken;
	}
	*run = (short) (*run - taken);
	long passed = runs - 1;
	if (*run == 0)
	{
		run++;
		passed = runs;
	}
	*runLengths = run;
	*styles = *styles + passed;
	return lengths;
}


// ROM 0x0017cc40 FindNextTabStop__FRC6RefVarlT2Pl
long
FindNextTabStop(RefArg tabs, long left, long x, long* index)
{
	long count = 0;
	long from = *index;
	long stop = left;
	if (NOTNIL(tabs))
	{
		count = Length(tabs);
		for (long i = from; i < count; i++)
		{
			stop = RINT(GetArraySlotRef(tabs, i)) + left;
			if (x < stop)
			{
				*index = i;
				return stop;
			}
		}
	}
	if (count <= from && 0 < count)
		stop = RINT(GetArraySlotRef(tabs, count - 1)) + left;
	long base = count < from ? from : count;
	stop += (base - count) * kDefaultTabWidth;
	long more = 0;
	while ((stop += kDefaultTabWidth) <= x)
		more++;
	*index = base + more;
	return stop;
}


// ROM 0x0017cd50 NextTabOrCRCharOffset__FPUsPUc
long
NextTabOrCRCharOffset(const UniChar* text, Boolean* isCR)
{
	long offset = 0;
	for ( ; ; )
	{
		if (*text == 0)
			return -1;
		if (*text == kTabChar)
		{
			*isCR = false;
			break;
		}
		if (*text == kReturnChar)
		{
			*isCR = true;
			break;
		}
		offset++;
		text++;
	}
	return offset;
}


// ROM 0x0017cf1c GetMaxAscent__FPP11StyleRecordT1PlN23
long
GetMaxAscent(StyleRecord** first, StyleRecord** last, long* descent, long* lineAscent, long* lineDescent)
{
	long maxAscent = 0, maxDescent = 0, maxLineAscent = 0, maxLineDescent = 0;
	for ( ; first <= last; first++)
	{
		long a, d, la, ld;
		GetParagraphStyleRecordMetrics(*first, &a, &d, &la, &ld);
		if (a < maxAscent)
			a = maxAscent;
		if (d < maxDescent)
			d = maxDescent;
		if (la < maxLineAscent)
			la = maxLineAscent;
		if (ld < maxLineDescent)
			ld = maxLineDescent;
		maxAscent = a;
		maxDescent = d;
		maxLineAscent = la;
		maxLineDescent = ld;
	}
	*descent = maxDescent;
	*lineAscent = maxLineAscent;
	*lineDescent = maxLineDescent;
	return maxAscent;
}


/*------------------------------------------------------------------------------
	T h e   t e x t   o b j e c t s
------------------------------------------------------------------------------*/

// ROM 0x0017d390 LeftEdgeOfEmptyLine__FRC5TRectl
short
LeftEdgeOfEmptyLine(const Rect& bounds, ULong justify)
{
	ULong h = justify & 3;
	if (h == 1)
		return bounds.right;
	if (h != 2)
		return bounds.left;
	return (short) (bounds.left + ((short) ((unsigned short) bounds.right - (unsigned short) bounds.left) >> 1));
}


// ROM 0x0017d0a8 TPoint2FPoint__FR6TPointP6FPoint
void
TPoint2FPoint(const Point& pt, FPoint* fpt)
{
	fpt->x = ToFixed(pt.h);
	fpt->y = ToFixed(pt.v);
}


// ROM 0x0017d0cc FPoint2TPoint__FR6FPointP6TPoint
void
FPoint2TPoint(const FPoint& fpt, Point* pt)
{
	pt->h = (short) ((fpt.x + 0x8000) >> 16);
	pt->v = (short) ((fpt.y + 0x8000) >> 16);
}


// ROM 0x0017d100 FRect2TRect__FR5FRectP5TRect
void
FRect2TRect(const FRect& frect, Rect* rect)
{
	rect->left = (short) ((frect.left + 0x8000) >> 16);
	rect->top = (short) ((frect.top + 0x8000) >> 16);
	rect->right = (short) ((frect.right + 0x8000) >> 16);
	rect->bottom = (short) ((frect.bottom + 0x8000) >> 16);
}


// ROM 0x0017ce40 GetTextObjBounds__FlP5TRect
void
GetTextObjBounds(TextObjectRef text, Rect* bounds)
{
	TextBoundsInfo info;
	GetTextObjField(text, kTextObjBounds, &info);
	FRect frect;
	frect.left = info.fLeft;
	frect.top = info.fTop;
	frect.right = info.fRight;
	frect.bottom = info.fBottom;
	FRect2TRect(frect, bounds);
	bounds->bottom = (short) (bounds->bottom + ((info.fLeading + 0x8000) >> 16));
}


// ROM 0x0017ce90 GetTextObjBaseline__FlP6TPoint
void
GetTextObjBaseline(TextObjectRef text, Point* baseline)
{
	FPoint location;
	GetTextObjField(text, kTextObjLocation, &location);
	FPoint2TPoint(location, baseline);
}


// ROM 0x0017cec0 SetTextObjBaseline__FlR6TPoint
void
SetTextObjBaseline(TextObjectRef text, Point& baseline)
{
	FPoint location;
	TPoint2FPoint(baseline, &location);
	SetTextObjField(text, kTextObjLocation, &location);
}


// ROM 0x0017cef4 GetTextObjFlush__Fl
Fixed
GetTextObjFlush(TextObjectRef text)
{
	TextOptions* options;
	GetTextObjField(text, kTextObjOptions, &options);
	return options->fAlignment;
}


// ROM 0x0017d5c4 CharLeftEdge__FlT1P6TPoint
void
CharLeftEdge(TextObjectRef text, long offset, Point* pt)
{
	FPoint where;
	CharToPoint(text, offset, &where);
	pt->h = (short) ((where.x + 0x8000) >> 16);
	pt->v = (short) ((where.y + 0x8000) >> 16);
}


// ROM 0x0017d614 CoordToInterCharGap__Fls
long
CoordToInterCharGap(TextObjectRef text, short h)
{
	Point pt;
	pt.v = 0;
	pt.h = h;
	FPoint where;
	TPoint2FPoint(pt, &where);
	return PointToChar(text, where);
}


// ROM 0x0017d66c CoordToChar__Fls
// The boundary nearest h, or the one before it when h is left of it.
long
CoordToChar(TextObjectRef text, short h)
{
	long gap = CoordToInterCharGap(text, h);
	Point edge;
	CharLeftEdge(text, gap, &edge);
	if (h < edge.h && gap > 0)
		gap--;
	return gap;
}


/*------------------------------------------------------------------------------
	L i n e L o o p
------------------------------------------------------------------------------*/

// ROM 0x0010d8d4 __ct__8LineLoopFP14TParagraphViewPPlPP11StyleRecordPs
// The loop set at the text's start and the view's top-left.  The first
// baseline is the first style's ascent below the top - or, with a
// viewLineSpacing, three pixels (four for a spacing over 20) above the
// first ruled line, whatever the font, which is what puts an input
// line's text on its line; or where the view's +0x7c says (a baseline
// a previous layout found, AddNextLine).  The lines run to the view's
// right edge - except for a view that sizes itself to its text (text
// flag 4), which runs to its parent's: a word typed on a page grows to
// the right rather than wrapping a character to a line.  Text with tabs
// in it is not right-aligned or centred: the tab stops are from the left.
LineLoop::LineLoop(TParagraphView* view, TextObjectRef** textObjects, StyleRecord** styles, short* runLengths)
{
	fTabs = NILREF;
	fView = view;
	fTextObjects = textObjects;
	fStyles = styles;
	fRunLengths = runLengths;
	fText = view->Text();
	LockRef(fText);
	fTextStart = GetCString(fText);
	long length = Ustrlen(fTextStart);
	fCurrent = fTextStart;
	fObjIndex = -1;
	fTextEnd = fTextStart + length;
	fStyle = fStyles;
	TView* parent = view->fParent;
	fLeft = view->viewBounds.left;
	const Rect* edges = (view->fTextFlags & 4) != 0 ? &parent->viewBounds : &view->viewBounds;
	fRight = edges->right;
	FontInfo info;
	GetStyleFontInfo(*styles, &info);
	fLineTop = view->viewBounds.top;
	fPenX = view->viewBounds.left;
	fNextTop = fLineTop;
	fBaseline = fLineTop;
	fBaselineX = fPenX;
	long spacing = view->fLineSpacing;
	short baseline;
	if (spacing < 1)
	{
		if (view->fFirstBaselineOffset == 0)
			baseline = (short) (fBaseline + info.ascent);
		else
			baseline = (short) (fBaseline + view->fFirstBaselineOffset);
	}
	else
		baseline = (short) (spacing - (spacing < 0x15 ? 3 : 4) + fBaseline);
	fBaseline = baseline;
	fInterLineSpacing = view->GetInterLineSpacing();
	fRequestedSpacing = view->GetRequestedLineSpacing();
	fNoMoreTabs = false;
	fTabCount = 0;
	fTabs = view->Tabs();
	if (NOTNIL(fTabs))
		fTabCount = Length(fTabs);
	ULong justify = (ULong) view->fViewJustify;
	ULong flush = justify & 0x3fffffff;
	if ((justify & 3) != 0 && Ustrchr(fTextStart, kTabChar) != nil)
		flush = justify & 0x3ffffffc;
	InitializeTextOptions(&view->fTextOptions, flush, (short) (fRight - fLeft), (ULong) view->fTransferMode);
	fOptions = &view->fTextOptions;
	fLineHeight = 0;
}


// ROM 0x0010db5c __dt__8LineLoopFv
LineLoop::~LineLoop()
{
	UnlockRef(fText);
}


// ROM 0x0010db98 AddNextLine__8LineLoopFPlN41P5TRect
Boolean
LineLoop::AddNextLine(long* count, long* ascent, long* descent, long* lineAscent, long* lineDescent, Rect* bounds)
{
	long first = fObjIndex + 1;
	Boolean firstLine = fCurrent == fTextStart;
	StyleRecord** lineStyle = fStyle;
	Boolean done;
	StyleRecord** lastStyle = fStyle;		// (the ROM's is set by the first run)
	if (!AddNextTextRun(&done, &lastStyle))
		return false;
	while (!done)
		if (!AddNextTextRun(&done, &lastStyle))
			return false;
	long objects = fObjIndex - first + 1;
	long maxDescent, maxLineAscent, maxLineDescent;
	long maxAscent = GetMaxAscent(lineStyle, lastStyle, &maxDescent, &maxLineAscent, &maxLineDescent);
	long wasBaseline = fBaseline;
	Rect r;
	ComputeLineBounds(first, objects, maxAscent, maxDescent, maxLineAscent, maxLineDescent, &r);
	long baseline = fBaseline;
	long shift = baseline - wasBaseline;
	fNextTop = baseline + maxLineDescent;
	if (firstLine)
		fView->fFirstBaselineOffset = (short) (baseline - (unsigned short) fView->viewBounds.top);
	fPenX = fLeft;
	fLineTop = r.bottom;
	long lineHeight;
	if (fRequestedSpacing > 0)
		lineHeight = (short) fRequestedSpacing;
	else
	{
		long a, d, la, ld;
		GetParagraphStyleRecordMetrics(*lastStyle, &a, &d, &la, &ld);
		lineHeight = (short) (la + ld);
	}
	fLineHeight = lineHeight;
	fBaselineX = fPenX;
	fBaseline = (short) (baseline + lineHeight);
	*count = objects;
	*descent = r.bottom - baseline;
	*ascent = (short) (lineHeight + shift) - *descent;
	*lineAscent = maxLineAscent;
	*lineDescent = maxLineDescent;
	*bounds = r;
	return true;
}


// ROM 0x0010ddc0 ComputeLineBounds__8LineLoopFlN51P5TRect
// The line's box and its baseline.  The box runs from the text's left (the
// pen's left for left-aligned text; the first object's for any other,
// which its alignment has placed; the middle or the right edge of an
// empty line) to where the pen got to, never past the right edge.  Its
// top is the next line's top and its height the line's tallest ascent and
// descent (its fonts' or what their line metrics ask for).
//
// The baseline stays where the loop has it when the line's spacing is the
// one the last line set and this is not the first line.  Otherwise it
// moves - and every text object of the line with it: a one-line view
// centres or bottoms the line in itself (vjOneLineOnly with vjCenterV or
// vjBottomV); the first line puts its baseline its fonts' greatest ascent
// below the top; any other line puts its baseline its line ascent below
// the last line's descent.  A first line set by a viewLineSpacing or by
// a baseline remembered at +0x7c instead takes its top from the baseline,
// and a view that calculates its bounds and has no spacing is moved to
// put its top there.
void
LineLoop::ComputeLineBounds(long firstObj, long count, long maxAscent, long maxDescent, long lineAscent, long lineDescent, Rect* bounds)
{
	long ascent = maxAscent <= lineAscent ? lineAscent : maxAscent;
	long descent = maxDescent <= lineDescent ? lineDescent : maxDescent;
	long height = ascent + descent;
	long pseudo = GetPseudoSpacing(lineAscent, lineDescent);
	Rect r;
	Fixed alignment = fOptions->fAlignment;
	if (alignment == 0)
	{
		r.left = fLeft;
		r.right = fPenX;
	}
	else if (count == 0)
	{
		const Rect& view = fView->viewBounds;
		short x;
		if (alignment == 0x10000)
			x = (short) ((unsigned short) view.right - 1);
		else
			x = (short) (view.left + ((short) ((unsigned short) view.right - (unsigned short) view.left) >> 1));
		r.left = x;
		r.right = x;
	}
	else
	{
		Rect first;
		GetTextObjBounds((*fTextObjects)[firstObj], &first);
		r.left = first.left;
		r.right = fPenX;
	}
	if (r.right == r.left)
		r.right = (short) (r.right + 1);
	if (r.right >= fRight)
		r.right = fRight;
	r.top = fLineTop;
	ULong justify = (ULong) fView->fViewJustify & 0x3fffffff;
	long spacing = fView->fLineSpacing;
	Boolean firstLine = fView->viewBounds.top == r.top;
	short ascentTop = (short) (fBaseline - (short) ascent);
	if (firstLine && (spacing > 0 || fView->fFirstBaselineOffset != 0))
	{
		r.top = ascentTop;
		r.bottom = (short) (r.top + height);
		if (fView->viewBounds.top != r.top && (fView->fFlags & vCalculateBounds) != 0 && spacing == 0)
		{
			Rect moved = fView->viewBounds;
			moved.top = r.top;
			Point origin = fView->fParent->ContentsOrigin();
			OffsetRect(&moved, (short) -origin.h, (short) -origin.v);
			fView->fCachesValid = false;
			if ((fView->fFlags & 0x82) == 0)
				fView->SetDataSlot(RSSYMviewbounds, RefVar(ToObject(moved)));
			fView->SetBounds(moved);
			fView->fCachesValid = true;
		}
		*bounds = r;
		return;
	}
	if (fLineHeight == pseudo && !firstLine)
	{
		if (r.top < ascentTop)
			ascentTop = r.top;
		r.top = ascentTop;
		short spaced = (short) (fLineTop + fLineHeight);
		short below = (short) (fBaseline + descent);
		r.bottom = spaced <= below ? below : spaced;
		*bounds = r;
		return;
	}
	short shift = 0;
	if ((justify & vjOneLineOnly) != 0 && (justify & 0xc) != 0)
	{
		ULong v = justify & 0xc;
		short viewHeight = (short) ((unsigned short) fView->viewBounds.bottom - (unsigned short) fView->viewBounds.top);
		long room;
		if (v == 4)
			room = (viewHeight - height) / 2;
		else if (v == 8)
			room = viewHeight - height;
		else
			goto moved;
		shift = (short) room;
		if (shift != 0)
			r.top = (short) (fLineTop + shift);
	}
	else if (firstLine)
		shift = (short) (fLineTop - (short) (fBaseline - maxAscent));
	else
	{
		shift = (short) (fNextTop + lineAscent - fBaseline);
		r.top = (short) (fBaseline + shift - (short) ascent);
	}
moved:
	if (shift != 0)
	{
		TextObjectRef* obj = *fTextObjects + firstObj;
		for (long i = 0; i < count; i++, obj++)
		{
			Point baseline;
			GetTextObjBaseline(*obj, &baseline);
			baseline.v = (short) (baseline.v + shift);
			SetTextObjBaseline(*obj, baseline);
		}
		fBaseline = (short) (fBaseline + shift);
	}
	r.bottom = (short) (r.top + height);
	if (!firstLine && r.top >= fLineTop)
		r.top = fLineTop;
	*bounds = r;
}


// ROM 0x0010e394 AddNextTextRun__8LineLoopFPUcPPP11StyleRecord
// The next text object of the line.  Leading tabs move the pen to the
// next stop and end the line when that is past the right edge; a return
// ends the line.  Otherwise the text up to the next tab or return goes
// into a text object at the pen, as wide as is left of the line, and is
// cut to what fits and back to a word break (a line in the view's last
// line's room cuts two characters short instead, leaving space for the
// ellipsis RealDraw adds); the first object of a line that fits nothing
// at all is widened three pixels at a time until it fits a character.
// Text that is not left-aligned leaves a space it ends with out of its
// object, so the space does not count in the alignment.  The spaces and
// the return after the object are passed over, and the pen moves to its
// right.
Boolean
LineLoop::AddNextTextRun(Boolean* done, StyleRecord*** lastStyle)
{
	if (fCurrent >= fTextEnd)
		return false;
	Fixed width = fOptions->fWidth;
	long fits = fView->LineFitsInBounds(fLineTop, fBaseline, fInterLineSpacing, *fStyle);
	if (fits == 2)
		return false;
	*done = false;
	Boolean noTabs = true;
	const UniChar* runStart = fCurrent;
	long tabX = SkipLeadingTabs();
	Boolean atCR = fCurrent < fTextEnd && *fCurrent == kReturnChar;
	if (tabX > 0 || atCR)
	{
		if (tabX > 0)
		{
			fPenX = (short) tabX;
			fBaselineX = (short) tabX;
			width = ToFixed((short) ((unsigned short) fRight - tabX));
			noTabs = false;
		}
		if (atCR)
			fCurrent++;
		UpdateStyleRunLengths(&fRunLengths, &fStyle, lastStyle, fCurrent - runStart, false);
		if (fPenX >= fRight || atCR || fCurrent >= fTextEnd)
		{
			*done = true;
			return true;
		}
	}
	long maxLength = Ustrlen(fCurrent);
	long length = maxLength;
	Boolean endsInCR = false;
	if (!fNoMoreTabs)
	{
		long next = NextTabOrCRCharOffset(fCurrent, &endsInCR);
		if (next >= 0)
			length = next;
		else
			fNoMoreTabs = true;
	}
	Point pen;
	pen.v = fBaseline;
	pen.h = fBaselineX;
	FPoint location;
	TPoint2FPoint(pen, &location);
	TextRef* ref = new TextRef;
	if (ref == nil)
		OutOfMemory();
	ref->fView = fView;
	ref->fOffset = fCurrent - fTextStart;
	TextOptions* options = fOptions;
	if (options->fWidth != width)
	{
		options = new TextOptions;
		if (options == nil)
		{
			delete ref;
			OutOfMemory();
		}
		*options = *fOptions;
		options->fWidth = width;
	}
	TextObjectRef obj = NewText(ref, length, fStyle, fRunLengths, location, options);
	if (obj == 0)
	{
		delete ref;
		if (options != fOptions)
			delete options;
		OutOfMemory();
	}
	long fitted;
	GetTextObjField(obj, kTextObjFittedLength, &fitted);
	if (fitted == 0)
	{
		if (!noTabs)
		{
			delete ref;
			if (options != fOptions)
				delete options;
			DisposeText(obj);
			*done = true;
			return true;
		}
		// ROM QUIRK: options may be the view's own (+0x84), whose width is
		// then left three pixels wider for the rest of the layout
		do
		{
			options->fWidth = ToFixed(((options->fWidth + 0x8000) >> 16) + 3);
			SetTextObjField(obj, kTextObjOptions, options);
			SetTextObjField(obj, kTextObjFittedLength, &length);
			GetTextObjField(obj, kTextObjFittedLength, &fitted);
		} while (fitted == 0);
	}
	if (fitted < length || endsInCR || fitted == maxLength)
		*done = true;
	long n = fitted;
	if (fitted < length && fCurrent[fitted - 1] != kSpaceChar && fCurrent[fitted] != kSpaceChar)
	{
		if (fits == 1)
		{
			n = fitted - 2;
			if (n < 1)
				n = 1;
		}
		else
		{
			ULong wordStart, wordEnd;
			FindWordBreaks(fCurrent, Ustrlen(fCurrent), (ULong) fitted, true, fView->fLineBreakTable, &wordStart, &wordEnd);
			n = (long) wordStart;
			if (wordStart == 0)
			{
				if (!noTabs)
				{
					delete ref;
					if (options != fOptions)
						delete options;
					DisposeText(obj);
					*done = true;
					return true;
				}
				n = fitted;
			}
		}
	}
	Boolean dropSpace = fOptions->fAlignment != 0 && fCurrent[n - 1] == kSpaceChar && n > 1;
	if (dropSpace)
		n--;
	if (n != fitted)
		SetTextObjField(obj, kTextObjFittedLength, &n);
	if (dropSpace)
		n++;
	fCurrent += n;
	if (fRunLengths == nil)
		*lastStyle = fStyle;
	else
	{
		StyleRecord** before = fStyle;
		short* lengths = UpdateStyleRunLengths(&fRunLengths, &fStyle, lastStyle, (ULong) n, true);
		if (dropSpace)
			lengths[*lastStyle - before] -= 1;
		SetTextObjField(obj, kTextObjRunLengths, lengths);
	}
	const UniChar* next = SkipUpToTwoSpacesAndCR(fCurrent, fTextEnd);
	if (fCurrent < next)
	{
		long skipped = next - fCurrent;
		fCurrent = next;
		// (the ROM guards this with a handler that disposes of the object;
		// without a copy nothing in it can throw)
		UpdateStyleRunLengths(&fRunLengths, &fStyle, lastStyle, (ULong) skipped, false);
	}
	fObjIndex++;
	if (CacheMaxLength(*fTextObjects) < fObjIndex + 1)
	{
		newton_try
		{
			GrowTextObjectCache(fTextObjects, 5);
		}
		cleanup
		{
			delete ref;
			if (options != fOptions)
				delete options;
			DisposeText(obj);
		}
		end_try;
	}
	(*fTextObjects)[fObjIndex] = obj;
	Rect objBounds;
	GetTextObjBounds(obj, &objBounds);
	fPenX = objBounds.right;
	fBaselineX = fPenX;
	return true;
}


// ROM 0x0010eb50 SkipLeadingTabs__8LineLoopFv
// The tabs at the current position passed over, the pen moved from stop
// to stop - the first found from the pen, then the tabs slot's in turn,
// then every 48 pixels after the last - stopping at the first past the
// right edge.  ==> where the pen goes, 0 when there were no tabs.
long
LineLoop::SkipLeadingTabs(void)
{
	const UniChar* start = fCurrent;
	long index = 0;
	long last = fLeft;
	Boolean first = true;
	long x = 0;		// (the ROM's register is unset until the first tab)
	if (fTabCount > 0)
		last = RINT(GetArraySlotRef(fTabs, fTabCount - 1)) + fLeft;
	while (*fCurrent == kTabChar)
	{
		fCurrent++;
		index++;
		if (first)
		{
			index = 0;
			x = FindNextTabStop(fTabs, fLeft, fPenX, &index);
			first = false;
		}
		else if (index < fTabCount)
			x = RINT(GetArraySlotRef(fTabs, index)) + fLeft;
		else
			x = last + (index - fTabCount) * kDefaultTabWidth + kDefaultTabWidth;
		if (fRight <= x)
			break;
	}
	return fCurrent == start ? 0 : (short) x;
}


// ROM 0x0010ec90 CurrentLineFitsInBounds__8LineLoopFv
// LineFitsInBounds for the line the loop is at, in its run's style (the
// last run's, at the text's end).
long
LineLoop::CurrentLineFitsInBounds(void)
{
	StyleRecord** style = fStyle;
	if (*style == nil)
		style--;
	return fView->LineFitsInBounds(fLineTop, fBaseline, fInterLineSpacing, *style);
}


// ROM 0x0010ecd4 GetPseudoSpacing__8LineLoopFlT1
// The spacing a line of these metrics takes: the view's viewLineSpacing
// when it has one, else the requested spacing when the line is at least
// three pixels shorter than it and no shorter than four fifths of it,
// else the line's own height.  (GetInterLineSpacing's test is the other
// way round - the font no more than three taller than the spacing - but
// this is the ROM's.)
long
LineLoop::GetPseudoSpacing(long ascent, long descent)
{
	long spacing = fView->fLineSpacing;
	if (spacing != 0)
		return spacing;
	long height = ascent + descent;
	long requested = fRequestedSpacing;
	if (requested != 0 && height + 3 <= requested && (requested * 8) / 10 <= height)
		return requested;
	return height;
}


/*------------------------------------------------------------------------------
	T h e   p a r a g r a p h ' s   c a c h e s
------------------------------------------------------------------------------*/

// ROM 0x001721fc LineFitsInBounds__14TParagraphViewFlN21P11StyleRecord
// Whether a line at lineTop (baseline at `baseline`) in the style fits the
// view: 0 it does (and always, for a view that calculates its bounds), 1
// it does but the next would not - its middle is past the bottom by the
// spacing - 2 it does not (its own middle is past the bottom; never for
// the view's first line).
long
TParagraphView::LineFitsInBounds(long lineTop, long baseline, long spacing, StyleRecord* style)
{
	if (fFlags & vCalculateBounds)
		return 0;
	FontInfo info;
	GetStyleFontInfo(style, &info);
	long height = info.descent + info.ascent;
	long bottom;
	if (spacing < 1)
	{
		bottom = lineTop + height;
		spacing = info.leading + height;
	}
	else
		bottom = info.descent + baseline;
	if (viewBounds.bottom < spacing + bottom - (height >> 1))
	{
		if (viewBounds.bottom < bottom - (height >> 1) && lineTop != viewBounds.top)
			return 2;
		return 1;
	}
	return 0;
}


// ROM 0x0016c2f0 CreateStyleRecordCache__14TParagraphViewFPPs
// The style records the text is laid out in.  A single style is the
// record at +0xa8, the cache the two pointers at +0xc8 (it and nil) and
// no lengths; runs are an array of records, a nil-ended array of
// pointers to them and the runs' lengths, which *runLengths takes.  A
// space on its own after an ink word written small (12 point or less) is
// made no bigger than 12 point, so it does not stretch the line.
void
TParagraphView::CreateStyleRecordCache(short** runLengths)
{
	RefVar styles(GetStyles());
	ULong textFlags = (ULong) fTextFlags;			// (vtable +0x20)
	RefVar deflt(GetDefaultViewStyle());
	if (!IsArray(styles))
	{
		CreateParagraphStyleRecord(styles, &fSingleStyle, textFlags, deflt);
		fSingleStyles[1] = nil;
		fSingleStyles[0] = &fSingleStyle;
		*runLengths = nil;
		fStyleCache = fSingleStyles;
		return;
	}
	long runs = Length(styles) / 2;
	StyleRecord* records = new StyleRecord[runs > 0 ? runs : 1];
	if (records == nil)
		OutOfMemory();
	StyleRecord** pointers = (StyleRecord**) NewPtr((runs + 1) * sizeof(StyleRecord*));
	if (pointers == nil)
	{
		delete[] records;
		OutOfMemory();
	}
	short* lengths = (short*) NewPtr((runs > 0 ? runs : 1) * sizeof(short));
	if (lengths == nil)
	{
		delete[] records;
		DisposPtr((Ptr) pointers);
		OutOfMemory();
	}
	RefVar previous;
	RefVar spec;
	long offset = 0;
	Fixed previousSize = 0;
	RefVar textRef(Text());
	LockRef(textRef);
	const UniChar* text = GetCString(textRef);
	StyleRecord* record = records;
	StyleRecord** pointer = pointers;
	for (long i = 0; i < runs; i++)
	{
		*pointer++ = record;
		previous = spec;
		spec = GetArraySlotRef(styles, i * 2 + 1);
		short length = (short) RINT(GetArraySlotRef(styles, i * 2));
		CreateParagraphStyleRecord(spec, record, textFlags, deflt);
		if (length == 1 && record->fFontSize > 0xc0000 && IsWhiteSpace(text[offset])
		 && IsInkWord(previous) && previousSize < 0xc0001)
			record->fFontSize = 0xc0000;
		previousSize = record->fFontSize;
		lengths[i] = length;
		offset += length;
		record++;
	}
	*pointer = nil;
	UnlockRef(textRef);
	*runLengths = lengths;
	fStyleCache = pointers;
}


// ROM 0x0016c5ec DestroyStyleRecordCache__14TParagraphViewFv
// The records of the runs disposed of; the single style's cache is the
// view's own and stays (CreateStyleRecordCache makes it again).
void
TParagraphView::DestroyStyleRecordCache(void)
{
	if (fStyleCache == nil || fStyleCache == fSingleStyles)
		return;
	if (fStyleCache[0] != nil)
	{
		StyleRecord* records = fStyleCache[0];
		for (StyleRecord** p = fStyleCache; *p != nil; p++)
			DisposeStyleRecord(*p);
		delete[] records;
	}
	DisposPtr((Ptr) fStyleCache);
	fStyleCache = nil;
}


// ROM 0x0016bbd0 ClearAllCaches__14TParagraphViewFv
// The style records, the text objects and the lines thrown away; the text
// object and line caches emptied, or made with room for a line per 32
// characters (at most a tenth of the screen's height).
void
TParagraphView::ClearAllCaches(void)
{
	if (fStyleCache != nil)
		DestroyStyleRecordCache();
	long length = (Length(RefVar(Text())) - 2) >> 1;
	InitializeTextObjectCache(&fTextObjects, length, &fTextOptions);
	long count = InitialCacheSize(length);
	if (fLineCache != nil)
	{
		if (count <= CacheMaxLength(fLineCache))
		{
			for (LineInfo** line = fLineCache; *line != nil; line++)
			{
				delete *line;
				*line = nil;
			}
			return;
		}
		DestroyLineInfoCache(&fLineCache);
	}
	fLineCache = (LineInfo**) NewCache(count);
	fLineCache[0] = nil;
	InitializeCache(fLineCache, count);
}


// ROM 0x0016c25c RefillAllCaches__14TParagraphViewFv
// Only once the view is set up (SetupDone sets +0x78): the caches
// cleared, the style records made and the lines laid out.
void
TParagraphView::RefillAllCaches(void)
{
	if (!fCachesValid)
		return;
	ClearAllCaches();
	short* runLengths = nil;
	CreateStyleRecordCache(&runLengths);
	newton_try
	{
		FillAllCaches(runLengths);
	}
	cleanup
	{
		if (runLengths != nil)
			DisposPtr((Ptr) runLengths);
	}
	end_try;
	if (runLengths != nil)
		DisposPtr((Ptr) runLengths);
}


// ROM 0x0016baa8 CreateAllCaches__14TParagraphViewFv
// The caches made; a view that calculates its bounds made as tall as its
// text (as wide too when it sizes itself to its text, text flag 4) -
// which is how a paragraph made with no height (MakeTextNote's) comes to
// show - and the hilites' areas made again.
void
TParagraphView::CreateAllCaches(void)
{
	RefillAllCaches();
	if (fFlags & vCalculateBounds)
	{
		Rect bounds;
		bounds.top = viewBounds.top;
		bounds.left = viewBounds.left;
		bounds.right = viewBounds.right;
		bounds.bottom = fTextBounds.bottom;
		Point origin = fParent->ContentsOrigin();
		OffsetRect(&bounds, (short) -origin.h, (short) -origin.v);
		fCachesValid = false;
		if (fTextFlags & 4)
			bounds.right = (short) (fTextBounds.right - origin.h);
		WriteBounds(bounds);
		fCachesValid = true;
	}
	UpdateHiliteArea();
}


// ROM 0x0016bc38 FillAllCaches__14TParagraphViewFPs
// The text laid out a line at a time (LineLoop), and the lines that show
// kept: those in what the parents show of the view - the walk up stopping
// at a print or a remote view, what is below one being drawn elsewhere -
// or, with text flag 0x800, in the view's own bounds and ending within
// them (how a paragraph reflowed for the printer is cut between two lines
// where the page runs out).  A line that does not show has its text
// objects disposed of; one above what shows is skipped, one below it ends
// the layout unless the view calculates its bounds (whose text bounds
// take every line).  The first line's baseline and line ascent, the last
// line's baseline and line descent, the last line's height (+0x3c) and
// the text bounds (+0x40: the lines' union - the view's top-left when
// there are none - as tall as the view unless it calculates its bounds,
// a line taller for a final return) are kept; a heavy face widens the
// text bounds ten pixels, as far as the parent's right.
void
TParagraphView::FillAllCaches(short* runLengths)
{
	long textLength = (Length(RefVar(Text())) - 2) >> 1;
	long lines = 0;
	long objectsKept = 0;
	long room = CacheMaxLength(fLineCache);
	long lineStart = 0;
	long objIndex = 0;
	Rect text;
	text.top = -32768;
	text.bottom = -32768;
	text.left = 0;
	text.right = 0;
	Rect clip = viewBounds;
	Boolean ownBounds = (fTextFlags & 0x800) != 0;		// (the ROM's accessor at vtable +0x20: fTextFlags)
	if (!ownBounds)
	{
		TView* ancestor = fParent;
		clip = ancestor->viewBounds;
		do
		{
			ancestor = ancestor->fParent;
			// (host: a view without a root above it ends the walk)
			if (ancestor == nil)
				break;
			if (ancestor->DerivedFrom(clPrintView) || ancestor->DerivedFrom(clRemoteView))
				break;
			SectRect(&ancestor->viewBounds, &clip, &clip);
		} while (ancestor != gRootView);
	}
	Rect test = clip;
	Boolean first = true;
	LineLoop loop(this, &fTextObjects, fStyleCache, runLengths);
	long count = 0, ascent = 0, descent = 0, lineAscent = 0, lineDescent = 0;
	Rect r;
	SetRect(&r, 0, 0, 0, 0);
	Boolean more = loop.AddNextLine(&count, &ascent, &descent, &lineAscent, &lineDescent, &r);
	while (more)
	{
		test.top = r.top;
		test.bottom = r.bottom;
		if (!Overlaps(&clip, &test) || (ownBounds && clip.bottom < test.bottom))
		{
			DestroyTextObjectCacheContents(fTextObjects + objIndex, &fTextOptions);
			loop.fObjIndex -= count;
			if (r.bottom <= clip.top)
				lineStart = loop.fCurrent - loop.fTextStart;
			else if (!fCalculateBounds)
				break;
		}
		else
		{
			if (lines >= room)
			{
				GrowLineInfoCache(&fLineCache, 5);
				room += 5;
			}
			LineInfo* line = new LineInfo;
			if (line == nil)
				OutOfMemory();
			line->fStart = lineStart;
			line->fEnd = loop.fCurrent - loop.fTextStart;
			line->fEndsWithSpace = IsWhiteSpace(loop.fCurrent[-1]);
			line->fFirstObj = objIndex;
			line->fEndObj = objIndex + count;
			line->fAscent = ascent;
			line->fHeight = descent;
			line->fBounds = r;
			fLineCache[lines++] = line;
			lineStart = line->fEnd;
			objIndex = line->fEndObj;
			objectsKept += count;
		}
		fLineHeight = (short) (r.bottom - r.top);
		if (first)
		{
			fFirstBaseline = (short) (r.bottom - descent);
			fFirstLineAscent = (short) lineAscent;
			first = false;
		}
		Union(&text, &r);
		more = loop.AddNextLine(&count, &ascent, &descent, &lineAscent, &lineDescent, &r);
	}
	fLastBaseline = (short) (r.bottom - descent);
	fLastLineDescent = (short) lineDescent;
	fLineCache[lines] = nil;
	if (textLength != 0 && loop.fCurrent == loop.fTextStart + textLength && loop.fCurrent[-1] == kReturnChar
	 && loop.CurrentLineFitsInBounds() == 2 && lines > 0)
		fLineCache[lines - 1]->fEnd -= 1;
	if (objectsKept > 0)
	{
		Point baseline;
		GetTextObjBaseline(fTextObjects[0], &baseline);
		fTextOrigin.h = (short) (baseline.h - viewBounds.left);
		fTextOrigin.v = (short) (baseline.v - viewBounds.top);
	}
	if (text.top == -32768)
	{
		text = viewBounds;
		text.right = text.left;
	}
	else if (!fCalculateBounds)
		text.bottom = (short) (text.top + (viewBounds.bottom - viewBounds.top));
	if (textLength != 0 && loop.fCurrent[-1] == kReturnChar)
		text.bottom = (short) (text.bottom + fLineHeight);
	fTextBounds = text;
	if (fHasHeavyFaces)
	{
		short right = (short) (fTextBounds.right + 10);
		if (right <= fParent->viewBounds.right)
			fTextBounds.right = right;
	}
	long length = Ustrlen(loop.fTextStart);
	if (fCaretOffset > length)
		fCaretOffset = length;
}


// ROM 0x0016991c OffsetCachedBounds__14TParagraphViewFR6TPoint
// The lines and their text objects moved with the view, by how far the
// first text object's baseline is from where the view's top-left and
// the text origin (+0x80) put it - so lines laid out again after a move
// are never moved a second time; the hilites' areas made again.  (The
// delta is not used: the ROM works the move out for itself.)
void
TParagraphView::OffsetCachedBounds(Point& /*delta*/)
{
	if (fLineCache != nil && fLineCache[0] != nil && fTextObjects[0] != 0)
	{
		Point baseline;
		GetTextObjBaseline(fTextObjects[0], &baseline);
		short dh = (short) (viewBounds.left + fTextOrigin.h - baseline.h);
		short dv = (short) (viewBounds.top + fTextOrigin.v - baseline.v);
		if (dh != 0 || dv != 0)
		{
			for (LineInfo** line = fLineCache; *line != nil; line++)
				OffsetRect(&(*line)->fBounds, dh, dv);
			for (TextObjectRef* obj = fTextObjects; *obj != 0; obj++)
			{
				FPoint location;
				GetTextObjField(*obj, kTextObjLocation, &location);
				location.x += ToFixed(dh);
				location.y += ToFixed(dv);
				SetTextObjField(*obj, kTextObjLocation, &location);
			}
			OffsetRect(&fTextBounds, dh, dv);
		}
	}
	UpdateHiliteArea();
}


/*------------------------------------------------------------------------------
	W h a t   i s   a s k e d   o f   t h e   l i n e s
------------------------------------------------------------------------------*/

// a text object's first character in the paragraph's text, and how many
// characters it holds (the fitted length)
static long
TextObjOffset(TextObjectRef obj)
{
	TextRef* ref;
	GetTextObjField(obj, kTextObjText, &ref);
	return ref->fOffset;
}

static long
TextObjLength(TextObjectRef obj)
{
	long length;
	GetTextObjField(obj, kTextObjFittedLength, &length);
	return length;
}


// ROM 0x0017d32c PointInMarginsToOffset__FRC6TPointPC8LineInfo
// A point beside a line's text: left of it (or above) is its start, right
// of it (or below) its end - before the space it ends with, if it does.
long
PointInMarginsToOffset(const Point& pt, const LineInfo* line)
{
	if (pt.v < line->fBounds.top || pt.h <= line->fBounds.left)
		return line->fStart;
	if ((line->fBounds.bottom <= pt.v || line->fBounds.right <= pt.h) && line->fEndsWithSpace)
		return line->fEnd - 1;
	return line->fEnd;
}


// ROM 0x0017d3dc CharBounds__FPC8LineInfolT2P5TRect
// The box of a character of a text object: from its left edge to the
// next one's (or nothing wide at the object's end), the line's height.
void
CharBounds(const LineInfo* line, TextObjectRef run, long offset, Rect* bounds)
{
	long length = TextObjLength(run);
	if (offset < 0)
		offset = 0;
	else if (length < offset)
		offset = length;
	Point left;
	CharLeftEdge(run, offset, &left);
	short right = left.h;
	if (offset < length)
	{
		Point next;
		CharLeftEdge(run, offset + 1, &next);
		right = next.h;
	}
	bounds->left = left.h;
	bounds->top = line->fBounds.top;
	bounds->right = right;
	bounds->bottom = line->fBounds.bottom;
}


// ROM 0x0017d4ac TabBounds__FP8LineInfolT2RC6RefVarP5TRect
// The box of a tab: from where the text before it ends (the run's right,
// or the line's left when there is no run before it) stop by stop to the
// tab at the offset.
void
TabBounds(const LineInfo* line, long offset, TextObjectRef run, RefArg tabs, Rect* bounds)
{
	long at;
	long x;
	if (run == 0)
	{
		at = line->fStart;
		x = line->fBounds.left;
	}
	else
	{
		Rect box;
		GetTextObjBounds(run, &box);
		at = TextObjOffset(run) + TextObjLength(run);
		x = box.right;
	}
	long lineLeft = line->fBounds.left;
	long index = 0;
	long left = x;
	long stop = FindNextTabStop(tabs, lineLeft, x, &index);
	for ( ; at < offset; at++)
	{
		long next = FindNextTabStop(tabs, lineLeft, stop + 1, &index);
		left = stop;
		stop = next;
	}
	bounds->left = (short) left;
	bounds->top = line->fBounds.top;
	bounds->right = (short) stop;
	bounds->bottom = line->fBounds.bottom;
}


// ROM 0x00177b08 FindTextRunContainingCharOffset__14TParagraphViewFP8LineInfolPl
// The text object of the line the offset is in.  *kind is 0 for a
// character of a text object, 1 for a tab before one (the object before
// it is answered), 2 for an offset past the line's last object (1 again
// if a tab is there).  ==> the object's place in the cache, nil for none.
TextObjectRef*
TParagraphView::FindTextRunContainingCharOffset(const LineInfo* line, long offset, long* kind)
{
	TextObjectRef* first = fTextObjects + line->fFirstObj;
	TextObjectRef* end = fTextObjects + line->fEndObj;
	RefVar tabs(Tabs());
	*kind = 0;
	if (offset < 0)
		return nil;
	TextObjectRef* run;
	long start = 0;
	for (run = first; ; run++)
	{
		if (end <= run)
		{
			RefVar text(Text());
			const UniChar* chars = GetCString(text);
			*kind = chars[offset] == 0x09 ? 1 : 2;
			return first < end ? end - 1 : nil;
		}
		start = TextObjOffset(*run);
		if (offset < start + TextObjLength(*run))
			break;
	}
	if (offset < start)
	{
		*kind = 1;
		return first < run ? run - 1 : nil;
	}
	return run;
}


// ROM 0x0017789c FindTextRunContainingCoordinate__14TParagraphViewFP8LineInfosPl
// The text object of the line under h.  Over a tab rather than text,
// *tabOffset is the tab's offset (the next one when h is past the middle
// of a tab before a flush object) and the object before it is answered;
// otherwise *tabOffset is -1.  ==> nil for h off the line.
TextObjectRef*
TParagraphView::FindTextRunContainingCoordinate(const LineInfo* line, short h, long* tabOffset)
{
	*tabOffset = -1;
	if (h < line->fBounds.left || line->fBounds.right <= h)
		return nil;
	TextObjectRef* first = fTextObjects + line->fFirstObj;
	TextObjectRef* end = fTextObjects + line->fEndObj;
	RefVar tabs(Tabs());
	long lineLeft = line->fBounds.left;
	long at = line->fStart;
	long x = lineLeft;
	if (first < end)
	{
		TextObjectRef* last = end - 1;
		for (TextObjectRef* run = first; run < end; run++)
		{
			Rect box;
			GetTextObjBounds(*run, &box);
			if (h < box.right)
			{
				if (h < box.left && GetTextObjFlush(*run) == 0)
				{
					long index = 0;
					FindNextTabStop(tabs, lineLeft, h, &index);
					long before = index;
					long stop = FindNextTabStop(tabs, lineLeft, box.left - 1, &index);
					long count = index - before;
					*tabOffset = TextObjOffset(*run) - (count + 1);
					Rect r;
					OffsetToBounds(*tabOffset, &r);
					if ((stop + r.left) / 2 < h)
						*tabOffset += 1;
					return first < run ? run - 1 : nil;
				}
				return run;
			}
			if (run == last)
			{
				x = box.right;
				at = TextObjOffset(*run) + TextObjLength(*run);
			}
		}
	}
	long index = 0;
	long stop = x;
	while (at < line->fEnd - 1)
	{
		stop = FindNextTabStop(tabs, lineLeft, stop, &index);
		if (h < stop)
			break;
		at++;
	}
	*tabOffset = at;
	return first < end ? end - 1 : nil;
}


// ROM 0x00178104 OffsetInRunToBounds__14TParagraphViewFlP8LineInfoN21P5TRect
// The box of the offset as FindTextRunContainingCharOffset placed it: a
// character's (CharBounds), a tab's (TabBounds), or past the line's text -
// from its right (the empty line's place when it has next to no width) to
// the view's right edge.
void
TParagraphView::OffsetInRunToBounds(long offset, const LineInfo* line, TextObjectRef run, long kind, Rect* bounds)
{
	if (kind == 1)
	{
		RefVar tabs(Tabs());
		TabBounds(line, offset, run, tabs, bounds);
	}
	else if (kind == 2)
	{
		*bounds = line->fBounds;
		if ((short) (bounds->right - bounds->left) < 2)
			bounds->left = LeftEdgeOfEmptyLine(viewBounds, (ULong) fViewJustify & 0x3fffffff);
		else
			bounds->left = bounds->right;
		bounds->right = viewBounds.right;
	}
	else
		CharBounds(line, run, offset - TextObjOffset(run), bounds);
}
