/*
	File:		frames/FramesPart.h

	Contains:	Frames parts: a package part of kind kFrames is an object
				area in the MessagePad's layout whose first object is an
				array holding the part's top-level frame (NTK's partFrame:
				{app, text, icon, theForm, partData, ...} for a 'form or
				'auto part, the book for a 'book part), with every pointer
				ref an address the part's objects have once the package is
				loaded.  On the MessagePad the part is used where it lies
				(FramePartToplevelFrame answers the frame); on the host it is
				imported into a host object area first (ImportFramesPart,
				through ObjectAreaImport.h), refs to the ROM's objects
				translated to the imported ROM's.  A "streamed" part is NSOF
				instead (TFramePartHandler::Expand reads it with
				TObjectReader) and needs no import.

	Reconstructed from the MP2x00 US ROM (FramePartToplevelFrame
	0x000d1744); the import is a host stand-in for the part being mapped.
*/

#ifndef __FRAMESPART_H
#define __FRAMESPART_H

#ifndef __OBJECTAREAIMPORT_H
#include "ObjectAreaImport.h"
#endif

// The part's objects (size bytes at part) imported: refBase is the address
// the part's refs take its first byte to be at (a package built into the
// ROM extension: the part's ROM address; a package as NTK writes it: the
// part's offset in the package, its refs being relative to the package's
// start).  ==> the imported area (nil when the bytes are not a run of
// objects), whose first object is the part's array.  The area lives until
// RemoveFramesPart.
TImportedObjectArea*	ImportFramesPart(const void* part, ULong size, ULong32 refBase, long align = 4);
void					RemoveFramesPart(TImportedObjectArea* area);		// refs into it are declawed
Boolean					InFramesPartArea(Ref r);							// an object of an imported part

// The top-level frame of the part whose first object (the array) is at
// part; nil when there is no such array.
Ref		FramePartToplevelFrame(void* part);

#endif	/* __FRAMESPART_H */
