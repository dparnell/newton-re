/*
	File:		packages/ROMClassInfo.h

	Contains:	A protocol part's class info read as it lies in a package.

				A package's protocol part (kind kProtocol: a driver, a comms
				tool, a card handler - NTK's native code) starts with the
				TClassInfo the package manager registers: fifteen big-endian
				words, the second to fourth self-relative offsets to the
				implementation's name, the interface's name and the
				capability list (name, value pairs of C strings ended by an
				empty name), the rest offsets and ARM branches into the
				code (tools/newton-rom/analysis/classinfo.py decodes all of
				it; --package reads a package file).

				DEVIATION: the host's TClassInfo (protocols/Protocols.h) is
				function pointers, and a part's code is ARM, so a part's
				class info is not registered; this reads what it would have
				been registered as, so that the host can say so and a host
				replacement can be registered under the same names.
*/

#ifndef __ROMCLASSINFO_H
#define __ROMCLASSINFO_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

struct ROMClassInfoNames
{
	const char*	fImplementation;	// e.g. "TLanternCardHandler"
	const char*	fInterface;			// e.g. "TCardHandler"
	const char*	fCapabilities;		// the first capability's name ("" for none)
	char		fSignature[128];	// every capability as "name=value; ..."
};

// ==> whether the size bytes at part hold a class info whose names lie
// within them.
Boolean	ReadROMClassInfo(const void* part, ULong size, ROMClassInfoNames* names);

#endif	/* __ROMCLASSINFO_H */
