/*
	File:		recognition/LiveInker.h

	Contains:	TLiveInker: how the inker draws the pen's trail on the screen
				as the pen moves, before any view has seen the stroke.

				The ink goes into a small offscreen tile - at most 64x64
				pixels' worth of bytes (the buffer is (64 >> the screen's
				pixels-per-byte shift) * 64 bytes) - and is ORed onto the
				screen from there, so the inker never touches a port.  The
				inker adds the points it is about to join to the tile's
				extent (AddPoint, each point grown by the pen's size), and
				stops adding when the extent, aligned as the screen driver
				asks (ScreenInfo's two alignment words), would no longer fit
				the buffer; then StartLiveInk clears the tile over the
				aligned extent cut to the screen, InkLine draws each segment
				into it (InkerLine) and StopLiveInk blits it to the screen
				(BlitToScreens, srcOr) with the LCD's activity held off.

				Not in the DDK.  Reconstructed from the MP2x00 US ROM
				(0x00113840-0x00113cac).  TLiveInker is 0x3c bytes there.
*/

#ifndef __LIVEINKER_H
#define __LIVEINKER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __NEWTQD_H
#include "NewtQD.h"
#endif

class TLiveInker
{
public:
					TLiveInker();								// ROM 0x00113840 __ct__10TLiveInkerFv
					~TLiveInker();								// ROM 0x00113870 __dt__10TLiveInkerFv
	long			Init(void);									// ROM 0x001138a0 Init__10TLiveInkerFv - the tile's buffer; ==> 0 or kError_No_Memory
	void			ResetAccumulator(void);						// ROM 0x00113928 ResetAccumulator__10TLiveInkerFv - the extent emptied, the alignment read
	Boolean			AddPoint(const Point pt, const Point pen);	// ROM 0x00113980 AddPoint__10TLiveInkerFC5PointT1 - ==> whether it still fits
	Boolean			MapLCDExtent(const Rect* extent, Rect* aligned);	// ROM 0x00113aac MapLCDExtent__10TLiveInkerFPC4RectP4Rect - ==> whether it fits the buffer
	void			StartLiveInk(void);							// ROM 0x00113b9c StartLiveInk__10TLiveInkerFv
	void			InkLine(const Point from, const Point to, const Point pen);	// ROM 0x00113c3c InkLine__10TLiveInkerFC5PointN21
	void			StopLiveInk(void);							// ROM 0x00113c74 StopLiveInk__10TLiveInkerFv

	Ptr				fBuffer;			// +00
	PixelMap		fMap;				// +04 the tile (its bounds +0C, flags +14)
	Rect			fExtent;			// +20 the points so far, grown by the pen
	long			fCount;				// +28 the points added
	long			fAlignV;			// +2C the screen's row alignment (ScreenInfo +0x14)
	long			fAlignH;			// +30 its column alignment (ScreenInfo +0x18)
	long			fBufferSize;		// +34
	long			fDepthShift;		// +38 log2 of the pixels in a byte
};

#endif	/* __LIVEINKER_H */
