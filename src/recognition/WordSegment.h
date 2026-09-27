/*
	File:		recognition/WordSegment.h

	Contains:	ParaGraph's word segmenter: where one word of writing ends
				and the next begins.

				This is how the machine groups strokes that no recogniser
				read - ink - into words (`InkGroups.h` hands it the strokes
				one at a time).  It is ParaGraph's code, part of the same
				library as the cursive recogniser (`ParaGraph.h`), and like
				the rest of that library it works on a *trace*: every
				stroke's points one after another as `PS_point_type`s, in
				eighths of a pixel, a stroke ended by a pen-up point whose y
				is -1.

				A call to `WordStrokes` gives it one more stroke.  It keeps
				the state of the line being written in a `ws_data_type`
				(0x18cc bytes), and the heart of that is a *histogram* of
				the line along x, one byte per four trace units: every
				stroke is laid into it slanted by the writing's slope
				(`WS_HistTheStroke`: the pen's path is run along its
				length, each step adding to the column it falls in, and the
				bottoms of downstrokes adding more), so that a column with
				nothing in it is space and a column with a lot in it is the
				body of a letter.  The columns the strokes' *cores* fall in
				are marked (0x80); `WS_CalcGaps` walks the line and makes a
				*gap* of each run of unmarked columns - its middle, where it
				starts, how much of it is truly empty and how wide it is -
				and `WS_SegmentWords` asks, for each gap, whether it is the
				space between two words.  That question is answered by a
				little net (`NeuroNetWS`: eleven measurements of the gap
				and the line, turned through an 11x11 matrix for each of
				two classes and scored against 120 trained Gaussian cells
				each - the "is a space" and "is not a space" likelihoods -
				the difference, times five, being how sure it is), whose
				tables come from the ROM (`WordSegmentTables.cpp`,
				generated).  What comes back (`ws_results_type`) is a list
				of words, each a run of stroke numbers with the gap's
				sureness beside each.

				The line's measurements are kept running from stroke to
				stroke (the line height, the mean letter width, the word
				distance, the slope) and learnt across lines
				(`InitForNewLine` averages the old line's into the new);
				`WS_NewLine` decides from where a stroke sits whether a new
				line has been started, in which case the old one is
				finished off.

				Everything is integer arithmetic in trace units; a division
				is C's, towards nought, as the ROM's `__rt_sdiv` is.  The
				ROM keeps the state in engine handles (`HWRMemory*`) and
				locks them for each call; DEVIATION: the host's structures
				hold host pointers where the ROM's hold 32-bit ones, so they
				are sized with `sizeof` rather than the ROM's constants.

	Reconstructed from the MP2x00 US ROM (0x0026e8e8-0x00271da0,
	0x001286b4-0x00128ab8); each function cites its origin.
*/

#ifndef __WORDSEGMENT_H
#define __WORDSEGMENT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

// A point of a trace, in eighths of a pixel; y -1 is a pen-up, which ends
// a stroke.
struct PS_point_type
{
	short	x;
	short	y;
};

// A word as the segmenter hands it back (0x14 bytes).
struct ws_word_type
{
	UByte	fFlags;			// +00  1 the line was closed by a new line, 4 compressed already (its strokes are done), 8 a dash may end it, 0x10 the line began with a space gesture, 0x20 more than eight words written
	UByte	fLine;			// +01  the line it is on
	UByte	fWord;			// +02  its number on the line (from 1; 0: an empty slot)
	UByte	fGapRatio;		// +03  the gap after it as a percentage of twice the word distance (100 at most)
	UByte	fWordDist;		// +04  the line's word-distance level
	SByte	fSlope;			// +05  the writing's slope
	UByte	fFirst;			// +06  its first stroke in the results' stroke list
	UByte	fCount;			// +07  how many strokes
	short	fLineY;			// +08  the line's y under it
	short	fLineHeight;	// +0a
	short	fLeft;			// +0c
	short	fRight;			// +0e
	short	fLetterWidth;	// +10  the line's mean letter width
	short	fField12;		// +12
};

// What the segmenter answers (0x1fc bytes in the ROM).
struct ws_results_type
{
	UByte			fNumWords;		// +000
	UByte			fLineFirst;		// +001  the first word of the line last segmented
	UByte			fNumStrokes;	// +002  strokes put in words so far
	ws_word_type*	fWords;			// +004
	UByte			fStrokes[0xfa];	// +008  the words' stroke numbers
	SByte			fSure[0xfa];	// +102  how sure the gap after each was
};

// What the segmenter is asked (0x24 bytes in the ROM).
struct ws_control_type
{
	long	fNumPoints;		// +00  the stroke's points (nought: none)
	ULong	fFlags;			// +04  1 the writing is finished (close the line), 2 ..., 4 a space gesture may start a line, 0x80 throw the state away
	long	fMode;			// +08  segment the line as it goes when positive (3)
	long	fSureLevel;		// +0c  how sure a gap must be to end a word
	long	fWordDistLevel;	// +10  the writer's spacing, 1-10 (nought: learnt)
	long	fLineDist;		// +14  a fixed line distance (nought: worked out)
	long	fDefLineHeight;	// +18  (0x50)
	Handle	fMem;			// +1c  the state (a ws_memory_header_type)
	long	fField20;		// +20
};

// One of the gaps of a line (0x10 bytes).
struct ws_gap_type
{
	short	fMid;			// +00
	short	fStart;			// +02
	short	fZeroStart;		// +04  where its empty columns start
	short	fSize;			// +06  how wide it counts as
	short	fSize2;			// +08  ... as the word distance sees it
	short	fZeroWidth;		// +0a  its empty columns, in trace units
	short	fWidth;			// +0c
	UByte	fFlags;			// +0e  2 merged with a neighbour
	SByte	fSure;			// +0f  the net's answer when it was not a space
};

// A stroke of the line as the segmenter measured it (8 bytes).
struct ws_stroke_box
{
	short	x0;
	short	x1;
	short	y0;
	short	coreRight;
};

// The line being segmented (0x18cc bytes in the ROM).  The fields are
// named by their ROM offsets where their meaning is not plain.
struct ws_data_type
{
	PS_point_type*	fTrace;			// +000  the stroke being added
	long	f004_endDist;				// +004  where a line is cut when segmented early
	long	f008_wordDistLevel;			// +008  0-10
	long	f00c_lineDist;				// +00c
	ULong	f010_controlFlags;			// +010
	long	f014_sureLevel;				// +014
	long	f018_defLineHeight;			// +018
	long	f01c;						// +01c
	ULong	f020_flags;					// +020  1 the end, 2 no new stroke, 0x10 a space gesture, 0x80 the stroke had no peak
	long	f024_numPoints;				// +024
	long	f028_x0;					// +028  the stroke's box
	long	f02c_x1;					// +02c
	long	f030_y0;					// +030
	long	f034_y1;					// +034
	long	f038_width;					// +038
	long	f03c_height;				// +03c
	long	f040_meanX;					// +040
	long	f044_meanY;					// +044
	long	f048_step;					// +048
	long	f04c_coreLeft;				// +04c
	long	f050_coreRight;				// +050
	long	f054_prevWidth;				// +054
	long	f058_prevHeight;			// +058
	long	f05c;						// +05c
	long	f060_wordDist;				// +060
	long	f064_letterWidth;			// +064
	long	f068_letterPitch;			// +068
	long	f06c_lineHeight;			// +06c
	long	f070_firstStroke;			// +070  the line's first stroke
	long	f074_firstWord;				// +074  the line's first word
	long	f078_lineLeft;				// +078
	long	f07c_lineRight;				// +07c
	long	f080_coreLeft;				// +080
	long	f084_coreRight;				// +084
	long	f088_peaks;					// +088
	long	f08c_strokeInLine;			// +08c
	long	f090;						// +090
	long	f094;						// +094
	long	f098;						// +098
	long	f09c_level;					// +09c
	long	f0a0;						// +0a0
	long	f0a4;						// +0a4
	long	f0a8_numGaps;				// +0a8
	long	f0ac_closing;				// +0ac  1 a new line, 2 the end
	long	f0b0_numWords;				// +0b0
	long	f0b4_numStrokes;			// +0b4
	long	f0b8_numLines;				// +0b8
	long	f0bc;						// +0bc
	long	f0c0;						// +0c0  the learnt letter width
	long	f0c4;						// +0c4  the learnt letter pitch
	long	f0c8;						// +0c8  the learnt level
	long	f0cc;						// +0cc  peaks on earlier lines
	long	f0d0;						// +0d0
	long	f0d4;						// +0d4
	long	f0d8_heightSum;				// +0d8
	long	f0dc_heightCount;			// +0dc
	long	f0e0_meanHeight;			// +0e0
	long	f0e4;						// +0e4
	long	f0e8;						// +0e8
	long	f0ec_slope;					// +0ec
	long	f0f0_slopeX;				// +0f0
	long	f0f4_slopeY;				// +0f4
	long	f0f8_histOrigin;			// +0f8  the x the stroke histogram starts at
	UByte	f0fc_line[0x7d8];			// +0fc  the line's histogram, a byte per four units (0x80: a core)
	UByte*	f8d4_stroke;				// +8d4  the stroke's own histogram
	short	f8d8_lineY[0x1f6];			// +8d8  the line's y, one per sixteen units
	ws_stroke_box	fcc4_boxes[0xfb];	// +cc4  the line's strokes
	UByte	f149c_segments[0xfb][4];	// +149c  the words found: first gap, last gap
	ws_gap_type*	f1888_gaps;			// +1888  (locked)
	Handle	f188c_gapsHandle;			// +188c
	long	f1890_smallGap;				// +1890
	long	f1894_bigGap;				// +1894
	long	f1898_pitch;				// +1898
	long	f189c_wordDist;				// +189c
	long	f18a0_mode;					// +18a0
	long	f18a4_net[7];				// +18a4  the line's inputs to the net
	long	f18c0;						// +18c0
	long	f18c4;						// +18c4
	long	f18c8;						// +18c8
};

// What survives from line to line (0x44 bytes in the ROM).
struct ws_learnt_type
{
	short	fLineHeight;
	short	fLetterWidth;
	short	fLetterPitch;
	short	fSlope;
	UByte	fLevel;
};

struct ws_memory_header_type
{
	Handle			fData;			// +00  the ws_data_type
	ws_data_type*	fDataPtr;		// +04  ... locked
	ws_learnt_type	fLearnt;		// +08  the mean of the last four lines
	ws_learnt_type	fHistory[4];	// +14  the last four lines
};

long	WordStrokes(PS_point_type* stroke, ws_control_type* control, ws_results_type* results);	// ROM 0x0026e8e8 WordStrokes__FP13PS_point_typeP15ws_control_typeP15ws_results_type - ==> 0, or 1 for a failure
long	InitWSData(ws_control_type* control, ws_memory_header_type** header);		// ROM 0x0026eb18 InitWSData__FP15ws_control_typePP21ws_memory_header_type
long	WS_HistTheStroke(ws_data_type* data);							// ROM 0x0026ed6c WS_HistTheStroke__FP12ws_data_type
long	WS_AddStrokeToHist(ws_data_type* data);							// ROM 0x0026f5c0 WS_AddStrokeToHist__FP12ws_data_type
long	WS_CalcGaps(ws_data_type* data);								// ROM 0x0026f758 WS_CalcGaps__FP12ws_data_type
long	WS_CountPiks(ws_data_type* data);								// ROM 0x0026f9c8 WS_CountPiks__FP12ws_data_type
long	WS_SetLineVars(ws_data_type* data);								// ROM 0x0026fb00 WS_SetLineVars__FP12ws_data_type
long	WS_CalcLineHeight(ws_data_type* data);							// ROM 0x0026fd3c WS_CalcLineHeight__FP12ws_data_type
long	WS_PostprocessGaps(ws_data_type* data);							// ROM 0x0026fde8 WS_PostprocessGaps__FP12ws_data_type
long	WS_SegmentWords(long left, ws_data_type* data);					// ROM 0x0026ff4c WS_SegmentWords__FiP12ws_data_type - ==> how many words
long	WS_GetWordDist(ws_data_type* data);								// ROM 0x00270154 WS_GetWordDist__FP12ws_data_type
long	WS_FlyLearn(ws_control_type* control, ws_memory_header_type* header, ws_data_type* data);	// ROM 0x00270520 WS_FlyLearn__FP15ws_control_typeP21ws_memory_header_typeP12ws_data_type
long	ReleaseWSData(ws_control_type* control, ws_memory_header_type** header);	// ROM 0x0027068c ReleaseWSData__FP15ws_control_typePP21ws_memory_header_type
long	UnlockWSData(ws_control_type* control, ws_memory_header_type** header);	// ROM 0x002707a0 UnlockWSData__FP15ws_control_typePP21ws_memory_header_type
void	InitForNewLine(ws_data_type* data);								// ROM 0x0027086c InitForNewLine__FP12ws_data_type
long	WordLineStrokes(ws_data_type* data, ws_results_type* results);	// ROM 0x00270ab0 WordLineStrokes__FP12ws_data_typeP15ws_results_type
long	WS_GetStrokeBoxAndSlope(ws_data_type* data);					// ROM 0x002715f4 WS_GetStrokeBoxAndSlope__FP12ws_data_type
long	WS_NewLine(ws_data_type* data);									// ROM 0x002718e4 WS_NewLine__FP12ws_data_type - ==> 1 for a new line
long	CheckForSpaceGesture(ws_data_type* data);						// ROM 0x00271bac CheckForSpaceGesture__FP12ws_data_type - ==> 0 for a space gesture
long	WS_WriteStrokeHorzValues(ws_data_type* data);					// ROM 0x00271c2c WS_WriteStrokeHorzValues__FP12ws_data_type

long	NeuroNetWS(long* inputs);										// ROM 0x001286b4 NeuroNetWS__FPi - ==> -100 (not a space) to 100 (a space)
long	Rget_answer(long* inputs, long* outputs);						// ROM 0x0012874c Rget_answer__FPiPl
long	EXP(long x);													// ROM 0x00128a44 EXP__Fl - e^x in 24.8, x from -5 to 0

#endif	/* __WORDSEGMENT_H */
