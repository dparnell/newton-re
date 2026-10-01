/*
	File:		views/ParagraphLines.h

	Contains:	A paragraph's line layout, as the ROM does it: LineLoop
				lays the text out a line at a time, each line made of text
				objects (qd/TextObject.h) - one for each stretch of the
				line between tabs, its characters read through a TextRef
				(the paragraph and an offset into its text) rather than
				held - and FillAllCaches keeps the lines that show
				(LineInfo) and their text objects in two caches.  Every
				question about where a character is or which character is
				under a point is then put to the text objects of the line
				it is on (CharLeftEdge, CoordToChar, ...).

				The caches are arrays of words ended by a nought and then
				-1 (InitializeCache), so a cache knows its length
				(CacheLength: up to the nought) and its room (CacheMaxLength:
				up to the -1); they start with room for a line per 32
				characters, at most a tenth of the screen's height, and
				grow five at a time.

				The view's style records are a third cache
				(TParagraphView::CreateStyleRecordCache): a nil-ended array
				of pointers to the records of the styles' runs, with the
				runs' lengths beside it - which LineLoop consumes as it goes
				(UpdateStyleRunLengths), giving each text object a copy of
				the lengths of the runs it covers.

	Reconstructed from the MP2x00 US ROM (0x0010d87c-0x0010ed2c,
	0x0017c58c-0x0017d0a8, 0x0017d2a0-0x0017d6c0); each function cites its
	origin.  docs/views/README.md, "The line layout".
*/

#ifndef __PARAGRAPHLINES_H
#define __PARAGRAPHLINES_H

#include "TextObject.h"

class TParagraphView;
struct LineInfo;

// What a paragraph's text object holds in place of its characters: the
// paragraph and where the object's characters start in its text.  The
// scanner fetches the text from the view while a pass holds it (the ROM's
// 0xc bytes: the view, the offset, and a RefHandle for the text).
struct TextRef
{
	TParagraphView*	fView;			// +0x00
	long			fOffset;		// +0x04
	RefStruct		fText;			// +0x08  the text while it is held (nil otherwise)
};

long	TextRefScanner(void* refCon, long offset, long count, long charSize, void** chars);	// ROM 0x0017cfdc TextRefScanner__FPvlN22PPv
void	InitializeTextOptions(TextOptions* options, ULong justify, short width, ULong transferMode);	// ROM 0x0010d87c InitializeTextOptions__FP11TextOptionsUlsT2

// The caches (the words are pointer-sized on the host).
void	InitializeCache(void* cache, long count);					// ROM 0x0017c7c0 InitializeCache__FPvl - count noughts, a nought, then -1
long	CacheMaxLength(void* cache);								// ROM 0x0017c888 CacheMaxLength__FPv - the room
long	CacheLength(void* cache);									// ROM 0x0017c8b0 CacheLength__FPv - the entries (nought for no cache)
void	InitializeTextObjectCache(TextObjectRef** cache, long textLength, TextOptions* options);	// ROM 0x0017c58c InitializeTextObjectCache__FPPllP11TextOptions
void	GrowTextObjectCache(TextObjectRef** cache, long more);		// ROM 0x0017c64c GrowTextObjectCache__FPPll
void	DestroyTextObjectCache(TextObjectRef** cache, TextOptions* options);	// ROM 0x0017c6d0 DestroyTextObjectCache__FPPlP11TextOptions
void	DestroyTextObjectCacheContents(TextObjectRef* cache, TextOptions* options);	// ROM 0x0017c704 DestroyTextObjectCacheContents__FPlP11TextOptions - this entry and every one after it
void	InitializeLineInfoCache(LineInfo*** cache, long textLength);	// ROM 0x0017c8e4 InitializeLineInfoCache__FPPP8LineInfol
void	GrowLineInfoCache(LineInfo*** cache, long more);			// ROM 0x0017c9cc GrowLineInfoCache__FPPP8LineInfol
void	DestroyLineInfoCache(LineInfo*** cache);					// ROM 0x0017ca50 DestroyLineInfoCache__FPPP8LineInfo

// The style runs consumed: `count` characters taken off the front of the
// runs (*runLengths and *styles moved on past the runs used up),
// *lastStyle the run the last of them is in; with `copy`, ==> the
// lengths of the runs they cover (the last cut to what was taken), a new
// array for a text object to own.
short*	UpdateStyleRunLengths(short** runLengths, StyleRecord*** styles, StyleRecord*** lastStyle, ULong count, Boolean copy);	// ROM 0x0017caa0 UpdateStyleRunLengths__FPPsPPP11StyleRecordT2UlUc

// The tab stops: the tabs slot's positions from `left`, then every 48
// pixels after the last.  ==> the first stop right of x; *index (in: where
// to start looking) the stop's index.
long	FindNextTabStop(RefArg tabs, long left, long x, long* index);	// ROM 0x0017cc40 FindNextTabStop__FRC6RefVarlT2Pl
long	NextTabOrCRCharOffset(const UniChar* text, Boolean* isCR);	// ROM 0x0017cd50 NextTabOrCRCharOffset__FPUsPUc - -1 when neither comes before the end

// The greatest of the four metrics (GetParagraphStyleRecordMetrics) of
// the style records from `first` to `last`; ==> the ascent.
long	GetMaxAscent(StyleRecord** first, StyleRecord** last, long* descent, long* lineAscent, long* lineDescent);	// ROM 0x0017cf1c GetMaxAscent__FPP11StyleRecordT1PlN23

// What is asked of a paragraph's text objects.
void	GetTextObjBounds(TextObjectRef text, Rect* bounds);		// ROM 0x0017ce40 GetTextObjBounds__FlP5TRect - the bounds, the leading under the bottom
void	GetTextObjBaseline(TextObjectRef text, Point* baseline);	// ROM 0x0017ce90 GetTextObjBaseline__FlP6TPoint
void	SetTextObjBaseline(TextObjectRef text, Point& baseline);	// ROM 0x0017cec0 SetTextObjBaseline__FlR6TPoint
Fixed	GetTextObjFlush(TextObjectRef text);						// ROM 0x0017cef4 GetTextObjFlush__Fl - its options' alignment
void	CharLeftEdge(TextObjectRef text, long offset, Point* pt);	// ROM 0x0017d5c4 CharLeftEdge__FlT1P6TPoint - where the character starts (its baseline)
long	CoordToInterCharGap(TextObjectRef text, short h);			// ROM 0x0017d614 CoordToInterCharGap__Fls - the boundary nearest h
long	CoordToChar(TextObjectRef text, short h);					// ROM 0x0017d66c CoordToChar__Fls - the character h is over
// The boxes of the characters and tabs of a line, and a point beside it.
void	CharBounds(const LineInfo* line, TextObjectRef run, long offset, Rect* bounds);	// ROM 0x0017d3dc CharBounds__FPC8LineInfolT2P5TRect
void	TabBounds(const LineInfo* line, long offset, TextObjectRef run, RefArg tabs, Rect* bounds);	// ROM 0x0017d4ac TabBounds__FP8LineInfolT2RC6RefVarP5TRect
long	PointInMarginsToOffset(const Point& pt, const LineInfo* line);	// ROM 0x0017d32c PointInMarginsToOffset__FRC6TPointPC8LineInfo
short	LeftEdgeOfEmptyLine(const Rect& bounds, ULong justify);	// ROM 0x0017d390 LeftEdgeOfEmptyLine__FRC5TRectl - where the caret goes on an empty line: the left, right or middle of the bounds
void	TPoint2FPoint(const Point& pt, FPoint* fpt);				// ROM 0x0017d0a8 TPoint2FPoint__FR6TPointP6FPoint
void	FPoint2TPoint(const FPoint& fpt, Point* pt);				// ROM 0x0017d0cc FPoint2TPoint__FR6FPointP6TPoint - rounded
void	FRect2TRect(const FRect& frect, Rect* rect);				// ROM 0x0017d100 FRect2TRect__FR5FRectP5TRect - rounded

// The ROM's 0x54-byte object (the offsets are the ROM's).
class LineLoop
{
public:
				LineLoop(TParagraphView* view, TextObjectRef** textObjects, StyleRecord** styles, short* runLengths);	// ROM 0x0010d8d4 __ct__8LineLoopFP14TParagraphViewPPlPP11StyleRecordPs
				~LineLoop();											// ROM 0x0010db5c __dt__8LineLoopFv

	// The next line: its text objects made (AddNextTextRun until one
	// says the line is done), its box worked out and its objects moved
	// onto its baseline (ComputeLineBounds), and the loop moved on to the
	// next line.  ==> false when there is no more text, or no more room.
	// *count the line's objects; *ascent and *descent the baseline's
	// place in the box (the line height's part above the bottom's
	// distance below it - LineInfo +0x14 - and that distance - +0x18);
	// *lineAscent, *lineDescent its fonts' greatest.
	Boolean		AddNextLine(long* count, long* ascent, long* descent, long* lineAscent, long* lineDescent, Rect* bounds);	// ROM 0x0010db98 AddNextLine__8LineLoopFPlN41P5TRect
	void		ComputeLineBounds(long firstObj, long count, long maxAscent, long maxDescent, long lineAscent, long lineDescent, Rect* bounds);	// ROM 0x0010ddc0 ComputeLineBounds__8LineLoopFlN51P5TRect
	// One text object: the text from the current position to the next tab
	// or return, cut to what fits the rest of the line and back to a word
	// break.  ==> false when there is no text or no room left; *done when
	// the line ends with it.
	Boolean		AddNextTextRun(Boolean* done, StyleRecord*** lastStyle);	// ROM 0x0010e394 AddNextTextRun__8LineLoopFPUcPPP11StyleRecord
	long		SkipLeadingTabs(void);									// ROM 0x0010eb50 SkipLeadingTabs__8LineLoopFv - ==> the pen's place after them, 0 for none
	long		CurrentLineFitsInBounds(void);							// ROM 0x0010ec90 CurrentLineFitsInBounds__8LineLoopFv
	long		GetPseudoSpacing(long ascent, long descent);			// ROM 0x0010ecd4 GetPseudoSpacing__8LineLoopFlT1

	const UniChar*	fTextStart;			// +0x00
	const UniChar*	fCurrent;			// +0x04
	long			fObjIndex;			// +0x08  the last text object made (-1 none yet)
	TParagraphView*	fView;				// +0x0c
	TextObjectRef**	fTextObjects;		// +0x10  the view's text object cache
	StyleRecord**	fStyles;			// +0x14
	short*			fRunLengths;		// +0x18  the runs' lengths, consumed (nil: one style)
	RefStruct		fText;				// +0x1c  the text, locked (the ROM's TObjectPtr)
	TextOptions*	fOptions;			// +0x20  the view's (+0x84)
	const UniChar*	fTextEnd;			// +0x24
	StyleRecord**	fStyle;				// +0x28  the run at the current position
	short			fLeft;				// +0x2c
	short			fRight;				// +0x2e  the view's right - or its parent's, text flag 4
	short			fLineTop;			// +0x30  the next line's top
	short			fPenX;				// +0x32  where the next text object starts
	short			fBaseline;			// +0x34  (a TPoint: the baseline, and the next object's left)
	short			fBaselineX;			// +0x36
	Boolean			fNoMoreTabs;		// +0x38  no tab or return before the end
	long			fInterLineSpacing;	// +0x3c  GetInterLineSpacing
	long			fRequestedSpacing;	// +0x40  GetRequestedLineSpacing
	long			fLineHeight;		// +0x44  the last line's spacing
	long			fNextTop;			// +0x48  the last line's baseline plus its fonts' descent
	RefStruct		fTabs;				// +0x4c
	long			fTabCount;			// +0x50
};

#endif	/* __PARAGRAPHLINES_H */
