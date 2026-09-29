/*
	File:		text/TXGraphicsRun.h

	Contains:	The graphics runs: a picture standing in the text as one
				character.

				A `TXGraphicsRun` (TXRun.h) is a box of a size its subclass
				answers (`GetDimensions`), sitting on the baseline (its
				height is all ascent) and drawn by the subclass into a
				rectangle (`DrawContent`).  It stands for a thing in the
				text, which is what `GetObjFlags` & 4 says - so a graphics
				run is never shared (`Reference` answers a copy) and never
				run together with its neighbour (TXObjectRange.h).  A tap
				on its middle selects it whole, a tap nearer an edge puts
				the caret beside it (`PixelToChar`: the margins are a
				quarter of its width each, when its character is a control
				character).

				A graphics run wider than the room left on a line is
				squeezed onto an empty one, the difference kept as a
				negative extra width (`LineBreak`, `fExtraWidth`).  When
				selected it is framed in a gray pattern drawn in XOR, so
				the same drawing takes the frame away again (`SetHilite`,
				`DrawHilite`); bit 1 of its flags asks for a two-pixel
				margin round it for the frame (`GetHiliteInset`).

				`TXNewtGraphicsRun` ('graf, public type 'shap) is the
				Newton's: a frame whose `shape` slot is a NewtonScript
				shape (views/DrawShape.h), measured by `ShapeBounds` with
				two pixels round it (16 by 16 with no shape) and drawn by
				`DrawShape`.

				The ROM's objects are 0x10 and 0x14 bytes.

	Reconstructed from the MP2x00 US ROM (0x0023ac54-0x0023b124,
	0x0023ded4-0x0023e168); each function cites its origin.
*/

#ifndef __TXGRAPHICSRUN_H
#define __TXGRAPHICSRUN_H

#ifndef __TXRUN_H
#include "TXRun.h"
#endif

const long	kTXGraphicsRunClassId	= 0x67726166;		// 'graf'
const long	kTXShapePublicType		= 0x73686170;		// 'shap'


class TXGraphicsRun : public TXRun
{
public:
					TXGraphicsRun();								// ROM 0x0023ac54 __ct__13TXGraphicsRunFv

	// TXAttrObject
	virtual TXAttrObject* Reference(void);							// ROM 0x0023aca0 Reference__13TXGraphicsRunFv - a copy
	virtual unsigned long GetObjFlags(void) const;					// ROM 0x0023aed4 GetObjFlags__13TXGraphicsRunCFv - | 7

	// TXRun
	virtual Boolean	IsTextRun(void) const;							// ROM 0x0023aeec IsTextRun__13TXGraphicsRunCFv
	virtual void	GetHeightInfo(int* ascent, int* descent, int* leading);	// ROM 0x0023af78 GetHeightInfo__13TXGraphicsRunFPiN21
	virtual void	PixelToChar(const TXLineRunDisplayInfo& info, Fixed pixel, TXOffsetRange* range);	// ROM 0x0023ace0 PixelToChar__13TXGraphicsRunFRC20TXLineRunDisplayInfolP13TXOffsetRange
	virtual Fixed	CharToPixel(const TXLineRunDisplayInfo& info, long offset);	// ROM 0x0023acf8 CharToPixel__13TXGraphicsRunFRC20TXLineRunDisplayInfol
	virtual void	Draw(const TXLineRunDisplayInfo& info, Fixed x, const Rect& line, int baseline);	// ROM 0x0023b0a8 Draw__13TXGraphicsRunFRC20TXLineRunDisplayInfolRC4Recti
	virtual Fixed	MeasureWidth(const TXLineRunDisplayInfo& info);	// ROM 0x0023ad54 MeasureWidth__13TXGraphicsRunFRC20TXLineRunDisplayInfo
	virtual long	LineBreak(const UniChar* text, long count, long start, Fixed* width, Boolean mayCutWord, long* length);	// ROM 0x0023ad7c LineBreak__13TXGraphicsRunFPCUslT2PlUcT4
	virtual void	SetHilite(char on, const TXRunPositionInfo& where, Boolean draw);	// ROM 0x0023ad04 SetHilite__13TXGraphicsRunFcRC17TXRunPositionInfoUc
	virtual void	DrawHilite(const TXRunPositionInfo& where);		// ROM 0x0023ae18 DrawHilite__13TXGraphicsRunFRC17TXRunPositionInfo

	// its own, from vtable +0x84
	virtual void	GetDimensions(int* height, int* width) = 0;	// (pure: +0x84)
	virtual int		GetHiliteInset(void);							// ROM 0x0023aef4 GetHiliteInset__13TXGraphicsRunFv (+0x88)
	virtual void	DrawContent(const Rect& box) = 0;				// (pure: +0x8c)

	// The size with the extra width and the hilite's margins.
	void			GetTotalDimensions(int* height, int* width);	// ROM 0x0023af18 GetTotalDimensions__13TXGraphicsRunFPiT1
	// The run's box on a line: at its left, standing on the line's bottom.
	void			GetRunRect(const TXRunPositionInfo& where, Rect* box);	// ROM 0x0023afb8 GetRunRect__13TXGraphicsRunFRC17TXRunPositionInfoP4Rect
	void			AdjustRunRect(Rect* box);						// ROM 0x0023b01c AdjustRunRect__13TXGraphicsRunFP4Rect

	long			fExtraWidth;	// +0x08  what LineBreak squeezed it by (negative)
	char			fHilite;		// +0x0c  0 none, 2 the gray frame, else the darker one
};


class TXNewtGraphicsRun : public TXGraphicsRun
{
public:
					TXNewtGraphicsRun();							// ROM 0x0023ded4 __ct__17TXNewtGraphicsRunFv

	virtual TXAttrObject* CreateNew(void) const;					// ROM 0x0023df28 CreateNew__17TXNewtGraphicsRunCFv
	virtual long	GetClassId(void) const;							// ROM 0x0023df78 GetClassId__17TXNewtGraphicsRunCFv - 'graf'
	virtual unsigned long GetObjFlags(void) const;					// ROM 0x0023df90 GetObjFlags__17TXNewtGraphicsRunCFv - no hilite margin
	virtual void	Assign(const TXAttrObject* other);				// ROM 0x0023df44 Assign__17TXNewtGraphicsRunFPC12TXAttrObject
	virtual Ref		GetNSObject(void) const;						// ROM 0x0023e168 GetNSObject__17TXNewtGraphicsRunCFv
	virtual void	SetNSObject(RefArg obj);						// ROM 0x0023df30 SetNSObject__17TXNewtGraphicsRunFRC6RefVar
	virtual long	GetPublicType(void) const;						// ROM 0x0023df84 GetPublicType__17TXNewtGraphicsRunCFv - 'shap'
	virtual unsigned long GetAttributeFlags(TXAttrTag tag) const;	// ROM 0x0023dfa8 GetAttributeFlags__17TXNewtGraphicsRunCFUl

	virtual void	GetDimensions(int* height, int* width);		// ROM 0x0023dfd4 GetDimensions__17TXNewtGraphicsRunFPiT1
	virtual void	DrawContent(const Rect& box);					// ROM 0x0023e094 DrawContent__17TXNewtGraphicsRunFRC4Rect

	RefStruct		fObject;		// +0x10  a frame with a `shape` slot
};

#endif	/* __TXGRAPHICSRUN_H */
