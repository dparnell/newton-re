/*
	File:		frames/RichString.h

	Contains:	TRichString, the ROM's view of a NewtonScript string: its
				UniChar text and, in a rich string, the ink words stored
				after the text (a trailer word ends the object: text
				length << 4 | 1).  The string functions work through it.

	NOT YET RECONSTRUCTED: the ink - a rich string's text is read and
	written (its format and lengths are computed as the ROM computes them)
	but MungeRange keeps no ink data and the ink word functions
	(GetInkData, NumInkWords, ...) are not here; CompareUnicodeText's
	collation (the sort tables) is a plain, case-folding comparison.

	The DDK has no header for TRichString; the layout is the ROM's (0x28).
*/

#ifndef __RICHSTRING_H
#define __RICHSTRING_H

#ifndef __OBJECTS_H
#include "objects.h"
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

// the collation compare the string functions use: < 0, 0, > 0; exact
// compares cases, else letters are folded
int		CompareUnicodeText(const UniChar* a, long aLength, const UniChar* b, long bLength, Boolean exact);

#endif	/* __RICHSTRING_H */
