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


// ROM 0x001ac9d8 __ct__11TRichStringFv
TRichString::TRichString()
{
	SetNoStringData();
}


// ROM 0x001aca20 __ct__11TRichStringFRC6RefVar
TRichString::TRichString(RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	SetStringData(str);
}


// ROM 0x001aca90 __ct__11TRichStringFPUsUl
TRichString::TRichString(UniChar* str, ULong size)
{
	SetCStringData(str, size);
}


// ROM 0x001acae8 __ct__11TRichStringFPUs
TRichString::TRichString(UniChar* str)
{
	SetCPlainStringData(str);
}


TRichString::~TRichString()
{ }


// ROM 0x001acb38 SetNoStringData__11TRichStringFv
void
TRichString::SetNoStringData(void)
{
	static UniChar empty = 0;
	fString = NILREF;
	fCString = nil;
	fFlag = false;
	SetFormatAndLength(&empty, 0);
}


// ROM 0x001ab21c SetStringData__11TRichStringFRC6RefVar
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


// ROM 0x001ab27c SetCStringData__11TRichStringFPUsUl
void
TRichString::SetCStringData(UniChar* str, ULong size)
{
	fString = NILREF;
	fCString = str;
	fFlag = false;
	SetFormatAndLength(str, size);
}


// ROM 0x001ab298 SetCPlainStringData__11TRichStringFPUs
void
TRichString::SetCPlainStringData(UniChar* str)
{
	fString = NILREF;
	fCString = str;
	fFlag = false;
	SetFormatAndLength(str, (Ustrlen(str) + 1) * sizeof(UniChar));
}


// ROM 0x001ac6bc SetFormatAndLength__11TRichStringFPUsUl
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


// ROM 0x001ac754 Format__11TRichStringCFv
long
TRichString::Format(void) const
{
	return fFormat;
}


// ROM 0x001ac670 GrabPtr__11TRichStringCFv
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


// ROM 0x001ac6a8 ReleasePtr__11TRichStringCFv
void
TRichString::ReleasePtr(void) const
{
	if ((Ref) fString != NILREF)
		UnlockRef(fString);
}


// ROM 0x001ab764 SetObjectSize__11TRichStringFl
void
TRichString::SetObjectSize(long size)
{
	SetLength(fString, size);
}


// ROM 0x001abf38 GetChar__11TRichStringCFUl
UniChar
TRichString::GetChar(ULong index) const
{
	UniChar* text = GrabPtr();
	UniChar c = text[index];
	ReleasePtr();
	return c;
}


// ROM 0x001ab8f4 SetChar__11TRichStringFUlUs
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


// ROM 0x001ab2cc DeleteRange__11TRichStringFUlT1
void
TRichString::DeleteRange(ULong start, ULong count)
{
	MungeRange(start, count, nil, 0, 0);
}


// ROM 0x001ab2f4 InsertRange__11TRichStringFRC11TRichStringUlN22
void
TRichString::InsertRange(const TRichString& src, ULong srcStart, ULong count, ULong at)
{
	MungeRange(at, 0, &src, srcStart, count);
}


// ROM 0x001ab31c MungeRange__11TRichStringFUlT1PC11TRichStringN21
// count characters at start replaced by srcCount characters of src from
// srcStart (a deletion for no src, an insertion for count 0), and the ink
// of those characters replaced by the ink of the source's; the object
// grows or shrinks.
//
// The two are moved separately, because the ink region starts on a word
// boundary after the text: an odd number of characters going in or out
// moves the text by two bytes and the ink by nought or four, and which of
// the two has to move first depends on which way they are going.  A
// string that ends up with ink where it had none gains the four-byte
// trailer, and one that loses all its ink loses it again.
//
// (host: the trailer and the blobs' length halfwords are written as the
// UniChars they are read back as, rather than as the ROM's four and two
// bytes, so that a string object's halfwords stay in the host's order -
// SetFormatAndLength reads the trailer the same way.)
void
TRichString::MungeRange(ULong start, ULong count, const TRichString* src, ULong srcStart, ULong srcCount)
{
	long oldLength = fLength;
	long oldSize = (long) fSize;
	ULong srcInkOffset = 0, srcInkSize = 0;
	if (src != nil)
		src->GetInkData(srcStart, srcCount, &srcInkOffset, &srcInkSize);
	ULong myInkOffset = 0, myInkSize = 0;
	GetInkData(start, count, &myInkOffset, &myInkSize);

	long textDelta = ((long) srcCount - (long) count) * (long) sizeof(UniChar);
	long inkDelta = (long) srcInkSize - (long) myInkSize;
	long trailerDelta = 0;
	long newFormat = fFormat;
	Boolean formatChanges = false;
	if (newFormat == kRichStringFormatPlain)
	{
		if (inkDelta > 0)
		{
			newFormat = kRichStringFormatInk;		// ink where there was none: the trailer comes too
			trailerDelta = 4;
			formatChanges = true;
		}
	}
	else if (srcInkSize == 0 && (long) myInkSize == oldSize - (long) fInkStart - 4)
	{
		newFormat = kRichStringFormatPlain;		// all of the ink goes and none comes in
		trailerDelta = -4;
		formatChanges = true;
	}

	long newInkOffset = (oldLength + 1) * (long) sizeof(UniChar) + textDelta;
	if (newFormat != kRichStringFormatPlain)
		newInkOffset = (newInkOffset + 3) & ~3;
	long inkShift = newInkOffset - (long) fInkOffset;
	long shift = (newFormat == kRichStringFormatPlain && !formatChanges) ? textDelta : inkShift;
	long sizeDelta = shift + inkDelta + trailerDelta;
	if (sizeDelta > 0)
		SetObjectSize(oldSize + sizeDelta);

	UniChar* srcText = src != nil ? src->GrabPtr() : nil;
	UniChar* text = GrabPtr();
	char* base = (char*) text;
	if (textDelta != 0)
	{
		char* tail = base + (start + count) * sizeof(UniChar);
		if ((textDelta & 3) == 0 || fFormat == kRichStringFormatPlain)
			BlockMove(tail, tail + textDelta, oldSize - (tail - base));
		else
		{
			// the ink moves by a different amount from the text: whichever
			// is going backwards goes first, so that neither is written
			// over before it has been read
			char* ink = base + fInkStart;
			long inkBytes = oldSize - (long) fInkStart;
			long tailBytes = ((oldLength + 1) - (long) (start + count)) * (long) sizeof(UniChar);
			if (textDelta < 0)
			{
				BlockMove(tail, tail + textDelta, tailBytes);
				if (inkShift != 0)
					BlockMove(ink, ink + inkShift, inkBytes);
			}
			else
			{
				if (inkShift != 0)
					BlockMove(ink, ink + inkShift, inkBytes);
				BlockMove(tail, tail + textDelta, tailBytes);
			}
		}
	}
	long newSize = oldSize + inkShift;
	if (src != nil && srcCount != 0)
		BlockMove(srcText + srcStart, text + start, srcCount * sizeof(UniChar));

	long newLength = oldLength + textDelta / (long) sizeof(UniChar);
	fFormat = newFormat;
	fLength = newLength;
	fSize = (ULong) ((long) fSize + sizeDelta);
	if (newFormat == kRichStringFormatInk)
	{
		ULong offset = (ULong) ((newLength * (long) sizeof(UniChar) + 5) & ~3);
		fInkOffset = offset;
		fInkStart = offset;
	}
	char* ink = base + fInkStart;
	if (srcInkSize != myInkSize)
	{
		char* after = ink + myInkOffset + myInkSize;
		BlockMove(after, after + inkDelta, newSize - (after - base));
		newSize += inkDelta;
	}
	newSize += trailerDelta;
	if (src != nil && srcInkSize != 0)
		BlockMove((char*) srcText + src->fInkStart + srcInkOffset, ink + myInkOffset, srcInkSize);
	text[newLength] = 0;		// (the ROM moves the terminator along with the tail)
	if (newFormat == kRichStringFormatInk)
	{
		ULong trailer = ((ULong) newLength << 4) | 1;
		UniChar* end = (UniChar*) (base + newSize);
		end[-2] = (UniChar) (trailer >> 16);
		end[-1] = (UniChar) trailer;
		fInkSize = newSize - (long) fInkStart - 4;
	}
	else
	{
		fInkStart = 0;
		fInkOffset = (ULong) newSize;
		fInkSize = 0;
	}
	fInkSize2 = fInkSize;
	ReleasePtr();
	if (src != nil)
		src->ReleasePtr();
	if (sizeDelta < 0)
		SetObjectSize(newSize);
}


// ROM 0x001ab7f4 GetInkData__11TRichStringCFUlT1PUlT3
// Where the ink of count characters at start is, and how much of it there
// is: both byte counts within the ink region.  The characters up to the
// range are walked for the offset and those in it for the size, each
// kInkChar stepping over its blob.
void
TRichString::GetInkData(ULong start, ULong count, ULong* outOffset, ULong* outSize) const
{
	const char* base = (const char*) GrabPtr();
	const UniChar* text = (const UniChar*) base;
	const char* ink = base + fInkStart;
	ULong before = 0;
	ULong total = 0;
	Boolean inRange = false;
	for (ULong left = start + count; left != 0; )
	{
		left--;
		UniChar c = *text++;
		if (c != kInkChar)
			continue;
		ULong size = InkBlobSize(*(const UniChar*) ink);
		ULong next = total;
		if (left < count)
		{
			if (!inRange)
			{
				inRange = true;
				next = 0;
				before = total;
			}
		}
		else
			before = total + size;
		total = next + size;
		ink += size;
	}
	ReleasePtr();
	*outOffset = before;
	*outSize = inRange ? total : 0;
}


// ROM 0x001abe2c GetInkWordNoInfoOffset__11TRichStringCFUl
// Where the index'th ink word's blob is, as a byte offset from the start
// of the object - 0 when there is no such ink word.
ULong
TRichString::GetInkWordNoInfoOffset(ULong index) const
{
	const char* base = (const char*) GrabPtr();
	const UniChar* text = (const UniChar*) base;
	ULong offset = fInkStart;
	ULong found = 0;
	for (UniChar c = *text; c != 0; c = *text)
	{
		text++;
		if (c != kInkChar)
			continue;
		if (index == 0)
		{
			found = offset;
			break;
		}
		index--;
		offset += InkBlobSize(*(const UniChar*) (base + offset));
	}
	ReleasePtr();
	return found;
}


// ROM 0x001abeb8 CloneInkWordNo__11TRichStringCFUl
// The index'th ink word's data as an 'inkWord binary of its own.
Ref
TRichString::CloneInkWordNo(ULong index) const
{
	const char* base = (const char*) GrabPtr();
	ULong offset = GetInkWordNoInfoOffset(index);
	long length = *(const UniChar*) (base + offset);
	RefVar word(AllocateBinary(RSSYMinkword, length));
	BlockMove(base + offset + sizeof(UniChar), BinaryData(word), length);
	ReleasePtr();
	return word;
}


// ROM 0x001abb10 NumInkWords__11TRichStringCFv
long
TRichString::NumInkWords(void) const
{
	if (Format() == kRichStringFormatPlain)
		return 0;
	const UniChar* text = GrabPtr();
	long count = 0;
	for (UniChar c = *text; c != 0; c = *text)
	{
		text++;
		if (c == kInkChar)
			count++;
	}
	ReleasePtr();
	return count;
}


// ROM 0x001abb78 NumInkWordsInRange__11TRichStringCFUlT1
// The ink words among count characters at start - the walk stops at the
// text's end wherever the count would have taken it.
long
TRichString::NumInkWordsInRange(ULong start, ULong count) const
{
	if (Format() == kRichStringFormatPlain)
		return 0;
	const UniChar* text = GrabPtr();
	long found = 0;
	for (; start != 0; start--)
		if (*text++ == 0)
		{
			ReleasePtr();
			return 0;
		}
	for (; count != 0; count--)
	{
		UniChar c = *text++;
		if (c == 0)
			break;
		if (c == kInkChar)
			found++;
	}
	ReleasePtr();
	return found;
}


// ROM 0x001abc20 InkWordNoAtOffset__11TRichStringCFUl
// Which ink word the character at the offset is, or -1 when it is not one.
long
TRichString::InkWordNoAtOffset(ULong offset) const
{
	const UniChar* text = GrabPtr();
	long found = -1;
	if (text[offset] == kInkChar)
	{
		found = -1;
		for (ULong left = offset + 1; left != 0; left--)
		{
			UniChar c = *text++;
			if (c == 0)
				break;
			if (c == kInkChar)
				found++;
		}
	}
	ReleasePtr();
	return found;
}


// ROM 0x001abc88 NumInkAndTextRunsInRange__11TRichStringCFUlT1
// How many runs of plain text and single ink words count characters at
// start come to - each ink word is a run of its own and everything
// between two of them is one.
long
TRichString::NumInkAndTextRunsInRange(ULong start, ULong count) const
{
	const UniChar* text = GrabPtr() + start;
	long runs = 0;
	if ((ULong) fLength < start + count)
		count = (ULong) fLength - start;
	while ((long) count > 0)
	{
		count--;
		if (*text++ != kInkChar)
			while (count != 0 && *text != kInkChar)
			{
				count--;
				text++;
			}
		runs++;
	}
	ReleasePtr();
	return runs;
}


// ROM 0x001aba5c CompareInk__11TRichStringCFPC11TRichStringUlT2
// Two ink words compared by their data: the shorter prefix first, and
// the shorter word first when they agree on it.
int
TRichString::CompareInk(const TRichString* other, ULong offset, ULong otherOffset) const
{
	const char* base = (const char*) GrabPtr();
	const char* otherBase = (const char*) other->GrabPtr();
	ULong mine = GetInkWordNoInfoOffset((ULong) InkWordNoAtOffset(offset));
	long myLength = *(const UniChar*) (base + mine);
	ULong theirs = other->GetInkWordNoInfoOffset((ULong) other->InkWordNoAtOffset(otherOffset));
	long theirLength = *(const UniChar*) (otherBase + theirs);
	long shared = myLength < theirLength ? myLength : theirLength;
	int result = memcmp(base + mine + sizeof(UniChar), otherBase + theirs + sizeof(UniChar), (size_t) shared);
	if (result == 0)
		result = (int) (myLength - theirLength);
	ReleasePtr();
	other->ReleasePtr();
	return result;
}


// ROM 0x001ab9a0 CompareInkProc__FlT1Pv
// What CompareUnicodeText calls when both strings have an ink word where
// it has got to.  The offsets are the characters', counted from where the
// comparison started in each string.
long
CompareInkProc(long offset, long otherOffset, void* refCon)
{
	CompareInkInfo* info = (CompareInkInfo*) refCon;
	return info->fString->CompareInk(info->fOther, (ULong) (info->fStart + offset), (ULong) otherOffset);
}


// ROM 0x001ab9bc CompareSubStringCommon__11TRichStringCFRC11TRichStringUllUc
// count characters of this from start (-1: to the end) against all of
// other.
int
TRichString::CompareSubStringCommon(const TRichString& other, ULong start, long count, Boolean exact) const
{
	if (count == -1)
		count = fLength - start;
	UniChar* text = GrabPtr();
	UniChar* otherText = other.GrabPtr();
	// two ink words are compared by their data (CompareInkProc), not by
	// the kInkChar that stands for them
	CompareInkInfo info;
	info.fString = this;
	info.fStart = (long) start;
	info.fOther = &other;
	int result = CompareUnicodeText(text + start, count, otherText, other.fLength,
									kDefaultSortTable, exact, CompareInkProc, &info);
	other.ReleasePtr();
	ReleasePtr();
	return result;
}


// ROM 0x001ac324 Verify__11TRichStringCFv
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


// ROM 0x000dc120 IsInkWord__FRC6RefVar
// Whether the object is an ink word: a binary of class 'inkWord.
Boolean
IsInkWord(RefArg obj)
{
	if (!IsBinary(obj))
		return false;
	return EQRef(ClassOf(obj), RSSYMinkword);
}
