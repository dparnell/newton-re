/*
	File:		qd/Screen.cpp

	Contains:	The screen pixel map, its driver and the drawing brackets.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Screen.h"
#include "Rects.h"
#include "Regions.h"
#include "NewtonMemory.h"
#include <string.h>

TScreenDriver*	gTheScreen = nil;			// ROM 0x0c101c28
Rect			gScreenDirtyRect;			// ROM 0x0c101c34
long			screenWidth = 0;			// ROM 0x0c101d4c
long			screenHeight = 0;			// ROM 0x0c101d50

// host: how many StartDrawings are in progress (the ROM: the screen
// semaphore group's lock count) - the display is updated when the last
// StopDrawing comes
static long		gScreenDrawingDepth = 0;


// ROM 0x0037ee40 Delete__13TScreenDriverFv (the protocol glue)
TScreenDriver*
TScreenDriver::New(const char* implementation)
{
	TScreenDriver* p = (TScreenDriver*) AllocInstanceByName("TScreenDriver", implementation);
	return p != nil ? (TScreenDriver*) p->GlueNew() : nil;
}


void
TScreenDriver::Delete()
{
	GlueDelete();
}


// ROM 0x001ceee4 SetupScreenPixelMap__Fv
// The screen pixel map from the driver's info: the row bytes the width
// rounded up to a 64-bit word's pixels times the depth, the bounds the
// size, the flags a pointer map of the depth, the resolution.  (The bits
// stay as they are: SetGrafInfo's orientation change keeps them.)
void
SetupScreenPixelMap(void)
{
	ScreenInfo info;
	gTheScreen->GetScreenInfo(&info);
	long pixelsPerWord = 64 / info.fDepth;
	long roundedWidth = (info.fWidth + pixelsPerWord - 1) & -pixelsPerWord;
	PixelMap* screen = &qdGlobals.fScreenBits;
	screen->rowBytes = (short) ((roundedWidth / 8) * info.fDepth);
	screen->grayTable = nil;
	SetRect(&screen->bounds, 0, 0, (short) info.fWidth, (short) info.fHeight);
	screen->pixMapFlags = kPixMapPtr + info.fDepth;
	screen->deviceRes.h = info.fResolutionH;
	screen->deviceRes.v = info.fResolutionV;
	// NOT YET RECONSTRUCTED: SetScreenInfo (the alert code's TAlertScreenInfo)
}


// ROM 0x001cec68 InitScreen__Fv
// The screen driver (the ROM: NewByName("TScreenDriver",
// "TMainDisplayDriver") - the host is given one) set up and powered,
// the screen pixel map made over bits enough for either orientation
// (zeroed: white), the dirty rectangle emptied.  NOT YET RECONSTRUCTED:
// the screen semaphores and the update task (InitScreenTask: the host
// updates the display from StopDrawing), the power on through the
// object manager.  Host: the default port and the screen size globals
// are set over the new screen (the ROM's boot re-opens its ports after).
void
InitScreen(TScreenDriver* driver)
{
	gTheScreen = driver;
	gTheScreen->ScreenSetup();
	gTheScreen->PowerInit();
	SetupScreenPixelMap();
	PixelMap* screen = &qdGlobals.fScreenBits;
	if (screen->baseAddr == nil)
	{
		ScreenInfo info;
		gTheScreen->GetScreenInfo(&info);
		long pixelsPerWord = 64 / info.fDepth;
		long portraitRow = (((info.fWidth + pixelsPerWord - 1) & -pixelsPerWord) / 8) * info.fDepth;
		long landscapeRow = (((info.fHeight + pixelsPerWord - 1) & -pixelsPerWord) / 8) * info.fDepth;
		long size = portraitRow * info.fHeight;
		if (landscapeRow * info.fWidth > size)
			size = landscapeRow * info.fWidth;
		screen->baseAddr = NewPtr(size);
		if (screen->baseAddr != nil)
			memset(screen->baseAddr, 0, size);
	}
	SetEmptyRect(&gScreenDirtyRect);
	gScreenDrawingDepth = 0;
	gTheScreen->PowerOn();
	screenWidth = screen->bounds.right - screen->bounds.left;
	screenHeight = screen->bounds.bottom - screen->bounds.top;
	GrafPort* port = GetCurrentPort();
	port->portBits = *screen;
	port->portRect = screen->bounds;
	InitPortRgns(port);
}


// the bits of the map are the screen's
static Boolean
IsScreen(PixelMap* map)
{
	if (map == nil)
		map = &GetCurrentPort()->portBits;
	return GetPixelMapBits(map) == GetPixelMapBits(&qdGlobals.fScreenBits);
}


// ROM 0x001cf3b8 BlitToScreens__FP8PixelMapP4RectT2l
// The rectangle of the map blitted to the display (and to a video card's
// screen, NOT YET).
void
BlitToScreens(PixelMap* map, Rect* src, Rect* dst, long mode)
{
	if (gTheScreen != nil)
		gTheScreen->Blit(map, src, dst, mode);
}


// ROM 0x001cf35c UpdateHardwareScreen__Fv
// What is dirty within the screen shown, and the dirty rectangle
// emptied.
void
UpdateHardwareScreen(void)
{
	Rect r;
	if (SectRect(&gScreenDirtyRect, &qdGlobals.fScreenBits.bounds, &r))
		BlitToScreens(&qdGlobals.fScreenBits, &r, &r, 0);
	SetEmptyRect(&gScreenDirtyRect);
}


// ROM 0x001cf1e0 QDStartDrawing__FP8PixelMapP4Rect
// QuickDraw about to draw on the map (the current port's when nil): when
// it is the screen, the screen's RAM is taken (the ROM's locking
// semaphore; the host counts).  ==> whether it is the screen.
Boolean
QDStartDrawing(PixelMap* map, Rect* /*r*/)
{
	if (!IsScreen(map))
		return false;
	gScreenDrawingDepth++;
	return true;
}


// ROM 0x001cf228 QDStopDrawing__FP8PixelMapP4Rect
// QuickDraw done with the map: the rectangle drawn (in the map's
// coordinates) added to the dirty rectangle, the display told when the
// dirty rectangle was empty (the update task's trigger; the host updates
// when the last drawing stops), the RAM let go.
void
QDStopDrawing(PixelMap* map, Rect* r)
{
	if (!IsScreen(map))
		return;
	if (map == nil)
		map = &GetCurrentPort()->portBits;
	if (r != nil)
	{
		Rect dirty = *r;
		OffsetRect(&dirty, -map->bounds.left, -map->bounds.top);
		UnionRect(&dirty, &gScreenDirtyRect, &gScreenDirtyRect);
	}
	if (--gScreenDrawingDepth <= 0)
	{
		gScreenDrawingDepth = 0;
		UpdateHardwareScreen();
	}
}


// ROM 0x001cf6b8 StartDrawing__FP8PixelMapP4Rect
// Drawing on the screen begun (the views' bracket): the screen's lock
// taken - nested brackets stack.
void
StartDrawing(PixelMap* map, Rect* /*r*/)
{
	if (!IsScreen(map))
		return;
	gScreenDrawingDepth++;
}


// ROM 0x001cf704 StopDrawing__FP8PixelMapP4Rect
// Drawing on the screen done: the rectangle added to the dirty
// rectangle; when this was the outermost bracket the display is
// updated.
void
StopDrawing(PixelMap* map, Rect* r)
{
	if (!IsScreen(map))
		return;
	if (map == nil)
		map = &GetCurrentPort()->portBits;
	if (r != nil)
	{
		Rect dirty = *r;
		OffsetRect(&dirty, -map->bounds.left, -map->bounds.top);
		UnionRect(&dirty, &gScreenDirtyRect, &gScreenDirtyRect);
	}
	if (--gScreenDrawingDepth <= 0)
	{
		gScreenDrawingDepth = 0;
		UpdateHardwareScreen();
	}
}


// ROM 0x001cf828 GetGrafInfo__FlPv
// The screen's pixel map (0), resolution (1), depth (2), the driver's
// contrast (3), orientation (4: 1 without a driver), backlight (5),
// feature 5 (6: 10 without a driver), its ScreenInfo (7).
long
GetGrafInfo(long selector, void* info)
{
	switch (selector)
	{
	case kGrafInfoScreenPixelMap:
		memcpy(info, &qdGlobals.fScreenBits, sizeof(PixelMap));
		break;
	case kGrafInfoResolution:
		memcpy(info, &qdGlobals.fScreenBits.deviceRes, sizeof(Point));
		break;
	case kGrafInfoDepth:
		*(long*) info = qdGlobals.fScreenBits.pixMapFlags & kPixMapDepth;
		break;
	case kGrafInfoContrast:
		*(long*) info = gTheScreen != nil ? gTheScreen->GetFeature(kScreenFeatureContrast) : 0;
		break;
	case kGrafInfoOrientation:
		*(long*) info = gTheScreen != nil ? gTheScreen->GetFeature(kScreenFeatureOrientation) : 1;
		break;
	case kGrafInfoBacklight:
		*(long*) info = gTheScreen != nil ? gTheScreen->GetFeature(kScreenFeatureBacklight) : 0;
		break;
	case 6:
		*(long*) info = gTheScreen != nil ? gTheScreen->GetFeature(5) : 10;
		break;
	case 7:
		if (gTheScreen != nil)
			gTheScreen->GetScreenInfo((ScreenInfo*) info);
		break;
	default:
		break;
	}
	return 0;
}


// ROM 0x001cedb0 SetGrafInfo__FlT1
// The contrast (3: the driver's feature 0; NOT YET: the temperature
// sampling), the orientation (4: the driver's feature 4, the screen
// pixel map made again for it and its bits cleared), or feature 5 -
// through the driver.
void
SetGrafInfo(long selector, long value)
{
	if (gTheScreen == nil)
		return;
	if (selector == kGrafInfoContrast)
		gTheScreen->SetFeature(kScreenFeatureContrast, value);
	if (selector == kGrafInfoOrientation)
	{
		gTheScreen->SetFeature(kScreenFeatureOrientation, value);
		SetupScreenPixelMap();
		PixelMap* screen = &qdGlobals.fScreenBits;
		long size = screen->rowBytes * (screen->bounds.bottom - screen->bounds.top);
		Ptr bits = GetPixelMapBits(screen);
		if (bits != nil)
			memset(bits, 0, size);
	}
	if (selector == 6)
		gTheScreen->SetFeature(5, value);
}


// ROM 0x0020040c SetOrientation__Fl
// The screen turned: the driver's orientation set, the default port's
// bits, rect and regions re-made over the new screen, the screen size
// globals set (the larger side the width for orientations 1 and 3).
// NOT YET RECONSTRUCTED: the tablet's orientation, the Newt globals'
// port, the gestalt (the screen's size is used).
void
SetOrientation(long orientation)
{
	SetGrafInfo(kGrafInfoOrientation, orientation);
	GrafPort* port = &gGrafPort;
	GetGrafInfo(kGrafInfoScreenPixelMap, &port->portBits);
	port->portRect = port->portBits.bounds;
	InitPortRgns(port);
	long width = qdGlobals.fScreenBits.bounds.right - qdGlobals.fScreenBits.bounds.left;
	long height = qdGlobals.fScreenBits.bounds.bottom - qdGlobals.fScreenBits.bounds.top;
	long larger = width > height ? width : height;
	long smaller = width > height ? height : width;
	if (orientation == 1 || orientation == 3)
	{
		screenWidth = larger;
		screenHeight = smaller;
	}
	else
	{
		screenWidth = smaller;
		screenHeight = larger;
	}
}
