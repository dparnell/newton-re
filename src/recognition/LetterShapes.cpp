/*
	File:		recognition/LetterShapes.cpp

	Contains:	The Letter Shapes preference.  See LetterShapes.h.
*/

#include "LetterShapes.h"
#include "ParaGraph.h"			// FGetLetterImages
#include "XrDomains.h"			// kXrWordDomainType
#include "Controller.h"
#include "Recognizer.h"			// gController
#include "RandomWords.h"		// RangeRand
#include "Words.h"				// gEnabledLanguage
#include "Stroke.h"				// TStrokePublic
#include "UnitPublic.h"			// StrokeFromRef
#include "View.h"
#include "Rects.h"
#include "Draw.h"
#include "Shapes.h"
#include "Regions.h"
#include "RegionVars.h"
#include "Screen.h"				// screenWidth
#include "Fonts.h"
#include "Text.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "NewtonTime.h"
#include <string.h>

// ROM 0x0c1010cc gLIHiliteIndex
ULong	gLIHiliteIndex = 0;

enum { kLetterCell = 0x23 };		// a picture's cell: 35 pixels

static TDomain*
XrWordDomain(void)
{
	return gController->GetTypedDomain(kXrWordDomainType);
}

// a big-endian halfword of the images
static inline ULong
LIHalf(const UByte* p)
{
	return (p[0] << 8) | p[1];
}


/*------------------------------------------------------------------------------
	T h e   i m a g e s
------------------------------------------------------------------------------*/

// ROM 0x00105de4 LIInit__Fv
void
LIInit(void)
{ }


// ROM 0x00107444 LIGetImageData__Fv
UByte*
LIGetImageData(void)
{
	RefVar images(FGetLetterImages(RefVar()));
	return (UByte*) BinaryData(images);
}


// ROM 0x00107fa8 LILetterCount__FPv
ULong
LILetterCount(void* image)
{
	const UByte* p = (const UByte*) image;
	return ((ULong) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}


// ROM 0x00108284 LIFirstInfo__FPv
LILetterInfoType*
LIFirstInfo(void* image)
{
	return (LILetterInfoType*) image + 4;
}


// ROM 0x001082e8 GetNextLetter__FP16LILetterInfoType
LILetterInfoType*
GetNextLetter(LILetterInfoType* info)
{
	return info + info[1] * 2 + 2;
}


// ROM 0x0010828c LIVariantCount__FPv
long
LIVariantCount(void* image)
{
	if (image == nil)
		return 0;
	long count = 0;
	ULong letters = LILetterCount(image);
	LILetterInfoType* info = LIFirstInfo(image);
	for (ULong i = 0; i < letters; i++)
	{
		count += info[1];
		info = GetNextLetter(info);
	}
	return count;
}


// ROM 0x00106004 LIGetLetterInfo__FPvUc
LILetterInfoType*
LIGetLetterInfo(void* image, UByte c)
{
	return GetLetterHeaderOffset(image, c);
}


// ROM 0x00105de8 GetLetterHeaderOffset__FPvUc
LILetterInfoType*
GetLetterHeaderOffset(void* image, UByte c)
{
	long letters = LILetterCount(image);
	LILetterInfoType* info = LIFirstInfo(image);
	for (long i = 0; i < letters; i++)
	{
		if (info[0] == c)
			return info;
		info = GetNextLetter(info);
	}
	return nil;
}


// ROM 0x00105e3c LIGetVariantIndex__FPvUcs
long
LIGetVariantIndex(void* image, UByte c, short variant)
{
	long index = 0;
	ULong letters = LILetterCount(image);
	LILetterInfoType* info = LIFirstInfo(image);
	for (ULong i = 0; i < letters; i++)
	{
		if (info[0] == c)
			break;
		index += info[1];
		info = GetNextLetter(info);
	}
	return index + variant;
}


// ROM 0x00105ea4 LIGetIndexedLetterInfo__FPvUlPP16LILetterInfoTypePs
void
LIGetIndexedLetterInfo(void* image, ULong index, LILetterInfoType** info, short* variant)
{
	*info = nil;
	*variant = 0;
	ULong letters = LILetterCount(image);
	LILetterInfoType* p = LIFirstInfo(image);
	ULong before = 0;
	for (ULong i = 0; i < letters; i++)
	{
		ULong count = p[1];
		if (index < count + before)
		{
			*info = p;
			*variant = index - before;
			return;
		}
		p = GetNextLetter(p);
		before += count;
	}
}


// ROM 0x0010600c LIGetGroupNumber__FPvP16LILetterInfoTypes
long
LIGetGroupNumber(void* image, LILetterInfoType* info, short variant)
{
	if (variant < info[1])
		return (short) ((UByte*) image)[LIHalf(info + variant * 2 + 2)];
	return -1;
}


// ROM 0x00106040 LIGetLengthGroup__FPvP16LILetterInfoTypes
long
LIGetLengthGroup(void* image, LILetterInfoType* info, short variant)
{
	long v = variant;
	if (v >= info[1])
		return -1;
	UByte group = ((UByte*) image)[LIHalf(info + v * 2 + 2)];
	short length = 1;
	while (++v < info[1])
		if (((UByte*) image)[LIHalf(info + v * 2 + 2)] == group)
			length++;
	return length;
}


// ROM 0x001060b0 LIGetVariantInfo__FPvP16LILetterInfoTypes
LILetterVarType*
LIGetVariantInfo(void* image, LILetterInfoType* info, short variant)
{
	if (variant < info[1])
		return (UByte*) image + LIHalf(info + variant * 2 + 2);
	return nil;
}


// ROM 0x001060d4 LIGetStrokeInfo__FPvP15LILetterVarTypes
LIStrokeType*
LIGetStrokeInfo(void* image, LILetterVarType* variant, short stroke)
{
	if (stroke < variant[1])
		return (UByte*) image + LIHalf(variant + stroke * 2 + 2);
	return nil;
}


// ROM 0x00106180 LIGetPoint__FP12LIStrokeTypesP5Point
long
LIGetPoint(LIStrokeType* stroke, short index, Point* pt)
{
	if (index < stroke[0])
	{
		pt->h = stroke[index * 2 + 1];
		pt->v = stroke[index * 2 + 2];
		return 0;
	}
	return -1;
}


// ROM 0x001061c4 LIGetVariantBBox__FPvP15LILetterVarTypeP4Rect
long
LIGetVariantBBox(void* image, LILetterVarType* variant, Rect* box)
{
	if (variant == nil || image == nil)
		return -1;
	box->top = 0;
	box->bottom = 0xff;
	box->left = 0x7fff;
	box->right = (short) 0x8000;
	for (long s = 0; s < variant[1]; s++)
	{
		LIStrokeType* stroke = LIGetStrokeInfo(image, variant, s);
		if (stroke == nil)
			return -1;
		for (long i = 0; i < stroke[0]; i++)
		{
			Point pt;
			if (LIGetPoint(stroke, i, &pt) == -1)
				return -1;
			if (pt.h < box->left)
				box->left = pt.h;
			if (pt.h > box->right)
				box->right = pt.h;
		}
	}
	if (box->right == box->left)
		box->right = box->right + 1;
	return 0;
}


// ROM 0x0010630c LIGetVariantBaseLine__FPvP15LILetterVarTypeP4Rect
long
LIGetVariantBaseLine(void* image, LILetterVarType* variant, Rect* lines)
{
	if (variant == nil || image == nil || LIGetVariantBBox(image, variant, lines) == -1)
		return -1;
	lines->top = 0x55;
	lines->bottom = 0xaa;
	return 0;
}


/*------------------------------------------------------------------------------
	T h e   w e i g h t s
------------------------------------------------------------------------------*/

// ROM 0x001060f8 LIBeginWeights__Fv
Handle
LIBeginWeights(void)
{
	TDomain* domain = XrWordDomain();
	ULong size;
	domain->DomainParameter(0, (ULong) &size, 0);
	Handle info = MakeHandle(size);
	NameHandle(info, 'info');
	if (info != nil)
		domain->DomainParameter(1, 0, (ULong) info);
	return info;
}


// ROM 0x00107068 LIEndWeights__FPPc
void
LIEndWeights(Handle weights)
{
	XrWordDomain()->DomainParameter(3, 0, (ULong) weights);
	if (weights != nil)
		DisposHandle(weights);
}


// ROM 0x00105f94 LIGetVariantWeight__FPPcUcs
UByte
LIGetVariantWeight(Handle weights, UByte c, short group)
{
	UByte request[3];
	request[0] = c;
	request[1] = group;
	request[2] = 2;
	if (XrWordDomain()->DomainParameter(0x20017, (ULong) request, (ULong) weights) == -1)
		request[2] = 3;
	return request[2];
}


// ROM 0x00105f34 LISetVariantWeight__FPPcUcsT2
void
LISetVariantWeight(Handle weights, UByte c, short group, UByte weight)
{
	UByte request[3];
	request[0] = c;
	request[1] = group;
	request[2] = weight;
	XrWordDomain()->DomainParameter(0x20016, (ULong) request, (ULong) weights);
}


/*------------------------------------------------------------------------------
	D r a w i n g
------------------------------------------------------------------------------*/

// ROM 0x001066dc CalculateScreenRect__FP4RectN21
long
CalculateScreenRect(Rect* extent, Rect* rect, Rect* screen)
{
	long left = rect->left;
	long right = rect->right;
	long width = right - left - 2;
	long height = rect->bottom - rect->top - 2;
	long srcWidth = extent->right - extent->left;
	long srcHeight = extent->bottom - extent->top;
	if (width <= 0 || height <= 0 || srcWidth < 0 || srcHeight < 0)
		return -1;
	if (srcHeight == 0 && srcWidth == 0)
	{
		screen->left = left / 2 + right / 2 - 1;
		screen->right = rect->left / 2 + rect->right / 2 + 1;
		screen->top = rect->top / 2 + rect->bottom / 2 - 1;
		screen->bottom = rect->top / 2 + rect->bottom / 2 + 1;
		return 0;
	}
	if (srcWidth == 0)
		srcWidth = 1;
	if (srcHeight == 0)
		srcHeight = 1;
	long across = (width << 16) / srcWidth;
	long down = (height << 16) / srcHeight;
	long bottom;
	if (across > down)
	{
		long inset;
		if (srcWidth == 1)
			inset = width / 2 - 2;
		else
			inset = (width - (height * srcWidth) / srcHeight) / 2;
		inset = (short) inset;
		screen->left = left + inset + 1;
		screen->right = (UShort) rect->right - inset - 1;
		screen->top = (UShort) rect->top + 1;
		bottom = (UShort) rect->bottom;
	}
	else
	{
		long inset;
		if (srcHeight == 1)
			inset = height / 2 - 2;
		else
			inset = (height - (width * srcHeight) / srcWidth) / 2;
		screen->left = left + 1;
		screen->right = (UShort) rect->right - 1;
		inset = (short) inset;
		screen->top = (UShort) rect->top + inset + 1;
		bottom = (UShort) rect->bottom - inset;
	}
	screen->bottom = bottom - 1;
	if (screen->right == screen->left)
		screen->right = screen->right + 1;
	if (screen->top == screen->bottom)
		screen->top = screen->top + 1;
	return 0;
}


// ROM 0x001069bc ConvertToScreenCoord__FP5PointP4RectT2
long
ConvertToScreenCoord(Point* pt, Rect* extent, Rect* screen)
{
	long width = screen->right - screen->left;
	long height = screen->bottom - screen->top;
	long srcWidth = extent->right - extent->left;
	long srcHeight = extent->bottom - extent->top;
	if (srcWidth == 0)
		srcWidth = 1;
	if (srcHeight == 0)
		srcHeight = 1;
	if (width <= 0 || height <= 0 || srcWidth <= 0 || srcHeight <= 0)
		return -1;
	pt->h = (width * (pt->h - extent->left)) / srcWidth + screen->left;
	pt->v = (height * (pt->v - extent->top)) / srcHeight + (UShort) screen->top;
	return 0;
}


// ROM 0x00106a98 PairedChar__Fl
long
PairedChar(long c)
{
	if (gEnabledLanguage == 8)
	{
		if (c == 0x81)
			return 0x8c;
		if (c == 0x80)
			return 0x8a;
		if (c == 0x85)
			return 0x9a;
	}
	if (c >= 'A' && c <= 'Z')
		return c + 0x20;
	switch (c)
	{
	case '(':	return ')';
	case '"':	return '\'';
	case '$':	return 0xa3;
	case ',':	return '.';
	case ';':	return ':';
	}
	return 0;
}


// The two body lines drawn in gray across a cell, each where the
// picture's box puts it: from a pixel in from the left to a pixel in
// from the right, or `extendLeft`/`extendRight` further.
static long
DrawBodyLines(Rect* extent, Rect* screen, Rect* lines, Rect* cell, long leftExtra, long rightExtra)
{
	PenNormal();
	SetFgPattern(GetStdPattern(grayPat));
	for (long which = 0; which < 2; which++)
	{
		short v = (which == 0) ? lines->top : lines->bottom;
		// (the ROM leaves the point's h as it was: only v is wanted)
		Point pt;
		pt.v = v;
		pt.h = 0;
		if (ConvertToScreenCoord(&pt, extent, screen) == -1)
			return -1;
		pt.h = (UShort) cell->left + 1 + leftExtra;
		MoveTo(pt.h, pt.v);
		pt.v = v;
		if (ConvertToScreenCoord(&pt, extent, screen) == -1)
			return -1;
		pt.h = (UShort) cell->right - 1 + rightExtra;
		LineTo(pt.h, pt.v);
	}
	return 0;
}


// ROM 0x00106364 DrawLetterImage__FPvP15LILetterVarTypeP4RectUcUlT5T4
long
DrawLetterImage(void* image, LILetterVarType* variant, Rect* rect, UByte weight, ULong pause, ULong animate, UByte frame)
{
	Rect extent, screen, lines;
	long delay = 0;
	if (variant == nil
	||  LIGetVariantBBox(image, variant, &extent) == -1
	||  CalculateScreenRect(&extent, rect, &screen) == -1)
		return -1;
	EraseRect(rect);
	if (frame != 0)
	{
		Rect box = *rect;
		PenNormal();
		PenSize(1, 1);
		FrameRoundRect(&box, 10, 10);
	}
	if (LIGetVariantBaseLine(image, variant, &lines) == -1)
		return -1;
	if (DrawBodyLines(&extent, &screen, &lines, rect, 0, 0) == -1)
		return -1;
	PenNormal();
	if (weight == 2)
		SetFgPattern(GetStdPattern(grayPat));
	else if (weight == 1)
		SetFgPattern(GetStdPattern(dkGrayPat));
	PenSize(2, 2);
	for (long s = 0; s < variant[1]; s++)
	{
		LIStrokeType* stroke = LIGetStrokeInfo(image, variant, s);
		if (stroke == nil)
			return -1;
		if (animate != 0 && stroke[0] > 1)
		{
			delay = (stroke[0] - 1) / 40;
			if (delay == 0)
				delay = 1;
		}
		for (long i = 0; i < stroke[0]; i++)
		{
			Point pt;
			if (LIGetPoint(stroke, i, &pt) == -1)
				return -1;
			if (ConvertToScreenCoord(&pt, &extent, &screen) == -1)
				return -1;
			if (i == 0)
				MoveTo(pt.h, pt.v);
			LineTo(pt.h, pt.v);
			if (animate != 0 && stroke[0] != 2)
				SleepTillTicks(Ticks() + delay);
		}
		if (pause != 0 && stroke[0] > 1)
			SleepTillTicks(Ticks() + pause);
	}
	return 0;
}


// ROM 0x00106b30 DrawLetterGroup__FPvP16LILetterInfoTypesT3P4RectUcUlT7T6
// The group's variants in the order they come, but the `hilite`th moved
// to the end (the one drawn last, and drawn slowly when animating).
long
DrawLetterGroup(void* image, LILetterInfoType* info, short start, short hilite, Rect* rect, UByte weight, ULong pause, ULong animate, UByte frame)
{
	long delay = 0;
	long length = LIGetLengthGroup(image, info, start);
	Rect group = *rect;
	group.right = group.left + length * kLetterCell;
	EraseRect(&group);
	if (frame != 0)
	{
		PenNormal();
		PenSize(1, 1);
		FrameRoundRect(&group, 10, 10);
	}
	long cell = 0;
	for (long i = 0; i < length; i++, cell++)
	{
		if (i == hilite)
			cell++;
		if (cell == length)
			cell = hilite;
		Boolean first = (cell == 0);
		Boolean last = (length - 1 == cell);
		Rect r = *rect;
		OffsetRect(&r, cell * kLetterCell, 0);
		LILetterVarType* variant = LIGetVariantInfo(image, info, start + cell);
		Rect extent, screen, lines;
		if (LIGetVariantBBox(image, variant, &extent) == -1
		||  CalculateScreenRect(&extent, &r, &screen) == -1
		||  LIGetVariantBaseLine(image, variant, &lines) == -1)
			return -1;
		PenNormal();
		SetFgPattern(GetStdPattern(grayPat));
		for (long which = 0; which < 2; which++)
		{
			short v = (which == 0) ? lines.top : lines.bottom;
			Point pt;
			pt.v = v;
			pt.h = 0;
			if (ConvertToScreenCoord(&pt, &extent, &screen) == -1)
				return -1;
			pt.h = r.left + 1;
			if (first)
				pt.h = pt.h + 1;
			MoveTo(pt.h, pt.v);
			pt.v = v;
			if (ConvertToScreenCoord(&pt, &extent, &screen) == -1)
				return -1;
			pt.h = r.right - 1;
			if (!last)
				pt.h = pt.h + 5;
			LineTo(pt.h, pt.v);
		}
		PenNormal();
		if (weight == 2)
			SetFgPattern(GetStdPattern(grayPat));
		else if (weight == 1)
			SetFgPattern(GetStdPattern(dkGrayPat));
		PenSize(2, 2);
		for (long s = 0; s < variant[1]; s++)
		{
			if (pause != 0 && cell == hilite)
				SleepTillTicks(Ticks() + pause);
			LIStrokeType* stroke = LIGetStrokeInfo(image, variant, s);
			if (stroke == nil)
				return -1;
			if (animate != 0 && stroke[0] > 1)
			{
				delay = (stroke[0] - 1) / 40;
				if (delay == 0)
					delay = 1;
			}
			for (long j = 0; j < stroke[0]; j++)
			{
				Point pt;
				if (LIGetPoint(stroke, j, &pt) == -1)
					return -1;
				if (ConvertToScreenCoord(&pt, &extent, &screen) == -1)
					return -1;
				if (j == 0)
					MoveTo(pt.h, pt.v);
				LineTo(pt.h, pt.v);
				if (animate != 0 && cell == hilite && stroke[0] != 2)
					SleepTillTicks(Ticks() + delay);
			}
		}
	}
	return 0;
}


/*------------------------------------------------------------------------------
	T h e   c u r s o r
------------------------------------------------------------------------------*/

// ROM 0x0010701c InitLetterCursor__FP12LetterCursor
Boolean
InitLetterCursor(LetterCursor* cursor)
{
	cursor->fImage = LIGetImageData();
	if (cursor->fImage != nil)
		cursor->fWeights = LIBeginWeights();
	cursor->fLine = 0;
	cursor->fColumn = 0;
	cursor->fIndex = 0;
	return cursor->fImage != nil;
}


// ROM 0x001070b4 DoneLetterCursor__FP12LetterCursor
void
DoneLetterCursor(LetterCursor* cursor)
{
	LIEndWeights(cursor->fWeights);
}


// ROM 0x001070bc InitLetterBounds__FP5TViewP12LetterCursor
// The first cell in the view's top left corner, `indent` in; the page is
// as many 35-pixel cells as the view holds.
void
InitLetterBounds(TView* view, LetterCursor* cursor)
{
	short indent = RINT(RefVar(view->GetVar(RSSYMindent)));
	Rect bounds = view->viewBounds;
	cursor->fBounds = bounds;
	cursor->fBounds.bottom = (UShort) cursor->fBounds.top + 0x1f;
	cursor->fBounds.left = (UShort) cursor->fBounds.left + indent;
	cursor->fBounds.right = (UShort) cursor->fBounds.left + 0x1f;
	cursor->fLines = (short) (bounds.bottom - bounds.top) / kLetterCell;
	cursor->fColumns = ((short) (bounds.right - bounds.left) - indent) / kLetterCell;
}


// ROM 0x00107198 ShowTitleLetter__FP12LetterCursorP5TView
// The letter written in the view's font at the start of its line, two
// thirds of the way down the cell.
void
ShowTitleLetter(LetterCursor* cursor, TView* view)
{
	StyleRecord style;
	StyleRecord* styles = &style;
	RefVar font(view->GetVar(RSSYMviewfont));
	if (NOTNIL(font))
	{
		CreateTextStyleRecord(font, &style);
		UniChar text[2];
		text[0] = ToUni(cursor->fLetter);
		text[1] = 0;
		FPoint where;
		where.x = (Fixed) ((ULong) ((UShort) view->viewBounds.left + 5) << 16);
		long top = cursor->fBounds.top;
		long height = (short) ((UShort) cursor->fBounds.bottom - top);
		where.y = (Fixed) ((ULong) ((height * 2) / 3 + top) << 16);
		DrawTextOnce(text, 1, &styles, nil, where, nil, nil);
	}
	if (style.fPattern != nil)
		DisposePattern(style.fPattern);
}


// ROM 0x001072b4 SetLetter__FP12LetterCursorUs
void
SetLetter(LetterCursor* cursor, UShort c)
{
	cursor->fLetter = c;
}


// ROM 0x001072c4 NextLetter__FP12LetterCursor
UShort
NextLetter(LetterCursor* cursor)
{
	cursor->fLetter = PairedChar(cursor->fLetter);
	return cursor->fLetter;
}


// ROM 0x001072f8 InitLetter__FP12LetterCursor
void
InitLetter(LetterCursor* cursor)
{
	cursor->fInfo = LIGetLetterInfo(cursor->fImage, cursor->fLetter);
	cursor->fGroupStart = 0;
}


// ROM 0x00107330 MoreLetters__FP12LetterCursor
Boolean
MoreLetters(LetterCursor* cursor)
{
	return cursor->fLetter != 0 && cursor->fLine < cursor->fLines;
}


// ROM 0x0010735c InitLetterGroup__FP12LetterCursor
Boolean
InitLetterGroup(LetterCursor* cursor)
{
	cursor->fVariant = LIGetVariantInfo(cursor->fImage, cursor->fInfo, cursor->fGroupStart);
	cursor->fGroup = LIGetGroupNumber(cursor->fImage, cursor->fInfo, cursor->fGroupStart);
	cursor->fGroupLength = LIGetLengthGroup(cursor->fImage, cursor->fInfo, cursor->fGroupStart);
	cursor->fWeight = LIGetVariantWeight(cursor->fWeights, cursor->fInfo[0], cursor->fGroup);
	return cursor->fWeight != 3 && cursor->fVariant != nil;
}


// ROM 0x00107400 MoreGroups__FP12LetterCursor
Boolean
MoreGroups(LetterCursor* cursor)
{
	return cursor->fGroupStart < cursor->fInfo[1];
}


// ROM 0x00107420 GroupFitsOnLine__FP12LetterCursor
Boolean
GroupFitsOnLine(LetterCursor* cursor)
{
	return cursor->fColumn + cursor->fGroupLength <= cursor->fColumns;
}


// ROM 0x001074ac PointInGroup__FP12LetterCursor6TPointPlT3
Boolean
PointInGroup(LetterCursor* cursor, Point pt, long* index, long* offset)
{
	Rect r = cursor->fBounds;
	for (long i = 0; i < cursor->fGroupLength; i++)
	{
		if (PtInRect(pt, &r))
		{
			*index = cursor->fIndex;
			*offset = i;
			return true;
		}
		OffsetRect(&r, kLetterCell, 0);
	}
	return false;
}


// ROM 0x00107548 DisplayNextLine__FP12LetterCursor
void
DisplayNextLine(LetterCursor* cursor)
{
	OffsetRect(&cursor->fBounds, cursor->fColumn * -kLetterCell, kLetterCell);
	cursor->fLine++;
	cursor->fColumn = 0;
}


// ROM 0x00107588 GroupHilited__FP12LetterCursorl
Boolean
GroupHilited(LetterCursor* cursor, long index)
{
	return !(index < cursor->fIndex || cursor->fIndex + cursor->fGroupLength <= index);
}


// ROM 0x001075b4 DrawGroup__FP12LetterCursorl
void
DrawGroup(LetterCursor* cursor, long hilite)
{
	DrawLetterGroup(cursor->fImage, cursor->fInfo, cursor->fGroupStart, cursor->fGroupStart, &cursor->fBounds,
					cursor->fWeight, 0, 0, cursor->fIndex == hilite);
}


// ROM 0x00107608 DrawGroup__FP12LetterCursorlN22Uc
void
DrawGroup(LetterCursor* cursor, long which, long pause, long animate, UByte frame)
{
	DrawLetterGroup(cursor->fImage, cursor->fInfo, cursor->fGroupStart, which, &cursor->fBounds,
					cursor->fWeight, pause, animate, frame);
}


// ROM 0x0010765c DisplayNextGroup__FP12LetterCursor
void
DisplayNextGroup(LetterCursor* cursor)
{
	OffsetRect(&cursor->fBounds, cursor->fGroupLength * kLetterCell, 0);
	cursor->fColumn += cursor->fGroupLength;
}


// ROM 0x0010769c GetNextGroup__FP12LetterCursor
void
GetNextGroup(LetterCursor* cursor)
{
	cursor->fGroupStart = cursor->fGroupStart + cursor->fGroupLength;
	cursor->fIndex += cursor->fGroupLength;
}


// ROM 0x001076cc EndOfPage__FP12LetterCursor
Boolean
EndOfPage(LetterCursor* cursor)
{
	return cursor->fLines <= cursor->fLine;
}


// ROM 0x001076e8 ToUni__FUs
UniChar
ToUni(UShort c)
{
	UByte text[2];
	UniChar u[2];
	text[0] = c;
	text[1] = 0;
	ConvertToUnicode(text, u, kMacRomanEncoding, 0x7fffffff);
	return u[0];
}


// ROM 0x0010772c FromUni__FUs
UByte
FromUni(UShort c)
{
	UniChar u[2];
	UByte text[4];
	u[0] = c;
	u[1] = 0;
	ConvertFromUnicode(u, text, kMacRomanEncoding, 0x7fffffff);
	return text[0];
}


// ROM 0x00107778 GetTitleLetter__FP5TView
UByte
GetTitleLetter(TView* view)
{
	UniChar u[2];
	UByte text[4];
	u[0] = RCHAR(RefVar(view->GetVar(RSSYMorigin)));
	u[1] = 0;
	ConvertFromUnicode(u, text, kMacRomanEncoding, 0x7fffffff);
	return text[0];
}


// ROM 0x0010793c HiliteLetter__FP5TViewUllUc
// The group whose first variant is `index` drawn again, framed or not -
// with its `offset`th variant drawn slowly when that is one of its own.
void
HiliteLetter(TView* view, ULong index, long offset, UByte on)
{
	LetterCursor cursor;
	if (!InitLetterCursor(&cursor))
		return;
	InitLetterBounds(view, &cursor);
	SetLetter(&cursor, GetTitleLetter(view));
	do
	{
		InitLetter(&cursor);
		while (MoreGroups(&cursor))
		{
			if (InitLetterGroup(&cursor))
			{
				if (!GroupFitsOnLine(&cursor))
					DisplayNextLine(&cursor);
				if (EndOfPage(&cursor))
					break;
				if (GroupHilited(&cursor, index))
				{
					long pause = 0;
					long animate = 0;
					if (offset >= 0 && offset < cursor.fGroupLength)
					{
						pause = 0x10;
						animate = 3;
					}
					DrawGroup(&cursor, offset, pause, animate, on);
				}
				DisplayNextGroup(&cursor);
			}
			GetNextGroup(&cursor);
		}
		if (NextLetter(&cursor) != 0)
			DisplayNextLine(&cursor);
	} while (MoreLetters(&cursor));
	DoneLetterCursor(&cursor);
}


// ROM 0x00107dc0 CountEnabledGroups__FPvP16LILetterInfoType
long
CountEnabledGroups(void* image, LILetterInfoType* info)
{
	long count = 0;
	Handle weights = LIBeginWeights();
	long previous = -1;
	for (long i = 0; i < info[1]; i++)
	{
		long group = LIGetGroupNumber(image, info, i);
		if (group != previous && (previous = LIGetVariantWeight(weights, info[0], group)) == 0)
			count++;
		previous = group;
	}
	LIEndWeights(weights);
	return count;
}


/*------------------------------------------------------------------------------
	T h e   n a t i v e s
------------------------------------------------------------------------------*/

// ROM 0x001077cc FDrawLetterShapes
// DrawLetterScript(): the view's letters drawn, one to a line, a rule
// between them.
Ref
FDrawLetterShapes(RefArg rcvr)
{
	TView* view = GetView(rcvr);
	LetterCursor cursor;
	if (!InitLetterCursor(&cursor))
		return NILREF;
	InitLetterBounds(view, &cursor);
	SetLetter(&cursor, GetTitleLetter(view));
	do
	{
		InitLetter(&cursor);
		ShowTitleLetter(&cursor, view);
		while (MoreGroups(&cursor))
		{
			if (InitLetterGroup(&cursor))
			{
				if (!GroupFitsOnLine(&cursor))
					DisplayNextLine(&cursor);
				if (EndOfPage(&cursor))
					break;
				DrawGroup(&cursor, gLIHiliteIndex);
				DisplayNextGroup(&cursor);
			}
			GetNextGroup(&cursor);
		}
		if (NextLetter(&cursor) != 0)
		{
			DisplayNextLine(&cursor);
			short v = cursor.fBounds.top - 3;
			PenNormal();
			PenSize(2, 2);
			Rect bounds = view->viewBounds;
			MoveTo(bounds.left + 5, v);
			LineTo(bounds.right - 5, v);
		}
	} while (MoreLetters(&cursor));
	DoneLetterCursor(&cursor);
	return NILREF;
}


// ROM 0x00107a7c FClickLetterShapes
// ClickLetterScript(unit): the group under the pen hilited (the one that
// was, put back) and made the hilited one.  ==> its letter, or nil.
// (The drawing is done in the view's visible region, which is copied
// into the port's afterwards.)
Ref
FClickLetterShapes(RefArg rcvr, RefArg unit)
{
	TView* view = GetView(rcvr);
	long offset = 0;
	long index = -1;
	RefVar result;
	LetterCursor cursor;
	if (!InitLetterCursor(&cursor))
		return result;
	InitLetterBounds(view, &cursor);
	SetLetter(&cursor, GetTitleLetter(view));
	Point pt = StrokeFromRef(unit)->FirstPoint();
	UShort letter;
	do
	{
		InitLetter(&cursor);
		letter = cursor.fLetter;
		while (MoreGroups(&cursor))
		{
			if (InitLetterGroup(&cursor))
			{
				if (!GroupFitsOnLine(&cursor))
					DisplayNextLine(&cursor);
				if (EndOfPage(&cursor) || PointInGroup(&cursor, pt, &index, &offset))
					break;
				DisplayNextGroup(&cursor);
			}
			GetNextGroup(&cursor);
		}
		if (NextLetter(&cursor) != 0)
			DisplayNextLine(&cursor);
	} while (MoreLetters(&cursor) && index < 0);
	if (index >= 0)
	{
		TRegion vis(view->SetupVisRgn());
		TRegionVar visRgn(vis);
		if (gLIHiliteIndex < (ULong) index || (ULong) (index + cursor.fGroupLength) <= gLIHiliteIndex)
		{
			HiliteLetter(view, gLIHiliteIndex, -1, 0);
			HiliteLetter(view, index, -1, 1);
		}
		else
			HiliteLetter(view, index, offset, 1);
		GrafPort* port;
		GetPort(&port);
		CopyRgn(visRgn, port->visRgn);
		gLIHiliteIndex = index;
		result = MAKECHAR(ToUni(letter));
	}
	DoneLetterCursor(&cursor);
	return result;
}


// ROM 0x00107cdc FCountLetterShapes
// CountLetters(): how many pictures there are.
Ref
FCountLetterShapes(RefArg /*rcvr*/)
{
	UByte* image = LIGetImageData();
	if (image != nil)
		return MAKEINT(LIVariantCount(image));
	return MAKEINT(0);
}


// The hilited group's letter and variant: the view's letter, or the
// other of its pair when the index is past the first letter's variants.
static void
HilitedGroup(UByte* image, TView* view, LILetterInfoType** info, long* variant)
{
	long c = GetTitleLetter(view);
	*info = LIGetLetterInfo(image, c);
	*variant = gLIHiliteIndex;
	long other;
	if ((*info)[1] <= gLIHiliteIndex && (other = (short) PairedChar(c)) != 0)
	{
		*variant -= (*info)[1];
		*info = LIGetLetterInfo(image, other);
	}
}


// ROM 0x00107d04 FGetHiliteWeight
// GetLetterHilite(): the hilited group's weight (0 used, 1 less, 2 not;
// 3 not in the letter set).
Ref
FGetHiliteWeight(RefArg rcvr)
{
	UByte* image = LIGetImageData();
	Handle weights = LIBeginWeights();
	TView* view = GetView(rcvr);
	LILetterInfoType* info;
	long variant;
	HilitedGroup(image, view, &info, &variant);
	short group = LIGetGroupNumber(image, info, variant);
	long weight = LIGetVariantWeight(weights, info[0], group);
	LIEndWeights(weights);
	return MAKEINT(weight);
}


// ROM 0x00107e54 FSetHiliteWeight
// SetLetterHilite(weight): the hilited group given the weight - unless
// that leaves the letter with no group in use, when it is put back and
// true answered.
Ref
FSetHiliteWeight(RefArg rcvr, RefArg weightArg)
{
	UByte* image = LIGetImageData();
	Handle weights = LIBeginWeights();
	TView* view = GetView(rcvr);
	RefVar result;
	LILetterInfoType* info;
	long variant;
	HilitedGroup(image, view, &info, &variant);
	short group = LIGetGroupNumber(image, info, variant);
	UByte was = LIGetVariantWeight(weights, info[0], group);
	LISetVariantWeight(weights, info[0], group, RINT(weightArg));
	if (CountEnabledGroups(image, info) == 0)
	{
		LISetVariantWeight(weights, info[0], group, was);
		result = TRUEREF;
	}
	LIEndWeights(weights);
	return result;
}


// ROM 0x00107f94 FGetHiliteIndex
Ref
FGetHiliteIndex(RefArg /*rcvr*/)
{
	return MAKEINT(gLIHiliteIndex);
}


// ROM 0x00107fb0 FSetHiliteIndex
Ref
FSetHiliteIndex(RefArg /*rcvr*/, RefArg index)
{
	gLIHiliteIndex = RINT(index);
	return NILREF;
}


// ROM 0x00107fe8 FGetIndexChar
// getIndexChar(index): the letter of the index-th picture - its eight-bit
// code made a character as it is, not converted.
Ref
FGetIndexChar(RefArg /*rcvr*/, RefArg index)
{
	UByte* image = LIGetImageData();
	LILetterInfoType* info;
	short variant;
	LIGetIndexedLetterInfo(image, RINT(index), &info, &variant);
	return MAKECHAR(info[0]);
}


// ROM 0x0010804c FGetLetterIndex
// getLetterIndex(char): the index of the letter's first picture.
Ref
FGetLetterIndex(RefArg /*rcvr*/, RefArg c)
{
	UByte* image = LIGetImageData();
	return MAKEINT(LIGetVariantIndex(image, (UByte) RCHAR(c), 0));
}


// ROM 0x001080b0 FDrawStringShapes
// DrawStringShapes(string, top): the string written across the middle of
// the screen at `top` in a picture of each letter, the variant picked at
// random (a 't' among its first five).
Ref
FDrawStringShapes(RefArg /*rcvr*/, RefArg string, RefArg top)
{
	UByte* image = LIGetImageData();
	Handle weights = LIBeginWeights();
	const UniChar* s = (const UniChar*) BinaryData(string);
	Rect rect;
	rect.top = RINT(top);
	rect.left = ((short) screenWidth - (short) Ustrlen(s) * 22) / 2;
	rect.bottom = rect.top + 0x20;
	rect.right = rect.left + 0x16;
	Rect erase = rect;
	erase.left = 0;
	erase.right = screenWidth;
	EraseRect(&erase);
	for (UniChar c; (c = *s++) != 0; )
	{
		LILetterInfoType* info = LIGetLetterInfo(image, c & 0xff);
		if (info != nil)
		{
			long most = info[1] - 1;
			if (c == 't')
				most = 4;
			LILetterVarType* variant = LIGetVariantInfo(image, info, RangeRand(0, most));
			if (variant != nil)
				DrawLetterImage(image, variant, &rect, 0, 0, 0, 0);
		}
		OffsetRect(&rect, 0x16, 0);
	}
	LIEndWeights(weights);
	return NILREF;
}


void
RegisterLetterShapesNatives(void)
{
	RegisterNativeFunction("FDrawLetterShapes", (void*) FDrawLetterShapes, 0);
	RegisterNativeFunction("FClickLetterShapes", (void*) FClickLetterShapes, 1);
	RegisterNativeFunction("FCountLetterShapes", (void*) FCountLetterShapes, 0);
	RegisterNativeFunction("FGetHiliteWeight", (void*) FGetHiliteWeight, 0);
	RegisterNativeFunction("FSetHiliteWeight", (void*) FSetHiliteWeight, 1);
	RegisterNativeFunction("FGetHiliteIndex", (void*) FGetHiliteIndex, 0);
	RegisterNativeFunction("FSetHiliteIndex", (void*) FSetHiliteIndex, 1);
	RegisterNativeFunction("FGetIndexChar", (void*) FGetIndexChar, 1);
	RegisterNativeFunction("FGetLetterIndex", (void*) FGetLetterIndex, 1);
	RegisterNativeFunction("FDrawStringShapes", (void*) FDrawStringShapes, 2);
}
