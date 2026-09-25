/*
	File:		os600/user/GestaltSources.h

	Contains:	Where the name server's kGestalt_SystemInfo answer gets the
				screen and the tablet from.  The ROM's TNameServer::Gestalt
				calls GetGrafInfo and GetTabletResolution straight across
				the image; here the name server sits below QuickDraw and
				the recognition system, so those layers hand it the two
				calls when they start (SetupScreenPixelMap, and the tablet
				buffer's own start-up) and it answers nought for what it
				has not been given.
*/

#ifndef __GESTALTSOURCES_H
#define __GESTALTSOURCES_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

// GetGrafInfo (qd/Screen.h): selector 0 the screen's pixel map, 1 its
// resolution.
extern long	(*gGestaltGrafInfo)(long selector, void* info);
// GetTabletResolution (recognition/TabletBuffer.h): Fixed samples an inch.
extern void	(*gGestaltTabletResolution)(long* x, long* y);

#endif	/* __GESTALTSOURCES_H */
