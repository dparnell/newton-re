/*
	File:		frames/RichString.cpp

	Contains:	TRichString (RichString.h): the ROM's string view, its
				text munging and comparison.
*/

#include "RichString.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Unicode.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"

#include <string.h>


// ROM 0x001aeec0 __ct__11TRichStringFv
TRichString::TRichString()
{
	SetNoStringData();
}


// ROM 0x001aef08 __ct__11TRichStringFRC6RefVar
TRichString::TRichString(RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	SetStringData(str);
}


// ROM 0x001aef78 __ct__11TRichStringFPUsUl
TRichString::TRichString(UniChar* str, ULong size)
{
	SetCStringData(str, size);
}


// ROM 0x001aefd0 __ct__11TRichStringFPUs
TRichString::TRichString(UniChar* str)
{
	SetCPlainStringData(str);
}


TRichString::~TRichString()
{ }


// ROM 0x001af020 SetNoStringData__11TRichStringFv
void
TRichString::SetNoStringData(void)
{
	static UniChar empty = 0;
	fString = NILREF;
	fCString = nil;
	fFlag = false;
	SetFormatAndLength(&empty, 0);
}


// ROM 0x001ad704 SetStringData__11TRichStringFRC6RefVar
void
TRichString::SetStringData(RefArg str)
{
	fString = str;
	fCString = nil;
	fFlag = false;
	UniChar* text = GrabPtr();
	SetFormatAndLength(text, ::Length(fString));
	ReleasePtr();
}


// ROM 0x001ad764 SetCStringData__11TRichStringFPUsUl
void
TRichString::SetCStringData(UniChar* str, ULong size)
{
	fString = NILREF;
	fCString = str;
	fFlag = false;
	SetFormatAndLength(str, size);
}


// ROM 0x001ad780 SetCPlainStringData__11TRichStringFPUs
void
TRichString::SetCPlainStringData(UniChar* str)
{
	fString = NILREF;
	fCString = str;
	fFlag = false;
	SetFormatAndLength(str, (Ustrlen(str) + 1) * sizeof(UniChar));
}


// ROM 0x001aeba4 SetFormatAndLength__11TRichStringFPUsUl
// From the text of size bytes: the format is the low two bits of the
// last UniChar; a rich string's text length is in the trailer word
// (>> 4), the ink follows the text (padded to a word) and ends before
// the trailer.
void
TRichString::SetFormatAndLength(UniChar* str, ULong size)
{
	long chars = (long) (size / sizeof(UniChar)) - 1;
	long length = chars;
	long format = chars >= 0 ? (str[chars] & 3) : 0;
	if (format == kRichStringFormatInk)
	{
		ULong trailer = ((ULong) str[chars - 1] << 16) | str[chars];
		length = trailer >> 4;
	}
	fFormat = format;
	fSize = size;
	fLength = length;
	if (format == kRichStringFormatPlain)
	{
		fInkStart = 0;
		fInkOffset = size;
		fInkSize = 0;
	}
	else
	{
		ULong inkOffset = (length * sizeof(UniChar) + 5) & ~3;
		fInkOffset = inkOffset;
		fInkStart = inkOffset;
		fInkSize = size - inkOffset - 4;
	}
	fInkSize2 = fInkSize;
}


// ROM 0x001aec3c Format__11TRichStringCFv
long
TRichString::Format(void) const
{
	return fFormat;
}


// ROM 0x001aeb58 GrabPtr__11TRichStringCFv
// The text, the object locked against the collector (an indirect
// binary's data through its procedures - NOT YET RECONSTRUCTED).
UniChar*
TRichString::GrabPtr(void) const
{
	if ((Ref) fString == NILREF)
		return fCString;
	LockRef(fString);
	return (UniChar*) BinaryData(fString);
}


// ROM 0x001aeb90 ReleasePtr__11TRichStringCFv
void
TRichString::ReleasePtr(void) const
{
	if ((Ref) fString != NILREF)
		UnlockRef(fString);
}


// ROM 0x001adc4c SetObjectSize__11TRichStringFl
void
TRichString::SetObjectSize(long size)
{
	SetLength(fString, size);
}


// ROM 0x001ae420 GetChar__11TRichStringCFUl
UniChar
TRichString::GetChar(ULong index) const
{
	UniChar* text = GrabPtr();
	UniChar c = text[index];
	ReleasePtr();
	return c;
}


// ROM 0x001adddc SetChar__11TRichStringFUlUs
// (an ink word's character is replaced through MungeRange: NOT YET)
void
TRichString::SetChar(ULong index, UniChar c)
{
	UniChar* text = GrabPtr();
	if (text[index] == kInkChar)
	{
		UniChar one[2] = { c, 0 };
		TRichString replacement(one);
		MungeRange(index, 1, &replacement, 0, 1);
	}
	else
		text[index] = c;
	ReleasePtr();
}


// ROM 0x001ad7b4 DeleteRange__11TRichStringFUlT1
void
TRichString::DeleteRange(ULong start, ULong count)
{
	MungeRange(start, count, nil, 0, 0);
}


// ROM 0x001ad7dc InsertRange__11TRichStringFRC11TRichStringUlN22
void
TRichString::InsertRange(const TRichString& src, ULong srcStart, ULong count, ULong at)
{
	MungeRange(at, 0, &src, srcStart, count);
}


// ROM 0x001ad804 MungeRange__11TRichStringFUlT1PC11TRichStringN21
// count characters at start replaced by srcCount characters of src from
// srcStart (a deletion for no src, an insertion for count 0); the
// object grows or shrinks.  NOT YET RECONSTRUCTED: the ink data moved
// with the text and the trailer of a rich result - the result is plain.
void
TRichString::MungeRange(ULong start, ULong count, const TRichString* src, ULong srcStart, ULong srcCount)
{
	long delta = ((long) srcCount - (long) count) * sizeof(UniChar);
	long newLength = fLength + (long) srcCount - (long) count;
	long newSize = (newLength + 1) * sizeof(UniChar);
	long oldSize = fSize;
	if (newSize > oldSize)
		SetObjectSize(newSize);
	UniChar* srcText = src != nil ? src->GrabPtr() : nil;
	UniChar* text = GrabPtr();
	// the tail after the range moves by delta; the range takes the source
	long tail = fLength - (start + count);
	if (tail > 0 && delta != 0)
		BlockMove(text + start + count, text + start + srcCount, tail * sizeof(UniChar));
	if (src != nil && srcCount != 0)
		BlockMove(srcText + srcStart, text + start, srcCount * sizeof(UniChar));
	text[newLength] = 0;
	ReleasePtr();
	if (src != nil)
		src->ReleasePtr();
	if (newSize < oldSize)
		SetObjectSize(newSize);
	fFormat = kRichStringFormatPlain;
	fLength = newLength;
	fSize = newSize;
	fInkStart = 0;
	fInkOffset = newSize;
	fInkSize = fInkSize2 = 0;
}


// ROM 0x001adea4 CompareSubStringCommon__11TRichStringCFRC11TRichStringUllUc
// count characters of this from start (-1: to the end) against all of
// other.
int
TRichString::CompareSubStringCommon(const TRichString& other, ULong start, long count, Boolean exact) const
{
	if (count == -1)
		count = fLength - start;
	UniChar* text = GrabPtr();
	UniChar* otherText = other.GrabPtr();
	// NOT YET RECONSTRUCTED: CompareInkProc (0x001ade0c), which compares
	// two ink words - so ink collates as the kInkChar standing for it.
	int result = CompareUnicodeText(text + start, count, otherText, other.fLength,
									kDefaultSortTable, exact, nil, nil);
	other.ReleasePtr();
	ReleasePtr();
	return result;
}


// ROM 0x001ae80c Verify__11TRichStringCFv
// A well-formed string: a plain one has its terminator; a rich one's
// trailer and lengths agree (NOT YET RECONSTRUCTED: the ink words'
// structure).  ==> 0 when valid.
long
TRichString::Verify(void) const
{
	if ((Ref) fString == NILREF)
		return 0;
	long size = ::Length(fString);
	if (size < (long) sizeof(UniChar) || (size & 1) != 0)
		return 1;
	UniChar* text = GrabPtr();
	long result;
	if (fFormat == kRichStringFormatPlain)
		result = text[size / sizeof(UniChar) - 1] == 0 ? 0 : 1;
	else
		result = (fLength >= 0 && (ULong) (fLength * sizeof(UniChar)) <= fInkOffset && fInkSize >= 0) ? 0 : 1;
	ReleasePtr();
	return result;
}


// ROM 0x000dd3c0 IsInkWord__FRC6RefVar
// Whether the object is an ink word: a binary of class 'inkWord.
Boolean
IsInkWord(RefArg obj)
{
	if (!IsBinary(obj))
		return false;
	return EQRef(ClassOf(obj), RSSYMinkword);
}
