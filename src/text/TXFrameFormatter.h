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
				its text is as tall as all the lines together.  (The
				multi-frame and page formatters are NOT YET.)

				The ROM's objects are 0x24 bytes (the lines' heights and a
				pointer to the formatter's line ranges).

	Reconstructed from the MP2x00 US ROM (0x002396e4-0x00239b4c); each
	function cites its origin.
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
// frames, and CatchFrame does not look: a third would be written over
// fFirst and what follows (the formatters only ever catch the frame an
// edit starts in and the one it ends in).
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

extern TXFramesEditInfo	gFramesEditInfo;						// ROM 0x0c104d98 gFramesEditInfo


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

#endif	/* __TXFRAMEFORMATTER_H */
