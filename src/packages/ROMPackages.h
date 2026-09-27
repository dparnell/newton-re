/*
	File:		packages/ROMPackages.h

	Contains:	The packages built into the ROM, getting them installed, and
				the package natives a script uses.

				A MessagePad's ROM extension carries the applications the
				machine boots with - on this ROM: Cardfile (the Names
				application), Connection, FaxViewer, Formulas, the help
				book, ListView, the screen drivers, Setup and WorldData - as
				packages listed in its `pkgl` configuration entry.  The
				boot loads them (LoadHighROMFramesPackages, from
				TNewtWorld::PreMain): each is sent to the package manager
				as a package in memory, where it lies, and the part
				handlers install its parts - the applications through the
				frames part handlers (FramePartHandler.h) and the global
				InstallPart, WorldData's cities as a package store.

				Their install scripts are what make the globals and the
				registrations the rest of the boot expects.  They do not,
				though, make the soups: PreMain loads them after
				InitToolbox has already run the init scripts, and an
				application makes its soup when it is first used - which is
				why bootRunInitScripts' PreSetupUserConfig asks for a
				"Names" soup that nobody has made (docs/newt/README.md).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __ROMPACKAGES_H
#define __ROMPACKAGES_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif

// A ROM extension's configuration entry: where it is and how big, or 0
// when that extension has no such entry.  The tags are four characters -
// 'pkgl is the package list, 'fexp the frames export table.
// DEVIATION: the address is where the host keeps the entry, in the ROM
// image it read in (ROMPackages.cpp).
VAddr	GetRExConfigEntry(ULong rexId, ULong tag, ULong* size);		// ROM 0x0011ef10 GetRExConfigEntry
VAddr	GetPackageList(ULong rexId);								// ROM 0x0011eeec GetPackageList

// Every package of every ROM extension's package list loaded.
void	LoadHighROMFramesPackages(void);							// ROM 0x000e7040 LoadHighROMFramesPackages__Fv

// GetPackages(): a frame for every package installed.
Ref		FGetPackages(RefArg rcvr);								// ROM 0x001fbaf8 FGetPackages__FRC6RefVar
void	RegisterPackageNatives(void);

#endif	/* __ROMPACKAGES_H */
