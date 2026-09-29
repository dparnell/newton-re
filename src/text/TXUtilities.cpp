/*
	File:		text/TXUtilities.cpp

	Contains:	The text engine's small helpers - see TXUtilities.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXUtilities.h"
#include "Rects.h"
#include "Ports.h"
#include "RootView.h"


TXTempReferences*	gTXTempRegions = nil;			// ROM 0x0c104d78 gTXTempRegions
Boolean				gTXHasColor = false;			// ROM 0x0c104d74 gTXHasColor
TXRegisteredObjects*	gRegisteredRuns = nil;		// ROM 0x0c104d80 gRegisteredRuns
TXRegisteredObjects*	gRegisteredRulers = nil;	// ROM 0x0c104d84 gRegisteredRulers


// ROM 0x00233f04 TXClipValue__FlN21
long
TXClipValue(long value, long min, long max)
{
	if (value <= min)
		return min;
	if (max <= value)
		value = max;
	return value;
}


// ROM 0x00233f24 Offset__10TXLongRectFlT1
void
TXLongRect::Offset(long dh, long dv)
{
	left += dh;
	right += dh;
	top += dv;
	bottom += dv;
}


// ROM 0x00233f58 Sect__10TXLongRectCFRC10TXLongRectP10TXLongRect
// The overlap written into `result` whether or not there is one.
Boolean
TXLongRect::Sect(const TXLongRect& other, TXLongRect* result) const
{
	long t = top > other.top ? top : other.top;
	result->top = t;
	long l = left > other.left ? left : other.left;
	result->left = l;
	long b = other.bottom <= bottom ? other.bottom : bottom;
	result->bottom = b;
	long r = other.right <= right ? other.right : right;
	result->right = r;
	return t < b && l < r;
}


// ROM 0x00234784 IsPointInside__10TXLongRectCFRC11TXLongPoint
// ROM QUIRK kept: both edges are inside (a QuickDraw rectangle leaves its
// bottom and right edges out).
Boolean
TXLongRect::IsPointInside(const TXLongPoint& pt) const
{
	return pt.h >= left && pt.h <= right && pt.v >= top && pt.v <= bottom;
}


// ROM 0x00234238 TXAddToArrayElements__FlPcT1i
void
TXAddToArrayElements(long delta, char* elements, long count, int size)
{
	for (long i = count - 1; i >= 0; i--)
	{
		*(long*) elements += delta;
		elements += size;
	}
}


// ROM 0x00234258 TXAddToLongArray__FlPlT1
void
TXAddToLongArray(long delta, long* array, long count)
{
	for (long i = count - 1; i >= 0; i--)
		*array++ += delta;
}


/*------------------------------------------------------------------------------
	T h e   p o i n t i n g   d e v i c e
------------------------------------------------------------------------------*/

// ROM 0x00234384 __ct__16TXPointingDeviceFv
TXPointingDevice::TXPointingDevice()
{ }


// ROM 0x002343b8 __dt__16TXPointingDeviceFv
TXPointingDevice::~TXPointingDevice()
{ }


/*------------------------------------------------------------------------------
	T h e   s c r a t c h   r e f e r e n c e s
------------------------------------------------------------------------------*/

// ROM 0x00234134 __ct__16TXTempReferencesFv
TXTempReferences::TXTempReferences()
{
	for (long i = 0; i < kTXTempReferencesCount; i++)
	{
		fRefs[i].fRef = nil;
		fRefs[i].fInUse = false;
	}
}


// ROM 0x00234190 Get__16TXTempReferencesFv
// A free slot's object, made the first time the slot is used; with every
// slot in use, an object that is not the pool's.
void*
TXTempReferences::Get(void)
{
	for (long i = 0; i < kTXTempReferencesCount; i++)
	{
		TXTempReference* slot = &fRefs[i];
		if (!slot->fInUse)
		{
			slot->fInUse = true;
			if (slot->fRef == nil)
				slot->fRef = CreateNewReference();
			return slot->fRef;
		}
	}
	return CreateNewReference();
}


// ROM 0x002341f4 Done__16TXTempReferencesFPv
void
TXTempReferences::Done(void* ref)
{
	for (long i = 0; i < kTXTempReferencesCount; i++)
	{
		if (fRefs[i].fRef == ref)
		{
			fRefs[i].fInUse = false;
			return;
		}
	}
	FreeReference(ref);
}


// ROM 0x0023422c CreateNewReference__13TXTempRegionsFv
void*
TXTempRegions::CreateNewReference(void)
{
	return NewRgn();
}


// ROM 0x00234230 FreeReference__13TXTempRegionsFPv
void
TXTempRegions::FreeReference(void* ref)
{
	DisposeRgn((RgnHandle) ref);
}


/*------------------------------------------------------------------------------
	C l i p p i n g
------------------------------------------------------------------------------*/

// ROM 0x00234590 TXClipFurther__FP4RectPP6Region
Boolean
TXClipFurther(Rect* rect, RgnHandle savedClip)
{
	GrafPort* port;
	GetPort(&port);
	RgnHandle clip = port->clipRgn;
	if ((*clip)->rgnSize == kRectRgnSize)
	{
		if (!SectRect(rect, &(*clip)->rgnBBox, rect))
			return false;
		GetClip(savedClip);
		ClipRect(rect);
		return true;
	}
	RgnHandle temp = (RgnHandle) gTXTempRegions->Get();
	GetClip(savedClip);
	RectRgn(temp, rect);
	SectRgn(temp, clip, clip);
	gTXTempRegions->Done(temp);
	if (EmptyRgn(clip))
	{
		SetClip(savedClip);
		return false;
	}
	*rect = (*clip)->rgnBBox;
	return true;
}


// ROM 0x00234680 TXCalcClipRect__FP4Rect
Boolean
TXCalcClipRect(Rect* rect)
{
	GrafPort* port;
	GetPort(&port);
	RgnHandle clip = port->clipRgn;
	if ((*clip)->rgnSize == kRectRgnSize)
		return SectRect(&(*clip)->rgnBBox, rect, rect);
	RgnHandle temp = (RgnHandle) gTXTempRegions->Get();
	RectRgn(temp, rect);
	SectRgn(clip, temp, temp);
	Boolean shows = SectRect(&(*temp)->rgnBBox, rect, rect);
	gTXTempRegions->Done(temp);
	return shows;
}


// ROM 0x00234724 TXInvalSectRect__FP4RectPP6Region
void
TXInvalSectRect(Rect* rect, RgnHandle rgn)
{
	if (rgn == nil)
	{
		GrafPort* port;
		GetPort(&port);
		rgn = port->clipRgn;
	}
	if (RectInRgn(rect, rgn))
		gRootView->Dirty(rect);
}


// ROM 0x002347c8 TXGetNewDefaultObject__FUl
TXAttrObject*
TXGetNewDefaultObject(unsigned long kind)
{
	TXRegisteredObjects* registered = kind == kTXRunObjectKind ? gRegisteredRuns : gRegisteredRulers;
	return registered->GetIndObject(0)->CreateNew();
}
