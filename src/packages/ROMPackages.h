/*
	File:		packages/ROMPackages.h

	Contains:	The packages built into the ROM, and getting them installed.

				A MessagePad's ROM extension carries the applications the
				machine boots with - on this ROM: Cardfile (the Names
				application), Connection, FaxViewer, Formulas, the help
				book, ListView, Setup and WorldData - as packages listed in
				its `pkgl` configuration entry.  The boot loads them:
				LoadHighROMFramesPackages for the frames parts (the
				applications), LoadHighROMDriverPackages for the drivers.

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

// A ROM extension's configuration entry: where it is in the ROM and how
// big, or nil when that extension has no such entry.  The tags are four
// characters - 'pkgl is the package list.
VAddr	GetRExConfigEntry(ULong rexId, ULong tag, ULong* size);		// ROM 0x0011ef10 GetRExConfigEntry
VAddr	GetPackageList(ULong rexId);								// ROM 0x0011eeec GetPackageList

// One part installed: the install information frame the ROM builds
// (canonicalFramePartInstallInfo) handed to the NewtonScript InstallPart,
// whose answer is the cookie that removes it again.
Ref		InstallPart(RefArg partType, RefArg partFrame, RefArg packageName,
					ULong packageId, ULong partIndex, ULong size, ULong packageType);

// Every frames part of every package in the ROM extensions installed.
void	LoadHighROMFramesPackages(void);							// ROM 0x000e7040 LoadHighROMFramesPackages__Fv

// The packages a script can ask about.  On the machine that is the
// package manager's business and the answer comes off a TPMIterator;
// DEVIATION: the package manager is NOT YET RECONSTRUCTED, so the host
// keeps its own note of what it installed above and reads the same
// facts out of the package's own directory.
long	InstalledPackageCount(void);								// how many were installed
void*	InstalledPackageAt(long index, ULong* packageId);		// its bytes, and the id it was installed under
Ref		FGetPackages(RefArg rcvr);								// ROM 0x001fbaf8 FGetPackages__FRC6RefVar
void	RegisterPackageNatives(void);

#endif	/* __ROMPACKAGES_H */
