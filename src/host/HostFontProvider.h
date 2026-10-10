/*
	File:		host/HostFontProvider.h

	Contains:	The host's fonts given to the Newton: the font provider
				(qd/HostFonts.h) made of the platform's rasteriser
				(host/HostFontRaster.h) and registered at boot, with the
				NewtonScript natives.  Host fonts are added to vars.fonts
				only when asked for - the Host preferences panel's "More
				fonts from the host", or NEWTON_HOST_FONTS:

					unset or 0	none (unless the panel's setting is kept on)
					1			the host's usual families (HostFontRasterDefaults)
					*			every family the host has
					A,B,...		those families, by name

				docs/qd/host-fonts.md.
*/

#ifndef __HOSTFONTPROVIDER_H
#define __HOSTFONTPROVIDER_H

// in the newt world, before the Host panel (which may turn them on)
void		HostInstallFonts(void);

// whether the host draws fonts; the families to add when they are turned
// on (nil: the host's usual ones)
bool		HostFontsAvailable(void);
const char*	HostFontsChosen(void);

#endif	/* __HOSTFONTPROVIDER_H */
