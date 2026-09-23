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
				and the drawn text objects; the host keeps the lines.

				Display, and the caret: the paragraph can be the key view -
				SetCaretOffset keeps its caret offset (fCaretOffset), and
				OffsetToCaret/PointToCaret place the caret from the line
				cache (host: the text measured up to the offset; the ROM
				asks its text objects - CharBounds).  Typing: the key
				commands (RealDoCommand) insert and delete at the caret
				through InsertStyledText, which makes an aeReplaceText
				command (MakeAndDoReplaceCommand) that HandleReplaceText
				carries out - the text munged, the style runs adjusted
				(AdjustStyles), the inverse posted for undo (consecutive
				keys merged by AddKeyToCurrUndo), the caret moved, the lines
				laid out again (RangeChanged).  NOT YET RECONSTRUCTED: the
				hilites (a selection typed over), ink words and the
				recogniser's word handling (the pen gestures are here:
				HandleScrub, ScrubLines, ScrubWords, HandleCaret,
				InsertHorizontalSpace, InsertVerticalSpace, CheckAndDoJoin,
				HandleLineGesture), the correction info, the other edit
				commands (styles changed, cut and paste), the tab stops
				(tabs draw as characters), the text objects (each line is
				laid out from the text when drawn), the bounds recalculation
				of vCalculateBounds paragraphs (the lines are all laid out;
				the view keeps its bounds), the locale's break tables in
				the word breaks (see FindWordBreaks), printing.

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

// one line of the paragraph as FillAllCaches (0x0016dc68) records it: the
// ROM's LineInfo is 0x24 bytes - the offsets of the line's first
// character and of the one after its last, its first text object and the
// one after its last, whether it ends in white space, and its box
struct LineInfo
{
	long		fStart;				// +0x00  the line's first character
	long		fEnd;				// +0x04  where the next line starts: after
									//        the spaces and the return that
									//        end this one, as the ROM's
									//        LineLoop leaves it
	long		fTextEnd;			// (host) after the line's last drawn
									//        character - what the ROM's text
									//        objects hold, and what this
									//        reconstruction measures instead
	long		fFirstObj;			// +0x08  its first text object (host: the first style run)
	long		fEndObj;			// +0x0c  after its last
	Boolean		fEndsWithSpace;		// +0x10
	long		fAscent;			// +0x14  the baseline below the line's top
	long		fHeight;			// +0x18  the line's height
	Rect		fBounds;			// +0x1c  its box (the text's width)
};

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

class TParagraphView : public TDataView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x0017e3ac ClassID__14TParagraphViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x0017e3b4 DerivedFrom__14TParagraphViewCFl
	virtual			~TParagraphView();									// ROM 0x001805f4 __dt__14TParagraphViewFv
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x0017edc0 Constructor__14TParagraphViewFRC6RefVarP5TView
	virtual void	SetupDone(void);									// ROM 0x0017f5d8 SetupDone__14TParagraphViewFv
	virtual long	Idle(long reason);									// ROM 0x0017e964 Idle__14TParagraphViewFl - reason 2 runs a deferred tap
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
	virtual void	PointToCaret(Point& pt, Rect* caret, Rect* bounds);	// ROM 0x001716c8 PointToCaret__14TParagraphViewFR6TPointP5TRectT2

	Ref			Text(void);												// ROM 0x00181004 Text__14TParagraphViewFv
	Ref			Styles(void);											// ROM 0x00181448 Styles__14TParagraphViewFv
	Ref			GetStyles(void);										// ROM 0x00181104 GetStyles__14TParagraphViewFv
	Ref			Tabs(void);												// ROM 0x001814b4 Tabs__14TParagraphViewFv
	Ref			GetDefaultViewStyle(void);								// ROM 0x001789bc GetDefaultViewStyle__14TParagraphViewFv
	long		GetInterLineSpacing(void);								// ROM 0x00169460 GetInterLineSpacing__14TParagraphViewFv
	void		CreateAllCaches(void);									// ROM 0x0016baa8 CreateAllCaches__14TParagraphViewFv
	void		ClearAllCaches(void);									// ROM 0x0016bbd0 ClearAllCaches__14TParagraphViewFv
	void		RefillAllCaches(void);									// ROM 0x0016c25c RefillAllCaches__14TParagraphViewFv
	void		FillAllCaches(void);									// ROM 0x0016bc38 FillAllCaches__14TParagraphViewFPs
	void		OffsetCachedBounds(Point& delta);						// ROM 0x0016991c OffsetCachedBounds__14TParagraphViewFR6TPoint
	long		FindLineContainingCharOffset(long offset);				// ROM 0x001786f8 FindLineContainingCharOffset__14TParagraphViewFl (host: the line's index, -1 for none)
	void		OffsetToBounds(long offset, Rect* bounds);				// ROM 0x00177f20 OffsetToBounds__14TParagraphViewFlP5TRect
	long		PointToOffset(const Point& pt);							// ROM 0x00177520 PointToOffset__14TParagraphViewFRC6TPoint10MarginSizeUcP5TRectPP8LineInfoPlPUc (host: the nearest character)
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
	long		CheckAndDoJoin(Point& armA, Point& point, Point& armB);	// ROM 0x00175964 CheckAndDoJoin__14TParagraphViewFR6TPointN21
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
	Boolean		ScrubCharacter(long line, const Rect& bounds, long* outOffset);	// ROM 0x00174808 ScrubCharacter__14TParagraphViewFP8LineInfolRC5TRectPl (host: no text objects - see the definition)
	// The word a point is in, and the boundary of it the point is nearest.
	// (host: the ROM asks its text objects, and answers the line, the run
	// and whether the character is a tab; here the line's index is enough
	// and a tab is a character like any other.)
	Boolean		PointToWord(const Point& pt, long* start, long* end, long* outLine);	// ROM 0x001776f0 PointToWord__14TParagraphViewFRC6TPointPlT210MarginSizePP8LineInfoT2PUc
	long		PointToWordBoundary(const Point& pt, long bias, long* outLine);	// ROM 0x00177dcc PointToWordBoundary__14TParagraphViewF6TPoint10MarginSizelPP8LineInfoPlPUc - -1 for no word there
	void		DeleteHilitedTextOnly(RefArg hilite);					// ROM 0x00174dbc DeleteHilitedTextOnly__14TParagraphViewFRC6RefVar
	// Things put into the paragraph from outside - a recognised word,
	// a dropped clipping, an ink word split off another - which the
	// view is sent as command 0x4d.
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
	void		ChangeStyleOfSelection(RefArg style);					// ROM 0x00179a68 ChangeStyleOfSelection__14TParagraphViewFRC6RefVar - the selected text restyled
	void		ChangeStylesOfRange(long start, long length, RefArg style, Boolean redraw);	// ROM 0x00179464 ChangeStylesOfRange__14TParagraphViewFlT1RC6RefVarUc
	void		MakeHilite(long start, long end, Boolean caretOnEmpty);	// ROM 0x0016a49c MakeHilite__14TParagraphViewFlT1Uc - select the characters between the offsets
	void		DrawHilites(Boolean scaled);							// ROM 0x0016aecc DrawHilites__14TParagraphViewFUc - invert the hilited text (host: over the current port)
	void		SetupArea(TParagraphHilite* hilite);					// ROM 0x0016a744 SetupArea__14TParagraphViewFP16TParagraphHilite - the region a hilite covers, worked out once
	Boolean		SelectionRegion(long start, long end, RgnHandle rgn);	// host: the region covering a range of the text
	Boolean		SelectWordAt(Point pt);									// the word under the point selected (the ROM's aeDoubleTap case of RealDoCommand at 0x0016e688, over ScanWordStart/End 0x001a37d0/0x001a36b4)
	Ref			GetStyleForInsertion(long offset, Boolean useNextStyle, Boolean skipWhiteSpace);	// ROM 0x00178748 GetStyleForInsertion__14TParagraphViewFlUcT2
	Ref			GetStyleAtOffset(long offset, long* run, long* offsetInRun);	// ROM 0x001791a8 GetStyleAtOffset__14TParagraphViewFlPlT2
	// The ink word at an offset, if the style there is one, and the box
	// the view draws it in; nil when the character there is ordinary text.
	Ref			GetInkRefAndBounds(long offset, Rect* bounds);			// ROM 0x00178210 GetInkRefAndBounds__14TParagraphViewFlP5TRect
	Ref			GetStylesOfRange(long offset, long length, Boolean clone);	// ROM 0x001791f8 GetStylesOfRange__14TParagraphViewFlT1Uc
	Ref			GetWriteableTextStylesArray(void);						// ROM 0x00179248 GetWriteableTextStylesArray__14TParagraphViewFv
	void		RangeChanged(long offset, long removed, long inserted, RefArg slot);	// ROM 0x00180bd8 RangeChanged__14TParagraphViewFlN21RC6RefVar
	Boolean		ProcessStyles(Boolean redraw);							// ROM 0x00180ce4 ProcessStyles__14TParagraphViewFUc
	void		FixupBBox(void);										// ROM 0x001815b8 FixupBBox__14TParagraphViewFv
	long		TextLength(void);										// the text's characters (host)

	long		LineCount(void) const				{ return fLineCount; }
	const LineInfo&	Line(long index) const			{ return fLines[index]; }
	const Rect&	TextBounds(void) const				{ return fTextBounds; }

	long		fTextFlags;			// +0x30  the input view's text flags (-1 until SetupDone)
	long		fTransferMode;		// +0x34  viewTransferMode (srcOr when none)
	long		fLineSpacing;		// +0x38  viewLineSpacing (0 when none)
	long		fLineHeight;		// +0x3c  the default style's height (ascent + descent + leading), then the last line's
	Rect		fCachedBounds;		// +0x40  the bounds the lines were laid out in
	Boolean		fCalculateBounds;	// +0x58  vCalculateBounds is set
	Boolean		fTapped;			// +0x59  a tap is pending the double-tap interval (Idle reason 2 runs it)
	Point		fTapPoint;			// +0x5c  where the tap was
	long		fCaretOffset;		// +0x60  the caret's character offset (SetCaretOffset)
	RefStruct	fWordBreakTable;	// +0x64  the locale's
	RefStruct	fLineBreakTable;	// +0x68
	LineInfo*	fLines;				// +0x74  the line cache (nil until made)
	long		fLineCount;
	long		fLineCapacity;
	Boolean		fCachesValid;		// +0x78
	Boolean		fSetupDone;			// +0x79  SetupDone has run (RangeChanged processes the styles; cleared while it does)
	TextOptions	fTextOptions;		// +0x84  the width and alignment the lines are laid out with
	Rect		fTextBounds;		// +0xa0  the lines' union

private:
	void		LayoutRuns(RefArg styles, long textLength);
	void		DrawLine(const UniChar* text, const LineInfo& line, Boolean ellipsis);
	void		DisposeRuns(void);

	// host: the style runs as records for DoTextOnce
	StyleRecord**	fRunStyles;
	short*			fRunLengths;
	long			fRunCount;
	RefStruct		fRunSpecs;		// the specs of the runs (an array; a single spec's run covers the text)
};

void	GrowLineInfoCache(LineInfo** cache, long* capacity);			// ROM 0x0017c9cc GrowLineInfoCache__FPPP8LineInfol

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
