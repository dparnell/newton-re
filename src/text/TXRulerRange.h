/*
	File:		text/TXRulerRange.h

	Contains:	Which paragraph of the text is laid out against which
				ruler.

				A `TXRulerRange` is the TXObjectRange (TXObjectRange.h) of
				a document's rulers (TXRuler.h), plus the text itself
				(to find where paragraphs start and end) and a *pending
				ruler*.  When the text is empty, or the caret sits at the
				very end after a line break, the next character typed
				starts a paragraph that has no range yet - but a ruler
				slip must still show it and may change it.  That ruler
				is `fDefaultRuler`: `GetPendingRuler` answers it for such
				a place (brought up to date from the ruler before it the
				first time it is asked since it was last invalidated),
				and `OffsetToObject`/`UpdateRangeObjects` answer out of
				it there instead of out of the ranges.  The ROM's object
				is 0x2c bytes.

				Every range must start at the beginning of a paragraph:
				`ValidateRuler` merges a range that starts mid-paragraph
				into the one before, and `ValidateRulerRange` does so for
				the two ends of an edited stretch.

				`TXGetParagStartOffset`/`TXGetParagEndOffset` measure a
				paragraph from an offset through the text's own search
				(TXChars' `SearchCharBack`/`SearchChar` with 0x0c, which
				is "any line break"), at most 0x7fff characters away.

	Reconstructed from the MP2x00 US ROM (0x00242b7c-0x00242c28,
	0x00242c68-0x0024323c); each function cites its origin.
*/

#ifndef __TXRULERRANGE_H
#define __TXRULERRANGE_H

#ifndef __TXOBJECTRANGE_H
#include "TXObjectRange.h"
#endif
#ifndef __TXOFFSET_H
#include "TXOffset.h"
#endif
#ifndef __TXCHARS_H
#include "TXChars.h"
#endif
#ifndef __TXRULER_H
#include "TXRuler.h"
#endif


// How far back from `at` its paragraph starts (at most 0x7fff).
long	TXGetParagStartOffset(TXChars* chars, long at);			// ROM 0x00242b7c TXGetParagStartOffset__FP7TXCharsl
// How far on from `at` its paragraph ends, its line break included (at
// most 0x7fff; to the end of the text when it has no break).
long	TXGetParagEndOffset(TXChars* chars, long at);			// ROM 0x00242bc8 TXGetParagEndOffset__FP7TXCharsl


class TXRulerRange : public TXObjectRange
{
public:
					TXRulerRange(TXChars* chars, TXRuler* defaultRuler);	// ROM 0x00242c68 __ct__12TXRulerRangeFP7TXCharsP7TXRuler
	virtual			~TXRulerRange();								// ROM 0x00242cc4 __dt__12TXRulerRangeFv

	virtual NewtonErr FreeData(Boolean compact);					// ROM 0x00242e48 FreeData__12TXRulerRangeFUc
	virtual TXAttrObject* OffsetToObject(TXOffset offset, Boolean atStart);	// ROM 0x00242ff8 OffsetToObject__12TXRulerRangeF8TXOffset
	virtual unsigned long UpdateRangeObjects(TXOffset at, long length, const TXAttrValues* values, long how);	// ROM 0x0024302c UpdateRangeObjects__12TXRulerRangeFlT1PC12TXAttrValuesT1

	// The two ends widened to whole paragraphs: the start back to its
	// paragraph's first character, the end on past its line break
	// (unless it already sits just after one at a boundary).
	void			CharRangeToParagRange(TXOffsetPos* start, TXOffsetPos* end) const;	// ROM 0x00242d14 CharRangeToParagRange__12TXRulerRangeCFP8TXOffsetT1
	// What replacing start..end with text must take in as well to keep
	// the paragraphs whole; the pending ruler through `pending`.
	long			GetReplaceExtraChars(TXOffset start, TXOffset end, TXAttrObject** pending);	// ROM 0x00242dac GetReplaceExtraChars__12TXRulerRangeFlT1PP12TXAttrObject
	TXRuler*		GetPendingRuler(TXOffset at, long length);		// ROM 0x00242eac GetPendingRuler__12TXRulerRangeFlT1 - the ruler of the paragraph not yet typed, when that is where `at` is; else nil
	void			InvalidatePendingRuler(TXOffset at, long length);	// ROM 0x00242fac InvalidatePendingRuler__12TXRulerRangeFlT1
	void			NukePendingRuler(void);							// ROM 0x00242fec NukePendingRuler__12TXRulerRangeFv
	Boolean			ValidateRuler(long index);						// ROM 0x0024309c ValidateRuler__12TXRulerRangeFl - ==> true when range `index` already starts a paragraph
	long			ValidateRulerRange(TXOffset at, long length);	// ROM 0x00243138 ValidateRulerRange__12TXRulerRangeFlT1

	TXChars*		fChars;			// +0x20  the text
	TXRuler*		fDefaultRuler;	// +0x24  the pending ruler
	Boolean			fPendingInvalid;	// +0x28  the pending ruler must be brought up to date before it is next answered
};


#endif	/* __TXRULERRANGE_H */
