/*
	File:		packages/ROMPackages.h

	Contains:	The packages built into the ROM, and getting them installed.

				A MessagePad's ROM extension carries the applications the
				machine boots with - on this ROM: Cardfile (the Names
				application), Verbindung, FaxViewer, Tabellen, the help
				book, ListView, Profil and WorldData - as packages listed in
				its `pkgl` configuration entry.  The boot loads them:
				LoadHighROMFramesPackages for the frames parts (the
				applications), LoadHighROMDriverPackages for the drivers.

				Their install scripts are what make the soups and the
				globals the rest of the ROM's boot expects; without them
				bootRunInitScripts' PreSetupUserConfig asks for a "Names"
				soup that nobody made and the rest of its init functions
				never run.

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

#endif	/* __ROMPACKAGES_H */
