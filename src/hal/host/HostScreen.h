/*
	File:		hal/host/HostScreen.h

	Contains:	THostScreenDriver: the host's display - a TScreenDriver
				(qd/Screen.h) whose "LCD" is a buffer of one gray byte per
				pixel that the screen pixel map is blitted into, and that
				can be written out as a portable graymap (WritePGM) or
				bitmap (WritePBM) to be looked at.  The size, depth and
				resolution are the driver's to choose (the MP2100's are
				320 x 480, 4 bits deep, 100 dpi); the orientation feature
				swaps width and height.  Where the ROM's TMainDisplayDriver
				(in the ROM extension) programs the LCD controller, this
				keeps bytes.

				What is shown follows the panel's power and its backlight:
				powered off (PowerOff, the machine asleep) the display is
				blank, and lit (the backlight feature) the ink is shown a
				quarter lighter, as an electroluminescent panel washes it
				out.  The drawing is kept meanwhile (the panel's memory) and
				shown again as it was when the power comes back or the
				light goes off.  DEVIATION (hardware): the host has no panel
				to power or light.
*/

#ifndef __HAL_HOST_SCREEN_H
#define __HAL_HOST_SCREEN_H

#ifndef __SCREEN_H
#include "qd/Screen.h"
#endif

class THostScreenDriver : public TScreenDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostScreenDriver);
	THostScreenDriver*	New();
	void			Delete();

	void		ScreenSetup(void);
	void		GetScreenInfo(ScreenInfo* info);
	void		PowerInit(void);
	void		PowerOn(void);
	void		PowerOff(void);
	void		Blit(PixelMap* map, Rect* src, Rect* dst, long mode);
	long		GetFeature(long feature);
	void		SetFeature(long feature, long value);
	void		AutoAdjustFeatures(void);
	void		DoubleBlit(PixelMap* map, PixelMap* map2, Rect* src, Rect* dst, long mode);
	void		EnterIdleMode(void);
	void		ExitIdleMode(void);

	// host
	void		Configure(long width, long height, long depth, long dpi);		// before InitScreen: the display's size (portrait), depth (1, 2, 4 or 8) and resolution
	long		Width(void) const			{ return fLandscape ? fHeight : fWidth; }
	long		Height(void) const			{ return fLandscape ? fWidth : fHeight; }
	unsigned char	Gray(long x, long y) const;										// 0 white .. 255 black
	const unsigned char*	Pixels(void) const		{ return fPixels; }			// the grays, Width() per row
	Boolean		WritePGM(const char* path) const;								// the display as a binary PGM (P5)
	Boolean		WritePBM(const char* path) const;								// ... as a PBM (P4): gray from half black is black
	long		fBlits;				// how many Blits came (tests)
	Rect		fLastBlit;			// the last one's destination

private:
	Boolean		Rendered(void) const		{ return fBlanked || fBacklight != 0; }
	void		Render(long left, long top, long right, long bottom);		// the panel's grays shown as the power and the light have them
	void		SetShown(Boolean blanked, long backlight);

	long		fWidth;				// portrait
	long		fHeight;
	long		fDepth;
	long		fDPI;
	Boolean		fLandscape;
	long		fContrast;
	long		fBacklight;
	long		fOrientation;
	Boolean		fPowered;
	Boolean		fBlanked;			// powered off: nothing shown
	unsigned char*	fPixels;		// Width() x Height() grays, as shown
	unsigned char*	fPanel;			// what was drawn, while it is not shown as it is (blanked or lit)
	long		fPixelBytes;		// how big that buffer is, so ScreenSetup can keep it
};

#endif	/* __HAL_HOST_SCREEN_H */
