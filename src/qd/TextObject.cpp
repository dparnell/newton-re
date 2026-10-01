/*
	File:		qd/TextObject.cpp

	Contains:	Text objects: made, thrown away, and handed to the port's
				text proc - TextObject.h says what they are.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TextObject.h"
#include "TextLayout.h"
#include "Ports.h"
#include "Fonts.h"
#include "FixedMath.h"
#include "NewtonMemory.h"
#include "Regions.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include <string.h>


// ROM 0x0035bfc4 NewText__FPvlPP11StyleRecordPs6FPointP11TextOptions
// A text object in a handle of its own, flagged as allocated (so
// DisposeText frees it), everything else nought.  (Host: the handle is
// sizeof(TextObject), the ROM's 0x50 bytes holding pointers.)  ==> nil if
// there was no room.
TextObjectRef
NewText(const void* text, long length, StyleRecord** styles, const short* runLengths, FPoint location, TextOptions* options)
{
	Handle h = NewHandle(sizeof(TextObject));
	if (h != nil)
	{
		TextObject* obj = (TextObject*) *h;
		memset(obj, 0, sizeof(TextObject));
		obj->fRunLengths = runLengths;
		obj->fStyles = styles;
		obj->fLength = length;
		obj->fText = text;
		obj->fLocation = location;
		obj->fOptions = options;
		obj->fFlags = kTextObjAllocated;
	}
	return (TextObjectRef) h;
}


// ROM 0x0035c3fc DefaultScanner__FPvlN22PPv
// The characters straight out of the text word: offset characters of
// charSize bytes along (nothing to do to hold or let go of them).
long
DefaultScanner(void* refCon, long offset, long count, long charSize, void** chars)
{
	if (offset < 0)
		return 0;
	*chars = (char*) refCon + charSize * offset;
	return count;
}


// (host) A pass over a text object's characters: the scanner the options
// name (not a picture's, which the player clears) or DefaultScanner.
TTextObjectChars::TTextObjectChars(const TextObject* obj)
{
	fRefCon = (void*) obj->fText;
	fScanner = (obj->fOptions != nil && obj->fOptions->fScanner != nil) ? obj->fOptions->fScanner : DefaultScanner;
	fScanner(fRefCon, -2, 0, 0, nil);
	void* chars = nil;
	fScanner(fRefCon, 0, obj->fLength, 2, &chars);
	fChars = (const UniChar*) chars;
}


TTextObjectChars::~TTextObjectChars()
{
	fScanner(fRefCon, -1, 0, 0, nil);
}


// ROM 0x0035b624 InvalCachedTextInfo__Fl
// The layout's caches thrown away (each from the heap it came from) and
// the layout state cleared, so the next operation lays the text out again.
void
InvalCachedTextInfo(TextObjectRef text)
{
	if (text == 0)
		return;
	TextObject* obj = TextObj(text);
	ULong32 flags = obj->fFlags;
	if (obj->fCache40 != nil)
	{
		DisposPtr((Ptr) obj->fCache40);
		obj->fCache40 = nil;
	}
	obj->fField44 = 0;
	obj->fField48 = 0;
	if (obj->fCache20 != nil)
	{
		if ((flags & kTextObjTempCache1) == 0)
			DisposPtr((Ptr) obj->fCache20);
		else
		{
			QDDisposeTempPtr(obj->fCache20);
			flags &= ~kTextObjTempCache1;
		}
		obj->fCache20 = nil;
	}
	if (obj->fCache28 != nil)
	{
		if ((flags & kTextObjTempCache2) == 0)
			DisposPtr((Ptr) obj->fCache28);
		else
		{
			QDDisposeTempPtr(obj->fCache28);
			flags &= ~kTextObjTempCache2;
		}
		obj->fCache28 = nil;
	}
	obj->fFlags = flags & ~kTextObjLayoutMask;
}


// ROM 0x0035dc94 DisposeText__Fl
// The caches thrown away, and the object itself if NewText made it (one
// on a caller's stack is left there).
void
DisposeText(TextObjectRef text)
{
	if (text == 0)
		return;
	ULong32 flags = TextObj(text)->fFlags;
	InvalCachedTextInfo(text);
	if ((flags & kTextObjAllocated) == 0)
		return;
	DisposHandle((Handle) text);
}


// The port's text proc, or the standard one: with no procs at all, or for
// an operation other than drawing on a printer's port (pixMapFlags' kind
// 0x200), whose proc only draws.  (Host: a nil proc is the standard one,
// as for the other procs.)
static TextObjProc
TextProc(TextObjectRef text)
{
	GrafPort* port = GetCurrentPort();
	if (port->grafProcs == nil
	 || ((TextObj(text)->fFlags & kTextObjOpMask) != 0 && (port->portBits.pixMapFlags & 0xf00) == 0x200)
	 || port->grafProcs->textProc == nil)
		return StdText;
	return port->grafProcs->textProc;
}


// ROM 0x0035a60c CallDrawText__FlN21
// The text object handed to the text proc at the scales given, for
// whatever operation its flags ask.
void
CallDrawText(TextObjectRef text, Fixed hScale, Fixed vScale)
{
	TextProc(text)(text, hScale, vScale);
}


// ROM 0x0035df74 DrawTextObj__Fl
// The text object drawn at full size.
void
DrawTextObj(TextObjectRef text)
{
	TextObj(text)->fFlags &= ~kTextObjOpMask;
	TextProc(text)(text, 0x10000, 0x10000);
}


// (host) The object's text laid out as the drawing lays it out: every
// character's advance (MeasureGlyphWidths at the object's scales, the
// length cut to what fits the options' width, as the ROM's is), each
// run's font scale, and the justification spread over them
// (JustifyText), whose answer - where the text starts from its location -
// comes back through `start`.  The ROM keeps this in the object's caches
// (+0x20, +0x28, +0x40) and the start at +0x2c; the host keeps no caches
// (the DEVIATION above) and works it out for each question.  ==> false
// when there was no room for it; the caller gives the arrays back with
// HostDoneLayOut.
Boolean
HostLayOut(TextObject* obj, TextLayout* layout, Fixed* start)
{
	long length = obj->fLength;
	if (length < 0)
		length = 0;
	layout->fAdvances = (Fixed*) QDNewTempPtr((length + 1) * sizeof(Fixed));
	layout->fRuns = (long*) QDNewTempPtr((length + 1) * sizeof(long));
	layout->fRunScales = (Fixed*) QDNewTempPtr((length + 1) * sizeof(Fixed));
	if (layout->fAdvances == nil || layout->fRuns == nil || layout->fRunScales == nil)
	{
		HostDoneLayOut(layout);
		return false;
	}
	TTextObjectChars characters(obj);
	const UniChar* chars = characters.fChars;
	// (the scales UpdateLayoutState set; an object never asked yet is at 1.0)
	Fixed hScale = (obj->fHScale != 0) ? obj->fHScale : 0x10000;
	Fixed vScale = (obj->fVScale != 0) ? obj->fVScale : 0x10000;
	long fitted = MeasureGlyphWidths(chars, length, obj->fStyles, obj->fRunLengths, obj->fOptions, layout, GetCurrentPort(), hScale, vScale, obj->fFlags);
	if (fitted < length)
	{
		layout->fWidth = 0;
		for (long i = 0; i < fitted; i++)
			layout->fWidth += layout->fAdvances[i];
		length = fitted;
		obj->fLength = fitted;
	}
	layout->fCount = length;
	*start = JustifyText(chars, length, obj->fOptions, layout);
	return true;
}


void
HostDoneLayOut(TextLayout* layout)
{
	QDDisposeTempPtr(layout->fAdvances);
	QDDisposeTempPtr(layout->fRuns);
	QDDisposeTempPtr(layout->fRunScales);
	layout->fAdvances = nil;
	layout->fRuns = nil;
	layout->fRunScales = nil;
}


// (host) CalcTextAdvance's sum over a layout in hand: the widths of the
// first `count` characters added up a run at a time, each run's sum
// scaled by its font's scale (the ROM's +0x40 cache, a scale for each run;
// +0x44 when there is one run) when that is not 1.0.
Fixed
LayoutAdvance(TextObject* obj, const TextLayout* layout, long count)
{
	if (count > layout->fCount)
		count = layout->fCount;
	const short* runLengths = obj->fRunLengths;
	long left = (runLengths != nil) ? runLengths[0] : count;
	long run = 0;
	Fixed total = 0;
	Fixed sum = 0;
	for (long i = 0; i < count; i++)
	{
		sum += layout->fAdvances[i];
		if (--left == 0)
		{
			if (layout->fRunScales[run] != 0x10000)
				sum = FixedMultiply(sum, layout->fRunScales[run]);
			total += sum;
			sum = 0;
			if (count - i > 1)
			{
				run++;
				left = runLengths[run];
			}
		}
	}
	if (sum != 0)
	{
		if (layout->fRunScales[run] != 0x10000)
			sum = FixedMultiply(sum, layout->fRunScales[run]);
		total += sum;
	}
	return total;
}


// ROM 0x0035c080 UpdateLayoutState__FlN31
// The layout brought up to `level` (1 measured, 2 justified, 3 the widths
// remapped), the caches thrown away first when the scales have changed.
// DEVIATION: the host keeps no caches, so there is nothing to bring up to
// date - each question lays the text out afresh (HostLayOut) - and only
// the bookkeeping is kept: the scales and the level reached.
Boolean
UpdateLayoutState(TextObjectRef text, long level, Fixed hScale, Fixed vScale)
{
	if (text == 0)
		return true;
	TextObject* obj = TextObj(text);
	if (obj->fHScale != hScale || obj->fVScale != vScale)
	{
		InvalCachedTextInfo(text);
		obj = TextObj(text);
		obj->fVScale = vScale;
		obj->fHScale = hScale;
	}
	long have = obj->fFlags & kTextObjLayoutMask;
	level &= 0xff;
	if (have < level)
		obj->fFlags = (obj->fFlags & ~kTextObjLayoutMask) | level;
	return true;
}


// ROM 0x0035bfbc RemapCharWidths__Fl
// The widths remapped to the characters' positions (the ROM's answers
// yes and does nothing).
Boolean
RemapCharWidths(TextObjectRef /*text*/)
{
	return true;
}


// ROM 0x0035b220 CalcTextAdvance__FlP6FPointT1
// The advance of the first `count` characters (at most the laid-out
// length): each character's width, a run's sum scaled by the run's font
// scale (a strike stretched to the size asked for).  The vertical advance
// is nought.
void
CalcTextAdvance(TextObjectRef text, FPoint* advance, long count)
{
	advance->x = 0;
	advance->y = 0;
	if (text == 0)
		return;
	TextObject* obj = TextObj(text);
	TextLayout layout;
	Fixed start;
	if (!HostLayOut(obj, &layout, &start))
		return;
	if (count > 0)
		advance->x = LayoutAdvance(TextObj(text), &layout, count);
	HostDoneLayOut(&layout);
}


// ROM 0x0035b3f8 CalcTextBounds__FlPvN21
// The text object's bounds, into the six words at `result`: where the
// justification starts it, its advance (x and y), and the greatest
// ascent, descent and leading of its runs' fonts - opened at the object's
// scales, the font engine's own scale applied to a stretched strike's -
// all taken back to the scales asked for.
void
CalcTextBounds(TextObjectRef text, void* result, Fixed hScale, Fixed vScale)
{
	if (text == 0)
		return;
	TextObject* obj = TextObj(text);
	TextLayout layout;
	Fixed start;
	if (!HostLayOut(obj, &layout, &start))
		return;
	obj = TextObj(text);
	long count = layout.fCount;			// (the ROM's +0x24, else the length)
	if (count == 0)
		count = obj->fLength;
	HostDoneLayOut(&layout);
	Fixed ascent = 0;
	Fixed descent = 0;
	Fixed leading = 0;
	GrafPort* port = GetCurrentPort();
	FPoint advance;
	CalcTextAdvance(text, &advance, count);
	obj = TextObj(text);
	StyleRecord** styles = obj->fStyles;
	const short* runLengths = obj->fRunLengths;
	while (count > 0)
	{
		FontEngineInfo info;
		if (OpenFont(&port->portBits, *styles, obj->fHScale, obj->fVScale, &info) != 3)
		{
			Fixed a = (Fixed) ((ULong32) info.fAscent << 16);
			Fixed d = (Fixed) ((ULong32) info.fDescent << 16);
			Fixed l = (Fixed) ((ULong32) info.fLeading << 16);
			if (info.fScaleY != 0x10000)
			{
				a = FixedMultiply(a, info.fScaleY);
				d = FixedMultiply(d, info.fScaleY);
				l = FixedMultiply(l, info.fScaleY);
			}
			if (ascent < a)
				ascent = a;
			if (descent < d)
				descent = d;
			if (leading < l)
				leading = l;
			CloseFont(&info);
		}
		// (host: a run with no font has nothing to measure - the ROM
		//  reads the engine's info regardless)
		obj = TextObj(text);
		styles++;
		if (runLengths == nil)
			count = 0;
		else
			count -= *runLengths++;
	}
	Fixed offset = start;
	if (hScale != 0x10000)
	{
		offset = FixedDivide(offset, hScale);
		advance.x = FixedDivide(advance.x, hScale);
	}
	if (vScale != 0x10000)
	{
		advance.y = FixedDivide(advance.y, vScale);
		ascent = FixedDivide(ascent, vScale);
		descent = FixedDivide(descent, vScale);
		leading = FixedDivide(leading, vScale);
	}
	Fixed* answer = (Fixed*) result;
	answer[0] = offset;
	answer[1] = advance.x;
	answer[2] = advance.y;
	answer[3] = ascent;
	answer[4] = descent;
	answer[5] = leading;
}


// the arguments CharToPoint leaves at the object's +0x4c
struct CharToPointArgs
{
	long		fOffset;
	FPoint*		fPoint;
};

// ROM 0x0035e13c DoCharToPoint__FlN21
// Where the character at the offset starts: the text's location, plus
// where the justification starts it, plus the advance of the characters
// before it (at the horizontal scale).  With no layout, the location.
void
DoCharToPoint(TextObjectRef text, Fixed hScale, Fixed vScale)
{
	TextObject* obj = TextObj(text);
	CharToPointArgs* args = (CharToPointArgs*) obj->fResult;
	if (!UpdateLayoutState(text, 3, hScale, vScale))
	{
		*args->fPoint = TextObj(text)->fLocation;
		return;
	}
	obj = TextObj(text);
	TextLayout layout;
	Fixed start = 0;
	if (HostLayOut(obj, &layout, &start))
		HostDoneLayOut(&layout);
	FPoint advance;
	CalcTextAdvance(text, &advance, args->fOffset);
	Fixed x = start + advance.x;
	if (hScale != 0x10000)
		x = FixedDivide(x, hScale);
	obj = TextObj(text);
	args->fPoint->x = obj->fLocation.x + x;
	args->fPoint->y = obj->fLocation.y;
}


// the arguments PointToChar leaves at the object's +0x4c
struct PointToCharArgs
{
	FPoint		fPoint;
	long		fOffset;		// ==>
};

// ROM 0x00359d80 DoPointToChar__FlN21
// The character boundary nearest the point: 0 before the text's start,
// its length past its end, otherwise the characters whose middles the
// point is past.  NOT YET RECONSTRUCTED: text at an angle (the options'
// +0x0c, projected through FractSineCosine) - the host's text is never
// turned, so the horizontal distance is taken as the ROM's does for
// unturned text.
void
DoPointToChar(TextObjectRef text, Fixed hScale, Fixed vScale)
{
	TextObject* obj = TextObj(text);
	PointToCharArgs* args = (PointToCharArgs*) obj->fResult;
	args->fOffset = 0;
	if (!UpdateLayoutState(text, 3, hScale, vScale))
		return;
	obj = TextObj(text);
	TextLayout layout;
	Fixed start;
	if (!HostLayOut(obj, &layout, &start))
		return;
	long count = layout.fCount;
	Fixed d = args->fPoint.x - obj->fLocation.x;
	if (hScale != 0x10000)
		d = FixedMultiply(d, hScale);
	Fixed x = d - start;
	if (x >= 0)
	{
		if (x > layout.fWidth)
			args->fOffset = TextObj(text)->fLength;
		else
		{
			Fixed sum = 0;
			long left = count;
			for (long i = 0; left > 0; i++)
			{
				Fixed w = layout.fAdvances[i];
				if (sum + (w >> 1) > x)
					break;
				sum += w;
				left--;
			}
			args->fOffset = count - left;
		}
	}
	HostDoneLayOut(&layout);
}


// ROM 0x00359d40 PointToChar__Fl6FPoint
long
PointToChar(TextObjectRef text, FPoint point)
{
	PointToCharArgs args;
	args.fPoint = point;
	args.fOffset = 0;
	TextObject* obj = TextObj(text);
	obj->fFlags = (obj->fFlags & ~kTextObjOpMask) | kTextObjOpPointToChar;
	obj->fResult = &args;
	CallDrawText(text, 0x10000, 0x10000);
	return args.fOffset;
}


// ROM 0x0035e100 CharToPoint__FlT1P6FPoint
void
CharToPoint(TextObjectRef text, long offset, FPoint* point)
{
	CharToPointArgs args;
	args.fOffset = offset;
	args.fPoint = point;
	TextObject* obj = TextObj(text);
	obj->fFlags = (obj->fFlags & ~kTextObjOpMask) | kTextObjOpCharToPoint;
	obj->fResult = &args;
	CallDrawText(text, 0x10000, 0x10000);
}


// ROM 0x0035df90 GetTextObjField__Fl15TextObjectFieldPv
// A field of the object, or (the fitted length, the bounds, the layout's
// numbers) what the text proc answers when asked - the bounds through
// DispatchCalcBounds.  NOT YET RECONSTRUCTED: the layout's numbers (7),
// which StdText does not answer yet - the result is left as it was.
void
GetTextObjField(TextObjectRef text, TextObjectField field, void* result)
{
	TextObject* obj = TextObj(text);
	switch (field)
	{
	case kTextObjText:
		*(const void**) result = obj->fText;
		break;
	case kTextObjFittedLength:
		obj->fFlags = (obj->fFlags & ~kTextObjOpMask) | kTextObjOpWidth;
		obj->fResult = result;
		CallDrawText(text, 0x10000, 0x10000);
		break;
	case kTextObjStyles:
		*(StyleRecord***) result = obj->fStyles;
		break;
	case kTextObjRunLengths:
		*(const short**) result = obj->fRunLengths;
		break;
	case kTextObjLocation:
		memmove(result, &obj->fLocation, sizeof(FPoint));
		break;
	case kTextObjOptions:
		*(TextOptions**) result = obj->fOptions;
		break;
	case kTextObjBounds:
		DispatchCalcBounds(text, (TextBoundsInfo*) result);
		break;
	case kTextObjMetrics:
		obj->fFlags = (obj->fFlags & ~kTextObjOpMask) | kTextObjOpMetrics;
		obj->fResult = result;
		CallDrawText(text, 0x10000, 0x10000);
		break;
	default:
		break;
	}
}


// ROM 0x0035e028 SetTextObjField__Fl15TextObjectFieldPv
// A field of the object changed, the layout's caches thrown away (except
// for the location, which moves the text without changing its layout).
// The text is the pointer `value` points at; the styles, the run lengths
// and the options are `value` itself.  ==> whether it took.  NOT YET
// RECONSTRUCTED: a length that grows rewinds the layout (RewindLength,
// 0x0035e0c8) - the host keeps none to rewind.
Boolean
SetTextObjField(TextObjectRef text, TextObjectField field, void* value)
{
	TextObject* obj = TextObj(text);
	switch (field)
	{
	case kTextObjText:
		obj->fText = *(const void**) value;
		break;
	case kTextObjFittedLength:
	{
		long length = *(long*) value;
		long had = obj->fLength;
		obj->fLength = length;
		if (had < length)
			return true;					// (RewindLength)
		break;
	}
	case kTextObjStyles:
		obj->fStyles = (StyleRecord**) value;
		break;
	case kTextObjRunLengths:
		obj->fRunLengths = (const short*) value;
		break;
	case kTextObjLocation:
		memcpy(&obj->fLocation, value, sizeof(FPoint));
		return true;
	case kTextObjOptions:
		obj->fOptions = (TextOptions*) value;
		break;
	case kTextObjBounds:
	case kTextObjMetrics:
		return false;
	case kTextObjSetFlag10000:
		obj->fFlags |= 0x10000;
		return false;
	}
	InvalCachedTextInfo(text);
	return true;
}


// ROM 0x00195cb0 GetTextObjField__16TQDLibraryDriverFliPv
void
TQDLibraryDriver::GetTextObjField(TextObjectRef text, int field, void* result)
{
	::GetTextObjField(text, (TextObjectField) field, result);
}


// ROM 0x00195cbc CharToPoint__16TQDLibraryDriverFlT1P6FPoint
void
TQDLibraryDriver::CharToPoint(TextObjectRef text, long offset, FPoint* point)
{
	::CharToPoint(text, offset, point);
}


// ROM 0x00195cc0 PointToChar__16TQDLibraryDriverFl6FPoint
long
TQDLibraryDriver::PointToChar(TextObjectRef text, FPoint point)
{
	return ::PointToChar(text, point);
}


// ROM 0x0035b07c StdText
// The standard text proc: the operation the object's flags ask for.
// Drawing is recorded into an open picture and then drawn; the fitted
// length is the object's length once it is laid out (it is cut to what
// fits); CharToPoint and PointToChar are answered into the arguments the
// object's +0x4c points at, and so are the bounds (0x200: CalcTextBounds,
// or -1 for both advances when there is no layout).  NOT YET
// RECONSTRUCTED: the layout's three numbers (0x400) and TextArrow
// (0x2000).
extern "C" void
StdText(TextObjectRef text, Fixed hScale, Fixed vScale)
{
	switch (TextObj(text)->fFlags & kTextObjOpMask)
	{
	case kTextObjOpDraw:
		DoPutText(text, hScale, vScale);
		DrText(text, hScale, vScale);
		break;
	case kTextObjOpWidth:
		{
			UpdateLayoutState(text, 2, hScale, vScale);
			TextObject* obj = TextObj(text);
			TextLayout layout;
			Fixed start;
			if (HostLayOut(obj, &layout, &start))
				HostDoneLayOut(&layout);
			obj = TextObj(text);
			*(long*) obj->fResult = obj->fLength;
		}
		break;
	case kTextObjOpBounds:
		if (!UpdateLayoutState(text, 2, hScale, vScale))
		{
			Fixed* answer = (Fixed*) TextObj(text)->fResult;
			answer[1] = -1;
			answer[2] = -1;
		}
		else
			CalcTextBounds(text, TextObj(text)->fResult, hScale, vScale);
		break;
	case kTextObjOpCharToPoint:
		DoCharToPoint(text, hScale, vScale);
		break;
	case kTextObjOpPointToChar:
		DoPointToChar(text, hScale, vScale);
		break;
	default:
		break;
	}
}
