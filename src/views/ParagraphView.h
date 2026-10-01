/*
	File:		views/ParagraphView.h

	Contains:	TParagraphView (clParagraphView, 81): a view of styled text -
				the class of protoStaticText and every editable paragraph.
				Its text slot is a string, its styles slot the style runs
				([length, style, ...], see StyleRuns.h; a single font spec
				when there are none), its tabs slot the tab stops; the text
				is wrapped into the bounds a line at a time, each line the
				height its fonts need (or viewLineSpacing when the view has
				one that fits its font), aligned by the viewJustify text
				bits, and an ellipsis marks text that does not fit.  The
				ROM's object is 0xd0 bytes and caches the lines (LineInfo)
				and the text objects they are drawn as, laid out by the
				ROM's LineLoop (ParagraphLines.h).

				Display, and the caret: the paragraph can be the key view -
				SetCaretOffset keeps its caret offset (fCaretOffset), and
				OffsetToCaret/PointToCaret place the caret from the line
				cache (host: the character's text object measured up to the
				offset; the ROM asks CharBounds).  Typing: the key
				commands (RealDoCommand) insert and delete at the caret
				through InsertStyledText, which makes an aeReplaceText
				command (MakeAndDoReplaceCommand) that HandleReplaceText
				carries out - the text munged, the style runs adjusted
				(AdjustStyles), the inverse posted for undo (consecutive
				keys merged by AddKeyToCurrUndo), the caret moved, the lines
				laid out again (RangeChanged).  The hilites (a selection
				typed over), ink words, the recogniser's words and the pen
				gestures (HandleScrub, ScrubLines, ScrubWords, HandleCaret,
				InsertHorizontalSpace, InsertVerticalSpace, CheckAndDoJoin,
				HandleLineGesture), the correction info, the readers of the
				laid-out lines and the insert areas
				(ParagraphInsertAreas.cpp) are here.

	Reconstructed from the MP2x00 US ROM (0x0016911c-0x0016c260,
	0x00178748-0x00178a30, 0x0017edc0-0x00181580); each function cites its
	origin.
*/

#ifndef __PARAGRAPHVIEW_H
#define __PARAGRAPHVIEW_H

#ifndef __DATAVIEW_H
#include "DataView.h"
#endif
#ifndef __TEXTVIEW_H
#include "TextView.h"		// vjOneLineOnly, vjNoLineLimits
#endif
#ifndef __TEXT_H
#include "Text.h"
#endif
#include "TextObject.h"		// TextObjectRef: the line layout's text objects

// one line of the paragraph as FillAllCaches (0x0016bc38) records it: the
// ROM's LineInfo is 0x24 bytes - the offsets of the line's first
// character and of the one after its last, its first text object and the
// one after its last (indices into the view's text object cache), whether
// it ends in white space, where its baseline is, and its box
// (views/ParagraphLines.h)
struct LineInfo
{
	long		fStart;				// +0x00  the line's first character
	long		fEnd;				// +0x04  where the next line starts: after
									//        the spaces and the return that
									//        end this one, as LineLoop leaves it
	long		fFirstObj;			// +0x08  its first text object
	long		fEndObj;			// +0x0c  after its last
	Boolean		fEndsWithSpace;		// +0x10
	long		fAscent;			// +0x14  the line's spacing less fHeight - its ascent,
									//        for a line in one font
	long		fHeight;			// +0x18  the line's bottom below the baseline (the
									//        two together are the line's height -
									//        LineLoop::AddNextLine's outputs)
	Rect		fBounds;			// +0x1c  its box (the text's width)
};

// Where a word written on the page goes in a paragraph.  The word comes
// in - the box it was written in, the middle of its base line, its text
// and the unit it came from - and the three Find functions leave the
// answer in the rest: which view, at which offset, over how many
// characters, and whether it starts a new line.
class TParagraphView;
class TUnitPublic;
class CList;

// A stretch of the text a caret gesture opened (spaces or returns put in
// for the writer to write into), kept in the paragraph's list (+0x4c) so
// that what is left of it unwritten can be taken out again.  The list is
// a CList of these in order of their start.
struct InsertRun				// 0x0c bytes
{
	ULong		fStart;				// +0x00  where it starts
	ULong		fLength;			// +0x04  how long it is
	Boolean		fChanged;			// +0x08  something was written into it (or out of it)
};

struct Finder					// 0x2c bytes
{
	Rect		fBox;				// +0x00  where the word was written
	Point		fBase;				// +0x08  the middle of its base line
	const UniChar*	fText;			// +0x0c  the word
	ULong		fLength;			// +0x10  its characters
	TParagraphView*	fView;			// +0x14  the view it belongs to
	long		fOffset;			// +0x18  the character offset it goes at
	long		fReplaceLength;		// +0x1c  how many characters it replaces
	Boolean		fExact;				// +0x20  it replaces a character exactly (score 6)
	Boolean		fNewLine;			// +0x21  it starts a new line
	Boolean		fReallyDoIt;		// +0x22  the word is really going in
	long		fTab;				// +0x24  the tab stop it was written at (always 0)
	TUnitPublic*	fUnit;			// +0x28  the unit it came from, if any
};

// What a character written over a character of the text is told about
// where it landed, on its way to DoReplaceSym.
struct WordHit					// 0x28 bytes
{
	const UniChar*	fText;			// +0x00  the line's characters
	long		fIndexInRun;		// +0x04  the character it landed on, in the line
	long		fIndex;				// +0x08  the same, in the whole text
	long		fReplaceLength;		// +0x0c  1 it replaces that character, 0 it goes beside it
	long		fBaseline;			// +0x10  the line's bottom
	long		fUnused14;			// +0x14
	class TUnitPublic*	fUnit;		// +0x18  the writing, if it came from the pen
	const UniChar*	fWord;			// +0x1c  what was read
	struct LineInfo*	fLine;		// +0x20
	Boolean		fReallyDoIt;		// +0x24
};

// The last replacement, so that a writer correcting the same letter of
// the same word again is understood to be choosing between its readings.
extern long		gLastReplacedIndex;					// ROM 0x0c101740
extern UniChar*	gLastReplacedWord;					// ROM 0x0c101744
// The one-character string a replacement's answer is handed back as.
extern UniChar	gAlternateWord[4];					// ROM 0x0c101738 gAlternateWord

// The letter under the writing replaced by what the recogniser read, and
// the word it belongs to given a new set of readings.
Boolean	DoReplaceSym(TParagraphView* para, WordHit* hit, UniChar* out, RefArg breakTable);	// ROM 0x0017b1b8 DoReplaceSym__FP14TParagraphViewP7WordHitPUsRC6RefVar
// Whether the writing between two characters is over a run of spaces.
Boolean	WordOverSpaces(const UniChar* text, const long from, const long to);	// ROM 0x0017bc84 WordOverSpaces__FPUsClT2

// How wide a gap has to be before it is taken for a tab rather than a
// space.
// Whether the writer is putting a single letter into the middle of a
// word at the caret, which wants no space around it.
Boolean	IsMidWordLetterInsertion(TParagraphView* para, TUnitPublic* unit);	// ROM 0x00172584 IsMidWordLetterInsertion__FP14TParagraphViewP11TUnitPublic
long	MinWidthToIntuitTab(const UniChar* text, const Rect& box);	// ROM 0x001733e4 MinWidthToIntuitTab__FPCUsRC5TRect

class TParagraphHilite;

// The text flags of an input view: what kind of text it takes.
ULong	GetInputViewTextFlags(ULong textFlags, ULong viewFlags);	// ROM 0x00261d2c GetInputViewTextFlags__FUlT1

// Whether a font spec is a font frame (it names a family) rather than
// something else a style slot may hold.
Boolean	IsFontFrame(RefArg fontSpec);						// ROM 0x00179f08 IsFontFrame__FRC6RefVar

// The style record a run of a paragraph is laid out and drawn with.
// This is CreateTextStyleRecord with the two things a paragraph knows
// about on top: the view's own default font, and ink words - a run
// whose "font" is an 'inkWord binary is a word of writing, and the
// record made for it is what qd/Fonts.h's OpenFont turns into a font of
// one glyph (ink/InkFont.h).
//
// `textFlags` are the view's: bit 3 says every run takes the default
// font whatever its own spec says, and bit 4 that an ink word is to be
// laid out at the text's size rather than its own.
void	CreateParagraphStyleRecord(RefArg fontSpec, StyleRecord* style, ULong textFlags,
								   RefArg defaultFont);		// ROM 0x00179f58 CreateParagraphStyleRecord__FRC6RefVarP11StyleRecordUlT1

// A style record's ascent and descent, and (when asked) the ascent and
// descent a line holding it wants - an ink word's by its size: 14 over 5
// up to 12 points, 17 over 5 up to 39, and above that 17 and 5 for each
// 18 points past 22.
void	GetParagraphStyleRecordMetrics(StyleRecord* style, long* ascent, long* descent,
									   long* lineAscent, long* lineDescent);	// ROM 0x0017a0fc GetParagraphStyleRecordMetrics__FP11StyleRecordPlN32

// Whether every character of the run is white space (a count of -1: to
// the end of the string).
Boolean	ContainsOnlyWhiteSpace(const UniChar* text, ULong count);	// ROM 0x0017a310 ContainsOnlyWhiteSpace__FPUsUl

// Which of a word's two edges a point is nearest, with a bias: a positive
// bias takes the right edge only once the point is that far (per cent)
// across the word, a negative one takes it unless the point is within
// that much of the right edge.
long	FindNearestWordBoundary(const Point& pt, long left, long right, long bias);	// ROM 0x0017d2a0 FindNearestWordBoundary__FRC6TPointlN22

// The view the recogniser last put a word into, and where it put it.  A
// scrub that arrived before the word did is not allowed to take it away.
class TView;
TView*	GetLastAddedWordView(void);							// ROM 0x0016c63c GetLastAddedWordView__Fv
extern TView*	gLastAddedWordView;							// ROM 0x0c101714 gLastAddedWordView
extern ULong	gLastAddedWordAddTime;						// ROM 0x0c101724 gLastAddedWordAddTime
extern long		gLastAddedWordEndOffset;					// ROM 0x0c10172c gLastAddedWordEndOffset
extern Rect		gLastAddedWordBox;							// ROM 0x0c101718 gLastAddedWordBox
extern Point	gLastAddedWordBase;							// ROM 0x0c101728 gLastAddedWordBase
extern ULong	gLastAddedWordInkEndTime;					// ROM 0x0c101720 gLastAddedWordInkEndTime
Rect*	GetLastAddedWordBox(void);							// ROM 0x0016c64c GetLastAddedWordBox__Fv
Point*	GetLastAddedWordBase(void);							// ROM 0x00170094 GetLastAddedWordBase__Fv

// The word around a character offset: forward or back while the
// characters are of the same kind (text or ink) and not white space.
// Whether the point is on an ink word inside the view's selection.
Boolean	HitsHilitedInkWord(TView* view, Point pt);			// ROM 0x00171344 HitsHilitedInkWord__FP5TView6TPoint
long	ScanWordStart(const UniChar* text, long offset, long limit);	// ROM 0x001a1250 ScanWordStart__FPUslT2
long	ScanWordEnd(const UniChar* text, long offset, long limit);	// ROM 0x001a1134 ScanWordEnd__FPUslT2
long	ScanNextWord(const UniChar* text, long offset, long limit);	// ROM 0x001a1398 ScanNextWord__FPUslT2 - the start of the next word
long	ScanPrevWordEnd(const UniChar* text, long offset, long limit);	// ROM 0x001a1484 ScanPrevWordEnd__FPUslT2 - the end of the one before (-1: none)

// The room a paragraph allows around itself when it is asked whether a
// word written on the page belongs to it.
void	AddMarginsToBounds(Rect* bounds);					// ROM 0x00172048 AddMarginsToBounds__FP5TRect
// Whether one box was written beside another on the same line, and
// whether one is on the line above the other.
Boolean	AdjacentBoxes(const Rect& box, const Rect& next, const Point& base,
				  const Point& nextBase, long gap);			// ROM 0x0017b010 AdjacentBoxes__FRC5TRectT1RC6TPointT3l
Boolean	BoxAboveBox(const Rect& box, const Rect& below);		// ROM 0x0017b08c BoxAboveBox__FRC5TRectT1

class TParagraphView : public TDataView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x0017e3ac ClassID__14TParagraphViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x0017e3b4 DerivedFrom__14TParagraphViewCFl
	virtual long	TextFlags(void) const;								// ROM 0x0038abc8 (unnamed) - the vtable's +0x20 - fTextFlags
	virtual			~TParagraphView();									// ROM 0x001805f4 __dt__14TParagraphViewFv
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x0017edc0 Constructor__14TParagraphViewFRC6RefVarP5TView
	virtual void	SetupDone(void);									// ROM 0x0017f5d8 SetupDone__14TParagraphViewFv
	virtual long	Idle(long reason);									// ROM 0x0017e964 Idle__14TParagraphViewFl - reason 2 runs a deferred tap
	virtual Ref		GetRangeText(long offset, long length);				// ROM 0x00180248 GetRangeText__14TParagraphViewFlT1 (vtable +0x40)
	virtual void	HandleTap(Point& pt);								// ROM 0x001752c4 HandleTap__14TParagraphViewFR6TPoint (vtable +0x11c) - the caret placed at the tap
	virtual Boolean	PointInHilite(Point& pt);							// host: the point tested against the selection region (the ROM TView::PointInHilite 0x0026051c asks each hilite Encloses)
	virtual void	RealDraw(Rect& bounds);								// ROM 0x0016911c RealDraw__14TParagraphViewFR5TRect
	virtual void	SetBounds(const Rect& bounds);						// ROM 0x0017e3e8 SetBounds__14TParagraphViewFRC5TRect
	virtual void	SetCaretOffset(long* offset, long* length);			// ROM 0x0017efd8 SetCaretOffset__14TParagraphViewFPlT1
	virtual Ref		GetSelection(void);									// ROM 0x0017f050 GetSelection__14TParagraphViewFv
	virtual void	SetValue(RefArg slot, RefArg value);				// ROM 0x0018081c SetValue__14TParagraphViewFRC6RefVarT1
	virtual void	SetSelection(RefArg selection, long* offset, long* length);	// ROM 0x0017f178 SetSelection__14TParagraphViewFRC6RefVarPlT2
	virtual void	ActivateSelection(Boolean on);						// ROM 0x0017f2d8 ActivateSelection__14TParagraphViewFUc
	virtual void	OffsetToCaret(long offset, Rect* caret);			// ROM 0x00171ad4 OffsetToCaret__14TParagraphViewFlP5TRect
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar
	virtual long	HandleScrub(const Rect& bounds, long kind, TUnitPublic* unit, Boolean reallyDoIt);	// ROM 0x00173fac HandleScrub__14TParagraphViewFRC5TRectlP11TUnitPublicUc
	virtual long	HandleCaret(ULong kind, long angle, Point& armA, Point& point,
								Point& armB, Point& tail);			// ROM 0x001753b4 HandleCaret__14TParagraphViewFUllR6TPointN33
	virtual long	HandleLineGesture(long angle, Point& from, Point& to);	// ROM 0x00176bd4 HandleLineGesture__14TParagraphViewFlR6TPointT2
	virtual void	HiliteText(long start, long length, Boolean caretOnEmpty);	// ROM 0x0016a490 HiliteText__14TParagraphViewFlT1Uc
	virtual long	HandleWord(const UniChar* text, ULong length, const Rect& box,
							   const Point& pt, ULong startTime, ULong endTime, RefArg info,
							   Boolean reallyDoIt, long* outOffset, TUnitPublic* unit);	// ROM 0x00172760 HandleWord__14TParagraphViewFPCUsUlRC5TRectRC6TPointN22RC6RefVarUcPlP11TUnitPublic (vtable +0x148)
	virtual void	SaveAddedUnitBounds(const Rect& box, const Point& base, ULong inkEndTime);	// ROM 0x00172e68 SaveAddedUnitBounds__14TParagraphViewFRC5TRectRC6TPointUl (vtable +0x150)
	virtual void	PointToCaret(Point& pt, Rect* caret, Rect* bounds);	// ROM 0x001716c8 PointToCaret__14TParagraphViewFR6TPointP5TRectT2
	// the selection, and the paragraph as a drag's source and target
	virtual Boolean	IsCompletelyHilited(RefArg hilite);					// ROM 0x0017ed50 IsCompletelyHilited__14TParagraphViewFRC6RefVar
	virtual void	DeleteHilited(RefArg hilite);						// ROM 0x0017eaf8 DeleteHilited__14TParagraphViewFRC6RefVar
	virtual void	RemoveHilite(RefArg hilite);						// ROM 0x0017edcc RemoveHilite__14TParagraphViewFRC6RefVar
	virtual long	ClickOptions(void);									// ROM 0x0017ea80 ClickOptions__14TParagraphViewFv
	virtual Boolean	AddDragInfo(TDragInfo* dragInfo);					// ROM 0x0017f320 AddDragInfo__14TParagraphViewFP9TDragInfo
	virtual Ref		GetDropData(RefArg dragType, RefArg dragRef);		// ROM 0x0017f3f4 GetDropData__14TParagraphViewFRC6RefVarT1
	virtual Boolean	Drop(RefArg dropType, RefArg dropData, Point* dropPt);	// ROM 0x0017fc20 Drop__14TParagraphViewFRC6RefVarT1P6TPoint
	virtual Boolean	DropMove(RefArg dragRef, const Point& delta, const Point& dropPt, Boolean copy);	// ROM 0x0017fff8 DropMove__14TParagraphViewFRC6RefVarRC6TPointT2Uc
	virtual Boolean	DropRemove(RefArg dragRef);							// ROM 0x00180164 DropRemove__14TParagraphViewFRC6RefVar
	virtual Boolean	DropDone(void);										// ROM 0x001800e4 DropDone__14TParagraphViewFv
	virtual Boolean	DragFeedback(const TDragInfo& dragInfo, const Point& pt, Boolean show);	// ROM 0x001801d0 DragFeedback__14TParagraphViewFRC9TDragInfoRC6TPointUc
	virtual Ref		GetSupportedDropTypes(const Point& pt);				// ROM 0x0017f378 GetSupportedDropTypes__14TParagraphViewFRC6TPoint
	virtual void	DrawScaledData(const Rect& src, const Rect& dst, Rect* bounds);	// ROM 0x0016afe4 DrawScaledData__14TParagraphViewFRC5TRectT1P5TRect
	virtual TView*	AddHilited(RefArg hilite, class TEditView* editor);	// ROM 0x0017ebb4 AddHilited__14TParagraphViewFRC6RefVarP9TEditView
	virtual void	CleanupData(void);									// ROM 0x0017e83c CleanupData__14TParagraphViewFv
	virtual Ref		GetProperties(RefArg hilite);						// ROM 0x00181418 GetProperties__14TParagraphViewFRC6RefVar (vtable +0x154)
	Boolean			HiliteClick(TStrokePublic* stroke);					// ROM 0x0017ede4 HiliteClick__14TParagraphViewFP13TStrokePublic
	Boolean			IconClick(TStrokePublic* stroke);					// ROM 0x0017efa8 IconClick__14TParagraphViewFP13TStrokePublic
	Boolean			ClickCommand(RefArg cmd);							// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0x187c (aeClick)
	Boolean			ScaleCommand(RefArg cmd);							// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0x2464 (aeScaleData)
	virtual long	PointOverHilitedText(Point& pt);					// ROM 0x0016b1b8 PointOverHilitedText__14TParagraphViewFR6TPoint (vtable +0x134) - 0 no, 1 over the selection, 2 over one that runs to the end, 3 just below such a one
	virtual Boolean	PointOverText(Point& pt, Point* onLine);			// ROM 0x00177c5c PointOverText__14TParagraphViewFR6TPointP6TPoint (vtable +0x138)
	long			FindLineContainingPoint(Point* pt, long margin);	// ROM 0x001782e8 FindLineContainingPoint__14TParagraphViewFP6TPoint10MarginSize (host: the line's index, -1 for none)
	Ref				GetRangeProperties(long start, long end);			// ROM 0x001811b0 GetRangeProperties__14TParagraphViewFlT1
	void			ROMDeleteHilited(RefArg hilite);					// ROM 0x00174aac ROMDeleteHilited__14TParagraphViewFRC6RefVar

	Ref			Text(void);												// ROM 0x00181004 Text__14TParagraphViewFv
	Ref			Styles(void);											// ROM 0x00181448 Styles__14TParagraphViewFv
	Ref			GetStyles(void);										// ROM 0x00181104 GetStyles__14TParagraphViewFv
	Ref			Tabs(void);												// ROM 0x001814b4 Tabs__14TParagraphViewFv
	Ref			GetDefaultViewStyle(void);								// ROM 0x001789bc GetDefaultViewStyle__14TParagraphViewFv
	long		GetInterLineSpacing(void);								// ROM 0x00169460 GetInterLineSpacing__14TParagraphViewFv
	long		GetRequestedLineSpacing(void);							// ROM 0x001693fc GetRequestedLineSpacing__14TParagraphViewFv - viewLineSpacing, 0 when none
	// The baselines a list lays its paragraphs out by (views/ListView.h):
	// the first line's, the last line's (a line lower when the text ends in
	// a return) and where the next paragraph's first would go.
	long		GetFirstBaseline(void);									// ROM 0x0016b77c GetFirstBaseline__14TParagraphViewFv
	long		GetLastBaseline(void);									// ROM 0x0016b8a8 GetLastBaseline__14TParagraphViewFv
	long		GetNextBaseline(TParagraphView* next);					// ROM 0x0016b454 GetNextBaseline__14TParagraphViewFP14TParagraphView
	void		AdjustBoundsForFirstBaseline(long baseline);			// ROM 0x0016b750 AdjustBoundsForFirstBaseline__14TParagraphViewFl - the box moved to put the first baseline there
	void		CreateAllCaches(void);									// ROM 0x0016baa8 CreateAllCaches__14TParagraphViewFv
	void		ClearAllCaches(void);									// ROM 0x0016bbd0 ClearAllCaches__14TParagraphViewFv
	void		RefillAllCaches(void);									// ROM 0x0016c25c RefillAllCaches__14TParagraphViewFv
	void		FillAllCaches(short* runLengths);						// ROM 0x0016bc38 FillAllCaches__14TParagraphViewFPs
	void		OffsetCachedBounds(Point& delta);						// ROM 0x0016991c OffsetCachedBounds__14TParagraphViewFR6TPoint
	long		FindLineContainingCharOffset(long offset);				// ROM 0x001786f8 FindLineContainingCharOffset__14TParagraphViewFl (host: the line's index, -1 for none)
	void		OffsetToBounds(long offset, Rect* bounds);				// ROM 0x00177f20 OffsetToBounds__14TParagraphViewFlP5TRect
	// The character offset at a point: the line by FindLineContainingPoint
	// (margin: 0 the line's box, 1 or 2 any distance to its sides - 2 also
	// above and below the paragraph - 3 half a line above it and ten pixels
	// to the sides), the text object or tab under it, and then the nearest
	// character boundary, or (onChar) the character the point is over.
	// With nothing else asked, a point beside the line's box answers its
	// start or end.  ==> -1 for no line there.  The character's box, the
	// line (its index), the text object and whether it was a tab can be
	// asked for too.
	long		PointToOffset(const Point& pt, long margin, Boolean onChar, Rect* charBox,
							  long* outLine, TextObjectRef* outRun, Boolean* outTab);	// ROM 0x00177520 PointToOffset__14TParagraphViewFRC6TPoint10MarginSizeUcP5TRectPP8LineInfoPlPUc
	TextObjectRef*	FindTextRunContainingCharOffset(const LineInfo* line, long offset, long* kind);	// ROM 0x00177b08 FindTextRunContainingCharOffset__14TParagraphViewFP8LineInfolPl
	TextObjectRef*	FindTextRunContainingCoordinate(const LineInfo* line, short h, long* tabOffset);	// ROM 0x0017789c FindTextRunContainingCoordinate__14TParagraphViewFP8LineInfosPl
	void		OffsetInRunToBounds(long offset, const LineInfo* line, TextObjectRef run, long kind, Rect* bounds);	// ROM 0x00178104 OffsetInRunToBounds__14TParagraphViewFlP8LineInfoN21P5TRect
	void		FlushWordAtCaret(void);									// ROM 0x00174c7c FlushWordAtCaret__14TParagraphViewFv
	// Which way the caret has gone out of a rectangle, which is what tells
	// a scrolling view where to scroll to: 0 it has not gone out (and 0
	// for a view that does not hold the caret at all), 1 it is inside, 2
	// below, 3 above, 4 left of it, 5 right of it.  The line cache is
	// asked first - the caret's character offset against the first and
	// last cached lines - and the view's own bounds when there are no
	// lines cached; only when neither has it out is the root view asked
	// where the caret actually is.
	long		CaretRelativeToVisibleRect(const Rect& visible);			// ROM 0x00171e8c CaretRelativeToVisibleRect__14TParagraphViewFRC5TRect

	// editing
	void		HandleReplaceText(RefArg cmd);							// ROM 0x0016ef00 HandleReplaceText__14TParagraphViewFRC6RefVar
	Boolean		ScrubHilite(const Rect& bounds);							// ROM 0x00173ea8 ScrubHilite__14TParagraphViewFRC5TRect - a scrub over the selection deletes it
	long		ScrubLines(const Rect& bounds, TUnitPublic* unit, Boolean reallyDoIt);	// ROM 0x001748b8 ScrubLines__14TParagraphViewFRC5TRectP11TUnitPublicUc
	long		ScrubWords(const Rect& bounds, TUnitPublic* unit, Boolean reallyDoIt);	// ROM 0x001740c4 ScrubWords__14TParagraphViewFRC5TRectP11TUnitPublicUc
	// The caret gesture's insertion.  `width` is how wide the caret was (-1
	// for a plain one, which is a single space); `height` how tall, which is
	// what says how many line breaks to put in when there is no width.
	long		InsertHorizontalSpace(Point& pt, long width, long height, Boolean typed);	// ROM 0x00175dac InsertHorizontalSpace__14TParagraphViewFR6TPointlT2Uc
	long		InsertVerticalSpace(Point& pt, long height);			// ROM 0x001764c4 InsertVerticalSpace__14TParagraphViewFR6TPointl
	// A caret drawn over a word of writing cuts it in two rather than
	// opening space in the text.
	// The word put into the text where the Finder says.
	void		AddWord(Finder* finder, const UniChar* text, ULong length,
						RefArg info, long* outOffset);		// ROM 0x00172eb4 AddWord__14TParagraphViewFP6FinderPCUsUlRC6RefVarPl
	// Where a word written on the page goes in the text.
	void		FindWordInParagraph(Finder* finder);		// ROM 0x0017348c FindWordInParagraph__14TParagraphViewFP6Finder
	Boolean		FindWordInRun(Finder* finder);				// ROM 0x00173668 FindWordInRun__14TParagraphViewFP6Finder
	// A character written over a character of the text replaces it.
	Boolean		ReplaceCharacter(const LineInfo* line, const TextObjectRef run, Finder* finder);	// ROM 0x00174e14 ReplaceCharacter__14TParagraphViewFPC8LineInfoClP6Finder
	void		SetFinderBelowParagraph(Finder* finder);	// ROM 0x001735e4 SetFinderBelowParagraph__14TParagraphViewFP6Finder
	long		FindTab(Finder* finder, long x);			// ROM 0x00173ea0 FindTab__14TParagraphViewFP6Finderl - always 0
	long		NearTabStop(long x);						// ROM 0x00173cc4 NearTabStop__14TParagraphViewFl
	Boolean		PreviousLineNeedsCR(UniChar* text, UniChar* word);	// ROM 0x00173268 PreviousLineNeedsCR__14TParagraphViewFPUsT1 - always false

	// Where a word written on the page falls in relation to this
	// paragraph, which is what says whether it belongs to it.
	Boolean		WordOnLastLine(const Rect& box);			// ROM 0x00172008 WordOnLastLine__14TParagraphViewFRC5TRect
	void		BoundsOfLastLine(Rect* bounds);				// ROM 0x001721ac BoundsOfLastLine__14TParagraphViewFP5TRect
	long		OffsetPastVisible(void);					// ROM 0x0016ba24 OffsetPastVisible__14TParagraphViewFv - where the laid-out lines stop, -1 when they hold it all
	Boolean		WordOnLineBelowParagraph(const Rect& box, const Point& base);	// ROM 0x0017207c WordOnLineBelowParagraph__14TParagraphViewFRC5TRectRC6TPoint

	long		CheckAndDoSplitInk(Point& pt, long offset);	// ROM 0x00176208 CheckAndDoSplitInk__14TParagraphViewFR6TPointl
	long		CheckAndDoJoin(Point& armA, Point& point, Point& armB);	// ROM 0x00175964 CheckAndDoJoin__14TParagraphViewFR6TPointN21
	void		AddSpaceToEnd(long returns);							// ROM 0x001768d8 AddSpaceToEnd__14TParagraphViewFl
	void		AdjustInsertAreasAfterDeletion(CList* list, ULong offset, ULong length);	// ROM 0x001769d4 AdjustInsertAreasAfterDeletion__14TParagraphViewFP13InsertRunListUlT2
	void		AdjustInsertAreasAfterInsertion(CList* list, ULong offset, ULong length, Boolean onlyWhiteSpace);	// ROM 0x00176af0 AdjustInsertAreasAfterInsertion__14TParagraphViewFP13InsertRunListUlT2Uc
	void		RemoveExcessWhiteSpace(InsertRun* run);				// ROM 0x0017e5c4 RemoveExcessWhiteSpace__14TParagraphViewFP9InsertRun
	// The line nearest a point's v: the ROM measures each line's box less
	// the leading it carries, which this cache does not keep apart, so the
	// box's top is what is measured.  ==> its index, -1 for none, and -1
	// too when the point is further past the last line than the line
	// spacing.
	long		FindClosestBaseline(short v);							// ROM 0x00178548 FindClosestBaseline__14TParagraphViewFs
	// The line a word written in the box belongs to: the box's middle is
	// tried first (flag 1), then its top (2), then its bottom (4), and
	// whichever baseline is nearest wins.
	long		FindLineForWord(const Rect& box, long flags);			// ROM 0x00175840 FindLineForWord__14TParagraphViewFRC5TRectl
	Boolean		ScrubCharacter(const LineInfo* line, TextObjectRef run, const Rect& bounds, long* outOffset);	// ROM 0x00174808 ScrubCharacter__14TParagraphViewFP8LineInfolRC5TRectPl
	// The word a point is in (the word breaks round the character under it,
	// or a tab on its own), and the boundary of it the point is nearest
	// (bias: the percentage of the word's width past which the end is
	// nearer); the line (its index), the text object and whether it was a
	// tab answered too.
	Boolean		PointToWord(const Point& pt, long* start, long* end, long margin,
							long* outLine, TextObjectRef* outRun, Boolean* outTab);	// ROM 0x001776f0 PointToWord__14TParagraphViewFRC6TPointPlT210MarginSizePP8LineInfoT2PUc
	long		PointToWordBoundary(Point pt, long margin, long bias, long* outLine,
									TextObjectRef* outRun, Boolean* outTab);	// ROM 0x00177dcc PointToWordBoundary__14TParagraphViewF6TPoint10MarginSizelPP8LineInfoPlPUc - -1 for no word there
	void		DeleteHilitedTextOnly(RefArg hilite);					// ROM 0x00174dbc DeleteHilitedTextOnly__14TParagraphViewFRC6RefVar
	// Things put into the paragraph from outside - a recognised word,
	// a dropped clipping, an ink word split off another - which the
	// view is sent as command 0x4d.
	Boolean		InkWordCommand(RefArg cmd);				// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0x134c (aeInkWord)
	virtual long	HandleInkWord(RefArg cmd, Boolean reallyDoIt);	// ROM 0x001722a4 HandleInkWord__14TParagraphViewFRC6RefVarUc
	Boolean		WordCommand(RefArg cmd);				// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0xbc (aeWord)
	// Deferred recognition (Rerecognize.h): the ink word at the command's
	// start read again and replaced by what it says, and every ink word
	// of a range read again the same way.
	Boolean		RecognizeInkCommand(RefArg cmd);		// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0xc04 (command 0x19)
	Boolean		RecognizeRangeCommand(RefArg cmd);		// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar +0x358 (command 0x1a)
	// The characters the line cache covers: the first line's start and
	// the length to the last line's end (nought and nought with no lines).
	void		GetCachedRange(long* start, long* length);	// ROM 0x001690b4 GetCachedRange__14TParagraphViewFPlT1
	Boolean		HandleInsertItems(RefArg spec);			// ROM 0x001700a0 HandleInsertItems__14TParagraphViewFRC6RefVar

	void		InsertStyledText(ULong offset, const UniChar* text, ULong length, RefArg styles, RefArg correctInfo, ULong styleOffset, ULong removeLength, Boolean typed);	// ROM 0x00178a3c InsertStyledText__14TParagraphViewFUlPCUsT1RC6RefVarT4N21Uc
	// A bundle of strokes put into the text as one ink word: the
	// character 0xf701, whose style is the ink itself.
	void		InsertInk(ULong offset, RefArg bundle, ULong removeLength);	// ROM 0x001814c0 InsertInk__14TParagraphViewFUlRC6RefVarT1
	void		RemoveText(ULong offset, ULong length);					// ROM 0x00178b98 RemoveText__14TParagraphViewFUlT1
	void		MakeAndDoReplaceCommand(ULong offset, const UniChar* text, ULong length, RefArg styles, RefArg correctInfo, ULong styleOffset, ULong removeLength, Boolean typed);	// ROM 0x00178d5c MakeAndDoReplaceCommand__14TParagraphViewFUlPCUsT1RC6RefVarT4N21Uc
	Boolean		AddKeyToCurrUndo(UniChar ch, long offset);				// ROM 0x00177218 AddKeyToCurrUndo__14TParagraphViewFUsl
	void		AdjustStyles(long offset, long removed, long inserted, RefArg styles, long styleOffset);	// ROM 0x00178ed4 AdjustStyles__14TParagraphViewFlN21RC6RefVarT1
	void		AdjustHilites(long offset, long delta);					// ROM 0x0016a824 AdjustHilites__14TParagraphViewFlT1
	void		UpdateHiliteArea(void);									// ROM 0x0016a7bc UpdateHiliteArea__14TParagraphViewFv
	void		ChangeStyleOfSelection(RefArg style);					// ROM 0x00179a68 ChangeStyleOfSelection__14TParagraphViewFRC6RefVar - the selected text restyled
	void		ChangeStylesOfRange(long start, long length, RefArg style, Boolean redraw);	// ROM 0x00179464 ChangeStylesOfRange__14TParagraphViewFlT1RC6RefVarUc
	// What a hilite stroke over the paragraph selects, in the order the
	// four kinds are tried.
	virtual void	HiliteAll(void);									// ROM 0x00169d8c HiliteAll__14TParagraphViewFv - the whole of the text
	virtual long	HandleHilite(TUnitPublic* unit, long kind, Boolean reallyDoIt);	// ROM 0x00169b0c HandleHilite__14TParagraphViewFP11TUnitPubliclUc
	Boolean		HiliteWords(TUnitPublic* unit, Boolean reallyDoIt);		// ROM 0x0016a400 HiliteWords__14TParagraphViewFP11TUnitPublicUc - answers nothing, as the ROM does
	Boolean		HiliteParagraph(TUnitPublic* unit, Boolean reallyDoIt);	// ROM 0x00169c0c HiliteParagraph__14TParagraphViewFP11TUnitPublicUc
	Boolean		HiliteLines(TUnitPublic* unit, Boolean reallyDoIt);		// ROM 0x00169da0 HiliteLines__14TParagraphViewFP11TUnitPublicUc
	Boolean		HiliteRange(TUnitPublic* unit, Boolean reallyDoIt);		// ROM 0x00169ec4 HiliteRange__14TParagraphViewFP11TUnitPublicUc
	long		FindFirstWordHitByHilite(const Point* points, long count, Point offset, Boolean fromEnd);	// ROM 0x00169fbc FindFirstWordHitByHilite__14TParagraphViewFP6TPointl6TPointUc
	void		MakeHilite(long start, long end, Boolean caretOnEmpty);	// ROM 0x0016a49c MakeHilite__14TParagraphViewFlT1Uc - select the characters between the offsets
	void		DrawHilites(Boolean scaled);							// ROM 0x0016aecc DrawHilites__14TParagraphViewFUc - invert the hilited text (host: over the current port)
	void		SetupArea(TParagraphHilite* hilite);					// ROM 0x0016a744 SetupArea__14TParagraphViewFP16TParagraphHilite - the region a hilite covers, worked out once
	void		Area(long start, long end, RgnHandle area);			// ROM 0x0016a92c Area__14TParagraphViewFlT1 - the region a range of the text covers (host: into `area`)
	// The word under a point: where it starts and where it sits; ==> how
	// long it is, 0 when there is no word there.
	long		FindWordOffset(Point pt, long* offset, Point* where);	// ROM 0x00177cbc FindWordOffset__14TParagraphViewF6TPointPlP6TPoint
	Boolean		SelectWordAt(Point pt);									// the word under the point selected (the ROM's aeDoubleTap case of RealDoCommand at 0x0016e688, over ScanWordStart/End 0x001a37d0/0x001a36b4)
	Ref			GetStyleForInsertion(long offset, Boolean useNextStyle, Boolean skipWhiteSpace);	// ROM 0x00178748 GetStyleForInsertion__14TParagraphViewFlUcT2
	Ref			GetStyleAtOffset(long offset, long* run, long* offsetInRun);	// ROM 0x001791a8 GetStyleAtOffset__14TParagraphViewFlPlT2
	// The ink word at an offset, if the style there is one, and the box
	// the view draws it in; nil when the character there is ordinary text.
	Ref			GetInkRefAndBounds(long offset, Rect* bounds);			// ROM 0x00178210 GetInkRefAndBounds__14TParagraphViewFlP5TRect
	Ref			GetStylesOfRange(long offset, long length, Boolean clone);	// ROM 0x001791f8 GetStylesOfRange__14TParagraphViewFlT1Uc
	Ref			ExtractTextRange(ULong offset, ULong length);			// ROM 0x001726a4 ExtractTextRange__14TParagraphViewFUlT1 - the characters as a plain string
	Ref			GetWriteableTextStylesArray(void);						// ROM 0x00179248 GetWriteableTextStylesArray__14TParagraphViewFv
	void		RangeChanged(long offset, long removed, long inserted, RefArg slot);	// ROM 0x00180bd8 RangeChanged__14TParagraphViewFlN21RC6RefVar
	Boolean		ProcessStyles(Boolean redraw);							// ROM 0x00180ce4 ProcessStyles__14TParagraphViewFUc
	void		HandleUpDownKey(Boolean up);							// ROM 0x0016c658 RealDoCommand__14TParagraphViewFRC6RefVar (its up and down arrows, 0x0016e254-0x0016e590)
	Boolean		CheckStyles(void);										// ROM 0x001804d4 CheckStyles__14TParagraphViewFv - whether the styles hold an ink word (fHasInkWords), and whether any face is italic, outlined or shadowed (fHasHeavyFaces)
	void		FixupBBox(void);										// ROM 0x001815b8 FixupBBox__14TParagraphViewFv
	long		TextLength(void);										// the text's characters (host)

	// The line layout (ParagraphLines.cpp).
	long		LineFitsInBounds(long lineTop, long baseline, long spacing, StyleRecord* style);	// ROM 0x001721fc LineFitsInBounds__14TParagraphViewFlN21P11StyleRecord
	void		CreateStyleRecordCache(short** runLengths);				// ROM 0x0016c2f0 CreateStyleRecordCache__14TParagraphViewFPPs
	void		DestroyStyleRecordCache(void);							// ROM 0x0016c5ec DestroyStyleRecordCache__14TParagraphViewFv

	// the cached lines: as many as the cache holds, and each
	long		LineCount(void) const;
	LineInfo&	Line(long index) const				{ return *fLineCache[index]; }
	const Rect&	TextBounds(void) const				{ return fTextBounds; }

	long		fTextFlags;			// +0x30  the input view's text flags (-1 until SetupDone)
	long		fTransferMode;		// +0x34  viewTransferMode (srcOr when none)
	long		fLineSpacing;		// +0x38  viewLineSpacing (0 when none)
	long		fLineHeight;		// +0x3c  the default style's height (ascent + descent + leading), then the last line's
	Rect		fTextBounds;		// +0x40  the lines' union (FillAllCaches)
	Boolean		fHasInkWords;		// +0x48  CheckStyles: an ink word among the styles
	Boolean		fHasHeavyFaces;		// +0x49  CheckStyles: a face with italic, outline or shadow (0x1a) - drawn past its advances
	CList*		fInsertRunList;		// +0x4c  the stretches caret gestures opened (InsertRun), made by SetupDone
	Boolean		fInsertAreasChanged;	// +0x50  one of them was written into: Idle reason 1 is due
	ULong		fInsertAreasTime;	// +0x54  when (Ticks)
	Boolean		fCalculateBounds;	// +0x58  vCalculateBounds is set
	Boolean		fTapped;			// +0x59  a tap is pending the double-tap interval (Idle reason 2 runs it)
	Point		fTapPoint;			// +0x5c  where the tap was
	long		fCaretOffset;		// +0x60  the caret's character offset (SetCaretOffset)
	RefStruct	fWordBreakTable;	// +0x64  the locale's
	RefStruct	fLineBreakTable;	// +0x68
	StyleRecord**	fStyleCache;	// +0x6c  the runs' style records, nil-ended (fSingleStyles for one)
	TextObjectRef*	fTextObjects;	// +0x70  the text objects of the lines, nought-ended
	LineInfo**	fLineCache;			// +0x74  the lines that show, nil-ended (nil until made)
	Boolean		fCachesValid;		// +0x78  the caches may be filled (set by SetupDone; cleared while the
									//        view's bounds are being written by the layout itself)
	Boolean		fSetupDone;			// +0x79  SetupDone has run (RangeChanged processes the styles; cleared while it does)
	short		fFirstBaselineOffset;	// +0x7c  the first line's baseline below the top, kept from one
										//        layout to the next (LineLoop)
	Point		fTextOrigin;		// +0x80  the first text object's baseline from the top-left
									//        (OffsetCachedBounds moves the lines by its change)
	TextOptions	fTextOptions;		// +0x84  the width and alignment the lines are laid out with
	short		fFirstBaseline;		// +0xa0  the first line's baseline
	short		fFirstLineAscent;	// +0xa2  its fonts' line ascent
	short		fLastBaseline;		// +0xa4  the last line's baseline
	short		fLastLineDescent;	// +0xa6  its fonts' line descent
	StyleRecord	fSingleStyle;		// +0xa8  the one style, when there are no runs
	StyleRecord*	fSingleStyles[2];	// +0xc8  the cache for it: it and nil
};

// Text with its tabs and returns made single spaces (a run of them one
// space, one at the end none); nil when there are none.
long		LengthSansTabsAndCRs(const UniChar* text, Boolean* found);	// ROM 0x0017aefc LengthSansTabsAndCRs__FPUsPUc
UniChar*	RemoveTabsAndCRs(const UniChar* text, RefArg styles);		// ROM 0x0017ad6c RemoveTabsAndCRs__FPUsRC6RefVar

// The insert areas (ParagraphInsertAreas.cpp).
void		SaveInsertArea(CList* list, ULong offset, ULong length);	// ROM 0x00176818 SaveInsertArea__FP13InsertRunListUlT2
Boolean		ContainsOnlyInsertedWhiteSpace(const UniChar* text, ULong length);	// ROM 0x0017a2d0 ContainsOnlyInsertedWhiteSpace__FPUsUl
Boolean		FindPreviousWhiteSpaceBlock(const UniChar* from, const UniChar* limit,
										const UniChar** block, ULong* length);	// ROM 0x0017af5c FindPreviousWhiteSpaceBlock__FPUsT1PPUsPUl
extern ULong	gLastParagraphClick;								// ROM 0x0c101760 (unnamed)

extern Boolean	gRemoveEmptyParagraph;						// ROM 0x0c101735

// Whether the view is drawn inside a print view or a remote view (a
// print preview, a page's thumbnail) - somewhere below one of them.
Boolean	InPrintOrPreview(TView* view);						// ROM 0x00180c70 InPrintOrPreview__FP5TView

Boolean	RangesIntersect(long start1, long end1, long start2, long end2, long* start, long* end);	// ROM 0x0016a8e8 RangesIntersect__FlN31PlT5

// The view as a paragraph, or a throw saying that it is not one.
TParagraphView*	FailGetParagraphView(RefArg context);				// ROM 0x001ee218 FailGetParagraphView__FRC6RefVar

// The horizontal justification a piece of dropped or written text asks
// for: the low two bits of its viewJustify, but only when the view it
// came from was told to calculate its own bounds (vCalculateBounds).
// ==> 0 when it asks for nothing, 4 when it has a justification the
// caller should ignore.
long	GetJustificationOfDroppedText(RefArg info);		// ROM 0x000a2e24 GetJustificationOfDroppedText__FRC6RefVar

// The context frame of a new paragraph: a clone of the ROM's
// starterParagraph with the bounds and the text put in, and whatever the
// `info` frame says about how it should look.
Ref		MakeParagraphForm(UniChar* text, long length, const Rect& bounds,
						  RefArg info, Boolean flag);		// ROM 0x0017a4d8 MakeParagraphForm__FPUslRC5TRectRC6RefVarUc

// The command a view is sent to have things put into it, and the two
// ways of sending it.
enum { kInsertItemsCommand = 0x4d };
void	TimeStampHiliteChange(TView* view);				// ROM 0x0016a408 TimeStampHiliteChange__FP5TView
void	UpdateStylePalette(void);						// ROM 0x0017b108 UpdateStylePalette__Fv

Ref		DoInsertItems(TView* view, RefArg items, Boolean addSpace, Boolean undoable,
					  long insertOffset, long replaceChars, Boolean moveCaret,
					  RefArg defaultFontSpec);	// ROM 0x00170f7c DoInsertItems__FP5TViewRC6RefVarUcT3lT5T3T2
Boolean	InsertItemsAtCaret(RefArg spec);			// ROM 0x00171168 InsertItemsAtCaret__FRC6RefVar

// What goes between two pieces of text being joined: a space, or
// nothing.
void	GetAppendDelimiter(UniChar* out, const UniChar* left, const UniChar* right,
						   const ULong leftLength, const ULong rightLength);	// ROM 0x000edc24 GetAppendDelimiter__FPUsPCUsT2CUlT4
// Whether two style runs are the same style.
Boolean	EqualStyles(RefArg a, RefArg b);			// ROM 0x0016fa08 EqualStyles__FRC6RefVarT1
// The frame the corrector keeps its alternatives in.
Ref		NewCorrectInfo(void);						// ROM 0x0007623c NewCorrectInfo__Fv

#endif	/* __PARAGRAPHVIEW_H */
