/*
	File:		comms/fax/T4FaxLine.cpp

	Contains:	TT4FaxLine, the MH decoder of a received page's lines
				(T4FaxLine.h).

	Reconstructed from the MP2x00 US ROM (0x00204698-0x00204e34); each
	function cites its origin.
*/

#include "T4FaxLine.h"
#include "NewtonExceptions.h"
#include "CommErrors.h"

// ROM 0x003712f0 exFaxBufOverrunException
// (the ring running dry - named as evt.ex itself, so catching it catches
// every exception under evt.ex)
static char exFaxBufOverrunException[] = "evt.ex";


// ROM 0x00204698 __ct__10TT4FaxLineFv
TT4FaxLine::TT4FaxLine()
{
	fBufEnd = nil;
	fWritePtr = nil;
	fOutEnd = nil;
	fOut = nil;
	fReadPtr = nil;
	fBufStart = nil;
	fOutBitsFree = 0;
	fBitsLeft = 0;
	fBufSize = 0;
}


// ROM 0x002046f4 __dt__10TT4FaxLineFv
TT4FaxLine::~TT4FaxLine()
{ }


// ROM 0x00204a44 Init__10TT4FaxLineFPUci
void
TT4FaxLine::Init(UChar* buffer, int size)
{
	fBufStart = buffer;
	fBufSize = size;
	fBufEnd = buffer + size;
	Reset();
}


// ROM 0x00204a54 Reset__10TT4FaxLineFv
void
TT4FaxLine::Reset()
{
	fWritePtr = fBufStart;
	fReadPtr = fBufStart;
	fBitsLeft = 0;
	fWrapped = false;
}


// ROM 0x00204a70 AppendTo__10TT4FaxLineFPPUcPiT2
// As much of the data as the ring has room for, the third and later of a
// run of nought bytes (the fill between lines) left out; the data and its
// count moved past what was taken.  false when the ring is full.
Boolean
TT4FaxLine::AppendTo(UChar** data, int* count, int* appended)
{
	UChar* p = *data;
	int n = *count;
	int taken = 0;
	ULong noughts = 0;
	while (!(fReadPtr == fWritePtr && fWrapped == 1) && n != 0)
	{
		UChar c = *p;
		if (c == 0)
			noughts++;
		else
			noughts = 0;
		if (noughts < 3)
		{
			*fWritePtr++ = c;
			taken++;
			if (fWritePtr == fBufEnd)
			{
				fWritePtr = fBufStart;
				fWrapped ^= 1;
			}
		}
		p++;
		n--;
	}
	*data = p;
	*count = n;
	*appended = taken;
	return !(fReadPtr == fWritePtr && fWrapped == 1);
}


// ROM 0x00204cec GetLength__10TT4FaxLineFv
// The bytes not yet read (1 if only the bits of the one in hand are left).
int
TT4FaxLine::GetLength()
{
	int length;
	if (fWrapped == 1)
		length = (fBufEnd - fReadPtr) + (fWritePtr - fBufStart);
	else
		length = fWritePtr - fReadPtr;
	if (length == 0 && fBitsLeft != 0)
		length = 1;
	return length;
}


// ROM 0x00204940 GetNextBit__10TT4FaxLineFv
// The next bit, least significant first.  fReadPtr is the byte last read,
// so the next is the one after it.  BUG: after Reset nothing has been read,
// yet the first byte is stepped over all the same - the ring's first byte
// is never decoded.  The ring empty throws exFaxBufOverrunException with
// kFaxToolErrT4DecodeUnderflow.
int
TT4FaxLine::GetNextBit()
{
	if (fBitsLeft == 0)
	{
		UChar* write = fWritePtr;
		if (fReadPtr != write || fWrapped != 0)
			fReadPtr++;
		if (fBufEnd == fReadPtr)
		{
			fReadPtr = fBufStart;
			fWrapped ^= 1;
		}
		if (fReadPtr == write && fWrapped == 0)
			Throw(exFaxBufOverrunException, (void*) (long) kFaxToolErrT4DecodeUnderflow, nil);
		fCurByte = *fReadPtr;
		fBitsLeft = 8;
	}
	fBitsLeft--;
	int bit = fCurByte & 1;
	fCurByte >>= 1;
	return bit;
}


// ROM 0x00204a00 GetBits__10TT4FaxLineFi
// count bits, the first read the most significant.
int
TT4FaxLine::GetBits(int count)
{
	int bits = 0;
	for (int i = count - 1; i >= 0; i--)
		bits = GetNextBit() | (bits << 1);
	return bits;
}


// ROM 0x00204848 MHGetNextCode__10TT4FaxLineFQ210TT4FaxLine8RunColor
// The next code of a run of the colour given: a run length (0 to 63), a
// make-up (0x40 and up), 0x68 an end of line (its noughts and its one
// read), 0xfe no code.  A black code's first bits find its tree bit by bit,
// and the six shortest black codes (2 to 6 pixels, and 1) are answered
// straight from them.
int
TT4FaxLine::MHGetNextCode(RunColor color)
{
	const unsigned char* tree;
	if (color == kWhite)
		tree = kMajorIndexWhite[GetBits(4)];
	else
	{
		if (GetNextBit() == 1)
			return GetNextBit() == 1 ? 2 : 3;					// 11, 10
		if (GetNextBit() == 1)
			return GetNextBit() == 1 ? 4 : 1;					// 011, 010
		if (GetNextBit() == 1)
			return GetNextBit() == 1 ? 5 : 6;					// 0011, 0010
		tree = kMajorIndexBlack[GetBits(2)];					// 000..
	}
	int node = 1;
	while (tree[node] == 0xff)
		node = (node << 1) + GetNextBit();
	int code = tree[node];
	if (code == 0x68)
		while (GetNextBit() == 0)
			;
	return code;
}


// ROM 0x0020470c EmitBits__10TT4FaxLineFQ210TT4FaxLine8RunColoriPi
// A run of pixels put into the line (a black pixel a 1, most significant
// first), as far as the line reaches; the bytes finished answered.  false
// when the line was already full or the run went past its end.
Boolean
TT4FaxLine::EmitBits(RunColor color, int run, int* bytesWritten)
{
	if (fOut >= fOutEnd)
	{
		*bytesWritten = 0;
		return false;
	}
	UByte fill = (color != kBlack) ? 0x00 : 0xff;
	if (fOutBitsFree > run)
	{
		fOutBitsFree -= run;
		if (color == kBlack)
			fOutByte |= kFaxMaskFrom[run] << fOutBitsFree;
		*bytesWritten = 0;
		return true;
	}
	UChar* start = fOut;
	if (fOutBitsFree != 8)
	{
		if (color == kBlack)
			fOutByte |= kFaxMaskFrom[fOutBitsFree];
		run -= fOutBitsFree;
		*fOut++ = fOutByte;
		fOutBitsFree = 8;
		fOutByte = 0;
	}
	int bytes = run >> 3;
	int room = fOutEnd - fOut;
	if (bytes > room)
		bytes = room;
	run -= bytes << 3;
	while (bytes-- > 0)
		*fOut++ = fill;
	if (run < 8)
	{
		if (color == kBlack)
			fOutByte = kFaxMaskBelow[run];
		fOutBitsFree = 8 - run;
	}
	*bytesWritten = fOut - start;
	return run < 8;
}


// ROM 0x00204d34 DoMHDecodeLine__10TT4FaxLineFPUciRi
// A line decoded, from white, to its end of line (true) - or to a code
// that is not one, or a run past the line's end (false).  bytesDecoded is
// the bytes of the line finished.
Boolean
TT4FaxLine::DoMHDecodeLine(UChar* line, int lineBytes, int& bytesDecoded)
{
	RunColor color = kWhite;
	bytesDecoded = 0;
	fOut = line;
	fOutBitsFree = 8;
	fOutByte = 0;
	fOutEnd = line + lineBytes;
	int code = MHGetNextCode(color);
	while (code != 0x68)
	{
		if (code == 0xfe)
			return false;
		int run;
		if (code >= 0x40)
		{
			int next = MHGetNextCode(color);
			if (next == 0x68 || next >= 0x40)
				return false;
			run = next + (code << 6) - 0xfc0;
		}
		else
			run = code;
		int written;
		if (EmitBits(color, run, &written) != true)
		{
			bytesDecoded += written;
			return false;
		}
		bytesDecoded += written;
		color = (color != kBlack) ? kBlack : kWhite;
		code = MHGetNextCode(color);
	}
	return true;
}


// ROM 0x00204b24 SkipPastEOL__10TT4FaxLineFv
// To just past the next end of line (eleven noughts or more and a one);
// false if the ring runs dry first.
Boolean
TT4FaxLine::SkipPastEOL()
{
	Boolean found = true;
	newton_try
	{
		int noughts;
		do
		{
			noughts = 0;
			while (GetNextBit() != 0)
				;
			do
				noughts++;
			while (GetNextBit() == 0);
		} while (noughts < 11);
	}
	newton_catch(exFaxBufOverrunException)
	{
		found = false;
	}
	end_try;
	return found;
}


// ROM 0x00204bc4 DecodeLine__10TT4FaxLineFPUciRiUl
// A line decoded (catchOverrun: the ring running dry is an undecoded line
// rather than an exception).  An empty line (an end of line alone) is
// passed over, up to six of them; a bad one skipped to the next end of
// line.  true only for a whole line - or for no line at all after the six
// empty ones when none was asked for.
Boolean
TT4FaxLine::DecodeLine(UChar* line, int lineBytes, int& bytesDecoded, ULong catchOverrun)
{
	Boolean result;
	int tries = 0;
	for ( ; ; )
	{
		if (catchOverrun == 0)
			result = DoMHDecodeLine(line, lineBytes, bytesDecoded);
		else
		{
			newton_try
			{
				result = DoMHDecodeLine(line, lineBytes, bytesDecoded);
			}
			newton_catch(exFaxBufOverrunException)
			{
				result = false;
			}
			end_try;
		}
		if (bytesDecoded == 0 && result == true)
		{
			if (tries++ < 6)
				continue;
		}
		else if (result == false)
			SkipPastEOL();
		break;
	}
	if (bytesDecoded != lineBytes)
	{
		if (tries >= 6 && lineBytes == 0)
			result = true;
		else
			result = false;
	}
	return result;
}
