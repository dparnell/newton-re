/*
	File:		recognition/WordDescriptors.h

	Contains:	The cursive recogniser's word descriptors: what it keeps
				of each word the segmenter has found while the word is
				read.

				A cursive unit ('STXR', `TStrXrUnit`) holds its writing's
				words in one block of eight *word descriptors*
				(`GCNewRecSegment`), a doubly linked list threaded through
				the block by halfword indices (0xffff: none; a slot whose
				two links are both nought is free).  Each descriptor
				names the strokes of its word - a run from `fFirst` to
				`fLast` and up to eight more (`fExtra`, nought-ended),
				numbered in the unit's own list of strokes - and carries
				what the segmenter said of the word (the line it is on,
				where it starts, `ws_word_info_type`: the gaps inside it
				the segmenter was least sure of), what the recogniser
				made of it (`fRecResults`: the readings; `fLearning`:
				what the learning is to be given) and flags saying how far
				it has got:

				  0x002  settled by the segmenter (its words may still be
				         taken back while fewer than lineAtATime follow)
				  0x004  to be read now
				  0x008  read: the readings are alternatives (0x28)
				  0x010  read (0x50)
				  0x080  ends in a dash: the next word may continue it
				  0x100  not a contiguous run of the line's strokes
				  0x200  the low level failed
				  0x400  the xr reader failed
				  0x800  out of memory

				`GCWriteNewGroupResults` turns the segmenter's words into
				descriptors (`GCWordDescWriteGroupResults` keeping one
				whose strokes are unchanged, throwing away one whose
				strokes have moved to another word, and joining a word to
				the one before it when that ended in a dash -
				`GCMergeWordDesc`), and `GCWDGetTrace` makes the trace a
				descriptor is read from: its strokes out of the unit's,
				with a word continued after a dash moved up to the end of
				the line it continues (`GCMergeLinesAndRemoveDash`, the
				dash itself taken out).

	Reconstructed from the MP2x00 US ROM (0x000d5ba4-0x000d5cc0,
	0x000d5ff0-0x000d635c, 0x000d6d24-0x000d6e84, 0x000d7384-0x000d8300,
	0x000d84f8-0x000d87f0, 0x0019f15c-0x0019f2ac, 0x0019f464-0x0019f50c);
	each function cites its origin.
*/

#ifndef __WORDDESCRIPTORS_H
#define __WORDDESCRIPTORS_H

#ifndef __WORDSEGMENT_H
#include "WordSegment.h"
#endif

struct rc_type;
struct rec_w_type;
struct GCGroupParmStruct;

// The gaps inside a word the segmenter was least sure of: the stroke
// after each (numbered from one, or from nought), and how sure it was.
struct ws_word_info_type
{
	UByte		fStrokes[8];		// +00  nought-ended
	SByte		fSure[8];			// +08
};

// A word descriptor (0x50 bytes in the ROM; eight to a block).
struct GCWordDescrType
{
	ULong				fFlags;			// +00  (above)
	Handle				fRecResults;	// +04  the readings (GCWDWriteRecResults)
	Handle				fLearning;		// +08  what the learning is given
	short				fLineHeight;	// +0c  the line's height (GetWSBorder)
	short				fBaseLine;		// +0e  its middle
	short				fNewLine;		// +10  whether the word starts a line
	short				fField12;		// +12  (rc +0xac)
	UByte				fInkBox[8];		// +14  (rc +0xd8: the ink box as read)
	short				fBase[4];		// +1c  the base line as read (rc +0xea-0xf0)
	short				fPrevBase[4];	// +24  the base line of the word before (GCWDWritePrevBaseLine)
	UByte				fFirst;			// +2c  its strokes: a run
	UByte				fLast;			// +2d
	UByte				fExtra[8];		// +2e  and more, nought-ended
	UShort				fPrev;			// +36  0xffff: none
	UShort				fNext;			// +38
	short				fJoinX;			// +3a  continued after a dash: where the line it continues ended ...
	short				fJoinY;			// +3c  ... and its y (both relative once joined)
	UByte				fMerged;		// +3e  joined: how many of its strokes are the first part's
	UByte				fField3F;		// +3f
	ws_word_info_type	fInfo;			// +40
};

enum
{
	kWordDescrCount = 8,
	kWordDescrNone = 0xffff
};

// DEVIATION: the block and a descriptor are sized from sizeof on the
// host (the ROM's are 0x280 and 0x50 bytes: two words are handles).
Handle	GCNewRecSegment(void);													// ROM 0x000d7384 GCNewRecSegment__Fv - a block of eight free descriptors
GCWordDescrType*	GCGetFirstWordDescriptor(GCWordDescrType* words);				// ROM 0x000d73e4 GCGetFirstWordDescriptor__FP15GCWordDescrType
GCWordDescrType*	GCGetLastWordDescriptor(GCWordDescrType* words);				// ROM 0x000d7f78 GCGetLastWordDescriptor__FP15GCWordDescrType
GCWordDescrType*	GCGetNextWordDescriptor(GCWordDescrType* words, GCWordDescrType* word);	// ROM 0x000d84f8 GCGetNextWordDescriptor__FP15GCWordDescrTypeT1
GCWordDescrType*	GCGetPrevWordDescriptor(GCWordDescrType* words, GCWordDescrType* word);	// ROM 0x000d8524 GCGetPrevWordDescriptor__FP15GCWordDescrTypeT1
UShort	GCGetWordDescriptorIndex(GCWordDescrType* words, GCWordDescrType* word);		// ROM 0x000d8550 GCGetWordDescriptorIndex__FP15GCWordDescrTypeT1
GCWordDescrType*	GCNewWordDescriptor(GCWordDescrType* words);					// ROM 0x000d8588 GCNewWordDescriptor__FP15GCWordDescrType - a free slot put at the end of the list
long	GCWordDescriptorDispose(GCWordDescrType* words, GCWordDescrType* word);		// ROM 0x000d8650 GCWordDescriptorDispose__FP15GCWordDescrTypeT1
// The first descriptor whose flags equal `flags` (`exact`) or share a
// bit with them; nil for none.
GCWordDescrType*	GCGetWordDescWithFlags(GCWordDescrType* words, ULong flags, ULong exact);	// ROM 0x000d7438 GCGetWordDescWithFlags__FP15GCWordDescrTypeUlUi
long	GCCountWordDescWithFlags(GCWordDescrType* words, ULong flags, ULong exact);	// ROM 0x000d876c GCCountWordDescWithFlags__FP15GCWordDescrTypeUlUi
long	GCChangeFlagForFirstNWordDescr(GCWordDescrType* words, ULong flags, short n, ULong set);	// ROM 0x000d86f0 GCChangeFlagForFirstNWordDescr__FP15GCWordDescrTypeUlsUi
long	GCSortWordDescByStrokesOrder(GCWordDescrType* words);						// ROM 0x000d74a8 GCSortWordDescByStrokesOrder__FP15GCWordDescrType
Boolean	GCIsWordDescContainsStroke(GCWordDescrType* word, UByte stroke);			// ROM 0x000d75d8 GCIsWordDescContainsStroke__FP15GCWordDescrTypeUc
long	GCWordDescWriteGroupResults(GCWordDescrType* words, UByte first, UByte last, UByte* extra, short joinX, short joinY, long lineHeight, long baseLine, long newLine, ULong flags, ws_word_info_type* info);	// ROM 0x000d7640 GCWordDescWriteGroupResults__FP15GCWordDescrTypeUcT2PUcsT5iN27UlP17ws_word_info_type
GCWordDescrType*	GCMergeWordDesc(GCWordDescrType* words, GCWordDescrType* a, GCWordDescrType* b);	// ROM 0x000d798c GCMergeWordDesc__FP15GCWordDescrTypeN21
long	GCRecSegmentSetGroupFlags(GCWordDescrType* words, short lineAtATime, ULong final);	// ROM 0x000d7bbc GCRecSegmentSetGroupFlags__FP15GCWordDescrTypesUi
long	GCWDWriteRecResults(GCWordDescrType* word, rc_type* rc, rec_w_type* readings, Handle learning, long err, void* splitInfo, ULong alternatives);	// ROM 0x000d7c50 GCWDWriteRecResults__FP15GCWordDescrTypeP7rc_typeP10rec_w_typeUliP20RecwordSplitInfoTypeUi
long	GCWDWritePrevBaseLine(short a, short b, short c, short d, GCWordDescrType* word);	// ROM 0x000d7e40 GCWDWritePrevBaseLine__FsN31P15GCWordDescrType
long	GCWDFillBaseLineParameters(GCWordDescrType* word, rc_type* rc, PS_point_type* trace);	// ROM 0x000d7ea8 GCWDFillBaseLineParameters__FP15GCWordDescrTypeP7rc_typeP13PS_point_type
long	GCWDGetTrace(PS_point_type* trace, GCWordDescrType* word, UByte* strokes, PS_point_type** wordTrace, short* nPoints, ULong* copied);	// ROM 0x000d7fd8 GCWDGetTrace__FP13PS_point_typeP15GCWordDescrTypePUcPP13PS_point_typePsPUi
long	GCWDRemoveStrokesFromList(GCWordDescrType* word, UByte* strokes);			// ROM 0x000d8300 GCWDRemoveStrokesFromList__FP15GCWordDescrTypePUc
// The line a word continued after a dash is on moved up to the end of
// the line before, and the dash taken out.  ==> which stroke was the
// dash, -1 for nothing to do.
long	GCMergeLinesAndRemoveDash(PS_point_type* trace, short* nPoints, short dx, short dy, UByte merged, long callerR8);	// ROM 0x000d6d24 GCMergeLinesAndRemoveDash__FP13PS_point_typePssT3Uc

// The readings of a word, in a handle: the header, then the readings
// (rec_w_type), then the split information.
struct GCRecResults
{
	rec_w_type*	fWords;				// +00  (set when locked)
	void*		fSplit;				// +04  (nil for none)
	short		fCount;				// +08
	short		fSplitSize;			// +0a
};
GCRecResults*	GCLockRecResultsHandle(Handle results);						// ROM 0x000d7f24 GCLockRecResultsHandle__FUl
long	GCUnlockRecResultsHandle(Handle results);									// ROM 0x000d7fcc GCUnlockRecResultsHandle__FUl

// The segmenter's words turned into descriptors.  ==> 0, -1, or -4 when
// a descriptor could not be made.  (The ROM declares it answering a
// GCWordDescrType*, which is the error code.)
long	GCWriteNewGroupResults(GCWordDescrType* words, ws_results_type* results, GCGroupParmStruct* parm, ULong final);	// ROM 0x000d5ff0 GCWriteNewGroupResults__FP15GCWordDescrTypeP15ws_results_typeP17GCGroupParmStructUi
// Every word of the segmenter's whose strokes a descriptor being read
// holds marked compressed (4), so it is not handed out again.
long	GCGroupResultsCopyFlags(ws_results_type* results, GCWordDescrType* words, short numStrokes);	// ROM 0x000d5ba4 GCGroupResultsCopyFlags__FP15ws_results_typeP15GCWordDescrTypes

// What the segmenter says of a word's line: its height, its middle, and
// whether the word starts a new one.  ==> 0; 1 (and a new line) when
// it cannot say.
long	GetWSBorder(long word, ws_results_type* results, long* lineHeight, long* baseLine, long* newLine);	// ROM 0x0019f464 GetWSBorder__FiP15ws_results_typePiN23
// The gaps inside a word the segmenter was least sure of, added to the
// info (numbered from one, or from nought with `fromZero`).  ==> 0, 1
// when the info is full.
long	SetStrokeSureValuesWS(long fromZero, long word, ws_results_type* results, ws_word_info_type* info);	// ROM 0x0019f15c SetStrokeSureValuesWS__FiT1P15ws_results_typeP17ws_word_info_type

#endif	/* __WORDDESCRIPTORS_H */
