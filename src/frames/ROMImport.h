/*
	File:		frames/ROMImport.h

	Contains:	The ROM object importer: the MessagePad's ROM holds the frames
				the OS is built on - 46538 objects in one area (gROMSoupData:
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

// rom is the ROM as it appears at address 0 (build/MP2x00US/rom.bin) or the
// AIF image it was extracted from (DebugRom/...: the 128-byte header is
// skipped).  Answers kError_Bad_Parameters for a ROM without the object
// area where this build's ROMConstants.h says it is, kError_No_Memory when
// the area cannot be made.
NewtonErr	ImportROMObjects(const void* rom, ULong romSize);

// the same from a file - which may instead be the ROM-free track's object
// file (below), told by its signature
NewtonErr	ImportROMObjectsFromFile(const char* path);

// The objects from the ROM-free track's object file instead of a ROM
// image: the area and the magic pointers built from the ROM source tree
// (tools/newton-rom/analysis/romsrc.py build -o; docs/rom-free/README.md).
// There is no image behind them, so ROMImageBase answers nil.
NewtonErr	ImportBuiltObjects(const void* data, ULong size);
NewtonErr	ImportBuiltObjectsFromFile(const char* path);

Boolean		ROMObjectsImported(void);
long		ROMObjectCount(void);
void		ROMObjectAreaBounds(char** start, char** end);	// where the imported ROM objects lie (both nil: none) - Uriah's gUriahROM
Ref			TranslateROMRef(ULong32 ref);		// a ROM ref as a host ref (nil: a pointer that is not a ROM object's)

// The ROM's own bytes, with ROM address 0 at the start (an AIF image's
// header already stepped over); nil when nothing has been imported.  The
// device's ROM is simply there to be read - its extension's package list
// and the packages themselves are in it - so the host keeps the image the
// objects came from rather than letting it go.  An image handed to
// ImportROMObjects must outlive the import for this to answer it.
const void*	ROMImageBase(ULong* size);

// The ROM's bytes at a ROM address, `length` of them: out of the image, or
// out of the blocks of ROM data an object file carries (the lexicons); nil
// when neither has them.
const void*	ROMBytesAt(ULong address, ULong length);

// The places the ROM's bytes are: the image (address 0 on), then each block
// an object file carries (the lexicons, the ROM extension).  ROMAddressOf
// says whether a pointer is into one of them, and at what ROM address -
// what a package in the ROM extension is imported at.
long		ROMRegionCount(void);
const void*	ROMRegion(long index, ULong* address, ULong* size);
Boolean		ROMAddressOf(const void* p, ULong* address, const void** regionStart);

#endif	/* __ROMIMPORT_H */
