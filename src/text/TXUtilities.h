/*
	File:		text/TXUtilities.h

	Contains:	The text engine's small helpers: long rectangles and points
				(the engine lays a document out in longs, not in
				QuickDraw's shorts), clamping and array arithmetic, the pool
				of scratch regions, the clipping and invalidating it draws
				with, and the default run and ruler objects.

				`TXTempReferences` is a pool of five scratch objects: `Get`
				hands out a free one (making it the first time a slot is
				used, through the first virtual) and `Done` gives it back;
				with all five out, `Get` makes one that is not the pool's
				and `Done` frees it (the second virtual).  `TXTempRegions`
				is the pool of regions (`gTXTempRegions`, made by
				Textension's start-up).  The ROM's object is 0x2c bytes.

				`TXClipFurther` narrows the port's clip to a rectangle
				(saving the old clip) and says whether anything is left;
				`TXCalcClipRect` narrows a rectangle to what the clip
				shows; `TXInvalSectRect` marks a rectangle for redrawing
				on the root view when it shows through a region.

				`TXScrollRect` scrolls a rectangle of the port (widened by
				the scroll when asked) over QuickDraw's ScrollRect.

	Reconstructed from the MP2x00 US ROM (0x00233f04-0x00233fd0,
	0x00234134-0x00234278, 0x00234384-0x002343d0, 0x0023441c-0x00234590, 0x00234590-0x00234804);
	each function cites its origin.
*/

#ifndef __TXUTILITIES_H
#define __TXUTILITIES_H

#ifndef __TXOBJECTRANGE_H
#include "TXObjectRange.h"
#endif
#ifndef __REGIONS_H
#include "Regions.h"
#endif


// A point and a rectangle in longs: the document's own coordinates.
struct TXLongPoint
{
	long		v;					// +0x00
	long		h;					// +0x04
};

struct TXLongRect
{
	long		top;				// +0x00
	long		left;				// +0x04
	long		bottom;				// +0x08
	long		right;				// +0x0c

	void		Offset(long dh, long dv);								// ROM 0x00233f24 Offset__10TXLongRectFlT1
	Boolean		Sect(const TXLongRect& other, TXLongRect* result) const;	// ROM 0x00233f58 Sect__10TXLongRectCFRC10TXLongRectP10TXLongRect - ==> whether they meet
	Boolean		IsPointInside(const TXLongPoint& pt) const;				// ROM 0x00234784 IsPointInside__10TXLongRectCFRC11TXLongPoint - edges included
};


long	TXClipValue(long value, long min, long max);					// ROM 0x00233f04 TXClipValue__FlN21
// `delta` added to the first long of each of `count` elements `size` bytes apart.
void	TXAddToArrayElements(long delta, char* elements, long count, int size);	// ROM 0x00234238 TXAddToArrayElements__FlPcT1i
void	TXAddToLongArray(long delta, long* array, long count);			// ROM 0x00234258 TXAddToLongArray__FlPlT1


// The pen as the engine's click tracking sees it (the ROM's is a vtable
// and nothing else; its subclasses answer where the pen is).
class TXPointingDevice
{
public:
					TXPointingDevice();								// ROM 0x00234384 __ct__16TXPointingDeviceFv
	// The ROM's vtable is the four pure entries; the destructor is not in
	// it.  (Host: virtual, after them, so a subclass is deleted whole.)
	virtual Point	FirstLocation(void) = 0;						// (pure: +0x00) where the pen went down
	virtual Point	CurrentLocation(void) = 0;						// (pure: +0x04)
	virtual Boolean	IsStillDown(void) = 0;							// (pure: +0x08)
	virtual long	GetDoubleClickTime(void) = 0;					// (pure: +0x0c) in ticks
	virtual			~TXPointingDevice();							// ROM 0x002343b8 __dt__16TXPointingDeviceFv
};


const long	kTXTempReferencesCount	= 5;

struct TXTempReference
{
	void*		fRef;				// +0x00
	Boolean		fInUse;				// +0x04
};

class TXTempReferences
{
public:
					TXTempReferences();								// ROM 0x00234134 __ct__16TXTempReferencesFv

	virtual void*	CreateNewReference(void) = 0;					// (pure: +0x00)
	virtual void	FreeReference(void* ref) = 0;					// (pure: +0x04)

	void*			Get(void);										// ROM 0x00234190 Get__16TXTempReferencesFv
	void			Done(void* ref);								// ROM 0x002341f4 Done__16TXTempReferencesFPv

	TXTempReference	fRefs[kTXTempReferencesCount];	// +0x04
};

class TXTempRegions : public TXTempReferences
{
public:
	virtual void*	CreateNewReference(void);						// ROM 0x0023422c CreateNewReference__13TXTempRegionsFv - NewRgn
	virtual void	FreeReference(void* ref);						// ROM 0x00234230 FreeReference__13TXTempRegionsFPv - DisposeRgn
};

extern TXTempReferences*	gTXTempRegions;						// ROM 0x0c104d78 gTXTempRegions
extern Boolean				gTXHasColor;						// ROM 0x0c104d74 gTXHasColor


Boolean	TXClipFurther(Rect* rect, RgnHandle savedClip);			// ROM 0x00234590 TXClipFurther__FP4RectPP6Region - ==> whether anything of it shows (rect narrowed; the clip saved into savedClip and narrowed)
Boolean	TXCalcClipRect(Rect* rect);								// ROM 0x00234680 TXCalcClipRect__FP4Rect - ==> whether anything of it shows
void	TXInvalSectRect(Rect* rect, RgnHandle rgn);				// ROM 0x00234724 TXInvalSectRect__FP4RectPP6Region - rgn nil: the port's clip
// The rectangle's bits scrolled by (dh, dv) - no further than the port
// is tall or wide - over the part of the port it covers; `extend` widens
// it first by the scroll on the side it moves towards.  The strip left
// behind comes back in `update`; ==> whether there is one.
Boolean	TXScrollRect(const Rect& r, long dh, long dv, RgnHandle update, Boolean extend);	// ROM 0x0023441c TXScrollRect__FRC4RectlT2PP6RegionUc


// The documents' shared runs and rulers (TXObjectRange.h), made by
// Textension's start-up; the first of each is the default.
extern TXRegisteredObjects*	gRegisteredRuns;					// ROM 0x0c104d80 gRegisteredRuns
extern TXRegisteredObjects*	gRegisteredRulers;					// ROM 0x0c104d84 gRegisteredRulers

const unsigned long	kTXRunObjectKind	= 0x7478726e;			// 'txrn'
// A new object like the default run ('txrn) or, for anything else, like
// the default ruler.
TXAttrObject*	TXGetNewDefaultObject(unsigned long kind);		// ROM 0x002347c8 TXGetNewDefaultObject__FUl


#endif	/* __TXUTILITIES_H */
