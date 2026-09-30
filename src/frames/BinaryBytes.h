/*
	File:		frames/BinaryBytes.h

	Contains:	A binary object's bytes seen as a MessagePad sees them, for
				the built-ins that read and write a binary byte by byte.

				DEVIATION: the host keeps strings, reals and the shapes'
				halfword structures in its own byte order (HostOrder.h).  A
				script that copies bytes between such an object and another
				binary - BinaryMunger from a package's header into a
				string, ExtractUniChar out of a string - means the
				MessagePad's big-endian bytes, which is what the other
				binary holds.  So while such a built-in works on one, its
				data is turned into the MessagePad's order and back again
				afterwards.  Newt's Cape's addFile (nwcp20r2.pkg) is the one
				that showed it: a downloaded package's name BinaryMunger'd
				out of its header into a string came out as boxes.
*/

#ifndef __BINARYBYTES_H
#define __BINARYBYTES_H

#ifndef __HOSTORDER_H
#include "HostOrder.h"
#endif

// the object's data in the MessagePad's order for as long as this lives
// (nothing unless it is one the host keeps in its own)
class TBinaryBytesAsROM
{
public:
	TBinaryBytesAsROM(RefArg obj)
		:	fObj(obj), fKind(HostOrderOf(obj))
	{
		if (fKind != kROMOrder)
			SwapHostOrder(fKind, BinaryData(obj), Length(obj));
	}
	~TBinaryBytesAsROM()
	{
		if (fKind != kROMOrder)
			SwapHostOrder(fKind, BinaryData(fObj), Length(fObj));
	}

private:
	RefVar		fObj;
	EHostOrder	fKind;
};

// an object whose bytes were just written as a MessagePad's: its data
// turned into the host's order (nothing unless it is one kept so)
inline void
BinaryBytesFromROM(RefArg obj)
{
	EHostOrder kind = HostOrderOf(obj);
	if (kind != kROMOrder)
		SwapHostOrder(kind, BinaryData(obj), Length(obj));
}

#endif	/* __BINARYBYTES_H */
