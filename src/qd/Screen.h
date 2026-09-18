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

	Reconstructed from the MP2x00 US ROM (0x001cc894-0x001cd4fc,
	0x00202b3c); each function cites its origin.  The ROM's screen driver
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

	VIRTUAL void		ScreenSetup(void) ENDVIRTUAL;								// ROM 0x003885bc ScreenSetup__13TScreenDriverFv
	VIRTUAL void		GetScreenInfo(ScreenInfo* info) ENDVIRTUAL;					// ROM 0x003885c8 GetScreenInfo__13TScreenDriverFP10ScreenInfo
	VIRTUAL void		PowerInit(void) ENDVIRTUAL;									// ROM 0x003885d4 PowerInit__13TScreenDriverFv
	VIRTUAL void		PowerOn(void) ENDVIRTUAL;									// ROM 0x003885e0 PowerOn__13TScreenDriverFv
	VIRTUAL void		PowerOff(void) ENDVIRTUAL;									// ROM 0x003885ec PowerOff__13TScreenDriverFv
	VIRTUAL void		Blit(PixelMap* map, Rect* src, Rect* dst, long mode) ENDVIRTUAL;	// ROM 0x003885f8 Blit__13TScreenDriverFP8PixelMapP4RectT2l
	VIRTUAL long		GetFeature(long feature) ENDVIRTUAL;						// ROM 0x00388604 GetFeature__13TScreenDriverFl
	VIRTUAL void		SetFeature(long feature, long value) ENDVIRTUAL;			// ROM 0x00388610 SetFeature__13TScreenDriverFlT1
	VIRTUAL void		AutoAdjustFeatures(void) ENDVIRTUAL;						// ROM 0x0038861c AutoAdjustFeatures__13TScreenDriverFv
	VIRTUAL void		DoubleBlit(PixelMap* map, PixelMap* map2, Rect* src, Rect* dst, long mode) ENDVIRTUAL;	// ROM 0x00388628 DoubleBlit__13TScreenDriverFP8PixelMapT1P4RectT3l
	VIRTUAL void		EnterIdleMode(void) ENDVIRTUAL;								// ROM 0x00388634 EnterIdleMode__13TScreenDriverFv
	VIRTUAL void		ExitIdleMode(void) ENDVIRTUAL;								// ROM 0x00388640 ExitIdleMode__13TScreenDriverFv
};

extern TScreenDriver*	gTheScreen;				// ROM 0x0c101a4c gTheScreen
extern Rect				gScreenDirtyRect;		// ROM 0x0c101a58 gScreenDirtyRect - what the display has not been shown yet
extern long				screenWidth;			// ROM 0x0c104c58 screenWidth
extern long				screenHeight;			// ROM 0x0c104c5c screenHeight

void	InitScreen(TScreenDriver* driver);		// ROM 0x001cc894 InitScreen__Fv (host: the driver given instead of made by name)
void	SetupScreenPixelMap(void);				// ROM 0x001ccb10 SetupScreenPixelMap__Fv
Boolean	QDStartDrawing(PixelMap* map, Rect* r);	// ROM 0x001cce0c QDStartDrawing__FP8PixelMapP4Rect
void	QDStopDrawing(PixelMap* map, Rect* r);	// ROM 0x001cce54 QDStopDrawing__FP8PixelMapP4Rect
void	StartDrawing(PixelMap* map, Rect* r);	// ROM 0x001cd2b4 StartDrawing__FP8PixelMapP4Rect
void	StopDrawing(PixelMap* map, Rect* r);	// ROM 0x001cd300 StopDrawing__FP8PixelMapP4Rect
void	UpdateHardwareScreen(void);				// ROM 0x001ccf88 UpdateHardwareScreen__Fv
void	ReleaseScreenLock(void);				// ROM 0x001cd3f4 ReleaseScreenLock__Fv
Ref		FLockScreen(RefArg rcvr, RefArg lock);	// ROM 0x001edc84 FLockScreen
void	BlitToScreens(PixelMap* map, Rect* src, Rect* dst, long mode);	// ROM 0x001ccfe4 BlitToScreens__FP8PixelMapP4RectT2l
long	GetGrafInfo(long selector, void* info);	// ROM 0x001cd424 GetGrafInfo__FlPv
void	SetGrafInfo(long selector, long value);	// ROM 0x001cc9dc SetGrafInfo__FlT1
void	SetOrientation(long orientation);		// ROM 0x00202b3c SetOrientation__Fl

// what a script can ask of the screen (ScreenNatives.cpp)
Ref		FGetLCDContrast(RefArg rcvr);			// ROM 0x00202d64 FGetLCDContrast__FRC6RefVar
Ref		FSetLCDContrast(RefArg rcvr, RefArg contrast);	// ROM 0x00202a3c FSetLCDContrast__FRC6RefVarT1
Ref		FGetOrientation(RefArg rcvr);			// ROM 0x00202ae4 FGetOrientation
Ref		FSetOrientation(RefArg rcvr, RefArg orientation);	// ROM 0x00202b0c FSetOrientation
void	RegisterScreenNatives(void);

#endif	/* __SCREEN_H */
