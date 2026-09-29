/*
	File:		text/TXVBOChars.cpp

	Contains:	A document's characters in a large binary on a store
				(TXVBOChars.h).

	Reconstructed from the MP2x00 US ROM (0x0023ebb0-0x0023efc8); each
	function cites its origin.
*/

#include "TXVBOChars.h"
#include "Objects.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "NewtErrors.h"
#include "Frames.h"
#include "LargeBinaries.h"
#include "NewtWorld.h"

#include <string.h>

const long	kTXVBOChunkBytes	= 0x400;	// 512 characters a chunk

void	MungeLargeBinary(RefArg b, long offset, long count);	// (stores/LargeBinaries.cpp; the DDK declares it out of reach)


// ROM 0x0023ebb0 __ct__10TXVBOCharsFRC6RefVar
TXVBOChars::TXVBOChars(RefArg store)
	: TXChunkedChars(0x200)
{
	fStore = store;
}


// ROM 0x0023ec2c SetCharsVBO__10TXVBOCharsFRC6RefVar
void
TXVBOChars::SetCharsVBO(RefArg binary)
{
	fBinary = binary;
	fStore = FGetBinaryStore(RefVar(NILREF), binary);
}


// ROM 0x0023ed64 GetCharsVBO__10TXVBOCharsFv
// The binary, made on the store (empty, a 'text under a
// TLZStoreCompander) the first time it is asked for.
Ref
TXVBOChars::GetCharsVBO(void)
{
	if (ISNIL(fBinary))
	{
		RefVar compander(MakeString("TLZStoreCompander"));
		RefVar data(NILREF);
		RefVar length(MAKEINT(0));
		fBinary = FLBAllocCompressed(fStore, RSSYMtext, length, compander, data);
	}
	return fBinary;
}


// ROM 0x0023ee20 GetChunkPtr__10TXVBOCharsFlUcT2
UniChar*
TXVBOChars::GetChunkPtr(long chunk, Boolean /*forWrite*/, Boolean /*lock*/)
{
	return (UniChar*) (BinaryData(fBinary) + chunk * kTXVBOChunkBytes);
}


// ROM 0x0023ee44 AllocateChunks__10TXVBOCharsFlT1
// Room for `count` chunks made at chunk `at`.
NewtonErr
TXVBOChars::AllocateChunks(long at, long count)
{
	if (count > 1)
		BusyBoxSend(0x33);
	NewtonErr err = noErr;
	newton_try
	{
		RefVar binary(GetCharsVBO());
		MungeLargeBinary(binary, at << 10, count << 10);
	}
	newton_catch_all
	{
		err = GetExceptionErr(&_info.exception);
	}
	end_try;
	return err;
}


// ROM 0x0023eed4 RemoveChunks__10TXVBOCharsFlT1
// (a failure is not answered)
void
TXVBOChars::RemoveChunks(long at, long count)
{
	if (count > 1)
		BusyBoxSend(0x33);
	newton_try
	{
		MungeLargeBinary(fBinary, at << 10, count * -kTXVBOChunkBytes);
	}
	newton_catch_all
	{ }
	end_try;
}


// ROM 0x0023ef38 MungeChunk__10TXVBOCharsFlN21P16TXTextDescriptorT1
// A chunk that shrank has the room it gave up cleared.
NewtonErr
TXVBOChars::MungeChunk(long chunk, long at, long oldLen, TXTextDescriptor* source, long newLen)
{
	NewtonErr err = TXChunkedChars::MungeChunk(chunk, at, oldLen, source, newLen);
	if (err == noErr && newLen < oldLen)
	{
		UniChar* chars = GetChunkPtr(chunk, false, false);
		long used = fChunks->GetRangeLen(chunk);
		memset(chars + used, 0, (oldLen - newLen) * 2);
	}
	return err;
}
