/*
	File:		frames/RichString.h

	Contains:	TRichString, the ROM's view of a NewtonScript string: its
				UniChar text and, in a rich string, the ink words stored
				after the text (a trailer word ends the object: text
				length << 4 | 1).  The string functions work through it.

	The ink region holds one blob per kInkChar of the text, in text
	order: a halfword giving the blob's length, that many bytes, and
	padding to a word - so a blob takes (length + 5) & ~3 bytes.  The
	blob is an ink word's data, the same bytes the 'inkWord binary
	CloneInkWordNo makes carries.  MungeRange moves the blobs with the
	characters they belong to, and the comparisons hand
	CompareUnicodeText (frames/SortTables.h) CompareInkProc so two ink
	words are compared by their bytes rather than collating as the
	kInkChar that stands for them.

	MakeParagraphTextSlot and MakeParagraphStylesSlot are how a
	paragraph's text and styles are made out of a string of mixed ink and
	text; MakeRichString goes the other way, and StripInk takes the ink
	characters out of one.

	NOT YET RECONSTRUCTED: the ink words' structure in Verify.

	The DDK has no header for TRichString; the layout is the ROM's (0x28).
*/

#ifndef __RICHSTRING_H
#define __RICHSTRING_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __SORTTABLES_H
#include "SortTables.h"		// the collation the string comparisons go through
#endif

const UniChar kInkChar = 0xf700;			// the character standing for an ink word in a rich string
const long kRichStringFormatPlain = 0;
const long kRichStringFormatInk = 1;

class TRichString
{
public:
	TRichString();
	TRichString(RefArg str);
	TRichString(UniChar* str, ULong size);
	TRichString(UniChar* str);
	~TRichString();

	void		SetStringData(RefArg str);
	void		SetCStringData(UniChar* str, ULong size);
	void		SetCPlainStringData(UniChar* str);
	void		SetNoStringData(void);
	void		SetFormatAndLength(UniChar* str, ULong size);
	long		Format(void) const;
	long		Length(void) const				{ return fLength; }
	Ref			String(void) const				{ return fString; }

	UniChar		GetChar(ULong index) const;
	void		SetChar(ULong index, UniChar c);
	void		DeleteRange(ULong start, ULong count);
	void		InsertRange(const TRichString& src, ULong srcStart, ULong count, ULong at);
	void		MungeRange(ULong start, ULong count, const TRichString* src, ULong srcStart, ULong srcCount);
	int			CompareSubStringCommon(const TRichString& other, ULong start, long count, Boolean exact) const;
	long		Verify(void) const;				// 0 when well formed

	// The ink.  An offset "in the ink" is a byte offset from the start of
	// the ink region (fInkStart); GetInkWordNoInfoOffset answers one from
	// the start of the object instead, because that is what its callers
	// add to GrabPtr.
	void		GetInkData(ULong start, ULong count, ULong* offset, ULong* size) const;	// ROM 0x001ab7f4 GetInkData__11TRichStringCFUlT1PUlT3
	ULong		GetInkWordNoInfoOffset(ULong index) const;	// ROM 0x001abe2c GetInkWordNoInfoOffset__11TRichStringCFUl
	Ref			CloneInkWordNo(ULong index) const;			// ROM 0x001abeb8 CloneInkWordNo__11TRichStringCFUl
	long		NumInkWords(void) const;					// ROM 0x001abb10 NumInkWords__11TRichStringCFv
	long		NumInkWordsInRange(ULong start, ULong count) const;	// ROM 0x001abb78 NumInkWordsInRange__11TRichStringCFUlT1
	long		InkWordNoAtOffset(ULong offset) const;		// ROM 0x001abc20 InkWordNoAtOffset__11TRichStringCFUl
	long		NumInkAndTextRunsInRange(ULong start, ULong count) const;	// ROM 0x001abc88 NumInkAndTextRunsInRange__11TRichStringCFUlT1
	// Those runs one by one: how many characters each covers, and - for
	// an ink run, which is always one character - where its blob is.  A
	// text run's `data` is nil.  Both arrays want room for as many runs
	// as NumInkAndTextRunsInRange counted.
	void		GetLengthsAndDataInRange(ULong start, ULong count, short* lengths,
										 void** data) const;	// ROM 0x001abd20 GetLengthsAndDataInRange__11TRichStringCFUlT1PsPc
	// The two halves of the same text as a paragraph keeps it: a plain
	// string whose every word of writing is the character 0xf701, and
	// a styles array whose run for that character is the word itself.
	Ref			MakeParagraphTextSlot(void) const;			// ROM 0x001abf6c MakeParagraphTextSlot__11TRichStringCFv
	Ref			MakeParagraphStylesSlot(RefArg style) const;	// ROM 0x001ac038 MakeParagraphStylesSlot__11TRichStringCFRC6RefVar
	int			CompareInk(const TRichString* other, ULong offset, ULong otherOffset) const;	// ROM 0x001aba5c CompareInk__11TRichStringCFPC11TRichStringUlT2

	UniChar*	GrabPtr(void) const;			// the text, the object locked
	void		ReleasePtr(void) const;
	void		SetObjectSize(long size);

	RefStruct	fString;			// +0x00  the string object (nil for a C string)
	UniChar*	fCString;			// +0x04  the text when there is no object
	ULong		fSize;				// +0x08  the object's size in bytes
	long		fLength;			// +0x0c  the text's length in UniChars
	long		fFormat;			// +0x10  0 plain, 1 with ink
	ULong		fInkOffset;			// +0x14  where the ink starts (the size for a plain string)
	ULong		fInkStart;			// +0x18  the ink's offset (0 for a plain string)
	long		fInkSize;			// +0x1c
	long		fInkSize2;			// +0x20
	Boolean		fFlag;				// +0x24
};

Boolean	IsInkWord(RefArg obj);				// an 'inkWord binary (the ink of a word not yet recognised)

// The bytes an ink word's blob takes in a rich string's ink region: its
// length halfword, its data, and the padding that keeps the next blob on
// a word boundary.
inline ULong	InkBlobSize(ULong length)	{ return (length + 5) & ~3UL; }

// What CompareInkProc is given as its refCon: the two strings being
// compared and where in the first one the comparison started.
struct CompareInkInfo
{
	const TRichString*	fString;		// +0x00
	long				fStart;			// +0x04
	const TRichString*	fOther;			// +0x08
};

long	CompareInkProc(long offset, long otherOffset, void* refCon);	// ROM 0x001ab9a0 CompareInkProc__FlT1Pv


// The rich-string natives: a string made out of a paragraph's text and
// styles and taken apart again, and the ink characters stripped out of
// one.  (Registered by frames/StringNatives.cpp.)
extern const UniChar kParagraphInkChar;	// 0xf701, the character a paragraph uses
Ref		FMakeRichString(RefArg rcvr, RefArg text, RefArg styles);	// ROM 0x001fe4e4 FMakeRichString__FRC6RefVarN21
Ref		FDecodeRichString(RefArg rcvr, RefArg string, RefArg style);	// ROM 0x001fe4f4 FDecodeRichString__FRC6RefVarN21
Ref		FStripInk(RefArg rcvr, RefArg string, RefArg replacement);	// ROM 0x001fe990 FStripInk

#endif	/* __RICHSTRING_H */
