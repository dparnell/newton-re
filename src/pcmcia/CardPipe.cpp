/*
	File:		pcmcia/CardPipe.cpp

	Contains:	TCardPipe (CardPipe.h).

	Reconstructed from the MP2x00 US ROM (0x0004fefc-0x000502f8); each
	function cites its origin.
*/

#include "CardPipe.h"
#include "CardDefines.h"
#include "NewtonExceptions.h"

extern const ExceptionName exPipeException;


// ROM 0x000500d0 __ct__9TCardPipeFPvUlUc
TCardPipe::TCardPipe(void* base, ULong size, UChar attributeMemory)
	:	fBase((UByte*) base), fSize(size), fPosition(0), fAttributeMemory(attributeMemory)
{ }


// (the ROM's destructor is CPipe's, its vtable's first slot)
TCardPipe::~TCardPipe()
{ }


// ROM 0x000502b8 ReadSeek__9TCardPipeFli
// -1: from the start; 0: from here; 1: back from the end.  ==> where it is.
long
TCardPipe::ReadSeek(long offset, int mode)
{
	if (mode == -1)
		fPosition = offset;
	else if (mode == 0)
		fPosition = fPosition + offset;
	else if (mode == 1)
		fPosition = fSize - offset;
	return fPosition;
}


// ROM 0x000502ec ReadPosition__9TCardPipeCFv
long
TCardPipe::ReadPosition(void) const
{
	return fPosition;
}


// ROM 0x000502f4 WriteSeek__9TCardPipeFli
long
TCardPipe::WriteSeek(long /*offset*/, int /*mode*/)
{
	return 0;
}


// ROM 0x0004fefc WritePosition__9TCardPipeCFv
long
TCardPipe::WritePosition(void) const
{
	return 0;
}


// ROM 0x00050130 ReadChunk__9TCardPipeFPvRlRUc
// As many of the count bytes as there are, a byte at a time (attribute
// memory's odd bytes through CardAttrMemReadByte); count comes back as
// what was read, and eof when the stretch ran out before it.  A card that
// faults - gone, or not to be read - is a pipe exception.
void
TCardPipe::ReadChunk(void* data, long& count, Boolean& eof)
{
	long wanted = count;
	UByte* to = (UByte*) data;
	eof = false;
	newton_try
	{
		UByte* common = fBase + fPosition;
		while (count > 0 && fPosition < fSize)
		{
			UByte b;
			if (fAttributeMemory == 0)
				b = *common++;
			else
				b = CardAttrMemReadByte(fBase + fPosition * 2 + 1);
			*to++ = b;
			fPosition++;
			count--;
		}
		if (count > 0 && fPosition >= fSize)
			eof = true;
	}
	newton_catch(exPermissionViolation)
	{
		Throw(exPipeException, (void*) (long) -10061, nil);
	}
	newton_catch(exBusError)
	{
		Throw(exPipeException, (void*) (long) -10059, nil);
	}
	newton_catch(exWriteProtected)
	{
		Throw(exPipeException, (void*) (long) -10065, nil);
	}
	end_try;
	count = wanted - count;
}


// ROM 0x000502b4 WriteChunk__9TCardPipeFPvlUc
void
TCardPipe::WriteChunk(const void* /*data*/, long /*count*/, Boolean /*flush*/)
{ }


// ROM 0x0004ff04 FlushRead__9TCardPipeFv
void
TCardPipe::FlushRead(void)
{
	Reset();
}


// ROM 0x0004ff0c FlushWrite__9TCardPipeFv
void
TCardPipe::FlushWrite(void)
{
	Reset();
}


// ROM 0x0004ff14 Reset__9TCardPipeFv
void
TCardPipe::Reset(void)
{
	fPosition = 0;
}


// ROM 0x0004ff20 Overflow__9TCardPipeFv
void
TCardPipe::Overflow(void)
{ }


// ROM 0x0004ff24 Underflow__9TCardPipeFlRUc
void
TCardPipe::Underflow(long /*count*/, Boolean& eof)
{
	eof = true;
}
