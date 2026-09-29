/*
	File:		text/TXFrames.h

	Contains:	The frames: where the lines of a document go.

				`TXFrames` is the geometry of the rectangles the text is
				poured into.  It works in *absolute* coordinates - longs,
				the document's own, which may run far past a QuickDraw
				Rect's 16 bits - and turns them into *draw* coordinates for
				the port through three origins: where the view has scrolled
				to (`FramesScrolled`), where the drawing starts in the port
				(`SetDrawOrigin`) and where the frames start
				(`SetFramesOrigin`); a draw coordinate is clipped to
				±0x7fff.  Each frame has a text rectangle (`GetAbsTextBounds`:
				the margins' top left and the size the subclass keeps) and,
				outside it, the margins (`GetAbsFrameBounds`).  The lines'
				heights are the frame formatter's (TXFrameFormatter.h), so
				the frames answer which line a point is on (`PointToLine`),
				a line's rectangle (`GetLineBounds`), and the lines a
				rectangle crosses, as bands of equal lines
				(`SectLines`: a `TXSectLine` per band).

				`TXMonoSizeFrames` is frames that are all one size;
				`TXMonoFrame` is the one frame of a view, its formatter a
				TXMonoFrameFormatter and its height unbounded until given
				one (0x40000000).  `TXSectFrames` walks the frames a
				rectangle crosses, and `TXDisplayChanges` records what a
				change of size or margins means for the display (bits: 1
				the width changed, 2 only the height, 0x18 the margins).

				The ROM's objects are 0x28 bytes (TXFrames) and 0x30
				(TXMonoSizeFrames, TXMonoFrame).  TXPageFrames is NOT YET.

	Reconstructed from the MP2x00 US ROM (0x00239e0c-0x0023ac54); each
	function cites its origin.
*/

#ifndef __TXFRAMES_H
#define __TXFRAMES_H

#ifndef __TXFRAMEFORMATTER_H
#include "TXFrameFormatter.h"
#endif
#ifndef __TXUTILITIES_H
#include "TXUtilities.h"
#endif

// What a change to the frames means for the display.  The ROM's is 0xc
// bytes.
struct TXDisplayChanges
{
					TXDisplayChanges();								// ROM 0x0023ab48 __ct__16TXDisplayChangesFv
	void			GetFormatRange(TXOffsetPair* range) const;		// ROM 0x0023ab80 GetFormatRange__16TXDisplayChangesCFP12TXOffsetPair

	unsigned long	fFlags;			// +0x00
	TXOffset		fFormatStart;	// +0x04  0x7fffffff: none
	TXOffset		fFormatEnd;		// +0x08
};

// The frames a rectangle crosses: a uniform run (`first` to `last`,
// `perRow` in a row, rows `stride` apart), or a list.  The ROM's is 0x24
// bytes.
struct TXSectFrames
{
	long			GetNextFrame(void);								// ROM 0x00239e0c GetNextFrame__12TXSectFramesFv - -1 at the end
	void			SetUniform(long first, long perRow, long last, long stride);	// ROM 0x00239eac SetUniform__12TXSectFramesFlN31

	long			fFirst;			// +0x00
	long			fPerRow;		// +0x04
	long			fLast;			// +0x08
	long			fStride;		// +0x0c
	long			fInRow;			// +0x10
	long			fListCount;		// +0x14  -1: uniform
	long			fList[2];		// +0x18
	long			fCurrent;		// +0x20  -1: not started, -2: none
};

// A band of equal lines a rectangle crosses (SectLines).  0x14 bytes.
struct TXSectLine
{
	long			fLine;			// +0x00  the first
	Rect			fRect;			// +0x04  the first line's, in draw coordinates
	long			fCount;			// +0x0c  0: the rectangle below the last line
	long			fAscent;		// +0x10
};


class TXFrames : public TXVirtualObject
{
public:
					TXFrames();										// ROM 0x0023ab90 __ct__8TXFramesFv
	virtual			~TXFrames();									// ROM 0x0023ac08 __dt__8TXFramesFv - the formatter deleted

	virtual void	FreeData(void);									// ROM 0x00239ecc FreeData__8TXFramesFv - the formatter's
	virtual void	GetTextBoundsSize(TXLongPoint* size, long frame) const = 0;	// (pure: +0x08)
	virtual void	SetTextBoundsSize(const TXLongPoint& size, TXDisplayChanges* changes, long frame) = 0;	// (pure: +0x0c)
	virtual void	GetAbsTextBounds(long frame, TXLongRect* bounds) const;	// ROM 0x0023a218 GetAbsTextBounds__8TXFramesCFlP10TXLongRect
	virtual void	SetFramesMargins(const Rect& margins, TXDisplayChanges* changes);	// ROM 0x0023a140 SetFramesMargins__8TXFramesFRC4RectP16TXDisplayChanges
	virtual void	GetFramesMargins(Rect* margins) const;			// ROM 0x0023a18c GetFramesMargins__8TXFramesCFP4Rect
	virtual long	GetTotalHeight(void) const = 0;					// (pure: +0x1c)
	virtual long	GetTotalWidth(void) const = 0;					// (pure: +0x20)
	virtual void	InvalFramePart(long frame, int parts, long unused, RgnHandle rgn);	// ROM 0x00239ed8 InvalFramePart__8TXFramesFliT1PP6Region
	virtual long	GetLineFormatWidth(long line) const = 0;		// (pure: +0x28)
	virtual long	GetLineMaxWidth(long line) const = 0;			// (pure: +0x2c)
	virtual void	SectFrames(const Rect& r, TXSectFrames* frames) const = 0;	// (pure: +0x30)
	virtual void	Draw(long frame) const;							// ROM 0x0023a33c Draw__8TXFramesCFl - nothing
	virtual long	PointToNearestFrame(const TXLongPoint& pt) const = 0;	// (pure: +0x38)

	short			HAbsToDraw(long h) const;						// ROM 0x00239f0c HAbsToDraw__8TXFramesCFl
	short			VAbsToDraw(long v) const;						// ROM 0x00239f50 VAbsToDraw__8TXFramesCFl
	long			HDrawToAbs(long h) const;						// ROM 0x00239f94 HDrawToAbs__8TXFramesCFl
	long			VDrawToAbs(long v) const;						// ROM 0x00239fb0 VDrawToAbs__8TXFramesCFl
	void			AbsToDraw(const TXLongRect& abs, Rect* draw) const;	// ROM 0x00239fcc AbsToDraw__8TXFramesCFRC10TXLongRectP4Rect
	void			DrawToAbs(const Rect& draw, TXLongRect* abs) const;	// ROM 0x0023a044 DrawToAbs__8TXFramesCFRC4RectP10TXLongRect
	Point			AbsToDraw(const TXLongPoint& abs) const;		// ROM 0x0023a0ac AbsToDraw__8TXFramesCFRC11TXLongPoint
	void			DrawToAbs(Point draw, TXLongPoint* abs) const;	// ROM 0x0023a104 DrawToAbs__8TXFramesCF5PointP11TXLongPoint
	void			GetAbsFrameBounds(long frame, TXLongRect* bounds) const;	// ROM 0x0023a19c GetAbsFrameBounds__8TXFramesCFlP10TXLongRect - the text bounds and the margins
	void			GetTextBounds(long frame, Rect* bounds) const;	// ROM 0x0023a290 GetTextBounds__8TXFramesCFlP4Rect
	void			GetFrameBounds(long frame, Rect* bounds) const;	// ROM 0x0023a2cc GetFrameBounds__8TXFramesCFlP4Rect
	void			FramesScrolled(long dh, long dv);				// ROM 0x0023a300 FramesScrolled__8TXFramesFlT1
	void			SetDrawOrigin(long h, long v);					// ROM 0x0023a31c SetDrawOrigin__8TXFramesFlT1
	void			SetFramesOrigin(long h, long v);				// ROM 0x0023a32c SetFramesOrigin__8TXFramesFlT1
	// The line the point is on; `*outside` whether it is outside every
	// frame's text, `*past` whether it is past the lines there are (the
	// last line, or the line before the frame, answered).  -1 for none.
	long			PointToLine(Point pt, unsigned char* outside, unsigned char* past) const;	// ROM 0x0023a340 PointToLine__8TXFramesCF5PointPUcT2
	Boolean			GetLineBounds(long line, TXLongRect* bounds) const;	// ROM 0x0023a454 GetLineBounds__8TXFramesCFlP10TXLongRect
	Boolean			GetLineBounds(long line, Rect* bounds) const;	// ROM 0x0023a524 GetLineBounds__8TXFramesCFlP4Rect
	long			PointToFrame(const TXLongPoint& pt, unsigned char* outside) const;	// ROM 0x0023a564 PointToFrame__8TXFramesCFRC11TXLongPointPUc
	long			PointToFrame(Point pt, unsigned char* outside) const;	// ROM 0x0023a5e0 PointToFrame__8TXFramesCF5PointPUc
	// The lines of `frame` that `*r` crosses, as bands of equal lines
	// (TXSectLine) into `lines`; `*r` is clipped to the frame's text,
	// `*first` is the first line and `*count` how many.  ==> false when
	// the rectangle misses the text.
	Boolean			SectLines(Rect* r, long frame, long* first, long* count, TXArray* lines) const;	// ROM 0x0023a614 SectLines__8TXFramesCFP4RectlPlT3P7TXArray

	TXFrameFormatter* fFormatter;	// +0x04
	long			fScrollV;		// +0x08  how far the view has scrolled
	long			fScrollH;		// +0x0c
	long			fDrawOriginV;	// +0x10  where drawing starts in the port
	long			fDrawOriginH;	// +0x14
	long			fFramesOriginV;	// +0x18  where the frames start
	long			fFramesOriginH;	// +0x1c
	Rect			fMargins;		// +0x20  round the text
};


class TXMonoSizeFrames : public TXFrames
{
public:
					TXMonoSizeFrames();								// ROM 0x0023a8e4 __ct__16TXMonoSizeFramesFv

	virtual void	GetTextBoundsSize(TXLongPoint* size, long frame) const;	// ROM 0x0023a9bc GetTextBoundsSize__16TXMonoSizeFramesCFP11TXLongPointl
	virtual void	SetTextBoundsSize(const TXLongPoint& size, TXDisplayChanges* changes, long frame);	// ROM 0x0023a934 SetTextBoundsSize__16TXMonoSizeFramesFRC11TXLongPointP16TXDisplayChangesl
	virtual long	GetLineFormatWidth(long line) const;			// ROM 0x0023a9cc GetLineFormatWidth__16TXMonoSizeFramesCFl - the width
	virtual long	GetLineMaxWidth(long line) const;				// ROM 0x0023a9d4 GetLineMaxWidth__16TXMonoSizeFramesCFl

	TXLongPoint		fSize;			// +0x28  every frame's text
};


class TXMonoFrame : public TXMonoSizeFrames
{
public:
					TXMonoFrame();									// ROM 0x0023a9dc __ct__11TXMonoFrameFv

	// A height of nought is no limit (0x40000000).
	virtual void	SetTextBoundsSize(const TXLongPoint& size, TXDisplayChanges* changes, long frame);	// ROM 0x0023aa5c SetTextBoundsSize__11TXMonoFrameFRC11TXLongPointP16TXDisplayChangesl
	// The lines and the margins, or the frame when that is taller (and
	// not unbounded).
	virtual long	GetTotalHeight(void) const;						// ROM 0x0023aaa4 GetTotalHeight__11TXMonoFrameCFv
	virtual long	GetTotalWidth(void) const;						// ROM 0x0023ab1c GetTotalWidth__11TXMonoFrameCFv
	virtual void	SectFrames(const Rect& r, TXSectFrames* frames) const;	// ROM 0x0023aa30 SectFrames__11TXMonoFrameCFRC4RectP12TXSectFrames - frame 0
	virtual long	PointToNearestFrame(const TXLongPoint& pt) const;	// ROM 0x0023aa28 PointToNearestFrame__11TXMonoFrameCFRC11TXLongPoint - 0
};

#endif	/* __TXFRAMES_H */
