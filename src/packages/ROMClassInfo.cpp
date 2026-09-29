/*
	File:		packages/ROMClassInfo.cpp

	Contains:	ReadROMClassInfo (ROMClassInfo.h).  Host only: the ROM reads
				the same fields through TClassInfo's accessors
				(ImplementationName, InterfaceName, GetCapability).
*/

#include "ROMClassInfo.h"
#include "ByteOrder.h"

#include <string.h>
#include <stdio.h>


// The C string at the word at +field's self-relative offset, if it ends
// within the part.
static const char*
StringAt(const UByte* part, ULong size, ULong field)
{
	long delta = (long) (int32_t) GetBigEndianWord(part + field);
	if (delta == 0)
		return nil;
	long at = (long) field + delta;
	if (at < 0 || (ULong) at >= size)
		return nil;
	if (memchr(part + at, 0, size - at) == nil)
		return nil;
	return (const char*) part + at;
}


Boolean
ReadROMClassInfo(const void* data, ULong size, ROMClassInfoNames* names)
{
	const UByte* part = (const UByte*) data;
	if (size < 0x3c)
		return false;
	names->fImplementation = StringAt(part, size, 0x04);
	names->fInterface = StringAt(part, size, 0x08);
	const char* caps = StringAt(part, size, 0x0c);
	names->fCapabilities = caps != nil ? caps : "";
	names->fSignature[0] = 0;
	if (names->fImplementation == nil || names->fInterface == nil)
		return false;
	const char* end = (const char*) part + size;
	size_t used = 0;
	while (caps != nil && caps < end && *caps != 0)
	{
		const char* value = caps + strlen(caps) + 1;
		if (value >= end)
			break;
		int n = snprintf(names->fSignature + used, sizeof(names->fSignature) - used, "%s%s%s%s",
						 used > 0 ? "; " : "", caps, *value != 0 ? "=" : "", value);
		if (n < 0 || used + n >= sizeof(names->fSignature))
			break;
		used += n;
		caps = value + strlen(value) + 1;
	}
	return true;
}
