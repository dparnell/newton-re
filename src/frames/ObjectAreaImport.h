/*
	File:		frames/ObjectAreaImport.h

	Contains:	The object area importer: an area of frames objects in the
				MessagePad's layout (32-bit big-endian words: a header of
				size << 8 | flags and the GC word, the class at +8, slots or
				data from +0xc, blocks rounded to 4 bytes) read into a host
				object area (ObjHeader.h's layout), every ref translated: a
				pointer into the area to its host object, a pointer elsewhere
				through the caller's translator (the ROM's objects, say),
				everything else (integers, immediates, magic pointers) as it
				is.  Symbol hashes, reals and strings become the host's; other
				binary data keeps its persistent format.

				The ROM's object area (ROMImport.h) and the frames parts of
				packages (FramesPart.h) are imported this way.

	Not a reconstruction: on the MessagePad these objects are used where
	they lie.  A host stand-in.
*/

#ifndef __OBJECTAREAIMPORT_H
#define __OBJECTAREAIMPORT_H

#ifndef __OBJHEADER_H
#include "ObjHeader.h"
#endif

// the MessagePad's layout
const ULong kARMObjHeaderSize = 8;
const ULong kARMObjBodySize = 12;
const ULong kARMWord = 4;

// an object of the area: its address in the source and its host object
struct ImportedObjectEntry
{
	ULong32		fAddress;
	ObjHeader*	fObject;
};

// a ref that points outside the area, as a host ref (nil: none)
typedef Ref (*OutsideRefTranslator)(ULong32 ref, void* refCon);

class TImportedObjectArea
{
public:
				TImportedObjectArea();
				~TImportedObjectArea();

	// The area's bytes (size of them) as they lie at address base in the
	// source's address space; kError_Bad_Parameters when they are not a
	// run of objects, kError_No_Memory when the host area cannot be made.
	// `align` is what the objects are packed to in the source bytes: four
	// for a frames part out of a version 1 package, eight for one out of a
	// version 0 package, whose gaps are filled with a pattern that is not
	// an object header.
	NewtonErr	Import(const unsigned char* bytes, ULong32 base, ULong32 size, OutsideRefTranslator outside, void* refCon, long align = 4);
	void		Dispose(void);

	ObjHeader*	ObjectAt(ULong32 address) const;		// the host object of a source address; nil when none starts there
	Ref			TranslateRef(ULong32 ref) const;		// a source ref as a host ref
	Boolean		Contains(Ref r) const					{ return (char*) r >= fArea && (char*) r < fAreaEnd; }

	char*		fArea;				// the host objects
	char*		fAreaEnd;
	ImportedObjectEntry*	fObjects;	// by source address
	long		fCount;
	ULong32		fBase;				// the source address of the first object
	ULong32		fSize;
	const unsigned char*	fBytes;	// the source bytes (they must outlive the area: a part's package is kept while it is installed)
	OutsideRefTranslator	fOutside;
	void*		fRefCon;
};

#endif	/* __OBJECTAREAIMPORT_H */
