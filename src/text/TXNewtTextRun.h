/*
	File:		text/TXNewtTextRun.h

	Contains:	The text run: characters in a font.

				A `TXNewtTextRun` is the Newton's own TXRun (TXRun.h) - a
				font family, a size and a face, which is all a style of
				the engine is.  The family is a NewtonScript Ref (a symbol
				out of `vars.fonts`, or the number of one of the ROM's
				fonts) held in a RefHandle of the run's own; the run
				answers its three attributes as 'font', 'size' and 'face'
				(the 'font' value being a `TXNewtFontFamilyInfo`, a little
				object the attribute list owns and frees, since a Ref must
				be held somewhere the garbage collector sees it).

				Everything it does with the characters it does through
				QuickDraw's text objects (qd/TextObject.h): a style record
				made from the three numbers (`GetNewtStyleRecord`), then
				`DrawTextOnce` to draw, `MeasureTextOnce` to measure, a
				text object's `CharToPoint`/`PointToChar` to hit-test and
				the fitted length (`GetTextObjField`) with the locale's
				word breaks (`FindWordBreaks`) to break a line.  The height
				(ascent, descent, leading) is cached in the run and thrown
				away whenever an attribute changes.

				A new run takes its font from the user's preference
				(`userFont`), which is what makes a new document's text
				the font the writer chose.

				The ROM's object is 0x20 bytes.

	Reconstructed from the MP2x00 US ROM (0x0023f648-0x0024054c); each
	function cites its origin.
*/

#ifndef __TXNEWTTEXTRUN_H
#define __TXNEWTTEXTRUN_H

#ifndef __TXRUN_H
#include "TXRun.h"
#endif

struct StyleRecord;

const long	kTXTextRunClassId	= 0x74657874;		// 'text'


// The value of a run's 'font' attribute: the family, where the garbage
// collector can see it.  An attribute list owns one and deletes it with
// the entry.  The ROM's object is 8 bytes.
class TXNewtFontFamilyInfo : public TXVirtualObject
{
public:
					TXNewtFontFamilyInfo(RefArg family);			// ROM 0x00240158 __ct__20TXNewtFontFamilyInfoFRC6RefVar
	virtual			~TXNewtFontFamilyInfo();						// ROM 0x002401c0 __dt__20TXNewtFontFamilyInfoFv

	RefStruct		fFamily;		// +0x04
};


class TXNewtTextRun : public TXRun
{
public:
					TXNewtTextRun();								// ROM 0x0023f648 __ct__13TXNewtTextRunFv

	// TXAttrObject
	virtual TXAttrObject* CreateNew(void) const;					// ROM 0x0023f6fc CreateNew__13TXNewtTextRunCFv
	virtual long	GetClassId(void) const;							// ROM 0x0023fe50 GetClassId__13TXNewtTextRunCFv - 'text'
	virtual void	GetAttributesValues(TXAttrValues* values);		// ROM 0x00240434 GetAttributesValues__13TXNewtTextRunFP12TXAttrValues
	virtual Boolean	IsEqual(const TXAttrObject* other) const;		// ROM 0x0023f850 IsEqual__13TXNewtTextRunCFPC12TXAttrObject
	virtual void	Assign(const TXAttrObject* other);				// ROM 0x00240384 Assign__13TXNewtTextRunFPC12TXAttrObject
	virtual Boolean	GetAttributeValue(TXAttrTag tag, void* value) const;	// ROM 0x002403d8 GetAttributeValue__13TXNewtTextRunCFUlPv
	virtual void	SetAttributeValue(TXAttrTag tag, const void* value);	// ROM 0x002404d8 SetAttributeValue__13TXNewtTextRunFUlPCv
	virtual Ref		GetNSObject(void) const;						// ROM 0x00240104 GetNSObject__13TXNewtTextRunCFv - the font spec
	virtual void	SetNSObject(RefArg obj);						// ROM 0x00240114 SetNSObject__13TXNewtTextRunFRC6RefVar
	virtual Boolean	GetCommonAttrValue(TXAttrTag tag, void* value) const;	// ROM 0x0023f7b0 GetCommonAttrValue__13TXNewtTextRunCFUlPv
	virtual unsigned long GetAttributeFlags(TXAttrTag tag) const;	// ROM 0x0023f8c4 GetAttributeFlags__13TXNewtTextRunCFUl
	// 'face' with `how` 4 adds the bits, with 8 takes them away; anything
	// else is set.
	virtual void	UpdateAttribute(TXAttrTag tag, const void* value, long how);	// ROM 0x0023f704 UpdateAttribute__13TXNewtTextRunFUlPCvl

	// TXRun
	virtual Boolean	IsTextRun(void) const;							// ROM 0x0024037c IsTextRun__13TXNewtTextRunCFv
	virtual void	GetHeightInfo(int* ascent, int* descent, int* leading);	// ROM 0x0023f9f4 GetHeightInfo__13TXNewtTextRunFPiN21
	virtual void	PixelToChar(const TXLineRunDisplayInfo& info, Fixed pixel, TXOffsetRange* range);	// ROM 0x0023fc34 PixelToChar__13TXNewtTextRunFRC20TXLineRunDisplayInfolP13TXOffsetRange
	virtual Fixed	CharToPixel(const TXLineRunDisplayInfo& info, long offset);	// ROM 0x0023fd58 CharToPixel__13TXNewtTextRunFRC20TXLineRunDisplayInfol
	virtual void	Draw(const TXLineRunDisplayInfo& info, Fixed x, const Rect& line, int baseline);	// ROM 0x0023faa4 Draw__13TXNewtTextRunFRC20TXLineRunDisplayInfolRC4Recti
	virtual Fixed	FullJustifPortion(const TXLineRunDisplayInfo& info);	// ROM 0x0023fe5c FullJustifPortion__13TXNewtTextRunFRC20TXLineRunDisplayInfo
	virtual long	VisibleLen(const UniChar* text, long count);	// ROM 0x0023feb0 VisibleLen__13TXNewtTextRunFPCUsl - less the trailing spaces and line ends
	virtual Fixed	MeasureWidth(const TXLineRunDisplayInfo& info);	// ROM 0x0023fb94 MeasureWidth__13TXNewtTextRunFRC20TXLineRunDisplayInfo
	virtual long	LineBreak(const UniChar* text, long count, long start, Fixed* width, Boolean mayCutWord, long* length);	// ROM 0x0023fef4 LineBreak__13TXNewtTextRunFPCUslT2PlUcT4

	virtual void	AddFace(long face, long* faces);				// ROM 0x0024053c AddFace__13TXNewtTextRunFlPl (vtable +0x84)
	void			RemoveFace(long face, long* faces) const;		// ROM 0x0024054c RemoveFace__13TXNewtTextRunCFlPl

	// The QuickDraw style the run draws in.  The caller disposes of the
	// record's pattern (there never is one).
	void			GetNewtStyleRecord(StyleRecord* style);			// ROM 0x0023f908 GetNewtStyleRecord__13TXNewtTextRunFP11StyleRecord

	RefStruct		fFamily;		// +0x08  a symbol, or a ROM font's number
	long			fSize;			// +0x0c
	long			fFace;			// +0x10
	long			fAscent;		// +0x14  -1: the height is to be worked out again
	long			fDescent;		// +0x18
	long			fLeading;		// +0x1c
};


// A font spec (a frame) as an attribute list - 'font', and 'size' and
// 'face' for the slots that are integers.  The caller deletes the list.
TXAttrValues*	TXGetRunAttrValues(RefArg fontSpec);				// ROM 0x00240208 TXGetRunAttrValues__FRC6RefVar

#endif	/* __TXNEWTTEXTRUN_H */
