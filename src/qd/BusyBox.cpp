/*
	File:		qd/BusyBox.cpp

	Contains:	The busy box: the ROM's 32x32 "busy" picture put straight
				onto the display at the top middle of the screen, and the
				screen's own bits put back over it (qd/Screen.h).  Like the
				live ink it goes to the display only, never into the
				screen's bits.  The inker's TBusyBox shows and hides it
				(recognition/Inker.h).

				Reconstructed from the MP2x00 US ROM (0x00047ad4-0x00047bb4);
				each function cites its origin.
*/

#include "Screen.h"
#include "Ports.h"
#include "Rects.h"

extern const unsigned char	blast4bits[512];		// BusyBoxBits.cpp
extern const unsigned char	blast2bits[256];
extern const unsigned char	blastbits[128];


// ROM 0x00047ad4 QDHideBusyBox__FP8PixelMap
// The screen's bits blitted back over where the box was.
void
QDHideBusyBox(PixelMap* box)
{
	BlockLCDActivity(true);
	BlitToScreens(&qdGlobals.fScreenBits, &box->bounds, &box->bounds, srcCopy);
	BlockLCDActivity(false);
}


// ROM 0x00047b10 QDShowBusyBox__FP8PixelMap
// The box placed at the top of the screen, in the middle, its bits the
// busy picture of the map's depth (a depth with none keeps the map's own),
// and blitted onto the display.
void
QDShowBusyBox(PixelMap* box)
{
	Rect* screen = &qdGlobals.fScreenBits.bounds;
	long left = (screen->right - screen->left - 32) >> 1;
	SetRect(&box->bounds, (short) left, 0, (short) (left + 32), 32);
	switch (box->pixMapFlags & 0xff)
	{
	case 1:	box->baseAddr = (Ptr) blastbits;	break;
	case 2:	box->baseAddr = (Ptr) blast2bits;	break;
	case 4:	box->baseAddr = (Ptr) blast4bits;	break;
	}
	BlockLCDActivity(true);
	BlitToScreens(box, &box->bounds, &box->bounds, srcCopy);
	BlockLCDActivity(false);
}
