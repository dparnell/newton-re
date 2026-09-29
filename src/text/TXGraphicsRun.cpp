/*
	File:		text/TXGraphicsRun.cpp

	Contains:	The graphics runs (TXGraphicsRun.h).

	Reconstructed from the MP2x00 US ROM (0x0023ac54-0x0023b124,
	0x0023ded4-0x0023e168); each function cites its origin.
*/

#include "TXGraphicsRun.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "Ports.h"
#include "Rects.h"
#include "Draw.h"
#include "DrawShape.h"


// ROM 0x0023ac54 __ct__13TXGraphicsRunFv
TXGraphicsRun::TXGraphicsRun()
	: fExtraWidth(0), fHilite(0)
{ }


// ROM 0x0023aca0 Reference__13TXGraphicsRunFv
// A graphics run is one thing in the text: never shared, so a reference
// to it is a copy.
TXAttrObject*
TXGraphicsRun::Reference(void)
{
	TXAttrObject* copy = CreateNew();
	if (copy != nil)
		copy->Assign(this);
	return copy;
}


// ROM 0x0023aed4 GetObjFlags__13TXGraphicsRunCFv
unsigned long
TXGraphicsRun::GetObjFlags(void) const
{
	return TXAttrObject::GetObjFlags() | 7;
}


// ROM 0x0023aeec IsTextRun__13TXGraphicsRunCFv
Boolean
TXGraphicsRun::IsTextRun(void) const
{
	return false;
}


// ROM 0x0023aef4 GetHiliteInset__13TXGraphicsRunFv
int
TXGraphicsRun::GetHiliteInset(void)
{
	return (GetObjFlags() & 1) ? 2 : 0;
}


// ROM 0x0023af18 GetTotalDimensions__13TXGraphicsRunFPiT1
void
TXGraphicsRun::GetTotalDimensions(int* height, int* width)
{
	GetDimensions(height, width);
	*width = fExtraWidth + *width;
	int inset = GetHiliteInset();
	*height += inset * 2;
	*width += inset * 2;
}


// ROM 0x0023af78 GetHeightInfo__13TXGraphicsRunFPiN21
// All of it above the baseline.
void
TXGraphicsRun::GetHeightInfo(int* ascent, int* descent, int* leading)
{
	int height, width;
	GetTotalDimensions(&height, &width);
	*ascent = height;
	*descent = 0;
	*leading = 0;
}


// ROM 0x0023ad54 MeasureWidth__13TXGraphicsRunFRC20TXLineRunDisplayInfo
Fixed
TXGraphicsRun::MeasureWidth(const TXLineRunDisplayInfo& /*info*/)
{
	int height, width;
	GetTotalDimensions(&height, &width);
	return (Fixed) ((ULong) width << 16);
}


// ROM 0x0023ad7c LineBreak__13TXGraphicsRunFPCUslT2PlUcT4
// The whole run goes on the line (==> 2), or exactly fills it (==> 0), or
// does not fit: then it goes on a line that is empty so far, squeezed by
// what it is short (==> 1), and otherwise not at all (==> 0).  Note the
// length answered is the whole count, not the count after `start`.
long
TXGraphicsRun::LineBreak(const UniChar* /*text*/, long count, long /*start*/, Fixed* width, Boolean mayCutWord, long* length)
{
	fExtraWidth = 0;
	int height, wide;
	GetTotalDimensions(&height, &wide);
	*width = *width + (Fixed) ((ULong) wide * (ULong) -0x10000);
	*length = 0;
	if (*width < 0)
	{
		if (!mayCutWord)
			return 0;
		*length = count;
		fExtraWidth = *width >> 16;
		return 1;
	}
	*length = count;
	return (*width != 0) ? 2 : 0;
}


// ROM 0x0023acf8 CharToPixel__13TXGraphicsRunFRC20TXLineRunDisplayInfol
Fixed
TXGraphicsRun::CharToPixel(const TXLineRunDisplayInfo& info, long offset)
{
	if (offset == 0)
		return 0;
	return info.fWidth + info.fJustifyExtra;
}


// ROM 0x0023ace0 PixelToChar__13TXGraphicsRunFRC20TXLineRunDisplayInfolP13TXOffsetRange
// Left of the margin: in front of it; right of the other margin: after
// it; between them: the whole run.  The margins are a quarter of the
// width when the run's character is a control character, else none.
void
TXGraphicsRun::PixelToChar(const TXLineRunDisplayInfo& info, Fixed pixel, TXOffsetRange* range)
{
	int width = (short) ((ULong) (info.fWidth + info.fJustifyExtra + 0x8000) >> 16);
	int margin = 0;
	if (info.fText[0] < 0x20)
		margin = width >> 2;
	long x = pixel >> 16;
	if (margin < x)
	{
		if (x < width - margin)
		{
			range->Set(0, info.fLength, false, true);
			return;
		}
		long end = info.fLength;
		range->Set(end, end, end != 0, end != 0);
	}
	else
		range->Set(0, 0, false, false);
}


// ROM 0x0023ad04 SetHilite__13TXGraphicsRunFcRC17TXRunPositionInfoUc
// The frame is drawn in XOR, so drawing the old one again takes it away.
void
TXGraphicsRun::SetHilite(char on, const TXRunPositionInfo& where, Boolean draw)
{
	if (draw)
	{
		DrawHilite(where);
		fHilite = on;
		DrawHilite(where);
		return;
	}
	fHilite = on;
}


// ROM 0x0023afb8 GetRunRect__13TXGraphicsRunFRC17TXRunPositionInfoP4Rect
void
TXGraphicsRun::GetRunRect(const TXRunPositionInfo& where, Rect* box)
{
	box->top = where.fTop;
	box->bottom = where.fHeight + box->top;
	box->left = where.fLeft >> 16;
	box->right = box->left + (where.fWidth >> 16);
	int height, width;
	GetTotalDimensions(&height, &width);
	box->top = box->top + ((box->bottom - box->top) - height);
	box->bottom = box->top + height;
	int inset = GetHiliteInset();
	InsetRect(box, inset, inset);
}


// ROM 0x0023b01c AdjustRunRect__13TXGraphicsRunFP4Rect
// The box made the run's height, standing on its bottom, less the
// hilite's margin.
void
TXGraphicsRun::AdjustRunRect(Rect* box)
{
	int height, width;
	GetTotalDimensions(&height, &width);
	box->top = box->top + ((box->bottom - box->top) - height);
	box->bottom = box->top + height;
	int inset = GetHiliteInset();
	InsetRect(box, inset, inset);
}


// ROM 0x0023ae18 DrawHilite__13TXGraphicsRunFRC17TXRunPositionInfo
// A frame round the run in a gray pattern, in XOR (pen mode 10); a
// polygon being recorded takes the frame as it is.
void
TXGraphicsRun::DrawHilite(const TXRunPositionInfo& where)
{
	if (fHilite == 0)
		return;
	Rect box;
	GetRunRect(where, &box);
	int inset = GetHiliteInset();
	InsetRect(&box, -inset, -inset);
	GrafPtr port;
	GetPort(&port);
	if (port->polySave == nil)
	{
		PenState saved;
		GetPenState(&saved);
		PenNormal();
		PenMode(10);
		SetFgPattern(GetStdPattern((GetPatSelector) (fHilite == 2 ? 2 : 4)));
		FrameRect(&box);
		SetPenState(&saved);
	}
	else
		FrameRect(&box);
}


// ROM 0x0023b0a8 Draw__13TXGraphicsRunFRC20TXLineRunDisplayInfolRC4Recti
// The line's height, from x for the run's width, made the run's box and
// drawn into.
void
TXGraphicsRun::Draw(const TXLineRunDisplayInfo& info, Fixed x, const Rect& line, int /*baseline*/)
{
	Rect box;
	box.top = line.top;
	box.left = (short) ((ULong) (x + 0x8000) >> 16);
	box.right = box.left + (short) ((ULong) (info.fWidth + info.fJustifyExtra + 0x8000) >> 16);
	box.bottom = line.bottom;
	AdjustRunRect(&box);
	DrawContent(box);
}


// ROM 0x0023ded4 __ct__17TXNewtGraphicsRunFv
TXNewtGraphicsRun::TXNewtGraphicsRun()
{ }


// ROM 0x0023df28 CreateNew__17TXNewtGraphicsRunCFv
TXAttrObject*
TXNewtGraphicsRun::CreateNew(void) const
{
	return new TXNewtGraphicsRun;
}


// ROM 0x0023df78 GetClassId__17TXNewtGraphicsRunCFv
long
TXNewtGraphicsRun::GetClassId(void) const
{
	return kTXGraphicsRunClassId;
}


// ROM 0x0023df84 GetPublicType__17TXNewtGraphicsRunCFv
long
TXNewtGraphicsRun::GetPublicType(void) const
{
	return kTXShapePublicType;
}


// ROM 0x0023df90 GetObjFlags__17TXNewtGraphicsRunCFv
unsigned long
TXNewtGraphicsRun::GetObjFlags(void) const
{
	return TXGraphicsRun::GetObjFlags() & ~1UL;
}


// ROM 0x0023dfa8 GetAttributeFlags__17TXNewtGraphicsRunCFUl
unsigned long
TXNewtGraphicsRun::GetAttributeFlags(TXAttrTag tag) const
{
	unsigned long flags = (tag == kTXGraphicsRunClassId) ? 3 : 0;
	return TXAttrObject::GetAttributeFlags(tag) | flags;
}


// ROM 0x0023df44 Assign__17TXNewtGraphicsRunFPC12TXAttrObject
void
TXNewtGraphicsRun::Assign(const TXAttrObject* other)
{
	if (other == this)
		return;
	TXRun::Assign(other);
	fObject = ((const TXNewtGraphicsRun*) other)->fObject;
}


// ROM 0x0023e168 GetNSObject__17TXNewtGraphicsRunCFv
Ref
TXNewtGraphicsRun::GetNSObject(void) const
{
	return fObject;
}


// ROM 0x0023df30 SetNSObject__17TXNewtGraphicsRunFRC6RefVar
void
TXNewtGraphicsRun::SetNSObject(RefArg obj)
{
	fObject = obj;
}


// ROM 0x0023dfd4 GetDimensions__17TXNewtGraphicsRunFPiT1
// The shape's bounds and two pixels all round; 16 by 16 for none.
void
TXNewtGraphicsRun::GetDimensions(int* height, int* width)
{
	if (NOTNIL(fObject))
	{
		RefVar shape(GetFrameSlotRef(fObject, RSSYMshape));
		if (NOTNIL(shape))
		{
			Rect bounds;
			ShapeBounds(shape, &bounds);
			*height = (short) (bounds.bottom - bounds.top) + 4;
			*width = (short) (bounds.right - bounds.left) + 4;
			return;
		}
	}
	*height = 16;
	*width = 16;
}


// ROM 0x0023e094 DrawContent__17TXNewtGraphicsRunFRC4Rect
// The shape drawn with its bounds' top left two pixels into the box.
void
TXNewtGraphicsRun::DrawContent(const Rect& box)
{
	if (ISNIL(fObject))
		return;
	RefVar shape(GetFrameSlotRef(fObject, RSSYMshape));
	if (NOTNIL(shape))
	{
		Rect bounds;
		ShapeBounds(shape, &bounds);
		Point origin;
		origin.h = box.left - bounds.left + 2;
		origin.v = box.top - bounds.top + 2;
		DrawShape(shape, RefVar(NILREF), origin);
	}
}
