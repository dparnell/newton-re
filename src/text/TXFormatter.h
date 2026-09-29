/*
	File:		text/TXFormatter.h

	Contains:	The formatter: styled text broken into lines.

				`TXFormatter` keeps where every line of a document ends (a
				TXRanges of line ends: line i is [end i-1, end i)) and
				hands each line's height to the frames' formatter
				(TXFrameFormatter.h, the lines' heights).  `BreakLine`
				finds where one line ends: it walks the runs from the
				line's start (a TXObjectIterator over the runs) and asks
				each run to fit its characters into the room left
				(`BreakRun` -> `BreakVisibleChars` -> TXRun::LineBreak),
				stepping over the control characters as it goes - a tab
				takes the width the ruler gives it, a tab whose width
				waits on what follows is settled as TXLine does
				(`BreakCtrlChar`, `BreakAlignTabChars`) - and no line is
				longer than 128 characters.  The line's height is the
				tallest ascent, descent and leading of its runs, and the
				paragraph's ruler adjusts it for the line spacing.

				`Format(start, end)` formats the lines a stretch touches:
				the whole text (`FormatAll`), or from the line the stretch
				starts in (or the one before, when that line does not end
				a paragraph, since an edit may pull its first word back)
				until a line comes out ending where it used to, past the
				stretch's end (`FormatRange`) - lines are inserted when
				the text now wraps onto more of them and removed when onto
				fewer (`RemoveFormattedLines`).  `ReplaceRange` is what an
				edit calls: the line ends after it moved by what was put in
				or taken out, the lines inside what was taken out removed,
				and the rest reformatted.  A text that ends in a line break
				always has an empty last line (`AppendEmptyLine`: a run's
				height, or 12 with an ascent of 9 when there is none).

				The ROM's object is 0x48 bytes.  The multi-frame side
				(`CheckFramesReflow`) is here, for a
				TXMultiFrameFormatter (TXFrameFormatter.h) to ask for.

	Reconstructed from the MP2x00 US ROM (0x00237540-0x002390d4); each
	function cites its origin.
*/

#ifndef __TXFORMATTER_H
#define __TXFORMATTER_H

#ifndef __TXFRAMES_H
#include "TXFrames.h"
#endif
#ifndef __TXRUN_H
#include "TXRun.h"
#endif

class TXStyledText;
class TXRulerRange;
class TXStream;

// A line as BreakLine works it out.  The ROM's is 12 bytes; the line
// ranges keep only its first word.
struct TXLineInfo
{
	TXOffset		fEnd;			// +0x00
	TXLineHeightInfo fHeight;		// +0x04
};

// Where formatting has got to.
struct TXFormattingInfo
{
	long			fLine;			// +0x00  the line (-1: a new last one)
	TXOffset		fOldEnd;		// +0x04  where it ended before
	TXOffset		fEnd;			// +0x08  how far the formatting must go
};

typedef TXObjectIterator	TXRunsIterator;


class TXFormatter : public TXVirtualObject
{
public:
					TXFormatter();									// ROM 0x00237540 __ct__11TXFormatterFv
	virtual			~TXFormatter();									// ROM 0x0023758c __dt__11TXFormatterFv - the line ends deleted

	virtual void	SetLineInfo(TXLineInfo* line, TXFormattingInfo* info, Boolean flag);	// ROM 0x00238ff8 SetLineInfo__11TXFormatterFP10TXLineInfoP16TXFormattingInfoUc
	virtual NewtonErr InsertLine(TXLineInfo* line, TXFormattingInfo* info);	// ROM 0x00238f3c InsertLine__11TXFormatterFP10TXLineInfoP16TXFormattingInfo
	// Where the line from `start` ends in `width` pixels (0: 0x7fff).
	virtual void	BreakLine(TXOffset start, long width, TXRunsIterator* runs, TXLineInfo* line);	// ROM 0x00237bf8 BreakLine__11TXFormatterFlT1P14TXRunsIteratorP10TXLineInfo
	virtual long	BreakVisibleChars(const UniChar* text, TXOffset lineStart, TXOffset at, long count, Fixed* width, TXRun* run);	// ROM 0x0023770c BreakVisibleChars__11TXFormatterFPCUslN22PlP5TXRun
	virtual long	BreakRun(const UniChar* text, TXOffset lineStart, TXOffset at, long count, Fixed* width, TXRun* run);	// ROM 0x00237970 BreakRun__11TXFormatterFPCUslN22PlP5TXRun
	virtual Boolean	BreakCtrlChar(TXOffset lineStart, TXOffset at, Fixed* width);	// ROM 0x00237798 BreakCtrlChar__11TXFormatterFlT1Pl

	// The styled text, the frames and the rulers the formatter works for;
	// `kind` 2 is a document's (the line ends grow twenty at a time).
	void			SetHandlers(TXStyledText* text, TXFrames* frames, TXRulerRange* rulers, char kind);	// ROM 0x002380b0 SetHandlers__11TXFormatterFP12TXStyledTextP8TXFramesP12TXRulerRangec
	// The lines of [start, end) formatted (end -1: the text's end);
	// `*first`/`*last` the lines that changed.
	NewtonErr		Format(TXOffset start, TXOffset end, long* first, long* last);	// ROM 0x0023849c Format__11TXFormatterFlT1PlT3
	NewtonErr		FormatAll(void);								// ROM 0x0023811c FormatAll__11TXFormatterFv
	NewtonErr		FormatRange(TXOffset start, TXOffset end, long* first, long* last);	// ROM 0x00238574 FormatRange__11TXFormatterFlT1PlT3
	// `oldLength` characters at `start` became `newLength`.  (Unlike Format,
	// `first` and `last` must be given: the ROM writes through `first`.)
	NewtonErr		ReplaceRange(TXOffset start, long oldLength, long newLength, unsigned long flags, long* first, long* last);	// ROM 0x00238844 ReplaceRange__11TXFormatterFlN21UlPlT5 - ==> Format's error
	// The rulers' margins and tabs brought within the width: a margin
	// leaving less than 50 pixels goes, and so does a tab past the edge.
	NewtonErr		CheckRulerSettings(void);						// ROM 0x0023825c CheckRulerSettings__11TXFormatterFv
	Boolean			AppendEmptyLine(void);							// ROM 0x00237f78 AppendEmptyLine__11TXFormatterFv
	void			RemoveLines(long line, long count, TXFormatReflowLines* reflow);	// ROM 0x002375d8 RemoveLines__11TXFormatterFlT1P19TXFormatReflowLines
	// The lines after `line` that end no further than it now does;
	// ==> where the last of them ended, or -1 for none.
	TXOffset		RemoveFormattedLines(long line, TXFormatReflowLines* reflow);	// ROM 0x00237640 RemoveFormattedLines__11TXFormatterFlP19TXFormatReflowLines
	Boolean			IsLineFeed(TXOffset offset) const;				// ROM 0x002376c8 IsLineFeed__11TXFormatterCFl
	long			BreakAlignTabChars(const UniChar* text, TXOffset lineStart, TXOffset at, long count, Fixed* width, TXRun* run);	// ROM 0x00237894 BreakAlignTabChars__11TXFormatterFPCUslN22PlP5TXRun - ==> the characters placed
	void			CalcRunsHeight(TXOffset start, long count, TXRunsIterator* runs, TXLineHeightInfo* info);	// ROM 0x00237b28 CalcRunsHeight__11TXFormatterFlT1P14TXRunsIteratorP16TXLineHeightInfo
	void			CalcLinesHeights(void);							// ROM 0x00238c1c CalcLinesHeights__11TXFormatterFv
	void			CheckFramesReflow(const TXFormatReflowLines& reflow, TXFormattingInfo* info, TXOffset* lineEnd);	// ROM 0x00238d6c CheckFramesReflow__11TXFormatterFRC19TXFormatReflowLinesP16TXFormattingInfoPl
	void			GetLineRange(long line, TXOffsetRange* range) const;	// ROM 0x00238f00 GetLineRange__11TXFormatterCFlP13TXOffsetRange
	// Every line gone; `appendEmpty` puts the empty last line back.
	long			FreeData(Boolean appendEmpty);					// ROM 0x00238cfc FreeData__11TXFormatterFUc
	NewtonErr		Compact(void);									// ROM 0x00238d40 Compact__11TXFormatterFv
	NewtonErr		ReserveLines(long count);						// ROM 0x00238d64 ReserveLines__11TXFormatterFl
	NewtonErr		WriteToStream(TXStream* stream);				// ROM 0x00238a90 WriteToStream__11TXFormatterFP8TXStream
	NewtonErr		ReadFromStream(TXStream* stream);				// ROM 0x00238b38 ReadFromStream__11TXFormatterFP8TXStream

	TXStyledText*	fText;			// +0x04
	TXRulerRange*	fRulers;		// +0x08
	TXFrames*		fFrames;		// +0x0c
	TXFrameFormatter* fFrameFormatter;	// +0x10  the frames'
	TXRanges*		fLineEnds;		// +0x14
	TXRunRange*		fRuns;			// +0x18  the styled text's
	Boolean			fNoWrap;		// +0x1c  a paragraph is one line
	long			fSuspended;		// +0x20  negative: Format does nothing
	TXRuler*		fRuler;			// +0x24  the paragraph being broken's
	Fixed			fLineWidth;		// +0x28  the room between the margins
	long			fLastLine;		// +0x2c  -1: none
	TXPendingTab	fPending;		// +0x30  a tab waiting on what follows it
	Fixed			fSincePending;	// +0x40  what has been placed since
	Fixed			fLeft;			// +0x44  the left margin
};

#endif	/* __TXFORMATTER_H */
