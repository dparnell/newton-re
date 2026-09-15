/*
	File:		user/Loader.h

	Contains:	TLoader, the application world named 'drvl' that InitialKSRVTask
				starts in the 'user' environment: its TheMain loads the drivers
				and starts every service of the system, ends by starting the
				'main' task (UserMain, the NewtonScript world), and kills
				itself.  It has no header in the DDK; this follows the ROM
				(TLoader's vtable is TAppWorld's with TheMain, MainConstructor,
				MainDestructor and GetSizeOf of its own, the last three thunks
				to TAppWorld's).
*/

#ifndef __LOADER_H
#define __LOADER_H

#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif

class TLoader : public TAppWorld		// 0x70 bytes (a TAppWorld)
{
public:
	virtual ULong		GetSizeOf();
	virtual long		MainConstructor();
	virtual void		MainDestructor();
	virtual void		TheMain();
};

// The 'main' task's entry: the NewtonScript world.  NOT YET RECONSTRUCTED
// (0x002e6894); on the host it runs gHostUserMain, if set, and ends.
void	UserMain();
extern void	(*gHostUserMain)();

#endif	/* __LOADER_H */
