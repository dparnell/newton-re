/*
	File:		text/TXBinaryChars.cpp

	Contains:	A document's characters in one NewtonScript string
				(TXBinaryChars.h).

	Reconstructed from the MP2x00 US ROM (0x0023e7ec-0x0023ebb0); each
	function cites its origin.
*/

#include "TXBinaryChars.h"
#include "Objects.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "NewtErrors.h"
#include "Frames.h"

#include <string.h>


// ROM 0x0023e7ec __ct__13TXBinaryCharsFRC6RefVar
TXBinaryChars::TXBinaryChars(RefArg string)
{
	Ref text = string;
	if (text == NILREF)
		text = AllocateBinary(RSSYMstring, sizeof(UniChar));
	fString = text;
}


// ROM 0x0023e874 Count__13TXBinaryCharsCFv
long
TXBinaryChars::Count(void) const
{
	return ((unsigned long) Length(fString) >> 1) - 1;
}


// ROM 0x0023e898 Replace__13TXBinaryCharsFlT1P16TXTextDescriptor
// The string grown first (a failure answered as an error), the rest -
// the nought too - slid along, shrunk after, and the new characters
// copied in.
NewtonErr
TXBinaryChars::Replace(long at, long count, TXTextDescriptor* source)
{
	long newCount = source->fCount;
	long length = Length(fString);
	long delta = newCount * 2 - count * 2;
	if (delta > 0)
	{
		NewtonErr err = noErr;
		newton_try
		{
			SetLength(fString, length + delta);
		}
		newton_catch_all
		{
			err = GetExceptionErr(&_info.exception);
		}
		end_try;
		if (err != noErr)
			return err;
	}
	Ptr p = BinaryData(fString);
	if (delta != 0)
	{
		// (the ROM's memcpy is its memmove: the blocks overlap)
		memmove(p + newCount * 2 + at * 2, p + count * 2 + at * 2, length - at * 2 - count * 2);
		if (delta < 0)
			SetLength(fString, length + delta);
	}
	if (newCount != 0)
	{
		// (the ROM holds it locked through a TObjectPtr)
		LockRef(fString);
		TXTextDescriptor into;
		into.Set((UniChar*) BinaryData(fString) + at, newCount);
		NewtonErr err = source->CopyTo(&into, newCount);
		UnlockRef(fString);
		return err;
	}
	return noErr;
}


// ROM 0x0023e9c4 CopyTo__13TXBinaryCharsFP16TXTextDescriptorlT2
NewtonErr
TXBinaryChars::CopyTo(TXTextDescriptor* to, long at, long count)
{
	LockRef(fString);
	TXTextDescriptor from;
	from.Set((UniChar*) BinaryData(fString) + at, count);
	NewtonErr err = from.CopyTo(to, count);
	UnlockRef(fString);
	return err;
}


// ROM 0x0023ea2c AcquireCharChunk__13TXBinaryCharsFlPlT2
// The rest of the text, locked; `chunk` is not set.
UniChar*
TXBinaryChars::AcquireCharChunk(long at, long* /*chunk*/, long* count)
{
	long n = Count() - at;
	*count = n;
	if (n > 0)
	{
		LockRef(fString);
		return (UniChar*) BinaryData(fString) + at;
	}
	*count = 0;
	return nil;
}


// ROM 0x0023ea90 ReleaseCharChunk__13TXBinaryCharsFl
void
TXBinaryChars::ReleaseCharChunk(long /*chunk*/)
{
	UnlockRef(fString);
}


// ROM 0x0023ea9c GetLineChars__13TXBinaryCharsFlT1Pl
// The text itself, locked; `chunk` is not set.
UniChar*
TXBinaryChars::GetLineChars(long at, long /*count*/, long* /*chunk*/)
{
	LockRef(fString);
	return (UniChar*) BinaryData(fString) + at;
}


// ROM 0x0023ead4 GetChar__13TXBinaryCharsFl
UniChar
TXBinaryChars::GetChar(long at)
{
	return ((UniChar*) BinaryData(fString))[at];
}


// ROM 0x0023eb00 SearchChar__13TXBinaryCharsFUslT2
long
TXBinaryChars::SearchChar(UniChar c, long at, long count)
{
	return ::SearchChar(c, (UniChar*) BinaryData(fString) + at, count);
}


// ROM 0x0023eb3c SearchCharBack__13TXBinaryCharsFUslT2
long
TXBinaryChars::SearchCharBack(UniChar c, long at, long count)
{
	return ::SearchCharBack(c, (UniChar*) BinaryData(fString) + at, count);
}


// ROM 0x0023eb78 GetCtrlCharOffset__13TXBinaryCharsFlT1PUs
long
TXBinaryChars::GetCtrlCharOffset(long at, long count, UniChar* found)
{
	return ::GetCtrlCharOffset((UniChar*) BinaryData(fString) + at, count, found);
}
