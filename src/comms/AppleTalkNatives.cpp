/*
	File:		comms/AppleTalkNatives.cpp

	Contains:	The AppleTalk natives (comms/AppleTalkNatives.h): GetNames
				and ExtractNameFromNetAddress under it.  An NBP address is
				"name:type@zone"; its name is what comes before the colon.
*/

#include "AppleTalkNatives.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "Unicode.h"


// ROM 0x000669d4 ExtractNameFromNetAddress__FRC6RefVar
// An NBP address's name: a copy of the string cut at its first colon.
Ref
ExtractNameFromNetAddress(RefArg address)
{
	RefVar copy(Clone(address));
	LockRef(copy);
	UniChar* str = (UniChar*) BinaryData(copy);
	long i = 0;
	while (str[i] != (UniChar) ':' && str[i] != 0)
		i++;
	str[i] = 0;
	Ref name = MakeString(str);
	UnlockRef(copy);
	return name;
}


// ROM 0x00066b38 FGetNames__FRC6RefVarT1
// GetNames(address or array of them): the name of each, as a string or an
// array of them; nil for anything else.
static Ref
FGetNames(RefArg rcvr, RefArg addresses)
{
	RefVar result(NILREF);
	if (IsString(addresses))
		result = ExtractNameFromNetAddress(addresses);
	else if (IsArray(addresses))
	{
		long count = Length(addresses);
		result = MakeArray(count);
		for (long i = 0; i < count; i++)
		{
			RefVar address(GetArraySlotRef(addresses, i));
			RefVar name(ExtractNameFromNetAddress(address));
			SetArraySlotRef(result, i, name);
		}
	}
	return result;
}


void
RegisterAppleTalkNatives(void)
{
	RegisterNativeFunction("FGetNames__FRC6RefVarT1", (void*) FGetNames, 1);
}
