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


// (host) The busy picture at eight bits: the ROM has none - its pictures
// are one, two and four bits, and on another depth the box's map keeps its
// own bits, which the inker's TBusyBox leaves nil, so on the host's
// eight-bit screen the display read through nil.  DEVIATION (an extension
// for eight-bit and colour screens, docs/qd/colour.md): the four-bit
// picture widened as StretchBits widens grays (v * 17), made once.
static const unsigned char*
Blast8Bits(void)
{
	static unsigned char bits[32 * 32];
	static bool made = false;
	if (!made)
	{
		for (long i = 0; i < 512; i++)
		{
			bits[2 * i] = (unsigned char) ((blast4bits[i] >> 4) * 17);
			bits[2 * i + 1] = (unsigned char) ((blast4bits[i] & 15) * 17);
		}
		made = true;
	}
	return bits;
}


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
// busy picture of the map's depth (a depth with none keeps the map's own;
// host: eight bits has one too), and blitted onto the display.
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
	case 8:	box->baseAddr = (Ptr) Blast8Bits();	break;		// (host: above)
	}
	BlockLCDActivity(true);
	BlitToScreens(box, &box->bounds, &box->bounds, srcCopy);
	BlockLCDActivity(false);
}
