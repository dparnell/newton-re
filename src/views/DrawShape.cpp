/*
	File:		views/DrawShape.cpp

	Contains:	NewtonScript shapes: making them, their bounds, DrawShape
				with its style frames.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "DrawShape.h"
#include "Ink.h"
#include "Transform.h"
#include "Rects.h"
#include "Regions.h"
#include "RegionVars.h"
#include "Draw.h"
#include "Shapes.h"
#include "Polygons.h"
#include "Text.h"
#include "Pictures.h"
#include "Fonts.h"
#include "RichString.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "Locale.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include <string.h>

static const char kGrafException[] = "evt.ex.graf";
const long kGrafErrNotAShape = -8804;			// the ROM's 0xffffdd9c: ShapeBounds of what is no shape
const long kGrafErrBadBounds = -8801;			// 0xffffdd9f: a bitmap without proper bounds

// the shape class names as symbols, once
static Ref gSYMTextBox = NILREF;


/*------------------------------------------------------------------------------
	T P a t t e r n
------------------------------------------------------------------------------*/

// ROM 0x001981bc __dt__8TPatternFv
// An owned pattern disposed - the port's pen pattern put back to black
// first when it is this one.
TPattern::~TPattern()
{
	if (fOwned)
	{
		if (fRestoreFg && GetFgPattern() == fPattern)
			SetFgPattern(GetStdPattern(blackPat));
		DisposePattern(fPattern);
	}
}


// ROM 0x00198178 GetFillPattern__8TPatternFRC6RefVarUc
// The pattern from its slot (GetPattern); ==> whether there is one - a
// nil slot answers isPen (a pen without a pattern is still drawn, in
// black).
Boolean
TPattern::GetFillPattern(RefArg spec, Boolean isPen)
{
	if (ISNIL(spec))
		return isPen;
	return GetPattern(spec, &fOwned, &fPattern, isPen);
}


/*------------------------------------------------------------------------------
	T S t y l e S a v e
------------------------------------------------------------------------------*/

// ROM 0x00198dd8 Init__9SaveLevelFP9SaveLevel
void
SaveLevel::Init(SaveLevel* previous)
{
	fPrevious = previous;
	fClip = nil;
	fTransformLevel = 0;
	fFlags = 0;
}


// ROM 0x00198220 __ct__10TStyleSaveFv
// No patterns, the port, an empty base level; the style in force is not
// any frame yet.
TStyleSave::TStyleSave()
{
	fPen = false;
	fFill = false;
	fTextPatternSet = false;
	fSelection = 0;
	fTransferMode = srcOr;
	fJustification = 0;
	fAlignment = 0;
	GetPort(&fPort);
	fBaseLevel.Init(nil);
	fLevel = &fBaseLevel;
	fClipDepth = 0;
	fTransformDepth = 0;
	fStyle = (Ref) -4;			// (the ROM's: no frame is this)
}


// ROM 0x0019836c __dt__10TStyleSaveFv
// The levels ended (their clips put back), the patterns disposed.
TStyleSave::~TStyleSave()
{
	EndLevel();
}


// ROM 0x00198d38 BeginLevel__10TStyleSaveFP9SaveLevel
void
TStyleSave::BeginLevel(SaveLevel* level)
{
	level->Init(fLevel);
	fLevel = level;
}


// ROM 0x00198d60 EndLevel__10TStyleSaveFv
// The level's clipping undone: the clip it saved put back.
void
TStyleSave::EndLevel(void)
{
	if (fLevel->fFlags & 1)
	{
		TQDScaler::StopScaling();
		fTransformDepth--;
	}
	if (fLevel->fFlags & 2)
	{
		SetClip(fLevel->fClip);
		DisposeRgn(fLevel->fClip);
		fLevel->fClip = nil;
		fClipDepth--;
	}
	if (fLevel->fPrevious != nil)
		fLevel = fLevel->fPrevious;
	else
		fLevel->fFlags = 0;
}


// ROM 0x0019846c SetStyle__10TStyleSaveFRC6RefVarRC6TPointl
// The style frame put in force: the pen normal, then its slots -
// clipping (a shape, or a region shape: the port's clip narrowed to it,
// offset by the origin, the clip before saved in the level; ==> false
// when nothing of the port's visible region is left), transform (NOT YET
// RECONSTRUCTED: scaling), selection, penSize (an integer for both, or
// [h, v]; negative is 0), fillPattern, transferMode (8, patCopy, draws as
// srcOr; the pen mode is the pattern mode), penPattern, textPattern,
// justification ('center, 'right), font (the userFont preference
// otherwise).  flags: 1 leaves the transform, 2 leaves the pen and text
// slots (a style put back after a nested list).  A nil style is the pen
// alone.
Boolean
TStyleSave::SetStyle(RefArg style, const Point& origin, long flags)
{
	Boolean keepTransform = (flags & 1) != 0;
	Boolean keepPen = (flags & 2) != 0;
	fStyle = style;
	PenNormal();
	fPen = true;
	fFill = false;
	fTextPatternSet = false;
	fAlignment = 0;
	fTransferMode = srcOr;
	fJustification = 0;
	fSelection = 0;
	Boolean fontSet = false;
	Boolean visible = true;
	if (NOTNIL(style))
	{
		RefVar value(GetProtoVariable(style, RSSYMclipping, nil));
		if (NOTNIL(value))
		{
			if (!EQRef(ClassOf(value), RSSYMregion))
			{
				RefVar shape(value);
				TRegionVar rgn;
				OpenRgn();
				DrawShape(shape, RefVar(NILREF), MakePoint(0, 0));
				CloseRgn(rgn);
				if ((fLevel->fFlags & 2) == 0)
				{
					fLevel->fClip = NewRgn();
					GetClip(fLevel->fClip);
					fLevel->fFlags |= 2;
					fClipDepth++;
				}
				if (origin.h != 0 || origin.v != 0)
					OffsetRgn(rgn, origin.h, origin.v);
				TRegionVar clip;
				GetClip(clip);
				SectRgn(clip, rgn, clip);
				SetClip(clip);
				visible = RectInRgn(&(*clip)->rgnBBox, fPort->visRgn);
			}
			else
			{
				RefVar data(GetFrameSlotRef(value, RSSYMdata));
				LockRef(data);
				TRegionVar rgn;
				RgnHandle fake = (RgnHandle) NewFakeHandle(BinaryData(data), Length(data));
				CopyRgn(fake, rgn);
				DisposHandle((Handle) fake);
				UnlockRef(data);
				if ((fLevel->fFlags & 2) == 0)
				{
					fLevel->fClip = NewRgn();
					GetClip(fLevel->fClip);
					fLevel->fFlags |= 2;
					fClipDepth++;
				}
				if (origin.h != 0 || origin.v != 0)
					OffsetRgn(rgn, origin.h, origin.v);
				TRegionVar clip;
				GetClip(clip);
				SectRgn(clip, rgn, clip);
				SetClip(clip);
				visible = RectInRgn(&(*clip)->rgnBBox, fPort->visRgn);
			}
		}
		if (!keepTransform)
		{
			value = GetProtoVariable(style, RSSYMtransform, nil);
			if (ISNIL(value))
				fLevel->fFlags &= ~1;
			else
			{
				// [srcRect, dstRect], or [dx, dy] - which stands for two
				// ten-by-ten rectangles offset by them, so it only moves
				Rect src, dst;
				RefVar first(GetArraySlotRef(value, 0));
				if (ISINT(first))
				{
					SetRect(&src, 0, 0, 10, 10);
					SetRect(&dst, 0, 0, 10, 10);
					OffsetRect(&dst, RINT(first), RINT(RefVar(GetArraySlotRef(value, 1))));
				}
				else
				{
					FromObject(first, src);
					FromObject(RefVar(GetArraySlotRef(value, 1)), dst);
				}
				if (!EqualRect(&src, &dst))
				{
					// the origin goes into the first transform, which is
					// why the drawing below does not add it again
					if (fTransformDepth == 0)
						OffsetRect(&dst, origin.h, origin.v);
					TTransform transform;
					transform.fFlags = 0;
					transform.Setup(&src, &dst, false);
					if ((fLevel->fFlags & 1) == 0)
					{
						TQDScaler::StartScaling(transform);
						fLevel->fFlags |= 1;
					}
					else
						TQDScaler::ReplaceScaling(transform);
					fTransformDepth++;
				}
			}
		}
		value = GetProtoVariable(style, RSSYMselection, nil);
		if (NOTNIL(value))
			fSelection = RINT(value);
		value = GetProtoVariable(style, RSSYMpensize, nil);
		if (NOTNIL(value))
		{
			long width, height;
			if (ISINT(value))
			{
				width = RVALUE(value);
				if (width < 1)
					width = 0;
				height = width;
			}
			else
			{
				width = RINT(GetArraySlotRef(value, 0));
				if (width < 1)
					width = 0;
				height = RINT(GetArraySlotRef(value, 1));
				if (height < 1)
					height = 0;
			}
			PenSize(width, height);
		}
		value = GetProtoVariable(style, RSSYMfillpattern, nil);
		if (NOTNIL(value))
			fFill = fFillPattern.GetFillPattern(value, false);
		if (!keepPen)
		{
			value = GetProtoVariable(style, RSSYMtransfermode, nil);
			if (NOTNIL(value))
			{
				fTransferMode = RINT(value);
				PenMode((fTransferMode == patCopy ? srcOr : fTransferMode) + 8);
			}
			value = GetProtoVariable(style, RSSYMpenpattern, nil);
			if (NOTNIL(value))
				fPen = fPenPattern.GetFillPattern(value, true);
			value = GetProtoVariable(style, RSSYMtextpattern, nil);
			if (NOTNIL(value))
			{
				fTextPatternSet = fTextPattern.GetFillPattern(value, true);
				if (fTextPatternSet)
					fTextPattern.fRestoreFg = true;
			}
			value = GetProtoVariable(style, RSSYMjustification, nil);
			if (NOTNIL(value))
			{
				if (EQRef(value, RSSYMcenter))
					fAlignment = 0x8000;
				else if (EQRef(value, RSSYMright))
					fAlignment = 0x10000;
			}
			value = GetProtoVariable(style, RSSYMfont, nil);
			if (NOTNIL(value))
			{
				fontSet = true;
				fFont = value;
			}
		}
	}
	if (!fontSet)
		fFont = GetPreference(RSSYMuserfont);
	return visible;
}


/*------------------------------------------------------------------------------
	S h a p e s
------------------------------------------------------------------------------*/

// ROM 0x000dd7e4 IsStyleFrame__FRC6RefVar
// A style is a plain frame (class 'frame).
Boolean
IsStyleFrame(RefArg obj)
{
	return EQRef(ClassOf(obj), RSSYMframe);
}


static Ref
SYMTextBox(void)
{
	if (gSYMTextBox == NILREF)
		gSYMTextBox = Intern((char*) "TextBox");
	return gSYMTextBox;
}


// ROM 0x000dd828 IsPrimShape__FRC6RefVar
// Whether the object is one shape: a pointer object of class 'rectangle,
// 'line, 'TextBox, 'ink, 'roundRectangle, 'oval, 'bitmap, 'picture (a
// frame), 'polygon, 'wedge, 'region or 'text.
Boolean
IsPrimShape(RefArg obj)
{
	if (!ISPTR(obj))
		return false;
	RefVar cls(ClassOf(obj));
	if (EQRef(cls, RSSYMrectangle) || EQRef(cls, RSSYMline) || EQRef(cls, SYMTextBox()) || EQRef(cls, RSSYMink)
	 || EQRef(cls, RSSYMroundrectangle) || EQRef(cls, RSSYMoval) || EQRef(cls, RSSYMbitmap))
		return true;
	if (EQRef(cls, RSSYMpicture) && IsFrame(obj))
		return true;
	return EQRef(cls, RSSYMpolygon) || EQRef(cls, RSSYMwedge) || EQRef(cls, RSSYMregion) || EQRef(cls, RSSYMtext);
}


// ROM 0x000e1360 MakeRectShape__FRC6RefVarN41
// An 8-byte binary of the class holding the rectangle.
Ref
MakeRectShape(RefArg cls, RefArg left, RefArg top, RefArg right, RefArg bottom)
{
	Rect r;
	r.top = (short) RINT(top);
	r.left = (short) RINT(left);
	r.bottom = (short) RINT(bottom);
	r.right = (short) RINT(right);
	RefVar shape(AllocateBinary(cls, sizeof(Rect)));
	memmove(BinaryData(shape), &r, sizeof(Rect));
	return shape;
}


// the rectangle a binary shape (or a bounds binary) holds; a bounds frame
// {left, top, right, bottom} is taken too (host: a picture frame's, which
// the ROM's MakeShape turns into a binary first)
static void
RectOf(RefArg binary, Rect* r)
{
	if (IsFrame(binary))
	{
		if (!FromObject(binary, *r))
			SetEmptyRect(r);
		return;
	}
	memmove(r, BinaryData(binary), sizeof(Rect));
}


// ROM 0x000e0f20 ShapeBounds__FRC6RefVarP5TRect
// The bounds of a shape: a list's the union of its members' (styles
// skipped); a region's its data's box; a polygon's its data's box, a
// pixel wider and taller; a bitmap's, picture's, text's or ink's its
// bounds slot; a rectangle's, oval's, round rectangle's, wedge's or
// line's its own rectangle - a line's two ends put in order, a pixel
// wide or high when they lie on one line.  Anything else: evt.ex.graf
// -8804.
void
ShapeBounds(RefArg shape, Rect* bounds)
{
	if (IsArray(shape))
	{
		SetEmptyRect(bounds);
		for (long i = 0, count = Length(shape); i < count; i++)
		{
			RefVar member(GetArraySlotRef(shape, i));
			if (NOTNIL(member) && !IsStyleFrame(member))
			{
				Rect memberBounds;
				ShapeBounds(member, &memberBounds);
				UnionRect(bounds, &memberBounds, bounds);
			}
		}
		return;
	}
	RefVar cls(ClassOf(shape));
	if (EQRef(cls, RSSYMregion))
	{
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		memmove(bounds, (char*) BinaryData(data) + 4, sizeof(Rect));
		return;
	}
	if (EQRef(cls, RSSYMink))
	{
		RefVar data(GetProtoVariable(shape, RSSYMbounds, nil));
		RectOf(data, bounds);
		return;
	}
	if (EQRef(cls, RSSYMpolygon))
	{
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		memmove(bounds, (char*) BinaryData(data) + 4, sizeof(Rect));
		bounds->right++;
		bounds->bottom++;
		return;
	}
	if (!IsPrimShape(shape))
		Throw((ExceptionName) kGrafException, (void*) kGrafErrNotAShape, nil);
	Boolean isLine = EQRef(cls, RSSYMline);
	if (EQRef(cls, RSSYMbitmap) || EQRef(cls, RSSYMpicture) || EQRef(cls, RSSYMtext) || EQRef(cls, SYMTextBox()))
	{
		RefVar data(GetProtoVariable(shape, RSSYMbounds, nil));
		RectOf(data, bounds);
	}
	else
		RectOf(shape, bounds);
	if (isLine)
	{
		if (bounds->bottom <= bounds->top)
		{
			if (bounds->bottom == bounds->top)
				bounds->bottom++;
			else
			{
				short t = bounds->top;
				bounds->top = bounds->bottom;
				bounds->bottom = t;
			}
		}
		if (bounds->right <= bounds->left)
		{
			if (bounds->right == bounds->left)
				bounds->right++;
			else
			{
				short l = bounds->left;
				bounds->left = bounds->right;
				bounds->right = l;
			}
		}
	}
}


// Where a shape is drawn: the caller's origin, or - while a transform is
// in force - the offset the transforms come to, because SetStyle folded
// the origin into the first of them.
//
// DEVIATION: the ROM adds the origin only when there is no transform and
// leaves the mapping to TQDScaler, which puts every coordinate QuickDraw
// is given through the stack of them.  That scaler is NOT YET (see
// qd/Transform.h), so the offset is added here instead; a transform that
// really scales is still drawn unscaled.
static Point
DrawOrigin(const Point& origin, TStyleSave* style)
{
	return style->fTransformDepth == 0 ? origin : TQDScaler::Offset();
}


// ROM 0x000dfc60 GetBoundsRect__FRC6RefVarP5TRectRC6TPointP10TStyleSave
// A shape's bounds slot, offset by the origin (not when scaling).
void
GetBoundsRect(RefArg shape, Rect* bounds, const Point& origin, TStyleSave* style)
{
	RefVar data(GetProtoVariable(shape, RSSYMbounds, nil));
	RectOf(data, bounds);
	Point at = DrawOrigin(origin, style);
	OffsetRect(bounds, at.h, at.v);
}


// ROM 0x000e148c WedgeBox__FP5TRectsT2
// The box of the wedge of the oval in the box between the angles.  NOT
// YET RECONSTRUCTED: the ROM's quadrant arithmetic - the whole oval's box
// is answered (only whole turns are drawn, see DrawArc).
void
WedgeBox(Rect* /*box*/, short /*startAngle*/, short /*arcAngle*/)
{
}


// ROM 0x000df7c8 DrawShape__FRC6RefVarT1RC6TPoint
// The shape (or list) drawn with the style from the origin: the pen
// state saved and put back, a level for what the style sets.
void
DrawShape(RefArg shape, RefArg style, const Point& origin)
{
	TStyleSave styleSave;
	PenState pen;
	GetPenState(&pen);
	if (styleSave.SetStyle(style, origin, 0))
	{
		SaveLevel level;
		styleSave.BeginLevel(&level);
		newton_try
		{
			DrawShapeList(shape, origin, &styleSave);
		}
		cleanup
		{
			styleSave.EndLevel();
			SetPenState(&pen);
		}
		end_try;
		styleSave.EndLevel();
	}
	SetPenState(&pen);
}


// ROM 0x000dfabc DrawShapeList__FRC6RefVarRC6TPointP10TStyleSave
// A list: each member drawn - a style frame is put in force for the rest
// (a style leaving nothing visible skips them); a nested list is drawn
// in its own level with the style in force put back after it; nil is
// skipped.  A single shape: DrawOneShape.
void
DrawShapeList(RefArg shape, const Point& origin, TStyleSave* style)
{
	if (!IsArray(shape))
	{
		DrawOneShape(shape, origin, style);
		return;
	}
	Boolean hidden = false;
	for (long i = 0, count = Length(shape); i < count; i++)
	{
		RefVar member(GetArraySlotRef(shape, i));
		if (IsStyleFrame(member))
		{
			hidden = !style->SetStyle(member, origin, 0);
			continue;
		}
		if (ISNIL(member) || hidden)
			continue;
		if (!IsArray(member))
		{
			DrawOneShape(member, origin, style);
			continue;
		}
		RefVar savedStyle(style->fStyle);
		SaveLevel level;
		style->BeginLevel(&level);
		newton_try
		{
			DrawShapeList(member, origin, style);
			style->SetStyle(savedStyle, origin, 1);
		}
		cleanup
		{
			style->EndLevel();
		}
		end_try;
		style->EndLevel();
	}
}


// the outline's pattern set: the pen pattern, else black
static void
SetPenPattern(TStyleSave* style)
{
	PatternHandle pattern = style->fPenPattern.fPattern;
	if (pattern == nil)
		pattern = GetStdPattern(blackPat);
	SetFgPattern(pattern);
}


// ROM 0x000dfd00 DrawOneShape__FRC6RefVarRC6TPointP10TStyleSave
// One shape drawn from the origin: with the fill pattern set when the
// style fills; nothing when its bounds miss the port's clip.  A
// rectangle, oval, round rectangle or wedge is painted (fill) then
// framed (pen) in the pen pattern; a line drawn from its first end to
// its second in the pen pattern; a polygon or region painted and framed
// through a handle over its data (offset to the origin and back); a
// bitmap drawn by DrawBitmap (its mask first in srcBic for a patCopy
// style); a text shape drawn as a line of its font from its bounds'
// left at its top plus the ascent, aligned across the bounds by the
// justification, in the text pattern (else the fill's when filling); a
// TextBox wrapped into its bounds by TextBox, clipped to them.  NOT YET
// RECONSTRUCTED: 'picture shapes (QuickDraw pictures), ink, scaling.
void
DrawOneShape(RefArg shape, const Point& origin, TStyleSave* style)
{
	if (style->fFill)
	{
		PatternHandle pattern = style->fFillPattern.fPattern;
		if (pattern == nil)
			pattern = GetStdPattern(blackPat);
		SetFgPattern(pattern);
	}
	RefVar cls(ClassOf(shape));
	Rect bounds;
	ShapeBounds(shape, &bounds);
	{
		Point at = DrawOrigin(origin, style);
		OffsetRect(&bounds, at.h, at.v);
	}
	if (style->fClipDepth != 0 && style->fTransformDepth == 0)
	{
		GrafPort* port;
		GetPort(&port);
		Rect clip = (*port->clipRgn)->rgnBBox;
		Rect common;
		if (!SectRect(&bounds, &clip, &common))
			return;
	}
	if (EQRef(cls, RSSYMrectangle) || EQRef(cls, RSSYMoval))
	{
		Rect r;
		RectOf(shape, &r);
		{
			Point at = DrawOrigin(origin, style);
			OffsetRect(&r, at.h, at.v);
		}
		Boolean oval = EQRef(cls, RSSYMoval);
		if (style->fFill)
			oval ? PaintOval(&r) : PaintRect(&r);
		if (style->fPen)
		{
			SetPenPattern(style);
			oval ? FrameOval(&r) : FrameRect(&r);
		}
		return;
	}
	if (EQRef(cls, RSSYMline))
	{
		Rect r;
		RectOf(shape, &r);
		{
			Point at = DrawOrigin(origin, style);
			OffsetRect(&r, at.h, at.v);
		}
		SetPenPattern(style);
		MoveTo(r.left, r.top);
		LineTo(r.right, r.bottom);
		return;
	}
	if (EQRef(cls, RSSYMroundrectangle) || EQRef(cls, RSSYMwedge))
	{
		struct { Rect fRect; short fA; short fB; } data;
		memmove(&data, BinaryData(shape), sizeof(data));
		Point at = DrawOrigin(origin, style);
		OffsetRect(&data.fRect, at.h, at.v);
		if (EQRef(cls, RSSYMwedge))
		{
			if (style->fFill)
				PaintArc(&data.fRect, data.fA, data.fB);
			if (style->fPen)
			{
				SetPenPattern(style);
				FrameArc(&data.fRect, data.fA, data.fB);
			}
		}
		else
		{
			if (style->fFill)
				PaintRoundRect(&data.fRect, data.fA, data.fA);
			if (style->fPen)
			{
				SetPenPattern(style);
				FrameRoundRect(&data.fRect, data.fA, data.fA);
			}
		}
		return;
	}
	if (EQRef(cls, RSSYMink))
	{
		// ink is drawn out of the box it was made in (originalBounds)
		// and into the one it is to fill now, so a sketch that has been
		// resized stretches; the pen's width is what says how the points
		// are to be thinned on the way.
		Rect to;
		Rect from;
		RectOf(RefVar(GetProtoVariable(shape, RSSYMbounds, nil)), &to);
		RectOf(RefVar(GetProtoVariable(shape, RSSYMoriginalbounds, nil)), &from);
		if (style->fTransformDepth == 0)
		{
			Point at = DrawOrigin(origin, style);
			OffsetRect(&to, at.h, at.v);
			OffsetRect(&from, at.h, at.v);
		}
		if (style->fPen)
			SetPenPattern(style);
		PenState pen;
		GetPenState(&pen);
		InkDrawInRect(RefVar(GetProtoVariable(shape, RSSYMdata, nil)), (ULong) pen.pnSize.h,
					  &from, &to, false);
		return;
	}
	if (EQRef(cls, RSSYMpolygon) || EQRef(cls, RSSYMregion))
	{
		Boolean isPoly = EQRef(cls, RSSYMpolygon);
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		LockRef(data);
		Handle h = NewHandle(Length(data));
		if (h != nil)
		{
			memmove(*h, BinaryData(data), Length(data));
			Point at = DrawOrigin(origin, style);
			isPoly ? OffsetPoly((PolyHandle) h, at.h, at.v) : OffsetRgn((RgnHandle) h, at.h, at.v);
			if (style->fFill)
				isPoly ? PaintPoly((PolyHandle) h) : PaintRgn((RgnHandle) h);
			if (style->fPen)
			{
				SetPenPattern(style);
				isPoly ? FramePoly((PolyHandle) h) : FrameRgn((RgnHandle) h);
			}
			DisposHandle(h);
		}
		UnlockRef(data);
		return;
	}
	if (EQRef(cls, RSSYMbitmap))
	{
		Rect box;
		GetBoundsRect(shape, &box, origin, style);
		long mode = style->fTransferMode;
		RefVar mask(GetFrameSlotRef(shape, RSSYMmask));
		if (mode == patCopy)
		{
			if (NOTNIL(mask))
				DrawBitmap(mask, &box, srcBic);
			mode = srcOr;
		}
		DrawBitmap(shape, &box, mode);
		return;
	}
	if (EQRef(cls, RSSYMtext))
	{
		Rect box;
		GetBoundsRect(shape, &box, origin, style);
		StyleRecord record;
		CreateTextStyleRecord(style->fFont, &record);
		PatternHandle textPattern = nil;
		if (style->fTextPatternSet)
			textPattern = style->fTextPattern.fPattern;
		else if (style->fFill)
			textPattern = style->fFillPattern.fPattern;
		if (style->fTextPatternSet || style->fFill)
		{
			if (textPattern == nil)
				textPattern = GetStdPattern(blackPat);
			record.fFontPattern = AddressToRef(textPattern);
		}
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		TRichString rich((UniChar*) BinaryData(data), Length(data));
		if (EQRef(ClassOf(data), SYMTextBox()))
		{
			long hJustify = 0;
			if (style->fAlignment == 0x8000)
				hJustify = 2;
			else if (style->fAlignment == 0x10000)
				hJustify = 1;
			TRegionVar savedClip;
			TRegionVar clip;
			GetClip(savedClip);
			GetClip(clip);
			TRectangularRegion boxRgn(box);
			SectRgn(clip, boxRgn, clip);
			SetClip(clip);
			RefVar spec(Clone(RefVar(Rcanonicalgrayfontspec)));
			SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(GetFontSize(style->fFont))));
			SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(GetFontFace(style->fFont))));
			SetFrameSlot(spec, RSSYMfamily, RefVar(GetFontFamilySym(style->fFont)));
			SetFrameSlot(spec, RSSYMcolor, RefVar(record.fFontPattern));
			TextBox(rich, spec, box, hJustify, 0, srcOr);
			SetClip(savedClip);
		}
		else
		{
			FontInfo fontInfo;
			GetStyleFontInfo(&record, &fontInfo);
			TextOptions options;
			memset(&options, 0, sizeof(options));
			options.fJustification = style->fJustification;
			options.fAlignment = style->fAlignment;
			options.fWidth = ToFixed(box.right - box.left);
			options.fTransferMode = style->fTransferMode == patCopy ? srcOr : style->fTransferMode;
			FPoint where;
			where.x = ToFixed(box.left);
			where.y = ToFixed(box.top + fontInfo.ascent);
			DrawRichString(rich, 0, rich.Length(), &record, where, &options, nil);
		}
		DisposeStyleRecord(&record);
		return;
	}
	// 'picture, 'ink: NOT YET RECONSTRUCTED - nothing drawn
}


/*------------------------------------------------------------------------------
	N a t i v e s
------------------------------------------------------------------------------*/

// (defined below, with the other shape makers)
static Ref	FMakeText(RefArg rcvr, RefArg str, RefArg left, RefArg top, RefArg right, RefArg bottom);


// The width of a run of the string, in whole pixels.
static long
RunWidth(TRichString& rich, long start, long length, StyleRecord* style)
{
	FPoint where;
	where.x = 0;
	where.y = 0;
	TextBoundsInfo bounds;
	MeasureRichString(rich, (ULong) start, length, style, where, nil, &bounds);
	return (short) RoundFixed(bounds.fWidth);
}


// ROM 0x000dd234 FMakeTextLines
// MakeTextLines(text, bounds, lineHeight, font) - the text broken into
// lines that fit the bounds, each line a MakeText shape of its own, as an
// array ready to be drawn or put in a shape list.  With no line height it
// is the font's ascent, descent and leading.
//
// The wrapping is the ordinary one done the ordinary way: walk to the end
// of a word, measure from the start of the line, and if it still fits
// remember where the word ended and go on to the next.  When it does not
// fit, the line breaks at the last word that did - and when even the
// first word of the line is too wide, the line is backed off a character
// at a time until what is left fits, so a long word is broken rather than
// lost.
//
// ==> nil when the bounds are not even one line tall; an array otherwise,
// shortened to the lines actually used when the text runs out before the
// bounds do.
static Ref
FMakeTextLines(RefArg /*rcvr*/, RefArg text, RefArg boundsFrame, RefArg lineHeightRef, RefArg font)
{
	Rect bounds;
	FromObject(boundsFrame, bounds);
	TRichString rich(text);
	StyleRecord style;
	style.fFontPattern = NILREF;
	style.fPattern = nil;
	CreateTextStyleRecord(font, &style);
	FontInfo info;
	GetStyleFontInfo(&style, &info);
	long lineHeight = ISNIL(lineHeightRef)
					? info.ascent + info.descent + info.leading
					: RINT(lineHeightRef);
	long lines = (bounds.bottom - bounds.top) / lineHeight;
	if (lines <= 0)
	{
		DisposeStyleRecord(&style);
		return NILREF;
	}
	long fits = bounds.right - bounds.left;
	RefVar result(MakeArray(lines));
	RefVar piece;
	long used = 0;				// lines filled so far
	long lineStart = 0;			// the first character of the line being built
	long at = 0;				// where the walk has got to
	long lastBreak = -1;		// the end of the last word that fitted
	long y = bounds.top;
	for (;;)
	{
		// on to the end of the word
		UniChar ch;
		while ((ch = rich.GetChar((ULong) at)) != 0 && !IsWhiteSpace(ch))
			at++;
		long width = RunWidth(rich, lineStart, at - lineStart, &style);
		if (IsSpace(ch) && width <= fits)
		{
			// it still fits: remember the break and take the next word too
			lastBreak = at;
			while ((ch = rich.GetChar((ULong) at)) != 0 && IsSpace(ch))
				at++;
			continue;
		}
		if (width > fits && lastBreak == -1)
		{
			// the first word of the line is wider than the line: back off
			// a character at a time until what is left fits
			do
			{
				at--;
				if (at == lineStart)
					break;
				width = RunWidth(rich, lineStart, at - lineStart, &style);
			}
			while (width >= fits);
			if (at == lineStart)
			{
				SetLength(result, used);		// nothing more will fit
				break;
			}
		}
		if (width > fits)
			at = lastBreak;						// break at the last word that did
		ch = rich.GetChar((ULong) at);
		// the whole string, when it is one line and starts at the beginning
		piece = (ch == 0 && lineStart == 0) ? (Ref) text
			  : Substring(text, lineStart, at - lineStart);
		long line = used++;
		RefVar shape(FMakeText(RefVar(NILREF), piece,
							   RefVar(MAKEINT(bounds.left)), RefVar(MAKEINT(y)),
							   RefVar(MAKEINT(bounds.right)), RefVar(MAKEINT(y + lineHeight))));
		SetArraySlot(result, line, shape);
		if (ch == 0)
		{
			SetLength(result, used);			// the text ran out first
			break;
		}
		if (used == lines)
			break;								// the bounds ran out first
		// past the whitespace, and on to the next line
		while ((ch = rich.GetChar((ULong) at)) != 0 && IsWhiteSpace(ch))
			at++;
		lineStart = at;
		lastBreak = -1;
		y += lineHeight;
	}
	DisposeStyleRecord(&style);
	return result;
}


// ROM 0x000e17bc HitShape__FRC6RefVarRC6TPointT1
// Whether the point is in the shape.  A list of shapes is walked - the
// frames in it are the style frames, and are skipped - and the index of
// the first one that is hit is added to `path`; because the walk is
// recursive and each level adds its index on the way out, the path comes
// out innermost first (FHitShape turns it round).
//
// Everything is cut to the shape's bounds first.  A rectangle, a bitmap,
// a piece of text and a picture are then their bounds and nothing more; a
// line is tested with a tolerance that grows with its length (Aligned); a
// region is tested against its own bytes.  Anything else - an oval, a
// round rectangle, a polygon, a wedge - is *drawn* into a region and the
// point tested against that, which is the same rasteriser that would have
// put it on the screen, so what is hit is exactly what is seen.
Boolean
HitShape(RefArg shape, const Point& pt, RefArg path)
{
	Boolean hit = false;
	if (IsArray(shape))
	{
		long index = 0;
		TObjectIterator* iter = NewTObjectIterator(shape);
		for (; !iter->Done(); iter->Next(), index++)
		{
			RefVar value(iter->Value());
			if (ISNIL(value) || EQRef(ClassOf(value), RSSYMframe))
				continue;					// a style frame, not a shape
			hit = HitShape(value, pt, path);
			if (hit)
			{
				AddArraySlot(path, RefVar(MAKEINT(index)));
				break;
			}
		}
		DeleteTObjectIterator(iter);
		return hit;
	}

	Rect bounds;
	ShapeBounds(shape, &bounds);
	if (!PtInRect(pt, &bounds))
		return false;
	RefVar shapeClass(ClassOf(shape));
	if (EQRef(shapeClass, RSSYMrectangle) || EQRef(shapeClass, RSSYMbitmap)
		|| EQRef(shapeClass, RSSYMtext) || EQRef(shapeClass, RSSYMpicture))
		return true;						// the bounds are the shape
	if (EQRef(shapeClass, RSSYMline))
	{
		// a line's bounds are its two ends, in order
		Point from, to;
		from.v = bounds.top;
		from.h = bounds.left;
		to.v = bounds.bottom;
		to.h = bounds.right;
		return Aligned(pt, from, to) == 3;
	}
	if (EQRef(shapeClass, RSSYMregion))
	{
		// the region's bytes are already a Macintosh region; a handle is
		// faked round them rather than copying them
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		TBinaryDataPtr bits(data);
		Handle fake = NewFakeHandle((Ptr) (char*) bits, Length(data));
		hit = PtInRgn(pt, (RgnHandle) fake);
		DisposHandle(fake);
		return hit;
	}

	// anything else: drawn into a region, and the point tested against it
	TRegionVar rgn;
	OpenRgn();
	// (the ROM turns the QD scaler off around this - TQDScaler::ForceScaling
	//  0x002f8e28 - so that the shape records at its own size.  NOT YET
	//  RECONSTRUCTED: the scaler, so there is nothing to turn off.)
	Point origin;
	origin.h = 0;
	origin.v = 0;
	newton_try
	{
		DrawShape(shape, RefVar(NILREF), origin);
	}
	newton_catch_all
	{
		CloseRgn(rgn);
		rethrow;
	}
	end_try;
	CloseRgn(rgn);
	return PtInRgn(pt, rgn);
}


// ROM 0x000e1640 FHitShape
// HitShape(shape, x, y) - whether the point is in the shape.  A plain
// shape answers true or nil; a list of shapes answers the path down to
// the one that was hit, outermost first, which is what a script indexes
// the list with to find out what was tapped.
static Ref
FHitShape(RefArg /*rcvr*/, RefArg shape, RefArg x, RefArg y)
{
	Point pt;
	pt.h = (short) RINT(x);
	pt.v = (short) RINT(y);
	RefVar path(AllocateArray(RSSYMpathexpr, 0));
	Boolean hit = HitShape(shape, pt, path);
	long depth = Length(path);
	if (depth == 0)
		return MAKEBOOLEAN(hit);
	// the path was built innermost first
	RefVar swap;
	for (long i = 0; i < depth / 2; i++)
	{
		long j = depth - 1 - i;
		swap = GetArraySlotRef(path, j);
		SetArraySlot(path, j, RefVar(GetArraySlotRef(path, i)));
		SetArraySlot(path, i, swap);
	}
	return path;
}


// ROM 0x000dc844 FDrawShape
// DrawShape(shape, style) on a view: drawn from the view's top left (a
// slot of the ROM's root template, so every view has it).
Ref
FDrawShape(RefArg rcvr, RefArg shape, RefArg style)
{
	TView* view = FailGetView(rcvr);
	Point origin = MakePoint(view->viewBounds.left, view->viewBounds.top);
	DrawShape(shape, style, origin);
	return NILREF;
}


// ROM 0x000dc894 FMakeRect
static Ref
FMakeRect(RefArg /*rcvr*/, RefArg left, RefArg top, RefArg right, RefArg bottom)
{
	return MakeRectShape(RSSYMrectangle, left, top, right, bottom);
}


// ROM 0x000dda10 FMakeOval
static Ref
FMakeOval(RefArg /*rcvr*/, RefArg left, RefArg top, RefArg right, RefArg bottom)
{
	return MakeRectShape(RSSYMoval, left, top, right, bottom);
}


// ROM 0x000e0dd0 FMakeRoundRect
// A 12-byte 'roundRectangle: the rectangle and the corners' diameter.
static Ref
FMakeRoundRect(RefArg /*rcvr*/, RefArg left, RefArg top, RefArg right, RefArg bottom, RefArg diameter)
{
	struct { Rect fRect; short fDiameter; short fPad; } data;
	data.fRect.top = (short) RINT(top);
	data.fRect.left = (short) RINT(left);
	data.fRect.bottom = (short) RINT(bottom);
	data.fRect.right = (short) RINT(right);
	data.fDiameter = (short) RINT(diameter);
	data.fPad = 0;
	RefVar shape(AllocateBinary(RSSYMroundrectangle, sizeof(data)));
	memmove(BinaryData(shape), &data, sizeof(data));
	return shape;
}


// ROM 0x000de860 FMakeLine
// An 8-byte 'line: {y1, x1, y2, x2} - a rectangle of the two ends.
static Ref
FMakeLine(RefArg /*rcvr*/, RefArg x1, RefArg y1, RefArg x2, RefArg y2)
{
	Rect r;
	r.top = (short) RINT(y1);
	r.left = (short) RINT(x1);
	r.bottom = (short) RINT(y2);
	r.right = (short) RINT(x2);
	RefVar shape(AllocateBinary(RSSYMline, sizeof(Rect)));
	memmove(BinaryData(shape), &r, sizeof(Rect));
	return shape;
}


// ROM 0x000e2b60 FMakeWedge
// A 12-byte 'wedge: the oval's rectangle, the start angle and the arc.
static Ref
FMakeWedge(RefArg /*rcvr*/, RefArg left, RefArg top, RefArg right, RefArg bottom, RefArg startAngle, RefArg arcAngle)
{
	struct { Rect fRect; short fStart; short fArc; } data;
	data.fRect.top = (short) RINT(top);
	data.fRect.left = (short) RINT(left);
	data.fRect.bottom = (short) RINT(bottom);
	data.fRect.right = (short) RINT(right);
	data.fStart = (short) RINT(startAngle);
	data.fArc = (short) RINT(arcAngle);
	RefVar shape(AllocateBinary(RSSYMwedge, sizeof(data)));
	memmove(BinaryData(shape), &data, sizeof(data));
	return shape;
}


// ROM 0x000e3a78 FMakePolygon
// A 'polygon frame (canonicalPolygonShape) whose data is a Polygon of the
// points [x0, y0, x1, y1, ...], its box their bounds.
static Ref
FMakePolygon(RefArg /*rcvr*/, RefArg points)
{
	long count = Length(points) / 2;
	long size = count * 4 + 12;
	RefVar shape(Clone(RefVar(Rcanonicalpolygonshape)));
	RefVar data(AllocateBinary(RSSYMpolygondata, size));
	SetFrameSlot(shape, RSSYMdata, data);
	Polygon* poly = (Polygon*) BinaryData(data);
	poly->polySize = (short) size;
	poly->filler = 0;
	Rect box;
	SetRect(&box, 0x7fff, 0x7fff, -0x8000, -0x8000);
	for (long i = 0; i < count; i++)
	{
		Point pt;
		pt.h = (short) RINT(GetArraySlotRef(points, 2 * i));
		pt.v = (short) RINT(GetArraySlotRef(points, 2 * i + 1));
		poly = (Polygon*) BinaryData(data);
		poly->polyPoints[i] = pt;
		if (pt.v < box.top) box.top = pt.v;
		if (pt.h < box.left) box.left = pt.h;
		if (pt.v > box.bottom) box.bottom = pt.v;
		if (pt.h > box.right) box.right = pt.h;
	}
	if (count == 0)
		SetEmptyRect(&box);
	poly = (Polygon*) BinaryData(data);
	poly->polyBBox = box;
	return shape;
}


// ROM 0x000e31b4 FMakeRegion
// A 'region frame (canonicalRegionShape) whose data is the region of the
// shape: the shape drawn (with no style) into an open region.
static Ref
FMakeRegion(RefArg /*rcvr*/, RefArg shape)
{
	TRegionVar rgn;
	OpenRgn();
	newton_try
	{
		DrawShape(shape, RefVar(NILREF), MakePoint(0, 0));
	}
	cleanup
	{
		CloseRgn(rgn);
	}
	end_try;
	CloseRgn(rgn);
	long size = (*rgn)->rgnSize;
	RefVar result(Clone(RefVar(Rcanonicalregionshape)));
	RefVar data(AllocateBinary(RSSYMregiondata, size));
	SetFrameSlot(result, RSSYMdata, data);
	memmove(BinaryData(data), *rgn, size);
	return result;
}


// ROM 0x000dcfc8 FMakeText
// A 'text frame (canonicalTextShape): its bounds a 'boundsRect binary,
// its data a copy of the string as 'textData.
static Ref
FMakeText(RefArg /*rcvr*/, RefArg str, RefArg left, RefArg top, RefArg right, RefArg bottom)
{
	RefVar shape(Clone(RefVar(Rcanonicaltextshape)));
	SetFrameSlot(shape, RSSYMbounds, RefVar(MakeRectShape(RSSYMboundsrect, left, top, right, bottom)));
	RefVar data(Clone(str));
	SetClass(data, RSSYMtextdata);
	SetFrameSlot(shape, RSSYMdata, data);
	return shape;
}


// ROM 0x000dd094 FMakeTextBox
// The same with the data a 'textBox: wrapped into the bounds when drawn.
static Ref
FMakeTextBox(RefArg /*rcvr*/, RefArg str, RefArg left, RefArg top, RefArg right, RefArg bottom)
{
	RefVar shape(Clone(RefVar(Rcanonicaltextshape)));
	SetFrameSlot(shape, RSSYMbounds, RefVar(MakeRectShape(RSSYMboundsrect, left, top, right, bottom)));
	RefVar data(Clone(str));
	SetClass(data, RefVar(SYMTextBox()));
	SetFrameSlot(shape, RSSYMdata, data);
	return shape;
}


// ROM 0x000ddd5c FShapeBounds
static Ref
FShapeBounds(RefArg /*rcvr*/, RefArg shape)
{
	Rect bounds;
	ShapeBounds(shape, &bounds);
	return ToObject(bounds);
}


// ROM 0x000ddd84 FScaleShape
// ScaleShape(shape, src, dst): the shape stretched in place, as though
// the rectangle `src` had been pulled into `dst` and the shape had come
// with it.  A nil `src` means the shape's own bounds, so the shape is
// simply fitted into `dst`.
//
// Each kind of shape is mapped the way QuickDraw maps it: a region and a
// polygon through their own data (MapRgn, MapPoly), a line through its
// two points, and everything else through the rectangle it keeps - its
// `bounds` for a bitmap, picture, text or ink, and the binary itself for
// the rest.  A list of shapes is mapped member by member from the list's
// own bounds, the style frames in it left alone.
//
// A mapped region is not the size it was, so its data is grown or shrunk
// to the region that came back.
static Ref
FScaleShape(RefArg rcvr, RefArg shape, RefArg src, RefArg dst)
{
	RefVar cls(ClassOf(shape));
	Boolean ownBounds = ISNIL(src);				// (nil: the shape's own bounds)
	Rect from;
	Rect to;
	if ((!ownBounds && !FromObject(src, from)) || !FromObject(dst, to))
		return shape;

	if (IsArray(shape))
	{
		// a list: the members are mapped out of the list's own bounds, so
		// that they keep their places within it
		RefVar listSrc(src);
		if (ownBounds)
		{
			Rect bounds;
			ShapeBounds(shape, &bounds);
			listSrc = ToObject(bounds);
		}
		for (TObjectIterator iter(shape, false); !iter.Done(); iter.Next())
		{
			RefVar member(iter.Value());
			if (NOTNIL(member) && !EQRef(ClassOf(member), RSSYMframe))
				FScaleShape(rcvr, member, listSrc, dst);
		}
		return shape;
	}

	if (EQRef(cls, RSSYMregion))
	{
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		TRegionVar region;
		LockRef(data);
		Handle fake = NewFakeHandle(BinaryData(data), Length(data));
		CopyRgn((RgnHandle) fake, region);
		DisposHandle(fake);
		UnlockRef(data);
		if (ownBounds)
			from = (*(RgnHandle) region)->rgnBBox;
		MapRgn(region, &from, &to);
		Size size = GetHandleSize((Handle) (RgnHandle) region);
		SetLength(data, size);
		BlockMove(*(RgnHandle) region, BinaryData(data), size);
		return shape;
	}

	if (EQRef(cls, RSSYMpolygon))
	{
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		LockRef(data);
		PolyHandle poly = (PolyHandle) NewFakeHandle(BinaryData(data), Length(data));
		if (ownBounds)
			from = (*poly)->polyBBox;
		MapPoly(poly, &from, &to);
		DisposHandle((Handle) poly);
		UnlockRef(data);
		return shape;
	}

	if (EQRef(cls, RSSYMline))
	{
		if (ownBounds)
			ShapeBounds(shape, &from);
		LockRef(shape);
		Point* points = (Point*) BinaryData(shape);
		MapPt(&points[0], &from, &to);
		MapPt(&points[1], &from, &to);
		UnlockRef(shape);
		return shape;
	}

	RefVar binary(shape);
	if (EQRef(cls, RSSYMbitmap) || EQRef(cls, RSSYMpicture) || EQRef(cls, RSSYMtext) || EQRef(cls, RSSYMink))
		binary = GetProtoVariable(shape, RSSYMbounds, nil);
	LockRef(binary);
	Rect* rect = (Rect*) BinaryData(binary);
	if (ownBounds)
		from = *rect;
	MapRect(rect, &from, &to);
	UnlockRef(binary);
	return shape;
}


// ROM 0x000dda60 FOffsetShape
// The shape moved in place: a list's members each (styles left alone); a
// region's or polygon's data offset; a bitmap's, picture's, text's or
// ink's bounds; else the binary's rectangle.  ==> the shape.
static Ref
FOffsetShape(RefArg rcvr, RefArg shape, RefArg dx, RefArg dy)
{
	long dh = (short) RINT(dx);
	long dv = (short) RINT(dy);
	if (dh == 0 && dv == 0)
		return shape;
	if (IsArray(shape))
	{
		for (long i = 0, count = Length(shape); i < count; i++)
		{
			RefVar member(GetArraySlotRef(shape, i));
			if (NOTNIL(member) && !IsStyleFrame(member))
				FOffsetShape(rcvr, member, dx, dy);
		}
		return shape;
	}
	RefVar cls(ClassOf(shape));
	if (EQRef(cls, RSSYMregion) || EQRef(cls, RSSYMpolygon))
	{
		RefVar data(GetProtoVariable(shape, RSSYMdata, nil));
		LockRef(data);
		Handle fake = NewFakeHandle(BinaryData(data), Length(data));
		if (EQRef(cls, RSSYMregion))
			OffsetRgn((RgnHandle) fake, dh, dv);
		else
			OffsetPoly((PolyHandle) fake, dh, dv);
		DisposHandle(fake);
		UnlockRef(data);
		return shape;
	}
	RefVar binary(shape);
	if (EQRef(cls, RSSYMbitmap) || EQRef(cls, RSSYMpicture) || EQRef(cls, RSSYMtext) || EQRef(cls, RSSYMink) || EQRef(cls, SYMTextBox()))
		binary = GetProtoVariable(shape, RSSYMbounds, nil);
	OffsetRect((Rect*) BinaryData(binary), dh, dv);
	return shape;
}


// ROM 0x000dc8fc FMakeShape
// MakeShape(object): a shape made of whatever it is given.
//
//   - a 'polygonShape binary - what the recogniser answers, a verb and a
//     run of points - becomes an oval (verb 0) or a rectangle (verb 10
//     or 11) of the points' bounds, or a 'polygon shape holding them;
//   - a 'picture binary (a QuickDraw picture, its frame in the eight
//     bytes after the size) becomes a picture shape;
//   - a frame with a `bits` or `colorData` slot - a bitmap frame -
//     becomes a bitmap shape with the same data, bounds and mask;
//   - an array (a shape list), a bounds frame (a rectangle) or a shape
//     already is taken as it stands;
//   - anything else that is a view's context becomes a picture of the
//     view.
//
// DEVIATION: the ROM locks the object it reads (TObjectPtr) and works
// from the pointer; here the data is fetched again after each allocation
// instead, which is the same thing on a heap that may move it.
static Ref
FMakeShape(RefArg /*rcvr*/, RefArg obj)
{
	RefVar shape;
	RefVar cls(ClassOf(obj));
	Rect bounds;
	if (EQRef(cls, RSSYMpolygonshape))
	{
		long verb = *(const short*) BinaryData(obj);
		long count = *(const short*) (BinaryData(obj) + 2);
		if (verb == 0 || verb == 10 || verb == 11)
		{
			shape = AllocateBinary(verb == 0 ? RSSYMoval : RSSYMrectangle, sizeof(Rect));
			Rect* r = (Rect*) BinaryData(shape);
			// the mark UnionPt reads as "nothing yet"; left and right are
			// left as they lie, which shows with no points at all
			r->top = (short) 0x8000;
			r->bottom = (short) 0x8000;
			const Point* points = (const Point*) (BinaryData(obj) + 4);
			for (long i = 0; i < count; i++)
				UnionPt(r, points[i]);
		}
		else
		{
			long size = count * 4 + 12;
			shape = Clone(RefVar(Rcanonicalpolygonshape));
			RefVar data(AllocateBinary(RSSYMpolygondata, size));
			SetFrameSlot(shape, RSSYMdata, data);
			Polygon* poly = (Polygon*) BinaryData(data);
			poly->polySize = (short) size;
			Rect box;
			box.top = (short) 0x8000;
			box.bottom = (short) 0x8000;
			const Point* points = (const Point*) (BinaryData(obj) + 4);
			for (long i = 0; i < count; i++)
			{
				UnionPt(&box, points[i]);
				poly->polyPoints[i] = points[i];
			}
			poly->polyBBox = box;
		}
	}
	else if (EQRef(cls, RSSYMpicture) && IsBinary(obj))
	{
		memmove(&bounds, BinaryData(obj) + 2, sizeof(Rect));
		shape = Clone(RefVar(Rcanonicalpictureshape));
		RefVar box(AllocateBinary(RSSYMboundsrect, sizeof(Rect)));
		SetFrameSlot(shape, RSSYMbounds, box);
		memmove(BinaryData(box), &bounds, sizeof(Rect));
		SetFrameSlot(shape, RSSYMdata, obj);
	}
	else if (EQRef(cls, RSSYMframe) && (FrameHasSlot(obj, RSSYMbits) || FrameHasSlot(obj, RSSYMcolordata)))
	{
		if (!FromObject(RefVar(GetProtoVariable(obj, RSSYMbounds, nil)), bounds))
			Throw((ExceptionName) kGrafException, (void*) kGrafErrBadBounds, nil);
		RefVar box(AllocateBinary(RSSYMboundsrect, sizeof(Rect)));
		memmove(BinaryData(box), &bounds, sizeof(Rect));
		shape = Clone(RefVar(Rcanonicalbitmapshape));
		SetFrameSlot(shape, RSSYMcolordata, RefVar(GetFrameSlotRef(obj, RSSYMcolordata)));
		SetFrameSlot(shape, RSSYMdata, RefVar(GetFrameSlotRef(obj, RSSYMbits)));
		SetFrameSlot(shape, RSSYMbounds, box);
		if (FrameHasSlot(obj, RSSYMmask))
			SetFrameSlot(shape, RSSYMmask, RefVar(GetFrameSlotRef(obj, RSSYMmask)));
	}
	else if (IsArray(obj))
		shape = obj;
	else if (IsFrame(obj) && FromObject(obj, bounds))
	{
		shape = AllocateBinary(RSSYMrectangle, sizeof(Rect));
		memmove(BinaryData(shape), &bounds, sizeof(Rect));
	}
	else if (IsPrimShape(obj))
		shape = obj;
	else if (GetView(obj) != nil)
	{
		// NOT YET RECONSTRUCTED: the view's own picture - the ROM asks the
		// view for its bounds and hands them to CommonMakePict 0x000dc8c0,
		// which is not reconstructed; nil stands in for the picture.
	}
	return shape;
}

// ROM 0x000dda3c FIsPrimShape
static Ref
FIsPrimShape(RefArg /*rcvr*/, RefArg shape)
{
	return MAKEBOOLEAN(IsPrimShape(shape));
}


// ROM 0x0003eee0 FDrawIntoBitmap
// DrawIntoBitmap(shape, styles, bitmap): the shape drawn into a bitmap
// made by MakeBitmap rather than onto the screen.  A port is opened over
// the bitmap's pixel map, the shape is drawn into it at the origin, and
// the port that was current is put back - whether the drawing threw or
// not.
//
// The bits are pointed to rather than offset from the map while the port
// holds them (kPixMapPtr), because the port's copy of the map is not the
// map inside the binary; the port rect and the *visible* region become
// the bitmap's bounds, which is what confines the drawing to it (a fresh
// port's visible region is the screen's).
//
// NOT YET RECONSTRUCTED: a bitmap whose resolution is not 72 dpi, which
// the ROM draws through DrawShapeScaled; and TQDScaler::ForceScaling,
// which it turns off around the unscaled case.
static Ref
FDrawIntoBitmap(RefArg /*rcvr*/, RefArg shape, RefArg styles, RefArg bitmap)
{
	GrafPort* saved;
	GetPort(&saved);
	RefVar data(GetFrameSlotRef(bitmap, RSSYMdata));
	LockRef(data);
	PixelMap* pm = (PixelMap*) BinaryData(data);
	GrafPort port;
	OpenPort(&port);
	SetPortBits(pm);
	port.portBits.baseAddr = GetPixelMapBits(pm);
	port.portBits.pixMapFlags = (port.portBits.pixMapFlags & ~kPixMapStorage) | kPixMapPtr;
	port.portRect = port.portBits.bounds;
	RectRgn(port.visRgn, &port.portBits.bounds);
	Point origin;
	origin.h = 0;
	origin.v = 0;
	newton_try
	{
		DrawShape(shape, styles, origin);
	}
	newton_catch_all
	{
		SetPort(saved);
		ClosePort(&port);
		UnlockRef(data);
		rethrow;
	}
	end_try;
	SetPort(saved);
	ClosePort(&port);
	UnlockRef(data);
	return NILREF;
}

// ROM 0x0003f074 FViewIntoBitmap
// view:ViewIntoBitmap(src, dst, bitmap) - the view drawn into a bitmap
// rather than onto the screen.  `src` is the part of the view to take,
// in the view's own coordinates, and defaults to the whole of its outer
// bounds; a src that is given is relative to the view's top left, so it
// is offset by that.  `dst` is where it goes in the bitmap, and defaults
// to the same size at the bitmap's origin.
//
// The bitmap's bounds are moved into the view's coordinate space
// (by src.topLeft - dst.topLeft), so that drawing the view where it
// thinks it is lands in the right part of the bitmap; the two rectangles
// are then intersected down to what both can hold, and that is the clip.
//
// Unlike DrawIntoBitmap the ROM sets the *clip* region rather than the
// visible one, and leaves the visible region as a fresh port has it -
// the screen's bounds - which is what a view's own coordinates are in.
// The pen is put back to normal for the drawing and restored after.
Ref
FViewIntoBitmap(RefArg rcvr, RefArg srcRect, RefArg dstRect, RefArg bitmap)
{
	GrafPort* saved;
	GetPort(&saved);
	TView* view = FailGetView(rcvr);

	Rect src;
	if (ISNIL(srcRect))
		view->OuterBounds(&src);
	else
	{
		if (!FromObject(srcRect, src))
			Throw((ExceptionName) kGrafException, (void*) kGrafErrBadBounds, nil);
		OffsetRect(&src, view->viewBounds.left, view->viewBounds.top);
	}

	Rect dst;
	if (ISNIL(dstRect))
		SetRect(&dst, 0, 0, (short) (src.right - src.left), (short) (src.bottom - src.top));
	else if (!FromObject(dstRect, dst))
		Throw((ExceptionName) kGrafException, (void*) kGrafErrBadBounds, nil);

	GrafPort port;
	OpenPort(&port);
	RefVar data(GetFrameSlotRef(bitmap, RSSYMdata));
	LockRef(data);
	PixelMap* pm = (PixelMap*) BinaryData(data);
	SetPortBits(pm);
	port.portBits.baseAddr = GetPixelMapBits(pm);
	port.portBits.pixMapFlags = (port.portBits.pixMapFlags & ~kPixMapStorage) | kPixMapPtr;
	OffsetRect(&port.portBits.bounds, src.left - dst.left, src.top - dst.top);
	Rect held = port.portBits.bounds;
	SectRect(&held, &src, &src);
	OffsetRect(&dst, src.left - dst.left, src.top - dst.top);
	SectRect(&dst, &src, &src);
	RectRgn(port.clipRgn, &src);

	PenState pen;
	GetPenState(&pen);
	PenNormal();
	newton_try
	{
		view->Draw(src, false);
	}
	newton_catch_all
	{
		SetPenState(&pen);
		SetPort(saved);
		ClosePort(&port);
		UnlockRef(data);
		rethrow;
	}
	end_try;
	SetPenState(&pen);
	SetPort(saved);
	ClosePort(&port);
	UnlockRef(data);
	return NILREF;
}

void
RegisterShapeNatives(void)
{
	RegisterNativeFunction("FDrawShape", (void*) FDrawShape, 2);
	RegisterNativeFunction("FDrawIntoBitmap", (void*) FDrawIntoBitmap, 3);
	RegisterNativeFunction("FViewIntoBitmap", (void*) FViewIntoBitmap, 3);
	RegisterNativeFunction("FHitShape", (void*) FHitShape, 3);
	RegisterNativeFunction("FMakeTextLines", (void*) FMakeTextLines, 4);
	RegisterNativeFunction("FMakeRect", (void*) FMakeRect, 4);
	RegisterNativeFunction("FMakeOval", (void*) FMakeOval, 4);
	RegisterNativeFunction("FMakeRoundRect", (void*) FMakeRoundRect, 5);
	RegisterNativeFunction("FMakeLine", (void*) FMakeLine, 4);
	RegisterNativeFunction("FMakeWedge", (void*) FMakeWedge, 6);
	RegisterNativeFunction("FMakePolygon", (void*) FMakePolygon, 1);
	RegisterNativeFunction("FMakeRegion", (void*) FMakeRegion, 1);
	RegisterNativeFunction("FMakeText", (void*) FMakeText, 5);
	RegisterNativeFunction("FMakeTextBox", (void*) FMakeTextBox, 5);
	RegisterNativeFunction("FShapeBounds", (void*) FShapeBounds, 1);
	RegisterNativeFunction("FOffsetShape", (void*) FOffsetShape, 3);
	RegisterNativeFunction("FScaleShape", (void*) FScaleShape, 3);
	RegisterNativeFunction("FMakeShape", (void*) FMakeShape, 1);
	RegisterNativeFunction("FIsPrimShape", (void*) FIsPrimShape, 1);
}


// ROM 0x00191600 MakePolygonForm__FP6TPointlT2RC5TRectT2
// The shape frame a polygon is drawn from: the ROM's starter frame
// cloned, the points put in a 'polygonShape binary after a halfword verb
// and a halfword count, and the box as the bounds.  Ink is the odd verb
// out - it has a starter frame of its own and no points at all, the
// drawing coming from an `ink` slot whoever makes the shape adds.
//
// A pen of two is what a shape has already, so only another size is
// written, into the frame's viewFormat where the pen bits are.
Ref
MakePolygonForm(const Point* points, long count, long verb, const Rect& box, long pen)
{
	RefVar form;
	if (verb == kInkVerb)
		form = Clone(RefVar(Rstarterink));
	else
	{
		form = Clone(RefVar(Rstarterpolygon));
		RefVar shape(AllocateBinary(RSSYMpolygonshape, count * (long) sizeof(Point) + 4));
		UByte* data = (UByte*) BinaryData(shape);
		BlockMove(points, data + 4, count * (long) sizeof(Point));
		data[0] = (UByte) (verb >> 8);
		data[1] = (UByte) verb;
		data[2] = (UByte) (count >> 8);
		data[3] = (UByte) count;
		SetFrameSlot(form, RSSYMpoints, shape);
	}
	SetFrameSlot(form, RSSYMviewbounds, RefVar(ToObject(box)));
	if (pen != 2)
		SetFrameSlot(form, RSSYMviewformat, RefVar(MAKEINT(pen << 8)));
	return form;
}
