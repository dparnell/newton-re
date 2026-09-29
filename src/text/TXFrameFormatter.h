/*
	File:		text/TXFrameFormatter.h

	Contains:	The frame formatters: the lines' heights (TXLinesHeights.h)
				with what the frames the lines are poured into need to know
				about them.

				A *frame* is one rectangle the text flows through - a view
				has one, a paginated document one per page.  The frame
				formatter is what the frames (TXFrames.h) ask how many
				frames there are, which lines are in which
				(`GetFrameLineRange`, `LineToFrame`), how tall each frame
				is and how much of it the text takes, and whether a line
				would overflow its frame (`TestFrameOverflow`).  It also
				keeps a note of the frames an edit touched and how their
				text's height changed (`CatchFrame`, `GetNextFrameEditInfo`,
				over the one `gFramesEditInfo`), so that a display can
				redraw only those.

				`TXMonoFrameFormatter` is the one frame of a view: every
				line is in frame 0, the frame is 0x7fff pixels tall, and
				its text is as tall as all the lines together.

				`TXMultiFrameFormatter` is text poured through a run of
				frames one after another: a TXRanges of the frames, each
				element the line after the frame's last (the range's end)
				and the height of the text in it.  Every change to a
				line's height is added to its frame and, when the frame
				now holds more than it has room for (or less, or a page
				break is involved), `CheckReflow` moves lines on into the
				next frame or back out of it - `MeasureFrame` saying where
				the frame's lines should end now and `BreakFrame` ending
				them there - carrying what moved from frame to frame and
				appending a frame at the end for whatever is left over.
				It also keeps the page breaks: the offsets of the
				character 10s in the text (`CharRangeChanged`), a line
				ending on one ending its frame too (`CheckFrameBreaks`).
				`TXPageFormatter` is the pages of a paginated document:
				frames all one height (`SetFrameHeight`), and `Format`
				lays the lines out into them from scratch.

				The ROM's objects are 0x24 bytes (the lines' heights and a
				pointer to the formatter's line ranges), 0x30 (the
				multi-frame formatter) and 0x34 (the page formatter).

	Reconstructed from the MP2x00 US ROM (0x002396e4-0x00239b4c,
	0x002415b0-0x0024282c); each function cites its origin.
*/

#ifndef __TXFRAMEFORMATTER_H
#define __TXFRAMEFORMATTER_H

#ifndef __TXLINESHEIGHTS_H
#include "TXLinesHeights.h"
#endif

class TXStream;

// What an edit did to one frame.  The ROM's is 0x18 bytes.
struct TXFrameEditInfo
{
	long			fFrame;			// +0x00
	unsigned long	fFlags;			// +0x04  SetEditFlag's
	long			fField08;		// +0x08
	long			fHeightChange;	// +0x0c  the mono formatter's: the text's height before, then how it changed
	long			fField10;		// +0x10
	long			fField14;		// +0x14
};

// The frames an edit touched.  The ROM's is 0x40 bytes - room for two
// frames, and CatchFrame does not look.  ROM BUG: TXDisplay::BeginEdit
// catches every frame in the view, so a paginated view showing three
// pages or more writes the third frame's entry over fFirst, fLast (made
// nought) and fNext, and the fourth and later ones on into the eight
// bytes after the object and gTXParagCtrlChars, which the ROM keeps
// next to it; the frames past the second are then not found, and are
// not redrawn as they should be after an edit.  The host keeps the three
// together as the ROM does (TXFrameFormatter.cpp), so that it goes wrong
// in the same way - up to the eighth frame, which is the end of
// gTXParagCtrlChars.
struct TXFramesEditInfo
{
	TXFrameEditInfo* CatchFrame(long frame);						// ROM 0x002399b0 CatchFrame__16TXFramesEditInfoFl - ==> its entry
	// The entry of `frame`, into `*info`; ==> whether it has none of the
	// flags in `mask`.
	Boolean			GetEditInfoPtr(long frame, TXFrameEditInfo** info, int mask) const;	// ROM 0x002399f0 GetEditInfoPtr__16TXFramesEditInfoCFlPP15TXFrameEditInfoi
	TXFrameEditInfo* GetNext(void);									// ROM 0x00239a74 GetNext__16TXFramesEditInfoFv - nil at the end, when it starts again
	// `flag` set on `count` frames from `frame` (0x7fffffff: all after it).
	void			SetEditFlag(int flag, long frame, long count);	// ROM 0x00239aa8 SetEditFlag__16TXFramesEditInfoFilT2

	long			fCount;			// +0x00
	TXFrameEditInfo	fInfos[2];		// +0x04
	long			fFirst;			// +0x34  the first frame caught
	long			fLast;			// +0x38  the last
	long			fNext;			// +0x3c  GetNext's place
};

extern TXFramesEditInfo&	gFramesEditInfo;						// ROM 0x0c104d98 gFramesEditInfo


class TXFrameFormatter : public TXLinesHeights
{
public:
					TXFrameFormatter();								// ROM 0x002396e4 __ct__16TXFrameFormatterFv

	// from vtable +0x18
	virtual void	SetFrameHeight(long frame, long height) = 0;	// (pure: +0x18)
	virtual long	GetFrameHeight(long frame) const = 0;			// (pure: +0x1c)
	virtual long	GetFrameTextHeight(long frame) const = 0;		// (pure: +0x20)
	virtual NewtonErr ForceOverflow(long line) = 0;					// (pure: +0x24)
	virtual NewtonErr Format(void);									// ROM 0x0023980c Format__16TXFrameFormatterFv (+0x28: nothing)
	virtual long	GetCountFrames(void) const = 0;					// (pure: +0x2c)
	// The frame's first and last lines; ==> whether it has any.
	virtual Boolean	GetFrameLineRange(long frame, TXOffsetPair* lines) const = 0;	// (pure: +0x30)
	virtual long	LineToFrame(TXOffset line, Boolean atStart) const = 0;	// (pure: +0x34)
	virtual void	CharRangeChanged(TXChars* chars, long start, long oldLength, long newLength, unsigned long flags);	// ROM 0x00239808 CharRangeChanged__16TXFrameFormatterFP7TXCharslN22Ul (+0x38: nothing)
	virtual void	BeginEdit(void);								// ROM 0x00239814 BeginEdit__16TXFrameFormatterFv
	virtual TXFrameEditInfo* CatchFrame(long frame);				// ROM 0x00239840 CatchFrame__16TXFrameFormatterFl
	virtual TXFrameEditInfo* GetNextFrameEditInfo(void);			// ROM 0x0023984c GetNextFrameEditInfo__16TXFrameFormatterFv
	virtual void	EndEdit(void);									// ROM 0x0023982c EndEdit__16TXFrameFormatterFv
	virtual NewtonErr WriteToStream(TXStream* stream);				// ROM 0x00239858 WriteToStream__16TXFrameFormatterFP8TXStream - nothing to write
	virtual NewtonErr ReadFromStream(TXStream* stream);				// ROM 0x00239860 ReadFromStream__16TXFrameFormatterFP8TXStream

	// Whether line `line`, `extra` pixels taller, would no longer fit its
	// frame.
	Boolean			TestFrameOverflow(long line, long extra);		// ROM 0x00239724 TestFrameOverflow__16TXFrameFormatterFlT1
	long			CharToFrame(TXOffset offset, Boolean atStart) const;	// ROM 0x002397c0 CharToFrame__16TXFrameFormatterCF8TXOffset

	TXRanges*		fLineEnds;		// +0x20  the formatter's (TXFormatter::SetHandlers)
};


class TXMonoFrameFormatter : public TXFrameFormatter
{
public:
					TXMonoFrameFormatter();							// ROM 0x00239868 __ct__20TXMonoFrameFormatterFv

	virtual void	SetFrameHeight(long frame, long height);		// ROM 0x00239904 SetFrameHeight__20TXMonoFrameFormatterFlT1 - nothing
	virtual long	GetFrameHeight(long frame) const;				// ROM 0x00239908 GetFrameHeight__20TXMonoFrameFormatterCFl - 0x7fff
	virtual long	GetFrameTextHeight(long frame) const;			// ROM 0x00239914 GetFrameTextHeight__20TXMonoFrameFormatterCFl - all the lines
	virtual NewtonErr ForceOverflow(long line);						// ROM 0x0023991c ForceOverflow__20TXMonoFrameFormatterFl
	virtual long	GetCountFrames(void) const;						// ROM 0x00239924 GetCountFrames__20TXMonoFrameFormatterCFv - 1
	virtual Boolean	GetFrameLineRange(long frame, TXOffsetPair* lines) const;	// ROM 0x00239978 GetFrameLineRange__20TXMonoFrameFormatterCFlP12TXOffsetPair
	virtual long	LineToFrame(TXOffset line, Boolean atStart) const;	// ROM 0x0023999c LineToFrame__20TXMonoFrameFormatterCF8TXOffset - 0
	// The frame's entry records the text's height before the edit ...
	virtual TXFrameEditInfo* CatchFrame(long frame);				// ROM 0x002398a8 CatchFrame__20TXMonoFrameFormatterFl
	// ... and comes back with how much it changed.
	virtual TXFrameEditInfo* GetNextFrameEditInfo(void);			// ROM 0x002398cc GetNextFrameEditInfo__20TXMonoFrameFormatterFv
};


// One frame of a multi-frame formatter: the range's end (the line after
// the frame's last) and how tall the text in it is.  The ROM's element
// is 8 bytes.
struct TXFrameLines
{
	long			fEnd;			// +0x00  the TXRanges' long
	long			fHeight;		// +0x04  the text in the frame
};


class TXMultiFrameFormatter : public TXFrameFormatter
{
public:
					TXMultiFrameFormatter();						// ROM 0x002415b0 __ct__21TXMultiFrameFormatterFv
	virtual			~TXMultiFrameFormatter();						// ROM 0x00241610 __dt__21TXMultiFrameFormatterFv - the frames and the page breaks deleted

	virtual void	FreeData(void);									// ROM 0x00241680 FreeData__21TXMultiFrameFormatterFv
	// The line's height added to its frame, and the frame reflowed when
	// it is now too full.
	virtual NewtonErr InsertLine(const TXLineHeightInfo& info, TXFormatReflowLines* reflow, long line);	// ROM 0x00241b44 InsertLine__21TXMultiFrameFormatterFRC16TXLineHeightInfoP19TXFormatReflowLinesl
	virtual NewtonErr SetLineHeightInfo(const TXLineHeightInfo& info, long line, TXFormatReflowLines* reflow);	// ROM 0x00241cc4 SetLineHeightInfo__21TXMultiFrameFormatterFRC16TXLineHeightInfolP19TXFormatReflowLines
	virtual void	RemoveLines(long count, long line, TXFormatReflowLines* reflow);	// ROM 0x00241ec0 RemoveLines__21TXMultiFrameFormatterFlT1P19TXFormatReflowLines

	virtual long	GetFrameTextHeight(long frame) const;			// ROM 0x0024177c GetFrameTextHeight__21TXMultiFrameFormatterCFl
	// Line `line` and those after it in its frame pushed into the next.
	virtual NewtonErr ForceOverflow(long line);						// ROM 0x00242170 ForceOverflow__21TXMultiFrameFormatterFl
	virtual long	GetCountFrames(void) const;						// ROM 0x00241704 GetCountFrames__21TXMultiFrameFormatterCFv
	virtual Boolean	GetFrameLineRange(long frame, TXOffsetPair* lines) const;	// ROM 0x00241710 GetFrameLineRange__21TXMultiFrameFormatterCFlP12TXOffsetPair
	virtual long	LineToFrame(TXOffset line, Boolean atStart) const;	// ROM 0x0024176c LineToFrame__21TXMultiFrameFormatterCF8TXOffset
	// The page breaks moved with an edit, those in what was taken out
	// dropped, and (flags bit 2) the character 10s in what was put in
	// added.
	virtual void	CharRangeChanged(TXChars* chars, long start, long oldLength, long newLength, unsigned long flags);	// ROM 0x002422c8 CharRangeChanged__21TXMultiFrameFormatterFP7TXCharslN22Ul
	// The page breaks: a halfword count, then each break's offset.
	virtual NewtonErr WriteToStream(TXStream* stream);				// ROM 0x00242568 WriteToStream__21TXMultiFrameFormatterFP8TXStream
	virtual NewtonErr ReadFromStream(TXStream* stream);				// ROM 0x0024262c ReadFromStream__21TXMultiFrameFormatterFP8TXStream

	// from vtable +0x54
	virtual NewtonErr Compact(void);								// ROM 0x002416cc Compact__21TXMultiFrameFormatterFv
	virtual Boolean	VariableSizeFrames(void) const;					// ROM 0x002416fc VariableSizeFrames__21TXMultiFrameFormatterCFv - false
	// Where frame `frame`'s lines should end now (`*end`, the line after
	// the last) and by how much its text's height would change
	// (`*shift`: what is to go into the next frame, negative for what is
	// to come back); ==> whether anything moves, `*lines` the lines that
	// do.
	virtual Boolean	MeasureFrame(long frame, long* shift, long* end, TXOffsetPair* lines);	// ROM 0x00241798 MeasureFrame__21TXMultiFrameFormatterFlPlT2P12TXOffsetPair
	// ... and the frame ended there.
	virtual Boolean	BreakFrame(long frame, long* shift, TXOffsetPair* lines, TXFormatReflowLines* reflow);	// ROM 0x00241898 BreakFrame__21TXMultiFrameFormatterFlPlP12TXOffsetPairP19TXFormatReflowLines
	// A frame `height` tall added after the last, ending before line
	// `end` (-1: after the last line).
	virtual NewtonErr AppendFrame(long height, long end);			// ROM 0x00242118 AppendFrame__21TXMultiFrameFormatterFlT1

	// From frame `frame` (the one before, when `back`) on, each frame
	// broken again and what moves carried into the next (`shift` of it
	// to begin with), until a frame after `frame` is left as it was.
	NewtonErr		CheckReflow(long frame, TXFormatReflowLines* reflow, Boolean back, long shift, const TXOffsetPair* lines);	// ROM 0x00241900 CheckReflow__21TXMultiFrameFormatterFlP19TXFormatReflowLinesUcT1PC12TXOffsetPair
	void			RemoveFrames(long count, long frame);			// ROM 0x00242054 RemoveFrames__21TXMultiFrameFormatterFlT1
	void			RemoveFrameLines(long frame, long first, long last);	// ROM 0x002420a0 RemoveFrameLines__21TXMultiFrameFormatterFlN21 - their height taken off the frame's
	// The frames from `frame` on that `*shift` pixels empty completely,
	// removed; `*shift` comes back less their heights.  ==> how many.
	long			RemoveFormattedFrames(long frame, long* shift, TXOffsetPair* lines);	// ROM 0x00242258 RemoveFormattedFrames__21TXMultiFrameFormatterFlPlP12TXOffsetPair
	// A frame starting at line `first` and ending before `*end` ended
	// instead after a line that ends on a page break inside it, `*height`
	// its lines' height.
	void			CheckFrameBreaks(long first, long* end, long* height);	// ROM 0x002424a4 CheckFrameBreaks__21TXMultiFrameFormatterFlPlT2

	TXRanges*		fFrames;		// +0x24  of TXFrameLines
	TXLongTagArray*	fPageBreaks;	// +0x28  the offsets of the character 10s (nil: none)
	Boolean			fReflowAll;		// +0x2c  a page break was lost: reflow on every change
};


class TXPageFormatter : public TXMultiFrameFormatter
{
public:
					TXPageFormatter();								// ROM 0x00242704 __ct__15TXPageFormatterFv

	virtual void	SetFrameHeight(long frame, long height);		// ROM 0x0024281c SetFrameHeight__15TXPageFormatterFlT1 - every page's
	virtual long	GetFrameHeight(long frame) const;				// ROM 0x00242824 GetFrameHeight__15TXPageFormatterCFl
	// The lines laid out into pages from the first.
	virtual NewtonErr Format(void);									// ROM 0x0024274c Format__15TXPageFormatterFv

	long			fPageHeight;	// +0x30
};

#endif	/* __TXFRAMEFORMATTER_H */
