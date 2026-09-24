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


// ROM 0x001abd20 GetLengthsAndDataInRange__11TRichStringCFUlT1PsPc
// The runs of a range one by one: an ink character is a run of its own,
// and the text between two of them is a run.  For an ink run the answer
// is where its blob is, which is what the text engine turns into a font
// of one glyph; for a text run it is nil.
void
TRichString::GetLengthsAndDataInRange(ULong start, ULong count, short* lengths,
									  void** data) const
{
	const char* base = (const char*) GrabPtr();
	const UniChar* text = (const UniChar*) base;
	if (fLength < start + count)
		count = fLength - start;
	// which ink word the range starts at
	ULong wordNo = 0;
	for (ULong i = 0; i < start; i++)
		if (text[i] == kInkChar)
			wordNo++;
	const UniChar* at = text + start;
	while (count != 0)
	{
		short length;
		if (*at == kInkChar)
		{
			*data = (void*) (base + GetInkWordNoInfoOffset(wordNo));
			length = 1;
			count--;
			at++;
			wordNo++;
		}
		else
		{
			*data = nil;
			length = 0;
			UniChar c = 0;
			do
			{
				length++;
				count--;
				if (count != 0)
					c = *++at;
			}
			while (count != 0 && c != kInkChar);
		}
		data++;
		*lengths++ = length;
	}
	ReleasePtr();
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


/*------------------------------------------------------------------------------
	A   r i c h   s t r i n g   a s   a   p a r a g r a p h
------------------------------------------------------------------------------*/

// A rich string and a paragraph keep the same thing two different ways.
// A rich string is one object: the characters, with 0xf700 standing for
// each word of writing, followed by a region holding the writing itself.
// A paragraph is two: a plain string in which each word of writing is
// the character 0xf701, and a styles array whose run for that character
// *is* the 'inkWord binary.  These two make the second form out of the
// first, which is what lets a rich string - a note dropped from
// somewhere else, a clipping off the clipboard - be put into a
// paragraph without losing its writing.

// The character a paragraph stands a word of writing as.  It is
// `ink/Ink.h`'s kInkWordChar; the ink area is built on this one, not
// the other way round, so the constant is repeated here as the ROM
// repeats it.
const UniChar kParagraphInkChar = 0xf701;


// ROM 0x001abf6c MakeParagraphTextSlot__11TRichStringCFv
// The text a paragraph would hold: the characters copied out (with the
// terminator), every 0xf700 turned into the 0xf701 a paragraph uses.
Ref
TRichString::MakeParagraphTextSlot(void) const
{
	long size = fLength * (long) sizeof(UniChar) + (long) sizeof(UniChar);
	RefVar text(AllocateBinary(RSSYMstring, size));
	UniChar* chars = (UniChar*) BinaryData(text);
	BlockMove(GrabPtr(), chars, size);
	for (long i = 0; i < fLength; i++)
		if (chars[i] == kInkChar)
			chars[i] = kParagraphInkChar;
	ReleasePtr();
	return text;
}


// ROM 0x001ac038 MakeParagraphStylesSlot__11TRichStringCFRC6RefVar
// The styles array that goes with it: pairs of (how many characters,
// what style).  A stretch of ordinary characters is one run in `style`;
// each word of writing is a run of one character whose style is a copy
// of the word's own 'inkWord binary, which is how the font engine finds
// it again.  A string with no writing in it is one run over the whole
// of it.
Ref
TRichString::MakeParagraphStylesSlot(RefArg style) const
{
	RefVar styles;
	if (NumInkWords() == 0)
	{
		styles = AllocateArray(RSSYMstyles, 2);
		SetArraySlot(styles, 0, RefVar(MAKEINT(fLength)));
		SetArraySlot(styles, 1, style);
		return styles;
	}

	const UniChar* text = GrabPtr();
	// how many runs there are: each word of writing is one, and each
	// stretch between them is one
	long runs = 0;
	long at = 0;
	while (text[at] != 0)
	{
		if (text[at] == kInkChar)
			at++;
		else
			while (text[at] != kInkChar && text[at] != 0)
				at++;
		runs++;
	}
	styles = AllocateArray(RSSYMstyles, runs * 2);

	long run = 0;				// the run being written
	long word = 0;				// which word of writing
	long count = 0;				// the plain characters gathered so far
	at = 0;
	while (text[at] != 0)
	{
		if (text[at] == kInkChar)
		{
			if (count != 0)
			{
				// the stretch before it
				SetArraySlot(styles, run * 2, RefVar(MAKEINT(count)));
				SetArraySlot(styles, run * 2 + 1, style);
				run++;
			}
			// the word itself, out of the ink region: a halfword of size
			// and then the blob
			ULong offset = GetInkWordNoInfoOffset((ULong) word);
			const char* bytes = (const char*) text + offset;
			long size = *(const UniChar*) bytes;
			RefVar blob(AllocateBinary(RSSYMinkword, size));
			BlockMove(bytes + sizeof(UniChar), BinaryData(blob), size);
			word++;
			SetArraySlot(styles, run * 2, RefVar(MAKEINT(1)));
			SetArraySlot(styles, run * 2 + 1, blob);
			run++;
			at++;
			count = 0;
		}
		else
			while (text[at] != kInkChar && text[at] != 0)
			{
				at++;
				count++;
			}
	}
	if (count != 0)
	{
		SetArraySlot(styles, run * 2, RefVar(MAKEINT(count)));
		SetArraySlot(styles, run * 2 + 1, style);
	}
	ReleasePtr();
	return styles;
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

// ROM 0x001fe4e4 FMakeRichString__FRC6RefVarN21
// MakeRichString(text, styles): a string with the writing in it.
//
// A paragraph keeps its ink as an `'inkWord` binary in the style run of
// the character that stands for it, which is why the two halves have to
// be carried separately.  A *rich string* carries both in one object:
// the text, then one blob per ink character - a halfword of length, the
// blob, and padding to a word - and a trailer word of the character
// count with a 1 in its low nibble that says the string has ink in it.
//
// The paragraph's ink character (0xf701) becomes the rich string's own
// (0xf700) on the way.  A text with no ink in its styles is just cloned.
//
// (host: as in MungeRange above, the blobs' length halfwords and the
// trailer are written as the UniChars they are read back as, rather than
// as the ROM's two and four bytes, so that a string object's halfwords
// stay in the host's order.)
Ref
FMakeRichString(RefArg /*rcvr*/, RefArg text, RefArg styles)
{
	if (ISNIL(styles) || ISINT(styles))
		return Clone(text);

	long textSize = Length(text);
	long total = (textSize + 3) & ~3;
	long pairs = Length(styles) / 2;
	long inkCount = 0;
	for (long i = 0; i < pairs; i++)
	{
		RefVar style(GetArraySlotRef(styles, i * 2 + 1));
		if (IsInkWord(style))
		{
			total += (Length(style) + 5) & ~3;
			inkCount++;
		}
	}
	if (inkCount == 0)
		return Clone(text);

	RefVar rich(AllocateBinary(RSSYMstring, total + 4));
	UByte* data = (UByte*) BinaryData(rich);
	BlockMove(BinaryData(text), data, textSize);
	long characters = (textSize >> 1) - 1;		// without the terminator
	UniChar* chars = (UniChar*) data;
	for (long i = 0; i < characters; i++)
		if (chars[i] == kParagraphInkChar)
			chars[i] = kInkChar;

	UByte* out = data + ((textSize + 3) & ~3);
	for (long i = 0; i < pairs; i++)
	{
		RefVar style(GetArraySlotRef(styles, i * 2 + 1));
		if (!IsInkWord(style))
			continue;
		long size = Length(style);
		*(UniChar*) out = (UniChar) size;
		BlockMove(BinaryData(style), out + 2, size);
		out += (size + 5) & ~3;
	}
	ULong trailer = (((ULong) characters & 0x0fffffff) << 4) | kRichStringFormatInk;
	((UniChar*) out)[0] = (UniChar) (trailer >> 16);
	((UniChar*) out)[1] = (UniChar) trailer;
	return rich;
}


// ROM 0x001fe4f4 FDecodeRichString__FRC6RefVarN21
// DecodeRichString(string, style): the other way - a frame of the `text`
// and `styles` slots a paragraph wants, with the ink taken back out of
// the string and put into the style runs.
Ref
FDecodeRichString(RefArg /*rcvr*/, RefArg string, RefArg style)
{
	TRichString rich(string);
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMtext, RefVar(rich.MakeParagraphTextSlot()));
	SetFrameSlot(frame, RSSYMstyles, RefVar(rich.MakeParagraphStylesSlot(style)));
	return frame;
}


// ROM 0x001fe990 FStripInk
// StripInk(string, replacement): the ink characters taken out of a rich
// string, or replaced by a character of the caller's choosing.  The
// string is changed in place and answered.
//
// (The blobs are left where they are: what goes is only the character
//  that stands for them, so the string still carries the writing and
//  still says it is a rich string.  That is the ROM's own doing.)
Ref
FStripInk(RefArg /*rcvr*/, RefArg string, RefArg replacement)
{
	TRichString rich(string);
	if (ISNIL(replacement))
	{
		ULong at = 0;
		while (rich.GetChar(at) != 0)
		{
			if (rich.GetChar(at) == kInkChar)
				rich.DeleteRange(at, 1);
			else
				at++;
		}
	}
	else if (ISCHAR(replacement))
	{
		UniChar with = XRCHAR(replacement);
		long length = rich.Length();
		for (long at = 0; at < length; at++)
			if (rich.GetChar((ULong) at) == kInkChar)
				rich.SetChar((ULong) at, with);
	}
	return string;
}
