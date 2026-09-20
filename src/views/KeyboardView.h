/*
	File:		views/KeyboardView.h

	Contains:	TKeyboardView (clKeyboardView, 79) - the on-screen
				keyboard, and the two iterators that walk a keyboard's
				definition.

				A keyboard is a `keyDefinitions` array of rows.  Each row
				is itself an array: two numbers, and then three slots per
				key.

					[pitch, height,
					 legend, result, info,
					 legend, result, info, ...]

				`pitch` is how far down the next row starts and `height`
				how tall this one is drawn, both in eighths of the
				keyboard's unit cell; `legend` is what is drawn on the key
				(a character, a string, or a bitmap frame) and `result`
				what it produces.  `info` is packed:

					bits  0-7   the key's height, in eighths of the cell
					bits  8-15  the key's width, in eighths of the cell
					bits 23-24  the 3-D depth of its edge
					bits 25-27  how far its face is inset in its cell
					bit  29     the entry is a gap, not a key

				so a plain key on a keyboard whose cell is 8 by 8 is
				0x11300808 - one cell each way, a two-pixel edge - and a
				quarter-width gap is 0xe0000208.

				TRawKeyIterator walks the definition and answers each
				key's three slots.  TVisKeyIterator walks it as it is laid
				out: it keeps the pen at the end of the last key, works
				out the rectangle each key covers, and hands back the
				three rectangles a key is drawn from - its cell, its face
				and its shadow.

	Not in the DDK; reconstructed from the MP2x00 US ROM (0x000fac10-
	0x000fd1fc), each function citing its origin.
*/

#ifndef __KEYBOARDVIEW_H
#define __KEYBOARDVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif


// The bits of a key's `info` word.
enum
{
	kKeyHeightMask		= 0x000000ff,	// eighths of the cell's height
	kKeyWidthShift		= 8,			// eighths of the cell's width
	kKeyWidthMask		= 0x000000ff,
	kKeyDepthShift		= 23,			// the 3-D edge, 0-3 pixels
	kKeyDepthMask		= 0x00000003,
	kKeyInsetShift		= 25,			// the face inset in the cell, 0-7
	kKeyInsetMask		= 0x00000007,
	kKeyIsGap			= 0x20000000	// a space between keys, not a key
};


// A keyboard's definition walked key by key.  The ROM's object is 0x2c
// bytes; the offsets below are its own (a RefStruct is pointer-sized
// here, so the reconstruction's are larger).
class TRawKeyIterator
{
public:
					TRawKeyIterator(RefArg keys);		// ROM 0x000fac10 __ct__15TRawKeyIteratorFRC6RefVar
	void			Reset(void);						// ROM 0x000fad08 Reset__15TRawKeyIteratorFv
	Boolean			LoadKey(void);						// ROM 0x000fceb8 LoadKey__15TRawKeyIteratorFv - ==> whether there is no key there
	void			LoadRow(void);						// ROM 0x000fcfec LoadRow__15TRawKeyIteratorFv
	Boolean			Next(void);							// ROM 0x000fd064 Next__15TRawKeyIteratorFv
	void			CopyInto(TRawKeyIterator* other);	// ROM 0x000fb7a8 CopyInto__15TRawKeyIteratorFP15TRawKeyIterator

	// Whether the iterator has walked off the end of the definition.
	// (The ROM's is out of line in the initialised data, 0x0038ab28.)
	Boolean			Done(void) const	{ return fRowIndex >= fRowCount; }

	RefStruct		fLegend;		// +0x00  what is drawn on the key
	RefStruct		fResult;		// +0x04  what it produces
	long			fInfo;			// +0x08  the packed word above
	RefStruct		fKeys;			// +0x0c  the keyDefinitions array
	RefStruct		fRow;			// +0x10  the row being walked
	long			fKeyIndex;		// +0x14  the key within the row
	long			fRowIndex;		// +0x18
	// +0x1c is not used
	long			fRowKeys;		// +0x20  keys in this row
	long			fRowCount;		// +0x24
	long			fTotalKeys;		// +0x28  keys in the whole definition
};


// The same walk, laid out.  The ROM's object is 0x74 bytes.
class TVisKeyIterator : public TRawKeyIterator
{
public:
					TVisKeyIterator(RefArg keys, const Rect& cell, Point origin);	// ROM 0x000fd0ac __ct__15TVisKeyIteratorFRC6RefVarR5TRect6TPoint
	Boolean			Reset(void);						// ROM 0x000fd124 Reset__15TVisKeyIteratorFv
	Boolean			LoadKey(void);						// ROM 0x000fadd8 LoadKey__15TVisKeyIteratorFv
	void			LoadRow(void);						// ROM 0x000faf80 LoadRow__15TVisKeyIteratorFv
	Boolean			Next(void);							// ROM 0x000fad38 Next__15TVisKeyIteratorFv
	Boolean			SkipToStartOfNextRow(void);			// ROM 0x000fad80 SkipToStartOfNextRow__15TVisKeyIteratorFv
	Boolean			FindEnclosingKey(Point pt);			// ROM 0x000fb194 FindEnclosingKey__15TVisKeyIteratorF6TPoint
	void			CopyInto(TVisKeyIterator* other);	// ROM 0x000fd164 CopyInto__15TVisKeyIteratorFP15TVisKeyIterator

	Rect			fKeyBounds;		// +0x2c  the cell the key fills
	Rect			fKeyFace;		// +0x34  its face: the cell, grown by the 3-D depth
	Rect			fKeyShadow;		// +0x3c  its shadow: the cell, moved by the depth
	Rect			fRowBounds;		// +0x44  the row's own rectangle
	Boolean			fRowHasKeys;	// +0x4c  the row has something in it that is not a gap
	long			fFirstKey;		// +0x50  the first such key's index
	long			fLastKey;		// +0x54  and the last
	Rect			fCell;			// +0x58  the unit a key's eighths are of
	Point			fOrigin;		// +0x60  where the keyboard's top left is
	long			fX;				// +0x64  the pen: the right edge of the last key
	long			fY;				// +0x68  the top of the row
	long			fRowPitch;		// +0x6c  how far down the next row starts
	long			fRowHeight;		// +0x70  how tall this row is drawn
};


#endif	/* __KEYBOARDVIEW_H */
