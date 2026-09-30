/*
	File:		host/HostObjectsFile.h

	Contains:	Where newton and newtonscript find what they boot from when
				the command line does not say: the object file built from the
				committed ROM source tree (romsrc/README.md), which the
				default build makes as <build>/romsrc-objects.bin.  The ROM
				image is not looked for: booting it is asked for with --rom.

	Not a reconstruction: a host program's own start-up.
*/

#ifndef __HOSTOBJECTSFILE_H
#define __HOSTOBJECTSFILE_H

// The object file to boot from, in this order: NEWTON_OBJECTS in the
// environment (used as it is, whether or not there is a file there); the
// file romsrc-objects.bin beside the running program, or in the directory
// above it (an installed newton, and the build directory's host/newton);
// the path the build compiled in (compiledIn).  nil when none of the
// candidates is a file - the caller then says how to build one
// (HostObjectsFileMissing) and stops.
const char*	HostDefaultObjectsFile(const char* argv0, const char* compiledIn);

// what to say when there is none: how to build the object file, and how to
// boot a ROM image instead
void		HostObjectsFileMissing(const char* program, const char* compiledIn);

#endif	/* __HOSTOBJECTSFILE_H */
