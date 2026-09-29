/*
	File:		text/TXRun.h

	Contains:	The runs - what a stretch of the text *is* - and the
				ranges that say which stretch is which run.

				A `TXRun` is an attribute object (TXAttributes.h) that
				knows how to measure, break, draw and hit-test the
				characters it covers.  It is abstract: the text runs
				(`TXNewtTextRun`, characters in a font) and the graphics
				runs (`TXGraphicsRun`, a picture standing in the text as
				one character) are what a document holds.  Of its twelve
				virtuals only `FullJustifPortion`, `VisibleLen`, `Click`,
				`SetHilite`, `DrawHilite` and `Assign` have bodies here,
				and all of them do nothing to speak of; the rest are the
				subclasses'.  The ROM's object is 8 bytes.

				A `TXRunRange` is the TXObjectRange (TXObjectRange.h) of a
				document's runs.  `CharToTextRun` answers the text run an
				offset is in - or, for an offset in a picture, the nearest
				text run before it, else after it - which is where typing
				at that point takes its style from.

	Reconstructed from the MP2x00 US ROM (0x00245cc4-0x00245ec8); each
	function cites its origin.
*/

#ifndef __TXRUN_H
#define __TXRUN_H

#ifndef __TXOBJECTRANGE_H
#include "TXObjectRange.h"
#endif
#ifndef __TXOFFSET_H
#include "TXOffset.h"
#endif

struct TXLineRunDisplayInfo;		// the line, the run's place on it and where it is drawn (TXLine)
struct TXRunPositionInfo;			// a run's place on a line, for its hilite
class TXPointingDevice;				// the pen, as the engine's click tracking sees it
struct TXClickCommandInfo;			// what a click in a run asks for


class TXRun : public TXAttrObject
{
public:
					TXRun();										// ROM 0x00245e64 __ct__5TXRunFv

	virtual void	Assign(const TXAttrObject* other);				// ROM 0x00245eac Assign__5TXRunFPC12TXAttrObject - nothing

	// The run's own virtuals, from vtable +0x54 (the order is the ROM's;
	// the pure ones are named from TXGraphicsRun's vtable)
	virtual Boolean	IsTextRun(void) const = 0;						// (pure: +0x54)
	virtual void	GetHeightInfo(int* ascent, int* descent, int* leading) = 0;	// (pure: +0x58)
	virtual long	PixelToChar(const TXLineRunDisplayInfo& info, long pixel, TXOffsetRange* range) = 0;	// (pure: +0x5c)
	virtual long	CharToPixel(const TXLineRunDisplayInfo& info, long offset) = 0;	// (pure: +0x60)
	virtual void	Draw(const TXLineRunDisplayInfo& info, long offset, const Rect& clip, int mode) = 0;	// (pure: +0x64)
	virtual long	FullJustifPortion(const TXLineRunDisplayInfo& info);	// ROM 0x00245ea4 FullJustifPortion__5TXRunFRC20TXLineRunDisplayInfo (+0x68: 0)
	virtual long	VisibleLen(const UniChar* text, long count);	// ROM 0x00245eb8 VisibleLen__5TXRunFPCUsl (+0x6c: all of it)
	virtual long	MeasureWidth(const TXLineRunDisplayInfo& info) = 0;	// (pure: +0x70)
	virtual long	LineBreak(const UniChar* text, long count, long width, long* broken, Boolean wordWrap, long* widthUsed) = 0;	// (pure: +0x74)
	virtual long	Click(const TXRunPositionInfo& where, TXPointingDevice* pen, long offset, int clicks, const Rect& bounds, TXClickCommandInfo* command);	// ROM 0x00245eb0 Click__5TXRunFRC17TXRunPositionInfoP16TXPointingDeviceliRC4RectP18TXClickCommandInfo (+0x78: 0)
	virtual void	SetHilite(char on, const TXRunPositionInfo& where, Boolean draw);	// ROM 0x00245ec0 SetHilite__5TXRunFcRC17TXRunPositionInfoUc (+0x7c: nothing)
	virtual void	DrawHilite(const TXRunPositionInfo& where);		// ROM 0x00245ec4 DrawHilite__5TXRunFRC17TXRunPositionInfo (+0x80: nothing)
};


class TXRunRange : public TXObjectRange
{
public:
	// `kind` 2 is a document's (its array grows ten entries at a time);
	// anything else one at a time.
					TXRunRange(char kind);							// ROM 0x00245cc4 __ct__10TXRunRangeFc
	virtual			~TXRunRange();									// ROM 0x00245d18 __dt__10TXRunRangeFv

	TXRun*			IsTextRun(long index) const;					// ROM 0x00245d58 IsTextRun__10TXRunRangeCFl - range index's run when it is a text run, else nil
	// The text run the character at `offset` is in, or failing that the
	// nearest one before it, or failing that after it; nil for none.
	TXRun*			CharToTextRun(TXOffset offset, Boolean atStart) const;	// ROM 0x00245d84 CharToTextRun__10TXRunRangeCF8TXOffset
	TXRun*			SearchTextRunBackward(long index) const;		// ROM 0x00245dc8 SearchTextRunBackward__10TXRunRangeCFl
	TXRun*			SearchTextRunForward(long index) const;			// ROM 0x00245e04 SearchTextRunForward__10TXRunRangeCFl
};


#endif	/* __TXRUN_H */
