/*
	File:		qd/Screen.cpp

	Contains:	The screen pixel map, its driver and the drawing brackets.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Screen.h"
#include "GestaltSources.h"
#include "Rects.h"
#include "Regions.h"
#include "NewtonMemory.h"
#include "UserSemaphore.h"
#include "UserTasks.h"
#include "NewtonTime.h"
#include "KernelGlobals.h"			// gOSIsRunning
#include <string.h>

TScreenDriver*	gTheScreen = nil;			// ROM 0x0c101c28
Rect			gScreenDirtyRect;			// ROM 0x0c101c34
long			screenWidth = 0;			// ROM 0x0c101d4c
long			screenHeight = 0;			// ROM 0x0c101d50

// The screen's semaphores (InitScreenTask): a group of three - 0 the LCD
// (taken while it is being updated, or held by an alert), 1 the trigger
// (QuickDraw drew on a clean screen: the update task is wanted), 2 the
// drawing brackets in progress - and the lists of operations on them, the
// RAM's locking semaphore (QuickDraw drawing into the screen's bits), and
// the task that shows what was drawn.
TUTask*				gScreenDriverTask = nil;			// ROM 0x0c101a60 gScreenDriverTask
TUSemaphoreGroup*	gScreenSemaphores = nil;			// ROM 0x0c101a64 gScreenSemaphores
TUSemaphoreOpList*	gScreenAcquireLCDList = nil;		// ROM 0x0c101a68 gScreenAcquireLCDList - LCD free, take it
TUSemaphoreOpList*	gScreenReleaseLCDList = nil;		// ROM 0x0c101a6c gScreenReleaseLCDList
TUSemaphoreOpList*	gScreenLockList = nil;				// ROM 0x0c101a70 gScreenLockList - a bracket more
TUSemaphoreOpList*	gScreenUnlockList = nil;			// ROM 0x0c101a74 gScreenUnlockList - a bracket fewer
TUSemaphoreOpList*	gScreenTriggerLCDList = nil;		// ROM 0x0c101a78 gScreenTriggerLCDList
TUSemaphoreOpList*	gScreenStartLCDUpdateList = nil;	// ROM 0x0c101a7c gScreenStartLCDUpdateList - triggered, LCD free, no brackets: take the LCD
TUSemaphoreOpList*	gScreenFinishLCDUpdateList = nil;	// ROM 0x0c101a80 gScreenFinishLCDUpdateList
TUSemaphoreOpList*	gScreenTestUnlockList = nil;		// ROM 0x0c101a84 gScreenTestUnlockList - the last bracket?
TULockingSemaphore*	gScreenRamSemaphore = nil;			// ROM 0x0c101a88 gScreenRamSemaphore

// DEVIATION: QuickDraw is also run with no operating system (the unit
// tests, newtonscript), where there are no semaphores to take: there the
// brackets are counted, the display updated when the last one closes and
// an alert's block kept as a count - what the semaphores and the update
// task come to with one task drawing.
static long		gScreenDrawingDepth = 0;
static long		gLCDBlocked = 0;


// ROM 0x003885a0 Delete__13TScreenDriverFv (the protocol glue)
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


// ROM 0x001ccb10 SetupScreenPixelMap__Fv
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
	gGestaltGrafInfo = GetGrafInfo;			// (host: the name server's way to the screen - GestaltSources.h)
	SetScreenInfo();
}


TAlertScreenInfo	gAlertScreenInfo;		// ROM 0x0c105ef0 gAlertScreenInfo


// ROM 0x0002e7d4 SetAlertScreenInfo__FP16TAlertScreenInfo
void
SetAlertScreenInfo(TAlertScreenInfo* info)
{
	gAlertScreenInfo = *info;
}


// ROM 0x001cd04c SetScreenInfo__Fv
// The alerts told the screen: the driver, the screen's pixel map (its
// bits when it has them) and the driver's orientation.
void
SetScreenInfo(void)
{
	TAlertScreenInfo info;
	memset(&info, 0, sizeof(info));
	info.fDriver = gTheScreen;
	if (qdGlobals.fScreenBits.baseAddr != nil)
	{
		info.fScreen.baseAddr = qdGlobals.fScreenBits.baseAddr;
		info.fScreen.rowBytes = qdGlobals.fScreenBits.rowBytes;
	}
	info.fScreen.bounds = qdGlobals.fScreenBits.bounds;
	info.fScreen.pixMapFlags = qdGlobals.fScreenBits.pixMapFlags;
	info.fScreen.deviceRes = qdGlobals.fScreenBits.deviceRes;
	info.fScreen.grayTable = qdGlobals.fScreenBits.grayTable;
	info.fFeature4 = (UChar) gTheScreen->GetFeature(4);
	SetAlertScreenInfo(&info);
}


// ROM 0x001ccf34 BlockLCDActivity__FUc
// The screen's LCD kept from being updated by anybody else while an alert
// is up (block) - taken, so that a task that would update it waits - or
// let go.  The alert itself blits through the driver.
void
BlockLCDActivity(Boolean block)
{
	if (gScreenSemaphores != nil)
	{
		gScreenSemaphores->SemOp(block ? gScreenAcquireLCDList : gScreenReleaseLCDList, kWaitOnBlock);
		return;
	}
	if (block)
		gLCDBlocked++;
	else if (gLCDBlocked > 0)
		gLCDBlocked--;
}


// ROM 0x001cc97c LCDPowerInit__FUc
// The screen driver's power set up again after a sleep (the argument, which
// says whether the machine was asleep, makes no difference).
void
LCDPowerInit(UChar /*wasAsleep*/)
{
	gTheScreen->PowerInit();
}


// ROM 0x001ccf54 LCDPowerOn__FUc
// The panel powered.
void
LCDPowerOn(UChar /*wasAsleep*/)
{
	gTheScreen->PowerOn();
}


// ROM 0x001cd288 LCDPowerOff__FUc
// The panel powered off.
void
LCDPowerOff(UChar /*toSleep*/)
{
	gTheScreen->PowerOff();
}


// ROM 0x001cd11c ScreenUpdateTask__FPvUlT2
// The screen's update task: each time it is triggered and nobody is
// drawing or holding the LCD, what is dirty is shown (with the screen's
// RAM taken), at most every 33 ms.  Once a minute the LCD contrast's
// temperature is sampled - NOT YET: the machine's ADC (TADC) is hardware
// the host has none of.
static void
ScreenUpdateTask(void* /*object*/, ULong /*size*/, TObjectId /*taskId*/)
{
	TTime nextSample;
	nextSample.time.hi = 0;
	nextSample.time.lo = 0;
	for (;;)
	{
		gScreenSemaphores->SemOp(gScreenStartLCDUpdateList, kWaitOnBlock);
		gScreenRamSemaphore->Acquire(kWaitOnBlock);
		TTime next = TimeFromNow(0x1db26);
		UpdateHardwareScreen();
		gScreenRamSemaphore->Release();
		gScreenSemaphores->SemOp(gScreenFinishLCDUpdateList, kWaitOnBlock);
		TTime now = GetGlobalTime();
		if (CompCompare(&now.time, &nextSample.time) > 0)
			nextSample = TimeFromNow(0xd2f0000);
		SleepTill(&next);
	}
}


// ROM 0x001ccbd8 InitScreenTask__Fv
// The screen's semaphores, their operation lists and the update task
// made (once), the screen powered and the task started.
static void
InitScreenTask(void)
{
	if (gScreenSemaphores != nil)
		return;
	gScreenSemaphores = new TUSemaphoreGroup;
	gScreenSemaphores->Init(3);
	gScreenRamSemaphore = new TULockingSemaphore;
	gScreenRamSemaphore->Init();
	gScreenLockList = new TUSemaphoreOpList;
	gScreenLockList->Init(1, (ULong) MAKESEMLISTITEM(2, 1));
	gScreenUnlockList = new TUSemaphoreOpList;
	gScreenUnlockList->Init(1, (ULong) MAKESEMLISTITEM(2, -1));
	gScreenTestUnlockList = new TUSemaphoreOpList;
	gScreenTestUnlockList->Init(3, (ULong) MAKESEMLISTITEM(2, -1), (ULong) MAKESEMLISTITEM(2, 0), (ULong) MAKESEMLISTITEM(2, 1));
	gScreenTriggerLCDList = new TUSemaphoreOpList;
	gScreenTriggerLCDList->Init(1, (ULong) MAKESEMLISTITEM(1, 1));
	gScreenAcquireLCDList = new TUSemaphoreOpList;
	gScreenAcquireLCDList->Init(2, (ULong) MAKESEMLISTITEM(0, 0), (ULong) MAKESEMLISTITEM(0, 1));
	gScreenReleaseLCDList = new TUSemaphoreOpList;
	gScreenReleaseLCDList->Init(1, (ULong) MAKESEMLISTITEM(0, -1));
	gScreenStartLCDUpdateList = new TUSemaphoreOpList;
	gScreenStartLCDUpdateList->Init(4, (ULong) MAKESEMLISTITEM(0, 0), (ULong) MAKESEMLISTITEM(0, 1), (ULong) MAKESEMLISTITEM(1, -1), (ULong) MAKESEMLISTITEM(2, 0));
	gScreenFinishLCDUpdateList = new TUSemaphoreOpList;
	gScreenFinishLCDUpdateList->Init(1, (ULong) MAKESEMLISTITEM(0, -1));
	gScreenDriverTask = new TUTask;
	gScreenDriverTask->Init(ScreenUpdateTask, 0x1000, 0, nil, 11, 'scrn');
	gTheScreen->PowerOn();
	gScreenDriverTask->Start();
}


// ROM 0x001cc894 InitScreen__Fv
// The screen driver (the ROM: NewByName("TScreenDriver",
// "TMainDisplayDriver") - the host is given one) set up and powered,
// the screen pixel map made over bits enough for either orientation
// (zeroed: white), the dirty rectangle emptied, the screen semaphores and
// the update task made (InitScreenTask - with the operating system
// running: see gScreenDrawingDepth).  Host: the default port and the
// screen size globals are set over the new screen (the ROM's boot
// re-opens its ports after).
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
		SetScreenInfo();		// (host: again - the bits are made after the pixel map here)
	}
	SetEmptyRect(&gScreenDirtyRect);
	gScreenDrawingDepth = 0;
	if (gOSIsRunning && gCurrentTask != nil)
		InitScreenTask();
	else
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


// ROM 0x001ccfe4 BlitToScreens__FP8PixelMapP4RectT2l
// The rectangle of the map blitted to the display (and to a video card's
// screen, NOT YET).
void
BlitToScreens(PixelMap* map, Rect* src, Rect* dst, long mode)
{
	if (gTheScreen != nil)
		gTheScreen->Blit(map, src, dst, mode);
}


// ROM 0x001ccf88 UpdateHardwareScreen__Fv
// What is dirty within the screen shown, and the dirty rectangle
// emptied.
void
UpdateHardwareScreen(void)
{
	Rect r;
	if (gScreenSemaphores == nil && gLCDBlocked > 0)
		return;					// (no semaphores: kept dirty until the LCD is let go)
	if (SectRect(&gScreenDirtyRect, &qdGlobals.fScreenBits.bounds, &r))
		BlitToScreens(&qdGlobals.fScreenBits, &r, &r, 0);
	SetEmptyRect(&gScreenDirtyRect);
}


// ROM 0x001cce0c QDStartDrawing__FP8PixelMapP4Rect
// QuickDraw about to draw on the map (the current port's when nil): when
// it is the screen, the screen's RAM is taken.  ==> whether it is the
// screen.
Boolean
QDStartDrawing(PixelMap* map, Rect* /*r*/)
{
	if (!IsScreen(map))
		return false;
	if (gScreenSemaphores != nil)
		gScreenRamSemaphore->Acquire(kWaitOnBlock);
	else
		gScreenDrawingDepth++;
	return true;
}


// ROM 0x001cce54 QDStopDrawing__FP8PixelMapP4Rect
// QuickDraw done with the map: the rectangle drawn (in the map's
// coordinates) added to the dirty rectangle, the update task triggered
// when the dirty rectangle was empty, the RAM let go.
void
QDStopDrawing(PixelMap* map, Rect* r)
{
	if (!IsScreen(map))
		return;
	if (map == nil)
		map = &GetCurrentPort()->portBits;
	Rect dirty;
	if (r != nil)
	{
		dirty = *r;
		OffsetRect(&dirty, -map->bounds.left, -map->bounds.top);
	}
	Boolean wasClean = EmptyRect(&gScreenDirtyRect);
	if (r != nil)
		UnionRect(&dirty, &gScreenDirtyRect, &gScreenDirtyRect);
	if (gScreenSemaphores != nil)
	{
		if (wasClean)
			gScreenSemaphores->SemOp(gScreenTriggerLCDList, kWaitOnBlock);
		gScreenRamSemaphore->Release();
		return;
	}
	if (--gScreenDrawingDepth <= 0)
	{
		gScreenDrawingDepth = 0;
		UpdateHardwareScreen();
	}
}


// ROM 0x001cd3f4 ReleaseScreenLock__Fv
// The screen lock dropped however deep it went: the ROM unlocks its
// semaphore group over and over until there is nothing left to unlock.
// The event dispatch does this at the end of every event
// (TNewtWorld::AEDispatch), which is what lets go of the lock the world's
// MainConstructor takes and never gives back - and of any bracket a Throw
// unwound past.
//
// What was drawn meanwhile is shown by the update task once the locks are
// gone.  (With no semaphores the drawing depth goes to zero and what is
// dirty is shown now.)
void
ReleaseScreenLock(void)
{
	if (gScreenSemaphores != nil)
	{
		while (gScreenSemaphores->SemOp(gScreenUnlockList, kNoWaitOnBlock) == noErr)
			;
		return;
	}
	if (gScreenDrawingDepth <= 0)
		return;
	gScreenDrawingDepth = 0;
	UpdateHardwareScreen();
}


// ROM 0x001cd2b4 StartDrawing__FP8PixelMapP4Rect
// Drawing on the screen begun (the views' bracket): the screen's lock
// taken - nested brackets stack.
void
StartDrawing(PixelMap* map, Rect* /*r*/)
{
	if (!IsScreen(map))
		return;
	if (gScreenSemaphores != nil)
		gScreenSemaphores->SemOp(gScreenLockList, kNoWaitOnBlock);
	else
		gScreenDrawingDepth++;
}


// ROM 0x001cd300 StopDrawing__FP8PixelMapP4Rect
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
	if (gScreenSemaphores != nil)
	{
		// the last bracket: the screen shown now, with the LCD and the RAM
		// taken, before the bracket goes
		if (gScreenSemaphores->SemOp(gScreenTestUnlockList, kNoWaitOnBlock) == noErr)
		{
			gScreenSemaphores->SemOp(gScreenAcquireLCDList, kWaitOnBlock);
			gScreenRamSemaphore->Acquire(kWaitOnBlock);
			UpdateHardwareScreen();
			gScreenRamSemaphore->Release();
			gScreenSemaphores->SemOp(gScreenReleaseLCDList, kWaitOnBlock);
		}
		gScreenSemaphores->SemOp(gScreenUnlockList, kNoWaitOnBlock);
		return;
	}
	if (--gScreenDrawingDepth <= 0)
	{
		gScreenDrawingDepth = 0;
		UpdateHardwareScreen();
	}
}


// ROM 0x001cd424 GetGrafInfo__FlPv
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


// ROM 0x001cc9dc SetGrafInfo__FlT1
// The contrast (3: the driver's feature 0; NOT YET: the temperature
// sampling), the orientation (4: the driver's feature 4, the screen
// pixel map made again for it and its bits cleared), or the backlight
// (5: the driver's feature 2) - through the driver.  Those are the only
// three it sets; the depth, resolution and the rest are the screen's to
// say and GetGrafInfo's to report.
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
	if (selector == kGrafInfoBacklight)
		gTheScreen->SetFeature(kScreenFeatureBacklight, value);
}


// ROM 0x00202b3c SetOrientation__Fl
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
