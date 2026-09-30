/*
	File:		frames/StringBytes.h

	Contains:	A string's bytes seen as the ROM sees them, for the
				built-ins that read and write a binary object byte by byte.

				DEVIATION: the host keeps a string's UniChars in its own
				byte order (the importer turns a package's and the ROM's
				big-endian ones round, docs/frames/README.md), where a
				MessagePad keeps them big-endian.  A script that copies
				bytes between a string and another binary - BinaryMunger
				from a package's header into a string, ExtractUniChar out
				of a string - means the big-endian bytes, which is what the
				other binary holds.  So while such a built-in works on a
				string, its UniChars are turned big-endian (on a
				little-endian host) and back again afterwards.  Newt's
				Cape's addFile (nwcp20r2.pkg) is the one that showed it:
				a downloaded package's name BinaryMunger'd out of its
				header into a string came out as boxes.
*/

#ifndef __STRINGBYTES_H
#define __STRINGBYTES_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif
#include "ByteOrder.h"

// the object's UniChars big-endian for as long as this lives (nothing
// unless it is a string on a little-endian host)
class TStringBytesAsROM
{
public:
	TStringBytesAsROM(RefArg obj)
		:	fObj(obj), fCount(0)
	{
		if (!HostIsBigEndian() && ISPTR((Ref) obj) && IsString(obj))
		{
			fCount = Length(obj) / 2;
			SwapUniChars(BinaryData(obj), fCount);
		}
	}
	~TStringBytesAsROM()
	{
		if (fCount != 0)
			SwapUniChars(BinaryData(fObj), Length(fObj) / 2);
	}

private:
	RefVar	fObj;
	long	fCount;
};

// a string whose bytes were just written as the ROM's (big-endian): its
// UniChars turned into the host's order
inline void
StringBytesFromROM(RefArg obj)
{
	if (!HostIsBigEndian() && ISPTR((Ref) obj) && IsString(obj))
		SwapUniChars(BinaryData(obj), Length(obj) / 2);
}

#endif	/* __STRINGBYTES_H */
