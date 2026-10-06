/*
	File:		host/HostWordEngines.h

	Contains:	The host's handwriting engines (recognition/WordEngines.h)
				offered in the Newton's own Handwriting Recognition slip:
				`HostWordEngines()` for NewtonScript, and HostWordEngines.ns,
				which registers a copy of the slip with a button for each
				engine under the ROM's two letter-set buttons
				(docs/recognition/engines.md).
*/

#ifndef __HOSTWORDENGINES_H
#define __HOSTWORDENGINES_H

// in the newt world, once its globals are built: the native and the slip
void	HostInstallWordEngines(void);

#endif	/* __HOSTWORDENGINES_H */
