/*
	File:		frames/ROMImport.h

	Contains:	The ROM object importer: the MessagePad's ROM holds the frames
				the OS is built on - 40597 objects in one area (gROMSoupData:
				the symbol table, the built-in functions, the protos and the
				magic pointer table that names them) - in the ARM's layout
				(32-bit big-endian words, 4-byte rounding).  On the MessagePad
				they are used in place; the host reads them out of a ROM image
				into a read-only object area of its own in the host layout
				(ObjHeader.h) through the object area importer
				(ObjectAreaImport.h), and fills in the constants the C++
				names (ROMConstants.h, RSSymbols.h).

	Not a reconstruction: the ROM has nothing to import.  A host stand-in
	for the ROM being mapped at address 0.  ImportROMObjects must run
	before InitObjects, which then uses the ROM's symbol table (and not
	InitROMSymbols' small symbol space).
*/

#ifndef __ROMIMPORT_H
#define __ROMIMPORT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif

// rom is the ROM as it appears at address 0 (build/MP2100D/rom.bin) or the
// AIF image it was extracted from (DebugRom/...: the 128-byte header is
// skipped).  Answers kError_Bad_Parameters for a ROM without the object
// area where this build's ROMConstants.h says it is, kError_No_Memory when
// the area cannot be made.
NewtonErr	ImportROMObjects(const void* rom, ULong romSize);

// the same from a file
NewtonErr	ImportROMObjectsFromFile(const char* path);

Boolean		ROMObjectsImported(void);
long		ROMObjectCount(void);
Ref			TranslateROMRef(ULong32 ref);		// a ROM ref as a host ref (nil: a pointer that is not a ROM object's)

// The ROM's own bytes, with ROM address 0 at the start (an AIF image's
// header already stepped over); nil when nothing has been imported.  The
// device's ROM is simply there to be read - its extension's package list
// and the packages themselves are in it - so the host keeps the image the
// objects came from rather than letting it go.  An image handed to
// ImportROMObjects must outlive the import for this to answer it.
const void*	ROMImageBase(ULong* size);

#endif	/* __ROMIMPORT_H */
