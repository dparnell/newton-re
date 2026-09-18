/*
	File:		qd/Screen.h

	Contains:	The screen: QuickDraw's screen pixel map (the QD globals'
				fScreenBits, drawn into by every port opened on the screen)
				and the TScreenDriver protocol that puts it on the display.
				InitScreen makes the driver's screen (its ScreenInfo gives
				the size, depth and resolution), allocates the bits and
				tells the alert code (SetScreenInfo, NOT YET).  Drawing on
				the screen is bracketed by StartDrawing/StopDrawing (the
				views, the animations): StopDrawing adds the rectangle drawn
				to gScreenDirtyRect and, when no drawing is in progress any
				more, UpdateHardwareScreen blits the dirty rectangle to the
				display through the driver (the ROM's screen update task
				does this every 33 ms while the LCD semaphore says so;
				QDStartDrawing/QDStopDrawing are QuickDraw's own bracket).
				GetGrafInfo answers the screen's pixel map, resolution,
				depth and the driver's features; SetOrientation turns the
				screen (NOT YET: the tablet, the gestalt - the port is
				re-made over the screen).

	Reconstructed from the MP2100 D ROM (0x001cec68-0x001cf900,
	0x0020040c); each function cites its origin.  The ROM's screen driver
	(TMainDisplayDriver, in the ROM extension) drives the LCD; the host's
	is hal/host/Screen.h.
*/

#ifndef __SCREEN_H
#define __SCREEN_H

#ifndef __PORTS_H
#include "Ports.h"
#endif

#ifndef __FRAMES_H
#include "Frames.h"
#endif
#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

// what a screen driver says of its display (0x1c bytes, as
// SetupScreenPixelMap 0x001ceee4 reads it)
struct ScreenInfo
{
	long		fHeight;			// +0x00  pixels
	long		fWidth;				// +0x04
	long		fDepth;				// +0x08  bits per pixel
	long		fReserved;			// +0x0c
	short		fReserved2;			// +0x10
	short		fResolutionH;		// +0x12  dots per inch
	short		fResolutionV;		// +0x14
	short		fReserved3;			// +0x16
	long		fReserved4;			// +0x18
};

// the driver's features (GetFeature/SetFeature)
enum
{
	kScreenFeatureContrast		= 0,
	kScreenFeatureBacklight		= 2,
	kScreenFeatureOrientation	= 4
};

// GetGrafInfo's selectors
enum
{
	kGrafInfoScreenPixelMap		= 0,	// the PixelMap (0x1c bytes)
	kGrafInfoResolution			= 1,	// the Point of dots per inch
	kGrafInfoDepth				= 2,
	kGrafInfoContrast			= 3,
	kGrafInfoOrientation		= 4,
	kGrafInfoBacklight			= 5
};

PROTOCOL TScreenDriver : public TProtocol
{
public:
	static TScreenDriver*	New(const char* implementation);
	void			Delete();

	VIRTUAL void		ScreenSetup(void) ENDVIRTUAL;								// ROM 0x0037ee5c ScreenSetup__13TScreenDriverFv
	VIRTUAL void		GetScreenInfo(ScreenInfo* info) ENDVIRTUAL;					// ROM 0x0037ee68 GetScreenInfo__13TScreenDriverFP10ScreenInfo
	VIRTUAL void		PowerInit(void) ENDVIRTUAL;									// ROM 0x0037ee74 PowerInit__13TScreenDriverFv
	VIRTUAL void		PowerOn(void) ENDVIRTUAL;									// ROM 0x0037ee80 PowerOn__13TScreenDriverFv
	VIRTUAL void		PowerOff(void) ENDVIRTUAL;									// ROM 0x0037ee8c PowerOff__13TScreenDriverFv
	VIRTUAL void		Blit(PixelMap* map, Rect* src, Rect* dst, long mode) ENDVIRTUAL;	// ROM 0x0037ee98 Blit__13TScreenDriverFP8PixelMapP4RectT2l
	VIRTUAL long		GetFeature(long feature) ENDVIRTUAL;						// ROM 0x0037eea4 GetFeature__13TScreenDriverFl
	VIRTUAL void		SetFeature(long feature, long value) ENDVIRTUAL;			// ROM 0x0037eeb0 SetFeature__13TScreenDriverFlT1
	VIRTUAL void		AutoAdjustFeatures(void) ENDVIRTUAL;						// ROM 0x0037eebc AutoAdjustFeatures__13TScreenDriverFv
	VIRTUAL void		DoubleBlit(PixelMap* map, PixelMap* map2, Rect* src, Rect* dst, long mode) ENDVIRTUAL;	// ROM 0x0037eec8 DoubleBlit__13TScreenDriverFP8PixelMapT1P4RectT3l
	VIRTUAL void		EnterIdleMode(void) ENDVIRTUAL;								// ROM 0x0037eed4 EnterIdleMode__13TScreenDriverFv
	VIRTUAL void		ExitIdleMode(void) ENDVIRTUAL;								// ROM 0x0037eee0 ExitIdleMode__13TScreenDriverFv
};

extern TScreenDriver*	gTheScreen;				// ROM 0x0c101c28 gTheScreen
extern Rect				gScreenDirtyRect;		// ROM 0x0c101c34 gScreenDirtyRect - what the display has not been shown yet
extern long				screenWidth;			// ROM 0x0c101d4c screenWidth
extern long				screenHeight;			// ROM 0x0c101d50 screenHeight

void	InitScreen(TScreenDriver* driver);		// ROM 0x001cec68 InitScreen__Fv (host: the driver given instead of made by name)
void	SetupScreenPixelMap(void);				// ROM 0x001ceee4 SetupScreenPixelMap__Fv
Boolean	QDStartDrawing(PixelMap* map, Rect* r);	// ROM 0x001cf1e0 QDStartDrawing__FP8PixelMapP4Rect
void	QDStopDrawing(PixelMap* map, Rect* r);	// ROM 0x001cf228 QDStopDrawing__FP8PixelMapP4Rect
void	StartDrawing(PixelMap* map, Rect* r);	// ROM 0x001cf6b8 StartDrawing__FP8PixelMapP4Rect
void	StopDrawing(PixelMap* map, Rect* r);	// ROM 0x001cf704 StopDrawing__FP8PixelMapP4Rect
void	UpdateHardwareScreen(void);				// ROM 0x001cf35c UpdateHardwareScreen__Fv
void	BlitToScreens(PixelMap* map, Rect* src, Rect* dst, long mode);	// ROM 0x001cf3b8 BlitToScreens__FP8PixelMapP4RectT2l
long	GetGrafInfo(long selector, void* info);	// ROM 0x001cf828 GetGrafInfo__FlPv
void	SetGrafInfo(long selector, long value);	// ROM 0x001cedb0 SetGrafInfo__FlT1
void	SetOrientation(long orientation);		// ROM 0x0020040c SetOrientation__Fl

// what a script can ask of the screen (ScreenNatives.cpp)
Ref		FGetLCDContrast(RefArg rcvr);			// ROM 0x00200634 FGetLCDContrast__FRC6RefVar
Ref		FSetLCDContrast(RefArg rcvr, RefArg contrast);	// ROM 0x0020030c FSetLCDContrast__FRC6RefVarT1
Ref		FGetOrientation(RefArg rcvr);			// ROM 0x002003b4 FGetOrientation
Ref		FSetOrientation(RefArg rcvr, RefArg orientation);	// ROM 0x002003dc FSetOrientation
void	RegisterScreenNatives(void);

#endif	/* __SCREEN_H */
