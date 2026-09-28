/*
	File:		XrPost.h

	Contains:	The cursive reader's post-processing: the answers' letters
				scored again by the letter table's rules, and the answers
				sorted by what that comes to (`EvaluateAndSortAnswers`).

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	Every letter of every answer the word graph holds is looked at again
	(`EvaluateCharQuality`): the prototype data (the PDF part of the letter
	table, `XrRules.cpp`) may have a rule for the letter's variant, and one
	for each letter it may stand next to, and a rule is a little program -
	a group of *queues*, each a string of bytecodes run by a stack machine
	(`CalculateQueueResult`) - whose answers, weighed and added, say how
	well the xrs the letter was read from bear it out.  A queue pushes
	constants, fields of the xrs the letter (or the one before or after
	it) was read from, and entries of the PDF's constant table, applies
	arithmetic and fuzzy comparisons to them (`CalculateLess` and its
	like answer 0 to 20, not a truth value), and calls the 74 functions
	of the `Functions` table - measures of the trace between points,
	boxes, bends, beaks, crossings, whether a letter competes with its
	neighbour for its xrs - each of which may fail with an error that
	makes the whole queue count nought.

	What the rules come to moves the letter's `f0b` byte in the graph,
	and with it the answers' weights; the side reasoning (a word that has
	a mark no letter of it accounts for, a letter with no mark where it
	needs one, the boxes of neighbouring letters), the diacritics'
	directions and the crosses no letter claimed move them further.

	DEVIATION: the ROM's `_POST_PARAMS` is 0x90 bytes; it holds pointers,
	so on the host it is `sizeof` and its fields are at the ROM's offsets
	in the comments only.  The rules' stack holds `long`s in the ROM and
	some of them are pointers to xrs, so on the host it holds `intptr_t`s
	(the arithmetic is done as the ROM's, 32 bits wide).

	Reconstructed from the MP2x00 US ROM (0x003359bc-0x0033c36c,
	0x0007c130-0x0007cc4c, 0x002af390-0x002af7ac, 0x00087f68,
	0x00305c88, 0x00307290-0x00307358, 0x003078b0-0x00307ad8,
	0x00308b08-0x00308de4); each function cites its origin.
*/

#ifndef __XRPOST_H
#define __XRPOST_H

#ifndef __XRREADER_H
#include "XrReader.h"
#endif

#include <stdint.h>

struct PDFHeader;
struct PS_point_type;
struct rc_type;
struct xrdata_type;
struct _RECT;

// What the post-processing works in (ROM _POST_PARAMS, 0x90 bytes).
struct POST_PARAMS
{
	PDFHeader*		pdf;			// +00  the prototype data (DTI +0xb8)
	rc_type*		rc;				// +04
	xrdata_type*	xr;				// +08
	PS_point_type*	trace;			// +0c  (rc +0xf8)
	short*			x;				// +10  the trace's points (EvaluateAnswers)
	short*			y;				// +14
	RWG_type*		rwg;			// +18
	RWS_type*		rws;			// +1c
	RWG_PPD_type*	ppd;			// +20
	short*			prevSyms;		// +24  the symbols of the letter before, -1 ending them
	short*			nextSyms;		// +28  and after
	intptr_t*		stack;			// +2c  the rules' stack
	long			isPrev;			// +30  the rule being run is the one with the letter before
	long			isNext;			// +34  ... after
	long			noPrev;			// +38  the letter is the first
	long			noNext;			// +3c  ... the last
	short			cur;			// +40  the symbol of the graph being looked at
	short			nPoints;		// +42  the trace's points (rc +0x96)
	short			stackBytes;		// +44  the stack's size in bytes (60)
	short			queues;			// +46  queues run
	intptr_t		vars[10];		// +48  the rules' variables (SetInternalVariable)
	long			flags;			// +70  1 the letter's own rules, 2 the neighbours', 4 the side reasoning, 8 the word's; 0xe for a one-answer graph
	UByte			changeLetter;	// +74  a letter a rule would change this one into (ChangePPDLetter)
	long			changePriority;	// +78  ... and how sure it is
	long			defineEnds;		// +7c  (0) the ends are the fields below, not noPrev/noNext
	long			firstChar;		// +80
	long			lastChar;		// +84
	long			competeOn;		// +88  (1) RetOneIfLettersCompeteForXRs may work
	UByte			curChar;		// +8c
	UByte			prevChar;		// +8d
	UByte			nextChar;		// +8e
};

// A function of the rules: three arguments off the stack (as many as it
// takes), where to put an error, and the parameters.
typedef intptr_t	(*PPFunction)(intptr_t a, intptr_t b, intptr_t c, int* err, POST_PARAMS* pp);

// A queue as CalculateFunction reads it (ROM QUEUE: +0 the bytes, +0xc
// where the current one is).
struct QUEUE
{
	const UByte*	bytes;			// +00
	long			f04;
	long			f08;
	long			index;			// +0c
};

// the interpreter (XrPostCalc.cpp)
long	FindXrIndex(RWG_PPD_type* ppd, short sym, long j);					// ROM 0x00336808 FindXrIndex__FPA13_15RWG_PPD_el_typesi - the xr the j'th prototype element of the symbol read (a skip or an xr read), -1 for none
intptr_t	CalculateQueueResult(POST_PARAMS* pp, const UByte* queue, UByte* error);	// ROM 0x003359bc CalculateQueueResult__FP12_POST_PARAMSPcPs - the bottom of the stack when it ends; the error (big-endian) 0 or the queue's failure
long	CalculateGroupResult(POST_PARAMS* pp, const UByte* rule, long floor);	// ROM 0x00336840 CalculateGroupResult__FP12_POST_PARAMSP15PDF_RULE_HEADERl
intptr_t	CalculateFunction(POST_PARAMS* pp, QUEUE* queue, intptr_t* stack, short* depth, int* err);	// ROM 0x00336b70 CalculateFunction__FP12_POST_PARAMSP5QUEUEPlPsPi
long	CalculatePow(long a, long b, int* err);							// ROM 0x00335f94 CalculatePow__FlT1Pi
long	CalculateChangeSign(long a, int* err);								// ROM 0x00335fd0 CalculateChangeSign__FlPi
long	CalculateLess(long a, long b, int* err);							// ROM 0x003375dc CalculateLess__FlT1Pi - 0..20, 20 for b well above a
long	CalculateGreater(long a, long b, int* err);						// ROM 0x00338d34 CalculateGreater__FlT1Pi
long	CalculateEquals(long a, long b, int* err);							// ROM 0x00338d44 CalculateEquals__FlT1Pi
intptr_t	CalculateAdd(intptr_t a, intptr_t b, int* err);				// ROM 0x00338d98 CalculateAdd__FlT1Pi
intptr_t	CalculateSubtract(intptr_t a, intptr_t b, int* err);			// ROM 0x00338da8 CalculateSubtract__FlT1Pi
long	CalculateMultiply(long a, long b, int* err);						// ROM 0x00338db8 CalculateMultiply__FlT1Pi
long	CalculateDivide(long a, long b, int* err);							// ROM 0x00338dc8 CalculateDivide__FlT1Pi - +-10000 for nought

// the functions of the table and what they need (XrPostCalc.cpp)
long	FindIRangeOfCorrXRs(RWG_PPD_type* ppd, xrdata_type* xr, short* syms, ULong withMarks, long* iMin, long* iMax);	// ROM 0x00339198 FindIRangeOfCorrXRs__FPA13_15RWG_PPD_el_typeP11xrdata_typePsUiPiT5 - ==> whether any
long	FindBeak(short* x, short* y, long i, long j, long* at);				// ROM 0x00339b88 FindBeak__FPsT1iT3Pi - ==> 0, 1 for none
long	IsAnyExtr(short* x, short* y, long i, long dist);					// ROM 0x0033a0f8 IsAnyExtr__FPsT1iT3 - 1 a top, 2 a bottom, 3 leftmost, 4 rightmost, 0 none
long	filtr_gid(long n, short* x, short* y, short* xOut, short* yOut, short* index, long* count, long cosLimit, long dist);	// ROM 0x0033a32c filtr_gid__FiPsN42PiN21 - ==> 0, 1 for no room
long	CheckChord(short* x, short* y, long i, long j, long cosLimit, long dist);	// ROM 0x0033a950 CheckChord__FPsT1iN33 - the point to keep between, -1 for none
void	UpdateBoxWithXr(xrd_el_type* el, xrd_el_type** last, _RECT* box, POST_PARAMS* pp, ULong track, long pass);	// ROM 0x0033ad08 UpdateBoxWithXr__FP11xrd_el_typePP11xrd_el_typeP5_RECTP12_POST_PARAMSUii
long	FindXrLetterBox(long which, POST_PARAMS* pp, _RECT* box, ULong track, int* err);	// ROM 0x0033ae54 FindXrLetterBox__FiP12_POST_PARAMSP5_RECTUiPi - ==> 1, 0 (and an error)
long	TooManyStrongElems(RWG_PPD_type* ppd, xrdata_type* xr, short* syms);	// ROM 0x0033b894 TooManyStrongElems__FPA13_15RWG_PPD_el_typeP11xrdata_typePs
long	GetXrCorr(xrd_el_type* xr, UByte sym, long var, long idx, DTIHeader* dti);	// ROM 0x0033bcc4 GetXrCorr__FP10xrinp_typeUciT3P14dti_descr_type - how well the xr matches the prototype element, -1 for none
long	PostCompareXrs(xrd_el_type* a, xrd_el_type* b);						// ROM 0x0033be50 PostCompareXrs__FP11xrd_el_typeT1 - 1 for different types
void	ResetChangePPDLetterInfo(POST_PARAMS* pp);							// ROM 0x0033c0d8 ResetChangePPDLetterInfo__FP12_POST_PARAMS
void	ApplyChangePPDLetterInfo(POST_PARAMS* pp);							// ROM 0x0033c100 ApplyChangePPDLetterInfo__FP12_POST_PARAMS
long	IsFirstCharAsDefined(POST_PARAMS* pp);								// ROM 0x0033c328 IsFirstCharAsDefined__FP12_POST_PARAMS
long	IsLastCharAsDefined(POST_PARAMS* pp);								// ROM 0x0033c33c IsLastCharAsDefined__FP12_POST_PARAMS
Boolean	X_IsStrongElem(xrd_el_type* el);									// ROM 0x00305c88 X_IsStrongElem__FP11xrd_el_type
long	iXmax_left(short* x, short* y, long i, long dx);					// ROM 0x00307290 iXmax_left__FPsT1iT3
long	iXmin_left(short* x, short* y, long i, long dx);					// ROM 0x003072f4 iXmin_left__FPsT1iT3
long	CurveHasSelfCrossing(short* x, short* y, long i, long j, long* pI, long* pJ, long minSquare);	// ROM 0x00308b08 CurveHasSelfCrossing__FPsT1iT3PiT5l
PPFunction	PPFunctionAt(long id, long* nargs);								// host: the Functions table's entry, nil for none

// the evaluation (XrPostEval.cpp)
void	EvaluateAndSortAnswers(rec_w_type* readings, rc_type* rc, xrdata_type* xr, RWG_type* rwg);	// ROM 0x00337ee8 EvaluateAndSortAnswers__FP10rec_w_typeP7rc_typeP11xrdata_typeP8RWG_type
long	EvaluateAnswers(POST_PARAMS* pp, rec_w_type* readings, const UByte* controls, ULong* multi);	// ROM 0x00337624 EvaluateAnswers__FP12_POST_PARAMSP10rec_w_typeP13POST_CONTROLSPUi - ==> 1 when anything was scored
long	EvaluateCharQuality(POST_PARAMS* pp);								// ROM 0x00335fe0 EvaluateCharQuality__FP12_POST_PARAMS
long	EvaluateWordUsingSideReasoning(const UByte* word, xrdata_type* xr);	// ROM 0x003368f4 EvaluateWordUsingSideReasoning__FPcP11xrdata_type
long	EvaluateLetterUsingSideReasoning(UByte c, POST_PARAMS* pp);		// ROM 0x00336934 EvaluateLetterUsingSideReasoning__FUcP12_POST_PARAMS
long	EvaluateMissingCross(POST_PARAMS* pp, long from, long count);		// ROM 0x003373c8 EvaluateMissingCross__FP12_POST_PARAMSiT2
long	CalculateBoxes_Side_Result(short c, short prev, POST_PARAMS* pp);	// ROM 0x0033b920 CalculateBoxes_Side_Result__FsT1P12_POST_PARAMS
long	CheckDiacriticsDirections(POST_PARAMS* pp, rec_w_type* reading, xrdata_type* xr, short which, short* result);	// NOT YET (0x0007c9a0): answers nought
long	CheckDigitsLine(rec_w_type* reading, xrdata_type* xr, short* result);	// ROM 0x002af648 CheckDigitsLine__FP10rec_w_typeP11xrdata_typePs

// tables (XrPostTables.cpp, generated)
extern const int			Functions[666];				// 74 entries of nine words: a name, the function, a range check (never used) and the arguments it takes
extern const int			RD_N_PP_FUNCTIONS[1];
extern const int			globalSizeArray[16];		// each bytecode's length
extern const char* const	kXrToLetters0[3];			// XR_TO_LETTERS: the xrs, the letters that account for them, more letters
extern const unsigned char	kXrToLetters0Tail[4];		// ... the penalty (big-endian) and the heights
extern const char* const	kXrToLetters1[3];
extern const unsigned char	kXrToLetters1Tail[4];
extern const char* const	kLettersToXr[3];			// LETTERS_TO_XR: the letters, the xrs they need, the xrs that make them unnecessary
extern const unsigned char	kLettersToXrTail[4];

#endif	/* __XRPOST_H */
