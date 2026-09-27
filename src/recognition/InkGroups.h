/*
	File:		recognition/InkGroups.h

	Contains:	Strokes nobody read grouped into words of ink.

				A stroke that no recogniser claims is *expired*
				(`HandleExpiredStroke`): the stroke world keeps it
				(`StrokeCentral::AddExpiredStroke`) and hands it to
				`IGGroupAndCompressStrokes`, which keeps a *group* of the
				expired strokes not yet placed (`GroupDataStruct`: the
				stroke units, and a 256-bit list of the ones still to be
				segmented) and puts them through ParaGraph's word
				segmenter (`WordSegment.h`) as a trace.  Each time a word
				is settled - the segmenter has moved on past it, the line
				has ended, or half a second has passed with nothing
				written (`ExpireAll`, the `final` call) - its strokes are
				taken out of the group and handed back
				(`StrokeCentral::IGCompressGroup`) to become one piece of
				ink: an aeInkWord or aeRawInk command to the view under
				them, or, while a script's strokes are being read
				(`Recognize`), a stroke bundle for its correct info.

				The GC layer (`GCGroupStrokes` and its helpers) is shared
				with the cursive recogniser, which calls it with *word
				descriptors* to keep the words it has read in step with
				the segmenter; the ink grouping calls it without, and
				NOT YET RECONSTRUCTED are the word descriptors themselves
				(`GCWordDescr*`, `GCWriteNewGroupResults`,
				`GCSortWordDescByStrokesOrder`, `GCRecSegmentSetGroupFlags`,
				`GCGroupResultsCopyFlags`), which only that recogniser's
				reading reaches.

	Reconstructed from the MP2x00 US ROM (0x000ea554-0x000eb360,
	0x000d55c4-0x000d5cc0, 0x000d83a0-0x000d84f8, 0x0006583c-0x00065b2c,
	0x00144c6c-0x001454fc); each function cites its origin.
*/

#ifndef __INKGROUPS_H
#define __INKGROUPS_H

#ifndef __WORDSEGMENT_H
#include "WordSegment.h"
#endif

class TStroke;
class TStrokeUnit;
struct GCWordDescrType;

// The group of expired strokes (0x84 bytes, and four for each stroke
// past twenty, in the ROM).
struct GroupDataStruct
{
	Handle			fGRes;			// +00  the segmenter's state and results (GCResizeAndLockGResHandle)
	ULong			fMarked;		// +04  how many strokes are in fStrokes
	ULong			fNumGrouped;	// +08  strokes given to the segmenter so far
	UByte			fStrokes[32];	// +0c  the strokes still to segment, one bit each, first stroke in bit 7
	ULong			fCount;			// +2c
	ULong			fCapacity;		// +30  (20 more at a time)
	TStrokeUnit*	fUnits[1];		// +34
};

// What GCGroupStrokes is asked (0x3c bytes in the ROM).
struct GCGroupParmStruct
{
	short		fSpacing;			// +00  the writer's letter spacing
	UByte		fField02;			// +02
	UByte		fField03;			// +03
	short		fLineAtATime;		// +04  the compress waits for this many words (0: none) when the 'lineAtATime preference is set
	short		fSureLevel;			// +06
	long		fInProgress;		// +08
	Handle		fGRes;				// +0c
	short		fNumStrokes;		// +10  strokes given to the segmenter
	short		fNext;				// +12
	UByte		fStrokes[32];		// +14  the strokes to segment
	short		fJoinX;				// +34  a word being read again after a dash: where the line it continues ended (the cursive recogniser's)
	short		fJoinY;				// +36
	UByte		fMerged;			// +38
	UByte		fPad[3];
};

// The segmenter's state and results in one block: its capacity in words,
// the control and results records and the words.
struct GResBlock
{
	ULong			fCapacity;
	ws_control_type	fControl;
	ws_results_type	fResults;
	ws_word_type	fWords[1];
};

// A list of strokes as a trace, the points in eighths of a pixel from the
// strokes' top left, each stroke after a pen-up point; nStrokes and
// nPoints (the pen-ups counted) answered.
void	GetTraceFromStrokes(TStroke** strokes, PS_point_type** trace, short* nStrokes, short* nPoints);		// ROM 0x0006583c GetTraceFromStrokes__FPP7TStrokePP13PS_point_typePsT3 - into a new trace
void	NewGetTraceFromStrokes(TStroke** strokes, PS_point_type** trace, short* nStrokes, short* nPoints);	// ROM 0x00065848 NewGetTraceFromStrokes__FPP7TStrokePP13PS_point_typePsT3 - into *trace, made when nil

long	GCGroupStrokes(GCWordDescrType* words, PS_point_type* trace, short nPoints, ULong final, ULong param5, GCGroupParmStruct* parm);	// ROM 0x000d55c4 GCGroupStrokes__FP15GCWordDescrTypeP13PS_point_typesUiT4P17GCGroupParmStruct
long	GCResizeAndLockGResHandle(Handle* gres, ws_control_type** control, ws_results_type** results, ULong words);	// ROM 0x000d5994 GCResizeAndLockGResHandle__FPUlPPvT2Ul
long	GCUnlockGResHandle(Handle gres);																// ROM 0x000d5af0 GCUnlockGResHandle__FUl
long	GCDisposeGResHandle(Handle* gres);																// ROM 0x000d5b0c GCDisposeGResHandle__FPUl
long	GCTryToRemoveLastWords(ws_results_type* results, UByte* strokes, GCWordDescrType* words, short last, short* next);	// ROM 0x000d5cc0 GCTryToRemoveLastWords__FP15ws_results_typePUcP15GCWordDescrTypesPs
long	GCGetRealStrokeIndex(UByte* strokes, short index);												// ROM 0x000d83a0 GCGetRealStrokeIndex__FPUcs - the set bits before the index
long	GCGetNumOfStrokesInList(UByte* strokes);														// ROM 0x000d8418 GCGetNumOfStrokesInList__FPUc
void	GCGetStrokeFromTrace(PS_point_type* trace, short nPoints, short index, PS_point_type** stroke, short* count);	// ROM 0x000d844c GCGetStrokeFromTrace__FP13PS_point_typesT2PP13PS_point_typePs

ULong	IGGroupAndCompressStrokes(ULong strokeWorld, TStrokeUnit* unit, ULong spacing, UChar final, Handle* group);	// ROM 0x000ea554 IGGroupAndCompressStrokes__FUlP11TStrokeUnitT1UcPPPc - ==> whether any strokes were compressed
long	IGCompressStrokes(ULong strokeWorld, Handle group, GroupDataStruct** data, GCGroupParmStruct* parm, short base, ULong final, ULong* compressed);	// ROM 0x000eabec IGCompressStrokes__FUlPPcPP15GroupDataStructP17GCGroupParmStructsUiPUi
void	IGAllocTrace(GroupDataStruct* data, UByte* strokes, short base, PS_point_type** trace, short* nPoints);		// ROM 0x000eafd4 IGAllocTrace__FP15GroupDataStructPUcsPP13PS_point_typePs
long	IGNewGroupData(Handle* group);																	// ROM 0x000eb120 IGNewGroupData__FPPPc
void	IGDisposeGroupData(Handle* group);																// ROM 0x000eb16c IGDisposeGroupData__FPPPc
GroupDataStruct*	IGLockGroupData(Handle group);													// ROM 0x000eb1ac IGLockGroupData__FPPc
long	IGUnlockGroupData(Handle group);																// ROM 0x000eb1b0 IGUnlockGroupData__FPPc
void	IGGetStrokesQueue(GroupDataStruct* data, TStrokeUnit*** units, ULong* count);					// ROM 0x000eb1b4 IGGetStrokesQueue__FPvPPP11TStrokeUnitPUl
long	IGAddSroke(Handle* group, GroupDataStruct** data, TStrokeUnit* unit);							// ROM 0x000eb1e8 IGAddSroke__FPPPcPP15GroupDataStructP11TStrokeUnit
ULong	IGGetNumOfSrokes(GroupDataStruct* data);														// ROM 0x000eb2f0 IGGetNumOfSrokes__FP15GroupDataStruct
ULong	IGGetRealStrokeIndex(GroupDataStruct* data, ULong index);										// ROM 0x000eb300 IGGetRealStrokeIndex__FP15GroupDataStructUl
void	IGCompressGroup(ULong strokeWorld, TStrokeUnit** units);										// ROM 0x00144c6c IGCompressGroup__FUlPP11TStrokeUnit
ULong	IGGetCompressBufSize(ULong strokeWorld);														// ROM 0x00144c70 IGGetCompressBufSize__FUl - (40)
void	WRecEndInkStrokeGroup(TStrokeUnit** units);														// ROM 0x00144cc8 WRecEndInkStrokeGroup__FPP11TStrokeUnit

#endif	/* __INKGROUPS_H */
