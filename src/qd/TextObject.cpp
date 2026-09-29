/*
	File:		qd/TextObject.cpp

	Contains:	Text objects: made, thrown away, and handed to the port's
				text proc - TextObject.h says what they are.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TextObject.h"
#include "NewtonMemory.h"
#include "Regions.h"
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


// ROM 0x0035b07c StdText
// The standard text proc: the operation the object's flags ask for.
// Drawing is recorded into an open picture and then drawn.  NOT YET
// RECONSTRUCTED: the width (0x100), the bounds (0x200: UpdateLayoutState,
// CalcTextBounds), 0x400, CharToPoint (0x800), PointToChar (0x1000) and
// TextArrow (0x2000) - Text.cpp measures for itself meanwhile.
extern "C" void
StdText(TextObjectRef text, Fixed hScale, Fixed vScale)
{
	switch (TextObj(text)->fFlags & kTextObjOpMask)
	{
	case kTextObjOpDraw:
		DoPutText(text, hScale, vScale);
		DrText(text, hScale, vScale);
		break;
	default:
		break;
	}
}
