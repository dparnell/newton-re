/*
	File:		host/android/EinkDirect.h

	Contains:	Region updates sent straight to the Supernote Manta's / Nomad's
				e-ink driver (/dev/ebc), past Android's compositor, which is
				what makes ink on those panels lag (docs/host-android.md).

				The driver interface and the table of panel layouts are those
				of MPdairy's fasteink library (MIT, github.com/mpdairy/fasteink,
				docs/DEVICES.md there), written here from its description.  A
				panel is used only when the driver's own answer matches a
				layout seen on a real device; anything else is not touched.
*/

#ifndef EINKDIRECT_H
#define EINKDIRECT_H

// opens the driver; false when there is none, it is not allowed, or it is a panel not known
bool	EinkDirectOpen(void);
void	EinkDirectClose(void);
int		EinkDirectWidth(void);		// the panel's size, in the (portrait) frame the window uses
int		EinkDirectHeight(void);

// show [left,right) x [top,bottom) of the panel: gray(x, y) is 0 (black) to 255 (white);
// 0 sent, 1 busy (keep the damage, try again), -1 failed (use the window)
int		EinkDirectPresent(int left, int top, int right, int bottom, unsigned char (*gray)(int x, int y, void* ctx), void* ctx);

#endif
