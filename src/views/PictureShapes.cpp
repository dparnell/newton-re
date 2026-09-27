/*
	File:		views/PictureShapes.cpp

	Contains:	A QuickDraw picture turned into NewtonScript shapes:
				PictToShape, and the OpcodeProcs table DrawPicture's
				toShapes plays the picture through (qd/PicPlay.h).

				ParsePicCodes reads each opcode as it would to draw it,
				maps what it read onto the destination and leaves it in
				the PicPlay's fProc* fields, then calls the table's proc
				for the opcode's sixteen instead of drawing.  The procs
				make the shape - a rectangle, round rectangle, oval,
				wedge, line, polygon, region, bitmap or text box - and
				storeShape keeps it waiting in fShape with a style frame
				(MungeStyleFrame: the pen, fill and transfer mode the port
				would have drawn it with) in fStyle.  A shape of the same
				class and bounds after it does not go in: it adds its
				style to the waiting one's, which is how a rectangle
				painted and then framed comes out as one rectangle with a
				fill and a pen.  flushShape adds the waiting shape to the
				answer, preceded by its style frame when that is not equal
				to the one added last (StylesEqual).  Lines that join end
				to end are gathered into one polygon (fInkPoly) until
				something else comes along (FlushAnyInk).  The end of the
				picture (0xff) flushes the last shape.

	Reconstructed from the MP2x00 US ROM (0x00330068-0x0033197c, the table
	0x00380a9c, PictToShape 0x000dd6dc); each function cites its origin.
*/

#include "DrawShape.h"
#include "View.h"
#include "PicPlay.h"
#include "Pictures.h"
#include "Draw.h"
#include "Rects.h"
#include "Regions.h"
#include "Polygons.h"
#include "Shapes.h"
#include "Text.h"
#include "Fonts.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "NewtonMemory.h"
#include "ByteOrder.h"
#include "Unicode.h"
#include <string.h>

static void	FlushAnyInk(PicPlay* play, GrafPort* port);
static void	storeShape(long opcode, RefArg shape, PicPlay* play, GrafPort* port);
static void	PolyPicCodes(long opcode, PicPlay* play, GrafPort* port);


/*------------------------------------------------------------------------------
	S t y l e s
------------------------------------------------------------------------------*/

// ROM 0x00330f54 StyleToNSFont__FP11StyleRecord
// A style record as a packed font spec: the family's tsID (its screenSym
// when it has none), the size rounded to a whole point, the face.
static Ref
StyleToNSFont(StyleRecord* style)
{
	RefVar family(GetFrameSlotRef(style->fFontFamily, RSSYMtsid));
	if (ISNIL(family))
		family = GetFrameSlotRef(style->fFontFamily, RSSYMscreensym);
	return MakeCompactFont(family, (short) ((ULong32) (style->fFontSize + 0x8000) >> 16), style->fFontFace);
}


// ROM 0x00330ff0 GetNSFont__FP7PicPlay
// The picture's text style as a packed font spec (StyleToNSFont of it).
static Ref
GetNSFont(PicPlay* play)
{
	RefVar family(GetFrameSlotRef(play->fTextFamily, RSSYMtsid));
	if (ISNIL(family))
		family = GetFrameSlotRef(play->fTextFamily, RSSYMscreensym);
	return MakeCompactFont(family, (short) ((ULong32) (play->fTextSize + 0x8000) >> 16), play->fTextFace);
}


// ROM 0x00330ff8 GetNSPattern__FPP8PixelMap
// A pattern as a script holds it: all black or all white the standard
// pattern (5 or 1), a one-bit pattern its eight rows, one gray all over
// that gray as a packed colour, else a 'grayPattern of its pixels - only
// the top half of them when the bottom half is the same.
//
// ROM QUIRK, kept: at a depth other than 1, 2 or 4 the halves are compared
// at an offset of the pattern handle's own address.
static Ref
GetNSPattern(PatternHandle pattern)
{
	long blackOrWhite = BlackOrWhitePat(pattern);
	if (blackOrWhite != 0)
		return blackOrWhite == 2 ? MAKEINT(1) : MAKEINT(5);
	PixelMap* pm = *pattern;
	long depth = pm->pixMapFlags & 0xff;
	if (depth == 1)
		return MakeNSPattern(pm, 8);
	ULong gray;
	if (MonochromePat(pattern, &gray))
	{
		ULong red, green, blue;
		GrayToRGB((UChar) gray, &red, &green, &blue, depth);
		return MAKEINT(PackRGBvalues(red, green, blue));
	}
	ULong offset = (ULong) pattern;
	if (((*pattern)->pixMapFlags & 0xff) == 2)
		offset = 8;
	else if (((*pattern)->pixMapFlags & 0xff) == 4)
		offset = 0x10;
	char* bits = (char*) GetPixelMapBits(pm);
	long count = pm->rowBytes * (pm->bounds.bottom - pm->bounds.top);
	long half = count / 2;
	if (EqualBytes(bits, bits + offset, half))
		count = half;
	return MakeNSPattern(pm, count);
}


// ROM 0x0033113c MungeStyleFrame__FlP7PicPlayP8GrafPort
// The waiting shape's style frame (made when there is none) given what the
// port would draw the opcode with: a line its pen; text its font, left
// justified in black; a bitmap its mode; a shape by its verb - frame the
// pen, paint the fill, erase the background as the fill, invert black
// xored, fill the pen's pattern in patCopy.  A mode over 7 is taken as
// nought.
static void
MungeStyleFrame(long opcode, PicPlay* play, GrafPort* port)
{
	if (port == nil)
		return;
	RefVar style(play->fStyle);
	if (ISNIL(style))
		style = AllocateFrame();
	RefVar pattern(GetNSPattern(port->fgPat));
	long mode = port->pnMode;
	if (mode >= 8)
		mode = 0;
	RefVar transferMode(MAKEINT(mode));
	if (opcode >= 0x20 && opcode <= 0x23)
	{
		SetFrameSlot(style, RSSYMpenpattern, pattern);
		SetFrameSlot(style, RSSYMpensize, RefVar(MAKEINT(port->pnSize.h)));
		SetFrameSlot(style, RSSYMtransfermode, transferMode);
	}
	else if (opcode == 0x81a3 || (opcode >= 0x28 && opcode <= 0x2b))
	{
		SetFrameSlot(style, RSSYMtransfermode, transferMode);
		SetFrameSlot(style, RSSYMfont, RefVar(GetNSFont(play)));
		SetFrameSlot(style, RSSYMjustification, RSSYMleft);
		SetFrameSlot(style, RSSYMtextpattern, RefVar(MAKEINT(5)));
	}
	else if (opcode >= 0x90 && opcode <= 0x9b)
	{
		long bitsMode = play->fProcMode;
		if (bitsMode >= 8)
			bitsMode = 0;
		SetFrameSlot(style, RSSYMtransfermode, RefVar(MAKEINT(bitsMode)));
	}
	else
	{
		switch (opcode & 7)
		{
		case frame:
			SetFrameSlot(style, RSSYMpenpattern, pattern);
			SetFrameSlot(style, RSSYMpensize, RefVar(MAKEINT(port->pnSize.h)));
			SetFrameSlot(style, RSSYMtransfermode, transferMode);
			break;
		case paint:
			SetFrameSlot(style, RSSYMfillpattern, pattern);
			SetFrameSlot(style, RSSYMtransfermode, transferMode);
			break;
		case erase:
			SetFrameSlot(style, RSSYMfillpattern, RefVar(GetNSPattern(port->bgPat)));
			SetFrameSlot(style, RSSYMtransfermode, RefVar(MAKEINT(0)));
			break;
		case invert:
			SetFrameSlot(style, RSSYMfillpattern, RefVar(MAKEINT(5)));
			SetFrameSlot(style, RSSYMtransfermode, RefVar(MAKEINT(2)));
			break;
		case fill:
			SetFrameSlot(style, RSSYMfillpattern, pattern);
			SetFrameSlot(style, RSSYMtransfermode, RefVar(MAKEINT(8)));
			break;
		default:
			break;
		}
	}
	play->fStyle = style;
}


// ROM 0x003314c8 StylesEqual__FRC6RefVarT1
// Whether two style frames say the same: as many slots, and each of the
// first's the same integer in the second, or a region the same region.
//
// ROM QUIRK, kept: anything else - a pattern binary, a font spec - makes
// them different, as does being empty, so a style frame with a binary in
// it goes in again before every shape.
static Boolean
StylesEqual(RefArg a, RefArg b)
{
	Boolean equal = false;
	if (ISNIL(a) || ISNIL(b))
		return false;
	if (Length(a) != Length(b))
		return false;
	RefVar other;
	RefVar aData, bData;
	TObjectIterator iter(a, false);
	for (; !iter.Done(); iter.Next())
	{
		other = GetFrameSlotRef(b, iter.Tag());
		RefVar value(iter.Value());
		if (ISINT(other) && ISINT(value) && RVALUE(value) == RVALUE(other))
		{
			equal = true;
			continue;
		}
		if (!EQ(RefVar(ClassOf(other)), RSSYMregion) || !EQ(RefVar(ClassOf(value)), RSSYMregion))
		{
			equal = false;
			break;
		}
		aData = GetFrameSlotRef(value, RSSYMdata);
		bData = GetFrameSlotRef(other, RSSYMdata);
		LockRef(aData);
		LockRef(bData);
		RgnHandle aRgn = (RgnHandle) NewFakeHandle(BinaryData(aData), Length(aData));
		RgnHandle bRgn = (RgnHandle) NewFakeHandle(BinaryData(bData), Length(bData));
		Boolean same = EqualRgn(aRgn, bRgn);
		UnlockRef(bData);
		UnlockRef(aData);
		// (ROM BUG, kept: the two fake handles are never given back)
		if (!same)
		{
			equal = false;
			break;
		}
		equal = true;
	}
	return equal;
}


/*------------------------------------------------------------------------------
	T h e   s h a p e s
------------------------------------------------------------------------------*/

// ROM 0x00331754 flushShape__FP7PicPlay
// The waiting shape added to the answer (made an empty array the first
// time), its style frame in front of it when that differs from the one
// added last.
static void
flushShape(PicPlay* play)
{
	if (ISNIL(play->fShapes))
		play->fShapes = MakeArray(0);
	if (ISNIL(play->fShape))
		return;
	if (!StylesEqual(play->fStyle, play->fLastStyle))
	{
		AddArraySlot(play->fShapes, play->fStyle);
		play->fLastStyle = play->fStyle;
	}
	AddArraySlot(play->fShapes, play->fShape);
	play->fShape = NILREF;
	play->fStyle = NILREF;
}


// ROM 0x00331804 storeShape__FlRC6RefVarP7PicPlayP8GrafPort
// A shape made of the opcode: when it is the same class as the one
// waiting (and not a bitmap) with the same bounds, only its style is added
// to the waiting one's and that goes in; otherwise the waiting one goes
// in and this one waits with its own style.
static void
storeShape(long opcode, RefArg shape, PicPlay* play, GrafPort* port)
{
	FlushAnyInk(play, port);
	RefVar waiting(play->fShape);
	RefVar waitingClass(ClassOf(waiting));
	RefVar shapeClass(ClassOf(shape));
	if (NOTNIL(waiting) && EQ(waitingClass, shapeClass) && !EQ(waitingClass, RSSYMbitmap))
	{
		Rect waitingBounds, bounds;
		ShapeBounds(waiting, &waitingBounds);
		ShapeBounds(shape, &bounds);
		if (EqualRect(&bounds, &waitingBounds))
		{
			MungeStyleFrame(opcode, play, port);
			flushShape(play);
			return;
		}
	}
	if (NOTNIL(shape))
	{
		flushShape(play);
		MungeStyleFrame(opcode, play, port);
		play->fShape = shape;
	}
}


// ROM 0x003300b8 FlushAnyInk__FP7PicPlayP8GrafPort
// The lines gathered so far closed into a polygon and made a shape.
static void
FlushAnyInk(PicPlay* play, GrafPort* port)
{
	if (play->fInkPoly == nil)
		return;
	ClosePoly();
	play->fProcHandle = (Handle) play->fInkPoly;
	play->fInkPoly = nil;
	PolyPicCodes(0x70, play, port);
	DisposHandle(play->fProcHandle);
}


// a text box for text at the point in the font: from the point's left and
// the font's ascent above it, as wide as the text and five more, down to
// its descent below
static Ref
TextShape(RefArg str, RefArg font, long h, long v)
{
	long width = RINT(RefVar(FStrFontWidth(RefVar(), str, font)));
	long ascent = RINT(RefVar(FFontAscent(RefVar(), font)));
	long descent = RINT(RefVar(FFontDescent(RefVar(), font)));
	return FMakeTextBox(RefVar(), str, RefVar(MAKEINT(h)), RefVar(MAKEINT(v - ascent)),
						RefVar(MAKEINT(width + h + 5)), RefVar(MAKEINT(descent + v)));
}


/*------------------------------------------------------------------------------
	T h e   p r o c s
------------------------------------------------------------------------------*/

// ROM 0x0033196c EarlyPicCodes__FlP7PicPlayP8GrafPort
// A state opcode (0x00-0x1f): before it is played, the lines gathered so
// far become a shape.
static void
EarlyPicCodes(long opcode, PicPlay* play, GrafPort* port)
{
	if (opcode < 0)
		FlushAnyInk(play, port);
}


// ROM 0x00330108 LinePicCodes__FlP7PicPlayP8GrafPort
// Text (0x28-0x2b) a text box; a line joined to the lines before it when
// it starts where the last ended, else begun as a new polygon - unless the
// pen is empty or does not draw (mode 0x17 or more), the line lies wholly
// above and left of the origin, or it has no length.
static void
LinePicCodes(long opcode, PicPlay* play, GrafPort* port)
{
	if (ImpossibleToDraw(port))
		return;
	if (opcode > 0x27)
	{
		RefVar str(MakeString(play->fProcText));
		RefVar font(GetNSFont(play));
		RefVar shape(TextShape(str, font, play->fProcPt.h, play->fProcPt.v));
		storeShape(opcode, shape, play, port);
		return;
	}
	long startH = port->pnLoc.h;
	long startV = port->pnLoc.v;
	long endH = play->fProcPt.h;
	long endV = play->fProcPt.v;
	if (port->pnSize.h + port->pnSize.v == 0)
		return;
	if (port->pnMode >= 0x17)
		return;
	if (startH < 0 && endH < 0 && startV < 0 && endV < 0)
		return;
	if (startH == endH && startV == endV)
		return;
	if (play->fInkPoly == nil || startH != play->fInkLastPt.h || startV != play->fInkLastPt.v)
	{
		FlushAnyInk(play, port);
		play->fInkPoly = OpenPoly();
		MoveTo(startH, startV);
	}
	LineTo(endH, endV);
	play->fInkLastPt.h = (short) endH;
	play->fInkLastPt.v = (short) endV;
}


// ROM 0x003303b4 RectPicCodes__FlP7PicPlayP8GrafPort
// A rectangle, round rectangle (its corners the oval size's width) or oval.
static void
RectPicCodes(long opcode, PicPlay* play, GrafPort* port)
{
	if (ImpossibleToDraw(port))
		return;
	RefVar left(MAKEINT(play->fProcRect.left));
	RefVar top(MAKEINT(play->fProcRect.top));
	RefVar right(MAKEINT(play->fProcRect.right));
	RefVar bottom(MAKEINT(play->fProcRect.bottom));
	RefVar shape;
	ULong group = opcode & 0xfff0;
	if (group == 0x30 || group == 0x38)
		shape = FMakeRect(RefVar(), left, top, right, bottom);
	else if (group == 0x40)
		shape = FMakeRoundRect(RefVar(), left, top, right, bottom, RefVar(MAKEINT(play->fOvalSize.h)));
	else if (group == 0x50)
		shape = FMakeOval(RefVar(), left, top, right, bottom);
	storeShape(opcode, shape, play, port);
}


// ROM 0x00330654 CommentPicCodes__FlP7PicPlayP8GrafPort
// A comment makes no shape.
static void
CommentPicCodes(long /*opcode*/, PicPlay* /*play*/, GrafPort* /*port*/)
{ }


// ROM 0x00330658 ArcPicCodes__FlP7PicPlayP8GrafPort
// An arc as a wedge of its oval.
static void
ArcPicCodes(long opcode, PicPlay* play, GrafPort* port)
{
	if (ImpossibleToDraw(port))
		return;
	RefVar shape(FMakeWedge(RefVar(), RefVar(MAKEINT(play->fProcRect.left)), RefVar(MAKEINT(play->fProcRect.top)),
							RefVar(MAKEINT(play->fProcRect.right)), RefVar(MAKEINT(play->fProcRect.bottom)),
							RefVar(MAKEINT(play->fProcMode)), RefVar(MAKEINT(play->fProcArc))));
	storeShape(opcode, shape, play, port);
}


// ROM 0x00330798 PolyPicCodes__FlP7PicPlayP8GrafPort
// A polygon: of two points a line (stored as a line opcode), else a
// polygon shape of its points.
static void
PolyPicCodes(long opcode, PicPlay* play, GrafPort* port)
{
	if (ImpossibleToDraw(port))
		return;
	Polygon* poly = (Polygon*) *play->fProcHandle;
	ULong count = (ULong) ((unsigned short) poly->polySize - 0xc) >> 2;
	if (count == 2)
	{
		Point from = poly->polyPoints[0];
		Point to = poly->polyPoints[1];
		RefVar shape(FMakeLine(RefVar(), RefVar(MAKEINT(from.h)), RefVar(MAKEINT(from.v)),
							   RefVar(MAKEINT(to.h)), RefVar(MAKEINT(to.v))));
		storeShape(0x20, shape, play, port);
		return;
	}
	RefVar points(MakeArray(count * 2));
	RefVar h, v;
	for (ULong i = 0; i < count; i++)
	{
		poly = (Polygon*) *play->fProcHandle;
		h = MAKEINT(poly->polyPoints[i].h);
		v = MAKEINT(poly->polyPoints[i].v);
		SetArraySlot(points, 2 * i, h);
		SetArraySlot(points, 2 * i + 1, v);
	}
	RefVar shape(FMakePolygon(RefVar(), points));
	storeShape(opcode, shape, play, port);
}


// ROM 0x003309cc RegionPicCodes__FlP7PicPlayP8GrafPort
// A region: canonicalRegionShape with a copy of the region as its data.
static void
RegionPicCodes(long opcode, PicPlay* play, GrafPort* port)
{
	if (ImpossibleToDraw(port))
		return;
	Handle rgn = play->fProcHandle;
	long length = GetHandleSize(rgn);
	RefVar shape(Clone(RefVar(Rcanonicalregionshape)));
	RefVar data(AllocateBinary(RSSYMregiondata, length));
	SetFrameSlot(shape, RSSYMdata, data);
	memmove(BinaryData(data), *rgn, length);
	storeShape(opcode, shape, play, port);
}


// ROM 0x00330aa4 BitsPicCodes__FlP7PicPlayP8GrafPort
// A bitmap the size of its destination with the pixels copied (scaled)
// into it, moved to where the destination is.
static void
BitsPicCodes(long opcode, PicPlay* play, GrafPort* port)
{
	if (ImpossibleToDraw(port))
		return;
	PixelMap map = *play->fProcBits;
	Rect r = play->fProcRect;
	long width = r.right - r.left;
	long height = r.bottom - r.top;
	RefVar shape(FMakeBitmap(RefVar(), RefVar(MAKEINT(width)), RefVar(MAKEINT(height)), RefVar()));
	RefVar pixels(GetFrameSlotRef(shape, RSSYMdata));
	LockRef(pixels);
	PixelMap* pm = (PixelMap*) BinaryData(pixels);
	Rect dst;
	dst.top = 0;
	dst.left = 0;
	dst.bottom = (short) height;
	dst.right = (short) width;
	CopyBits(&map, pm, &map.bounds, &dst, srcCopy, nil);
	FOffsetShape(RefVar(), shape, RefVar(MAKEINT(r.left)), RefVar(MAKEINT(r.top)));
	storeShape(opcode, shape, play, port);
	UnlockRef(pixels);
}


// ROM 0x00330cc0 EOPPicCodes__FlP7PicPlayP8GrafPort
// The end of the picture (0xff): the lines gathered and the waiting shape
// go in.  (The ROM writes flushShape out again here.)
static void
EOPPicCodes(long opcode, PicPlay* play, GrafPort* port)
{
	if (opcode != 0xff)
		return;
	FlushAnyInk(play, port);
	flushShape(play);
}


// ROM 0x00330cf0 XtndPicCodes__FlP7PicPlayP8GrafPort
// The Newton's text (0x81a3) as a text box in its own style - which
// becomes the picture's text style from then on.
static void
XtndPicCodes(long opcode, PicPlay* play, GrafPort* port)
{
	if (opcode != 0x81a3)
		return;
	long size = (play->fXTextCount + 1) * 2;
	UniChar* text = (UniChar*) QDNewTempPtr(size);
	if (text == nil)
		return;
	BlockMove(play->fXText, text, size);
	text[play->fXTextCount] = 0;
	RefVar str(MakeString(text));
	QDDisposeTempPtr((char*) text);
	RefVar font(StyleToNSFont(&play->fXStyle));
	play->fTextFamily = play->fXStyle.fFontFamily;
	play->fTextSize = play->fXStyle.fFontSize;
	play->fTextFace = play->fXStyle.fFontFace;
	long h = (short) ((ULong32) (play->fXTextLoc.x + 0x8000) >> 16);
	long v = (short) ((ULong32) (play->fXTextLoc.y + 0x8000) >> 16);
	RefVar shape(TextShape(str, font, h, v));
	storeShape(0x81a3, shape, play, port);
}


// ROM 0x00330f50 HuhPicCodes__FlP7PicPlayP8GrafPort
// The opcodes that make no shape (0xb0-0xef).
static void
HuhPicCodes(long /*opcode*/, PicPlay* /*play*/, GrafPort* /*port*/)
{ }


// ROM 0x00380a9c OpcodeProcs
// One proc per sixteen opcodes; the seventeenth for everything from 0x100.
static const OpcodeProc	kOpcodeProcs[17] =
{
	EarlyPicCodes, EarlyPicCodes,					// 0x00, 0x10
	LinePicCodes,									// 0x20 lines, text
	RectPicCodes, RectPicCodes, RectPicCodes,		// 0x30 rectangles, 0x40 round, 0x50 ovals
	ArcPicCodes,									// 0x60
	PolyPicCodes,									// 0x70
	RegionPicCodes,									// 0x80
	BitsPicCodes,									// 0x90
	CommentPicCodes,								// 0xa0
	HuhPicCodes, HuhPicCodes, HuhPicCodes, HuhPicCodes,	// 0xb0-0xe0
	EOPPicCodes,									// 0xf0, 0xff
	XtndPicCodes									// 0x100 and on
};


// ROM 0x000dd6dc FPictToShape
// PictToShape(picture, bounds): the picture turned into an array of shapes
// and style frames, played into the bounds frame (its own frame when nil).
Ref
FPictToShape(RefArg /*rcvr*/, RefArg picture, RefArg bounds)
{
	gOpcodeProcs = kOpcodeProcs;
	LockRef(picture);
	Ptr data = (Ptr) BinaryData(picture);
	Rect r;
	if (ISNIL(bounds))
	{
		r.top = (short) GetBigEndianHalf((const UChar*) data + 2);
		r.left = (short) GetBigEndianHalf((const UChar*) data + 4);
		r.bottom = (short) GetBigEndianHalf((const UChar*) data + 6);
		r.right = (short) GetBigEndianHalf((const UChar*) data + 8);
	}
	else
		FromObject(bounds, r);
	RefVar result;
	newton_try
	{
		result = DrawPicture((PicHandle) &data, &r, true);
	}
	cleanup
	{
		UnlockRef(picture);
	}
	end_try;
	UnlockRef(picture);
	return result;
}


void
RegisterPictureShapeNatives(void)
{
	gOpcodeProcs = kOpcodeProcs;
	RegisterNativeFunction("FPictToShape", (void*) FPictToShape, 2);
}
